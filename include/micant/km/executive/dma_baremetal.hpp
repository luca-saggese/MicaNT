#pragma once

#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/arbiter.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/hal/amd64_firmware_interrupts.hpp"
#include <cstring>

namespace micant::io::dma {
// HAL/IOMMU installs this provider only after establishing an owned domain
// and cache-coherency policy for the claimed PCI function. No provider means
// DMA is unavailable; CPU physical addresses are never assumed to be IOVAs.
struct DomainProvider {
    bool (*map)(const void*, uint64_t, uint64_t, size_t, uint64_t, uint64_t&) noexcept{};
    bool (*unmap)(const void*, uint64_t, uint64_t, size_t) noexcept{};
};
inline DomainProvider provider{};
struct RamRange { uint64_t first{}, last{}; };
inline RamRange writeBackRam[1024]{};
inline size_t writeBackRamCount{};
inline bool registerWriteBackRam(uint64_t start, uint64_t size) noexcept {
    if (!size || start > UINT64_MAX - (size - 1) || writeBackRamCount == 1024) return false;
    writeBackRam[writeBackRamCount++] = {start, start + size - 1};
    return true;
}
inline bool isWriteBackRam(uint64_t start, size_t size) noexcept {
    if (!size || start > UINT64_MAX - (size - 1)) return false;
    for (size_t i = 0; i < writeBackRamCount; ++i)
        if (start >= writeBackRam[i].first && start + size - 1 <= writeBackRam[i].last) return true;
    return false;
}
inline bool mapDirect(const void*, uint64_t, uint64_t physical, size_t size,
                      uint64_t highest, uint64_t& deviceAddress) noexcept {
    if (!size || physical > highest ||
        size - 1 > highest - physical) return false;
    if (!hal::amd64::acquireBootstrapDirectDmaLease()) return false;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    deviceAddress = physical;
    return true;
}
inline bool unmapDirect(const void*, uint64_t, uint64_t, size_t) noexcept {
    if (!hal::amd64::bootstrapDirectDmaAvailable()) return false;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    hal::amd64::releaseBootstrapDirectDmaLease();
    return true;
}
inline bool initializeDirectProvider() noexcept {
    if (!hal::amd64::bootstrapDirectDmaAvailable()) return false;
    provider = {mapDirect, unmapDirect};
    return true;
}
struct Buffer {
    const void* owner{};
    uint64_t token{}, claim{}, pfn{}, deviceAddress{};
    size_t pages{};
    void* mapping{};
    bool domainMapped{};
};
inline Buffer buffers[64]{};
inline uint64_t nextToken{1};
// Single-BSP critical section. Domain hooks must be nonblocking and may not
// enable interrupts while the allocation transaction is in progress.
struct Guard {
    uint64_t flags{};
    Guard() noexcept { __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory"); }
    ~Guard() { if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory"); }
};

inline NtStatus allocate(const void* owner, uint64_t claim, size_t bytes, size_t alignment,
                         uint64_t highestAddress, void** virtualAddress,
                         uint64_t* deviceAddress, uint64_t* token) noexcept {
    if (!owner || !claim || !virtualAddress || !deviceAddress || !token || !bytes ||
        !alignment || (alignment & (alignment - 1)) || bytes > SIZE_MAX - 4095)
        return NtStatus::InvalidParameter;
    *virtualAddress = nullptr; *deviceAddress = 0; *token = 0;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceState;
    Guard guard;
    if (!provider.map || !provider.unmap) return NtStatus::NotSupported;
    Buffer* slot{};
    for (auto& buffer : buffers) if (!buffer.token) { slot = &buffer; break; }
    if (!slot || !nextToken) return NtStatus::InsufficientResources;
    if (!km::arb::retainDma(owner, claim)) return NtStatus::InvalidHandle;
    const size_t pages = (bytes + 4095) / 4096;
    const uint64_t alignmentPages = alignment <= 4096 ? 1 : alignment / 4096;
    const uint64_t highestPfn = provider.map == mapDirect ? (highestAddress >> 12) : UINT64_MAX;
    const uint64_t pfn = mm::PfnDatabase::get().allocateContiguousPages(
        pages, highestPfn, alignmentPages, isWriteBackRam);
    if (!pfn) { (void)km::arb::releaseDma(owner, claim); return NtStatus::InsufficientResources; }
    if (!isWriteBackRam(pfn << 12, pages * 4096)) {
        mm::PfnDatabase::get().freeContiguousPages(pfn, pages);
        (void)km::arb::releaseDma(owner, claim);
        return NtStatus::DeviceNotReady;
    }
    auto* mapping = hal::amd64::mapBootstrapDmaMemory(pfn << 12, pages * 4096);
    if (!mapping) {
        mm::PfnDatabase::get().freeContiguousPages(pfn, pages);
        (void)km::arb::releaseDma(owner, claim);
        return NtStatus::InsufficientResources;
    }
    std::memset(mapping, 0, pages * 4096);
    uint64_t iova{};
    if (!provider.map(owner, claim, pfn << 12, pages * 4096, highestAddress, iova)) {
        if (!hal::amd64::releaseBootstrapDeviceMemory(mapping, pages * 4096)) __builtin_trap();
        mm::PfnDatabase::get().freeContiguousPages(pfn, pages);
        (void)km::arb::releaseDma(owner, claim);
        return NtStatus::DeviceNotReady;
    }
    if ((iova & (alignment - 1)) || iova > highestAddress ||
        pages * 4096 - 1 > highestAddress - iova) {
        if (!provider.unmap(owner, claim, iova, pages * 4096)) __builtin_trap();
        if (!hal::amd64::releaseBootstrapDeviceMemory(mapping, pages * 4096)) __builtin_trap();
        mm::PfnDatabase::get().freeContiguousPages(pfn, pages);
        (void)km::arb::releaseDma(owner, claim);
        return NtStatus::DeviceNotReady;
    }
    *slot = {owner, nextToken++, claim, pfn, iova, pages, mapping, true};
    *virtualAddress = mapping; *deviceAddress = iova; *token = slot->token;
    return NtStatus::Success;
}

inline NtStatus release(const void* owner, uint64_t token, km::arb::ReleaseCheck stopped) noexcept {
    if (!owner || !token) return NtStatus::InvalidParameter;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceState;
    Guard guard;
    for (auto& buffer : buffers) if (buffer.owner == owner && buffer.token == token) {
        if (!km::arb::dmaStopped(owner, buffer.claim, stopped)) return NtStatus::DeviceBusy;
        if (buffer.domainMapped) {
            if (!provider.unmap || !provider.unmap(owner, buffer.claim, buffer.deviceAddress,
                                                    buffer.pages * 4096)) return NtStatus::DeviceBusy;
            buffer.domainMapped = false;
        }
        if (!hal::amd64::releaseBootstrapDeviceMemory(buffer.mapping, buffer.pages * 4096))
            return NtStatus::DeviceBusy;
        mm::PfnDatabase::get().freeContiguousPages(buffer.pfn, buffer.pages);
        if (!km::arb::releaseDma(owner, buffer.claim)) __builtin_trap();
        buffer = {};
        return NtStatus::Success;
    }
    return NtStatus::InvalidHandle;
}
}
