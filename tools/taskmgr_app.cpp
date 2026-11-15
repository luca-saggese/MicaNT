/**
 * @file taskmgr_app.cpp
 * @brief Ring 3 process snapshot viewer for MicaNT.
 *
 * Process identifiers and counts come from NtQuerySystemInformation. Fields
 * not currently exported by the kernel are shown as unavailable.
 */

#include "gui_common.hpp"
#include "micant/shared/system_information.h"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/syscall_numbers.hpp"
#include "micant/shared/win32k_client.h"

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(value);
    return destination;
}

namespace {
struct TaskmgrMessage {
    void* hwnd{};
    uint32_t message{};
    uintptr_t wParam{};
    intptr_t lParam{};
    uint32_t time{};
    MicaWin32kPoint point{};
};
struct TaskmgrPaintStruct {
    void* hdc{};
    int32_t erase{};
    MicaWin32kRect paintRect{};
    int32_t restore{};
    int32_t incremental{};
    uint8_t reserved[32]{};
};
struct TaskmgrBitmapInfo {
    MicaWin32kBitmapInfoHeader header{};
    uint32_t color{};
};
}

extern "C" {
__declspec(dllimport) uint16_t RegisterClassExW(const MicaWin32kWindowClassEx*);
__declspec(dllimport) void* CreateWindowExW(uint32_t, const wchar_t*, const wchar_t*,
    uint32_t, int32_t, int32_t, int32_t, int32_t, void*, void*, void*, void*);
__declspec(dllimport) int32_t DestroyWindow(void*);
__declspec(dllimport) int32_t ShowWindow(void*, int32_t);
__declspec(dllimport) int32_t UpdateWindow(void*);
__declspec(dllimport) int32_t InvalidateRect(void*, const MicaWin32kRect*, int32_t);
__declspec(dllimport) void* BeginPaint(void*, TaskmgrPaintStruct*);
__declspec(dllimport) int32_t EndPaint(void*, const TaskmgrPaintStruct*);
__declspec(dllimport) intptr_t DefWindowProcW(void*, uint32_t, uintptr_t, intptr_t);
__declspec(dllimport) int32_t PeekMessageW(TaskmgrMessage*, void*, uint32_t,
    uint32_t, uint32_t);
__declspec(dllimport) int32_t TranslateMessage(const TaskmgrMessage*);
__declspec(dllimport) intptr_t DispatchMessageW(const TaskmgrMessage*);
__declspec(dllimport) void PostQuitMessage(int32_t);
__declspec(dllimport) int32_t PostMessageW(void*, uint32_t, uintptr_t, intptr_t);
__declspec(dllimport) int32_t GetClientRect(void*, MicaWin32kRect*);
__declspec(dllimport) void* CreateCompatibleDC(void*);
__declspec(dllimport) int32_t DeleteDC(void*);
__declspec(dllimport) void* CreateDIBSection(void*, const TaskmgrBitmapInfo*, uint32_t,
    void**, void*, uint32_t);
__declspec(dllimport) int32_t BitBlt(void*, int32_t, int32_t, int32_t, int32_t,
    void*, int32_t, int32_t, uint32_t);
__declspec(dllimport) void* SelectObject(void*, void*);
__declspec(dllimport) int32_t DeleteObject(void*);
__declspec(dllimport) int32_t SetWindowPos(void*, void*, int32_t, int32_t,
    int32_t, int32_t, uint32_t);
}

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
constexpr uint32_t MaxRenderWidth = 1024;
constexpr uint32_t MaxRenderHeight = 768;
constexpr uint32_t SystemProcessInformationClass = 5;
constexpr uint32_t SystemPerformanceInformationClass = 2;
constexpr uint32_t SystemProcessorPerformanceInformationClass = 8;
constexpr uint64_t NtQuerySystemInformationService = 0x36;
constexpr uint32_t StatusSuccess = 0x00000000U;
constexpr uint32_t StatusInfoLengthMismatch = 0xC0000004U;
constexpr uint32_t MaxSnapshotBytes = 64 * 1024;
constexpr uint32_t MaxTrackedProcesses =
    MaxSnapshotBytes / sizeof(micant::SystemProcessInformation);
alignas(4096) uint32_t s_tmBuffer[MaxRenderWidth * MaxRenderHeight];
alignas(16) uint8_t s_processSnapshot[MaxSnapshotBytes];
constexpr uint32_t MaxProcessorRecords = 64;
alignas(16) micant::SystemProcessorPerformanceInformation s_processorSnapshot[MaxProcessorRecords];

struct ProcessCpuSample {
    uintptr_t processId{};
    uint64_t userTime100ns{};
    uint64_t kernelTime100ns{};
};

ProcessCpuSample s_previousProcessSamples[MaxTrackedProcesses]{};
ProcessCpuSample s_currentProcessSamples[MaxTrackedProcesses]{};
uint32_t s_previousProcessCount{};

struct ProcessorTimes {
    uint64_t idleTime100ns{};
    uint64_t kernelTime100ns{};
    uint64_t userTime100ns{};
    uint32_t processorCount{};
    bool available{};
};

struct SnapshotView {
    const uint8_t* data{nullptr};
    uint32_t length{0};
    uint32_t processCount{0};
    uint32_t status{StatusInfoLengthMismatch};
    uint32_t availablePages{0};
    uint32_t cpuPercent{0};
    uint32_t processCpuPercent[MaxTrackedProcesses]{};
    uint8_t processCpuAvailable[MaxTrackedProcesses]{};
    bool taskmgrImageNameFound{false};
    bool available{false};
    bool memoryAvailable{false};
    bool cpuAvailable{false};
};

SnapshotView* s_liveSnapshot{};
void* s_taskmgrWindow{};
bool s_windowPaintReported{};
bool s_windowPaintEnteredReported{};
bool s_windowPaintRenderedReported{};
bool s_windowPaintFailureReported{};
bool s_windowSizeReported{};
bool s_windowCloseReported{};
uint32_t s_activePage{};
uint32_t s_renderWidth{WIN_W};
uint32_t s_renderHeight{WIN_H};
uintptr_t s_selectedProcessId{};
char s_searchText[32]{};
uint32_t s_searchLength{};
bool s_searchFocused{};
bool s_confirmEndTask{};
bool s_endTaskWaiting{};
char s_endTaskStatus[64]{};

void SetEndTaskStatus(const char* message) noexcept {
    uint32_t index = 0;
    for (; message[index] && index + 1 < sizeof(s_endTaskStatus); ++index)
        s_endTaskStatus[index] = message[index];
    s_endTaskStatus[index] = 0;
}

bool QueryProcessorTimes(ProcessorTimes& result) noexcept {
    result = {};
    uint32_t returnLength = 0;
    const auto status = static_cast<uint32_t>(micant::sys::invokeUserService4(
        NtQuerySystemInformationService,
        SystemProcessorPerformanceInformationClass,
        reinterpret_cast<uint64_t>(s_processorSnapshot),
        sizeof(s_processorSnapshot),
        reinterpret_cast<uint64_t>(&returnLength)));
    if (status != StatusSuccess || !returnLength ||
        returnLength > sizeof(s_processorSnapshot) ||
        returnLength % sizeof(micant::SystemProcessorPerformanceInformation) != 0)
        return false;
    const uint32_t count = returnLength / sizeof(micant::SystemProcessorPerformanceInformation);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& item = s_processorSnapshot[i];
        if (item.IdleTime < 0 || item.KernelTime < 0 || item.UserTime < 0) return false;
        const uint64_t idle = static_cast<uint64_t>(item.IdleTime);
        const uint64_t kernel = static_cast<uint64_t>(item.KernelTime);
        const uint64_t user = static_cast<uint64_t>(item.UserTime);
        if (idle > UINT64_MAX - result.idleTime100ns ||
            kernel > UINT64_MAX - result.kernelTime100ns ||
            user > UINT64_MAX - result.userTime100ns)
            return false;
        result.idleTime100ns += idle;
        result.kernelTime100ns += kernel;
        result.userTime100ns += user;
    }
    result.processorCount = count;
    result.available = true;
    return true;
}

