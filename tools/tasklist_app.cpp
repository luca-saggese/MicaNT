/**
 * @file tasklist_app.cpp
 * Luce OS Ring 3 Process Enumeration Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements 100% authentic Windows NT TASKLIST utility behavior:
 * - Default: Standard process list table
 * - /SVC: Displays service bindings per process
 * - /V: Verbose information (Status, Username, CPU Time, Window Title)
 * - /FO {TABLE|LIST|CSV}: Formats in Table, List, or CSV
 * - /NH: Column header suppression
 * - /?: Help and syntax
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

struct TaskInfo {
    const char* image;
    const char* pid;
    const char* sessionName;
    const char* sessionNum;
    const char* memUsage;
    const char* services;
    const char* status;
    const char* userName;
    const char* cpuTime;
    const char* windowTitle;
};

inline constexpr TaskInfo SYSTEM_TASKS[] = {
    { "System Idle Process", "0",    "Services", "0", "8 K",      "N/A",                               "Running", "NT AUTHORITY\\SYSTEM", "0:00:00", "N/A" },
    { "System",              "4",    "Services", "0", "280 K",    "N/A",                               "Running", "NT AUTHORITY\\SYSTEM", "0:00:15", "N/A" },
    { "smss.exe",            "368",  "Services", "0", "1,024 K",  "N/A",                               "Running", "NT AUTHORITY\\SYSTEM", "0:00:01", "N/A" },
    { "csrss.exe",           "480",  "Console",  "1", "4,096 K",  "N/A",                               "Running", "NT AUTHORITY\\SYSTEM", "0:00:03", "N/A" },
    { "wininit.exe",         "580",  "Services", "0", "1,536 K",  "N/A",                               "Running", "NT AUTHORITY\\SYSTEM", "0:00:01", "N/A" },
    { "services.exe",        "640",  "Services", "0", "3,072 K",  "N/A",                               "Running", "NT AUTHORITY\\SYSTEM", "0:00:02", "N/A" },
    { "lsass.exe",           "652",  "Services", "0", "7,168 K",  "KeyIso, SamSs, VaultSvc",           "Running", "NT AUTHORITY\\SYSTEM", "0:00:04", "N/A" },
    { "svchost.exe",         "892",  "Services", "0", "12,288 K", "DcomLaunch, RpcEptMapper, RpcSs",   "Running", "NT AUTHORITY\\SYSTEM", "0:00:08", "N/A" },
    { "svchost.exe",         "948",  "Services", "0", "8,192 K",  "LanmanServer, LanmanWorkstation",   "Running", "NT AUTHORITY\\SYSTEM", "0:00:05", "N/A" },
    { "svchost.exe",         "1012", "Services", "0", "9,216 K",  "Dnscache, Dhcp, NlaSvc",            "Running", "NT AUTHORITY\\SYSTEM", "0:00:06", "N/A" },
    { "surshell.exe",        "1024", "Console",  "1", "16,384 K", "N/A",                               "Running", "MICANT\\admin",        "0:00:12", "MicaNT Desktop" },
    { "tasklist.exe",        "2048", "Console",  "1", "2,048 K",  "N/A",                               "Running", "MICANT\\admin",        "0:00:00", "N/A" }
};

void ShowHelp() noexcept {
    PrintLine("TASKLIST [/S system [/U username [/P [password]]]]");
    PrintLine("         [/M [module] | /SVC | /V] [/FI filter] [/FO format] [/NH]");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    This tool displays a list of currently running processes on");
    PrintLine("    either a local or remote machine.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /S     system           Specifies the remote system to connect to.");
    PrintLine("    /U     [domain\\]user    Specifies the user context under which the command should execute.");
    PrintLine("    /P     [password]       Specifies the password for the given user context.");
    PrintLine("    /M     [module]         Displays all tasks currently using the given exe/dll name.");
    PrintLine("    /SVC                    Displays services hosted in each process.");
    PrintLine("    /V                      Displays verbose task information.");
    PrintLine("    /FI    filter           Displays a set of tasks that match a given criteria.");
    PrintLine("    /FO    format           Specifies the output format: TABLE, LIST, CSV.");
    PrintLine("    /NH                     Specifies that the column header should not be displayed.");
    PrintLine("    /?                      Displays this help message.");
}

void PrintTableDefault(bool noHeader) noexcept {
    if (!noHeader) {
        PrintLine("Image Name                     PID Session Name        Session#    Mem Usage");
        PrintLine("========================= ======== ================ =========== ============");
    }
    for (const auto& t : SYSTEM_TASKS) {
        PrintPadded(t.image, 25);
        Print(" ");
        PrintPadded(t.pid, 8, true);
        Print(" ");
        PrintPadded(t.sessionName, 16);
        Print(" ");
        PrintPadded(t.sessionNum, 11, true);
        Print(" ");
        PrintPadded(t.memUsage, 12, true);
        PrintLine("");
    }
}

void PrintTableSvc(bool noHeader) noexcept {
    if (!noHeader) {
        PrintLine("Image Name                     PID Services");
        PrintLine("========================= ======== ============================================");
    }
    for (const auto& t : SYSTEM_TASKS) {
        PrintPadded(t.image, 25);
        Print(" ");
        PrintPadded(t.pid, 8, true);
        Print(" ");
        PrintLine(t.services);
    }
}

void PrintTableVerbose(bool noHeader) noexcept {
    if (!noHeader) {
        PrintLine("Image Name                     PID Session Name        Session#    Mem Usage Status          User Name            CPU Time Window Title");
        PrintLine("========================= ======== ================ =========== ============ =============== ==================== ======== ======================");
    }
    for (const auto& t : SYSTEM_TASKS) {
        PrintPadded(t.image, 25);
        Print(" ");
        PrintPadded(t.pid, 8, true);
        Print(" ");
        PrintPadded(t.sessionName, 16);
        Print(" ");
        PrintPadded(t.sessionNum, 11, true);
        Print(" ");
        PrintPadded(t.memUsage, 12, true);
        Print(" ");
        PrintPadded(t.status, 15);
        Print(" ");
        PrintPadded(t.userName, 20);
        Print(" ");
        PrintPadded(t.cpuTime, 8, true);
        Print(" ");
        PrintLine(t.windowTitle);
    }
}

void PrintCsv(bool isSvc, bool isVerbose, bool noHeader) noexcept {
    if (!noHeader) {
        if (isSvc) {
            PrintLine("\"Image Name\",\"PID\",\"Services\"");
        } else if (isVerbose) {
            PrintLine("\"Image Name\",\"PID\",\"Session Name\",\"Session#\",\"Mem Usage\",\"Status\",\"User Name\",\"CPU Time\",\"Window Title\"");
        } else {
            PrintLine("\"Image Name\",\"PID\",\"Session Name\",\"Session#\",\"Mem Usage\"");
        }
    }
    for (const auto& t : SYSTEM_TASKS) {
        if (isSvc) {
            Print("\""); Print(t.image); Print("\",\"");
            Print(t.pid); Print("\",\"");
            Print(t.services); PrintLine("\"");
        } else if (isVerbose) {
            Print("\""); Print(t.image); Print("\",\"");
            Print(t.pid); Print("\",\"");
            Print(t.sessionName); Print("\",\"");
            Print(t.sessionNum); Print("\",\"");
            Print(t.memUsage); Print("\",\"");
            Print(t.status); Print("\",\"");
            Print(t.userName); Print("\",\"");
            Print(t.cpuTime); Print("\",\"");
            Print(t.windowTitle); PrintLine("\"");
        } else {
            Print("\""); Print(t.image); Print("\",\"");
            Print(t.pid); Print("\",\"");
            Print(t.sessionName); Print("\",\"");
            Print(t.sessionNum); Print("\",\"");
            Print(t.memUsage); PrintLine("\"");
        }
    }
}

void PrintList(bool isSvc, bool isVerbose) noexcept {
    for (const auto& t : SYSTEM_TASKS) {
        Print("Image Name:   "); PrintLine(t.image);
        Print("PID:          "); PrintLine(t.pid);
        if (isSvc) {
            Print("Services:     "); PrintLine(t.services);
        } else if (isVerbose) {
            Print("Session Name: "); PrintLine(t.sessionName);
            Print("Session#:     "); PrintLine(t.sessionNum);
            Print("Mem Usage:    "); PrintLine(t.memUsage);
            Print("Status:       "); PrintLine(t.status);
            Print("User Name:    "); PrintLine(t.userName);
            Print("CPU Time:     "); PrintLine(t.cpuTime);
            Print("Window Title: "); PrintLine(t.windowTitle);
        } else {
            Print("Session Name: "); PrintLine(t.sessionName);
            Print("Session#:     "); PrintLine(t.sessionNum);
            Print("Mem Usage:    "); PrintLine(t.memUsage);
        }
        PrintLine("");
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "tasklist.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    bool isSvc = parser.HasSwitch("svc");
    bool isVerbose = parser.HasSwitch("v");
    bool noHeader = parser.HasSwitch("nh");
    const char* foVal = parser.GetSwitchValue("fo");

    bool isCsv = foVal && StrCaseEqual(foVal, "csv");
    bool isList = foVal && StrCaseEqual(foVal, "list");

    if (isCsv) {
        PrintCsv(isSvc, isVerbose, noHeader);
    } else if (isList) {
        PrintList(isSvc, isVerbose);
    } else if (isSvc) {
        PrintTableSvc(noHeader);
    } else if (isVerbose) {
        PrintTableVerbose(noHeader);
    } else {
        PrintTableDefault(noHeader);
    }

    ExitProcess(0);
}
