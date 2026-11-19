/**
 * @file sysprep_app.cpp
 * Luce OS Ring 3 System Preparation Tool (sysprep) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows System Preparation Tool window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes image generalization telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 440;
constexpr uint32_t WIN_H = 280;
alignas(4096) uint32_t s_spBuffer[WIN_W * WIN_H];

void RenderSysprepWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_spBuffer, WIN_W, WIN_H, "System Preparation Tool 3.14", nullptr);

    // 2. Descriptive header
    DrawString(s_spBuffer, WIN_W, 20, 38, "System Preparation Tool (Sysprep) prepares the machine", PackBGRA(255, 255, 255));
    DrawString(s_spBuffer, WIN_W, 20, 52, "for hardware-independent imaging or delivery to a customer.", PackBGRA(160, 180, 210));

    // Separator line
    FillRect(s_spBuffer, WIN_W, 20, 72, WIN_W - 40, 1, PackBGRA(40, 56, 80));

    // 3. System Cleanup Action
    DrawString(s_spBuffer, WIN_W, 20, 84, "System Cleanup Action", PackBGRA(220, 235, 255));

    FillRect(s_spBuffer, WIN_W, 20, 102, WIN_W - 40, 24, PackBGRA(10, 14, 22));
    DrawRect(s_spBuffer, WIN_W, 20, 102, WIN_W - 40, 24, PackBGRA(0, 150, 255), 1);
    DrawString(s_spBuffer, WIN_W, 28, 108, "Enter System Out-of-Box Experience (OOBE)", PackBGRA(255, 255, 255));
    DrawString(s_spBuffer, WIN_W, WIN_W - 40, 108, "v", PackBGRA(160, 180, 210));

    // Generalize Checkbox
    FillRect(s_spBuffer, WIN_W, 20, 136, 14, 14, PackBGRA(0, 120, 215));
    DrawRect(s_spBuffer, WIN_W, 20, 136, 14, 14, PackBGRA(255, 255, 255), 1);
    DrawString(s_spBuffer, WIN_W, 23, 138, "v", PackBGRA(255, 255, 255));
    DrawString(s_spBuffer, WIN_W, 42, 138, "Generalize (Remove system-specific SIDs and drivers)", PackBGRA(220, 235, 255));

    // 4. Shutdown Options
    DrawString(s_spBuffer, WIN_W, 20, 164, "Shutdown Options", PackBGRA(220, 235, 255));

    FillRect(s_spBuffer, WIN_W, 20, 182, WIN_W - 40, 24, PackBGRA(10, 14, 22));
    DrawRect(s_spBuffer, WIN_W, 20, 182, WIN_W - 40, 24, PackBGRA(50, 70, 100), 1);
    DrawString(s_spBuffer, WIN_W, 28, 188, "Reboot", PackBGRA(255, 255, 255));
    DrawString(s_spBuffer, WIN_W, WIN_W - 40, 188, "v", PackBGRA(160, 180, 210));

    // 5. Button Row: [OK] [Cancel]
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_spBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(0, 120, 215));
    DrawRect(s_spBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_spBuffer, WIN_W, WIN_W - 146, btnY + 7, "OK", PackBGRA(255, 255, 255));

    FillRect(s_spBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(30, 42, 60));
    DrawRect(s_spBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_spBuffer, WIN_W, WIN_W - 76, btnY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT System Preparation Tool [sysprep.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Usage: sysprep.exe [/oobe | /audit] [/generalize] [/reboot | /shutdown | /quit]\n\n"
        "Image Generalization Parameters:\n"
        "  Target Image State      : IMAGE_STATE_GENERALIZE_RESEAL_TO_OOBE\n"
        "  Machine SID Reset       : Stripping machine-specific GUIDs & SAM SIDs\n"
        "  Hardware Drivers State  : Unbinding device tree for target silicon migration\n"
        "Status: Presenting sysprep.exe window (440x280 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderSysprepWindow();
    PresentWindow(s_spBuffer, WIN_W, WIN_H);

    PrintStr("[Sysprep] Image generalization ready. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
