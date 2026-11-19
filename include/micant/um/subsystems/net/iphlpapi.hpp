#pragma once

/**
 * @file iphlpapi.hpp
 * @brief Clean-Room IP Helper API (iphlpapi.dll) & ICMP Subsystem.
 *
 * Implements standard Windows NT IP Helper network configuration inspection:
 * - GetAdaptersAddresses (IP_ADAPTER_ADDRESSES_LH with Unicast, DNS, FriendlyName)
 * - GetAdaptersInfo (IP_ADAPTER_INFO legacy)
 * - GetNetworkParams (FIXED_INFO)
 * - GetIpForwardTable (MIB_IPFORWARDTABLE routing table)
 * - GetIpNetTable (MIB_IPNETTABLE ARP table)
 * - GetTcpTable2 (MIB_TCPTABLE2 TCP connection table)
 * - GetUdpTable (MIB_UDPTABLE active UDP listeners)
 * - IcmpCreateFile, IcmpSendEcho, IcmpCloseHandle (icmp.dll / iphlpapi.dll)
 *
 * References: Microsoft Learn IP Helper API & 17 U.S.C. § 1201(f).
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "micant/shared/ntdef.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/native/ldr.hpp"
#include "micant/km/drivers/net/tcpip.hpp"
#if !defined(MICANT_NATIVE_USERMODE)
#include "micant/um/subsystems/winbase/version.hpp"
#endif

namespace micant::iphlpapi {

using HANDLE = void*;
using BOOL = int;

inline constexpr uint32_t MAX_ADAPTER_NAME_LENGTH        = 256;
inline constexpr uint32_t MAX_ADAPTER_DESCRIPTION_LENGTH = 128;
inline constexpr uint32_t MAX_ADAPTER_ADDRESS_LENGTH     = 8;
inline constexpr uint32_t MAX_HOSTNAME_LEN               = 128;
inline constexpr uint32_t MAX_DOMAIN_NAME_LEN            = 128;
inline constexpr uint32_t MAX_SCOPE_ID_LEN               = 256;

// Adapter Types
inline constexpr uint32_t MIB_IF_TYPE_OTHER     = 1;
inline constexpr uint32_t MIB_IF_TYPE_ETHERNET  = 6;
inline constexpr uint32_t MIB_IF_TYPE_TOKENRING = 9;
inline constexpr uint32_t MIB_IF_TYPE_FDDI      = 15;
inline constexpr uint32_t MIB_IF_TYPE_PPP       = 23;
inline constexpr uint32_t MIB_IF_TYPE_LOOPBACK  = 24;

// Error Codes
inline constexpr uint32_t ERROR_SUCCESS           = 0;
inline constexpr uint32_t ERROR_INVALID_PARAMETER = 87;
inline constexpr uint32_t ERROR_BUFFER_OVERFLOW   = 111;
inline constexpr uint32_t ERROR_NO_DATA           = 232;
inline constexpr uint32_t IP_SUCCESS              = 0;

// Flags for GetAdaptersAddresses
inline constexpr uint32_t GAA_FLAG_SKIP_UNICAST           = 0x0001;
inline constexpr uint32_t GAA_FLAG_SKIP_ANYCAST           = 0x0002;
inline constexpr uint32_t GAA_FLAG_SKIP_MULTICAST         = 0x0004;
inline constexpr uint32_t GAA_FLAG_SKIP_DNS_SERVER        = 0x0008;
inline constexpr uint32_t GAA_FLAG_INCLUDE_PREFIX         = 0x0010;
inline constexpr uint32_t GAA_FLAG_SKIP_FRIENDLY_NAME     = 0x0020;
inline constexpr uint32_t GAA_FLAG_INCLUDE_WINS_INFO      = 0x0040;
inline constexpr uint32_t GAA_FLAG_INCLUDE_GATEWAYS       = 0x0080;
inline constexpr uint32_t GAA_FLAG_INCLUDE_ALL_INTERFACES = 0x0100;
inline constexpr uint32_t GAA_FLAG_INCLUDE_ALL_COMPARTMENTS = 0x0200;

// Adapter Flags
inline constexpr uint32_t IP_ADAPTER_DDNS_ENABLED               = 0x0001;
inline constexpr uint32_t IP_ADAPTER_REGISTER_ADAPTER_SUFFIX    = 0x0002;
inline constexpr uint32_t IP_ADAPTER_DHCP_ENABLED                = 0x0004;
inline constexpr uint32_t IP_ADAPTER_RECEIVE_ONLY                = 0x0008;
inline constexpr uint32_t IP_ADAPTER_NO_MULTICAST               = 0x0010;
inline constexpr uint32_t IP_ADAPTER_IPV6_OTHER_STATEFUL_CONFIG  = 0x0020;
inline constexpr uint32_t IP_ADAPTER_NETBIOS_OVER_TCPIP_ENABLED  = 0x0040;
inline constexpr uint32_t IP_ADAPTER_IPV4_ENABLED                = 0x0080;
inline constexpr uint32_t IP_ADAPTER_IPV6_ENABLED                = 0x0100;

enum IF_OPER_STATUS {
    IfOperStatusUp = 1,
    IfOperStatusDown = 2,
    IfOperStatusTesting = 3,
    IfOperStatusUnknown = 4,
    IfOperStatusDormant = 5,
    IfOperStatusNotPresent = 6,
    IfOperStatusLowerLayerDown = 7
};

#pragma pack(push, 4)

struct IP_ADDRESS_STRING {
    char str[16]{0};
};

struct IP_ADDR_STRING {
    IP_ADDR_STRING*   next{nullptr};
    IP_ADDRESS_STRING ipAddress;
    IP_ADDRESS_STRING ipMask;
    uint32_t          context{0};
};

struct IP_ADAPTER_INFO {
    IP_ADAPTER_INFO* next{nullptr};
    uint32_t comboIndex{0};
    char adapterName[MAX_ADAPTER_NAME_LENGTH + 4]{0};
    char description[MAX_ADAPTER_DESCRIPTION_LENGTH + 4]{0};
    uint32_t addressLength{6};
    uint8_t address[MAX_ADAPTER_ADDRESS_LENGTH]{0};
    uint32_t index{1};
    uint32_t type{MIB_IF_TYPE_ETHERNET};
    uint32_t dhcpEnabled{0};
    IP_ADDR_STRING currentIpAddress{};
    IP_ADDR_STRING ipAddressList{};
    IP_ADDR_STRING gatewayList{};
    IP_ADDR_STRING dhcpServer{};
    int haveWins{0};
    IP_ADDR_STRING primaryWinsServer{};
    IP_ADDR_STRING secondaryWinsServer{};
    int64_t leaseObtained{0};
    int64_t leaseExpires{0};
};

struct FIXED_INFO {
    char hostName[MAX_HOSTNAME_LEN + 4]{0};
    char domainName[MAX_DOMAIN_NAME_LEN + 4]{0};
    IP_ADDR_STRING* currentDnsServer{nullptr};
    IP_ADDR_STRING dnsServerList{};
    uint32_t nodeType{1};
    char scopeId[MAX_SCOPE_ID_LEN + 4]{0};
    uint32_t enableRouting{0};
    uint32_t enableProxy{0};
    uint32_t enableDns{1};
};

struct SOCKET_ADDRESS {
    void* lpSockaddr{nullptr};
    int iSockaddrLength{0};
};

struct IP_ADAPTER_UNICAST_ADDRESS_LH {
    union {
        struct {
            uint32_t Length;
            uint32_t Flags;
        };
    };
    IP_ADAPTER_UNICAST_ADDRESS_LH* Next{nullptr};
    SOCKET_ADDRESS Address{};
    int PrefixOrigin{0};
    int SuffixOrigin{0};
    int DadState{0};
    uint32_t ValidLifetime{0xFFFFFFFF};
    uint32_t PreferredLifetime{0xFFFFFFFF};
    uint32_t LeaseLifetime{0xFFFFFFFF};
    uint8_t OnLinkPrefixLength{24};
};

struct IP_ADAPTER_DNS_SERVER_ADDRESS {
    union {
        struct {
            uint32_t Length;
            uint32_t Reserved;
        };
    };
    IP_ADAPTER_DNS_SERVER_ADDRESS* Next{nullptr};
    SOCKET_ADDRESS Address{};
};

struct IP_ADAPTER_ADDRESSES_LH {
    union {
        uint64_t Alignment{0};
        struct {
            uint32_t Length;
            uint32_t IfIndex;
        };
    };
    IP_ADAPTER_ADDRESSES_LH* Next{nullptr};
    char* AdapterName{nullptr};
    IP_ADAPTER_UNICAST_ADDRESS_LH* FirstUnicastAddress{nullptr};
    void* FirstAnycastAddress{nullptr};
    void* FirstMulticastAddress{nullptr};
    IP_ADAPTER_DNS_SERVER_ADDRESS* FirstDnsServerAddress{nullptr};
    wchar_t* DnsSuffix{nullptr};
    wchar_t* Description{nullptr};
    wchar_t* FriendlyName{nullptr};
    uint8_t PhysicalAddress[8]{0};
    uint32_t PhysicalAddressLength{6};
    uint32_t Flags{IP_ADAPTER_IPV4_ENABLED | IP_ADAPTER_IPV6_ENABLED};
    uint32_t Mtu{1500};
    uint32_t IfType{MIB_IF_TYPE_ETHERNET};
    IF_OPER_STATUS OperStatus{IfOperStatusUp};
    uint32_t Ipv6IfIndex{1};
    uint32_t ZoneIndices[16]{0};
    void* FirstPrefix{nullptr};
    uint64_t TransmitLinkSpeed{1000000000ULL};
    uint64_t ReceiveLinkSpeed{1000000000ULL};
};

// MIB Routing Table Structures
inline constexpr uint32_t MIB_IPROUTE_TYPE_OTHER    = 1;
inline constexpr uint32_t MIB_IPROUTE_TYPE_INVALID  = 2;
inline constexpr uint32_t MIB_IPROUTE_TYPE_DIRECT   = 3;
inline constexpr uint32_t MIB_IPROUTE_TYPE_INDIRECT = 4;

struct MIB_IPFORWARDROW {
    uint32_t dwForwardDest{0};
    uint32_t dwForwardMask{0};
    uint32_t dwForwardPolicy{0};
    uint32_t dwForwardNextHop{0};
    uint32_t dwForwardIfIndex{1};
    uint32_t dwForwardType{MIB_IPROUTE_TYPE_DIRECT};
    uint32_t dwForwardProto{3}; // MIB_IPPROTO_NETMGMT
    uint32_t dwForwardAge{0};
    uint32_t dwForwardNextHopAS{0};
    uint32_t dwForwardMetric1{1};
    uint32_t dwForwardMetric2{static_cast<uint32_t>(-1)};
    uint32_t dwForwardMetric3{static_cast<uint32_t>(-1)};
    uint32_t dwForwardMetric4{static_cast<uint32_t>(-1)};
    uint32_t dwForwardMetric5{static_cast<uint32_t>(-1)};
};

struct MIB_IPFORWARDTABLE {
    uint32_t dwNumEntries{0};
    MIB_IPFORWARDROW table[16];
};

// MIB ARP Table Structures
inline constexpr uint32_t MIB_IPNET_TYPE_OTHER   = 1;
inline constexpr uint32_t MIB_IPNET_TYPE_INVALID = 2;
inline constexpr uint32_t MIB_IPNET_TYPE_DYNAMIC = 3;
inline constexpr uint32_t MIB_IPNET_TYPE_STATIC  = 4;

struct MIB_IPNETROW {
    uint32_t dwIndex{1};
    uint32_t dwPhysAddrLen{6};
    uint8_t  bPhysAddr[8]{0};
    uint32_t dwAddr{0};
    uint32_t dwType{MIB_IPNET_TYPE_DYNAMIC};
};

struct MIB_IPNETTABLE {
    uint32_t dwNumEntries{0};
    MIB_IPNETROW table[64];
};

// MIB TCP Table Structures
inline constexpr uint32_t MIB_TCP_STATE_CLOSED     = 1;
inline constexpr uint32_t MIB_TCP_STATE_LISTEN     = 2;
inline constexpr uint32_t MIB_TCP_STATE_SYN_SENT   = 3;
inline constexpr uint32_t MIB_TCP_STATE_SYN_RCVD   = 4;
inline constexpr uint32_t MIB_TCP_STATE_ESTAB      = 5;
inline constexpr uint32_t MIB_TCP_STATE_FIN_WAIT1  = 6;
inline constexpr uint32_t MIB_TCP_STATE_FIN_WAIT2  = 7;
inline constexpr uint32_t MIB_TCP_STATE_CLOSE_WAIT = 8;
inline constexpr uint32_t MIB_TCP_STATE_CLOSING    = 9;
inline constexpr uint32_t MIB_TCP_STATE_LAST_ACK   = 10;
inline constexpr uint32_t MIB_TCP_STATE_TIME_WAIT  = 11;
inline constexpr uint32_t MIB_TCP_STATE_DELETE_TCB = 12;

struct MIB_TCPROW2 {
    uint32_t dwState{0};
    uint32_t dwLocalAddr{0};
    uint32_t dwLocalPort{0};
    uint32_t dwRemoteAddr{0};
    uint32_t dwRemotePort{0};
    uint32_t dwOwningPid{4};
    uint32_t dwOffloadState{0};
};

struct MIB_TCPTABLE2 {
    uint32_t dwNumEntries{0};
    MIB_TCPROW2 table[64];
};

// MIB UDP Table Structures
struct MIB_UDPROW {
    uint32_t dwLocalAddr{0};
    uint32_t dwLocalPort{0};
};

struct MIB_UDPTABLE {
    uint32_t dwNumEntries{0};
    MIB_UDPROW table[64];
};

// ICMP Echo Structures
struct IP_OPTION_INFORMATION {
    uint8_t  Ttl{64};
    uint8_t  Tos{0};
    uint8_t  Flags{0};
    uint8_t  OptionsSize{0};
    uint8_t* OptionsData{nullptr};
};

struct ICMP_ECHO_REPLY {
    uint32_t Address{0};
    uint32_t Status{IP_SUCCESS};
    uint32_t RoundTripTime{1}; // ms
    uint16_t DataSize{0};
    uint16_t Reserved{0};
    void*    Data{nullptr};
    IP_OPTION_INFORMATION Options{};
};

#pragma pack(pop)

// ----------------------------------------------------------------------------
// IP Helper Implementations
// ----------------------------------------------------------------------------

inline uint32_t GetAdaptersInfo(IP_ADAPTER_INFO* pAdapterInfo, uint32_t* pOutBufLen) noexcept {
    if (!pOutBufLen) return ERROR_INVALID_PARAMETER;

    uint32_t requiredSize = sizeof(IP_ADAPTER_INFO);
    if (!pAdapterInfo || *pOutBufLen < requiredSize) {
        *pOutBufLen = requiredSize;
        return ERROR_BUFFER_OVERFLOW;
    }

    auto& net = tcpip::NetworkStack::get();
    auto adapter = net.getAdapter();

    std::memset(pAdapterInfo, 0, sizeof(IP_ADAPTER_INFO));
    pAdapterInfo->next = nullptr;
    pAdapterInfo->comboIndex = 1;
    pAdapterInfo->index = 1;
    pAdapterInfo->type = MIB_IF_TYPE_ETHERNET;
    pAdapterInfo->addressLength = 6;

    if (adapter) {
        auto mac = adapter->getMacAddress();
        std::memcpy(pAdapterInfo->address, mac.bytes, 6);
        std::string name = "{4D36E972-E325-11CE-BFC1-08002BE10318}";
        std::strncpy(pAdapterInfo->adapterName, name.c_str(), sizeof(pAdapterInfo->adapterName) - 1);
        auto fn = adapter->getFriendlyName();
        std::string desc(fn.begin(), fn.end());
        std::strncpy(pAdapterInfo->description, desc.c_str(), sizeof(pAdapterInfo->description) - 1);
    }

    // IP Address List
    std::string ipStr = net.getLocalIp().toString();
    std::string maskStr = net.getSubnetMask().toString();
    std::string gwStr = net.getGateway().toString();

    std::strncpy(pAdapterInfo->ipAddressList.ipAddress.str, ipStr.c_str(), 15);
    std::strncpy(pAdapterInfo->ipAddressList.ipMask.str, maskStr.c_str(), 15);
    std::strncpy(pAdapterInfo->gatewayList.ipAddress.str, gwStr.c_str(), 15);

    *pOutBufLen = requiredSize;
    return ERROR_SUCCESS;
}

inline uint32_t GetNetworkParams(FIXED_INFO* pFixedInfo, uint32_t* pOutBufLen) noexcept {
    if (!pOutBufLen) return ERROR_INVALID_PARAMETER;

    uint32_t requiredSize = sizeof(FIXED_INFO);
    if (!pFixedInfo || *pOutBufLen < requiredSize) {
        *pOutBufLen = requiredSize;
        return ERROR_BUFFER_OVERFLOW;
    }

    auto& net = tcpip::NetworkStack::get();

    std::memset(pFixedInfo, 0, sizeof(FIXED_INFO));
    std::strncpy(pFixedInfo->hostName, "MicaNT-Server", sizeof(pFixedInfo->hostName) - 1);
    std::strncpy(pFixedInfo->domainName, "micant.internal", sizeof(pFixedInfo->domainName) - 1);
    pFixedInfo->nodeType = 1; // Broadcast node
    pFixedInfo->enableDns = 1;

    std::string dnsStr = net.getDnsServer().toString();
    std::strncpy(pFixedInfo->dnsServerList.ipAddress.str, dnsStr.c_str(), 15);

    *pOutBufLen = requiredSize;
    return ERROR_SUCCESS;
}

inline uint32_t WINAPI GetAdaptersAddresses(
    uint32_t /*Family*/,
    uint32_t /*Flags*/,
    void* /*Reserved*/,
    void* AdapterAddresses,
    uint32_t* SizePointer
) noexcept {
    if (!SizePointer) return ERROR_INVALID_PARAMETER;

    // Fixed payload layout:
    // [IP_ADAPTER_ADDRESSES_LH]
    // [IP_ADAPTER_UNICAST_ADDRESS_LH (v4)]
    // [sockaddr_in (v4)]
    // [IP_ADAPTER_DNS_SERVER_ADDRESS]
    // [sockaddr_in (DNS)]
    // [Wide Strings: FriendlyName, Description, DnsSuffix, AdapterName]
    constexpr uint32_t kRequiredSize = 1024;
    if (!AdapterAddresses || *SizePointer < kRequiredSize) {
        *SizePointer = kRequiredSize;
        return ERROR_BUFFER_OVERFLOW;
    }

    std::memset(AdapterAddresses, 0, *SizePointer);
    auto* pAddr = reinterpret_cast<IP_ADAPTER_ADDRESSES_LH*>(AdapterAddresses);
    pAddr->Length = sizeof(IP_ADAPTER_ADDRESSES_LH);
    pAddr->IfIndex = 1;
    pAddr->Ipv6IfIndex = 1;
    pAddr->IfType = MIB_IF_TYPE_ETHERNET;
    pAddr->OperStatus = IfOperStatusUp;
    pAddr->Mtu = 1500;
    pAddr->PhysicalAddressLength = 6;
    pAddr->Flags = IP_ADAPTER_IPV4_ENABLED | IP_ADAPTER_IPV6_ENABLED;

    auto& net = tcpip::NetworkStack::get();
    auto adapter = net.getAdapter();
    if (adapter) {
        auto mac = adapter->getMacAddress();
        std::memcpy(pAddr->PhysicalAddress, mac.bytes, 6);
        pAddr->TransmitLinkSpeed = adapter->getSpeedBps();
        pAddr->ReceiveLinkSpeed = adapter->getSpeedBps();
    } else {
        uint8_t defaultMac[6] = {0x00, 0x1B, 0x21, 0x99, 0x88, 0x77};
        std::memcpy(pAddr->PhysicalAddress, defaultMac, 6);
        pAddr->TransmitLinkSpeed = 1000000000ULL;
        pAddr->ReceiveLinkSpeed = 1000000000ULL;
    }

    uint8_t* cur = reinterpret_cast<uint8_t*>(AdapterAddresses) + sizeof(IP_ADAPTER_ADDRESSES_LH);

    // 1. Unicast Address (IPv4)
    auto* uAddr = reinterpret_cast<IP_ADAPTER_UNICAST_ADDRESS_LH*>(cur);
    cur += sizeof(IP_ADAPTER_UNICAST_ADDRESS_LH);
    pAddr->FirstUnicastAddress = uAddr;
    uAddr->Length = sizeof(IP_ADAPTER_UNICAST_ADDRESS_LH);
    uAddr->OnLinkPrefixLength = 24;

    struct InternalSockaddrIn {
        uint16_t sin_family{2}; // AF_INET
        uint16_t sin_port{0};
        uint32_t sin_addr{0};
        uint8_t  sin_zero[8]{0};
    };

    auto* sIn = reinterpret_cast<InternalSockaddrIn*>(cur);
    cur += sizeof(InternalSockaddrIn);
    sIn->sin_family = 2; // AF_INET
    sIn->sin_addr = net.getLocalIp().addr;
    uAddr->Address.lpSockaddr = sIn;
    uAddr->Address.iSockaddrLength = sizeof(InternalSockaddrIn);

    // 2. DNS Server Address
    auto* dnsAddr = reinterpret_cast<IP_ADAPTER_DNS_SERVER_ADDRESS*>(cur);
    cur += sizeof(IP_ADAPTER_DNS_SERVER_ADDRESS);
    pAddr->FirstDnsServerAddress = dnsAddr;
    dnsAddr->Length = sizeof(IP_ADAPTER_DNS_SERVER_ADDRESS);

    auto* sDns = reinterpret_cast<InternalSockaddrIn*>(cur);
    cur += sizeof(InternalSockaddrIn);
    sDns->sin_family = 2;
    sDns->sin_addr = net.getDnsServer().addr;
    dnsAddr->Address.lpSockaddr = sDns;
    dnsAddr->Address.iSockaddrLength = sizeof(InternalSockaddrIn);

    // 3. String tables
    static const char kAdapterGuid[] = "{4D36E972-E325-11CE-BFC1-08002BE10318}";
    pAddr->AdapterName = reinterpret_cast<char*>(cur);
    std::memcpy(cur, kAdapterGuid, sizeof(kAdapterGuid));
    cur += sizeof(kAdapterGuid);

    uintptr_t curAddr = reinterpret_cast<uintptr_t>(cur);
    curAddr = (curAddr + alignof(wchar_t) - 1) & ~(alignof(wchar_t) - 1);
    cur = reinterpret_cast<uint8_t*>(curAddr);

    static const wchar_t kFriendly[] = L"Ethernet 0";
    pAddr->FriendlyName = reinterpret_cast<wchar_t*>(cur);
    std::memcpy(cur, kFriendly, sizeof(kFriendly));
    cur += sizeof(kFriendly);

    static const wchar_t kDesc[] = L"Intel(R) I210 Gigabit Network Connection";
    pAddr->Description = reinterpret_cast<wchar_t*>(cur);
    std::memcpy(cur, kDesc, sizeof(kDesc));
    cur += sizeof(kDesc);

    static const wchar_t kSuffix[] = L"micant.internal";
    pAddr->DnsSuffix = reinterpret_cast<wchar_t*>(cur);
    std::memcpy(cur, kSuffix, sizeof(kSuffix));

    return ERROR_SUCCESS;
}

