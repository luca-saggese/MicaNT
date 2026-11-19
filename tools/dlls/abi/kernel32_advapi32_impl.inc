#if defined(MICANT_FREESTANDING_KERNEL32)

#include <cstdint>
#include <climits>
#include <cwchar>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/native_process_abi.hpp"
#include "micant/shared/system_time.hpp"
#include "micant/um/subsystems/winbase/utf8_conversion.hpp"

static_assert(sizeof(void*) == 8);

namespace { void PaintDebugTrace(uint32_t, uint32_t, uint32_t) noexcept; }

extern "C" {


__declspec(dllimport) int32_t __stdcall NtQuerySystemTime(int64_t* systemTime) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryPerformanceCounter(
    int64_t* performanceCounter, int64_t* performanceFrequency) noexcept;
__declspec(dllimport) int32_t __stdcall NtDelayExecution(
    uint8_t alertable, const int64_t* delayInterval) noexcept;
__declspec(dllimport) int32_t __stdcall NtYieldExecution() noexcept;
__declspec(dllimport) int32_t __stdcall NtProtectVirtualMemory(
    void* process, void** base, size_t* size, uint32_t newProtection,
    uint32_t* oldProtection) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryVirtualMemory(
    void* process, const void* base, uint32_t informationClass,
    void* information, size_t informationLength, size_t* returnLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtClose(void* handle) noexcept;
__declspec(dllimport) int32_t __stdcall NtCreateSection(
    void** section, uint32_t access, void* attributes, void* maximumSize,
    uint32_t protection, uint32_t allocationAttributes, void* file) noexcept;
__declspec(dllimport) int32_t __stdcall NtMapViewOfSection(
    void* section, void* process, uintptr_t* base, uintptr_t zeroBits,
    size_t commitSize, void* offset, size_t* viewSize, uint32_t inherit,
    uint32_t allocationType, uint32_t protection) noexcept;
__declspec(dllimport) int32_t __stdcall NtUnmapViewOfSection(
    void* process, uintptr_t base) noexcept;
__declspec(dllimport) int32_t __stdcall NtCreateThreadEx(
    void** thread, uint32_t access, void* attributes, void* process,
    void* start, void* argument, uint32_t flags, uintptr_t zeroBits,
    size_t stackSize, size_t maximumStackSize, void* attributeList) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryInformationThread(
    void* thread, uint32_t informationClass, void* information,
    uint32_t informationLength, uint32_t* returnLength) noexcept;
__declspec(dllimport) uint32_t __stdcall RtlNtStatusToDosError(int32_t status) noexcept;
__declspec(dllimport) int32_t __stdcall NtCreateEvent(void** handle, uint32_t access, void* attributes, uint32_t type, uint8_t initialState) noexcept;
__declspec(dllimport) int32_t __stdcall NtSetEvent(void* handle, int32_t* previousState) noexcept;
__declspec(dllimport) int32_t __stdcall NtResetEvent(void* handle, int32_t* previousState) noexcept;
__declspec(dllimport) int32_t __stdcall NtWaitForSingleObject(void* handle, uint8_t alertable, const int64_t* timeout) noexcept;
__declspec(dllimport) int32_t __stdcall NtAllocateVirtualMemory(void* process, void** base, uint64_t zeroBits, size_t* size, uint32_t type, uint32_t protect) noexcept;
__declspec(dllimport) int32_t __stdcall NtFreeVirtualMemory(void* process, void** base, size_t* size, uint32_t type) noexcept;
__declspec(dllimport) int32_t __stdcall NtDuplicateObject(
    void* sourceProcess, void* sourceHandle, void* targetProcess,
    void** targetHandle, uint32_t desiredAccess, uint32_t handleAttributes,
    uint32_t options) noexcept;
__declspec(dllimport) int32_t __stdcall NtWaitForSingleObject(
    void* handle, uint8_t alertable, const int64_t* timeout) noexcept;
__declspec(dllexport) void __stdcall SetLastError(uint32_t error) noexcept;
__declspec(dllimport) int32_t __stdcall NtTerminateProcess(void* process, int32_t exitStatus) noexcept;
__declspec(dllimport) void __stdcall RtlZeroMemory(void* destination, size_t length) noexcept;
__declspec(dllimport) void __stdcall RtlRaiseException(void* exceptionRecord) noexcept;
__declspec(dllimport) void* __cdecl malloc(size_t size) noexcept;
__declspec(dllimport) void __cdecl free(void* block) noexcept;
__declspec(dllimport) int32_t __stdcall NtCreateUserProcess(
    micant::Handle* processHandle, micant::Handle* threadHandle,
    uint32_t processDesiredAccess, uint32_t threadDesiredAccess,
    micant::ObjectAttributes* processAttributes, micant::ObjectAttributes* threadAttributes,
    uint32_t processFlags, uint32_t threadFlags, void* processParameters,
    micant::shared::native_process::ProcessCreateInfo64* createInfo,
    micant::PsAttributeList* attributeList) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryInformationProcess(
    void* process, uint32_t informationClass, void* information,
    uint32_t informationLength, uint32_t* returnLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryInformationThread(
    void* thread, uint32_t informationClass, void* information,
    uint32_t informationLength, uint32_t* returnLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtFlushInstructionCache(
    void* process, const void* base, size_t size) noexcept;
__declspec(dllimport) int32_t __stdcall NtCreateFile(
    micant::Handle* fileHandle, uint32_t desiredAccess,
    micant::ObjectAttributes* objectAttributes, micant::IoStatusBlock* ioStatusBlock,
    micant::LargeInteger* allocationSize, uint32_t fileAttributes,
    uint32_t shareAccess, uint32_t createDisposition, uint32_t createOptions,
    void* eaBuffer, uint32_t eaLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryDirectoryFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock,
    void* fileInformation, uint32_t length,
    micant::FileInformationClass fileInformationClass, bool returnSingleEntry,
    micant::UnicodeString* fileName, bool restartScan) noexcept;
__declspec(dllexport) uint32_t __stdcall GetCurrentDirectoryW(
    uint32_t capacity, wchar_t* buffer) noexcept;
__declspec(dllimport) int32_t __stdcall NtReadFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock, void* buffer,
    uint32_t length, micant::LargeInteger* byteOffset, uint32_t* key) noexcept;
__declspec(dllimport) int32_t __stdcall NtWriteFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock, const void* buffer,
    uint32_t length, micant::LargeInteger* byteOffset, uint32_t* key) noexcept;
__declspec(dllimport) int32_t __stdcall NtFlushBuffersFile(
    micant::Handle fileHandle, micant::IoStatusBlock* ioStatusBlock) noexcept;
__declspec(dllimport) int32_t __stdcall NtDeviceIoControlFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock,
    uint32_t ioControlCode, const void* inputBuffer, uint32_t inputBufferLength,
    void* outputBuffer, uint32_t outputBufferLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryInformationFile(
    micant::Handle fileHandle, micant::IoStatusBlock* ioStatusBlock,
    void* fileInformation, uint32_t length,
    micant::FileInformationClass fileInformationClass) noexcept;
__declspec(dllimport) int32_t __stdcall NtSetInformationFile(
    micant::Handle fileHandle, micant::IoStatusBlock* ioStatusBlock,
    void* fileInformation, uint32_t length,
    micant::FileInformationClass fileInformationClass) noexcept;
__declspec(dllexport) void __stdcall SetLastError(uint32_t error) noexcept;

struct Kernel32SystemTime {
    uint16_t year, month, dayOfWeek, day, hour, minute, second, milliseconds;
};
struct Kernel32FileTime { uint32_t low, high; };
struct Kernel32TimeZoneInformation {
    int32_t bias;
    wchar_t standardName[32];
    Kernel32SystemTime standardDate;
    int32_t standardBias;
    wchar_t daylightName[32];
    Kernel32SystemTime daylightDate;
    int32_t daylightBias;
};
static_assert(sizeof(Kernel32SystemTime) == 16);
static_assert(sizeof(Kernel32FileTime) == 8);
static_assert(sizeof(Kernel32TimeZoneInformation) == 172);

static bool ReadSystemTimeZone(Kernel32TimeZoneInformation& zone,
                               uint32_t& zoneId) noexcept {
    const auto* base = reinterpret_cast<const volatile uint8_t*>(0x7FFE0000ULL);
    const auto* low = reinterpret_cast<const volatile uint32_t*>(base + 0x20);
    const auto* high1 = reinterpret_cast<const volatile int32_t*>(base + 0x24);
    const auto* high2 = reinterpret_cast<const volatile int32_t*>(base + 0x28);
    bool stable = false;
    int32_t signedHigh{};
    uint32_t lowValue{};
    for (unsigned retry = 0; retry < 8; ++retry) {
        const int32_t first = *high1;
        const uint32_t middle = *low;
        const int32_t second = *high2;
        if (first == second) {
            signedHigh = first;
            lowValue = middle;
            stable = true;
            break;
        }
    }
    if (!stable) return false;
    const int64_t biasTicks = static_cast<int64_t>(
        (static_cast<uint64_t>(static_cast<uint32_t>(signedHigh)) << 32) | lowValue);
    if (biasTicks % static_cast<int64_t>(micant::shared::time::TicksPerMinute))
        return false;
    const int64_t bias = biasTicks /
        static_cast<int64_t>(micant::shared::time::TicksPerMinute);
    if (bias < -1440 || bias > 1440) return false;
    zone = {};
    zone.bias = static_cast<int32_t>(bias);
    if (bias == 0) {
        zone.standardName[0] = L'U'; zone.standardName[1] = L'T';
        zone.standardName[2] = L'C';
    } else {
        zone.standardName[0] = L'M'; zone.standardName[1] = L'i';
        zone.standardName[2] = L'c'; zone.standardName[3] = L'a';
        zone.standardName[4] = L'N'; zone.standardName[5] = L'T';
    }
    zoneId = *reinterpret_cast<const volatile uint32_t*>(base + 0x240);
    return zoneId <= 2;
}

static bool QueryUtcFields(micant::shared::time::SystemTimeFields& fields) noexcept {
    int64_t ticks{};
    if (NtQuerySystemTime(&ticks) < 0 ||
        !micant::shared::time::fromSystemTime100ns(static_cast<uint64_t>(ticks), fields))
        return false;
    return true;
}

static bool ApplyTimeZone(const Kernel32TimeZoneInformation* zone,
                          const Kernel32SystemTime* utc,
                          Kernel32SystemTime* local) noexcept {
    using namespace micant::shared::time;
    if (!zone || !utc || !local || zone->bias < -1440 || zone->bias > 1440 ||
        zone->standardBias < -1440 || zone->standardBias > 1440 ||
        zone->daylightBias < -1440 || zone->daylightBias > 1440) return false;
    SystemTimeFields civil{utc->year, utc->month, utc->day, utc->hour, utc->minute,
        utc->second, utc->milliseconds, utc->dayOfWeek};
    uint64_t utcTicks{};
    if (!toSystemTime100ns(civil, utcTicks)) return false;
    int64_t bias = static_cast<int64_t>(zone->bias) + zone->standardBias;

    auto transition = [&](const Kernel32SystemTime& rule, uint16_t year,
                          int32_t priorBias, uint64_t& instant) noexcept {
        if (rule.month == 0) return false;
        if (rule.year && rule.year != year) return false;
        const uint16_t effectiveYear = rule.year ? rule.year : year;
        uint32_t day = rule.day;
        if (rule.year == 0) {
            if (rule.month > 12 || rule.day < 1 || rule.day > 5 || rule.dayOfWeek > 6)
                return false;
            SystemTimeFields first{effectiveYear, rule.month, 1, 0, 0, 0, 0, 0};
            uint64_t firstTicks{};
            if (!toSystemTime100ns(first, firstTicks)) return false;
            const uint32_t firstWeekday = fromSystemTime100ns(firstTicks, first)
                ? first.weekday : 0;
            day = 1 + (rule.dayOfWeek + 7 - firstWeekday) % 7 +
                  (rule.day - 1) * 7;
            const auto maxDay = daysInMonth(effectiveYear, rule.month);
            if (day > maxDay) day -= 7;
        }
        SystemTimeFields localRule{effectiveYear, rule.month, static_cast<uint16_t>(day),
            rule.hour, rule.minute, rule.second, rule.milliseconds, 0};
        uint64_t localTicks{};
        if (!toSystemTime100ns(localRule, localTicks)) return false;
        const int64_t offset = priorBias * static_cast<int64_t>(TicksPerMinute);
        if (offset >= 0) {
            if (localTicks > UINT64_MAX - static_cast<uint64_t>(offset)) return false;
            instant = localTicks + static_cast<uint64_t>(offset);
        } else {
            const auto add = static_cast<uint64_t>(-offset);
            if (localTicks < add) return false;
            instant = localTicks - add;
        }
        return true;
    };

    if ((zone->daylightDate.month == 0) != (zone->standardDate.month == 0))
        return false;
    if (zone->daylightDate.month != 0) {
        uint64_t daylightStart{}, standardStart{};
        const int64_t daylightTotal = static_cast<int64_t>(zone->bias) + zone->daylightBias;
        if (!transition(zone->daylightDate, utc->year, static_cast<int32_t>(bias), daylightStart) ||
            !transition(zone->standardDate, utc->year, static_cast<int32_t>(daylightTotal), standardStart))
            return false;
        const bool inDaylight = daylightStart < standardStart
            ? utcTicks >= daylightStart && utcTicks < standardStart
            : utcTicks >= daylightStart || utcTicks < standardStart;
        if (inDaylight) bias = daylightTotal;
    }
    const int64_t delta = -bias * static_cast<int64_t>(TicksPerMinute);
    uint64_t localTicks{};
    if (delta >= 0) {
        if (utcTicks > UINT64_MAX - static_cast<uint64_t>(delta)) return false;
        localTicks = utcTicks + static_cast<uint64_t>(delta);
    } else {
        if (utcTicks < static_cast<uint64_t>(-delta)) return false;
        localTicks = utcTicks - static_cast<uint64_t>(-delta);
    }
    if (!fromSystemTime100ns(localTicks, civil)) return false;
    *local = {civil.year, civil.month, civil.weekday, civil.day, civil.hour,
              civil.minute, civil.second, civil.milliseconds};
    return true;
}

static bool LocalTimeToUtc(const Kernel32TimeZoneInformation* zone,
                           const Kernel32SystemTime* local,
                           Kernel32SystemTime* utc) noexcept {
    using namespace micant::shared::time;
    if (!zone || !local || !utc || zone->bias < -1440 || zone->bias > 1440 ||
        zone->standardBias < -1440 || zone->standardBias > 1440 ||
        zone->daylightBias < -1440 || zone->daylightBias > 1440) return false;
    SystemTimeFields localFields{local->year, local->month, local->day, local->hour,
        local->minute, local->second, local->milliseconds, local->dayOfWeek};
    uint64_t localTicks{};
    if (!toSystemTime100ns(localFields, localTicks)) return false;
    const int64_t standardBias = static_cast<int64_t>(zone->bias) + zone->standardBias;
    const int64_t daylightBias = static_cast<int64_t>(zone->bias) + zone->daylightBias;
    auto candidateForBias = [&](int64_t bias, Kernel32SystemTime& result) noexcept {
        if (bias < -2880 || bias > 2880) return false;
        const int64_t delta = bias * static_cast<int64_t>(TicksPerMinute);
        uint64_t candidate{};
        if (delta >= 0) {
            if (localTicks > UINT64_MAX - static_cast<uint64_t>(delta)) return false;
            candidate = localTicks + static_cast<uint64_t>(delta);
        } else {
            if (localTicks < static_cast<uint64_t>(-delta)) return false;
            candidate = localTicks - static_cast<uint64_t>(-delta);
        }
        SystemTimeFields fields{};
        if (!fromSystemTime100ns(candidate, fields)) return false;
        Kernel32SystemTime utcCandidate{fields.year, fields.month, fields.weekday,
            fields.day, fields.hour, fields.minute, fields.second, fields.milliseconds};
        Kernel32SystemTime roundTrip{};
        if (!ApplyTimeZone(zone, &utcCandidate, &roundTrip) ||
            roundTrip.year != local->year || roundTrip.month != local->month ||
            roundTrip.day != local->day || roundTrip.hour != local->hour ||
            roundTrip.minute != local->minute || roundTrip.second != local->second ||
            roundTrip.milliseconds != local->milliseconds) return false;
        result = utcCandidate;
        return true;
    };
    Kernel32SystemTime result{};
    if (!candidateForBias(standardBias, result) &&
        (!zone->daylightDate.month || !zone->standardDate.month ||
         !candidateForBias(daylightBias, result))) return false;
    *utc = result;
    return true;
}

__declspec(dllexport) int32_t __stdcall FileTimeToSystemTime(
    const Kernel32FileTime* fileTime, Kernel32SystemTime* systemTime) noexcept {
    if (!fileTime || !systemTime) { SetLastError(87); return 0; }
    const uint64_t ticks = (static_cast<uint64_t>(fileTime->high) << 32) | fileTime->low;
    micant::shared::time::SystemTimeFields fields{};
    if (!micant::shared::time::fromSystemTime100ns(ticks, fields)) {
        SetLastError(87); return 0;
    }
    *systemTime = {fields.year, fields.month, fields.weekday, fields.day,
        fields.hour, fields.minute, fields.second, fields.milliseconds};
    return 1;
}

__declspec(dllexport) int32_t __stdcall SystemTimeToFileTime(
    const Kernel32SystemTime* systemTime, Kernel32FileTime* fileTime) noexcept {
    if (!systemTime || !fileTime) { SetLastError(87); return 0; }
    const micant::shared::time::SystemTimeFields fields{systemTime->year,
        systemTime->month, systemTime->day, systemTime->hour, systemTime->minute,
        systemTime->second, systemTime->milliseconds, systemTime->dayOfWeek};
    uint64_t ticks{};
    if (!micant::shared::time::toSystemTime100ns(fields, ticks)) {
        SetLastError(87); return 0;
    }
    fileTime->low = static_cast<uint32_t>(ticks);
    fileTime->high = static_cast<uint32_t>(ticks >> 32);
    return 1;
}

__declspec(dllexport) int32_t __stdcall FileTimeToLocalFileTime(
    const Kernel32FileTime* fileTime, Kernel32FileTime* localFileTime) noexcept {
    if (!fileTime || !localFileTime) { SetLastError(87); return 0; }
    const uint64_t ticks = (static_cast<uint64_t>(fileTime->high) << 32) | fileTime->low;
    micant::shared::time::SystemTimeFields utcFields{};
    if (!micant::shared::time::fromSystemTime100ns(ticks, utcFields)) {
        SetLastError(87); return 0;
    }
    Kernel32SystemTime utc{utcFields.year, utcFields.month, utcFields.weekday,
        utcFields.day, utcFields.hour, utcFields.minute, utcFields.second,
        utcFields.milliseconds};
    Kernel32TimeZoneInformation zone{}; uint32_t zoneId{};
    Kernel32SystemTime local{};
    uint64_t localTicks{};
    if (!ReadSystemTimeZone(zone, zoneId) || !ApplyTimeZone(&zone, &utc, &local) ||
        !micant::shared::time::toSystemTime100ns(
            {local.year, local.month, local.day, local.hour, local.minute,
             local.second, local.milliseconds, local.dayOfWeek}, localTicks)) {
        SetLastError(87); return 0;
    }
    (void)zoneId;
    localFileTime->low = static_cast<uint32_t>(localTicks);
    localFileTime->high = static_cast<uint32_t>(localTicks >> 32);
    return 1;
}

using VectoredExceptionHandler = int32_t (__stdcall *)(void*);
__declspec(dllimport) void* __stdcall RtlAddVectoredExceptionHandler(
    uint32_t first, VectoredExceptionHandler handler) noexcept;
__declspec(dllimport) unsigned long __stdcall RtlRemoveVectoredExceptionHandler(
    void* cookie) noexcept;
__declspec(dllimport) void* __stdcall RtlAddVectoredContinueHandler(
    uint32_t first, VectoredExceptionHandler handler) noexcept;
__declspec(dllimport) unsigned long __stdcall RtlRemoveVectoredContinueHandler(
    void* cookie) noexcept;

__declspec(dllexport) void* __stdcall AddVectoredExceptionHandler(
    uint32_t first, VectoredExceptionHandler handler) noexcept {
    return RtlAddVectoredExceptionHandler(first, handler);
}

__declspec(dllexport) unsigned long __stdcall RemoveVectoredExceptionHandler(
    void* cookie) noexcept {
    return RtlRemoveVectoredExceptionHandler(cookie);
}

__declspec(dllexport) void* __stdcall AddVectoredContinueHandler(
    uint32_t first, VectoredExceptionHandler handler) noexcept {
    return RtlAddVectoredContinueHandler(first, handler);
}

__declspec(dllexport) unsigned long __stdcall RemoveVectoredContinueHandler(
    void* cookie) noexcept {
    return RtlRemoveVectoredContinueHandler(cookie);
}

__declspec(dllexport) uint32_t __stdcall GetCurrentProcessId() noexcept {
    uint32_t processId = 0;
    __asm__ __volatile__("movl %%gs:0x40, %0" : "=r"(processId));
    return processId;
}

__declspec(dllexport) uint32_t __stdcall GetCurrentThreadId() noexcept {
    uint32_t threadId = 0;
    __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(threadId));
    return threadId;
}

static micant::shared::native_process::ProcessParametersPrefix64*
CurrentProcessParameters() noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || teb > UINTPTR_MAX - 0x60) return nullptr;
    const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    if (peb < 0x10000 || peb > UINTPTR_MAX - 0x28) return nullptr;
    return *reinterpret_cast<micant::shared::native_process::ProcessParametersPrefix64**>(peb + 0x20);
}

__declspec(dllexport) void* __stdcall GetStdHandle(uint32_t standardHandle) noexcept {
    auto* parameters = CurrentProcessParameters();
    if (!parameters) { SetLastError(6); return nullptr; }
    uintptr_t* value{};
    switch (standardHandle) {
        case static_cast<uint32_t>(-10): value = &parameters->StandardInput; break;
        case static_cast<uint32_t>(-11): value = &parameters->StandardOutput; break;
        case static_cast<uint32_t>(-12): value = &parameters->StandardError; break;
        default: SetLastError(87); return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    return reinterpret_cast<void*>(*value);
}

__declspec(dllexport) int32_t __stdcall SetStdHandle(
    uint32_t standardHandle, void* handle) noexcept {
    auto* parameters = CurrentProcessParameters();
    if (!parameters) { SetLastError(6); return 0; }
    uintptr_t* value{};
    switch (standardHandle) {
        case static_cast<uint32_t>(-10): value = &parameters->StandardInput; break;
        case static_cast<uint32_t>(-11): value = &parameters->StandardOutput; break;
        case static_cast<uint32_t>(-12): value = &parameters->StandardError; break;
        default: SetLastError(87); return 0;
    }
    *value = reinterpret_cast<uintptr_t>(handle);
    return 1;
}

// CMD is hosted by Luce's compositor terminal window.  Its process parameters
// already carry the terminal's standard streams, so AllocConsole attaches to
// that existing console instead of trying to create a second native window.
__declspec(dllexport) int32_t __stdcall AllocConsole() noexcept {
    auto* parameters = CurrentProcessParameters();
    if (!parameters || !parameters->StandardInput ||
        !parameters->StandardOutput || !parameters->StandardError) {
        SetLastError(6);
        return 0;
    }
    return 1;
}

__declspec(dllexport) int32_t __stdcall FreeConsole() noexcept {
    // The compositor owns the terminal session lifetime; detaching is not
    // implemented until Luce exposes a per-process console attachment object.
    SetLastError(50);
    return 0;
}

using Kernel32CopyProgressRoutine = uint32_t (__stdcall *)(
    int64_t, int64_t, int64_t, int64_t, uint32_t, uint32_t, void*, void*, void*);
__declspec(dllexport) int32_t __stdcall CopyFileExW(
    const wchar_t* existingFileName, const wchar_t* newFileName,
    Kernel32CopyProgressRoutine progressRoutine, void* data, int32_t* cancel,
    uint32_t flags) noexcept {
    (void)progressRoutine;
    (void)data;
    (void)cancel;
    if (!existingFileName || !newFileName || (flags & ~0xFu)) {
        SetLastError(87);
        return 0;
    }
    // File creation/truncation is not yet part of Luce's native filesystem
    // syscall contract. Report that limitation instead of claiming a copy.
    SetLastError(50);
    return 0;
}

__declspec(dllexport) int32_t __stdcall CopyFileW(
    const wchar_t* existingFileName, const wchar_t* newFileName,
    int32_t failIfExists) noexcept {
    if (failIfExists != 0 && failIfExists != 1) {
        SetLastError(87);
        return 0;
    }
    return CopyFileExW(existingFileName, newFileName, nullptr, nullptr, nullptr,
                       failIfExists ? 1u : 0u);
}

__declspec(dllexport) int32_t __stdcall CreateDirectoryW(
    const wchar_t* pathName, void* securityAttributes) noexcept {
    (void)securityAttributes;
    if (!pathName || !*pathName) {
        SetLastError(87);
        return 0;
    }
    SetLastError(50); // Directory creation is not exposed by the native FS ABI yet.
    return 0;
}

__declspec(dllexport) int32_t __stdcall CreatePipe(
    void** readPipe, void** writePipe, void* securityAttributes,
    uint32_t size) noexcept {
    (void)securityAttributes;
    (void)size;
    if (readPipe) *readPipe = nullptr;
    if (writePipe) *writePipe = nullptr;
    if (!readPipe || !writePipe) {
        SetLastError(87);
        return 0;
    }
    SetLastError(50); // Anonymous pipes are not yet in the Luce native I/O ABI.
    return 0;
}

__declspec(dllexport) int32_t __stdcall DeleteFileW(const wchar_t* pathName) noexcept {
    if (!pathName || !*pathName) {
        SetLastError(87);
        return 0;
    }
    SetLastError(50); // Native filesystem deletion is not exposed to Ring 3 yet.
    return 0;
}

__declspec(dllexport) int32_t __stdcall DeviceIoControl(
    void* device, uint32_t controlCode, void* inputBuffer,
    uint32_t inputBufferSize, void* outputBuffer, uint32_t outputBufferSize,
    uint32_t* bytesReturned, void* overlapped) noexcept {
    if (bytesReturned) *bytesReturned = 0;
    if (!device || device == reinterpret_cast<void*>(static_cast<intptr_t>(-1)) ||
        (!inputBuffer && inputBufferSize) || (!outputBuffer && outputBufferSize) || overlapped) {
        SetLastError(87);
        return 0;
    }
    micant::IoStatusBlock ioStatus{};
    const int32_t status = NtDeviceIoControlFile(
        reinterpret_cast<micant::Handle>(device), 0, nullptr, nullptr, &ioStatus,
        controlCode, inputBuffer, inputBufferSize, outputBuffer, outputBufferSize);
    if (status < 0) {
        SetLastError(status == static_cast<int32_t>(0xC000000Du) ? 87u : 31u);
        return 0;
    }
    if (bytesReturned) *bytesReturned = static_cast<uint32_t>(ioStatus.information);
    return 1;
}

__declspec(dllexport) int32_t __stdcall WriteFile(
    void* file, const void* buffer, uint32_t bytesToWrite, uint32_t* bytesWritten,
    void* overlapped) noexcept {
    if (bytesWritten) *bytesWritten = 0;
    if (!file || file == reinterpret_cast<void*>(static_cast<intptr_t>(-1)) ||
        (!buffer && bytesToWrite) || overlapped) {
        SetLastError(87);
        return 0;
    }
    micant::IoStatusBlock ioStatus{};
    const int32_t status = NtWriteFile(reinterpret_cast<micant::Handle>(file),
        0, nullptr, nullptr, &ioStatus, buffer, bytesToWrite, nullptr, nullptr);
    if (status < 0) {
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD)
        PaintDebugTrace(212u, static_cast<uint32_t>(status), bytesToWrite);
#endif
        SetLastError(status == static_cast<int32_t>(0xC000000Du) ? 87u : 31u);
        return 0;
    }
    if (bytesWritten) *bytesWritten = static_cast<uint32_t>(ioStatus.information);
    return 1;
}

