#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/hal/amd64_firmware_interrupts.hpp"
#include "micant/km/hal/amd64_irql.hpp"
#include "micant/km/hal/cpu.hpp"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"
#include "micant/shared/syscall_numbers.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/core/clock_timer.hpp"
#include "micant/km/core/passive_work.hpp"
#include <atomic>

extern "C" volatile bool g_ring3ProbeActive{};
extern "C" volatile bool g_ring3ClockProbeActive{};

namespace {

#if defined(__x86_64__) || defined(_M_X64)
std::atomic_flag g_pciConfigLock = ATOMIC_FLAG_INIT;

class PciConfigLockGuard {
public:
    PciConfigLockGuard() noexcept {
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(savedFlags_) : : "memory");
        while (g_pciConfigLock.test_and_set(std::memory_order_acquire))
            __asm__ __volatile__("pause" : : : "memory");
    }

    ~PciConfigLockGuard() noexcept {
        g_pciConfigLock.clear(std::memory_order_release);
        if (savedFlags_ & (uint64_t{1} << 9))
            __asm__ __volatile__("sti" : : : "memory");
    }

    PciConfigLockGuard(const PciConfigLockGuard&) = delete;
    PciConfigLockGuard& operator=(const PciConfigLockGuard&) = delete;

private:
    uint64_t savedFlags_{};
};

void writePciConfigAddress(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t registerOffset) noexcept {
    const uint32_t address = (uint32_t{1} << 31) |
        (static_cast<uint32_t>(bus) << 16) |
        (static_cast<uint32_t>(device) << 11) |
        (static_cast<uint32_t>(function) << 8) | registerOffset;
    __asm__ __volatile__("outl %0, %w1" : : "a"(address), "Nd"(uint16_t{0xCF8}) : "memory");
}

uint32_t readPciConfigData() noexcept {
    uint32_t value{};
    __asm__ __volatile__("inl %w1, %0" : "=a"(value) : "Nd"(uint16_t{0xCFC}) : "memory");
    return value;
}

void writePciConfigData(uint32_t value) noexcept {
    __asm__ __volatile__("outl %0, %w1" : : "a"(value), "Nd"(uint16_t{0xCFC}) : "memory");
}
#endif

bool validPciConfigAddress(uint16_t bus, uint16_t device, uint16_t function,
                           uint16_t registerOffset) noexcept {
    return bus <= 0xFF && device <= 31 && function <= 7 &&
           registerOffset <= 0xFC && (registerOffset & 3) == 0;
}

constexpr uint64_t kPresent = 1;
constexpr uint64_t kWritable = 2;
constexpr uint64_t kUser = 4;
constexpr uint64_t kNoExecute = uint64_t{1} << 63;
constexpr uint64_t kPhysicalMask = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t kOwnedTable = uint64_t{1} << 9;
constexpr uint64_t kGuardLeaf = uint64_t{1} << 10;
constexpr uint64_t kNoAccessLeaf = uint64_t{1} << 11;
constexpr uint64_t kUserBase = uint64_t{128} << 39;
constexpr uint64_t kUserTebBase = kUserBase + 0x8000;
constexpr uint64_t kDeviceBase = uint64_t{129} << 39;
constexpr uint64_t kDeviceMappingWindowSize = 16 * 1024 * 1024;
constexpr uint64_t kTestBase = uint64_t{130} << 39;
constexpr uint64_t kKuserKernelAlias = uint64_t{131} << 39;
constexpr uint64_t kKuserRootAlias = kKuserKernelAlias + 4096;
constexpr uint64_t kUserStackBase = kUserBase + 4096;
alignas(4096) uint64_t g_pml4[512]{};
uint64_t g_deviceNext = kDeviceBase;
struct BootstrapProtectedMmio { uint64_t first{}, last{}, owner{}; };
BootstrapProtectedMmio g_protectedIommuMmio[16]{};
size_t g_protectedIommuMmioCount{};
bool g_iommuProtectionCommitted{};
bool isProtectedIommuPhysicalPage(uint64_t physical) noexcept {
    for (size_t i = 0; i < g_protectedIommuMmioCount; ++i)
        if (physical <= g_protectedIommuMmio[i].last &&
            g_protectedIommuMmio[i].first <= physical + 4095) return true;
    return false;
}
bool g_addressSpaceReady{};
micant::hal::amd64::ProcessPagingRoot* g_processPagingRoots{};
bool g_ownedSharedRootSlots[2]{};
alignas(4096) uint8_t g_kuserSharedPage[4096]{};
bool g_kuserSharedMappingReady{};
bool g_kuserKernelAliasReady{};
uint64_t* g_kuserAliasPageTable{};
alignas(4096) uint8_t g_userCode[4096]{};
alignas(4096) uint8_t g_userStack[4 * 4096]{};
struct alignas(4096) BootstrapTebStorage {
    micant::um::Teb64 teb{};
    uint8_t padding[8192 - sizeof(micant::um::Teb64)]{};
};
static_assert(sizeof(BootstrapTebStorage) == 8192);
BootstrapTebStorage g_userTebStorage{};
alignas(4096) uint8_t g_nativeProbeImage[8192]{};
alignas(4096) uint8_t g_ntdllProbeImage[0x20000]{};
const uint8_t g_ntdllProbeFile[]{
#include "ntdll-probe.inc"
};
uint8_t g_nativeProbeFile[512 + 4096]{};
micant::um::NativeUserImageSet64 g_nativeProbeModules{};
alignas(16) uint8_t g_kernelStack[32768]{};
volatile bool g_probeSucceeded{};
uint32_t g_probeMode{};

uint64_t g_probeReturnRip{};
micant::hal::amd64::UserServiceRoutine g_dispatch{};
bool g_schedulerClockActive{};
bool g_clockIrqlMaskReady{};
uint32_t g_schedulerTimerPeriod{};
std::atomic<uint64_t> g_bootstrapApicClockTicks{};
uint64_t g_bootstrapApicClockTickOrigin{};
std::atomic<uint64_t> g_bootstrapApicClockLastCounter{};
std::atomic<uint8_t> g_bootstrapTscState{}; // 0=uninitialized, 1=calibrating, 2=ready
uint64_t g_bootstrapTscFrequency{};
std::atomic<uint64_t> g_bootstrapTscFrequencyHint{};
enum class BootstrapTscCalibrationSource : uint8_t { None, Cpuid15, PitChannel2, UefiStall };
std::atomic<BootstrapTscCalibrationSource> g_bootstrapTscCalibrationSource{
    BootstrapTscCalibrationSource::None};
std::atomic<uint8_t> g_bootstrapSystemTimeState{}; // 0=unset, 1=initializing, 2=ready
uint64_t g_bootstrapSystemTime100ns{};
uint64_t g_bootstrapSystemTimeCounter{};
uint64_t g_bootstrapSystemTimeFrequency{};
uint64_t g_bootstrapMonotonicCounter{};
uint64_t g_bootstrapMonotonicFrequency{};
std::atomic<uint8_t> g_bootstrapMonotonicState{}; // 0=unset, 2=ready
std::atomic_flag g_cmosRtcLock = ATOMIC_FLAG_INIT;
// The firmware selects the APIC mode before boot. Do not change modes while
// other processors or interrupt sources may be using the controller.
class BootstrapLocalApic {
public:
    struct Register {
        BootstrapLocalApic& apic;
        size_t index;
        operator uint32_t() const noexcept { return apic.read(index); }
        Register& operator=(uint32_t value) noexcept { apic.write(index, value); return *this; }
    };
    void initialize(volatile uint32_t* mapping, bool x2) noexcept { mmio_ = mapping; x2_ = x2; }
    [[nodiscard]] bool available() const noexcept { return x2_ || mmio_; }
    [[nodiscard]] bool readCurrentCount(uint32_t& value) const noexcept {
        if (!mmio_) return false;
        value = mmio_[0x390 / 4];
        return true;
    }
    [[nodiscard]] bool timerInterruptPending() const noexcept {
        if (!mmio_) return true;
        constexpr uint32_t vector = 0xd1;
        constexpr uint32_t irrRegister = 0x200 + (vector / 32) * 0x10;
        return (mmio_[irrRegister / 4] & (uint32_t{1} << (vector % 32))) != 0;
    }
    [[nodiscard]] Register operator[](size_t index) noexcept { return {*this, index}; }
    uint32_t read(size_t index) const noexcept {
        return x2_ ? static_cast<uint32_t>(micant::cpu::cpuRdmsr(0x800u + static_cast<uint32_t>(index / 4)))
                   : mmio_[index];
    }
    void write(size_t index, uint32_t value) const noexcept {
        __asm__ __volatile__("" : : : "memory");
        if (x2_) micant::cpu::cpuWrmsr(0x800u + static_cast<uint32_t>(index / 4), value);
        else mmio_[index] = value;
        __asm__ __volatile__("" : : : "memory");
    }
private:
    volatile uint32_t* mmio_{};
    bool x2_{};
};
BootstrapLocalApic g_timerApic{};

// Serialize the transition between CR8 and APIC TPR access during the
// diagnostic's save/restore; the runtime path updates priority through CR8.
void serializeApicPriorityAccess() noexcept {
    uint32_t eax = 0, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : : "memory");
}
micant::ke::KThread g_bootstrapIdleThread{};
alignas(16) uint8_t g_bootstrapIdleStack[32768]{};
alignas(16) uint8_t g_syscallStack[65536]{};
struct SyscallCpuState {
    uint64_t userRsp;
    uint64_t kernelRsp;
};
SyscallCpuState g_syscallCpu{};
using micant::hal::amd64::SyscallFrame;
void (*g_log)(const char*) noexcept{};

#pragma pack(push, 1)
struct InterruptGate {
    uint16_t offsetLow{};
    uint16_t selector{};
    uint8_t ist{};
    uint8_t access{};
    uint16_t offsetMiddle{};
    uint32_t offsetHigh{};
    uint32_t reserved{};
};
struct Idtr {
    uint16_t limit;
    uint64_t base;
};
#pragma pack(pop)

alignas(16) InterruptGate g_idt[256]{};
alignas(16) InterruptGate g_managedFaultIdt[256]{};
constexpr uint8_t kBootstrapMsiVector = 0xd2;
micant::hal::amd64::BootstrapMsiRoutine g_bootstrapMsiRoutine{};
void* g_bootstrapMsiContext{};
InterruptGate g_savedBootstrapMsiGate{};
uint64_t g_bootstrapMsiToken{};
uint64_t g_nextBootstrapMsiToken{1};
uintptr_t g_managedFaultAddress{};
uintptr_t g_managedFaultRip{};
uintptr_t g_managedFaultResume{};
uint64_t g_managedFaultError{};
volatile bool g_managedFaultArmed{};
volatile bool g_managedFaultObserved{};

uint64_t walk(uint64_t root, uint64_t address) noexcept {
    auto* table = reinterpret_cast<const uint64_t*>(root & kPhysicalMask);
    for (int shift = 39; shift >= 12; shift -= 9) {
        const uint64_t entry = table[(address >> shift) & 511];
        if ((entry & kPresent) == 0) {
            return 0;
        }
        if ((shift == 30 || shift == 21) && (entry & 128)) {
            const uint64_t offsetMask = (uint64_t{1} << shift) - 1;
            return (entry & kPhysicalMask & ~offsetMask) | (address & offsetMask);
        }
        if (shift == 12) {
            return (entry & kPhysicalMask) | (address & 4095);
        }
        table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
    }
    return 0;
}

bool queryPagingPermissions(uint64_t root, uint64_t address,
                           bool& userAccessible, bool& writable) noexcept {
    auto* table = reinterpret_cast<const uint64_t*>(root & kPhysicalMask);
    userAccessible = true;
    writable = true;
    for (int shift = 39; shift >= 12; shift -= 9) {
        const uint64_t entry = table[(address >> shift) & 511];
        if (!(entry & kPresent)) return false;
        userAccessible = userAccessible && (entry & kUser);
        writable = writable && (entry & kWritable);
        if ((shift == 30 || shift == 21) && (entry & 128)) return true;
        if (shift == 12) return true;
        table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
    }
    return false;
}

uint64_t* activePagingRoot() noexcept {
    uint64_t cr3{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(cr3));
    return reinterpret_cast<uint64_t*>(cr3 & kPhysicalMask);
}

bool mappedRange(uint64_t address, size_t size, bool user, bool writable,
                 bool executable) noexcept {
    if (!g_addressSpaceReady || !size || size - 1 > UINT64_MAX - address) {
        return false;
    }
    const uint64_t end = address + size - 1;
    const uint64_t region = address >> 47;
    if ((region != 0 && region != 0x1FFFF) || (end >> 47) != region) {
        return false;
    }
    for (uint64_t page = address & ~uint64_t{4095};; page += 4096) {
        const uint64_t* table = activePagingRoot();
        bool userAccessible = true;
        for (int shift = 39; shift >= 12; shift -= 9) {
            const uint64_t entry = table[(page >> shift) & 511];
            userAccessible = userAccessible && (entry & kUser);
            if (!(entry & kPresent) || (user && !userAccessible) ||
                (writable && !(entry & kWritable)) ||
                (executable && (entry & kNoExecute))) {
                return false;
            }
            if (shift > 12 && (entry & 128)) {
                if (user || (shift != 30 && shift != 21)) return false;
                break;
            }
            if (shift == 12) break;
            table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
        }
        if (userAccessible != user) return false;
        if (end - page < 4096) return true;
    }
}

bool bootstrapPagingContext() noexcept {
    // The bootstrap has only its BSP online; no remote TLB shootdown exists.
    uint64_t flags{}, cr3{};
    __asm__ __volatile__("pushfq; popq %0; movq %%cr3, %1"
                         : "=r"(flags), "=r"(cr3) : : "memory");
    if (!g_addressSpaceReady || (flags & (uint64_t{1} << 9)) ||
        micant::mm::PfnDatabase::get().getTotalPages() == 0) return false;
    if ((cr3 & kPhysicalMask) == reinterpret_cast<uint64_t>(g_pml4)) return true;
    for (const auto* root = g_processPagingRoots; root; root = root->next)
        if ((root->pfn << micant::mm::PageShift4KB) == (cr3 & kPhysicalMask)) return true;
    return false;
}

class BootstrapWriteProtectWindow {
public:
    BootstrapWriteProtectWindow() noexcept {
        // This bootstrap-only edit runs on the BSP with interrupts disabled;
        // there are no remote CPUs or pageable callbacks in the window.
        if (micant::ke::g_CurrentCpu.processorNumber != 0 ||
            !bootstrapPagingContext()) return;
        __asm__ __volatile__("mov %%cr0, %0" : "=r"(original_) : : "memory");
        if (!(original_ & (uint64_t{1} << 16))) {
            valid_ = true;
            return;
        }
        const uint64_t writable = original_ & ~(uint64_t{1} << 16);
        __asm__ __volatile__("mov %0, %%cr0" : : "r"(writable) : "memory");
        valid_ = true;
        changed_ = true;
    }
    BootstrapWriteProtectWindow(const BootstrapWriteProtectWindow&) = delete;
    BootstrapWriteProtectWindow& operator=(const BootstrapWriteProtectWindow&) = delete;
    ~BootstrapWriteProtectWindow() noexcept { restore(); }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    void restore() noexcept {
        if (!valid_) return;
        if (changed_)
            __asm__ __volatile__("mov %0, %%cr0" : : "r"(original_) : "memory");
        valid_ = false;
    }

private:
    uint64_t original_{};
    bool valid_{};
    bool changed_{};
};

bool pagingContextMatches(void* context) noexcept {
    if (!bootstrapPagingContext()) return false;
    const auto active = reinterpret_cast<uint64_t>(activePagingRoot());
    if (!context) return active == reinterpret_cast<uint64_t>(g_pml4);
    for (const auto* root = g_processPagingRoots; root; root = root->next)
        if (root == context) return (root->pfn << micant::mm::PageShift4KB) == active;
    return false;
}

void invalidatePage(uint64_t address) noexcept {
    __asm__ __volatile__("invlpg (%0)" : : "r"(address) : "memory");
}

bool ownedAddress(uint64_t address, bool user) noexcept {
    const uint64_t slot = address >> 39;
    return user ? slot == 128 : slot == 129 || slot == 130 || slot == 131;
}

bool emptyTable(const uint64_t* table) noexcept {
    for (size_t i = 0; i < 512; ++i) {
        if (table[i] != 0) return false;
    }
    return true;
}

bool mapPage(uint64_t address, uint64_t physical, uint64_t flags,
             int failAfter = -1) noexcept {
    if (!bootstrapPagingContext() || (address & 4095) || (physical & 4095) ||
        physical > kPhysicalMask || !ownedAddress(address, (flags & kUser) != 0)) {
        return false;
    }
    if (isProtectedIommuPhysicalPage(physical)) return false;
    auto& database = micant::mm::PfnDatabase::get();
    uint64_t* createdEntries[3]{};
    uint64_t createdPfns[3]{};
    size_t createdCount{};
    uint64_t* table = activePagingRoot();
    for (int shift = 39; shift > 12; shift -= 9) {
        uint64_t& entry = table[(address >> shift) & 511];
        if (entry == 0) {
            const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
            if (!pfn) goto rollback;
            if (failAfter > 0) --failAfter;
            const uint64_t page = pfn << 12;
            if (page > kPhysicalMask || !mappedRange(page, 4096, false, true, false) ||
                walk(reinterpret_cast<uint64_t>(activePagingRoot()), page) != page) {
                database.freePage(pfn);
                goto rollback;
            }
            database.zeroPage(pfn);
            entry = page | kPresent | kWritable | kOwnedTable | (flags & kUser);
            createdEntries[createdCount] = &entry;
            createdPfns[createdCount++] = pfn;
        }
        if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) ||
            !(entry & kWritable) || ((entry & kUser) != (flags & kUser)) ||
            (entry & kNoExecute)) {
            goto rollback;
        }
        table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
    }
    {
        uint64_t& leaf = table[(address >> 12) & 511];
        if (leaf != 0) goto rollback;
        const auto pte = micant::mm::PageTableEntry::create(
            (physical & kPhysicalMask) >> 12, (flags & kWritable) != 0,
            (flags & kUser) != 0, (flags & kNoExecute) != 0);
        leaf = pte.value | (flags & ~(kPresent | kWritable | kUser | kNoExecute));
        invalidatePage(address);
        return true;
    }
rollback:
    while (createdCount) {
        --createdCount;
        *createdEntries[createdCount] = 0;
        invalidatePage(address);
        database.freePage(createdPfns[createdCount]);
    }
    return false;
}

class KuserPageTableEdit {
public:
    ~KuserPageTableEdit() noexcept {
        if (!committed_) {
            while (editCount_) {
                --editCount_;
                *entries_[editCount_] = original_[editCount_];
            }
        }
        clearTableAliases();
        if (!committed_)
            for (size_t index = 0; index < allocatedCount_; ++index)
                micant::mm::PfnDatabase::get().freePage(allocatedPfns_[index]);
    }

    bool set(uint64_t& entry, uint64_t value) noexcept {
        if (entry == value) return true;
        if (editCount_ == sizeof(entries_) / sizeof(entries_[0])) return false;
        entries_[editCount_] = &entry;
        original_[editCount_++] = entry;
        entry = value;
        return true;
    }

    uint64_t allocateTable() noexcept {
        if (allocatedCount_ == sizeof(allocatedPfns_) / sizeof(allocatedPfns_[0])) return 0;
        auto& database = micant::mm::PfnDatabase::get();
        const uint64_t pfn = database.allocatePage(false);
        if (!pfn || pfn > (kPhysicalMask >> 12)) {
            if (pfn) database.freePage(pfn);
            return 0;
        }
        const uint64_t physical = pfn << 12;
        database.zeroPage(pfn);
        allocatedPfns_[allocatedCount_++] = pfn;
        return physical;
    }

    uint64_t* mapTable(uint64_t physical) noexcept {
        if (!g_kuserAliasPageTable || !physical || (physical & 4095) ||
            physical > kPhysicalMask ||
            aliasCount_ == sizeof(aliasSlots_) / sizeof(aliasSlots_[0])) return nullptr;
        const uint16_t slot = static_cast<uint16_t>(3 + aliasCount_);
        const uint64_t address = kKuserKernelAlias + uint64_t{slot} * 4096;
        if (g_kuserAliasPageTable[slot] != 0) return nullptr;
        g_kuserAliasPageTable[slot] = physical | kPresent | kWritable | kNoExecute;
        invalidatePage(address);
        if (!mappedRange(address, 4096, false, true, false) ||
            walk(reinterpret_cast<uint64_t>(activePagingRoot()), address) != physical) {
            g_kuserAliasPageTable[slot] = 0;
            invalidatePage(address);
            return nullptr;
        }
        aliasSlots_[aliasCount_++] = slot;
        return reinterpret_cast<uint64_t*>(address);
    }

    void commit() noexcept { committed_ = true; }

private:
    void clearTableAliases() noexcept {
        if (!g_kuserAliasPageTable) return;
        while (aliasCount_) {
            const uint16_t slot = aliasSlots_[--aliasCount_];
            g_kuserAliasPageTable[slot] = 0;
            invalidatePage(kKuserKernelAlias + uint64_t{slot} * 4096);
        }
    }

    uint64_t* entries_[8]{};
    uint64_t original_[8]{};
    uint64_t allocatedPfns_[4]{};
    uint16_t aliasSlots_[8]{};
    size_t editCount_{};
    size_t allocatedCount_{};
    size_t aliasCount_{};
    bool committed_{};
};

bool splitOneGiBMapping(KuserPageTableEdit& edit, uint64_t& entry) noexcept {
    constexpr uint64_t physicalMask1GiB = 0x000fffffc0000000ULL;
    const uint64_t old = entry;
    if (!(old & kPresent) || !(old & 128)) return false;
    const uint64_t tablePhysical = edit.allocateTable();
    if (!tablePhysical) return false;
    const uint64_t physicalBase = old & physicalMask1GiB;
    const uint64_t flags = old & (kPresent | kWritable | kUser | 0x18ULL |
        (uint64_t{1} << 5) | (uint64_t{1} << 6) | 128ULL | 0x1800ULL | kNoExecute);
    auto* table = edit.mapTable(tablePhysical);
    if (!table) return false;
    for (size_t index = 0; index < 512; ++index)
        table[index] = physicalBase + index * 0x200000ULL | flags;
    return edit.set(entry, tablePhysical | kPresent | kWritable | kUser | kOwnedTable);
}

bool splitTwoMiBMapping(KuserPageTableEdit& edit, uint64_t& entry) noexcept {
    constexpr uint64_t physicalMask2MiB = 0x000fffffffe00000ULL;
    const uint64_t old = entry;
    if (!(old & kPresent) || !(old & 128)) return false;
    const uint64_t tablePhysical = edit.allocateTable();
    if (!tablePhysical) return false;
    const uint64_t physicalBase = old & physicalMask2MiB;
    uint64_t flags = old & (kPresent | kWritable | kUser | 0x18ULL |
        (uint64_t{1} << 5) | (uint64_t{1} << 6) | (uint64_t{1} << 8) |
        0x0e00ULL | kNoExecute);
    flags &= ~uint64_t{128}; // PS becomes PAT only when the old large-page PAT was set.
    if (old & (uint64_t{1} << 12)) flags |= uint64_t{128};
    flags &= ~(uint64_t{1} << 9); // reserve software bit 9 for owned table links.
    auto* table = edit.mapTable(tablePhysical);
    if (!table) return false;
    for (size_t index = 0; index < 512; ++index)
        table[index] = physicalBase + index * 4096ULL | flags;
    return edit.set(entry, tablePhysical | kPresent | kWritable | kUser | kOwnedTable);
}

bool installKuserUserMapping(uint64_t physical, uint64_t* writableRoot) noexcept {
    const auto fail = [](const char* message) noexcept {
        if (g_log) g_log(message);
        return false;
    };
    constexpr uint64_t address = micant::UserSharedDataAddress;
    if (!bootstrapPagingContext() || (physical & 4095) ||
        physical > kPhysicalMask || !physical || isProtectedIommuPhysicalPage(physical) ||
        reinterpret_cast<uint64_t>(activePagingRoot()) != reinterpret_cast<uint64_t>(g_pml4) ||
        !writableRoot)
        return fail("[MicaNT Hal] KUSER map precondition failed\r\n");

    KuserPageTableEdit edit;
    uint64_t& pml4Entry = writableRoot[(address >> 39) & 511];
    if (pml4Entry & 128) return fail("[MicaNT Hal] KUSER PML4 large-page conflict\r\n");
    uint64_t* pdpt{};
    if (pml4Entry & kPresent) {
        pdpt = edit.mapTable(pml4Entry & kPhysicalMask);
        if (!pdpt) return fail("[MicaNT Hal] Existing KUSER PDPT alias failed\r\n");
        if (!edit.set(pml4Entry, pml4Entry | kUser | kWritable))
            return fail("[MicaNT Hal] KUSER PML4 permission edit failed\r\n");
    } else {
        const uint64_t table = edit.allocateTable();
        if (!table || !(pdpt = edit.mapTable(table)) || !edit.set(pml4Entry,
                table | kPresent | kWritable | kUser | kOwnedTable))
            return fail("[MicaNT Hal] KUSER PML4 allocate/edit failed\r\n");
    }
    uint64_t& pdptEntry = pdpt[(address >> 30) & 511];
    uint64_t* pd{};
    if (!(pdptEntry & kPresent)) {
        const uint64_t table = edit.allocateTable();
        if (!table || !(pd = edit.mapTable(table)) || !edit.set(pdptEntry,
                table | kPresent | kWritable | kUser | kOwnedTable))
            return fail("[MicaNT Hal] KUSER PDPT allocate/edit failed\r\n");
    } else if (pdptEntry & 128) {
        if (!splitOneGiBMapping(edit, pdptEntry))
            return fail("[MicaNT Hal] KUSER split 1GiB mapping failed\r\n");
        pd = edit.mapTable(pdptEntry & kPhysicalMask);
        if (!pd) return fail("[MicaNT Hal] KUSER split PD alias failed\r\n");
    } else {
        pd = edit.mapTable(pdptEntry & kPhysicalMask);
        if (!pd || !edit.set(pdptEntry, pdptEntry | kUser | kWritable))
            return fail("[MicaNT Hal] KUSER PDPT permission edit failed\r\n");
    }

    uint64_t& pdEntry = pd[(address >> 21) & 511];
    uint64_t* pt{};
    if (!(pdEntry & kPresent)) {
        const uint64_t table = edit.allocateTable();
        if (!table || !(pt = edit.mapTable(table)) || !edit.set(pdEntry,
                table | kPresent | kWritable | kUser | kOwnedTable))
            return fail("[MicaNT Hal] KUSER PD allocate/edit failed\r\n");
    } else if (pdEntry & 128) {
        if (!splitTwoMiBMapping(edit, pdEntry))
            return fail("[MicaNT Hal] KUSER split 2MiB mapping failed\r\n");
        pt = edit.mapTable(pdEntry & kPhysicalMask);
        if (!pt) return fail("[MicaNT Hal] KUSER split PT alias failed\r\n");
    } else {
        pt = edit.mapTable(pdEntry & kPhysicalMask);
        if (!pt || !edit.set(pdEntry, pdEntry | kUser | kWritable))
            return fail("[MicaNT Hal] KUSER PD permission edit failed\r\n");
    }

    uint64_t& leaf = pt[(address >> 12) & 511];
    const uint64_t userReadOnlyNoExecute = physical | kPresent | kUser | kNoExecute;
    if (!edit.set(leaf, userReadOnlyNoExecute))
        return fail("[MicaNT Hal] KUSER leaf edit failed\r\n");
    edit.commit();
    invalidatePage(address);
    uint64_t root = reinterpret_cast<uint64_t>(g_pml4);
    __asm__ __volatile__("movq %0, %%cr3" : : "r"(root) : "memory");
    return true;
}

bool installKuserKernelAliases(uint64_t backingPhysical, uint64_t rootPhysical) noexcept {
    if (!bootstrapPagingContext() || g_pml4[(kKuserKernelAlias >> 39) & 511] != 0 ||
        !backingPhysical || (backingPhysical & 4095) || backingPhysical > kPhysicalMask ||
        !rootPhysical || (rootPhysical & 4095) || rootPhysical > kPhysicalMask)
        return false;

    auto& database = micant::mm::PfnDatabase::get();
    uint64_t pfns[3]{};
    for (size_t index = 0; index < 3; ++index) {
        pfns[index] = database.allocatePage(false);
        if (!pfns[index] || pfns[index] > (kPhysicalMask >> 12)) break;
        const uint64_t page = pfns[index] << 12;
        if (!mappedRange(page, 4096, false, true, false) ||
            walk(reinterpret_cast<uint64_t>(activePagingRoot()), page) != page)
            break;
        database.zeroPage(pfns[index]);
    }
    if (!pfns[0] || !pfns[1] || !pfns[2] ||
        pfns[0] > (kPhysicalMask >> 12) || pfns[1] > (kPhysicalMask >> 12) ||
        pfns[2] > (kPhysicalMask >> 12)) {
        for (uint64_t pfn : pfns) if (pfn && pfn <= (kPhysicalMask >> 12))
            database.freePage(pfn);
        return false;
    }

    const uint64_t pdptPhysical = pfns[0] << 12;
    const uint64_t pdPhysical = pfns[1] << 12;
    const uint64_t ptPhysical = pfns[2] << 12;
    auto* pdpt = reinterpret_cast<uint64_t*>(pdptPhysical);
    auto* pd = reinterpret_cast<uint64_t*>(pdPhysical);
    auto* pt = reinterpret_cast<uint64_t*>(ptPhysical);
    if (!mappedRange(pdptPhysical, 4096, false, true, false) ||
        !mappedRange(pdPhysical, 4096, false, true, false) ||
        !mappedRange(ptPhysical, 4096, false, true, false)) {
        for (uint64_t pfn : pfns) database.freePage(pfn);
        return false;
    }
    pdpt[0] = pdPhysical | kPresent | kWritable | kOwnedTable;
    pd[0] = ptPhysical | kPresent | kWritable | kOwnedTable;
    pt[(kKuserKernelAlias >> 12) & 511] =
        backingPhysical | kPresent | kWritable | kNoExecute;
    pt[(kKuserRootAlias >> 12) & 511] =
        rootPhysical | kPresent | kWritable | kNoExecute;
    const uint64_t aliasPageTableVa = kKuserKernelAlias + 2 * 4096;
    pt[(aliasPageTableVa >> 12) & 511] =
        ptPhysical | kPresent | kWritable | kNoExecute;

    // The hierarchy is fully allocated, zeroed and validated before CR0.WP is
    // changed. The protected window performs exactly one root entry store.
    BootstrapWriteProtectWindow writeWindow;
    if (!writeWindow.valid()) {
        for (uint64_t pfn : pfns) database.freePage(pfn);
        return false;
    }
    g_pml4[(kKuserKernelAlias >> 39) & 511] =
        pdptPhysical | kPresent | kWritable | kOwnedTable;
    writeWindow.restore();
    invalidatePage(kKuserKernelAlias);
    invalidatePage(kKuserRootAlias);
    invalidatePage(aliasPageTableVa);
    g_kuserAliasPageTable = reinterpret_cast<uint64_t*>(aliasPageTableVa);
    return walk(reinterpret_cast<uint64_t>(activePagingRoot()), kKuserKernelAlias) ==
            backingPhysical &&
        walk(reinterpret_cast<uint64_t>(activePagingRoot()), kKuserRootAlias) == rootPhysical &&
        walk(reinterpret_cast<uint64_t>(activePagingRoot()), aliasPageTableVa) == ptPhysical &&
        mappedRange(kKuserKernelAlias, 4096, false, true, false) &&
        mappedRange(kKuserRootAlias, 4096, false, true, false) &&
        mappedRange(aliasPageTableVa, 4096, false, true, false);
}

