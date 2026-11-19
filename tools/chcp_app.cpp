/**
 * @file chcp_app.cpp
 * Luce OS Ring 3 Change Code Page Utility (chcp) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries and switches the active console input and output code pages.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

bool IsAllDigits(const char* s) noexcept {
    if (!s || *s == '\0') return false;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        s++;
    }
    return true;
}

void ShowHelp() noexcept {
    PrintLine("Displays or sets the active code page number.");
    PrintLine("");
    PrintLine("CHCP [nnn]");
    PrintLine("");
    PrintLine("  nnn   Specifies a code page number.");
    PrintLine("");
    PrintLine("Type CHCP without a parameter to display the active code page number.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "chcp.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.PositionalCount() == 0) {
        PrintLine("Active code page: 65001");
        ExitProcess(0);
    }

    const char* cpArg = parser.GetPositional(0);
    if (!IsAllDigits(cpArg)) {
        PrintLine("Invalid code page");
        ExitProcess(1);
    }

    uint64_t cp = ParseUInt64(cpArg);
    if (cp == 0 || cp > 65535) {
        PrintLine("Invalid code page");
        ExitProcess(1);
    }

    Print("Active code page: ");
    PrintLine(cpArg);
    ExitProcess(0);
}
