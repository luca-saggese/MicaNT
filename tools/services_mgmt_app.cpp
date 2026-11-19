/**
 * @file services_mgmt_app.cpp
 * Luce OS Ring 3 Services Management Console Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Services Console window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_svcBuffer[WIN_W * WIN_H];

void RenderServicesWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_svcBuffer, WIN_W, WIN_H, "Services", "File   Action   View   Help");

    // 2. Toolbar
    FillRect(s_svcBuffer, WIN_W, 1, 49, WIN_W - 2, 22, PackBGRA(22, 30, 46));
    DrawRect(s_svcBuffer, WIN_W, 1, 70, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_svcBuffer, WIN_W, 10, 56, "[Start]  [Stop]  [Pause]  [Restart]  |  Services (Local)", PackBGRA(200, 220, 245));

    // 3. Column Header (Height = 20)
    FillRect(s_svcBuffer, WIN_W, 1, 71, WIN_W - 2, 20, PackBGRA(22, 28, 44));
    DrawRect(s_svcBuffer, WIN_W, 1, 90, WIN_W - 2, 1, PackBGRA(35, 48, 70));

    DrawString(s_svcBuffer, WIN_W, 10, 77, "Name", PackBGRA(140, 165, 200));
    DrawString(s_svcBuffer, WIN_W, 150, 77, "Description", PackBGRA(140, 165, 200));
    DrawString(s_svcBuffer, WIN_W, 360, 77, "Status", PackBGRA(140, 165, 200));
    DrawString(s_svcBuffer, WIN_W, 440, 77, "Startup Type", PackBGRA(140, 165, 200));

    // Service Rows
    const struct SvcRow {
        const char* name;
        const char* desc;
        const char* status;
        const char* startup;
    } srows[] = {
        {"EventLog",      "MicaNT Event Log Service",     "Running", "Automatic"},
        {"LanmanServer",  "Server File Sharing Driver",   "Running", "Automatic"},
        {"LanmanWorkstn", "Workstation Client Service",   "Running", "Automatic"},
        {"PlugPlay",      "User-Mode Plug and Play Host", "Running", "Automatic"},
        {"RpcSs",         "Remote Procedure Call (RPC)",  "Running", "Automatic"},
        {"SamSs",         "Security Accounts Manager",    "Running", "Automatic"},
        {"Schedule",      "Task Scheduler Service",       "Running", "Automatic"},
        {"Spooler",       "Print Spooler Subsystem",      "Running", "Automatic"},
    };

    uint32_t sy = 94;
    for (size_t i = 0; i < 8; ++i) {
        FillRect(s_svcBuffer, WIN_W, 1, sy, WIN_W - 2, 22, (i % 2 == 1) ? PackBGRA(16, 22, 34) : PackBGRA(14, 18, 28));
        DrawString(s_svcBuffer, WIN_W, 10, sy + 7, srows[i].name, PackBGRA(0, 210, 255));
        DrawString(s_svcBuffer, WIN_W, 150, sy + 7, srows[i].desc, PackBGRA(180, 200, 230));
        DrawString(s_svcBuffer, WIN_W, 360, sy + 7, srows[i].status, PackBGRA(100, 240, 160));
        DrawString(s_svcBuffer, WIN_W, 440, sy + 7, srows[i].startup, PackBGRA(200, 220, 245));
        sy += 22;
    }

    // 4. Status Bar
    FillRect(s_svcBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_svcBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_svcBuffer, WIN_W, 12, WIN_H - 15, "8 Services Active | Service Control Manager (services.exe PID 704)", PackBGRA(180, 200, 230));

    PresentWindow(s_svcBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Services Management (services.msc):");
    PrintLine("  SCM Binding: \\RPC_Control\\epmapper -> services.exe (PID 704)");
    PrintLine("  Services Active: EventLog, LanmanServer, LanmanWorkstn, PlugPlay, RpcSs, SamSs");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderServicesWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
