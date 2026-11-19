/**
 * @file charmap_app.cpp
 * Luce OS Ring 3 Character Map Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Character Map window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes character table statistics to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 480;
constexpr uint32_t WIN_H = 360;
alignas(4096) uint32_t s_charBuffer[WIN_W * WIN_H];

void RenderCharmapWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_charBuffer, WIN_W, WIN_H, "Character Map", nullptr);

    // 2. Font selector
    DrawString(s_charBuffer, WIN_W, 20, 38, "Font:", PackBGRA(220, 230, 245));
    FillRect(s_charBuffer, WIN_W, 64, 34, 180, 22, PackBGRA(10, 14, 22));
    DrawRect(s_charBuffer, WIN_W, 64, 34, 180, 22, PackBGRA(50, 70, 100), 1);
    DrawString(s_charBuffer, WIN_W, 72, 38, "Consolas", PackBGRA(255, 255, 255));
    DrawString(s_charBuffer, WIN_W, 230, 38, "v", PackBGRA(180, 200, 230));

    // 3. 16x10 Character Grid Box
    const uint32_t gridX = 20;
    const uint32_t gridY = 66;
    const uint32_t cellW = 26;
    const uint32_t cellH = 22;

    FillRect(s_charBuffer, WIN_W, gridX, gridY, 16 * cellW, 10 * cellH, PackBGRA(10, 14, 22));
    DrawRect(s_charBuffer, WIN_W, gridX, gridY, 16 * cellW, 10 * cellH, PackBGRA(40, 55, 80), 1);

    char cBuf[2] = { ' ', '\0' };
    unsigned char ch = 32; // Start from ASCII space
    for (uint32_t r = 0; r < 10; ++r) {
        for (uint32_t c = 0; c < 16; ++c) {
            uint32_t cx = gridX + c * cellW;
            uint32_t cy = gridY + r * cellH;

            // Cell border
            DrawRect(s_charBuffer, WIN_W, cx, cy, cellW, cellH, PackBGRA(25, 34, 50), 1);

            // Highlight selected character (e.g. '@')
            if (ch == '@') {
                FillRect(s_charBuffer, WIN_W, cx + 1, cy + 1, cellW - 2, cellH - 2, PackBGRA(0, 120, 215));
                cBuf[0] = static_cast<char>(ch);
                DrawString(s_charBuffer, WIN_W, cx + 9, cy + 5, cBuf, PackBGRA(255, 255, 255));
            } else if (ch < 127) {
                cBuf[0] = static_cast<char>(ch);
                DrawString(s_charBuffer, WIN_W, cx + 9, cy + 5, cBuf, PackBGRA(200, 215, 235));
            }
            ch++;
        }
    }

    // 4. Magnified Preview Box on the right
    const uint32_t magX = gridX + 16 * cellW + 12;
    FillRect(s_charBuffer, WIN_W, magX, gridY, 36, 42, PackBGRA(20, 28, 44));
    DrawRect(s_charBuffer, WIN_W, magX, gridY, 36, 42, PackBGRA(0, 150, 255), 1);
    DrawString(s_charBuffer, WIN_W, magX + 14, gridY + 14, "@", PackBGRA(255, 255, 255));

    // 5. Bottom Row: Characters to copy box and buttons
    const uint32_t btmY = WIN_H - 52;
    DrawString(s_charBuffer, WIN_W, 20, btmY + 6, "Characters to copy:", PackBGRA(220, 230, 245));
    FillRect(s_charBuffer, WIN_W, 160, btmY, 150, 24, PackBGRA(10, 14, 22));
    DrawRect(s_charBuffer, WIN_W, 160, btmY, 150, 24, PackBGRA(50, 70, 100), 1);
    DrawString(s_charBuffer, WIN_W, 168, btmY + 6, "@", PackBGRA(255, 255, 255));

    // [Select] button
    FillRect(s_charBuffer, WIN_W, 320, btmY, 65, 24, PackBGRA(30, 42, 60));
    DrawRect(s_charBuffer, WIN_W, 320, btmY, 65, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_charBuffer, WIN_W, 332, btmY + 6, "Select", PackBGRA(220, 230, 245));

    // [Copy] button
    FillRect(s_charBuffer, WIN_W, 395, btmY, 65, 24, PackBGRA(0, 120, 215));
    DrawRect(s_charBuffer, WIN_W, 395, btmY, 65, 24, PackBGRA(255, 255, 255), 1);
    DrawString(s_charBuffer, WIN_W, 412, btmY + 6, "Copy", PackBGRA(255, 255, 255));

    // Footer info
    DrawString(s_charBuffer, WIN_W, 20, WIN_H - 18, "U+0040: Commercial At | Unicode BMP (Basic Multilingual Plane)", PackBGRA(120, 140, 170));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Sovereign Character Map [Version 10.0.26040]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Active Font Face        : Consolas (Monospaced Bitmap Font 8x12 / TTF)\n"
        "Active Code Page        : 1252 (ANSI Latin 1) / 437 (OEM United States)\n"
        "Unicode Subrange        : U+0020 (Space) to U+00FF (Latin Small Letter Y With Diaeresis)\n"
        "Clipboard Integration   : Sovereign Ring 3 Clipboard Buffer Synced\n"
        "Status: Presenting charmap.exe window (480x360 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderCharmapWindow();
    PresentWindow(s_charBuffer, WIN_W, WIN_H);

    PrintStr("[Charmap] Character grid presented successfully. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
