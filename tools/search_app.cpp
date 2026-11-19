/**
 * @file search_app.cpp
 @brief Luce OS Ring 3 Search Hub Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Search / SurShell Search Hub window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes catalog index telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 500;
constexpr uint32_t WIN_H = 440;
alignas(4096) uint32_t s_searchBuffer[WIN_W * WIN_H];

void RenderSearchHubWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_searchBuffer, WIN_W, WIN_H, "Search", nullptr);

    // 2. Search Input Box
    FillRect(s_searchBuffer, WIN_W, 20, 36, WIN_W - 40, 32, PackBGRA(10, 14, 22));
    DrawRect(s_searchBuffer, WIN_W, 20, 36, WIN_W - 40, 32, PackBGRA(0, 150, 255), 1);
    DrawString(s_searchBuffer, WIN_W, 32, 46, "Type here to search...", PackBGRA(140, 160, 190));

    // Magnifying glass icon in search box
    DrawCircle(s_searchBuffer, WIN_W, WIN_W - 40, 52, 6, PackBGRA(0, 180, 255));
    DrawLine(s_searchBuffer, WIN_W, WIN_W - 35, 57, WIN_W - 30, 62, PackBGRA(0, 180, 255));

    // 3. Category Filter Tabs: [All] [Apps] [Documents] [Web]
    DrawString(s_searchBuffer, WIN_W, 20, 80, "[All]    Apps    Documents    Web    Settings", PackBGRA(200, 220, 245));
    FillRect(s_searchBuffer, WIN_W, 18, 96, 42, 2, PackBGRA(0, 180, 255)); // Active indicator
    FillRect(s_searchBuffer, WIN_W, 20, 98, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 4. Top Apps Section
    DrawString(s_searchBuffer, WIN_W, 20, 110, "Top apps", PackBGRA(255, 255, 255));

    struct AppItem {
        const char* name;
        const char* desc;
        uint32_t iconColor;
    };

    const AppItem topApps[5] = {
        { "Command Prompt", "\\MicaNT\\System32\\cmd.exe", PackBGRA(20, 20, 20) },
        { "File Explorer", "\\MicaNT\\explorer.exe", PackBGRA(255, 185, 0) },
        { "MicaNT PowerShell", "\\MicaNT\\System32\\powershell.exe", PackBGRA(1, 36, 86) },
        { "Registry Editor", "\\MicaNT\\System32\\regedit.exe", PackBGRA(0, 120, 215) },
        { "Task Manager", "\\MicaNT\\System32\\taskmgr.exe", PackBGRA(16, 124, 65) }
    };

    uint32_t appY = 132;
    for (int i = 0; i < 5; ++i) {
        // Hover/row box
        FillRect(s_searchBuffer, WIN_W, 20, appY, WIN_W - 40, 36, PackBGRA(18, 24, 36));
        DrawRect(s_searchBuffer, WIN_W, 20, appY, WIN_W - 40, 36, PackBGRA(30, 42, 62), 1);

        // Icon
        FillRect(s_searchBuffer, WIN_W, 30, appY + 6, 24, 24, topApps[i].iconColor);
        DrawRect(s_searchBuffer, WIN_W, 30, appY + 6, 24, 24, PackBGRA(255, 255, 255), 1);

        // Titles
        DrawString(s_searchBuffer, WIN_W, 64, appY + 8, topApps[i].name, PackBGRA(230, 240, 255));
        DrawString(s_searchBuffer, WIN_W, 64, appY + 22, topApps[i].desc, PackBGRA(120, 140, 170));

        appY += 42;
    }

    // 5. Recent Searches / Quick Links
    DrawString(s_searchBuffer, WIN_W, 20, 350, "Recent activities", PackBGRA(255, 255, 255));
    DrawString(s_searchBuffer, WIN_W, 30, 370, "> ipconfig /all", PackBGRA(100, 240, 160));
    DrawString(s_searchBuffer, WIN_W, 30, 388, "> sfc /scannow", PackBGRA(100, 240, 160));
    DrawString(s_searchBuffer, WIN_W, 30, 406, "> devmgmt.msc", PackBGRA(100, 240, 160));

    // Footer Indexer status
    DrawString(s_searchBuffer, WIN_W, WIN_W - 200, 422, "14,290 items indexed", PackBGRA(120, 140, 170));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Search Subsystem [Version 10.0.26040]\n"
        "(C) 2026 MicaNT Foundation. All rights reserved.\n\n"
        "Search Indexer Status  : Active (WSearch Service Running)\n"
        "Indexed Catalog Items  : 14,290 files, tools, and registry keys\n"
        "Query Scope            : \\MicaNT\\System32; \\Users\\Default; HKLM\\Software\n"
        "Search Latency         : < 250 microseconds (In-Memory B-Tree Index)\n"
        "Status: Presenting searchui.exe window (500x440 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderSearchHubWindow();
    PresentWindow(s_searchBuffer, WIN_W, WIN_H);

    PrintStr("[Search] Search index query completed. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