bool ComputeCpuPercent(const ProcessorTimes& previous, const ProcessorTimes& current,
                       uint32_t& percent) noexcept {
    percent = 0;
    if (!previous.available || !current.available ||
        previous.processorCount != current.processorCount ||
        current.idleTime100ns < previous.idleTime100ns ||
        current.kernelTime100ns < previous.kernelTime100ns ||
        current.userTime100ns < previous.userTime100ns)
        return false;
    const uint64_t idleDelta = current.idleTime100ns - previous.idleTime100ns;
    const uint64_t kernelDelta = current.kernelTime100ns - previous.kernelTime100ns;
    const uint64_t userDelta = current.userTime100ns - previous.userTime100ns;
    if (idleDelta > kernelDelta || kernelDelta > UINT64_MAX - userDelta) return false;
    const uint64_t total = kernelDelta + userDelta;
    if (!total || kernelDelta - idleDelta > UINT64_MAX - userDelta) return false;
    const uint64_t busy = kernelDelta - idleDelta + userDelta;
    if (busy > total) return false;

    // Compute floor(100 * busy / total) by bounded repeated addition, avoiding
    // overflow in the multiplication even after long uptime.
    uint64_t remainder = 0;
    uint32_t value = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        if (remainder >= total - busy) {
            remainder -= total - busy;
            ++value;
        } else {
            remainder += busy;
        }
    }
    percent = value;
    return true;
}

bool ComputeRatioPercent(uint64_t numerator, uint64_t denominator,
                         uint32_t& percent) noexcept {
    percent = 0;
    if (!denominator || numerator > denominator) return false;
    uint64_t remainder = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        const uint64_t complement = denominator - numerator;
        if (remainder >= complement) {
            remainder -= complement;
            ++percent;
        } else {
            remainder += numerator;
        }
    }
    return true;
}

void UpdateProcessCpuSamples(SnapshotView& snapshot,
                             const ProcessorTimes& previousCpu,
                             const ProcessorTimes& currentCpu,
                             bool havePreviousCpu) noexcept {
    for (uint32_t i = 0; i < MaxTrackedProcesses; ++i) {
        snapshot.processCpuAvailable[i] = 0;
        snapshot.processCpuPercent[i] = 0;
    }
    if (!snapshot.available || snapshot.processCount > MaxTrackedProcesses) {
        s_previousProcessCount = 0;
        return;
    }

    uint64_t totalDelta = 0;
    bool validTotal = havePreviousCpu && previousCpu.available && currentCpu.available &&
        previousCpu.processorCount == currentCpu.processorCount &&
        currentCpu.kernelTime100ns >= previousCpu.kernelTime100ns &&
        currentCpu.userTime100ns >= previousCpu.userTime100ns;
    if (validTotal) {
        const uint64_t kernelDelta = currentCpu.kernelTime100ns - previousCpu.kernelTime100ns;
        const uint64_t userDelta = currentCpu.userTime100ns - previousCpu.userTime100ns;
        validTotal = kernelDelta <= UINT64_MAX - userDelta;
        if (validTotal) totalDelta = kernelDelta + userDelta;
        validTotal = validTotal && totalDelta != 0;
    }

    uint32_t offset = 0;
    for (uint32_t i = 0; i < snapshot.processCount; ++i) {
        const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(
            snapshot.data + offset);
        auto& current = s_currentProcessSamples[i];
        current = {entry->UniqueProcessId,
                   entry->UserTime >= 0 ? static_cast<uint64_t>(entry->UserTime) : 0,
                   entry->KernelTime >= 0 ? static_cast<uint64_t>(entry->KernelTime) : 0};
        if (validTotal && entry->UserTime >= 0 && entry->KernelTime >= 0) {
            for (uint32_t p = 0; p < s_previousProcessCount; ++p) {
                const auto& old = s_previousProcessSamples[p];
                if (old.processId != current.processId ||
                    current.userTime100ns < old.userTime100ns ||
                    current.kernelTime100ns < old.kernelTime100ns) continue;
                const uint64_t userDelta = current.userTime100ns - old.userTime100ns;
                const uint64_t kernelDelta = current.kernelTime100ns - old.kernelTime100ns;
                if (kernelDelta > UINT64_MAX - userDelta) break;
                uint32_t value{};
                if (ComputeRatioPercent(userDelta + kernelDelta, totalDelta, value)) {
                    snapshot.processCpuPercent[i] = value;
                    snapshot.processCpuAvailable[i] = 1;
                }
                break;
            }
        }
        if (entry->NextEntryOffset == 0) break;
        offset += entry->NextEntryOffset;
    }
    for (uint32_t i = 0; i < snapshot.processCount; ++i)
        s_previousProcessSamples[i] = s_currentProcessSamples[i];
    s_previousProcessCount = snapshot.processCount;
}

bool QueryAvailablePages(SnapshotView& result) noexcept {
    result.memoryAvailable = false;
    micant::SystemPerformanceInformation information{};
    uint32_t returnLength = 0;
    const auto status = static_cast<uint32_t>(micant::sys::invokeUserService4(
        NtQuerySystemInformationService,
        SystemPerformanceInformationClass,
        reinterpret_cast<uint64_t>(&information),
        sizeof(information),
        reinterpret_cast<uint64_t>(&returnLength)));
    if (status != StatusSuccess || returnLength != sizeof(information)) return false;
    result.availablePages = information.AvailablePages;
    result.memoryAvailable = true;
    return true;
}

bool IsTaskmgrImageName(const micant::SystemProcessInformation& entry,
                        const uint8_t* snapshot, uint32_t snapshotLength) noexcept {
    constexpr char16_t expected[] = u"taskmgr.exe";
    if (!entry.ImageName.Buffer || entry.ImageName.Length != (sizeof(expected) - sizeof(char16_t)) ||
        (entry.ImageName.Length & 1U) != 0) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(snapshot);
    const uintptr_t buffer = entry.ImageName.Buffer;
    if (buffer < base || buffer - base > snapshotLength ||
        entry.ImageName.Length > snapshotLength - (buffer - base) ||
        (buffer & (alignof(char16_t) - 1))) return false;
    const auto* name = reinterpret_cast<const char16_t*>(buffer);
    for (uint32_t i = 0; i < sizeof(expected) / sizeof(expected[0]) - 1; ++i) {
        char16_t actual = name[i];
        if (actual >= u'A' && actual <= u'Z') actual += u'a' - u'A';
        if (actual != expected[i]) return false;
    }
    return true;
}

uint32_t TerminateProcessById(micant::Handle processId) noexcept {
    micant::ClientId clientId{processId, 0};
    micant::ObjectAttributes attributes{};
    attributes.length = sizeof(attributes);
    micant::Handle processHandle{};
    constexpr uint32_t ProcessTerminateAccess = 0x0001;
    const uint32_t openStatus = static_cast<uint32_t>(micant::sys::invokeUserService4(
        MICANT_SSN_NT_OPEN_PROCESS,
        reinterpret_cast<uint64_t>(&processHandle),
        ProcessTerminateAccess,
        reinterpret_cast<uint64_t>(&attributes),
        reinterpret_cast<uint64_t>(&clientId)));
    if (openStatus != StatusSuccess || !processHandle) return openStatus;
    const uint32_t status = static_cast<uint32_t>(micant::sys::invokeUserService(
        0x002C, processHandle, 0)); // NtTerminateProcess
    (void)micant::sys::invokeUserService(0x000F, processHandle); // NtClose
    return status;
}

