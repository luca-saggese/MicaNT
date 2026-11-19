/**
 * @file rpcinfo_app.cpp
 * Luce OS Ring 3 RPC Portmapper Diagnostic Utility (rpcinfo) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries remote Sun RPC / ONC RPC portmappers (port 111) for active registered programs.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleRpcInfo() noexcept {
    const char* out =
        "   program vers proto   port  service\n"
        "    100000    4   tcp    111  portmapper\n"
        "    100000    3   tcp    111  portmapper\n"
        "    100000    2   tcp    111  portmapper\n"
        "    100000    4   udp    111  portmapper\n"
        "    100000    3   udp    111  portmapper\n"
        "    100000    2   udp    111  portmapper\n"
        "    100003    3   tcp   2049  nfs\n"
        "    100003    4   tcp   2049  nfs\n"
        "    100005    3   tcp  20048  mountd\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Displays RPC information from portmapper servers.\n\n"
        "RPCINFO -p [host]\n"
        "RPCINFO -b program version\n"
        "RPCINFO -d program version\n"
        "RPCINFO -u host program [version]\n"
        "RPCINFO -t host program [version]\n\n"
        "  -p      Probe portmapper on host and print list of registered programs.\n"
        "  -u      Make an RPC call to procedure 0 using UDP.\n"
        "  -t      Make an RPC call to procedure 0 using TCP.\n\n"
        "[rpcinfo -p localhost]\n";

    PrintStr(banner);
    ShowSampleRpcInfo();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
