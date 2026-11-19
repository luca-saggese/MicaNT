/**
 * @file kernel32_toolhelp.cpp
 * @brief Win32 Toolhelp process snapshots over the native Ps process snapshot.
 */

#include <cstddef>
#include <cstdint>
#include <iterator>
#include "micant/shared/dos_device_map.hpp"
#include "micant/shared/system_information.h"

extern "C" {
__declspec(dllimport) int32_t __stdcall NtQuerySystemInformation(
    uint32_t informationClass, void* information, uint32_t length,
    uint32_t* returnLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryInformationProcess(
    void* process, uint32_t informationClass, void* information,
    uint32_t informationLength, uint32_t* returnLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtCreateSection(
    void** section, uint32_t access, void* attributes, void* maximumSize,
    uint32_t protection, uint32_t allocationAttributes, void* file) noexcept;
__declspec(dllimport) int32_t __stdcall NtClose(void* handle) noexcept;
__declspec(dllimport) uint32_t __stdcall RtlNtStatusToDosError(int32_t status) noexcept;
__declspec(dllimport) void* __cdecl malloc(size_t) noexcept;
__declspec(dllimport) void __cdecl free(void*) noexcept;
void __stdcall SetLastError(uint32_t) noexcept;
}

namespace {
struct ProcessEntry32W {
    uint32_t dwSize, cntUsage, th32ProcessID;
    uintptr_t th32DefaultHeapID;
    uint32_t th32ModuleID, cntThreads, th32ParentProcessID;
    int32_t pcPriClassBase;
    uint32_t dwFlags;
    wchar_t szExeFile[260];
};
struct ProcessBasicInformation {
    int32_t exitStatus;
    uint32_t padding;
    void* pebBaseAddress;
    uintptr_t affinityMask;
    int32_t basePriority;
    uint32_t padding2;
    uintptr_t uniqueProcessId;
    uintptr_t inheritedFromUniqueProcessId;
};
struct IoCounters {
    uint64_t readOperationCount, writeOperationCount, otherOperationCount;
    uint64_t readTransferCount, writeTransferCount, otherTransferCount;
};
struct NumberFormatW {
    uint32_t numDigits, leadingZero, grouping;
    wchar_t* decimalSeparator;
    wchar_t* thousandSeparator;
    uint32_t negativeOrder;
};
static_assert(offsetof(ProcessEntry32W, szExeFile) == 44);
static_assert(sizeof(ProcessEntry32W) == 568);

struct Snapshot {
    void* handle{};
    micant::SystemProcessInformation* records{};
    uint32_t recordCount{};
    uint32_t nextRecord{};
};
Snapshot g_snapshots[8]{};
uint32_t g_lock{};
uint32_t g_shutdownLevel{0x280u};
uint32_t g_shutdownFlags{};
struct Guard {
    Guard() noexcept {
        while (__atomic_exchange_n(&g_lock, 1u, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause" ::: "memory");
    }
    ~Guard() { __atomic_store_n(&g_lock, 0u, __ATOMIC_RELEASE); }
};
Snapshot* findSnapshot(void* handle) noexcept {
    for (auto& snapshot : g_snapshots)
        if (snapshot.handle == handle && handle) return &snapshot;
    return nullptr;
}
void closeSnapshot(void* handle) noexcept {
    Guard guard;
    if (auto* snapshot = findSnapshot(handle)) {
        free(snapshot->records);
        *snapshot = {};
    }
}
int32_t copyEntry(void* handle, ProcessEntry32W* output, bool first) noexcept {
    if (!output || output->dwSize != sizeof(*output)) {
        SetLastError(87);
        return 0;
    }
    Guard guard;
    auto* snapshot = findSnapshot(handle);
    if (!snapshot) { SetLastError(6); return 0; }
    const uint32_t index = first ? 0 : snapshot->nextRecord;
    if (index >= snapshot->recordCount) { SetLastError(18); return 0; }
    auto* record = snapshot->records;
    for (uint32_t i = 0; i < index; ++i) {
        if (!record->NextEntryOffset) { SetLastError(18); return 0; }
        record = reinterpret_cast<micant::SystemProcessInformation*>(
            reinterpret_cast<uint8_t*>(record) + record->NextEntryOffset);
    }
    ProcessEntry32W result{};
    result.dwSize = sizeof(result);
    result.th32ProcessID = static_cast<uint32_t>(record->UniqueProcessId);
    result.th32ParentProcessID = static_cast<uint32_t>(record->InheritedFromUniqueProcessId);
    result.cntThreads = record->NumberOfThreads;
    result.pcPriClassBase = record->BasePriority;
    const wchar_t* imageName = reinterpret_cast<const wchar_t*>(record->ImageName.Buffer);
    size_t chars = record->ImageName.Length / sizeof(wchar_t);
    if (!imageName || !chars) {
        imageName = result.th32ProcessID ? L"System" : L"System Idle Process";
        while (imageName[chars]) ++chars;
    }
    if (chars >= std::size(result.szExeFile)) chars = std::size(result.szExeFile) - 1;
    for (size_t i = 0; i < chars; ++i) result.szExeFile[i] = imageName[i];
    result.szExeFile[chars] = L'\0';
    *output = result;
    snapshot->nextRecord = index + 1;
    return 1;
}
}

extern "C" {
__declspec(dllexport) void* __stdcall CreateToolhelp32Snapshot(uint32_t flags,
        uint32_t processId) noexcept {
    constexpr uint32_t snapshotProcess = 0x00000002u;
    if (flags != snapshotProcess || processId != 0) {
        SetLastError(flags == snapshotProcess ? 87u : 24u);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    uint32_t bytes{};
    int32_t status = NtQuerySystemInformation(5, nullptr, 0, &bytes);
    constexpr uint32_t maximumSnapshotBytes = 16u * 1024u * 1024u;
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        if (!bytes || bytes > maximumSnapshotBytes) {
            SetLastError(8);
            return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        }
        auto* records = static_cast<micant::SystemProcessInformation*>(malloc(bytes));
        if (!records) { SetLastError(8); return reinterpret_cast<void*>(static_cast<intptr_t>(-1)); }
        uint32_t returned{};
        status = NtQuerySystemInformation(5, records, bytes, &returned);
        if (status == 0) {
            if (returned && returned < bytes) bytes = returned;
            uint32_t count = 0;
            for (size_t offset = 0; offset + sizeof(*records) <= bytes;) {
                auto* record = reinterpret_cast<micant::SystemProcessInformation*>(
                    reinterpret_cast<uint8_t*>(records) + offset);
                ++count;
                if (!record->NextEntryOffset) break;
                if (record->NextEntryOffset < sizeof(*records) ||
                    record->NextEntryOffset > bytes - offset) { count = 0; break; }
                offset += record->NextEntryOffset;
            }
            if (!count) {
                free(records);
                SetLastError(13);
                return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
            }
            int64_t maximumSize = bytes;
            void* handle{};
            const int32_t sectionStatus = NtCreateSection(&handle, 0x000F001Fu,
                nullptr, &maximumSize, 0x04u, 0x08000000u, nullptr);
            if (sectionStatus < 0) {
                free(records);
                SetLastError(RtlNtStatusToDosError(sectionStatus));
                return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
            }
            Guard guard;
            for (auto& slot : g_snapshots) {
                if (slot.handle) continue;
                slot = {handle, records, count, 0};
                return handle;
            }
            (void)NtClose(handle);
            free(records);
            SetLastError(8);
            return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        }
        free(records);
        if (returned <= bytes || returned > maximumSnapshotBytes) break;
        bytes = returned;
    }
    SetLastError(RtlNtStatusToDosError(status));
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

__declspec(dllexport) int32_t __stdcall Process32FirstW(void* snapshot,
        ProcessEntry32W* entry) noexcept {
    return copyEntry(snapshot, entry, true);
}
__declspec(dllexport) int32_t __stdcall Process32NextW(void* snapshot,
        ProcessEntry32W* entry) noexcept {
    return copyEntry(snapshot, entry, false);
}

__declspec(dllexport) int32_t __stdcall GetProcessIoCounters(void* process,
        IoCounters* counters) noexcept {
    if (!process || !counters) { SetLastError(87); return 0; }
    ProcessBasicInformation basic{};
    int32_t status = NtQueryInformationProcess(process, 0, &basic,
        sizeof(basic), nullptr);
    if (status < 0) {
        SetLastError(RtlNtStatusToDosError(status));
        return 0;
    }
    if (basic.uniqueProcessId > UINT32_MAX) { SetLastError(87); return 0; }
    uint32_t bytes{};
    status = NtQuerySystemInformation(5, nullptr, 0, &bytes);
    constexpr uint32_t maximumSnapshotBytes = 16u * 1024u * 1024u;
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        if (!bytes || bytes > maximumSnapshotBytes) { SetLastError(8); return 0; }
        auto* records = static_cast<uint8_t*>(malloc(bytes));
        if (!records) { SetLastError(8); return 0; }
        uint32_t returned{};
        status = NtQuerySystemInformation(5, records, bytes, &returned);
        if (status == 0) {
            bool found = false;
            IoCounters result{};
            for (size_t offset = 0; offset + sizeof(micant::SystemProcessInformation) <= bytes;) {
                auto* row = reinterpret_cast<micant::SystemProcessInformation*>(records + offset);
                if (row->UniqueProcessId == basic.uniqueProcessId) {
                    if (row->ReadOperationCount < 0 || row->WriteOperationCount < 0 ||
                        row->ReadTransferCount < 0 || row->WriteTransferCount < 0) {
                        free(records); SetLastError(13); return 0;
                    }
                    result.readOperationCount = static_cast<uint64_t>(row->ReadOperationCount);
                    result.writeOperationCount = static_cast<uint64_t>(row->WriteOperationCount);
                    result.readTransferCount = static_cast<uint64_t>(row->ReadTransferCount);
                    result.writeTransferCount = static_cast<uint64_t>(row->WriteTransferCount);
                    found = true;
                    break;
                }
                if (!row->NextEntryOffset || row->NextEntryOffset < sizeof(*row) ||
                    row->NextEntryOffset > bytes - offset) break;
                offset += row->NextEntryOffset;
            }
            free(records);
            if (!found) { SetLastError(87); return 0; }
            *counters = result;
            return 1;
        }
        free(records);
        if (returned <= bytes || returned > maximumSnapshotBytes) break;
        bytes = returned;
    }
    SetLastError(RtlNtStatusToDosError(status));
    return 0;
}

__declspec(dllexport) uint32_t __stdcall GetNumberFormatW(uint32_t locale,
        uint32_t flags, const wchar_t* value, const NumberFormatW* format,
        wchar_t* output, int32_t outputChars) noexcept {
    constexpr uint32_t localeInvariant = 0x007Fu;
    constexpr uint32_t localeUserDefault = 0x0400u;
    constexpr uint32_t localeSystemDefault = 0x0800u;
    if ((locale != localeInvariant && locale != 0x0409u &&
         locale != localeUserDefault && locale != localeSystemDefault) ||
        flags || !value || outputChars < 0 || (outputChars && !output)) {
        SetLastError(87);
        return 0;
    }
    const uint32_t digits = format ? format->numDigits : 2u;
    const uint32_t grouping = format ? format->grouping : 3u;
    const uint32_t leadingZero = format ? format->leadingZero : 1u;
    const uint32_t negativeOrder = format ? format->negativeOrder : 1u;
    const wchar_t* decimal = format && format->decimalSeparator
        ? format->decimalSeparator : L".";
    const wchar_t* thousands = format && format->thousandSeparator
        ? format->thousandSeparator : L",";
    if (digits > 18 || grouping > 9 || leadingZero > 1 || negativeOrder > 4) {
        SetLastError(87);
        return 0;
    }
    wchar_t integer[256]{};
    wchar_t fraction[64]{};
    size_t integerLength = 0, fractionLength = 0;
    bool negative = false, seenDecimal = false, sawDigit = false;
    const wchar_t* cursor = value;
    if (*cursor == L'-') { negative = true; ++cursor; }
    else if (*cursor == L'+') ++cursor;
    for (; *cursor; ++cursor) {
        if (*cursor == L'.' && !seenDecimal) { seenDecimal = true; continue; }
        if (*cursor < L'0' || *cursor > L'9') { SetLastError(87); return 0; }
        sawDigit = true;
        if (!seenDecimal) {
            if (integerLength + 1 >= std::size(integer)) { SetLastError(87); return 0; }
            integer[integerLength++] = *cursor;
        } else if (fractionLength + 1 < std::size(fraction)) {
            fraction[fractionLength++] = *cursor;
        }
    }
    if (!sawDigit) { SetLastError(87); return 0; }
    if (!integerLength && leadingZero) integer[integerLength++] = L'0';
    while (fractionLength < digits) fraction[fractionLength++] = L'0';
    if (fractionLength > digits) fractionLength = digits;

    wchar_t body[512]{};
    size_t bodyLength = 0;
    for (size_t i = 0; i < integerLength; ++i) {
        if (i && grouping && ((integerLength - i) % grouping) == 0)
            for (const wchar_t* p = thousands; *p; ++p) body[bodyLength++] = *p;
        body[bodyLength++] = integer[i];
    }
    if (digits) {
        for (const wchar_t* p = decimal; *p; ++p) body[bodyLength++] = *p;
        for (uint32_t i = 0; i < digits; ++i) body[bodyLength++] = fraction[i];
    }
    wchar_t result[544]{};
    size_t resultLength = 0;
    if (negative && negativeOrder == 0) result[resultLength++] = L'(';
    else if (negative && (negativeOrder == 1 || negativeOrder == 2)) {
        result[resultLength++] = L'-';
        if (negativeOrder == 2) result[resultLength++] = L' ';
    }
    for (size_t i = 0; i < bodyLength; ++i) result[resultLength++] = body[i];
    if (negative && negativeOrder == 0) result[resultLength++] = L')';
    else if (negative && negativeOrder == 3) result[resultLength++] = L'-';
    else if (negative && negativeOrder == 4) {
        result[resultLength++] = L' ';
        result[resultLength++] = L'-';
    }
    if (resultLength + 1 > static_cast<size_t>(INT32_MAX)) { SetLastError(122); return 0; }
    const uint32_t required = static_cast<uint32_t>(resultLength + 1);
    if (!output || !outputChars) return required;
    if (static_cast<uint32_t>(outputChars) < required) { SetLastError(122); return 0; }
    for (size_t i = 0; i < resultLength; ++i) output[i] = result[i];
    output[resultLength] = L'\0';
    return required;
}

__declspec(dllexport) uint32_t __stdcall QueryDosDeviceW(const wchar_t* deviceName,
        wchar_t* targetPath, uint32_t targetChars) noexcept {
    if (!targetPath || !targetChars) { SetLastError(87); return 0; }
    auto equalsInsensitive = [](const wchar_t* left, const char* right) noexcept {
        if (!left || !right) return false;
        size_t i = 0;
        while (left[i] && right[i]) {
            wchar_t ch = left[i];
            if (ch >= L'a' && ch <= L'z') ch -= L'a' - L'A';
            char rhs = right[i];
            if (rhs >= 'a' && rhs <= 'z') rhs -= 'a' - 'A';
            if (ch != static_cast<wchar_t>(rhs)) return false;
            ++i;
        }
        return !left[i] && !right[i];
    };
    if (deviceName) {
        for (const auto& mapping : micant::shared::BareMetalDosDeviceMappings) {
            if (!equalsInsensitive(deviceName, mapping.driveName)) continue;
            size_t length = 0;
            for (const char* p = mapping.targetName; *p; ++p) ++length;
            if (length + 2 > targetChars) { SetLastError(122); return 0; }
            for (size_t i = 0; i < length; ++i) targetPath[i] = mapping.targetName[i];
            targetPath[length] = targetPath[length + 1] = L'\0';
            return static_cast<uint32_t>(length + 1);
        }
        SetLastError(2);
        return 0;
    }
    size_t total = 1;
    for (const auto& mapping : micant::shared::BareMetalDosDeviceMappings) {
        size_t length = 0;
        for (const char* p = mapping.driveName; *p; ++p) ++length;
        if (total > targetChars || length + 1 > targetChars - total) {
            SetLastError(122); return 0;
        }
        for (size_t i = 0; i < length; ++i) targetPath[total + i] = mapping.driveName[i];
        targetPath[total + length] = L'\0';
        total += length + 1;
    }
    targetPath[total - 1] = L'\0';
    return static_cast<uint32_t>(total);
}

__declspec(dllexport) int32_t __stdcall SetProcessShutdownParameters(
        uint32_t level, uint32_t flags) noexcept {
    constexpr uint32_t maximumShutdownLevel = 0x3FFu;
    constexpr uint32_t shutdownNoRetry = 0x00000001u;
    if (level > maximumShutdownLevel || (flags & ~shutdownNoRetry)) {
        SetLastError(87);
        return 0;
    }
    g_shutdownLevel = level;
    g_shutdownFlags = flags;
    (void)g_shutdownLevel;
    (void)g_shutdownFlags;
    return 1;
}

void MicaKernel32ReleaseToolhelpSnapshot(void* handle) noexcept {
    closeSnapshot(handle);
}
}