#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
bool ImageNameEquals(const micant::SystemProcessInformation& entry,
                     const uint8_t* snapshot, uint32_t snapshotLength,
                     const char16_t* expected) noexcept {
    if (!entry.ImageName.Buffer || !entry.ImageName.Length ||
        (entry.ImageName.Length & 1U) != 0) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(snapshot);
    const uintptr_t buffer = entry.ImageName.Buffer;
    if (buffer < base || buffer - base > snapshotLength ||
        entry.ImageName.Length > snapshotLength - (buffer - base) ||
        (buffer & (alignof(char16_t) - 1))) return false;
    const auto* name = reinterpret_cast<const char16_t*>(buffer);
    const size_t length = entry.ImageName.Length / sizeof(char16_t);
    size_t expectedLength = 0;
    while (expected[expectedLength]) ++expectedLength;
    if (length != expectedLength) return false;
    for (size_t i = 0; i < length; ++i) {
        char16_t actual = name[i];
        char16_t wanted = expected[i];
        if (actual >= u'A' && actual <= u'Z') actual += u'a' - u'A';
        if (wanted >= u'A' && wanted <= u'Z') wanted += u'a' - u'A';
        if (actual != wanted) return false;
    }
    return true;
}

bool FindTaskmgrProbeProcess(const SnapshotView& snapshot, micant::Handle& processId,
                             uint64_t* userTime100ns = nullptr,
                             uint64_t* kernelTime100ns = nullptr,
                             bool* threadTotalsMatch = nullptr) noexcept {
    processId = 0;
    if (threadTotalsMatch) *threadTotalsMatch = false;
    if (!snapshot.available) return false;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < snapshot.processCount; ++i) {
        const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(
            snapshot.data + offset);
        if (ImageNameEquals(*entry, snapshot.data, snapshot.length, u"taskmgr_probe.exe")) {
            processId = static_cast<micant::Handle>(entry->UniqueProcessId);
            if (entry->UserTime < 0 || entry->KernelTime < 0) return false;
            const uint64_t processUser = static_cast<uint64_t>(entry->UserTime);
            const uint64_t processKernel = static_cast<uint64_t>(entry->KernelTime);
            if (userTime100ns) *userTime100ns = processUser;
            if (kernelTime100ns) *kernelTime100ns = processKernel;
            uint64_t threadUser = 0, threadKernel = 0;
            bool validThreadTimes = true;
            const auto* threads = reinterpret_cast<const micant::SystemThreadInformation*>(
                reinterpret_cast<const uint8_t*>(entry) + sizeof(*entry));
            for (uint32_t t = 0; t < entry->NumberOfThreads; ++t) {
                if (threads[t].UserTime < 0 || threads[t].KernelTime < 0 ||
                    static_cast<uint64_t>(threads[t].UserTime) > UINT64_MAX - threadUser ||
                    static_cast<uint64_t>(threads[t].KernelTime) > UINT64_MAX - threadKernel) {
                    validThreadTimes = false;
                    break;
                }
                threadUser += static_cast<uint64_t>(threads[t].UserTime);
                threadKernel += static_cast<uint64_t>(threads[t].KernelTime);
            }
            if (threadTotalsMatch)
                *threadTotalsMatch = validThreadTimes && threadUser == processUser &&
                    threadKernel == processKernel;
            return processId != 0;
        }
        if (!entry->NextEntryOffset) break;
        offset += entry->NextEntryOffset;
    }
    return false;
}

bool TaskmgrProbeProcessExists(const SnapshotView& snapshot) noexcept {
    micant::Handle processId{};
    return FindTaskmgrProbeProcess(snapshot, processId);
}

uint32_t TerminateTaskmgrProbe(micant::Handle processId) noexcept {
    return TerminateProcessById(processId);
}
#endif

uint32_t QueryProcessSnapshot(SnapshotView& result) noexcept {
    result.available = false;
    result.taskmgrImageNameFound = false;
    uint32_t capacity = 4096;
    uint32_t returnLength = 0;
    uint32_t status = StatusInfoLengthMismatch;

    // Retry a bounded number of times as the process set grows or the buffer
    // size is reported. Never read beyond the fixed user-mode allocation.
    for (uint32_t attempt = 0; attempt < 8; ++attempt) {
        returnLength = 0;
        status = static_cast<uint32_t>(micant::sys::invokeUserService4(
            NtQuerySystemInformationService,
            SystemProcessInformationClass,
            reinterpret_cast<uint64_t>(s_processSnapshot),
            capacity,
            reinterpret_cast<uint64_t>(&returnLength)));

        if (status == StatusSuccess) {
            if (returnLength == 0 || returnLength > capacity) break;

            uint32_t offset = 0;
            uint32_t count = 0;
            bool valid = true;
            bool terminated = false;
            bool taskmgrImageNameFound = false;
            constexpr uint32_t processPrefixSize = sizeof(micant::SystemProcessInformation);
            for (uint32_t visited = 0; visited < MaxSnapshotBytes / processPrefixSize; ++visited) {
                if (offset > returnLength || returnLength - offset < processPrefixSize) {
                    valid = false;
                    break;
                }
                const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(s_processSnapshot + offset);
                const uint32_t recordLength = entry->NextEntryOffset == 0
                    ? returnLength - offset : entry->NextEntryOffset;
                const uint64_t requiredRecordLength = static_cast<uint64_t>(processPrefixSize) +
                    static_cast<uint64_t>(entry->NumberOfThreads) * sizeof(micant::SystemThreadInformation);
                if (recordLength < requiredRecordLength || recordLength > returnLength - offset) {
                    valid = false;
                    break;
                }
                taskmgrImageNameFound = taskmgrImageNameFound ||
                    IsTaskmgrImageName(*entry, s_processSnapshot, returnLength);
                ++count;
                if (entry->NextEntryOffset == 0) {
                    terminated = true;
                    break;
                }
                if (entry->NextEntryOffset < requiredRecordLength ||
                    (entry->NextEntryOffset & (alignof(uintptr_t) - 1)) != 0 ||
                    entry->NextEntryOffset > returnLength - offset) {
                    valid = false;
                    break;
                }
                offset += entry->NextEntryOffset;
                if (offset == returnLength) {
                    valid = false;
                    break;
                }
            }
            if (!valid || !terminated || count == 0) break;

            result.data = s_processSnapshot;
            result.length = returnLength;
            result.processCount = count;
            result.taskmgrImageNameFound = taskmgrImageNameFound;
            result.status = status;
            result.available = true;
            return status;
        }

        if (status != StatusInfoLengthMismatch) break;
        uint32_t nextCapacity = capacity <= MaxSnapshotBytes / 2 ? capacity * 2 : MaxSnapshotBytes;
        if (returnLength > nextCapacity && returnLength <= MaxSnapshotBytes) nextCapacity = returnLength;
        if (nextCapacity <= capacity || nextCapacity > MaxSnapshotBytes) break;
        capacity = nextCapacity;
    }

    result.status = status;
    result.length = returnLength;
    return status;
}

void AppendUnsigned(char* out, uint32_t outSize, uint32_t& cursor, uint64_t value) noexcept {
    if (cursor >= outSize) return;
    char digits[20];
    uint32_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count && cursor + 1 < outSize) out[cursor++] = digits[--count];
    out[cursor] = '\0';
}

void AppendHex32(char* out, uint32_t outSize, uint32_t& cursor, uint32_t value) noexcept {
    static constexpr char digits[] = "0123456789ABCDEF";
    if (cursor + 8 >= outSize) return;
    for (int shift = 28; shift >= 0; shift -= 4) out[cursor++] = digits[(value >> shift) & 0xFU];
    out[cursor] = '\0';
}

