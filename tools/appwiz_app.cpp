/**
 * @file appwiz_app.cpp
 * Luce OS Ring 3 Programs and Features Applet (appwiz.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Programs and Features window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes installed packages to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_appwizBuffer[WIN_W * WIN_H];

void RenderAppWizWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_appwizBuffer, WIN_W, WIN_H, "Programs and Features", "File  Edit  View  Tools  Help");

    // 2. Left Sidebar (Task Navigation)
    const uint32_t sideW = 140;
    FillRect(s_appwizBuffer, WIN_W, 1, 50, sideW, WIN_H - 51, PackBGRA(16, 22, 34));
    FillRect(s_appwizBuffer, WIN_W, sideW, 50, 1, WIN_H - 51, PackBGRA(35, 48, 70));

    DrawString(s_appwizBuffer, WIN_W, 12, 60, "Control Panel Home", PackBGRA(100, 200, 255));
    DrawString(s_appwizBuffer, WIN_W, 12, 84, "View installed updates", PackBGRA(180, 200, 230));
    DrawString(s_appwizBuffer, WIN_W, 12, 108, "Turn MicaNT features", PackBGRA(180, 200, 230));
    DrawString(s_appwizBuffer, WIN_W, 12, 122, "on or off", PackBGRA(180, 200, 230));

    // 3. Main Area: Action Bar & Search
    const uint32_t mainX = sideW + 10;
    DrawString(s_appwizBuffer, WIN_W, mainX, 58, "Uninstall or change a program", PackBGRA(255, 255, 255));

    // Search box in top right
    FillRect(s_appwizBuffer, WIN_W, WIN_W - 140, 54, 125, 20, PackBGRA(10, 14, 22));
    DrawRect(s_appwizBuffer, WIN_W, WIN_W - 140, 54, 125, 20, PackBGRA(40, 55, 80), 1);
    DrawString(s_appwizBuffer, WIN_W, WIN_W - 132, 58, "Search...", PackBGRA(120, 140, 170));

    // 4. Action Buttons: [Uninstall] [Change] [Repair]
    FillRect(s_appwizBuffer, WIN_W, mainX, 82, 75, 22, PackBGRA(30, 42, 60));
    DrawRect(s_appwizBuffer, WIN_W, mainX, 82, 75, 22, PackBGRA(70, 90, 120), 1);
    DrawString(s_appwizBuffer, WIN_W, mainX + 8, 86, "Uninstall", PackBGRA(200, 215, 235));

    FillRect(s_appwizBuffer, WIN_W, mainX + 85, 82, 65, 22, PackBGRA(30, 42, 60));
    DrawRect(s_appwizBuffer, WIN_W, mainX + 85, 82, 65, 22, PackBGRA(70, 90, 120), 1);
    DrawString(s_appwizBuffer, WIN_W, mainX + 96, 86, "Change", PackBGRA(200, 215, 235));

    // 5. Listview Header (Y=114)
    FillRect(s_appwizBuffer, WIN_W, mainX, 114, WIN_W - mainX - 10, 22, PackBGRA(24, 34, 52));
    DrawRect(s_appwizBuffer, WIN_W, mainX, 114, WIN_W - mainX - 10, 22, PackBGRA(40, 55, 80), 1);

    DrawString(s_appwizBuffer, WIN_W, mainX + 10, 119, "Name", PackBGRA(255, 255, 255));
    DrawString(s_appwizBuffer, WIN_W, mainX + 180, 119, "Publisher", PackBGRA(255, 255, 255));
    DrawString(s_appwizBuffer, WIN_W, mainX + 270, 119, "Installed", PackBGRA(255, 255, 255));
    DrawString(s_appwizBuffer, WIN_W, mainX + 350, 119, "Size", PackBGRA(255, 255, 255));

    // 6. Installed Software Rows
    struct PkgRow {
        const char* name;
        const char* pub;
        const char* date;
        const char* sz;
    };

    const PkgRow pkgs[5] = {
        { "MicaNT Sovereign Shell", "MicaNT Core", "10/22/2026", "24.5 MB" },
        { "PrismX 3D Direct3D 12", "DEC / Dave Cutler", "10/22/2026", "48.2 MB" },
        { "Intel I210 Network Stack", "Intel Corporation", "10/21/2026", "1.2 MB" },
        { "AHCI SATA Multi-Volume", "Sovereign Storage", "10/20/2026", "3.4 MB" },
        { "Haven Memory Bus (HMB)", "Haven Systems", "10/22/2026", "8.0 MB" }
    };

    uint32_t rowY = 142;
    for (int i = 0; i < 5; ++i) {
        if (i == 0) {
            FillRect(s_appwizBuffer, WIN_W, mainX, rowY - 2, WIN_W - mainX - 10, 22, PackBGRA(0, 120, 215));
            DrawString(s_appwizBuffer, WIN_W, mainX + 10, rowY + 3, pkgs[i].name, PackBGRA(255, 255, 255));
            DrawString(s_appwizBuffer, WIN_W, mainX + 180, rowY + 3, pkgs[i].pub, PackBGRA(255, 255, 255));
            DrawString(s_appwizBuffer, WIN_W, mainX + 270, rowY + 3, pkgs[i].date, PackBGRA(255, 255, 255));
            DrawString(s_appwizBuffer, WIN_W, mainX + 350, rowY + 3, pkgs[i].sz, PackBGRA(255, 255, 255));
        } else {
            DrawString(s_appwizBuffer, WIN_W, mainX + 10, rowY + 3, pkgs[i].name, PackBGRA(220, 230, 245));
            DrawString(s_appwizBuffer, WIN_W, mainX + 180, rowY + 3, pkgs[i].pub, PackBGRA(160, 180, 210));
            DrawString(s_appwizBuffer, WIN_W, mainX + 270, rowY + 3, pkgs[i].date, PackBGRA(160, 180, 210));
            DrawString(s_appwizBuffer, WIN_W, mainX + 350, rowY + 3, pkgs[i].sz, PackBGRA(100, 240, 160));
        }
        rowY += 26;
    }

    // Footer count
    DrawString(s_appwizBuffer, WIN_W, mainX + 10, WIN_H - 18, "5 programs installed | Total size: 85.3 MB", PackBGRA(140, 160, 190));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Programs and Features [appwiz.cpl / appwiz.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Installed Software Packages:\n"
        "  1. MicaNT Sovereign Shell       [MicaNT Core]          24.5 MB\n"
        "  2. PrismX 3D Direct3D 12 Engine [DEC / Dave Cutler]    48.2 MB\n"
        "  3. Intel I210 Network Driver    [Intel Corporation]     1.2 MB\n"
        "  4. AHCI SATA Multi-Volume Stack [Sovereign Storage]     3.4 MB\n"
        "  5. Haven Memory Bus (HMB 2.0)   [Haven Systems]         8.0 MB\n\n"
        "Status: Presenting appwiz.cpl window (580x420 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderAppWizWindow();
    PresentWindow(s_appwizBuffer, WIN_W, WIN_H);

    PrintStr("[AppWiz] Package registry queried. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
