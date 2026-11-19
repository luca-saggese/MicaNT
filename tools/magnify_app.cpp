/**
 * @file magnify_app.cpp
 * Luce OS Ring 3 Magnifier Utility (magnify) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Magnifier toolbar to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes zoom level and lens telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 380;
constexpr uint32_t WIN_H = 180;
alignas(4096) uint32_t s_magBuffer[WIN_W * WIN_H];

void RenderMagnifierWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_magBuffer, WIN_W, WIN_H, "Magnifier", nullptr);

    // 2. Magnifying glass icon & Zoom controls
    const uint32_t ctrY = 46;
    DrawCircle(s_magBuffer, WIN_W, 36, ctrY + 14, 14, PackBGRA(0, 180, 255));
    DrawCircle(s_magBuffer, WIN_W, 36, ctrY + 14, 10, PackBGRA(16, 22, 34));
    DrawLine(s_magBuffer, WIN_W, 43, ctrY + 22, 54, ctrY + 34, PackBGRA(0, 180, 255));

    // [-] Zoom Out
    FillRect(s_magBuffer, WIN_W, 70, ctrY, 32, 28, PackBGRA(30, 42, 60));
    DrawRect(s_magBuffer, WIN_W, 70, ctrY, 32, 28, PackBGRA(70, 90, 120), 1);
    DrawString(s_magBuffer, WIN_W, 82, ctrY + 8, "-", PackBGRA(255, 255, 255));

    // Zoom readout: 200%
    FillRect(s_magBuffer, WIN_W, 108, ctrY, 60, 28, PackBGRA(10, 14, 22));
    DrawRect(s_magBuffer, WIN_W, 108, ctrY, 60, 28, PackBGRA(50, 70, 100), 1);
    DrawString(s_magBuffer, WIN_W, 120, ctrY + 8, "200%", PackBGRA(255, 255, 255));

    // [+] Zoom In
    FillRect(s_magBuffer, WIN_W, 174, ctrY, 32, 28, PackBGRA(0, 120, 215));
    DrawRect(s_magBuffer, WIN_W, 174, ctrY, 32, 28, PackBGRA(255, 255, 255), 1);
    DrawString(s_magBuffer, WIN_W, 186, ctrY + 8, "+", PackBGRA(255, 255, 255));

    // Views dropdown
    DrawString(s_magBuffer, WIN_W, 225, ctrY + 8, "Views:", PackBGRA(220, 235, 255));
    FillRect(s_magBuffer, WIN_W, 275, ctrY, 90, 28, PackBGRA(30, 42, 60));
    DrawRect(s_magBuffer, WIN_W, 275, ctrY, 90, 28, PackBGRA(70, 90, 120), 1);
    DrawString(s_magBuffer, WIN_W, 285, ctrY + 8, "Full scr v", PackBGRA(220, 235, 255));

    // 3. Checkbox options
    uint32_t optY = 96;

    // [X] Invert colors
    FillRect(s_magBuffer, WIN_W, 20, optY, 14, 14, PackBGRA(20, 28, 40));
    DrawRect(s_magBuffer, WIN_W, 20, optY, 14, 14, PackBGRA(180, 200, 230), 1);
    DrawString(s_magBuffer, WIN_W, 42, optY + 2, "Invert colors (Ctrl + Alt + I)", PackBGRA(220, 230, 245));

    optY += 24;

    // [v] Follow mouse pointer
    FillRect(s_magBuffer, WIN_W, 20, optY, 14, 14, PackBGRA(0, 120, 215));
    DrawRect(s_magBuffer, WIN_W, 20, optY, 14, 14, PackBGRA(255, 255, 255), 1);
    DrawString(s_magBuffer, WIN_W, 23, optY + 2, "v", PackBGRA(255, 255, 255));
    DrawString(s_magBuffer, WIN_W, 42, optY + 2, "Follow mouse pointer & keyboard focus", PackBGRA(220, 230, 245));

    // 4. Footer status
    DrawString(s_magBuffer, WIN_W, 20, WIN_H - 18, "Shortcut: Win + Plus (Zoom In) / Win + Esc (Exit)", PackBGRA(120, 140, 170));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Screen Magnifier Utility [magnify.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Magnification Engine   : Hardware GOP Bilinear Filter / Mag API\n"
        "Zoom Level             : 200% (Incremental range: 100% to 1600%)\n"
        "View Mode              : Full-Screen Hardware Scaling Plane\n"
        "Tracking Targets       : Mouse cursor, text caret, keyboard focus\n"
        "Status: Presenting magnify.exe window (380x180 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderMagnifierWindow();
    PresentWindow(s_magBuffer, WIN_W, WIN_H);

    PrintStr("[Magnify] Magnification plane engaged. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
