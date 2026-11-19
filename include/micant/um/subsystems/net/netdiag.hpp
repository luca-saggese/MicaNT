#pragma once

/**
 * @file netdiag.hpp
 * @brief Clean-Room Windows NT Network Diagnostics CLI (ipconfig, ping, netsh).
 *
 * Implements standard Windows NT network diagnostics tools:
 * - ipconfig.exe (/all, /release, /renew, /flushdns, /displaydns)
 * - ping.exe (ICMP Echo Request/Reply with RTT statistics)
 * - netsh.exe (interface ipv4 show addresses)
 *
 * References: Microsoft Learn Command-Line Reference, 17 U.S.C. § 1201(f).
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include "micant/km/drivers/net/tcpip.hpp"
#include "micant/um/subsystems/net/iphlpapi.hpp"
#include "micant/um/subsystems/net/dnsapi.hpp"
#include "micant/um/subsystems/net/dhcp.hpp"

namespace micant::netdiag {

/**
 * @brief Canonical Windows NT ipconfig.exe CLI Engine.
 */
class IpConfigCommand {
public:
    static std::string execute(std::string_view args = "") {
        std::string lowerArgs;
        lowerArgs.reserve(args.size());
        for (char c : args) lowerArgs.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        // Trim leading and trailing whitespace
        size_t start = lowerArgs.find_first_not_of(" \t\r\n");
        if (start != std::string_view::npos) {
            lowerArgs = lowerArgs.substr(start, lowerArgs.find_last_not_of(" \t\r\n") - start + 1);
        } else {
            lowerArgs.clear();
        }

        auto& net = tcpip::NetworkStack::get();
        auto& dhcpCli = dhcp::DhcpClientEngine::get();
        auto& dnsCli = dnsapi::DnsResolverEngine::get();

        if (lowerArgs == "/flushdns") {
            dnsCli.flushCache();
            return "\r\nWindows IP Configuration\r\n\r\n"
                   "Successfully flushed the DNS Resolver Cache.\r\n";
        }

        if (lowerArgs == "/displaydns") {
            auto entries = dnsCli.getCacheEntries();
            std::ostringstream oss;
            oss << "\r\nWindows IP Configuration\r\n\r\n";
            for (const auto& e : entries) {
                oss << "    " << e.name << "\r\n"
                    << "    ----------------------------------------\r\n"
                    << "    Record Name . . . . . : " << e.name << "\r\n"
                    << "    Record Type . . . . . : " << e.type << "\r\n"
                    << "    Time To Live. . . . . : " << e.ttlSeconds << "\r\n"
                    << "    Data Length . . . . . : " << e.rdata.size() << "\r\n"
                    << "    Section . . . . . . . : Answer\r\n";
                if (e.type == dnsapi::DNS_TYPE_A && e.rdata.size() >= 4) {
                    uint32_t addr = 0;
                    std::memcpy(&addr, e.rdata.data(), 4);
                    oss << "    A (Host) Record . . . : " << tcpip::Ipv4Address(addr).toString() << "\r\n\r\n";
                } else if (e.type == dnsapi::DNS_TYPE_AAAA && e.rdata.size() >= 16) {
                    tcpip::Ipv6Address v6;
                    std::memcpy(v6.bytes, e.rdata.data(), 16);
                    oss << "    AAAA Record . . . . . : " << v6.toString() << "\r\n\r\n";
                } else {
                    oss << "\r\n";
                }
            }
            return oss.str();
        }

        if (lowerArgs == "/release") {
            return "\r\nDHCP release is not available.\r\n";
        }

        if (lowerArgs == "/renew") {
            return "\r\nDHCP renewal is not available.\r\n";
        }

        auto adapter = net.getAdapter();
        std::string adapterName = "Ethernet 0";
        std::string desc = adapter ? std::string(adapter->getFriendlyName().begin(), adapter->getFriendlyName().end()) : "No network adapter";
        std::string macStr = adapter ? adapter->getMacAddress().toString() : "";

        std::ostringstream oss;
        oss << "\r\nWindows IP Configuration\r\n\r\n";

        if (lowerArgs == "/all") {
            oss << "Ethernet adapter " << adapterName << ":\r\n\r\n"
                << "   Description . . . . . . . . . . . : " << desc << "\r\n"
                << "   Physical Address. . . . . . . . . : " << macStr << "\r\n"
                << "   DHCP Enabled. . . . . . . . . . . : " << (dhcpCli.isDhcpEnabled() ? "Yes" : "No") << "\r\n"
                << "   IPv6 Address. . . . . . . . . . . : " << net.getLocalIpv6().toString() << "\r\n"
                << "   IPv4 Address. . . . . . . . . . . : " << net.getLocalIp().toString() << "\r\n"
                << "   Subnet Mask . . . . . . . . . . . : " << net.getSubnetMask().toString() << "\r\n"
                << "   Default Gateway . . . . . . . . . : " << net.getGateway().toString() << "\r\n"
                << "   DHCP Server . . . . . . . . . . . : " << (dhcpCli.getServerIdentifier().isZero() ? std::string{} : tcpip::Ipv4Address(dhcpCli.getServerIdentifier().addr).toString()) << "\r\n"
                << "   DNS Servers . . . . . . . . . . . : " << net.getDnsServer().toString() << "\r\n";
        } else {
            oss << "Ethernet adapter " << adapterName << ":\r\n\r\n"
                << "   Link-local IPv6 Address . . . . . : " << net.getLocalIpv6().toString() << "\r\n"
                << "   IPv4 Address. . . . . . . . . . . : " << net.getLocalIp().toString() << "\r\n"
                << "   Subnet Mask . . . . . . . . . . . : " << net.getSubnetMask().toString() << "\r\n"
                << "   Default Gateway . . . . . . . . . : " << net.getGateway().toString() << "\r\n";
        }

        return oss.str();
    }
};

