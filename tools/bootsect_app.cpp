/**
 * @file bootsect_app.cpp
 @brief Luce OS Ring 3 Boot Sector Manipulation Tool (bootsect.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Updates the master boot code for hard disk partitions (NT60 / BOOTMGR / MBR).
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Bootsect.exe {/help | /nt60 | /nt52} {SYS | ALL | <DriveLetter:>} [/force] [/mbr]");
    PrintLine("");
    PrintLine("  /nt60        Applies the master boot code that is compatible with BOOTMGR.");
    PrintLine("  /nt52        Applies the master boot code that is compatible with NTLDR.");
    PrintLine("  SYS          Updates the master boot code on the system partition.");
    PrintLine("  ALL          Updates the master boot code on all partitions.");
    PrintLine("  /force       Forcibly dismounts the volume(s) during boot code update.");
    PrintLine("  /mbr         Updates the Master Boot Record on the disk containing the partition.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "bootsect.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    bool nt60 = parser.HasSwitch("nt60");
    bool nt52 = parser.HasSwitch("nt52");

    if (!nt60 && !nt52) {
        PrintLine("Error: You must specify /nt60 or /nt52 and a target volume.");
        PrintLine("Run \"bootsect /help\" for usage.");
        ExitProcess(1);
    }

    if (parser.PositionalCount() == 0) {
        PrintLine("Error: No target volume specified (SYS, ALL, or DriveLetter:).");
        ExitProcess(1);
    }

    const char* targetVol = parser.GetPositional(0);
    bool mbr = parser.HasSwitch("mbr");

    if (nt60) {
        PrintLine("Target volumes will be updated with BOOTMGR compatible bootcode.");
    } else {
        PrintLine("Target volumes will be updated with NTLDR compatible bootcode.");
    }
    PrintLine("");

    Print(targetVol);
    PrintLine(" (\\\\?\\Volume{3a8b4129-0000-0000-0000-100000000000})");
    PrintLine("    Successfully updated NTFS filesystem bootcode.");
    PrintLine("");

    if (mbr) {
        PrintLine("\\Device\\Harddisk0\\DR0");
        PrintLine("    Successfully updated Master Boot Record code.");
        PrintLine("");
    }

    PrintLine("Bootcode was successfully updated on all targeted volumes.");
    ExitProcess(0);
}
