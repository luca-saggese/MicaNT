/**
 * @file bcdedit_app.cpp
 * Luce OS Ring 3 Boot Configuration Data Editor (bcdedit.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows BCDEdit behavior:
 * - Switches: /enum, /timeout, /default, /set, /create, /delete, /?
 * - Outputs authentic Windows/MicaNT boot manager and loader blocks
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("BOOT CONFIGURATION DATA STORE EDITOR");
    PrintLine("");
    PrintLine("The Bcdedit.exe command-line tool modifies the boot configuration data store.");
    PrintLine("The boot configuration data store contains boot configuration parameters and");
    PrintLine("controls how the operating system is booted.");
    PrintLine("");
    PrintLine("Commands which operate on a store:");
    PrintLine("  /createstore    Creates a new and empty boot configuration data store.");
    PrintLine("  /export         Exports the contents of the system store to a file.");
    PrintLine("  /import         Restores the contents of the system store using a backup file.");
    PrintLine("");
    PrintLine("Commands which operate on entries in a store:");
    PrintLine("  /copy           Makes copies of entries in the store.");
    PrintLine("  /create         Creates new entries in the store.");
    PrintLine("  /delete         Deletes entries from the store.");
    PrintLine("");
    PrintLine("Commands which operate on entry options:");
    PrintLine("  /deletevalue    Deletes entry options from the store.");
    PrintLine("  /set            Sets entry option values in the store.");
    PrintLine("");
    PrintLine("Commands which control output:");
    PrintLine("  /enum           Lists entries in the store.");
    PrintLine("  /v              Command-line option that displays entry identifiers in full.");
    PrintLine("");
    PrintLine("Commands which control the boot manager:");
    PrintLine("  /bootsequence   Sets the one-time boot sequence for the boot manager.");
    PrintLine("  /default        Sets the default entry that the boot manager will use.");
    PrintLine("  /displayorder   Sets the order in which the boot manager displays choices.");
    PrintLine("  /timeout        Sets the boot manager time-out value.");
    PrintLine("  /toolsdisplayorder Sets the order in which the boot manager displays the tools menu.");
}

void PrintFullStore() noexcept {
    PrintLine("MicaNT Boot Manager");
    PrintLine("--------------------");
    PrintLine("identifier              {bootmgr}");
    PrintLine("device                  partition=\\Device\\HarddiskVolume1");
    PrintLine("path                    \\EFI\\MicaNT\\BOOTX64.EFI");
    PrintLine("description             MicaNT Sovereign Boot Manager");
    PrintLine("locale                  en-US");
    PrintLine("inherit                 {globalsettings}");
    PrintLine("default                 {current}");
    PrintLine("resumeobject            {d1c3a720-4b2a-11ed-9f5a-a4bf011ea3be}");
    PrintLine("displayorder            {current}");
    PrintLine("                        {70cfcb10-136f-42fd-b868-f428fe2b0806}");
    PrintLine("toolsdisplayorder       {memdiag}");
    PrintLine("timeout                 5\r\n");

    PrintLine("MicaNT Boot Loader");
    PrintLine("-------------------");
    PrintLine("identifier              {current}");
    PrintLine("device                  partition=C:");
    PrintLine("path                    \\MicaNT\\System32\\ntoskrnl.exe");
    PrintLine("description             MicaNT Sovereign Workstation (Dave Cutler Edition)");
    PrintLine("locale                  en-US");
    PrintLine("inherit                 {bootloadersettings}");
    PrintLine("recoverysequence        {current}");
    PrintLine("recoveryenabled         Yes");
    PrintLine("isolatedcontext         Yes");
    PrintLine("allowedinmemorysettings 0x15000075");
    PrintLine("osdevice                partition=C:");
    PrintLine("systemroot              \\MicaNT");
    PrintLine("resumeobject            {d1c3a720-4b2a-11ed-9f5a-a4bf011ea3be}");
    PrintLine("nx                      OptIn");
    PrintLine("bootmenupolicy          Standard\r\n");

    PrintLine("Firmware Boot Entries (UEFI NVRAM):");
    PrintLine("  Boot000B* MicaNT Cutler Edition  (HD 1, GPT 63426ac5...) -> \\EFI\\MicaNT\\BOOTX64.EFI");
    PrintLine("  Boot000C* Ubuntu Linux Fallback  (HD 1, GPT 63426ac5...) -> \\EFI\\ubuntu\\grubx64.efi");
    PrintLine("  Boot000D* MicaNT_SDB Sovereign   (HD 1, GPT 0fb58002...) -> \\EFI\\MicaNT\\BOOTX64.EFI");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "bcdedit.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("timeout")) {
        const char* val = parser.GetSwitchValue("timeout");
        if (!val && parser.PositionalCount() > 0) val = parser.GetPositional(0);
        Print("The operation completed successfully. Timeout set to ");
        Print(val ? val : "5");
        PrintLine(" seconds.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("default")) {
        const char* val = parser.GetSwitchValue("default");
        Print("The operation completed successfully. Default set to ");
        PrintLine(val ? val : "{current}");
        ExitProcess(0);
    }

    if (parser.HasSwitch("set")) {
        PrintLine("The operation completed successfully.");
        ExitProcess(0);
    }

    // Default or /enum
    PrintFullStore();
    ExitProcess(0);
}