bool unmapPage(uint64_t address, bool user) noexcept {
    if (!bootstrapPagingContext() || (address & 4095) || !ownedAddress(address, user)) {
        return false;
    }
    uint64_t* parents[3]{};
    uint64_t* tables[3]{};
    uint64_t* table = activePagingRoot();
    size_t level{};
    for (int shift = 39; shift > 12; shift -= 9) {
        uint64_t* entry = &table[(address >> shift) & 511];
        if (!(*entry & kPresent) || !(*entry & kOwnedTable) || (*entry & 128) ||
            ((*entry & kUser) != (user ? kUser : 0))) return false;
        parents[level] = entry;
        table = reinterpret_cast<uint64_t*>(*entry & kPhysicalMask);
        tables[level++] = table;
    }
    uint64_t& leaf = table[(address >> 12) & 511];
    if (!(leaf & (kPresent | kGuardLeaf | kNoAccessLeaf))) return false;
    leaf = 0;
    invalidatePage(address);
    auto& database = micant::mm::PfnDatabase::get();
    while (level && emptyTable(tables[level - 1])) {
        // Process roots share these supervisor branches. Keep their top-level
        // tables pinned while any process root can still reference them.
        const uint64_t slot = address >> 39;
        if (level == 1 && g_processPagingRoots && !user &&
            (slot == 129 || slot == 130 || slot == 131)) break;
        --level;
        const uint64_t pfn = (*parents[level] & kPhysicalMask) >> 12;
        *parents[level] = 0;
        invalidatePage(address);
        database.freePage(pfn);
    }
    return true;
}

bool scaleTscDelta100ns(uint64_t delta, uint64_t frequency,
                        uint64_t& ticks) noexcept {
    if (!frequency) return false;
    const uint64_t whole = delta / frequency;
    const uint64_t remainder = delta % frequency;
    if (whole > UINT64_MAX / micant::shared::time::TicksPerSecond ||
        remainder > UINT64_MAX / micant::shared::time::TicksPerSecond)
        return false;
    const uint64_t wholeTicks = whole * micant::shared::time::TicksPerSecond;
    const uint64_t fractionalTicks =
        (remainder * micant::shared::time::TicksPerSecond) / frequency;
    if (fractionalTicks > UINT64_MAX - wholeTicks) return false;
    ticks = wholeTicks + fractionalTicks;
    return true;
}

uint8_t readCmosRegister(uint8_t reg, uint8_t nmiMask) noexcept {
    const uint8_t index = static_cast<uint8_t>((reg & 0x7f) | nmiMask);
    uint8_t value{};
    __asm__ __volatile__("outb %0, $0x70; inb $0x71, %0"
                         : "=a"(value) : "0"(index) : "memory");
    return value;
}

bool waitForRtcUpdateClear(uint8_t nmiMask) noexcept {
    // CMOS status-A UIP is the only hardware timeout source before the TSC
    // calibration has completed. The iteration cap keeps a missing/broken
    // RTC from hanging boot indefinitely.
    for (uint32_t attempt = 0; attempt < 1'000'000; ++attempt) {
        if ((readCmosRegister(0x0A, nmiMask) & 0x80) == 0) return true;
        __asm__ __volatile__("pause" : : : "memory");
    }
    return false;
}

struct CmosSnapshot {
    uint8_t second{}, minute{}, hour{}, day{}, month{}, year{}, century{}, statusB{}, statusD{};
};

bool readCmosSnapshot(uint8_t nmiMask, CmosSnapshot& snapshot) noexcept {
    if (!waitForRtcUpdateClear(nmiMask)) return false;
    snapshot.second = readCmosRegister(0x00, nmiMask);
    snapshot.minute = readCmosRegister(0x02, nmiMask);
    snapshot.hour = readCmosRegister(0x04, nmiMask);
    snapshot.day = readCmosRegister(0x07, nmiMask);
    snapshot.month = readCmosRegister(0x08, nmiMask);
    snapshot.year = readCmosRegister(0x09, nmiMask);
    snapshot.century = readCmosRegister(0x32, nmiMask);
    snapshot.statusB = readCmosRegister(0x0B, nmiMask);
    snapshot.statusD = readCmosRegister(0x0D, nmiMask);
    return (snapshot.statusD & 0x80) != 0;
}

bool sameCmosTime(const CmosSnapshot& left, const CmosSnapshot& right) noexcept {
    return left.second == right.second && left.minute == right.minute &&
        left.hour == right.hour && left.day == right.day && left.month == right.month &&
        left.year == right.year && left.century == right.century &&
        left.statusB == right.statusB && left.statusD == right.statusD;
}

bool decodeCmosValue(uint8_t raw, bool binary, uint8_t& value) noexcept {
    if (binary) { value = raw; return true; }
    const uint8_t low = raw & 0x0f;
    const uint8_t high = raw >> 4;
    if (low > 9 || high > 9) return false;
    value = static_cast<uint8_t>(high * 10 + low);
    return true;
}

[[noreturn]] void stopOnTrap() noexcept {
    if (g_log) {
        g_log("[MicaNT Hal] FATAL: unexpected bootstrap trap\r\n");
    }
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

void logTrapValue(const char* label, uint64_t value) noexcept {
    if (!g_log) return;
    constexpr char digits[] = "0123456789ABCDEF";
    char message[64]{};
    size_t index = 0;
    while (label[index] && index < sizeof(message) - 19) {
        message[index] = label[index];
        ++index;
    }
    for (int shift = 60; shift >= 0; shift -= 4) {
        message[index++] = digits[(value >> shift) & 0xF];
    }
    message[index++] = '\r';
    message[index++] = '\n';
    g_log(message);
}

} // namespace

extern "C" {
extern uint32_t MicantAmd64XsaveMask;
extern uint32_t MicantAmd64ExtendedSize;
// Private Ps hook; it must not enable interrupts, wait or switch threads:
// the bootstrap page-fault gate currently uses a shared IST stack.
uint32_t MicantPsResolveUserStackFault(uintptr_t address, uint64_t errorCode, uint64_t savedCs) noexcept;
uint32_t MicantPsResolveUserCopyOnWrite(uintptr_t address) noexcept;
bool MicantPsPrepareUserException(uint32_t exceptionCode, uint32_t exceptionVector,
                                  uintptr_t address,
                                  uint64_t* hardwareFrame, uint64_t* savedRegisters,
                                  const void* floatingState) noexcept;
bool MicantPsPrepareUserApc(uint64_t* hardwareFrame, uint64_t* savedRegisters,
                            const void* floatingState) noexcept;
bool MicantPsTerminateFailedUserException(uint32_t exitStatus) noexcept;
uintptr_t MicantBootstrapInterruptStackFloor{};
uintptr_t MicantBootstrapExecutionStackTop{};
uintptr_t MicantBootstrapUserInterruptStackFloor{};
uintptr_t MicantBootstrapUserInterruptStackBase{};
uintptr_t MicantBootstrapUserInterruptStackTop{};
uintptr_t MicantBootstrapSyscallStackBase{};
uintptr_t MicantBootstrapSyscallStackTop{};
uintptr_t MicantBootstrapSyscallStackFloor{};
volatile uint64_t MicantTimerTicks{};
volatile bool MicantTimerPreemptionEnabled{};
volatile bool MicantTimerDispatchFailed{};
micant::ke::KThread* MicantTimerBootThread{};
bool MicantTimerBootQueued{};
uint64_t MicantTimerBootDeadline{24};

uint64_t MicantBootstrapEnterInterrupt(uint64_t savedCs) noexcept {
    auto* thread = micant::ke::KeGetCurrentThread();
    if (!thread) return 0;
    const bool previous = thread->previousUserMode;
    if ((savedCs & 3) == 3) thread->previousUserMode = true;
    return previous ? 1 : 0;
}

void MicantBootstrapLeaveInterrupt(uint64_t previous) noexcept {
    if (auto* thread = micant::ke::KeGetCurrentThread())
        thread->previousUserMode = previous != 0;
}

void MicantBootstrapInterruptExit(uint64_t* hardwareFrame, uint64_t* savedRegisters,
                                  const void* floatingState) noexcept {
    if (!hardwareFrame || !savedRegisters || !floatingState) return;
    const uint64_t savedFlags = hardwareFrame[2];
    const uint64_t savedCs = hardwareFrame[1];
    if (savedFlags & (uint64_t{1} << 9)) {
        micant::ke::detail::servicePendingDpcs();
        micant::ke::detail::servicePendingKernelApcs(true);
        if ((savedCs & 3) == 3) {
            micant::ke::detail::serviceUserReturnTermination();
            if (micant::ke::KeGetCurrentIrql() == micant::ke::PASSIVE_LEVEL)
                (void)MicantPsPrepareUserApc(hardwareFrame, savedRegisters, floatingState);
        }
    }
}

void MicantBootstrapTimerDispatch() noexcept {
    MicantTimerTicks = MicantTimerTicks + 1;
    // This clock-source count is monotonic across scheduler/test counters,
    // which intentionally reset while validating interrupt behavior.
    g_bootstrapApicClockTicks.fetch_add(1, std::memory_order_release);
    g_timerApic.write(0xb0 / 4, 0);
    if (g_kuserSharedMappingReady &&
        micant::hal::amd64::updateBootstrapKuserSharedData() !=
            micant::hal::amd64::BootstrapClockStatus::Success)
        MicantTimerDispatchFailed = true;
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    micant::ke::clock_timer::onClockInterrupt();
#endif
    if (!MicantTimerPreemptionEnabled) return;
    using namespace micant::ke;
    const KIRQL previous = KeGetCurrentIrql();
    if (previous >= DISPATCH_LEVEL) return;
    KfRaiseIrql(DISPATCH_LEVEL);
    auto* current = KeGetCurrentThread();
    auto& scheduler = PriorityScheduler::get();
    const uintptr_t expectedSyscallStack = current && current->kernelStackTop ? current->kernelStackTop :
        reinterpret_cast<uintptr_t>(g_syscallStack + sizeof(g_syscallStack));
    const uintptr_t expectedInterruptStack = current && current->kernelStackTop ? current->kernelStackTop :
        reinterpret_cast<uintptr_t>(g_kernelStack + sizeof(g_kernelStack));
    if (!current || g_syscallCpu.kernelRsp != expectedSyscallStack ||
        micant::cpu::CpuHardwareEngine::get().getTss().rsp0 != expectedInterruptStack)
        MicantTimerDispatchFailed = true;
    if (MicantTimerBootThread && MicantTimerTicks >= MicantTimerBootDeadline && !MicantTimerBootQueued) {
        if (!scheduler.readyKThread(MicantTimerBootThread)) MicantTimerDispatchFailed = true;
        else MicantTimerBootQueued = true;
    }
    auto* next = scheduler.selectOnClockTick(current);
    if (next && !KiSwapContext(current, next, current != g_CurrentCpu.idleThread)) {
        if (!scheduler.cancelQuantumPreemption(current, next)) __builtin_trap();
        MicantTimerDispatchFailed = true;
    }
    KeLowerIrql(previous);
}

void MicantBootstrapMsiDispatch() noexcept {
    using namespace micant::ke;
    const auto previous = KfRaiseIrql(DIRQL_MIN);
    const auto routine = g_bootstrapMsiRoutine;
    void* const context = g_bootstrapMsiContext;
    if (!routine || !g_bootstrapMsiToken) __builtin_trap();
    routine(context);
    g_timerApic.write(0xb0 / 4, 0);
    KeLowerIrql(previous);
}

__attribute__((naked)) void MicantBootstrapTimerInterrupt() noexcept {
    __asm__ __volatile__(
        // A thread-owned stack must have space for the interrupt frame and
        // dispatcher. Stop before overwriting its allocation on overflow.
        "pushq %rax\nleaq 8(%rsp), %rax\n"
        "cmpq MicantBootstrapUserInterruptStackBase(%rip), %rax\njb 5f\n"
        "cmpq MicantBootstrapUserInterruptStackTop(%rip), %rax\njae 5f\n"
        "cmpq MicantBootstrapUserInterruptStackFloor(%rip), %rax\njae 6f\nud2\n"
        "5: cmpq MicantBootstrapSyscallStackBase(%rip), %rax\njb 7f\n"
        "cmpq MicantBootstrapSyscallStackTop(%rip), %rax\njae 7f\n"
        "cmpq MicantBootstrapSyscallStackFloor(%rip), %rax\njae 6f\nud2\n"
        "7: cmpq MicantBootstrapExecutionStackTop(%rip), %rax\njae 8f\n"
        "cmpq MicantBootstrapInterruptStackFloor(%rip), %rax\njae 6f\n8: ud2\n6:\n"
        "cld\n"
        "pushq %rcx\npushq %rdx\npushq %rbx\npushq %rbp\n"
        "pushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %r12\nmovl MicantAmd64ExtendedSize(%rip), %eax\n"
        "addq $96, %rax\nsubq %rax, %rsp\nandq $-64, %rsp\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 1f\n"
        "leaq 64(%rsp), %r13\n"
        "movq $0, 512(%r13)\nmovq $0, 520(%r13)\n"
        "movq $0, 528(%r13)\nmovq $0, 536(%r13)\n"
        "movq $0, 544(%r13)\nmovq $0, 552(%r13)\n"
        "movq $0, 560(%r13)\nmovq $0, 568(%r13)\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xsave64 (%r13)\njmp 2f\n"
        "1: fxsave64 64(%rsp)\n"
        "2: movq 128(%r12), %rcx\ncallq MicantBootstrapEnterInterrupt\n"
        "movq %rax, 40(%rsp)\ncallq MicantBootstrapTimerDispatch\n"
        "leaq 120(%r12), %rcx\nmovq %r12, %rdx\nleaq 64(%rsp), %r8\n"
        "callq MicantBootstrapInterruptExit\n"
        "movq 40(%rsp), %rcx\ncallq MicantBootstrapLeaveInterrupt\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 3f\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xrstor64 64(%rsp)\njmp 4f\n"
        "3: fxrstor64 64(%rsp)\n"
        "4: movq %r12, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\n"
        "popq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rbx\n"
        "popq %rdx\npopq %rcx\npopq %rax\niretq\n");
}

__attribute__((naked)) void MicantBootstrapMsiInterrupt() noexcept {
    __asm__ __volatile__(
        "pushq %rax\nleaq 8(%rsp), %rax\n"
        "cmpq MicantBootstrapUserInterruptStackBase(%rip), %rax\njb 5f\n"
        "cmpq MicantBootstrapUserInterruptStackTop(%rip), %rax\njae 5f\n"
        "cmpq MicantBootstrapUserInterruptStackFloor(%rip), %rax\njae 6f\nud2\n"
        "5: cmpq MicantBootstrapSyscallStackBase(%rip), %rax\njb 7f\n"
        "cmpq MicantBootstrapSyscallStackTop(%rip), %rax\njae 7f\n"
        "cmpq MicantBootstrapSyscallStackFloor(%rip), %rax\njae 6f\nud2\n"
        "7: cmpq MicantBootstrapExecutionStackTop(%rip), %rax\njae 8f\n"
        "cmpq MicantBootstrapInterruptStackFloor(%rip), %rax\njae 6f\n8: ud2\n6:\n"
        "cld\npushq %rcx\npushq %rdx\npushq %rbx\npushq %rbp\n"
        "pushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %r12\nmovl MicantAmd64ExtendedSize(%rip), %eax\n"
        "addq $96, %rax\nsubq %rax, %rsp\nandq $-64, %rsp\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 1f\n"
        "leaq 64(%rsp), %r13\n"
        "movq $0, 512(%r13)\nmovq $0, 520(%r13)\n"
        "movq $0, 528(%r13)\nmovq $0, 536(%r13)\n"
        "movq $0, 544(%r13)\nmovq $0, 552(%r13)\n"
        "movq $0, 560(%r13)\nmovq $0, 568(%r13)\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xsave64 (%r13)\njmp 2f\n"
        "1: fxsave64 64(%rsp)\n"
        "2: movq 128(%r12), %rcx\ncallq MicantBootstrapEnterInterrupt\n"
        "movq %rax, 40(%rsp)\ncallq MicantBootstrapMsiDispatch\n"
        "leaq 120(%r12), %rcx\nmovq %r12, %rdx\nleaq 64(%rsp), %r8\n"
        "callq MicantBootstrapInterruptExit\n"
        "movq 40(%rsp), %rcx\ncallq MicantBootstrapLeaveInterrupt\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 3f\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xrstor64 64(%rsp)\njmp 4f\n"
        "3: fxrstor64 64(%rsp)\n"
        "4: movq %r12, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\n"
        "popq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rbx\n"
        "popq %rdx\npopq %rcx\npopq %rax\niretq\n");
}

void MicantBootstrapPs2Dispatch() noexcept {
    using namespace micant::ke;
    const auto previous = KfRaiseIrql(DIRQL_MIN);
    micant::hal::amd64::captureBootstrapPs2Interrupt();
    g_timerApic.write(0xb0 / 4, 0);
    KeLowerIrql(previous);
}

__attribute__((naked)) void MicantBootstrapPs2Interrupt() noexcept {
    __asm__ __volatile__(
        // A thread-owned stack must have space for the interrupt frame and
        // dispatcher. Stop before overwriting its allocation on overflow.
        "pushq %rax\nleaq 8(%rsp), %rax\n"
        "cmpq MicantBootstrapUserInterruptStackBase(%rip), %rax\njb 5f\n"
        "cmpq MicantBootstrapUserInterruptStackTop(%rip), %rax\njae 5f\n"
        "cmpq MicantBootstrapUserInterruptStackFloor(%rip), %rax\njae 6f\nud2\n"
        "5: cmpq MicantBootstrapSyscallStackBase(%rip), %rax\njb 7f\n"
        "cmpq MicantBootstrapSyscallStackTop(%rip), %rax\njae 7f\n"
        "cmpq MicantBootstrapSyscallStackFloor(%rip), %rax\njae 6f\nud2\n"
        "7: cmpq MicantBootstrapExecutionStackTop(%rip), %rax\njae 8f\n"
        "cmpq MicantBootstrapInterruptStackFloor(%rip), %rax\njae 6f\n8: ud2\n6:\n"
        "cld\n"
        "pushq %rcx\npushq %rdx\npushq %rbx\npushq %rbp\n"
        "pushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %r12\nmovl MicantAmd64ExtendedSize(%rip), %eax\n"
        "addq $96, %rax\nsubq %rax, %rsp\nandq $-64, %rsp\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 1f\n"
        "leaq 64(%rsp), %r13\n"
        "movq $0, 512(%r13)\nmovq $0, 520(%r13)\n"
        "movq $0, 528(%r13)\nmovq $0, 536(%r13)\n"
        "movq $0, 544(%r13)\nmovq $0, 552(%r13)\n"
        "movq $0, 560(%r13)\nmovq $0, 568(%r13)\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xsave64 (%r13)\njmp 2f\n"
        "1: fxsave64 64(%rsp)\n"
        "2: movq 128(%r12), %rcx\ncallq MicantBootstrapEnterInterrupt\n"
        "movq %rax, 40(%rsp)\ncallq MicantBootstrapPs2Dispatch\n"
        "leaq 120(%r12), %rcx\nmovq %r12, %rdx\nleaq 64(%rsp), %r8\n"
        "callq MicantBootstrapInterruptExit\n"
        "movq 40(%rsp), %rcx\ncallq MicantBootstrapLeaveInterrupt\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 3f\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xrstor64 64(%rsp)\njmp 4f\n"
        "3: fxrstor64 64(%rsp)\n"
        "4: movq %r12, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\n"
        "popq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rbx\n"
        "popq %rdx\npopq %rcx\npopq %rax\niretq\n");
}

__attribute__((naked)) void MicantBootstrapSpuriousInterrupt() noexcept {
    __asm__ __volatile__("iretq\n");
}

extern char MicantManagedReadFault[], MicantManagedReadResume[];
extern char MicantManagedWriteFault[], MicantManagedWriteResume[];

__attribute__((naked)) void MicantManagedReadProbe(uintptr_t) noexcept {
    __asm__ __volatile__(
        ".globl MicantManagedReadFault\nMicantManagedReadFault:\n"
        "movb (%rcx), %al\n"
        ".globl MicantManagedReadResume\nMicantManagedReadResume:\nretq\n");
}

__attribute__((naked)) void MicantManagedWriteProbe(uintptr_t) noexcept {
    __asm__ __volatile__(
        ".globl MicantManagedWriteFault\nMicantManagedWriteFault:\n"
        "movb $0x5a, (%rcx)\n"
        ".globl MicantManagedWriteResume\nMicantManagedWriteResume:\nretq\n");
}

void MicantManagedPageFault(uint64_t* frame) noexcept {
    uint64_t address{};
    __asm__ __volatile__("movq %%cr2, %0" : "=r"(address));
    if (!g_managedFaultArmed || address != g_managedFaultAddress ||
        frame[0] != g_managedFaultError || frame[1] != g_managedFaultRip ||
        (frame[2] & 3) != 0) {
        if (g_log) g_log("[MicaNT Mm] Unexpected managed probe page fault\r\n");
        logTrapValue("[MicaNT Mm] PF CR2=0x", address);
        logTrapValue("[MicaNT Mm] PF error=0x", frame[0]);
        logTrapValue("[MicaNT Mm] PF RIP=0x", frame[1]);
        logTrapValue("[MicaNT Mm] PF CS=0x", frame[2]);
        stopOnTrap();
    }
    g_managedFaultArmed = false;
    g_managedFaultObserved = true;
    frame[1] = g_managedFaultResume;
}

__attribute__((naked)) void MicantManagedPageFaultEntry() {
    __asm__ __volatile__(
        "cld\n"
        "pushq %rax\npushq %rcx\npushq %rdx\n"
        "pushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\n"
        "leaq 64(%rsp), %rcx\nmovq %rsp, %r12\n"
        "andq $-16, %rsp\nsubq $32, %rsp\ncallq MicantManagedPageFault\n"
        "movq %r12, %rsp\n"
        "popq %r12\npopq %r11\npopq %r10\npopq %r9\n"
        "popq %r8\npopq %rdx\npopq %rcx\npopq %rax\n"
        "addq $8, %rsp\niretq\n");
}

uint64_t MicantProbeKernelRsp{};
uint64_t MicantProbeUserRip = kUserBase;
uint64_t MicantProbeUserRsp =
    kUserStackBase + sizeof(g_userStack) - 16;

void restoreSyscallUserSegments(const micant::hal::amd64::UserSegmentState& state) noexcept {
    // SYSCALL's active GS is the entry structure; the user GS base lives in
    // IA32_KERNEL_GS_BASE until the epilogue SWAPGS. Loading a selector can
    // replace its hidden base, so restore both the entry and user bases.
    if (!micant::hal::amd64::bootstrapSyscallGsActive()) __builtin_trap();
    const uint64_t entryGsBase = micant::cpu::cpuRdmsr(micant::cpu::MSR_GS_BASE);
    __asm__ __volatile__("movw %0, %%ds; movw %1, %%es; movw %2, %%fs; movw %3, %%gs"
                         : : "rm"(state.ds), "rm"(state.es), "rm"(state.fs), "rm"(state.gs) : "memory");
    micant::cpu::cpuWrmsr(micant::cpu::MSR_FS_BASE, state.fsBase);
    micant::cpu::cpuWrmsr(micant::cpu::MSR_GS_BASE, entryGsBase);
    micant::cpu::cpuWrmsr(micant::cpu::MSR_KERNEL_GS_BASE, state.userGsBase);
}

void MicantBootstrapDispatch(SyscallFrame* frame, const void* floatingState) noexcept {
    if (!g_dispatch) {
        if (g_log) g_log("[MicaNT Hal] Bootstrap syscall dispatcher is unset\r\n");
        stopOnTrap();
    }
    // A process can unmap or reprotect its own stack and code while a
    // syscall is active. Canonical user addresses are safe for IRET even if
    // their pages are absent: the ensuing CPL3 page fault belongs to that
    // process and must enter its ordinary exception dispatcher. Non-user or
    // noncanonical control state would fault during IRET in ring 0 instead.
    constexpr uint64_t highestUserAddress = 0x00007FFFFFFFFFFFULL;
    if (frame->rip > highestUserAddress || frame->rsp > highestUserAddress ||
        frame->cs != 0x33 || frame->ss != 0x2b) {
        auto* current = micant::ke::KeGetCurrentThread();
        const bool previousUserMode = current && current->previousUserMode;
        if (current) current->previousUserMode = true;
        (void)MicantPsTerminateFailedUserException(
            static_cast<uint32_t>(micant::NtStatus::AccessViolation));
        if (current) current->previousUserMode = previousUserMode;
        if (g_log) g_log("[MicaNT Hal] Invalid syscall return frame\r\n");
        stopOnTrap();
    }
    micant::hal::amd64::UserSegmentState userSegments{};
    __asm__ __volatile__("movw %%ds, %0; movw %%es, %1; movw %%fs, %2; movw %%gs, %3"
                         : "=rm"(userSegments.ds), "=rm"(userSegments.es),
                           "=rm"(userSegments.fs), "=rm"(userSegments.gs));
    userSegments.fsBase = micant::cpu::cpuRdmsr(micant::cpu::MSR_FS_BASE);
    userSegments.userGsBase = micant::cpu::cpuRdmsr(micant::cpu::MSR_KERNEL_GS_BASE);
    const micant::hal::amd64::UserServiceArguments args{
        frame->rax, frame->r10, frame->rdx, frame->r8, frame->r9, frame->rsp,
        frame, const_cast<void*>(floatingState), MicantAmd64ExtendedSize,
        MicantAmd64XsaveMask, &userSegments
    };
    auto* syscallThread = micant::ke::KeGetCurrentThread();
    const bool previousUserMode = syscallThread && syscallThread->previousUserMode;
    if (syscallThread) syscallThread->previousUserMode = true;
    const uint64_t serviceResult = g_dispatch(args);
    // Some services temporarily load kernel selectors. Recover the actual
    // entry selectors before constructing an APC or dispatching deferred work.
    restoreSyscallUserSegments(userSegments);
    const bool continuationApplied =
        serviceResult == micant::hal::amd64::BootstrapNtContinueApplied;
    if (!continuationApplied)
        frame->rax = serviceResult;
    // Service routines can make a higher-priority thread ready while the
    // syscall entry has interrupts disabled. Dispatch after service cleanup.
    if (g_schedulerClockActive) {
        if (frame->flags & (uint64_t{1} << 9)) micant::ke::detail::servicePendingDpcs();
        micant::ke::detail::dispatchHigherPriority();
        if (frame->flags & (uint64_t{1} << 9)) micant::ke::detail::servicePendingKernelApcs(true);
    }
    // Even early bootstrap user contexts may return with IF clear. A pending
    // process termination must be serviced before any return to user mode.
    micant::ke::detail::serviceUserReturnTermination();
    // Keep queued APCs out of both first- and second-chance exception
    // dispatch. The second-chance path can call process termination and return
    // an error; injecting another APC there would interrupt the active
    // exception dispatcher and could run callbacks after an earlier callback
    // faulted.
    const bool dispatchingRaisedException = args.number == MICANT_SSN_NT_RAISE_EXCEPTION;
    const bool explicitApcAlert = args.number == MICANT_SSN_NT_TEST_ALERT;
    if (!dispatchingRaisedException && (frame->cs & 3) == 3 &&
        ((frame->flags & (uint64_t{1} << 9)) || explicitApcAlert) && floatingState &&
        micant::ke::KeGetCurrentIrql() == micant::ke::PASSIVE_LEVEL) {
        uint64_t hardwareFrame[5]{frame->rip, frame->cs, frame->flags, frame->rsp, frame->ss};
        uint64_t savedRegisters[15]{
            frame->r15, frame->r14, frame->r13, frame->r12, frame->r11, frame->r10,
            frame->r9, frame->r8, frame->rdi, frame->rsi, frame->rbp, frame->rbx,
            frame->rdx, frame->rcx, frame->rax
        };
        if (MicantPsPrepareUserApc(hardwareFrame, savedRegisters, floatingState)) {
            frame->rip = hardwareFrame[0];
            frame->cs = hardwareFrame[1];
            frame->flags = hardwareFrame[2];
            frame->rsp = hardwareFrame[3];
            frame->ss = hardwareFrame[4];
            frame->r15 = savedRegisters[0]; frame->r14 = savedRegisters[1];
            frame->r13 = savedRegisters[2]; frame->r12 = savedRegisters[3];
            frame->r11 = savedRegisters[4];
            frame->r10 = savedRegisters[5]; frame->r9 = savedRegisters[6];
            frame->r8 = savedRegisters[7]; frame->rdi = savedRegisters[8];
            frame->rsi = savedRegisters[9]; frame->rbp = savedRegisters[10];
            frame->rbx = savedRegisters[11]; frame->rdx = savedRegisters[12];
            frame->rcx = savedRegisters[13];
            frame->rax = savedRegisters[14];
        }
    }
    // Preserve user IF only after the BSP scheduler clock owns IRQ routing.
    // NtContinue carries a validated user CONTEXT, including TF/RF/AC/ID.
    constexpr uint64_t userRestorableFlags = 0x00250FD7;
    if (continuationApplied) {
        const uint64_t allowed = g_schedulerClockActive
            ? userRestorableFlags : (userRestorableFlags & ~uint64_t{0x200});
        frame->flags = (frame->flags & allowed) | 2;
    } else {
        frame->flags = (frame->flags & (g_schedulerClockActive ? 0xED5 : 0xCD5)) | 2;
    }
    if (syscallThread) syscallThread->previousUserMode = previousUserMode;
    // Kernel callbacks may have switched threads; reestablish the captured
    // or NtContinue-selected user state before the epilogue performs SWAPGS.
    restoreSyscallUserSegments(userSegments);
}

__attribute__((naked)) void MicantBootstrapSyscallEntry() {
    __asm__ __volatile__(
        "swapgs\n"
        "movq %rsp, %gs:0\nmovq %gs:8, %rsp\n"
        "pushq $0x2b\npushq %gs:0\npushq %r11\npushq $0x33\npushq %rcx\n"
        // SYSCALL consumes RCX/R11 for the architectural return RIP/flags.
        // Reserve explicit user-RCX/R11 slots for NtContinue to populate.
        "pushq $0\npushq $0\n"
        "pushq %rax\npushq %rbx\npushq %rbp\npushq %rdi\npushq %rsi\n"
        "pushq %rdx\npushq %r8\npushq %r9\npushq %r10\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %r12\nmovl MicantAmd64ExtendedSize(%rip), %eax\n"
        "addq $128, %rax\nsubq %rax, %rsp\nandq $-64, %rsp\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 1f\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xsave64 64(%rsp)\njmp 2f\n"
        "1: fxsave64 64(%rsp)\n"
        "2: movq %r12, %rcx\nleaq 64(%rsp), %rdx\n"
        "callq MicantBootstrapDispatch\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 3f\n"
        // Restore the legacy x87/SSE context separately. NtContinue may carry
        // CONTEXT.FltSave with XSAVE header bits 0/1 clear, while XSAVE tail
        // restoration intentionally covers only enabled extended components.
        "fxrstor64 64(%rsp)\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nandl $-4, %eax\nxorl %edx, %edx\n"
        "xrstor64 64(%rsp)\njmp 4f\n"
        "3: fxrstor64 64(%rsp)\n"
        "4: movq %r12, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\n"
        "popq %r10\npopq %r9\npopq %r8\npopq %rdx\n"
        "popq %rsi\npopq %rdi\npopq %rbp\npopq %rbx\npopq %rax\n"
        "popq %rcx\npopq %r11\n"
        "swapgs\niretq\n"
    );
}

extern const uint8_t MicantSyscallProbeEnd[];
extern const uint8_t MicantSyscallClientEnd[];
extern const uint8_t MicantSyscallClientBreakpoint[];

__attribute__((noinline, section(".uclient$a")))
void MicantSyscallClientProgram() {
    auto* clockData = reinterpret_cast<volatile uint64_t*>(kUserBase + 4096 + 512);
    auto* systemTime = reinterpret_cast<volatile int64_t*>(clockData);
    auto* qpcCounter = reinterpret_cast<volatile int64_t*>(clockData + 1);
    auto* qpcFrequency = reinterpret_cast<volatile int64_t*>(clockData + 2);
    const uint64_t firstTimeStatus = micant::sys::invokeUserService(0x47,
        reinterpret_cast<uint64_t>(systemTime));
    clockData[3] = firstTimeStatus;
    __asm__ __volatile__("movq %%r8, %0" : "=m"(clockData[5]) : : "memory");
    const uint64_t firstQpcStatus = micant::sys::invokeUserService(0x31,
        reinterpret_cast<uint64_t>(qpcCounter),
        reinterpret_cast<uint64_t>(qpcFrequency));
    clockData[4] = firstQpcStatus;
    __asm__ __volatile__("movq %%r8, %0" : "=m"(clockData[6]) : : "memory");
    const bool systemTimeReady = firstTimeStatus == 0;
    if ((!systemTimeReady && firstTimeStatus != 0xC00000A3ULL) ||
        (systemTimeReady && *systemTime <= 0) ||
        firstQpcStatus != 0 ||
        *qpcCounter <= 0 || *qpcFrequency < 1'000'000 ||
        *qpcFrequency > 1'000'000'000'000LL) __builtin_trap();
    const int64_t firstSystemTime = *systemTime;
    const int64_t firstQpc = *qpcCounter;
    const int64_t calibratedFrequency = *qpcFrequency;
    auto* sharedTimeLow = reinterpret_cast<volatile const uint32_t*>(0x7FFE0014ULL);
    auto* sharedTimeHigh1 = reinterpret_cast<volatile const int32_t*>(0x7FFE0018ULL);
    auto* sharedTimeHigh2 = reinterpret_cast<volatile const int32_t*>(0x7FFE001cULL);
    auto* sharedTickCount = reinterpret_cast<volatile const uint64_t*>(0x7FFE0320ULL);
    uint64_t sharedSystemTime{};
    bool sharedTimeStable = false;
    for (unsigned retry = 0; retry < 16; ++retry) {
        const int32_t high1 = *sharedTimeHigh1;
        const uint32_t low = *sharedTimeLow;
        const int32_t high2 = *sharedTimeHigh2;
        if (high1 == high2) {
            sharedSystemTime = (static_cast<uint64_t>(static_cast<uint32_t>(high1)) << 32) | low;
            sharedTimeStable = true;
            break;
        }
    }
    const uint64_t firstTickCount = *sharedTickCount;
    if (!sharedTimeStable) __builtin_trap();
    if (systemTimeReady) {
        const uint64_t firstSystemTimeBits = static_cast<uint64_t>(firstSystemTime);
        const uint64_t sharedDifference = sharedSystemTime > firstSystemTimeBits
            ? sharedSystemTime - firstSystemTimeBits : firstSystemTimeBits - sharedSystemTime;
        if (!sharedSystemTime || sharedDifference > micant::shared::time::TicksPerSecond)
            __builtin_trap();
    } else if (sharedSystemTime) {
        __builtin_trap(); // Never publish an unknown/local wall clock as UTC.
    }
    for (uint32_t spin = 0; spin < 2'000'000; ++spin)
        __asm__ __volatile__("pause" : : : "memory");
    if (firstTickCount > UINT64_MAX - 50) __builtin_trap();
    const uint64_t targetTickCount = firstTickCount + 50;
    for (uint32_t spin = 0; spin < 40'000'000 && *sharedTickCount < targetTickCount; ++spin)
        __asm__ __volatile__("pause" : : : "memory");
    const uint64_t secondTimeStatus = micant::sys::invokeUserService(0x47,
        reinterpret_cast<uint64_t>(systemTime));
    if ((systemTimeReady && (secondTimeStatus != 0 || *systemTime <= firstSystemTime)) ||
        (!systemTimeReady && secondTimeStatus != 0xC00000A3ULL) ||
        micant::sys::invokeUserService(0x31,
            reinterpret_cast<uint64_t>(qpcCounter),
            reinterpret_cast<uint64_t>(qpcFrequency)) != 0 ||
        *qpcCounter <= firstQpc ||
        *qpcFrequency != calibratedFrequency || *sharedTickCount < targetTickCount)
        __builtin_trap();
    const uint64_t secondQpc = static_cast<uint64_t>(*qpcCounter);
    if (systemTimeReady) {
        const uint64_t firstUtc = static_cast<uint64_t>(firstSystemTime);
        const uint64_t secondUtc = static_cast<uint64_t>(*systemTime);
        const uint64_t qpcElapsed = secondQpc - static_cast<uint64_t>(firstQpc);
        if (qpcElapsed > UINT64_MAX / micant::shared::time::TicksPerSecond)
            __builtin_trap();
        const uint64_t expected100ns =
            (qpcElapsed * micant::shared::time::TicksPerSecond) /
            static_cast<uint64_t>(calibratedFrequency);
        const uint64_t actual100ns = secondUtc - firstUtc;
        const uint64_t error100ns = actual100ns > expected100ns
            ? actual100ns - expected100ns : expected100ns - actual100ns;
        if (error100ns > expected100ns / 20 + 50'000) __builtin_trap();
    }
    auto* video = reinterpret_cast<micant::um::UserVideoInfo64*>(kUserBase + 4096);
    if (micant::sys::invokeUserService(0x80, reinterpret_cast<uint64_t>(video)) ||
        video->width != 1024 || video->height != 768 || video->physicalBase) {
        __builtin_trap();
    }
    auto* request = reinterpret_cast<MicaWin32kRequest*>(kUserBase + 8192);
    auto clearRequest = [request]() {
        auto* bytes = reinterpret_cast<volatile uint8_t*>(request);
        for (size_t index = 0; index < sizeof(*request); ++index) {
            bytes[index] = 0;
        }
    };
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_REGISTER_CLASS;
    volatile uint16_t* className = request->className;
    className[0] = 'M'; className[1] = 'i'; className[2] = 'c';
    className[3] = 'a'; className[4] = 'N'; className[5] = 'T';
    className[6] = '.'; className[7] = 'P'; className[8] = 'r';
    className[9] = 'o'; className[10] = 'b'; className[11] = 'e';
    className[12] = 'W'; className[13] = 'i'; className[14] = 'n';
    className[15] = 'd'; className[16] = 'o'; className[17] = 'w';
    className[18] = 0;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->result == 0) {
        __builtin_trap();
    }
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_CREATE_WINDOW;
    className = request->className;
    className[0] = 'M'; className[1] = 'i'; className[2] = 'c';
    className[3] = 'a'; className[4] = 'N'; className[5] = 'T';
    className[6] = '.'; className[7] = 'P'; className[8] = 'r';
    className[9] = 'o'; className[10] = 'b'; className[11] = 'e';
    className[12] = 'W'; className[13] = 'i'; className[14] = 'n';
    className[15] = 'd'; className[16] = 'o'; className[17] = 'w';
    className[18] = 0;
    volatile uint16_t* title = request->title;
    title[0] = 'C'; title[1] = 'P'; title[2] = 'L'; title[3] = '3';
    title[4] = ' '; title[5] = 'W'; title[6] = 'i'; title[7] = 'n';
    title[8] = '3'; title[9] = '2'; title[10] = 'k'; title[11] = ' ';
    title[12] = 'P'; title[13] = 'r'; title[14] = 'o'; title[15] = 'b';
    title[16] = 'e'; title[17] = 0;
    request->args[1] = 0x10000000;
    request->args[2] = 10;
    request->args[3] = 20;
    request->args[4] = 320;
    request->args[5] = 240;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->result == 0) {
        __builtin_trap();
    }
    const uint64_t window = request->result;
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_GET_CLIENT_RECT;
    request->args[0] = window;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->rect.right != 320 || request->rect.bottom != 240) {
        __builtin_trap();
    }
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_DESTROY_WINDOW;
    request->args[0] = window;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->result != 1) {
        __builtin_trap();
    }
    auto* message = reinterpret_cast<char*>(kUserBase + 4096 + 64);
    message[0] = 'C'; message[1] = 'P'; message[2] = 'L';
    message[3] = '3'; message[4] = '\r'; message[5] = '\n';
    if (micant::sys::invokeUserService(0x25, reinterpret_cast<uint64_t>(message), 6)) {
        __builtin_trap();
    }
    // The loader resolves these addresses from the actual NTDLL PE exports.
    auto* exports = reinterpret_cast<const uint64_t*>(kUserBase + 4096 + 768);
    using Create = int32_t (*)(void**, uint32_t, void*, int64_t*, uint32_t, uint32_t, void*);
    using Map = int32_t (*)(void*, void*, void**, uintptr_t, size_t, int64_t*, size_t*, uint32_t, uint32_t, uint32_t);
    using Unmap = int32_t (*)(void*, void*);
    using Close = int32_t (*)(void*);
    using Allocate = int32_t (*)(void*, void**, uintptr_t, size_t*, uint32_t, uint32_t);
    using Free = int32_t (*)(void*, void**, size_t*, uint32_t);
    using CreateEvent = int32_t (*)(void**, uint32_t, void*, uint32_t, uint8_t);
    using SetEvent = int32_t (*)(void*, int32_t*);
    using Wait = int32_t (*)(void*, uint8_t, int64_t*);
    auto create = reinterpret_cast<Create>(exports[0]);
    auto map = reinterpret_cast<Map>(exports[1]);
    auto unmap = reinterpret_cast<Unmap>(exports[2]);
    auto close = reinterpret_cast<Close>(exports[3]);
    auto allocate = reinterpret_cast<Allocate>(exports[4]);
    auto freeMemory = reinterpret_cast<Free>(exports[5]);
    auto createEvent = reinterpret_cast<CreateEvent>(exports[6]);
    auto setEvent = reinterpret_cast<SetEvent>(exports[7]);
    auto wait = reinterpret_cast<Wait>(exports[8]);
    void* event{};
    int64_t poll = 0;
    int32_t previous = -1;
    if (createEvent(nullptr, 0x100002, nullptr, 1, 0) != static_cast<int32_t>(0xc0000005u) ||
        createEvent(&event, 0x100002, nullptr, 1, 0) || !event ||
        setEvent(event, reinterpret_cast<int32_t*>(1)) != static_cast<int32_t>(0xc0000005u) ||
        wait(event, 0, reinterpret_cast<int64_t*>(1)) != static_cast<int32_t>(0xc0000005u) ||
        wait(event, 0, nullptr) != static_cast<int32_t>(0xc0000002u) ||
        wait(event, 0, &poll) != static_cast<int32_t>(0xc0000002u) ||
        setEvent(event, &previous) || previous != 0 ||
        wait(event, 0, &poll) != static_cast<int32_t>(0xc0000002u) ||
        close(event) || setEvent(event, nullptr) != static_cast<int32_t>(0xc0000008u)) __builtin_trap();
    void* section{};
    void* first{};
    void* second{};
    int64_t maximum = 4096;
    size_t firstSize = 4096, secondSize = 4096;
    micant::ObjectAttributes attributes{};
    auto* current = reinterpret_cast<void*>(~uint64_t{0});
    void* privateMemory{};
    size_t privateSize = 37;
    if (allocate(current, nullptr, 0, &privateSize, 0x3000, 4) != static_cast<int32_t>(0xc0000005u) ||
        allocate(current, &privateMemory, 0, &privateSize, 0x3000, 4) ||
        !privateMemory || privateSize != 4096 ||
        *static_cast<volatile uint64_t*>(privateMemory) != 0) __builtin_trap();
    *static_cast<volatile uint64_t*>(privateMemory) = 0x123456789abcdef0ULL;
    if (*static_cast<volatile uint64_t*>(privateMemory) != 0x123456789abcdef0ULL) __builtin_trap();
    size_t invalidRelease = 1;
    if (freeMemory(current, &privateMemory, &invalidRelease, 0x8000) != static_cast<int32_t>(0xc000000du) ||
        invalidRelease != 1 || *static_cast<volatile uint64_t*>(privateMemory) != 0x123456789abcdef0ULL)
        __builtin_trap();
    size_t decommitSize = 1;
    if (freeMemory(current, &privateMemory, &decommitSize, 0x4000) || decommitSize != 4096)
        __builtin_trap();
    size_t recommitSize = 4096;
    if (allocate(current, &privateMemory, 0, &recommitSize, 0x1000, 4) ||
        *static_cast<volatile uint64_t*>(privateMemory) != 0) __builtin_trap();
    size_t releaseSize = 0;
    if (freeMemory(current, &privateMemory, &releaseSize, 0x8000) || releaseSize != 4096)
        __builtin_trap();
    if (create(nullptr, 4, nullptr, &maximum, 4, 0x08000000, nullptr) !=
            static_cast<int32_t>(0xc0000005u) ||
        create(&section, 4, &attributes, &maximum, 4, 0x08000000, nullptr) ||
        map(section, current, &first, 0, 0, nullptr, &firstSize, 2, 0, 4) !=
            static_cast<int32_t>(0xc0000022u) ||
        map(section, current, &first, 0, 0, nullptr, &firstSize, 1, 0, 2) ||
        !first || unmap(current, first) || close(section)) __builtin_trap();
    first = nullptr;
    section = nullptr;
    if (create(&section, 6, nullptr, &maximum, 4, 0x08000000, nullptr) ||
        map(section, current, &first, 0, 0, nullptr, &firstSize, 2, 0, 4) ||
        map(section, current, &second, 0, 0, nullptr, &secondSize, 2, 0, 2)) __builtin_trap();
    *static_cast<volatile uint64_t*>(first) = 0x12345678;
    if (*static_cast<volatile uint64_t*>(second) != 0x12345678 ||
        unmap(current, first) || close(section) ||
        *static_cast<volatile uint64_t*>(second) != 0x12345678 ||
        unmap(current, second) ||
        unmap(current, second) != static_cast<int32_t>(0xc0000019u)) __builtin_trap();
    __asm__ __volatile__(
        ".globl MicantSyscallClientBreakpoint\nMicantSyscallClientBreakpoint:\n"
        "int3\nud2\n"
    );
    __builtin_unreachable();
}

