/**
 * @file fsmgmt_app.cpp
 @brief Luce OS Ring 3 Shared Folders Management Console (fsmgmt.msc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows MMC Shared Folders window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes SMB shares & active session status to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 520;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_fsBuffer[WIN_W * WIN_H];

void RenderFsMgmtWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_fsBuffer, WIN_W, WIN_H, "fsmgmt - [Shared Folders (Local)]", "File  Action  View  Help");

    // 2. Toolbar
    FillRect(s_fsBuffer, WIN_W, 1, 50, WIN_W - 2, 24, PackBGRA(20, 28, 44));
    FillRect(s_fsBuffer, WIN_W, 1, 74, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_fsBuffer, WIN_W, 12, 56, "[<]  [>]  |  [New Share]  |  [Stop Sharing]", PackBGRA(180, 200, 230));

    // 3. Two-Pane Splitter
    const uint32_t treeW = 160;
    const uint32_t paneY = 75;
    const uint32_t paneH = WIN_H - paneY - 24;

    // Left Console Tree
    FillRect(s_fsBuffer, WIN_W, 1, paneY, treeW, paneH, PackBGRA(14, 18, 28));
    FillRect(s_fsBuffer, WIN_W, treeW + 1, paneY, 2, paneH, PackBGRA(40, 56, 80));

    DrawString(s_fsBuffer, WIN_W, 10, paneY + 10, "v Shared Folders", PackBGRA(255, 255, 255));

    // Selected "Shares" node
    FillRect(s_fsBuffer, WIN_W, 18, paneY + 28, treeW - 22, 20, PackBGRA(0, 120, 215));
    DrawString(s_fsBuffer, WIN_W, 24, paneY + 32, "  Shares", PackBGRA(255, 255, 255));

    DrawString(s_fsBuffer, WIN_W, 24, paneY + 54, "  Sessions", PackBGRA(200, 215, 235));
    DrawString(s_fsBuffer, WIN_W, 24, paneY + 74, "  Open Files", PackBGRA(200, 215, 235));

    // 4. Right Details Pane (Shares List)
    const uint32_t rightX = treeW + 4;
    const uint32_t rightW = WIN_W - rightX - 2;

    FillRect(s_fsBuffer, WIN_W, rightX, paneY, rightW, paneH, PackBGRA(10, 14, 22));

    // Listview Header
    FillRect(s_fsBuffer, WIN_W, rightX, paneY, rightW, 22, PackBGRA(22, 30, 46));
    FillRect(s_fsBuffer, WIN_W, rightX, paneY + 22, rightW, 1, PackBGRA(40, 56, 80));
    DrawString(s_fsBuffer, WIN_W, rightX + 8, paneY + 6, "Shared Folder", PackBGRA(255, 255, 255));
    DrawString(s_fsBuffer, WIN_W, rightX + 110, paneY + 6, "Shared Path", PackBGRA(255, 255, 255));
    DrawString(s_fsBuffer, WIN_W, rightX + 220, paneY + 6, "Type", PackBGRA(255, 255, 255));

    struct ShareRow {
        const char* name;
        const char* path;
        const char* type;
    };

    const ShareRow shares[3] = {
        { "ADMIN$", "C:\\MicaNT", "MicaNT (Remote Admin)" },
        { "C$", "C:\\", "MicaNT (Default Share)" },
        { "IPC$", "", "MicaNT (Remote IPC)" }
    };

    uint32_t rowY = paneY + 28;
    for (int i = 0; i < 3; ++i) {
        if (i == 1) {
            FillRect(s_fsBuffer, WIN_W, rightX, rowY - 2, rightW, 20, PackBGRA(0, 100, 180));
            DrawString(s_fsBuffer, WIN_W, rightX + 8, rowY + 3, shares[i].name, PackBGRA(255, 255, 255));
            DrawString(s_fsBuffer, WIN_W, rightX + 110, rowY + 3, shares[i].path, PackBGRA(255, 255, 255));
            DrawString(s_fsBuffer, WIN_W, rightX + 220, rowY + 3, shares[i].type, PackBGRA(200, 230, 255));
        } else {
            DrawString(s_fsBuffer, WIN_W, rightX + 8, rowY + 3, shares[i].name, PackBGRA(220, 235, 255));
            DrawString(s_fsBuffer, WIN_W, rightX + 110, rowY + 3, shares[i].path, PackBGRA(160, 180, 210));
            DrawString(s_fsBuffer, WIN_W, rightX + 220, rowY + 3, shares[i].type, PackBGRA(140, 160, 190));
        }
        rowY += 24;
    }

    // 5. Status Bar
    FillRect(s_fsBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(18, 24, 36));
    FillRect(s_fsBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_fsBuffer, WIN_W, 16, WIN_H - 17, "3 active share(s) | LanmanServer service running", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Shared Folders Console [fsmgmt.msc / fsmgmt.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "LanmanServer Subsystem  : Online (SMB 3.1.1 Protocol)\n"
        "Active SMB Shares       : ADMIN$ (C:\\MicaNT), C$ (C:\\), IPC$\n"
        "Client Sessions         : 1 Active Session (MICANT\\MicaAdmin)\n"
        "Status: Presenting fsmgmt.msc console window (520x380 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderFsMgmtWindow();
    PresentWindow(s_fsBuffer, WIN_W, WIN_H);

    PrintStr("[FsMgmt] Shared Folders console presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
