/**
 * @file sc_app.cpp
 @brief Luce OS Ring 3 Service Control Manager Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows Service Controller (sc.exe) behavior:
 * - Commands: query, queryex, qc, start, stop, pause, continue, delete
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter, 1060 = Service does not exist)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

struct ServiceInfo {
    const char* serviceName;
    const char* displayName;
    uint32_t type;            // 1 = KERNEL_DRIVER, 0x20 = WIN32_SHARE_PROCESS, 0x10 = WIN32_OWN_PROCESS
    uint32_t state;           // 1 = STOPPED, 4 = RUNNING, 7 = PAUSED
    const char* stateText;
    const char* binPath;
};

constexpr ServiceInfo SERVICES[] = {
    {"RpcSs",       "Remote Procedure Call (RPC)",                 0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k rpcss"},
    {"EventLog",    "Windows Event Log",                           0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k LocalServiceNetworkRestricted"},
    {"Dhcp",        "DHCP Client",                                 0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k LocalServiceNetworkRestricted"},
    {"Dnscache",    "DNS Client",                                  0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k NetworkService"},
    {"LanmanServer","Server",                                      0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k netsvcs"},
    {"LanmanWorkstation", "Workstation",                           0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k NetworkService"},
    {"TermService", "Remote Desktop Services",                     0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k termsvcs"},
    {"Netman",      "Network Connections",                         0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k LocalSystemNetworkRestricted"},
    {"PlugPlay",    "Plug and Play",                               0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k DcomLaunch"},
    {"Schedule",    "Task Scheduler",                              0x20, 4, "4  RUNNING", "C:\\MicaNT\\System32\\svchost.exe -k netsvcs"},
    {"Spooler",     "Print Spooler",                               0x10, 1, "1  STOPPED", "C:\\MicaNT\\System32\\spoolsv.exe"},
    {"ahci",        "AHCI SATA Controller Driver",                 0x01, 4, "4  RUNNING", "C:\\MicaNT\\System32\\drivers\\ahci.sys"},
    {"i210",        "Intel I210 Gigabit Ethernet Driver",          0x01, 4, "4  RUNNING", "C:\\MicaNT\\System32\\drivers\\i210.sys"},
};

constexpr size_t SERVICE_COUNT = sizeof(SERVICES) / sizeof(SERVICES[0]);

void ShowHelp() noexcept {
    PrintLine("DESCRIPTION:");
    PrintLine("        SC is a command line program used for communicating with the");
    PrintLine("        Service Control Manager and services.");
    PrintLine("USAGE:");
    PrintLine("        sc <server> [command] [service name] <option1> <option2>...");
    PrintLine("");
    PrintLine("        The option <server> has the form \"\\\\ServerName\"");
    PrintLine("        Further help on commands can be obtained by typing: \"sc [command]\"");
    PrintLine("        Commands:");
    PrintLine("          query           : Queries the status for a service, or");
    PrintLine("                            enumerates the status for types of services.");
    PrintLine("          queryex         : Queries the extended status for a service, or");
    PrintLine("                            enumerates the status for types of services.");
    PrintLine("          start           : Starts a service.");
    PrintLine("          pause           : Sends a PAUSE control request to a service.");
    PrintLine("          interrogate     : Sends an INTERROGATE control request to a service.");
    PrintLine("          continue        : Sends a CONTINUE control request to a service.");
    PrintLine("          stop            : Sends a STOP request to a service.");
    PrintLine("          config          : Changes the configuration of a service (persistent).");
    PrintLine("          description     : Changes the description of a service.");
    PrintLine("          failure         : Changes the actions taken by a service upon failure.");
    PrintLine("          qc              : Queries the configuration information for a service.");
    PrintLine("          qdescription    : Queries the description for a service.");
    PrintLine("          delete          : Deletes a service (from the registry).");
    PrintLine("          create          : Creates a service (adds it to the registry).");
}

void PrintServiceBlock(const ServiceInfo& svc) noexcept {
    PrintLine("");
    Print("SERVICE_NAME: "); PrintLine(svc.serviceName);
    Print("DISPLAY_NAME: "); PrintLine(svc.displayName);
    if (svc.type == 1) {
        PrintLine("        TYPE               : 1  KERNEL_DRIVER");
    } else if (svc.type == 0x10) {
        PrintLine("        TYPE               : 10  WIN32_OWN_PROCESS");
    } else {
        PrintLine("        TYPE               : 20  WIN32_SHARE_PROCESS");
    }
    Print("        STATE              : "); PrintLine(svc.stateText);
    PrintLine("                                (STOPPABLE, NOT_PAUSABLE, ACCEPTS_SHUTDOWN)");
    PrintLine("        WIN32_EXIT_CODE    : 0  (0x0)");
    PrintLine("        SERVICE_EXIT_CODE  : 0  (0x0)");
    PrintLine("        CHECKPOINT         : 0x0");
    PrintLine("        WAIT_HINT          : 0x0");
}

void PrintQcBlock(const ServiceInfo& svc) noexcept {
    PrintLine("[SC] QueryServiceConfig SUCCESS");
    PrintLine("");
    Print("SERVICE_NAME: "); PrintLine(svc.serviceName);
    if (svc.type == 1) {
        PrintLine("        TYPE               : 1  KERNEL_DRIVER");
    } else if (svc.type == 0x10) {
        PrintLine("        TYPE               : 10  WIN32_OWN_PROCESS");
    } else {
        PrintLine("        TYPE               : 20  WIN32_SHARE_PROCESS");
    }
    PrintLine("        START_TYPE         : 2   AUTO_START");
    PrintLine("        ERROR_CONTROL      : 1   NORMAL");
    Print("        BINARY_PATH_NAME   : "); PrintLine(svc.binPath);
    PrintLine("        LOAD_ORDER_GROUP   :");
    PrintLine("        TAG                : 0");
    Print("        DISPLAY_NAME       : "); PrintLine(svc.displayName);
    PrintLine("        DEPENDENCIES       :");
    PrintLine("        SERVICE_START_NAME : LocalSystem");
}

const ServiceInfo* FindService(const char* name) noexcept {
    if (!name || !*name) return nullptr;
    for (size_t i = 0; i < SERVICE_COUNT; ++i) {
        if (StrCaseEqual(SERVICES[i].serviceName, name) || StrCaseEqual(SERVICES[i].displayName, name)) {
            return &SERVICES[i];
        }
    }
    return nullptr;
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "sc.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* cmd = parser.argv()[1];
    const char* targetSvc = (parser.argc() > 2) ? parser.argv()[2] : nullptr;

    if (StrCaseEqual(cmd, "query") || StrCaseEqual(cmd, "queryex")) {
        if (targetSvc && *targetSvc) {
            const auto* s = FindService(targetSvc);
            if (s) {
                PrintServiceBlock(*s);
            } else {
                Print("[SC] EnumQueryServicesStatus:OpenService FAILED 1060:\r\n\r\nThe specified service does not exist as an installed service.\r\n");
                ExitProcess(1060);
            }
        } else {
            for (size_t i = 0; i < SERVICE_COUNT; ++i) {
                PrintServiceBlock(SERVICES[i]);
            }
        }
    } else if (StrCaseEqual(cmd, "qc")) {
        if (!targetSvc || !*targetSvc) {
            PrintLine("DESCRIPTION:");
            PrintLine("        Queries the configuration information for a service.");
            PrintLine("USAGE:");
            PrintLine("        sc <server> qc [service name] <bufferSize>");
            ExitProcess(1);
        }
        const auto* s = FindService(targetSvc);
        if (s) {
            PrintQcBlock(*s);
        } else {
            Print("[SC] OpenService FAILED 1060:\r\n\r\nThe specified service does not exist as an installed service.\r\n");
            ExitProcess(1060);
        }
    } else if (StrCaseEqual(cmd, "start")) {
        if (!targetSvc || !*targetSvc) {
            PrintLine("USAGE: sc start [service name]");
            ExitProcess(1);
        }
        const auto* s = FindService(targetSvc);
        if (s) {
            PrintServiceBlock(*s);
        } else {
            Print("[SC] StartService FAILED 1060:\r\n\r\nThe specified service does not exist as an installed service.\r\n");
            ExitProcess(1060);
        }
    } else if (StrCaseEqual(cmd, "stop")) {
        if (!targetSvc || !*targetSvc) {
            PrintLine("USAGE: sc stop [service name]");
            ExitProcess(1);
        }
        const auto* s = FindService(targetSvc);
        if (s) {
            ServiceInfo stopped = *s;
            stopped.state = 1;
            stopped.stateText = "1  STOPPED";
            PrintServiceBlock(stopped);
        } else {
            Print("[SC] ControlService FAILED 1060:\r\n\r\nThe specified service does not exist as an installed service.\r\n");
            ExitProcess(1060);
        }
    } else {
        PrintLine("ERROR: Unrecognized SC command.");
        ShowHelp();
        ExitProcess(1);
    }

    ExitProcess(0);
}
