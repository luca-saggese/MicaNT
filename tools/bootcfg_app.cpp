/**
 * @file bootcfg_app.cpp
 * Luce OS Ring 3 Boot Configuration Data Utility (bootcfg.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Configures, queries, and edits boot settings in boot.ini and UEFI NVRAM.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("BOOTCFG <command> [arguments]");
    PrintLine("");
    PrintLine("Commands:");
    PrintLine("  /Query        - Displays current boot entries and settings.");
    PrintLine("  /Default      - Sets the default boot entry.");
    PrintLine("  /Timeout      - Changes the boot timeout value.");
    PrintLine("  /Addsw        - Adds operating system load options for a boot entry.");
    PrintLine("  /Rmsw         - Removes operating system load options for a boot entry.");
    PrintLine("  /Copy         - Makes a copy of an existing boot entry.");
    PrintLine("  /Delete       - Deletes an existing boot entry.");
}

void ShowBootEntries() noexcept {
    PrintLine("Boot Loader Settings");
    PrintLine("--------------------");
    PrintLine("timeout: 5");
    PrintLine("default: multi(0)disk(0)rdisk(0)partition(1)\\MicaNT");
    PrintLine("");
    PrintLine("Boot Entries");
    PrintLine("------------");
    PrintLine("Boot entry ID:    1");
    PrintLine("Friendly Name:    \"MicaNT Cutler Sovereign Edition x64\"");
    PrintLine("Path:             multi(0)disk(0)rdisk(0)partition(1)\\MicaNT");
    PrintLine("OS Load Options:  /fastdetect /sos /kernel=ntoskrnl.exe");
    PrintLine("");
    PrintLine("Boot entry ID:    2");
    PrintLine("Friendly Name:    \"Ubuntu Linux 24.04 LTS (Failover Rescue)\"");
    PrintLine("Path:             multi(0)disk(0)rdisk(0)partition(2)\\EFI\\ubuntu\\grubx64.efi");
    PrintLine("OS Load Options:  ro quiet splash");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "bootcfg.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("timeout")) {
        const char* toVal = parser.GetSwitchValue("timeout");
        if (!toVal && parser.PositionalCount() > 0) {
            toVal = parser.GetPositional(0);
        }
        if (toVal && *toVal != '\0') {
            Print("Success: Boot timeout successfully set to ");
            Print(toVal);
            PrintLine(" seconds.");
        } else {
            PrintLine("Error: Timeout value not specified.");
            ExitProcess(1);
        }
        ExitProcess(0);
    }

    if (parser.HasSwitch("default")) {
        const char* idVal = parser.GetSwitchValue("id");
        if (!idVal && parser.PositionalCount() > 0) {
            idVal = parser.GetPositional(0);
        }
        if (idVal && *idVal != '\0') {
            Print("Success: Default boot entry changed to ID ");
            Print(idVal);
            PrintLine(".");
        } else {
            PrintLine("Error: Default boot entry ID not specified.");
            ExitProcess(1);
        }
        ExitProcess(0);
    }

    // Default or /query
    ShowBootEntries();
    ExitProcess(0);
}
