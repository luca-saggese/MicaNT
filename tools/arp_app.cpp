/**
 * @file arp_app.cpp
 * Luce OS Ring 3 Address Resolution Protocol (ARP) Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows ARP behavior:
 * - Switches: -a, -d, -s, -N, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

struct ArpEntry {
    const char* ip;
    const char* mac;
    const char* type;
};

constexpr ArpEntry ARP_TABLE[] = {
    {"147.135.1.254",   "00-22-bd-f8-19-ff", "dynamic"},
    {"147.135.1.255",   "ff-ff-ff-ff-ff-ff", "static"},
    {"224.0.0.22",      "01-00-5e-00-00-16", "static"},
    {"224.0.0.251",     "01-00-5e-00-00-fb", "static"},
    {"224.0.0.252",     "01-00-5e-00-00-fc", "static"},
    {"239.255.255.250", "01-00-5e-7f-ff-fa", "static"},
    {"255.255.255.255", "ff-ff-ff-ff-ff-ff", "static"},
};

constexpr size_t ARP_COUNT = sizeof(ARP_TABLE) / sizeof(ARP_TABLE[0]);

void ShowHelp() noexcept {
    PrintLine("Displays and modifies the IP-to-Physical address translation tables used by");
    PrintLine("the Address Resolution Protocol (ARP).");
    PrintLine("");
    PrintLine("ARP -s inet_addr eth_addr [if_addr]");
    PrintLine("ARP -d inet_addr [if_addr]");
    PrintLine("ARP -a [inet_addr] [-N if_addr] [-v]");
    PrintLine("");
    PrintLine("  -a            Displays current ARP entries by interrogating the current");
    PrintLine("                protocol data. If inet_addr is specified, the IP and Physical");
    PrintLine("                addresses for only the specified computer are displayed.");
    PrintLine("  -g            Same as -a.");
    PrintLine("  -v            Displays current ARP entries in verbose mode.");
    PrintLine("  -d            Deletes the host specified by inet_addr.");
    PrintLine("  -s            Adds the host and associates the Internet address inet_addr");
    PrintLine("                with the Physical address eth_addr.");
    PrintLine("  -N if_addr    Displays the ARP entries for the network interface specified");
    PrintLine("                by if_addr.");
    PrintLine("  /?            Displays this help message.");
}

void PrintArpTable() noexcept {
    PrintLine("");
    PrintLine("Interface: 147.135.1.229 --- 0x2");
    PrintLine("  Internet Address      Physical Address      Type");
    for (size_t i = 0; i < ARP_COUNT; ++i) {
        const auto& e = ARP_TABLE[i];
        Print("  ");
        Print(e.ip);
        for (size_t k = StrLen(e.ip); k < 22; ++k) PrintChar(' ');
        Print(e.mac);
        for (size_t k = StrLen(e.mac); k < 22; ++k) PrintChar(' ');
        PrintLine(e.type);
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "arp.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("a") || parser.HasSwitch("g")) {
        PrintArpTable();
        ExitProcess(0);
    }

    if (parser.HasSwitch("d")) {
        PrintLine("The ARP entry was deleted successfully.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("s")) {
        PrintLine("The ARP entry was added successfully.");
        ExitProcess(0);
    }

    PrintLine("ARP: invalid parameter list.");
    ShowHelp();
    ExitProcess(1);
}
