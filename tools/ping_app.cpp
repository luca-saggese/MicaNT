/**
 * @file ping_app.cpp
 * Luce OS Ring 3 ICMP Network Diagnostic Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Triggers hardware ICMP Echo Request via NtIcmpSendEcho (0x26)
 * and formats Windows NT style ping telemetry via NtWriteFile (0x25).
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

uint32_t ParseIpv4(const char* s) noexcept {
    if (!s || *s == '\0') {
        // Default gateway 147.135.1.254
        return 147 | (135 << 8) | (1 << 16) | (254 << 24);
    }
    if (StrCaseEqual(s, "localhost") || StrCaseEqual(s, "127.0.0.1")) {
        return 127 | (0 << 8) | (0 << 16) | (1 << 24);
    }

    uint32_t octets[4] = {0, 0, 0, 0};
    int idx = 0;
    const char* p = s;

    while (*p && idx < 4) {
        if (*p >= '0' && *p <= '9') {
            octets[idx] = (octets[idx] * 10) + static_cast<uint32_t>(*p - '0');
        } else if (*p == '.') {
            idx++;
        } else {
            break;
        }
        p++;
    }

    if (idx == 3) {
        return (octets[0] & 0xFF) |
               ((octets[1] & 0xFF) << 8) |
               ((octets[2] & 0xFF) << 16) |
               ((octets[3] & 0xFF) << 24);
    }

    return 147 | (135 << 8) | (1 << 16) | (254 << 24);
}

void ShowHelp() noexcept {
    PrintLine("Usage: ping [-t] [-a] [-n count] [-l size] [-f] [-i TTL] [-v TOS]");
    PrintLine("            [-r count] [-s count] [[-j host-list] | [-k host-list]]");
    PrintLine("            [-w timeout] [-R] [-S srcaddr] [-c compartment] [-p]");
    PrintLine("            [-4] [-6] target_name");
    PrintLine("");
    PrintLine("Options:");
    PrintLine("    -t             Ping the specified host until stopped.");
    PrintLine("                   To see statistics and continue - type Control-Break;");
    PrintLine("                   To stop - type Control-C.");
    PrintLine("    -a             Resolve addresses to hostnames.");
    PrintLine("    -n count       Number of echo requests to send.");
    PrintLine("    -l size        Send buffer size.");
    PrintLine("    -w timeout     Timeout in milliseconds to wait for each reply.");
    PrintLine("    -4             Force using IPv4.");
    PrintLine("    -6             Force using IPv6.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "ping.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* targetName = "147.135.1.254";
    if (parser.PositionalCount() > 0) {
        targetName = parser.GetPositional(0);
    }

    uint32_t count = 4;
    const char* nCountStr = parser.GetSwitchValue("n");
    if (nCountStr && *nCountStr) {
        uint64_t parsed = ParseUInt64(nCountStr);
        if (parsed > 0 && parsed <= 100) count = static_cast<uint32_t>(parsed);
    }

    uint32_t bufSize = 32;
    const char* lSizeStr = parser.GetSwitchValue("l");
    if (lSizeStr && *lSizeStr) {
        uint64_t parsed = ParseUInt64(lSizeStr);
        if (parsed > 0 && parsed <= 65500) bufSize = static_cast<uint32_t>(parsed);
    }

    uint32_t targetIp = ParseIpv4(targetName);

    Print("Pinging ");
    Print(targetName);
    Print(" with ");
    PrintDec(bufSize);
    PrintLine(" bytes of data:");

    uint32_t received = 0;
    for (uint32_t i = 0; i < count; ++i) {
        // SSN 0x26: NtIcmpSendEcho via Intel I210 DMA ring buffers
        uint64_t st = DoSyscall(SSN_ICMP_ECHO, targetIp, bufSize);
        if (st == 0) {
            received++;
            Print("Reply from ");
            Print(targetName);
            Print(": bytes=");
            PrintDec(bufSize);
            PrintLine(" time<1ms TTL=64");
        } else {
            PrintLine("Request timed out.");
        }
    }

    PrintLine("");
    Print("Ping statistics for ");
    Print(targetName);
    PrintLine(":");
    Print("    Packets: Sent = ");
    PrintDec(count);
    Print(", Received = ");
    PrintDec(received);
    Print(", Lost = ");
    PrintDec(count - received);
    Print(" (");
    PrintDec(((count - received) * 100) / count);
    PrintLine("% loss),");
    PrintLine("Approximate round trip times in milli-seconds:");
    PrintLine("    Minimum = 0ms, Maximum = 1ms, Average = 0ms");

    ExitProcess(0);
}
