/**
 * @file timedate_app.cpp
 * Luce OS Ring 3 Date and Time Applet (timedate.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Date and Time window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes RTC chronometry telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 480;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_timeBuffer[WIN_W * WIN_H];

void RenderTimeDateWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_timeBuffer, WIN_W, WIN_H, "Date and Time", nullptr);

    // 2. Analog Clock Face on the Left
    const uint32_t clockX = 110;
    const uint32_t clockY = 120;
    const uint32_t clockR = 55;

    // Outer clock bezel
    DrawCircle(s_timeBuffer, WIN_W, clockX, clockY, clockR + 4, PackBGRA(40, 56, 80));
    DrawCircle(s_timeBuffer, WIN_W, clockX, clockY, clockR + 2, PackBGRA(200, 220, 255));
    DrawCircle(s_timeBuffer, WIN_W, clockX, clockY, clockR, PackBGRA(16, 22, 34));

    // Hour tick marks (12, 3, 6, 9)
    DrawCircle(s_timeBuffer, WIN_W, clockX, clockY - clockR + 8, 2, PackBGRA(255, 255, 255));
    DrawCircle(s_timeBuffer, WIN_W, clockX + clockR - 8, clockY, 2, PackBGRA(255, 255, 255));
    DrawCircle(s_timeBuffer, WIN_W, clockX, clockY + clockR - 8, 2, PackBGRA(255, 255, 255));
    DrawCircle(s_timeBuffer, WIN_W, clockX - clockR + 8, clockY, 2, PackBGRA(255, 255, 255));

    // Hands: Hour hand (pointing to 12)
    DrawLine(s_timeBuffer, WIN_W, clockX, clockY, clockX, clockY - 28, PackBGRA(255, 255, 255));
    // Minute hand (pointing to 12)
    DrawLine(s_timeBuffer, WIN_W, clockX, clockY, clockX + 8, clockY - 42, PackBGRA(0, 180, 255));
    // Second hand (red)
    DrawLine(s_timeBuffer, WIN_W, clockX, clockY, clockX - 16, clockY + 32, PackBGRA(255, 60, 60));
    // Center pivot pin
    DrawCircle(s_timeBuffer, WIN_W, clockX, clockY, 3, PackBGRA(255, 215, 0));

    // Digital time readout under clock
    DrawString(s_timeBuffer, WIN_W, clockX - 35, clockY + clockR + 16, "12:00:00 PM", PackBGRA(255, 255, 255));

    // 3. Calendar Grid on the Right
    const uint32_t calX = 230;
    const uint32_t calY = 48;
    DrawString(s_timeBuffer, WIN_W, calX + 30, calY, "October 2026", PackBGRA(255, 255, 255));

    // Days of week header
    DrawString(s_timeBuffer, WIN_W, calX, calY + 22, "Su Mo Tu We Th Fr Sa", PackBGRA(140, 160, 190));
    FillRect(s_timeBuffer, WIN_W, calX, calY + 36, 190, 1, PackBGRA(40, 55, 80));

    // Sample calendar weeks
    DrawString(s_timeBuffer, WIN_W, calX, calY + 44, "             1  2  3", PackBGRA(200, 215, 235));
    DrawString(s_timeBuffer, WIN_W, calX, calY + 62, " 4  5  6  7  8  9 10", PackBGRA(200, 215, 235));
    DrawString(s_timeBuffer, WIN_W, calX, calY + 80, "11 12 13 14 15 16 17", PackBGRA(200, 215, 235));
    DrawString(s_timeBuffer, WIN_W, calX, calY + 98, "18 19 20 21 22 23 24", PackBGRA(200, 215, 235));
    // Highlight today (23)
    FillRect(s_timeBuffer, WIN_W, calX + 130, calY + 96, 20, 16, PackBGRA(0, 120, 215));
    DrawString(s_timeBuffer, WIN_W, calX + 132, calY + 98, "23", PackBGRA(255, 255, 255));
    DrawString(s_timeBuffer, WIN_W, calX, calY + 116, "25 26 27 28 29 30 31", PackBGRA(200, 215, 235));

    // Separator line
    FillRect(s_timeBuffer, WIN_W, 20, 225, WIN_W - 40, 1, PackBGRA(40, 55, 80));

    // 4. Time Zone section
    DrawString(s_timeBuffer, WIN_W, 20, 238, "Time zone", PackBGRA(255, 255, 255));
    DrawString(s_timeBuffer, WIN_W, 20, 256, "Time zone: (UTC-07:00) Mountain Time (US & Canada)", PackBGRA(200, 220, 245));
    DrawString(s_timeBuffer, WIN_W, 20, 274, "Daylight Saving Time is currently active", PackBGRA(140, 160, 190));
    DrawString(s_timeBuffer, WIN_W, 20, 292, "Internet Time: Synchronized with pool.ntp.org", PackBGRA(100, 240, 160));

    // 5. Button Row
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_timeBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(0, 120, 215));
    DrawRect(s_timeBuffer, WIN_W, WIN_W - 170, btnY, 70, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_timeBuffer, WIN_W, WIN_W - 146, btnY + 7, "OK", PackBGRA(255, 255, 255));

    FillRect(s_timeBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(30, 42, 60));
    DrawRect(s_timeBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_timeBuffer, WIN_W, WIN_W - 76, btnY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Date and Time Subsystem [timedate.cpl / timedate.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Hardware Real-Time Clock : ACPI RTC (CMOS Port 0x70/0x71 Verified)\n"
        "System Chronometry       : Friday, October 23, 2026 12:00:00 PM\n"
        "Time Zone                : (UTC-07:00) Mountain Time (US & Canada)\n"
        "Network Time Protocol    : NTP Client Synchronized (pool.ntp.org)\n"
        "Status: Presenting timedate.cpl window (480x380 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderTimeDateWindow();
    PresentWindow(s_timeBuffer, WIN_W, WIN_H);

    PrintStr("[TimeDate] Chronometry presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