__asm__(".section .uclient$z,\"xr\"\n"
        ".globl MicantSyscallClientEnd\nMicantSyscallClientEnd:\n.text\n");

__attribute__((naked)) void MicantSyscallProbeProgram() {
    __asm__ __volatile__(
        "movabsq $0x400000001000, %r10\n"
        "movq $0x1234, %rdx\nmovq $0x5678, %r8\nmovq $0x9abc, %r9\n"
        "movq $0x1357, %rbx\nmovq $0x2468, %r12\n"
        "movq %rdx, %xmm0\nmovq %r8, %xmm6\nmovq %r9, %xmm15\n"
        "std\nmovl $0x80, %eax\nsyscall\ncld\n"
        "testq %rax, %rax\njnz 9f\n"
        "cmpl $1024, (%r10)\njne 9f\ncmpl $768, 4(%r10)\njne 9f\n"
        "cmpq $0, 24(%r10)\njne 9f\n" // No physical display pointer escapes.
        "cmpq $0x1234, %rdx\njne 9f\ncmpq $0x5678, %r8\njne 9f\n"
        "cmpq $0x9abc, %r9\njne 9f\ncmpq $0x1357, %rbx\njne 9f\n"
        "cmpq $0x2468, %r12\njne 9f\n"
        "movq %xmm0, %rax\ncmpq %rdx, %rax\njne 9f\n"
        "movq %xmm6, %rax\ncmpq %r8, %rax\njne 9f\n"
        "movq %xmm15, %rax\ncmpq %r9, %rax\njne 9f\n"
        "xorq %r10, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Null output.
        "movabsq $0x400000000000, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Read-only code.
        "movabsq $0x400000004ff0, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Crosses into an unmapped page.
        "movq $0x1000, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Supervisor address.
        "movq $-16, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Wraparound.
        "movabsq $0x400000001000, %r10\n"
        "movabsq $0x00000a0d334c5043, %rax\nmovq %rax, 64(%r10)\n"
        "addq $64, %r10\nmovq $6, %rdx\nmovl $0x25, %eax\nsyscall\n"
        "testq %rax, %rax\njnz 9f\ncmpq $6, %rdx\njne 9f\n"
        "movabsq $0x400000001000, %r10\n"
        "leaq 128(%r10), %rax\nmovq %rax, (%r10)\n"
        "movq $0, 8(%r10)\n"
        "movl $2, 16(%r10)\nmovl $2, 20(%r10)\n"
        "movl $2, 24(%r10)\nmovl $0, 28(%r10)\n"
        "movabsq $0xff112233ff112233, %rax\n"
        "movq %rax, 128(%r10)\nmovq %rax, 136(%r10)\n"
        "movl $0x81, %eax\nsyscall\ntestq %rax, %rax\njnz 9f\n"
        "movl $0, 16(%r10)\nmovl $0x81, %eax\nsyscall\n"
        "cmpl $0xc000000d, %eax\njne 9f\n" // Zero width.
        "movl $2, 16(%r10)\nmovq $0, (%r10)\n"
        "movl $0x81, %eax\nsyscall\ncmpl $0xc0000005, %eax\njne 9f\n"
        "movabsq $0x400000004ff8, %rax\nmovq %rax, (%r10)\n"
        "movl $0x81, %eax\nsyscall\ncmpl $0xc0000005, %eax\njne 9f\n"
        // Windows x64 syscall stack slots: return address, home area, then
        // arguments five onward. The probe supplies this frame explicitly.
        "subq $96, %rsp\n"
        "movabsq $0x400000001100, %rbx\n"
        "movq $4096, 8(%rbx)\nmovq $0, 16(%rbx)\nmovq $0, 24(%rbx)\n"
        "movq $4096, 32(%rbx)\nmovq $4096, 40(%rbx)\n"
        "movq $4, 40(%rsp)\nmovq $0x08000000, 48(%rsp)\nmovq $0, 56(%rsp)\n"
        // A read-only handle cannot create a writable view.
        "leaq 0(%rbx), %r10\nmovq $4, %rdx\nxorq %r8, %r8\nleaq 8(%rbx), %r9\n"
        "movl $0x4a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $0, 40(%rsp)\nmovq $0, 48(%rsp)\nleaq 32(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq $2, 64(%rsp)\nmovq $0, 72(%rsp)\nmovq $4, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 16(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ncmpl $0xc0000022, %eax\njne 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $4, 40(%rsp)\nmovq $0x08000000, 48(%rsp)\nmovq $0, 56(%rsp)\n"
        "leaq 0(%rbx), %r10\nmovq $6, %rdx\nxorq %r8, %r8\nleaq 8(%rbx), %r9\n"
        "movl $0x4a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $0, 40(%rsp)\nmovq $0, 48(%rsp)\nleaq 32(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq $2, 64(%rsp)\nmovq $0, 72(%rsp)\nmovq $4, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 16(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "leaq 40(%rbx), %rax\nmovq %rax, 56(%rsp)\nmovq $2, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 24(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 16(%rbx), %rax\nmovq $0x12345678, (%rax)\n"
        "movq 24(%rbx), %rax\ncmpq $0x12345678, (%rax)\njne 9f\n"
        "movq $-1, %r10\nmovq 16(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ncmpl $0xc0000008, %eax\njne 9f\n"
        "movq 24(%rbx), %rax\ncmpq $0x12345678, (%rax)\njne 9f\n"
        "movq $-1, %r10\nmovq 24(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        // Map the first and seventeenth pages of a larger section. The input
        // offset is rounded down to the 64 KiB allocation granularity.
        "movq $69632, 8(%rbx)\nmovq $0, 16(%rbx)\nmovq $0, 24(%rbx)\n"
        "movq $4096, 32(%rbx)\nmovq $4096, 40(%rbx)\nmovq $65537, 48(%rbx)\n"
        "movq $4, 40(%rsp)\nmovq $0x08000000, 48(%rsp)\nmovq $0, 56(%rsp)\n"
        "leaq 0(%rbx), %r10\nmovq $6, %rdx\nxorq %r8, %r8\nleaq 8(%rbx), %r9\n"
        "movl $0x4a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $0, 40(%rsp)\nmovq $0, 48(%rsp)\nleaq 32(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq $2, 64(%rsp)\nmovq $0, 72(%rsp)\nmovq $4, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 16(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "leaq 48(%rbx), %rax\nmovq %rax, 48(%rsp)\nleaq 40(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 24(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "cmpq $65536, 48(%rbx)\njne 9f\n"
        "movq 16(%rbx), %rax\nmovq $123, (%rax)\nmovq 24(%rbx), %rax\ncmpq $0, (%rax)\njne 9f\n"
        "movq $-1, %r10\nmovq 16(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $-1, %r10\nmovq 24(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "addq $96, %rsp\n"
        "movl $0x7fffffff, %eax\nsyscall\n"
        "cmpl $0xc00000bb, %eax\njne 9f\n"
        "int3\n9: ud2\n"
        ".globl MicantSyscallProbeEnd\nMicantSyscallProbeEnd:\n"
    );
}

[[noreturn]] __attribute__((naked)) void MicantProbeReturn() {
    __asm__ __volatile__(
        "movq MicantProbeKernelRsp(%rip), %rsp\n"
        "movdqu 0(%rsp), %xmm6\nmovdqu 16(%rsp), %xmm7\n"
        "movdqu 32(%rsp), %xmm8\nmovdqu 48(%rsp), %xmm9\n"
        "movdqu 64(%rsp), %xmm10\nmovdqu 80(%rsp), %xmm11\n"
        "movdqu 96(%rsp), %xmm12\nmovdqu 112(%rsp), %xmm13\n"
        "movdqu 128(%rsp), %xmm14\nmovdqu 144(%rsp), %xmm15\n"
        "addq $160, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\n"
        "popq %rsi\npopq %rdi\npopq %rbp\npopq %rbx\n"
        "movb $0, g_ring3ProbeActive(%rip)\nretq\n"
    );
}

void MicantProbeTrap(const uint64_t* frame) noexcept {
    // The trap frame is written by the CPU, not supplied by user memory.
    g_probeSucceeded = (g_probeMode == 0 || g_probeMode == 5 ||
                        g_probeMode == 6 || g_probeMode == 7) &&
                       frame[0] == g_probeReturnRip &&
                       frame[1] == micant::cpu::SELECTOR_UCODE64 &&
                       frame[4] == micant::cpu::SELECTOR_UDATA64 &&
                       (g_probeMode == 6
                           ? micant::hal::amd64::bootstrapUserRange(frame[3], 8, true)
                           : frame[3] == MicantProbeUserRsp) &&
                       (frame[2] & 0x3000) == 0;
    if (!g_probeSucceeded) {
        if (g_log) {
            if (frame[0] != g_probeReturnRip) {
                g_log("[MicaNT Hal] CPL3 probe trap RIP mismatch\r\n");
            }
            if (frame[1] != micant::cpu::SELECTOR_UCODE64 ||
                frame[4] != micant::cpu::SELECTOR_UDATA64) {
                g_log("[MicaNT Hal] CPL3 probe trap selector mismatch\r\n");
            }
            if ((g_probeMode == 6 &&
                 !micant::hal::amd64::bootstrapUserRange(frame[3], 8, true)) ||
                (g_probeMode != 6 && frame[3] != MicantProbeUserRsp)) {
                g_log("[MicaNT Hal] CPL3 probe trap stack mismatch\r\n");
            }
            if (frame[2] & 0x3000) {
                g_log("[MicaNT Hal] CPL3 probe trap privilege flags mismatch\r\n");
            }
        }
        stopOnTrap();
    }
    if (g_log && g_probeMode == 6) {
        g_log("[MicaNT Hal] CPL3 syscall probe breakpoint accepted\r\n");
    }
    MicantProbeReturn();
}

void MicantProbePageFault(const uint64_t* frame) noexcept {
    uint64_t faultAddress{};
    __asm__ __volatile__("movq %%cr2, %0" : "=r"(faultAddress));
    const uint64_t expectedAddress = g_probeMode == 1
        ? reinterpret_cast<uint64_t>(g_pml4)
        : g_probeMode == 2 ? kUserBase
        : g_probeMode == 3 ? kUserBase + 4096 : kDeviceBase;
    const uint64_t expectedError = g_probeMode == 2 ? 7 : g_probeMode == 3 ? 21 : 5;
    const uint64_t expectedRip = g_probeMode == 3 ? expectedAddress : kUserBase + 10;
    g_probeSucceeded = g_probeMode >= 1 && g_probeMode <= 4 &&
                       faultAddress == expectedAddress &&
                       frame[0] == expectedError &&
                       frame[1] == expectedRip &&
                       frame[2] == micant::cpu::SELECTOR_UCODE64 &&
                       frame[5] == micant::cpu::SELECTOR_UDATA64 &&
                       frame[4] == MicantProbeUserRsp &&
                       (frame[3] & 0x3000) == 0;
    if (!g_probeSucceeded) {
        if (g_log) {
            g_log("[MicaNT Hal] Unexpected bootstrap page fault\r\n");
        }
        logTrapValue("[MicaNT Hal] PF CR2=0x", faultAddress);
        logTrapValue("[MicaNT Hal] PF error=0x", frame[0]);
        logTrapValue("[MicaNT Hal] PF RIP=0x", frame[1]);
        logTrapValue("[MicaNT Hal] PF CS=0x", frame[2]);
        stopOnTrap();
    }
    MicantProbeReturn();
}

// The hardware frame is error, RIP, CS, RFLAGS, RSP, SS. The active #PF
// interrupt gate has IF clear. A handled stack guard is retried by IRETQ;
// all other faults retain the existing bootstrap probe/fatal path.
bool MicantDispatchBootstrapPageFault(uint64_t* frame, uint64_t* savedRegisters,
                                      const void* floatingState) noexcept {
    if (g_probeMode >= 1 && g_probeMode <= 4) return false;
    if ((frame[2] & 3) != 3 || micant::ke::KeGetCurrentIrql() != micant::ke::PASSIVE_LEVEL)
        return false;
    uintptr_t address{};
    __asm__ __volatile__("movq %%cr2, %0" : "=r"(address));
    // User write to a present, write-protected image page may be a COW
    // request. Resolve it before stack growth or exception dispatch.
    if ((frame[0] & 0x1f) == 0x07) {
        const auto cowStatus = static_cast<micant::NtStatus>(MicantPsResolveUserCopyOnWrite(address));
        if (cowStatus == micant::NtStatus::Success) return true;
        if (cowStatus != micant::NtStatus::AccessViolation) {
            const auto code = static_cast<uint32_t>(cowStatus);
            if (MicantPsPrepareUserException(code, 14, address, frame, savedRegisters, floatingState))
                return true;
            (void)MicantPsTerminateFailedUserException(code);
            return false;
        }
    }
    const auto resolved = static_cast<micant::NtStatus>(
        MicantPsResolveUserStackFault(address, frame[0], frame[2]));
    if (resolved == micant::NtStatus::Success) return true;
    uint32_t exceptionCode{};
    if (resolved == micant::NtStatus::AccessViolation) {
        exceptionCode = static_cast<uint32_t>(micant::NtStatus::AccessViolation);
    } else if (resolved == micant::NtStatus::GuardPageViolation) {
        exceptionCode = static_cast<uint32_t>(micant::NtStatus::GuardPageViolation);
    } else {
        // The Ps resolver returns other failures only after recognizing the
        // tracked guard and attempting to grow it (for example, commit could
        // not obtain backing). That is exhausted stack growth, not an AV at
        // an arbitrary user address. Keep the failure on the controlled
        // STATUS_STACK_OVERFLOW delivery/termination path.
        exceptionCode = static_cast<uint32_t>(micant::NtStatus::StackOverflow);
    }
    if (MicantPsPrepareUserException(exceptionCode, 14, address, frame, savedRegisters,
                                      floatingState)) return true;
    (void)MicantPsTerminateFailedUserException(exceptionCode);
    return false;
}

uint32_t mapFloatingPointException(uint64_t vector, const void* floatingState) noexcept {
    constexpr uint32_t FloatDivideByZero = 0xC000008Eu;
    constexpr uint32_t FloatInexact = 0xC000008Fu;
    constexpr uint32_t FloatInvalid = 0xC0000090u;
    constexpr uint32_t FloatOverflow = 0xC0000091u;
    constexpr uint32_t FloatUnderflow = 0xC0000093u;
    constexpr uint32_t Unhandled = 0xC0000144u;
    if (!floatingState) return Unhandled;

    const auto* bytes = static_cast<const uint8_t*>(floatingState);
    uint32_t pending{};
    if (vector == 19) {
        // FXSAVE/XSAVE legacy region: MXCSR at byte offset 24, status flags
        // in bits 0..5 and corresponding exception masks in bits 7..12.
        uint32_t mxcsr{};
        for (unsigned i = 0; i < sizeof(mxcsr); ++i)
            mxcsr |= static_cast<uint32_t>(bytes[24 + i]) << (i * 8);
        constexpr uint32_t flags = 0x3Fu;
        const uint32_t masks = (mxcsr >> 7) & flags;
        pending = (mxcsr & flags) & ~masks;
    } else if (vector == 16) {
        // Deferred x87 exception: control word at 0, status word at 2.
        const uint16_t control = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
        const uint16_t status = static_cast<uint16_t>(bytes[2] | (bytes[3] << 8));
        pending = static_cast<uint32_t>(status & 0x3Fu) &
                  ~static_cast<uint32_t>(control & 0x3Fu);
    } else {
        return Unhandled;
    }

    // Match the AMD64 kernel's documented exception priority. ReactOS also
    // maps an unmasked denormal operand to STATUS_FLOAT_INVALID_OPERATION.
    if (pending & (1u << 0)) return FloatInvalid;
    if (pending & (1u << 1)) return FloatInvalid;
    if (pending & (1u << 2)) return FloatDivideByZero;
    if (pending & (1u << 3)) return FloatOverflow;
    if (pending & (1u << 4)) return FloatUnderflow;
    if (pending & (1u << 5)) return FloatInexact;
    return Unhandled;
}

bool MicantDispatchBootstrapException(uint64_t* frame, uint64_t* savedRegisters,
                                      const void* floatingState, uint64_t vector) noexcept {
    if (vector == 3 && g_ring3ProbeActive) MicantProbeTrap(frame + 1);
    if (g_probeMode || (frame[2] & 3) != 3 ||
        micant::ke::KeGetCurrentIrql() != micant::ke::PASSIVE_LEVEL) return false;
    uint32_t code{};
    switch (vector) {
        case 0:  code = 0xC0000094u; break; // STATUS_INTEGER_DIVIDE_BY_ZERO
        case 1:  code = static_cast<uint32_t>(micant::NtStatus::SingleStep); break;
        case 3:  code = static_cast<uint32_t>(micant::NtStatus::Breakpoint); break;
        case 4:  code = 0xC0000095u; break; // STATUS_INTEGER_OVERFLOW
        case 5:  code = 0xC000008Cu; break; // STATUS_ARRAY_BOUNDS_EXCEEDED
        case 6:  code = static_cast<uint32_t>(micant::NtStatus::IllegalInstruction); break;
        case 10: // #TS, #NP, #SS and #GP are selector/segment faults; their
        case 11: // hardware error code is not a page-fault access descriptor.
        case 12:
        case 13: code = static_cast<uint32_t>(micant::NtStatus::AccessViolation); break;
        case 16: // #MF: deferred unmasked x87 exception.
        case 19: // #XM: unmasked SIMD exception described by MXCSR.
            code = mapFloatingPointException(vector, floatingState);
            break;
        case 17: code = static_cast<uint32_t>(micant::NtStatus::DatatypeMisalignment); break;
        case 21: code = 0xC000060Au; break; // STATUS_CONTROL_PROTECTION_VIOLATION
        default: code = 0xC0000144u; break; // STATUS_UNHANDLED_EXCEPTION
    }
    const uintptr_t exceptionAddress = static_cast<uintptr_t>(frame[1]);
    if (MicantPsPrepareUserException(code, static_cast<uint32_t>(vector),
                                     exceptionAddress, frame,
                                     savedRegisters, floatingState)) return true;
    (void)MicantPsTerminateFailedUserException(code);
    return false;
}

void MicantProbeUnexpectedInvalidOpcode(const uint64_t* frame) noexcept {
    if (g_log) {
        g_log("[MicaNT Hal] Unexpected bootstrap invalid opcode (#UD)\r\n");
    }
    logTrapValue("[MicaNT Hal] UD RIP=0x", frame[0]);
    stopOnTrap();
}

void MicantProbeUnexpectedGeneralProtection() noexcept {
    if (g_log) {
        g_log("[MicaNT Hal] Unexpected bootstrap general protection fault (#GP)\r\n");
    }
    stopOnTrap();
}

__attribute__((naked)) void MicantProbeTrapEntry() {
    __asm__ __volatile__(
        "cld\nmovq %rsp, %rcx\nandq $-16, %rsp\nsubq $32, %rsp\ncallq MicantProbeTrap\n"
        "ud2\n"
    );
}

__attribute__((naked)) void MicantProbeFaultEntry() {
    __asm__ __volatile__(
        "cli\ncld\nandq $-16, %rsp\nsubq $32, %rsp\ncallq MicantProbeUnexpectedTrap\nud2\n"
    );
}

__attribute__((naked)) void MicantProbeInvalidOpcodeEntry() {
    __asm__ __volatile__(
        "cli\ncld\nmovq %rsp, %rcx\nandq $-16, %rsp\nsubq $32, %rsp\n"
        "callq MicantProbeUnexpectedInvalidOpcode\nud2\n"
    );
}

__attribute__((naked)) void MicantProbeGeneralProtectionEntry() {
    __asm__ __volatile__(
        "cli\ncld\nandq $-16, %rsp\nsubq $32, %rsp\n"
        "callq MicantProbeUnexpectedGeneralProtection\nud2\n"
    );
}

void MicantProbeUnexpectedTrap() noexcept {
    stopOnTrap();
}

void MicantProbeUnexpectedException(const uint64_t* frame, uint64_t vector) noexcept {
    logTrapValue("[MicaNT Hal] Unhandled exception vector=0x", vector);
    if (frame) {
        logTrapValue("[MicaNT Hal] Unhandled exception RIP=0x", frame[1]);
        logTrapValue("[MicaNT Hal] Unhandled exception CS=0x", frame[2]);
        // The probe entry saved RAX/RCX/RDX ahead of this hardware frame.
        // Keep the faulting service result available for bootstrap diagnosis.
        logTrapValue("[MicaNT Hal] Unhandled exception RAX=0x", frame[-2]);
        logTrapValue("[MicaNT Hal] Unhandled exception RDX=0x", frame[-4]);
    }
    stopOnTrap();
}

__attribute__((naked)) void MicantProbePageFaultEntry() {
    __asm__ __volatile__(
        // Save all interrupted integer registers and the configured x87,
        // SSE and extended state before calling C++. Keep the error-code
        // frame untouched until the final IRETQ.
        "cld\npushq %rax\npushq %rcx\npushq %rdx\npushq %rbx\npushq %rbp\n"
        "pushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %r12\nmovl MicantAmd64ExtendedSize(%rip), %eax\n"
        "cmpl $512, %eax\njae 6f\nmovl $512, %eax\n6:\n"
        "addq $96, %rax\nsubq %rax, %rsp\nandq $-64, %rsp\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 1f\n"
        "leaq 64(%rsp), %r13\n"
        "movq $0, 512(%r13)\nmovq $0, 520(%r13)\n"
        "movq $0, 528(%r13)\nmovq $0, 536(%r13)\n"
        "movq $0, 544(%r13)\nmovq $0, 552(%r13)\n"
        "movq $0, 560(%r13)\nmovq $0, 568(%r13)\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xsave64 (%r13)\njmp 2f\n"
        "1: fxsave64 64(%rsp)\n"
        "2: leaq 120(%r12), %rcx\nmovq %r12, %rdx\nleaq 64(%rsp), %r8\n"
        "callq MicantDispatchBootstrapPageFault\n"
        "testb %al, %al\njnz 3f\n"
        "leaq 120(%r12), %rcx\ncallq MicantProbePageFault\nud2\n"
        "3: cmpl $0, MicantAmd64XsaveMask(%rip)\nje 4f\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xrstor64 64(%rsp)\njmp 5f\n"
        "4: fxrstor64 64(%rsp)\n"
        "5: movq %r12, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\n"
        "popq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rbx\n"
        "popq %rdx\npopq %rcx\npopq %rax\naddq $8, %rsp\niretq\n"
    );
}

__attribute__((naked)) void MicantProbeUserInvalidOpcodeEntry() {
    __asm__ __volatile__("pushq $0\npushq $6\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserDivideEntry() {
    __asm__ __volatile__("pushq $0\npushq $0\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserOverflowEntry() {
    __asm__ __volatile__("pushq $0\npushq $4\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserBoundsEntry() {
    // #BR does not push a hardware error code.
    __asm__ __volatile__("pushq $0\npushq $5\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserErrorCodeEntry() {
    __asm__ __volatile__("pushq $13\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserTaskSegmentEntry() {
    __asm__ __volatile__("pushq $10\njmp MicantProbeUserExceptionEntry\n");
}
__attribute__((naked)) void MicantProbeUserSegmentNotPresentEntry() {
    __asm__ __volatile__("pushq $11\njmp MicantProbeUserExceptionEntry\n");
}
__attribute__((naked)) void MicantProbeUserStackSegmentEntry() {
    __asm__ __volatile__("pushq $12\njmp MicantProbeUserExceptionEntry\n");
}
__attribute__((naked)) void MicantProbeUserAlignmentEntry() {
    __asm__ __volatile__("pushq $17\njmp MicantProbeUserExceptionEntry\n");
}
__attribute__((naked)) void MicantProbeUserControlProtectionEntry() {
    __asm__ __volatile__("pushq $21\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserFloatEntry() {
    __asm__ __volatile__("pushq $0\npushq $16\njmp MicantProbeUserExceptionEntry\n");
}
__attribute__((naked)) void MicantProbeUserSimdFloatEntry() {
    __asm__ __volatile__("pushq $0\npushq $19\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserBreakpointEntry() {
    __asm__ __volatile__("pushq $0\npushq $3\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserSingleStepEntry() {
    __asm__ __volatile__("pushq $0\npushq $1\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserGeneralProtectionEntry() {
    __asm__ __volatile__("pushq $13\njmp MicantProbeUserExceptionEntry\n");
}

__attribute__((naked)) void MicantProbeUserExceptionEntry() {
    __asm__ __volatile__(
        "cld\npushq %rax\npushq %rcx\npushq %rdx\npushq %rbx\npushq %rbp\n"
        "pushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %r12\nmovl MicantAmd64ExtendedSize(%rip), %eax\n"
        "cmpl $512, %eax\njae 6f\nmovl $512, %eax\n6:\n"
        "addq $96, %rax\nsubq %rax, %rsp\nandq $-64, %rsp\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 1f\n"
        "leaq 64(%rsp), %r13\nmovq $0, 512(%r13)\nmovq $0, 520(%r13)\n"
        "movq $0, 528(%r13)\nmovq $0, 536(%r13)\nmovq $0, 544(%r13)\n"
        "movq $0, 552(%r13)\nmovq $0, 560(%r13)\nmovq $0, 568(%r13)\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\nxsave64 (%r13)\njmp 2f\n"
        "1: fxsave64 64(%rsp)\n"
        "2: leaq 128(%r12), %rcx\nmovq %r12, %rdx\nleaq 64(%rsp), %r8\n"
        "movq 120(%r12), %r9\ncallq MicantDispatchBootstrapException\n"
        "testb %al, %al\njnz 3f\n"
        "movq 120(%r12), %rdx\nleaq 128(%r12), %rcx\n"
        "callq MicantProbeUnexpectedException\nud2\n"
        "3: cmpl $0, MicantAmd64XsaveMask(%rip)\nje 4f\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\nxrstor64 64(%rsp)\njmp 5f\n"
        "4: fxrstor64 64(%rsp)\n"
        "5: movq %r12, %rsp\npopq %r15\npopq %r14\npopq %r13\npopq %r12\n"
        "popq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\n"
        "popq %rbp\npopq %rbx\npopq %rdx\npopq %rcx\npopq %rax\n"
        "addq $16, %rsp\niretq\n");
}

__attribute__((naked)) void MicantProbeEnter() {
    __asm__ __volatile__(
        "movb $1, g_ring3ProbeActive(%rip)\n"
        "pushq %rbx\npushq %rbp\npushq %rdi\npushq %rsi\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "subq $160, %rsp\n"
        "movdqu %xmm6, 0(%rsp)\nmovdqu %xmm7, 16(%rsp)\n"
        "movdqu %xmm8, 32(%rsp)\nmovdqu %xmm9, 48(%rsp)\n"
        "movdqu %xmm10, 64(%rsp)\nmovdqu %xmm11, 80(%rsp)\n"
        "movdqu %xmm12, 96(%rsp)\nmovdqu %xmm13, 112(%rsp)\n"
        "movdqu %xmm14, 128(%rsp)\nmovdqu %xmm15, 144(%rsp)\n"
        "movq %rsp, MicantProbeKernelRsp(%rip)\n"
        "movw $0x2b, %ax\nmovw %ax, %ds\nmovw %ax, %es\n"
        "pushq $0x2b\n"
        "movq MicantProbeUserRsp(%rip), %rax\npushq %rax\n"
        // The native time probe must observe clock interrupts while it waits
        // for KUSER tick advancement. Keep isolated privilege probes with IF
        // clear so their expected fault frames remain deterministic.
        "movl $2, %eax\ncmpb $0, g_ring3ClockProbeActive(%rip)\nje 1f\norl $0x200, %eax\n1: pushq %rax\n"
        "pushq $0x33\n"
        "movq MicantProbeUserRip(%rip), %rax\npushq %rax\niretq\n"
    );
}
}

namespace micant::hal::amd64 {


PciBarProbeResult probeQuiescentPciBar(uint16_t bus, uint16_t device,
                                      uint16_t function, uint8_t index,
                                      PciBarInfo& bar) noexcept {
    bar = {};
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    if (!validPciConfigAddress(bus, device, function, 0) || index >= 6)
        return PciBarProbeResult::Invalid;
    PciConfigLockGuard guard;
    auto read = [&](uint16_t offset) noexcept {
        writePciConfigAddress(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                              static_cast<uint8_t>(function), offset);
        return readPciConfigData();
    };
    auto write = [&](uint16_t offset, uint32_t value) noexcept {
        writePciConfigAddress(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                              static_cast<uint8_t>(function), offset);
        writePciConfigData(value);
    };
    if ((read(0) & 0xffffu) == 0xffffu) return PciBarProbeResult::Absent;
    if (read(4) & 7u) return PciBarProbeResult::Active;
    const auto header = (read(0x0c) >> 16) & 0x7fu;
    const unsigned count = header == 0 ? 6 : header == 1 ? 2 : 0;
    if (index >= count) return PciBarProbeResult::Invalid;
    for (uint8_t i = 0; i < index; ++i) {
        const uint32_t value = read(static_cast<uint16_t>(0x10 + i * 4));
        if (!(value & 1u) && ((value >> 1) & 3u) == 2) {
            if (i + 1 == index) return PciBarProbeResult::Invalid;
            ++i;
        }
    }
    const uint16_t offset = static_cast<uint16_t>(0x10 + index * 4);
    const uint32_t low = read(offset);
    const bool io = low & 1u;
    const uint32_t memoryType = (low >> 1) & 3u;
    if (!io && memoryType != 0 && memoryType != 2) return PciBarProbeResult::Invalid;
    const bool wide = !io && memoryType == 2;
    if (wide && index + 1 >= count) return PciBarProbeResult::Invalid;
    const uint32_t high = wide ? read(offset + 4) : 0;
    write(offset, UINT32_MAX);
    if (wide) write(offset + 4, UINT32_MAX);
    const uint32_t maskLow = read(offset);
    const uint32_t maskHigh = wide ? read(offset + 4) : 0;
    if (wide) write(offset + 4, high);
    write(offset, low);
    if (read(offset) != low || (wide && read(offset + 4) != high))
        return PciBarProbeResult::RestoreFailed;
    const uint32_t addressMask = io ? ~uint32_t{3} : ~uint32_t{15};
    const uint32_t cleanMask = maskLow & addressMask;
    if (!cleanMask && (!wide || !maskHigh)) return PciBarProbeResult::Absent;
    bar.address = static_cast<uint64_t>(low & addressMask) | (uint64_t{high} << 32);
    bar.ioSpace = io;
    if (wide) {
        const uint64_t mask = (uint64_t{maskHigh} << 32) | cleanMask;
        bar.length = ~mask + 1;
    } else {
        bar.length = static_cast<uint32_t>(~cleanMask + 1u);
    }
    if (!bar.length || (bar.length & (bar.length - 1)) ||
        (bar.address & (bar.length - 1))) return PciBarProbeResult::Invalid;
    return PciBarProbeResult::Success;
#else
    (void)bus; (void)device; (void)function; (void)index;
    return PciBarProbeResult::Invalid;
#endif
}

PciConfigAccessResult readPciConfig32(uint16_t bus, uint16_t device,
                                     uint16_t function, uint16_t registerOffset,
                                     uint32_t& value) noexcept {
    if (!validPciConfigAddress(bus, device, function, registerOffset))
        return PciConfigAccessResult::InvalidParameter;
#if defined(__x86_64__) || defined(_M_X64)
    PciConfigLockGuard guard;
    writePciConfigAddress(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                          static_cast<uint8_t>(function), static_cast<uint8_t>(registerOffset));
    value = readPciConfigData();
    return PciConfigAccessResult::Success;
#else
    (void)value;
    return PciConfigAccessResult::UnsupportedPlatform;
#endif
}

PciConfigAccessResult writePciConfig32(uint16_t bus, uint16_t device,
                                      uint16_t function, uint16_t registerOffset,
                                      uint32_t value) noexcept {
    if (!validPciConfigAddress(bus, device, function, registerOffset))
        return PciConfigAccessResult::InvalidParameter;
#if defined(__x86_64__) || defined(_M_X64)
    PciConfigLockGuard guard;
    writePciConfigAddress(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                          static_cast<uint8_t>(function), static_cast<uint8_t>(registerOffset));
    writePciConfigData(value);
    return PciConfigAccessResult::Success;
#else
    (void)value;
    return PciConfigAccessResult::UnsupportedPlatform;
#endif
}

bool connectBootstrapMsiInterrupt(BootstrapMsiRoutine routine, void* context,
                                  BootstrapMsiMessage& message) noexcept {
    message = {};
    if (!routine || !context || !g_schedulerClockActive || !g_timerApic.available() ||
        (cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE) & (uint64_t{1} << 10)) || !g_nextBootstrapMsiToken)
        return false;
    Idtr active{};
    __asm__ __volatile__("sidt %0" : "=m"(active));
    if (active.base != reinterpret_cast<uintptr_t>(g_idt) || active.limit != sizeof(g_idt) - 1)
        return false;
    const uint64_t apicBase = cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE);
    if (!(apicBase & (uint64_t{1} << 11))) return false;
    const uint32_t localApicId = g_timerApic.read(0x20 / 4) >> 24;
    if (localApicId > UINT8_MAX) return false;
    const uint64_t physicalBase = apicBase & 0x0000000ffffff000ULL;
    if (!physicalBase) return false;

    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (g_bootstrapMsiToken) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    g_savedBootstrapMsiGate = g_idt[kBootstrapMsiVector];
    const uint64_t handler = reinterpret_cast<uint64_t>(MicantBootstrapMsiInterrupt);
    g_idt[kBootstrapMsiVector] = InterruptGate{
        static_cast<uint16_t>(handler), cpu::SELECTOR_KCODE64, 0, 0x8e,
        static_cast<uint16_t>(handler >> 16), static_cast<uint32_t>(handler >> 32), 0};
    g_bootstrapMsiRoutine = routine;
    g_bootstrapMsiContext = context;
    g_bootstrapMsiToken = g_nextBootstrapMsiToken++;
    message.address = (physicalBase & 0x0000000ffff00000ULL) |
        (uint64_t{localApicId} << 12);
    message.data = kBootstrapMsiVector;
    message.vector = kBootstrapMsiVector;
    message.token = g_bootstrapMsiToken;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return true;
}

bool disconnectBootstrapMsiInterrupt(uint64_t token) noexcept {
    if (!token) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (g_bootstrapMsiToken != token) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    g_bootstrapMsiRoutine = nullptr;
    g_bootstrapMsiContext = nullptr;
    g_idt[kBootstrapMsiVector] = g_savedBootstrapMsiGate;
    g_savedBootstrapMsiGate = {};
    g_bootstrapMsiToken = 0;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return true;
}

void setBootstrapClockIrqlMask(uint8_t irql) noexcept {
    if (!g_clockIrqlMaskReady) return;
    const uint64_t priority = detail::bootstrapHardwarePriority(irql);
    __asm__ __volatile__("movq %0, %%cr8" : : "r"(priority) : "memory");
}

bool prepareBootstrapPs2Interrupt(uint32_t& gsi) noexcept {
    if (!g_schedulerClockActive || !g_timerApic.available()) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    if (flags & (uint64_t{1} << 9)) return false;
    bool low{}, level{};
    uint32_t resolved{};
    if (!resolveBootstrapIsaInterrupt(12, resolved, low, level)) return false;
    const uint32_t rawId = g_timerApic.read(0x20 / 4);
    const uint32_t id = (cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE) & (1u << 10)) ? rawId : rawId >> 24;
    if (id > 255 || !prepareBootstrapIoApicRoute(resolved, 0x21, static_cast<uint8_t>(id), low, level)) return false;
    const uint64_t handler = reinterpret_cast<uint64_t>(MicantBootstrapPs2Interrupt);
    g_idt[0x21] = InterruptGate{static_cast<uint16_t>(handler), cpu::SELECTOR_KCODE64, 0, 0x8e,
        static_cast<uint16_t>(handler >> 16), static_cast<uint32_t>(handler >> 32), 0};
    gsi = resolved;
    return true;
}

bool bootstrapSyscallGsActive() noexcept {
    return g_dispatch && cpu::cpuRdmsr(cpu::MSR_GS_BASE) == reinterpret_cast<uintptr_t>(&g_syscallCpu);
}

void saveBootstrapThreadSegments(ThreadSegmentState& state, bool syscallGsActive) noexcept {
    __asm__ __volatile__("movw %%ds, %0; movw %%es, %1; movw %%fs, %2; movw %%gs, %3"
                         : "=rm"(state.ds), "=rm"(state.es), "=rm"(state.fs), "=rm"(state.gs));
    state.fsBase = cpu::cpuRdmsr(cpu::MSR_FS_BASE);
    state.userGsBase = cpu::cpuRdmsr(syscallGsActive ? cpu::MSR_KERNEL_GS_BASE : cpu::MSR_GS_BASE);
    state.saved = true;
}

void restoreBootstrapThreadSegments(const ThreadSegmentState& state) noexcept {
    // KiSwapContext holds IF clear. Loading GS can replace its active base;
    // keep that base valid until setBootstrapThreadEntryStack installs the
    // incoming thread's user/syscall GS pair immediately afterward.
    const uint64_t currentGsBase = cpu::cpuRdmsr(cpu::MSR_GS_BASE);
    __asm__ __volatile__("movw %0, %%ds; movw %1, %%es; movw %2, %%fs; movw %3, %%gs"
                         : : "rm"(state.ds), "rm"(state.es), "rm"(state.fs), "rm"(state.gs) : "memory");
    cpu::cpuWrmsr(cpu::MSR_FS_BASE, state.saved ? state.fsBase : 0);
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, currentGsBase);
}

bool bootstrapStackHasHeadroom(uintptr_t stackPointer, uintptr_t threadBase,
                               uintptr_t threadTop, size_t required) noexcept {
    uintptr_t base = threadBase;
    uintptr_t top = threadTop;
    if (stackPointer >= MicantBootstrapUserInterruptStackBase &&
        stackPointer < MicantBootstrapUserInterruptStackTop) {
        base = MicantBootstrapUserInterruptStackBase;
        top = MicantBootstrapUserInterruptStackTop;
    }
    else if (stackPointer >= MicantBootstrapSyscallStackBase &&
             stackPointer < MicantBootstrapSyscallStackTop) {
        base = MicantBootstrapSyscallStackBase;
        top = MicantBootstrapSyscallStackTop;
    }
    return base && top > base && stackPointer >= base && stackPointer < top &&
        stackPointer - base >= required;
}

void setBootstrapThreadEntryStack(uintptr_t stackTop, uintptr_t userTeb, bool syscallGsActive,
                                  uintptr_t stackBase, uintptr_t executionStackTop) noexcept {
    // A clock IRQ may switch context while its own XSAVE frame is resident.
    MicantBootstrapInterruptStackFloor = stackBase ? stackBase + 4096 + 2 * MicantAmd64ExtendedSize : 0;
    MicantBootstrapExecutionStackTop = executionStackTop;
    const uintptr_t userEntryBase = stackTop ? stackBase : reinterpret_cast<uintptr_t>(g_kernelStack);
    MicantBootstrapUserInterruptStackBase = userEntryBase;
    MicantBootstrapUserInterruptStackTop = stackTop ? stackTop :
        reinterpret_cast<uintptr_t>(g_kernelStack + sizeof(g_kernelStack));
    MicantBootstrapUserInterruptStackFloor = userEntryBase + 4096 + 2 * MicantAmd64ExtendedSize;
    MicantBootstrapSyscallStackBase = stackTop ? stackBase : reinterpret_cast<uintptr_t>(g_syscallStack);
    MicantBootstrapSyscallStackTop = stackTop ? stackTop :
        reinterpret_cast<uintptr_t>(g_syscallStack + sizeof(g_syscallStack));
    MicantBootstrapSyscallStackFloor = MicantBootstrapSyscallStackBase + 4096 + 2 * MicantAmd64ExtendedSize;
    g_syscallCpu.kernelRsp = stackTop ? stackTop :
        reinterpret_cast<uintptr_t>(g_syscallStack + sizeof(g_syscallStack));
    auto& hardware = cpu::CpuHardwareEngine::get();
    if (hardware.isInitialized()) {
        hardware.setKernelStack(stackTop ? stackTop :
            reinterpret_cast<uintptr_t>(g_kernelStack + sizeof(g_kernelStack)));
    }
    if (g_dispatch) {
        const uintptr_t kernelGs = reinterpret_cast<uintptr_t>(&g_syscallCpu);
        cpu::cpuWrmsr(cpu::MSR_GS_BASE, syscallGsActive ? kernelGs : userTeb);
        cpu::cpuWrmsr(cpu::MSR_KERNEL_GS_BASE, syscallGsActive ? userTeb : kernelGs);
    } else {
        cpu::cpuWrmsr(cpu::MSR_GS_BASE, userTeb);
    }
}

uintptr_t bootstrapThreadSyscallStack() noexcept { return g_syscallCpu.kernelRsp; }

extern "C" __attribute__((naked, noreturn)) void MicantEnterBootstrapUserThread(
    uintptr_t, uintptr_t, bool, uintptr_t, uintptr_t) noexcept {
    __asm__ __volatile__(
        "movq 40(%rsp), %r11\nmovw $0x2b, %ax\nmovw %ax, %ds\nmovw %ax, %es\n"
        "pushq $0x2b\npushq %rdx\nmovq $2, %rax\ntestb %r8b, %r8b\njz 1f\n"
        "orq $0x200, %rax\n1: pushq %rax\npushq $0x33\npushq %rcx\n"
        // Pass even a null start routine through to RtlUserThreadStart. It
        // must fault as a user callback, not accidentally call the entry
        // trampoline recursively with RCX still holding the trampoline VA.
        "movq %r9, %rcx\nmovq %r11, %rdx\n"
        "xorq %rax, %rax\nxorq %rbx, %rbx\nxorq %rbp, %rbp\n"
        "xorq %rsi, %rsi\nxorq %rdi, %rdi\nxorq %r8, %r8\nxorq %r9, %r9\n"
        "xorq %r10, %r10\nxorq %r11, %r11\nxorq %r12, %r12\nxorq %r13, %r13\n"
        "xorq %r14, %r14\nxorq %r15, %r15\niretq\n");
}

[[noreturn]] void enterBootstrapUserThread(uintptr_t entry, uintptr_t userStack, bool interruptible,
                                            uintptr_t startRoutine, uintptr_t startArgument) noexcept {
    // No scheduling transition may invalidate the idle-thread invariant
    // between this check and iret; iret installs the requested user IF.
    __asm__ __volatile__("cli" : : : "memory");
    const auto* thread = ke::KeGetCurrentThread();
    const auto* idle = ke::g_CurrentCpu.idleThread;
    if (!g_dispatch || (interruptible &&
        (!g_schedulerClockActive || !MicantTimerPreemptionEnabled || !idle || idle == thread ||
         idle->readyQueued || (idle->state != ke::ThreadState::Initialized &&
                               idle->state != ke::ThreadState::Ready))) ||
        !thread || !thread->owningProcess || !thread->kernelStackTop ||
        g_syscallCpu.kernelRsp != thread->kernelStackTop ||
        entry < um::MM_LOWEST_USER_ADDRESS || entry > um::MM_HIGHEST_USER_ADDRESS ||
        userStack < um::MM_LOWEST_USER_ADDRESS + 8 || userStack > um::MM_HIGHEST_USER_ADDRESS ||
        (thread->tebAddress && (thread->tebAddress < um::MM_LOWEST_USER_ADDRESS ||
                                thread->tebAddress > um::MM_HIGHEST_USER_ADDRESS)))
        __builtin_trap();
    // The first transition to CPL3 can occur without a context switch. Publish
    // this thread's kernel stack bounds to timer/syscall entry before user
    // interrupts become possible, using the same path as KiSwapContext.
    setBootstrapThreadEntryStack(thread->kernelStackTop, thread->tebAddress, false,
                                 thread->kernelStackBase, thread->kernelExecutionStackTop);
    // The user process may unmap or reprotect the entry, stack or TEB after
    // creation. Keep those races on the ordinary CPL3 exception path; only
    // corrupted kernel bookkeeping and non-user addresses are invariants.
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, thread->tebAddress);
    MicantEnterBootstrapUserThread(entry, userStack, interruptible, startRoutine, startArgument);
}

bool initializeBootstrapAddressSpace(void (*log)(const char*) noexcept) noexcept {
    g_log = log;
    if (g_addressSpaceReady) {
        return true;
    }
    uint64_t cr3{};
    uint64_t cr4{};
    __asm__ __volatile__("movq %%cr3, %0; movq %%cr4, %1" : "=r"(cr3), "=r"(cr4));
    if (cr4 & (uint64_t{1} << 12)) {
        return false;
    }
    const auto* oldRoot = reinterpret_cast<const uint64_t*>(cr3 & kPhysicalMask);
    if (oldRoot[128] || oldRoot[129] ||
        walk(cr3, reinterpret_cast<uint64_t>(g_pml4)) !=
            reinterpret_cast<uint64_t>(g_pml4)) {
        return false;
    }
    for (size_t index = 0; index < 512; ++index) {
        g_pml4[index] = oldRoot[index] & ~kUser;
    }
    cpu::cpuWrmsr(cpu::MSR_EFER, cpu::cpuRdmsr(cpu::MSR_EFER) | cpu::EFER_NXE);
    // CR3 reload alone can retain firmware global translations.
    __asm__ __volatile__("movq %0, %%cr4" : : "r"(cr4 & ~uint64_t{128}) : "memory");
    __asm__ __volatile__("movq %0, %%cr3" : : "r"(g_pml4) : "memory");
    __asm__ __volatile__("movq %0, %%cr4" : : "r"(cr4) : "memory");
    g_addressSpaceReady = true;
    return true;
}

ProcessPagingRoot::~ProcessPagingRoot() {
    if (pfn || next) __builtin_trap();
}

static void reclaimUnusedSharedRootSlots() noexcept {
    if (g_processPagingRoots) return;
    auto& database = mm::PfnDatabase::get();
    for (size_t index = 0; index < 2; ++index) {
        if (!g_ownedSharedRootSlots[index]) continue;
        uint64_t& entry = g_pml4[129 + index];
        if (!entry) { g_ownedSharedRootSlots[index] = false; continue; }
        auto* table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
        if (!emptyTable(table)) continue;
        const uint64_t pfn = (entry & kPhysicalMask) >> mm::PageShift4KB;
        entry = 0;
        invalidatePage((129 + index) << 39);
        database.freePage(pfn);
        g_ownedSharedRootSlots[index] = false;
    }
}

static bool createProcessPagingRootImpl(ProcessPagingRoot& root, int failAfter) noexcept {
    if (!bootstrapPagingContext() || root.pfn || root.next) return false;
    uint64_t cr4{};
    __asm__ __volatile__("movq %%cr4, %0" : "=r"(cr4));
    if (cr4 & (uint64_t{1} << 17)) return false; // PCID is not supported here.
    auto& database = mm::PfnDatabase::get();
    for (size_t index = 0; index < 2; ++index) {
        uint64_t& entry = g_pml4[129 + index];
        if (!entry) {
            const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
            if (!pfn) { reclaimUnusedSharedRootSlots(); return false; }
            if (failAfter > 0) --failAfter;
            const uint64_t physical = pfn << mm::PageShift4KB;
            if (physical > kPhysicalMask || !mappedRange(physical, 4096, false, true, false) ||
                walk(reinterpret_cast<uint64_t>(g_pml4), physical) != physical) {
                database.freePage(pfn);
                reclaimUnusedSharedRootSlots();
                return false;
            }
            database.zeroPage(pfn);
            entry = physical | kPresent | kWritable | kOwnedTable;
            g_ownedSharedRootSlots[index] = true;
        }
        if (!(entry & kPresent) || !(entry & kWritable) || !(entry & kOwnedTable) ||
            (entry & (kUser | kNoExecute | 128))) {
            reclaimUnusedSharedRootSlots();
            return false;
        }
    }
    const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
    if (!pfn) { reclaimUnusedSharedRootSlots(); return false; }
    const uint64_t physical = pfn << mm::PageShift4KB;
    if (physical > kPhysicalMask || !mappedRange(physical, 4096, false, true, false) ||
        walk(reinterpret_cast<uint64_t>(g_pml4), physical) != physical) {
        database.freePage(pfn);
        reclaimUnusedSharedRootSlots();
        return false;
    }
    database.zeroPage(pfn);
    auto* table = reinterpret_cast<uint64_t*>(physical);
    for (size_t i = 0; i < 512; ++i) {
        table[i] = i == 128 ? 0 : g_pml4[i] & ~kUser;
        // PML4[0] carries the read-only KUSER_SHARED_DATA alias at 0x7FFE0000.
        // Only that leaf and the clock shared page tables beneath it have U/S
        // set; all other low identity mappings remain supervisor-only.
        if (i == 0 && g_kuserSharedMappingReady) table[i] |= kUser;
    }
    root.pfn = pfn;
    root.next = g_processPagingRoots;
    g_processPagingRoots = &root;
    return true;
}

bool createProcessPagingRoot(ProcessPagingRoot& root) noexcept {
    return createProcessPagingRootImpl(root, -1);
}

bool verifyProcessPagingRootRollback() noexcept {
    if (!bootstrapPagingContext() || g_processPagingRoots || g_pml4[129] || g_pml4[130])
        return false;
    const size_t active = mm::PfnDatabase::get().getActiveCount();
    for (int failure = 0; failure < 3; ++failure) {
        ProcessPagingRoot root;
        if (createProcessPagingRootImpl(root, failure) || root.pfn || root.next ||
            g_pml4[129] || g_pml4[130] || mm::PfnDatabase::get().getActiveCount() != active)
            return false;
    }
    return true;
}

bool switchProcessPagingRoot(const ProcessPagingRoot* root) noexcept {
    uint64_t flags{}, cr3{}, cr4{};
    __asm__ __volatile__("pushfq; popq %0; movq %%cr3, %1; movq %%cr4, %2"
                         : "=r"(flags), "=r"(cr3), "=r"(cr4) : : "memory");
    if (!g_addressSpaceReady || (flags & (uint64_t{1} << 9)) || (cr4 & (uint64_t{1} << 17)))
        return false;
    bool currentKnown = (cr3 & kPhysicalMask) == reinterpret_cast<uint64_t>(g_pml4);
    bool targetKnown = root == nullptr;
    for (const auto* item = g_processPagingRoots; item; item = item->next) {
        if ((item->pfn << mm::PageShift4KB) == (cr3 & kPhysicalMask)) currentKnown = true;
        if (item == root) targetKnown = true;
    }
    if (!currentKnown || !targetKnown) return false;
    const uint64_t target = root ? root->pfn << mm::PageShift4KB : reinterpret_cast<uint64_t>(g_pml4);
    __asm__ __volatile__("movq %0, %%cr3" : : "r"(target) : "memory");
    return true;
}

bool switchProcessPagingRootByPhysical(uintptr_t physicalRoot) noexcept {
    if (physicalRoot == reinterpret_cast<uintptr_t>(g_pml4))
        return switchProcessPagingRoot(nullptr);
    for (const auto* root = g_processPagingRoots; root; root = root->next) {
        if ((root->pfn << mm::PageShift4KB) == physicalRoot)
            return switchProcessPagingRoot(root);
    }
    return false;
}

bool destroyProcessPagingRoot(ProcessPagingRoot& root) noexcept {
    if (!bootstrapPagingContext() || !root.pfn) return false;
    if (reinterpret_cast<uint64_t>(activePagingRoot()) == (root.pfn << mm::PageShift4KB)) return false;
    auto** link = &g_processPagingRoots;
    while (*link && *link != &root) link = &(*link)->next;
    if (!*link) return false;
    auto* table = reinterpret_cast<const uint64_t*>(root.pfn << mm::PageShift4KB);
    if (table[128]) return false;
    *link = root.next;
    const uint64_t pfn = root.pfn;
    root.pfn = 0;
    root.next = nullptr;
    mm::PfnDatabase::get().freePage(pfn);
    reclaimUnusedSharedRootSlots();
    return true;
}

uint64_t translateBootstrapAddress(const void* address) noexcept {
    if (!g_addressSpaceReady) {
        return 0;
    }
    return walk(reinterpret_cast<uint64_t>(g_pml4),
                reinterpret_cast<uint64_t>(address));
}

uint64_t translateBootstrapDmaRange(const void* address, size_t size) noexcept {
    if (!g_addressSpaceReady || !address || !size) return 0;
    const uint64_t virtualStart = reinterpret_cast<uintptr_t>(address);
    if (size - 1 > UINT64_MAX - virtualStart) return 0;
    const uint64_t virtualEnd = virtualStart + size - 1;
    const uint64_t virtualRegion = virtualStart >> 47;
    if ((virtualRegion != 0 && virtualRegion != 0x1FFFF) ||
        (virtualEnd >> 47) != virtualRegion) return 0;

    const uint64_t physicalStart = translateBootstrapAddress(address);
    if (!physicalStart || size - 1 > UINT64_MAX - physicalStart) return 0;
    const uint64_t physicalEnd = physicalStart + size - 1;
    if ((physicalEnd & ~uint64_t{4095}) > kPhysicalMask) return 0;

    uint64_t virtualCursor = virtualStart;
    size_t bytesChecked = 0;
    while (bytesChecked < size) {
        const uint64_t physical = translateBootstrapAddress(
            reinterpret_cast<const void*>(static_cast<uintptr_t>(virtualCursor)));
        if (!physical || physical != physicalStart + bytesChecked) return 0;

        const size_t pageOffset = static_cast<size_t>(virtualCursor & 4095);
        const size_t pageBytes = 4096 - pageOffset;
        const size_t remaining = size - bytesChecked;
        const size_t step = remaining < pageBytes ? remaining : pageBytes;
        bytesChecked += step;
        if (bytesChecked < size) virtualCursor += step;
    }
    return physicalStart;
}

bool bootstrapUserRange(uint64_t address, size_t size, bool writable,
                        bool executable) noexcept {
    if (!g_addressSpaceReady || size == 0) return false;
    if (address >= micant::UserSharedDataAddress &&
        address <= micant::UserSharedDataAddress + 4096 &&
        size <= 4096 - (address - micant::UserSharedDataAddress)) {
        if (writable || executable) return false;
        return mappedRange(address, size, true, false, false);
    }
    if (address < kUserBase ||
        address >= kUserBase + (uint64_t{1} << 39) ||
        size > kUserBase + (uint64_t{1} << 39) - address) {
        return false;
    }
    return mappedRange(address, size, true, writable, executable);
}

bool bootstrapKernelRange(uint64_t address, size_t size, bool writable) noexcept {
    return mappedRange(address, size, false, writable, false);
}

bool mapBootstrapManagedPage(void* context, uintptr_t address, uint64_t pfn,
                             uint32_t protection) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (address & mm::PageMask4KB) || !pfn ||
        pfn > (kPhysicalMask >> mm::PageShift4KB) ||
        (protection != mm::PAGE_READWRITE && protection != mm::PAGE_READONLY &&
         protection != mm::PAGE_EXECUTE_READ && protection != mm::PAGE_EXECUTE_READWRITE &&
         protection != mm::PAGE_NOACCESS)) {
        return false;
    }
    if (!mapPage(address, pfn << mm::PageShift4KB, kUser |
                   ((protection == mm::PAGE_EXECUTE_READ || protection == mm::PAGE_EXECUTE_READWRITE) ? 0 : kNoExecute) |
                   ((protection == mm::PAGE_READWRITE || protection == mm::PAGE_EXECUTE_READWRITE) ? kWritable : 0))) return false;
    if (protection == mm::PAGE_NOACCESS &&
        !mapBootstrapProtectManagedPage(context, address, protection)) {
        if (!unmapPage(address, true)) __builtin_trap();
        return false;
    }
    return true;
}

static bool mapBootstrapManagedViewImpl(void* context, uintptr_t address, const uint64_t* pfns,
                                        size_t count, uint32_t protection, int failAfter) noexcept {
    if (!pagingContextMatches(context) || !pfns || !count ||
        address < BootstrapManagedBase || (address & mm::PageMask4KB) ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        count > (BootstrapManagedSize - (address - BootstrapManagedBase)) / mm::PageSize4KB ||
        (protection != mm::PAGE_READONLY && protection != mm::PAGE_READWRITE)) return false;

    // This 16 MiB aligned window needs at most eight PTs, one PD and one
    // PDPT. Upper levels are installed without any present leaf PTEs.
    uint64_t* createdEntries[16]{};
    uint64_t createdPfns[16]{};
    size_t createdCount{};
    auto& database = mm::PfnDatabase::get();
    bool prepared = true;
    for (size_t i = 0; i < count && prepared; ++i) {
        if (!pfns[i] || pfns[i] > (kPhysicalMask >> mm::PageShift4KB) ||
            isProtectedIommuPhysicalPage(pfns[i] << mm::PageShift4KB)) {
            prepared = false;
            break;
        }
        const uintptr_t va = address + i * mm::PageSize4KB;
        uint64_t* table = activePagingRoot();
        for (int shift = 39; shift > 12; shift -= 9) {
            uint64_t& entry = table[(va >> shift) & 511];
            if (!entry) {
                if (createdCount == 16) { prepared = false; break; }
                const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
                if (!pfn) { prepared = false; break; }
                if (failAfter > 0) --failAfter;
                const uint64_t physical = pfn << 12;
                if (physical > kPhysicalMask || !mappedRange(physical, 4096, false, true, false) ||
                    walk(reinterpret_cast<uint64_t>(activePagingRoot()), physical) != physical) {
                    database.freePage(pfn);
                    prepared = false;
                    break;
                }
                database.zeroPage(pfn);
                entry = physical | kPresent | kWritable | kOwnedTable | kUser;
                createdEntries[createdCount] = &entry;
                createdPfns[createdCount++] = pfn;
            }
            if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) ||
                !(entry & kWritable) || !(entry & kUser) || (entry & kNoExecute)) {
                prepared = false;
                break;
            }
            table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
        }
        if (prepared && table[(va >> 12) & 511]) prepared = false;
    }
    if (!prepared) {
        while (createdCount) {
            --createdCount;
            *createdEntries[createdCount] = 0;
            invalidatePage(address);
            database.freePage(createdPfns[createdCount]);
        }
        return false;
    }
    // Interrupts are disabled and only the BSP is running. No leaf can change
    // between validation and publication, and this phase cannot fail.
    for (size_t i = 0; i < count; ++i) {
        const uintptr_t va = address + i * mm::PageSize4KB;
        uint64_t* table = activePagingRoot();
        for (int shift = 39; shift > 12; shift -= 9)
            table = reinterpret_cast<uint64_t*>(table[(va >> shift) & 511] & kPhysicalMask);
        table[(va >> 12) & 511] = mm::PageTableEntry::create(
            pfns[i], protection == mm::PAGE_READWRITE, true, true).value;
        invalidatePage(va);
    }
    return true;
}

bool mapBootstrapManagedView(void* context, uintptr_t address, const uint64_t* pfns,
                             size_t count, uint32_t protection) noexcept {
    return mapBootstrapManagedViewImpl(context, address, pfns, count, protection, -1);
}

bool unmapBootstrapManagedView(void* context, uintptr_t address, size_t count) noexcept {
    if (!pagingContextMatches(context) || !count || (address & mm::PageMask4KB) ||
        address < BootstrapManagedBase || address - BootstrapManagedBase >= BootstrapManagedSize ||
        count > (BootstrapManagedSize - (address - BootstrapManagedBase)) / mm::PageSize4KB)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const uintptr_t va = address + i * mm::PageSize4KB;
        const uint64_t* table = activePagingRoot();
        for (int shift = 39; shift > 12; shift -= 9) {
            const uint64_t entry = table[(va >> shift) & 511];
            if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) || !(entry & kUser))
                return false;
            table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
        }
        if (!(table[(va >> 12) & 511] & kPresent)) return false;
    }
    // No interrupt or concurrent CPU can alter the validated page tables.
    for (size_t i = 0; i < count; ++i) {
        if (!unmapPage(address + i * mm::PageSize4KB, true)) __builtin_trap();
    }
    return true;
}

