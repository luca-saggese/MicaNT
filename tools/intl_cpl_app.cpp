/**
 * @file intl_cpl_app.cpp
 * Luce OS Ring 3 Region and Language Applet (intl.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Region and Language window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes locale & codepage telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 500;
constexpr uint32_t WIN_H = 400;
alignas(4096) uint32_t s_intlBuffer[WIN_W * WIN_H];

void RenderRegionWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_intlBuffer, WIN_W, WIN_H, "Region", nullptr);

    // 2. Tabs Row: [Formats] [Location] [Administrative]
    const uint32_t tabY = 36;
    FillRect(s_intlBuffer, WIN_W, 20, tabY, 70, 22, PackBGRA(30, 42, 60));
    DrawRect(s_intlBuffer, WIN_W, 20, tabY, 70, 22, PackBGRA(0, 150, 255), 1);
    DrawString(s_intlBuffer, WIN_W, 28, tabY + 5, "Formats", PackBGRA(255, 255, 255));

    DrawString(s_intlBuffer, WIN_W, 105, tabY + 5, "Location", PackBGRA(160, 180, 210));
    DrawString(s_intlBuffer, WIN_W, 185, tabY + 5, "Administrative", PackBGRA(160, 180, 210));

    FillRect(s_intlBuffer, WIN_W, 20, tabY + 22, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 3. Format selection dropdown
    DrawString(s_intlBuffer, WIN_W, 20, 68, "Format:", PackBGRA(220, 235, 255));
    FillRect(s_intlBuffer, WIN_W, 80, 64, 260, 24, PackBGRA(10, 14, 22));
    DrawRect(s_intlBuffer, WIN_W, 80, 64, 260, 24, PackBGRA(50, 70, 100), 1);
    DrawString(s_intlBuffer, WIN_W, 90, 70, "English (United States)", PackBGRA(255, 255, 255));
    DrawString(s_intlBuffer, WIN_W, 325, 70, "v", PackBGRA(160, 180, 210));

    DrawString(s_intlBuffer, WIN_W, 20, 96, "Language preferences", PackBGRA(0, 180, 255));

    // Separator line
    FillRect(s_intlBuffer, WIN_W, 20, 114, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 4. Date and Time Formats Group
    DrawString(s_intlBuffer, WIN_W, 20, 126, "Date and time formats", PackBGRA(255, 255, 255));

    struct FormatItem {
        const char* label;
        const char* format;
    };

    const FormatItem fmtItems[5] = {
        { "Short date:", "M/d/yyyy" },
        { "Long date:", "dddd, MMMM d, yyyy" },
        { "Short time:", "h:mm tt" },
        { "Long time:", "h:mm:ss tt" },
        { "First day of week:", "Sunday" }
    };

    uint32_t fY = 146;
    for (int i = 0; i < 5; ++i) {
        DrawString(s_intlBuffer, WIN_W, 20, fY, fmtItems[i].label, PackBGRA(180, 200, 230));
        DrawString(s_intlBuffer, WIN_W, 160, fY, fmtItems[i].format, PackBGRA(255, 255, 255));
        fY += 22;
    }

    // Separator line
    FillRect(s_intlBuffer, WIN_W, 20, 260, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 5. Examples Group
    DrawString(s_intlBuffer, WIN_W, 20, 272, "Examples", PackBGRA(255, 255, 255));
    DrawString(s_intlBuffer, WIN_W, 20, 292, "Short date:   10/23/2026", PackBGRA(100, 240, 160));
    DrawString(s_intlBuffer, WIN_W, 20, 310, "Long date:    Friday, October 23, 2026", PackBGRA(100, 240, 160));
    DrawString(s_intlBuffer, WIN_W, 20, 328, "Short time:   12:00 PM", PackBGRA(100, 240, 160));

    // 6. Dialog Buttons Row
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_intlBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(0, 120, 215));
    DrawRect(s_intlBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_intlBuffer, WIN_W, WIN_W - 220, btnY + 7, "OK", PackBGRA(255, 255, 255));

    FillRect(s_intlBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_intlBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_intlBuffer, WIN_W, WIN_W - 146, btnY + 7, "Cancel", PackBGRA(200, 215, 235));

    FillRect(s_intlBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_intlBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_intlBuffer, WIN_W, WIN_W - 66, btnY + 7, "Apply", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Region and Language Subsystem [intl.cpl / intl.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Locale Identifier (LCID): 0x0409 (en-US - United States)\n"
        "ANSI Code Page (ACP)    : 1252 (Western European Latin-1)\n"
        "OEM Code Page (OCP)     : 437 (IBM PC Extended ASCII)\n"
        "Calendar Algorithm      : Gregorian Calendar (ACPI RTC Real-Time Epoch)\n"
        "Status: Presenting intl.cpl window (500x400 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderRegionWindow();
    PresentWindow(s_intlBuffer, WIN_W, WIN_H);

    PrintStr("[Intl CPL] Regional settings presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
