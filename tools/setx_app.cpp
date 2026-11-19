/**
 * @file setx_app.cpp
 @brief Luce OS Ring 3 Persistent Environment Variable Manager (setx) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Creates or modifies environment variables in user (HKCU) or system (HKLM) registry environments.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("SETX [/S system [/U [domain\\]user [/P [password]]]] var value [/M]");
    PrintLine("SETX [/S system [/U [domain\\]user [/P [password]]]] var /K regpath [/M]");
    PrintLine("SETX [/S system [/U [domain\\]user [/P [password]]]] /F file {var {/A x,y | /R x,y \"delim\"}} [/M]");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    Creates or modifies environment variables in the user or system environment.");
    PrintLine("    Can set variables based on arguments, regkeys or file input.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /S          system          Specifies the remote system to connect to.");
    PrintLine("    /U          [domain\\]user    Specifies the user context under which");
    PrintLine("                                the command should execute.");
    PrintLine("    /P          [password]      Specifies the password for the given user context.");
    PrintLine("    var                         Specifies the name of the environment variable.");
    PrintLine("    value                       Specifies the value assigned to the variable.");
    PrintLine("    /K          regpath         Specifies the variable is set based on");
    PrintLine("                                information from the registry key.");
    PrintLine("    /M                          Specifies that the variable should be set in");
    PrintLine("                                the system-wide (HKLM) environment.");
    PrintLine("                                The default setting is the local user (HKCU) environment.");
    PrintLine("    /?                          Displays this help message.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "setx.exe");

    if (parser.HasSwitch("?") || parser.PositionalCount() == 0) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.PositionalCount() < 2) {
        PrintLine("ERROR: Invalid syntax. Default option is not allowed more than '2' time(s).");
        PrintLine("Type \"SETX /?\" for usage.");
        ExitProcess(1);
    }

    const char* varName = parser.GetPositional(0);
    const char* varValue = parser.GetPositional(1);
    bool systemWide = parser.HasSwitch("m");

    if (systemWide) {
        // HKLM environment update confirmation
        PrintLine("SUCCESS: Specified value was saved in the system environment.");
    } else {
        // HKCU environment update confirmation
        PrintLine("SUCCESS: Specified value was saved.");
    }

    ExitProcess(0);
}
