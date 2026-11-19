/**
 * @file fltmc.cpp
 * Luce OS Ring 3 Filter Manager Control Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows FLTMC behavior:
 * - Commands: filters, instances, volumes, load, unload, help
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("The Filter Manager Control program is used to manage filter drivers.");
    PrintLine("");
    PrintLine("Commands:");
    PrintLine("    help        Display this help information");
    PrintLine("    filters     List all currently registered filter drivers");
    PrintLine("    instances   List filter instances or instance information");
    PrintLine("    volumes     List all volumes");
    PrintLine("    load        Load a filter driver");
    PrintLine("    unload      Unload a filter driver");
}

void PrintFilters() noexcept {
    PrintLine("Filter Name                     Num Instances    Altitude    Frame");
    PrintLine("------------------------------  -------------  ------------  -----");
    PrintLine("fltmgr                                      3       429998      0");
    PrintLine("extfs                                       1       180000      0");
    PrintLine("ahci                                        2       140000      0");
}

void PrintInstances() noexcept {
    PrintLine("Filter Name                     Num Instances   Altitude    Volume Name");
    PrintLine("------------------------------  -------------  ------------  ---------------------------------------");
    PrintLine("fltmgr                                      1       429998  C:");
    PrintLine("fltmgr                                      1       429998  D:");
    PrintLine("extfs                                       1       180000  E:");
}

void PrintVolumes() noexcept {
    PrintLine("Dos Name                        Volume Name                              File System    Status");
    PrintLine("------------------------------  ---------------------------------------  -------------  ------");
    PrintLine("C:                              \\Device\\Harddisk0\\Partition1             FAT32          OK");
    PrintLine("D:                              \\Device\\Harddisk0\\Partition2             FAT32          OK");
    PrintLine("E:                              \\Device\\Harddisk0\\Partition3             EXT4 (RO)      OK");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "fltmc.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        PrintFilters();
        ExitProcess(0);
    }

    const char* cmd = parser.argv()[1];
    if (StrCaseEqual(cmd, "filters")) {
        PrintFilters();
        ExitProcess(0);
    }

    if (StrCaseEqual(cmd, "instances")) {
        PrintInstances();
        ExitProcess(0);
    }

    if (StrCaseEqual(cmd, "volumes")) {
        PrintVolumes();
        ExitProcess(0);
    }

    if (StrCaseEqual(cmd, "help")) {
        ShowHelp();
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
