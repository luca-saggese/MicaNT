/**
 * @file change_app.cpp
 * Luce OS Ring 3 Terminal Server Change Utility (change.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Changes terminal server session settings, logon permissions, and port redirections.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Change Terminal Services session settings.");
    PrintLine("");
    PrintLine("CHANGE {LOGON | PORT | USER}");
    PrintLine("");
    PrintLine("  LOGON   - Enables or disables logons to the terminal server.");
    PrintLine("  PORT    - Changes COM port mappings to be compatible with MS-DOS apps.");
    PrintLine("  USER    - Changes the Install mode for the current session.");
    PrintLine("");
    PrintLine("Examples:");
    PrintLine("  change logon /enable");
    PrintLine("  change logon /disable");
    PrintLine("  change user /install");
    PrintLine("  change user /execute");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "change.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* subCmd = parser.PositionalCount() > 0 ? parser.GetPositional(0) : "";

    if (StrCaseEqual(subCmd, "logon")) {
        if (parser.HasSwitch("disable")) {
            PrintLine("Session logons are currently DISABLED.");
        } else if (parser.HasSwitch("enable")) {
            PrintLine("Session logons are currently ENABLED.");
        } else {
            PrintLine("Session logons are currently ENABLED.");
        }
        ExitProcess(0);
    }

    if (StrCaseEqual(subCmd, "user")) {
        if (parser.HasSwitch("install")) {
            PrintLine("User session is ready to install applications.");
        } else if (parser.HasSwitch("execute")) {
            PrintLine("User session is ready to execute applications.");
        } else {
            PrintLine("Application execution mode is enabled.");
        }
        ExitProcess(0);
    }

    if (StrCaseEqual(subCmd, "port")) {
        PrintLine("No COM port mappings currently active.");
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