inline uint32_t WINAPI GetIpForwardTable(
    MIB_IPFORWARDTABLE* pIpForwardTable,
    uint32_t* pdwSize,
    int /*bOrder*/
) noexcept {
    if (!pdwSize) return ERROR_INVALID_PARAMETER;
    uint32_t reqSize = sizeof(MIB_IPFORWARDTABLE);
    if (!pIpForwardTable || *pdwSize < reqSize) {
        *pdwSize = reqSize;
        return ERROR_BUFFER_OVERFLOW;
    }

    auto& net = tcpip::NetworkStack::get();
    std::memset(pIpForwardTable, 0, sizeof(MIB_IPFORWARDTABLE));

    // Entry 0: Default Gateway (0.0.0.0 / 0.0.0.0 -> Gateway IP)
    pIpForwardTable->table[0].dwForwardDest = 0;
    pIpForwardTable->table[0].dwForwardMask = 0;
    pIpForwardTable->table[0].dwForwardNextHop = net.getGateway().addr;
    pIpForwardTable->table[0].dwForwardIfIndex = 1;
    pIpForwardTable->table[0].dwForwardType = MIB_IPROUTE_TYPE_INDIRECT;
    pIpForwardTable->table[0].dwForwardProto = 3;
    pIpForwardTable->table[0].dwForwardMetric1 = 10;

    // Entry 1: Local Subnet Route (e.g. 147.135.1.0 / 255.255.255.0 -> On-link)
    uint32_t subnetNet = net.getLocalIp().addr & net.getSubnetMask().addr;
    pIpForwardTable->table[1].dwForwardDest = subnetNet;
    pIpForwardTable->table[1].dwForwardMask = net.getSubnetMask().addr;
    pIpForwardTable->table[1].dwForwardNextHop = net.getLocalIp().addr;
    pIpForwardTable->table[1].dwForwardIfIndex = 1;
    pIpForwardTable->table[1].dwForwardType = MIB_IPROUTE_TYPE_DIRECT;
    pIpForwardTable->table[1].dwForwardProto = 3;
    pIpForwardTable->table[1].dwForwardMetric1 = 256;

    // Entry 2: Loopback Route (127.0.0.0 / 255.0.0.0)
    pIpForwardTable->table[2].dwForwardDest = 0x0000007F; // 127.0.0.0
    pIpForwardTable->table[2].dwForwardMask = 0x000000FF; // 255.0.0.0
    pIpForwardTable->table[2].dwForwardNextHop = 0x0100007F; // 127.0.0.1
    pIpForwardTable->table[2].dwForwardIfIndex = 1;
    pIpForwardTable->table[2].dwForwardType = MIB_IPROUTE_TYPE_DIRECT;
    pIpForwardTable->table[2].dwForwardProto = 3;
    pIpForwardTable->table[2].dwForwardMetric1 = 1;

    pIpForwardTable->dwNumEntries = 3;
    *pdwSize = reqSize;
    return ERROR_SUCCESS;
}