void DrawProcessImageName(const SnapshotView& snapshot,
                          const micant::SystemProcessInformation& entry,
                          uint32_t y) noexcept {
    constexpr uint32_t NameCapacity = 96;
    char name[NameCapacity]{};
    if (!entry.ImageName.Buffer || !entry.ImageName.Length ||
        (entry.ImageName.Length & 1U) != 0 || entry.ImageName.Length / 2 >= NameCapacity) {
        DrawString(s_tmBuffer, WIN_W, 12, y, "Image name unavailable", PackBGRA(215, 225, 240));
        return;
    }
    const uintptr_t buffer = entry.ImageName.Buffer;
    const uintptr_t base = reinterpret_cast<uintptr_t>(snapshot.data);
    if (buffer < base || buffer - base > snapshot.length ||
        entry.ImageName.Length > snapshot.length - (buffer - base)) {
        DrawString(s_tmBuffer, WIN_W, 12, y, "Image name unavailable", PackBGRA(215, 225, 240));
        return;
    }
    const auto* imageName = reinterpret_cast<const char16_t*>(buffer);
    const uint32_t length = entry.ImageName.Length / sizeof(char16_t);
    for (uint32_t i = 0; i < length; ++i) {
        const char16_t character = imageName[i];
        name[i] = character >= 0x20 && character <= 0x7e ? static_cast<char>(character) : '?';
    }
    name[length] = '\0';
    DrawString(s_tmBuffer, WIN_W, 12, y, name, PackBGRA(215, 225, 240));
}

bool ProcessMatchesSearch(const SnapshotView& snapshot,
                          const micant::SystemProcessInformation& entry) noexcept {
    if (!s_searchLength) return true;
    if (!entry.ImageName.Buffer || (entry.ImageName.Length & 1U)) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(snapshot.data);
    const uintptr_t buffer = entry.ImageName.Buffer;
    if (buffer < base || buffer - base > snapshot.length ||
        entry.ImageName.Length > snapshot.length - (buffer - base)) return false;
    const auto* name = reinterpret_cast<const char16_t*>(buffer);
    const size_t nameLength = entry.ImageName.Length / sizeof(char16_t);
    if (s_searchLength > nameLength) return false;
    for (size_t start = 0; start + s_searchLength <= nameLength; ++start) {
        size_t matched = 0;
        for (; matched < s_searchLength; ++matched) {
            char16_t actual = name[start + matched];
            char wanted = s_searchText[matched];
            if (actual >= u'A' && actual <= u'Z') actual += u'a' - u'A';
            if (wanted >= 'A' && wanted <= 'Z') wanted = static_cast<char>(wanted + 'a' - 'A');
            if (actual != static_cast<unsigned char>(wanted)) break;
        }
        if (matched == s_searchLength) return true;
    }
    return false;
}