bool verifyBootstrapManagedViewRollback() noexcept {
    auto& database = mm::PfnDatabase::get();
    const size_t active = database.getActiveCount();
    const uint64_t pfn = database.allocatePage();
    if (!pfn) return false;
    const size_t held = database.getActiveCount();
    const uint64_t pfns[]{pfn, 0};
    bool ok = true;
    for (int failAfter = 0; failAfter <= 3; ++failAfter) {
        if (mapBootstrapManagedViewImpl(nullptr, BootstrapManagedBase, pfns, 2,
                                        mm::PAGE_READWRITE, failAfter) ||
            bootstrapUserRange(BootstrapManagedBase, mm::PageSize4KB, false) ||
            database.getActiveCount() != held) {
            ok = false;
            break;
        }
    }
    if (ok) {
        const uintptr_t occupied = BootstrapManagedBase + mm::PageSize4KB;
        const uint64_t pair[]{pfn, pfn};
        if (!mapBootstrapManagedPage(nullptr, occupied, pfn, mm::PAGE_READWRITE)) {
            ok = false;
        } else {
            const size_t mapped = database.getActiveCount();
            if (unmapBootstrapManagedView(nullptr, BootstrapManagedBase, 2) ||
                mapBootstrapManagedView(nullptr, BootstrapManagedBase, pair, 2, mm::PAGE_READWRITE) ||
                bootstrapUserRange(BootstrapManagedBase, mm::PageSize4KB, false) ||
                !bootstrapUserRange(occupied, mm::PageSize4KB, true) ||
                database.getActiveCount() != mapped) ok = false;
            if (!unmapBootstrapManagedPage(nullptr, occupied)) ok = false;
        }
    }
    database.freePage(pfn);
    return ok && database.getActiveCount() == active;
}

