#if defined(MICANT_FREESTANDING_KERNEL32)

#include <cstdint>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/native_process_abi.hpp"
#include "micant/shared/system_time.hpp"
#include "micant/um/subsystems/winbase/utf8_conversion.hpp"

static_assert(sizeof(void*) == 8);

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
__declspec(dllimport) int32_t __stdcall NtWaitForSingleObject(
    void* handle, uint8_t alertable, const int64_t* timeout) noexcept;
__declspec(dllexport) void __stdcall SetLastError(uint32_t error) noexcept;
__declspec(dllimport) int32_t __stdcall NtTerminateProcess(void* process, int32_t exitStatus) noexcept;
__declspec(dllimport) void __stdcall RtlZeroMemory(void* destination, size_t length) noexcept;
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
__declspec(dllimport) int32_t __stdcall NtCreateFile(
    micant::Handle* fileHandle, uint32_t desiredAccess,
    micant::ObjectAttributes* objectAttributes, micant::IoStatusBlock* ioStatusBlock,
    micant::LargeInteger* allocationSize, uint32_t fileAttributes,
    uint32_t shareAccess, uint32_t createDisposition, uint32_t createOptions,
    void* eaBuffer, uint32_t eaLength) noexcept;
__declspec(dllimport) int32_t __stdcall NtReadFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock, void* buffer,
    uint32_t length, micant::LargeInteger* byteOffset, uint32_t* key) noexcept;
__declspec(dllimport) int32_t __stdcall NtQueryInformationFile(
    micant::Handle fileHandle, micant::IoStatusBlock* ioStatusBlock,
    void* fileInformation, uint32_t length,
    micant::FileInformationClass fileInformationClass) noexcept;

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

__declspec(dllexport) void __stdcall SetLastError(uint32_t error) noexcept;

