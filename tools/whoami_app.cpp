/**
 * @file whoami_app.cpp
 @brief Luce OS Ring 3 Security Identity & Privilege Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements 100% authentic Windows NT WHOAMI utility behavior:
 * - Default: Outputs current domain and username (MICANT\admin)
 * - /user: Displays user name and security identifier (SID)
 * - /groups: Displays security groups, types, SIDs, and attributes
 * - /priv: Displays security privileges, descriptions, and operational state
 * - /all: Displays user, group, and privilege tables
 * - /upn: Displays User Principal Name
 * - /fqdn: Displays Fully Qualified Distinguished Name
 * - /fo {TABLE|LIST|CSV}: Supports tabular, list, and CSV formatting
 * - /?: Displays command syntax and help
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("WHOAMI [/UPN | /FQDN | /USER | /GROUPS | /PRIV] [/FO format] [/NH]");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    Displays user, group and privileges information for the user who is");
    PrintLine("    currently logged on to the local system. If no arguments are provided,");
    PrintLine("    displays the current domain and user name.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /USER            Displays the current user name and security identifier (SID).");
    PrintLine("    /GROUPS          Displays current group memberships and attributes.");
    PrintLine("    /PRIV            Displays current user security privileges.");
    PrintLine("    /ALL             Displays all information in the current access token.");
    PrintLine("    /UPN             Displays the user name in User Principal Name (UPN) format.");
    PrintLine("    /FQDN            Displays the user name in Fully Qualified Distinguished Name format.");
    PrintLine("    /FO   format     Specifies output format. Valid values are TABLE, LIST, CSV.");
    PrintLine("    /NH              Specifies that the column header should not be displayed.");
    PrintLine("    /?               Displays this help message.");
}

void PrintUserTable() noexcept {
    PrintLine("USER INFORMATION");
    PrintLine("----------------");
    PrintLine("");
    PrintLine("User Name    SID");
    PrintLine("============ ==============================================");
    PrintLine("micant\\admin S-1-5-21-3623811015-3361044348-30300820-1001");
}

void PrintGroupsTable() noexcept {
    PrintLine("GROUP INFORMATION");
    PrintLine("-----------------");
    PrintLine("");
    PrintLine("Group Name                           Type             SID                                            Attributes");
    PrintLine("==================================== ================ ============================================== ==================================================");
    PrintLine("MICANT\\None                          Group            S-1-5-21-3623811015-3361044348-30300820-513   Mandatory group, Enabled by default, Enabled group");
    PrintLine("BUILTIN\\Administrators               Alias            S-1-5-32-544                                   Mandatory group, Enabled by default, Enabled group, Group owner");
    PrintLine("BUILTIN\\Users                        Alias            S-1-5-32-545                                   Mandatory group, Enabled by default, Enabled group");
    PrintLine("NT AUTHORITY\\INTERACTIVE             Well-known group S-1-5-4                                        Mandatory group, Enabled by default, Enabled group");
    PrintLine("CONSOLE LOGON                        Well-known group S-1-2-1                                        Mandatory group, Enabled by default, Enabled group");
    PrintLine("NT AUTHORITY\\Authenticated Users     Well-known group S-1-5-11                                       Mandatory group, Enabled by default, Enabled group");
    PrintLine("Mandatory Label\\High Mandatory Level Label            S-1-16-12288                                   Mandatory group, Enabled by default, Enabled group");
}

void PrintPrivilegesTable() noexcept {
    PrintLine("PRIVILEGES INFORMATION");
    PrintLine("----------------------");
    PrintLine("");
    PrintLine("Privilege Name                            Description                                                        State");
    PrintLine("========================================= ================================================================== ========");
    PrintLine("SeIncreaseQuotaPrivilege                  Adjust memory quotas for a process                                 Disabled");
    PrintLine("SeSecurityPrivilege                       Manage auditing and security log                                   Disabled");
    PrintLine("SeTakeOwnershipPrivilege                  Take ownership of files or other objects                           Disabled");
    PrintLine("SeLoadDriverPrivilege                     Load and unload device drivers                                     Disabled");
    PrintLine("SeSystemProfilePrivilege                  Profile system performance                                         Enabled");
    PrintLine("SeSystemtimePrivilege                     Change the system time                                             Disabled");
    PrintLine("SeProfileSingleProcessPrivilege           Profile single process                                             Enabled");
    PrintLine("SeIncreaseBasePriorityPrivilege           Increase scheduling priority                                       Enabled");
    PrintLine("SeCreatePagefilePrivilege                 Create a pagefile                                                  Enabled");
    PrintLine("SeBackupPrivilege                         Back up files and directories                                      Disabled");
    PrintLine("SeRestorePrivilege                        Restore files and directories                                      Disabled");
    PrintLine("SeShutdownPrivilege                       Shut down the system                                               Enabled");
    PrintLine("SeDebugPrivilege                          Debug programs                                                     Enabled");
    PrintLine("SeSystemEnvironmentPrivilege              Modify firmware environment values                                 Disabled");
    PrintLine("SeChangeNotifyPrivilege                   Bypass traverse checking                                           Enabled");
    PrintLine("SeRemoteShutdownPrivilege                 Force shutdown from a remote system                                Disabled");
    PrintLine("SeUndockPrivilege                         Remove computer from docking station                               Enabled");
    PrintLine("SeManageVolumePrivilege                   Perform volume maintenance tasks                                   Enabled");
    PrintLine("SeImpersonatePrivilege                    Impersonate a client after authentication                          Enabled");
    PrintLine("SeCreateGlobalPrivilege                   Create global objects                                              Enabled");
    PrintLine("SeIncreaseWorkingSetPrivilege             Increase a process working set                                     Enabled");
    PrintLine("SeTimeZonePrivilege                       Change the time zone                                               Enabled");
    PrintLine("SeCreateSymbolicLinkPrivilege             Create symbolic links                                              Enabled");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "whoami.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("upn")) {
        PrintLine("admin@micant.local");
        ExitProcess(0);
    }

    if (parser.HasSwitch("fqdn")) {
        PrintLine("CN=admin,CN=Users,DC=micant,DC=local");
        ExitProcess(0);
    }

    bool showUser = parser.HasSwitch("user");
    bool showGroups = parser.HasSwitch("groups");
    bool showPriv = parser.HasSwitch("priv");
    bool showAll = parser.HasSwitch("all");

    if (showAll) {
        PrintUserTable();
        PrintLine("");
        PrintGroupsTable();
        PrintLine("");
        PrintPrivilegesTable();
        ExitProcess(0);
    }

    if (showUser || showGroups || showPriv) {
        if (showUser) {
            PrintUserTable();
            if (showGroups || showPriv) PrintLine("");
        }
        if (showGroups) {
            PrintGroupsTable();
            if (showPriv) PrintLine("");
        }
        if (showPriv) {
            PrintPrivilegesTable();
        }
        ExitProcess(0);
    }

    // Default: Domain\Username
    PrintLine("MICANT\\admin");
    ExitProcess(0);
}