bool unmapBootstrapManagedPage(void* context, uintptr_t address) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (address & mm::PageMask4KB)) {
        return false;
    }
    return unmapPage(address, true);
}

bool replaceBootstrapManagedPage(void* context, uintptr_t address, uint64_t expectedPfn,
                                 uint64_t replacementPfn, uint32_t protection) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize || (address & mm::PageMask4KB) ||
        !expectedPfn || !replacementPfn || replacementPfn > (kPhysicalMask >> mm::PageShift4KB) ||
        (protection != mm::PAGE_READONLY && protection != mm::PAGE_READWRITE &&
         protection != mm::PAGE_EXECUTE_READ && protection != mm::PAGE_EXECUTE_READWRITE &&
         protection != mm::PAGE_NOACCESS) ||
        isProtectedIommuPhysicalPage(replacementPfn << mm::PageShift4KB)) return false;
    uint64_t* table = activePagingRoot();
    for (int shift = 39; shift > 12; shift -= 9) {
        const uint64_t entry = table[(address >> shift) & 511];
        if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) ||
            !(entry & kUser) || !(entry & kWritable) || (entry & kNoExecute)) return false;
        table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
    }
    uint64_t& leaf = table[(address >> 12) & 511];
    if (!(leaf & (kPresent | kNoAccessLeaf)) || !(leaf & kUser) || (leaf & kGuardLeaf) ||
        ((leaf & kPhysicalMask) >> mm::PageShift4KB) != expectedPfn) return false;
    const bool writable = protection == mm::PAGE_READWRITE || protection == mm::PAGE_EXECUTE_READWRITE;
    const bool executable = protection == mm::PAGE_EXECUTE_READ || protection == mm::PAGE_EXECUTE_READWRITE;
    const auto pte = mm::PageTableEntry::create(replacementPfn, true, writable, !executable);
    leaf = protection == mm::PAGE_NOACCESS
        ? ((pte.value & ~(kPresent | kWritable)) | kNoAccessLeaf | kNoExecute)
        : pte.value;
    invalidatePage(address);
    return true;
}

bool mapBootstrapProtectManagedPage(void* context, uintptr_t address,
                                    uint32_t protection) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (address & mm::PageMask4KB) ||
        (protection != mm::PAGE_READONLY && protection != mm::PAGE_READWRITE &&
         protection != mm::PAGE_EXECUTE_READ && protection != mm::PAGE_EXECUTE_READWRITE &&
         protection != mm::PAGE_NOACCESS)) {
        return false;
    }
    uint64_t* table = activePagingRoot();
    for (int shift = 39; shift > 12; shift -= 9) {
        const uint64_t entry = table[(address >> shift) & 511];
        if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) ||
            !(entry & kUser) || !(entry & kWritable) || (entry & kNoExecute)) {
            return false;
        }
        table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
    }
    uint64_t& leaf = table[(address >> 12) & 511];
    if (!(leaf & kUser) || (leaf & kGuardLeaf) ||
        !(leaf & (kPresent | kNoAccessLeaf))) return false;
    if (protection == mm::PAGE_NOACCESS) {
        leaf = (leaf & ~(kPresent | kWritable)) | kNoAccessLeaf | kNoExecute;
        invalidatePage(address);
        return true;
    }
    leaf = (leaf | kPresent) & ~kNoAccessLeaf;
    leaf = (protection == mm::PAGE_READWRITE || protection == mm::PAGE_EXECUTE_READWRITE)
        ? leaf | kWritable : leaf & ~kWritable;
    leaf = (protection == mm::PAGE_EXECUTE_READ || protection == mm::PAGE_EXECUTE_READWRITE)
        ? leaf & ~kNoExecute : leaf | kNoExecute;
    invalidatePage(address);
    return true;
}

bool mapBootstrapGuardManagedPage(void* context, uintptr_t address, bool guarded) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize || (address & mm::PageMask4KB)) return false;
    uint64_t* table = activePagingRoot();
    for (int shift = 39; shift > 12; shift -= 9) {
        const uint64_t entry = table[(address >> shift) & 511];
        if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) || !(entry & kUser)) return false;
        table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
    }
    uint64_t& leaf = table[(address >> 12) & 511];
    if (!(leaf & kUser) || !(leaf & kWritable)) return false;
    if (guarded) {
        if (!(leaf & kPresent) || (leaf & kGuardLeaf)) return false;
        leaf = (leaf & ~kPresent) | kGuardLeaf;
    } else {
        if ((leaf & kPresent) || !(leaf & kGuardLeaf)) return false;
        leaf = (leaf | kPresent) & ~kGuardLeaf;
    }
    invalidatePage(address);
    return true;
}

bool bootstrapQemuFaultTestsAvailable() noexcept {
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(ecx & (uint32_t{1} << 31))) return false;
    eax = 0x40000000;
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    // QEMU's TCG hypervisor signature; the runner explicitly selects TCG.
    return eax >= 0x40000000 && ebx == 0x54474354 &&
           ecx == 0x43544743 && edx == 0x47435447;
}

bool verifyBootstrapManagedPageFault(uintptr_t address, bool write,
                                     bool present) noexcept {
    if (!bootstrapQemuFaultTestsAvailable() || !bootstrapPagingContext() ||
        address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (write && !present) || (!write && present) || g_managedFaultArmed) return false;

    uint16_t cs{};
    uint64_t cr0{};
    Idtr previous{};
    __asm__ __volatile__("movw %%cs, %0; movq %%cr0, %1; sidt %2"
                         : "=r"(cs), "=r"(cr0), "=m"(previous) : : "memory");
    if (previous.base == reinterpret_cast<uint64_t>(g_managedFaultIdt)) return false;
    for (size_t vector = 0; vector < 256; ++vector) {
        const uint64_t handler = vector == 14
            ? reinterpret_cast<uint64_t>(MicantManagedPageFaultEntry)
            : reinterpret_cast<uint64_t>(MicantProbeFaultEntry);
        g_managedFaultIdt[vector] = {static_cast<uint16_t>(handler), cs, 0, 0x8e,
                                    static_cast<uint16_t>(handler >> 16),
                                    static_cast<uint32_t>(handler >> 32), 0};
    }
    const Idtr idtr{sizeof(g_managedFaultIdt) - 1,
                    reinterpret_cast<uint64_t>(g_managedFaultIdt)};
    g_managedFaultAddress = address;
    g_managedFaultError = write ? 3 : 0;
    g_managedFaultRip = reinterpret_cast<uintptr_t>(write ? MicantManagedWriteFault
                                                          : MicantManagedReadFault);
    g_managedFaultResume = reinterpret_cast<uintptr_t>(write ? MicantManagedWriteResume
                                                             : MicantManagedReadResume);
    g_managedFaultObserved = false;
    g_managedFaultArmed = true;
    __asm__ __volatile__("lidt %0; movq %1, %%cr0"
                         : : "m"(idtr), "r"(cr0 | (uint64_t{1} << 16)) : "memory");
    if (write) MicantManagedWriteProbe(address);
    else MicantManagedReadProbe(address);
    __asm__ __volatile__("movq %0, %%cr0; lidt %1"
                         : : "r"(cr0), "m"(previous) : "memory");
    g_managedFaultArmed = false;
    Idtr restored{};
    __asm__ __volatile__("sidt %0" : "=m"(restored) : : "memory");
    return g_managedFaultObserved && restored.base == previous.base &&
           restored.limit == previous.limit;
}

bool verifyBootstrapPageTables() noexcept {
    if (!bootstrapPagingContext() || g_pml4[130] != 0) return false;
    auto& database = mm::PfnDatabase::get();
    const size_t active = database.getActiveCount();
    const size_t available = database.getFreeCount() + database.getZeroedCount() +
                             database.getStandbyCount();
    const uint64_t originalRoot = g_pml4[0];
    const uint64_t rootAddress = reinterpret_cast<uint64_t>(g_pml4);
    const uint64_t rootTranslation = walk(rootAddress, rootAddress);
    if (!rootTranslation || unmapPage(rootAddress & ~uint64_t{4095}, false) ||
        mapPage(rootAddress & ~uint64_t{4095}, rootAddress, kWritable)) return false;
    const uint64_t first = database.allocatePage();
    const uint64_t second = database.allocatePage();
    if (!first || !second) {
        if (first) database.freePage(first);
        if (second) database.freePage(second);
        return false;
    }
    database.zeroPage(first);
    database.zeroPage(second);
    auto* firstBytes = reinterpret_cast<volatile uint64_t*>(first << 12);
    auto* secondBytes = reinterpret_cast<volatile uint64_t*>(second << 12);
    const bool zeroed = *firstBytes == 0 && firstBytes[511] == 0 &&
                        *secondBytes == 0 && secondBytes[511] == 0;
    *firstBytes = 0x1122334455667788ULL;
    *secondBytes = 0x8877665544332211ULL;
    bool ok = zeroed;
    for (int fail = 0; fail < 3; ++fail) {
        if (mapPage(kTestBase, first << 12, kWritable | kNoExecute, fail) ||
            g_pml4[130] != 0 || database.getActiveCount() != active + 2) ok = false;
    }
    const uint64_t sibling = kTestBase + 4096;
    const uint64_t branch = kTestBase + (uint64_t{1} << 30);
    if (!mapPage(kTestBase, first << 12, kWritable | kNoExecute)) ok = false;
    if (ok) {
        auto* mapped = reinterpret_cast<volatile uint64_t*>(kTestBase);
        if (*mapped != *firstBytes || !bootstrapKernelRange(kTestBase, 4096, true) ||
            bootstrapUserRange(kTestBase, 4096, true) ||
            mapPage(kTestBase, second << 12, kWritable | kNoExecute)) ok = false;
        *mapped = 0xA1B2C3D4E5F60718ULL;
        if (*firstBytes != 0xA1B2C3D4E5F60718ULL) ok = false;
        for (int fail = 0; fail < 2; ++fail) {
            if (mapPage(branch, second << 12, kWritable | kNoExecute, fail) ||
                database.getActiveCount() != active + 5 ||
                *mapped != *firstBytes) ok = false;
        }
        if (!mapPage(branch, second << 12, kWritable | kNoExecute) ||
            database.getActiveCount() != active + 7 ||
            !unmapPage(branch, false) || unmapPage(branch, false) ||
            database.getActiveCount() != active + 5 || *mapped != *firstBytes) ok = false;
        if (!mapPage(sibling, second << 12, kWritable | kNoExecute) ||
            database.getActiveCount() != active + 5 ||
            !unmapPage(kTestBase, false) ||
            database.getActiveCount() != active + 5 ||
            !mapPage(kTestBase, second << 12, kWritable | kNoExecute) ||
            *mapped != *secondBytes) ok = false;
        if (!unmapPage(kTestBase, false) || !unmapPage(sibling, false)) ok = false;
    }
    database.freePage(first);
    database.freePage(second);
    return ok && g_pml4[130] == 0 && g_pml4[0] == originalRoot &&
           walk(rootAddress, rootAddress) == rootTranslation &&
           database.getActiveCount() == active &&
           database.getFreeCount() + database.getZeroedCount() +
               database.getStandbyCount() == available;
}

bool mapBootstrapUserMemory(uint64_t address, const void* backing, size_t size,
                            bool writable, bool executable) noexcept {
    const uint64_t physical = reinterpret_cast<uint64_t>(backing);
    if (!g_addressSpaceReady || !size || (size & 4095) || (address & 4095) ||
        (physical & 4095) || !physical || address < kUserBase ||
        address >= kUserBase + (uint64_t{1} << 39) ||
        size > kUserBase + (uint64_t{1} << 39) - address ||
        physical > kPhysicalMask || size - 1 > kPhysicalMask + 4095 - physical) {
        return false;
    }
    const uint64_t root = reinterpret_cast<uint64_t>(g_pml4);
    for (size_t offset = 0; offset < size; offset += 4096) {
        if (walk(root, address + offset) ||
            walk(root, physical + offset) != physical + offset) {
            return false;
        }
    }
    const uint64_t flags = kUser | (writable ? kWritable : 0) |
                           (executable ? 0 : kNoExecute);
    for (size_t offset = 0; offset < size; offset += 4096) {
        if (!mapPage(address + offset, physical + offset, flags)) {
            while (offset) {
                offset -= 4096;
                (void)unmapPage(address + offset, true);
            }
            return false;
        }
    }
    return true;
}

