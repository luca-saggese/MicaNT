/**
 * @file gpedit_app.cpp
 @brief Luce OS Ring 3 Local Group Policy Editor (gpedit.msc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows MMC Local Group Policy Editor window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes GPO tree status to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_gpeBuffer[WIN_W * WIN_H];

void RenderGpeditWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_gpeBuffer, WIN_W, WIN_H, "Local Group Policy Editor", "File  Action  View  Help");

    // 2. Toolbar Row (Y=50)
    FillRect(s_gpeBuffer, WIN_W, 1, 50, WIN_W - 2, 24, PackBGRA(20, 28, 44));
    FillRect(s_gpeBuffer, WIN_W, 1, 74, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_gpeBuffer, WIN_W, 12, 56, "[<]  [>]  [^]  |  [Tree]  [Properties]", PackBGRA(180, 200, 230));

    // 3. Two-Pane Splitter
    const uint32_t treeW = 210;
    const uint32_t paneY = 75;
    const uint32_t paneH = WIN_H - paneY - 24;

    // Left Console Tree Pane
    FillRect(s_gpeBuffer, WIN_W, 1, paneY, treeW, paneH, PackBGRA(14, 18, 28));
    FillRect(s_gpeBuffer, WIN_W, treeW + 1, paneY, 2, paneH, PackBGRA(40, 56, 80));

    // Console Tree Nodes
    DrawString(s_gpeBuffer, WIN_W, 10, paneY + 10, "v Local Computer Policy", PackBGRA(255, 255, 255));

    DrawString(s_gpeBuffer, WIN_W, 22, paneY + 28, "v Computer Configuration", PackBGRA(220, 235, 255));
    DrawString(s_gpeBuffer, WIN_W, 36, paneY + 44, "> Software Settings", PackBGRA(160, 180, 210));
    DrawString(s_gpeBuffer, WIN_W, 36, paneY + 60, "> MicaNT Settings", PackBGRA(160, 180, 210));
    DrawString(s_gpeBuffer, WIN_W, 36, paneY + 76, "v Administrative Templates", PackBGRA(100, 200, 255));
    DrawString(s_gpeBuffer, WIN_W, 48, paneY + 92, "> Control Panel", PackBGRA(160, 180, 210));
    DrawString(s_gpeBuffer, WIN_W, 48, paneY + 108, "> Network", PackBGRA(160, 180, 210));
    DrawString(s_gpeBuffer, WIN_W, 48, paneY + 124, "> System", PackBGRA(160, 180, 210));
    DrawString(s_gpeBuffer, WIN_W, 48, paneY + 140, "> MicaNT Components", PackBGRA(160, 180, 210));

    DrawString(s_gpeBuffer, WIN_W, 22, paneY + 162, "> User Configuration", PackBGRA(220, 235, 255));

    // 4. Right Details Pane
    const uint32_t rightX = treeW + 4;
    const uint32_t rightW = WIN_W - rightX - 2;

    FillRect(s_gpeBuffer, WIN_W, rightX, paneY, rightW, paneH, PackBGRA(10, 14, 22));

    // Listview Header
    FillRect(s_gpeBuffer, WIN_W, rightX, paneY, rightW, 22, PackBGRA(22, 30, 46));
    FillRect(s_gpeBuffer, WIN_W, rightX, paneY + 22, rightW, 1, PackBGRA(40, 56, 80));
    DrawString(s_gpeBuffer, WIN_W, rightX + 8, paneY + 6, "Setting", PackBGRA(255, 255, 255));
    DrawString(s_gpeBuffer, WIN_W, rightX + 220, paneY + 6, "State", PackBGRA(255, 255, 255));

    // Policy rows
    struct GpoRow {
        const char* name;
        const char* state;
        bool active;
    };

    const GpoRow rows[6] = {
        { "Turn off real-time antivirus", "Disabled", true },
        { "Enforce driver code integrity", "Enabled", true },
        { "Prevent installation of unsigned PE", "Enabled", true },
        { "Disable Windows Error Reporting", "Enabled", false },
        { "Configure Automatic Updates", "Not configured", false },
        { "Allow Telemetry & Data Collection", "Disabled", true }
    };

    uint32_t rowY = paneY + 28;
    for (int i = 0; i < 6; ++i) {
        if (i == 1) {
            FillRect(s_gpeBuffer, WIN_W, rightX, rowY - 2, rightW, 20, PackBGRA(0, 120, 215));
            DrawString(s_gpeBuffer, WIN_W, rightX + 8, rowY + 3, rows[i].name, PackBGRA(255, 255, 255));
            DrawString(s_gpeBuffer, WIN_W, rightX + 220, rowY + 3, rows[i].state, PackBGRA(255, 255, 255));
        } else {
            DrawString(s_gpeBuffer, WIN_W, rightX + 8, rowY + 3, rows[i].name, PackBGRA(220, 235, 255));
            DrawString(s_gpeBuffer, WIN_W, rightX + 220, rowY + 3, rows[i].state, rows[i].active ? PackBGRA(100, 240, 160) : PackBGRA(140, 160, 190));
        }
        rowY += 24;
    }

    // 5. Status Bar at Bottom
    FillRect(s_gpeBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(18, 24, 36));
    FillRect(s_gpeBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_gpeBuffer, WIN_W, 16, WIN_H - 17, "6 setting(s) in selected folder | Registry GPO Engine Synced", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Local Group Policy Editor [gpedit.msc / gpedit.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Policy Storage Root    : C:\\MicaNT\\System32\\GroupPolicy\\\n"
        "Active GPO Scope       : Computer Configuration & User Configuration\n"
        "Security Enforcement  : Strict Code Integrity & Zero Telemetry Baseline\n"
        "Status: Presenting gpedit.msc console window (580x420 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderGpeditWindow();
    PresentWindow(s_gpeBuffer, WIN_W, WIN_H);

    PrintStr("[GPEdit] Group Policy console presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
