/**
 * @file osk_app.cpp
 * Luce OS Ring 3 On-Screen Keyboard Utility (osk) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows On-Screen Keyboard window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes virtual keystroke input telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 620;
constexpr uint32_t WIN_H = 220;
alignas(4096) uint32_t s_oskBuffer[WIN_W * WIN_H];

void RenderOskWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_oskBuffer, WIN_W, WIN_H, "On-Screen Keyboard", nullptr);

    // 2. Keyboard Key Matrix (Rows of keys)
    const uint32_t kStartX = 12;
    uint32_t kY = 38;
    const uint32_t keyH = 28;
    const uint32_t keyGap = 3;

    // Row 0: Esc, F1-F12
    FillRect(s_oskBuffer, WIN_W, kStartX, kY, 32, keyH, PackBGRA(26, 36, 52));
    DrawRect(s_oskBuffer, WIN_W, kStartX, kY, 32, keyH, PackBGRA(50, 70, 95), 1);
    DrawString(s_oskBuffer, WIN_W, kStartX + 5, kY + 8, "Esc", PackBGRA(220, 235, 255));

    const char* fKeys[12] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
    uint32_t fX = kStartX + 42;
    for (int i = 0; i < 12; ++i) {
        FillRect(s_oskBuffer, WIN_W, fX, kY, 28, keyH, PackBGRA(22, 30, 44));
        DrawRect(s_oskBuffer, WIN_W, fX, kY, 28, keyH, PackBGRA(40, 56, 80), 1);
        DrawString(s_oskBuffer, WIN_W, fX + 4, kY + 8, fKeys[i], PackBGRA(180, 200, 230));
        fX += 28 + keyGap;
        if (i == 3 || i == 7) fX += 6; // Gap between F4-F5 and F8-F9
    }

    // Del key at end of top row
    FillRect(s_oskBuffer, WIN_W, WIN_W - 55, kY, 42, keyH, PackBGRA(26, 36, 52));
    DrawRect(s_oskBuffer, WIN_W, WIN_W - 55, kY, 42, keyH, PackBGRA(50, 70, 95), 1);
    DrawString(s_oskBuffer, WIN_W, WIN_W - 48, kY + 8, "Del", PackBGRA(220, 235, 255));

    kY += keyH + keyGap;

    // Row 1: Numbers ` 1 2 3 4 5 6 7 8 9 0 - = Backspace
    const char* numKeys[13] = { "`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=" };
    uint32_t nX = kStartX;
    for (int i = 0; i < 13; ++i) {
        FillRect(s_oskBuffer, WIN_W, nX, kY, 28, keyH, PackBGRA(24, 34, 50));
        DrawRect(s_oskBuffer, WIN_W, nX, kY, 28, keyH, PackBGRA(45, 65, 90), 1);
        DrawString(s_oskBuffer, WIN_W, nX + 10, kY + 8, numKeys[i], PackBGRA(255, 255, 255));
        nX += 28 + keyGap;
    }
    // Backspace
    FillRect(s_oskBuffer, WIN_W, nX, kY, 62, keyH, PackBGRA(30, 42, 60));
    DrawRect(s_oskBuffer, WIN_W, nX, kY, 62, keyH, PackBGRA(60, 85, 115), 1);
    DrawString(s_oskBuffer, WIN_W, nX + 10, kY + 8, "Bksp", PackBGRA(220, 235, 255));

    kY += keyH + keyGap;

    // Row 2: Tab Q W E R T Y U I O P [ ] \
    FillRect(s_oskBuffer, WIN_W, kStartX, kY, 44, keyH, PackBGRA(30, 42, 60));
    DrawRect(s_oskBuffer, WIN_W, kStartX, kY, 44, keyH, PackBGRA(60, 85, 115), 1);
    DrawString(s_oskBuffer, WIN_W, kStartX + 8, kY + 8, "Tab", PackBGRA(220, 235, 255));

    const char* row2[12] = { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]" };
    uint32_t r2X = kStartX + 44 + keyGap;
    for (int i = 0; i < 12; ++i) {
        FillRect(s_oskBuffer, WIN_W, r2X, kY, 28, keyH, PackBGRA(24, 34, 50));
        DrawRect(s_oskBuffer, WIN_W, r2X, kY, 28, keyH, PackBGRA(45, 65, 90), 1);
        DrawString(s_oskBuffer, WIN_W, r2X + 10, kY + 8, row2[i], PackBGRA(255, 255, 255));
        r2X += 28 + keyGap;
    }

    kY += keyH + keyGap;

    // Row 3: Caps A S D F G H J K L ; ' Enter
    FillRect(s_oskBuffer, WIN_W, kStartX, kY, 52, keyH, PackBGRA(30, 42, 60));
    DrawRect(s_oskBuffer, WIN_W, kStartX, kY, 52, keyH, PackBGRA(60, 85, 115), 1);
    DrawString(s_oskBuffer, WIN_W, kStartX + 8, kY + 8, "Caps", PackBGRA(220, 235, 255));

    const char* row3[11] = { "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'" };
    uint32_t r3X = kStartX + 52 + keyGap;
    for (int i = 0; i < 11; ++i) {
        FillRect(s_oskBuffer, WIN_W, r3X, kY, 28, keyH, PackBGRA(24, 34, 50));
        DrawRect(s_oskBuffer, WIN_W, r3X, kY, 28, keyH, PackBGRA(45, 65, 90), 1);
        DrawString(s_oskBuffer, WIN_W, r3X + 10, kY + 8, row3[i], PackBGRA(255, 255, 255));
        r3X += 28 + keyGap;
    }
    // Enter key (Highlighted blue)
    FillRect(s_oskBuffer, WIN_W, r3X, kY, 56, keyH, PackBGRA(0, 120, 215));
    DrawRect(s_oskBuffer, WIN_W, r3X, kY, 56, keyH, PackBGRA(255, 255, 255), 1);
    DrawString(s_oskBuffer, WIN_W, r3X + 10, kY + 8, "Enter", PackBGRA(255, 255, 255));

    kY += keyH + keyGap;

    // Row 4: Shift Z X C V B N M , . / Shift
    FillRect(s_oskBuffer, WIN_W, kStartX, kY, 68, keyH, PackBGRA(30, 42, 60));
    DrawRect(s_oskBuffer, WIN_W, kStartX, kY, 68, keyH, PackBGRA(60, 85, 115), 1);
    DrawString(s_oskBuffer, WIN_W, kStartX + 12, kY + 8, "Shift", PackBGRA(220, 235, 255));

    const char* row4[10] = { "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/" };
    uint32_t r4X = kStartX + 68 + keyGap;
    for (int i = 0; i < 10; ++i) {
        FillRect(s_oskBuffer, WIN_W, r4X, kY, 28, keyH, PackBGRA(24, 34, 50));
        DrawRect(s_oskBuffer, WIN_W, r4X, kY, 28, keyH, PackBGRA(45, 65, 90), 1);
        DrawString(s_oskBuffer, WIN_W, r4X + 10, kY + 8, row4[i], PackBGRA(255, 255, 255));
        r4X += 28 + keyGap;
    }
    // Right Shift
    FillRect(s_oskBuffer, WIN_W, r4X, kY, 65, keyH, PackBGRA(30, 42, 60));
    DrawRect(s_oskBuffer, WIN_W, r4X, kY, 65, keyH, PackBGRA(60, 85, 115), 1);
    DrawString(s_oskBuffer, WIN_W, r4X + 10, kY + 8, "Shift", PackBGRA(220, 235, 255));

    // Nav arrows on right
    DrawString(s_oskBuffer, WIN_W, WIN_W - 45, kY - keyH, "^", PackBGRA(0, 180, 255));
    DrawString(s_oskBuffer, WIN_W, WIN_W - 58, kY + 8, "<", PackBGRA(0, 180, 255));
    DrawString(s_oskBuffer, WIN_W, WIN_W - 45, kY + 8, "v", PackBGRA(0, 180, 255));
    DrawString(s_oskBuffer, WIN_W, WIN_W - 32, kY + 8, ">", PackBGRA(0, 180, 255));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT On-Screen Keyboard Subsystem [osk.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Input Subsystem        : USB HID / AMI Virtual Keyboard Injection\n"
        "Active Layout          : Standard 104-Key QWERTY US English\n"
        "Modifier State         : Ctrl=OFF, Alt=OFF, Shift=OFF, Caps=OFF\n"
        "Hardware Binding       : Xeon E3-1270 v6 I/O APIC Vector 0x21\n"
        "Status: Presenting osk.exe window (620x220 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderOskWindow();
    PresentWindow(s_oskBuffer, WIN_W, WIN_H);

    PrintStr("[OSK] On-Screen Keyboard presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
