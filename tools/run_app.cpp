/**
 * @file run_app.cpp
 * Luce OS Ring 3 Win+R Run Dialog Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Run Dialog window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes status to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 420;
constexpr uint32_t WIN_H = 210;
alignas(4096) uint32_t s_runBuffer[WIN_W * WIN_H];

void RenderRunDialog(const char* targetCmd) noexcept {
    // 1. Window Frame & Title Bar
    DrawWindowFrame(s_runBuffer, WIN_W, WIN_H, "Run", nullptr);

    // 2. Icon placeholder (Blue accent square with 'R')
    FillRect(s_runBuffer, WIN_W, 20, 44, 32, 32, PackBGRA(0, 120, 215));
    DrawRect(s_runBuffer, WIN_W, 20, 44, 32, 32, PackBGRA(255, 255, 255), 1);
    DrawString(s_runBuffer, WIN_W, 31, 54, "R", PackBGRA(255, 255, 255));

    // 3. Descriptive Prompt Text
    DrawString(s_runBuffer, WIN_W, 64, 42, "Type the name of a program, folder, document, or", PackBGRA(220, 230, 245));
    DrawString(s_runBuffer, WIN_W, 64, 58, "Internet resource, and MicaNT will open it for you.", PackBGRA(160, 180, 210));

    // 4. "Open:" Label and Input Box
    DrawString(s_runBuffer, WIN_W, 20, 96, "Open:", PackBGRA(220, 230, 245));
    FillRect(s_runBuffer, WIN_W, 64, 90, WIN_W - 84, 24, PackBGRA(10, 14, 22));
    DrawRect(s_runBuffer, WIN_W, 64, 90, WIN_W - 84, 24, PackBGRA(0, 150, 255), 1);

    const char* displayCmd = (targetCmd && *targetCmd) ? targetCmd : "cmd.exe";
    DrawString(s_runBuffer, WIN_W, 72, 96, displayCmd, PackBGRA(255, 255, 255));

    // Blinking cursor simulation
    size_t cmdLen = StrLen(displayCmd);
    uint32_t curX = 72 + static_cast<uint32_t>(cmdLen * 8) + 2;
    if (curX < WIN_W - 25) {
        FillRect(s_runBuffer, WIN_W, curX, 94, 2, 16, PackBGRA(0, 220, 255));
    }

    // 5. Button Row: [OK] [Cancel] [Browse...]
    const uint32_t btnY = WIN_H - 42;
    const uint32_t btnW = 75;
    const uint32_t btnH = 24;

    // OK Button (Default Active)
    FillRect(s_runBuffer, WIN_W, WIN_W - 265, btnY, btnW, btnH, PackBGRA(0, 120, 215));
    DrawRect(s_runBuffer, WIN_W, WIN_W - 265, btnY, btnW, btnH, PackBGRA(255, 255, 255), 1);
    DrawString(s_runBuffer, WIN_W, WIN_W - 240, btnY + 6, "OK", PackBGRA(255, 255, 255));

    // Cancel Button
    FillRect(s_runBuffer, WIN_W, WIN_W - 180, btnY, btnW, btnH, PackBGRA(30, 42, 60));
    DrawRect(s_runBuffer, WIN_W, WIN_W - 180, btnY, btnW, btnH, PackBGRA(70, 90, 120), 1);
    DrawString(s_runBuffer, WIN_W, WIN_W - 165, btnY + 6, "Cancel", PackBGRA(200, 215, 235));

    // Browse... Button
    FillRect(s_runBuffer, WIN_W, WIN_W - 95, btnY, btnW, btnH, PackBGRA(30, 42, 60));
    DrawRect(s_runBuffer, WIN_W, WIN_W - 95, btnY, btnW, btnH, PackBGRA(70, 90, 120), 1);
    DrawString(s_runBuffer, WIN_W, WIN_W - 88, btnY + 6, "Browse...", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Run Subsystem [Version 10.0.26040]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "[Run Dialog] Initializing Ring 3 Win+R interactive prompt...\n"
        "Default Target : \\MicaNT\\System32\\cmd.exe\n"
        "Working Path   : C:\\MicaNT\\System32\\\n"
        "Security Token : MICANT\\MicaAdmin (SeInteractiveLogonRight, High Integrity)\n"
        "Status         : Presenting Run Dialog to GOP surface (420x210 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderRunDialog("cmd.exe");
    PresentWindow(s_runBuffer, WIN_W, WIN_H);

    PrintStr("[Run Dialog] Interactive execution target armed. Exiting with status 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