inline uint32_t WINAPI GetIpNetTable(
    MIB_IPNETTABLE* pIpNetTable,
    uint32_t* pdwSize,
    int /*bOrder*/
) noexcept {
    if (!pdwSize) return ERROR_INVALID_PARAMETER;
    uint32_t reqSize = sizeof(MIB_IPNETTABLE);
    if (!pIpNetTable || *pdwSize < reqSize) {
        *pdwSize = reqSize;
        return ERROR_BUFFER_OVERFLOW;
    }

    std::memset(pIpNetTable, 0, sizeof(MIB_IPNETTABLE));
    auto& net = tcpip::NetworkStack::get();

    // Populate entries from NetworkStack ARP cache
    uint32_t count = 0;
    ndis::MacAddress resolved;

    // 1. Gateway
    if (net.lookupArp(net.getGateway(), resolved)) {
        pIpNetTable->table[count].dwIndex = 1;
        pIpNetTable->table[count].dwPhysAddrLen = 6;
        std::memcpy(pIpNetTable->table[count].bPhysAddr, resolved.bytes, 6);
        pIpNetTable->table[count].dwAddr = net.getGateway().addr;
        pIpNetTable->table[count].dwType = MIB_IPNET_TYPE_DYNAMIC;
        count++;
    }

    // 2. Broadcast
    pIpNetTable->table[count].dwIndex = 1;
    pIpNetTable->table[count].dwPhysAddrLen = 6;
    std::memset(pIpNetTable->table[count].bPhysAddr, 0xFF, 6);
    pIpNetTable->table[count].dwAddr = 0xFFFFFFFF;
    pIpNetTable->table[count].dwType = MIB_IPNET_TYPE_STATIC;
    count++;

    // 3. Local IP
    pIpNetTable->table[count].dwIndex = 1;
    pIpNetTable->table[count].dwPhysAddrLen = 6;
    auto myMac = net.getMacAddress();
    std::memcpy(pIpNetTable->table[count].bPhysAddr, myMac.bytes, 6);
    pIpNetTable->table[count].dwAddr = net.getLocalIp().addr;
    pIpNetTable->table[count].dwType = MIB_IPNET_TYPE_STATIC;
    count++;

    pIpNetTable->dwNumEntries = count;
    *pdwSize = reqSize;
    return ERROR_SUCCESS;
}