static uint32_t Win32ErrorFromNtStatus(int32_t status) noexcept {
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
    micant::UnicodeString name(fileName);
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
        SetLastError(Win32ErrorFromNtStatus(status));
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    return reinterpret_cast<void*>(handle);
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

__declspec(dllexport) void* __stdcall GetModuleHandleW(const wchar_t* name) noexcept {
    return reinterpret_cast<void*>(FindModule(name));
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

}

#else

/**
 * @file winnt_kernel32_advapi32_system32_abi.cpp
 * @brief Horizon 110 (Kernel Unit #82): Sovereign WinNT Core User-Mode System32 DLLs
 *        (kernel32.dll & advapi32.dll) Official PE32+ Exports, KUSER_SHARED_DATA 10MHz
 *        QPC Timer, D3D12Fence Event Synchronization, DynamicLoader, and Cm Registry.
 */

#include "micant/um/subsystems/winbase/winnt_kernel32_advapi32_system32_abi.hpp"

namespace micant::um::winnt_kernel32_advapi32_abi {

namespace {
uint32_t g_lastError = ERROR_SUCCESS_OFFICIAL;

WinNtEventHandleObject g_eventPool[32]{};
uint32_t g_eventPoolNext = 0;

WinNtRegistryKeyObject g_regKeyPool[32]{};
uint32_t g_regKeyPoolNext = 0;

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

void* Kernel32_CreateEventExW(void* /*lpEventAttributes*/, const wchar_t* lpName, uint32_t dwFlags, uint32_t /*dwDesiredAccess*/) noexcept {
    WinNtEventHandleObject* ev = &g_eventPool[(g_eventPoolNext++) & 31U];
    ev->magic       = 0x4D4E544556454E54ULL;
    ev->manualReset = (dwFlags & 0x00000001U) ? 1 : 0;
    ev->signaled    = (dwFlags & 0x00000002U) ? 1 : 0;
    ev->waitCount   = 0;
    if (lpName) {
        size_t i = 0;
        while (lpName[i] != L'\0' && i + 1 < 64) {
            ev->name[i] = lpName[i];
            ++i;
        }
        ev->name[i] = L'\0';
    } else {
        ev->name[0] = L'\0';
    }
    return ev;
}

void* Kernel32_CreateEventW(void* lpEventAttributes, int32_t bManualReset, int32_t bInitialState, const wchar_t* lpName) noexcept {
    uint32_t flags = (bManualReset ? 0x1U : 0U) | (bInitialState ? 0x2U : 0U);
    return Kernel32_CreateEventExW(lpEventAttributes, lpName, flags, 0x1F0003U);
}

int32_t Kernel32_SetEvent(void* hEvent) noexcept {
    if (!hEvent) return 0;
    auto* ev = static_cast<WinNtEventHandleObject*>(hEvent);
    if (ev->magic != 0x4D4E544556454E54ULL) return 0;
    ev->signaled = 1;
    return 1;
}

int32_t Kernel32_ResetEvent(void* hEvent) noexcept {
    if (!hEvent) return 0;
    auto* ev = static_cast<WinNtEventHandleObject*>(hEvent);
    if (ev->magic != 0x4D4E544556454E54ULL) return 0;
    ev->signaled = 0;
    return 1;
}

uint32_t Kernel32_WaitForSingleObjectEx(void* hHandle, uint32_t /*dwMilliseconds*/, int32_t /*bAlertable*/) noexcept {
    if (!hHandle) return 0xFFFFFFFFU;
    auto* ev = static_cast<WinNtEventHandleObject*>(hHandle);
    if (ev->magic != 0x4D4E544556454E54ULL) return WAIT_OBJECT_0_OFFICIAL;
    ev->waitCount++;
    if (ev->signaled) {
        if (!ev->manualReset) {
            ev->signaled = 0;
        }
        return WAIT_OBJECT_0_OFFICIAL;
    }
    return WAIT_TIMEOUT_OFFICIAL;
}

uint32_t Kernel32_WaitForSingleObject(void* hHandle, uint32_t dwMilliseconds) noexcept {
    return Kernel32_WaitForSingleObjectEx(hHandle, dwMilliseconds, 0);
}

uint32_t Kernel32_WaitForMultipleObjectsEx(uint32_t nCount, const void* const* lpHandles, int32_t /*bWaitAll*/, uint32_t dwMilliseconds, int32_t bAlertable) noexcept {
    if (nCount == 0 || !lpHandles) return 0xFFFFFFFFU;
    return Kernel32_WaitForSingleObjectEx(const_cast<void*>(lpHandles[0]), dwMilliseconds, bAlertable);
}

int32_t Kernel32_CloseHandle(void* hObject) noexcept {
    if (!hObject) return 0;
    auto* ev = static_cast<WinNtEventHandleObject*>(hObject);
    if (ev->magic == 0x4D4E544556454E54ULL) {
        ev->magic = 0;
    }
    return 1;
}

void* Kernel32_VirtualAlloc(void* /*lpAddress*/, size_t dwSize, uint32_t /*flAllocationType*/, uint32_t /*flProtect*/) noexcept {
    if (dwSize == 0 || dwSize > sizeof(g_virtualAllocArena)) return nullptr;
    ByteZero(g_virtualAllocArena, dwSize);
    return g_virtualAllocArena;
}

int32_t Kernel32_VirtualFree(void* lpAddress, size_t /*dwSize*/, uint32_t dwFreeType) noexcept {
    if (!lpAddress) return 0;
    return (dwFreeType == MEM_RELEASE_OFFICIAL) ? 1 : 0;
}

void* Kernel32_LoadLibraryExW(const wchar_t* lpLibFileName, void* /*hFile*/, uint32_t /*dwFlags*/) noexcept {
    if (!lpLibFileName) return nullptr;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x00007FF810000000ULL));
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
    return hLibModule ? 1 : 0;
}

void Kernel32_Sleep(uint32_t milliseconds) noexcept {
    if (milliseconds == 0) {
        (void)NtYieldExecution();
        return;
    }
    const int64_t interval = -static_cast<int64_t>(milliseconds) * 10000;
    (void)NtDelayExecution(0, &interval);
}
int32_t Kernel32_SwitchToThread() noexcept { return 1; }
uint32_t Kernel32_GetCurrentProcessId() noexcept { return 4096U; }
uint32_t Kernel32_GetCurrentThreadId() noexcept { return 4100U; }
uint32_t Kernel32_GetLastError() noexcept { return g_lastError; }
void Kernel32_SetLastError(uint32_t dwErrCode) noexcept { g_lastError = dwErrCode; }
int32_t Kernel32_SetThreadDescription(void* /*hThread*/, const wchar_t* /*lpThreadDescription*/) noexcept { return 0; }

// advapi32.dll implementations
int32_t Advapi32_RegCreateKeyExW(
    void* /*hKey*/,
    const wchar_t* lpSubKey,
    uint32_t /*Reserved*/,
    wchar_t* /*lpClass*/,
    uint32_t /*dwOptions*/,
    uint32_t samDesired,
    void* /*lpSecurityAttributes*/,
    void** phkResult,
    uint32_t* lpdwDisposition) noexcept {
    if (!phkResult) return 87; // ERROR_INVALID_PARAMETER
    WinNtRegistryKeyObject* keyObj = &g_regKeyPool[(g_regKeyPoolNext++) & 31U];
    keyObj->magic          = 0x4D4E545245474B59ULL;
    keyObj->samDesired     = samDesired;
    keyObj->lastDwordValue = 1U;
    if (lpSubKey) {
        size_t i = 0;
        while (lpSubKey[i] != L'\0' && i + 1 < 128) {
            keyObj->subKey[i] = lpSubKey[i];
            ++i;
        }
        keyObj->subKey[i] = L'\0';
    } else {
        keyObj->subKey[0] = L'\0';
    }
    *phkResult = keyObj;
    if (lpdwDisposition) *lpdwDisposition = 1U; // REG_CREATED_NEW_KEY
    return ERROR_SUCCESS_OFFICIAL;
}

int32_t Advapi32_RegOpenKeyExW(void* hKey, const wchar_t* lpSubKey, uint32_t /*ulOptions*/, uint32_t samDesired, void** phkResult) noexcept {
    return Advapi32_RegCreateKeyExW(hKey, lpSubKey, 0, nullptr, 0, samDesired, nullptr, phkResult, nullptr);
}

int32_t Advapi32_RegSetValueExW(void* hKey, const wchar_t* /*lpValueName*/, uint32_t /*Reserved*/, uint32_t dwType, const uint8_t* lpData, uint32_t cbData) noexcept {
    if (!hKey || !lpData) return 87;
    auto* keyObj = static_cast<WinNtRegistryKeyObject*>(hKey);
    if (keyObj->magic == 0x4D4E545245474B59ULL && dwType == REG_DWORD_OFFICIAL && cbData >= sizeof(uint32_t)) {
        ByteCopy(&keyObj->lastDwordValue, lpData, sizeof(uint32_t));
    }
    return ERROR_SUCCESS_OFFICIAL;
}

int32_t Advapi32_RegQueryValueExW(void* hKey, const wchar_t* /*lpValueName*/, uint32_t* /*lpReserved*/, uint32_t* lpType, uint8_t* lpData, uint32_t* lpcbData) noexcept {
    if (!hKey) return 87;
    auto* keyObj = static_cast<WinNtRegistryKeyObject*>(hKey);
    if (lpType) *lpType = REG_DWORD_OFFICIAL;
    if (lpData && lpcbData && *lpcbData >= sizeof(uint32_t)) {
        uint32_t val = (keyObj->magic == 0x4D4E545245474B59ULL) ? keyObj->lastDwordValue : 1U;
        ByteCopy(lpData, &val, sizeof(uint32_t));
    }
    if (lpcbData) *lpcbData = sizeof(uint32_t);
    return ERROR_SUCCESS_OFFICIAL;
}

int32_t Advapi32_RegGetValueW(void* hkey, const wchar_t* /*lpSubKey*/, const wchar_t* lpValue, uint32_t /*dwFlags*/, uint32_t* pdwType, void* pvData, uint32_t* pcbData) noexcept {
    return Advapi32_RegQueryValueExW(hkey, lpValue, nullptr, pdwType, static_cast<uint8_t*>(pvData), pcbData);
}

int32_t Advapi32_RegCloseKey(void* hKey) noexcept {
    if (!hKey) return 87;
    auto* keyObj = static_cast<WinNtRegistryKeyObject*>(hKey);
    if (keyObj->magic == 0x4D4E545245474B59ULL) {
        keyObj->magic = 0;
    }
    return ERROR_SUCCESS_OFFICIAL;
}

int32_t Advapi32_GetUserNameW(wchar_t* lpBuffer, uint32_t* pcbBuffer) noexcept {
    constexpr wchar_t kUser[] = L"MicaNTPlayer";
    constexpr uint32_t kLen = sizeof(kUser) / sizeof(wchar_t);
    if (!pcbBuffer) return 0;
    if (lpBuffer && *pcbBuffer >= kLen) {
        ByteCopy(lpBuffer, kUser, sizeof(kUser));
    }
    *pcbBuffer = kLen;
    return 1;
}

uint8_t Advapi32_SystemFunction036(void* RandomBuffer, uint32_t RandomBufferLength) noexcept {
    if (!RandomBuffer && RandomBufferLength > 0) return 0;
    auto* bytes = static_cast<uint8_t*>(RandomBuffer);
    uint32_t state = 0xA55A3C69U;
    for (uint32_t i = 0; i < RandomBufferLength; ++i) {
        state = state * 1664525U + 1013904223U;
        bytes[i] = static_cast<uint8_t>((state >> 16) & 0xFFU);
    }
    return 1;
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
__declspec(dllexport) int32_t RegOpenKeyExW(void* hKey, const wchar_t* lpSubKey, uint32_t ulOptions, uint32_t samDesired, void** phkResult) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
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
__declspec(dllexport) int32_t GetUserNameW(wchar_t* lpBuffer, uint32_t* pcbBuffer) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_GetUserNameW(lpBuffer, pcbBuffer);
}
__declspec(dllexport) uint8_t SystemFunction036(void* RandomBuffer, uint32_t RandomBufferLength) {
    return micant::um::winnt_kernel32_advapi32_abi::Advapi32_SystemFunction036(RandomBuffer, RandomBufferLength);
}
} // extern "C"
#endif

#endif
