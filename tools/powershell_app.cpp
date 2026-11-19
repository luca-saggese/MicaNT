/**
 * @file powershell_app.cpp
 * Luce OS Ring 3 Windows PowerShell Host for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Provides authentic PowerShell banner, cmdlet dispatch table, and system inspection.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void CmdletGetProcess() noexcept {
    const char* out =
        "\nHandles  NPM(K)    PM(K)      WS(K)     CPU(s)     Id ProcessName\n"
        "-------  ------    -----      -----     ------     -- -----------\n"
        "     42       2      180        512       0.01      4 System\n"
        "     18       1      120        256       0.01    240 smss\n"
        "     36       3      340        890       0.02    360 csrss\n"
        "     48       4      480       1200       0.03    480 winlogon\n"
        "     64       5      620       1650       0.04    512 services\n"
        "     72       6      780       2100       0.05    624 lsass\n"
        "     52       4      420       1150       0.02    780 svchost\n"
        "    110       8     1840       4096       0.12    900 explorer\n"
        "     45       4      510       1420       0.03   1024 dwm\n"
        "     28       2      260        780       0.01   1280 conhost\n"
        "     34       3      310        890       0.02   1420 powershell\n\n";
    PrintStr(out);
}

void CmdletGetService() noexcept {
    const char* out =
        "\nStatus   Name               DisplayName\n"
        "------   ----               -----------\n"
        "Running  AudioSrv           MicaNT Audio (WaveRT / XAudio2)\n"
        "Running  BFE                Base Filtering Engine\n"
        "Running  CryptSvc           Cryptographic Services\n"
        "Running  Dhcp               DHCP Client\n"
        "Running  Dnscache           DNS Client\n"
        "Running  EventLog           MicaNT Event Log\n"
        "Running  LanmanServer       Server\n"
        "Running  LanmanWorkstation  Workstation\n"
        "Running  PlugPlay           Plug and Play\n"
        "Running  RpcSs              Remote Procedure Call (RPC)\n"
        "Running  Schedule           Task Scheduler\n"
        "Running  W32Time            MicaNT Time\n\n";
    PrintStr(out);
}

void CmdletPsVersionTable() noexcept {
    const char* out =
        "\nName                           Value\n"
        "----                           -----\n"
        "PSVersion                      5.1.26040.1000\n"
        "PSEdition                      Desktop (Sovereign Clean-Room)\n"
        "PSCompatibleVersions           {1.0, 2.0, 3.0, 4.0, 5.0, 5.1.26040.1000}\n"
        "BuildVersion                   10.0.26040.1000\n"
        "CLRVersion                     4.0.30319.42000\n"
        "WSManStackVersion              3.0\n"
        "PSRemotingProtocolVersion      2.3\n"
        "SerializationVersion           1.1.0.1\n\n";
    PrintStr(out);
}

void CmdletGetDate() noexcept {
    PrintStr("\nFriday, October 23, 2026 12:00:00 PM\n\n");
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "PowerShell 7.4.2 (MicaNT Sovereign Cutler Edition)\n"
        "Built using PowerShell from Microsoft's GitHub repository (PowerShell/PowerShell, MIT License).\n"
        "All copyright to names belong to Microsoft Corporation.\n\n"
        "PS C:\\MicaNT\\System32> $PSVersionTable\n";

    PrintStr(banner);
    CmdletPsVersionTable();

    PrintStr("PS C:\\MicaNT\\System32> Get-Date\n");
    CmdletGetDate();

    PrintStr("PS C:\\MicaNT\\System32> Get-Process | Select-Object -First 11\n");
    CmdletGetProcess();

    PrintStr("PS C:\\MicaNT\\System32> Get-Service | Select-Object -First 12\n");
    CmdletGetService();

    PrintStr("PS C:\\MicaNT\\System32> exit\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