inline uint32_t WINAPI GetTcpTable2(
    MIB_TCPTABLE2* pTcpTable,
    uint32_t* pdwSize,
    int /*bOrder*/
) noexcept {
    if (!pdwSize) return ERROR_INVALID_PARAMETER;
    uint32_t reqSize = sizeof(MIB_TCPTABLE2);
    if (!pTcpTable || *pdwSize < reqSize) {
        *pdwSize = reqSize;
        return ERROR_BUFFER_OVERFLOW;
    }

    std::memset(pTcpTable, 0, sizeof(MIB_TCPTABLE2));
    auto& net = tcpip::NetworkStack::get();
    auto endpoints = net.getEndpoints();

    uint32_t count = 0;
    for (const auto& ep : endpoints) {
        if (count >= 64) break;
        pTcpTable->table[count].dwLocalAddr = ep.localIp.addr;
        pTcpTable->table[count].dwLocalPort = tcpip::htons(ep.localPort);
        pTcpTable->table[count].dwRemoteAddr = ep.remoteIp.addr;
        pTcpTable->table[count].dwRemotePort = tcpip::htons(ep.remotePort);
        pTcpTable->table[count].dwOwningPid = 4; // System / Executive

        if (ep.tcpState == tcpip::TcpState::Listen) pTcpTable->table[count].dwState = MIB_TCP_STATE_LISTEN;
        else if (ep.tcpState == tcpip::TcpState::Established) pTcpTable->table[count].dwState = MIB_TCP_STATE_ESTAB;
        else if (ep.tcpState == tcpip::TcpState::SynSent) pTcpTable->table[count].dwState = MIB_TCP_STATE_SYN_SENT;
        else if (ep.tcpState == tcpip::TcpState::CloseWait) pTcpTable->table[count].dwState = MIB_TCP_STATE_CLOSE_WAIT;
        else if (ep.tcpState == tcpip::TcpState::TimeWait) pTcpTable->table[count].dwState = MIB_TCP_STATE_TIME_WAIT;
        else pTcpTable->table[count].dwState = MIB_TCP_STATE_CLOSED;

        count++;
    }

    pTcpTable->dwNumEntries = count;
    *pdwSize = reqSize;
    return ERROR_SUCCESS;
}