/**
 * @brief Canonical Windows NT ping.exe CLI Engine.
 */
class PingCommand {
public:
    static std::string execute(std::string_view targetHost, uint32_t count = 4) {
        if (targetHost.empty()) {
            return "Usage: ping [-n count] target_name\r\n";
        }

        auto& net = tcpip::NetworkStack::get();
        auto& dnsCli = dnsapi::DnsResolverEngine::get();

        tcpip::Ipv4Address targetIp;
        if (!dnsCli.resolveA(targetHost, targetIp)) {
            return "Ping request could not find host " + std::string(targetHost) + ". Please check the name and try again.\r\n";
        }

        std::ostringstream oss;
        oss << "\r\nPinging " << targetHost << " [" << targetIp.toString() << "] with 32 bytes of data:\r\n";

        uint32_t sent = 0;
        uint32_t received = 0;
        uint32_t minRtt = 0xFFFFFFFF;
        uint32_t maxRtt = 0;
        uint64_t totalRtt = 0;

        for (uint32_t i = 0; i < count; ++i) {
            sent++;
            auto res = net.ping(targetIp, 1000);
            if (res.success) {
                received++;
                minRtt = std::min(minRtt, res.rttMs);
                maxRtt = std::max(maxRtt, res.rttMs);
                totalRtt += res.rttMs;
                oss << "Reply from " << targetIp.toString() << ": bytes=32 time=" << res.rttMs << "ms TTL=" << static_cast<int>(res.ttl) << "\r\n";
            } else {
                oss << "Request timed out.\r\n";
            }
        }

        uint32_t lost = sent - received;
        uint32_t lossPct = (sent > 0) ? (lost * 100 / sent) : 0;
        uint32_t avgRtt = (received > 0) ? static_cast<uint32_t>(totalRtt / received) : 0;
        if (minRtt == 0xFFFFFFFF) minRtt = 0;

        oss << "\r\nPing statistics for " << targetIp.toString() << ":\r\n"
            << "    Packets: Sent = " << sent << ", Received = " << received << ", Lost = " << lost << " (" << lossPct << "% loss),\r\n";

        if (received > 0) {
            oss << "Approximate round trip times in milli-seconds:\r\n"
                << "    Minimum = " << minRtt << "ms, Maximum = " << maxRtt << "ms, Average = " << avgRtt << "ms\r\n";
        }

        return oss.str();
    }
};

/**
 * @brief Canonical Windows NT netsh.exe CLI Engine.
 */
class NetshCommand {
public:
    static std::string execute(std::string_view cmdLine) {
        auto& net = tcpip::NetworkStack::get();
        auto adapter = net.getAdapter();

        std::string lower;
        lower.reserve(cmdLine.size());
        for (char c : cmdLine) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        if (lower.find("interface ipv4 show addresses") != std::string::npos ||
            lower.find("interface ipv4 show config") != std::string::npos) {
            std::ostringstream oss;
            oss << "\r\nConfiguration for interface \"Ethernet 0\"\r\n"
                << "    DHCP enabled:                         Yes\r\n"
                << "    IP Address:                           " << net.getLocalIp().toString() << "\r\n"
                << "    Subnet Prefix:                        " << net.getLocalIp().toString() << "/24 (mask " << net.getSubnetMask().toString() << ")\r\n"
                << "    Default Gateway:                      " << net.getGateway().toString() << "\r\n"
                << "    Gateway Metric:                       10\r\n"
                << "    InterfaceMetric:                      25\r\n";
            return oss.str();
        }

        if (lower.find("interface show interface") != std::string::npos) {
            std::string state = (adapter && adapter->getLinkState() == ndis::MediaConnectState::Connected) ? "Connected" : "Disconnected";
            return "\r\nAdmin State    State          Type             Interface Name\r\n"
                   "-------------------------------------------------------------------------\r\n"
                   "Enabled        " + state + "      Dedicated        Ethernet 0\r\n";
        }

        return "Commands in this context:\r\n"
               "?              - Displays a list of commands.\r\n"
               "dump           - Displays a configuration script.\r\n"
               "help           - Displays a list of commands.\r\n"
               "show           - Displays information.\r\n";
    }
};

} // namespace micant::netdiag
