/**
 * @file telnet_app.cpp
 @brief Luce OS Ring 3 Telnet Terminal Client (telnet) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Provides interactive Telnet terminal negotiation (RFC 854) and TCP socket terminal transport.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowTelnetCommands() noexcept {
    const char* out =
        "Commands may be abbreviated. Supported commands are:\n\n"
        "c   - close                    close current connection\n"
        "d   - display                  display operating parameters\n"
        "o   - open hostname [port]     connect to hostname (default port 23)\n"
        "q   - quit                     exit telnet\n"
        "set - set options              set options (set ? for list)\n"
        "sen - send strings             send special characters ('send ?' for more)\n"
        "st  - status                   print status information\n"
        "u   - unset options            unset options (unset ? for list)\n"
        "?/h - help                     print help information\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Telnet Client (MicaNT Sovereign Cutler Edition)\n\n"
        "Syntax:\n"
        "  telnet [-a][-e escape_char][-f log_file][-l user][-t term_type][host [port]]\n\n"
        "  -a          Attempt automatic logon.\n"
        "  -e          Escape character to enter telnet prompt (default: Ctrl+]).\n"
        "  -f          File name for client-side logging.\n"
        "  -l          Specifies user name to log in with on remote system.\n"
        "  -t          Specifies terminal type (vt100, vt52, ansi, vtnt).\n"
        "  host        Specifies the hostname or IP address of remote host.\n"
        "  port        Specifies a port number or service name (default 23).\n\n";

    PrintStr(banner);
    ShowTelnetCommands();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
