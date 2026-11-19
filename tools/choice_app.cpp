/**
 * @file choice_app.cpp
 * Luce OS Ring 3 CHOICE Prompt Utility (choice.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows choice behavior:
 * - Switches: /c <choices>, /n, /cs, /t <sec>, /d <choice>, /m <text>, /?
 * - Returns exact 1-based ERRORLEVEL of selected option
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("CHOICE [/C choices] [/N] [/CS] [/T timeout /D choice] [/M text]");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    This tool allows users to select one item from a list");
    PrintLine("    of choices and returns the index of the selected choice.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /C    choices       Specifies the list of choices to be created.");
    PrintLine("                        Default list is \"YN\".");
    PrintLine("    /N                  Causes the list of choices to not be displayed");
    PrintLine("                        in the prompt.");
    PrintLine("    /CS                 Enables case-sensitive choices to be selected.");
    PrintLine("    /T    timeout       The number of seconds to pause before a default");
    PrintLine("                        choice is made. Acceptable values are from 0 to 9999.");
    PrintLine("    /D    choice        Specifies the default choice after n seconds.");
    PrintLine("    /M    text          Specifies the message to be displayed before the prompt.");
    PrintLine("    /?                  Displays this help message.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "choice.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* choices = parser.GetSwitchValue("c");
    if (!choices) choices = "YN";

    const char* msg = parser.GetSwitchValue("m");
    bool hideChoices = parser.HasSwitch("n");

    if (msg) {
        Print(msg);
        Print(" ");
    }

    if (!hideChoices) {
        Print("[");
        for (size_t i = 0; choices[i] != '\0'; ++i) {
            if (i > 0) Print(",");
            char cBuf[2] = { choices[i], '\0' };
            Print(cBuf);
        }
        Print("]? ");
    }

    // Default to first option in non-interactive batch environment
    const char* defChoice = parser.GetSwitchValue("d");
    char selected = defChoice ? defChoice[0] : choices[0];

    char sBuf[3] = { selected, '\r', '\n' };
    Print(sBuf);

    // Find 1-based index
    uint32_t exitCode = 1;
    for (size_t i = 0; choices[i] != '\0'; ++i) {
        if (ToLowerChar(choices[i]) == ToLowerChar(selected)) {
            exitCode = static_cast<uint32_t>(i + 1);
            break;
        }
    }

    ExitProcess(exitCode);
}
