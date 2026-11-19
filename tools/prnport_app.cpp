/**
 * @file prnport_app.cpp
 * Luce OS Ring 3 Printer Port Configuration Utility (prnport) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Creates, deletes, and lists standard TCP/IP and RAW/LPR printer ports.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSamplePorts() noexcept {
    const char* out =
        "Server name <local>\n"
        "Port name               LPT1:\n"
        "Host address            \n"
        "Protocol                Local Parallel Port\n\n"
        "Port name               COM1:\n"
        "Host address            \n"
        "Protocol                Local Serial Communications Port\n\n"
        "Port name               IP_147.135.1.200\n"
        "Host address            147.135.1.200\n"
        "Protocol                RAW\n"
        "Port number             9100\n"
        "SNMP                    Disabled\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage: prnport [-adlsg?][c] [-s server][-u user_name][-w password]\n"
        "               [-r port_name][-h host_address][-o {raw|lpr}][-n port_number]\n"
        "               [-m{e|d}][-i snmp_index][-y community_name]\n\n"
        "Arguments:\n"
        "  -a     - add a standard TCP/IP port\n"
        "  -d     - delete a port\n"
        "  -l     - list all ports\n"
        "  -g     - display port configuration\n\n"
        "[prnport -l]\n";

    PrintStr(banner);
    ShowSamplePorts();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
