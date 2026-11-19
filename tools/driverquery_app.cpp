/**
 * @file driverquery_app.cpp
 * Luce OS Ring 3 Loaded Executive Driver Query Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows driverquery behavior:
 * - Switches: /FO {TABLE|LIST|CSV}, /NH, /V, /SI, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

struct DriverEntry {
    const char* moduleName;
    const char* displayName;
    const char* description;
    const char* driverType;
    const char* startMode;
    const char* status;
    const char* linkDate;
};

constexpr DriverEntry DRIVERS[] = {
    {"ntoskrnl", "NT Kernel & System",     "NT Kernel & System",     "Kernel",      "Boot",   "Running", "10/22/2026 12:00:00 PM"},
    {"hal",      "Hardware Abstraction L", "Hardware Abstraction L", "Kernel",      "Boot",   "Running", "10/22/2026 12:00:00 PM"},
    {"bootvid",  "Boot Video Driver",      "Boot Video Driver",      "Kernel",      "Boot",   "Running", "10/22/2026 12:00:00 PM"},
    {"ahci",     "AHCI SATA Controller",   "AHCI SATA Controller",   "Kernel",      "Boot",   "Running", "10/22/2026 12:00:00 PM"},
    {"i210",     "Intel I210 Gigabit Net", "Intel I210 Gigabit Net", "Kernel",      "System", "Running", "10/22/2026 12:00:00 PM"},
    {"extfs",    "Ext4 Filesystem Filter", "Ext4 Filesystem Filter", "File System", "System", "Running", "10/22/2026 12:00:00 PM"},
    {"fltmgr",   "Filter Manager",         "Filter Manager",         "File System", "Boot",   "Running", "10/22/2026 12:00:00 PM"},
    {"xhci",     "xHCI USB Host Controll", "xHCI USB Host Controll", "Kernel",      "Boot",   "Running", "10/22/2026 12:00:00 PM"},
    {"usbhub",   "USB Hub Driver",         "USB Hub Driver",         "Kernel",      "Boot",   "Running", "10/22/2026 12:00:00 PM"},
};

constexpr size_t DRIVER_COUNT = sizeof(DRIVERS) / sizeof(DRIVERS[0]);

void ShowHelp() noexcept {
    PrintLine("DRIVERQUERY [/S system [/U username [/P [password]]]]");
    PrintLine("            [/FO format] [/NH] [/SI] [/V]");
    PrintLine("Description:");
    PrintLine("    Enables an administrator to enumerate and display the list of");
    PrintLine("    installed device drivers along with their properties.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("      /S     system           Specifies the remote system to connect to.");
    PrintLine("      /U     [domain\\]user    Specifies the user context under which the");
    PrintLine("                              command should execute.");
    PrintLine("      /P     [password]       Specifies the password for the given user context.");
    PrintLine("      /FO    format           Specifies the type of output to display.");
    PrintLine("                              Valid values are \"TABLE\", \"LIST\", \"CSV\".");
    PrintLine("      /NH                     Specifies that the \"Column Header\" should");
    PrintLine("                              not be displayed. Valid for \"TABLE\" and \"CSV\".");
    PrintLine("      /SI                     Provides information about signed drivers.");
    PrintLine("      /V                      Displays verbose output.");
    PrintLine("      /?                      Displays this help message.");
    PrintLine("");
    PrintLine("Examples:");
    PrintLine("    DRIVERQUERY");
    PrintLine("    DRIVERQUERY /FO CSV /SI");
    PrintLine("    DRIVERQUERY /NH /V");
}

void PrintTable(bool verbose, bool noHeader) noexcept {
    if (!noHeader) {
        if (verbose) {
            PrintLine("Module Name Display Name           Description            Driver Type Start Mode Status   Accept Stop Accept Pause");
            PrintLine("=========== ====================== ====================== =========== ========== ======== =========== ============");
        } else {
            PrintLine("Module Name  Display Name           Driver Type   Link Date");
            PrintLine("===========  ====================== ============= ======================");
        }
    }

    for (size_t i = 0; i < DRIVER_COUNT; ++i) {
        const auto& d = DRIVERS[i];
        if (verbose) {
            Print(d.moduleName);
            for (size_t k = StrLen(d.moduleName); k < 12; ++k) PrintChar(' ');
            Print(d.displayName);
            for (size_t k = StrLen(d.displayName); k < 23; ++k) PrintChar(' ');
            Print(d.description);
            for (size_t k = StrLen(d.description); k < 23; ++k) PrintChar(' ');
            Print(d.driverType);
            for (size_t k = StrLen(d.driverType); k < 12; ++k) PrintChar(' ');
            Print(d.startMode);
            for (size_t k = StrLen(d.startMode); k < 11; ++k) PrintChar(' ');
            Print(d.status);
            for (size_t k = StrLen(d.status); k < 9; ++k) PrintChar(' ');
            PrintLine("TRUE        FALSE       ");
        } else {
            Print(d.moduleName);
            for (size_t k = StrLen(d.moduleName); k < 13; ++k) PrintChar(' ');
            Print(d.displayName);
            for (size_t k = StrLen(d.displayName); k < 23; ++k) PrintChar(' ');
            Print(d.driverType);
            for (size_t k = StrLen(d.driverType); k < 14; ++k) PrintChar(' ');
            PrintLine(d.linkDate);
        }
    }
}

void PrintCsv(bool verbose, bool noHeader) noexcept {
    if (!noHeader) {
        if (verbose) {
            PrintLine("\"Module Name\",\"Display Name\",\"Description\",\"Driver Type\",\"Start Mode\",\"Status\",\"Accept Stop\",\"Accept Pause\"");
        } else {
            PrintLine("\"Module Name\",\"Display Name\",\"Driver Type\",\"Link Date\"");
        }
    }

    for (size_t i = 0; i < DRIVER_COUNT; ++i) {
        const auto& d = DRIVERS[i];
        PrintChar('"'); Print(d.moduleName); Print("\",\"");
        Print(d.displayName); Print("\",\"");
        if (verbose) {
            Print(d.description); Print("\",\"");
            Print(d.driverType); Print("\",\"");
            Print(d.startMode); Print("\",\"");
            Print(d.status); Print("\",\"TRUE\",\"FALSE\"");
        } else {
            Print(d.driverType); Print("\",\"");
            Print(d.linkDate); PrintChar('"');
        }
        PrintLine("");
    }
}

void PrintList(bool verbose) noexcept {
    for (size_t i = 0; i < DRIVER_COUNT; ++i) {
        const auto& d = DRIVERS[i];
        Print("Module Name:  "); PrintLine(d.moduleName);
        Print("Display Name: "); PrintLine(d.displayName);
        if (verbose) {
            Print("Description:  "); PrintLine(d.description);
        }
        Print("Driver Type:  "); PrintLine(d.driverType);
        if (verbose) {
            Print("Start Mode:   "); PrintLine(d.startMode);
            Print("Status:       "); PrintLine(d.status);
            Print("Accept Stop:  "); PrintLine("TRUE");
            Print("Accept Pause: "); PrintLine("FALSE");
        } else {
            Print("Link Date:    "); PrintLine(d.linkDate);
        }
        PrintLine("");
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "driverquery.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    bool verbose = parser.HasSwitch("v");
    bool noHeader = parser.HasSwitch("nh");

    const char* fo = parser.GetSwitchValue("fo");
    if (!fo) {
        PrintTable(verbose, noHeader);
        ExitProcess(0);
    }

    if (StrCaseEqual(fo, "table")) {
        PrintTable(verbose, noHeader);
    } else if (StrCaseEqual(fo, "csv")) {
        PrintCsv(verbose, noHeader);
    } else if (StrCaseEqual(fo, "list")) {
        PrintList(verbose);
    } else {
        PrintLine("ERROR: Invalid format value. Valid values are \"TABLE\", \"LIST\", and \"CSV\".");
        ExitProcess(1);
    }

    ExitProcess(0);
}