struct Kernel32Coord { int16_t x, y; };
__declspec(dllexport) int32_t __stdcall FillConsoleOutputAttribute(
    void* console, uint16_t attribute, uint32_t length, Kernel32Coord coordinate,
    uint32_t* written) noexcept {
    (void)attribute; (void)length; (void)coordinate;
    if (written) *written = 0;
    if (!console || console == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        SetLastError(6); return 0;
    }
    SetLastError(50); return 0;
}
__declspec(dllexport) int32_t __stdcall FillConsoleOutputCharacterW(
    void* console, wchar_t character, uint32_t length, Kernel32Coord coordinate,
    uint32_t* written) noexcept {
    (void)character; (void)length; (void)coordinate;
    if (written) *written = 0;
    if (!console || console == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        SetLastError(6); return 0;
    }
    SetLastError(50); return 0;
}

__declspec(dllexport) uint32_t __stdcall GetConsoleCP() noexcept { return 65001; }
__declspec(dllexport) uint32_t __stdcall GetConsoleOutputCP() noexcept { return 65001; }
__declspec(dllexport) int32_t __stdcall GetConsoleMode(void* console, uint32_t* mode) noexcept {
    if (mode) *mode = 0;
    if (!console || !mode || console == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        SetLastError(6); return 0;
    }
    SetLastError(50); return 0;
}
struct Kernel32ConsoleScreenBufferInfo {
    Kernel32Coord size; Kernel32Coord cursorPosition; uint16_t attributes;
    uint16_t left, top, right, bottom; Kernel32Coord maximumWindowSize;
};
__declspec(dllexport) int32_t __stdcall GetConsoleScreenBufferInfo(
    void* console, Kernel32ConsoleScreenBufferInfo* info) noexcept {
    if (info) *info = {};
    if (!console || !info || console == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        SetLastError(6); return 0;
    }
    SetLastError(50); return 0;
}
__declspec(dllexport) uint32_t __stdcall GetConsoleTitleW(
    wchar_t* title, uint32_t capacity) noexcept {
    if (title && capacity) title[0] = 0;
    if (!title && capacity) { SetLastError(87); return 0; }
    return 0;
}
__declspec(dllexport) void* __stdcall GetCurrentProcess() noexcept {
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}
__declspec(dllexport) int32_t __stdcall SetConsoleCursorPosition(
    void* console, Kernel32Coord position) noexcept {
    (void)position;
    if (!console || console == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        SetLastError(6); return 0;
    }
    SetLastError(50); return 0;
}
__declspec(dllexport) int32_t __stdcall ReadConsoleInputW(
    void* console, void* records, uint32_t length, uint32_t* eventsRead) noexcept {
    if (eventsRead) *eventsRead = 0;
    if (!console || (!records && length) || !eventsRead) { SetLastError(87); return 0; }
    SetLastError(50); return 0;
}
__declspec(dllexport) int32_t __stdcall WriteConsoleW(
    void* console, const void* buffer, uint32_t length, uint32_t* written,
    void* reserved) noexcept {
    (void)reserved;
    if (written) *written = 0;
    if (!console || (!buffer && length) || !written) { SetLastError(87); return 0; }
    SetLastError(50); return 0;
}
__declspec(dllexport) int32_t __stdcall WriteConsoleOutputCharacterW(
    void* console, const wchar_t* characters, uint32_t length,
    Kernel32Coord coordinate, uint32_t* written) noexcept {
    (void)coordinate;
    if (written) *written = 0;
    if (!console || (!characters && length) || !written) { SetLastError(87); return 0; }
    SetLastError(50); return 0;
}

// These exports are present so native PE import binding succeeds. Until the
// corresponding locale or job-control service is available, each returns a
// documented failure instead of reporting synthetic success.
#define LUCE_KERNEL32_UNSUPPORTED(name) \
    __declspec(dllexport) uintptr_t __stdcall name(...) noexcept { \
        SetLastError(50); return 0; \
    }
LUCE_KERNEL32_UNSUPPORTED(FormatMessageW)
LUCE_KERNEL32_UNSUPPORTED(GetDateFormatW)
LUCE_KERNEL32_UNSUPPORTED(GetDiskFreeSpaceW)
LUCE_KERNEL32_UNSUPPORTED(GetFileSize)
LUCE_KERNEL32_UNSUPPORTED(GetFileTime)
LUCE_KERNEL32_UNSUPPORTED(GetFileType)
LUCE_KERNEL32_UNSUPPORTED(GetModuleFileNameW)
LUCE_KERNEL32_UNSUPPORTED(GetNumaHighestNodeNumber)
LUCE_KERNEL32_UNSUPPORTED(GetVolumeInformationW)
LUCE_KERNEL32_UNSUPPORTED(GetVolumePathNameW)
LUCE_KERNEL32_UNSUPPORTED(GlobalMemoryStatus)
LUCE_KERNEL32_UNSUPPORTED(MoveFileExW)
LUCE_KERNEL32_UNSUPPORTED(MoveFileW)
LUCE_KERNEL32_UNSUPPORTED(RemoveDirectoryW)
LUCE_KERNEL32_UNSUPPORTED(ResumeThread)
LUCE_KERNEL32_UNSUPPORTED(SearchPathW)
LUCE_KERNEL32_UNSUPPORTED(SetFileAttributesW)
LUCE_KERNEL32_UNSUPPORTED(SetFilePointer)
LUCE_KERNEL32_UNSUPPORTED(SetFileTime)
LUCE_KERNEL32_UNSUPPORTED(SetHandleInformation)
LUCE_KERNEL32_UNSUPPORTED(SetLocalTime)
LUCE_KERNEL32_UNSUPPORTED(SetProcessAffinityMask)
LUCE_KERNEL32_UNSUPPORTED(TerminateProcess)
LUCE_KERNEL32_UNSUPPORTED(VirtualAlloc)
LUCE_KERNEL32_UNSUPPORTED(VirtualFree)
LUCE_KERNEL32_UNSUPPORTED(WaitForMultipleObjects)
#undef LUCE_KERNEL32_UNSUPPORTED

__declspec(dllexport) int32_t __stdcall lstrcmpW(
    const wchar_t* left, const wchar_t* right) noexcept {
    if (!left || !right) { SetLastError(87); return left ? 1 : (right ? -1 : 0); }
    while (*left && *left == *right) { ++left; ++right; }
    return static_cast<int32_t>(static_cast<uint16_t>(*left) - static_cast<uint16_t>(*right));
}
__declspec(dllexport) int32_t __stdcall lstrcmpiW(
    const wchar_t* left, const wchar_t* right) noexcept {
    if (!left || !right) { SetLastError(87); return left ? 1 : (right ? -1 : 0); }
    while (*left) {
        wchar_t a = *left++, b = *right++;
        if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
        if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
        if (a != b) return static_cast<int32_t>(static_cast<uint16_t>(a) - static_cast<uint16_t>(b));
    }
    return *right ? -1 : 0;
}
__declspec(dllexport) int32_t __stdcall lstrlenW(const wchar_t* text) noexcept {
    if (!text) return 0;
    size_t length{};
    while (text[length] && length < static_cast<size_t>(INT32_MAX)) ++length;
    return static_cast<int32_t>(length);
}
__declspec(dllexport) wchar_t* __stdcall lstrcpynW(
    wchar_t* destination, const wchar_t* source, int32_t capacity) noexcept {
    if (!destination || !source || capacity < 0) {
        SetLastError(87);
        return nullptr;
    }
    if (capacity == 0) return destination;
    int32_t index = 0;
    for (; index + 1 < capacity && source[index]; ++index)
        destination[index] = source[index];
    destination[index] = L'\0';
    return destination;
}

__declspec(dllexport) int32_t __stdcall DuplicateHandle(
    void* sourceProcess, void* sourceHandle, void* targetProcess,
    void** targetHandle, uint32_t desiredAccess, int32_t inheritHandle,
    uint32_t options) noexcept {
    if (targetHandle) *targetHandle = nullptr;
    constexpr uint32_t duplicateCloseSource = 1u;
    constexpr uint32_t duplicateSameAccess = 2u;
    if (!sourceProcess || !targetProcess || !sourceHandle || !targetHandle ||
        (options & ~(duplicateCloseSource | duplicateSameAccess)) ||
        (inheritHandle != 0 && inheritHandle != 1)) {
        SetLastError(87);
        return 0;
    }
    void* duplicated{};
    const int32_t status = NtDuplicateObject(sourceProcess, sourceHandle,
        targetProcess, &duplicated, desiredAccess, inheritHandle ? 2u : 0u, options);
    if (status < 0) {
        SetLastError(status == static_cast<int32_t>(0xC0000008u) ? 6u : 5u);
        return 0;
    }
    *targetHandle = duplicated;
    return 1;
}

__declspec(dllexport) const wchar_t* __stdcall GetCommandLineW() noexcept {
    auto* parameters = CurrentProcessParameters();
    if (!parameters || !parameters->CommandLine.Buffer ||
        (parameters->CommandLine.Length & 1) ||
        parameters->CommandLine.Length > parameters->CommandLine.MaximumLength) {
        SetLastError(87);
        return nullptr;
    }
    return reinterpret_cast<const wchar_t*>(parameters->CommandLine.Buffer);
}

namespace {
volatile uint32_t g_currentDirectoryLock{};
wchar_t* g_ownedCurrentDirectory{};
uint32_t Win32ErrorFromNtStatus(int32_t status) noexcept;

void LockCurrentDirectory() noexcept {
    while (__atomic_exchange_n(&g_currentDirectoryLock, 1, __ATOMIC_ACQUIRE))
        __asm__ __volatile__("pause");
}
void UnlockCurrentDirectory() noexcept {
    __atomic_store_n(&g_currentDirectoryLock, 0, __ATOMIC_RELEASE);
}

struct CurrentDirectoryGuard {
    CurrentDirectoryGuard() noexcept { LockCurrentDirectory(); }
    ~CurrentDirectoryGuard() { UnlockCurrentDirectory(); }
};

bool IsPathSeparator(wchar_t value) noexcept;

bool CopyCurrentDirectoryPath(
    const micant::shared::native_process::ProcessParametersPrefix64* parameters,
    wchar_t* destination, size_t capacity, size_t& length) noexcept {
    length = 0;
    if (!parameters || !destination || !capacity) return false;
    const auto& current = parameters->CurrentDirectoryDosPath;
    const char16_t* source = current.Buffer;
    size_t sourceLength = current.Length / sizeof(char16_t);
    if (!source || (current.Length & 1) || current.Length > current.MaximumLength ||
        sourceLength < 2) {
        // Kernel-started images may not receive an explicit current directory.
        // Use their containing directory, matching the normal shell launch path.
        const auto& image = parameters->ImagePathName;
        if (!image.Buffer || (image.Length & 1) || image.Length > image.MaximumLength)
            return false;
        const size_t imageLength = image.Length / sizeof(char16_t);
        size_t separator = imageLength;
        while (separator && !IsPathSeparator(static_cast<wchar_t>(image.Buffer[separator - 1])))
            --separator;
        if (separator < 3) return false;
        source = image.Buffer;
        sourceLength = separator;
    }
    if (sourceLength >= capacity) return false;
    for (; length < sourceLength; ++length)
        destination[length] = static_cast<wchar_t>(source[length]);
    destination[length] = L'\0';
    return true;
}

bool IsPathSeparator(wchar_t value) noexcept {
    return value == L'\\' || value == L'/';
}

void NormalizeDosPath(wchar_t* path, size_t& length) noexcept {
    const size_t preserved = length >= 2 && IsPathSeparator(path[0]) &&
        IsPathSeparator(path[1]) ? 2 : 0;
    size_t output{};
    for (size_t input = 0; input < length; ++input) {
        const wchar_t value = IsPathSeparator(path[input]) ? L'\\' : path[input];
        if (value == L'\\' && output > 0 && output >= preserved &&
            path[output - 1] == L'\\')
            continue;
        path[output++] = value;
    }
    length = output;
    path[length] = L'\0';
}

constexpr size_t FindStateCapacity = 32;
constexpr uint64_t FindHandleTag = 0x4D49434600000000ULL;
constexpr uint32_t FileListDirectoryAccess = 0x0001u;
constexpr uint32_t FileReadAttributesAccess = 0x0080u;
constexpr uint32_t FileSynchronizeAccess = 0x00100000u;
constexpr uint32_t FileDirectoryOpenOption = 0x0001u;
constexpr uint32_t FileSynchronousOpenOption = 0x0020u;

struct Win32FileTime { uint32_t low{}, high{}; };
struct Win32FindDataW {
    uint32_t attributes{};
    Win32FileTime creationTime{}, lastAccessTime{}, lastWriteTime{};
    uint32_t fileSizeHigh{}, fileSizeLow{}, reserved0{}, reserved1{};
    wchar_t fileName[260]{};
    wchar_t alternateFileName[14]{};
};
static_assert(sizeof(Win32FindDataW) == 592);

struct FindState {
    micant::Handle directory{};
    uint32_t generation{};
    wchar_t pattern[260]{};
    bool active{};
};
FindState g_findStates[FindStateCapacity]{};
uint32_t g_findGeneration{1};

bool ResolveDosPath(const wchar_t* input, wchar_t* output,
                    size_t capacity, size_t& outputLength) noexcept {
    outputLength = 0;
    if (!input || !*input || !output || capacity < 4) return false;
    size_t inputLength = wcslen(input);
    if (inputLength >= capacity) return false;
    const bool driveAbsolute = inputLength >= 3 && input[1] == L':' &&
        IsPathSeparator(input[2]);
    const bool uncAbsolute = inputLength >= 2 && IsPathSeparator(input[0]) &&
        IsPathSeparator(input[1]);
    if (driveAbsolute || uncAbsolute) {
        for (; outputLength < inputLength; ++outputLength)
            output[outputLength] = IsPathSeparator(input[outputLength])
                ? L'\\' : input[outputLength];
        NormalizeDosPath(output, outputLength);
        return true;
    }

    CurrentDirectoryGuard guard;
    auto* parameters = CurrentProcessParameters();
    if (!parameters) return false;
    wchar_t currentPath[32767]{};
    size_t currentLength{};
    if (!CopyCurrentDirectoryPath(parameters, currentPath, std::size(currentPath), currentLength) ||
        currentLength + inputLength + 2 >= capacity) return false;
    for (; outputLength < currentLength; ++outputLength) output[outputLength] = currentPath[outputLength];
    size_t inputStart = 0;
    if (IsPathSeparator(input[0])) {
        outputLength = 2;
        output[0] = currentPath[0];
        output[1] = L':';
        inputStart = 1;
    } else if (inputLength >= 2 && input[1] == L':') {
        if (input[0] != output[0]) return false;
        inputStart = 2;
    } else if (outputLength && !IsPathSeparator(output[outputLength - 1])) {
        output[outputLength++] = L'\\';
    }
    for (size_t i = inputStart; i < inputLength; ++i)
        output[outputLength++] = IsPathSeparator(input[i]) ? L'\\' : input[i];
    NormalizeDosPath(output, outputLength);
    return true;
}

FindState* LookupFindState(void* handle) noexcept {
    const uint64_t token = reinterpret_cast<uint64_t>(handle);
    if ((token & 0xFFFFFFFF00000000ULL) != FindHandleTag) return nullptr;
    const uint32_t slot = static_cast<uint32_t>(token & 0xFFu);
    const uint32_t generation = static_cast<uint32_t>((token >> 8) & 0x00FFFFFFu);
    if (!slot || slot > FindStateCapacity) return nullptr;
    auto& state = g_findStates[slot - 1];
    return state.active && state.generation == generation ? &state : nullptr;
}

void* FindStateHandle(const FindState& state) noexcept {
    const uint64_t slot = static_cast<uint64_t>(&state - g_findStates + 1);
    return reinterpret_cast<void*>(FindHandleTag |
        (static_cast<uint64_t>(state.generation) << 8) | slot);
}

bool WildcardMatches(const wchar_t* pattern, const wchar_t* name) noexcept {
    if (!pattern || !name) return false;
    if (pattern[0] == L'*' && pattern[1] == L'.' && pattern[2] == L'*' && !pattern[3])
        return true;
    const wchar_t* star = nullptr;
    const wchar_t* retry = nullptr;
    while (*name) {
        wchar_t left = *pattern;
        wchar_t right = *name;
        if (left >= L'A' && left <= L'Z') left += L'a' - L'A';
        if (right >= L'A' && right <= L'Z') right += L'a' - L'A';
        if (left == L'?' || left == right) { ++pattern; ++name; continue; }
        if (left == L'*') { star = pattern++; retry = name; continue; }
        if (!star) return false;
        pattern = star + 1;
        name = ++retry;
    }
    while (*pattern == L'*') ++pattern;
    return !*pattern;
}

int32_t FillFindData(const micant::FileBothDirInformation& entry,
                     Win32FindDataW* output) noexcept {
    if (!output || entry.fileNameLength > 259u * sizeof(wchar_t)) return 0;
    RtlZeroMemory(output, sizeof(*output));
    output->attributes = entry.fileAttributes;
    output->creationTime.low = static_cast<uint32_t>(entry.creationTime.quadPart);
    output->creationTime.high = static_cast<uint32_t>(
        static_cast<uint64_t>(entry.creationTime.quadPart) >> 32);
    output->lastAccessTime.low = static_cast<uint32_t>(entry.lastAccessTime.quadPart);
    output->lastAccessTime.high = static_cast<uint32_t>(
        static_cast<uint64_t>(entry.lastAccessTime.quadPart) >> 32);
    output->lastWriteTime.low = static_cast<uint32_t>(entry.lastWriteTime.quadPart);
    output->lastWriteTime.high = static_cast<uint32_t>(
        static_cast<uint64_t>(entry.lastWriteTime.quadPart) >> 32);
    const uint64_t fileSize = entry.endOfFile.quadPart > 0
        ? static_cast<uint64_t>(entry.endOfFile.quadPart) : 0;
    output->fileSizeLow = static_cast<uint32_t>(fileSize);
    output->fileSizeHigh = static_cast<uint32_t>(fileSize >> 32);
    const size_t characters = entry.fileNameLength / sizeof(wchar_t);
    for (size_t i = 0; i < characters; ++i) output->fileName[i] = entry.fileName[i];
    output->fileName[characters] = L'\0';
    return 1;
}

bool NextMatchingFile(FindState& state, Win32FindDataW* output,
                      bool restart) noexcept {
    for (uint32_t attempt = 0; attempt < 4096; ++attempt) {
        micant::FileBothDirInformation entry{};
        micant::IoStatusBlock ioStatus{};
        const int32_t status = NtQueryDirectoryFile(state.directory, micant::Handle{},
            nullptr, nullptr, &ioStatus, &entry, sizeof(entry),
            micant::FileInformationClass::FileBothDirectoryInformation,
            true, nullptr, restart && attempt == 0);
        if (status < 0) {
            SetLastError(status == static_cast<int32_t>(0x80000006u) ? 18u
                : Win32ErrorFromNtStatus(status));
            return false;
        }
        if (WildcardMatches(state.pattern, entry.fileName))
            return FillFindData(entry, output) != 0;
    }
    SetLastError(18);
    return false;
}
}

__declspec(dllexport) uint32_t __stdcall GetFullPathNameW(
    const wchar_t* fileName, uint32_t capacity, wchar_t* buffer,
    wchar_t** filePart) noexcept {
    if (filePart) *filePart = nullptr;
    if (!fileName || !*fileName || (!buffer && capacity)) {
        SetLastError(87);
        return 0;
    }
    wchar_t fullPath[32767]{};
    size_t length{};
    if (!ResolveDosPath(fileName, fullPath, sizeof(fullPath) / sizeof(fullPath[0]), length)) {
        SetLastError(87);
        return 0;
    }
    if (filePart) {
        size_t leaf = length;
        while (leaf && !IsPathSeparator(fullPath[leaf - 1])) --leaf;
        if (leaf < length && buffer && capacity > length)
            *filePart = buffer + leaf;
    }
    if (!buffer || capacity <= length) {
        SetLastError(122);
        return static_cast<uint32_t>(length + 1);
    }
    for (size_t i = 0; i <= length; ++i) buffer[i] = fullPath[i];
    return static_cast<uint32_t>(length);
}

