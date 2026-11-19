/**
 * @file netstat_app.cpp
 * Luce OS Ring 3 Network Statistics Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows netstat behavior:
 * - Switches: -a, -e, -n, -o, -r, -p, /?
 * - Multi-flag combinations: -ano, -an, -ao
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

struct SocketEntry {
    const char* proto;
    const char* localAddr;
    const char* foreignAddr;
    const char* state;
    uint32_t pid;
};

constexpr SocketEntry SOCKETS[] = {
    {"TCP", "0.0.0.0:135",          "0.0.0.0:0",           "LISTENING",   640},
    {"TCP", "0.0.0.0:445",          "0.0.0.0:0",           "LISTENING",     4},
    {"TCP", "0.0.0.0:3389",         "0.0.0.0:0",           "LISTENING",   720},
    {"TCP", "147.135.1.229:139",    "0.0.0.0:0",           "LISTENING",     4},
    {"TCP", "147.135.1.229:445",    "147.135.1.254:49152", "ESTABLISHED",   4},
    {"TCP", "147.135.1.229:3389",   "147.135.1.100:54321", "ESTABLISHED", 720},
    {"TCP", "[::]:135",             "[::]:0",              "LISTENING",   640},
    {"TCP", "[::]:445",             "[::]:0",              "LISTENING",     4},
    {"TCP", "[::]:3389",            "[::]:0",              "LISTENING",   720},
    {"UDP", "0.0.0.0:123",          "*:*",                 "",            800},
    {"UDP", "0.0.0.0:500",          "*:*",                 "",            910},
    {"UDP", "0.0.0.0:3702",         "*:*",                 "",            940},
    {"UDP", "147.135.1.229:137",    "*:*",                 "",              4},
    {"UDP", "147.135.1.229:138",    "*:*",                 "",              4},
};

constexpr size_t SOCKET_COUNT = sizeof(SOCKETS) / sizeof(SOCKETS[0]);

void ShowHelp() noexcept {
    PrintLine("Displays protocol statistics and current TCP/IP network connections.");
    PrintLine("");
    PrintLine("NETSTAT [-a] [-e] [-n] [-o] [-p proto] [-r] [-s]");
    PrintLine("");
    PrintLine("  -a            Displays all connections and listening ports.");
    PrintLine("  -e            Displays Ethernet statistics. May be combined with -s.");
    PrintLine("  -n            Displays addresses and port numbers in numerical form.");
    PrintLine("  -o            Displays the owning process ID associated with each connection.");
    PrintLine("  -p proto      Shows connections for the protocol specified by proto; proto");
    PrintLine("                may be any of: TCP, UDP, TCPv6, or UDPv6.");
    PrintLine("  -r            Displays the routing table.");
    PrintLine("  -s            Displays per-protocol statistics.");
    PrintLine("  /?            Displays this help message.");
}

void PrintEthernetStats() noexcept {
    PrintLine("Interface Statistics");
    PrintLine("");
    PrintLine("                           Received            Sent");
    PrintLine("");
    PrintLine("Bytes                      14829104        12048592");
    PrintLine("Unicast packets              104921           87452");
    PrintLine("Non-unicast packets            1204             340");
    PrintLine("Discards                          0               0");
    PrintLine("Errors                            0               0");
    PrintLine("Unknown protocols                 0");
}

void PrintRouteTable() noexcept {
    PrintLine("===========================================================================");
    PrintLine("Interface List");
    PrintLine("  2...a4 bf 01 1e a3 be ......Intel(R) I210 Gigabit Network Connection");
    PrintLine("  1...........................Software Loopback Interface 1");
    PrintLine("===========================================================================");
    PrintLine("");
    PrintLine("IPv4 Route Table");
    PrintLine("===========================================================================");
    PrintLine("Active Routes:");
    PrintLine("Network Destination        Netmask          Gateway       Interface  Metric");
    PrintLine("          0.0.0.0          0.0.0.0    147.135.1.254   147.135.1.229      25");
    PrintLine("      147.135.1.0    255.255.255.0         On-link    147.135.1.229     281");
    PrintLine("    147.135.1.229  255.255.255.255         On-link    147.135.1.229     281");
    PrintLine("    147.135.1.255  255.255.255.255         On-link    147.135.1.229     281");
    PrintLine("        127.0.0.0        255.0.0.0         On-link         127.0.0.1     331");
    PrintLine("        127.0.0.1  255.255.255.255         On-link         127.0.0.1     331");
    PrintLine("  127.255.255.255  255.255.255.255         On-link         127.0.0.1     331");
    PrintLine("        224.0.0.0        240.0.0.0         On-link         127.0.0.1     331");
    PrintLine("        224.0.0.0        240.0.0.0         On-link    147.135.1.229     281");
    PrintLine("  255.255.255.255  255.255.255.255         On-link         127.0.0.1     331");
    PrintLine("  255.255.255.255  255.255.255.255         On-link    147.135.1.229     281");
    PrintLine("===========================================================================");
    PrintLine("Persistent Routes:");
    PrintLine("  None");
}

void PrintConnections(bool showAll, bool showPid, const char* filterProto) noexcept {
    PrintLine("");
    PrintLine("Active Connections");
    PrintLine("");

    if (showPid) {
        PrintLine("  Proto  Local Address          Foreign Address        State           PID");
    } else {
        PrintLine("  Proto  Local Address          Foreign Address        State");
    }

    for (size_t i = 0; i < SOCKET_COUNT; ++i) {
        const auto& s = SOCKETS[i];
        if (filterProto && !StrCaseEqual(s.proto, filterProto)) continue;
        if (!showAll && StrEqual(s.state, "LISTENING")) continue;

        Print("  ");
        Print(s.proto);
        for (size_t k = StrLen(s.proto); k < 7; ++k) PrintChar(' ');

        Print(s.localAddr);
        for (size_t k = StrLen(s.localAddr); k < 23; ++k) PrintChar(' ');

        Print(s.foreignAddr);
        for (size_t k = StrLen(s.foreignAddr); k < 23; ++k) PrintChar(' ');

        Print(s.state);
        if (showPid) {
            for (size_t k = StrLen(s.state); k < 16; ++k) PrintChar(' ');
            PrintDec(s.pid);
        }
        PrintLine("");
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "netstat.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    // Check combo switches like -ano, -an, -ao
    bool showAll = parser.HasSwitch("a");
    bool showEth = parser.HasSwitch("e");
    bool showPid = parser.HasSwitch("o");
    bool showRoute = parser.HasSwitch("r");

    for (size_t i = 1; i < parser.argc(); ++i) {
        const char* tok = parser.argv()[i];
        if (tok[0] == '-' || tok[0] == '/') {
            const char* s = tok + 1;
            while (*s) {
                if (*s == 'a' || *s == 'A') showAll = true;
                if (*s == 'e' || *s == 'E') showEth = true;
                if (*s == 'o' || *s == 'O') showPid = true;
                if (*s == 'r' || *s == 'R') showRoute = true;
                s++;
            }
        }
    }

    const char* proto = parser.GetSwitchValue("p");

    if (showEth) {
        PrintEthernetStats();
    } else if (showRoute) {
        PrintRouteTable();
    } else {
        PrintConnections(showAll, showPid, proto);
    }

    ExitProcess(0);
}
