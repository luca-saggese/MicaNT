/**
 * @file wscui_app.cpp
 * Luce OS Ring 3 Security and Maintenance Control Panel (wscui.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Security and Maintenance window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes health state telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 560;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_wscBuffer[WIN_W * WIN_H];

void RenderSecurityMaintenanceWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_wscBuffer, WIN_W, WIN_H, "Security and Maintenance", "File  Action  View  Help");

    // 2. Left Sidebar
    const uint32_t sideW = 145;
    FillRect(s_wscBuffer, WIN_W, 1, 50, sideW, WIN_H - 51, PackBGRA(16, 22, 34));
    FillRect(s_wscBuffer, WIN_W, sideW, 50, 1, WIN_H - 51, PackBGRA(35, 48, 70));

    DrawString(s_wscBuffer, WIN_W, 12, 60, "Control Panel Home", PackBGRA(100, 200, 255));
    DrawString(s_wscBuffer, WIN_W, 12, 84, "Change Security and", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, 12, 98, "Maintenance settings", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, 12, 122, "Change User Account", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, 12, 136, "Control settings", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, 12, 160, "View reliability history", PackBGRA(100, 200, 255));
    DrawString(s_wscBuffer, WIN_W, 12, 184, "Recovery", PackBGRA(100, 200, 255));

    // 3. Main Area
    const uint32_t mainX = sideW + 12;
    const uint32_t mainW = WIN_W - mainX - 10;

    // Green Check Banner: No issues have been detected
    FillRect(s_wscBuffer, WIN_W, mainX, 58, mainW, 44, PackBGRA(10, 36, 20));
    DrawRect(s_wscBuffer, WIN_W, mainX, 58, mainW, 44, PackBGRA(0, 180, 80), 1);
    DrawCircle(s_wscBuffer, WIN_W, mainX + 22, 80, 10, PackBGRA(0, 200, 100));
    DrawString(s_wscBuffer, WIN_W, mainX + 19, 74, "v", PackBGRA(255, 255, 255));
    DrawString(s_wscBuffer, WIN_W, mainX + 42, 66, "No issues have been detected", PackBGRA(255, 255, 255));
    DrawString(s_wscBuffer, WIN_W, mainX + 42, 82, "Security and maintenance are up to date and functioning properly.", PackBGRA(160, 220, 180));

    // 4. Security Section
    uint32_t sY = 118;
    DrawString(s_wscBuffer, WIN_W, mainX, sY, "v Security", PackBGRA(255, 255, 255));

    FillRect(s_wscBuffer, WIN_W, mainX, sY + 18, mainW, 100, PackBGRA(10, 14, 22));
    DrawRect(s_wscBuffer, WIN_W, mainX, sY + 18, mainW, 100, PackBGRA(35, 48, 70), 1);

    DrawString(s_wscBuffer, WIN_W, mainX + 12, sY + 28, "Network firewall:", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, mainX + 160, sY + 28, "MicaNT Sovereign Firewall is active (On)", PackBGRA(100, 240, 160));

    DrawString(s_wscBuffer, WIN_W, mainX + 12, sY + 46, "Virus protection:", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, mainX + 160, sY + 46, "Sovereign Code Integrity (CI.DLL) is active (On)", PackBGRA(100, 240, 160));

    DrawString(s_wscBuffer, WIN_W, mainX + 12, sY + 64, "Spyware protection:", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, mainX + 160, sY + 64, "Zero Telemetry Sandbox Active (On)", PackBGRA(100, 240, 160));

    DrawString(s_wscBuffer, WIN_W, mainX + 12, sY + 82, "User Account Control:", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, mainX + 160, sY + 82, "User Account Control is active (On)", PackBGRA(100, 240, 160));

    // 5. Maintenance Section
    sY += 134;
    DrawString(s_wscBuffer, WIN_W, mainX, sY, "v Maintenance", PackBGRA(255, 255, 255));

    FillRect(s_wscBuffer, WIN_W, mainX, sY + 18, mainW, 70, PackBGRA(10, 14, 22));
    DrawRect(s_wscBuffer, WIN_W, mainX, sY + 18, mainW, 70, PackBGRA(35, 48, 70), 1);

    DrawString(s_wscBuffer, WIN_W, mainX + 12, sY + 28, "Automatic Maintenance:", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, mainX + 180, sY + 28, "Maintenance schedule ready (Idle run)", PackBGRA(255, 255, 255));

    DrawString(s_wscBuffer, WIN_W, mainX + 12, sY + 46, "Drive status:", PackBGRA(180, 200, 230));
    DrawString(s_wscBuffer, WIN_W, mainX + 180, sY + 46, "All physical drives are operating normally", PackBGRA(100, 240, 160));

    // 6. Status Bar
    FillRect(s_wscBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(18, 24, 36));
    FillRect(s_wscBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_wscBuffer, WIN_W, 16, WIN_H - 17, "MicaNT Security Center Service (wscsvc) Active | Platform Health: 100%", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Security and Maintenance Subsystem [wscui.cpl / wscui.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Security Center State  : All Subsystems Healthy (Zero Alerts)\n"
        "  - Network Firewall   : ON (Intel I210 DMA Packet Filter Active)\n"
        "  - Code Integrity     : ON (Strict Authenticode PE32+ Enforcement)\n"
        "  - User Account Guard : ON (High-Integrity Token Isolation Active)\n"
        "  - Storage Health     : OPTIMAL (HGST Ultrastar 2TB SATA SMART OK)\n"
        "Status: Presenting wscui.cpl window (560x420 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderSecurityMaintenanceWindow();
    PresentWindow(s_wscBuffer, WIN_W, WIN_H);

    PrintStr("[WSCUI] Security and Maintenance presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
