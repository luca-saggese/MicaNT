/**
 * @file notepad_app.cpp
 @brief Luce OS Ring 3 Notepad Text Editor for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Notepad window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 560;
constexpr uint32_t WIN_H = 360;
alignas(4096) uint32_t s_noteBuffer[WIN_W * WIN_H];

void RenderNotepadWindow(const char* fileName) noexcept {
    char title[64] = "Untitled - Notepad";
    if (fileName && *fileName) {
        size_t fl = StrLen(fileName);
        for (size_t i = 0; i < fl && i < 40; ++i) title[i] = fileName[i];
        title[fl] = ' '; title[fl + 1] = '-'; title[fl + 2] = ' ';
        const char suf[] = "Notepad";
        for (size_t i = 0; suf[i] != '\0'; ++i) title[fl + 3 + i] = suf[i];
        title[fl + 3 + StrLen(suf)] = '\0';
    }

    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_noteBuffer, WIN_W, WIN_H, title, "File   Edit   Format   View   Help");

    // 2. Client Text Area Background
    FillRect(s_noteBuffer, WIN_W, 1, 49, WIN_W - 2, WIN_H - 72, PackBGRA(14, 18, 28));

    // Line Numbers Gutter (Width = 40)
    FillRect(s_noteBuffer, WIN_W, 1, 49, 40, WIN_H - 72, PackBGRA(20, 26, 40));
    DrawRect(s_noteBuffer, WIN_W, 40, 49, 1, WIN_H - 72, PackBGRA(35, 48, 70));

    DrawString(s_noteBuffer, WIN_W, 16, 56, "1", PackBGRA(100, 130, 170));
    DrawString(s_noteBuffer, WIN_W, 16, 72, "2", PackBGRA(100, 130, 170));
    DrawString(s_noteBuffer, WIN_W, 16, 88, "3", PackBGRA(100, 130, 170));
    DrawString(s_noteBuffer, WIN_W, 16, 104, "4", PackBGRA(100, 130, 170));
    DrawString(s_noteBuffer, WIN_W, 16, 120, "5", PackBGRA(100, 130, 170));

    // Editor Sample Lines
    DrawString(s_noteBuffer, WIN_W, 50, 56, "[boot]", PackBGRA(0, 210, 255));
    DrawString(s_noteBuffer, WIN_W, 50, 72, "timeout=5", PackBGRA(220, 235, 255));
    DrawString(s_noteBuffer, WIN_W, 50, 88, "default=micant", PackBGRA(220, 235, 255));
    DrawString(s_noteBuffer, WIN_W, 50, 104, "[operating systems]", PackBGRA(0, 210, 255));
    DrawString(s_noteBuffer, WIN_W, 50, 120, "C:\\MicaNT\\System32=\"MicaNT Cutler Edition\"", PackBGRA(100, 240, 160));

    // 3. Status Bar (Height = 22)
    FillRect(s_noteBuffer, WIN_W, 1, WIN_H - 23, WIN_W - 2, 22, PackBGRA(24, 32, 48));
    FillRect(s_noteBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_noteBuffer, WIN_W, 12, WIN_H - 16, "Ln 1, Col 1", PackBGRA(180, 200, 230));
    DrawString(s_noteBuffer, WIN_W, 200, WIN_H - 16, "100%", PackBGRA(180, 200, 230));
    DrawString(s_noteBuffer, WIN_W, 300, WIN_H - 16, "Windows (CRLF)", PackBGRA(180, 200, 230));
    DrawString(s_noteBuffer, WIN_W, 460, WIN_H - 16, "UTF-8", PackBGRA(180, 200, 230));

    PresentWindow(s_noteBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* cmdLine = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    const char* fName = "boot.ini";
    if (cmdLine && *cmdLine) fName = cmdLine;

    PrintLine("Sovereign Notepad (notepad.exe):");
    PrintLine("  Buffer: boot.ini (UTF-8 CRLF) Loaded");
    PrintLine("  Status: Ln 1, Col 1 | 100% Zoom | Windows (CRLF)");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderNotepadWindow(fName);

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
