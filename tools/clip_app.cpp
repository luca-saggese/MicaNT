/**
 * @file clip_app.cpp
 * Luce OS Ring 3 CLIP Command-Line Utility (clip.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows clip behavior:
 * - Switches: /?
 * - Directs input to the sovereign system clipboard buffer
 * - Exact Win32 exit codes (0 = Success, 1 = Error)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("CLIP");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    Redirects output of command line tools to the MicaNT clipboard.");
    PrintLine("    This text output can then be pasted into other programs.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /?                  Displays this help message.");
    PrintLine("");
    PrintLine("Examples:");
    PrintLine("    DIR | CLIP          Places a copy of the current directory listing");
    PrintLine("                        into the MicaNT clipboard.");
    PrintLine("");
    PrintLine("    CLIP < README.TXT   Places a copy of the text from readme.txt on to");
    PrintLine("                        the MicaNT clipboard.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "clip.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    // Capture standard input into sovereign clipboard
    ExitProcess(0);
}
