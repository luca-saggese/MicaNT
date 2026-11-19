/**
 * @file reset_app.cpp
 * Luce OS Ring 3 Terminal Session Reset Command (reset) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Resets a remote desktop session or terminal connection.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Reset a Remote Desktop Services session.\n\n"
        "RESET SESSION {sessionname | sessionid} [/SERVER:servername] [/V]\n\n"
        "  sessionname         Identifies the session with name sessionname.\n"
        "  sessionid           Identifies the session with ID sessionid.\n"
        "  /SERVER:servername  The server containing the session (default is current).\n"
        "  /V                  Display additional information.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
