/**
 * @file hdwwiz_app.cpp
 @brief Luce OS Ring 3 Add Hardware Wizard (hdwwiz.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Add Hardware Wizard window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes PnP hardware discovery telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 500;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_hdwBuffer[WIN_W * WIN_H];

void RenderAddHardwareWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_hdwBuffer, WIN_W, WIN_H, "Add Hardware Wizard", nullptr);

    // 2. Banner Header
    DrawString(s_hdwBuffer, WIN_W, 20, 38, "Welcome to the Add Hardware Wizard", PackBGRA(255, 255, 255));
    DrawString(s_hdwBuffer, WIN_W, 20, 54, "This wizard helps you install hardware connected to your computer.", PackBGRA(160, 180, 210));

    // Separator line
    FillRect(s_hdwBuffer, WIN_W, 20, 74, WIN_W - 40, 1, PackBGRA(40, 56, 80));

    // 3. Selection Radio Group
    DrawString(s_hdwBuffer, WIN_W, 20, 86, "What do you want the wizard to do?", PackBGRA(220, 235, 255));

    // Radio 1: Search and install automatically (Selected)
    const uint32_t rX = 26;
    uint32_t rY = 110;
    DrawCircle(s_hdwBuffer, WIN_W, rX, rY + 6, 7, PackBGRA(0, 150, 255));
    DrawCircle(s_hdwBuffer, WIN_W, rX, rY + 6, 4, PackBGRA(255, 255, 255));
    DrawString(s_hdwBuffer, WIN_W, rX + 16, rY, "Search for and install the hardware automatically (Recommended)", PackBGRA(255, 255, 255));

    rY += 34;

    // Radio 2: Install manually from a list
    DrawCircle(s_hdwBuffer, WIN_W, rX, rY + 6, 7, PackBGRA(40, 56, 80));
    DrawString(s_hdwBuffer, WIN_W, rX + 16, rY, "Install the hardware that I manually select from a list (Advanced)", PackBGRA(220, 235, 255));

    // Separator line
    FillRect(s_hdwBuffer, WIN_W, 20, 170, WIN_W - 40, 1, PackBGRA(40, 56, 80));

    // 4. Detected Hardware Categories
    DrawString(s_hdwBuffer, WIN_W, 20, 182, "Common hardware types detected via ACPI & PCIe Enumerator:", PackBGRA(180, 200, 230));

    FillRect(s_hdwBuffer, WIN_W, 20, 202, WIN_W - 40, 80, PackBGRA(10, 14, 22));
    DrawRect(s_hdwBuffer, WIN_W, 20, 202, WIN_W - 40, 80, PackBGRA(35, 48, 70), 1);

    DrawString(s_hdwBuffer, WIN_W, 30, 210, "[*] Display Adapters      : ASPEED AST2400 VGA (PCIe Bus 01, Dev 00)", PackBGRA(220, 235, 255));
    DrawString(s_hdwBuffer, WIN_W, 30, 228, "[*] Network Adapters      : Intel I210 Gigabit Ethernet (PCIe Bus 02, Dev 00)", PackBGRA(220, 235, 255));
    DrawString(s_hdwBuffer, WIN_W, 30, 246, "[*] Storage Controllers   : Intel Sunrise Point AHCI SATA Controller (Port 1)", PackBGRA(220, 235, 255));
    DrawString(s_hdwBuffer, WIN_W, 30, 264, "[*] System Devices        : Intel Xeon E3-1270 v6 Host Bridge & Local APIC", PackBGRA(100, 240, 160));

    // 5. Button Row: [Back] [Next >] [Cancel]
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_hdwBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_hdwBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_hdwBuffer, WIN_W, WIN_W - 224, btnY + 7, "< Back", PackBGRA(160, 180, 210));

    FillRect(s_hdwBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(0, 120, 215));
    DrawRect(s_hdwBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_hdwBuffer, WIN_W, WIN_W - 146, btnY + 7, "Next >", PackBGRA(255, 255, 255));

    FillRect(s_hdwBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_hdwBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_hdwBuffer, WIN_W, WIN_W - 66, btnY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Add Hardware Wizard [hdwwiz.cpl / hdwwiz.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Plug and Play Manager   : Active (PCIe Configuration Space Root Enumeration)\n"
        "Silicon Platform        : Supermicro X11SSH-F (Intel C236 Platform Controller Hub)\n"
        "Hardware Drivers Loaded : 9 Kernel Drivers (.sys) Active\n"
        "Status: Presenting hdwwiz.cpl wizard window (500x380 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderAddHardwareWindow();
    PresentWindow(s_hdwBuffer, WIN_W, WIN_H);

    PrintStr("[HdwWiz] Hardware wizard presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
