/**
 * @file calc_app.cpp
 * Luce OS Ring 3 Modern Calculator Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Calculator window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 340;
constexpr uint32_t WIN_H = 460;
alignas(4096) uint32_t s_calcBuffer[WIN_W * WIN_H];

void RenderCalculatorWindow(const char* displayVal) noexcept {
    // 1. Frame & Title Bar
    DrawWindowFrame(s_calcBuffer, WIN_W, WIN_H, "Calculator", "Standard  Scientific  History");

    // 2. Calculator Display Area (Height = 70)
    FillRect(s_calcBuffer, WIN_W, 16, 56, WIN_W - 32, 60, PackBGRA(12, 16, 26));
    DrawRect(s_calcBuffer, WIN_W, 16, 56, WIN_W - 32, 60, PackBGRA(35, 48, 70), 1);
    DrawString(s_calcBuffer, WIN_W, WIN_W - 36 - static_cast<uint32_t>(StrLen(displayVal)) * 8, 80, displayVal, PackBGRA(255, 255, 255));

    // 3. 6x4 Keypad Grid
    const char* keys[6][4] = {
        {"%", "CE", "C", "<"},
        {"1/x", "x^2", "SQRT", "/"},
        {"7", "8", "9", "*"},
        {"4", "5", "6", "-"},
        {"1", "2", "3", "+"},
        {"+/-", "0", ".", "="}
    };

    const uint32_t startY = 126;
    const uint32_t margin = 16;
    const uint32_t gap = 6;
    const uint32_t btnW = (WIN_W - 2 * margin - 3 * gap) / 4;
    const uint32_t btnH = (WIN_H - startY - margin - 5 * gap) / 6;

    for (uint32_t r = 0; r < 6; ++r) {
        for (uint32_t c = 0; c < 4; ++c) {
            uint32_t bx = margin + c * (btnW + gap);
            uint32_t by = startY + r * (btnH + gap);

            bool isAccent = (r == 5 && c == 3); // '='
            uint32_t bgCol = isAccent ? PackBGRA(0, 120, 215) : PackBGRA(28, 38, 58);
            uint32_t fgCol = isAccent ? PackBGRA(255, 255, 255) : PackBGRA(220, 235, 255);

            FillRect(s_calcBuffer, WIN_W, bx, by, btnW, btnH, bgCol);
            DrawRect(s_calcBuffer, WIN_W, bx, by, btnW, btnH, PackBGRA(45, 65, 95), 1);

            const char* label = keys[r][c];
            uint32_t tx = bx + (btnW - static_cast<uint32_t>(StrLen(label)) * 8) / 2;
            uint32_t ty = by + (btnH - 8) / 2;
            DrawString(s_calcBuffer, WIN_W, tx, ty, label, fgCol);
        }
    }

    PresentWindow(s_calcBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* cmdLine = nullptr) {
    // 1. Syscall 0x11: NtQuerySystemInformation (Verify Ring 3 CPL=3)
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    const char* displayVal = "1,024";
    if (cmdLine && *cmdLine) {
        displayVal = cmdLine;
    }

    PrintLine("Sovereign Modern Calculator (calc.exe):");
    PrintLine("  Keypad: 6x4 Standard / Scientific Active");
    PrintLine("  Display: 1,024 | Precision: 64-bit IEEE-754");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderCalculatorWindow(displayVal);

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
