/**
 * @file telephon_cpl_app.cpp
 @brief Luce OS Ring 3 Phone and Modem / Serial COM Port Options (telephon.cpl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Phone and Modem window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes UART 16550 serial port telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 480;
constexpr uint32_t WIN_H = 360;
alignas(4096) uint32_t s_telBuffer[WIN_W * WIN_H];

void RenderTelephonyWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_telBuffer, WIN_W, WIN_H, "Phone and Modem", nullptr);

    // 2. Tabs Row: [Dialing Rules] [Modems & Serial] [Advanced]
    const uint32_t tabY = 36;
    FillRect(s_telBuffer, WIN_W, 20, tabY, 100, 22, PackBGRA(30, 42, 60));
    DrawRect(s_telBuffer, WIN_W, 20, tabY, 100, 22, PackBGRA(0, 150, 255), 1);
    DrawString(s_telBuffer, WIN_W, 26, tabY + 5, "Modems & Serial", PackBGRA(255, 255, 255));

    DrawString(s_telBuffer, WIN_W, 135, tabY + 5, "Dialing Rules", PackBGRA(160, 180, 210));
    DrawString(s_telBuffer, WIN_W, 245, tabY + 5, "Advanced", PackBGRA(160, 180, 210));

    FillRect(s_telBuffer, WIN_W, 20, tabY + 22, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 3. Hardware listview
    DrawString(s_telBuffer, WIN_W, 20, 68, "The following hardware ports and modems are installed:", PackBGRA(220, 235, 255));

    FillRect(s_telBuffer, WIN_W, 20, 88, WIN_W - 40, 140, PackBGRA(10, 14, 22));
    DrawRect(s_telBuffer, WIN_W, 20, 88, WIN_W - 40, 140, PackBGRA(35, 48, 70), 1);

    // Header row
    FillRect(s_telBuffer, WIN_W, 21, 89, WIN_W - 42, 20, PackBGRA(20, 28, 44));
    DrawString(s_telBuffer, WIN_W, 30, 93, "Port / Modem", PackBGRA(255, 255, 255));
    DrawString(s_telBuffer, WIN_W, 240, 93, "Attached To", PackBGRA(255, 255, 255));
    DrawString(s_telBuffer, WIN_W, WIN_W - 100, 93, "Baud Rate", PackBGRA(255, 255, 255));

    // Port 1: COM1 (UART 16550) - Selected
    FillRect(s_telBuffer, WIN_W, 21, 110, WIN_W - 42, 24, PackBGRA(0, 120, 215));
    DrawString(s_telBuffer, WIN_W, 30, 115, "Communications Port (COM1)", PackBGRA(255, 255, 255));
    DrawString(s_telBuffer, WIN_W, 240, 115, "I/O 0x3F8, IRQ 4", PackBGRA(255, 255, 255));
    DrawString(s_telBuffer, WIN_W, WIN_W - 100, 115, "115200 8N1", PackBGRA(255, 255, 255));

    // Port 2: COM2 (UART 16550)
    DrawString(s_telBuffer, WIN_W, 30, 138, "Communications Port (COM2)", PackBGRA(200, 215, 235));
    DrawString(s_telBuffer, WIN_W, 240, 138, "I/O 0x2F8, IRQ 3", PackBGRA(160, 180, 210));
    DrawString(s_telBuffer, WIN_W, WIN_W - 100, 138, "115200 8N1", PackBGRA(160, 180, 210));

    // Port 3: Serial Over LAN (SOL / IPMI BMC)
    DrawString(s_telBuffer, WIN_W, 30, 162, "Supermicro IPMI SOL Interface", PackBGRA(200, 215, 235));
    DrawString(s_telBuffer, WIN_W, 240, 162, "BMC Port 0xCA2", PackBGRA(160, 180, 210));
    DrawString(s_telBuffer, WIN_W, WIN_W - 100, 162, "Active", PackBGRA(100, 255, 160));

    // 4. Action Buttons
    const uint32_t actY = 240;
    FillRect(s_telBuffer, WIN_W, 20, actY, 80, 24, PackBGRA(30, 42, 60));
    DrawRect(s_telBuffer, WIN_W, 20, actY, 80, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_telBuffer, WIN_W, 34, actY + 6, "Add...", PackBGRA(200, 215, 235));

    FillRect(s_telBuffer, WIN_W, 108, actY, 80, 24, PackBGRA(30, 42, 60));
    DrawRect(s_telBuffer, WIN_W, 108, actY, 80, 24, PackBGRA(70, 90, 120), 1);
    DrawString(s_telBuffer, WIN_W, 120, actY + 6, "Remove", PackBGRA(160, 180, 210));

    FillRect(s_telBuffer, WIN_W, WIN_W - 110, actY, 90, 24, PackBGRA(0, 120, 215));
    DrawRect(s_telBuffer, WIN_W, WIN_W - 110, actY, 90, 24, PackBGRA(255, 255, 255), 1);
    DrawString(s_telBuffer, WIN_W, WIN_W - 100, actY + 6, "Properties", PackBGRA(255, 255, 255));

    // Separator line
    FillRect(s_telBuffer, WIN_W, 20, 276, WIN_W - 40, 1, PackBGRA(35, 48, 70));

    // 5. Diagnostics
    DrawString(s_telBuffer, WIN_W, 20, 288, "UART Driver: NS16550A FIFO Buffered High-Speed Serial", PackBGRA(100, 240, 160));

    // 6. Dialog OK Button
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_telBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(0, 120, 215));
    DrawRect(s_telBuffer, WIN_W, WIN_W - 90, btnY, 70, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_telBuffer, WIN_W, WIN_W - 74, btnY + 7, "OK", PackBGRA(255, 255, 255));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Phone and Modem Subsystem [telephon.cpl / telephon.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Serial Ports Detected  : COM1 (0x3F8, IRQ 4), COM2 (0x2F8, IRQ 3)\n"
        "UART Hardware Model    : NS16550A Compatible FIFO (16-byte Buffer)\n"
        "Baud Configuration     : 115,200 Baud, 8 Data Bits, No Parity, 1 Stop Bit\n"
        "Serial Over LAN (SOL)  : Supermicro AST2400 BMC SOL Channel Active\n"
        "Status: Presenting telephon.cpl window (480x360 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderTelephonyWindow();
    PresentWindow(s_telBuffer, WIN_W, WIN_H);

    PrintStr("[Telephon CPL] Serial communications presented. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
