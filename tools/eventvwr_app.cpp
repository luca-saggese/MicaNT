/**
 * @file eventvwr_app.cpp
 * Luce OS Ring 3 Event Viewer Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Event Viewer window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_evBuffer[WIN_W * WIN_H];

void RenderEventViewerWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_evBuffer, WIN_W, WIN_H, "Event Viewer", "File   Action   View   Help");

    // 2. Split Panes: Left Tree (width 160), Right Events (remaining)
    constexpr uint32_t treeW = 160;
    FillRect(s_evBuffer, WIN_W, 1, 49, treeW, WIN_H - 71, PackBGRA(14, 18, 28));
    DrawRect(s_evBuffer, WIN_W, treeW + 1, 49, 1, WIN_H - 71, PackBGRA(35, 48, 70));

    DrawString(s_evBuffer, WIN_W, 10, 56, "v Event Viewer", PackBGRA(0, 210, 255));
    DrawString(s_evBuffer, WIN_W, 20, 72, "v MicaNT Logs", PackBGRA(180, 200, 230));
    DrawString(s_evBuffer, WIN_W, 30, 88, "- Application", PackBGRA(160, 180, 210));
    DrawString(s_evBuffer, WIN_W, 30, 104, "- Security", PackBGRA(160, 180, 210));

    // Highlighted node: System
    FillRect(s_evBuffer, WIN_W, 26, 118, treeW - 28, 18, PackBGRA(28, 48, 80));
    DrawString(s_evBuffer, WIN_W, 30, 122, "v System (Active)", PackBGRA(255, 255, 255));

    DrawString(s_evBuffer, WIN_W, 20, 142, "> Applications", PackBGRA(180, 200, 230));
    DrawString(s_evBuffer, WIN_W, 20, 158, "> Subscriptions", PackBGRA(180, 200, 230));

    // Right Events Pane
    FillRect(s_evBuffer, WIN_W, treeW + 2, 49, WIN_W - treeW - 3, WIN_H - 71, PackBGRA(16, 22, 34));

    // Column Header
    FillRect(s_evBuffer, WIN_W, treeW + 2, 49, WIN_W - treeW - 3, 20, PackBGRA(22, 28, 44));
    DrawRect(s_evBuffer, WIN_W, treeW + 2, 68, WIN_W - treeW - 3, 1, PackBGRA(35, 48, 70));

    DrawString(s_evBuffer, WIN_W, treeW + 8, 55, "Level", PackBGRA(140, 165, 200));
    DrawString(s_evBuffer, WIN_W, treeW + 100, 55, "Source", PackBGRA(140, 165, 200));
    DrawString(s_evBuffer, WIN_W, treeW + 230, 55, "Event ID", PackBGRA(140, 165, 200));
    DrawString(s_evBuffer, WIN_W, treeW + 310, 55, "Task Category", PackBGRA(140, 165, 200));

    // Event Rows
    const struct EvRow {
        const char* level;
        const char* source;
        const char* id;
        const char* task;
    } erows[] = {
        {"Information", "Kernel-General",   "1",    "Startup"},
        {"Information", "Service Control",  "7036", "Service State"},
        {"Information", "LSA Security",     "4624", "Logon"},
        {"Information", "Disk Controller",  "1074", "Mount"},
        {"Information", "Filter Manager",   "6",    "Altitude Filter"},
        {"Information", "Intel I210 Driver","101",  "Link Up"},
        {"Information", "AHCI SATA Subsys", "201",  "DMA Channel"},
    };

    uint32_t ey = 72;
    for (size_t i = 0; i < 7; ++i) {
        FillRect(s_evBuffer, WIN_W, treeW + 2, ey, WIN_W - treeW - 3, 20, (i % 2 == 1) ? PackBGRA(18, 24, 38) : PackBGRA(16, 22, 34));
        DrawString(s_evBuffer, WIN_W, treeW + 8, ey + 4, erows[i].level, PackBGRA(100, 240, 160));
        DrawString(s_evBuffer, WIN_W, treeW + 100, ey + 4, erows[i].source, PackBGRA(230, 240, 255));
        DrawString(s_evBuffer, WIN_W, treeW + 230, ey + 4, erows[i].id, PackBGRA(0, 210, 255));
        DrawString(s_evBuffer, WIN_W, treeW + 310, ey + 4, erows[i].task, PackBGRA(180, 200, 230));
        ey += 20;
    }

    // 3. Status Bar
    FillRect(s_evBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_evBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_evBuffer, WIN_W, 12, WIN_H - 15, "Log Name: System | 128 Events Recorded | Logging Active", PackBGRA(180, 200, 230));

    PresentWindow(s_evBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Event Viewer (eventvwr.msc):");
    PrintLine("  Event Channels: Application, Security, System Active");
    PrintLine("  System Log: 128 Events Recorded (Kernel-General, SCM, LSA)");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderEventViewerWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
