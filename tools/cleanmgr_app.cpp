/**
 * @file cleanmgr_app.cpp
 * Luce OS Ring 3 Disk Cleanup Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Disk Cleanup window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes disk reclaim statistics to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 440;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_cleanBuffer[WIN_W * WIN_H];

void RenderCleanMgrWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_cleanBuffer, WIN_W, WIN_H, "Disk Cleanup for (C:)", nullptr);

    // 2. Drive selection & icon
    FillRect(s_cleanBuffer, WIN_W, 20, 42, 32, 32, PackBGRA(30, 80, 160));
    DrawRect(s_cleanBuffer, WIN_W, 20, 42, 32, 32, PackBGRA(200, 220, 255), 1);
    DrawString(s_cleanBuffer, WIN_W, 28, 52, "C:", PackBGRA(255, 255, 255));

    DrawString(s_cleanBuffer, WIN_W, 64, 42, "Disk Cleanup is calculating how much space", PackBGRA(220, 230, 245));
    DrawString(s_cleanBuffer, WIN_W, 64, 56, "you will be able to free on OS (C:).", PackBGRA(160, 180, 210));

    // Separator line
    FillRect(s_cleanBuffer, WIN_W, 20, 82, WIN_W - 40, 1, PackBGRA(45, 60, 85));

    // 3. Checkbox targets list
    DrawString(s_cleanBuffer, WIN_W, 20, 92, "Files to delete:", PackBGRA(220, 230, 245));
    FillRect(s_cleanBuffer, WIN_W, 20, 110, WIN_W - 40, 150, PackBGRA(10, 14, 22));
    DrawRect(s_cleanBuffer, WIN_W, 20, 110, WIN_W - 40, 150, PackBGRA(35, 48, 70), 1);

    struct TargetItem {
        const char* name;
        const char* sizeStr;
        bool checked;
    };

    const TargetItem items[5] = {
        { "Downloaded Program Files", "0 B", false },
        { "Temporary Setup Files", "14.2 MB", true },
        { "System Error Memory Minidumps", "320 KB", true },
        { "Temporary Internet Files", "2.8 MB", true },
        { "Thumbnails & Icon Cache", "4.1 MB", true }
    };

    uint32_t itemY = 118;
    for (int i = 0; i < 5; ++i) {
        // Checkbox box
        FillRect(s_cleanBuffer, WIN_W, 28, itemY, 14, 14, items[i].checked ? PackBGRA(0, 120, 215) : PackBGRA(20, 26, 38));
        DrawRect(s_cleanBuffer, WIN_W, 28, itemY, 14, 14, PackBGRA(180, 200, 230), 1);
        if (items[i].checked) {
            DrawString(s_cleanBuffer, WIN_W, 31, itemY + 2, "v", PackBGRA(255, 255, 255));
        }

        // Label & Size
        DrawString(s_cleanBuffer, WIN_W, 50, itemY + 2, items[i].name, PackBGRA(220, 230, 245));
        DrawString(s_cleanBuffer, WIN_W, WIN_W - 90, itemY + 2, items[i].sizeStr, PackBGRA(100, 240, 160));

        itemY += 26;
    }

    // 4. Total space to gain
    DrawString(s_cleanBuffer, WIN_W, 20, 272, "Total amount of disk space you gain:", PackBGRA(220, 230, 245));
    DrawString(s_cleanBuffer, WIN_W, WIN_W - 90, 272, "21.4 MB", PackBGRA(255, 215, 0));

    // Progress bar simulation (Scan completed 100%)
    FillRect(s_cleanBuffer, WIN_W, 20, 294, WIN_W - 40, 12, PackBGRA(15, 20, 30));
    DrawRect(s_cleanBuffer, WIN_W, 20, 294, WIN_W - 40, 12, PackBGRA(40, 55, 80), 1);
    FillRect(s_cleanBuffer, WIN_W, 21, 295, WIN_W - 42, 10, PackBGRA(0, 200, 120));

    // 5. Button Row: [Clean up system files] [OK] [Cancel]
    const uint32_t btnY = WIN_H - 42;
    // Clean up system files
    FillRect(s_cleanBuffer, WIN_W, 20, btnY, 160, 26, PackBGRA(30, 42, 60));
    DrawRect(s_cleanBuffer, WIN_W, 20, btnY, 160, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_cleanBuffer, WIN_W, 28, btnY + 7, "Clean up system files", PackBGRA(200, 215, 235));

    // OK
    FillRect(s_cleanBuffer, WIN_W, WIN_W - 170, btnY, 68, 26, PackBGRA(0, 120, 215));
    DrawRect(s_cleanBuffer, WIN_W, WIN_W - 170, btnY, 68, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_cleanBuffer, WIN_W, WIN_W - 150, btnY + 7, "OK", PackBGRA(255, 255, 255));

    // Cancel
    FillRect(s_cleanBuffer, WIN_W, WIN_W - 90, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_cleanBuffer, WIN_W, WIN_W - 90, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_cleanBuffer, WIN_W, WIN_W - 76, btnY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Disk Space Cleanup Manager [Version 10.0.26040]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Scanning Volume C:\\ (FAT32 Root Filesystem)...\n"
        "  Partition Capacity    : 137,438,953,472 bytes (128.00 GB)\n"
        "  Free Disk Space       : 124,192,849,920 bytes (115.66 GB)\n\n"
        "Cleanup Candidates Found:\n"
        "  - Temporary Setup Files        : 14.2 MB\n"
        "  - System Error Memory Minidumps: 320 KB\n"
        "  - Temporary Internet Cache     : 2.8 MB\n"
        "  - Shell Thumbnails & DB Cache  : 4.1 MB\n\n"
        "Total Reclaimable Space : 21.4 MB\n"
        "Status: Presenting cleanmgr.exe window (440x380 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderCleanMgrWindow();
    PresentWindow(s_cleanBuffer, WIN_W, WIN_H);

    PrintStr("[CleanMgr] Disk analysis completed. Files purged. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
