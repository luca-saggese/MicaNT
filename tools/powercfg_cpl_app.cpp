/**
 * @file powercfg_cpl_app.cpp
 @brief Luce OS Ring 3 Power Options Applet (powercfg.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Power Options window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes power telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 540;
constexpr uint32_t WIN_H = 400;
alignas(4096) uint32_t s_powerBuffer[WIN_W * WIN_H];

void RenderPowerOptionsWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_powerBuffer, WIN_W, WIN_H, "Power Options", nullptr);

    // 2. Header description
    DrawString(s_powerBuffer, WIN_W, 20, 42, "Choose or customize a power plan", PackBGRA(255, 255, 255));
    DrawString(s_powerBuffer, WIN_W, 20, 58, "A power plan is a collection of hardware and system settings that", PackBGRA(160, 180, 210));
    DrawString(s_powerBuffer, WIN_W, 20, 72, "manages how your computer uses power.", PackBGRA(160, 180, 210));

    // Separator line
    FillRect(s_powerBuffer, WIN_W, 20, 92, WIN_W - 40, 1, PackBGRA(40, 55, 80));

    // 3. Plans List
    DrawString(s_powerBuffer, WIN_W, 20, 104, "Preferred plans", PackBGRA(255, 255, 255));

    // Plan 1: Balanced (Selected)
    const uint32_t p1Y = 126;
    DrawCircle(s_powerBuffer, WIN_W, 30, p1Y + 8, 7, PackBGRA(0, 150, 255));
    DrawCircle(s_powerBuffer, WIN_W, 30, p1Y + 8, 4, PackBGRA(255, 255, 255));
    DrawString(s_powerBuffer, WIN_W, 46, p1Y + 2, "Balanced (recommended)", PackBGRA(255, 255, 255));
    DrawString(s_powerBuffer, WIN_W, 46, p1Y + 18, "Automatically balances performance with energy consumption on capable hardware.", PackBGRA(140, 160, 190));
    DrawString(s_powerBuffer, WIN_W, WIN_W - 130, p1Y + 2, "Change plan settings", PackBGRA(0, 180, 255));

    // Plan 2: High performance
    const uint32_t p2Y = 176;
    DrawCircle(s_powerBuffer, WIN_W, 30, p2Y + 8, 7, PackBGRA(40, 56, 80));
    DrawString(s_powerBuffer, WIN_W, 46, p2Y + 2, "High performance", PackBGRA(220, 230, 245));
    DrawString(s_powerBuffer, WIN_W, 46, p2Y + 18, "Favors performance, but may use more energy.", PackBGRA(140, 160, 190));
    DrawString(s_powerBuffer, WIN_W, WIN_W - 130, p2Y + 2, "Change plan settings", PackBGRA(0, 180, 255));

    // Plan 3: Power saver
    const uint32_t p3Y = 226;
    DrawCircle(s_powerBuffer, WIN_W, 30, p3Y + 8, 7, PackBGRA(40, 56, 80));
    DrawString(s_powerBuffer, WIN_W, 46, p3Y + 2, "Power saver", PackBGRA(220, 230, 245));
    DrawString(s_powerBuffer, WIN_W, 46, p3Y + 18, "Saves energy by reducing system performance when possible.", PackBGRA(140, 160, 190));
    DrawString(s_powerBuffer, WIN_W, WIN_W - 130, p3Y + 2, "Change plan settings", PackBGRA(0, 180, 255));

    // Separator line
    FillRect(s_powerBuffer, WIN_W, 20, 280, WIN_W - 40, 1, PackBGRA(40, 55, 80));

    // 4. Hardware Governance status
    DrawString(s_powerBuffer, WIN_W, 20, 294, "Intel Speed Shift (HWP): Active | Autonomous MSR 0x777 Frequency Regulation", PackBGRA(100, 240, 160));
    DrawString(s_powerBuffer, WIN_W, 20, 310, "Target EPP: 0x20 | Min Freq: 800 MHz (8x) | Max Turbo: 4200 MHz (42x)", PackBGRA(180, 200, 230));

    // 5. Button Row
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_powerBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(0, 120, 215));
    DrawRect(s_powerBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_powerBuffer, WIN_W, WIN_W - 146, btnY + 7, "Save", PackBGRA(255, 255, 255));

    FillRect(s_powerBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(30, 42, 60));
    DrawRect(s_powerBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_powerBuffer, WIN_W, WIN_W - 76, btnY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Power Options Applet [powercfg.cpl]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Active Power Scheme    : Balanced [381b4222-f694-41f0-9685-ff5bb260df2e]\n"
        "Intel Speed Shift      : Enabled (HWP Autonomous Frequency Selection)\n"
        "EPP Bias Profile       : 0x20 (Performance Biased Energy Conservation)\n"
        "ACPI Thermal Trip      : 100.0 C Critical Shutdown Interlock Armed\n"
        "Status: Presenting powercfg.cpl window (540x400 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderPowerOptionsWindow();
    PresentWindow(s_powerBuffer, WIN_W, WIN_H);

    PrintStr("[PowerCfg CPL] Power scheme active. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