__declspec(dllexport) void* __stdcall FindFirstFileW(
    const wchar_t* fileName, Win32FindDataW* output) noexcept {
    if (!fileName || !*fileName || !output) {
        SetLastError(87);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    wchar_t fullPattern[32767]{};
    size_t fullLength{};
    if (!ResolveDosPath(fileName, fullPattern, 32767, fullLength)) {
        SetLastError(87);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    size_t separator = fullLength;
    while (separator && !IsPathSeparator(fullPattern[separator - 1])) --separator;
    const size_t patternStart = separator;
    if (patternStart >= fullLength || fullLength - patternStart >= 260) {
        SetLastError(87);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    wchar_t directoryPath[32767]{};
    size_t directoryLength = separator;
    while (directoryLength > 3 && IsPathSeparator(fullPattern[directoryLength - 1]))
        --directoryLength;
    for (size_t i = 0; i < directoryLength; ++i)
        directoryPath[i] = fullPattern[i];
    if (directoryLength == 2 && fullPattern[1] == L':')
        directoryPath[directoryLength++] = L'\\';
    directoryPath[directoryLength] = L'\0';

    micant::UnicodeString directoryName(directoryPath);
    micant::ObjectAttributes attributes{};
    attributes.objectName = &directoryName;
    attributes.attributes = micant::OBJ_CASE_INSENSITIVE;
    micant::IoStatusBlock ioStatus{};
    micant::Handle directory{};
    const int32_t openStatus = NtCreateFile(&directory,
        FileListDirectoryAccess | FileReadAttributesAccess | FileSynchronizeAccess,
        &attributes, &ioStatus, nullptr, 0, 0, 1,
        FileDirectoryOpenOption | FileSynchronousOpenOption, nullptr, 0);
    if (openStatus < 0) {
        SetLastError(Win32ErrorFromNtStatus(openStatus));
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }

    FindState* state = nullptr;
    for (auto& candidate : g_findStates) {
        if (!candidate.active) { state = &candidate; break; }
    }
    if (!state) {
        (void)NtClose(reinterpret_cast<void*>(directory));
        SetLastError(8);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    uint32_t generation = g_findGeneration++ & 0x00FFFFFFu;
    if (!generation) generation = g_findGeneration++ & 0x00FFFFFFu;
    *state = {};
    state->directory = directory;
    state->generation = generation;
    state->active = true;
    for (size_t i = patternStart; i < fullLength; ++i)
        state->pattern[i - patternStart] = fullPattern[i];
    state->pattern[fullLength - patternStart] = L'\0';
    if (!NextMatchingFile(*state, output, true)) {
        (void)NtClose(reinterpret_cast<void*>(state->directory));
        *state = {};
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    return FindStateHandle(*state);
}

__declspec(dllexport) int32_t __stdcall FindNextFileW(
    void* findHandle, Win32FindDataW* output) noexcept {
    auto* state = LookupFindState(findHandle);
    if (!state || !output) {
        SetLastError(state ? 87u : 6u);
        return 0;
    }
    return NextMatchingFile(*state, output, false) ? 1 : 0;
}

__declspec(dllexport) int32_t __stdcall FindClose(void* findHandle) noexcept {
    auto* state = LookupFindState(findHandle);
    if (!state) { SetLastError(6); return 0; }
    const int32_t status = NtClose(reinterpret_cast<void*>(state->directory));
    *state = {};
    if (status < 0) { SetLastError(Win32ErrorFromNtStatus(status)); return 0; }
    return 1;
}

__declspec(dllexport) uint32_t __stdcall GetFileAttributesW(
    const wchar_t* fileName) noexcept {
    if (!fileName || !*fileName) { SetLastError(87); return UINT32_MAX; }
    Win32FindDataW data{};
    void* search = FindFirstFileW(fileName, &data);
    if (search == reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        return UINT32_MAX;
    (void)FindClose(search);
    return data.attributes;
}

__declspec(dllexport) uint32_t __stdcall GetCurrentDirectoryW(
    uint32_t capacity, wchar_t* buffer) noexcept {
    if (!buffer && capacity) { SetLastError(87); return 0; }
    CurrentDirectoryGuard guard;
    auto* parameters = CurrentProcessParameters();
    if (!parameters) { SetLastError(87); return 0; }
    wchar_t currentPath[32767]{};
    size_t length{};
    if (!CopyCurrentDirectoryPath(parameters, currentPath, std::size(currentPath), length)) {
        SetLastError(87);
        return 0;
    }
    if (length >= 32767 || length + 1 > UINT32_MAX) {
        SetLastError(206);
        return 0;
    }
    const uint32_t required = static_cast<uint32_t>(length + 1);
    if (capacity < required) {
        SetLastError(122);
        return required;
    }
    for (size_t i = 0; i <= length; ++i) buffer[i] = currentPath[i];
    return static_cast<uint32_t>(length);
}

__declspec(dllexport) int32_t __stdcall SetCurrentDirectoryW(
    const wchar_t* pathName) noexcept {
    if (!pathName || !*pathName) { SetLastError(87); return 0; }
    CurrentDirectoryGuard guard;
    auto* parameters = CurrentProcessParameters();
    if (!parameters) { SetLastError(87); return 0; }

    wchar_t fullPath[32767]{};
    size_t length = 0;
    const size_t inputLength = wcslen(pathName);
    if (!inputLength || inputLength >= 32767) { SetLastError(206); return 0; }
    const bool driveAbsolute = inputLength >= 3 && pathName[1] == L':' &&
        IsPathSeparator(pathName[2]);
    const bool uncAbsolute = inputLength >= 2 && IsPathSeparator(pathName[0]) &&
        IsPathSeparator(pathName[1]);
    if (driveAbsolute || uncAbsolute) {
        length = inputLength;
        for (size_t i = 0; i < length; ++i)
            fullPath[i] = IsPathSeparator(pathName[i]) ? L'\\' : pathName[i];
    } else {
        wchar_t currentPath[32767]{};
        size_t currentLength{};
        if (!CopyCurrentDirectoryPath(parameters, currentPath, std::size(currentPath), currentLength)) {
            SetLastError(87); return 0;
        }
        if (currentLength < 2 || currentLength >= 32767) { SetLastError(87); return 0; }
        if (IsPathSeparator(pathName[0])) {
            fullPath[0] = currentPath[0];
            fullPath[1] = L':';
            length = 2;
        } else {
            for (; length < currentLength; ++length) fullPath[length] = currentPath[length];
        }
        if (!length || !IsPathSeparator(fullPath[length - 1]))
            fullPath[length++] = L'\\';
        for (size_t i = IsPathSeparator(pathName[0]) ? 1u : 0u;
             i < inputLength; ++i) {
            if (length + 1 >= 32767) { SetLastError(206); return 0; }
            fullPath[length++] = IsPathSeparator(pathName[i]) ? L'\\' : pathName[i];
        }
    }
    NormalizeDosPath(fullPath, length);
    while (length > 3 && fullPath[length - 1] == L'\\') --length;
    fullPath[length] = L'\0';

    micant::UnicodeString objectName(fullPath);
    micant::ObjectAttributes attributes{};
    attributes.objectName = &objectName;
    attributes.attributes = micant::OBJ_CASE_INSENSITIVE;
    micant::IoStatusBlock ioStatus{};
    micant::Handle newDirectory{};
    constexpr uint32_t FileListDirectory = 0x0001;
    constexpr uint32_t FileReadAttributes = 0x0080;
    constexpr uint32_t Synchronize = 0x00100000;
    constexpr uint32_t ShareReadWriteDelete = 0x0007;
    constexpr uint32_t FileOpen = 1;
    constexpr uint32_t FileDirectoryFile = 0x0001;
    constexpr uint32_t FileSynchronousIoNonAlert = 0x0020;
    const int32_t status = NtCreateFile(&newDirectory,
        FileListDirectory | FileReadAttributes | Synchronize, &attributes,
        &ioStatus, nullptr, 0, ShareReadWriteDelete, FileOpen,
        FileDirectoryFile | FileSynchronousIoNonAlert, nullptr, 0);
    if (status < 0) {
        SetLastError(Win32ErrorFromNtStatus(status));
        return 0;
    }

    auto* ownedPath = static_cast<wchar_t*>(malloc((length + 1) * sizeof(wchar_t)));
    if (!ownedPath) {
        (void)NtClose(reinterpret_cast<void*>(newDirectory));
        SetLastError(8);
        return 0;
    }
    for (size_t i = 0; i <= length; ++i) ownedPath[i] = fullPath[i];
    auto* previousPath = g_ownedCurrentDirectory;
    const auto previousHandle = parameters->CurrentDirectoryHandle;
    parameters->CurrentDirectoryDosPath.Length = static_cast<uint16_t>(length * sizeof(wchar_t));
    parameters->CurrentDirectoryDosPath.MaximumLength =
        static_cast<uint16_t>((length + 1) * sizeof(wchar_t));
    parameters->CurrentDirectoryDosPath.Buffer =
        reinterpret_cast<const char16_t*>(ownedPath);
    parameters->CurrentDirectoryHandle = static_cast<uintptr_t>(newDirectory);
    g_ownedCurrentDirectory = ownedPath;
    if (previousHandle) (void)NtClose(reinterpret_cast<void*>(previousHandle));
    if (previousPath) free(previousPath);
    return 1;
}

namespace {
volatile uint32_t g_environmentLock{};
char16_t* g_ownedEnvironment{};

void LockEnvironment() noexcept {
    while (__atomic_exchange_n(&g_environmentLock, 1, __ATOMIC_ACQUIRE))
        __asm__ __volatile__("pause");
}
void UnlockEnvironment() noexcept {
    __atomic_store_n(&g_environmentLock, 0, __ATOMIC_RELEASE);
}
struct EnvironmentGuard {
    EnvironmentGuard() noexcept { LockEnvironment(); }
    ~EnvironmentGuard() { UnlockEnvironment(); }
};

struct EnvironmentSnapshot {
    wchar_t* block{};
};
EnvironmentSnapshot g_environmentSnapshots[16]{};
volatile uint32_t g_environmentSnapshotLock{};
void LockEnvironmentSnapshots() noexcept {
    while (__atomic_exchange_n(&g_environmentSnapshotLock, 1, __ATOMIC_ACQUIRE))
        __asm__ __volatile__("pause");
}
void UnlockEnvironmentSnapshots() noexcept {
    __atomic_store_n(&g_environmentSnapshotLock, 0, __ATOMIC_RELEASE);
}
}

__declspec(dllexport) wchar_t* __stdcall GetEnvironmentStringsW() noexcept {
    EnvironmentGuard environmentGuard;
    char16_t* source{};
    if (!micant::shared::native_process::currentEnvironment(source)) {
        SetLastError(87);
        return nullptr;
    }
    size_t characters = 0;
    // A valid environment block is bounded by the maximum process-parameter
    // payload supported by Ps. Require its terminating empty string.
    constexpr size_t maximumCharacters = 32768;
    while (characters + 1 < maximumCharacters &&
           (source[characters] || source[characters + 1])) ++characters;
    if (characters + 1 >= maximumCharacters) {
        SetLastError(87);
        return nullptr;
    }
    auto* copy = static_cast<wchar_t*>(malloc((characters + 2) * sizeof(wchar_t)));
    if (!copy) { SetLastError(8); return nullptr; }
    for (size_t i = 0; i <= characters + 1; ++i)
        copy[i] = static_cast<wchar_t>(source[i]);
    LockEnvironmentSnapshots();
    size_t slot = 0;
    while (slot < 16 && g_environmentSnapshots[slot].block) ++slot;
    if (slot < 16) g_environmentSnapshots[slot].block = copy;
    UnlockEnvironmentSnapshots();
    if (slot == 16) {
        free(copy);
        SetLastError(8);
        return nullptr;
    }
    return copy;
}

__declspec(dllexport) int32_t __stdcall FreeEnvironmentStringsW(wchar_t* block) noexcept {
    if (!block) { SetLastError(87); return 0; }
    LockEnvironmentSnapshots();
    size_t slot = 0;
    while (slot < 16 && g_environmentSnapshots[slot].block != block) ++slot;
    if (slot < 16) g_environmentSnapshots[slot].block = nullptr;
    UnlockEnvironmentSnapshots();
    if (slot == 16) { SetLastError(87); return 0; }
    free(block);
    return 1;
}

__declspec(dllexport) void __stdcall SetLastError(uint32_t error) noexcept;

namespace {
uint32_t Win32ErrorFromNtStatus(int32_t status) noexcept {
    switch (static_cast<uint32_t>(status)) {
        case 0xC0000008u: return 6;   // ERROR_INVALID_HANDLE
        case 0xC000000Du: return 87;  // ERROR_INVALID_PARAMETER
        case 0xC0000022u: return 5;   // ERROR_ACCESS_DENIED
        case 0xC0000034u: return 2;   // ERROR_FILE_NOT_FOUND
        case 0xC000003Au: return 3;   // ERROR_PATH_NOT_FOUND
        case 0xC0000035u: return 183; // ERROR_ALREADY_EXISTS
        case 0xC000009Au: return 8;   // ERROR_NOT_ENOUGH_MEMORY
        case 0xC00000A3u: return 21;  // ERROR_NOT_READY
        case 0xC00000BBu: return 50;  // ERROR_NOT_SUPPORTED
        default: return 31;           // ERROR_GEN_FAILURE
    }
}
}

__declspec(dllexport) void* __stdcall CreateFileW(
    const wchar_t* fileName, uint32_t desiredAccess, uint32_t shareMode,
    void* securityAttributes, uint32_t creationDisposition,
    uint32_t flagsAndAttributes, void* templateFile) noexcept {
    (void)securityAttributes;
    (void)templateFile;
    // The current native syscall supports FILE_OPEN and FILE_OPEN_IF. File
    // attributes are meaningful only on creation; normal existing-file reads
    // accept FILE_ATTRIBUTE_NORMAL but pass zero creation attributes.
    if (!fileName || (flagsAndAttributes & ~0x80u)) {
        SetLastError(87);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    if (creationDisposition != 3 && creationDisposition != 4) {
        SetLastError(50); // The filesystem syscall currently opens existing files only.
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    wchar_t resolvedPath[32767]{};
    const size_t inputLength = wcslen(fileName);
    if (!inputLength || inputLength >= 32767) {
        SetLastError(206);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    const bool driveAbsolute = inputLength >= 3 && fileName[1] == L':' &&
        IsPathSeparator(fileName[2]);
    const bool uncAbsolute = inputLength >= 2 && IsPathSeparator(fileName[0]) &&
        IsPathSeparator(fileName[1]);
    size_t resolvedLength{};
    if (driveAbsolute || uncAbsolute) {
        for (; resolvedLength < inputLength; ++resolvedLength)
            resolvedPath[resolvedLength] = IsPathSeparator(fileName[resolvedLength])
                ? L'\\' : fileName[resolvedLength];
    } else {
        CurrentDirectoryGuard guard;
        auto* parameters = CurrentProcessParameters();
        if (!parameters) {
            SetLastError(87);
            return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        }
        wchar_t currentPath[32767]{};
        size_t currentLength{};
        if (!CopyCurrentDirectoryPath(parameters, currentPath,
                                      std::size(currentPath), currentLength)) {
            SetLastError(87);
            return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        }
        resolvedLength = currentLength;
        if (resolvedLength < 2 || resolvedLength + inputLength + 2 >= 32767) {
            SetLastError(206);
            return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        }
        for (size_t i = 0; i < resolvedLength; ++i)
            resolvedPath[i] = currentPath[i];
        if (IsPathSeparator(fileName[0])) {
            resolvedLength = 2;
            resolvedPath[0] = currentPath[0];
            resolvedPath[1] = L':';
        } else if (resolvedLength && !IsPathSeparator(resolvedPath[resolvedLength - 1])) {
            resolvedPath[resolvedLength++] = L'\\';
        }
        const size_t inputStart = fileName[1] == L':' ? 2u : 0u;
        for (size_t i = inputStart; i < inputLength; ++i)
            resolvedPath[resolvedLength++] = IsPathSeparator(fileName[i])
                ? L'\\' : fileName[i];
    }
    resolvedPath[resolvedLength] = L'\0';
    micant::UnicodeString name(resolvedPath);
    micant::ObjectAttributes attributes{};
    attributes.objectName = &name;
    attributes.attributes = 0x40; // OBJ_CASE_INSENSITIVE
    micant::IoStatusBlock ioStatus{};
    micant::Handle handle{};
    const uint32_t disposition = creationDisposition == 3 ? 1u : 3u;
    const int32_t status = NtCreateFile(&handle, desiredAccess, &attributes,
        &ioStatus, nullptr, 0, shareMode,
        disposition, 0, nullptr, 0);
    if (status != 0) {
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD)
        PaintDebugTrace(211u, static_cast<uint32_t>(status), creationDisposition);
#endif
        SetLastError(Win32ErrorFromNtStatus(status));
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    return reinterpret_cast<void*>(handle);
}

struct Kernel32BeepParameters { uint32_t frequency; uint32_t duration; };
static_assert(sizeof(Kernel32BeepParameters) == 8);

static int32_t Kernel32_BeepNative(uint32_t frequency, uint32_t duration) noexcept {
    if (frequency < 37 || frequency > 32767) {
        SetLastError(87); // ERROR_INVALID_PARAMETER
        return 0;
    }
    micant::UnicodeString deviceName(L"\\Device\\Beep");
    micant::ObjectAttributes attributes{};
    attributes.objectName = &deviceName;
    attributes.attributes = micant::OBJ_CASE_INSENSITIVE;
    micant::IoStatusBlock ioStatus{};
    micant::Handle handle{};
    constexpr uint32_t FileOpen = 1;
    constexpr uint32_t FileSynchronousIoNonAlert = 0x20;
    constexpr uint32_t WriteData = 0x0002;
    constexpr uint32_t Synchronize = 0x00100000;
    const int32_t openStatus = NtCreateFile(&handle, WriteData | Synchronize,
        &attributes, &ioStatus, nullptr, 0, 0, FileOpen,
        FileSynchronousIoNonAlert, nullptr, 0);
    if (openStatus < 0) {
        SetLastError(Win32ErrorFromNtStatus(openStatus));
        return 0;
    }
    const Kernel32BeepParameters parameters{frequency, duration};
    const int32_t status = NtDeviceIoControlFile(handle, 0, nullptr, nullptr,
        &ioStatus, 0x00010000u, &parameters, sizeof(parameters), nullptr, 0);
    (void)NtClose(reinterpret_cast<void*>(handle));
    if (status < 0) {
        SetLastError(Win32ErrorFromNtStatus(status));
        return 0;
    }
    return 1;
}

struct Kernel32StartupInfoW {
    uint32_t cb{};
    wchar_t* reserved{};
    wchar_t* desktop{};
    wchar_t* title{};
    uint32_t x{}, y{}, xSize{}, ySize{}, xCountChars{}, yCountChars{}, fillAttribute{}, flags{};
    uint16_t showWindow{}, reserved2Size{};
    uint8_t* reserved2{};
    void* standardInput{};
    void* standardOutput{};
    void* standardError{};
};
struct Kernel32ProcessInformation {
    void* process{};
    void* thread{};
    uint32_t processId{};
    uint32_t threadId{};
};
static_assert(sizeof(Kernel32ProcessInformation) == 24);

static bool CopyWideString(const wchar_t* input, wchar_t* output, size_t capacity,
                           size_t& length) noexcept {
    length = 0;
    if (!input || !output || capacity == 0) return false;
    while (length < capacity - 1 && input[length]) {
        output[length] = input[length];
        ++length;
    }
    if (input[length] != L'\0') return false;
    output[length] = L'\0';
    return true;
}

static bool IsAbsoluteDosPath(const wchar_t* path, size_t length) noexcept {
    return length >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
        (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/');
}

// The current native Ps path accepts a drive-absolute image path and a
// bounded set of process parameters. Unsupported Win32 options are rejected
// before creating any kernel objects.
__declspec(dllexport) int32_t __stdcall CreateProcessW(
    const wchar_t* applicationName, wchar_t* commandLine,
    void* processSecurityAttributes, void* threadSecurityAttributes,
    int32_t inheritHandles, uint32_t creationFlags, void* environment,
    const wchar_t* currentDirectory, Kernel32StartupInfoW* startupInfo,
    Kernel32ProcessInformation* processInformation) noexcept {
    constexpr uint32_t CREATE_SUSPENDED = 0x00000004u;
    constexpr uint32_t SUPPORTED_FLAGS = CREATE_SUSPENDED;
    if (!processInformation || (creationFlags & ~SUPPORTED_FLAGS) || environment ||
        inheritHandles || (startupInfo && (startupInfo->cb != sizeof(Kernel32StartupInfoW) ||
        startupInfo->flags || startupInfo->reserved || startupInfo->reserved2Size ||
        startupInfo->reserved2 || startupInfo->standardInput || startupInfo->standardOutput ||
        startupInfo->standardError || startupInfo->desktop || startupInfo->title))) {
        SetLastError(50);
        return 0;
    }
    RtlZeroMemory(processInformation, sizeof(*processInformation));
    wchar_t imagePath[2049];
    size_t imageLength{};
    if (applicationName && *applicationName) {
        if (!CopyWideString(applicationName, imagePath, 2049, imageLength)) {
            SetLastError(206); return 0;
        }
    } else if (commandLine && *commandLine) {
        size_t i = 0;
        while (commandLine[i] == L' ' || commandLine[i] == L'\t') ++i;
        const bool quoted = commandLine[i] == L'"';
        if (quoted) ++i;
        const size_t begin = i;
        while (commandLine[i] && (quoted ? commandLine[i] != L'"' :
               (commandLine[i] != L' ' && commandLine[i] != L'\t'))) ++i;
        imageLength = i - begin;
        if (!imageLength || imageLength >= 2049) { SetLastError(87); return 0; }
        for (size_t n = 0; n < imageLength; ++n) imagePath[n] = commandLine[begin + n];
        imagePath[imageLength] = L'\0';
    } else {
        SetLastError(87); return 0;
    }

    wchar_t directory[2049];
    size_t directoryLength{};
    if (currentDirectory && *currentDirectory) {
        if (!CopyWideString(currentDirectory, directory, 2049, directoryLength)) {
            SetLastError(206); return 0;
        }
    } else {
        micant::shared::native_process::UnicodeString64 current{};
        if (micant::shared::native_process::currentDirectory(current) &&
            current.Length / sizeof(wchar_t) < 2049) {
            directoryLength = current.Length / sizeof(wchar_t);
            for (size_t i = 0; i < directoryLength; ++i) directory[i] = current.Buffer[i];
            directory[directoryLength] = L'\0';
        }
    }
    if (directoryLength && !IsAbsoluteDosPath(directory, directoryLength)) {
        SetLastError(87); return 0;
    }
    if (!IsAbsoluteDosPath(imagePath, imageLength)) {
        if (!directoryLength || imageLength + directoryLength + 2 >= 2049) {
            SetLastError(87); return 0;
        }
        wchar_t relative[2049];
        for (size_t i = 0; i < imageLength; ++i) relative[i] = imagePath[i];
        size_t joined = directoryLength;
        if (directory[joined - 1] != L'\\' && directory[joined - 1] != L'/') directory[joined++] = L'\\';
        for (size_t i = 0; i < imageLength; ++i) directory[joined + i] = relative[i];
        imageLength += joined;
        for (size_t i = 0; i < imageLength; ++i) imagePath[i] = directory[i];
        imagePath[imageLength] = L'\0';
    }

    wchar_t commandStorage[4097];
    size_t commandLength{};
    if (commandLine && *commandLine) {
        if (!CopyWideString(commandLine, commandStorage, 4097, commandLength)) {
            SetLastError(206); return 0;
        }
    } else {
        if (imageLength >= 4097) { SetLastError(206); return 0; }
        for (; commandLength < imageLength; ++commandLength) commandStorage[commandLength] = imagePath[commandLength];
        commandStorage[commandLength] = L'\0';
    }

    auto makeUnicode = [](micant::shared::native_process::UnicodeString64& value,
                          const wchar_t* text, size_t length) noexcept {
        value.Length = static_cast<uint16_t>(length * sizeof(wchar_t));
        value.MaximumLength = static_cast<uint16_t>((length + 1) * sizeof(wchar_t));
        value.Buffer = reinterpret_cast<const char16_t*>(text);
    };
    micant::shared::native_process::ProcessParametersPrefix64 parameters;
    RtlZeroMemory(&parameters, sizeof(parameters));
    parameters.MaximumLength = sizeof(parameters);
    parameters.Length = sizeof(parameters);
    parameters.Flags = 1;
    makeUnicode(parameters.ImagePathName, imagePath, imageLength);
    makeUnicode(parameters.CommandLine, commandStorage, commandLength);
    if (directoryLength) makeUnicode(parameters.CurrentDirectoryDosPath, directory, directoryLength);

    auto makeAttributes = [](void* security, micant::ObjectAttributes& attributes) noexcept {
        attributes = {};
        if (!security) return true;
        struct SecurityAttributesPrefix { uint32_t length; void* descriptor; int32_t inherit; };
        const auto* value = static_cast<const SecurityAttributesPrefix*>(security);
        if (value->length != sizeof(SecurityAttributesPrefix) || value->descriptor) return false;
        attributes.length = sizeof(attributes);
        attributes.attributes = value->inherit ? micant::OBJ_INHERIT : 0;
        return true;
    };
    micant::ObjectAttributes processAttributes{}, threadAttributes{};
    if (!makeAttributes(processSecurityAttributes, processAttributes) ||
        !makeAttributes(threadSecurityAttributes, threadAttributes)) {
        SetLastError(50); return 0;
    }
    micant::shared::native_process::ProcessCreateInfo64 createInfo;
    RtlZeroMemory(&createInfo, sizeof(createInfo));
    createInfo.Size = sizeof(createInfo);
    createInfo.State = micant::shared::native_process::CreateState::Initial;
    micant::Handle process{}, thread{};
    const int32_t status = NtCreateUserProcess(&process, &thread,
        0x001F0FFFu, 0x001F03FFu,
        processSecurityAttributes ? &processAttributes : nullptr,
        threadSecurityAttributes ? &threadAttributes : nullptr,
        0, (creationFlags & CREATE_SUSPENDED) ? 1u : 0u, &parameters,
        &createInfo, nullptr);
    if (status != 0) {
        if (createInfo.State == micant::shared::native_process::CreateState::FailOnSectionCreate &&
            createInfo.Data.FailSection.FileHandle)
            (void)NtClose(reinterpret_cast<void*>(createInfo.Data.FailSection.FileHandle));
        SetLastError(Win32ErrorFromNtStatus(status));
        return 0;
    }
    const int32_t closeSection = NtClose(reinterpret_cast<void*>(createInfo.Data.Success.SectionHandle));
    const int32_t closeImage = NtClose(reinterpret_cast<void*>(createInfo.Data.Success.FileHandle));
    if (closeSection != 0 || closeImage != 0) {
        (void)NtTerminateProcess(reinterpret_cast<void*>(process), closeSection != 0 ? closeSection : closeImage);
        (void)NtClose(reinterpret_cast<void*>(thread));
        (void)NtClose(reinterpret_cast<void*>(process));
        SetLastError(Win32ErrorFromNtStatus(closeSection != 0 ? closeSection : closeImage));
        return 0;
    }
    micant::ProcessBasicInformation processBasic{};
    micant::ThreadBasicInformation threadBasic{};
    const int32_t queryProcess = NtQueryInformationProcess(reinterpret_cast<void*>(process), 0,
        &processBasic, sizeof(processBasic), nullptr);
    const int32_t queryThread = queryProcess == 0 ? NtQueryInformationThread(
        reinterpret_cast<void*>(thread), 0, &threadBasic, sizeof(threadBasic), nullptr) : queryProcess;
    if (queryThread != 0) {
        (void)NtTerminateProcess(reinterpret_cast<void*>(process), queryThread);
        (void)NtClose(reinterpret_cast<void*>(thread));
        (void)NtClose(reinterpret_cast<void*>(process));
        SetLastError(Win32ErrorFromNtStatus(queryThread));
        return 0;
    }
    processInformation->process = reinterpret_cast<void*>(process);
    processInformation->thread = reinterpret_cast<void*>(thread);
    processInformation->processId = static_cast<uint32_t>(processBasic.uniqueProcessId);
    processInformation->threadId = static_cast<uint32_t>(threadBasic.clientId.uniqueThread);
    return 1;
}

// Keep ExitProcess available to native PE imports through the production
// Kernel32 DLL. Terminating the current process is a native process service;
// callers must not fall through into application code if it unexpectedly
// returns.
__declspec(dllexport) [[noreturn]] void __stdcall ExitProcess(uint32_t exitCode) noexcept {
    (void)NtTerminateProcess(reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
                             static_cast<int32_t>(exitCode));
    constexpr int64_t retryDelay100ns = -1'000'000;
    for (;;) (void)NtDelayExecution(0, &retryDelay100ns);
}

__declspec(dllexport) int32_t __stdcall ReadFile(
    void* file, void* buffer, uint32_t bytesToRead, uint32_t* bytesRead,
    void* overlapped) noexcept {
    if (bytesRead) *bytesRead = 0;
    if (!file || file == reinterpret_cast<void*>(static_cast<intptr_t>(-1)) ||
        (!buffer && bytesToRead) || overlapped) {
        SetLastError(87);
        return 0;
    }
    micant::IoStatusBlock ioStatus{};
    const int32_t status = NtReadFile(reinterpret_cast<micant::Handle>(file),
        0, nullptr, nullptr, &ioStatus, buffer, bytesToRead, nullptr, nullptr);
    if (status != 0) {
        SetLastError(Win32ErrorFromNtStatus(status));
        return 0;
    }
    if (bytesRead) *bytesRead = static_cast<uint32_t>(ioStatus.information);
    return 1;
}

// FlushFileBuffers maps to the native file-object flush contract. NT failures
// remain visible through GetLastError rather than becoming synthetic success.
__declspec(dllexport) int32_t __stdcall FlushFileBuffers(void* file) noexcept {
    if (!file || file == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return 0;
    }
    micant::IoStatusBlock ioStatus{};
    const int32_t status = NtFlushBuffersFile(
        reinterpret_cast<micant::Handle>(file), &ioStatus);
    if (status != 0) {
        SetLastError(Win32ErrorFromNtStatus(status));
        return 0;
    }
    return 1;
}

__declspec(dllexport) int32_t __stdcall GetFileSizeEx(
    void* file, micant::LargeInteger* fileSize) noexcept {
    if (!file || file == reinterpret_cast<void*>(static_cast<intptr_t>(-1)) || !fileSize) {
        SetLastError(87);
        return 0;
    }
    micant::IoStatusBlock ioStatus{};
    micant::FileStandardInformation information{};
    const int32_t status = NtQueryInformationFile(
        reinterpret_cast<micant::Handle>(file), &ioStatus, &information,
        sizeof(information), micant::FileInformationClass::FileStandardInformation);
    if (status != 0) {
        SetLastError(Win32ErrorFromNtStatus(status));
        return 0;
    }
    *fileSize = information.endOfFile;
    return 1;
}

__declspec(dllexport) int32_t __stdcall CloseHandle(void* handle) noexcept {
    const int32_t status = NtClose(handle);
    if (status == 0) return 1;
    uint32_t error = 31; // ERROR_GEN_FAILURE for statuses without a specific mapping here.
    if (static_cast<uint32_t>(status) == 0xC0000008u) error = 6; // ERROR_INVALID_HANDLE
    else if (static_cast<uint32_t>(status) == 0xC0000022u) error = 5; // ERROR_ACCESS_DENIED
    else if (static_cast<uint32_t>(status) == 0xC000000Du) error = 87; // ERROR_INVALID_PARAMETER
    SetLastError(error);
    return 0;
}

__declspec(dllexport) uint32_t __stdcall WaitForSingleObjectEx(
    void* handle, uint32_t milliseconds, int32_t alertable) noexcept {
    if (!handle) { SetLastError(6); return 0xFFFFFFFFu; }
    int64_t timeout{};
    const int64_t* timeoutPointer = nullptr;
    if (milliseconds != 0xFFFFFFFFu) {
        timeout = -static_cast<int64_t>(milliseconds) * 10000;
        timeoutPointer = &timeout;
    }
    const int32_t status = NtWaitForSingleObject(handle, alertable ? 1 : 0, timeoutPointer);
    if (status == 0) return 0;
    if (static_cast<uint32_t>(status) == 0x00000102u) return 0x00000102u;
    uint32_t error = 31;
    if (static_cast<uint32_t>(status) == 0xC0000008u) error = 6;
    else if (static_cast<uint32_t>(status) == 0xC0000022u) error = 5;
    else if (static_cast<uint32_t>(status) == 0xC000000Du) error = 87;
    SetLastError(error);
    return 0xFFFFFFFFu;
}

__declspec(dllexport) uint32_t __stdcall WaitForSingleObject(
    void* handle, uint32_t milliseconds) noexcept {
    return WaitForSingleObjectEx(handle, milliseconds, 0);
}

__declspec(dllexport) int32_t __stdcall GetExitCodeProcess(
    void* process, uint32_t* exitCode) noexcept {
    if (!process || !exitCode) { SetLastError(87); return 0; }
    micant::ProcessBasicInformation information{};
    uint32_t returned{};
    const int32_t status = NtQueryInformationProcess(process, 0, &information,
        sizeof(information), &returned);
    if (status != 0) {
        uint32_t error = 31;
        if (static_cast<uint32_t>(status) == 0xC0000008u) error = 6;
        else if (static_cast<uint32_t>(status) == 0xC0000022u) error = 5;
        else if (static_cast<uint32_t>(status) == 0xC000000Du) error = 87;
        SetLastError(error);
        return 0;
    }
    *exitCode = static_cast<uint32_t>(information.exitStatus);
    return 1;
}

__declspec(dllexport) uint32_t __stdcall GetLastError() noexcept {
    uint32_t error = 0;
    __asm__ __volatile__("movl %%gs:0x68, %0" : "=r"(error));
    return error;
}

__declspec(dllexport) void __stdcall SetLastError(uint32_t error) noexcept {
    __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
}

__declspec(dllexport) void __stdcall RaiseException(uint32_t code, uint32_t flags,
    uint32_t argumentCount, const uintptr_t* arguments) noexcept {
    struct ExceptionRecord64 {
        uint32_t code, flags;
        ExceptionRecord64* nested;
        void* address;
        uint32_t parameterCount, alignment;
        uintptr_t information[15];
    } record{};
    if (argumentCount > 15 || (argumentCount && !arguments)) {
        SetLastError(87);
        return;
    }
    record.code = code;
    record.flags = flags;
    record.address = __builtin_return_address(0);
    record.parameterCount = argumentCount;
    for (uint32_t i = 0; i < argumentCount; ++i) record.information[i] = arguments[i];
    RtlRaiseException(&record);
}

namespace {
struct Kernel32HeapBlock { void* base{}; size_t requested{}; size_t committed{}; };
Kernel32HeapBlock g_heapBlocks[2048]{};
volatile uint32_t g_heapLock{};
alignas(16) uint64_t g_processHeapMarker{};
constexpr uint32_t HeapZeroMemory = 0x08;

void LockHeapBlocks() noexcept {
    while (__atomic_exchange_n(&g_heapLock, 1, __ATOMIC_ACQUIRE)) __asm__ __volatile__("pause");
}
void UnlockHeapBlocks() noexcept { __atomic_store_n(&g_heapLock, 0, __ATOMIC_RELEASE); }
void SetHeapError(uint32_t error) noexcept { SetLastError(error); }

void* AllocateHeapBlock(size_t size, bool zero) noexcept {
    void* base = malloc(size ? size : 1);
    if (!base) { SetHeapError(8); return nullptr; }
    LockHeapBlocks();
    size_t slot = 0;
    while (slot < 2048 && g_heapBlocks[slot].base) ++slot;
    if (slot < 2048) g_heapBlocks[slot] = {base, size ? size : 1, size ? size : 1};
    UnlockHeapBlocks();
    if (slot == 2048) {
        free(base);
        SetHeapError(8); return nullptr;
    }
    if (zero) RtlZeroMemory(base, size ? size : 1);
    return base;
}

bool QueryHeapBlock(void* block, size_t& requested) noexcept {
    bool found = false;
    LockHeapBlocks();
    for (const auto& item : g_heapBlocks) if (item.base == block) {
        requested = item.requested; found = true; break;
    }
    UnlockHeapBlocks();
    return found;
}

bool FreeHeapBlock(void* block) noexcept {
    if (!block) return true;
    size_t slot = 2048;
    LockHeapBlocks();
    for (size_t i = 0; i < 2048; ++i) if (g_heapBlocks[i].base == block) {
        slot = i; break;
    }
    UnlockHeapBlocks();
    if (slot == 2048) { SetHeapError(87); return false; }
    LockHeapBlocks();
    if (g_heapBlocks[slot].base != block) { UnlockHeapBlocks(); SetHeapError(87); return false; }
    g_heapBlocks[slot] = {};
    UnlockHeapBlocks();
    free(block);
    return true;
}
}

__declspec(dllexport) void* __stdcall GetProcessHeap() noexcept {
    return &g_processHeapMarker;
}
__declspec(dllexport) void* __stdcall HeapAlloc(void* heap, uint32_t flags, size_t size) noexcept {
    if (heap != GetProcessHeap() || (flags & ~HeapZeroMemory)) { SetHeapError(87); return nullptr; }
    return AllocateHeapBlock(size, (flags & HeapZeroMemory) != 0);
}
__declspec(dllexport) int32_t __stdcall HeapFree(void* heap, uint32_t flags, void* block) noexcept {
    if (heap != GetProcessHeap() || flags) { SetHeapError(87); return 0; }
    return FreeHeapBlock(block) ? 1 : 0;
}
__declspec(dllexport) void* __stdcall HeapReAlloc(void* heap, uint32_t flags,
    void* block, size_t size) noexcept {
    if (heap != GetProcessHeap() || !block || (flags & ~HeapZeroMemory)) {
        SetHeapError(87); return nullptr;
    }
    size_t oldSize{};
    if (!QueryHeapBlock(block, oldSize)) { SetHeapError(87); return nullptr; }
    if (!size) size = 1;
    auto* replacement = static_cast<uint8_t*>(AllocateHeapBlock(size, false));
    if (!replacement) return nullptr;
    const size_t copyBytes = oldSize < size ? oldSize : size;
    auto* source = static_cast<const uint8_t*>(block);
    for (size_t i = 0; i < copyBytes; ++i) replacement[i] = source[i];
    if ((flags & HeapZeroMemory) && size > copyBytes)
        RtlZeroMemory(replacement + copyBytes, size - copyBytes);
    if (!FreeHeapBlock(block)) { (void)FreeHeapBlock(replacement); return nullptr; }
    return replacement;
}
__declspec(dllexport) void* __stdcall LocalAlloc(uint32_t flags, size_t size) noexcept {
    if (flags & ~(0x0040u | 0x0002u)) { SetHeapError(87); return nullptr; }
    if (flags & 0x0002u) { SetHeapError(120); return nullptr; }
    return AllocateHeapBlock(size, (flags & 0x0040u) != 0);
}
__declspec(dllexport) void* __stdcall LocalReAlloc(void* block, size_t size, uint32_t flags) noexcept {
    if (flags & ~(0x0040u | 0x0002u | 0x0080u)) { SetHeapError(87); return nullptr; }
    if (flags & (0x0002u | 0x0080u)) { SetHeapError(120); return nullptr; }
    if (!block) return LocalAlloc(flags, size);
    size_t oldSize{};
    if (!QueryHeapBlock(block, oldSize)) { SetHeapError(87); return nullptr; }
    if (!size) size = 1;
    auto* replacement = static_cast<uint8_t*>(AllocateHeapBlock(size, false));
    if (!replacement) return nullptr;
    const size_t copyBytes = oldSize < size ? oldSize : size;
    auto* source = static_cast<const uint8_t*>(block);
    for (size_t i = 0; i < copyBytes; ++i) replacement[i] = source[i];
    if ((flags & 0x0040u) && size > copyBytes) RtlZeroMemory(replacement + copyBytes, size - copyBytes);
    if (!FreeHeapBlock(block)) { (void)FreeHeapBlock(replacement); return nullptr; }
    return replacement;
}
__declspec(dllexport) void* __stdcall LocalFree(void* block) noexcept {
    return FreeHeapBlock(block) ? nullptr : block;
}
__declspec(dllexport) void* __stdcall LocalLock(void* block) noexcept {
    size_t ignoredSize{};
    if (!block || !QueryHeapBlock(block, ignoredSize)) { SetHeapError(87); return nullptr; }
    return block;
}
__declspec(dllexport) int32_t __stdcall LocalUnlock(void* block) noexcept {
    size_t ignoredSize{};
    if (!block || !QueryHeapBlock(block, ignoredSize)) { SetHeapError(87); return 0; }
    SetLastError(0);
    return 0;
}
struct Kernel32FilePositionInformation { int64_t currentByteOffset; };
__declspec(dllexport) int32_t __stdcall SetEndOfFile(void* file) noexcept {
    if (!file || file == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) {
        SetLastError(6); return 0;
    }
    micant::IoStatusBlock ioStatus{};
    Kernel32FilePositionInformation position{};
    int32_t status = NtQueryInformationFile(reinterpret_cast<micant::Handle>(file),
        &ioStatus, &position, sizeof(position),
        static_cast<micant::FileInformationClass>(14u));
    if (status >= 0) {
        status = NtSetInformationFile(reinterpret_cast<micant::Handle>(file),
            &ioStatus, &position, sizeof(position),
            static_cast<micant::FileInformationClass>(20u));
    }
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return 0; }
    return 1;
}

static void SetConversionError(micant::um::winbase::utf8::Error error) noexcept {
    uint32_t win32Error = 87;
    if (error == micant::um::winbase::utf8::Error::insufficient_buffer) win32Error = 122;
    if (error == micant::um::winbase::utf8::Error::no_unicode_translation) win32Error = 1113;
    SetLastError(win32Error);
}

__declspec(dllexport) int __stdcall MultiByteToWideChar(
    uint32_t codePage, uint32_t flags, const char* input, int inputCount,
    wchar_t* output, int outputCapacity) noexcept {
    if ((codePage != 0 && codePage != 65001) || (flags & ~0x8U) != 0 ||
        (output == nullptr && outputCapacity != 0) || inputCount == 0 ||
        inputCount < -1 || outputCapacity < 0) {
        SetConversionError(micant::um::winbase::utf8::Error::invalid_parameter);
        return 0;
    }
    static_assert(sizeof(wchar_t) == sizeof(uint16_t));
    const auto result = micant::um::winbase::utf8::to_utf16(
        input, inputCount, reinterpret_cast<uint16_t*>(output), outputCapacity,
        (flags & 0x8U) != 0);
    if (result.error != micant::um::winbase::utf8::Error::none) {
        SetConversionError(result.error);
        return 0;
    }
    return result.count;
}

__declspec(dllexport) int __stdcall WideCharToMultiByte(
    uint32_t codePage, uint32_t flags, const wchar_t* input, int inputCount,
    char* output, int outputCapacity, const char* defaultChar,
    int* usedDefaultChar) noexcept {
    if ((codePage != 0 && codePage != 65001) || (flags & ~0x80U) != 0 ||
        defaultChar != nullptr || usedDefaultChar != nullptr ||
        (output == nullptr && outputCapacity != 0) || inputCount == 0 ||
        inputCount < -1 || outputCapacity < 0) {
        SetConversionError(micant::um::winbase::utf8::Error::invalid_parameter);
        return 0;
    }
    static_assert(sizeof(wchar_t) == sizeof(uint16_t));
    const auto result = micant::um::winbase::utf8::to_utf8(
        reinterpret_cast<const uint16_t*>(input), inputCount, output, outputCapacity,
        (flags & 0x80U) != 0);
    if (result.error != micant::um::winbase::utf8::Error::none) {
        SetConversionError(result.error);
        return 0;
    }
    return result.count;
}

namespace {
wchar_t FoldEnvironmentChar(wchar_t ch) noexcept {
    return ch >= L'a' && ch <= L'z' ? ch - (L'a' - L'A') : ch;
}
bool FindEnvironmentValue(const wchar_t* name, const wchar_t*& value,
                          size_t& valueLength) noexcept {
    value = nullptr; valueLength = 0;
    if (!name || !*name) return false;
    char16_t* block{};
    if (!micant::shared::native_process::currentEnvironment(block)) return false;
    size_t offset = 0;
    while (offset < 32768 && block[offset]) {
        size_t end = offset;
        while (end < 32768 && block[end]) ++end;
        if (end == 32768) return false;
        size_t equals = offset;
        while (equals < end && block[equals] != u'=') ++equals;
        if (equals < end && equals - offset == 0) { offset = end + 1; continue; }
        size_t nameLength = 0;
        while (name[nameLength]) ++nameLength;
        if (equals < end && equals - offset == nameLength) {
            size_t i = 0;
            while (i < nameLength && FoldEnvironmentChar(static_cast<wchar_t>(block[offset + i])) ==
                   FoldEnvironmentChar(name[i])) ++i;
            if (i == nameLength) {
                value = reinterpret_cast<const wchar_t*>(block + equals + 1);
                valueLength = end - equals - 1;
                return true;
            }
        }
        offset = end + 1;
    }
    return false;
}
}

__declspec(dllexport) int32_t __stdcall SetEnvironmentVariableW(
    const wchar_t* name, const wchar_t* value) noexcept {
    bool hasEquals = false;
    if (name) for (const wchar_t* cursor = name; *cursor; ++cursor)
        if (*cursor == L'=') { hasEquals = true; break; }
    if (!name || !*name || hasEquals) {
        SetLastError(87);
        return 0;
    }
    EnvironmentGuard environmentGuard;
    auto* parameters = CurrentProcessParameters();
    char16_t* source{};
    if (!parameters || !micant::shared::native_process::currentEnvironment(source)) {
        SetLastError(87);
        return 0;
    }

    const size_t nameLength = wcslen(name);
    const size_t valueLength = value ? wcslen(value) : 0;
    if (nameLength > 32766 || valueLength > 32766) {
        SetLastError(206);
        return 0;
    }
    constexpr size_t MaximumEnvironmentCharacters = 32768;
    size_t required = 1;
    bool matched = false;
    size_t cursor = 0;
    while (cursor < MaximumEnvironmentCharacters && source[cursor]) {
        size_t end = cursor;
        while (end < MaximumEnvironmentCharacters && source[end]) ++end;
        if (end == MaximumEnvironmentCharacters) { SetLastError(87); return 0; }
        size_t equals = cursor;
        while (equals < end && source[equals] != u'=') ++equals;
        bool sameName = equals > cursor && equals - cursor == nameLength;
        for (size_t i = 0; sameName && i < nameLength; ++i)
            sameName = FoldEnvironmentChar(static_cast<wchar_t>(source[cursor + i])) ==
                FoldEnvironmentChar(name[i]);
        if (sameName) {
            matched = true;
        } else {
            const size_t entryCharacters = end - cursor + 1;
            if (required > MaximumEnvironmentCharacters - entryCharacters) {
                SetLastError(8);
                return 0;
            }
            required += entryCharacters;
        }
        cursor = end + 1;
    }
    if (cursor >= MaximumEnvironmentCharacters) { SetLastError(87); return 0; }
    const bool appendValue = value != nullptr;
    if (!appendValue && !matched)
        return 1;
    if (appendValue) {
        const size_t entryCharacters = nameLength + valueLength + 2;
        if (required > MaximumEnvironmentCharacters - entryCharacters) {
            SetLastError(8);
            return 0;
        }
        required += entryCharacters;
    }
    if (required == 1) required = 2;
    auto* replacement = static_cast<char16_t*>(malloc(required * sizeof(char16_t)));
    if (!replacement) { SetLastError(8); return 0; }

    size_t out = 0;
    cursor = 0;
    while (source[cursor]) {
        size_t end = cursor;
        while (source[end]) ++end;
        size_t equals = cursor;
        while (equals < end && source[equals] != u'=') ++equals;
        bool sameName = equals > cursor && equals - cursor == nameLength;
        for (size_t i = 0; sameName && i < nameLength; ++i)
            sameName = FoldEnvironmentChar(static_cast<wchar_t>(source[cursor + i])) ==
                FoldEnvironmentChar(name[i]);
        if (!sameName) {
            for (size_t i = cursor; i <= end; ++i) replacement[out++] = source[i];
        }
        cursor = end + 1;
    }
    if (appendValue) {
        for (size_t i = 0; i < nameLength; ++i)
            replacement[out++] = static_cast<char16_t>(name[i]);
        replacement[out++] = u'=';
        for (size_t i = 0; i < valueLength; ++i)
            replacement[out++] = static_cast<char16_t>(value[i]);
        replacement[out++] = u'\0';
    }
    replacement[out++] = u'\0';
    if (out == 1) replacement[out++] = u'\0';

    char16_t* previous = g_ownedEnvironment;
    parameters->Environment = replacement;
    g_ownedEnvironment = replacement;
    if (previous) free(previous);
    return 1;
}

__declspec(dllexport) uint32_t __stdcall GetEnvironmentVariableW(
    const wchar_t* name, wchar_t* buffer, uint32_t capacity) noexcept {
    EnvironmentGuard environmentGuard;
    if (!name || (!buffer && capacity)) { SetLastError(87); return 0; }
    const wchar_t* value{}; size_t length{};
    if (!FindEnvironmentValue(name, value, length)) { SetLastError(203); return 0; }
    if (!length) { if (buffer && capacity) buffer[0] = 0; return 0; }
    if (length == SIZE_MAX || length + 1 > UINT32_MAX) { SetLastError(122); return 0; }
    const uint32_t required = static_cast<uint32_t>(length + 1);
    if (!capacity || capacity < required) { SetLastError(122); return required; }
    for (size_t i = 0; i < length; ++i) buffer[i] = value[i];
    buffer[length] = 0;
    return static_cast<uint32_t>(length);
}

__declspec(dllexport) uint32_t __stdcall GetEnvironmentVariableA(
    const char* name, char* buffer, uint32_t capacity) noexcept {
    EnvironmentGuard environmentGuard;
    if (!name || (!buffer && capacity)) { SetLastError(87); return 0; }
    wchar_t wideName[32768]{};
    const int nameCount = MultiByteToWideChar(0, 0, name, -1, wideName,
        static_cast<int>(sizeof(wideName) / sizeof(wideName[0])));
    if (!nameCount) return 0;
    const wchar_t* value{}; size_t length{};
    if (!FindEnvironmentValue(wideName, value, length)) { SetLastError(203); return 0; }
    if (!length) { if (buffer && capacity) buffer[0] = 0; return 0; }
    if (length >= INT_MAX) { SetLastError(122); return 0; }
    const int required = WideCharToMultiByte(0, 0, value, static_cast<int>(length + 1),
        nullptr, 0, nullptr, nullptr);
    if (!required) return 0;
    if (!capacity || capacity < static_cast<uint32_t>(required)) {
        SetLastError(122); return static_cast<uint32_t>(required);
    }
    const int written = WideCharToMultiByte(0, 0, value, static_cast<int>(length + 1),
        buffer, static_cast<int>(capacity), nullptr, nullptr);
    return written ? static_cast<uint32_t>(written - 1) : 0;
}

namespace {
uintptr_t CurrentImageBaseForResource() noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (teb < 0x10000 || teb > UINTPTR_MAX - 0x68) return 0;
    const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    if (peb < 0x10000 || peb > UINTPTR_MAX - 0x18) return 0;
    return *reinterpret_cast<const uintptr_t*>(peb + 0x10);
}
struct ResourceDirectory { uint32_t flags, timestamp; uint16_t major, minor, named, ids; };
struct ResourceEntry { uint32_t name, offset; };
struct ResourceDataEntry { uint32_t rva, size, codePage, reserved; };
struct Kernel32ResourceHandle { void* data{}; uint32_t size{}; uintptr_t module{}; };
Kernel32ResourceHandle g_resourceHandles[128]{};
volatile uint32_t g_resourceLock{};

void LockResources() noexcept {
    while (__atomic_exchange_n(&g_resourceLock, 1, __ATOMIC_ACQUIRE)) __asm__ __volatile__("pause");
}
void UnlockResources() noexcept { __atomic_store_n(&g_resourceLock, 0, __ATOMIC_RELEASE); }
bool ResourceRange(uintptr_t base, uint32_t imageSize, uint32_t rva, size_t length) noexcept {
    return rva <= imageSize && length <= imageSize - rva &&
        base <= UINTPTR_MAX - rva && base + rva <= UINTPTR_MAX - length;
}
bool ResourceIdMatch(uintptr_t request, uint32_t encoded, uintptr_t base,
                     uint32_t imageSize) noexcept {
    if (request <= 0xFFFF) return (encoded & 0x80000000u) == 0 && (encoded & 0xFFFFu) == request;
    if (!(encoded & 0x80000000u)) return false;
    const uint32_t offset = encoded & 0x7FFFFFFFu;
    if (!ResourceRange(base, imageSize, offset, sizeof(uint16_t))) return false;
    const auto* name = reinterpret_cast<const uint16_t*>(base + offset);
    const uint16_t length = *name;
    if (!ResourceRange(base, imageSize, offset + sizeof(uint16_t),
                       static_cast<size_t>(length) * sizeof(wchar_t))) return false;
    const auto* text = reinterpret_cast<const wchar_t*>(name + 1);
    const auto* requested = reinterpret_cast<const wchar_t*>(request);
    size_t requestedLength = 0;
    while (requestedLength < 32768 && requested[requestedLength]) ++requestedLength;
    if (requestedLength != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (FoldEnvironmentChar(text[i]) != FoldEnvironmentChar(requested[i])) return false;
    return true;
}
bool FindResourceDirectoryEntry(uintptr_t base, uint32_t imageSize, uint32_t rootRva,
    uint32_t directoryOffset, uintptr_t id, bool wantDirectory,
    uint32_t& resultOffset) noexcept {
    if (directoryOffset > UINT32_MAX - rootRva) return false;
    const uint32_t directoryRva = rootRva + directoryOffset;
    if (!ResourceRange(base, imageSize, directoryRva, sizeof(ResourceDirectory))) return false;
    const auto* directory = reinterpret_cast<const ResourceDirectory*>(base + directoryRva);
    const size_t count = static_cast<size_t>(directory->named) + directory->ids;
    if (count > 4096 || !ResourceRange(base, imageSize,
            directoryRva + sizeof(ResourceDirectory), count * sizeof(ResourceEntry))) return false;
    const auto* entries = reinterpret_cast<const ResourceEntry*>(base + directoryRva + sizeof(ResourceDirectory));
    for (size_t i = 0; i < count; ++i) {
        if (!ResourceIdMatch(id, entries[i].name, base + rootRva, imageSize)) continue;
        const bool isDirectory = (entries[i].offset & 0x80000000u) != 0;
        if (isDirectory != wantDirectory) return false;
        resultOffset = entries[i].offset & 0x7FFFFFFFu;
        return true;
    }
    return false;
}
}

__declspec(dllexport) void* __stdcall FindResourceExW(void* module, const wchar_t* type,
    const wchar_t* name, uint16_t language) noexcept {
    if ((!type && !name) || !type || !name) { SetLastError(87); return nullptr; }
    const uintptr_t base = module ? reinterpret_cast<uintptr_t>(module) : CurrentImageBaseForResource();
    if (!base || base > UINTPTR_MAX - 0x1000 || *reinterpret_cast<const uint16_t*>(base) != 0x5A4D) {
        SetLastError(126); return nullptr;
    }
    const uint32_t peOffset = *reinterpret_cast<const uint32_t*>(base + 0x3C);
    if (peOffset > 0x100000 || base > UINTPTR_MAX - peOffset - 0x108) { SetLastError(193); return nullptr; }
    const uintptr_t nt = base + peOffset;
    if (*reinterpret_cast<const uint32_t*>(nt) != 0x00004550u) { SetLastError(193); return nullptr; }
    const uint16_t optionalSize = *reinterpret_cast<const uint16_t*>(nt + 20);
    const uintptr_t optional = nt + 24;
    if (optionalSize < 136 || *reinterpret_cast<const uint16_t*>(optional) != 0x20B) {
        SetLastError(193); return nullptr;
    }
    const uint32_t imageSize = *reinterpret_cast<const uint32_t*>(optional + 56);
    const uint32_t resourceRva = *reinterpret_cast<const uint32_t*>(optional + 128);
    const uint32_t resourceSize = *reinterpret_cast<const uint32_t*>(optional + 132);
    if (!resourceRva || resourceSize < sizeof(ResourceDirectory) ||
        !ResourceRange(base, imageSize, resourceRva, resourceSize)) { SetLastError(1813); return nullptr; }
    uint32_t typeDirectory{}, nameDirectory{};
    if (!FindResourceDirectoryEntry(base, imageSize, resourceRva, 0,
            reinterpret_cast<uintptr_t>(type), true, typeDirectory) ||
        !FindResourceDirectoryEntry(base, imageSize, resourceRva, typeDirectory,
            reinterpret_cast<uintptr_t>(name), true, nameDirectory)) {
        SetLastError(1813); return nullptr;
    }
    uint32_t languageOffset{};
    if (!FindResourceDirectoryEntry(base, imageSize, resourceRva, nameDirectory,
            language, false, languageOffset)) {
        if (!FindResourceDirectoryEntry(base, imageSize, resourceRva, nameDirectory,
                0, false, languageOffset)) {
            const uint32_t directoryRva = resourceRva + nameDirectory;
            if (!ResourceRange(base, imageSize, directoryRva, sizeof(ResourceDirectory))) {
                SetLastError(1813); return nullptr;
            }
            const auto* directory = reinterpret_cast<const ResourceDirectory*>(base + directoryRva);
            if (!directory->named && !directory->ids) { SetLastError(1813); return nullptr; }
            const auto* entry = reinterpret_cast<const ResourceEntry*>(directory + 1);
            if (entry->offset & 0x80000000u) { SetLastError(1813); return nullptr; }
            languageOffset = entry->offset & 0x7FFFFFFFu;
        }
    }
    const uint32_t dataRva = resourceRva + languageOffset;
    if (!ResourceRange(base, imageSize, dataRva, sizeof(ResourceDataEntry))) { SetLastError(1813); return nullptr; }
    const auto* data = reinterpret_cast<const ResourceDataEntry*>(base + dataRva);
    if (!ResourceRange(base, imageSize, data->rva, data->size)) { SetLastError(1813); return nullptr; }
    LockResources();
    size_t slot = 0;
    while (slot < 128 && g_resourceHandles[slot].data) ++slot;
    if (slot < 128) g_resourceHandles[slot] = {reinterpret_cast<void*>(base + data->rva), data->size, base};
    UnlockResources();
    if (slot == 128) { SetLastError(8); return nullptr; }
    return &g_resourceHandles[slot];
}

__declspec(dllexport) void* __stdcall LoadResource(void* module, void* resource) noexcept {
    if (!resource) { SetLastError(87); return nullptr; }
    const uintptr_t base = module ? reinterpret_cast<uintptr_t>(module) : CurrentImageBaseForResource();
    LockResources();
    for (auto& entry : g_resourceHandles) if (&entry == resource && entry.data && entry.module == base) {
        void* result = &entry;
        UnlockResources();
        return result;
    }
    UnlockResources();
    SetLastError(1813); return nullptr;
}

__declspec(dllexport) void* __stdcall LockResource(void* resourceData) noexcept {
    if (!resourceData) return nullptr;
    LockResources();
    for (auto& entry : g_resourceHandles) if (&entry == resourceData && entry.data) {
        void* result = entry.data;
        UnlockResources();
        return result;
    }
    UnlockResources();
    SetLastError(1813); return nullptr;
}

__declspec(dllexport) uint32_t __stdcall SizeofResource(void* module,
    void* resource) noexcept {
    const uintptr_t base = module ? reinterpret_cast<uintptr_t>(module)
                                  : CurrentImageBaseForResource();
    LockResources();
    for (auto& entry : g_resourceHandles) {
        if (&entry == resource && entry.data && entry.module == base) {
            const uint32_t size = entry.size;
            UnlockResources();
            return size;
        }
    }
    UnlockResources();
    SetLastError(1813);
    return 0;
}

int __stdcall DllMainCRTStartup(void*, unsigned long, void*) noexcept {
    return 1;
}

__declspec(dllexport) void __stdcall GetSystemTime(Kernel32SystemTime* output) noexcept {
    if (!output) return;
    micant::shared::time::SystemTimeFields value{};
    if (!QueryUtcFields(value)) return;
    *output = {value.year, value.month, value.weekday, value.day, value.hour,
               value.minute, value.second, value.milliseconds};
}

// Convert UTC using the active bias shared by the kernel; explicit TZI rules
// can be supplied through SystemTimeToTzSpecificLocalTime.
__declspec(dllexport) void __stdcall GetLocalTime(Kernel32SystemTime* output) noexcept {
    if (!output) return;
    micant::shared::time::SystemTimeFields current{};
    if (!QueryUtcFields(current)) return;
    const Kernel32SystemTime utc{current.year, current.month, current.weekday,
        current.day, current.hour, current.minute, current.second,
        current.milliseconds};
    Kernel32TimeZoneInformation zone{};
    uint32_t zoneId{};
    if (!ReadSystemTimeZone(zone, zoneId)) return;
    Kernel32SystemTime local{};
    if (ApplyTimeZone(&zone, &utc, &local)) *output = local;
}

__declspec(dllexport) uint32_t __stdcall GetTimeZoneInformation(
    Kernel32TimeZoneInformation* zone) noexcept {
    if (!zone) return 0xFFFFFFFFu;
    uint32_t zoneId{};
    if (!ReadSystemTimeZone(*zone, zoneId)) return 0xFFFFFFFFu;
    return zoneId;
}

__declspec(dllexport) int32_t __stdcall SystemTimeToTzSpecificLocalTime(
    const Kernel32TimeZoneInformation* zone, const Kernel32SystemTime* utc,
    Kernel32SystemTime* local) noexcept {
    if (!utc || !local) return 0;
    Kernel32TimeZoneInformation configured{};
    uint32_t zoneId{};
    if (!zone) {
        if (!ReadSystemTimeZone(configured, zoneId)) return 0;
        zone = &configured;
    }
    return ApplyTimeZone(zone, utc, local) ? 1 : 0;
}

__declspec(dllexport) int32_t __stdcall TzSpecificLocalTimeToSystemTime(
    const Kernel32TimeZoneInformation* zone, const Kernel32SystemTime* local,
    Kernel32SystemTime* utc) noexcept {
    if (!local || !utc) return 0;
    Kernel32TimeZoneInformation configured{};
    uint32_t zoneId{};
    if (!zone) {
        if (!ReadSystemTimeZone(configured, zoneId)) return 0;
        zone = &configured;
    }
    return LocalTimeToUtc(zone, local, utc) ? 1 : 0;
}

__declspec(dllexport) void __stdcall GetSystemTimeAsFileTime(Kernel32FileTime* output) noexcept {
    if (!output) return;
    int64_t ticks{};
    if (NtQuerySystemTime(&ticks) < 0) return;
    const uint64_t value = static_cast<uint64_t>(ticks);
    output->low = static_cast<uint32_t>(value);
    output->high = static_cast<uint32_t>(value >> 32);
}

__declspec(dllexport) void __stdcall GetSystemTimePreciseAsFileTime(
    Kernel32FileTime* output) noexcept {
    GetSystemTimeAsFileTime(output);
}

__declspec(dllexport) int32_t __stdcall QueryPerformanceCounter(int64_t* value) noexcept {
    if (!value) return 0;
    return NtQueryPerformanceCounter(value, nullptr) >= 0 ? 1 : 0;
}

__declspec(dllexport) int32_t __stdcall QueryPerformanceFrequency(int64_t* value) noexcept {
    if (!value) return 0;
    int64_t counter{};
    return NtQueryPerformanceCounter(&counter, value) >= 0 ? 1 : 0;
}

__declspec(dllexport) uint64_t __stdcall GetTickCount64() noexcept {
    auto* shared = reinterpret_cast<volatile const uint64_t*>(0x7FFE0320ULL);
    return *shared;
}

__declspec(dllexport) uint32_t __stdcall GetTickCount() noexcept {
    return static_cast<uint32_t>(GetTickCount64());
}

using UnhandledExceptionFilterFn = long (__stdcall *)(void*);
static UnhandledExceptionFilterFn g_unhandledExceptionFilter{};
__declspec(dllexport) UnhandledExceptionFilterFn __stdcall SetUnhandledExceptionFilter(
    UnhandledExceptionFilterFn filter) noexcept {
    return __atomic_exchange_n(&g_unhandledExceptionFilter, filter, __ATOMIC_SEQ_CST);
}

struct UserListEntry64 { uintptr_t next, previous; };
struct UserUnicodeString64 { uint16_t length, maximumLength; uint32_t padding; const wchar_t* buffer; };
struct WinCriticalSection64 {
    void* debugInfo;
    int32_t lockCount;
    int32_t recursionCount;
    uintptr_t owningThread;
    void* lockSemaphore;
    uintptr_t spinCount;
};
static_assert(sizeof(WinCriticalSection64) == 40);

static uintptr_t CurrentPeb() noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (teb < 0x10000 || teb > 0x00007FFFFFFF0000ULL - 0x60) return 0;
    const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    return peb >= 0x10000 && peb < 0x00007FFFFFFF0000ULL ? peb : 0;
}

static uintptr_t FindModule(const wchar_t* name) noexcept {
    const uintptr_t peb = CurrentPeb();
    if (!peb) return 0;
    if (!name) return *reinterpret_cast<const uintptr_t*>(peb + 0x10);
    const uintptr_t loader = *reinterpret_cast<const uintptr_t*>(peb + 0x18);
    if (loader < 0x10000 || loader >= 0x00007FFFFFFF0000ULL - 0x28) return 0;
    const uintptr_t headAddress = loader + 0x20;
    const auto* head = reinterpret_cast<const UserListEntry64*>(headAddress);
    uintptr_t link = head->next;
    for (size_t count = 0; count < 32 && link != headAddress; ++count) {
        if (link < 0x10010 || link >= 0x00007FFFFFFF0000ULL) return 0;
        const uintptr_t entry = link - 0x10;
        const uintptr_t base = *reinterpret_cast<const uintptr_t*>(entry + 0x30);
        const auto& fullName = *reinterpret_cast<const UserUnicodeString64*>(entry + 0x48);
        if (base && fullName.buffer && !(fullName.length & 1)) {
            const size_t length = fullName.length / sizeof(wchar_t);
            size_t pathBase{};
            for (size_t i = 0; i < length; ++i)
                if (fullName.buffer[i] == L'\\' || fullName.buffer[i] == L'/') pathBase = i + 1;
            size_t nameBase{};
            for (size_t i = 0; name[i]; ++i)
                if (name[i] == L'\\' || name[i] == L'/') nameBase = i + 1;
            size_t i = pathBase, j = nameBase;
            for (; i < length && name[j]; ++i, ++j) {
                wchar_t a = fullName.buffer[i], b = name[j];
                if (a > 0x7f || b > 0x7f) break;
                if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
                if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
                if (a != b) break;
            }
            if (i == length && name[j] == 0) return base;
        }
        link = reinterpret_cast<const UserListEntry64*>(link)->next;
    }
    return 0;
}

static uintptr_t FindLoadedModuleEntry(uintptr_t moduleBase) noexcept {
    const uintptr_t peb = CurrentPeb();
    if (!peb || moduleBase < 0x10000) return 0;
    const uintptr_t loader = *reinterpret_cast<const uintptr_t*>(peb + 0x18);
    if (loader < 0x10000 || loader >= 0x00007FFFFFFF0000ULL - 0x28) return 0;
    const uintptr_t headAddress = loader + 0x20;
    const auto* head = reinterpret_cast<const UserListEntry64*>(headAddress);
    uintptr_t link = head->next;
    for (size_t count = 0; count < 32 && link != headAddress; ++count) {
        if (link < 0x10010 || link >= 0x00007FFFFFFF0000ULL) return 0;
        const uintptr_t entry = link - 0x10;
        if (*reinterpret_cast<const uintptr_t*>(entry + 0x30) == moduleBase) return entry;
        link = reinterpret_cast<const UserListEntry64*>(link)->next;
    }
    return 0;
}

__declspec(dllexport) void* __stdcall GetModuleHandleW(const wchar_t* name) noexcept {
    return reinterpret_cast<void*>(FindModule(name));
}

// The system UI language is currently configured as en-US. Resource callers
// still pass an explicit LANGID, so FindResourceExW can select other installed
// resource languages without hard-wiring applications to English.
__declspec(dllexport) uint16_t __stdcall GetUserDefaultUILanguage() noexcept {
    return 0x0409;
}
__declspec(dllexport) uint16_t __stdcall GetUserDefaultLangID() noexcept {
    return GetUserDefaultUILanguage();
}

__declspec(dllexport) int32_t __stdcall GetLocaleInfoW(
    uint32_t locale, uint32_t type, wchar_t* data, int32_t capacity) noexcept {
    constexpr uint32_t ReturnNumber = 0x20000000u;
    const bool returnNumber = (type & ReturnNumber) != 0;
    type &= ~ReturnNumber;
    if ((locale != 0 && locale != 0x0409 && locale != 0x0400 && locale != 0x0800 &&
         locale != 0x007f) || capacity < 0 || (capacity > 0 && !data)) {
        SetLastError(87);
        return 0;
    }
    uint32_t number{};
    const wchar_t* text{};
    switch (type) {
    case 0x00000002: case 0x00001001: text = L"English"; break; // language
    case 0x00000003: text = L"ENU"; break; // abbreviated language
    case 0x00000006: case 0x00001002: text = L"United States"; break;
    case 0x00000007: text = L"USA"; break;
    case 0x0000000e: case 0x00000016: text = L"."; break; // decimal separators
    case 0x0000000f: case 0x00000017: text = L","; break; // thousand separators
    case 0x00000010: case 0x00000018: text = L"3;0"; break; // grouping
    case 0x00000013: text = L"0123456789"; break;
    case 0x0000001d: text = L"/"; break;
    case 0x0000001e: text = L":"; break;
    case 0x0000001f: text = L"M/d/yyyy"; break;
    case 0x00000020: text = L"dddd, MMMM d, yyyy"; break;
    case 0x00000028: text = L"AM"; break;
    case 0x00000029: text = L"PM"; break;
    case 0x00001003: text = L"h:mm:ss tt"; break;
    case 0x00000011: number = 2; break; // number of fractional digits
    case 0x00000012: number = 1; break; // leading zero
    case 0x00000019: case 0x0000001a: number = 2; break; // currency digits
    case 0x0000001b: number = 0; break; // currency symbol before value
    case 0x0000001c: number = 0; break; // negative currency format
    case 0x00000059: text = L"en"; break; // ISO language
    case 0x0000005a: text = L"US"; break; // ISO country
    default: SetLastError(87); return 0;
    }
    if (returnNumber) {
        if (text || capacity < static_cast<int32_t>(sizeof(uint32_t) / sizeof(wchar_t))) {
            if (!capacity) return static_cast<int32_t>(sizeof(uint32_t) / sizeof(wchar_t));
            SetLastError(122); return 0;
        }
        *reinterpret_cast<uint32_t*>(data) = number;
        return static_cast<int32_t>(sizeof(uint32_t) / sizeof(wchar_t));
    }
    if (!text) {
        static wchar_t numeric[12];
        uint32_t value = number;
        size_t length{};
        do { numeric[length++] = static_cast<wchar_t>(L'0' + value % 10); value /= 10; }
        while (value && length < 10);
        for (size_t i = 0; i < length / 2; ++i) {
            const wchar_t digit = numeric[i]; numeric[i] = numeric[length - i - 1]; numeric[length - i - 1] = digit;
        }
        numeric[length] = 0;
        text = numeric;
    }
    size_t required{}; while (text[required]) ++required; ++required;
    if (!capacity) return static_cast<int32_t>(required);
    if (static_cast<size_t>(capacity) < required) { SetLastError(122); return 0; }
    for (size_t i = 0; i < required; ++i) data[i] = text[i];
    return static_cast<int32_t>(required);
}

__declspec(dllexport) uint32_t __stdcall GetProfileIntW(
    const wchar_t* section, const wchar_t* key, int32_t defaultValue) noexcept {
    if (!section || !*section || !key || !*key) { SetLastError(87); return static_cast<uint32_t>(defaultValue); }
    void* file = CreateFileW(L"C:\\Windows\\win.ini", 0x80000000u, 1u, nullptr, 3u, 0u, nullptr);
    if (file == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) return static_cast<uint32_t>(defaultValue);
    micant::LargeInteger fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.quadPart < 0 || fileSize.quadPart > 32768) {
        (void)CloseHandle(file); return static_cast<uint32_t>(defaultValue);
    }
    char contents[32769]{};
    uint32_t bytesRead{};
    if (!ReadFile(file, contents, static_cast<uint32_t>(fileSize.quadPart), &bytesRead, nullptr)) {
        (void)CloseHandle(file); return static_cast<uint32_t>(defaultValue);
    }
    (void)CloseHandle(file);
    auto equalAsciiInsensitive = [](const wchar_t* wide, const char* narrow, size_t length) noexcept {
        size_t i{};
        while (i < length && wide[i] && static_cast<unsigned char>(narrow[i])) {
            wchar_t a = wide[i]; unsigned char b = static_cast<unsigned char>(narrow[i]);
            if (a > 0x7f) return false;
            if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) return false;
            ++i;
        }
        return i == length && wide[i] == 0;
    };
    bool inSection{};
    size_t offset{};
    while (offset < bytesRead) {
        size_t end = offset;
        while (end < bytesRead && contents[end] != '\r' && contents[end] != '\n') ++end;
        size_t begin = offset;
        while (begin < end && (contents[begin] == ' ' || contents[begin] == '\t')) ++begin;
        size_t trim = end;
        while (trim > begin && (contents[trim - 1] == ' ' || contents[trim - 1] == '\t')) --trim;
        if (begin < trim && contents[begin] == '[' && contents[trim - 1] == ']') {
            inSection = equalAsciiInsensitive(section, contents + begin + 1, trim - begin - 2);
        } else if (inSection && begin < trim && contents[begin] != ';' && contents[begin] != '#') {
            size_t equal = begin; while (equal < trim && contents[equal] != '=') ++equal;
            size_t keyEnd = equal; while (keyEnd > begin && (contents[keyEnd - 1] == ' ' || contents[keyEnd - 1] == '\t')) --keyEnd;
            if (equal < trim && equalAsciiInsensitive(key, contents + begin, keyEnd - begin)) {
                size_t value = equal + 1; while (value < trim && (contents[value] == ' ' || contents[value] == '\t')) ++value;
                bool negative = value < trim && contents[value] == '-'; if (negative) ++value;
                int64_t parsed{}; bool any{};
                while (value < trim && contents[value] >= '0' && contents[value] <= '9') {
                    any = true; parsed = parsed * 10 + (contents[value++] - '0');
                    if (parsed > 2147483648LL) break;
                }
                if (any && value == trim && parsed <= 2147483648LL)
                    return static_cast<uint32_t>(negative ? -parsed : parsed);
                return static_cast<uint32_t>(defaultValue);
            }
        }
        offset = end;
        while (offset < bytesRead && (contents[offset] == '\r' || contents[offset] == '\n')) ++offset;
    }
    return static_cast<uint32_t>(defaultValue);
}

__declspec(dllexport) int32_t __stdcall WriteProfileStringW(
    const wchar_t* section, const wchar_t* key, const wchar_t* value) noexcept {
    if (!section || !*section || (key && !*key)) { SetLastError(87); return 0; }
    // NT file services currently expose no seek/truncate or atomic replace
    // contract through this Kernel32 surface, so do not report a persisted
    // profile update when the win.ini backend cannot commit one.
    (void)key; (void)value;
    SetLastError(50); // ERROR_NOT_SUPPORTED
    return 0;
}

namespace {
struct GlobalMemoryHeader { uint64_t signature; uint32_t lockCount; uint32_t flags; size_t size; };
constexpr uint64_t GlobalMemorySignature = 0x4D494341474C4F42ULL;
GlobalMemoryHeader* GlobalHeader(void* handle) noexcept {
    if (!handle) return nullptr;
    auto* header = static_cast<GlobalMemoryHeader*>(handle);
    return header->signature == GlobalMemorySignature ? header : nullptr;
}
}
__declspec(dllexport) void* __stdcall GlobalAlloc(uint32_t flags, size_t bytes) noexcept {
    constexpr uint32_t Moveable = 0x0002u, ZeroInit = 0x0040u;
    constexpr uint32_t Accepted = 0x000fu | 0x0070u | 0x2000u | 0x4000u;
    if (flags & ~Accepted) { SetLastError(87); return nullptr; }
    if (!bytes) bytes = 1;
    const bool moveable = (flags & Moveable) != 0;
    if (moveable && bytes > SIZE_MAX - sizeof(GlobalMemoryHeader)) { SetLastError(8); return nullptr; }
    const size_t allocationSize = bytes + (moveable ? sizeof(GlobalMemoryHeader) : 0);
    void* block = HeapAlloc(GetProcessHeap(), (flags & ZeroInit) ? 8u : 0u, allocationSize);
    if (!block) return nullptr;
    if (!moveable) return block;
    auto* header = static_cast<GlobalMemoryHeader*>(block);
    header->signature = GlobalMemorySignature; header->lockCount = 0; header->flags = flags; header->size = bytes;
    return header;
}
__declspec(dllexport) void* __stdcall GlobalLock(void* handle) noexcept {
    if (!handle) { SetLastError(6); return nullptr; }
    if (auto* header = GlobalHeader(handle)) {
        uint32_t count = __atomic_load_n(&header->lockCount, __ATOMIC_ACQUIRE);
        do {
            if (count == UINT32_MAX) { SetLastError(8); return nullptr; }
        } while (!__atomic_compare_exchange_n(&header->lockCount, &count, count + 1,
                                               false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
        return header + 1;
    }
    return handle; // GMEM_FIXED and legacy GMEM_DDESHARE handles are addresses.
}
__declspec(dllexport) int32_t __stdcall GlobalUnlock(void* handle) noexcept {
    if (!handle) { SetLastError(6); return 0; }
    if (auto* header = GlobalHeader(handle)) {
        uint32_t count = __atomic_load_n(&header->lockCount, __ATOMIC_ACQUIRE);
        do {
            if (!count) { SetLastError(158); return 0; } // ERROR_NOT_LOCKED
        } while (!__atomic_compare_exchange_n(&header->lockCount, &count, count - 1,
                                               false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
        SetLastError(0);
        return count > 1 ? 1 : 0;
    }
    SetLastError(0); // Fixed global memory cannot be locked or unlocked.
    return 0;
}

struct Kernel32OsVersionInfoW {
    uint32_t size, major, minor, build, platform;
    wchar_t servicePack[128];
    uint16_t servicePackMajor, servicePackMinor, suiteMask;
    uint8_t productType, reserved;
};
__declspec(dllexport) int32_t __stdcall GetVersionExW(Kernel32OsVersionInfoW* info) noexcept {
    constexpr uint32_t VersionInfoSize = 276, VersionInfoExSize = 284;
    if (!info || (info->size != VersionInfoSize && info->size != VersionInfoExSize)) {
        SetLastError(87); return 0;
    }
    constexpr uintptr_t KuserSharedData = 0x7ffe0000ULL;
    const auto* shared = reinterpret_cast<const micant::KUserSharedData*>(KuserSharedData);
    info->major = shared->ntMajorVersion;
    info->minor = shared->ntMinorVersion;
    info->build = shared->ntBuildNumber & 0x0fffffffU;
    info->platform = 2; // VER_PLATFORM_WIN32_NT
    for (size_t i = 0; i < 128; ++i) info->servicePack[i] = 0;
    if (info->size == VersionInfoExSize) {
        info->servicePackMajor = 0; info->servicePackMinor = 0;
        info->suiteMask = 0; info->productType = shared->productType; info->reserved = 0;
    }
    return 1;
}

// This bounded loader entrypoint can acquire a reference to an image already
// present in the process loader list. Image mapping/relocation of new DLLs is
// not yet implemented; unsupported names fail with the documented Win32 error
// rather than receiving a fabricated module base.
__declspec(dllexport) void* __stdcall LoadLibraryW(const wchar_t* name) noexcept {
    if (!name || !*name) { SetLastError(87); return nullptr; }
    const uintptr_t module = FindModule(name);
    if (!module) { SetLastError(126); return nullptr; } // ERROR_MOD_NOT_FOUND
    const uintptr_t entry = FindLoadedModuleEntry(module);
    if (!entry) { SetLastError(126); return nullptr; }
    auto* loadCount = reinterpret_cast<uint16_t*>(entry + 0x6c);
    uint16_t count = __atomic_load_n(loadCount, __ATOMIC_ACQUIRE);
    do {
        if (count == UINT16_MAX) { SetLastError(8); return nullptr; }
    } while (!__atomic_compare_exchange_n(loadCount, &count, static_cast<uint16_t>(count + 1),
                                           false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    return reinterpret_cast<void*>(module);
}

__declspec(dllexport) int32_t __stdcall FreeLibrary(void* moduleHandle) noexcept {
    const uintptr_t entry = FindLoadedModuleEntry(reinterpret_cast<uintptr_t>(moduleHandle));
    if (!entry) { SetLastError(6); return 0; } // ERROR_INVALID_HANDLE
    auto* loadCount = reinterpret_cast<uint16_t*>(entry + 0x6c);
    uint16_t count = __atomic_load_n(loadCount, __ATOMIC_ACQUIRE);
    do {
        // This loader currently maps only the startup dependency graph. Its
        // initial reference keeps the module resident after transient opens.
        if (count <= 1) { SetLastError(6); return 0; }
    } while (!__atomic_compare_exchange_n(loadCount, &count, static_cast<uint16_t>(count - 1),
                                           false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    return 1;
}

__declspec(dllexport) int32_t __stdcall Beep(uint32_t frequency,
                                             uint32_t duration) noexcept {
    return Kernel32_BeepNative(frequency, duration);
}

namespace {
struct Kernel32ConsoleAlias {
    bool active;
    wchar_t executable[260];
    wchar_t source[128];
    wchar_t target[2048];
};
Kernel32ConsoleAlias g_consoleAliases[64]{};
volatile uint32_t g_consoleAliasLock{};

void LockConsoleAliases() noexcept {
    while (__atomic_exchange_n(&g_consoleAliasLock, 1, __ATOMIC_ACQUIRE))
        __asm__ __volatile__("pause");
}
void UnlockConsoleAliases() noexcept {
    __atomic_store_n(&g_consoleAliasLock, 0, __ATOMIC_RELEASE);
}
bool ConsoleAliasEqual(const wchar_t* left, const wchar_t* right) noexcept {
    if (!left || !right) return false;
    while (*left && *right) {
        wchar_t a = *left++, b = *right++;
        if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
        if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
        if (a != b) return false;
    }
    return *left == *right;
}
bool ConsoleAliasCopy(wchar_t* destination, size_t capacity,
                      const wchar_t* source) noexcept {
    if (!destination || !capacity || !source) return false;
    size_t i = 0;
    while (source[i] && i + 1 < capacity) {
        destination[i] = source[i];
        ++i;
    }
    if (source[i]) return false;
    destination[i] = L'\0';
    return true;
}
}

__declspec(dllexport) int32_t __stdcall AddConsoleAliasW(
    const wchar_t* source, const wchar_t* target, const wchar_t* executable) noexcept {
    if (!source || !*source || !executable || !*executable) {
        SetLastError(87);
        return 0;
    }
    LockConsoleAliases();
    size_t existing = 64, freeSlot = 64;
    for (size_t i = 0; i < 64; ++i) {
        if (g_consoleAliases[i].active &&
            ConsoleAliasEqual(g_consoleAliases[i].executable, executable) &&
            ConsoleAliasEqual(g_consoleAliases[i].source, source)) {
            existing = i;
            break;
        }
        if (!g_consoleAliases[i].active && freeSlot == 64) freeSlot = i;
    }
    if (!target) {
        if (existing != 64) g_consoleAliases[existing].active = false;
        UnlockConsoleAliases();
        return 1;
    }
    const size_t slot = existing != 64 ? existing : freeSlot;
    if (slot == 64 ||
        !ConsoleAliasCopy(g_consoleAliases[slot].source, 128, source) ||
        !ConsoleAliasCopy(g_consoleAliases[slot].target, 2048, target) ||
        !ConsoleAliasCopy(g_consoleAliases[slot].executable, 260, executable)) {
        UnlockConsoleAliases();
        SetLastError(slot == 64 ? 8u : 206u);
        return 0;
    }
    g_consoleAliases[slot].active = true;
    UnlockConsoleAliases();
    return 1;
}

__declspec(dllexport) uint32_t __stdcall GetConsoleAliasW(
    const wchar_t* source, wchar_t* targetBuffer, uint32_t targetBufferLength,
    const wchar_t* executable) noexcept {
    if (!source || !targetBuffer || !targetBufferLength || !executable) {
        SetLastError(87);
        return 0;
    }
    const size_t capacity = targetBufferLength / sizeof(wchar_t);
    if (!capacity) { SetLastError(122); return 0; }
    LockConsoleAliases();
    for (const auto& alias : g_consoleAliases) {
        if (alias.active && ConsoleAliasEqual(alias.executable, executable) &&
            ConsoleAliasEqual(alias.source, source)) {
            const size_t length = wcslen(alias.target);
            const size_t copied = length < capacity - 1 ? length : capacity - 1;
            for (size_t i = 0; i < copied; ++i) targetBuffer[i] = alias.target[i];
            targetBuffer[copied] = L'\0';
            UnlockConsoleAliases();
            if (copied != length) SetLastError(122);
            return static_cast<uint32_t>(copied);
        }
    }
    UnlockConsoleAliases();
    SetLastError(1168);
    return 0;
}

__declspec(dllexport) uint32_t __stdcall GetConsoleAliasesLengthW(
    const wchar_t* executable) noexcept {
    if (!executable) { SetLastError(87); return 0; }
    uint64_t characters = 0;
    LockConsoleAliases();
    for (const auto& alias : g_consoleAliases) {
        if (alias.active && ConsoleAliasEqual(alias.executable, executable))
            characters += wcslen(alias.source) + 1 + wcslen(alias.target) + 1;
    }
    UnlockConsoleAliases();
    if (characters > UINT32_MAX / sizeof(wchar_t)) { SetLastError(122); return 0; }
    return static_cast<uint32_t>(characters * sizeof(wchar_t));
}

__declspec(dllexport) uint32_t __stdcall GetConsoleAliasesW(
    wchar_t* aliasBuffer, uint32_t aliasBufferLength,
    const wchar_t* executable) noexcept {
    if (!aliasBuffer || aliasBufferLength < sizeof(wchar_t) || !executable) {
        SetLastError(87);
        return 0;
    }
    const size_t capacity = aliasBufferLength / sizeof(wchar_t);
    size_t used = 0;
    aliasBuffer[0] = L'\0';
    LockConsoleAliases();
    for (const auto& alias : g_consoleAliases) {
        if (!alias.active || !ConsoleAliasEqual(alias.executable, executable)) continue;
        const size_t sourceLength = wcslen(alias.source);
        const size_t targetLength = wcslen(alias.target);
        const size_t entryLength = sourceLength + 1 + targetLength + 1;
        if (entryLength + used > capacity) {
            UnlockConsoleAliases();
            SetLastError(122);
            return static_cast<uint32_t>(used * sizeof(wchar_t));
        }
        for (size_t i = 0; i < sourceLength; ++i) aliasBuffer[used++] = alias.source[i];
        aliasBuffer[used++] = L'=';
        for (size_t i = 0; i < targetLength; ++i) aliasBuffer[used++] = alias.target[i];
        aliasBuffer[used++] = L'\0';
    }
    UnlockConsoleAliases();
    if (used < capacity) aliasBuffer[used] = L'\0';
    return static_cast<uint32_t>(used * sizeof(wchar_t));
}

__declspec(dllexport) void* __stdcall GetModuleHandleA(const char* name) noexcept {
    if (!name) return reinterpret_cast<void*>(FindModule(nullptr));
    wchar_t wide[260]{};
    size_t length{};
    for (; name[length] && length + 1 < sizeof(wide) / sizeof(wide[0]); ++length) {
        const uint8_t ch = static_cast<uint8_t>(name[length]);
        if (ch >= 0x80) { SetLastError(1113); return nullptr; }
        wide[length] = static_cast<wchar_t>(ch);
    }
    if (name[length]) { SetLastError(206); return nullptr; }
    const uintptr_t module = FindModule(wide);
    if (!module) SetLastError(126);
    return reinterpret_cast<void*>(module);
}

__declspec(dllexport) void* __stdcall LoadLibraryA(const char* name) noexcept {
    if (!name || !*name) { SetLastError(87); return nullptr; }
    wchar_t wide[260]{};
    size_t length{};
    for (; name[length] && length + 1 < sizeof(wide) / sizeof(wide[0]); ++length) {
        const uint8_t ch = static_cast<uint8_t>(name[length]);
        if (ch >= 0x80) { SetLastError(1113); return nullptr; }
        wide[length] = static_cast<wchar_t>(ch);
    }
    if (name[length]) { SetLastError(206); return nullptr; }
    return LoadLibraryW(wide);
}

struct StartupInfoW64 {
    uint32_t cb;
    wchar_t* reserved, *desktop, *title;
    uint32_t x, y, xSize, ySize, xCountChars, yCountChars, fillAttribute, flags;
    uint16_t showWindow, reserved2Size;
    uint8_t* reserved2;
    void* stdInput, *stdOutput, *stdError;
};
__declspec(dllexport) void __stdcall GetStartupInfoW(StartupInfoW64* info) noexcept {
    if (!info) { SetLastError(87); return; }
    if (info->cb < sizeof(StartupInfoW64)) { SetLastError(87); return; }
    auto* bytes = reinterpret_cast<uint8_t*>(info);
    for (size_t i = 0; i < sizeof(StartupInfoW64); ++i) bytes[i] = 0;
    info->cb = sizeof(StartupInfoW64);
    if (const auto* parameters = CurrentProcessParameters()) {
        info->stdInput = reinterpret_cast<void*>(parameters->StandardInput);
        info->stdOutput = reinterpret_cast<void*>(parameters->StandardOutput);
        info->stdError = reinterpret_cast<void*>(parameters->StandardError);
    }
}

struct StartupInfoA64 {
    uint32_t cb;
    char* reserved, *desktop, *title;
    uint32_t x, y, xSize, ySize, xCountChars, yCountChars, fillAttribute, flags;
    uint16_t showWindow, reserved2Size;
    uint8_t* reserved2;
    void* stdInput, *stdOutput, *stdError;
};
__declspec(dllexport) void __stdcall GetStartupInfoA(StartupInfoA64* info) noexcept {
    if (!info) { SetLastError(87); return; }
    if (info->cb < sizeof(StartupInfoA64)) { SetLastError(87); return; }
    auto* bytes = reinterpret_cast<uint8_t*>(info);
    for (size_t i = 0; i < sizeof(StartupInfoA64); ++i) bytes[i] = 0;
    info->cb = sizeof(StartupInfoA64);
    if (const auto* parameters = CurrentProcessParameters()) {
        info->stdInput = reinterpret_cast<void*>(parameters->StandardInput);
        info->stdOutput = reinterpret_cast<void*>(parameters->StandardOutput);
        info->stdError = reinterpret_cast<void*>(parameters->StandardError);
    }
}
__declspec(dllexport) int32_t __stdcall IsDBCSLeadByte(uint8_t) noexcept {
    // The subsystem's active ANSI code page is UTF-8; UTF-8 has no DBCS lead bytes.
    return 0;
}

__declspec(dllexport) void* __stdcall GetProcAddress(void* module, const char* name) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    if (!name || !FindModule(nullptr) || !base || base >= 0x00007FFFFFFF0000ULL) return nullptr;
    bool loaded = false;
    const uintptr_t peb = CurrentPeb();
    const uintptr_t loader = peb ? *reinterpret_cast<const uintptr_t*>(peb + 0x18) : 0;
    if (loader < 0x10000 || loader >= 0x00007FFFFFFF0000ULL - 0x28) return nullptr;
    const uintptr_t headAddress = loader + 0x20;
    uintptr_t link = reinterpret_cast<const UserListEntry64*>(headAddress)->next;
    for (size_t count = 0; count < 32 && link != headAddress; ++count) {
        if (link < 0x10010 || link >= 0x00007FFFFFFF0000ULL) return nullptr;
        if (*reinterpret_cast<const uintptr_t*>(link - 0x10 + 0x30) == base) { loaded = true; break; }
        link = reinterpret_cast<const UserListEntry64*>(link)->next;
    }
    if (!loaded) return nullptr;
    const auto* image = reinterpret_cast<const uint8_t*>(base);
    if (*reinterpret_cast<const uint16_t*>(image) != 0x5a4d) return nullptr;
    const uint32_t peOffset = *reinterpret_cast<const uint32_t*>(image + 0x3c);
    if (peOffset > 0x100000) return nullptr;
    const auto* pe = image + peOffset;
    if (*reinterpret_cast<const uint32_t*>(pe) != 0x00004550 ||
        *reinterpret_cast<const uint16_t*>(pe + 24) != 0x20b) return nullptr;
    const uint32_t imageSize = *reinterpret_cast<const uint32_t*>(pe + 24 + 56);
    const uint32_t exportRva = *reinterpret_cast<const uint32_t*>(pe + 24 + 112);
    const uint32_t exportSize = *reinterpret_cast<const uint32_t*>(pe + 24 + 116);
    if (imageSize < 0x1000 || exportRva > imageSize || exportSize < 40 ||
        exportSize > imageSize - exportRva) return nullptr;
    const auto* directory = image + exportRva;
    const uint32_t ordinalBase = *reinterpret_cast<const uint32_t*>(directory + 16);
    const uint32_t functionCount = *reinterpret_cast<const uint32_t*>(directory + 20);
    const uint32_t nameCount = *reinterpret_cast<const uint32_t*>(directory + 24);
    const uint32_t functions = *reinterpret_cast<const uint32_t*>(directory + 28);
    const uint32_t names = *reinterpret_cast<const uint32_t*>(directory + 32);
    const uint32_t ordinals = *reinterpret_cast<const uint32_t*>(directory + 36);
    if (!functionCount || nameCount > functionCount || functions > imageSize ||
        functionCount * 4ULL > imageSize - functions || names > imageSize ||
        nameCount * 4ULL > imageSize - names || ordinals > imageSize ||
        nameCount * 2ULL > imageSize - ordinals) return nullptr;
    uint32_t functionIndex = UINT32_MAX;
    const uintptr_t ordinal = reinterpret_cast<uintptr_t>(name);
    if (ordinal <= 0xffff) {
        if (ordinal < ordinalBase || ordinal - ordinalBase >= functionCount) return nullptr;
        functionIndex = static_cast<uint32_t>(ordinal - ordinalBase);
    } else {
        for (uint32_t i = 0; i < nameCount; ++i) {
            const uint32_t nameRva = *reinterpret_cast<const uint32_t*>(image + names + i * 4ULL);
            if (!nameRva || nameRva >= imageSize) return nullptr;
            const char* candidate = reinterpret_cast<const char*>(image + nameRva);
            size_t j{};
            while (j < 512 && candidate[j] && name[j] && candidate[j] == name[j]) ++j;
            if (j < 512 && candidate[j] == 0 && name[j] == 0) {
                functionIndex = *reinterpret_cast<const uint16_t*>(image + ordinals + i * 2ULL);
                break;
            }
        }
    }
    if (functionIndex >= functionCount) return nullptr;
    const uint32_t rva = *reinterpret_cast<const uint32_t*>(image + functions + functionIndex * 4ULL);
    if (!rva || rva >= imageSize || (rva >= exportRva && rva - exportRva < exportSize)) return nullptr;
    return reinterpret_cast<void*>(base + rva);
}

__declspec(dllexport) void __stdcall Sleep(uint32_t milliseconds) noexcept {
    if (!milliseconds) { (void)NtYieldExecution(); return; }
    const int64_t interval = -static_cast<int64_t>(milliseconds) * 10000;
    (void)NtDelayExecution(0, &interval);
}

__declspec(dllexport) int32_t __stdcall VirtualProtect(
    void* address, size_t size, uint32_t protection, uint32_t* oldProtection) noexcept {
    if (!address || !size || !oldProtection) return 0;
    void* base = address;
    size_t regionSize = size;
    uint32_t previous{};
    const int32_t status = NtProtectVirtualMemory(reinterpret_cast<void*>(~uintptr_t{0}),
        &base, &regionSize, protection, &previous);
    if (status < 0) return 0;
    *oldProtection = previous;
    return 1;
}

struct Kernel32MemoryBasicInformation64 {
    void* baseAddress;
    void* allocationBase;
    uint32_t allocationProtect;
    uint16_t partitionId;
    uint16_t reserved;
    size_t regionSize;
    uint32_t state;
    uint32_t protect;
    uint32_t type;
    uint32_t padding;
};
static_assert(sizeof(Kernel32MemoryBasicInformation64) == 48);

__declspec(dllexport) size_t __stdcall VirtualQuery(
    const void* address, Kernel32MemoryBasicInformation64* information,
    size_t informationLength) noexcept {
    if (!information || informationLength < sizeof(*information)) {
        SetLastError(87);
        return 0;
    }
    size_t returned{};
    const int32_t status = NtQueryVirtualMemory(reinterpret_cast<void*>(~uintptr_t{0}),
        address, 0, information, sizeof(*information), &returned);
    if (status < 0) {
        SetLastError(status == static_cast<int32_t>(0xC0000003u) ? 87u : 487u);
        return 0;
    }
    return returned;
}

__declspec(dllexport) void __stdcall InitializeCriticalSection(void* section) noexcept {
    auto* cs = static_cast<WinCriticalSection64*>(section);
    if (!cs) return;
    cs->debugInfo = nullptr; cs->lockCount = -1; cs->recursionCount = 0;
    cs->owningThread = 0; cs->lockSemaphore = nullptr; cs->spinCount = 0;
}

__declspec(dllexport) int32_t __stdcall InitializeCriticalSectionEx(
    void* section, uint32_t spinCount, uint32_t flags) noexcept {
    if (!section || (flags & ~0x01000000U)) return 0;
    InitializeCriticalSection(section);
    static_cast<WinCriticalSection64*>(section)->spinCount = spinCount;
    return 1;
}

__declspec(dllexport) void __stdcall EnterCriticalSection(void* section) noexcept {
    auto* cs = static_cast<WinCriticalSection64*>(section);
    if (!cs) return;
    uint32_t thread{}; __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(thread));
    if (!thread) return;
    if (cs->owningThread == thread) { ++cs->recursionCount; return; }
    for (;;) {
        int32_t expected = -1;
        if (__atomic_compare_exchange_n(&cs->lockCount, &expected, 0, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            cs->owningThread = thread; cs->recursionCount = 1; return;
        }
        (void)NtYieldExecution();
    }
}

__declspec(dllexport) void __stdcall LeaveCriticalSection(void* section) noexcept {
    auto* cs = static_cast<WinCriticalSection64*>(section);
    uint32_t thread{}; __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(thread));
    if (!cs || !thread || cs->owningThread != thread || cs->recursionCount <= 0) return;
    if (--cs->recursionCount == 0) {
        cs->owningThread = 0;
        __atomic_store_n(&cs->lockCount, -1, __ATOMIC_RELEASE);
    }
}

__declspec(dllexport) void __stdcall DeleteCriticalSection(void* section) noexcept {
    auto* cs = static_cast<WinCriticalSection64*>(section);
    if (!cs) return;
    if (__atomic_load_n(&cs->lockCount, __ATOMIC_ACQUIRE) == -1) {
        cs->lockCount = 0; cs->recursionCount = 0; cs->owningThread = 0;
    }
}

// SRWLOCK is a pointer-sized, zero-initialized exclusive lock. This limited
// implementation accepts exclusive acquisition only; shared acquisitions
// remain explicit failures until shared-reader state is present.
__declspec(dllexport) void __stdcall AcquireSRWLockExclusive(void* lock) noexcept {
    if (!lock) return;
    auto* state = static_cast<uintptr_t*>(lock);
    for (;;) {
        uintptr_t expected = 0;
        if (__atomic_compare_exchange_n(state, &expected, 1, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
        (void)NtYieldExecution();
    }
}
__declspec(dllexport) void __stdcall ReleaseSRWLockExclusive(void* lock) noexcept {
    if (lock) __atomic_store_n(static_cast<uintptr_t*>(lock), 0, __ATOMIC_RELEASE);
}
__declspec(dllexport) int32_t __stdcall TryAcquireSRWLockExclusive(void* lock) noexcept {
    if (!lock) return 0;
    uintptr_t expected = 0;
    return __atomic_compare_exchange_n(static_cast<uintptr_t*>(lock), &expected, 1,
        false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 1 : 0;
}
__declspec(dllexport) int32_t __stdcall TryEnterCriticalSection(void* section) noexcept {
    auto* cs = static_cast<WinCriticalSection64*>(section);
    if (!cs) return 0;
    uint32_t thread{}; __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(thread));
    if (!thread) return 0;
    if (cs->owningThread == thread) { ++cs->recursionCount; return 1; }
    int32_t expected = -1;
    if (!__atomic_compare_exchange_n(&cs->lockCount, &expected, 0, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 0;
    cs->owningThread = thread; cs->recursionCount = 1;
    return 1;
}

__declspec(dllexport) void* __stdcall TlsGetValue(uint32_t index) noexcept {
    uintptr_t teb{}; __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || index >= 64) { SetLastError(87); return nullptr; }
    const uintptr_t vector = *reinterpret_cast<const uintptr_t*>(teb + 0x58);
    SetLastError(0);
    if (!vector) return nullptr;
    return reinterpret_cast<void*>(reinterpret_cast<const uintptr_t*>(vector)[index]);
}

__declspec(dllexport) void __stdcall RtlCaptureContext(void* context) noexcept {
    using Function = void (__stdcall *)(void*) noexcept;
    const auto function = reinterpret_cast<Function>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlCaptureContext"));
    if (function) function(context);
}
__declspec(dllexport) [[noreturn]] void __stdcall RtlRestoreContext(void* context, void* record) noexcept {
    using Function = void (__stdcall *)(void*, void*) noexcept;
    const auto function = reinterpret_cast<Function>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlRestoreContext"));
    if (function) function(context, record);
    __builtin_trap();
}

__declspec(dllexport) void* __stdcall RtlLookupFunctionEntry(
    uint64_t controlPc, uint64_t* imageBase, void* historyTable) noexcept {
    using Function = void* (__stdcall *)(uint64_t, uint64_t*, void*) noexcept;
    const auto function = reinterpret_cast<Function>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlLookupFunctionEntry"));
    return function ? function(controlPc, imageBase, historyTable) : nullptr;
}

__declspec(dllexport) void* __stdcall RtlVirtualUnwind(
    uint32_t handlerType, uint64_t imageBase, uint64_t controlPc,
    void* functionEntry, void* context, void** handlerData,
    uint64_t* establisherFrame, void* contextPointers) noexcept {
    using Function = void* (__stdcall *)(uint32_t, uint64_t, uint64_t, void*,
        void*, void**, uint64_t*, void*) noexcept;
    const auto function = reinterpret_cast<Function>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlVirtualUnwind"));
    return function ? function(handlerType, imageBase, controlPc, functionEntry,
        context, handlerData, establisherFrame, contextPointers) : nullptr;
}

__declspec(dllexport) [[noreturn]] void __stdcall RtlUnwindEx(
    void* targetFrame, void* targetIp, void* exceptionRecord, void* returnValue,
    void* context, void* historyTable) noexcept {
    using Function = void (__stdcall *)(void*, void*, void*, void*, void*, void*) noexcept;
    const auto function = reinterpret_cast<Function>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlUnwindEx"));
    if (function) function(targetFrame, targetIp, exceptionRecord, returnValue,
                           context, historyTable);
    __builtin_trap();
}

__declspec(dllexport) void* __stdcall FindResourceW(void* module,
    const wchar_t* name, const wchar_t* type) noexcept {
    return FindResourceExW(module, type, name, 0);
}
__declspec(dllexport) int32_t __stdcall IsTextUnicode(
    const void* buffer, int32_t byteCount, int32_t* resultFlags) noexcept {
    constexpr int32_t Ascii16 = 0x0001, Statistics = 0x0002,
        Controls = 0x0004, NullBytes = 0x0008, ReverseAscii16 = 0x0010,
        ReverseStatistics = 0x0020, ReverseControls = 0x0040,
        ReverseNullBytes = 0x0080, OddLength = 0x0100;
    if (!buffer || byteCount < 0) { SetLastError(87); return 0; }
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    const size_t length = static_cast<size_t>(byteCount);
    const size_t pairs = length / 2;
    size_t highZero{}, lowZero{}, controls{}, nulBytes{};
    for (size_t i = 0; i < pairs; ++i) {
        const uint8_t low = bytes[i * 2], high = bytes[i * 2 + 1];
        highZero += high == 0;
        lowZero += low == 0;
        nulBytes += low == 0 || high == 0;
        controls += (low < 0x20 && low != '\n' && low != '\r' && low != '\t') || high != 0;
    }
    int32_t detected{};
    if (pairs && highZero * 2 >= pairs) detected |= Ascii16 | Statistics;
    if (pairs && lowZero * 2 >= pairs) detected |= ReverseAscii16 | ReverseStatistics;
    if (pairs && controls * 2 < pairs) detected |= Controls | ReverseControls;
    if (nulBytes) detected |= NullBytes | ReverseNullBytes;
    if (length & 1u) detected |= OddLength;
    if (resultFlags) *resultFlags = detected;
    return pairs && highZero * 2 >= pairs && !(length & 1u) ? 1 : 0;
}
__declspec(dllexport) int32_t __stdcall FlushInstructionCache(
    void* process, const void* base, size_t size) noexcept {
    const int32_t status = NtFlushInstructionCache(process, base, size);
    if (status < 0) { SetLastError(87); return 0; }
    return 1;
}
__declspec(dllexport) int32_t __stdcall GetModuleHandleExW(uint32_t flags,
    const wchar_t* name, void** module) noexcept {
    constexpr uint32_t FromAddress = 4u, UnchangedRefcount = 2u;
    if (!module || (flags & ~(FromAddress | UnchangedRefcount)) ||
        ((flags & FromAddress) && !name)) { SetLastError(87); return 0; }
    if (flags & FromAddress) { SetLastError(50); return 0; }
    const uintptr_t base = FindModule(name);
    if (!base || !FindLoadedModuleEntry(base)) { SetLastError(126); return 0; }
    *module = reinterpret_cast<void*>(base);
    return 1;
}
__declspec(dllexport) uint32_t __stdcall GetSystemDirectoryW(
    wchar_t* buffer, uint32_t capacity) noexcept {
    constexpr wchar_t path[] = L"C:\\Windows\\System32";
    constexpr uint32_t length = static_cast<uint32_t>(sizeof(path) / sizeof(path[0]) - 1);
    if (!buffer || capacity <= length) { SetLastError(122); return length + 1; }
    for (uint32_t i = 0; i <= length; ++i) buffer[i] = path[i];
    return length;
}
__declspec(dllexport) void* __stdcall GlobalFree(void* handle) noexcept {
    if (!handle) return nullptr;
    auto* header = GlobalHeader(handle);
    if (header && __atomic_load_n(&header->lockCount, __ATOMIC_ACQUIRE)) {
        SetLastError(158); return handle;
    }
    void* block = header ? static_cast<void*>(header) : handle;
    if (header) header->signature = 0;
    return HeapFree(GetProcessHeap(), 0, block) ? nullptr : handle;
}
__declspec(dllexport) size_t __stdcall HeapSize(
    void*, uint32_t, const void*) noexcept {
    SetLastError(87);
    return static_cast<size_t>(-1);
}
__declspec(dllexport) int32_t __stdcall MulDiv(int32_t number,
    int32_t numerator, int32_t denominator) noexcept {
    if (!denominator) { SetLastError(87); return -1; }
    const int64_t product = static_cast<int64_t>(number) * numerator;
    const bool negative = (product < 0) != (denominator < 0);
    const uint64_t magnitude = product < 0 ? static_cast<uint64_t>(-product)
                                           : static_cast<uint64_t>(product);
    const uint64_t divisor = denominator < 0
        ? static_cast<uint64_t>(-static_cast<int64_t>(denominator))
        : static_cast<uint64_t>(denominator);
    const uint64_t quotient = (magnitude + divisor / 2) / divisor;
    if (quotient > static_cast<uint64_t>(INT32_MAX) + (negative ? 1u : 0u)) {
        SetLastError(87); return -1;
    }
    return static_cast<int32_t>(negative ? -static_cast<int64_t>(quotient)
                                         : static_cast<int64_t>(quotient));
}
__declspec(dllexport) int32_t __stdcall InitOnceExecuteOnce(void* once,
    int32_t (__stdcall *callback)(void*, void*, void**), void* parameter,
    void** context) noexcept {
    if (!once || !callback) { SetLastError(87); return 0; }
    auto* state = static_cast<uintptr_t*>(once);
    for (;;) {
        uintptr_t value = __atomic_load_n(state, __ATOMIC_ACQUIRE);
        if (value == 2) { if (context) *context = nullptr; return 1; }
        if (value == 0) {
            uintptr_t expected = 0;
            if (__atomic_compare_exchange_n(state, &expected, 1, false,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                void* resultContext{};
                if (!callback(once, parameter, &resultContext)) {
                    __atomic_store_n(state, 0, __ATOMIC_RELEASE); return 0;
                }
                if (context) *context = resultContext;
                __atomic_store_n(state, 2, __ATOMIC_RELEASE); return 1;
            }
        }
        (void)NtYieldExecution();
    }
}
__declspec(dllexport) uint32_t __stdcall GetThreadId(void* thread) noexcept {
    if (!thread) { SetLastError(6); return 0; }
    struct ThreadBasicInformation { int32_t exitStatus; uint32_t padding;
        void* teb; void* process; void* threadId; uintptr_t affinity;
        int32_t priority, basePriority; } information{};
    uint32_t returned{};
    if (NtQueryInformationThread(thread, 0, &information, sizeof(information), &returned) < 0) {
        SetLastError(6); return 0;
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(information.threadId));
}
__declspec(dllexport) int32_t __stdcall GetTempPathW(uint32_t, wchar_t*) noexcept {
    SetLastError(50); return 0;
}
__declspec(dllexport) uint32_t __stdcall GetTempFileNameW(
    const wchar_t*, const wchar_t*, uint32_t, wchar_t*) noexcept {
    SetLastError(50); return 0;
}
__declspec(dllexport) int32_t __stdcall GetTimeFormatW(uint32_t, uint32_t,
    const void*, const wchar_t*, wchar_t*, int32_t) noexcept {
    SetLastError(50); return 0;
}
__declspec(dllexport) int32_t __stdcall SleepConditionVariableSRW(
    void*, void*, uint32_t, uint32_t) noexcept {
    SetLastError(50); return 0;
}
__declspec(dllexport) void __stdcall WakeAllConditionVariable(void*) noexcept {}
__declspec(dllexport) void __stdcall WakeConditionVariable(void*) noexcept {}
namespace { void PaintDebugTrace(uint32_t, uint32_t, uint32_t) noexcept; }
__declspec(dllexport) void __stdcall OutputDebugStringW(const wchar_t* text) noexcept {
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_MSPAINT) || defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD)
    if (!text) return;
    uint32_t hash = 2166136261u;
    uint32_t length{};
    while (length < 4096 && text[length]) {
        hash = (hash ^ static_cast<uint16_t>(text[length])) * 16777619u;
        ++length;
    }
    PaintDebugTrace(210, hash, length);
#else
    (void)text;
#endif
}
__declspec(dllexport) int32_t __stdcall SwitchToThread() noexcept {
    const int32_t status = NtYieldExecution();
    return status >= 0 && static_cast<uint32_t>(status) != 0x40000024U;
}

namespace {
constexpr uint32_t FLSMaximumAvailable = 128;
struct NativeFlsData { void* listForward; void* listBackward; void* values[FLSMaximumAvailable]; };
using NativeFlsCallback = void (__stdcall *)(void*);
uint8_t g_flsAllocated[FLSMaximumAvailable]{};
NativeFlsCallback g_flsCallbacks[FLSMaximumAvailable]{};
uint32_t g_flsLock{};
void PaintDebugTrace(uint32_t stage, uint32_t value, uint32_t flags) noexcept {
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_MSPAINT) || defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD)
    using GetModuleFn = void* (__stdcall *)(const wchar_t*);
    using GetProcFn = void* (__stdcall *)(void*, const char*);
    using TraceFn = void (__stdcall *)(uint32_t, uint32_t, uint32_t);
    auto getModule = reinterpret_cast<GetModuleFn>(GetModuleHandleW);
    auto getProc = reinterpret_cast<GetProcFn>(GetProcAddress);
    void* module = getModule ? getModule(L"win32k.dll") : nullptr;
    auto trace = module && getProc ? reinterpret_cast<TraceFn>(
        getProc(module, "MicaWin32kDebugInputTrace")) : nullptr;
    if (trace) trace(stage, value, flags);
#else
    (void)stage; (void)value; (void)flags;
#endif
}
void LockFls() noexcept {
    while (__atomic_exchange_n(&g_flsLock, 1u, __ATOMIC_ACQUIRE))
        __asm__ __volatile__("pause" ::: "memory");
}
void UnlockFls() noexcept { __atomic_store_n(&g_flsLock, 0u, __ATOMIC_RELEASE); }
NativeFlsData* CurrentFlsData(bool create) noexcept {
    uintptr_t teb{}; __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || teb > UINTPTR_MAX - 0x2E8) return nullptr;
    auto** slot = reinterpret_cast<NativeFlsData**>(teb + 0x2E8);
    auto* data = *slot;
    if (!data && create) {
        data = static_cast<NativeFlsData*>(HeapAlloc(GetProcessHeap(), 8u, sizeof(NativeFlsData)));
        if (data) *slot = data;
    }
    return data;
}
}
__declspec(dllexport) uint32_t __stdcall FlsAlloc(NativeFlsCallback callback) noexcept {
    LockFls();
    for (uint32_t i = 0; i < FLSMaximumAvailable; ++i) {
        if (g_flsAllocated[i]) continue;
        g_flsAllocated[i] = 1; g_flsCallbacks[i] = callback;
        UnlockFls(); PaintDebugTrace(200, i, callback ? 1u : 0u); return i;
    }
    UnlockFls(); SetLastError(8); return UINT32_MAX;
}
__declspec(dllexport) int32_t __stdcall FlsFree(uint32_t index) noexcept {
    if (index >= FLSMaximumAvailable) { SetLastError(87); return 0; }
    LockFls();
    if (!g_flsAllocated[index]) { UnlockFls(); SetLastError(87); return 0; }
    const auto callback = g_flsCallbacks[index];
    g_flsAllocated[index] = 0; g_flsCallbacks[index] = nullptr;
    UnlockFls();
    if (auto* data = CurrentFlsData(false); data && data->values[index]) {
        void* value = data->values[index]; data->values[index] = nullptr;
        if (callback) callback(value);
    }
    return 1;
}
__declspec(dllexport) void* __stdcall FlsGetValue(uint32_t index) noexcept {
    if (index >= FLSMaximumAvailable || !g_flsAllocated[index]) {
        SetLastError(87); return nullptr;
    }
    auto* data = CurrentFlsData(false);
    SetLastError(0);
    return data ? data->values[index] : nullptr;
}
__declspec(dllexport) int32_t __stdcall FlsSetValue(uint32_t index, void* value) noexcept {
    if (index >= FLSMaximumAvailable || !g_flsAllocated[index]) {
        SetLastError(87); return 0;
    }
    auto* data = CurrentFlsData(true);
    if (!data) { SetLastError(8); return 0; }
    data->values[index] = value;
    PaintDebugTrace(201, index, value ? 1u : 0u);
    return 1;
}

__declspec(dllexport) void* __stdcall CreateThread(
    void*, size_t stackSize, uint32_t (__stdcall *start)(void*), void* argument,
    uint32_t flags, uint32_t* threadId) noexcept {
    if (!start) { SetLastError(87); return nullptr; }
    void* thread = nullptr;
    int32_t status = NtCreateThreadEx(&thread, 0x001F03FFu, nullptr,
        reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
        reinterpret_cast<void*>(start), argument, flags, 0, stackSize, 0, nullptr);
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return nullptr; }
    if (threadId) {
        struct ThreadBasicInformation {
            int32_t exitStatus; uint32_t padding; void* teb;
            uintptr_t processId; uintptr_t id; uintptr_t affinity;
            int32_t priority; int32_t basePriority;
        } information{};
        status = NtQueryInformationThread(thread, 0, &information,
            sizeof(information), nullptr);
        if (status < 0) {
            (void)NtClose(thread);
            SetLastError(RtlNtStatusToDosError(status));
            return nullptr;
        }
        *threadId = static_cast<uint32_t>(information.id);
    }
    return thread;
}

struct Kernel32LargeInteger { uint64_t quadPart; };
__declspec(dllexport) void* __stdcall CreateFileMappingW(
    void* file, void*, uint32_t protection, uint32_t maximumHigh,
    uint32_t maximumLow, const wchar_t*) noexcept {
    Kernel32LargeInteger maximum{(static_cast<uint64_t>(maximumHigh) << 32) | maximumLow};
    void* section = nullptr;
    const int32_t status = NtCreateSection(&section, 0x000F001Fu, nullptr,
        (maximum.quadPart ? &maximum : nullptr), protection ? protection : 0x04u,
        0x08000000u, file == reinterpret_cast<void*>(static_cast<intptr_t>(-1)) ? nullptr : file);
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return nullptr; }
    return section;
}
__declspec(dllexport) void* __stdcall MapViewOfFile(
    void* section, uint32_t, uint32_t offsetHigh, uint32_t offsetLow,
    size_t requestedSize) noexcept {
    uintptr_t base = 0;
    size_t viewSize = requestedSize;
    Kernel32LargeInteger offset{(static_cast<uint64_t>(offsetHigh) << 32) | offsetLow};
    const int32_t status = NtMapViewOfSection(section,
        reinterpret_cast<void*>(static_cast<intptr_t>(-1)), &base, 0,
        requestedSize, &offset, &viewSize, 1, 0x3000u, 0x04u);
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return nullptr; }
    return reinterpret_cast<void*>(base);
}
__declspec(dllexport) int32_t __stdcall UnmapViewOfFile(const void* base) noexcept {
    if (!base) { SetLastError(87); return 0; }
    const int32_t status = NtUnmapViewOfSection(
        reinterpret_cast<void*>(static_cast<intptr_t>(-1)), reinterpret_cast<uintptr_t>(base));
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return 0; }
    return 1;
}

}

#else

/**
 * @file winnt_kernel32_advapi32_system32_abi.cpp
 * @brief Horizon 110 (Kernel Unit #82): Sovereign WinNT Core User-Mode System32 DLLs
 *        (kernel32.dll & advapi32.dll) Official PE32+ Exports, KUSER_SHARED_DATA 10MHz
 *        QPC Timer, D3D12Fence Event Synchronization, DynamicLoader, and Cm Registry.
 */

#include "micant/um/subsystems/winbase/winnt_kernel32_advapi32_system32_abi.hpp"

extern "C" int __stdcall DllMainCRTStartup(void*, unsigned long, void*) noexcept {
    return 1;
}

extern "C" {
__declspec(dllexport) void __stdcall SetLastError(uint32_t error) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryPerformanceCounter(
    int64_t* counter, int64_t* frequency) noexcept;
__declspec(dllimport) int32_t __stdcall NtDelayExecution(
    uint8_t alertable, const int64_t* interval) noexcept;
__declspec(dllimport) int32_t __stdcall NtYieldExecution() noexcept;
__declspec(dllimport) int32_t __stdcall NtOpenKey(
    void** keyHandle, uint32_t desiredAccess, micant::ObjectAttributes* attributes) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryValueKey(
    void* keyHandle, micant::UnicodeString* valueName, uint32_t informationClass,
    void* information, size_t informationLength, size_t* resultLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtClose(void* handle) noexcept;
__declspec(dllimport) int32_t __stdcall NtCreateEvent(void** handle, uint32_t access, void* attributes, uint32_t type, uint8_t initialState) noexcept;
__declspec(dllimport) int32_t __stdcall NtSetEvent(void* handle, int32_t* previousState) noexcept;
__declspec(dllimport) int32_t __stdcall NtResetEvent(void* handle, int32_t* previousState) noexcept;
__declspec(dllimport) int32_t __stdcall NtWaitForSingleObject(void* handle, uint8_t alertable, const int64_t* timeout) noexcept;
__declspec(dllimport) int32_t __stdcall NtAllocateVirtualMemory(void* process, void** base, uint64_t zeroBits, size_t* size, uint32_t type, uint32_t protect) noexcept;
__declspec(dllimport) int32_t __stdcall NtFreeVirtualMemory(void* process, void** base, size_t* size, uint32_t type) noexcept;
}

namespace micant::um::winnt_kernel32_advapi32_abi {

namespace {
uint32_t g_lastError = ERROR_SUCCESS_OFFICIAL;
uint32_t Win32ErrorFromNtStatus(int32_t status) noexcept {
    switch (static_cast<uint32_t>(status)) {
        case 0xC0000008U: return 6;
        case 0xC000000DU: return 87;
        case 0xC0000022U: return 5;
        case 0xC000009AU: return 8;
        case 0xC00000BBU: return 50;
        default: return 31;
    }
}
}

namespace {

struct AdvapiRegistryKey { void* handle{}; wchar_t path[512]{}; bool active{}; };
AdvapiRegistryKey g_regKeyPool[32]{};
uint32_t g_regKeyPoolNext{};
uint32_t g_regKeyLock{};
struct AdvapiRegistryKeyGuard {
    AdvapiRegistryKeyGuard() noexcept {
        while (__atomic_exchange_n(&g_regKeyLock, 1, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause");
    }
    ~AdvapiRegistryKeyGuard() { __atomic_store_n(&g_regKeyLock, 0, __ATOMIC_RELEASE); }
};

alignas(64) uint8_t g_virtualAllocArena[256 * 1024]{};

inline void ByteCopy(void* dst, const void* src, size_t n) noexcept {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}

inline void ByteZero(void* dst, size_t n) noexcept {
    auto* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < n; ++i) d[i] = 0;
}
} // namespace

int32_t Kernel32_QueryPerformanceCounter(int64_t* lpPerformanceCount) noexcept {
    if (!lpPerformanceCount) return 0;
    return NtQueryPerformanceCounter(lpPerformanceCount, nullptr) >= 0 ? 1 : 0;
}

int32_t Kernel32_QueryPerformanceFrequency(int64_t* lpFrequency) noexcept {
    if (!lpFrequency) return 0;
    int64_t counter{};
    return NtQueryPerformanceCounter(&counter, lpFrequency) >= 0 ? 1 : 0;
}

uint64_t Kernel32_GetTickCount64() noexcept {
    auto* shared = reinterpret_cast<volatile const uint64_t*>(0x7FFE0320ULL);
    return *shared;
}

void* Kernel32_CreateEventExW(void* attributes, const wchar_t* lpName, uint32_t dwFlags, uint32_t desiredAccess) noexcept {
    if (attributes || lpName || (dwFlags & ~3U) != 0) { SetLastError(120); return nullptr; }
    void* handle{};
    const int32_t status = NtCreateEvent(&handle, desiredAccess, nullptr, (dwFlags & 1U) ? 0U : 1U, static_cast<uint8_t>((dwFlags & 2U) != 0));
    if (status < 0 || !handle) { SetLastError(Win32ErrorFromNtStatus(status)); return nullptr; }
    return handle;
}

void* Kernel32_CreateEventW(void* lpEventAttributes, int32_t bManualReset, int32_t bInitialState, const wchar_t* lpName) noexcept {
    uint32_t flags = (bManualReset ? 0x1U : 0U) | (bInitialState ? 0x2U : 0U);
    return Kernel32_CreateEventExW(lpEventAttributes, lpName, flags, 0x1F0003U);
}

int32_t Kernel32_SetEvent(void* hEvent) noexcept {
    const int32_t status = NtSetEvent(hEvent, nullptr);
    if (status < 0) SetLastError(Win32ErrorFromNtStatus(status));
    return status >= 0;
}

int32_t Kernel32_ResetEvent(void* hEvent) noexcept {
    const int32_t status = NtResetEvent(hEvent, nullptr);
    if (status < 0) SetLastError(Win32ErrorFromNtStatus(status));
    return status >= 0;
}

uint32_t Kernel32_WaitForSingleObjectEx(void* hHandle, uint32_t milliseconds, int32_t alertable) noexcept {
    const int64_t interval = -static_cast<int64_t>(milliseconds) * 10000;
    const int64_t* timeout = milliseconds == UINT32_MAX ? nullptr : &interval;
    const int32_t status = NtWaitForSingleObject(hHandle, static_cast<uint8_t>(alertable != 0), timeout);
    if (status == 0) return WAIT_OBJECT_0_OFFICIAL;
    if (static_cast<uint32_t>(status) == 0x102U) return WAIT_TIMEOUT_OFFICIAL;
    if (static_cast<uint32_t>(status) == 0x101U) return 0xC0U;
    SetLastError(Win32ErrorFromNtStatus(status));
    return 0xFFFFFFFFU;
}

uint32_t Kernel32_WaitForSingleObject(void* hHandle, uint32_t dwMilliseconds) noexcept {
    return Kernel32_WaitForSingleObjectEx(hHandle, dwMilliseconds, 0);
}

uint32_t Kernel32_WaitForMultipleObjectsEx(uint32_t nCount, const void* const* lpHandles, int32_t /*bWaitAll*/, uint32_t dwMilliseconds, int32_t bAlertable) noexcept {
    if (nCount != 1 || !lpHandles) { SetLastError(87); return 0xFFFFFFFFU; }
    return Kernel32_WaitForSingleObjectEx(const_cast<void*>(lpHandles[0]), dwMilliseconds, bAlertable);
}

int32_t Kernel32_CloseHandle(void* hObject) noexcept {
    const int32_t status = NtClose(hObject);
    if (status < 0) SetLastError(Win32ErrorFromNtStatus(status));
    return status >= 0;
}

void* Kernel32_VirtualAlloc(void* address, size_t size, uint32_t allocationType, uint32_t protect) noexcept {
    if (!size) { SetLastError(87); return nullptr; }
    size_t regionSize = size;
    const int32_t status = NtAllocateVirtualMemory(reinterpret_cast<void*>(static_cast<intptr_t>(-1)), &address, 0, &regionSize, allocationType, protect);
    if (status < 0) { SetLastError(Win32ErrorFromNtStatus(status)); return nullptr; }
    return address;
}

int32_t Kernel32_VirtualFree(void* lpAddress, size_t size, uint32_t freeType) noexcept {
    if (!lpAddress || (freeType == MEM_RELEASE_OFFICIAL && size != 0)) { SetLastError(87); return 0; }
    const int32_t status = NtFreeVirtualMemory(reinterpret_cast<void*>(static_cast<intptr_t>(-1)), &lpAddress, &size, freeType);
    if (status < 0) SetLastError(Win32ErrorFromNtStatus(status));
    return status >= 0;
}

void* Kernel32_LoadLibraryExW(const wchar_t* lpLibFileName, void* hFile, uint32_t flags) noexcept {
    if (!lpLibFileName || hFile || flags) { SetLastError(87); return nullptr; }
    // Returning a loaded base without incrementing its loader reference count
    // would make a later FreeLibrary unsafe. Until the loader owns reference
    // counts and disk-backed loading, report the operation as unsupported.
    SetLastError(120);
    return nullptr;
}

void* Kernel32_LoadLibraryW(const wchar_t* lpLibFileName) noexcept {
    return Kernel32_LoadLibraryExW(lpLibFileName, nullptr, 0);
}

struct UserListEntry64 { uintptr_t next, previous; };
struct UserUnicodeString64 { uint16_t length, maximumLength; uint32_t padding; const wchar_t* buffer; };

static uintptr_t CurrentPebAddress() noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (teb < 0x10000 || teb > 0x00007FFFFFFF0000ULL - 0x60) return 0;
    const auto peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    return peb >= 0x10000 && peb < 0x00007FFFFFFF0000ULL ? peb : 0;
}

static wchar_t FoldAscii(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z' ? value + (L'a' - L'A') : value;
}

static bool EqualModuleName(const wchar_t* fullPath, uint16_t byteLength,
                            const wchar_t* requested) noexcept {
    if (!fullPath || !requested || (byteLength & 1)) return false;
    const size_t count = byteLength / sizeof(wchar_t);
    size_t pathBase{};
    for (size_t i = 0; i < count; ++i)
        if (fullPath[i] == L'\\' || fullPath[i] == L'/') pathBase = i + 1;
    size_t requestedBase{};
    for (size_t i = 0; requested[i]; ++i)
        if (requested[i] == L'\\' || requested[i] == L'/') requestedBase = i + 1;
    size_t i = pathBase;
    size_t j = requestedBase;
    for (; i < count && requested[j]; ++i, ++j) {
        if (FoldAscii(fullPath[i]) != FoldAscii(requested[j])) return false;
        if (fullPath[i] > 0x7f || requested[j] > 0x7f) return false;
    }
    return i == count && requested[j] == 0;
}

static bool IsLoadedModule(uintptr_t peb, uintptr_t moduleBase) noexcept {
    if (!peb || moduleBase < 0x10000 || moduleBase >= 0x00007FFFFFFF0000ULL) return false;
    const uintptr_t loader = *reinterpret_cast<const uintptr_t*>(peb + 0x18);
    if (loader < 0x10000 || loader >= 0x00007FFFFFFF0000ULL - 0x28) return false;
    const uintptr_t headAddress = loader + 0x20;
    const auto* head = reinterpret_cast<const UserListEntry64*>(headAddress);
    uintptr_t link = head->next;
    for (size_t count = 0; count < 32 && link != headAddress; ++count) {
        if (link < 0x10000 + 0x10 || link >= 0x00007FFFFFFF0000ULL) return false;
        const uintptr_t entry = link - 0x10;
        if (*reinterpret_cast<const uintptr_t*>(entry + 0x30) == moduleBase) return true;
        link = reinterpret_cast<const UserListEntry64*>(link)->next;
    }
    return false;
}

void* Kernel32_GetModuleHandleW(const wchar_t* moduleName) noexcept {
    const uintptr_t peb = CurrentPebAddress();
    if (!peb) return nullptr;
    if (!moduleName) return reinterpret_cast<void*>(
        *reinterpret_cast<const uintptr_t*>(peb + 0x10));
    const uintptr_t loader = *reinterpret_cast<const uintptr_t*>(peb + 0x18);
    if (loader < 0x10000 || loader >= 0x00007FFFFFFF0000ULL - 0x28) return nullptr;
    const uintptr_t headAddress = loader + 0x20;
    const auto* head = reinterpret_cast<const UserListEntry64*>(headAddress);
    uintptr_t link = head->next;
    for (size_t count = 0; count < 32 && link != headAddress; ++count) {
        if (link < 0x10000 + 0x10 || link >= 0x00007FFFFFFF0000ULL) return nullptr;
        const uintptr_t entry = link - 0x10;
        const uintptr_t base = *reinterpret_cast<const uintptr_t*>(entry + 0x30);
        const auto& path = *reinterpret_cast<const UserUnicodeString64*>(entry + 0x48);
        if (base && EqualModuleName(path.buffer, path.length, moduleName))
            return reinterpret_cast<void*>(base);
        link = reinterpret_cast<const UserListEntry64*>(link)->next;
    }
    return nullptr;
}

void* Kernel32_GetProcAddress(void* hModule, const char* lpProcName) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(hModule);
    const uintptr_t peb = CurrentPebAddress();
    if (!base || !lpProcName || !IsLoadedModule(peb, base)) return nullptr;
    const auto* image = reinterpret_cast<const uint8_t*>(base);
    if (*reinterpret_cast<const uint16_t*>(image) != 0x5a4d) return nullptr;
    const uint32_t peOffset = *reinterpret_cast<const uint32_t*>(image + 0x3c);
    if (peOffset > 0x100000 || peOffset > UINTPTR_MAX - 0x108) return nullptr;
    const auto* peHeader = image + peOffset;
    if (*reinterpret_cast<const uint32_t*>(peHeader) != 0x00004550) return nullptr;
    const uint16_t optionalMagic = *reinterpret_cast<const uint16_t*>(peHeader + 24);
    if (optionalMagic != 0x20b) return nullptr;
    const uint32_t imageSize = *reinterpret_cast<const uint32_t*>(peHeader + 24 + 56);
    const uint32_t directoryCount = *reinterpret_cast<const uint32_t*>(peHeader + 24 + 108);
    if (imageSize < 0x1000 || directoryCount == 0) return nullptr;
    const uint32_t exportRva = *reinterpret_cast<const uint32_t*>(peHeader + 24 + 112);
    const uint32_t exportSize = *reinterpret_cast<const uint32_t*>(peHeader + 24 + 116);
    if (!exportRva || exportSize < 40 || exportRva > imageSize || exportSize > imageSize - exportRva)
        return nullptr;
    const auto* exports = image + exportRva;
    const uint32_t ordinalBase = *reinterpret_cast<const uint32_t*>(exports + 16);
    const uint32_t functionCount = *reinterpret_cast<const uint32_t*>(exports + 20);
    const uint32_t nameCount = *reinterpret_cast<const uint32_t*>(exports + 24);
    const uint32_t functionsRva = *reinterpret_cast<const uint32_t*>(exports + 28);
    const uint32_t namesRva = *reinterpret_cast<const uint32_t*>(exports + 32);
    const uint32_t ordinalsRva = *reinterpret_cast<const uint32_t*>(exports + 36);
    if (!functionCount || functionCount > imageSize / 4 || nameCount > functionCount ||
        functionsRva > imageSize || functionCount * 4ULL > imageSize - functionsRva ||
        namesRva > imageSize || nameCount * 4ULL > imageSize - namesRva ||
        ordinalsRva > imageSize || nameCount * 2ULL > imageSize - ordinalsRva) return nullptr;
    uint32_t index = UINT32_MAX;
    const uintptr_t nameValue = reinterpret_cast<uintptr_t>(lpProcName);
    if (nameValue <= 0xffff) {
        const uint32_t ordinal = static_cast<uint32_t>(nameValue);
        if (ordinal < ordinalBase || ordinal - ordinalBase >= functionCount) return nullptr;
        index = ordinal - ordinalBase;
    } else {
        for (uint32_t i = 0; i < nameCount; ++i) {
            const uint32_t nameRva = *reinterpret_cast<const uint32_t*>(image + namesRva + i * 4ULL);
            if (!nameRva || nameRva >= imageSize) return nullptr;
            const char* candidate = reinterpret_cast<const char*>(image + nameRva);
            size_t j{};
            while (j < 512 && candidate[j] && lpProcName[j] && candidate[j] == lpProcName[j]) ++j;
            if (j < 512 && candidate[j] == 0 && lpProcName[j] == 0) {
                const uint16_t ordinalIndex = *reinterpret_cast<const uint16_t*>(image + ordinalsRva + i * 2ULL);
                if (ordinalIndex >= functionCount) return nullptr;
                index = ordinalIndex;
                break;
            }
        }
    }
    if (index == UINT32_MAX) return nullptr;
    const uint32_t functionRva = *reinterpret_cast<const uint32_t*>(image + functionsRva + index * 4ULL);
    if (!functionRva || functionRva >= imageSize ||
        (functionRva >= exportRva && functionRva - exportRva < exportSize)) return nullptr;
    return reinterpret_cast<void*>(base + functionRva);
}

int32_t Kernel32_FreeLibrary(void* hLibModule) noexcept {
    SetLastError(hLibModule ? 120U : 6U);
    return 0;
}

#if defined(MICANT_FREESTANDING_KERNEL32)
int32_t Kernel32_Beep(uint32_t frequency, uint32_t duration) noexcept {
    return ::Kernel32_BeepNative(frequency, duration);
}
#endif

void Kernel32_Sleep(uint32_t milliseconds) noexcept {
    if (milliseconds == 0) {
        (void)NtYieldExecution();
        return;
    }
    const int64_t interval = -static_cast<int64_t>(milliseconds) * 10000;
    (void)NtDelayExecution(0, &interval);
}
int32_t Kernel32_SwitchToThread() noexcept {
    const int32_t status = NtYieldExecution();
    return status >= 0 && static_cast<uint32_t>(status) != 0x40000024U;
}
uint32_t Kernel32_GetCurrentProcessId() noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    return teb ? *reinterpret_cast<const uint32_t*>(teb + 0x40) : 0;
}
uint32_t Kernel32_GetCurrentThreadId() noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    return teb ? *reinterpret_cast<const uint32_t*>(teb + 0x48) : 0;
}
uint32_t Kernel32_GetLastError() noexcept { return g_lastError; }
void Kernel32_SetLastError(uint32_t dwErrCode) noexcept { g_lastError = dwErrCode; }
int32_t Kernel32_SetThreadDescription(void* /*hThread*/, const wchar_t* /*lpThreadDescription*/) noexcept { return static_cast<int32_t>(0x80004001U); }


// advapi32.dll implementations
int32_t Advapi32_RegCreateKeyExW(
    void* hKey,
    const wchar_t* lpSubKey,
    uint32_t Reserved,
    wchar_t* lpClass,
    uint32_t dwOptions,
    uint32_t samDesired,
    void* lpSecurityAttributes,
    void** phkResult,
    uint32_t* lpdwDisposition) noexcept {
    (void)hKey; (void)lpSubKey; (void)lpClass; (void)samDesired; (void)lpSecurityAttributes;
    if (!phkResult || Reserved || dwOptions != 0) return 87;
    *phkResult = nullptr;
    if (lpdwDisposition) *lpdwDisposition = 0;
    return 120; // ERROR_CALL_NOT_IMPLEMENTED until NtCreateKey is implemented
}

int32_t Advapi32_RegOpenKeyExW(void* hKey, const wchar_t* lpSubKey, uint32_t ulOptions, uint32_t samDesired, void** phkResult) noexcept {
    constexpr uintptr_t HKEY_CURRENT_USER_VALUE = 0x80000001u;
    constexpr uintptr_t HKEY_LOCAL_MACHINE_VALUE = 0x80000002u;
    if (!phkResult || !hKey || ulOptions != 0) return 87;
    *phkResult = nullptr;
    wchar_t path[512];
    ByteZero(path, sizeof(path));
    size_t length{};
    const wchar_t* parent{};
    if (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hKey)) == HKEY_LOCAL_MACHINE_VALUE) parent = L"\\Registry\\Machine";
    else if (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hKey)) == HKEY_CURRENT_USER_VALUE) parent = L"\\Registry\\User";
    else {
        AdvapiRegistryKeyGuard guard;
        for (auto& key : g_regKeyPool) if (hKey == &key && key.active) { parent = key.path; break; }
        if (!parent) return 6;
    }
    for (; parent[length] && length + 1 < 512; ++length) path[length] = parent[length];
    if (parent[length]) return 206;
    if (lpSubKey && *lpSubKey) {
        if (length && path[length - 1] != L'\\') path[length++] = L'\\';
        for (size_t i = 0; lpSubKey[i]; ++i) {
            if (length + 1 >= 512) return 206;
            path[length++] = lpSubKey[i];
        }
    }
    path[length] = L'\0';
    micant::UnicodeString name(path);
    micant::ObjectAttributes attributes{};
    attributes.objectName = &name;
    attributes.attributes = micant::OBJ_CASE_INSENSITIVE;
    void* nativeHandle{};
    const int32_t status = NtOpenKey(&nativeHandle, samDesired, &attributes);
    if (status < 0) {
        if (status == static_cast<int32_t>(micant::NtStatus::ObjectNameNotFound)) return 2;
        if (status == static_cast<int32_t>(micant::NtStatus::AccessDenied)) return 5;
        if (status == static_cast<int32_t>(micant::NtStatus::InvalidParameter)) return 87;
        return 31;
    }
    {
        AdvapiRegistryKeyGuard guard;
        for (uint32_t i = 0; i < 32; ++i) {
            auto& key = g_regKeyPool[(g_regKeyPoolNext + i) & 31u];
            if (key.active) continue;
            key.handle = nativeHandle;
            for (size_t n = 0; n <= length; ++n) key.path[n] = path[n];
            key.active = true;
            g_regKeyPoolNext = (g_regKeyPoolNext + i + 1) & 31u;
            *phkResult = &key;
            return ERROR_SUCCESS_OFFICIAL;
        }
    }
    (void)NtClose(nativeHandle);
    return 8;
}

int32_t Advapi32_RegSetValueExW(void* hKey, const wchar_t* lpValueName, uint32_t reserved, uint32_t /*dwType*/, const uint8_t* /*lpData*/, uint32_t /*cbData*/) noexcept {
    if (!hKey || !lpValueName || reserved) return 87;
    return 120; // NtSetValueKey is not yet implemented in the production NTDLL path
}

// These operations are deliberately reported as unavailable until the CM
// syscall layer implements their documented key enumeration/deletion classes.
// Returning ERROR_CALL_NOT_IMPLEMENTED keeps import resolution honest: callers
// can load the DLL, but the operation itself never reports synthetic success.
int32_t Advapi32_RegDeleteKeyW(void* hKey, const wchar_t* subKey) noexcept {
    return (!hKey || !subKey) ? 87 : 120;
}
int32_t Advapi32_RegEnumKeyExW(void* hKey, uint32_t index, wchar_t* name,
    uint32_t* nameLength, uint32_t* reserved, wchar_t* className,
    uint32_t* classLength, void* lastWriteTime) noexcept {
    (void)index; (void)name; (void)nameLength; (void)reserved;
    (void)className; (void)classLength; (void)lastWriteTime;
    return hKey ? 120 : 87;
}
int32_t Advapi32_RegQueryInfoKeyW(void* hKey, wchar_t* className,
    uint32_t* classLength, uint32_t* reserved, uint32_t* subKeys,
    uint32_t* maxSubKeyLength, uint32_t* maxClassLength, uint32_t* values,
    uint32_t* maxValueNameLength, uint32_t* maxValueLength,
    uint32_t* securityDescriptorSize, void* lastWriteTime) noexcept {
    (void)className; (void)classLength; (void)reserved; (void)subKeys;
    (void)maxSubKeyLength; (void)maxClassLength; (void)values;
    (void)maxValueNameLength; (void)maxValueLength; (void)securityDescriptorSize;
    (void)lastWriteTime;
    return hKey ? 120 : 87;
}

int32_t Advapi32_RegQueryValueExW(void* hKey, const wchar_t* lpValueName, uint32_t* lpReserved, uint32_t* lpType, uint8_t* lpData, uint32_t* lpcbData) noexcept {
    if (!hKey || lpReserved || !lpcbData) return 87;
    void* nativeHandle{};
    {
        AdvapiRegistryKeyGuard guard;
        for (auto& key : g_regKeyPool) if (&key == hKey && key.active) { nativeHandle = key.handle; break; }
    }
    if (!nativeHandle) return 6;
    micant::UnicodeString valueName(lpValueName ? lpValueName : L"");
    struct KeyValuePartialInformation { uint32_t titleIndex, type, dataLength; uint8_t data[1]; };
    uint8_t information[4096];
    ByteZero(information, sizeof(information));
    size_t returned{};
    const int32_t status = NtQueryValueKey(nativeHandle, &valueName, 2,
        information, sizeof(information), &returned);
    if (status < 0) {
        if (status == static_cast<int32_t>(micant::NtStatus::ObjectNameNotFound)) return 2;
        if (status == static_cast<int32_t>(micant::NtStatus::BufferTooSmall)) return 122;
        if (status == static_cast<int32_t>(micant::NtStatus::AccessDenied)) return 5;
        if (status == static_cast<int32_t>(micant::NtStatus::InvalidHandle)) return 6;
        return 31;
    }
    const auto* value = reinterpret_cast<const KeyValuePartialInformation*>(information);
    if (lpType) *lpType = value->type;
    if (!lpData) { *lpcbData = value->dataLength; return ERROR_SUCCESS_OFFICIAL; }
    if (*lpcbData < value->dataLength) { *lpcbData = value->dataLength; return 122; }
    ByteCopy(lpData, value->data, value->dataLength);
    *lpcbData = value->dataLength;
    return ERROR_SUCCESS_OFFICIAL;
}

int32_t Advapi32_RegGetValueW(void* hkey, const wchar_t* lpSubKey, const wchar_t* lpValue, uint32_t dwFlags, uint32_t* pdwType, void* pvData, uint32_t* pcbData) noexcept {
    if ((lpSubKey && *lpSubKey) || dwFlags != 0) return 120;
    return Advapi32_RegQueryValueExW(hkey, lpValue, nullptr, pdwType, static_cast<uint8_t*>(pvData), pcbData);
}

int32_t Advapi32_RegCloseKey(void* hKey) noexcept {
    if (!hKey) return 87;
    void* nativeHandle{};
    {
        AdvapiRegistryKeyGuard guard;
        for (auto& key : g_regKeyPool) {
            if (&key == hKey && key.active) { nativeHandle = key.handle; key = {}; break; }
        }
    }
    if (!nativeHandle) return 6;
    return NtClose(nativeHandle) >= 0 ? ERROR_SUCCESS_OFFICIAL : 31;
}

int32_t Advapi32_GetUserNameW(wchar_t* lpBuffer, uint32_t* pcbBuffer) noexcept {
    (void)lpBuffer;
    (void)pcbBuffer;
    Kernel32_SetLastError(120); // ERROR_CALL_NOT_IMPLEMENTED
    return 0;
}

uint8_t Advapi32_SystemFunction036(void* RandomBuffer, uint32_t RandomBufferLength) noexcept {
    (void)RandomBuffer;
    (void)RandomBufferLength;
    return 0;
}

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
void WinNtKernel32Advapi32Coordinator::initialize() noexcept {
    if (m_initialized) return;
    m_cachedReport = runKernel32Advapi32Audit();
    m_initialized = true;
}

Horizon110Kernel32Advapi32AuditReport WinNtKernel32Advapi32Coordinator::runKernel32Advapi32Audit() noexcept {
    Horizon110Kernel32Advapi32AuditReport rep{};
    rep.kernel32ExportCount = 24;
    rep.advapi32ExportCount = 8;

    Kernel32_QueryPerformanceFrequency(&rep.qpcFrequencyHz);
    Kernel32_QueryPerformanceCounter(&rep.qpcCounterValue);
    rep.tickCount64Ms = Kernel32_GetTickCount64();

    void* hEvent = Kernel32_CreateEventW(nullptr, 0, 0, L"Local\\D3D12FenceCompletionEvent");
    if (hEvent) {
        Kernel32_SetEvent(hEvent);
        uint32_t waitRes = Kernel32_WaitForSingleObjectEx(hEvent, INFINITE_OFFICIAL, 0);
        rep.eventCreateSetWaitVerified = (waitRes == WAIT_OBJECT_0_OFFICIAL);
        Kernel32_CloseHandle(hEvent);
    }

    void* mem = Kernel32_VirtualAlloc(nullptr, 65536, MEM_COMMIT_OFFICIAL | MEM_RESERVE_OFFICIAL, PAGE_READWRITE_OFFICIAL);
    if (mem) {
        static_cast<uint32_t*>(mem)[0] = 0x12345678U;
        rep.virtualAllocFreeVerified = (static_cast<uint32_t*>(mem)[0] == 0x12345678U) &&
                                       (Kernel32_VirtualFree(mem, 0, MEM_RELEASE_OFFICIAL) == 1);
    }

    void* hKey = nullptr;
    if (Advapi32_RegOpenKeyExW(nullptr, L"SOFTWARE\\Microsoft\\DirectX", 0, 0x20019U, &hKey) == 0 && hKey) {
        uint32_t writeVal = 0x0001000CU;
        Advapi32_RegSetValueExW(hKey, L"InstalledVersion", 0, REG_DWORD_OFFICIAL, reinterpret_cast<const uint8_t*>(&writeVal), sizeof(writeVal));
        uint32_t readVal = 0;
        uint32_t cbData = sizeof(readVal);
        uint32_t regType = 0;
        if (Advapi32_RegQueryValueExW(hKey, L"InstalledVersion", nullptr, &regType, reinterpret_cast<uint8_t*>(&readVal), &cbData) == 0) {
            rep.registryRoundTripVerified = (readVal == writeVal && regType == REG_DWORD_OFFICIAL);
        }
        Advapi32_RegCloseKey(hKey);
    }

    uint8_t rngBuf[16]{};
    rep.rtlGenRandomVerified = (Advapi32_SystemFunction036(rngBuf, sizeof(rngBuf)) == 1) && (rngBuf[0] != 0 || rngBuf[1] != 0);

    return rep;
}

void InitializeWinNtKernel32Advapi32Subsystem() noexcept {
    WinNtKernel32Advapi32Coordinator::instance().initialize();
}
#endif

} // namespace micant::um::winnt_kernel32_advapi32_abi

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
namespace micant::sys::internal {
void WinNtKernel32Advapi32InitializeExecutiveSubsystem() noexcept {
    micant::um::winnt_kernel32_advapi32_abi::InitializeWinNtKernel32Advapi32Subsystem();
}
} // namespace micant::sys::internal
#endif

