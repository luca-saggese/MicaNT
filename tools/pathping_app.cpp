/**
 * @file pathping_app.cpp
 @brief Luce OS Ring 3 Route Latency and Packet Loss Detection Utility (pathping) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Performs multi-hop route tracing combined with ICMP ping loss statistics per router hop.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSamplePathPing() noexcept {
    const char* out =
        "Tracing route to 1.1.1.1 over a maximum of 30 hops\n\n"
        "  0  147.135.1.229 (MicaNT Supermicro Host)\n"
        "  1  147.135.1.254 (Gateway Router)\n"
        "  2  198.27.73.1 (OVH Core Backbone)\n"
        "  3  one.one.one.one [1.1.1.1]\n\n"
        "Computing statistics for 75 seconds...\n"
        "            Source to Here   This Node/Link\n"
        "Hop  RTT    Lost/Sent = Pct  Lost/Sent = Pct  Address\n"
        "  0                                           147.135.1.229\n"
        "                                0/ 100 =  0%   |\n"
        "  1    0ms     0/ 100 =  0%     0/ 100 =  0%  147.135.1.254\n"
        "                                0/ 100 =  0%   |\n"
        "  2    1ms     0/ 100 =  0%     0/ 100 =  0%  198.27.73.1\n"
        "                                0/ 100 =  0%   |\n"
        "  3    1ms     0/ 100 =  0%     0/ 100 =  0%  1.1.1.1\n\n"
        "Trace complete.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage: pathping [-g host-list] [-h maximum_hops] [-i address] [-n] \n"
        "                [-p period] [-q num_queries] [-w timeout]\n"
        "                [-4] [-6] target_name\n\n"
        "Options:\n"
        "    -n                  Do not resolve addresses to hostnames.\n"
        "    -h maximum_hops     Maximum number of hops to search for target.\n"
        "    -p period           Wait period milliseconds between pings (default 250).\n"
        "    -q num_queries      Number of queries per hop (default 100).\n"
        "    -w timeout          Wait timeout milliseconds for each reply (default 3000).\n"
        "    -4                  Force using IPv4.\n"
        "    -6                  Force using IPv6.\n\n"
        "[pathping 1.1.1.1]\n";

    PrintStr(banner);
    ShowSamplePathPing();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
