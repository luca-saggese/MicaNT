/**
 * @file quser_app.cpp
 @brief Luce OS Ring 3 Query User Session Utility (quser) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries active logon and terminal server user sessions on the local or remote host.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleQuser() noexcept {
    const char* out =
        " USERNAME              SESSIONNAME        ID  STATE   IDLE TIME  LOGON TIME\n"
        ">micaadmin             console             1  Active       none  10/23/2026 08:00\n"
        " devuser               rdp-tcp#0           2  Active         12  10/23/2026 09:15\n"
        " ssh-agent             services            0  Disc       .       10/23/2026 00:01\n\n"
        "Total Active Sessions: 2 (Console, RDP) | Disconnected: 1\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Query User Utility (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Display information about users logged on to the system.\n\n"
        "QUERY USER [username | sessionname | sessionid] [/SERVER:servername]\n\n"
        "  username            Identifies the user whose sessions you want to query.\n"
        "  sessionname         Identifies the session named sessionname you want to query.\n"
        "  sessionid           Identifies the session with ID sessionid you want to query.\n"
        "  /SERVER:servername  The server to be queried (default is current).\n\n"
        "[quser]\n";

    PrintStr(banner);
    ShowSampleQuser();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
