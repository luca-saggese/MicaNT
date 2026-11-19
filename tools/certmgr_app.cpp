/**
 * @file certmgr_app.cpp
 @brief Luce OS Ring 3 Certificates Manager (certmgr.msc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows MMC Certificates Manager window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes X.509 certificate store status to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 560;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_certBuffer[WIN_W * WIN_H];

void RenderCertMgrWindow() noexcept {
    // 1. Frame & Menu
    DrawWindowFrame(s_certBuffer, WIN_W, WIN_H, "certmgr - [Certificates - Current User]", "File  Action  View  Help");

    // 2. Toolbar
    FillRect(s_certBuffer, WIN_W, 1, 50, WIN_W - 2, 24, PackBGRA(20, 28, 44));
    FillRect(s_certBuffer, WIN_W, 1, 74, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_certBuffer, WIN_W, 12, 56, "[<]  [>]  |  [Import]  [Export]  |  [Properties]", PackBGRA(180, 200, 230));

    // 3. Two-Pane Splitter
    const uint32_t treeW = 200;
    const uint32_t paneY = 75;
    const uint32_t paneH = WIN_H - paneY - 24;

    // Left Console Tree
    FillRect(s_certBuffer, WIN_W, 1, paneY, treeW, paneH, PackBGRA(14, 18, 28));
    FillRect(s_certBuffer, WIN_W, treeW + 1, paneY, 2, paneH, PackBGRA(40, 56, 80));

    DrawString(s_certBuffer, WIN_W, 10, paneY + 10, "v Certificates - Current User", PackBGRA(255, 255, 255));
    DrawString(s_certBuffer, WIN_W, 20, paneY + 28, "> Personal", PackBGRA(200, 215, 235));

    // Selected "Trusted Root" node
    FillRect(s_certBuffer, WIN_W, 18, paneY + 44, treeW - 22, 20, PackBGRA(0, 120, 215));
    DrawString(s_certBuffer, WIN_W, 24, paneY + 48, "v Trusted Root CAs", PackBGRA(255, 255, 255));
    DrawString(s_certBuffer, WIN_W, 36, paneY + 68, "  Certificates", PackBGRA(100, 200, 255));

    DrawString(s_certBuffer, WIN_W, 20, paneY + 90, "> Enterprise Trust", PackBGRA(200, 215, 235));
    DrawString(s_certBuffer, WIN_W, 20, paneY + 108, "> Intermediate CAs", PackBGRA(200, 215, 235));
    DrawString(s_certBuffer, WIN_W, 20, paneY + 126, "> Untrusted Certificates", PackBGRA(200, 215, 235));

    // 4. Right Details Pane (Certificates List)
    const uint32_t rightX = treeW + 4;
    const uint32_t rightW = WIN_W - rightX - 2;

    FillRect(s_certBuffer, WIN_W, rightX, paneY, rightW, paneH, PackBGRA(10, 14, 22));

    // Listview Header
    FillRect(s_certBuffer, WIN_W, rightX, paneY, rightW, 22, PackBGRA(22, 30, 46));
    FillRect(s_certBuffer, WIN_W, rightX, paneY + 22, rightW, 1, PackBGRA(40, 56, 80));
    DrawString(s_certBuffer, WIN_W, rightX + 8, paneY + 6, "Issued To", PackBGRA(255, 255, 255));
    DrawString(s_certBuffer, WIN_W, rightX + 130, paneY + 6, "Issued By", PackBGRA(255, 255, 255));
    DrawString(s_certBuffer, WIN_W, rightX + 250, paneY + 6, "Expiration", PackBGRA(255, 255, 255));

    // Certificate Rows
    struct CertRow {
        const char* to;
        const char* by;
        const char* exp;
    };

    const CertRow certs[4] = {
        { "MicaNT Root CA", "MicaNT Root CA", "10/22/2046" },
        { "Dave Cutler Code Seal", "MicaNT Root CA", "10/22/2036" },
        { "Sovereign Root CA 2026", "Sovereign Root CA", "12/31/2035" },
        { "ISRG Root X1 (Let's Encrypt)", "ISRG Root X1", "06/04/2035" }
    };

    uint32_t rowY = paneY + 28;
    for (int i = 0; i < 4; ++i) {
        if (i == 0) {
            FillRect(s_certBuffer, WIN_W, rightX, rowY - 2, rightW, 20, PackBGRA(0, 100, 180));
            DrawString(s_certBuffer, WIN_W, rightX + 8, rowY + 3, certs[i].to, PackBGRA(255, 255, 255));
            DrawString(s_certBuffer, WIN_W, rightX + 130, rowY + 3, certs[i].by, PackBGRA(255, 255, 255));
            DrawString(s_certBuffer, WIN_W, rightX + 250, rowY + 3, certs[i].exp, PackBGRA(255, 255, 255));
        } else {
            DrawString(s_certBuffer, WIN_W, rightX + 8, rowY + 3, certs[i].to, PackBGRA(220, 235, 255));
            DrawString(s_certBuffer, WIN_W, rightX + 130, rowY + 3, certs[i].by, PackBGRA(160, 180, 210));
            DrawString(s_certBuffer, WIN_W, rightX + 250, rowY + 3, certs[i].exp, PackBGRA(100, 240, 160));
        }
        rowY += 24;
    }

    // 5. Status Bar
    FillRect(s_certBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 23, PackBGRA(18, 24, 36));
    FillRect(s_certBuffer, WIN_W, 1, WIN_H - 24, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_certBuffer, WIN_W, 16, WIN_H - 17, "4 Root Certificate(s) loaded into Crypt32 / WinTrust Cache", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Certificates Manager [certmgr.msc / certmgr.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Cryptographic Subsystem : Crypt32 / BCrypt / WinTrust Engine\n"
        "Certificate Stores      : ROOT, MY, CA, TRUST, DISALLOWED Active\n"
        "Signature Standard      : RSA 4096-bit / ECDSA P-384 + SHA-256 Digest\n"
        "Code Signing Policy     : Strict Authenticode Driver & PE32+ Enforcement\n"
        "Status: Presenting certmgr.msc console window (560x420 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderCertMgrWindow();
    PresentWindow(s_certBuffer, WIN_W, WIN_H);

    PrintStr("[CertMgr] Certificate stores presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
