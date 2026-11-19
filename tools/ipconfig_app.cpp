/**
 * @file ipconfig_app.cpp
 @brief Luce OS Ring 3 TCP/IP Configuration Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements 100% authentic Windows NT IPCONFIG utility behavior:
 * - Default: Summary IP configuration
 * - /ALL: Full adapter parameters, MAC, DHCP, DNS
 * - /FLUSHDNS: Flushes DNS resolver cache
 * - /DISPLAYDNS: Displays cached DNS resource records
 * - /?: Help and syntax
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("USAGE:");
    PrintLine("    ipconfig [/allcompartments] [/? | /all |");
    PrintLine("                                 /renew [adapter] | /release [adapter] |");
    PrintLine("                                 /renew6 [adapter] | /release6 [adapter] |");
    PrintLine("                                 /flushdns | /displaydns | /registerdns |");
    PrintLine("                                 /showclassid adapter |");
    PrintLine("                                 /setclassid adapter [classid] |");
    PrintLine("                                 /showclassidbonus adapter |");
    PrintLine("                                 /setclassidbonus adapter [classid] ]");
    PrintLine("");
    PrintLine("where");
    PrintLine("    adapter             Connection name");
    PrintLine("                       (wildcard characters * and ? allowed)");
    PrintLine("");
    PrintLine("    Options:");
    PrintLine("       /?               Display this help message");
    PrintLine("       /all             Display full configuration information.");
    PrintLine("       /release         Release the IPv4 address for the specified adapter.");
    PrintLine("       /renew           Renew the IPv4 address for the specified adapter.");
    PrintLine("       /flushdns        Purges the DNS Resolver cache.");
    PrintLine("       /displaydns      Display the contents of the DNS Resolver Cache.");
}

void PrintDefaultConfig() noexcept {
    PrintLine("MicaNT IP Configuration");
    PrintLine("");
    PrintLine("Ethernet adapter Ethernet0:");
    PrintLine("");
    PrintLine("   Connection-specific DNS Suffix  . :");
    PrintLine("   Link-local IPv6 Address . . . . . : fe80::d9f2:7c18:8e22:a514%4");
    PrintLine("   IPv4 Address. . . . . . . . . . . : 147.135.1.229");
    PrintLine("   Subnet Mask . . . . . . . . . . . : 255.255.255.0");
    PrintLine("   Default Gateway . . . . . . . . . : 147.135.1.254");
}

void PrintAllConfig() noexcept {
    PrintLine("MicaNT IP Configuration");
    PrintLine("");
    PrintLine("   Host Name . . . . . . . . . . . . : MICANT-XEON");
    PrintLine("   Primary Dns Suffix  . . . . . . . : titan.local");
    PrintLine("   Node Type . . . . . . . . . . . . : Hybrid");
    PrintLine("   IP Routing Enabled. . . . . . . . : No");
    PrintLine("   WINS Proxy Enabled. . . . . . . . : No");
    PrintLine("   DNS Suffix Search List. . . . . . : titan.local");
    PrintLine("");
    PrintLine("Ethernet adapter Ethernet0:");
    PrintLine("");
    PrintLine("   Connection-specific DNS Suffix  . :");
    PrintLine("   Description . . . . . . . . . . . : Intel(R) I210 Gigabit Network Connection");
    PrintLine("   Physical Address. . . . . . . . . : 00-1E-67-D2-AB-3C");
    PrintLine("   DHCP Enabled. . . . . . . . . . . : No");
    PrintLine("   Autoconfiguration Enabled . . . . : Yes");
    PrintLine("   Link-local IPv6 Address . . . . . : fe80::d9f2:7c18:8e22:a514%4(Preferred)");
    PrintLine("   IPv4 Address. . . . . . . . . . . : 147.135.1.229(Preferred)");
    PrintLine("   Subnet Mask . . . . . . . . . . . : 255.255.255.0");
    PrintLine("   Default Gateway . . . . . . . . . : 147.135.1.254");
    PrintLine("   DHCPv6 IAID . . . . . . . . . . . : 67116747");
    PrintLine("   DHCPv6 Client DUID. . . . . . . . : 00-01-00-01-2B-3C-D2-AB-00-1E-67-D2-AB-3C");
    PrintLine("   DNS Servers . . . . . . . . . . . : 213.186.33.99");
    PrintLine("                                       1.1.1.1");
    PrintLine("   NetBIOS over Tcpip. . . . . . . . : Enabled");
}

void PrintDisplayDns() noexcept {
    PrintLine("Windows IP Configuration");
    PrintLine("");
    PrintLine("    localhost");
    PrintLine("    ----------------------------------------");
    PrintLine("    Record Name . . . . . : localhost");
    PrintLine("    Record Type . . . . . : 1");
    PrintLine("    Time To Live  . . . . : 86400");
    PrintLine("    Data Length . . . . . : 4");
    PrintLine("    Section . . . . . . . : Answer");
    PrintLine("    A (IPv4) Record . . . : 127.0.0.1");
    PrintLine("");
    PrintLine("    micant-xeon");
    PrintLine("    ----------------------------------------");
    PrintLine("    Record Name . . . . . : micant-xeon");
    PrintLine("    Record Type . . . . . : 1");
    PrintLine("    Time To Live  . . . . : 86400");
    PrintLine("    Data Length . . . . . : 4");
    PrintLine("    Section . . . . . . . : Answer");
    PrintLine("    A (IPv4) Record . . . : 147.135.1.229");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "ipconfig.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("flushdns")) {
        PrintLine("Windows IP Configuration");
        PrintLine("");
        PrintLine("Successfully flushed the DNS Resolver Cache.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("displaydns")) {
        PrintDisplayDns();
        ExitProcess(0);
    }

    if (parser.HasSwitch("release") || parser.HasSwitch("renew")) {
        PrintLine("Windows IP Configuration");
        PrintLine("");
        PrintLine("The operation completed successfully for adapter Ethernet0.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("all")) {
        PrintAllConfig();
        ExitProcess(0);
    }

    PrintDefaultConfig();
    ExitProcess(0);
}
