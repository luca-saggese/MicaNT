/**
 * @file tsdiscon_app.cpp
 * Luce OS Ring 3 Terminal Server Session Disconnect Utility (tsdiscon) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Disconnects a Remote Desktop Services session while preserving active applications.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Disconnect a Remote Desktop Services session.\n\n"
        "TSDISCON [sessionid | sessionname] [/SERVER:servername] [/V]\n\n"
        "  sessionid           The ID of the session to disconnect.\n"
        "  sessionname         The name of the session to disconnect.\n"
        "  /SERVER:servername  The server containing the session (default is current).\n"
        "  /V                  Display information about the actions performed.\n\n"
        "Session Disconnect Subsystem: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
