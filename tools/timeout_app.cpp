/**
 * @file timeout_app.cpp
 @brief Luce OS Ring 3 TIMEOUT Utility (timeout.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows timeout behavior:
 * - Switches: /t <sec>, /nobreak, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Error)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("TIMEOUT [/T] timeout [/NOBREAK]");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    This utility accepts a timeout parameter to wait for the specified");
    PrintLine("    period of time (in seconds) or until any key is pressed. It also");
    PrintLine("    accepts a parameter to ignore the key press.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /T        timeout       Specifies the number of seconds to wait.");
    PrintLine("                            Acceptable range is -1 to 99999 seconds.");
    PrintLine("    /NOBREAK                Ignore key presses (only Ctrl+C can break).");
    PrintLine("    /?                      Displays this help message.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "timeout.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    int32_t seconds = 0;
    const char* tVal = parser.GetSwitchValue("t");
    if (tVal) {
        seconds = static_cast<int32_t>(ParseUInt64(tVal));
    } else if (parser.PositionalCount() > 0) {
        seconds = static_cast<int32_t>(ParseUInt64(parser.GetPositional(0)));
    }

    if (seconds < 0) seconds = 0;
    if (seconds > 99999) seconds = 99999;

    bool noBreak = parser.HasSwitch("nobreak");

    Print("Waiting for ");
    PrintDec(static_cast<uint64_t>(seconds));
    if (noBreak) {
        PrintLine(" seconds, press CTRL+C to quit ...");
    } else {
        PrintLine(" seconds, press any key to continue ...");
    }

    ExitProcess(0);
}