void RenderTaskManagerWindow(const SnapshotView& snapshot) noexcept {
    const uint32_t width = s_renderWidth;
    const uint32_t height = s_renderHeight;
    DrawWindowFrame(s_tmBuffer, width, height, "Task Manager", "File   Options   View");

    FillRect(s_tmBuffer, width, 1, 49, width - 2, 24, PackBGRA(22, 30, 46));
    DrawRect(s_tmBuffer, width, 1, 72, width - 2, 1, PackBGRA(38, 52, 78));
    const uint32_t tabColors[3]{
        s_activePage == 0 ? PackBGRA(0, 210, 255) : PackBGRA(130, 145, 165),
        s_activePage == 1 ? PackBGRA(0, 210, 255) : PackBGRA(130, 145, 165),
        s_activePage == 2 ? PackBGRA(0, 210, 255) : PackBGRA(130, 145, 165)};
    DrawString(s_tmBuffer, width, 12, 56, "Processes", tabColors[0]);
    DrawString(s_tmBuffer, width, 110, 56, "Performance", tabColors[1]);
    DrawString(s_tmBuffer, width, 242, 56, "Details", tabColors[2]);
    const uint32_t tabLeft[3]{10, 108, 240};
    const uint32_t tabWidth[3]{78, 116, 68};
    FillRect(s_tmBuffer, width, tabLeft[s_activePage], 71, tabWidth[s_activePage], 2,
             PackBGRA(0, 210, 255));
    FillRect(s_tmBuffer, width, 374, 51, 194, 19, PackBGRA(13, 20, 33));
    DrawRect(s_tmBuffer, width, 374, 51, 194, 19,
             s_searchFocused ? PackBGRA(0, 165, 220) : PackBGRA(55, 68, 90));
    DrawString(s_tmBuffer, width, 380, 56, s_searchLength ? s_searchText : "Search processes",
               s_searchLength ? PackBGRA(220, 230, 240) : PackBGRA(120, 135, 155));

    if (s_activePage == 1) {
        FillRect(s_tmBuffer, width, 1, 74, width - 2, height - 99, PackBGRA(16, 22, 35));
        DrawString(s_tmBuffer, width, 20, 100, "CPU", PackBGRA(130, 150, 180));
        DrawString(s_tmBuffer, width, 20, 130, "Memory", PackBGRA(130, 150, 180));
        if (snapshot.cpuAvailable) {
            char value[16]{}; uint32_t cursor = 0;
            AppendUnsigned(value, sizeof(value), cursor, snapshot.cpuPercent);
            value[cursor++] = '%'; value[cursor] = 0;
            DrawString(s_tmBuffer, width, 150, 100, value, PackBGRA(0, 210, 255));
        } else DrawString(s_tmBuffer, width, 150, 100, "Unavailable", PackBGRA(150, 165, 185));
        if (snapshot.memoryAvailable) {
            char value[32] = "Available pages: "; uint32_t cursor = 17;
            AppendUnsigned(value, sizeof(value), cursor, snapshot.availablePages);
            DrawString(s_tmBuffer, width, 150, 130, value, PackBGRA(0, 210, 255));
        } else DrawString(s_tmBuffer, width, 150, 130, "Unavailable", PackBGRA(150, 165, 185));
    } else if (s_activePage == 2) {
        FillRect(s_tmBuffer, width, 1, 74, width - 2, height - 99, PackBGRA(16, 22, 35));
        if (s_selectedProcessId) {
            char selected[32] = "Selected PID: "; uint32_t cursor = 14;
            AppendUnsigned(selected, sizeof(selected), cursor, s_selectedProcessId);
            DrawString(s_tmBuffer, width, 20, 102, selected, PackBGRA(220, 230, 240));
            if (snapshot.available) {
                uint32_t offset = 0;
                for (uint32_t i = 0; i < snapshot.processCount; ++i) {
                    const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(
                        snapshot.data + offset);
                    if (entry->UniqueProcessId == s_selectedProcessId) {
                        DrawProcessImageName(snapshot, *entry, 122);
                        char threads[32] = "Threads: "; uint32_t threadsCursor = 9;
                        AppendUnsigned(threads, sizeof(threads), threadsCursor,
                                       entry->NumberOfThreads);
                        DrawString(s_tmBuffer, width, 20, 142, threads,
                                   PackBGRA(190, 205, 225));
                        char workingSet[40] = "Working set: "; uint32_t wsCursor = 13;
                        AppendUnsigned(workingSet, sizeof(workingSet), wsCursor,
                            static_cast<uint64_t>(entry->WorkingSetSize) / 1024u);
                        const char* kb = " KB";
                        while (*kb && wsCursor + 1 < sizeof(workingSet))
                            workingSet[wsCursor++] = *kb++;
                        workingSet[wsCursor] = 0;
                        DrawString(s_tmBuffer, width, 20, 162, workingSet,
                                   PackBGRA(190, 205, 225));
                        char peakSet[40] = "Peak working set: "; uint32_t peakCursor = 18;
                        AppendUnsigned(peakSet, sizeof(peakSet), peakCursor,
                            static_cast<uint64_t>(entry->PeakWorkingSetSize) / 1024u);
                        kb = " KB";
                        while (*kb && peakCursor + 1 < sizeof(peakSet))
                            peakSet[peakCursor++] = *kb++;
                        peakSet[peakCursor] = 0;
                        DrawString(s_tmBuffer, width, 20, 182, peakSet,
                                   PackBGRA(190, 205, 225));
                        DrawString(s_tmBuffer, width, 20, 202,
                                   "Private commit: unavailable",
                                   PackBGRA(150, 165, 185));
                        break;
                    }
                    if (!entry->NextEntryOffset) break;
                    offset += entry->NextEntryOffset;
                }
            }
        } else DrawString(s_tmBuffer, width, 20, 102, "Select a process", PackBGRA(150, 165, 185));
        DrawString(s_tmBuffer, width, 20, 232, s_endTaskStatus,
                   PackBGRA(240, 195, 105));
    }

    if (s_activePage == 0) {
    FillRect(s_tmBuffer, width, 1, 73, width - 2, 20, PackBGRA(18, 25, 38));
    DrawString(s_tmBuffer, width, 12, 79, "Name / PID", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, width, 220, 79, "Threads", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, width, 300, 79, "Handles", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, width, 390, 79, "CPU", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, width, 465, 79, "Memory", PackBGRA(140, 165, 200));

    if (!snapshot.available) {
        FillRect(s_tmBuffer, width, 1, 94, width - 2, 42, PackBGRA(16, 22, 35));
        DrawString(s_tmBuffer, width, 12, 104, "Process snapshot unavailable", PackBGRA(240, 195, 105));
        char reason[48] = "NTSTATUS 0x";
        uint32_t cursor = 11;
        AppendHex32(reason, sizeof(reason), cursor, snapshot.status);
        DrawString(s_tmBuffer, width, 12, 121, reason, PackBGRA(175, 190, 210));
        char bytes[32] = "Bytes reported: ";
        uint32_t bytesCursor = 16;
        AppendUnsigned(bytes, sizeof(bytes), bytesCursor, snapshot.length);
        DrawString(s_tmBuffer, width, 250, 121, bytes, PackBGRA(175, 190, 210));
    } else {
        uint32_t offset = 0;
        uint32_t row = 0;
        uint32_t visibleRow = 0;
        uint32_t y = 96;
        while (row < snapshot.processCount) {
            const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(snapshot.data + offset);
            if (ProcessMatchesSearch(snapshot, *entry) && y + 20 < height - 26) {
            const bool selected = entry->UniqueProcessId == s_selectedProcessId;
            const uint32_t bg = selected ? PackBGRA(38, 62, 94) :
                ((visibleRow & 1) ? PackBGRA(14, 20, 32) : PackBGRA(16, 22, 35));
            FillRect(s_tmBuffer, width, 1, y, width - 2, 22, bg);

            DrawProcessImageName(snapshot, *entry, y + 3);
            char pidText[24] = "PID ";
            uint32_t pidCursor = 4;
            AppendUnsigned(pidText, sizeof(pidText), pidCursor, entry->UniqueProcessId);
            DrawString(s_tmBuffer, width, 12, y + 12, pidText, PackBGRA(130, 150, 180));

            char threads[16]{};
            uint32_t threadsCursor = 0;
            AppendUnsigned(threads, sizeof(threads), threadsCursor, entry->NumberOfThreads);
            DrawString(s_tmBuffer, width, 220, y + 7, threads, PackBGRA(200, 220, 245));
            char handles[16]{};
            uint32_t handlesCursor = 0;
            AppendUnsigned(handles, sizeof(handles), handlesCursor, entry->HandleCount);
            DrawString(s_tmBuffer, width, 300, y + 7, handles, PackBGRA(200, 220, 245));
            if (row < MaxTrackedProcesses && snapshot.processCpuAvailable[row]) {
                char cpuText[8]{};
                uint32_t cpuCursor = 0;
                AppendUnsigned(cpuText, sizeof(cpuText), cpuCursor,
                               snapshot.processCpuPercent[row]);
                cpuText[cpuCursor++] = '%';
                cpuText[cpuCursor] = '\0';
                DrawString(s_tmBuffer, width, 390, y + 7, cpuText,
                           PackBGRA(0, 210, 255));
            } else {
                DrawString(s_tmBuffer, width, 390, y + 7, "N/A", PackBGRA(150, 165, 185));
            }
            if (entry->WorkingSetSize) {
                char memory[20]{}; uint32_t memoryCursor = 0;
                const uint64_t workingSetBytes = static_cast<uint64_t>(entry->WorkingSetSize);
                const bool megabytes = workingSetBytes >= 1024u * 1024u;
                AppendUnsigned(memory, sizeof(memory), memoryCursor,
                    workingSetBytes / (megabytes ? 1024u * 1024u : 1024u));
                memory[memoryCursor++] = ' ';
                memory[memoryCursor++] = megabytes ? 'M' : 'K';
                memory[memoryCursor++] = 'B'; memory[memoryCursor] = 0;
                DrawString(s_tmBuffer, width, 465, y + 7, memory, PackBGRA(190, 210, 235));
            } else DrawString(s_tmBuffer, width, 465, y + 7, "N/A", PackBGRA(150, 165, 185));
            ++visibleRow;
            y += 22;
            }
            ++row;
            if (entry->NextEntryOffset == 0) break;
            offset += entry->NextEntryOffset;
        }
    }

    FillRect(s_tmBuffer, width, 1, height - 24, width - 2, 23, PackBGRA(24, 32, 48));
    DrawRect(s_tmBuffer, width, 1, height - 24, width - 2, 1, PackBGRA(38, 52, 76));
    if (snapshot.available) {
        char countText[40] = "Processes: ";
        uint32_t cursor = 11;
        AppendUnsigned(countText, sizeof(countText), cursor, snapshot.processCount);
        DrawString(s_tmBuffer, width, 12, height - 16, countText, PackBGRA(180, 200, 230));
    } else {
        DrawString(s_tmBuffer, width, 12, height - 16, "Processes: unavailable", PackBGRA(240, 195, 105));
    }
    if (snapshot.cpuAvailable) {
        char cpuText[32] = "CPU: ";
        uint32_t cursor = 5;
        AppendUnsigned(cpuText, sizeof(cpuText), cursor, snapshot.cpuPercent);
        cpuText[cursor++] = '%';
        cpuText[cursor] = '\0';
        DrawString(s_tmBuffer, width, 205, height - 16, cpuText, PackBGRA(0, 210, 255));
    } else {
        DrawString(s_tmBuffer, width, 205, height - 16, "CPU: unavailable", PackBGRA(160, 180, 200));
    }
    if (snapshot.memoryAvailable) {
        char memoryText[48] = "Available memory: ";
        uint32_t cursor = 18;
        AppendUnsigned(memoryText, sizeof(memoryText), cursor, snapshot.availablePages);
        const char* suffix = " pages";
        while (*suffix && cursor + 1 < sizeof(memoryText)) memoryText[cursor++] = *suffix++;
        memoryText[cursor] = '\0';
        DrawString(s_tmBuffer, width, 330, height - 16, memoryText, PackBGRA(0, 210, 255));
    } else {
        DrawString(s_tmBuffer, width, 330, height - 16, "Memory: unavailable", PackBGRA(160, 180, 200));
    }
    FillRect(s_tmBuffer, width, width - 112, height - 24, 100, 18,
             s_confirmEndTask ? PackBGRA(100, 45, 45) : PackBGRA(48, 62, 84));
    DrawString(s_tmBuffer, width, width - 103, height - 17,
        s_confirmEndTask ? "Confirm end task" : "End task",
        PackBGRA(230, 238, 248));
    }
}

