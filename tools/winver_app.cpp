/**
 * @file winver_app.cpp
 * Luce OS Ring 3 About Windows / MicaNT Dialog (winver) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows About Windows dialog to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes build specifications to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 440;
constexpr uint32_t WIN_H = 360;
alignas(4096) uint32_t s_winverBuffer[WIN_W * WIN_H];

void RenderWinverWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_winverBuffer, WIN_W, WIN_H, "About MicaNT", nullptr);

    // 2. Windows 4-Color Logo in Header (Top Left)
    const uint32_t logoX = 26;
    const uint32_t logoY = 44;
    const uint32_t sqW = 16;
    const uint32_t gap = 3;

    // Top-left: Red
    FillRect(s_winverBuffer, WIN_W, logoX, logoY, sqW, sqW, PackBGRA(242, 80, 34));
    // Top-right: Green
    FillRect(s_winverBuffer, WIN_W, logoX + sqW + gap, logoY, sqW, sqW, PackBGRA(127, 186, 0));
    // Bottom-left: Blue
    FillRect(s_winverBuffer, WIN_W, logoX, logoY + sqW + gap, sqW, sqW, PackBGRA(0, 164, 239));
    // Bottom-right: Yellow
    FillRect(s_winverBuffer, WIN_W, logoX + sqW + gap, logoY + sqW + gap, sqW, sqW, PackBGRA(255, 185, 0));

    // OS Title
    DrawString(s_winverBuffer, WIN_W, logoX + 2 * sqW + gap + 16, logoY + 4, "MicaNT Operating System", PackBGRA(255, 255, 255));
    DrawString(s_winverBuffer, WIN_W, logoX + 2 * sqW + gap + 16, logoY + 20, "Dave Cutler Sovereign Edition (x86_64)", PackBGRA(0, 180, 255));

    // Separator line
    FillRect(s_winverBuffer, WIN_W, 20, 92, WIN_W - 40, 1, PackBGRA(40, 56, 80));

    // 3. Version and Build Info
    DrawString(s_winverBuffer, WIN_W, 20, 106, "MicaNT Sovereign Workstation [Version 10.0.26040.1000]", PackBGRA(220, 235, 255));
    DrawString(s_winverBuffer, WIN_W, 20, 124, "(c) 2026 MicaNT Foundation. All rights reserved.", PackBGRA(160, 180, 210));
    DrawString(s_winverBuffer, WIN_W, 20, 138, "All rights reserved. Clean-Room ISO C++23.", PackBGRA(160, 180, 210));

    DrawString(s_winverBuffer, WIN_W, 20, 162, "The MicaNT operating system and its user interface are", PackBGRA(200, 215, 235));
    DrawString(s_winverBuffer, WIN_W, 20, 176, "protected by trademark and other pending or existing", PackBGRA(200, 215, 235));
    DrawString(s_winverBuffer, WIN_W, 20, 190, "intellectual property rights in sovereign jurisdictions.", PackBGRA(200, 215, 235));

    // Separator line
    FillRect(s_winverBuffer, WIN_W, 20, 214, WIN_W - 40, 1, PackBGRA(40, 56, 80));

    // 4. Licensee Info & Hardware RAM
    DrawString(s_winverBuffer, WIN_W, 20, 226, "This product is licensed under the Sovereign Cutler Terms to:", PackBGRA(160, 180, 210));
    DrawString(s_winverBuffer, WIN_W, 36, 246, "MicaAdmin", PackBGRA(255, 255, 255));
    DrawString(s_winverBuffer, WIN_W, 36, 262, "Sovereign Executive Workstation", PackBGRA(220, 235, 255));

    DrawString(s_winverBuffer, WIN_W, 20, 288, "Installed Physical Memory (RAM): 32,768 MB ECC DDR4", PackBGRA(100, 240, 160));

    // 5. [OK] Button
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_winverBuffer, WIN_W, WIN_W - 95, btnY, 75, 26, PackBGRA(0, 120, 215));
    DrawRect(s_winverBuffer, WIN_W, WIN_W - 95, btnY, 75, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_winverBuffer, WIN_W, WIN_W - 68, btnY + 7, "OK", PackBGRA(255, 255, 255));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Operating System [winver.exe]\n"
        "(C) 2026 MicaNT Foundation. All rights reserved.\n\n"
        "OS Version             : MicaNT 10.0 (Build 26040.ge_release)\n"
        "Kernel Edition         : Dave Cutler Sovereign High-Half Architecture\n"
        "Processor Platform     : Intel(R) Xeon(R) CPU E3-1270 v6 @ 3.80GHz (8T)\n"
        "Installed Memory       : 32,768 MB ECC DDR4-2400 (Single-bit Correction)\n"
        "Licensed User          : MicaAdmin (Sovereign Executive Workstation)\n"
        "Status: Presenting winver.exe dialog (440x360 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderWinverWindow();
    PresentWindow(s_winverBuffer, WIN_W, WIN_H);

    PrintStr("[WinVer] About MicaNT presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