bool initializeBootstrapSyscalls(UserServiceRoutine dispatch) noexcept {
    if (!g_addressSpaceReady || !g_probeSucceeded || !dispatch || g_dispatch) {
        return false;
    }
    g_syscallCpu.kernelRsp = reinterpret_cast<uint64_t>(
        g_syscallStack + sizeof(g_syscallStack));
    g_dispatch = dispatch;
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, 0);
    cpu::cpuWrmsr(cpu::MSR_KERNEL_GS_BASE, reinterpret_cast<uint64_t>(&g_syscallCpu));
    cpu::cpuWrmsr(cpu::MSR_STAR, uint64_t{cpu::KGDT64_R3_CMCODE} << 48 |
                                   uint64_t{cpu::SELECTOR_KCODE64} << 32);
    cpu::cpuWrmsr(cpu::MSR_LSTAR, reinterpret_cast<uint64_t>(MicantBootstrapSyscallEntry));
    cpu::cpuWrmsr(cpu::MSR_SFMASK, cpu::SFMASK_DEFAULT | (uint64_t{1} << 18));
    cpu::cpuWrmsr(cpu::MSR_EFER, cpu::cpuRdmsr(cpu::MSR_EFER) | cpu::EFER_SCE);
    return true;
}

bool verifyBootstrapSyscalls() noexcept {
    if (!g_dispatch ||
        !bootstrapKernelRange(reinterpret_cast<uint64_t>(g_nativeProbeImage),
                              sizeof(g_nativeProbeImage), true) ||
        bootstrapKernelRange(kUserBase, 1, false) ||
        bootstrapKernelRange(kUserBase + 4096, 1, true) ||
        bootstrapKernelRange(0x0000800000000000ULL, 1, false) ||
        bootstrapKernelRange(UINT64_MAX - 8, 16, false) ||
        bootstrapKernelRange(reinterpret_cast<uint64_t>(g_nativeProbeImage), 0, true)) {
        return false;
    }
    const auto* begin = reinterpret_cast<const uint8_t*>(MicantSyscallProbeProgram);
    const size_t size = reinterpret_cast<uint64_t>(MicantSyscallProbeEnd) -
                        reinterpret_cast<uint64_t>(begin);
    if (size > sizeof(g_userCode) || size < 3) {
        return false;
    }
    for (size_t byte = 0; byte < size; ++byte) {
        g_userCode[byte] = begin[byte];
    }
    g_probeMode = 5;
    g_probeSucceeded = false;
    g_probeReturnRip = kUserBase + size - 2;
    const size_t activeBefore = mm::PfnDatabase::get().getActiveCount();
    MicantProbeEnter();
    if (!g_probeSucceeded || mm::PfnDatabase::get().getActiveCount() != activeBefore) {
        return false;
    }
    __asm__ __volatile__(
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\n"
        : : : "rax", "memory"
    );
    return true;
}

bool verifyManagedExecutablePage() noexcept {
    if (!g_probeSucceeded || !bootstrapPagingContext() || activePagingRoot() != g_pml4) return false;
    auto& database = mm::PfnDatabase::get();
    const size_t active = database.getActiveCount();
    bool passed = false;
    {
        mm::ProcessAddressSpace space;
        const mm::ProcessAddressSpace::PagingBackend backend{
            nullptr, mapBootstrapManagedPage, unmapBootstrapManagedPage, mapBootstrapProtectManagedPage,
            mapBootstrapManagedView, unmapBootstrapManagedView};
        if (!space.configurePagingBackend(backend, BootstrapManagedBase, BootstrapManagedSize)) return false;
        uintptr_t address = BootstrapManagedBase;
        if (space.allocate(address, mm::PageSize4KB, mm::MEM_RESERVE | mm::MEM_COMMIT,
                           mm::PAGE_READWRITE) != NtStatus::Success) return false;
        *reinterpret_cast<volatile uint8_t*>(address) = 0xcc; // CPL3 breakpoint instruction.
        uint32_t oldProtection{};
        if (space.protect(address, mm::PageSize4KB, mm::PAGE_EXECUTE_READ, &oldProtection) != NtStatus::Success ||
            oldProtection != mm::PAGE_READWRITE || !bootstrapUserRange(address, 1, false, true) ||
            bootstrapUserRange(address, 1, true)) return false;
        const uint32_t previousMode = g_probeMode;
        const uint64_t previousRip = MicantProbeUserRip;
        const uint64_t previousReturn = g_probeReturnRip;
        g_probeMode = 0;
        g_probeSucceeded = false;
        MicantProbeUserRip = address;
        g_probeReturnRip = address + 1;
        MicantProbeEnter();
        passed = g_probeSucceeded;
        g_probeMode = previousMode;
        MicantProbeUserRip = previousRip;
        g_probeReturnRip = previousReturn;
        __asm__ __volatile__("movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es"
                             : : : "rax", "memory");
        if (space.free(address, 0, mm::MEM_RELEASE) != NtStatus::Success) return false;
    }
    return passed && database.getActiveCount() == active;
}

bool verifyNativeImageLoading() noexcept {
    using namespace micant::pe;
    if (!g_dispatch || !g_probeSucceeded) return false;
    constexpr uint64_t imageBase = kUserBase + 0x10000;
    constexpr uint64_t ntdllBase = kUserBase + 0x20000;
    const auto* client = reinterpret_cast<const uint8_t*>(MicantSyscallClientProgram);
    const size_t clientSize = reinterpret_cast<uint64_t>(MicantSyscallClientEnd) -
                              reinterpret_cast<uint64_t>(client);
    const size_t breakpointOffset =
        reinterpret_cast<uint64_t>(MicantSyscallClientBreakpoint) -
        reinterpret_cast<uint64_t>(client);
    if (clientSize > 4096 - 16 || breakpointOffset >= clientSize) {
        return false;
    }
    ImageDosHeader dos{};
    dos.e_magic = DOS_MAGIC;
    dos.e_lfanew = 64;
    memcpy(g_nativeProbeFile, &dos, sizeof(dos));
    ImageNtHeaders64 nt{};
    nt.signature = NT_SIGNATURE;
    nt.fileHeader.machine = MACHINE_AMD64;
    nt.fileHeader.numberOfSections = 1;
    nt.fileHeader.sizeOfOptionalHeader = sizeof(ImageOptionalHeader64);
    nt.fileHeader.characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
    nt.optionalHeader.magic = PE32PLUS_MAGIC;
    nt.optionalHeader.imageBase = imageBase;
    nt.optionalHeader.addressOfEntryPoint = 4096;
    nt.optionalHeader.sectionAlignment = 4096;
    nt.optionalHeader.fileAlignment = 512;
    nt.optionalHeader.sizeOfImage = sizeof(g_nativeProbeImage);
    nt.optionalHeader.sizeOfHeaders = 512;
    nt.optionalHeader.subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
    memcpy(g_nativeProbeFile + 64, &nt, sizeof(nt));
    ImageSectionHeader text{};
    text.virtualAddress = 4096;
    text.misc.virtualSize = 4096;
    text.pointerToRawData = 512;
    text.sizeOfRawData = 4096;
    text.characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    memcpy(g_nativeProbeFile + 64 + sizeof(nt), &text, sizeof(text));
    // CALL gives a compiled Windows x64 function the required entry alignment.
    constexpr uint8_t call[]{0x48, 0x83, 0xEC, 0x20, 0xE8, 7, 0, 0, 0, 0x0F, 0x0B};
    memcpy(g_nativeProbeFile + 512, call, sizeof(call));
    for (size_t byte = 0; byte < clientSize; ++byte) {
        g_nativeProbeFile[512 + 16 + byte] = client[byte];
    }
    if (g_nativeProbeModules.addModule("syscall-probe.exe", g_nativeProbeFile,
            g_nativeProbeImage, imageBase) != micant::NtStatus::Success ||
        g_nativeProbeModules.addModule("ntdll.dll", g_ntdllProbeFile,
            g_ntdllProbeImage, ntdllBase) != micant::NtStatus::Success ||
        g_nativeProbeModules.bindAll() != micant::NtStatus::Success ||
        !mapBootstrapUserMemory(imageBase, g_nativeProbeImage, 4096, false, false) ||
        !mapBootstrapUserMemory(imageBase + 4096, g_nativeProbeImage + 4096,
                                4096, false, true)) {
        return false;
    }
    const auto* dll = g_nativeProbeModules.module(1);
    if (!dll || !mapBootstrapUserMemory(ntdllBase, g_ntdllProbeImage, 4096, false, false))
        return false;
    for (size_t index = 0; index < dll->view.headers().fileHeader.numberOfSections; ++index) {
        const auto section = dll->view.section(index);
        const size_t size = section.misc.virtualSize > section.sizeOfRawData
            ? section.misc.virtualSize : section.sizeOfRawData;
        if (!size) continue;
        if ((section.virtualAddress & 4095) || section.virtualAddress > dll->image.size() ||
            size > dll->image.size() - section.virtualAddress ||
            !mapBootstrapUserMemory(ntdllBase + section.virtualAddress,
                g_ntdllProbeImage + section.virtualAddress, (size + 4095) & ~size_t{4095},
                (section.characteristics & IMAGE_SCN_MEM_WRITE) != 0,
                (section.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)) return false;
    }
    constexpr const char* names[]{"NtCreateSection", "NtMapViewOfSection", "NtUnmapViewOfSection", "NtClose",
                                  "NtAllocateVirtualMemory", "NtFreeVirtualMemory", "NtCreateEvent", "NtSetEvent",
                                  "NtWaitForSingleObject"};
    auto* exports = reinterpret_cast<uint64_t*>(kUserBase + 4096 + 768);
    for (size_t index = 0; index < 9; ++index) {
        if (g_nativeProbeModules.resolve("ntdll.dll", names[index], 0, exports[index]) != micant::NtStatus::Success ||
            !bootstrapUserRange(exports[index], 1, false, true)) return false;
    }
    MicantProbeUserRip = imageBase + 4096;
    g_probeMode = 6;
    g_ring3ClockProbeActive = true;
    g_probeSucceeded = false;
    g_probeReturnRip = imageBase + 4096 + 16 + breakpointOffset + 1;
    const size_t activeBefore = mm::PfnDatabase::get().getActiveCount();
    MicantProbeEnter();
    if (g_log) g_log("[MicaNT Hal] CPL3 syscall probe returned to kernel\r\n");
    g_ring3ClockProbeActive = false;
    MicantProbeUserRip = kUserBase;
    __asm__ __volatile__(
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\n"
        : : : "rax", "memory"
    );
    // Deferred file deletes release their PFNs from a passive worker. Drain
    // them before comparing the active PFN count, otherwise the probe reports
    // a leak that is only pending cleanup.
    uint64_t savedFlags{};
    __asm__ __volatile__("pushfq; popq %0; sti" : "=r"(savedFlags) : : "memory");
    const uint64_t cleanupStart = bootstrapSchedulerClockTicks();
    for (size_t spin = 0; spin < 50000000 && io::IoDeferredFileDeletesPending() &&
         bootstrapSchedulerClockTicks() - cleanupStart < 100; ++spin)
        __asm__ __volatile__("pause");
    __asm__ __volatile__("cli" : : : "memory");
    const bool cleanupPending = io::IoDeferredFileDeletesPending();
    if (savedFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return g_probeSucceeded && !cleanupPending &&
        mm::PfnDatabase::get().getActiveCount() == activeBefore;
}

// Walk every existing leaf, including large pages, before making the IOMMU
// owner's mapping exclusive. Missing/inaccessible page tables fail closed.
static bool hasOnlyOwnerIommuMapping(uint64_t root, uint64_t tablePhysical,
                                     unsigned level, uint64_t virtualBase,
                                     uint64_t first, uint64_t last, uint64_t ownerPage,
                                     bool userAccessible = true) noexcept {
    if (!tablePhysical || walk(root, tablePhysical) != tablePhysical ||
        !mappedRange(tablePhysical, 4096, false, false, false)) return false;
    const auto* table = reinterpret_cast<const uint64_t*>(tablePhysical);
    const unsigned shift = 12 + 9 * (level - 1);
    for (uint64_t i = 0; i < 512; ++i) {
        const uint64_t entry = table[i];
        if (!(entry & kPresent)) continue;
        const bool childUserAccessible = userAccessible && (entry & kUser);
        uint64_t va = virtualBase | (i << shift);
        if (level == 4 && (i & 0x100u)) va |= 0xffff000000000000ull;
        const bool large = (level == 2 || level == 3) && (entry & 128u);
        if (level == 1 || large) {
            const uint64_t span = uint64_t{1} << shift;
            const uint64_t base = (entry & kPhysicalMask) & ~(span - 1);
            const bool ownerMapping = level == 1 && va == ownerPage && base == first &&
                                      last == first + 4095;
            const bool supervisorIdentityMapping = !childUserAccessible && va == base;
            if (base <= last && first <= base + span - 1 &&
                !ownerMapping && !supervisorIdentityMapping)
                return false;
        } else {
            if ((entry & 128u) || !hasOnlyOwnerIommuMapping(root, entry & kPhysicalMask,
                level - 1, va, first, last, ownerPage, childUserAccessible)) return false;
        }
    }
    return true;
}

bool protectBootstrapIommuRegisters(uint64_t physical, size_t size,
                                    const void* ownerMapping) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags) : : "memory");
    if ((flags & (uint64_t{1} << 9)) || !bootstrapPagingContext() || !ownerMapping ||
        (physical & 4095) || size != 4096 || physical > UINT64_MAX - 4095 ||
        (reinterpret_cast<uint64_t>(ownerMapping) & 4095) || g_protectedIommuMmioCount == 16)
        return false;
    const auto owner = reinterpret_cast<uint64_t>(ownerMapping);
    if (walk(reinterpret_cast<uint64_t>(g_pml4), owner) != physical ||
        // Firmware's supervisor-only identity map may cover this MMIO page
        // with a large leaf; it is not a user-accessible alias.
        !hasOnlyOwnerIommuMapping(reinterpret_cast<uint64_t>(g_pml4),
                                  reinterpret_cast<uint64_t>(g_pml4), 4, 0,
                                  physical, physical + 4095, owner)) return false;
    for (const auto* root = g_processPagingRoots; root; root = root->next) {
        const uint64_t table = root->pfn << 12;
        if (!hasOnlyOwnerIommuMapping(table, table, 4, 0, physical, physical + 4095, owner))
            return false;
    }
    g_protectedIommuMmio[g_protectedIommuMmioCount++] = {physical, physical + size - 1, owner};
    return true;
}

void commitBootstrapIommuProtection() noexcept { g_iommuProtectionCommitted = true; }

static void* mapBootstrapPhysicalWindow(uint64_t physical, size_t size, bool device) noexcept {
    if (!g_addressSpaceReady || size == 0 || size > kDeviceMappingWindowSize ||
        physical > kPhysicalMask || size > kPhysicalMask - physical) {
        return nullptr;
    }
    for (size_t i = 0; i < g_protectedIommuMmioCount; ++i)
        if (physical <= g_protectedIommuMmio[i].last &&
            g_protectedIommuMmio[i].first <= physical + size - 1) return nullptr;
    const uint64_t offset = physical & 4095;
    const uint64_t length = (size + offset + 4095) & ~uint64_t{4095};
    const uint64_t start = g_deviceNext;
    if (start < kDeviceBase || start - kDeviceBase > kDeviceMappingWindowSize ||
        length > kDeviceMappingWindowSize - (start - kDeviceBase)) {
        return nullptr;
    }
    for (uint64_t page = 0; page < length; page += 4096) {
        // MMIO is uncached; RAM DMA uses the firmware-validated WB policy.
        if (!mapPage(start + page, (physical & ~uint64_t{4095}) + page,
                     kWritable | kNoExecute | (device ? 0x18 : 0))) {
            while (page) {
                page -= 4096;
                if (!unmapPage(start + page, false)) __builtin_trap();
            }
            return nullptr;
        }
    }
    g_deviceNext += length;
    return reinterpret_cast<void*>(start + offset);
}

void* mapBootstrapDeviceMemory(uint64_t physical, size_t size) noexcept {
    return mapBootstrapPhysicalWindow(physical, size, true);
}

void* mapBootstrapDmaMemory(uint64_t physical, size_t size) noexcept {
    return mapBootstrapPhysicalWindow(physical, size, false);
}

bool releaseBootstrapDeviceMemory(void* mapping, size_t size) noexcept {
    const uint64_t address = reinterpret_cast<uintptr_t>(mapping);
    if (g_iommuProtectionCommitted)
        for (size_t i = 0; i < g_protectedIommuMmioCount; ++i)
            if (address == g_protectedIommuMmio[i].owner) return false;
    if (!mapping || !size || size > kDeviceMappingWindowSize || !pagingContextMatches(nullptr) ||
        address < kDeviceBase || address >= g_deviceNext) return false;
    const uint64_t offset = address & 4095;
    const uint64_t start = address & ~uint64_t{4095};
    const uint64_t length = (size + offset + 4095) & ~uint64_t{4095};
    if (start > UINT64_MAX - length || start + length != g_deviceNext) return false;
    for (uint64_t page = 0; page < length; page += 4096)
        if (!unmapPage(start + page, false)) __builtin_trap();
    g_deviceNext = start;
    return true;
}

bool copyBootstrapFirmwareMemory(uint64_t physical, void* destination, size_t size) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags) : : "memory");
    if (flags & (uint64_t{1} << 9)) return false;
    constexpr uint64_t scratch = kDeviceBase + 16 * 1024 * 1024;
    if (!destination || !size || physical > kPhysicalMask + 4095 ||
        size - 1 > kPhysicalMask + 4095 - physical || !pagingContextMatches(nullptr)) return false;
    auto* out = static_cast<uint8_t*>(destination);
    while (size) {
        const size_t offset = static_cast<size_t>(physical & 4095);
        const size_t count = size < 4096 - offset ? size : 4096 - offset;
        if (!mapPage(scratch, physical & ~uint64_t{4095}, kNoExecute)) return false;
        for (size_t i = 0; i < count; ++i)
            out[i] = reinterpret_cast<const volatile uint8_t*>(scratch + offset)[i];
        if (!unmapPage(scratch, false)) __builtin_trap();
        physical += count;
        out += count;
        size -= count;
    }
    return true;
}

struct TimerWorkerState {
    volatile uint64_t iterations{0};
    uint64_t canary{0};
    volatile uint64_t corrupted{0};
    uint64_t expected[8]{};
    uint64_t observed[8]{};
};
static_assert(offsetof(TimerWorkerState, expected) == 24);
static_assert(offsetof(TimerWorkerState, observed) == 88);
alignas(16) static uint8_t g_firstTimerWorkerStack[16384]{};
alignas(16) static uint8_t g_secondTimerWorkerStack[16384]{};
static TimerWorkerState g_firstTimerWorkerState{};
static TimerWorkerState g_secondTimerWorkerState{};

// These loops make no calls after loading their canaries: a timer interrupt
// must save and restore the entire vector register before the next comparison.
// They are reached only after checking CPUID and XCR0, so unsupported vector
// instructions are never executed.
extern "C" [[noreturn]] __attribute__((naked)) void MicantTimerAvxCanary(TimerWorkerState*) noexcept {
    __asm__ __volatile__(
        "vmovdqu 24(%rcx), %ymm6\nsti\n"
        "1: vmovdqu %ymm6, 88(%rcx)\n"
        "xorl %edx, %edx\n"
        "2: movq 24(%rcx,%rdx,8), %rax\n"
        "cmpq %rax, 88(%rcx,%rdx,8)\njne 3f\n"
        "incq %rdx\ncmpq $4, %rdx\njne 2b\n"
        "incq (%rcx)\njmp 1b\n"
        "3: movq $1, 16(%rcx)\njmp 1b\n");
}

extern "C" [[noreturn]] __attribute__((naked)) void MicantTimerAvx512Canary(TimerWorkerState*) noexcept {
    __asm__ __volatile__(
        "vmovdqu64 24(%rcx), %zmm6\n"
        "vmovdqu64 24(%rcx), %zmm31\nsti\n"
        "1: vmovdqu64 %zmm6, 88(%rcx)\ncallq 4f\n"
        "vmovdqu64 %zmm31, 88(%rcx)\ncallq 4f\n"
        "incq (%rcx)\njmp 1b\n"
        "4: xorl %edx, %edx\n"
        "2: movq 24(%rcx,%rdx,8), %rax\n"
        "cmpq %rax, 88(%rcx,%rdx,8)\njne 3f\n"
        "incq %rdx\ncmpq $8, %rdx\njne 2b\nretq\n"
        "3: movq $1, 16(%rcx)\nretq\n");
}

static unsigned timerVectorCanaryMode() noexcept {
    if ((MicantAmd64XsaveMask & 7u) != 7u) return 0;
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if ((ecx & ((1u << 27) | (1u << 28))) != ((1u << 27) | (1u << 28))) return 0;
    if ((MicantAmd64XsaveMask & 0xe0u) != 0xe0u) return 1;
    eax = 0;
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 7) return 1;
    eax = 7; ecx = 0;
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (ebx & (1u << 16)) ? 2 : 1;
}

[[noreturn]] static void timerWorker(void* argument) noexcept {
    auto* state = static_cast<TimerWorkerState*>(argument);
    ke::KeLowerIrql(ke::PASSIVE_LEVEL);
    const auto mode = timerVectorCanaryMode();
    if (mode == 2) MicantTimerAvx512Canary(state);
    if (mode == 1) MicantTimerAvxCanary(state);
    __asm__ __volatile__(
        "movq 8(%%rcx), %%r12\nmovq %%r12, %%xmm6\nsti\n"
        "1:\nmovq %%xmm6, %%rax\ncmpq %%r12, %%rax\njne 2f\n"
        "cmpq 8(%%rcx), %%r12\njne 2f\nincq (%%rcx)\njmp 1b\n"
        "2:\nmovq $1, 16(%%rcx)\njmp 1b\n"
        : : "c"(state) : "rax", "r12", "xmm6", "memory", "cc");
    __builtin_unreachable();
}

static bool verifyBootstrapPreemption() noexcept {
    using namespace ke;
    auto* boot = KeGetCurrentThread();
    auto& scheduler = PriorityScheduler::get();
    const auto readyNonPassiveWorkThreads = [&scheduler]() noexcept {
        auto count = scheduler.getReadyThreadCount();
        for (const auto& worker : passive_work::detail::workers)
            if (worker.readyQueued && count) --count;
        return count;
    };
    if (!boot || boot->state != ThreadState::Running || readyNonPassiveWorkThreads()) return false;
    auto& firstState = g_firstTimerWorkerState;
    auto& secondState = g_secondTimerWorkerState;
    firstState.iterations = 0;
    secondState.iterations = 0;
    firstState.corrupted = 0;
    secondState.corrupted = 0;
    firstState.canary = 0x12345678abcdef00ULL;
    secondState.canary = 0xfedcba9876543210ULL;
    for (size_t i = 0; i < 8; ++i) {
        firstState.expected[i] = firstState.canary ^ (0x0101010101010101ULL * (i + 1));
        secondState.expected[i] = secondState.canary ^ (0x1010101010101010ULL * (i + 1));
    }
    KThread first, second;
    first.directoryTableBase = second.directoryTableBase = g_CurrentCpu.currentCr3;
    first.priority = second.priority = PRIORITY_NORMAL;
    first.quantumRemaining = second.quantumRemaining = 1;
    if (!ke::detail::prepareBootstrapThread(first, g_firstTimerWorkerStack, sizeof(g_firstTimerWorkerStack), timerWorker, &firstState) ||
        !ke::detail::prepareBootstrapThread(second, g_secondTimerWorkerStack, sizeof(g_secondTimerWorkerStack), timerWorker, &secondState) ||
        !scheduler.readyKThread(&second)) return false;
    const auto originalPriority = boot->priority;
    const auto previous = KfRaiseIrql(DISPATCH_LEVEL);
    const uint64_t switches = g_CurrentCpu.contextSwitches;
    boot->priority = PRIORITY_REALTIME_MAX;
    const bool preemptionWasEnabled = MicantTimerPreemptionEnabled;
    MicantTimerBootThread = boot;
    MicantTimerBootQueued = false;
    MicantTimerBootDeadline = MicantTimerTicks + 24;
    MicantTimerPreemptionEnabled = true;
    const bool switched = KiSwapContext(boot, &first);
    MicantTimerPreemptionEnabled = preemptionWasEnabled;
    MicantTimerBootThread = nullptr;
    boot->priority = originalPriority;
    KeLowerIrql(previous);
    const bool success = switched && MicantTimerBootQueued && !MicantTimerDispatchFailed &&
        firstState.iterations && secondState.iterations && !firstState.corrupted && !secondState.corrupted &&
        g_syscallCpu.kernelRsp == (boot->kernelStackTop ? boot->kernelStackTop :
            reinterpret_cast<uintptr_t>(g_syscallStack + sizeof(g_syscallStack))) &&
        cpu::CpuHardwareEngine::get().getTss().rsp0 == (boot->kernelStackTop ? boot->kernelStackTop :
            reinterpret_cast<uintptr_t>(g_kernelStack + sizeof(g_kernelStack))) &&
        g_CurrentCpu.currentThread == boot && g_CurrentCpu.contextSwitches >= switches + 4;
    const bool removedFirst = scheduler.removeReadyThread(&first);
    const bool removedSecond = scheduler.removeReadyThread(&second);
    first.state = second.state = ThreadState::Terminated;
    return success && removedFirst && removedSecond && readyNonPassiveWorkThreads() == 0;
}

extern "C" bool MicantTestPsUserPreemption() noexcept;

// Verify the one routed hardware priority with IF still enabled. A pending
// clock interrupt must become deliverable after the original IRQL is restored.
static bool verifyClockIrqlMask(BootstrapLocalApic& apic) noexcept {
    const auto previous = ke::KfRaiseIrql(ke::CLOCK_LEVEL);
    const auto nested = ke::KfRaiseIrql(ke::HIGH_LEVEL);
    uint64_t highPriority{};
    __asm__ __volatile__("movq %%cr8, %0" : "=r"(highPriority));
    ke::KeLowerIrql(nested);
    uint64_t clockPriority{}, flags{};
    __asm__ __volatile__("movq %%cr8, %0; pushfq; popq %1"
                         : "=r"(clockPriority), "=r"(flags));
    const uint64_t before = MicantTimerTicks;
    constexpr uint32_t timerBit = 1u << (0xd1 % 32);
    constexpr size_t timerIrr = (0x200 + (0xd1 / 32) * 0x10) / 4;
    for (size_t spin = 0; spin < 50000000 && !(apic[timerIrr] & timerBit); ++spin)
        __asm__ __volatile__("pause");
    const bool blocked = (apic[timerIrr] & timerBit) && MicantTimerTicks == before;
    ke::KeLowerIrql(previous);
    for (size_t spin = 0; spin < 50000000 && MicantTimerTicks == before; ++spin)
        __asm__ __volatile__("pause");
    return previous == ke::PASSIVE_LEVEL && nested == ke::CLOCK_LEVEL &&
        highPriority == 15 && clockPriority == 13 && (flags & (uint64_t{1} << 9)) &&
        blocked && MicantTimerTicks > before;
}

static bool verifyClockInterruptDelivery(BootstrapLocalApic& apic) noexcept {
    const uint64_t before = MicantTimerTicks;
    __asm__ __volatile__("sti" : : : "memory");
    ke::SpinLock lock;
    const ke::KIRQL oldIrql = lock.acquire();
    ke::SpinLock nested;
    const ke::KIRQL nestedIrql = nested.acquire();
    nested.release(nestedIrql);
    uint64_t lockedFlags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(lockedFlags));
    const uint64_t beforeLockedWait = MicantTimerTicks;
    for (size_t spin = 0; spin < 200000; ++spin) __asm__ __volatile__("pause");
    const bool lockMaskedInterrupts = !(lockedFlags & (uint64_t{1} << 9)) &&
        MicantTimerTicks == beforeLockedWait;
    lock.release(oldIrql);
    uint64_t releasedFlags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(releasedFlags));
    const bool irqlMaskedClock = verifyClockIrqlMask(apic);
    for (size_t spin = 0; spin < 50000000 && MicantTimerTicks - before < 3; ++spin)
        __asm__ __volatile__("pause");
    __asm__ __volatile__("cli" : : : "memory");
    const bool delivered = MicantTimerTicks - before >= 3;
    const bool success = lockMaskedInterrupts && irqlMaskedClock && delivered &&
        (releasedFlags & (uint64_t{1} << 9));
    if (!success) {
        logTrapValue("[MicaNT Hal] Timer lock masked=0x", lockMaskedInterrupts);
        logTrapValue("[MicaNT Hal] Timer IRQL masked=0x", irqlMaskedClock);
        logTrapValue("[MicaNT Hal] Timer delivered=0x", delivered);
    }
    return success;
}

