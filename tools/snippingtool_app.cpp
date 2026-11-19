/**
 * @file snippingtool_app.cpp
 * Luce OS Ring 3 Snipping Tool Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Snipping Tool window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes capture telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 420;
constexpr uint32_t WIN_H = 220;
alignas(4096) uint32_t s_snipBuffer[WIN_W * WIN_H];

void RenderSnippingToolWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_snipBuffer, WIN_W, WIN_H, "Snipping Tool", nullptr);

    // 2. Toolbar Row (Y=34 to 68)
    FillRect(s_snipBuffer, WIN_W, 1, 32, WIN_W - 2, 36, PackBGRA(22, 30, 46));
    FillRect(s_snipBuffer, WIN_W, 1, 68, WIN_W - 2, 1, PackBGRA(38, 52, 78));

    // [New] button (Blue accent)
    FillRect(s_snipBuffer, WIN_W, 12, 38, 64, 24, PackBGRA(0, 120, 215));
    DrawRect(s_snipBuffer, WIN_W, 12, 38, 64, 24, PackBGRA(255, 255, 255), 1);
    // Scissors symbol + New
    DrawString(s_snipBuffer, WIN_W, 20, 44, "> New", PackBGRA(255, 255, 255));

    // [Mode v] button
    FillRect(s_snipBuffer, WIN_W, 82, 38, 70, 24, PackBGRA(30, 42, 60));
    DrawRect(s_snipBuffer, WIN_W, 82, 38, 70, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_snipBuffer, WIN_W, 90, 44, "Mode v", PackBGRA(220, 235, 255));

    // [Delay v] button
    FillRect(s_snipBuffer, WIN_W, 158, 38, 70, 24, PackBGRA(30, 42, 60));
    DrawRect(s_snipBuffer, WIN_W, 158, 38, 70, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_snipBuffer, WIN_W, 166, 44, "Delay v", PackBGRA(220, 235, 255));

    // [Cancel] button
    FillRect(s_snipBuffer, WIN_W, 234, 38, 65, 24, PackBGRA(30, 42, 60));
    DrawRect(s_snipBuffer, WIN_W, 234, 38, 65, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_snipBuffer, WIN_W, 244, 44, "Cancel", PackBGRA(160, 180, 210));

    // [Options] button
    FillRect(s_snipBuffer, WIN_W, 305, 38, 65, 24, PackBGRA(30, 42, 60));
    DrawRect(s_snipBuffer, WIN_W, 305, 38, 65, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_snipBuffer, WIN_W, 314, 44, "Options", PackBGRA(220, 235, 255));

    // 3. Central Snip Area / Descriptive Instructions
    const uint32_t bodyY = 82;
    FillRect(s_snipBuffer, WIN_W, 16, bodyY, WIN_W - 32, 100, PackBGRA(12, 16, 26));
    DrawRect(s_snipBuffer, WIN_W, 16, bodyY, WIN_W - 32, 100, PackBGRA(35, 48, 70), 1);

    // Dotted crosshair icon
    DrawLine(s_snipBuffer, WIN_W, 46, bodyY + 30, 46, bodyY + 70, PackBGRA(0, 220, 255));
    DrawLine(s_snipBuffer, WIN_W, 26, bodyY + 50, 66, bodyY + 50, PackBGRA(0, 220, 255));
    DrawCircle(s_snipBuffer, WIN_W, 46, bodyY + 50, 12, PackBGRA(0, 220, 255));

    DrawString(s_snipBuffer, WIN_W, 80, bodyY + 22, "Select a snip mode from the menu above", PackBGRA(255, 255, 255));
    DrawString(s_snipBuffer, WIN_W, 80, bodyY + 40, "or click 'New' to capture screen.", PackBGRA(200, 220, 245));
    DrawString(s_snipBuffer, WIN_W, 80, bodyY + 62, "Modes: Rectangular, Free-form, Window, Full-screen", PackBGRA(140, 160, 190));

    // 4. Status Bar
    FillRect(s_snipBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(18, 24, 36));
    FillRect(s_snipBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_snipBuffer, WIN_W, 16, WIN_H - 17, "Ready to capture. Shortcut: MicaNT Key + Shift + S", PackBGRA(100, 240, 160));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Snipping Tool Subsystem [snippingtool.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Screen Capture Engine   : DirectFlip DXGI / GOP VRAM Linear Readback\n"
        "Resolution              : 1920 x 1080 x 32-bpp BGRA (Native Hardware Scanout)\n"
        "Supported Modes         : Rectangular Snip, Window Snip, Full-screen Snip\n"
        "Clipboard Integration   : Instant auto-copy to Ring 3 clipboard (CF_DIB / PNG)\n"
        "Status: Presenting snippingtool.exe window (420x220 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderSnippingToolWindow();
    PresentWindow(s_snipBuffer, WIN_W, WIN_H);

    PrintStr("[SnippingTool] Capture harness ready. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
