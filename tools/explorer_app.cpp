/**
 * @file explorer_app.cpp
 @brief Luce OS Ring 3 File Explorer Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell File Explorer window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_expBuffer[WIN_W * WIN_H];

void RenderExplorerWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_expBuffer, WIN_W, WIN_H, "File Explorer", "File   Home   Share   View");

    // 2. Command Toolbar
    FillRect(s_expBuffer, WIN_W, 1, 49, WIN_W - 2, 22, PackBGRA(22, 30, 46));
    DrawRect(s_expBuffer, WIN_W, 1, 70, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_expBuffer, WIN_W, 10, 56, "[+] New   Cut   Copy   Paste   Rename   Delete   Properties", PackBGRA(200, 220, 245));

    // 3. Breadcrumb Address Bar (Height = 22)
    FillRect(s_expBuffer, WIN_W, 1, 71, WIN_W - 2, 22, PackBGRA(16, 22, 34));
    DrawRect(s_expBuffer, WIN_W, 1, 92, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_expBuffer, WIN_W, 10, 78, "This PC > Local Disk (C:) > MicaNT > System32", PackBGRA(0, 210, 255));

    // 4. Split Panes: Left Navigation (width 160), Right Items (remaining)
    constexpr uint32_t navW = 160;
    FillRect(s_expBuffer, WIN_W, 1, 93, navW, WIN_H - 115, PackBGRA(14, 18, 28));
    DrawRect(s_expBuffer, WIN_W, navW + 1, 93, 1, WIN_H - 115, PackBGRA(35, 48, 70));

    // Left Navigation Tree
    DrawString(s_expBuffer, WIN_W, 10, 102, "* Quick access", PackBGRA(180, 200, 230));
    DrawString(s_expBuffer, WIN_W, 10, 120, "v This PC", PackBGRA(0, 210, 255));
    DrawString(s_expBuffer, WIN_W, 22, 138, "- Local Disk (C:)", PackBGRA(240, 245, 255));
    DrawString(s_expBuffer, WIN_W, 22, 156, "- Data Volume (D:)", PackBGRA(160, 180, 210));
    DrawString(s_expBuffer, WIN_W, 22, 174, "- Linux Root (E:)", PackBGRA(160, 180, 210));
    DrawString(s_expBuffer, WIN_W, 10, 196, "Network", PackBGRA(180, 200, 230));

    // Right Items Pane
    FillRect(s_expBuffer, WIN_W, navW + 2, 93, WIN_W - navW - 3, WIN_H - 115, PackBGRA(16, 22, 35));

    // Items Column Header
    FillRect(s_expBuffer, WIN_W, navW + 2, 93, WIN_W - navW - 3, 20, PackBGRA(22, 28, 44));
    DrawRect(s_expBuffer, WIN_W, navW + 2, 112, WIN_W - navW - 3, 1, PackBGRA(35, 48, 70));

    DrawString(s_expBuffer, WIN_W, navW + 10, 99, "Name", PackBGRA(140, 165, 200));
    DrawString(s_expBuffer, WIN_W, navW + 170, 99, "Type", PackBGRA(140, 165, 200));
    DrawString(s_expBuffer, WIN_W, navW + 290, 99, "Size", PackBGRA(140, 165, 200));

    // Item rows
    const struct Item {
        const char* name;
        const char* type;
        const char* size;
    } items[] = {
        {"drivers",       "File folder",    ""},
        {"config",        "File folder",    ""},
        {"ntoskrnl.exe",  "Application",    "950 KB"},
        {"cmd.exe",       "Application",      "3 KB"},
        {"notepad.exe",   "Application",      "4 KB"},
        {"calc.exe",      "Application",      "4 KB"},
        {"taskmgr.exe",   "Application",      "4 KB"},
        {"regedit.exe",   "Application",      "4 KB"},
    };

    uint32_t iy = 118;
    for (size_t i = 0; i < 8; ++i) {
        DrawString(s_expBuffer, WIN_W, navW + 10, iy + 4, items[i].name, PackBGRA(230, 240, 255));
        DrawString(s_expBuffer, WIN_W, navW + 170, iy + 4, items[i].type, PackBGRA(160, 180, 210));
        DrawString(s_expBuffer, WIN_W, navW + 290, iy + 4, items[i].size, PackBGRA(100, 240, 160));
        iy += 18;
    }

    // 5. Status Bar
    FillRect(s_expBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_expBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_expBuffer, WIN_W, 12, WIN_H - 15, "65 items", PackBGRA(180, 200, 230));
    DrawString(s_expBuffer, WIN_W, 120, WIN_H - 15, "|  1 item selected", PackBGRA(180, 200, 230));
    DrawString(s_expBuffer, WIN_W, 360, WIN_H - 15, "128 GB Total (127 GB Free)", PackBGRA(0, 210, 255));

    PresentWindow(s_expBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign File Explorer (explorer.exe):");
    PrintLine("  Active Path: C:\\MicaNT\\System32 (65 items, 1.17 MB)");
    PrintLine("  Mounted Volumes: C: [FAT32 128G], D: [FAT32 1.7T], E: [Ext4 20G RO]");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderExplorerWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