static bool verifyBootstrapTimerInterruptImpl(bool verifyScheduling) noexcept {
    if (!bootstrapQemuFaultTestsAvailable() || !bootstrapPagingContext() ||
        !ke::MicantAmd64ConfigureExtendedState()) {
        if (g_log) g_log("[MicaNT Hal] Timer diagnostic prerequisites unavailable\r\n");
        return false;
    }
    Idtr current{};
    __asm__ __volatile__("sidt %0" : "=m"(current));
    if (current.base != reinterpret_cast<uint64_t>(g_idt) || current.limit != sizeof(g_idt) - 1) {
        if (g_log) g_log("[MicaNT Hal] Timer diagnostic bootstrap IDT is not active\r\n");
        return false;
    }
    if (g_schedulerClockActive) {
        if (!g_timerApic.available() || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL ||
            g_timerApic[0x320 / 4] != (0xd1 | (1u << 17)) ||
            g_timerApic[0x380 / 4] != g_schedulerTimerPeriod ||
            MicantTimerDispatchFailed) {
            if (g_log) g_log("[MicaNT Hal] Live timer state precondition failed\r\n");
            return false;
        }
        // The live clock owns its APIC and monotonic ticks; do not reset either.
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const uint64_t before = MicantTimerTicks;
        const bool interruptDelivery = verifyClockInterruptDelivery(g_timerApic);
        const bool kernelPreemption = !verifyScheduling || verifyBootstrapPreemption();
        const bool userPreemption = !verifyScheduling || !kernelPreemption || MicantTestPsUserPreemption();
        const bool ticksAdvanced = MicantTimerTicks > before && !MicantTimerDispatchFailed;
        const bool success = interruptDelivery && kernelPreemption && userPreemption && ticksAdvanced;
        if (!success && g_log) {
            if (!interruptDelivery) g_log("[MicaNT Hal] Live timer interrupt delivery failed\r\n");
            if (!kernelPreemption) g_log("[MicaNT Hal] Live kernel timer preemption failed\r\n");
            if (!userPreemption) g_log("[MicaNT Hal] Live CPL3 timer preemption failed\r\n");
            if (!ticksAdvanced) g_log("[MicaNT Hal] Live timer progress failed\r\n");
        }
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        if (!success && g_log) g_log("[MicaNT Hal] Live scheduler clock diagnostic failed\r\n");
        return success;
    }
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & (1u << 9))) {
        if (g_log) g_log("[MicaNT Hal] Timer diagnostic local APIC unsupported\r\n");
        return false;
    }
    const uint64_t apicBase = cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE);
    const bool x2 = (apicBase & (1u << 10)) != 0;
    if (!(apicBase & (1u << 11)) || (x2 && !(ecx & (1u << 21)))) {
        logTrapValue("[MicaNT Hal] APIC base=0x", apicBase);
        if (g_log) g_log("[MicaNT Hal] Timer diagnostic APIC mode unavailable\r\n");
        return false;
    }
    auto* mapping = x2 ? nullptr : static_cast<volatile uint32_t*>(
        mapBootstrapDeviceMemory(apicBase & kPhysicalMask, 4096));
    g_timerApic.initialize(mapping, x2);
    auto& apic = g_timerApic;
    if (!apic.available()) {
        if (g_log) g_log("[MicaNT Hal] Timer diagnostic APIC MMIO mapping failed\r\n");
        return false;
    }
    if (((apic[0x30 / 4] >> 16) & 0xff) < 5 || !(apic[0x320 / 4] & (1u << 16))) {
        logTrapValue("[MicaNT Hal] APIC version=0x", apic[0x30 / 4]);
        logTrapValue("[MicaNT Hal] APIC timer initial=0x", apic[0x380 / 4]);
        logTrapValue("[MicaNT Hal] APIC timer LVT=0x", apic[0x320 / 4]);
        if (g_log) g_log("[MicaNT Hal] Timer diagnostic APIC timer unavailable\r\n");
        return false;
    }

    const auto input = [](uint16_t port) noexcept {
        uint8_t value{};
        __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    };
    const auto output = [](uint16_t port, uint8_t value) noexcept {
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
    };
    const uint8_t masterMask = input(0x21), slaveMask = input(0xa1);
    output(0x21, 0xff);
    output(0xa1, 0xff);
    constexpr uint32_t offsets[]{0x320, 0x330, 0x340, 0x350, 0x360, 0x370};
    uint32_t savedLvt[6]{};
    for (size_t i = 0; i < 6; ++i) {
        savedLvt[i] = apic[offsets[i] / 4];
        apic[offsets[i] / 4] = savedLvt[i] | (1u << 16);
    }
    const uint32_t savedTpr = apic[0x80 / 4];
    const uint32_t savedSvr = apic[0xf0 / 4];
    const uint32_t savedDivide = apic[0x3e0 / 4];
    const uint32_t savedInitial = apic[0x380 / 4];
    const InterruptGate savedTimer = g_idt[0xd1], savedSpurious = g_idt[0xff];
    const auto gate = [](uint64_t handler) noexcept {
        return InterruptGate{static_cast<uint16_t>(handler), cpu::SELECTOR_KCODE64, 0, 0x8e,
                             static_cast<uint16_t>(handler >> 16), static_cast<uint32_t>(handler >> 32), 0};
    };
    g_idt[0xd1] = gate(reinterpret_cast<uint64_t>(MicantBootstrapTimerInterrupt));
    g_idt[0xff] = gate(reinterpret_cast<uint64_t>(MicantBootstrapSpuriousInterrupt));
    MicantTimerTicks = 0;
    g_clockIrqlMaskReady = true;
    setBootstrapClockIrqlMask(ke::KeGetCurrentIrql());
    apic[0xf0 / 4] = (savedSvr & ~0xffu) | 0x1ff;
    apic[0x3e0 / 4] = 0xb; // Divide by one; the test does not infer a frequency.
    apic[0x320 / 4] = 0xd1 | (1u << 17); // Periodic, unmasked.
    apic[0x380 / 4] = 100000;
    const bool timerDelivered = verifyClockInterruptDelivery(apic);
    bool preemptionVerified = false;
    bool userPreemption = false;
    if (verifyScheduling) {
        preemptionVerified = timerDelivered && verifyBootstrapPreemption();
        MicantTimerTicks = 0;
        MicantTimerBootThread = nullptr;
        MicantTimerDispatchFailed = false;
        MicantTimerPreemptionEnabled = preemptionVerified;
        userPreemption = preemptionVerified && MicantTestPsUserPreemption() &&
            MicantTimerTicks > 0 && !MicantTimerDispatchFailed;
    }
    MicantTimerPreemptionEnabled = false;
    apic[0x320 / 4] = savedLvt[0] | (1u << 16);
    apic[0x380 / 4] = 0;
    // Drain any timer already accepted into IRR before restoring the gate.
    constexpr uint32_t timerBit = 1u << (0xd1 % 32);
    constexpr size_t timerIrr = (0x200 + (0xd1 / 32) * 0x10) / 4;
    if (apic[timerIrr] & timerBit) {
        __asm__ __volatile__("sti" : : : "memory");
        for (size_t spin = 0; spin < 1000000 && (apic[timerIrr] & timerBit); ++spin)
            __asm__ __volatile__("pause");
        __asm__ __volatile__("cli" : : : "memory");
    }
    const bool success = timerDelivered &&
        (!verifyScheduling || (preemptionVerified && userPreemption)) &&
        !(apic[timerIrr] & timerBit);
    apic[0x3e0 / 4] = savedDivide;
    apic[0x380 / 4] = savedInitial;
    g_clockIrqlMaskReady = false;
    serializeApicPriorityAccess();
    apic[0x80 / 4] = savedTpr;
    serializeApicPriorityAccess();
    apic[0xf0 / 4] = savedSvr;
    for (size_t i = 0; i < 6; ++i) apic[offsets[i] / 4] = savedLvt[i];
    g_idt[0xd1] = savedTimer;
    g_idt[0xff] = savedSpurious;
    output(0x21, masterMask);
    output(0xa1, slaveMask);
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    const bool restored = !(flags & (uint64_t{1} << 9)) &&
        input(0x21) == masterMask && input(0xa1) == slaveMask &&
        apic[0x80 / 4] == savedTpr && apic[0xf0 / 4] == savedSvr &&
        apic[0x3e0 / 4] == savedDivide && apic[0x380 / 4] == savedInitial;
    g_timerApic.initialize(nullptr, false);
    if (!success) {
        logTrapValue("[MicaNT Hal] Timer ticks=0x", MicantTimerTicks);
        logTrapValue("[MicaNT Hal] Timer delivered=0x", timerDelivered);
        logTrapValue("[MicaNT Hal] Timer scheduling verified=0x", preemptionVerified);
        logTrapValue("[MicaNT Hal] Timer user preemption=0x", userPreemption);
        logTrapValue("[MicaNT Hal] Timer IRR pending=0x", apic[timerIrr] & timerBit);
    }
    if (!restored && g_log) g_log("[MicaNT Hal] Timer state restoration failed\r\n");
    return success && restored;
}

bool verifyBootstrapTimerSource() noexcept {
    return verifyBootstrapTimerInterruptImpl(false);
}

bool verifyBootstrapTimerInterrupt() noexcept {
    return verifyBootstrapTimerInterruptImpl(true);
}

static uint64_t readSerializedTsc() noexcept {
    uint32_t low{}, high{};
    __asm__ __volatile__("lfence; rdtsc; lfence"
                         : "=a"(low), "=d"(high) : : "memory");
    return (static_cast<uint64_t>(high) << 32) | low;
}

static bool hasInvariantTsc() noexcept {
    uint32_t eax = 0x80000000u, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000007u) return false;
    eax = 0x80000007u;
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 8)) != 0;
}

// Calibrate invariant TSC against PIT channel 2 without relying on a nominal
// CPU frequency. The speaker must be inactive; its gate and APIC state are
// restored. The measured source is BSP-only and can fail on platforms where
// channel 2 is unavailable or samples are unstable.
static bool calibrateBootstrapTsc(uint64_t& frequencyHz) noexcept {
    const auto useFirmwareCalibration = [&]() noexcept {
        const uint64_t hintedFrequency =
            g_bootstrapTscFrequencyHint.load(std::memory_order_acquire);
        if (hintedFrequency < 1'000'000 || hintedFrequency > 10'000'000'000ULL)
            return false;
        frequencyHz = hintedFrequency;
        g_bootstrapTscCalibrationSource.store(
            BootstrapTscCalibrationSource::UefiStall, std::memory_order_release);
        return true;
    };
    uint32_t maxBasic = 0, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(maxBasic), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (maxBasic >= 0x15) {
        uint32_t denominator = 0x15, numerator{}, crystalHz{}, reserved{};
        __asm__ __volatile__("cpuid" : "+a"(denominator), "=b"(numerator),
                             "=c"(crystalHz), "=d"(reserved));
        if (denominator && numerator && crystalHz) {
            const uint64_t scaled = static_cast<uint64_t>(crystalHz) * numerator;
            const uint64_t frequency = scaled / denominator;
            if (frequency >= 1'000'000 && frequency <= 10'000'000'000ULL) {
                // CPUID.15H reports the architectural TSC/crystal ratio and
                // the crystal frequency; unlike leaf 16H this is not a
                // nominal marketing frequency. The invariant flag is still
                // required so the calibrated scale remains stable at runtime.
                if (!hasInvariantTsc()) return false;
                frequencyHz = frequency;
                g_bootstrapTscCalibrationSource.store(
                    BootstrapTscCalibrationSource::Cpuid15, std::memory_order_release);
                return true;
            }
        }
    }
    if (!hasInvariantTsc()) return false;
    const auto input = [](uint16_t port) noexcept {
        uint8_t value{};
        __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    };
    const auto output = [](uint16_t port, uint8_t value) noexcept {
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
    };
    const uint8_t speaker = input(0x61);
    if (speaker & 3) return useFirmwareCalibration();

    constexpr uint16_t pitInterval = 11932;
    constexpr uint64_t pitFrequency = 1193182;
    uint64_t samples[5]{};
    for (auto& sample : samples) {
        output(0x61, static_cast<uint8_t>(speaker & ~uint8_t{3}));
        output(0x43, 0xb0); // Channel 2, low/high bytes, one-shot mode 0.
        output(0x42, static_cast<uint8_t>(pitInterval));
        output(0x42, static_cast<uint8_t>(pitInterval >> 8));
        const uint64_t start = readSerializedTsc();
        output(0x61, static_cast<uint8_t>((speaker & ~uint8_t{2}) | 1));
        size_t spin = 0;
        while (!(input(0x61) & 0x20) && spin++ < 50000000)
            __asm__ __volatile__("pause");
        const uint64_t end = readSerializedTsc();
        output(0x61, speaker);
        if (spin >= 50000000 || end <= start)
            return useFirmwareCalibration();
        sample = end - start;
    }

    for (size_t i = 1; i < 5; ++i) {
        const uint64_t sample = samples[i];
        size_t j = i;
        while (j && samples[j - 1] > sample) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = sample;
    }
    const uint64_t median = samples[2];
    if (!median || samples[3] - samples[1] > median / 20)
        return useFirmwareCalibration();
    if (median > (UINT64_MAX - pitInterval / 2) / pitFrequency)
        return useFirmwareCalibration();
    const uint64_t scaled = (median * pitFrequency + pitInterval / 2) / pitInterval;
    if (scaled < 1000000 || scaled > 10000000000ULL)
        return useFirmwareCalibration();
    frequencyHz = scaled;
    g_bootstrapTscCalibrationSource.store(
        BootstrapTscCalibrationSource::PitChannel2, std::memory_order_release);
    return true;
}

// Measure one approximately 10 ms interval against PIT channel 2. The
// speaker must be inactive; the port gate is restored after calibration.
static bool measureBootstrapApic(BootstrapLocalApic& apic, uint32_t& elapsed) noexcept {
    const auto input = [](uint16_t port) noexcept {
        uint8_t value{};
        __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    };
    const auto output = [](uint16_t port, uint8_t value) noexcept {
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
    };
    const uint8_t speaker = input(0x61);
    if (speaker & 3) return false;
    const uint32_t savedTimer = apic[0x320 / 4];
    const uint32_t savedDivide = apic[0x3e0 / 4];
    const uint32_t savedInitial = apic[0x380 / 4];
    constexpr uint16_t pitInterval = 11932; // 8254 input clock: approximately 1.193182 MHz.
    output(0x61, static_cast<uint8_t>(speaker & ~uint8_t{3}));
    output(0x43, 0xb0); // Channel 2, low/high bytes, mode 0, binary.
    output(0x42, static_cast<uint8_t>(pitInterval));
    output(0x42, static_cast<uint8_t>(pitInterval >> 8));
    apic[0x320 / 4] = (savedTimer | (1u << 16)) & ~(1u << 17);
    apic[0x3e0 / 4] = 0xb; // Divide by one.
    apic[0x380 / 4] = 0xffffffff;
    output(0x61, static_cast<uint8_t>((speaker & ~uint8_t{2}) | 1));
    size_t spin = 0;
    while (!(input(0x61) & 0x20) && spin++ < 50000000) __asm__ __volatile__("pause");
    const uint32_t remaining = apic[0x390 / 4];
    output(0x61, speaker);
    apic[0x380 / 4] = 0;
    apic[0x3e0 / 4] = savedDivide;
    apic[0x380 / 4] = savedInitial;
    apic[0x320 / 4] = savedTimer;
    elapsed = 0xffffffffu - remaining;
    if (spin >= 50000000 || elapsed < 100 || remaining == 0) return false;
    return true;
}

// A median suppresses isolated VM scheduling pauses and port-I/O latency.
// The PIT count is not exactly 10 ms, so scale it to the requested period.
// Reject unstable measurements instead of silently programming a bad clock.
static bool calibrateBootstrapApic(BootstrapLocalApic& apic, uint32_t& period) noexcept {
    constexpr uint64_t pitFrequency = 1193182;
    constexpr uint64_t pitInterval = 11932;
    uint32_t samples[5]{};
    for (auto& sample : samples) {
        if (!measureBootstrapApic(apic, sample)) return false;
    }
    for (size_t i = 1; i < 5; ++i) {
        const auto sample = samples[i];
        size_t j = i;
        while (j && samples[j - 1] > sample) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = sample;
    }
    const uint64_t median = samples[2];
    if (uint64_t{samples[3] - samples[1]} * 20 > median) return false;
    constexpr uint64_t denominator = pitInterval * 100;
    const uint64_t scaled = (median * pitFrequency + denominator / 2) / denominator;
    if (scaled < 100 || scaled > UINT32_MAX) return false;
    period = static_cast<uint32_t>(scaled);
    return true;
}

[[noreturn]] static void bootstrapIdle(void*) noexcept {
    ke::KeLowerIrql(ke::PASSIVE_LEVEL);
    for (;;) __asm__ __volatile__("sti; hlt; cli" : : : "memory");
}

bool startBootstrapSchedulerClock() noexcept {
    if (g_schedulerClockActive || !g_dispatch || !bootstrapPagingContext() ||
        !ke::KeGetCurrentThread()) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: bootstrap scheduler context unavailable\r\n");
        return false;
    }
    uint64_t flags{};
    Idtr current{};
    __asm__ __volatile__("pushfq; popq %0; sidt %1"
                         : "=r"(flags), "=m"(current));
    if (flags & (uint64_t{1} << 9)) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: interrupts enabled during setup\r\n");
        return false;
    }
    if (current.base != reinterpret_cast<uint64_t>(g_idt)) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: bootstrap IDT is not active\r\n");
        return false;
    }
    if (!ke::MicantAmd64ConfigureExtendedState()) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: extended processor state unavailable\r\n");
        return false;
    }
    // Prefer invariant TSC when the platform supplies one. The calibrated
    // APIC timer below remains a real monotonic clock source if it does not.
    (void)initializeBootstrapHighResolutionClock();
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & (1u << 9))) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: local APIC unsupported\r\n");
        return false;
    }
    const uint64_t apicBase = cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE);
    const bool x2 = (apicBase & (1u << 10)) != 0;
    if (!(apicBase & (1u << 11)) || (x2 && !(ecx & (1u << 21)))) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: local APIC mode unavailable\r\n");
        return false;
    }
    auto* mapping = x2 ? nullptr : static_cast<volatile uint32_t*>(
        mapBootstrapDeviceMemory(apicBase & kPhysicalMask, 4096));
    g_timerApic.initialize(mapping, x2);
    auto& apic = g_timerApic;
    if (!apic.available() || ((apic[0x30 / 4] >> 16) & 0xff) < 5 ||
        !(apic[0x320 / 4] & (1u << 16))) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: local APIC timer unavailable\r\n");
        return false;
    }
    uint32_t interval{};
    if (!calibrateBootstrapApic(apic, interval)) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: local APIC timer calibration failed\r\n");
        return false;
    }
    g_schedulerTimerPeriod = interval;
    // Idle outlives every process; never retain the caller's disposable CR3.
    g_bootstrapIdleThread.directoryTableBase = bootstrapKernelPagingRoot();
    g_bootstrapIdleThread.priority = ke::PRIORITY_IDLE;
    if (!ke::detail::prepareBootstrapThread(g_bootstrapIdleThread, g_bootstrapIdleStack,
            sizeof(g_bootstrapIdleStack), bootstrapIdle, nullptr)) {
        if (g_log) g_log("[MicaNT Hal] Clock start rejected: idle thread setup failed\r\n");
        return false;
    }

    // Only the clock vector is routed in this bootstrap configuration.
    __asm__ __volatile__("outb %0, $0x21; outb %0, $0xa1" : : "a"(uint8_t{0xff}));
    constexpr uint32_t localVectors[]{0x320, 0x330, 0x340, 0x350, 0x360, 0x370};
    for (const auto offset : localVectors) apic[offset / 4] = apic[offset / 4] | (1u << 16);
    const auto gate = [](uint64_t handler) noexcept {
        return InterruptGate{static_cast<uint16_t>(handler), cpu::SELECTOR_KCODE64, 0, 0x8e,
                             static_cast<uint16_t>(handler >> 16), static_cast<uint32_t>(handler >> 32), 0};
    };
    g_idt[0xd1] = gate(reinterpret_cast<uint64_t>(MicantBootstrapTimerInterrupt));
    g_idt[0xff] = gate(reinterpret_cast<uint64_t>(MicantBootstrapSpuriousInterrupt));
    MicantTimerTicks = 0;
    MicantTimerBootThread = nullptr;
    MicantTimerDispatchFailed = false;
    MicantTimerPreemptionEnabled = true;
    g_clockIrqlMaskReady = true;
    setBootstrapClockIrqlMask(ke::KeGetCurrentIrql());
    apic[0xf0 / 4] = (apic[0xf0 / 4] & ~0xffu) | 0x1ff;
    apic[0x3e0 / 4] = 0xb;
    apic[0x320 / 4] = 0xd1 | (1u << 17);
    apic[0x380 / 4] = interval;
    ke::g_CurrentCpu.idleThread = &g_bootstrapIdleThread;
    g_bootstrapApicClockTickOrigin =
        g_bootstrapApicClockTicks.load(std::memory_order_acquire);
    g_bootstrapApicClockLastCounter.store(0, std::memory_order_release);
    g_schedulerClockActive = true;
    if (g_bootstrapTscState.load(std::memory_order_acquire) != 2) {
        g_bootstrapMonotonicCounter = 0;
        g_bootstrapMonotonicFrequency = static_cast<uint64_t>(interval) * 100;
        g_bootstrapMonotonicState.store(2, std::memory_order_release);
        uint32_t currentCount{};
        if (g_log && g_timerApic.readCurrentCount(currentCount))
            g_log("[MicaNT Hal] Invariant TSC unavailable; using calibrated APIC current-count QPC\r\n");
        else if (g_log)
            g_log("[MicaNT Hal] Invariant TSC unavailable; using calibrated APIC ticks (10 ms resolution)\r\n");
    }
    return true;
}

namespace {
struct IdleWaitProbe {
    ke::KThread* boot;
    uint32_t stage{0};
};

alignas(16) uint8_t g_idleWaitProbeStack[16384]{};

[[noreturn]] void runIdleWaitProbe(void* argument) noexcept {
    auto& probe = *static_cast<IdleWaitProbe*>(argument);
    auto* current = ke::KeGetCurrentThread();
    ke::KeLowerIrql(ke::PASSIVE_LEVEL);
    probe.stage = 1;
    if (!ke::detail::blockCurrentThread() || probe.stage != 2 ||
        !ke::PriorityScheduler::get().readyKThread(probe.boot)) __builtin_trap();
    probe.stage = 3;
    current->state = ke::ThreadState::Terminated;
    ke::KfRaiseIrql(ke::DISPATCH_LEVEL);
    auto* next = ke::PriorityScheduler::get().selectNextThread();
    if (next != probe.boot || !ke::KiSwapContext(current, next)) __builtin_trap();
    __builtin_trap();
}

bool verifyIdleWaitAndWake() noexcept {
    auto& scheduler = ke::PriorityScheduler::get();
    auto* boot = ke::KeGetCurrentThread();
    const auto readyNonPassiveWorkThreads = [&scheduler]() noexcept {
        auto count = scheduler.getReadyThreadCount();
        for (const auto& worker : ke::passive_work::detail::workers)
            if (worker.readyQueued && count) --count;
        return count;
    };
    if (!boot || readyNonPassiveWorkThreads() ||
        ke::g_CurrentCpu.idleThread != &g_bootstrapIdleThread) return false;
    ke::KThread waiter;
    waiter.directoryTableBase = ke::g_CurrentCpu.currentCr3;
    IdleWaitProbe probe{boot};
    if (!ke::detail::prepareBootstrapThread(waiter, g_idleWaitProbeStack,
            sizeof(g_idleWaitProbeStack), runIdleWaitProbe, &probe)) return false;
    const auto originalPriority = boot->priority;
    boot->priority = ke::PRIORITY_REALTIME_MAX;
    MicantTimerBootThread = boot;
    MicantTimerBootQueued = false;
    MicantTimerBootDeadline = MicantTimerTicks + 3;
    const auto previousIrql = ke::KfRaiseIrql(ke::DISPATCH_LEVEL);
    const bool firstSwitch = ke::KiSwapContext(boot, &waiter);
    ke::KeLowerIrql(previousIrql);
    MicantTimerBootThread = nullptr;
    if (!firstSwitch || !MicantTimerBootQueued || probe.stage != 1 ||
        waiter.state != ke::ThreadState::Waiting || g_bootstrapIdleThread.readyQueued ||
        !scheduler.readyKThread(&waiter)) return false;
    probe.stage = 2;
    auto* next = scheduler.selectNextThread();
    if (next != &waiter) return false;
    const auto secondIrql = ke::KfRaiseIrql(ke::DISPATCH_LEVEL);
    const bool secondSwitch = ke::KiSwapContext(boot, next);
    ke::KeLowerIrql(secondIrql);
    const bool idleReady = g_bootstrapIdleThread.state == ke::ThreadState::Ready &&
        !g_bootstrapIdleThread.readyQueued;
    boot->priority = originalPriority;
    return secondSwitch && idleReady && probe.stage == 3 &&
        waiter.state == ke::ThreadState::Terminated && !readyNonPassiveWorkThreads() &&
        !MicantTimerDispatchFailed;
}
}

bool verifyBootstrapSchedulerClock() noexcept {
    if (!g_schedulerClockActive) return false;
    const uint64_t before = MicantTimerTicks;
    return MicantTestPsUserPreemption() && MicantTimerTicks > before && !MicantTimerDispatchFailed &&
        verifyIdleWaitAndWake();
}

bool bootstrapSchedulerClockFailed() noexcept { return MicantTimerDispatchFailed; }
bool bootstrapSchedulerClockActive() noexcept { return g_schedulerClockActive; }
uintptr_t bootstrapKernelPagingRoot() noexcept { return reinterpret_cast<uintptr_t>(g_pml4); }
uint64_t bootstrapSchedulerClockTicks() noexcept { return MicantTimerTicks; }

BootstrapClockStatus setBootstrapTscCalibrationHint(uint64_t frequencyHz) noexcept {
    if (ke::g_CurrentCpu.processorNumber != 0)
        return BootstrapClockStatus::NotBootstrapProcessor;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return BootstrapClockStatus::InvalidIrql;
    if (frequencyHz < 1'000'000 || frequencyHz > 10'000'000'000ULL)
        return BootstrapClockStatus::InvalidParameter;
    if (!hasInvariantTsc()) return BootstrapClockStatus::UnsupportedPlatform;
    if (g_bootstrapTscState.load(std::memory_order_acquire) != 0)
        return BootstrapClockStatus::InvalidParameter;
    uint64_t expected = 0;
    if (!g_bootstrapTscFrequencyHint.compare_exchange_strong(
            expected, frequencyHz, std::memory_order_acq_rel,
            std::memory_order_acquire) && expected != frequencyHz)
        return BootstrapClockStatus::InvalidParameter;
    return BootstrapClockStatus::Success;
}

BootstrapClockStatus initializeBootstrapHighResolutionClock() noexcept {
    if (ke::g_CurrentCpu.processorNumber != 0)
        return BootstrapClockStatus::NotBootstrapProcessor;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return BootstrapClockStatus::InvalidIrql;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags) : : "memory");
    if (flags & (uint64_t{1} << 9)) return BootstrapClockStatus::InvalidIrql;

    uint8_t expected = 0;
    if (!g_bootstrapTscState.compare_exchange_strong(
            expected, 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
        if (expected == 2) return BootstrapClockStatus::Success;
        return BootstrapClockStatus::ClockUnavailable;
    }

    uint64_t frequency{};
    if (!calibrateBootstrapTsc(frequency)) {
        g_bootstrapTscState.store(0, std::memory_order_release);
        return BootstrapClockStatus::CalibrationFailed;
    }
    if (g_log) {
        switch (g_bootstrapTscCalibrationSource.load(std::memory_order_acquire)) {
        case BootstrapTscCalibrationSource::Cpuid15:
            g_log("[MicaNT Hal] TSC frequency source: CPUID.15H\r\n");
            break;
        case BootstrapTscCalibrationSource::PitChannel2:
            g_log("[MicaNT Hal] TSC frequency source: PIT channel 2\r\n");
            break;
        case BootstrapTscCalibrationSource::UefiStall:
            g_log("[MicaNT Hal] TSC frequency source: validated UEFI Stall hint\r\n");
            break;
        default:
            g_log("[MicaNT Hal] TSC frequency source is invalid\r\n");
            g_bootstrapTscState.store(0, std::memory_order_release);
            return BootstrapClockStatus::CalibrationFailed;
        }
    }
    g_bootstrapTscFrequency = frequency;
    g_bootstrapTscState.store(2, std::memory_order_release);
    g_bootstrapMonotonicCounter = readSerializedTsc();
    g_bootstrapMonotonicFrequency = frequency;
    g_bootstrapMonotonicState.store(2, std::memory_order_release);
    return BootstrapClockStatus::Success;
}

namespace {
uint64_t publishMonotonicApicCounter(uint64_t candidate) noexcept {
    uint64_t observed = g_bootstrapApicClockLastCounter.load(std::memory_order_acquire);
    while (candidate > observed &&
           !g_bootstrapApicClockLastCounter.compare_exchange_weak(
               observed, candidate, std::memory_order_acq_rel,
               std::memory_order_acquire)) {}
    return candidate > observed ? candidate : observed;
}

BootstrapClockStatus queryBootstrapApicClock(uint64_t& counter,
                                             uint64_t& frequency) noexcept {
    if (!g_schedulerClockActive || !g_schedulerTimerPeriod)
        return BootstrapClockStatus::ClockUnavailable;
    const uint64_t calibratedFrequency =
        static_cast<uint64_t>(g_schedulerTimerPeriod) * 100;
    if (!calibratedFrequency) return BootstrapClockStatus::ClockUnavailable;

    const uint64_t totalTicks = g_bootstrapApicClockTicks.load(std::memory_order_acquire);
    if (totalTicks < g_bootstrapApicClockTickOrigin)
        return BootstrapClockStatus::ClockUnavailable;
    const uint64_t elapsedTicks = totalTicks - g_bootstrapApicClockTickOrigin;
    if (elapsedTicks > UINT64_MAX / g_schedulerTimerPeriod)
        return BootstrapClockStatus::CounterOverflow;
    uint64_t candidate = elapsedTicks * g_schedulerTimerPeriod;

    // xAPIC exposes its calibrated down-counter. Interpolate only while the
    // timer IRR is clear and the monotonically increasing interrupt count is
    // unchanged across the sample; otherwise retain the safe 100-Hz tick.
    // The atomic high-water mark prevents a pending/masked interrupt race from
    // making QPC regress even when the hardware count has already reloaded.
    uint32_t currentCount{};
    if (g_timerApic.readCurrentCount(currentCount) && currentCount != 0 &&
        currentCount <= g_schedulerTimerPeriod && !g_timerApic.timerInterruptPending()) {
        const uint64_t confirmedTicks =
            g_bootstrapApicClockTicks.load(std::memory_order_acquire);
        if (confirmedTicks == totalTicks && !g_timerApic.timerInterruptPending()) {
            const uint64_t elapsedInPeriod = g_schedulerTimerPeriod - currentCount;
            if (elapsedInPeriod <= UINT64_MAX - candidate)
                candidate += elapsedInPeriod;
        }
    }
    counter = publishMonotonicApicCounter(candidate);
    frequency = calibratedFrequency;
    return BootstrapClockStatus::Success;
}
} // namespace