inline uint32_t WINAPI GetUdpTable(
    MIB_UDPTABLE* pUdpTable,
    uint32_t* pdwSize,
    int /*bOrder*/
) noexcept {
    if (!pdwSize) return ERROR_INVALID_PARAMETER;
    uint32_t reqSize = sizeof(MIB_UDPTABLE);
    if (!pUdpTable || *pdwSize < reqSize) {
        *pdwSize = reqSize;
        return ERROR_BUFFER_OVERFLOW;
    }

    std::memset(pUdpTable, 0, sizeof(MIB_UDPTABLE));
    auto& net = tcpip::NetworkStack::get();

    // Default UDP endpoints: DHCP Client (68), DNS Client (53)
    pUdpTable->table[0].dwLocalAddr = 0;
    pUdpTable->table[0].dwLocalPort = tcpip::htons(68);
    pUdpTable->table[1].dwLocalAddr = net.getLocalIp().addr;
    pUdpTable->table[1].dwLocalPort = tcpip::htons(5353);
    pUdpTable->dwNumEntries = 2;

    *pdwSize = reqSize;
    return ERROR_SUCCESS;
}

// ----------------------------------------------------------------------------
// ICMP Echo API (icmp.dll / iphlpapi.dll)
// ----------------------------------------------------------------------------

