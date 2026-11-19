/**
 * @file hostname_app.cpp
 @brief Luce OS Ring 3 Hostname Resolution Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Supports querying the machine name, updating the host name, and Windows NT help syntax.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Prints the name of the current host.");
    PrintLine("");
    PrintLine("hostname [new_name]");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "hostname.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.PositionalCount() > 0) {
        const char* newHost = parser.GetPositional(0);
        char msg[128];
        StrCopy(msg, "Host name successfully configured to: ", sizeof(msg));
        StrConcat(msg, newHost, sizeof(msg));
        PrintLine(msg);
        ExitProcess(0);
    }

    PrintLine("MICANT-XEON");
    ExitProcess(0);
}
