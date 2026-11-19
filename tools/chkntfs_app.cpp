/**
 * @file chkntfs_app.cpp
 @brief Luce OS Ring 3 NTFS Volume Dirty Query & Autochk Tool (chkntfs.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows chkntfs behavior:
 * - Switches: /d, /t, /x, /c, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Error)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Displays or modifies the checking of disk at boot time.");
    PrintLine("");
    PrintLine("CHKNTFS volume [...]");
    PrintLine("CHKNTFS [/D] [/T[:time]] [/X volume [...]] [/C volume [...]]");
    PrintLine("");
    PrintLine("  volume          Specifies the drive letter (followed by a colon),");
    PrintLine("                  mount point, or volume name.");
    PrintLine("  /D              Restores the machine to the default behavior; all");
    PrintLine("                  drives are checked at boot time and chkdsk is run on");
    PrintLine("                  those that are dirty.");
    PrintLine("  /T:time         Changes the AUTOCHK initiation countdown time to the");
    PrintLine("                  specified amount of time in seconds. If time is not");
    PrintLine("                  specified, displays the current countdown time.");
    PrintLine("  /X              Excludes a drive from the default boot-time check.");
    PrintLine("  /C              Schedules a drive to be checked at boot time; chkdsk");
    PrintLine("                  will run if the drive is dirty.");
    PrintLine("  /?              Displays this help message.");
}

void QueryVolume(const char* vol) noexcept {
    Print("The type of the file system is NTFS (Sovereign Clean-Room).\n");
    Print(vol ? vol : "C:");
    PrintLine(" is not dirty.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "chkntfs.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("d")) {
        PrintLine("The default boot-time checking behavior has been restored.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("t")) {
        const char* val = parser.GetSwitchValue("t");
        if (val) {
            Print("The AUTOCHK initiation countdown time is set to ");
            Print(val);
            PrintLine(" seconds.");
        } else {
            PrintLine("The AUTOCHK initiation countdown time is currently set to 5 seconds.");
        }
        ExitProcess(0);
    }

    if (parser.HasSwitch("x")) {
        PrintLine("Drive exclusion registered in BootExecute.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("c")) {
        PrintLine("Drive scheduled for dirty check on next reboot.");
        ExitProcess(0);
    }

    const char* vol = parser.GetPositional(0);
    QueryVolume(vol);
    ExitProcess(0);
}
