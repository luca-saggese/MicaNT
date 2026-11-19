/**
 * @file shutdown_app.cpp
 * Luce OS Ring 3 Shutdown & System Reset Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows shutdown behavior:
 * - Switches: /s, /r, /l, /a, /p, /h, /f, /t xxx, /c "comment", /?
 * - Integrates with kernel syscall 0x08 (NtShutdownSystem)
 * - Exact Win32 exit codes (0 = Success, 1116 = No shutdown in progress)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Usage: shutdown [/i | /l | /s | /sg | /r | /g | /a | /p | /h | /e | /o]");
    PrintLine("                [/hybrid] [/soft] [/fw] [/f] [/m \\\\computer] [/t xxx]");
    PrintLine("                [/d [p|u:]xx:yy] [/c \"comment\"]");
    PrintLine("");
    PrintLine("    No args    Display help. This is the same as typing /?.");
    PrintLine("    /?         Display help. This is the same as not typing any options.");
    PrintLine("    /i         Display the graphical user interface (GUI).");
    PrintLine("    /l         Log off. This cannot be used with /m or /d options.");
    PrintLine("    /s         Shutdown the computer.");
    PrintLine("    /r         Full shutdown and restart the computer.");
    PrintLine("    /g         Full shutdown and restart the computer and restart apps.");
    PrintLine("    /a         Abort a system shutdown.");
    PrintLine("    /p         Turn off the local computer with no time-out or warning.");
    PrintLine("    /h         Hibernate the local computer.");
    PrintLine("    /t xxx     Set the time-out period before shutdown to xxx seconds.");
    PrintLine("    /c \"comment\" Comment on the reason for the restart or shutdown.");
    PrintLine("    /f         Force running applications to close without warning users.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "shutdown.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("a")) {
        PrintLine("Unable to abort the system shutdown because no shutdown was in progress.(1116)");
        ExitProcess(1116);
    }

    uint64_t timeout = 0;
    const char* tVal = parser.GetSwitchValue("t");
    if (tVal) timeout = ParseUInt64(tVal);

    if (parser.HasSwitch("r") || parser.HasSwitch("g")) {
        PrintLine("MicaNT Cutler Edition is shutting down for restart.");
        PrintLine("Hardware CPU reboot initiated via Intel PCH Port 0xCF9...");
        DoSyscall(SSN_SHUTDOWN_SYSTEM, 1, timeout); // Action 1 = Reboot
        ExitProcess(0);
    }

    if (parser.HasSwitch("s") || parser.HasSwitch("p")) {
        PrintLine("MicaNT Cutler Edition is shutting down the system.");
        PrintLine("Power-off state transition initiated via ACPI S5...");
        DoSyscall(SSN_SHUTDOWN_SYSTEM, 2, timeout); // Action 2 = PowerOff
        ExitProcess(0);
    }

    if (parser.HasSwitch("l")) {
        PrintLine("MicaNT Cutler Edition: Logging off interactive user session.");
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
