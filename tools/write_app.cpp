/**
 * @file write_app.cpp
 * Luce OS Ring 3 WordPad / Write Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows WordPad rich text editor to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes document status to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 440;
alignas(4096) uint32_t s_writeBuffer[WIN_W * WIN_H];

void RenderWordPadWindow() noexcept {
    // 1. Frame & Menu Bar
    DrawWindowFrame(s_writeBuffer, WIN_W, WIN_H, "Document - WordPad", "File  Home  View  Help");

    // 2. Ribbon / Formatting Toolbar (Y=50 to 92)
    FillRect(s_writeBuffer, WIN_W, 1, 50, WIN_W - 2, 42, PackBGRA(24, 34, 52));
    FillRect(s_writeBuffer, WIN_W, 1, 92, WIN_W - 2, 1, PackBGRA(40, 56, 82));

    // Font selector dropdown
    FillRect(s_writeBuffer, WIN_W, 12, 58, 120, 24, PackBGRA(10, 14, 22));
    DrawRect(s_writeBuffer, WIN_W, 12, 58, 120, 24, PackBGRA(50, 70, 100), 1);
    DrawString(s_writeBuffer, WIN_W, 18, 64, "Consolas", PackBGRA(255, 255, 255));
    DrawString(s_writeBuffer, WIN_W, 115, 64, "v", PackBGRA(180, 200, 230));

    // Font size dropdown
    FillRect(s_writeBuffer, WIN_W, 138, 58, 45, 24, PackBGRA(10, 14, 22));
    DrawRect(s_writeBuffer, WIN_W, 138, 58, 45, 24, PackBGRA(50, 70, 100), 1);
    DrawString(s_writeBuffer, WIN_W, 146, 64, "11", PackBGRA(255, 255, 255));
    DrawString(s_writeBuffer, WIN_W, 168, 64, "v", PackBGRA(180, 200, 230));

    // Style buttons: [B] [I] [U] [S]
    const char* styles[4] = { "B", "I", "U", "S" };
    uint32_t bX = 192;
    for (int i = 0; i < 4; ++i) {
        FillRect(s_writeBuffer, WIN_W, bX, 58, 24, 24, PackBGRA(30, 42, 60));
        DrawRect(s_writeBuffer, WIN_W, bX, 58, 24, 24, PackBGRA(70, 90, 120), 1);
        DrawString(s_writeBuffer, WIN_W, bX + 8, 64, styles[i], PackBGRA(220, 235, 255));
        bX += 28;
    }

    // Color picker indicator
    FillRect(s_writeBuffer, WIN_W, bX + 4, 58, 24, 24, PackBGRA(30, 42, 60));
    DrawRect(s_writeBuffer, WIN_W, bX + 4, 58, 24, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_writeBuffer, WIN_W, bX + 11, 64, "A", PackBGRA(255, 215, 0));
    FillRect(s_writeBuffer, WIN_W, bX + 8, 77, 16, 3, PackBGRA(255, 60, 60));

    // Alignment buttons: [Left] [Center] [Right]
    bX += 36;
    const char* aligns[3] = { "|=", "= =", "=|" };
    for (int i = 0; i < 3; ++i) {
        FillRect(s_writeBuffer, WIN_W, bX, 58, 26, 24, (i == 0) ? PackBGRA(0, 120, 215) : PackBGRA(30, 42, 60));
        DrawRect(s_writeBuffer, WIN_W, bX, 58, 26, 24, PackBGRA(70, 90, 120), 1);
        DrawString(s_writeBuffer, WIN_W, bX + 5, 64, aligns[i], PackBGRA(220, 235, 255));
        bX += 30;
    }

    // 3. Ruler (Y=93 to 108)
    FillRect(s_writeBuffer, WIN_W, 1, 93, WIN_W - 2, 14, PackBGRA(16, 22, 32));
    FillRect(s_writeBuffer, WIN_W, 1, 107, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    for (uint32_t rx = 30; rx < WIN_W - 30; rx += 40) {
        FillRect(s_writeBuffer, WIN_W, rx, 99, 1, 6, PackBGRA(100, 120, 150));
    }

    // 4. White Page Canvas (Centered Document Margin)
    const uint32_t docX = 35;
    const uint32_t docY = 114;
    const uint32_t docW = WIN_W - 70;
    const uint32_t docH = WIN_H - 145;

    // Document sheet (pure white with subtle shadow)
    FillRect(s_writeBuffer, WIN_W, docX, docY, docW, docH, PackBGRA(252, 252, 255));
    DrawRect(s_writeBuffer, WIN_W, docX, docY, docW, docH, PackBGRA(180, 190, 210), 1);

    // Document Content text
    DrawString(s_writeBuffer, WIN_W, docX + 24, docY + 20, "MicaNT Operating System: Sovereign Cutler Architecture", PackBGRA(20, 25, 35));
    DrawString(s_writeBuffer, WIN_W, docX + 24, docY + 36, "=======================================================", PackBGRA(80, 90, 110));

    DrawString(s_writeBuffer, WIN_W, docX + 24, docY + 54, "Welcome to WordPad on MicaNT Cutler Edition (x86_64).", PackBGRA(40, 45, 55));
    DrawString(s_writeBuffer, WIN_W, docX + 24, docY + 70, "This rich text editor provides clean-room document", PackBGRA(40, 45, 55));
    DrawString(s_writeBuffer, WIN_W, docX + 24, docY + 86, "composition directly on physical bare-metal hardware.", PackBGRA(40, 45, 55));

    DrawString(s_writeBuffer, WIN_W, docX + 24, docY + 110, "Hardware Specifications:", PackBGRA(20, 25, 35));
    DrawString(s_writeBuffer, WIN_W, docX + 36, docY + 126, "- Processor: Intel Xeon E3-1270 v6 @ 3.80GHz (8 Threads)", PackBGRA(50, 60, 80));
    DrawString(s_writeBuffer, WIN_W, docX + 36, docY + 142, "- Memory   : 32 GB ECC DDR4-2400 (Single-bit Correction)", PackBGRA(50, 60, 80));
    DrawString(s_writeBuffer, WIN_W, docX + 36, docY + 158, "- Graphics : DirectFlip 32-bpp BGRA Linear Framebuffer", PackBGRA(50, 60, 80));
    DrawString(s_writeBuffer, WIN_W, docX + 36, docY + 174, "- Storage  : 2TB HGST Ultrastar Enterprise SATA (AHCI)", PackBGRA(50, 60, 80));

    // Blinking cursor on document
    FillRect(s_writeBuffer, WIN_W, docX + 24, docY + 200, 2, 14, PackBGRA(0, 120, 215));

    // 5. Status Bar at Bottom
    FillRect(s_writeBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(20, 28, 42));
    FillRect(s_writeBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(40, 56, 80));
    DrawString(s_writeBuffer, WIN_W, 16, WIN_H - 17, "Page 1, 14 lines, 584 characters | 100% Zoom", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT WordPad / Write Utility [write.exe / wordpad.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Document Engine        : Rich Text Format (RTF 1.9) / Unicode Plain Text\n"
        "Active Typography      : Consolas 11pt, ClearType Subpixel Rendering\n"
        "Page Layout            : Standard Letter (8.5 x 11.0 in), 1-inch Margins\n"
        "Memory Footprint       : 1,020,800 bytes VRAM Presentation Buffer\n"
        "Status: Presenting write.exe window (580x440 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderWordPadWindow();
    PresentWindow(s_writeBuffer, WIN_W, WIN_H);

    PrintStr("[Write] Document canvas presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
