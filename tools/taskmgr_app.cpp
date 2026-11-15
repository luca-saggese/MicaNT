/**
 * @file taskmgr_app.cpp
 * @brief Ring 3 process snapshot viewer for MicaNT.
 *
 * Process identifiers and counts come from NtQuerySystemInformation. Fields
 * not currently exported by the kernel are shown as unavailable.
 */

#include "gui_common.hpp"
#include "micant/shared/system_information.h"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
constexpr uint32_t SystemProcessInformationClass = 5;
constexpr uint64_t NtQuerySystemInformationService = 0x36;
constexpr uint32_t StatusSuccess = 0x00000000U;
constexpr uint32_t StatusInfoLengthMismatch = 0xC0000004U;
constexpr uint32_t MaxSnapshotBytes = 64 * 1024;
alignas(4096) uint32_t s_tmBuffer[WIN_W * WIN_H];
alignas(16) uint8_t s_processSnapshot[MaxSnapshotBytes];

struct SnapshotView {
    const uint8_t* data{nullptr};
    uint32_t length{0};
    uint32_t processCount{0};
    uint32_t status{StatusInfoLengthMismatch};
    bool available{false};
};

uint32_t QueryProcessSnapshot(SnapshotView& result) noexcept {
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

            DrawString(s_tmBuffer, WIN_W, 12, y + 3, "Image name unavailable", PackBGRA(215, 225, 240));
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
            DrawString(s_tmBuffer, WIN_W, 390, y + 7, "N/A", PackBGRA(150, 165, 185));
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
    DrawString(s_tmBuffer, WIN_W, 205, WIN_H - 16, "CPU / memory metrics: unavailable", PackBGRA(160, 180, 200));
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
    while (true) {
        QueryProcessSnapshot(snapshot);
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