BootstrapClockStatus queryBootstrapClock(uint64_t& counter,
                                         uint64_t& frequency) noexcept {
    if (ke::g_CurrentCpu.processorNumber != 0)
        return BootstrapClockStatus::NotBootstrapProcessor;
    if (g_bootstrapTscState.load(std::memory_order_acquire) == 2 &&
        g_bootstrapTscFrequency) {
        counter = readSerializedTsc();
        frequency = g_bootstrapTscFrequency;
        return BootstrapClockStatus::Success;
    }
    return queryBootstrapApicClock(counter, frequency);
}

BootstrapClockStatus initializeBootstrapSystemTime(uint64_t utcTime100ns) noexcept {
    if (ke::g_CurrentCpu.processorNumber != 0)
        return BootstrapClockStatus::NotBootstrapProcessor;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return BootstrapClockStatus::InvalidIrql;
    micant::shared::time::SystemTimeFields check{};
    if (!micant::shared::time::fromSystemTime100ns(utcTime100ns, check))
        return BootstrapClockStatus::InvalidParameter;

    uint8_t expected = 0;
    if (!g_bootstrapSystemTimeState.compare_exchange_strong(
            expected, 1, std::memory_order_acq_rel, std::memory_order_acquire))
        return expected == 2 ? BootstrapClockStatus::InvalidParameter
                             : BootstrapClockStatus::ClockUnavailable;

    uint64_t counter{}, frequency{};
    const auto clockStatus = queryBootstrapClock(counter, frequency);
    if (clockStatus != BootstrapClockStatus::Success) {
        g_bootstrapSystemTimeState.store(0, std::memory_order_release);
        return clockStatus;
    }
    g_bootstrapSystemTime100ns = utcTime100ns;
    g_bootstrapSystemTimeCounter = counter;
    g_bootstrapSystemTimeFrequency = frequency;
    g_bootstrapSystemTimeState.store(2, std::memory_order_release);
    return BootstrapClockStatus::Success;
}

BootstrapClockStatus queryBootstrapUptime(uint64_t& uptime100ns) noexcept {
    if (g_bootstrapMonotonicState.load(std::memory_order_acquire) != 2)
        return BootstrapClockStatus::ClockUnavailable;
    uint64_t now{}, frequency{};
    const auto status = queryBootstrapClock(now, frequency);
    if (status != BootstrapClockStatus::Success) return status;
    if (frequency != g_bootstrapMonotonicFrequency ||
        now < g_bootstrapMonotonicCounter)
        return BootstrapClockStatus::ClockUnavailable;
    return scaleTscDelta100ns(now - g_bootstrapMonotonicCounter,
                              frequency, uptime100ns)
        ? BootstrapClockStatus::Success : BootstrapClockStatus::CounterOverflow;
}

BootstrapClockStatus queryBootstrapSystemTime(uint64_t& utcTime100ns) noexcept {
    if (g_bootstrapSystemTimeState.load(std::memory_order_acquire) != 2)
        return BootstrapClockStatus::ClockUnavailable;
    uint64_t now{}, frequency{};
    const auto status = queryBootstrapClock(now, frequency);
    if (status != BootstrapClockStatus::Success) return status;
    if (frequency != g_bootstrapSystemTimeFrequency ||
        now < g_bootstrapSystemTimeCounter)
        return BootstrapClockStatus::ClockUnavailable;
    uint64_t elapsed{};
    if (!scaleTscDelta100ns(now - g_bootstrapSystemTimeCounter,
                            frequency, elapsed) ||
        elapsed > UINT64_MAX - g_bootstrapSystemTime100ns)
        return BootstrapClockStatus::CounterOverflow;
    utcTime100ns = g_bootstrapSystemTime100ns + elapsed;
    return BootstrapClockStatus::Success;
}

BootstrapClockStatus queryBootstrapPerformanceCounter(
    uint64_t& counter, uint64_t& frequency) noexcept {
    return queryBootstrapClock(counter, frequency);
}

BootstrapRtcStatus readBootstrapRtc(
    micant::shared::time::SystemTimeFields& fields) noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    if (ke::g_CurrentCpu.processorNumber != 0)
        return BootstrapRtcStatus::UnsupportedPlatform;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return BootstrapRtcStatus::InvalidIrql;

    uint64_t flags{};
    uint8_t priorIndex{};
    __asm__ __volatile__("pushfq; popq %0; cli; inb $0x70, %1"
                         : "=r"(flags), "=a"(priorIndex) : : "memory");
    while (g_cmosRtcLock.test_and_set(std::memory_order_acquire))
        __asm__ __volatile__("pause" : : : "memory");
    const uint8_t nmiMask = priorIndex & 0x80;
    CmosSnapshot first{}, second{};
    bool stable = false;
    bool invalidRtc = false;
    bool timedOut = false;
    for (uint32_t attempt = 0; attempt < 8; ++attempt) {
        if (!waitForRtcUpdateClear(nmiMask)) { timedOut = true; break; }
        if (!readCmosSnapshot(nmiMask, first)) { invalidRtc = true; break; }
        if (!readCmosSnapshot(nmiMask, second)) { invalidRtc = true; break; }
        if (sameCmosTime(first, second)) { stable = true; break; }
    }
    __asm__ __volatile__("outb %0, $0x70" : : "a"(priorIndex) : "memory");
    g_cmosRtcLock.clear(std::memory_order_release);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (!stable) return timedOut ? BootstrapRtcStatus::Timeout
                                 : (invalidRtc ? BootstrapRtcStatus::InvalidData
                                               : BootstrapRtcStatus::Timeout);

    const bool binary = (first.statusB & 0x04) != 0;
    const bool twentyFourHour = (first.statusB & 0x02) != 0;
    const bool pm = (first.hour & 0x80) != 0;
    uint8_t secondValue{}, minuteValue{}, hourValue{}, dayValue{}, monthValue{};
    uint8_t yearValue{}, centuryValue{};
    if (!decodeCmosValue(first.second, binary, secondValue) ||
        !decodeCmosValue(first.minute, binary, minuteValue) ||
        !decodeCmosValue(static_cast<uint8_t>(first.hour & 0x7f), binary, hourValue) ||
        !decodeCmosValue(first.day, binary, dayValue) ||
        !decodeCmosValue(first.month, binary, monthValue) ||
        !decodeCmosValue(first.year, binary, yearValue))
        return BootstrapRtcStatus::InvalidData;
    if (twentyFourHour) {
        if (pm) return BootstrapRtcStatus::InvalidData;
    } else {
        if (hourValue < 1 || hourValue > 12) return BootstrapRtcStatus::InvalidData;
        hourValue = static_cast<uint8_t>((hourValue % 12) + (pm ? 12 : 0));
    }

    uint16_t fullYear{};
    if (decodeCmosValue(first.century, binary, centuryValue) &&
        centuryValue >= 19 && centuryValue <= 99) {
        fullYear = static_cast<uint16_t>(centuryValue * 100 + yearValue);
    } else {
        // Explicit no-century-register policy: DOS-compatible pivot, 00..69
        // maps to 2000..2069 and 70..99 maps to 1970..1999. The read status
        // does not claim the century register was valid.
        fullYear = static_cast<uint16_t>(yearValue >= 70
            ? 1900 + yearValue : 2000 + yearValue);
    }
    micant::shared::time::SystemTimeFields decoded{
        fullYear, monthValue, dayValue, hourValue, minuteValue, secondValue, 0, 0};
    if (!micant::shared::time::isValid(decoded))
        return BootstrapRtcStatus::InvalidData;
    decoded.weekday = static_cast<uint16_t>(
        (micant::shared::time::daysSince1601(decoded.year, decoded.month, decoded.day) + 1) % 7);
    fields = decoded;
    return BootstrapRtcStatus::Success;
#else
    (void)fields;
    return BootstrapRtcStatus::UnsupportedPlatform;
#endif
}

micant::KUserSharedData* bootstrapKuserSharedData() noexcept {
    const uintptr_t address = g_kuserKernelAliasReady
        ? static_cast<uintptr_t>(kKuserKernelAlias)
        : reinterpret_cast<uintptr_t>(g_kuserSharedPage);
    return reinterpret_cast<micant::KUserSharedData*>(address);
}

BootstrapClockStatus updateBootstrapKuserSharedData() noexcept {
    if (!g_kuserSharedMappingReady) return BootstrapClockStatus::ClockUnavailable;
    uint64_t uptime{}, systemTime{};
    const auto uptimeStatus = queryBootstrapUptime(uptime);
    if (uptimeStatus != BootstrapClockStatus::Success) return uptimeStatus;    const auto timeStatus = queryBootstrapSystemTime(systemTime);
    auto* shared = bootstrapKuserSharedData();
    if (timeStatus == BootstrapClockStatus::Success) {
        const uint32_t high = static_cast<uint32_t>(systemTime >> 32);
        // KSYSTEM_TIME readers sample High1, Low, High2 and retry on a
        // mismatch. Publish High2 first and High1 last around the low word.
        shared->systemTimeHigh2 = static_cast<int32_t>(high);
        std::atomic_thread_fence(std::memory_order_release);
        shared->systemTimeLow = static_cast<uint32_t>(systemTime);
        std::atomic_thread_fence(std::memory_order_release);
        shared->systemTimeHigh1 = static_cast<int32_t>(high);
    } else if (timeStatus == BootstrapClockStatus::ClockUnavailable) {
        // Do not leave a stale wall clock visible when UTC has no valid seed.
        // Preserve the KSYSTEM_TIME high/low/high reader contract even while
        // transitioning back to the unavailable state.
        shared->systemTimeHigh2 = 0;
        std::atomic_thread_fence(std::memory_order_release);
        shared->systemTimeLow = 0;
        std::atomic_thread_fence(std::memory_order_release);
        shared->systemTimeHigh1 = 0;
    }
    const uint32_t interruptHigh = static_cast<uint32_t>(uptime >> 32);
    shared->interruptTimeHigh2 = static_cast<int32_t>(interruptHigh);
    std::atomic_thread_fence(std::memory_order_release);
    shared->interruptTimeLow = static_cast<uint32_t>(uptime);
    std::atomic_thread_fence(std::memory_order_release);
    shared->interruptTimeHigh1 = static_cast<int32_t>(interruptHigh);
    shared->tickCountLowDeprecated = static_cast<uint32_t>(uptime / 10'000);
    shared->tickCountQuad = uptime / 10'000;
    std::atomic_thread_fence(std::memory_order_release);
    return timeStatus == BootstrapClockStatus::Success ||
        timeStatus == BootstrapClockStatus::ClockUnavailable
        ? BootstrapClockStatus::Success : timeStatus;
}

BootstrapClockStatus initializeBootstrapKuserSharedData() noexcept {
    if (ke::g_CurrentCpu.processorNumber != 0)
        return BootstrapClockStatus::NotBootstrapProcessor;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return BootstrapClockStatus::InvalidIrql;
    if (g_kuserSharedMappingReady) return updateBootstrapKuserSharedData();
    if (g_bootstrapMonotonicState.load(std::memory_order_acquire) != 2) {
        if (g_log) g_log("[MicaNT Hal] KUSER monotonic calibration unavailable\r\n");
        return BootstrapClockStatus::ClockUnavailable;
    }

    const uint64_t virtualPage = reinterpret_cast<uint64_t>(g_kuserSharedPage);
    const uint64_t physicalPage = walk(reinterpret_cast<uint64_t>(g_pml4), virtualPage);
    if (!physicalPage || (physicalPage & 4095) || physicalPage > kPhysicalMask) {
        if (g_log) g_log("[MicaNT Hal] KUSER backing page translation failed\r\n");
        return BootstrapClockStatus::ClockUnavailable;
    }
    const uint64_t rootPhysical = walk(reinterpret_cast<uint64_t>(g_pml4),
                                       reinterpret_cast<uint64_t>(g_pml4));
    if (!installKuserKernelAliases(physicalPage, rootPhysical)) {
        if (g_log) g_log("[MicaNT Hal] KUSER kernel aliases could not be installed\r\n");
        return BootstrapClockStatus::ClockUnavailable;
    }
    g_kuserKernelAliasReady = true;
    auto* shared = bootstrapKuserSharedData();
    std::memset(shared, 0, sizeof(*shared));
    if (!installKuserUserMapping(physicalPage,
            reinterpret_cast<uint64_t*>(kKuserRootAlias))) {
        g_kuserKernelAliasReady = false;
        if (!unmapPage(kKuserKernelAlias, false)) __builtin_trap();
        if (!unmapPage(kKuserRootAlias, false)) __builtin_trap();
        if (g_log) g_log("[MicaNT Hal] KUSER user mapping transaction failed\r\n");
        return BootstrapClockStatus::ClockUnavailable;
    }
    g_kuserSharedMappingReady = true;
    shared->tickCountMultiplier = 0xFA000000u;
    shared->timeZoneBiasHigh1 = 0;
    shared->timeZoneBiasLow = 0;
    shared->timeZoneBiasHigh2 = 0;
    shared->timeZoneId = 1; // UTC standard time until a zone provider is configured.
    const auto updateStatus = updateBootstrapKuserSharedData();
    const uint64_t root = reinterpret_cast<uint64_t>(activePagingRoot());
    const uint64_t kernelBacking = walk(root, kKuserKernelAlias) & ~uint64_t{4095};
    const uint64_t userBacking = walk(root, micant::UserSharedDataAddress) & ~uint64_t{4095};
    bool aliasUser{}, aliasWritable{}, userUser{}, userWritable{};
    const bool aliasPermissions = queryPagingPermissions(root, kKuserKernelAlias,
        aliasUser, aliasWritable);
    const bool userPermissions = queryPagingPermissions(root, micant::UserSharedDataAddress,
        userUser, userWritable);
    if (kernelBacking != physicalPage || userBacking != physicalPage ||
        !aliasPermissions || aliasUser || !aliasWritable || !userPermissions ||
        !userUser || userWritable ||
        !mappedRange(micant::UserSharedDataAddress, sizeof(*shared), true, false, false) ||
        updateStatus != BootstrapClockStatus::Success) {
        if (g_log) g_log("[MicaNT Hal] KUSER alias/backing permissions or initial update invalid\r\n");
        return BootstrapClockStatus::ClockUnavailable;
    }
    if (g_log) {
        uint64_t utc{};
        const bool utcAvailable =
            queryBootstrapSystemTime(utc) == BootstrapClockStatus::Success;
        if (!utcAvailable &&
            (shared->systemTimeLow || shared->systemTimeHigh1 || shared->systemTimeHigh2))
            g_log("[MicaNT Hal] KUSER unavailable UTC fields are nonzero after initialization\r\n");
        else if (!utcAvailable)
            g_log("[MicaNT Hal] KUSER backing PFN shared by supervisor RW alias and user RO view; UTC zero\r\n");
        else
            g_log("[MicaNT Hal] KUSER backing PFN shared by supervisor RW alias and user RO view; UTC seeded\r\n");
    }
    if (g_log) g_log("[MicaNT Hal] KUSER backing page has a supervisor RW alias and a user RO mapping\r\n");
    return BootstrapClockStatus::Success;
}

static BootstrapClockStatus deadlineFromCounter(uint64_t now, uint64_t frequency,
                                                 uint32_t timeoutMilliseconds,
                                                 BootstrapDeadline& deadline) noexcept {
    if (!timeoutMilliseconds || timeoutMilliseconds > 10000)
        return BootstrapClockStatus::InvalidParameter;
    if (!frequency) return BootstrapClockStatus::ClockUnavailable;
    if (frequency > UINT64_MAX / timeoutMilliseconds)
        return BootstrapClockStatus::CounterOverflow;
    const uint64_t product = frequency * timeoutMilliseconds;
    const uint64_t ticks = product / 1000 + (product % 1000 != 0);
    if (!ticks || now > UINT64_MAX - ticks)
        return BootstrapClockStatus::CounterOverflow;
    deadline.counter = now + ticks;
    return BootstrapClockStatus::Success;
}

BootstrapClockStatus makeBootstrapDeadline(uint32_t timeoutMilliseconds,
                                           BootstrapDeadline& deadline) noexcept {
    uint64_t now{}, frequency{};
    const auto status = queryBootstrapClock(now, frequency);
    if (status != BootstrapClockStatus::Success) return status;
    return deadlineFromCounter(now, frequency, timeoutMilliseconds, deadline);
}

BootstrapClockStatus isBootstrapDeadlineExpired(const BootstrapDeadline& deadline,
                                                bool& expired) noexcept {
    if (!deadline.counter) return BootstrapClockStatus::InvalidParameter;
    uint64_t now{}, frequency{};
    const auto status = queryBootstrapClock(now, frequency);
    if (status != BootstrapClockStatus::Success) return status;
    expired = now >= deadline.counter;
    return BootstrapClockStatus::Success;
}

BootstrapClockStatus delayBootstrapMilliseconds(uint32_t milliseconds) noexcept {
    if (!milliseconds) return BootstrapClockStatus::Success;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return BootstrapClockStatus::InvalidIrql;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags) : : "memory");
    if (!(flags & (uint64_t{1} << 9))) return BootstrapClockStatus::InvalidIrql;

    BootstrapDeadline deadline{};
    const auto status = makeBootstrapDeadline(milliseconds, deadline);
    if (status != BootstrapClockStatus::Success) return status;
    for (;;) {
        uint64_t counter{}, frequency{};
        const auto queryStatus = queryBootstrapClock(counter, frequency);
        if (queryStatus != BootstrapClockStatus::Success) return queryStatus;
        if (counter >= deadline.counter) break;
        __asm__ __volatile__("pause" : : : "memory");
    }
    return BootstrapClockStatus::Success;
}

bool verifyBootstrapClockTimeouts() noexcept {
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags) : : "memory");
    const bool restoreDisabledInterrupts = !(flags & (uint64_t{1} << 9));
    struct InterruptStateRestore {
        bool disabled;
        ~InterruptStateRestore() noexcept {
            if (disabled) __asm__ __volatile__("cli" : : : "memory");
        }
    } restore{restoreDisabledInterrupts};
    if (restoreDisabledInterrupts) __asm__ __volatile__("sti" : : : "memory");
    BootstrapDeadline deadline{};
    if (makeBootstrapDeadline(0, deadline) != BootstrapClockStatus::InvalidParameter ||
        makeBootstrapDeadline(10001, deadline) != BootstrapClockStatus::InvalidParameter ||
        deadlineFromCounter(UINT64_MAX - 999999, 1'000'000'000, 1, deadline) !=
            BootstrapClockStatus::CounterOverflow ||
        deadlineFromCounter(UINT64_MAX - 1, 1000, 1, deadline) !=
            BootstrapClockStatus::Success || deadline.counter != UINT64_MAX ||
        makeBootstrapDeadline(20, deadline) != BootstrapClockStatus::Success)
        return false;
    bool expired = true;
    if (isBootstrapDeadlineExpired(deadline, expired) != BootstrapClockStatus::Success || expired)
        return false;

    const uint64_t ticksBefore =
        g_bootstrapApicClockTicks.load(std::memory_order_acquire);
    if (delayBootstrapMilliseconds(20) != BootstrapClockStatus::Success ||
        isBootstrapDeadlineExpired(deadline, expired) != BootstrapClockStatus::Success ||
        !expired)
        return false;
    // Check the TSC scale against the independent 100-Hz APIC clock. A full
    // second reduces quantization error from its 10-ms timer interval.
    const uint64_t calibrationStartTicks =
        g_bootstrapApicClockTicks.load(std::memory_order_acquire);
    if (delayBootstrapMilliseconds(1000) != BootstrapClockStatus::Success)
        return false;
    const uint64_t finalApicTicks =
        g_bootstrapApicClockTicks.load(std::memory_order_acquire);
    const uint64_t elapsedApicTicks = finalApicTicks - calibrationStartTicks;
    return finalApicTicks > ticksBefore && elapsedApicTicks >= 90 &&
        elapsedApicTicks <= 110;
}

BootstrapClockStatus delayBootstrapMicroseconds(uint32_t microseconds) noexcept {
    if (!microseconds) return BootstrapClockStatus::Success;
    if (microseconds > 50)
        return BootstrapClockStatus::InvalidParameter;
    if (ke::KeGetCurrentIrql() > ke::DISPATCH_LEVEL)
        return BootstrapClockStatus::InvalidIrql;

    uint64_t now{}, frequency{};
    const auto status = queryBootstrapClock(now, frequency);
    if (status != BootstrapClockStatus::Success) return status;
    if (frequency > UINT64_MAX / microseconds)
        return BootstrapClockStatus::CounterOverflow;
    const uint64_t product = frequency * microseconds;
    const uint64_t ticks = product / 1000000 + (product % 1000000 != 0);
    if (!ticks || now > UINT64_MAX - ticks)
        return BootstrapClockStatus::CounterOverflow;
    const uint64_t deadline = now + ticks;
    while (readSerializedTsc() < deadline)
        __asm__ __volatile__("pause" : : : "memory");
    return BootstrapClockStatus::Success;
}

bool verifyRing3Transition() noexcept {
    if (!g_addressSpaceReady ||
        !mapPage(kUserBase, reinterpret_cast<uint64_t>(g_userCode), kUser)) {
        return false;
    }
    for (size_t offset = 0; offset < sizeof(g_userStack); offset += 4096) {
        if (!mapPage(kUserStackBase + offset,
                     reinterpret_cast<uint64_t>(g_userStack) + offset,
                     kUser | kWritable | kNoExecute)) {
            while (offset) {
                offset -= 4096;
                (void)unmapPage(kUserStackBase + offset, true);
            }
            (void)unmapPage(kUserBase, true);
            return false;
        }
    }
    const bool firstTeb = mapPage(kUserTebBase, reinterpret_cast<uint64_t>(&g_userTebStorage),
                                  kUser | kWritable | kNoExecute);
    if (!firstTeb ||
        !mapPage(kUserTebBase + 4096, reinterpret_cast<uint64_t>(&g_userTebStorage) + 4096,
                 kUser | kWritable | kNoExecute)) {
        if (firstTeb) (void)unmapPage(kUserTebBase, true);
        for (size_t offset = 0; offset < sizeof(g_userStack); offset += 4096) {
            (void)unmapPage(kUserStackBase + offset, true);
        }
        (void)unmapPage(kUserBase, true);
        return false;
    }
    g_userTebStorage.teb.self = kUserTebBase;
    g_userTebStorage.teb.uniqueProcessId = 0x11223344;
    g_userTebStorage.teb.uniqueThreadId = 0x55667788;
    // Read CS at CPL=3, verify RPL=3, then enter the kernel via #BP.
    constexpr uint8_t code[]{
        0x8c, 0xc8,             // mov ax, cs
        0x83, 0xe0, 0x03,       // and eax, 3
        0x83, 0xf8, 0x03,       // cmp eax, 3
        0x75, 0x02,             // jne fault
        0xcc,                   // int3
        0x90,                   // nop (unreachable)
        0x0f, 0x0b,             // ud2
    };
    for (size_t index = 0; index < sizeof(code); ++index) {
        g_userCode[index] = code[index];
    }
    g_userStack[0] = 0xCC; // A missing NX bit must trap, not execute arbitrary bytes.
    auto& hardware = cpu::CpuHardwareEngine::get();
    hardware.initialize(reinterpret_cast<uint64_t>(g_kernelStack + sizeof(g_kernelStack)), 0);
    const auto& gdtr = hardware.getGdtr();
    __asm__ __volatile__(
        "cli\nlgdt %0\n"
        "pushq $0x10\nleaq 1f(%%rip), %%rax\npushq %%rax\nlretq\n1:\n"
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\nmovw %%ax, %%ss\n"
        "movw $0x40, %%ax\nltr %%ax\n"
        : : "m"(gdtr) : "rax", "memory"
    );
    for (size_t vector = 0; vector < 256; ++vector) {
        const uint64_t handler = vector == 0
            ? reinterpret_cast<uint64_t>(MicantProbeUserDivideEntry)
            : vector == 1
                ? reinterpret_cast<uint64_t>(MicantProbeUserSingleStepEntry)
            : vector == 3
            ? reinterpret_cast<uint64_t>(MicantProbeUserBreakpointEntry)
            : vector == 4
                ? reinterpret_cast<uint64_t>(MicantProbeUserOverflowEntry)
            : vector == 5
                ? reinterpret_cast<uint64_t>(MicantProbeUserBoundsEntry)
            : vector == 6
                ? reinterpret_cast<uint64_t>(MicantProbeUserInvalidOpcodeEntry)
            : vector == 10 ? reinterpret_cast<uint64_t>(MicantProbeUserTaskSegmentEntry)
            : vector == 11 ? reinterpret_cast<uint64_t>(MicantProbeUserSegmentNotPresentEntry)
            : vector == 12 ? reinterpret_cast<uint64_t>(MicantProbeUserStackSegmentEntry)
            : vector == 13 ? reinterpret_cast<uint64_t>(MicantProbeUserGeneralProtectionEntry)
            : vector == 17 ? reinterpret_cast<uint64_t>(MicantProbeUserAlignmentEntry)
            : vector == 21 ? reinterpret_cast<uint64_t>(MicantProbeUserControlProtectionEntry)
            : vector == 16
                ? reinterpret_cast<uint64_t>(MicantProbeUserFloatEntry)
            : vector == 19
                ? reinterpret_cast<uint64_t>(MicantProbeUserSimdFloatEntry)
            : vector == 14 ? reinterpret_cast<uint64_t>(MicantProbePageFaultEntry)
                          : reinterpret_cast<uint64_t>(MicantProbeFaultEntry);
        g_idt[vector] = {
            static_cast<uint16_t>(handler),
            cpu::SELECTOR_KCODE64, 1,
            static_cast<uint8_t>(vector == 3 ? 0xEE : 0x8E),
            static_cast<uint16_t>(handler >> 16),
            static_cast<uint32_t>(handler >> 32), 0,
        };
    }
    const Idtr idtr{sizeof(g_idt) - 1, reinterpret_cast<uint64_t>(g_idt)};
    __asm__ __volatile__("lidt %0" : : "m"(idtr) : "memory");
    // Exercise IDT restoration while the CPL3 probe table is actually active.
    if (bootstrapQemuFaultTestsAvailable() &&
        !verifyBootstrapManagedPageFault(BootstrapManagedBase, false, false)) {
        return false;
    }
    g_probeReturnRip = kUserBase + 11;
    MicantProbeEnter();
    if (!g_probeSucceeded) {
        return false;
    }
    // User accesses must fault: kernel/MMIO reads, code writes, stack execution.
    const uint64_t apicBase = cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE);
    const bool x2Apic = (apicBase & (uint64_t{1} << 10)) != 0;
    bool apicMmioMapped = false;
    if (!x2Apic && (apicBase & (uint64_t{1} << 11))) {
        const uint64_t physicalApicPage = apicBase & kPhysicalMask;
        apicMmioMapped = physicalApicPage &&
            mapPage(kDeviceBase, physicalApicPage,
                    kWritable | kNoExecute | 0x18ULL);
        if (!apicMmioMapped) return false;
    }
    const uint32_t lastMode = apicMmioMapped ? 4 : 3;
    for (uint32_t mode = 1; mode <= lastMode; ++mode) {
        g_probeMode = mode;
        g_probeSucceeded = false;
        g_userCode[0] = 0x48;
        g_userCode[1] = 0xB8; // movabs rax, address
        const uint64_t address = mode == 1
            ? reinterpret_cast<uint64_t>(g_pml4)
            : mode == 2 ? kUserBase
            : mode == 3 ? kUserBase + 4096 : kDeviceBase;
        for (size_t byte = 0; byte < sizeof(address); ++byte) {
            g_userCode[2 + byte] = static_cast<uint8_t>(address >> (byte * 8));
        }
        g_userCode[10] = mode == 3 ? 0xFF : mode == 2 ? 0x89 : 0x8B;
        g_userCode[11] = mode == 3 ? 0xE0 : 0x00; // jmp rax / write / read
        g_userCode[12] = 0x0F;
        g_userCode[13] = 0x0B; // ud2 if protection was bypassed
        MicantProbeEnter();
        if (!g_probeSucceeded) {
            if (apicMmioMapped) (void)unmapPage(kDeviceBase, false);
            return false;
        }
    }
    if (apicMmioMapped) {
        if (!unmapPage(kDeviceBase, false)) return false;
        if (g_log) g_log("[MicaNT Mm] Supervisor APIC MMIO page blocked at CPL3\r\n");
    } else if (g_log) {
        g_log("[MicaNT Mm] APIC MMIO CPL3 probe skipped: x2APIC mode\r\n");
    }
    constexpr uint8_t tebCode[]{
        0x65, 0xC7, 0x04, 0x25, 0x68, 0x00, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x65, 0x8B, 0x04, 0x25, 0x68, 0x00, 0x00, 0x00,
        0x3D, 0x78, 0x56, 0x34, 0x12,
        0x75, 0x02,
        0x65, 0x48, 0xC7, 0x04, 0x25, 0x80, 0x14, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x65, 0x8B, 0x04, 0x25, 0x80, 0x14, 0x00, 0x00,
        0x3D, 0x78, 0x56, 0x34, 0x12,
        0x75, 0x02,
        0x65, 0x8B, 0x04, 0x25, 0x40, 0x00, 0x00, 0x00,
        0x3D, 0x44, 0x33, 0x22, 0x11,
        0x75, 0x02,
        0x65, 0x8B, 0x04, 0x25, 0x48, 0x00, 0x00, 0x00,
        0x3D, 0x88, 0x77, 0x66, 0x55,
        0x75, 0x02, 0xCC, 0x0F, 0x0B,
    };
    for (size_t index = 0; index < sizeof(tebCode); ++index) {
        g_userCode[index] = tebCode[index];
    }
    g_probeMode = 7;
    g_probeSucceeded = false;
    g_probeReturnRip = kUserBase + sizeof(tebCode) - 2;
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, kUserTebBase);
    MicantProbeEnter();
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, 0);
    if (!g_probeSucceeded ||
        g_userTebStorage.teb.lastErrorValue != 0x12345678 ||
        g_userTebStorage.teb.tlsSlots[0] != 0x12345678 ||
        g_userTebStorage.teb.uniqueProcessId != 0x11223344 ||
        g_userTebStorage.teb.uniqueThreadId != 0x55667788) {
        return false;
    }
    __asm__ __volatile__(
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\n"
        : : : "rax", "memory"
    );
    return g_probeSucceeded;
}

} // namespace micant::hal::amd64
