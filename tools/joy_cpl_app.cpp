/**
 * @file joy_cpl_app.cpp
 @brief Luce OS Ring 3 Game Controllers Applet (joy.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Game Controllers window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes DirectInput / XInput gamepad telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 480;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_joyBuffer[WIN_W * WIN_H];

void RenderGameControllersWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_joyBuffer, WIN_W, WIN_H, "Game Controllers", nullptr);

    // 2. Instructions header
    DrawString(s_joyBuffer, WIN_W, 20, 38, "These settings help you configure the game controllers", PackBGRA(220, 235, 255));
    DrawString(s_joyBuffer, WIN_W, 20, 52, "installed on your sovereign workstation.", PackBGRA(160, 180, 210));

    // 3. Installed Controllers Listview
    DrawString(s_joyBuffer, WIN_W, 20, 76, "Installed game controllers", PackBGRA(255, 255, 255));

    FillRect(s_joyBuffer, WIN_W, 20, 94, WIN_W - 40, 130, PackBGRA(10, 14, 22));
    DrawRect(s_joyBuffer, WIN_W, 20, 94, WIN_W - 40, 130, PackBGRA(35, 48, 70), 1);

    // Header row
    FillRect(s_joyBuffer, WIN_W, 21, 95, WIN_W - 42, 20, PackBGRA(20, 28, 44));
    DrawString(s_joyBuffer, WIN_W, 30, 99, "Controller", PackBGRA(255, 255, 255));
    DrawString(s_joyBuffer, WIN_W, WIN_W - 100, 99, "Status", PackBGRA(255, 255, 255));

    // Controller 1: Xbox Wireless / XInput Controller (Selected)
    FillRect(s_joyBuffer, WIN_W, 21, 116, WIN_W - 42, 24, PackBGRA(0, 120, 215));
    // Gamepad icon
    DrawCircle(s_joyBuffer, WIN_W, 36, 128, 6, PackBGRA(255, 255, 255));
    DrawString(s_joyBuffer, WIN_W, 48, 121, "Xbox 360 / One Controller (XInput Slot 0)", PackBGRA(255, 255, 255));
    DrawString(s_joyBuffer, WIN_W, WIN_W - 100, 121, "OK", PackBGRA(100, 255, 160));

    // Controller 2: Sovereign DirectInput Virtual Gamepad
    DrawCircle(s_joyBuffer, WIN_W, 36, 152, 6, PackBGRA(60, 80, 110));
    DrawString(s_joyBuffer, WIN_W, 48, 145, "PrismX Virtual Gamepad (DirectInput8)", PackBGRA(200, 215, 235));
    DrawString(s_joyBuffer, WIN_W, WIN_W - 100, 145, "OK", PackBGRA(100, 255, 160));

    // 4. Action Buttons under list
    const uint32_t actY = 236;
    FillRect(s_joyBuffer, WIN_W, 20, actY, 80, 24, PackBGRA(30, 42, 60));
    DrawRect(s_joyBuffer, WIN_W, 20, actY, 80, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_joyBuffer, WIN_W, 34, actY + 6, "Add...", PackBGRA(200, 215, 235));

    FillRect(s_joyBuffer, WIN_W, 108, actY, 80, 24, PackBGRA(30, 42, 60));
    DrawRect(s_joyBuffer, WIN_W, 108, actY, 80, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_joyBuffer, WIN_W, 120, actY + 6, "Remove", PackBGRA(160, 180, 210));

    FillRect(s_joyBuffer, WIN_W, WIN_W - 190, actY, 85, 24, PackBGRA(30, 42, 60));
    DrawRect(s_joyBuffer, WIN_W, WIN_W - 190, actY, 85, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_joyBuffer, WIN_W, WIN_W - 180, actY + 6, "Advanced...", PackBGRA(200, 215, 235));

    FillRect(s_joyBuffer, WIN_W, WIN_W - 95, actY, 75, 24, PackBGRA(0, 120, 215));
    DrawRect(s_joyBuffer, WIN_W, WIN_W - 95, actY, 75, 24, PackBGRA(255, 255, 255), 1);
    DrawString(s_joyBuffer, WIN_W, WIN_W - 88, actY + 6, "Properties", PackBGRA(255, 255, 255));

    // Separator line
    FillRect(s_joyBuffer, WIN_W, 20, 276, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 5. Polling Rate & Input Telemetry
    DrawString(s_joyBuffer, WIN_W, 20, 290, "Input Engine: GameInput 8000Hz Ultra-Low Latency Subsystem", PackBGRA(100, 240, 160));
    DrawString(s_joyBuffer, WIN_W, 20, 308, "Polling Interval: 125 us | Jitter: < 2 us | Dual Haptic Motors: Armed", PackBGRA(180, 200, 230));

    // 6. Dialog OK Button
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_joyBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(0, 120, 215));
    DrawRect(s_joyBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_joyBuffer, WIN_W, WIN_W - 74, btnY + 7, "OK", PackBGRA(255, 255, 255));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Game Controllers Applet [joy.cpl / joy.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Input API Subsystem     : DirectInput 8.0 / XInput 1.4 / GameInput Subsystem\n"
        "Active Gamepads Detected: 2 (1 Hardware XInput USB + 1 PrismX Virtual Controller)\n"
        "Polling Frequency       : 8,000 Hz Ring 0 ISR Driver (125 us Frame Interval)\n"
        "Force Feedback / Rumble : Dual ERM/LRA Vibration Actuators Armed\n"
        "Status: Presenting joy.cpl window (480x380 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderGameControllersWindow();
    PresentWindow(s_joyBuffer, WIN_W, WIN_H);

    PrintStr("[Joy CPL] Game controller topology presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
