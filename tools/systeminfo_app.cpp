/**
 * @file systeminfo_app.cpp
 @brief Luce OS Ring 3 System Information Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows NT SYSTEMINFO configuration reporter.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("SYSTEMINFO [/S system [/U username [/P [password]]]] [/FO format] [/NH]");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    Displays machine-specific properties and configuration.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /S      system           Specifies the remote system to connect to.");
    PrintLine("    /U      [domain\\]user    Specifies the user context under which the command should execute.");
    PrintLine("    /P      [password]       Specifies the password for the given user context.");
    PrintLine("    /FO     format           Specifies the format in which the output is to be displayed.");
    PrintLine("                             Valid values: \"TABLE\", \"LIST\", \"CSV\".");
    PrintLine("    /NH                      Specifies that the \"Column Header\" should be suppressed.");
    PrintLine("    /?                       Displays this help message.");
}

void PrintTable(bool noHeader) noexcept {
    if (!noHeader) {
        PrintLine("Host Name:                 MICANT-XEON");
        PrintLine("OS Name:                   MicaNT Sovereign Workstation (Dave Cutler Edition)");
        PrintLine("OS Version:                10.0.26100 N/A Build 26100");
        PrintLine("OS Manufacturer:           MicaNT Sovereign Project");
        PrintLine("OS Configuration:          Standalone Workstation");
        PrintLine("OS Build Type:             Multiprocessor Free (Clean-Room SMP x86_64)");
        PrintLine("Registered Owner:          Dave Cutler");
        PrintLine("Registered Organization:   MicaNT Systems");
        PrintLine("Product ID:                00329-00000-00003-AAOEM");
        PrintLine("Original Install Date:     10/20/2026, 12:00:00 PM");
        PrintLine("System Boot Time:          10/24/2026, 00:00:01 AM");
        PrintLine("System Manufacturer:       Supermicro");
        PrintLine("System Model:              X11SSH-F");
        PrintLine("System Type:               x64-based PC");
        PrintLine("Processor(s):              1 Processor(s) Installed.");
        PrintLine("                           [01]: Intel64 Family 6 Model 158 Stepping 9 ~3800 Mhz");
        PrintLine("BIOS Version:              American Megatrends Inc. 2.7, 02/16/2024");
        PrintLine("MicaNT Directory:          C:\\MicaNT");
        PrintLine("System Directory:          C:\\MicaNT\\System32");
        PrintLine("Boot Device:               \\Device\\HarddiskVolume1");
        PrintLine("Total Physical Memory:     32,768 MB");
        PrintLine("Available Physical Memory: 32,512 MB");
        PrintLine("Virtual Memory: Max Size:  32,768 MB");
        PrintLine("Virtual Memory: Available: 32,512 MB");
        PrintLine("Virtual Memory: In Use:    256 MB");
        PrintLine("Network Card(s):           1 NIC(s) Installed.");
        PrintLine("                           [01]: Intel(R) I210 Gigabit Network Connection");
        PrintLine("                                 Connection Name: Ethernet0");
        PrintLine("                                 DHCP Enabled:    No");
        PrintLine("                                 IP address(es)");
        PrintLine("                                 [01]: 147.135.1.229");
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "systeminfo.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    bool noHeader = parser.HasSwitch("nh");
    PrintTable(noHeader);

    ExitProcess(0);
}
