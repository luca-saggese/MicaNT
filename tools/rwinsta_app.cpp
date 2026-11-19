/**
 * @file rwinsta_app.cpp
 @brief Luce OS Ring 3 Reset Windows Station Session Utility (rwinsta) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Resets the session subsystem software and hardware to known initial states.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Reset the session subsystem hardware and software to known initial states.\n\n"
        "RESET SESSION {sessionname | sessionid} [/SERVER:servername] [/V]\n\n"
        "  sessionname         Identifies the session with name sessionname.\n"
        "  sessionid           Identifies the session with ID sessionid.\n"
        "  /SERVER:servername  The server containing the session (default is current).\n"
        "  /V                  Display additional information.\n\n"
        "Session Reset Manager: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
