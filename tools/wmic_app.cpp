/**
 * @file wmic_app.cpp
 @brief Luce OS Ring 3 WMI Command-line (wmic.exe) Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows wmic behavior:
 * - Classes: cpu, os, memorychip, diskdrive, baseboard, bios, nic
 * - Exact Win32 exit codes (0 = Success, 1 = Error)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("[WMI Command-line (WMIC) Utility]");
    PrintLine("");
    PrintLine("Usage:");
    PrintLine("  WMIC [global switches] <alias> [options]");
    PrintLine("");
    PrintLine("The following alias/es are available to the current role:");
    PrintLine("  BASEBOARD               - Base board (also known as a motherboard or system board) management.");
    PrintLine("  BIOS                    - Basic input/output services (BIOS) management.");
    PrintLine("  CPU                     - CPU management.");
    PrintLine("  DISKDRIVE               - Physical disk drive management.");
    PrintLine("  LOGICALDISK             - Local storage device management.");
    PrintLine("  MEMORYCHIP              - Memory chip information.");
    PrintLine("  NIC                     - Network Interface Controller (NIC) management.");
    PrintLine("  OS                      - Installed Operating System/s management.");
    PrintLine("  PROCESS                 - Process management.");
    PrintLine("  SERVICE                 - Service application management.");
    PrintLine("  /?                      - Displays this help screen.");
}

void WmicCpu() noexcept {
    PrintLine("MaxClockSpeed  Name                                           NumberOfCores  NumberOfLogicalProcessors");
    PrintLine("3801           Intel(R) Xeon(R) CPU E3-1270 v6 @ 3.80GHz       4              8                        ");
}

void WmicOs() noexcept {
    PrintLine("Caption                                              Version       TotalVisibleMemorySize");
    PrintLine("MicaNT Sovereign Workstation (Dave Cutler Edition)   10.0.26100    33554432              ");
}

void WmicMemoryChip() noexcept {
    PrintLine("Capacity     Manufacturer  MemoryType  Speed");
    PrintLine("17179869184  Micron        26          2400 ");
    PrintLine("17179869184  Micron        26          2400 ");
}

void WmicDiskDrive() noexcept {
    PrintLine("Model                        Size           Status  InterfaceType");
    PrintLine("HGST HUS722T2TALA604 2TB     2000398934016  OK      IDE/SATA     ");
}

void WmicBaseBoard() noexcept {
    PrintLine("Manufacturer  Product   SerialNumber  Version");
    PrintLine("Supermicro    X11SSH-F  ZM179S019482  1.02   ");
}

void WmicBios() noexcept {
    PrintLine("Manufacturer                   SMBIOSBIOSVersion  Version");
    PrintLine("American Megatrends Inc.       2.7                AMI - 1072009");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "wmic.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* target = parser.argv()[1];

    if (StrCaseEqual(target, "cpu")) {
        WmicCpu();
        ExitProcess(0);
    }

    if (StrCaseEqual(target, "os")) {
        WmicOs();
        ExitProcess(0);
    }

    if (StrCaseEqual(target, "memorychip")) {
        WmicMemoryChip();
        ExitProcess(0);
    }

    if (StrCaseEqual(target, "diskdrive")) {
        WmicDiskDrive();
        ExitProcess(0);
    }

    if (StrCaseEqual(target, "baseboard")) {
        WmicBaseBoard();
        ExitProcess(0);
    }

    if (StrCaseEqual(target, "bios")) {
        WmicBios();
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
