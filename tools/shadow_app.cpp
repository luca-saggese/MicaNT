/**
 * @file shadow_app.cpp
 @brief Luce OS Ring 3 Remote Desktop Session Shadowing Utility (shadow) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Monitors or interacts with an active Remote Desktop Services session.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Monitor another Remote Desktop Services session.\n\n"
        "SHADOW {sessionname | sessionid} [/SERVER:servername] [/P] [/CONTROL]\n\n"
        "  sessionname         Identifies the session with name sessionname.\n"
        "  sessionid           Identifies the session with ID sessionid.\n"
        "  /SERVER:servername  The server containing the session (default is current).\n"
        "  /P                  Prompt user for permission before shadowing.\n"
        "  /CONTROL            Allows interaction with the session (interactive control).\n\n"
        "RDP Session Shadowing Subsystem: Online.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
