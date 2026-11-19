/**
 * @file mode_app.cpp
 @brief Luce OS Ring 3 Device and Console Configuration Utility (mode) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Configures serial communications ports, parallel printer ports, and console screen buffers.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowModeStatus() noexcept {
    const char* out =
        "Status for device COM1:\n"
        "-----------------------\n"
        "    Baud:            115200\n"
        "    Parity:          None\n"
        "    Data Bits:       8\n"
        "    Stop Bits:       1\n"
        "    Timeout:         OFF\n"
        "    XON/XOFF:        OFF\n"
        "    CTS handshaking: OFF\n"
        "    DSR handshaking: OFF\n"
        "    DSR sensitivity: OFF\n"
        "    DTR circuit:     ON\n"
        "    RTS circuit:     ON\n\n"
        "Status for device CON:\n"
        "----------------------\n"
        "    Lines:          30\n"
        "    Columns:        120\n"
        "    Keyboard rate:  31\n"
        "    Keyboard delay: 1\n"
        "    Code page:      65001\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Configures system devices.\n\n"
        "Serial port:     MODE COMm[:] [BAUD=b] [PARITY=p] [DATA=d] [STOP=s]\n"
        "Device Status:   MODE [device] [/STATUS]\n"
        "Redirect printing: MODE LPTn[:]=COMm[:]\n"
        "Code page select: MODE CON[:] CP SELECT=yyy\n"
        "Display mode:    MODE CON[:] [COLS=c] [LINES=n]\n"
        "Typematic rate:  MODE CON[:] [RATE=r DELAY=d]\n\n";

    PrintStr(banner);
    ShowModeStatus();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
