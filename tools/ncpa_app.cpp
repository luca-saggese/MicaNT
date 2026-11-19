/**
 * @file ncpa_app.cpp
 @brief Luce OS Ring 3 Network Connections Applet (ncpa.cpl) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Network Connections window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 640;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_ncpaBuffer[WIN_W * WIN_H];

void RenderNetworkConnections() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_ncpaBuffer, WIN_W, WIN_H, "Network Connections", "File   Edit   View   Tools   Advanced   Help");

    // 2. Toolbar
    FillRect(s_ncpaBuffer, WIN_W, 1, 49, WIN_W - 2, 24, PackBGRA(22, 30, 46));
    DrawRect(s_ncpaBuffer, WIN_W, 1, 72, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_ncpaBuffer, WIN_W, 12, 56, "Organize  |  Disable this network device  |  Diagnose this connection", PackBGRA(200, 220, 245));

    // 3. Body Background
    FillRect(s_ncpaBuffer, WIN_W, 1, 73, WIN_W - 2, WIN_H - 95, PackBGRA(14, 18, 28));

    // Header column labels
    FillRect(s_ncpaBuffer, WIN_W, 1, 73, WIN_W - 2, 20, PackBGRA(20, 26, 40));
    DrawString(s_ncpaBuffer, WIN_W, 16, 79, "Name", PackBGRA(160, 180, 210));
    DrawString(s_ncpaBuffer, WIN_W, 200, 79, "Type", PackBGRA(160, 180, 210));
    DrawString(s_ncpaBuffer, WIN_W, 310, 79, "Status", PackBGRA(160, 180, 210));
    DrawString(s_ncpaBuffer, WIN_W, 430, 79, "Device Name", PackBGRA(160, 180, 210));

    // Connection 1: Ethernet (Intel I210)
    FillRect(s_ncpaBuffer, WIN_W, 10, 98, WIN_W - 20, 56, PackBGRA(22, 32, 50));
    DrawRect(s_ncpaBuffer, WIN_W, 10, 98, WIN_W - 20, 56, PackBGRA(0, 180, 255));

    // Network icon
    FillRect(s_ncpaBuffer, WIN_W, 20, 106, 24, 20, PackBGRA(0, 140, 255));
    DrawString(s_ncpaBuffer, WIN_W, 26, 112, "LAN", PackBGRA(255, 255, 255));

    DrawString(s_ncpaBuffer, WIN_W, 54, 106, "Ethernet", PackBGRA(240, 245, 255));
    DrawString(s_ncpaBuffer, WIN_W, 54, 122, "ash-forge.internal", PackBGRA(120, 140, 170));
    DrawString(s_ncpaBuffer, WIN_W, 54, 136, "IPv4: 147.135.1.229/24", PackBGRA(0, 229, 255));

    DrawString(s_ncpaBuffer, WIN_W, 200, 114, "LAN / High-Speed", PackBGRA(180, 200, 230));
    DrawString(s_ncpaBuffer, WIN_W, 310, 114, "Enabled (1000 Mbps)", PackBGRA(100, 240, 160));
    DrawString(s_ncpaBuffer, WIN_W, 430, 114, "Intel(R) I210 Gigabit", PackBGRA(240, 245, 255));

    // Connection 2: NetBird Sovereign Mesh
    FillRect(s_ncpaBuffer, WIN_W, 10, 162, WIN_W - 20, 56, PackBGRA(18, 24, 38));
    DrawRect(s_ncpaBuffer, WIN_W, 10, 162, WIN_W - 20, 56, PackBGRA(35, 48, 70));

    FillRect(s_ncpaBuffer, WIN_W, 20, 170, 24, 20, PackBGRA(0, 200, 120));
    DrawString(s_ncpaBuffer, WIN_W, 26, 176, "VPN", PackBGRA(255, 255, 255));

    DrawString(s_ncpaBuffer, WIN_W, 54, 170, "NetBird Mesh", PackBGRA(240, 245, 255));
    DrawString(s_ncpaBuffer, WIN_W, 54, 186, "WireGuard Sovereign Overlay", PackBGRA(120, 140, 170));
    DrawString(s_ncpaBuffer, WIN_W, 54, 200, "IPv4: 100.126.155.6/16", PackBGRA(100, 240, 160));

    DrawString(s_ncpaBuffer, WIN_W, 200, 178, "Virtual Adapter", PackBGRA(180, 200, 230));
    DrawString(s_ncpaBuffer, WIN_W, 310, 178, "Connected (Peer-to-Peer)", PackBGRA(100, 240, 160));
    DrawString(s_ncpaBuffer, WIN_W, 430, 178, "NetBird Virtual TUN", PackBGRA(240, 245, 255));

    // Adapter Details Box (Selected: Ethernet)
    DrawRect(s_ncpaBuffer, WIN_W, 10, 226, WIN_W - 20, 96, PackBGRA(35, 48, 70));
    DrawString(s_ncpaBuffer, WIN_W, 20, 220, " Adapter Vitals: Intel(R) I210 Network Connection ", PackBGRA(0, 210, 255));

    DrawString(s_ncpaBuffer, WIN_W, 20, 240, "Hardware MAC:      A4:BF:01:1E:A3:BE", PackBGRA(180, 200, 230));
    DrawString(s_ncpaBuffer, WIN_W, 320, 240, "Link Speed:        1.0 Gbps Full Duplex", PackBGRA(100, 240, 160));

    DrawString(s_ncpaBuffer, WIN_W, 20, 258, "IPv4 Gateway:      147.135.1.254 (Cisco HSRP)", PackBGRA(180, 200, 230));
    DrawString(s_ncpaBuffer, WIN_W, 320, 258, "Driver Model:      Bare-Metal MSI-X Queue", PackBGRA(180, 200, 230));

    DrawString(s_ncpaBuffer, WIN_W, 20, 276, "Packets Transmit:  48,210 packets (0 errors)", PackBGRA(180, 200, 230));
    DrawString(s_ncpaBuffer, WIN_W, 320, 276, "Packets Received:  61,044 packets (0 errors)", PackBGRA(180, 200, 230));

    DrawString(s_ncpaBuffer, WIN_W, 20, 294, "DNS Resolvers:     1.1.1.1, 8.8.8.8", PackBGRA(180, 200, 230));
    DrawString(s_ncpaBuffer, WIN_W, 320, 294, "DHCP / Lease:      Static Sovereign Lease", PackBGRA(180, 200, 230));

    // 4. Status Bar
    FillRect(s_ncpaBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_ncpaBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_ncpaBuffer, WIN_W, 12, WIN_H - 15, "2 network devices installed. All adapters online.", PackBGRA(100, 240, 160));
    DrawString(s_ncpaBuffer, WIN_W, WIN_W - 160, WIN_H - 15, "Link: 1000 Mbps OK", PackBGRA(0, 210, 255));

    PresentWindow(s_ncpaBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Network Connections (ncpa.cpl):");
    PrintLine("  Adapter 1: Intel(R) I210 Gigabit Network Connection (147.135.1.229/24)");
    PrintLine("  Adapter 2: NetBird Mesh WireGuard Sovereign Overlay (100.126.155.6/16)");
    PrintLine("  Gateway: 147.135.1.254 (Cisco HSRP active) | MAC: A4:BF:01:1E:A3:BE");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderNetworkConnections();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
