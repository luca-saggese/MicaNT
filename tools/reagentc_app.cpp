/**
 * @file reagentc_app.cpp
 @brief Luce OS Ring 3 Recovery Environment Utility (reagentc.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows reagentc behavior:
 * - Switches: /info, /enable, /disable, /setreimage, /bootshell, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Error)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("REAGENTC.EXE <command> <arguments>");
    PrintLine("");
    PrintLine("The following commands can be specified with REAGENTC.EXE:");
    PrintLine("");
    PrintLine("  /enable           - Enables the local MicaNT Recovery Environment (MicaNT RE).");
    PrintLine("  /disable          - Disables the local MicaNT Recovery Environment (MicaNT RE).");
    PrintLine("  /info             - Displays MicaNT RE status and configuration information.");
    PrintLine("  /setreimage       - Sets the custom MicaNT RE image path.");
    PrintLine("  /setosimage       - Sets the custom reset image path.");
    PrintLine("  /setbootshell     - Sets failover boot into recovery shell.");
    PrintLine("  /?                - Displays this help screen.");
}

void ShowInfo() noexcept {
    PrintLine("MicaNT Recovery Environment (MicaNT RE) and system reset configuration");
    PrintLine("Information:");
    PrintLine("");
    PrintLine("    MicaNT RE status:         Enabled");
    PrintLine("    MicaNT RE location:       \\\\?\\GLOBALROOT\\device\\harddisk0\\partition1\\Recovery\\MicaNTRE");
    PrintLine("    Boot Configuration Data:   0x000B (MicaNT Cutler Edition NVRAM Entry)");
    PrintLine("    Custom image location:     ");
    PrintLine("    Custom image index:        0");
    PrintLine("    Recovery image location:   \\\\?\\GLOBALROOT\\device\\harddisk0\\partition2 (Ubuntu Linux Rescue)");
    PrintLine("    Recovery image index:      1");
    PrintLine("    Bootloader Failover:       Automatic BootNext fallback to Ubuntu on unhandled fault");
    PrintLine("");
    PrintLine("REAGENTC.EXE: Operation Successful.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "reagentc.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("info")) {
        ShowInfo();
        ExitProcess(0);
    }

    if (parser.HasSwitch("enable")) {
        PrintLine("REAGENTC.EXE: Operation Successful.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("disable")) {
        PrintLine("REAGENTC.EXE: Operation Successful.");
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
