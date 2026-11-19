/**
 * @file help_app.cpp
 * Luce OS Ring 3 Command Catalog & Syntax Explorer (help) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Provides a comprehensive catalog of all commands and utilities available in MicaNT System32,
 * as well as detailed syntax reference when given a specific command argument.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowGeneralCatalog() noexcept {
    PrintLine("For more information on a specific command, type HELP command-name");
    PrintLine("ASSOC          Displays or modifies file extension associations.");
    PrintLine("ATTRIB         Displays or changes file attributes.");
    PrintLine("BCDEdit        Sets properties in boot database to control the boot loading.");
    PrintLine("BITSADMIN      Manages Background Intelligent Transfer Service jobs.");
    PrintLine("CACLS/ICACLS   Displays or modifies access control lists (ACLs) of files.");
    PrintLine("CD/CHDIR       Displays the name of or changes the current directory.");
    PrintLine("CHKDSK         Checks a disk and displays a status report.");
    PrintLine("CIPHER         Displays or alters the encryption of directories [files].");
    PrintLine("CLEANMGR       Automates disk cleanup tasks and frees storage space.");
    PrintLine("CMD            Starts a new instance of the MicaNT command interpreter.");
    PrintLine("COMPACT        Displays or alters the compression of files on NTFS/ReFS.");
    PrintLine("DEFRAG         Optimizes and defragments volumes and SSD storage tiers.");
    PrintLine("DIR            Displays a list of files and subdirectories in a directory.");
    PrintLine("DISKPART       Manages disks, partitions, and volumes.");
    PrintLine("DISM           Deployment Image Servicing and Management tool.");
    PrintLine("DRIVERQUERY    Enables an administrator to display a list of installed drivers.");
    PrintLine("EXPAND         Expands one or more compressed files (.cab / LZNT1).");
    PrintLine("FC             Compares two files or sets of files and displays differences.");
    PrintLine("FIND           Searches for a text string in a file or files.");
    PrintLine("FORMAT         Formats a disk for use with MicaNT (NTFS / FAT32 / ExtFS).");
    PrintLine("FSUTIL         Performs tasks related to file systems and volumes.");
    PrintLine("HELP           Provides Help information for MicaNT commands.");
    PrintLine("HOSTNAME       Prints the name of the current host.");
    PrintLine("IPCONFIG       Displays all current TCP/IP network configuration values.");
    PrintLine("LABEL          Creates, changes, or deletes the volume label of a disk.");
    PrintLine("MD/MKDIR       Creates a directory.");
    PrintLine("MORE           Displays output one screen at a time.");
    PrintLine("MOUNT/UMOUNT   Mounts and unmounts Network File System (NFS) network shares.");
    PrintLine("NET            Manages network resources, users, groups, and shares.");
    PrintLine("NETMSG         Resolves Win32 error codes and NTSTATUS to explanations.");
    PrintLine("NETSTAT        Displays protocol statistics and current TCP/IP network connections.");
    PrintLine("PING           Verifies IP-level connectivity to another TCP/IP computer.");
    PrintLine("PNPUTIL        Manages hardware device drivers and driver packages.");
    PrintLine("POWERCFG       Configures processor and system power management settings.");
    PrintLine("REPLACE        Replaces files or adds new files to destination directories.");
    PrintLine("ROBOCOPY       Advanced utility to copy file and directory trees.");
    PrintLine("SC             Communicates with the Service Control Manager and services.");
    PrintLine("SCHTASKS       Schedules commands and programs to run periodically.");
    PrintLine("SET            Displays, sets, or removes environment variables.");
    PrintLine("SETX           Creates or modifies environment variables in user or system.");
    PrintLine("SFC            Scans and verifies the integrity of protected system files.");
    PrintLine("SHUTDOWN       Allows proper local or remote shutdown of machine.");
    PrintLine("SYSTEMINFO     Displays machine specific properties and configuration.");
    PrintLine("TASKKILL       Terminates or stops a running process or application.");
    PrintLine("TASKLIST       Displays all currently running tasks including services.");
    PrintLine("TREE           Graphically displays the directory structure of a drive or path.");
    PrintLine("TYPE           Displays the contents of a text file.");
    PrintLine("VER            Displays the MicaNT version.");
    PrintLine("VOL            Displays a disk volume label and serial number.");
    PrintLine("VSSADMIN       Volume Shadow Copy Service administrative tool.");
    PrintLine("WBADMIN        MicaNT Backup command-line tool.");
    PrintLine("WHOAMI         Displays user, group and privileges information.");
    PrintLine("WPR            MicaNT Performance Recorder for Event Tracing (ETW).");
    PrintLine("XPERF          MicaNT Performance Analyzer trace capture and processing.");
    PrintLine("");
    PrintLine("Total Built-in Commands: 244 Sovereign Executables Registered.");
}

bool ShowCommandHelp(const char* cmd) noexcept {
    if (StrCaseEqual(cmd, "dir")) {
        PrintLine("Displays a list of files and subdirectories in a directory.");
        PrintLine("");
        PrintLine("DIR [drive:][path][filename] [/A[[:]attributes]] [/B] [/C] [/D] [/L] [/N]");
        PrintLine("  [/O[[:]sortorder]] [/P] [/Q] [/R] [/S] [/T[[:]timefield]] [/W] [/X] [/4]");
        return true;
    }
    if (StrCaseEqual(cmd, "copy")) {
        PrintLine("Copies one or more files to another location.");
        PrintLine("");
        PrintLine("COPY [/D] [/V] [/N] [/Y | /-Y] [/Z] [/L] [/A | /B ] source [/A | /B]");
        PrintLine("     [+ source [/A | /B] ...] [destination [/A | /B]]");
        return true;
    }
    if (StrCaseEqual(cmd, "type")) {
        PrintLine("Displays the contents of a text file or files.");
        PrintLine("");
        PrintLine("TYPE [drive:][path]filename");
        return true;
    }
    if (StrCaseEqual(cmd, "whoami")) {
        PrintLine("Displays user, group and privileges information for the current user.");
        PrintLine("");
        PrintLine("WHOAMI [/UPN | /FQDN | /USER | /GROUPS | /PRIV] [/FO format] [/NH]");
        return true;
    }
    if (StrCaseEqual(cmd, "hostname")) {
        PrintLine("Prints the name of the current host.");
        PrintLine("");
        PrintLine("hostname [new_name]");
        return true;
    }
    if (StrCaseEqual(cmd, "tasklist")) {
        PrintLine("Displays a list of currently running processes on either a local or remote machine.");
        PrintLine("");
        PrintLine("TASKLIST [/S system [/U username [/P [password]]]] [/M [module] | /SVC | /V] [/FI filter] [/FO format] [/NH]");
        return true;
    }
    if (StrCaseEqual(cmd, "taskkill")) {
        PrintLine("Terminates tasks by process id (PID) or image name.");
        PrintLine("");
        PrintLine("TASKKILL [/S system [/U username [/P [password]]]] { [/FI filter] [/PID processid | /IM imagename] } [/T] [/F]");
        return true;
    }
    if (StrCaseEqual(cmd, "ipconfig")) {
        PrintLine("Displays all current TCP/IP network configuration values.");
        PrintLine("");
        PrintLine("IPCONFIG [/allcompartments] [/? | /all | /renew [adapter] | /release [adapter] | /flushdns | /displaydns]");
        return true;
    }
    if (StrCaseEqual(cmd, "ping")) {
        PrintLine("Verifies IP-level connectivity to another TCP/IP computer.");
        PrintLine("");
        PrintLine("PING [-t] [-a] [-n count] [-l size] [-f] [-i TTL] [-v TOS] [-w timeout] target_name");
        return true;
    }
    if (StrCaseEqual(cmd, "chkdsk")) {
        PrintLine("Checks a disk and displays a status report.");
        PrintLine("");
        PrintLine("CHKDSK [volume[[path]filename]]] [/F] [/V] [/R] [/X] [/I] [/C] [/L[:size]] [/B]");
        return true;
    }
    if (StrCaseEqual(cmd, "netmsg") || StrCaseEqual(cmd, "net helpmsg")) {
        PrintLine("Explains the reason for a network or system error.");
        PrintLine("");
        PrintLine("NET HELPMSG <message#>");
        return true;
    }
    if (StrCaseEqual(cmd, "setx")) {
        PrintLine("Creates or modifies environment variables in the user or system environment.");
        PrintLine("");
        PrintLine("SETX [/S system [/U [domain\\]user [/P [password]]]] var value [/M]");
        return true;
    }
    if (StrCaseEqual(cmd, "format")) {
        PrintLine("Formats a disk for use with MicaNT.");
        PrintLine("");
        PrintLine("FORMAT volume [/FS:file-system] [/V:label] [/Q] [/L[:state]] [/A:size] [/C] [/I:enable]");
        return true;
    }
    if (StrCaseEqual(cmd, "shutdown")) {
        PrintLine("Allows you to shut down or restart a local or remote computer.");
        PrintLine("");
        PrintLine("SHUTDOWN [/i | /l | /s | /sg | /r | /g | /a | /p | /h | /e | /o] [/hybrid] [/soft] [/fw] [/f]");
        return true;
    }
    return false;
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "help.exe");

    if (parser.HasSwitch("?")) {
        PrintLine("Provides Help information for MicaNT commands.");
        PrintLine("");
        PrintLine("HELP [command]");
        PrintLine("");
        PrintLine("    command - displays help information on that command.");
        ExitProcess(0);
    }

    if (parser.PositionalCount() > 0) {
        const char* cmd = parser.GetPositional(0);
        if (!ShowCommandHelp(cmd)) {
            char msg[128];
            StrCopy(msg, "This command is not supported by the help utility. Try \"", sizeof(msg));
            StrConcat(msg, cmd, sizeof(msg));
            StrConcat(msg, " /?\".", sizeof(msg));
            PrintLine(msg);
        }
        ExitProcess(0);
    }

    ShowGeneralCatalog();
    ExitProcess(0);
}
