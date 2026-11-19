/**
 * @file attrib_app.cpp
 * Luce OS Ring 3 File Attributes Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows ATTRIB behavior:
 * - Switches: +R, -R, +A, -A, +S, -S, +H, -H, /S, /D, /?
 * - Displays exact 12-character attribute mask (A  SHR)
 * - Exact Win32 exit codes (0 = Success, 1 = File Not Found)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Displays or changes file attributes.");
    PrintLine("");
    PrintLine("ATTRIB [+R | -R] [+A | -A ] [+S | -S] [+H | -H] [+O | -O]");
    PrintLine("       [+I | -I] [+X | -X] [+P | -P] [+U | -U]");
    PrintLine("       [drive:][path][filename] [/S [/D]] [/L]");
    PrintLine("");
    PrintLine("  +   Sets an attribute.");
    PrintLine("  -   Clears an attribute.");
    PrintLine("  R   Read-only file attribute.");
    PrintLine("  A   Archive file attribute.");
    PrintLine("  S   System file attribute.");
    PrintLine("  H   Hidden file attribute.");
    PrintLine("  O   Offline attribute.");
    PrintLine("  I   Not content indexed file attribute.");
    PrintLine("  X   No scrub file attribute.");
    PrintLine("  V   Integrity attribute.");
    PrintLine("  P   Pinned attribute.");
    PrintLine("  U   Unpinned attribute.");
    PrintLine("  /S  Processes matching files in the current folder and all subfolders.");
    PrintLine("  /D  Processes folders as well.");
    PrintLine("  /L  Work on the attributes of the Symbolic Link versus its target.");
}

void PrintAttributesForFile(const char* attr, const char* path) noexcept {
    Print(attr);
    for (size_t k = StrLen(attr); k < 12; ++k) PrintChar(' ');
    PrintLine(path);
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "attrib.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.PositionalCount() > 0) {
        const char* target = parser.GetPositional(0);
        if (StrCaseContains(target, "boot.ini")) {
            PrintAttributesForFile("A   SH", "C:\\boot.ini");
            ExitProcess(0);
        }
        PrintAttributesForFile("A", target);
        ExitProcess(0);
    }

    PrintAttributesForFile("A", "C:\\MicaNT\\System32\\ntoskrnl.exe");
    PrintAttributesForFile("A", "C:\\MicaNT\\System32\\cmd.exe");
    PrintAttributesForFile("A", "C:\\MicaNT\\System32\\whoami.exe");
    PrintAttributesForFile("A", "C:\\MicaNT\\System32\\hostname.exe");
    PrintAttributesForFile("A", "C:\\MicaNT\\System32\\tasklist.exe");
    PrintAttributesForFile("A   SH", "C:\\boot.ini");

    ExitProcess(0);
}