inline HANDLE WINAPI IcmpCreateFile() noexcept {
    return reinterpret_cast<HANDLE>(0x1C3F0001); // Handle marker for ICMP session
}

inline BOOL WINAPI IcmpCloseHandle(HANDLE IcmpHandle) noexcept {
    return (IcmpHandle != nullptr) ? 1 : 0;
}

inline uint32_t WINAPI IcmpSendEcho(
    HANDLE IcmpHandle,
    uint32_t DestinationAddress,
    const void* RequestData,
    uint16_t RequestSize,
    const IP_OPTION_INFORMATION* RequestOptions,
    void* ReplyBuffer,
    uint32_t ReplySize,
    uint32_t TimeoutMs
) noexcept {
    if (!IcmpHandle || !ReplyBuffer || ReplySize < sizeof(ICMP_ECHO_REPLY)) {
        return 0;
    }

    auto& net = tcpip::NetworkStack::get();
    tcpip::Ipv4Address targetIp(DestinationAddress);

    auto pingRes = net.ping(targetIp, TimeoutMs);
    if (!pingRes.success) return 0;

    auto* reply = reinterpret_cast<ICMP_ECHO_REPLY*>(ReplyBuffer);
    std::memset(reply, 0, sizeof(ICMP_ECHO_REPLY));
    reply->Address = DestinationAddress;
    reply->Status = IP_SUCCESS;
    reply->RoundTripTime = pingRes.rttMs;
    reply->DataSize = RequestSize;
    reply->Options.Ttl = RequestOptions ? RequestOptions->Ttl : 64;

    uint8_t* pPayload = reinterpret_cast<uint8_t*>(ReplyBuffer) + sizeof(ICMP_ECHO_REPLY);
    if (ReplySize >= sizeof(ICMP_ECHO_REPLY) + RequestSize && RequestData && RequestSize > 0) {
        std::memcpy(pPayload, RequestData, RequestSize);
        reply->Data = pPayload;
    }

    return 1; // 1 reply received
}

