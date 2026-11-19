/**
 * @file firewall_app.cpp
 * Luce OS Ring 3 Windows Defender Firewall Applet (firewall.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Defender Firewall window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes packet filter telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 560;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_firewallBuffer[WIN_W * WIN_H];

void RenderFirewallWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_firewallBuffer, WIN_W, WIN_H, "MicaNT Sovereign Firewall", "File  Action  View  Help");

    // 2. Left Sidebar (Navigation Tasks)
    const uint32_t sideW = 145;
    FillRect(s_firewallBuffer, WIN_W, 1, 50, sideW, WIN_H - 51, PackBGRA(16, 22, 34));
    FillRect(s_firewallBuffer, WIN_W, sideW, 50, 1, WIN_H - 51, PackBGRA(35, 48, 70));

    DrawString(s_firewallBuffer, WIN_W, 12, 60, "Control Panel Home", PackBGRA(100, 200, 255));
    DrawString(s_firewallBuffer, WIN_W, 12, 84, "Allow an app or feature", PackBGRA(180, 200, 230));
    DrawString(s_firewallBuffer, WIN_W, 12, 108, "Change notification", PackBGRA(180, 200, 230));
    DrawString(s_firewallBuffer, WIN_W, 12, 122, "settings", PackBGRA(180, 200, 230));
    DrawString(s_firewallBuffer, WIN_W, 12, 146, "Turn MicaNT Firewall", PackBGRA(180, 200, 230));
    DrawString(s_firewallBuffer, WIN_W, 12, 160, "on or off", PackBGRA(180, 200, 230));
    DrawString(s_firewallBuffer, WIN_W, 12, 184, "Advanced settings", PackBGRA(100, 200, 255));

    // 3. Main Area: Shield Banner & Title
    const uint32_t mainX = sideW + 12;

    // Green Shield icon
    FillRect(s_firewallBuffer, WIN_W, mainX, 58, 28, 32, PackBGRA(0, 180, 80));
    DrawRect(s_firewallBuffer, WIN_W, mainX, 58, 28, 32, PackBGRA(255, 255, 255), 1);
    DrawString(s_firewallBuffer, WIN_W, mainX + 9, 66, "v", PackBGRA(255, 255, 255));

    DrawString(s_firewallBuffer, WIN_W, mainX + 38, 58, "MicaNT Sovereign Firewall", PackBGRA(255, 255, 255));
    DrawString(s_firewallBuffer, WIN_W, mainX + 38, 74, "Help protect your PC with MicaNT Sovereign Firewall", PackBGRA(160, 180, 210));

    // Separator line
    FillRect(s_firewallBuffer, WIN_W, mainX, 98, WIN_W - mainX - 10, 1, PackBGRA(40, 55, 80));

    // 4. Private / Domain Networks Section
    uint32_t secY = 108;
    DrawString(s_firewallBuffer, WIN_W, mainX, secY, "Private networks", PackBGRA(255, 255, 255));
    DrawString(s_firewallBuffer, WIN_W, WIN_W - 85, secY, "Connected", PackBGRA(100, 240, 160));

    FillRect(s_firewallBuffer, WIN_W, mainX, secY + 18, WIN_W - mainX - 10, 86, PackBGRA(10, 14, 22));
    DrawRect(s_firewallBuffer, WIN_W, mainX, secY + 18, WIN_W - mainX - 10, 86, PackBGRA(35, 48, 70), 1);

    DrawString(s_firewallBuffer, WIN_W, mainX + 10, secY + 26, "MicaNT Sovereign Firewall state:", PackBGRA(200, 215, 235));
    DrawString(s_firewallBuffer, WIN_W, mainX + 260, secY + 26, "On", PackBGRA(100, 240, 160));

    DrawString(s_firewallBuffer, WIN_W, mainX + 10, secY + 44, "Incoming connections:", PackBGRA(200, 215, 235));
    DrawString(s_firewallBuffer, WIN_W, mainX + 175, secY + 44, "Block all connections to apps not listed", PackBGRA(160, 180, 210));

    DrawString(s_firewallBuffer, WIN_W, mainX + 10, secY + 62, "Active private networks:", PackBGRA(200, 215, 235));
    DrawString(s_firewallBuffer, WIN_W, mainX + 190, secY + 62, "Intel I210 (147.135.1.0/24)", PackBGRA(100, 200, 255));

    DrawString(s_firewallBuffer, WIN_W, mainX + 10, secY + 80, "Notification state:", PackBGRA(200, 215, 235));
    DrawString(s_firewallBuffer, WIN_W, mainX + 155, secY + 80, "Notify when a new app is blocked", PackBGRA(160, 180, 210));

    // 5. Guest or Public Networks Section
    secY += 114;
    DrawString(s_firewallBuffer, WIN_W, mainX, secY, "Guest or public networks", PackBGRA(255, 255, 255));
    DrawString(s_firewallBuffer, WIN_W, WIN_W - 110, secY, "Not connected", PackBGRA(160, 180, 210));

    FillRect(s_firewallBuffer, WIN_W, mainX, secY + 18, WIN_W - mainX - 10, 48, PackBGRA(10, 14, 22));
    DrawRect(s_firewallBuffer, WIN_W, mainX, secY + 18, WIN_W - mainX - 10, 48, PackBGRA(35, 48, 70), 1);

    DrawString(s_firewallBuffer, WIN_W, mainX + 10, secY + 26, "MicaNT Sovereign Firewall state:", PackBGRA(200, 215, 235));
    DrawString(s_firewallBuffer, WIN_W, mainX + 260, secY + 26, "On", PackBGRA(100, 240, 160));
    DrawString(s_firewallBuffer, WIN_W, mainX + 10, secY + 44, "Incoming connections:", PackBGRA(200, 215, 235));
    DrawString(s_firewallBuffer, WIN_W, mainX + 175, secY + 44, "Block all connections to apps not listed", PackBGRA(160, 180, 210));

    // 6. Footer Button Row
    const uint32_t btnY = WIN_H - 38;
    FillRect(s_firewallBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(0, 120, 215));
    DrawRect(s_firewallBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_firewallBuffer, WIN_W, WIN_W - 74, btnY + 7, "Close", PackBGRA(255, 255, 255));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Sovereign Firewall Subsystem [firewall.cpl / firewall.exe]\n"
        "(C) 2026 MicaNT Foundation. All rights reserved.\n\n"
        "Firewall Engine        : Sovereign Base Filtering Engine (BFE / WFP)\n"
        "Hardware NIC Binding   : Intel I210 Gigabit Ethernet (147.135.1.229)\n"
        "Active Profiles        : Domain=ON, Private=ON, Public=ON\n"
        "Inbound Policy         : BlockInbound (Allow authorized TCP ports: 22, 23, 80, 443)\n"
        "Outbound Policy        : AllowOutbound (Full ICMP Echo & IPv4 Gateway Egress)\n"
        "Status: Presenting firewall.cpl window (560x420 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderFirewallWindow();
    PresentWindow(s_firewallBuffer, WIN_W, WIN_H);

    PrintStr("[Firewall] Firewall state active. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
