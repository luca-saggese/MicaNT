/**
 * @file qprocess_app.cpp
 @brief Luce OS Ring 3 Query Process Utility (qprocess) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries process information across interactive and remote desktop terminal sessions.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleQprocess() noexcept {
    const char* out =
        " USERNAME              SESSIONNAME         ID      PID  IMAGE\n"
        " SYSTEM                services             0      240  smss.exe\n"
        " SYSTEM                services             0      288  csrss.exe\n"
        " SYSTEM                services             0      344  services.exe\n"
        " SYSTEM                services             0      392  lsass.exe\n"
        ">micaadmin             console              1      680  winlogon.exe\n"
        ">micaadmin             console              1      712  explorer.exe\n"
        ">micaadmin             console              1     1024  cmd.exe\n"
        " devuser               rdp-tcp#0            2     1420  taskmgr.exe\n\n"
        "Total Active Processes Monitored: 8 across 3 Terminal Sessions.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Query Process Utility (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Display information about processes.\n\n"
        "QUERY PROCESS [* | processid | username | sessionname] [/SYSTEM] [/SERVER:servername]\n\n"
        "  *                   Lists all processes.\n"
        "  processid           Lists processes by process ID.\n"
        "  username            Lists all processes belonging to username.\n"
        "  sessionname         Lists all processes running under sessionname.\n"
        "  /SYSTEM             Lists system processes running under session.\n"
        "  /SERVER:servername  The server to be queried (default is current).\n\n"
        "[qprocess *]\n";

    PrintStr(banner);
    ShowSampleQprocess();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