// ----------------------------------------------------------------------------
// Network Interface Name / Index Conversions
// ----------------------------------------------------------------------------

struct NET_LUID {
    uint64_t Value;
};
using PNET_LUID = NET_LUID*;
using NETIO_STATUS = uint32_t;

inline NETIO_STATUS ConvertInterfaceGuidToLuid(const GUID* InterfaceGuid, PNET_LUID InterfaceLuid) noexcept {
    if (!InterfaceGuid || !InterfaceLuid) return 87; // ERROR_INVALID_PARAMETER
    InterfaceLuid->Value = (static_cast<uint64_t>(InterfaceGuid->Data1)) | 0x0100000000000000ULL;
    return 0; // NO_ERROR
}

inline NETIO_STATUS ConvertInterfaceLuidToAlias(const NET_LUID* InterfaceLuid, wchar_t* InterfaceAlias, size_t Length) noexcept {
    if (!InterfaceLuid || !InterfaceAlias || Length < 11) return 87;
    std::swprintf(InterfaceAlias, Length, L"Ethernet 0");
    return 0;
}

inline uint32_t __stdcall if_nametoindex(const char* /*InterfaceName*/) noexcept {
    return 1;
}

inline char* __stdcall if_indextoname(uint32_t /*InterfaceIndex*/, char* InterfaceName) noexcept {
    if (InterfaceName) {
        std::strcpy(InterfaceName, "eth0");
    }
    return InterfaceName;
}

inline uint32_t WINAPI ConvertInterfaceNameToLuidW(const wchar_t* /*InterfaceName*/, void* InterfaceLuid) noexcept {
    if (InterfaceLuid) *reinterpret_cast<uint64_t*>(InterfaceLuid) = 0x10001;
    return 0;
}

inline uint32_t WINAPI ConvertInterfaceIndexToLuid(uint32_t /*InterfaceIndex*/, void* InterfaceLuid) noexcept {
    if (InterfaceLuid) *reinterpret_cast<uint64_t*>(InterfaceLuid) = 0x10001;
    return 0;
}