#if defined(BUILD_SYSTEM32_KERNEL32_DLL)
extern "C" {
struct Kernel32LargeInteger { uint64_t quadPart; };
__declspec(dllexport) void* __stdcall CreateFileMappingW(
    void* file, void*, uint32_t protection, uint32_t maximumHigh,
    uint32_t maximumLow, const wchar_t*) noexcept {
    Kernel32LargeInteger maximum{(static_cast<uint64_t>(maximumHigh) << 32) | maximumLow};
    void* section = nullptr;
    const int32_t status = NtCreateSection(&section, 0x000F001Fu, nullptr,
        (maximum.quadPart ? &maximum : nullptr), protection ? protection : 0x04u,
        0x08000000u, file == reinterpret_cast<void*>(static_cast<intptr_t>(-1)) ? nullptr : file);
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return nullptr; }
    return section;
}
__declspec(dllexport) void* __stdcall MapViewOfFile(
    void* section, uint32_t, uint32_t offsetHigh, uint32_t offsetLow,
    size_t requestedSize) noexcept {
    uintptr_t base = 0;
    size_t viewSize = requestedSize;
    Kernel32LargeInteger offset{(static_cast<uint64_t>(offsetHigh) << 32) | offsetLow};
    const int32_t status = NtMapViewOfSection(section,
        reinterpret_cast<void*>(static_cast<intptr_t>(-1)), &base, 0,
        requestedSize, &offset, &viewSize, 1, 0x3000u, 0x04u);
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return nullptr; }
    return reinterpret_cast<void*>(base);
}
__declspec(dllexport) int32_t __stdcall UnmapViewOfFile(const void* base) noexcept {
    if (!base) { SetLastError(87); return 0; }
    const int32_t status = NtUnmapViewOfSection(
        reinterpret_cast<void*>(static_cast<intptr_t>(-1)), reinterpret_cast<uintptr_t>(base));
    if (status < 0) { SetLastError(RtlNtStatusToDosError(status)); return 0; }
    return 1;
}
__declspec(dllexport) int32_t QueryPerformanceCounter(int64_t* lpPerformanceCount) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_QueryPerformanceCounter(lpPerformanceCount);
}
__declspec(dllexport) int32_t QueryPerformanceFrequency(int64_t* lpFrequency) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_QueryPerformanceFrequency(lpFrequency);
}
__declspec(dllexport) uint64_t GetTickCount64() {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_GetTickCount64();
}
__declspec(dllexport) void* CreateEventW(void* lpEventAttributes, int32_t bManualReset, int32_t bInitialState, const wchar_t* lpName) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_CreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName);
}
__declspec(dllexport) void* CreateEventExW(void* lpEventAttributes, const wchar_t* lpName, uint32_t dwFlags, uint32_t dwDesiredAccess) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_CreateEventExW(lpEventAttributes, lpName, dwFlags, dwDesiredAccess);
}
__declspec(dllexport) int32_t SetEvent(void* hEvent) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_SetEvent(hEvent);
}
__declspec(dllexport) int32_t ResetEvent(void* hEvent) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_ResetEvent(hEvent);
}
__declspec(dllexport) uint32_t WaitForSingleObject(void* hHandle, uint32_t dwMilliseconds) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_WaitForSingleObject(hHandle, dwMilliseconds);
}
__declspec(dllexport) uint32_t WaitForSingleObjectEx(void* hHandle, uint32_t dwMilliseconds, int32_t bAlertable) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_WaitForSingleObjectEx(hHandle, dwMilliseconds, bAlertable);
}
__declspec(dllexport) uint32_t WaitForMultipleObjectsEx(uint32_t nCount, const void* const* lpHandles, int32_t bWaitAll, uint32_t dwMilliseconds, int32_t bAlertable) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_WaitForMultipleObjectsEx(nCount, lpHandles, bWaitAll, dwMilliseconds, bAlertable);
}
__declspec(dllexport) int32_t CloseHandle(void* hObject) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_CloseHandle(hObject);
}
__declspec(dllexport) void* VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect);
}
__declspec(dllexport) int32_t VirtualFree(void* lpAddress, size_t dwSize, uint32_t dwFreeType) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_VirtualFree(lpAddress, dwSize, dwFreeType);
}
__declspec(dllexport) void* LoadLibraryW(const wchar_t* lpLibFileName) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_LoadLibraryW(lpLibFileName);
}
__declspec(dllexport) void* LoadLibraryExW(const wchar_t* lpLibFileName, void* hFile, uint32_t dwFlags) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}
__declspec(dllexport) void* GetModuleHandleW(const wchar_t* lpModuleName) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_GetModuleHandleW(lpModuleName);
}
__declspec(dllexport) void* GetProcAddress(void* hModule, const char* lpProcName) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_GetProcAddress(hModule, lpProcName);
}
__declspec(dllexport) int32_t Beep(uint32_t frequency, uint32_t duration) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_Beep(frequency, duration);
}
__declspec(dllexport) int32_t FreeLibrary(void* hLibModule) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_FreeLibrary(hLibModule);
}
__declspec(dllexport) void Sleep(uint32_t dwMilliseconds) {
    micant::um::winnt_kernel32_advapi32_abi::Kernel32_Sleep(dwMilliseconds);
}
__declspec(dllexport) int32_t SwitchToThread() {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_SwitchToThread();
}
__declspec(dllexport) uint32_t GetCurrentProcessId() {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_GetCurrentProcessId();
}
__declspec(dllexport) uint32_t GetCurrentThreadId() {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_GetCurrentThreadId();
}
__declspec(dllexport) uint32_t GetLastError() {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_GetLastError();
}
__declspec(dllexport) void SetLastError(uint32_t dwErrCode) {
    micant::um::winnt_kernel32_advapi32_abi::Kernel32_SetLastError(dwErrCode);
}
__declspec(dllexport) int32_t SetThreadDescription(void* hThread, const wchar_t* lpThreadDescription) {
    return micant::um::winnt_kernel32_advapi32_abi::Kernel32_SetThreadDescription(hThread, lpThreadDescription);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_ADVAPI32_DLL)
