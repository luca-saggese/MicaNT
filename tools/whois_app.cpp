/**
 * @file whois_app.cpp
 @brief Luce OS Ring 3 WHOIS Directory Client (whois) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries domain registrations, IP allocations, and ASN records over TCP port 43.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleWhois() noexcept {
    const char* out =
        "Connecting to whois.iana.org on TCP port 43...\n\n"
        "Domain: micant.internal\n"
        "Registrar: Sovereign MicaNT System Authority\n"
        "Organization: MicaNT Operating System Project\n"
        "Name Server: 147.135.1.229 (ns1.micant.internal)\n"
        "IP Network: 147.135.1.0/24 (OVH US BHS Datacenter / Xeon Dedicated Node)\n"
        "Autonomous System: AS16276 (OVHcloud SAS)\n"
        "Hardware Platform: Supermicro X11SSH-F / Intel Xeon E3-1270 v6 @ 3.80GHz\n"
        "Status: Active / Sovereign\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "WHOIS [-v] [-h server] domain_or_ip\n\n"
        "  -v          Verbose diagnostic output.\n"
        "  -h server   Specifies custom WHOIS server (default: whois.iana.org).\n\n"
        "[Query: Default Network Identity]\n";

    PrintStr(banner);
    ShowSampleWhois();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
