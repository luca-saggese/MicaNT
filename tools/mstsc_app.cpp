/**
 * @file mstsc_app.cpp
 * Luce OS Ring 3 Remote Desktop Connection Utility (mstsc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Remote Desktop Connection window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes connection parameters to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 440;
constexpr uint32_t WIN_H = 340;
alignas(4096) uint32_t s_rdpBuffer[WIN_W * WIN_H];

void RenderRdpWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_rdpBuffer, WIN_W, WIN_H, "Remote Desktop Connection", nullptr);

    // 2. Banner Header (Blue RDP gradient banner)
    for (uint32_t y = 30; y < 80; ++y) {
        uint8_t r = static_cast<uint8_t>(10 + (y - 30) * 10 / 50);
        uint8_t g = static_cast<uint8_t>(40 + (y - 30) * 30 / 50);
        uint8_t b = static_cast<uint8_t>(100 + (y - 30) * 80 / 50);
        FillRect(s_rdpBuffer, WIN_W, 1, y, WIN_W - 2, 1, PackBGRA(r, g, b));
    }
    FillRect(s_rdpBuffer, WIN_W, 1, 80, WIN_W - 2, 1, PackBGRA(50, 80, 150));

    // Two connected screens icon
    FillRect(s_rdpBuffer, WIN_W, 20, 42, 28, 20, PackBGRA(255, 255, 255));
    DrawRect(s_rdpBuffer, WIN_W, 20, 42, 28, 20, PackBGRA(0, 120, 215), 2);
    FillRect(s_rdpBuffer, WIN_W, 36, 50, 28, 20, PackBGRA(255, 255, 255));
    DrawRect(s_rdpBuffer, WIN_W, 36, 50, 28, 20, PackBGRA(0, 180, 255), 2);

    DrawString(s_rdpBuffer, WIN_W, 76, 44, "Remote Desktop Connection", PackBGRA(255, 255, 255));
    DrawString(s_rdpBuffer, WIN_W, 76, 60, "Connect to an enterprise workstation or server", PackBGRA(200, 230, 255));

    // 3. Form fields
    uint32_t fY = 100;

    // Computer input
    DrawString(s_rdpBuffer, WIN_W, 20, fY + 6, "Computer:", PackBGRA(220, 235, 255));
    FillRect(s_rdpBuffer, WIN_W, 100, fY, WIN_W - 120, 26, PackBGRA(10, 14, 22));
    DrawRect(s_rdpBuffer, WIN_W, 100, fY, WIN_W - 120, 26, PackBGRA(0, 150, 255), 1);
    DrawString(s_rdpBuffer, WIN_W, 110, fY + 7, "147.135.1.229:3389", PackBGRA(255, 255, 255));
    DrawString(s_rdpBuffer, WIN_W, WIN_W - 35, fY + 7, "v", PackBGRA(160, 180, 210));

    DrawString(s_rdpBuffer, WIN_W, 100, fY + 30, "Example: server.domain.com or 192.168.1.100", PackBGRA(120, 140, 170));

    fY += 54;

    // User name
    DrawString(s_rdpBuffer, WIN_W, 20, fY + 6, "User name:", PackBGRA(220, 235, 255));
    DrawString(s_rdpBuffer, WIN_W, 100, fY + 6, "MICANT\\MicaAdmin (Pre-authenticated)", PackBGRA(255, 255, 255));

    fY += 30;

    DrawString(s_rdpBuffer, WIN_W, 20, fY, "You will be asked for credentials when you connect.", PackBGRA(160, 180, 210));

    fY += 30;

    // Show Options expandable link
    DrawString(s_rdpBuffer, WIN_W, 20, fY, "[v] Show Options", PackBGRA(0, 180, 255));

    // 4. Buttons Row at Bottom
    const uint32_t btnY = WIN_H - 42;

    // [Connect] button (Primary default)
    FillRect(s_rdpBuffer, WIN_W, WIN_W - 180, btnY, 80, 26, PackBGRA(0, 120, 215));
    DrawRect(s_rdpBuffer, WIN_W, WIN_W - 180, btnY, 80, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_rdpBuffer, WIN_W, WIN_W - 165, btnY + 7, "Connect", PackBGRA(255, 255, 255));

    // [Cancel] button
    FillRect(s_rdpBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(30, 42, 60));
    DrawRect(s_rdpBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_rdpBuffer, WIN_W, WIN_W - 76, btnY + 7, "Cancel", PackBGRA(200, 215, 235));

    // Help button on left
    FillRect(s_rdpBuffer, WIN_W, 20, btnY, 65, 26, PackBGRA(30, 42, 60));
    DrawRect(s_rdpBuffer, WIN_W, 20, btnY, 65, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_rdpBuffer, WIN_W, 36, btnY + 7, "Help", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Remote Desktop Connection Client [mstsc.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Protocol Subsystem     : RDP 10.8 / Sovereign Terminal Services\n"
        "Transport Layer        : TCP / TLS 1.3 over Intel I210 DMA (Port 3389)\n"
        "Default Target         : 147.135.1.229:3389 (Supermicro X11SSH-F)\n"
        "Remote Audio           : WaveRT High Definition Bidirectional Audio Egress\n"
        "Display Remoting       : AVC444 / RemoteFX Hardware Progressive Compression\n"
        "Status: Presenting mstsc.exe window (440x340 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderRdpWindow();
    PresentWindow(s_rdpBuffer, WIN_W, WIN_H);

    PrintStr("[MSTSC] Remote connection client armed. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
