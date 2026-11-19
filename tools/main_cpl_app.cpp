/**
 * @file main_cpl_app.cpp
 * Luce OS Ring 3 Mouse Properties Applet (main.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Mouse Properties window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes pointer telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 480;
constexpr uint32_t WIN_H = 400;
alignas(4096) uint32_t s_mouseBuffer[WIN_W * WIN_H];

void RenderMousePropertiesWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_mouseBuffer, WIN_W, WIN_H, "Mouse Properties", nullptr);

    // 2. Tabs Row: [Buttons] [Pointers] [Pointer Options] [Wheel] [Hardware]
    const uint32_t tabY = 36;
    FillRect(s_mouseBuffer, WIN_W, 20, tabY, 70, 22, PackBGRA(30, 42, 60));
    DrawRect(s_mouseBuffer, WIN_W, 20, tabY, 70, 22, PackBGRA(0, 150, 255), 1);
    DrawString(s_mouseBuffer, WIN_W, 28, tabY + 5, "Buttons", PackBGRA(255, 255, 255));

    DrawString(s_mouseBuffer, WIN_W, 102, tabY + 5, "Pointers", PackBGRA(160, 180, 210));
    DrawString(s_mouseBuffer, WIN_W, 175, tabY + 5, "Pointer Options", PackBGRA(160, 180, 210));
    DrawString(s_mouseBuffer, WIN_W, 305, tabY + 5, "Wheel", PackBGRA(160, 180, 210));
    DrawString(s_mouseBuffer, WIN_W, 365, tabY + 5, "Hardware", PackBGRA(160, 180, 210));

    FillRect(s_mouseBuffer, WIN_W, 20, tabY + 22, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 3. Button Configuration
    DrawString(s_mouseBuffer, WIN_W, 20, 68, "Button configuration", PackBGRA(255, 255, 255));

    // Checkbox: Switch primary and secondary buttons
    FillRect(s_mouseBuffer, WIN_W, 20, 88, 14, 14, PackBGRA(12, 16, 24));
    DrawRect(s_mouseBuffer, WIN_W, 20, 88, 14, 14, PackBGRA(50, 70, 100), 1);
    DrawString(s_mouseBuffer, WIN_W, 42, 90, "Switch primary and secondary buttons", PackBGRA(220, 235, 255));
    DrawString(s_mouseBuffer, WIN_W, 42, 106, "Select this check box to make the right button the one you", PackBGRA(140, 160, 190));
    DrawString(s_mouseBuffer, WIN_W, 42, 120, "use for primary functions such as selecting and dragging.", PackBGRA(140, 160, 190));

    // Separator line
    FillRect(s_mouseBuffer, WIN_W, 20, 142, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 4. Double-click speed section
    DrawString(s_mouseBuffer, WIN_W, 20, 154, "Double-click speed", PackBGRA(255, 255, 255));
    DrawString(s_mouseBuffer, WIN_W, 20, 172, "Double-click the folder to test your setting.", PackBGRA(160, 180, 210));

    // Speed Slider
    DrawString(s_mouseBuffer, WIN_W, 20, 198, "Slow", PackBGRA(140, 160, 190));
    FillRect(s_mouseBuffer, WIN_W, 60, 202, 160, 4, PackBGRA(30, 42, 60));
    // Slider thumb (centered)
    FillRect(s_mouseBuffer, WIN_W, 140, 194, 8, 18, PackBGRA(0, 180, 255));
    DrawRect(s_mouseBuffer, WIN_W, 140, 194, 8, 18, PackBGRA(255, 255, 255), 1);
    DrawString(s_mouseBuffer, WIN_W, 230, 198, "Fast", PackBGRA(140, 160, 190));

    // Test folder icon on right
    const uint32_t fX = 320;
    const uint32_t fY = 164;
    FillRect(s_mouseBuffer, WIN_W, fX, fY, 44, 40, PackBGRA(255, 185, 0));
    DrawRect(s_mouseBuffer, WIN_W, fX, fY, 44, 40, PackBGRA(255, 255, 255), 1);
    FillRect(s_mouseBuffer, WIN_W, fX, fY, 18, 8, PackBGRA(255, 200, 50));
    DrawString(s_mouseBuffer, WIN_W, fX - 10, fY + 48, "Test Folder", PackBGRA(180, 200, 230));

    // Separator line
    FillRect(s_mouseBuffer, WIN_W, 20, 240, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 5. ClickLock section
    DrawString(s_mouseBuffer, WIN_W, 20, 252, "ClickLock", PackBGRA(255, 255, 255));
    FillRect(s_mouseBuffer, WIN_W, 20, 272, 14, 14, PackBGRA(12, 16, 24));
    DrawRect(s_mouseBuffer, WIN_W, 20, 272, 14, 14, PackBGRA(50, 70, 100), 1);
    DrawString(s_mouseBuffer, WIN_W, 42, 274, "Turn on ClickLock", PackBGRA(220, 235, 255));
    DrawString(s_mouseBuffer, WIN_W, 42, 290, "Enables you to highlight or drag without holding down mouse button.", PackBGRA(140, 160, 190));

    // 6. Dialog Buttons Row
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_mouseBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(0, 120, 215));
    DrawRect(s_mouseBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_mouseBuffer, WIN_W, WIN_W - 220, btnY + 7, "OK", PackBGRA(255, 255, 255));

    FillRect(s_mouseBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_mouseBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_mouseBuffer, WIN_W, WIN_W - 146, btnY + 7, "Cancel", PackBGRA(200, 215, 235));

    FillRect(s_mouseBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_mouseBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_mouseBuffer, WIN_W, WIN_W - 66, btnY + 7, "Apply", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Mouse Properties Applet [main.cpl / main.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Pointer Hardware       : USB HID / AMI Virtual Mouse Pointer\n"
        "Pointer Resolution     : 1000 DPI Hardware Optical Sensor\n"
        "Double-Click Threshold : 500 ms (Double-click detection interval)\n"
        "Hardware Cursor Engine : GOP DirectFlip Hardware Sprite (540Hz Polling)\n"
        "Status: Presenting main.cpl window (480x400 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderMousePropertiesWindow();
    PresentWindow(s_mouseBuffer, WIN_W, WIN_H);

    PrintStr("[Main CPL] Mouse properties presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
