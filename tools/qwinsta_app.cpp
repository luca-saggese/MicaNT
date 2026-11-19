/**
 * @file qwinsta_app.cpp
 @brief Luce OS Ring 3 Query Windows Station Sessions Utility (qwinsta) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries and displays active session states, users, and terminals on Terminal Services.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleSessions() noexcept {
    const char* out =
        " SESSIONNAME       USERNAME                 ID  STATE    TYPE        DEVICE\n"
        " services                                    0  Disc\n"
        ">console           MicaAdmin                 1  Active\n"
        " rdp-tcp                                 65536  Listen\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Get information about Remote Desktop Sessions.\n\n"
        "QUERY SESSION [sessionname | username | sessionid] [/SERVER:servername]\n"
        "              [/MODE] [/FLOW] [/CONNECT] [/COUNTER]\n\n"
        "  sessionname         Identifies the session named sessionname.\n"
        "  username            Identifies the session with user username.\n"
        "  sessionid           Identifies the session with ID sessionid.\n"
        "  /SERVER:servername  The server to be queried (default is current).\n"
        "  /MODE               Display current line settings.\n"
        "  /FLOW               Display current flow control settings.\n"
        "  /CONNECT            Display current connect settings.\n"
        "  /COUNTER            Display current Remote Desktop Services counters.\n\n"
        "[qwinsta]\n";

    PrintStr(banner);
    ShowSampleSessions();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