inline uint32_t WINAPI ConvertInterfaceLuidToIndex(const void* /*InterfaceLuid*/, uint32_t* InterfaceIndex) noexcept {
    if (InterfaceIndex) *InterfaceIndex = 1;
    return 0;
}

inline uint32_t WINAPI ConvertInterfaceLuidToGuid(const void* /*InterfaceLuid*/, void* InterfaceGuid) noexcept {
    if (InterfaceGuid) std::memset(InterfaceGuid, 0, 16);
    return 0;
}

inline uint32_t WINAPI ConvertInterfaceLuidToNameW(const void* /*InterfaceLuid*/, wchar_t* InterfaceName, size_t Length) noexcept {
    if (InterfaceName && Length >= 11) std::memcpy(InterfaceName, L"Ethernet 0", 11 * sizeof(wchar_t));
    return 0;
}

inline uint32_t WINAPI NotifyUnicastIpAddressChange(uint16_t /*Family*/, void* /*Callback*/, void* /*CallerContext*/, uint8_t /*InitialNotification*/, void** NotificationHandle) noexcept {
    if (NotificationHandle) *NotificationHandle = reinterpret_cast<void*>(0x5001);
    return 0;
}

inline uint32_t WINAPI CancelMibChangeNotify2(void* /*NotificationHandle*/) noexcept {
    return 0;
}

inline void InitializeIpHlpApiSubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("iphlpapi.dll", "GetAdaptersInfo", reinterpret_cast<void*>(GetAdaptersInfo));
    ldr.registerExport("iphlpapi.dll", "GetNetworkParams", reinterpret_cast<void*>(GetNetworkParams));
    ldr.registerExport("iphlpapi.dll", "GetAdaptersAddresses", reinterpret_cast<void*>(GetAdaptersAddresses));
    ldr.registerExport("iphlpapi.dll", "GetIpForwardTable", reinterpret_cast<void*>(GetIpForwardTable));
    ldr.registerExport("iphlpapi.dll", "GetIpNetTable", reinterpret_cast<void*>(GetIpNetTable));
    ldr.registerExport("iphlpapi.dll", "GetTcpTable2", reinterpret_cast<void*>(GetTcpTable2));
    ldr.registerExport("iphlpapi.dll", "GetUdpTable", reinterpret_cast<void*>(GetUdpTable));
    ldr.registerExport("iphlpapi.dll", "ConvertInterfaceGuidToLuid", reinterpret_cast<void*>(ConvertInterfaceGuidToLuid));
    ldr.registerExport("iphlpapi.dll", "ConvertInterfaceLuidToAlias", reinterpret_cast<void*>(ConvertInterfaceLuidToAlias));
    ldr.registerExport("iphlpapi.dll", "if_nametoindex", reinterpret_cast<void*>(if_nametoindex));
    ldr.registerExport("iphlpapi.dll", "if_indextoname", reinterpret_cast<void*>(if_indextoname));
    ldr.registerExport("iphlpapi.dll", "ConvertInterfaceNameToLuidW", reinterpret_cast<void*>(ConvertInterfaceNameToLuidW));
    ldr.registerExport("iphlpapi.dll", "ConvertInterfaceIndexToLuid", reinterpret_cast<void*>(ConvertInterfaceIndexToLuid));
    ldr.registerExport("iphlpapi.dll", "ConvertInterfaceLuidToIndex", reinterpret_cast<void*>(ConvertInterfaceLuidToIndex));
    ldr.registerExport("iphlpapi.dll", "ConvertInterfaceLuidToGuid", reinterpret_cast<void*>(ConvertInterfaceLuidToGuid));
    ldr.registerExport("iphlpapi.dll", "ConvertInterfaceLuidToNameW", reinterpret_cast<void*>(ConvertInterfaceLuidToNameW));
    ldr.registerExport("iphlpapi.dll", "NotifyUnicastIpAddressChange", reinterpret_cast<void*>(NotifyUnicastIpAddressChange));
    ldr.registerExport("iphlpapi.dll", "CancelMibChangeNotify2", reinterpret_cast<void*>(CancelMibChangeNotify2));

    // ICMP exports
    ldr.registerExport("iphlpapi.dll", "IcmpCreateFile", reinterpret_cast<void*>(IcmpCreateFile));
    ldr.registerExport("iphlpapi.dll", "IcmpSendEcho", reinterpret_cast<void*>(IcmpSendEcho));
    ldr.registerExport("iphlpapi.dll", "IcmpCloseHandle", reinterpret_cast<void*>(IcmpCloseHandle));

    ldr.registerExport("icmp.dll", "IcmpCreateFile", reinterpret_cast<void*>(IcmpCreateFile));
    ldr.registerExport("icmp.dll", "IcmpSendEcho", reinterpret_cast<void*>(IcmpSendEcho));
    ldr.registerExport("icmp.dll", "IcmpCloseHandle", reinterpret_cast<void*>(IcmpCloseHandle));

    // Version database registration
    auto& verDb = version::VersionDatabase::Instance();
    verDb.RegisterModule("iphlpapi.dll", "10.0.26100.5900", "MicaNT IP Helper API");
    verDb.RegisterModule("icmp.dll", "10.0.26100.5900", "MicaNT ICMP Echo API");
}

inline void InitializeIpHelperApi() {
    InitializeIpHlpApiSubsystemExports();
}

} // namespace micant::iphlpapi