extern "C" {
__declspec(dllexport) int32_t IsTextUnicode(const void* buffer, int32_t byteCount,
                                             int32_t* resultFlags) {
    if (!buffer || byteCount < 0) { SetLastError(87); return 0; }
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    const size_t pairs = static_cast<size_t>(byteCount) / 2;
    size_t highZero{}, lowZero{};
    for (size_t i = 0; i < pairs; ++i) {
        highZero += bytes[i * 2 + 1] == 0;
        lowZero += bytes[i * 2] == 0;
    }
    int32_t flags{};
    if (pairs && highZero * 2 >= pairs) flags |= 0x0001 | 0x0002 | 0x0004;
    if (pairs && lowZero * 2 >= pairs) flags |= 0x0010 | 0x0020 | 0x0040;
    if (highZero || lowZero) flags |= 0x0008 | 0x0080;
    if (byteCount & 1) flags |= 0x0100;
    if (resultFlags) *resultFlags = flags;
    return pairs && highZero * 2 >= pairs && !(byteCount & 1) ? 1 : 0;
}
__declspec(dllexport) int32_t RegOpenKeyExW(void* hKey, const wchar_t* lpSubKey, uint32_t ulOptions, uint32_t samDesired, void** phkResult) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}
__declspec(dllexport) int32_t RegOpenKeyW(void* hKey, const wchar_t* subKey,
                                          void** result) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegOpenKeyExW(
        hKey, subKey, 0, 0x20019u, result);
}
__declspec(dllexport) int32_t RegCreateKeyExW(void* hKey, const wchar_t* lpSubKey, uint32_t Reserved, wchar_t* lpClass, uint32_t dwOptions, uint32_t samDesired, void* lpSecurityAttributes, void** phkResult, uint32_t* lpdwDisposition) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegCreateKeyExW(hKey, lpSubKey, Reserved, lpClass, dwOptions, samDesired, lpSecurityAttributes, phkResult, lpdwDisposition);
}
__declspec(dllexport) int32_t RegQueryValueExW(void* hKey, const wchar_t* lpValueName, uint32_t* lpReserved, uint32_t* lpType, uint8_t* lpData, uint32_t* lpcbData) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}
__declspec(dllexport) int32_t RegSetValueExW(void* hKey, const wchar_t* lpValueName, uint32_t Reserved, uint32_t dwType, const uint8_t* lpData, uint32_t cbData) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegSetValueExW(hKey, lpValueName, Reserved, dwType, lpData, cbData);
}
__declspec(dllexport) int32_t RegGetValueW(void* hkey, const wchar_t* lpSubKey, const wchar_t* lpValue, uint32_t dwFlags, uint32_t* pdwType, void* pvData, uint32_t* pcbData) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegGetValueW(hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData);
}
__declspec(dllexport) int32_t RegCloseKey(void* hKey) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegCloseKey(hKey);
}
__declspec(dllexport) int32_t RegDeleteKeyW(void* hKey, const wchar_t* subKey) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegDeleteKeyW(hKey, subKey);
}
__declspec(dllexport) int32_t RegEnumKeyExW(void* hKey, uint32_t index, wchar_t* name,
    uint32_t* nameLength, uint32_t* reserved, wchar_t* className,
    uint32_t* classLength, void* lastWriteTime) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegEnumKeyExW(hKey, index, name,
        nameLength, reserved, className, classLength, lastWriteTime);
}
__declspec(dllexport) int32_t RegQueryInfoKeyW(void* hKey, wchar_t* className,
    uint32_t* classLength, uint32_t* reserved, uint32_t* subKeys,
    uint32_t* maxSubKeyLength, uint32_t* maxClassLength, uint32_t* values,
    uint32_t* maxValueNameLength, uint32_t* maxValueLength,
    uint32_t* securityDescriptorSize, void* lastWriteTime) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegQueryInfoKeyW(hKey,
        className, classLength, reserved, subKeys, maxSubKeyLength, maxClassLength,
        values, maxValueNameLength, maxValueLength, securityDescriptorSize, lastWriteTime);
}
__declspec(dllexport) int32_t GetUserNameW(wchar_t* lpBuffer, uint32_t* pcbBuffer) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_GetUserNameW(lpBuffer, pcbBuffer);
}
__declspec(dllexport) uint8_t SystemFunction036(void* RandomBuffer, uint32_t RandomBufferLength) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_SystemFunction036(RandomBuffer, RandomBufferLength);
}
} // extern "C"
#endif

#endif
