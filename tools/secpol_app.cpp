/**
 * @file secpol_app.cpp
 @brief Luce OS Ring 3 Local Security Policy Console (secpol.msc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows MMC Local Security Policy window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes security baseline telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 560;
constexpr uint32_t WIN_H = 400;
alignas(4096) uint32_t s_secBuffer[WIN_W * WIN_H];

void RenderSecPolWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_secBuffer, WIN_W, WIN_H, "secpol - [Local Security Policy]", "File  Action  View  Help");

    // 2. Toolbar
    FillRect(s_secBuffer, WIN_W, 1, 50, WIN_W - 2, 24, PackBGRA(20, 28, 44));
    FillRect(s_secBuffer, WIN_W, 1, 74, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_secBuffer, WIN_W, 12, 56, "[<]  [>]  |  [Export Policy]  |  [Properties]", PackBGRA(180, 200, 230));

    // 3. Two-Pane Splitter
    const uint32_t treeW = 200;
    const uint32_t paneY = 75;
    const uint32_t paneH = WIN_H - paneY - 24;

    // Left Console Tree
    FillRect(s_secBuffer, WIN_W, 1, paneY, treeW, paneH, PackBGRA(14, 18, 28));
    FillRect(s_secBuffer, WIN_W, treeW + 1, paneY, 2, paneH, PackBGRA(40, 56, 80));

    DrawString(s_secBuffer, WIN_W, 10, paneY + 10, "v Security Settings", PackBGRA(255, 255, 255));
    DrawString(s_secBuffer, WIN_W, 20, paneY + 28, "> Account Policies", PackBGRA(200, 215, 235));

    // Selected "Local Policies" node
    FillRect(s_secBuffer, WIN_W, 18, paneY + 44, treeW - 22, 20, PackBGRA(0, 120, 215));
    DrawString(s_secBuffer, WIN_W, 24, paneY + 48, "v Local Policies", PackBGRA(255, 255, 255));
    DrawString(s_secBuffer, WIN_W, 36, paneY + 68, "  Audit Policy", PackBGRA(200, 215, 235));
    DrawString(s_secBuffer, WIN_W, 36, paneY + 86, "  User Rights Assignment", PackBGRA(200, 215, 235));
    DrawString(s_secBuffer, WIN_W, 36, paneY + 104, "  Security Options", PackBGRA(100, 200, 255));

    DrawString(s_secBuffer, WIN_W, 20, paneY + 126, "> MicaNT Sovereign Firewall", PackBGRA(200, 215, 235));
    DrawString(s_secBuffer, WIN_W, 20, paneY + 144, "> Application Control", PackBGRA(200, 215, 235));

    // 4. Right Details Pane (Security Options List)
    const uint32_t rightX = treeW + 4;
    const uint32_t rightW = WIN_W - rightX - 2;

    FillRect(s_secBuffer, WIN_W, rightX, paneY, rightW, paneH, PackBGRA(10, 14, 22));

    // Listview Header
    FillRect(s_secBuffer, WIN_W, rightX, paneY, rightW, 22, PackBGRA(22, 30, 46));
    FillRect(s_secBuffer, WIN_W, rightX, paneY + 22, rightW, 1, PackBGRA(40, 56, 80));
    DrawString(s_secBuffer, WIN_W, rightX + 8, paneY + 6, "Policy", PackBGRA(255, 255, 255));
    DrawString(s_secBuffer, WIN_W, rightX + 220, paneY + 6, "Security Setting", PackBGRA(255, 255, 255));

    struct SecRow {
        const char* pol;
        const char* set;
    };

    const SecRow secRows[5] = {
        { "User Account Control: Behavior", "Prompt for credentials" },
        { "Accounts: Limit local account use", "Enabled" },
        { "Network security: LAN Manager", "Send NTLMv2 response only" },
        { "System cryptography: FIPS compliant", "Enabled" },
        { "Audit: Force audit policy subcategory", "Enabled" }
    };

    uint32_t rowY = paneY + 28;
    for (int i = 0; i < 5; ++i) {
        if (i == 0) {
            FillRect(s_secBuffer, WIN_W, rightX, rowY - 2, rightW, 20, PackBGRA(0, 100, 180));
            DrawString(s_secBuffer, WIN_W, rightX + 8, rowY + 3, secRows[i].pol, PackBGRA(255, 255, 255));
            DrawString(s_secBuffer, WIN_W, rightX + 220, rowY + 3, secRows[i].set, PackBGRA(255, 255, 255));
        } else {
            DrawString(s_secBuffer, WIN_W, rightX + 8, rowY + 3, secRows[i].pol, PackBGRA(220, 235, 255));
            DrawString(s_secBuffer, WIN_W, rightX + 220, rowY + 3, secRows[i].set, PackBGRA(100, 240, 160));
        }
        rowY += 24;
    }

    // 5. Status Bar
    FillRect(s_secBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(18, 24, 36));
    FillRect(s_secBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_secBuffer, WIN_W, 16, WIN_H - 17, "Sovereign Security Baseline Active | Security Reference Monitor (SRM) Enforcing", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Local Security Policy Console [secpol.msc / secpol.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Security Reference Monitor : SRM Ring 0 Access Check Active\n"
        "Auditing Engine            : SeAuditPrivilege Active\n"
        "Cryptography Standard      : FIPS 140-3 Strict Mode\n"
        "User Account Control (UAC) : High-Integrity Token Isolation Active\n"
        "Status: Presenting secpol.msc console window (560x400 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderSecPolWindow();
    PresentWindow(s_secBuffer, WIN_W, WIN_H);

    PrintStr("[SecPol] Local Security Policy console presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
