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

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(value);
    return destination;
}

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
constexpr uint32_t SystemProcessInformationClass = 5;
constexpr uint32_t SystemPerformanceInformationClass = 2;
constexpr uint32_t SystemProcessorPerformanceInformationClass = 8;
constexpr uint64_t NtQuerySystemInformationService = 0x36;
constexpr uint32_t StatusSuccess = 0x00000000U;
constexpr uint32_t StatusInfoLengthMismatch = 0xC0000004U;
constexpr uint32_t MaxSnapshotBytes = 64 * 1024;
constexpr uint32_t MaxTrackedProcesses =
    MaxSnapshotBytes / sizeof(micant::SystemProcessInformation);
alignas(4096) uint32_t s_tmBuffer[WIN_W * WIN_H];
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

void RenderTaskManagerWindow(const SnapshotView& snapshot) noexcept {
    DrawWindowFrame(s_tmBuffer, WIN_W, WIN_H, "Task Manager", "File   Options   View");

    FillRect(s_tmBuffer, WIN_W, 1, 49, WIN_W - 2, 24, PackBGRA(22, 30, 46));
    DrawRect(s_tmBuffer, WIN_W, 1, 72, WIN_W - 2, 1, PackBGRA(38, 52, 78));
    DrawString(s_tmBuffer, WIN_W, 12, 56, "Processes", PackBGRA(0, 210, 255));
    FillRect(s_tmBuffer, WIN_W, 10, 71, 75, 2, PackBGRA(0, 210, 255));
    DrawString(s_tmBuffer, WIN_W, 110, 56, "Performance", PackBGRA(130, 145, 165));
    DrawString(s_tmBuffer, WIN_W, 242, 56, "Details", PackBGRA(130, 145, 165));

    FillRect(s_tmBuffer, WIN_W, 1, 73, WIN_W - 2, 20, PackBGRA(18, 25, 38));
    DrawString(s_tmBuffer, WIN_W, 12, 79, "Name / PID", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, WIN_W, 220, 79, "Threads", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, WIN_W, 300, 79, "Handles", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, WIN_W, 390, 79, "CPU", PackBGRA(140, 165, 200));
    DrawString(s_tmBuffer, WIN_W, 465, 79, "Memory", PackBGRA(140, 165, 200));

    if (!snapshot.available) {
        FillRect(s_tmBuffer, WIN_W, 1, 94, WIN_W - 2, 42, PackBGRA(16, 22, 35));
        DrawString(s_tmBuffer, WIN_W, 12, 104, "Process snapshot unavailable", PackBGRA(240, 195, 105));
        char reason[48] = "NTSTATUS 0x";
        uint32_t cursor = 11;
        AppendHex32(reason, sizeof(reason), cursor, snapshot.status);
        DrawString(s_tmBuffer, WIN_W, 12, 121, reason, PackBGRA(175, 190, 210));
        char bytes[32] = "Bytes reported: ";
        uint32_t bytesCursor = 16;
        AppendUnsigned(bytes, sizeof(bytes), bytesCursor, snapshot.length);
        DrawString(s_tmBuffer, WIN_W, 250, 121, bytes, PackBGRA(175, 190, 210));
    } else {
        uint32_t offset = 0;
        uint32_t row = 0;
        uint32_t y = 96;
        while (row < snapshot.processCount && y + 20 < WIN_H - 26) {
            const auto* entry = reinterpret_cast<const micant::SystemProcessInformation*>(snapshot.data + offset);
            const uint32_t bg = (row & 1) ? PackBGRA(14, 20, 32) : PackBGRA(16, 22, 35);
            FillRect(s_tmBuffer, WIN_W, 1, y, WIN_W - 2, 22, bg);

            DrawProcessImageName(snapshot, *entry, y + 3);
            char pidText[24] = "PID ";
            uint32_t pidCursor = 4;
            AppendUnsigned(pidText, sizeof(pidText), pidCursor, entry->UniqueProcessId);
            DrawString(s_tmBuffer, WIN_W, 12, y + 12, pidText, PackBGRA(130, 150, 180));

            char threads[16]{};
            uint32_t threadsCursor = 0;
            AppendUnsigned(threads, sizeof(threads), threadsCursor, entry->NumberOfThreads);
            DrawString(s_tmBuffer, WIN_W, 220, y + 7, threads, PackBGRA(200, 220, 245));
            char handles[16]{};
            uint32_t handlesCursor = 0;
            AppendUnsigned(handles, sizeof(handles), handlesCursor, entry->HandleCount);
            DrawString(s_tmBuffer, WIN_W, 300, y + 7, handles, PackBGRA(200, 220, 245));
            if (row < MaxTrackedProcesses && snapshot.processCpuAvailable[row]) {
                char cpuText[8]{};
                uint32_t cpuCursor = 0;
                AppendUnsigned(cpuText, sizeof(cpuText), cpuCursor,
                               snapshot.processCpuPercent[row]);
                cpuText[cpuCursor++] = '%';
                cpuText[cpuCursor] = '\0';
                DrawString(s_tmBuffer, WIN_W, 390, y + 7, cpuText,
                           PackBGRA(0, 210, 255));
            } else {
                DrawString(s_tmBuffer, WIN_W, 390, y + 7, "N/A", PackBGRA(150, 165, 185));
            }
            DrawString(s_tmBuffer, WIN_W, 465, y + 7, "N/A", PackBGRA(150, 165, 185));

            ++row;
            y += 22;
            if (entry->NextEntryOffset == 0) break;
            offset += entry->NextEntryOffset;
        }
    }

    FillRect(s_tmBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(24, 32, 48));
    DrawRect(s_tmBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(38, 52, 76));
    if (snapshot.available) {
        char countText[40] = "Processes: ";
        uint32_t cursor = 11;
        AppendUnsigned(countText, sizeof(countText), cursor, snapshot.processCount);
        DrawString(s_tmBuffer, WIN_W, 12, WIN_H - 16, countText, PackBGRA(180, 200, 230));
    } else {
        DrawString(s_tmBuffer, WIN_W, 12, WIN_H - 16, "Processes: unavailable", PackBGRA(240, 195, 105));
    }
    if (snapshot.cpuAvailable) {
        char cpuText[32] = "CPU: ";
        uint32_t cursor = 5;
        AppendUnsigned(cpuText, sizeof(cpuText), cursor, snapshot.cpuPercent);
        cpuText[cursor++] = '%';
        cpuText[cursor] = '\0';
        DrawString(s_tmBuffer, WIN_W, 205, WIN_H - 16, cpuText, PackBGRA(0, 210, 255));
    } else {
        DrawString(s_tmBuffer, WIN_W, 205, WIN_H - 16, "CPU: unavailable", PackBGRA(160, 180, 200));
    }
    if (snapshot.memoryAvailable) {
        char memoryText[48] = "Available memory: ";
        uint32_t cursor = 18;
        AppendUnsigned(memoryText, sizeof(memoryText), cursor, snapshot.availablePages);
        const char* suffix = " pages";
        while (*suffix && cursor + 1 < sizeof(memoryText)) memoryText[cursor++] = *suffix++;
        memoryText[cursor] = '\0';
        DrawString(s_tmBuffer, WIN_W, 330, WIN_H - 16, memoryText, PackBGRA(0, 210, 255));
    } else {
        DrawString(s_tmBuffer, WIN_W, 330, WIN_H - 16, "Memory: unavailable", PackBGRA(160, 180, 200));
    }
    PresentWindow(s_tmBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
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
#endif
    while (true) {
        QueryProcessSnapshot(snapshot);
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
#endif
        RenderTaskManagerWindow(snapshot);

        // Relative NT interval: 1 second in 100 ns units. Wait through the
        // native timer path so refresh has bounded overhead and no busy loop.
        const int64_t refreshInterval100ns = -10'000'000;
        (void)DoSyscall(0x34, 0, reinterpret_cast<uint64_t>(&refreshInterval100ns));
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
