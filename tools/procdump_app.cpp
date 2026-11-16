#include "micant/shared/native_process_abi.hpp"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/system_information.h"

#include <cstdint>
#include <cstddef>

using namespace micant;

extern "C" __declspec(dllimport) int32_t NtOpenProcess(
    void**, uint32_t, ObjectAttributes*, ClientId*) noexcept;
extern "C" __declspec(dllimport) int32_t NtOpenThread(
    void**, uint32_t, ObjectAttributes*, ClientId*) noexcept;
extern "C" __declspec(dllimport) int32_t NtClose(void*) noexcept;
extern "C" __declspec(dllimport) int32_t NtTerminateProcess(void*, int32_t) noexcept;
extern "C" __declspec(dllimport) int32_t NtQueryInformationProcess(
    void*, uint32_t, void*, uint32_t, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t NtQueryInformationThread(
    void*, uint32_t, void*, uint32_t, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t NtQueryVirtualMemory(
    void*, const void*, uint32_t, void*, size_t, size_t*) noexcept;
extern "C" __declspec(dllimport) int32_t NtQuerySystemInformation(
    uint32_t, void*, uint32_t, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t NtReadVirtualMemory(
    void*, const void*, void*, size_t, size_t*) noexcept;
extern "C" __declspec(dllimport) uint64_t NtWriteFile(
    void*, void*, void*, void*, IoStatusBlock*, const void*, uint32_t, void*, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t NtFlushBuffersFile(void*, IoStatusBlock*) noexcept;
extern "C" __declspec(dllimport) int32_t NtQuerySystemTime(LargeInteger*) noexcept;
extern "C" __declspec(dllimport) int32_t NtTestAlert() noexcept;
extern "C" __declspec(dllimport) int32_t NtAllocateVirtualMemory(
    void*, void**, uintptr_t, size_t*, uint32_t, uint32_t) noexcept;
extern "C" __declspec(dllimport) int32_t NtFreeVirtualMemory(
    void*, void**, size_t*, uint32_t) noexcept;
extern "C" __declspec(dllimport) int32_t NtCreateFile(
    void**, uint32_t, ObjectAttributes*, IoStatusBlock*, int64_t*, uint32_t,
    uint32_t, uint32_t, uint32_t, void*, uint32_t) noexcept;

namespace {
constexpr uint32_t ProcessQueryInformation = 0x0400;
constexpr uint32_t ProcessVmRead = 0x0010;
constexpr uint32_t ThreadQueryInformation = 0x0040;
constexpr uint32_t ProcessBasicInformationClass = 0;
constexpr uint32_t ProcessVmCountersClass = 3;
constexpr uint32_t SystemProcessInformationClass = 5;
constexpr uint32_t MemCommit = 0x1000;
constexpr uint32_t MemReserve = 0x2000;
constexpr uint32_t MemFree = 0x10000;
constexpr uint32_t PageNoAccess = 0x01;
constexpr uint32_t PageGuard = 0x100;
constexpr uintptr_t UserSpaceMin = 0x10000;
constexpr uintptr_t UserSpaceMax = 0x00007FFFFFFFFFFFULL;
constexpr size_t PageSize = 4096;
constexpr size_t MaxProcessSnapshot = 64 * 1024;
constexpr size_t RegionBatchCount = 128;
constexpr size_t DumpChunkBytes = 64 * 1024;
constexpr uint64_t DumpMaxBytes = 512ULL * 1024 * 1024;
constexpr uint32_t DumpRecordRegion = 1;
constexpr uint32_t DumpRecordData = 2;
constexpr uint32_t DumpRecordUnreadable = 3;
constexpr uint32_t DumpRecordProcess = 4;
constexpr uint32_t DumpRecordThread = 5;
constexpr uint32_t DumpRecordModule = 6;
constexpr uint32_t DumpRecordEnd = 0xffffffffu;

// LuceOS diagnostic dump v1. Records are little-endian AMD64 structures.
// Each data record carries CRC-32; capture is live/best-effort, not atomic.
struct DumpHeader {
    char magic[8]; // "LUCDMP1\0"
    uint32_t version;
    uint32_t headerSize;
    uint16_t architecture; // 0x8664 AMD64
    uint16_t flags; // bit 0: live best-effort; bit 1: mini
    uint32_t pid;
    uint64_t kernelBuildId; // zero means unavailable
    uint64_t captureTime;   // zero means unavailable
};
struct DumpRecordHeader { uint32_t type; uint32_t size; uint32_t payloadCrc32; };
struct DumpRegionRecord {
    uint64_t base;
    uint64_t size;
    uint32_t state;
    uint32_t protect;
    uint32_t type;
    uint32_t reserved;
};
struct DumpDataRecord {
    uint64_t base;
    uint32_t size;
    uint32_t crc32;
};
struct DumpProcessRecord {
    uint32_t pid;
    uint32_t handleCount;
    uint32_t threadCount;
    uint16_t imageNameBytes;
    uint16_t reserved;
};
struct DumpThreadRecord { SystemThreadInformation thread; };
struct DumpModuleRecord {
    uint64_t imageBase;
    uint64_t imageSize;
    uint16_t pathBytes;
    uint16_t reserved[3];
};
struct DumpThreadDetails {
    uint64_t teb;
    uint64_t stackBase;
    uint64_t stackLimit;
};
struct StackRange { uintptr_t base; uintptr_t limit; };
static_assert(sizeof(DumpHeader) == 40);
static_assert(sizeof(DumpRegionRecord) == 32);
static_assert(sizeof(DumpDataRecord) == 16);

struct BasicInformation {
    NtStatus exitStatus{};
    uintptr_t peb{};
    uintptr_t affinity{};
    int32_t priority{};
    Handle pid{};
    Handle parentPid{};
};
struct MemoryBasicInformation64 {
    uintptr_t baseAddress;
    uintptr_t allocationBase;
    uint32_t allocationProtect;
    uint16_t partitionId;
    uint16_t reserved;
    size_t regionSize;
    uint32_t state;
    uint32_t protect;
    uint32_t type;
    uint32_t padding;
};
static_assert(sizeof(MemoryBasicInformation64) == 48);

uint8_t processSnapshot[MaxProcessSnapshot]{};
MemoryWorkingSetExInformation workingSetBatch[RegionBatchCount]{};
uint8_t dumpChunk[DumpChunkBytes]{};
StackRange stackRanges[128]{};
uint64_t dumpWriteOffset{};
uint32_t processMemoryFailureStage{};
int32_t processMemoryLastWriteStatus{};
size_t processMemoryLastWriteBytes{};
int32_t processMemoryQueryStatus{};

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(value);
    return destination;
}
extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < count; ++i) output[i] = input[i];
    return destination;
}

void Copy(void* dst, const void* src, size_t length) noexcept {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < length; ++i) d[i] = s[i];
}

bool WriteStd(const char* data, size_t size) noexcept {
    auto* output = reinterpret_cast<void*>(0x14);
    size_t offset = 0;
    while (offset < size) {
        const auto amount = static_cast<uint32_t>(size - offset > UINT32_MAX
            ? UINT32_MAX : size - offset);
        IoStatusBlock iosb{};
        const auto status = static_cast<int32_t>(NtWriteFile(output, nullptr, nullptr, nullptr,
            &iosb, data + offset, amount, nullptr, nullptr));
        if (status != 0 || iosb.information == 0 || iosb.information > amount) return false;
        offset += iosb.information;
    }
    return true;
}

bool WriteFileAll(void* file, const void* data, size_t size) noexcept {
    size_t offset = 0;
    while (offset < size) {
        const auto amount = static_cast<uint32_t>(size - offset > UINT32_MAX
            ? UINT32_MAX : size - offset);
        if (dumpWriteOffset > INT64_MAX || amount > static_cast<uint64_t>(INT64_MAX) - dumpWriteOffset)
            return false;
        IoStatusBlock iosb{};
        const auto status = static_cast<int32_t>(NtWriteFile(file, nullptr, nullptr, nullptr,
            &iosb, static_cast<const uint8_t*>(data) + offset, amount, nullptr, nullptr));
        if (status != 0 || !iosb.information || iosb.information > amount) {
            processMemoryLastWriteStatus = status ? status : static_cast<int32_t>(iosb.status);
            processMemoryLastWriteBytes = iosb.information;
            return false;
        }
        offset += iosb.information;
        dumpWriteOffset += iosb.information;
    }
    return true;
}

uint32_t Crc32(const uint8_t* bytes, size_t count) noexcept {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

uint32_t Crc32Pair(const void* first, size_t firstSize,
                   const void* second, size_t secondSize) noexcept {
    uint32_t crc = 0xffffffffu;
    const auto update = [&crc](const uint8_t* bytes, size_t count) noexcept {
        for (size_t i = 0; i < count; ++i) {
            crc ^= bytes[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    };
    update(static_cast<const uint8_t*>(first), firstSize);
    update(static_cast<const uint8_t*>(second), secondSize);
    return ~crc;
}

bool WriteRecord(void* file, uint32_t type, const void* payload, uint32_t size) noexcept {
    const DumpRecordHeader header{type, size,
        size && payload ? Crc32(static_cast<const uint8_t*>(payload), size) : 0};
    return WriteFileAll(file, &header, sizeof(header)) &&
        (!size || WriteFileAll(file, payload, size));
}

bool WriteDataRecord(void* file, const DumpDataRecord& descriptor,
                     const uint8_t* data, size_t size) noexcept {
    if (size > UINT32_MAX - sizeof(descriptor) || (!data && size)) return false;
    const uint32_t payloadSize = static_cast<uint32_t>(sizeof(descriptor) + size);
    const DumpRecordHeader header{DumpRecordData, payloadSize,
        Crc32Pair(&descriptor, sizeof(descriptor), data, size)};
    return WriteFileAll(file, &header, sizeof(header)) &&
        WriteFileAll(file, &descriptor, sizeof(descriptor)) &&
        WriteFileAll(file, data, size);
}

bool ReadExact(void* process, uintptr_t address, void* buffer, size_t size) noexcept {
    size_t read = 0;
    return NtReadVirtualMemory(process, reinterpret_cast<const void*>(address), buffer,
        size, &read) == 0 && read == size;
}

struct RemoteUnicodeString { uint16_t length; uint16_t maximumLength; uint32_t padding; uint64_t buffer; };
struct RemoteListEntry { uint64_t flink; uint64_t blink; };
struct RemoteLoaderEntry {
    uint8_t reserved[0x30];
    uint64_t dllBase;
    uint64_t entryPoint;
    uint32_t imageSize;
    uint32_t reserved2;
    RemoteUnicodeString fullName;
};

template<size_t N>
bool WriteLiteral(const char (&value)[N]) noexcept {
    return WriteStd(value, N - 1);
}

struct Text {
    char data[512]{};
    size_t length{};
    void append(char c) noexcept { if (length < sizeof(data)) data[length++] = c; }
    void append(const char* value) noexcept {
        if (!value) return;
        while (*value && length < sizeof(data)) data[length++] = *value++;
    }
    void number(uint64_t value) noexcept {
        char digits[24]{};
        size_t n = 0;
        do { digits[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && n < sizeof(digits));
        while (n) append(digits[--n]);
    }
    void hex(uint64_t value) noexcept {
        static constexpr char digits[] = "0123456789abcdef";
        append('0'); append('x');
        bool started = false;
        for (int shift = 60; shift >= 0; shift -= 4) {
            const auto digit = static_cast<uint8_t>((value >> shift) & 0xf);
            if (digit || started || shift == 0) { append(digits[digit]); started = true; }
        }
    }
};

void JsonString(const char16_t* value, uint16_t bytes) noexcept {
    WriteStd("\"", 1);
    const size_t count = bytes / sizeof(char16_t);
    for (size_t i = 0; i < count; ++i) {
        const char16_t c = value[i];
        if (c == u'"' || c == u'\\') {
            const char prefix = '\\'; WriteStd(&prefix, 1);
            const char encoded = c <= 0x7f ? static_cast<char>(c) : '?'; WriteStd(&encoded, 1);
        } else if (c >= 0x20 && c <= 0x7e) {
            const char encoded = static_cast<char>(c); WriteStd(&encoded, 1);
        } else {
            const char escape[6] = {'\\','u','0','0',"0123456789abcdef"[(c >> 4) & 0xf],"0123456789abcdef"[c & 0xf]};
            WriteStd(escape, sizeof(escape));
        }
    }
    WriteStd("\"", 1);
}

bool ParseCommandLine(uint32_t& pid, bool& json, bool& full, bool& mini,
                      char16_t* output, size_t outputCapacity) noexcept {
    shared::native_process::UnicodeString64 command{};
    if (!shared::native_process::currentCommandLine(command)) return false;
    const size_t count = command.Length / sizeof(char16_t);
    size_t at = 0;
    while (at < count && command.Buffer[at] != u' ') ++at;
    while (at < count) {
        while (at < count && command.Buffer[at] == u' ') ++at;
        const size_t start = at;
        while (at < count && command.Buffer[at] != u' ') ++at;
        const size_t tokenLength = at - start;
        const auto* token = command.Buffer + start;
        if (tokenLength == 3 && token[0] == u'-' && token[1] == u'-' && token[2] == u'p') {
            // Accept --pid as a separate token below; this short form is ignored.
        }
        if (tokenLength == 5 && token[0] == u'-' && token[1] == u'-' &&
            token[2] == u'p' && token[3] == u'i' && token[4] == u'd') {
            while (at < count && command.Buffer[at] == u' ') ++at;
            if (at == count) return false;
            uint64_t parsed = 0;
            size_t digits = 0;
            while (at < count && command.Buffer[at] >= u'0' && command.Buffer[at] <= u'9') {
                parsed = parsed * 10 + static_cast<uint64_t>(command.Buffer[at] - u'0');
                if (parsed > UINT32_MAX) return false;
                ++at; ++digits;
            }
            if (!digits || parsed == 0) return false;
            pid = static_cast<uint32_t>(parsed);
            continue;
        }
        if (tokenLength == 6 && token[0] == u'-' && token[1] == u'-' && token[2] == u'j' &&
            token[3] == u's' && token[4] == u'o' && token[5] == u'n') json = true;
        if (tokenLength == 6 && token[0] == u'-' && token[1] == u'-' && token[2] == u'f' &&
            token[3] == u'u' && token[4] == u'l' && token[5] == u'l') full = true;
        if (tokenLength == 6 && token[0] == u'-' && token[1] == u'-' && token[2] == u'm' &&
            token[3] == u'i' && token[4] == u'n' && token[5] == u'i') mini = true;
        if (tokenLength && token[0] != u'-') {
            if (tokenLength >= outputCapacity) return false;
            for (size_t i = 0; i < tokenLength; ++i) output[i] = token[i];
            output[tokenLength] = 0;
        }
    }
    return pid != 0;
}

bool FindProcessRecord(uint32_t pid, SystemProcessInformation*& found) noexcept {
    uint32_t returned{};
    const auto status = NtQuerySystemInformation(SystemProcessInformationClass,
        processSnapshot, sizeof(processSnapshot), &returned);
    if (status != 0) return false;
    size_t offset = 0;
    while (offset <= sizeof(processSnapshot) - sizeof(SystemProcessInformation)) {
        auto* record = reinterpret_cast<SystemProcessInformation*>(processSnapshot + offset);
        if (record->UniqueProcessId == pid) { found = record; return true; }
        if (!record->NextEntryOffset) break;
        if (record->NextEntryOffset < sizeof(SystemProcessInformation) ||
            record->NextEntryOffset > sizeof(processSnapshot) - offset) return false;
        offset += record->NextEntryOffset;
    }
    return false;
}

bool ImageNameEqualsAscii(const SystemProcessInformation& process, const char* name) noexcept {
    if (!name || !process.ImageName.Buffer || (process.ImageName.Length & 1)) return false;
    const auto* image = reinterpret_cast<const char16_t*>(process.ImageName.Buffer);
    const size_t imageCount = process.ImageName.Length / sizeof(char16_t);
    size_t i = 0;
    for (; i < imageCount && name[i]; ++i) {
        char16_t c = image[i];
        char expected = name[i];
        if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c + (u'a' - u'A'));
        if (expected >= 'A' && expected <= 'Z') expected = static_cast<char>(expected + ('a' - 'A'));
        if (c != static_cast<char16_t>(expected)) return false;
    }
    return i == imageCount && name[i] == '\0';
}

bool QueryRegion(void* process, uintptr_t address, MemoryBasicInformation64& region) noexcept {
    size_t returned{};
    const auto status = NtQueryVirtualMemory(process, reinterpret_cast<const void*>(address), 0,
        &region, sizeof(region), &returned);
    if (status != 0 || returned != sizeof(region)) {
        processMemoryQueryStatus = status;
        return false;
    }
    return true;
}

bool CountRegionWorkingSet(void* process, uintptr_t base, size_t bytes,
                           uint64_t& resident, uint64_t& shared) noexcept {
    const uint64_t pages = (bytes + PageSize - 1) / PageSize;
    uint64_t index = 0;
    while (index < pages) {
        size_t batch = pages - index < RegionBatchCount
            ? static_cast<size_t>(pages - index) : RegionBatchCount;
        for (size_t i = 0; i < batch; ++i) {
            workingSetBatch[i] = {};
            const uint64_t address = static_cast<uint64_t>(base) + (index + i) * PageSize;
            if (address > UserSpaceMax) return false;
            workingSetBatch[i].virtualAddress = static_cast<uintptr_t>(address);
        }
        size_t returned{};
        const auto status = NtQueryVirtualMemory(process, nullptr, 4, workingSetBatch,
            batch * sizeof(MemoryWorkingSetExInformation), &returned);
        if (status != 0 || returned != batch * sizeof(MemoryWorkingSetExInformation)) return false;
        for (size_t i = 0; i < batch; ++i) {
            if (!(workingSetBatch[i].virtualAttributes & 1)) continue;
            ++resident;
            if (workingSetBatch[i].virtualAttributes & (uint64_t{1} << 15)) ++shared;
        }
        index += batch;
    }
    return true;
}

struct RegionTotals {
    uint64_t virtualBytes{};
    uint64_t reservedBytes{};
    uint64_t committedBytes{};
    uint64_t residentPages{};
    uint64_t sharedPages{};
    uint64_t imagePages{};
    uint64_t mappedRegions{};
    uint64_t guardPages{};
    uint64_t regions{};
    bool workingSetComplete{true};
};

bool ReportRegions(void* process, bool json, RegionTotals& totals) noexcept {
    uintptr_t address = UserSpaceMin;
    bool first = true;
    const auto fail = [&]() noexcept {
        if (processMemoryFailureStage < 62) processMemoryFailureStage = 61;
        if (json) WriteStd("]", 1);
        return false;
    };
    if (json) WriteStd(",\"regions\":[", 12);
    size_t n = 0;
    for (; n < 4096 && address <= UserSpaceMax; ++n) {
        MemoryBasicInformation64 region{};
        if (!QueryRegion(process, address, region)) { processMemoryFailureStage = 63; return fail(); }
        if (region.regionSize == 0 || region.regionSize - 1 > UserSpaceMax - address) {
            processMemoryFailureStage = 64; return fail();
        }
        if (region.state != MemFree) {
            ++totals.regions;
            uint64_t regionResidentPages = 0;
            uint64_t regionSharedPages = 0;
            bool workingSetAvailable = true;
            if (region.regionSize > UINT64_MAX - totals.virtualBytes) { processMemoryFailureStage = 65; return fail(); }
            totals.virtualBytes += region.regionSize;
            if (region.state == MemReserve) {
                if (region.regionSize > UINT64_MAX - totals.reservedBytes) { processMemoryFailureStage = 66; return fail(); }
                totals.reservedBytes += region.regionSize;
            }
            if (region.state == MemCommit) {
                if (region.regionSize > UINT64_MAX - totals.committedBytes) { processMemoryFailureStage = 67; return fail(); }
                totals.committedBytes += region.regionSize;
                workingSetAvailable = CountRegionWorkingSet(process, region.baseAddress,
                                           region.regionSize, regionResidentPages,
                                           regionSharedPages);
                if (!workingSetAvailable) {
                    totals.workingSetComplete = false;
                    regionResidentPages = 0;
                    regionSharedPages = 0;
                }
                if (regionResidentPages > UINT64_MAX - totals.residentPages ||
                    regionSharedPages > UINT64_MAX - totals.sharedPages) return fail();
                totals.residentPages += regionResidentPages;
                totals.sharedPages += regionSharedPages;
                if (region.type == 0x01000000u) {
                    if (totals.workingSetComplete) {
                        if (regionResidentPages > UINT64_MAX - totals.imagePages) return fail();
                        totals.imagePages += regionResidentPages;
                    }
                }
                if (region.type == 0x00040000u) ++totals.mappedRegions;
                if (region.protect & PageGuard) {
                    const uint64_t guardCount = (region.regionSize + PageSize - 1) / PageSize;
                    if (guardCount > UINT64_MAX - totals.guardPages) return fail();
                    totals.guardPages += guardCount;
                }
            }
            if (json) {
                if (!first) WriteStd(",", 1);
                Text line{};
                line.append("{\"base\":\""); line.hex(region.baseAddress);
                line.append('"');
                line.append(",\"size\":"); line.number(region.regionSize);
                line.append(",\"state\":"); line.number(region.state);
                line.append(",\"protect\":"); line.number(region.protect);
                line.append(",\"allocation_base\":\""); line.hex(region.allocationBase);
                line.append('"');
                line.append(",\"allocation_protect\":"); line.number(region.allocationProtect);
                line.append(",\"type\":"); line.number(region.type);
                if (region.state == MemCommit) {
                    if (workingSetAvailable) {
                        line.append(",\"resident_pages\":"); line.number(regionResidentPages);
                        line.append(",\"shared_pages\":"); line.number(regionSharedPages);
                        line.append(",\"private_resident_pages\":");
                        line.number(regionResidentPages >= regionSharedPages
                            ? regionResidentPages - regionSharedPages : 0);
                    } else line.append(",\"resident_pages\":null,\"shared_pages\":null,\"private_resident_pages\":null");
                } else {
                    line.append(",\"resident_pages\":0,\"shared_pages\":0,\"private_resident_pages\":0");
                }
                line.append(",\"section_backing\":null");
                line.append("}"); line.append('\0');
                WriteStd(line.data, line.length - 1);
            }
            first = false;
        }
        const uintptr_t next = region.baseAddress + region.regionSize;
        if (next <= address) { processMemoryFailureStage = 68; return fail(); }
        address = next;
    }
    if (n == 4096 && address <= UserSpaceMax) { processMemoryFailureStage = 69; return fail(); }
    if (json) WriteStd("]", 1);
    return true;
}

bool ReportSystemMemory(bool json) noexcept {
    SystemMemoryAccountingInformation memory{};
    uint32_t returned{};
    const auto status = NtQuerySystemInformation(SystemMemoryAccountingInformationClass,
        &memory, sizeof(memory), &returned);
    if (status != 0 || returned != sizeof(memory)) return false;
    if (json) {
        Text line{};
        line.append(",\"system_memory\":{\"version\":"); line.number(memory.version);
        line.append(",\"availability\":"); line.number(memory.availability);
        line.append(",\"managed_pfns\":"); line.number(memory.managedPages);
        line.append(",\"active_pfns\":"); line.number(memory.activePages);
        line.append(",\"zeroed_pfns\":"); line.number(memory.zeroedPages);
        line.append(",\"free_pfns\":"); line.number(memory.freePages);
        line.append(",\"standby_pfns\":"); line.number(memory.standbyPages);
        line.append(",\"modified_pfns\":"); line.number(memory.modifiedPages);
        line.append(",\"reserved_pfns\":"); line.number(memory.reservedPages);
        line.append(",\"managed_bytes\":");
        if (memory.managedPages <= UINT64_MAX / PageSize) line.number(memory.managedPages * PageSize);
        else line.append("null");
        line.append(",\"allocated_pfns\":"); line.number(memory.activePages);
        line.append(",\"allocated_bytes\":");
        if (memory.activePages <= UINT64_MAX / PageSize) line.number(memory.activePages * PageSize);
        else line.append("null");
        line.append(",\"bad_pfns\":"); line.number(memory.badPages);
        line.append(",\"shared_pfns\":null,\"page_table_pfns\":null,\"kernel_pfns\":null,");
        line.append("\"driver_dma_pfns\":null,\"compositor_pfns\":null,\"unattributed_pfns\":null}");
        line.append('\0');
        return WriteStd(line.data, line.length - 1);
    }
    Text line{};
    line.append("System PFNs: managed="); line.number(memory.managedPages);
    line.append(" active="); line.number(memory.activePages);
    line.append(" zeroed="); line.number(memory.zeroedPages);
    line.append(" free="); line.number(memory.freePages);
    line.append(" standby="); line.number(memory.standbyPages);
    line.append(" modified="); line.number(memory.modifiedPages);
    line.append(" reserved="); line.number(memory.reservedPages);
    line.append("; shared/kernel/page-table/driver/DMA/compositor attribution unavailable\r\n");
    return WriteStd(line.data, line.length);
}

bool QueryStackCommittedBytes(void* process, SystemProcessInformation* processRecord,
                             uint64_t& totalBytes) noexcept {
    totalBytes = 0;
    const auto* records = reinterpret_cast<const SystemThreadInformation*>(
        reinterpret_cast<const uint8_t*>(processRecord) + sizeof(SystemProcessInformation));
    const uint32_t threads = processRecord->NumberOfThreads < 128
        ? processRecord->NumberOfThreads : 128;
    for (uint32_t i = 0; i < threads; ++i) {
        ClientId threadId{static_cast<Handle>(processRecord->UniqueProcessId),
            static_cast<Handle>(records[i].UniqueThreadId)};
        ObjectAttributes attributes{};
        void* thread{};
        if (NtOpenThread(&thread, ThreadQueryInformation, &attributes, &threadId) != 0 || !thread)
            return false;
        ThreadBasicInformation basic{};
        uint32_t returned{};
        const auto status = NtQueryInformationThread(thread, 0, &basic, sizeof(basic), &returned);
        NtClose(thread);
        if (status != 0 || returned != sizeof(basic) || !basic.tebBaseAddress) return false;
        uint64_t tib[3]{};
        size_t read{};
        if (NtReadVirtualMemory(process, reinterpret_cast<const void*>(basic.tebBaseAddress),
            tib, sizeof(tib), &read) != 0 || read != sizeof(tib) || !tib[1] ||
            tib[1] <= tib[2] || tib[1] - tib[2] > 64 * 1024 * 1024) return false;
        uintptr_t cursor = static_cast<uintptr_t>(tib[2]);
        const uintptr_t end = static_cast<uintptr_t>(tib[1]);
        while (cursor < end) {
            MemoryBasicInformation64 region{};
            if (!QueryRegion(process, cursor, region) || !region.regionSize ||
                region.regionSize - 1 > UserSpaceMax - cursor) return false;
            const uintptr_t next = region.baseAddress + region.regionSize;
            if (next <= cursor) return false;
            if (region.state == MemCommit) {
                const uintptr_t committedStart = cursor > region.baseAddress ? cursor : region.baseAddress;
                const uintptr_t committedEnd = next < end ? next : end;
                const uint64_t bytes = committedEnd - committedStart;
                if (bytes > UINT64_MAX - totalBytes) return false;
                totalBytes += bytes;
            }
            cursor = next;
        }
    }
    return processRecord->NumberOfThreads <= threads;
}

bool Report(uint32_t pid, bool json) noexcept {
    processMemoryQueryStatus = 0;
    SystemProcessInformation* processRecord{};
    if (!FindProcessRecord(pid, processRecord)) return false;
    ClientId clientId{pid, 0};
    ObjectAttributes attributes{};
    void* processHandle{};
    auto status = NtOpenProcess(&processHandle, ProcessQueryInformation | ProcessVmRead, &attributes, &clientId);
    if (status != 0 || !processHandle) return false;

    BasicInformation basic{};
    uint32_t returned{};
    status = NtQueryInformationProcess(processHandle, ProcessBasicInformationClass,
        &basic, sizeof(basic), &returned);
    ProcessVmCountersEx vm{};
    if (status == 0)
        status = NtQueryInformationProcess(processHandle, ProcessVmCountersClass,
            &vm, sizeof(vm), &returned);
    RegionTotals regions{};
    uint64_t stackBytes{};
    const bool stackKnown = QueryStackCommittedBytes(processHandle, processRecord, stackBytes);
    bool ok = status == 0;
    if (json && ok) {
        WriteStd("{\"pid\":", 7);
        Text line{}; line.number(pid); line.append(",\"name\":"); line.append('\0');
        WriteStd(line.data, line.length - 1);
        JsonString(reinterpret_cast<const char16_t*>(processRecord->ImageName.Buffer),
                   processRecord->ImageName.Length);
        Text prefix{};
        prefix.append(",\"state\":\"");
        prefix.append(basic.exitStatus == static_cast<NtStatus>(0x00000103)
            ? "running" : "exited");
        prefix.append("\",\"exit_status\":"); prefix.number(static_cast<uint32_t>(basic.exitStatus));
        prefix.append(",\"handles\":"); prefix.number(processRecord->HandleCount);
        prefix.append(",\"threads\":"); prefix.number(processRecord->NumberOfThreads);
        prefix.append('\0'); WriteStd(prefix.data, prefix.length - 1);
        const bool regionsComplete = ReportRegions(processHandle, true, regions);
        bool terminatedDuringReport = false;
        if (!regionsComplete && processMemoryQueryStatus == static_cast<int32_t>(0xc000010au)) {
            BasicInformation finalBasic{};
            uint32_t finalBytes{};
            if (NtQueryInformationProcess(processHandle, ProcessBasicInformationClass,
                &finalBasic, sizeof(finalBasic), &finalBytes) == 0 &&
                finalBytes == sizeof(finalBasic) &&
                finalBasic.exitStatus != static_cast<NtStatus>(0x00000103)) {
                basic = finalBasic;
                terminatedDuringReport = true;
            }
        }
        Text tail{};
        tail.append(",\"regions_complete\":"); tail.append(regionsComplete ? "true" : "false");
        if (terminatedDuringReport)
            tail.append(",\"capture_error\":\"target terminated during region walk\"");
        tail.append(",\"virtual_bytes\":"); tail.number(regions.virtualBytes);
        tail.append(",\"reserved_bytes\":"); tail.number(regions.reservedBytes);
        tail.append(",\"committed_bytes\":"); tail.number(regions.committedBytes);
        tail.append(",\"resident_working_set_bytes\":"); tail.number(vm.counters.workingSetSize);
        tail.append(",\"private_resident_pages\":");
        if (regions.workingSetComplete)
            tail.number(regions.residentPages >= regions.sharedPages ? regions.residentPages - regions.sharedPages : 0);
        else tail.append("null");
        tail.append(",\"shared_resident_pages\":");
        if (regions.workingSetComplete) tail.number(regions.sharedPages); else tail.append("null");
        tail.append(",\"page_faults\":"); tail.number(vm.counters.pageFaultCount);
        tail.append(",\"peak_working_set_bytes\":"); tail.number(vm.counters.peakWorkingSetSize);
        tail.append(",\"peak_commit_bytes\":"); tail.number(vm.counters.peakPagefileUsage);
        tail.append(",\"private_commit_bytes\":"); tail.number(vm.privateUsage);
        tail.append(",\"image_backed_pages\":");
        if (regions.workingSetComplete) tail.number(regions.imagePages); else tail.append("null");
        tail.append(",\"mapped_sections\":"); tail.number(regions.mappedRegions);
        tail.append(",\"heap_bytes\":null,\"stack_bytes\":");
        if (stackKnown) tail.number(stackBytes); else tail.append("null");
        tail.append(",\"guard_pages\":");
        tail.number(regions.guardPages);
        tail.append(",\"page_table_bytes\":null");
        tail.append('\0'); WriteStd(tail.data, tail.length - 1);
        if (!ReportSystemMemory(true)) WriteStd(",\"system_memory\":null", 21);
        WriteStd("}\r\n", 3);
        ok = regionsComplete || terminatedDuringReport;
    } else if (ok) {
        const bool regionsComplete = ReportRegions(processHandle, false, regions);
        bool terminatedDuringReport = false;
        if (!regionsComplete && processMemoryQueryStatus == static_cast<int32_t>(0xc000010au)) {
            BasicInformation finalBasic{};
            uint32_t finalBytes{};
            if (NtQueryInformationProcess(processHandle, ProcessBasicInformationClass,
                &finalBasic, sizeof(finalBasic), &finalBytes) == 0 &&
                finalBytes == sizeof(finalBasic) &&
                finalBasic.exitStatus != static_cast<NtStatus>(0x00000103)) {
                basic = finalBasic;
                terminatedDuringReport = true;
            }
        }
        if (!regionsComplete) {
            const char* message = terminatedDuringReport
                ? "Region enumeration: partial; target terminated during report\r\n"
                : "Region enumeration: failed\r\n";
            size_t messageLength = 0;
            while (message[messageLength]) ++messageLength;
            WriteStd(message, messageLength);
        }
        ok = regionsComplete || terminatedDuringReport;
    }
    if (!json && ok) {
        Text line{};
        line.append("Process "); line.number(pid); line.append(" "); line.append('\0');
        WriteStd(line.data, line.length - 1);
        JsonString(reinterpret_cast<const char16_t*>(processRecord->ImageName.Buffer),
                   processRecord->ImageName.Length);
        WriteStd("\r\n", 2);
        Text metrics{};
        metrics.append("Virtual bytes: "); metrics.number(regions.virtualBytes);
        metrics.append("  Reserved bytes: "); metrics.number(regions.reservedBytes);
        metrics.append("  Committed bytes: "); metrics.number(regions.committedBytes);
        metrics.append("\r\nWorking set: "); metrics.number(vm.counters.workingSetSize);
        metrics.append("  Private resident pages: ");
        if (regions.workingSetComplete)
            metrics.number(regions.residentPages >= regions.sharedPages ? regions.residentPages - regions.sharedPages : 0);
        else metrics.append("unavailable");
        metrics.append("  Shared resident pages: ");
        if (regions.workingSetComplete) metrics.number(regions.sharedPages); else metrics.append("unavailable");
        metrics.append("\r\nHandles: "); metrics.number(processRecord->HandleCount);
        metrics.append("  Threads: "); metrics.number(processRecord->NumberOfThreads);
        metrics.append("  Faults: "); metrics.number(vm.counters.pageFaultCount);
        metrics.append("\r\nPeak working set: "); metrics.number(vm.counters.peakWorkingSetSize);
        metrics.append("  Peak commit: "); metrics.number(vm.counters.peakPagefileUsage);
        metrics.append("  Image resident pages: ");
        if (regions.workingSetComplete) metrics.number(regions.imagePages); else metrics.append("unavailable");
        metrics.append("  Mapped regions: "); metrics.number(regions.mappedRegions);
        metrics.append("  Guard pages: "); metrics.number(regions.guardPages);
        metrics.append("  Stack committed bytes: ");
        if (stackKnown) metrics.number(stackBytes); else metrics.append("unavailable");
        metrics.append("\r\n");
        WriteStd(metrics.data, metrics.length);
        ok = ReportSystemMemory(false);
    }
    NtClose(processHandle);
    return ok;
}

bool Dump(uint32_t pid, bool mini, const char16_t* path) noexcept {
    processMemoryFailureStage = 20;
    SystemProcessInformation* processRecord{};
    if (!FindProcessRecord(pid, processRecord)) { processMemoryFailureStage = 21; return false; }
    ClientId clientId{pid, 0};
    ObjectAttributes processAttributes{};
    void* process{};
    const auto openProcess = NtOpenProcess(&process, ProcessQueryInformation | ProcessVmRead,
        &processAttributes, &clientId);
    if (openProcess != 0 || !process) { processMemoryFailureStage = 22; return false; }

    BasicInformation basic{};
    uint32_t basicBytes{};
    if (NtQueryInformationProcess(process, ProcessBasicInformationClass, &basic,
        sizeof(basic), &basicBytes) != 0 || basicBytes != sizeof(basic)) {
        NtClose(process);
        processMemoryFailureStage = 23;
        return false;
    }

    UnicodeString name{};
    size_t pathLength = 0;
    while (path[pathLength]) ++pathLength;
    if (!pathLength || pathLength > ((UINT16_MAX - sizeof(char16_t)) / sizeof(char16_t))) {
        NtClose(process);
        processMemoryFailureStage = 24;
        return false;
    }
    name.length = static_cast<uint16_t>(pathLength * sizeof(char16_t));
    name.maximumLength = static_cast<uint16_t>(name.length + sizeof(char16_t));
    name.buffer = const_cast<wchar_t*>(reinterpret_cast<const wchar_t*>(path));
    ObjectAttributes fileAttributes{};
    fileAttributes.objectName = &name;
    fileAttributes.attributes = OBJ_CASE_INSENSITIVE;
    void* file{};
    IoStatusBlock createStatus{};
    const auto create = NtCreateFile(&file, 0x00100002u, &fileAttributes, &createStatus,
        nullptr, 0, 7, 3, 0x20, nullptr, 0);
    if (create != 0 || !file) {
        Text failure{};
        failure.append("[MicaNT ProcessMemoryDump] NtCreateFile status=");
        failure.hex(static_cast<uint32_t>(create));
        failure.append(" iosb=");
        failure.hex(static_cast<uint32_t>(createStatus.status));
        failure.append("\r\n");
        (void)WriteStd(failure.data, failure.length);
        NtClose(process);
        processMemoryFailureStage = 25;
        return false;
    }

    dumpWriteOffset = 0;
    DumpHeader header{{'L','U','C','D','M','P','1','\0'}, 1, sizeof(DumpHeader),
        0x8664, static_cast<uint16_t>(1u | (mini ? 2u : 0u)), pid, 0, 0};
    // Capture time is optional in v1 and remains zero until the Ring 3
    // system-time query is reliable in this bootstrap path.
    bool ok = WriteFileAll(file, &header, sizeof(header));
    if (!ok) processMemoryFailureStage = 26;
    if (ok) {
        const DumpProcessRecord metadata{pid, processRecord->HandleCount,
            processRecord->NumberOfThreads, processRecord->ImageName.Length, 0};
        const size_t imageBytes = processRecord->ImageName.Length;
        if (imageBytes > sizeof(processSnapshot) - sizeof(SystemProcessInformation)) ok = false;
        else {
            uint8_t payload[sizeof(DumpProcessRecord) + 512]{};
            if (imageBytes > sizeof(payload) - sizeof(metadata)) ok = false;
            else {
                Copy(payload, &metadata, sizeof(metadata));
                if (imageBytes) Copy(payload + sizeof(metadata),
                    reinterpret_cast<const void*>(processRecord->ImageName.Buffer), imageBytes);
                ok = WriteRecord(file, DumpRecordProcess, payload,
                    static_cast<uint32_t>(sizeof(metadata) + imageBytes));
            }
        }
        auto* thread = reinterpret_cast<const SystemThreadInformation*>(
            reinterpret_cast<const uint8_t*>(processRecord) + sizeof(SystemProcessInformation));
        const uint32_t threadCount = processRecord->NumberOfThreads < 128
            ? processRecord->NumberOfThreads : 128;
        for (uint32_t i = 0; ok && i < threadCount; ++i) {
            DumpThreadDetails details{};
            ClientId threadId{pid, static_cast<Handle>(thread[i].UniqueThreadId)};
            ObjectAttributes threadAttributes{};
            void* threadHandle{};
            if (NtOpenThread(&threadHandle, ThreadQueryInformation, &threadAttributes,
                             &threadId) == 0 && threadHandle) {
                ThreadBasicInformation threadBasic{};
                uint32_t returned{};
                if (NtQueryInformationThread(threadHandle, 0, &threadBasic,
                    sizeof(threadBasic), &returned) == 0 && returned == sizeof(threadBasic)) {
                    uint64_t tib[3]{};
                    details.teb = threadBasic.tebBaseAddress;
                    if (ReadExact(process, details.teb, tib, sizeof(tib))) {
                        details.stackBase = tib[1];
                        details.stackLimit = tib[2];
                        if (details.stackLimit && details.stackBase > details.stackLimit &&
                            details.stackBase - details.stackLimit <= 64 * 1024 * 1024) {
                            stackRanges[i] = {static_cast<uintptr_t>(details.stackBase),
                                static_cast<uintptr_t>(details.stackLimit)};
                        }
                    }
                }
                NtClose(threadHandle);
            }
            uint8_t payload[sizeof(thread[i]) + sizeof(details)];
            Copy(payload, &thread[i], sizeof(thread[i]));
            Copy(payload + sizeof(thread[i]), &details, sizeof(details));
            ok = WriteRecord(file, DumpRecordThread, payload, sizeof(payload));
            if (!ok) processMemoryFailureStage = 27;
        }
        // Enumerate the process's native AMD64 loader memory-order list. A
        // malformed or changing list ends module enumeration without trusting
        // pointers supplied by the target process.
        if (ok && basic.peb) {
            uint64_t ldr{};
            if (ReadExact(process, basic.peb + 0x18, &ldr, sizeof(ldr)) && ldr) {
                const uint64_t headAddress = ldr + 0x20;
                RemoteListEntry head{};
                if (ReadExact(process, headAddress, &head, sizeof(head))) {
                    uint64_t node = head.flink;
                    for (size_t i = 0; ok && i < 128 && node != headAddress; ++i) {
                        if (node < 0x10 || node > UserSpaceMax - sizeof(RemoteLoaderEntry)) break;
                        RemoteLoaderEntry entry{};
                        if (!ReadExact(process, static_cast<uintptr_t>(node - 0x10),
                                       &entry, sizeof(entry))) break;
                        if (!entry.dllBase || !entry.imageSize || entry.imageSize > 512 * 1024 * 1024 ||
                            entry.fullName.length > 512 || (entry.fullName.length & 1)) break;
                        uint8_t payload[sizeof(DumpModuleRecord) + 512]{};
                        const DumpModuleRecord module{entry.dllBase, entry.imageSize,
                            entry.fullName.length, {0, 0, 0}};
                        Copy(payload, &module, sizeof(module));
                        if (entry.fullName.length && !ReadExact(process,
                            static_cast<uintptr_t>(entry.fullName.buffer), payload + sizeof(module),
                            entry.fullName.length)) break;
                        ok = WriteRecord(file, DumpRecordModule, payload,
                            static_cast<uint32_t>(sizeof(module) + entry.fullName.length));
                        RemoteListEntry link{};
                        if (!ReadExact(process, static_cast<uintptr_t>(node), &link, sizeof(link)) ||
                            link.flink == node) break;
                        node = link.flink;
                    }
                }
            }
        }
    }
    if (!ok && processMemoryFailureStage < 27) processMemoryFailureStage = 28;
    uintptr_t address = UserSpaceMin;
    size_t regions = 0;
    uint64_t dumpedBytes = 0;
    for (; ok && regions < 4096 && address <= UserSpaceMax; ++regions) {
        MemoryBasicInformation64 region{};
        if (!QueryRegion(process, address, region) || !region.regionSize ||
            region.regionSize - 1 > UserSpaceMax - address) { ok = false; processMemoryFailureStage = 29; break; }
        const uintptr_t next = region.baseAddress + region.regionSize;
        if (next <= address) { ok = false; processMemoryFailureStage = 30; break; }
        address = next;
        if (region.state != MemCommit) continue;
        // Mini captures image mappings and writable private regions; full captures
        // every committed region except no-access/guard ranges.
        const bool image = region.type == 0x01000000u;
        bool threadStack = false;
        if (mini) for (const auto& stack : stackRanges) {
            if (stack.limit && region.baseAddress < stack.base &&
                region.baseAddress + region.regionSize > stack.limit) {
                threadStack = true;
                break;
            }
        }
        const bool privateWritable = region.type == 0x00020000u &&
            (region.protect & 0xffu) != PageNoAccess;
        if (mini ? !(image || threadStack || privateWritable) :
            ((region.protect & 0xffu) == PageNoAccess || (region.protect & PageGuard))) continue;
        const DumpRegionRecord regionRecord{region.baseAddress, region.regionSize,
            region.state, region.protect, region.type, 0};
        if (!WriteRecord(file, DumpRecordRegion, &regionRecord, sizeof(regionRecord))) { ok = false; processMemoryFailureStage = 31; break; }
        uintptr_t cursor = region.baseAddress;
        const uintptr_t end = region.baseAddress + region.regionSize;
        while (ok && cursor < end) {
            if (NtTestAlert() != 0 || dumpedBytes >= DumpMaxBytes) {
                ok = false;
                processMemoryFailureStage = 32;
                break;
            }
            size_t amount = static_cast<size_t>(end - cursor);
            if (amount > sizeof(dumpChunk)) amount = sizeof(dumpChunk);
            if (amount > DumpMaxBytes - dumpedBytes)
                amount = static_cast<size_t>(DumpMaxBytes - dumpedBytes);
            size_t read = 0;
            const auto readStatus = NtReadVirtualMemory(process,
                reinterpret_cast<const void*>(cursor), dumpChunk, amount, &read);
            if (read > amount) { ok = false; processMemoryFailureStage = 33; break; }
            if (read) {
                const DumpDataRecord data{cursor, static_cast<uint32_t>(read), Crc32(dumpChunk, read)};
                if (!WriteDataRecord(file, data, dumpChunk, read)) {
                    ok = false; processMemoryFailureStage = 39; break;
                }
                if (read > UINT64_MAX - dumpedBytes) { ok = false; processMemoryFailureStage = 33; break; }
                dumpedBytes += read;
                cursor += read;
            }
            if (readStatus != 0 || !read) {
                const uint32_t skipped = static_cast<uint32_t>(amount - read);
                const DumpDataRecord unreadable{cursor, skipped, 0};
                if (!WriteRecord(file, DumpRecordUnreadable, &unreadable, sizeof(unreadable))) { ok = false; processMemoryFailureStage = 35; break; }
                cursor += skipped;
            }
        }
    }
    if (regions == 4096 && address <= UserSpaceMax) { ok = false; processMemoryFailureStage = 36; }
    if (ok && !WriteRecord(file, DumpRecordEnd, nullptr, 0)) {
        ok = false;
        processMemoryFailureStage = 37;
    }
    if (ok) {
        IoStatusBlock flushStatus{};
        ok = NtFlushBuffersFile(file, &flushStatus) == 0;
        if (!ok) processMemoryFailureStage = 38;
    }
    NtClose(file);
    NtClose(process);
    return ok;
}

bool ProcessMemorySelfTest() noexcept {
    processMemoryFailureStage = 1;
    BasicInformation basic{};
    uint32_t returned{};
    if (NtQueryInformationProcess(reinterpret_cast<void*>(InvalidHandleValue),
        ProcessBasicInformationClass, &basic, sizeof(basic), &returned) != 0 ||
        returned != sizeof(basic)) return false;
    processMemoryFailureStage = 2;
    (void)WriteLiteral("[MicaNT ProcessMemoryTest] current-process query passed\r\n");
#if defined(MICANT_PROCESS_MEMORY_TEST_DESKTOP_TARGETS)
    const auto snapshotStatus = NtQuerySystemInformation(SystemProcessInformationClass,
        processSnapshot, sizeof(processSnapshot), &returned);
    if (snapshotStatus != 0) { processMemoryFailureStage = 50; return false; }
    bool foundExplorer = false;
    bool foundCompositor = false;
    bool foundThemeTest = false;
    uint32_t explorerPid{};
    uint32_t compositorPid{};
    uint32_t themeTestPid{};
    size_t offset = 0;
    for (size_t visited = 0; visited < 512 &&
         offset <= sizeof(processSnapshot) - sizeof(SystemProcessInformation); ++visited) {
        auto* process = reinterpret_cast<SystemProcessInformation*>(processSnapshot + offset);
        if (ImageNameEqualsAscii(*process, "explorer.exe")) {
            foundExplorer = true;
            explorerPid = static_cast<uint32_t>(process->UniqueProcessId);
        } else if (ImageNameEqualsAscii(*process, "compositor.exe")) {
            foundCompositor = true;
            compositorPid = static_cast<uint32_t>(process->UniqueProcessId);
        } else if (ImageNameEqualsAscii(*process, "theme_test.exe")) {
            foundThemeTest = true;
            themeTestPid = static_cast<uint32_t>(process->UniqueProcessId);
        }
        if (!process->NextEntryOffset) break;
        if (process->NextEntryOffset < sizeof(SystemProcessInformation) ||
            process->NextEntryOffset > sizeof(processSnapshot) - offset) return false;
        offset += process->NextEntryOffset;
    }
    if (!foundExplorer || !foundCompositor) { processMemoryFailureStage = 54; return false; }
#if defined(MICANT_PROCESS_MEMORY_TEST_THEME_TARGET)
    if (!foundThemeTest) return false;
    if (!Report(themeTestPid, false)) { processMemoryFailureStage = 53; return false; }
#endif
    if (!Report(explorerPid, false)) { processMemoryFailureStage = 51; return false; }
    if (!Report(compositorPid, false)) { processMemoryFailureStage = 52; return false; }
    if (!WriteLiteral("[MicaNT ProcessMemoryTest] desktop process reports passed\r\n")) return false;
#endif
    processMemoryFailureStage = 3;
    ProcessVmCountersEx before{};
    if (NtQueryInformationProcess(reinterpret_cast<void*>(InvalidHandleValue),
        ProcessVmCountersClass, &before, sizeof(before), &returned) != 0) return false;
    processMemoryFailureStage = 4;
    void* base = nullptr;
    size_t regionSize = PageSize;
    if (NtAllocateVirtualMemory(reinterpret_cast<void*>(InvalidHandleValue), &base, 0,
        &regionSize, 0x3000, 0x04) != 0 || !base || regionSize < PageSize) return false;
    auto* known = static_cast<uint8_t*>(base);
    for (size_t i = 0; i < PageSize; ++i) known[i] = static_cast<uint8_t>((i * 37u) ^ 0xa5u);
    processMemoryFailureStage = 5;
    ProcessVmCountersEx after{};
    const bool queryOk = NtQueryInformationProcess(reinterpret_cast<void*>(InvalidHandleValue),
        ProcessVmCountersClass, &after, sizeof(after), &returned) == 0;
    processMemoryFailureStage = 6;
    uint8_t copied[PageSize]{};
    size_t read{};
    const bool readOk = NtReadVirtualMemory(reinterpret_cast<void*>(InvalidHandleValue),
        base, copied, sizeof(copied), &read) == 0 && read == sizeof(copied);
    processMemoryFailureStage = 7;
    bool dataMatches = readOk;
    for (size_t i = 0; dataMatches && i < PageSize; ++i)
        dataMatches = copied[i] == static_cast<uint8_t>((i * 37u) ^ 0xa5u);
    processMemoryFailureStage = 8;
    MemoryBasicInformation64 region{};
    const bool regionOk = QueryRegion(reinterpret_cast<void*>(InvalidHandleValue),
        reinterpret_cast<uintptr_t>(base), region) && region.state == MemCommit;
    if (!queryOk || !dataMatches || !regionOk || after.counters.pagefileUsage < before.counters.pagefileUsage + PageSize) {
        (void)NtFreeVirtualMemory(reinterpret_cast<void*>(InvalidHandleValue), &base,
            &regionSize, 0x8000);
        return false;
    }
    processMemoryFailureStage = 9;
    (void)WriteLiteral("[MicaNT ProcessMemoryTest] known-page read and commit delta passed\r\n");
    processMemoryFailureStage = 10;
    if (!Report(static_cast<uint32_t>(basic.pid), true)) return false;
    const char16_t dumpPath[] = u"C:\\PROCDMP.LUD";
    processMemoryFailureStage = 11;
    if (!Dump(static_cast<uint32_t>(basic.pid), false, dumpPath)) return false;
    processMemoryFailureStage = 12;
    const uint32_t expected = 0xcbf43926u;
    if (Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) != expected) return false;
    processMemoryFailureStage = 13;
    // MEM_RELEASE requires RegionSize == 0 under the NT contract.
    regionSize = 0;
    const auto freeStatus = NtFreeVirtualMemory(reinterpret_cast<void*>(InvalidHandleValue),
        &base, &regionSize, 0x8000);
    ProcessVmCountersEx final{};
    const auto finalQueryStatus = NtQueryInformationProcess(reinterpret_cast<void*>(InvalidHandleValue),
        ProcessVmCountersClass, &final, sizeof(final), &returned);
    processMemoryFailureStage = 14;
    if (freeStatus != 0 || finalQueryStatus != 0) {
        Text failure{};
        failure.append("[MicaNT ProcessMemoryTest] cleanup free/query status=");
        failure.hex(static_cast<uint32_t>(freeStatus));
        failure.append("/");
        failure.hex(static_cast<uint32_t>(finalQueryStatus));
        failure.append("\r\n");
        (void)WriteStd(failure.data, failure.length);
        return false;
    }
    processMemoryFailureStage = 15;
    (void)WriteLiteral("[MicaNT ProcessMemoryTest] streamed dump, CRC, and release passed\r\n");
    return true;
}
} // namespace

extern "C" void _start() noexcept {
#if defined(MICANT_PROCESS_MEMORY_SELFTEST)
    const bool success = ProcessMemorySelfTest();
    if (!success) {
        (void)WriteLiteral("[MicaNT ProcessMemoryTest] FAILED stage=");
        Text failure{};
        failure.number(processMemoryFailureStage);
        failure.append(" write-status=");
        failure.hex(static_cast<uint32_t>(processMemoryLastWriteStatus));
        failure.append(" bytes=");
        failure.number(processMemoryLastWriteBytes);
        failure.append("\r\n");
        (void)WriteStd(failure.data, failure.length);
    }
    NtTerminateProcess(reinterpret_cast<void*>(InvalidHandleValue),
        success ? static_cast<int32_t>(NtStatus::Success)
                : static_cast<int32_t>(0xc0000100u + processMemoryFailureStage));
    return;
#else
    uint32_t pid{};
    bool json = false, full = false, mini = false;
    char16_t output[260]{};
    if (!ParseCommandLine(pid, json, full, mini, output, sizeof(output) / sizeof(output[0]))) {
        WriteLiteral("usage: procdump --pid <id> --report [--json]\r\n");
        NtTerminateProcess(reinterpret_cast<void*>(InvalidHandleValue), static_cast<int32_t>(NtStatus::InvalidParameter));
        return;
    }
    if (full || mini) {
        if (!output[0]) {
            WriteLiteral("usage: procdump --pid <id> --mini|--full <output.ludmp>\r\n");
            NtTerminateProcess(reinterpret_cast<void*>(InvalidHandleValue), static_cast<int32_t>(NtStatus::InvalidParameter));
            return;
        }
        const bool success = Dump(pid, mini, output);
        if (!success) WriteLiteral("dump failed or was partial; no success status reported\r\n");
        NtTerminateProcess(reinterpret_cast<void*>(InvalidHandleValue),
            static_cast<int32_t>(success ? NtStatus::Success : NtStatus::Unsuccessful));
        return;
    }
    const bool success = Report(pid, json);
    NtTerminateProcess(reinterpret_cast<void*>(InvalidHandleValue),
        static_cast<int32_t>(success ? NtStatus::Success : NtStatus::Unsuccessful));
#endif
}
