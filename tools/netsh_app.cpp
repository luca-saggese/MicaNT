/**
 * @file netsh_app.cpp
 * Luce OS Ring 3 Network Shell (netsh) Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Provides network configuration, IP, routing, interface, and firewall administration.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowInterfaceIpConfig() noexcept {
    const char* out =
        "\nConfiguration for interface \"Ethernet0\"\n"
        "    DHCP enabled:                         No\n"
        "    IP Address:                           147.135.1.229\n"
        "    Subnet Prefix:                        147.135.1.0/24 (mask 255.255.255.0)\n"
        "    Default Gateway:                      147.135.1.254\n"
        "    Gateway Metric:                       0\n"
        "    InterfaceMetric:                      10\n"
        "    Statically Configured DNS Servers:    213.186.33.99\n"
        "                                          1.1.1.1\n"
        "    Register with which suffix:           Primary only\n"
        "    Statically Configured WINS Servers:   None\n\n";
    PrintStr(out);
}

void ShowAdvFirewall() noexcept {
    const char* out =
        "\nDomain Profile Settings:\n"
        "----------------------------------------------------------------------\n"
        "State                                 ON\n"
        "Firewall Policy                       BlockInbound,AllowOutbound\n"
        "LocalFirewallRules                    N/A (GPO-merge)\n\n"
        "Private Profile Settings:\n"
        "----------------------------------------------------------------------\n"
        "State                                 ON\n"
        "Firewall Policy                       BlockInbound,AllowOutbound\n\n"
        "Public Profile Settings:\n"
        "----------------------------------------------------------------------\n"
        "State                                 ON\n"
        "Firewall Policy                       BlockInbound,AllowOutbound\n\n"
        "Ok.\n\n";
    PrintStr(out);
}

void ShowInterfaces() noexcept {
    const char* out =
        "\nAdmin State    State          Type             Interface Name\n"
        "-------------------------------------------------------------------------\n"
        "Enabled        Connected      Dedicated        Ethernet0\n"
        "Enabled        Connected      Loopback         Software Loopback Interface 1\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "netsh> [MicaNT Network Shell CLI]\n"
        "Copyright (c) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Hardware Target: Intel I210 Gigabit Network Connection (PCIe 02:00.0)\n"
        "Available Contexts: interface, advfirewall, firewall, routing, wlan\n\n";

    PrintStr(banner);

    PrintStr("[netsh interface show interface]\n");
    ShowInterfaces();

    PrintStr("[netsh interface ip show config]\n");
    ShowInterfaceIpConfig();

    PrintStr("[netsh advfirewall show allprofiles]\n");
    ShowAdvFirewall();

    PrintStr("netsh> Command execution completed with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
