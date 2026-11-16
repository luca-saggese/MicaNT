#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <atomic>
#include "micant/km/core/crash_record.hpp"

namespace micant::ke {

inline constexpr size_t CrashDumpBufferBytes = 4096;
inline constexpr uint32_t CrashDumpRecordContext = 7;
inline constexpr uint32_t CrashDumpRecordStack = 8;
inline constexpr uint32_t CrashDumpRecordEnd = 0xffffffffu;
inline constexpr uint16_t CrashDumpHeaderFlags = 1u << 2;

struct CrashDumpRawBackend final {
    void* Context{};
    bool (*Read)(void*, uint64_t, uint16_t, void*) noexcept{};
    bool (*Write)(void*, uint64_t, uint16_t, const void*) noexcept{};
    NtStatus (*Flush)(void*) noexcept{};
    uint64_t CapacitySectors{};
};

struct CrashDumpHeader final {
    char signature[8];
    uint32_t version;
    uint32_t size;
    uint16_t architecture;
    uint16_t flags;
    uint32_t pid;
    uint64_t buildId;
    uint64_t captureTime;
};
struct CrashDumpRecordHeader final { uint32_t type, size, crc32; };
struct CrashDumpDataDescriptor final { uint64_t base; uint32_t size, crc32; };
static_assert(sizeof(CrashDumpHeader) == 40);
static_assert(sizeof(CrashDumpRecordHeader) == 12);
static_assert(sizeof(CrashDumpDataDescriptor) == 16);

namespace detail {
alignas(4096) inline uint8_t crashDumpBuffer[CrashDumpBufferBytes]{};
alignas(4096) inline uint8_t pendingCrashDumpBuffer[CrashDumpBufferBytes]{};
alignas(4096) inline uint8_t crashDumpClearSector[512]{};
inline std::atomic_flag crashDumpBufferBusy = ATOMIC_FLAG_INIT;
inline uint32_t pendingCrashDumpLength{};
inline uint64_t pendingCrashDumpId{};
inline bool pendingCrashDumpReady{};
inline CrashDumpRawBackend crashDumpBackend{};

inline bool appendCrashBytes(size_t& offset, const void* source, size_t size) noexcept {
    if (offset > CrashDumpBufferBytes || size > CrashDumpBufferBytes - offset) return false;
    std::memcpy(crashDumpBuffer + offset, source, size);
    offset += size;
    return true;
}

inline bool appendCrashRecord(size_t& offset, uint32_t type,
                              const void* payload, uint32_t size) noexcept {
    const CrashDumpRecordHeader header{type, size,
        size ? crashRecordCrc32(payload, size) : 0};
    return appendCrashBytes(offset, &header, sizeof(header)) &&
           appendCrashBytes(offset, payload, size);
}
} // namespace detail

// The canonical AHCI boot-start driver supplies an already claimed controller
// and its bounded raw-partition callbacks. Initialization requires a real
// media-cache flush before enabling the fatal writer.
[[nodiscard]] inline bool initializeCrashDumpBackend(
    const CrashDumpRawBackend& backend) noexcept {
    if (!backend.Context || !backend.Read || !backend.Write || !backend.Flush ||
        backend.CapacitySectors < CrashDumpBufferBytes / 512) return false;
    detail::crashDumpBackend = backend;
    if (backend.Flush(backend.Context) != NtStatus::Success) {
        detail::crashDumpBackend = {};
        return false;
    }
    return true;
}

inline void clearCrashDumpBackend() noexcept {
    detail::crashDumpBackend = {};
    detail::pendingCrashDumpReady = false;
    detail::pendingCrashDumpLength = 0;
    detail::pendingCrashDumpId = 0;
}

// Crash path. This uses a bounded static buffer and only the validated raw
// partition API. An atomic claim fails immediately on re-entry; it never waits
// on a possibly interrupted writer. No filesystem or allocator is involved.
[[nodiscard]] inline bool persistCrashRecord(const CrashRecord& crash) noexcept {
    if (crash.signature != CrashRecordSignature ||
        crash.version != CrashRecordVersion ||
        crash.size != CrashRecordHeaderSize ||
        crash.stackBytes > CrashStackCaptureBytes) return false;
    if (detail::crashDumpBufferBusy.test_and_set(std::memory_order_acquire)) return false;
    auto release = []() noexcept {
        detail::crashDumpBufferBusy.clear(std::memory_order_release);
    };
    std::memset(detail::crashDumpBuffer, 0, sizeof(detail::crashDumpBuffer));
    size_t offset = 0;
    const CrashDumpHeader header{{'L','U','C','D','M','P','1','\0'}, 1,
        sizeof(CrashDumpHeader), 0x8664, CrashDumpHeaderFlags, 0,
        crash.buildId, 0};
    bool encoded = detail::appendCrashBytes(offset, &header, sizeof(header)) &&
        detail::appendCrashRecord(offset, CrashDumpRecordContext,
                                  &crash, CrashRecordHeaderSize);
    if (encoded && crash.stackBytes) {
        const CrashDumpDataDescriptor descriptor{crash.rsp, crash.stackBytes,
            crashRecordCrc32(crash.stack, crash.stackBytes)};
        uint8_t payload[sizeof(descriptor) + CrashStackCaptureBytes]{};
        std::memcpy(payload, &descriptor, sizeof(descriptor));
        std::memcpy(payload + sizeof(descriptor), crash.stack, crash.stackBytes);
        encoded = detail::appendCrashRecord(offset, CrashDumpRecordStack, payload,
            static_cast<uint32_t>(sizeof(descriptor) + crash.stackBytes));
    }
    if (encoded) encoded = detail::appendCrashRecord(offset, CrashDumpRecordEnd, nullptr, 0);
    const size_t sectors = (offset + 511) / 512;
    if (!encoded || !sectors || sectors > UINT16_MAX) {
        release();
        return false;
    }
    const size_t paddedBytes = sectors * 512;
    // Ensure the padding is deterministic and included in the raw write.
    if (paddedBytes > offset)
        std::memset(detail::crashDumpBuffer + offset, 0, paddedBytes - offset);
    const auto& backend = detail::crashDumpBackend;
    const bool written = backend.Write && backend.Write(backend.Context, 0,
        static_cast<uint16_t>(sectors), detail::crashDumpBuffer);
    const bool durable = written && backend.Flush(backend.Context) == NtStatus::Success;
    release();
    return durable;
}

// Boot-time recovery. The Ring 3 service performs full CRC and semantic
// validation before export; the kernel only bounds the record walk so an
// arbitrary partition payload cannot make it scan outside the fixed buffer.
[[nodiscard]] inline bool loadPendingCrashDump() noexcept {
    const auto& backend = detail::crashDumpBackend;
    if (!backend.Read || backend.CapacitySectors < CrashDumpBufferBytes / 512 ||
        !backend.Read(backend.Context, 0, CrashDumpBufferBytes / 512,
                      detail::pendingCrashDumpBuffer)) return false;

    const auto* header = reinterpret_cast<const CrashDumpHeader*>(
        detail::pendingCrashDumpBuffer);
    if (std::memcmp(header->signature, "LUCDMP1\0", 8) != 0 ||
        header->version != 1 || header->size != sizeof(CrashDumpHeader) ||
        header->architecture != 0x8664 ||
        !(header->flags & CrashDumpHeaderFlags) || header->pid != 0) return false;

    size_t offset = sizeof(CrashDumpHeader);
    while (offset <= CrashDumpBufferBytes - sizeof(CrashDumpRecordHeader)) {
        CrashDumpRecordHeader record{};
        std::memcpy(&record, detail::pendingCrashDumpBuffer + offset, sizeof(record));
        offset += sizeof(record);
        if (record.size > CrashDumpBufferBytes - offset) return false;
        if (record.type == CrashDumpRecordEnd) {
            if (record.size || record.crc32) return false;
            detail::pendingCrashDumpLength = static_cast<uint32_t>(offset);
            detail::pendingCrashDumpId = crashRecordCrc32(
                detail::pendingCrashDumpBuffer, detail::pendingCrashDumpLength);
            detail::pendingCrashDumpReady = true;
            return true;
        }
        offset += record.size;
    }
    return false;
}

[[nodiscard]] inline NtStatus queryPendingCrashDump(
    const uint8_t*& bytes, uint32_t& length, uint64_t& dumpId) noexcept {
    bytes = nullptr;
    length = 0;
    dumpId = 0;
    if (!detail::pendingCrashDumpReady) return NtStatus::NoMoreFiles;
    bytes = detail::pendingCrashDumpBuffer;
    length = detail::pendingCrashDumpLength;
    dumpId = detail::pendingCrashDumpId;
    return NtStatus::Success;
}

// Called only after Ring 3 has written and flushed the exported .dmp file.
// The slot is freed only after its invalidation sector is flushed successfully.
[[nodiscard]] inline NtStatus acknowledgePendingCrashDump(uint64_t dumpId) noexcept {
    if (!detail::pendingCrashDumpReady || dumpId != detail::pendingCrashDumpId)
        return NtStatus::InvalidParameter;
    std::memset(detail::crashDumpClearSector, 0, sizeof(detail::crashDumpClearSector));
    const auto& backend = detail::crashDumpBackend;
    if (!backend.Write || !backend.Flush ||
        !backend.Write(backend.Context, 0, 1, detail::crashDumpClearSector))
        return NtStatus::DeviceDataError;
    const NtStatus flushStatus = backend.Flush(backend.Context);
    if (flushStatus != NtStatus::Success) return flushStatus;
    std::memset(detail::pendingCrashDumpBuffer, 0, sizeof(detail::pendingCrashDumpBuffer));
    detail::pendingCrashDumpLength = 0;
    detail::pendingCrashDumpId = 0;
    detail::pendingCrashDumpReady = false;
    return NtStatus::Success;
}

} // namespace micant::ke
