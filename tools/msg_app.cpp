/**
 * @file msg_app.cpp
 * Luce OS Ring 3 Terminal Services Messaging Utility (msg) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Sends an interactive pop-up message to one or more user sessions.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Send a message to a user.\n\n"
        "MSG {username | sessionname | sessionid | @filename | *}\n"
        "    [/SERVER:servername] [/TIME:seconds] [/V] [/W] [message]\n\n"
        "  username            Identifies the specified username.\n"
        "  sessionname         The name of the session.\n"
        "  sessionid           The ID of the session.\n"
        "  @filename           Identifies a file containing a list of usernames,\n"
        "                      sessionnames, and sessionids to send the message to.\n"
        "  *                   Send message to all sessions on specified server.\n"
        "  /SERVER:servername  server to contact (default is current).\n"
        "  /TIME:seconds       Time delay to wait for receiver to acknowledge msg.\n"
        "  /V                  Display information about actions being performed.\n"
        "  /W                  Wait for response from user, useful with /V.\n"
        "  message             Message to send.  If none specified, prompts for it\n"
        "                      or reads from stdin.\n\n"
        "Terminal Services Messaging Protocol: Online.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
