/**
 * @file desk_app.cpp
 * Luce OS Ring 3 Display Settings Applet (desk.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Display Properties window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes video mode telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 540;
constexpr uint32_t WIN_H = 400;
alignas(4096) uint32_t s_deskBuffer[WIN_W * WIN_H];

void RenderDeskWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_deskBuffer, WIN_W, WIN_H, "Display Settings", nullptr);

    // 2. Monitor Diagram / Topology Visualizer (Centered Top)
    const uint32_t monW = 140;
    const uint32_t monH = 90;
    const uint32_t monX = (WIN_W - monW) / 2;
    const uint32_t monY = 46;

    // Monitor screen
    FillRect(s_deskBuffer, WIN_W, monX, monY, monW, monH, PackBGRA(0, 120, 215));
    DrawRect(s_deskBuffer, WIN_W, monX, monY, monW, monH, PackBGRA(255, 255, 255), 2);
    DrawString(s_deskBuffer, WIN_W, monX + 64, monY + 38, "1", PackBGRA(255, 255, 255));

    // Monitor stand
    FillRect(s_deskBuffer, WIN_W, monX + 55, monY + monH, 30, 16, PackBGRA(30, 42, 60));
    FillRect(s_deskBuffer, WIN_W, monX + 40, monY + monH + 16, 60, 4, PackBGRA(40, 56, 80));

    DrawString(s_deskBuffer, WIN_W, (WIN_W - 200) / 2, monY + monH + 26, "Supermicro ASPEED AST2400 VGA", PackBGRA(160, 180, 210));

    // 3. Settings Form Group (Y=186)
    const uint32_t formX = 30;
    uint32_t formY = 186;

    // Display resolution
    DrawString(s_deskBuffer, WIN_W, formX, formY, "Display resolution", PackBGRA(220, 230, 245));
    FillRect(s_deskBuffer, WIN_W, formX, formY + 16, 260, 24, PackBGRA(10, 14, 22));
    DrawRect(s_deskBuffer, WIN_W, formX, formY + 16, 260, 24, PackBGRA(40, 55, 80), 1);
    DrawString(s_deskBuffer, WIN_W, formX + 10, formY + 22, "1920 x 1080 (Recommended)", PackBGRA(255, 255, 255));
    DrawString(s_deskBuffer, WIN_W, formX + 240, formY + 22, "v", PackBGRA(180, 200, 230));

    // Refresh rate
    DrawString(s_deskBuffer, WIN_W, formX + 290, formY, "Refresh rate", PackBGRA(220, 230, 245));
    FillRect(s_deskBuffer, WIN_W, formX + 290, formY + 16, 180, 24, PackBGRA(10, 14, 22));
    DrawRect(s_deskBuffer, WIN_W, formX + 290, formY + 16, 180, 24, PackBGRA(40, 55, 80), 1);
    DrawString(s_deskBuffer, WIN_W, formX + 300, formY + 22, "60.000 Hz", PackBGRA(255, 255, 255));
    DrawString(s_deskBuffer, WIN_W, formX + 450, formY + 22, "v", PackBGRA(180, 200, 230));

    formY += 56;

    // Display orientation
    DrawString(s_deskBuffer, WIN_W, formX, formY, "Display orientation", PackBGRA(220, 230, 245));
    FillRect(s_deskBuffer, WIN_W, formX, formY + 16, 260, 24, PackBGRA(10, 14, 22));
    DrawRect(s_deskBuffer, WIN_W, formX, formY + 16, 260, 24, PackBGRA(40, 55, 80), 1);
    DrawString(s_deskBuffer, WIN_W, formX + 10, formY + 22, "Landscape", PackBGRA(255, 255, 255));
    DrawString(s_deskBuffer, WIN_W, formX + 240, formY + 22, "v", PackBGRA(180, 200, 230));

    // Scale & layout
    DrawString(s_deskBuffer, WIN_W, formX + 290, formY, "Scale", PackBGRA(220, 230, 245));
    FillRect(s_deskBuffer, WIN_W, formX + 290, formY + 16, 180, 24, PackBGRA(10, 14, 22));
    DrawRect(s_deskBuffer, WIN_W, formX + 290, formY + 16, 180, 24, PackBGRA(40, 55, 80), 1);
    DrawString(s_deskBuffer, WIN_W, formX + 300, formY + 22, "100% (Recommended)", PackBGRA(255, 255, 255));

    formY += 54;

    // Advanced display link
    DrawString(s_deskBuffer, WIN_W, formX, formY, "Advanced display settings", PackBGRA(0, 180, 255));
    DrawString(s_deskBuffer, WIN_W, formX + 290, formY, "Graphics settings", PackBGRA(0, 180, 255));

    // 4. Buttons Row [Apply] [Cancel]
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_deskBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(0, 120, 215));
    DrawRect(s_deskBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_deskBuffer, WIN_W, WIN_W - 152, btnY + 7, "Apply", PackBGRA(255, 255, 255));

    FillRect(s_deskBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(30, 42, 60));
    DrawRect(s_deskBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_deskBuffer, WIN_W, WIN_W - 76, btnY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Display Settings Subsystem [desk.cpl / desk.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Active Display Adapter : ASPEED AST2400 VGA (PCIe Bus 01, Dev 00, Func 0)\n"
        "Active GOP Mode        : 1920 x 1080 x 32-bpp BGRA (Mode #0)\n"
        "Scanout Refresh Rate   : 60.00 Hz (Hardware DirectFlip MPO Supported)\n"
        "Framebuffer Base       : 0x90000000 (16 MB Bar0 VRAM Allocation)\n"
        "Color Space            : sRGB IEC61966-2.1 | SDR 8-bit Per Channel\n"
        "Status: Presenting desk.cpl window (540x400 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderDeskWindow();
    PresentWindow(s_deskBuffer, WIN_W, WIN_H);

    PrintStr("[Desk] Display topology presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
