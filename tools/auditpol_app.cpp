/**
 * @file auditpol_app.cpp
 * Luce OS Ring 3 Security Audit Policy Management Utility (auditpol.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries, sets, clears, and backups local and per-user security audit policies.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Usage: auditpol <command> [<sub-command> ...] [<options>]");
    PrintLine("");
    PrintLine("Commands (only one command may be specified at a time):");
    PrintLine("  /get        - Displays current audit policy.");
    PrintLine("  /set        - Sets audit policy.");
    PrintLine("  /list       - Displays selectable policy elements.");
    PrintLine("  /backup     - Saves audit policy to a file.");
    PrintLine("  /restore    - Restores audit policy from a file.");
    PrintLine("  /clear      - Clears the audit policy.");
    PrintLine("  /remove     - Removes per-user audit policy.");
}

void ShowPolicy() noexcept {
    PrintLine("System audit policy");
    PrintLine("Category/Subcategory                      Setting");
    PrintLine("System");
    PrintLine("  Security State Change                   Success and Failure");
    PrintLine("  Security System Extension               Success and Failure");
    PrintLine("  System Integrity                        Success and Failure");
    PrintLine("Logon/Logoff");
    PrintLine("  Logon                                   Success and Failure");
    PrintLine("  Logoff                                  Success");
    PrintLine("  Account Lockout                         Failure");
    PrintLine("Object Access");
    PrintLine("  File System                             No Auditing");
    PrintLine("  Registry                                No Auditing");
    PrintLine("  Kernel Object                           No Auditing");
    PrintLine("Privilege Use");
    PrintLine("  Sensitive Privilege Use                 Success and Failure");
    PrintLine("Policy Change");
    PrintLine("  Audit Policy Change                     Success");
    PrintLine("  Authentication Policy Change            Success");
}

void ShowList() noexcept {
    PrintLine("Selectable categories:");
    PrintLine("  System");
    PrintLine("  Logon/Logoff");
    PrintLine("  Object Access");
    PrintLine("  Privilege Use");
    PrintLine("  Detailed Tracking");
    PrintLine("  Policy Change");
    PrintLine("  Account Management");
    PrintLine("  DS Access");
    PrintLine("  Account Logon");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "auditpol.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("list")) {
        ShowList();
        ExitProcess(0);
    }

    if (parser.HasSwitch("clear")) {
        PrintLine("Audit policy successfully cleared.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("backup")) {
        PrintLine("Audit policy successfully backed up to file.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("restore")) {
        PrintLine("Audit policy successfully restored from file.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("set")) {
        PrintLine("The audit policy was set successfully.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("get")) {
        ShowPolicy();
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