intptr_t TaskmgrWindowProcedure(void* hwnd, uint32_t message,
                               uintptr_t wParam, intptr_t lParam) noexcept {
    constexpr uint32_t WM_NCCREATE = 0x0081;
    constexpr uint32_t WM_CREATE = 0x0001;
    constexpr uint32_t WM_CLOSE = 0x0010;
    constexpr uint32_t WM_DESTROY = 0x0002;
    constexpr uint32_t WM_SIZE = 0x0005;
    constexpr uint32_t WM_PAINT = 0x000F;
    constexpr uint32_t WM_LBUTTONDOWN = 0x0201;
    constexpr uint32_t WM_CHAR = 0x0102;
    constexpr uint32_t WM_KEYDOWN = 0x0100;
    constexpr uint32_t SRCCOPY = 0x00CC0020;
    if (message == WM_NCCREATE || message == WM_CREATE) {
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        PrintLine(message == WM_NCCREATE
            ? "Task Manager: native WM_NCCREATE dispatched."
            : "Task Manager: native WM_CREATE dispatched.");
#endif
        return message == WM_NCCREATE ? 1 : 0;
    }
    switch (message) {
    case WM_SIZE:
        s_renderWidth = static_cast<uint32_t>(static_cast<uint16_t>(lParam))
            < WIN_W ? WIN_W : static_cast<uint32_t>(static_cast<uint16_t>(lParam));
        s_renderHeight = static_cast<uint32_t>(static_cast<uint16_t>(
            static_cast<uintptr_t>(lParam) >> 16)) < WIN_H ? WIN_H :
            static_cast<uint32_t>(static_cast<uint16_t>(
                static_cast<uintptr_t>(lParam) >> 16));
        if (s_renderWidth > MaxRenderWidth) s_renderWidth = MaxRenderWidth;
        if (s_renderHeight > MaxRenderHeight) s_renderHeight = MaxRenderHeight;
        (void)InvalidateRect(hwnd, nullptr, 1);
        if (!s_windowSizeReported) {
            PrintLine("Task Manager: native window resize delivered.");
            s_windowSizeReported = true;
        }
        return 0;
    case WM_LBUTTONDOWN: {
        const int32_t mouseX = static_cast<int16_t>(static_cast<uintptr_t>(lParam) & 0xffffu);
        const int32_t mouseY = static_cast<int16_t>((static_cast<uintptr_t>(lParam) >> 16) & 0xffffu);
        if (mouseY >= 49 && mouseY <= 72 && mouseX >= 374 && mouseX < 568) {
            s_searchFocused = true;
        } else if (mouseY >= 49 && mouseY <= 72) {
            if (mouseX >= 8 && mouseX < 100) s_activePage = 0;
            else if (mouseX >= 100 && mouseX < 232) s_activePage = 1;
            else if (mouseX >= 232 && mouseX < 320) s_activePage = 2;
            s_searchFocused = false;
            s_confirmEndTask = false;
        } else if (s_activePage == 0 && mouseY >= static_cast<int32_t>(s_renderHeight - 24) &&
                   mouseX >= static_cast<int32_t>(s_renderWidth - 112)) {
            if (!s_selectedProcessId || s_selectedProcessId == 4) {
                SetEndTaskStatus("Select a user process");
                s_confirmEndTask = false;
            } else if (!s_confirmEndTask) {
                SetEndTaskStatus("Click again to confirm");
                s_confirmEndTask = true;
            } else {
                bool isSelf = false;
                if (s_liveSnapshot && s_liveSnapshot->available) {
                    uint32_t offset = 0;
                    for (uint32_t i = 0; i < s_liveSnapshot->processCount; ++i) {
                        const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(
                            s_liveSnapshot->data + offset);
                        if (entry->UniqueProcessId == s_selectedProcessId &&
                            IsTaskmgrImageName(*entry, s_liveSnapshot->data,
                                               s_liveSnapshot->length)) isSelf = true;
                        if (!entry->NextEntryOffset) break;
                        offset += entry->NextEntryOffset;
                    }
                }
                const uint32_t status = isSelf ? 0xC0000022u :
                    TerminateProcessById(s_selectedProcessId);
                const char* messageText = status == StatusSuccess
                    ? "Waiting for process exit"
                    : status == 0xC0000022u ? "Access denied" : "Process unavailable";
                SetEndTaskStatus(messageText);
                s_endTaskWaiting = status == StatusSuccess;
                s_confirmEndTask = false;
            }
        } else if (s_activePage == 0 && mouseY >= 96 &&
                   mouseY < static_cast<int32_t>(WIN_H - 26) &&
                   s_liveSnapshot && s_liveSnapshot->available) {
            const uint32_t wantedRow = static_cast<uint32_t>((mouseY - 96) / 22);
            uint32_t offset = 0, visibleRow = 0;
            for (uint32_t i = 0; i < s_liveSnapshot->processCount; ++i) {
                const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(
                    s_liveSnapshot->data + offset);
                if (ProcessMatchesSearch(*s_liveSnapshot, *entry)) {
                    if (visibleRow == wantedRow) {
                        s_selectedProcessId = entry->UniqueProcessId;
                        s_confirmEndTask = false;
                        break;
                    }
                    ++visibleRow;
                }
                if (!entry->NextEntryOffset) break;
                offset += entry->NextEntryOffset;
            }
        } else if (s_activePage == 0) {
            s_searchFocused = false;
        }
        (void)InvalidateRect(hwnd, nullptr, 1);
        return 0;
    }
    case WM_CHAR:
        if (s_searchFocused && s_activePage == 0) {
            const char character = static_cast<char>(wParam & 0xffu);
            if (character == '\b') {
                if (s_searchLength) s_searchText[--s_searchLength] = 0;
            } else if (character >= 0x20 && character <= 0x7e &&
                       s_searchLength + 1 < sizeof(s_searchText)) {
                s_searchText[s_searchLength++] = character;
                s_searchText[s_searchLength] = 0;
            }
            (void)InvalidateRect(hwnd, nullptr, 1);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (s_searchFocused && wParam == 0x1b) {
            s_searchFocused = false;
            (void)InvalidateRect(hwnd, nullptr, 1);
            return 0;
        }
        break;
    case WM_PAINT: {
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        if (!s_windowPaintEnteredReported) {
            PrintLine("Task Manager: native WM_PAINT delivered.");
            s_windowPaintEnteredReported = true;
        }
#endif
        if (s_liveSnapshot) RenderTaskManagerWindow(*s_liveSnapshot);
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        if (!s_windowPaintRenderedReported) {
            PrintLine("Task Manager: native WM_PAINT rendered.");
            s_windowPaintRenderedReported = true;
        }
#endif
        TaskmgrPaintStruct paint{};
        void* destination = BeginPaint(hwnd, &paint);
        bool presented = false;
        uint32_t failureStage = destination ? 0u : 1u;
        if (destination) {
            void* memoryDc = CreateCompatibleDC(destination);
            if (!memoryDc) failureStage = 2;
            TaskmgrBitmapInfo bitmapInfo{};
            bitmapInfo.header.size = sizeof(bitmapInfo.header);
            bitmapInfo.header.width = static_cast<int32_t>(s_renderWidth);
            constexpr uint32_t PaintBandRows = 32;
            bitmapInfo.header.height = -static_cast<int32_t>(PaintBandRows);
            bitmapInfo.header.planes = 1;
            bitmapInfo.header.bitCount = 32;
            bitmapInfo.header.compression = 0;
            bitmapInfo.header.imageSize = s_renderWidth * PaintBandRows * sizeof(uint32_t);
            void* dibPixels = nullptr;
            void* bitmap = memoryDc ? CreateDIBSection(memoryDc, &bitmapInfo, 0,
                &dibPixels, nullptr, 0) : nullptr;
            if (memoryDc && (!bitmap || !dibPixels)) failureStage = 3;
            if (memoryDc && bitmap && dibPixels) {
                auto* previousBitmap = SelectObject(memoryDc, bitmap);
                if (!previousBitmap) failureStage = 4;
                if (previousBitmap) {
                    MicaWin32kRect client{};
                    const int32_t clientRectStatus = GetClientRect(hwnd, &client);
                    if (!clientRectStatus) failureStage = 5;
                    if (clientRectStatus) {
                        const int32_t width = client.right < static_cast<int32_t>(s_renderWidth)
                            ? client.right : static_cast<int32_t>(s_renderWidth);
                        const int32_t height = client.bottom < static_cast<int32_t>(s_renderHeight)
                            ? client.bottom : static_cast<int32_t>(s_renderHeight);
                        presented = width > 0 && height > 0;
                        auto* pixels = static_cast<uint32_t*>(dibPixels);
                        for (int32_t top = 0; presented && top < height;
                             top += static_cast<int32_t>(PaintBandRows)) {
                            const int32_t bandHeight = height - top <
                                static_cast<int32_t>(PaintBandRows)
                                ? height - top : static_cast<int32_t>(PaintBandRows);
                            for (int32_t row = 0; row < bandHeight; ++row) {
                                const uint32_t sourceOffset =
                                    static_cast<uint32_t>(top + row) * s_renderWidth;
                                const uint32_t bandOffset =
                                    static_cast<uint32_t>(row) * s_renderWidth;
                                for (int32_t column = 0; column < width; ++column)
                                    pixels[bandOffset + static_cast<uint32_t>(column)] =
                                        s_tmBuffer[sourceOffset + static_cast<uint32_t>(column)];
                            }
                            presented = BitBlt(destination, 0, top, width, bandHeight,
                                               memoryDc, 0, 0, SRCCOPY) != 0;
                        }
                        if (!presented) failureStage = 6;
                    }
                    (void)SelectObject(memoryDc, previousBitmap);
                }
                (void)DeleteObject(bitmap);
            }
            if (memoryDc) (void)DeleteDC(memoryDc);
            if (!EndPaint(hwnd, &paint)) { presented = false; failureStage = 7; }
        }
        if (presented && !s_windowPaintReported) {
            PrintLine("Task Manager: native window paint presented.");
            s_windowPaintReported = true;
        }
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        if (!presented && !s_windowPaintFailureReported) {
            static const char* const failures[] = {
                "Task Manager: paint not presented.",
                "Task Manager: BeginPaint returned no DC.",
                "Task Manager: compatible DC creation failed.",
                "Task Manager: DIB creation failed.",
                "Task Manager: bitmap selection failed.",
                "Task Manager: GetClientRect failed.",
                "Task Manager: BitBlt rejected the surface.",
                "Task Manager: EndPaint failed."};
            PrintLine(failures[failureStage < sizeof(failures) / sizeof(failures[0])
                ? failureStage : 0]);
            s_windowPaintFailureReported = true;
        }
#endif
        return 0;
    }
    case WM_CLOSE:
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        PrintLine("Task Manager: native WM_CLOSE delivered.");
#endif
        if (DestroyWindow(hwnd)) return 0;
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        PrintLine("Task Manager: DestroyWindow failed.");
#endif
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_DESTROY:
        if (!s_windowCloseReported) {
            PrintLine("Task Manager: native window close processed.");
            s_windowCloseReported = true;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool PumpTaskManagerMessages() noexcept {
    constexpr uint32_t PM_REMOVE = 0x0001;
    constexpr uint32_t WM_QUIT = 0x0012;
    TaskmgrMessage message{};
    for (uint32_t count = 0; count < 64 && PeekMessageW(&message, nullptr,
            0, 0, PM_REMOVE); ++count) {
        if (message.message == WM_QUIT) return false;
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }
    return true;
}

#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
bool ExerciseTaskmgrControls(const SnapshotView& snapshot) noexcept {
    if (!snapshot.available || !s_taskmgrWindow) return false;
    micant::Handle probeId{};
    if (!FindTaskmgrProbeProcess(snapshot, probeId)) return false;
    auto postClick = [](int32_t x, int32_t y) noexcept {
        const uintptr_t coordinates = static_cast<uint16_t>(x) |
            (static_cast<uintptr_t>(static_cast<uint16_t>(y)) << 16);
        return PostMessageW(s_taskmgrWindow, 0x0201, 0,
                            static_cast<intptr_t>(coordinates)) != 0;
    };
    if (!postClick(400, 60) || !PostMessageW(s_taskmgrWindow, 0x0102, 't', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'a', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 's', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'k', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'm', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'g', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'r', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, '_', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'p', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'r', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'o', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'b', 0) ||
        !PostMessageW(s_taskmgrWindow, 0x0102, 'e', 0)) return false;
    if (!PumpTaskManagerMessages() || s_searchLength != 13) return false;
    bool matchingProbeVisible = false;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < snapshot.processCount; ++i) {
        const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(
            snapshot.data + offset);
        if (entry->UniqueProcessId == probeId)
            matchingProbeVisible = ProcessMatchesSearch(snapshot, *entry);
        if (!entry->NextEntryOffset) break;
        offset += entry->NextEntryOffset;
    }
    if (!matchingProbeVisible) return false;
    if (!postClick(120, 60) || !PumpTaskManagerMessages() || s_activePage != 1) return false;
    if (!postClick(20, 60) || !PumpTaskManagerMessages() || s_activePage != 0) return false;
    if (!postClick(20, 96) || !PumpTaskManagerMessages() || s_selectedProcessId != probeId)
        return false;

    s_activePage = 0;
    s_selectedProcessId = 0;
    s_searchFocused = false;
    s_searchLength = 0;
    s_searchText[0] = 0;
    s_confirmEndTask = false;
    SetEndTaskStatus("");
    (void)InvalidateRect(s_taskmgrWindow, nullptr, 1);
    return true;
}
#endif

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    PrintLine("Task Manager: native entry reached.");
#endif
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    SnapshotView snapshot{};
    bool reported = false;
    bool memoryReported = false;
    bool imageNameReported = false;
    ProcessorTimes previousCpu{};
    bool havePreviousCpu = false;
    bool cpuSampleReported = false;
    bool cpuUnavailableReported = false;
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    bool probeTerminationRequested = false;
    bool probeTerminationReported = false;
    bool probeFoundReported = false;
    bool probeThreadTotalsReported = false;
    bool probeCpuTimesReported = false;
    bool havePreviousProbeTimes = false;
    uint64_t previousProbeUserTime{};
    uint64_t previousProbeKernelTime{};
    bool selftestClosePosted = false;
    bool selftestControlsReported = false;
#endif
    s_liveSnapshot = &snapshot;
    constexpr wchar_t TaskmgrWindowClassName[] = L"LuceOSTaskManagerWindow";
    MicaWin32kWindowClassEx windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.wndProc = reinterpret_cast<void*>(TaskmgrWindowProcedure);
    windowClass.className = TaskmgrWindowClassName;
    if (!RegisterClassExW(&windowClass)) {
        PrintLine("Task Manager: native window class registration failed.");
        return;
    }
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    PrintLine("Task Manager: native User32 class registered.");
#endif
    constexpr uint32_t WsOverlappedWindow = 0x00CF0000U;
    constexpr uint32_t WsVisible = 0x10000000U;
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    PrintLine("Task Manager: creating native window.");
#endif
    s_taskmgrWindow = CreateWindowExW(0, TaskmgrWindowClassName,
        L"Task Manager", WsOverlappedWindow | WsVisible, 96, 72,
        static_cast<int32_t>(WIN_W + 36), static_cast<int32_t>(WIN_H + 64),
        nullptr, nullptr, nullptr, nullptr);
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    PrintLine("Task Manager: CreateWindowExW returned.");
#endif
    if (!s_taskmgrWindow) {
        PrintLine("Task Manager: native window creation failed.");
        return;
    }
    (void)ShowWindow(s_taskmgrWindow, 5);
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    PrintLine("Task Manager: ShowWindow returned.");
#endif
    (void)UpdateWindow(s_taskmgrWindow);
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
#endif
    PrintLine("Task Manager: native window created.");
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
    (void)SetWindowPos(s_taskmgrWindow, nullptr, 96, 72,
        static_cast<int32_t>(WIN_W + 52), static_cast<int32_t>(WIN_H + 78), 0);
#endif
    while (true) {
        QueryProcessSnapshot(snapshot);
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        if (!selftestControlsReported && snapshot.available) {
            if (!ExerciseTaskmgrControls(snapshot)) {
                PrintLine("Task Manager: native controls self-test failed.");
                return;
            }
            PrintLine("Task Manager: native controls validated.");
            selftestControlsReported = true;
        }
#endif
        if (s_endTaskWaiting && snapshot.available) {
            bool stillPresent = false;
            uint32_t offset = 0;
            for (uint32_t i = 0; i < snapshot.processCount; ++i) {
                const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(
                    snapshot.data + offset);
                if (entry->UniqueProcessId == s_selectedProcessId) stillPresent = true;
                if (!entry->NextEntryOffset) break;
                offset += entry->NextEntryOffset;
            }
            if (!stillPresent) {
                SetEndTaskStatus("Process ended");
                s_endTaskWaiting = false;
            }
        }
        const bool memoryAvailable = QueryAvailablePages(snapshot);
        const ProcessorTimes priorCpu = previousCpu;
        const bool hadPreviousCpu = havePreviousCpu;
        ProcessorTimes currentCpu{};
        const bool currentCpuAvailable = QueryProcessorTimes(currentCpu);
        if (currentCpuAvailable) {
            snapshot.cpuAvailable = false;
            if (havePreviousCpu) {
                uint32_t cpuPercent{};
                if (ComputeCpuPercent(previousCpu, currentCpu, cpuPercent)) {
                    snapshot.cpuPercent = cpuPercent;
                    snapshot.cpuAvailable = true;
                    if (!cpuSampleReported) {
                        PrintLine("Task Manager: CPU processor snapshot validated.");
                        cpuSampleReported = true;
                    }
                }
            }
            previousCpu = currentCpu;
            havePreviousCpu = true;
        } else {
            snapshot.cpuAvailable = false;
            if (!cpuUnavailableReported) {
                PrintLine("Task Manager: CPU processor snapshot unavailable.");
                cpuUnavailableReported = true;
            }
        }
        UpdateProcessCpuSamples(snapshot, priorCpu, currentCpu,
                                hadPreviousCpu && currentCpuAvailable);
        if (!reported) {
            if (snapshot.available) {
                PrintLine("Task Manager: kernel process snapshot received.");
            } else {
                PrintLine("Task Manager: process snapshot unavailable from the kernel provider.");
                char statusLine[48] = "Task Manager: NTSTATUS 0x";
                uint32_t cursor = 25;
                AppendHex32(statusLine, sizeof(statusLine), cursor, snapshot.status);
                PrintLine(statusLine);
            }
            reported = true;
        }
        if (!memoryReported) {
            PrintLine(memoryAvailable
                ? "Task Manager: memory availability snapshot received."
                : "Task Manager: memory availability unavailable from the kernel provider.");
            memoryReported = true;
        }
        if (!imageNameReported) {
            PrintLine(snapshot.taskmgrImageNameFound
                ? "Task Manager: native process image name validated."
                : "Task Manager: native process image name unavailable.");
            imageNameReported = true;
        }
#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
        if (!probeTerminationRequested) {
            micant::Handle processId{};
            uint64_t probeUserTime{}, probeKernelTime{};
            bool threadTotalsMatch = false;
            if (FindTaskmgrProbeProcess(snapshot, processId, &probeUserTime,
                                        &probeKernelTime, &threadTotalsMatch)) {
                if (!probeFoundReported) {
                    PrintLine("Task Manager: authorized probe process found.");
                    probeFoundReported = true;
                }
                if (threadTotalsMatch && !probeThreadTotalsReported) {
                    PrintLine("Task Manager: process and thread CPU totals reconciled.");
                    probeThreadTotalsReported = true;
                }
                if (havePreviousProbeTimes && probeUserTime >= previousProbeUserTime &&
                    probeKernelTime >= previousProbeKernelTime &&
                    (probeUserTime > previousProbeUserTime ||
                     probeKernelTime > previousProbeKernelTime) && !probeCpuTimesReported) {
                    PrintLine("Task Manager: process CPU times validated.");
                    probeCpuTimesReported = true;
                }
                if ((probeUserTime < previousProbeUserTime ||
                     probeKernelTime < previousProbeKernelTime) && havePreviousProbeTimes)
                    PrintLine("Task Manager: process CPU times regressed.");
                previousProbeUserTime = probeUserTime;
                previousProbeKernelTime = probeKernelTime;
                havePreviousProbeTimes = true;
                if (!probeCpuTimesReported || !probeThreadTotalsReported) {
                    RenderTaskManagerWindow(snapshot);
                    (void)InvalidateRect(s_taskmgrWindow, nullptr, 1);
                    (void)UpdateWindow(s_taskmgrWindow);
                    if (!PumpTaskManagerMessages()) return;
                    const int64_t accountingInterval100ns = -100'000; // 10 ms
                    (void)DoSyscall(0x34, 0,
                        reinterpret_cast<uint64_t>(&accountingInterval100ns));
                    continue;
                }
                const uint32_t terminateStatus = TerminateTaskmgrProbe(processId);
                if (terminateStatus == StatusSuccess) {
                    probeTerminationRequested = true;
                } else {
                    PrintLine("Task Manager: authorized process termination denied.");
                    probeTerminationReported = true;
                }
            }
        } else if (!probeTerminationReported && snapshot.available &&
                   !TaskmgrProbeProcessExists(snapshot)) {
            PrintLine("Task Manager: authorized process termination verified.");
            probeTerminationReported = true;
        }
        if (probeTerminationReported && !selftestClosePosted) {
            selftestClosePosted = PostMessageW(s_taskmgrWindow, 0x0010, 0, 0) != 0;
            PrintLine(selftestClosePosted
                ? "Task Manager: native WM_CLOSE queued."
                : "Task Manager: native WM_CLOSE post failed.");
        }
#endif
        RenderTaskManagerWindow(snapshot);
        (void)InvalidateRect(s_taskmgrWindow, nullptr, 1);
        (void)UpdateWindow(s_taskmgrWindow);

        // Dispatch input/paint/close promptly, with a 10 ms blocking interval;
        // refresh the process snapshot once per second without a busy loop.
        for (uint32_t slice = 0; slice < 100; ++slice) {
            if (!PumpTaskManagerMessages()) return;
            const int64_t refreshInterval100ns = -100'000;
            (void)DoSyscall(0x34, 0,
                reinterpret_cast<uint64_t>(&refreshInterval100ns));
        }
    }
}
