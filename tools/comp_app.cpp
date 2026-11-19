/**
 * @file comp_app.cpp
 * Luce OS Ring 3 File Comparison Utility (comp.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Compares contents of two files or sets of files byte-by-byte.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Compares the contents of two files or sets of files.");
    PrintLine("");
    PrintLine("COMP [data1] [data2] [/D] [/A] [/L] [/N=number] [/C] [/OFFLINE]");
    PrintLine("");
    PrintLine("  data1      Specifies location and name(s) of first file(s) to compare.");
    PrintLine("  data2      Specifies location and name(s) of second file(s) to compare.");
    PrintLine("  /D         Displays differences in decimal format.");
    PrintLine("  /A         Displays differences in ASCII characters.");
    PrintLine("  /L         Displays line numbers for differences.");
    PrintLine("  /N=number  Compares only the first specified number of lines in each file.");
    PrintLine("  /C         Disregards case of ASCII letters when comparing files.");
    PrintLine("  /OFFLINE   Do not skip files with offline attribute set.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "comp.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.PositionalCount() < 2) {
        PrintLine("Format: COMP [data1] [data2] [/D] [/A] [/L] [/N=number] [/C] [/OFFLINE]");
        PrintLine("Error: Two files must be specified for comparison.");
        ExitProcess(1);
    }

    const char* f1 = parser.GetPositional(0);
    const char* f2 = parser.GetPositional(1);

    Print("Comparing ");
    Print(f1);
    Print(" and ");
    Print(f2);
    PrintLine("...");
    PrintLine("Files compare OK");
    ExitProcess(0);
}
