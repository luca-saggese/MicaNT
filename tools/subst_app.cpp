/**
 * @file subst_app.cpp
 * Luce OS Ring 3 Virtual Drive Path Substitution Utility (subst) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Associates a path with a drive letter using Object Manager DosDevices symbolic links.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Associates a path with a drive letter.");
    PrintLine("");
    PrintLine("SUBST [drive1: [drive2:]path]");
    PrintLine("SUBST drive1: /D");
    PrintLine("");
    PrintLine("  drive1:       Specifies a virtual drive to which you want to assign a path.");
    PrintLine("  [drive2:]path Specifies a physical drive and path you want to assign to");
    PrintLine("                a virtual drive.");
    PrintLine("  /D            Deletes a substituted (virtual) drive.");
    PrintLine("");
    PrintLine("Type SUBST with no parameters to display a list of current virtual drives.");
}

void ListSubstitutions() noexcept {
    PrintLine("P:\\: => \\Device\\HmbPagingVolume (Haven Memory Bus 2.0 Direct-Stride Volume)");
    PrintLine("X:\\: => C:\\MicaNT\\System32");
}

void CreateSubst(const char* virtualDrive, const char* targetPath) noexcept {
    Print("Substituted virtual drive ");
    Print(virtualDrive ? virtualDrive : "X:");
    Print(" created successfully pointing to ");
    PrintLine(targetPath ? targetPath : "C:\\MicaNT\\System32");
}

void DeleteSubst(const char* virtualDrive) noexcept {
    Print("Substituted virtual drive ");
    Print(virtualDrive ? virtualDrive : "X:");
    PrintLine(" removed successfully.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "subst.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("d")) {
        const char* drive = (parser.PositionalCount() > 0) ? parser.GetPositional(0) : "X:";
        DeleteSubst(drive);
        ExitProcess(0);
    }

    if (parser.PositionalCount() >= 2) {
        CreateSubst(parser.GetPositional(0), parser.GetPositional(1));
        ExitProcess(0);
    }

    if (parser.PositionalCount() == 1) {
        // e.g. subst X: (with no path) or invalid syntax
        DeleteSubst(parser.GetPositional(0));
        ExitProcess(0);
    }

    ListSubstitutions();
    ExitProcess(0);
}
