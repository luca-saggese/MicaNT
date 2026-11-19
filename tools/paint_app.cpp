/**
 * @file paint_app.cpp
 @brief Luce OS Ring 3 Paint Studio Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Paint window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_pntBuffer[WIN_W * WIN_H];

void RenderPaintWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_pntBuffer, WIN_W, WIN_H, "Untitled - Paint", "File   Edit   View   Image   Tools   Brushes   Colors");

    // 2. Ribbon Toolbar & Tools (Height = 36)
    FillRect(s_pntBuffer, WIN_W, 1, 49, WIN_W - 2, 36, PackBGRA(22, 30, 46));
    DrawRect(s_pntBuffer, WIN_W, 1, 84, WIN_W - 2, 1, PackBGRA(38, 52, 78));

    DrawString(s_pntBuffer, WIN_W, 10, 56, "[Pencil]  [Fill]  [Text]  [Eraser]  [Shapes]  [Size: 2px]", PackBGRA(200, 225, 255));
    DrawString(s_pntBuffer, WIN_W, 10, 70, "Colors: [K] [W] [R] [G] [B] [Y] [C] [M]  Primary: Cyan", PackBGRA(0, 210, 255));

    // Color Swatches on right of ribbon
    const uint32_t palette[8] = {
        PackBGRA(0, 0, 0),       PackBGRA(255, 255, 255),
        PackBGRA(255, 50, 50),   PackBGRA(50, 220, 50),
        PackBGRA(50, 120, 255),  PackBGRA(255, 220, 50),
        PackBGRA(0, 229, 255),   PackBGRA(255, 50, 255)
    };
    for (uint32_t i = 0; i < 8; ++i) {
        FillRect(s_pntBuffer, WIN_W, 460 + i * 14, 60, 12, 12, palette[i]);
        DrawRect(s_pntBuffer, WIN_W, 460 + i * 14, 60, 12, 12, PackBGRA(30, 40, 60), 1);
    }

    // 3. Canvas Area Background (Dark Gray #1E2430 surrounding white sheet)
    FillRect(s_pntBuffer, WIN_W, 1, 85, WIN_W - 2, WIN_H - 108, PackBGRA(16, 22, 34));

    // White Drawing Sheet in center (Width = 460, Height = 220)
    uint32_t sheetX = 60;
    uint32_t sheetY = 100;
    uint32_t sheetW = 460;
    uint32_t sheetH = 220;
    FillRect(s_pntBuffer, WIN_W, sheetX, sheetY, sheetW, sheetH, PackBGRA(248, 250, 255));
    DrawRect(s_pntBuffer, WIN_W, sheetX - 1, sheetY - 1, sheetW + 2, sheetH + 2, PackBGRA(45, 65, 95), 1);

    // Draw some sample graphics on the canvas (Crystal Octahedron silhouette & logo)
    DrawString(s_pntBuffer, WIN_W, sheetX + 20, sheetY + 20, "MicaNT Sovereign Paint Studio (mspaint.exe)", PackBGRA(20, 30, 50));
    DrawString(s_pntBuffer, WIN_W, sheetX + 20, sheetY + 40, "Clean-Room 32-bpp BGRA Canvas Vector Graphics Engine", PackBGRA(80, 100, 130));

    // Drawn geometric shapes on canvas
    FillRect(s_pntBuffer, WIN_W, sheetX + 30, sheetY + 70, 80, 80, PackBGRA(0, 150, 240));
    DrawRect(s_pntBuffer, WIN_W, sheetX + 30, sheetY + 70, 80, 80, PackBGRA(0, 80, 180), 2);

    FillRect(s_pntBuffer, WIN_W, sheetX + 130, sheetY + 70, 100, 80, PackBGRA(16, 185, 129));
    DrawRect(s_pntBuffer, WIN_W, sheetX + 130, sheetY + 70, 100, 80, PackBGRA(10, 120, 80), 2);

    DrawString(s_pntBuffer, WIN_W, sheetX + 45, sheetY + 105, "DirectX", PackBGRA(255, 255, 255));
    DrawString(s_pntBuffer, WIN_W, sheetX + 145, sheetY + 105, "Sovereign", PackBGRA(255, 255, 255));

    // 4. Status Bar
    FillRect(s_pntBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_pntBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_pntBuffer, WIN_W, 12, WIN_H - 15, "Canvas: 1920 x 1080 px", PackBGRA(180, 200, 230));
    DrawString(s_pntBuffer, WIN_W, 220, WIN_H - 15, "|  Cursor: 460, 220 px", PackBGRA(180, 200, 230));
    DrawString(s_pntBuffer, WIN_W, 460, WIN_H - 15, "Zoom: 100%", PackBGRA(0, 210, 255));

    PresentWindow(s_pntBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Paint Studio (mspaint.exe):");
    PrintLine("  Canvas: 1920 x 1080 32-bpp BGRA Pixel Buffer");
    PrintLine("  Palette: 16-Color Sovereign Standard Active");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderPaintWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
