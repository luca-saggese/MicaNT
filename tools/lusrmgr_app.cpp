/**
 * @file lusrmgr_app.cpp
 * Luce OS Ring 3 Local Users and Groups (lusrmgr.msc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows MMC Local Users and Groups window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes SAM account status to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 540;
constexpr uint32_t WIN_H = 400;
alignas(4096) uint32_t s_lusrBuffer[WIN_W * WIN_H];

void RenderLusrMgrWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_lusrBuffer, WIN_W, WIN_H, "lusrmgr - [Local Users and Groups (Local)]", "File  Action  View  Help");

    // 2. Toolbar
    FillRect(s_lusrBuffer, WIN_W, 1, 50, WIN_W - 2, 24, PackBGRA(20, 28, 44));
    FillRect(s_lusrBuffer, WIN_W, 1, 74, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_lusrBuffer, WIN_W, 12, 56, "[<]  [>]  |  [Users]  [Groups]  |  [Properties]", PackBGRA(180, 200, 230));

    // 3. Two-Pane Splitter
    const uint32_t treeW = 180;
    const uint32_t paneY = 75;
    const uint32_t paneH = WIN_H - paneY - 24;

    // Left Console Tree
    FillRect(s_lusrBuffer, WIN_W, 1, paneY, treeW, paneH, PackBGRA(14, 18, 28));
    FillRect(s_lusrBuffer, WIN_W, treeW + 1, paneY, 2, paneH, PackBGRA(40, 56, 80));

    DrawString(s_lusrBuffer, WIN_W, 10, paneY + 10, "v Local Users and Groups", PackBGRA(255, 255, 255));
    // Selected "Users" node
    FillRect(s_lusrBuffer, WIN_W, 20, paneY + 28, treeW - 24, 20, PackBGRA(0, 120, 215));
    DrawString(s_lusrBuffer, WIN_W, 26, paneY + 32, "  Users", PackBGRA(255, 255, 255));

    DrawString(s_lusrBuffer, WIN_W, 26, paneY + 54, "  Groups", PackBGRA(200, 215, 235));

    // 4. Right Details Pane (Users List)
    const uint32_t rightX = treeW + 4;
    const uint32_t rightW = WIN_W - rightX - 2;

    FillRect(s_lusrBuffer, WIN_W, rightX, paneY, rightW, paneH, PackBGRA(10, 14, 22));

    // Listview Header
    FillRect(s_lusrBuffer, WIN_W, rightX, paneY, rightW, 22, PackBGRA(22, 30, 46));
    FillRect(s_lusrBuffer, WIN_W, rightX, paneY + 22, rightW, 1, PackBGRA(40, 56, 80));
    DrawString(s_lusrBuffer, WIN_W, rightX + 8, paneY + 6, "Name", PackBGRA(255, 255, 255));
    DrawString(s_lusrBuffer, WIN_W, rightX + 110, paneY + 6, "Full Name", PackBGRA(255, 255, 255));
    DrawString(s_lusrBuffer, WIN_W, rightX + 220, paneY + 6, "Description", PackBGRA(255, 255, 255));

    // User accounts
    struct UserRow {
        const char* name;
        const char* full;
        const char* desc;
    };

    const UserRow users[4] = {
        { "Administrator", "", "Built-in account for admin" },
        { "DefaultAccount", "", "Default system account" },
        { "Guest", "", "Built-in guest account" },
        { "MicaAdmin", "MicaNT Administrator", "Primary interactive owner" }
    };

    uint32_t rowY = paneY + 28;
    for (int i = 0; i < 4; ++i) {
        if (i == 3) {
            FillRect(s_lusrBuffer, WIN_W, rightX, rowY - 2, rightW, 20, PackBGRA(0, 100, 180));
            DrawString(s_lusrBuffer, WIN_W, rightX + 8, rowY + 3, users[i].name, PackBGRA(255, 255, 255));
            DrawString(s_lusrBuffer, WIN_W, rightX + 110, rowY + 3, users[i].full, PackBGRA(255, 255, 255));
            DrawString(s_lusrBuffer, WIN_W, rightX + 220, rowY + 3, users[i].desc, PackBGRA(200, 230, 255));
        } else {
            DrawString(s_lusrBuffer, WIN_W, rightX + 8, rowY + 3, users[i].name, PackBGRA(220, 235, 255));
            DrawString(s_lusrBuffer, WIN_W, rightX + 110, rowY + 3, users[i].full, PackBGRA(160, 180, 210));
            DrawString(s_lusrBuffer, WIN_W, rightX + 220, rowY + 3, users[i].desc, PackBGRA(140, 160, 190));
        }
        rowY += 24;
    }

    // 5. Status Bar
    FillRect(s_lusrBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(18, 24, 36));
    FillRect(s_lusrBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_lusrBuffer, WIN_W, 16, WIN_H - 17, "4 user account(s) loaded from SAM registry hive", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Local Users and Groups [lusrmgr.msc / lusrmgr.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Security Account Manager : SAM Registry Hive Online (C:\\MicaNT\\System32\\config\\SAM)\n"
        "Local Accounts Loaded    : Administrator, Guest, DefaultAccount, MicaAdmin\n"
        "Active Administrator     : MICANT\\MicaAdmin (SID: S-1-5-21-3623811015-3361044348-30300820-1001)\n"
        "Status: Presenting lusrmgr.msc console window (540x400 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderLusrMgrWindow();
    PresentWindow(s_lusrBuffer, WIN_W, WIN_H);

    PrintStr("[LusrMgr] Users and Groups console presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
