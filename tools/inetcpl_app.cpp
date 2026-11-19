/**
 * @file inetcpl_app.cpp
 @brief Luce OS Ring 3 Internet Properties Applet (inetcpl.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Internet Properties window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes networking proxy & security zone telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 500;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_inetBuffer[WIN_W * WIN_H];

void RenderInternetPropertiesWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_inetBuffer, WIN_W, WIN_H, "Internet Properties", nullptr);

    // 2. Tabs Row: [General] [Security] [Privacy] [Content] [Connections] [Programs] [Advanced]
    const uint32_t tabY = 36;
    FillRect(s_inetBuffer, WIN_W, 16, tabY, 65, 22, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, 16, tabY, 65, 22, PackBGRA(0, 150, 255), 1);
    DrawString(s_inetBuffer, WIN_W, 22, tabY + 5, "General", PackBGRA(255, 255, 255));

    DrawString(s_inetBuffer, WIN_W, 90, tabY + 5, "Security", PackBGRA(160, 180, 210));
    DrawString(s_inetBuffer, WIN_W, 158, tabY + 5, "Privacy", PackBGRA(160, 180, 210));
    DrawString(s_inetBuffer, WIN_W, 222, tabY + 5, "Content", PackBGRA(160, 180, 210));
    DrawString(s_inetBuffer, WIN_W, 288, tabY + 5, "Connections", PackBGRA(160, 180, 210));
    DrawString(s_inetBuffer, WIN_W, 375, tabY + 5, "Advanced", PackBGRA(160, 180, 210));

    FillRect(s_inetBuffer, WIN_W, 16, tabY + 22, WIN_W - 32, 1, PackBGRA(35, 48, 70));

    // 3. Home page section
    DrawString(s_inetBuffer, WIN_W, 20, 68, "Home page", PackBGRA(255, 255, 255));
    DrawString(s_inetBuffer, WIN_W, 20, 84, "To create home page tabs, type each address on its own line.", PackBGRA(160, 180, 210));

    // URL input box
    FillRect(s_inetBuffer, WIN_W, 20, 102, WIN_W - 40, 26, PackBGRA(10, 14, 22));
    DrawRect(s_inetBuffer, WIN_W, 20, 102, WIN_W - 40, 26, PackBGRA(0, 150, 255), 1);
    DrawString(s_inetBuffer, WIN_W, 28, 108, "https://micant.sovereign.local", PackBGRA(255, 255, 255));

    // Home page action buttons
    FillRect(s_inetBuffer, WIN_W, 20, 134, 100, 24, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, 20, 134, 100, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_inetBuffer, WIN_W, 30, 140, "Use current", PackBGRA(200, 215, 235));

    FillRect(s_inetBuffer, WIN_W, 126, 134, 100, 24, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, 126, 134, 100, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_inetBuffer, WIN_W, 136, 140, "Use default", PackBGRA(200, 215, 235));

    FillRect(s_inetBuffer, WIN_W, 232, 134, 100, 24, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, 232, 134, 100, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_inetBuffer, WIN_W, 246, 140, "Use new tab", PackBGRA(200, 215, 235));

    // Separator line
    FillRect(s_inetBuffer, WIN_W, 20, 168, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 4. Browsing history section
    DrawString(s_inetBuffer, WIN_W, 20, 180, "Browsing history", PackBGRA(255, 255, 255));
    DrawString(s_inetBuffer, WIN_W, 20, 196, "Delete temporary files, history, cookies, and saved passwords.", PackBGRA(160, 180, 210));

    FillRect(s_inetBuffer, WIN_W, 20, 216, 14, 14, PackBGRA(12, 16, 24));
    DrawRect(s_inetBuffer, WIN_W, 20, 216, 14, 14, PackBGRA(50, 70, 100), 1);
    DrawString(s_inetBuffer, WIN_W, 42, 218, "Delete browsing history on exit", PackBGRA(220, 235, 255));

    FillRect(s_inetBuffer, WIN_W, WIN_W - 190, 238, 75, 24, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, WIN_W - 190, 238, 75, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_inetBuffer, WIN_W, WIN_W - 180, 244, "Delete...", PackBGRA(200, 215, 235));

    FillRect(s_inetBuffer, WIN_W, WIN_W - 105, 238, 85, 24, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, WIN_W - 105, 238, 85, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_inetBuffer, WIN_W, WIN_W - 98, 244, "Settings", PackBGRA(200, 215, 235));

    // Separator line
    FillRect(s_inetBuffer, WIN_W, 20, 276, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 5. Network Stack Telemetry
    DrawString(s_inetBuffer, WIN_W, 20, 290, "LAN Proxy Settings: Direct Hardware DMA (No Proxy)", PackBGRA(100, 240, 160));
    DrawString(s_inetBuffer, WIN_W, 20, 308, "DNS Resolvers: 213.186.33.99, 1.1.1.1 (Sovereign DNS Client)", PackBGRA(180, 200, 230));
    DrawString(s_inetBuffer, WIN_W, 20, 326, "TLS Encryption: TLS 1.3 / ChaCha20-Poly1305 Strict", PackBGRA(180, 200, 230));

    // 6. Dialog Buttons Row
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_inetBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(0, 120, 215));
    DrawRect(s_inetBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_inetBuffer, WIN_W, WIN_W - 220, btnY + 7, "OK", PackBGRA(255, 255, 255));

    FillRect(s_inetBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_inetBuffer, WIN_W, WIN_W - 146, btnY + 7, "Cancel", PackBGRA(200, 215, 235));

    FillRect(s_inetBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_inetBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_inetBuffer, WIN_W, WIN_W - 66, btnY + 7, "Apply", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Internet Properties Applet [inetcpl.cpl / inetcpl.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Network Protocol       : IPv4 / TCP / UDP / ICMP over Intel I210 DMA\n"
        "Security Zones         : Local Intranet, Trusted Sites, Internet Zone\n"
        "Cryptographic Engine   : Sovereign TLS 1.3 / BCrypt.sys Subsystem\n"
        "Status: Presenting inetcpl.cpl window (500x420 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderInternetPropertiesWindow();
    PresentWindow(s_inetBuffer, WIN_W, WIN_H);

    PrintStr("[INetCpl] Internet properties presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
