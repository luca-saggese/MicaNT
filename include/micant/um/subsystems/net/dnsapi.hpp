#pragma once

/**
 * @file dnsapi.hpp
 * @brief Clean-Room DNS Client API (dnsapi.dll) & RFC 1035 Resolver Subsystem.
 *
 * Implements RFC 1035 Domain Name System message encoding/decoding, in-memory
 * dynamic DNS resolution cache, and standard Windows NT C ABI exports:
 * - DnsQuery_W / DnsQuery_A
 * - DnsRecordListFree
 * - DnsFlushResolverCache
 * - DnsGetCacheDataTable
 *
 * References: RFC 1035, RFC 3596, 17 U.S.C. § 1201(f).
 */

#include <cstdint>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <memory>
#include <new>
#include <sstream>
#include <span>
#include "micant/shared/ntdef.hpp"
#if !defined(MICANT_NATIVE_USERMODE)
#include "micant/um/native/ldr.hpp"
#include "micant/um/servers/scm.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#endif
#include "micant/um/subsystems/net/ws2_32.hpp"

namespace micant::dnsapi {

[[nodiscard]] constexpr uint16_t dnsByteSwap16(uint16_t value) noexcept {
    return static_cast<uint16_t>((value << 8) | (value >> 8));
}

struct DnsIpv4Address {
    uint32_t addr{};
    explicit DnsIpv4Address(uint32_t value = 0) noexcept : addr(value) {}
    [[nodiscard]] bool isZero() const noexcept { return addr == 0; }
    [[nodiscard]] static bool parse(std::string_view name, uint32_t& address) noexcept {
        uint32_t result = 0;
        size_t cursor = 0;
        for (unsigned part = 0; part < 4; ++part) {
            unsigned value = 0, digits = 0;
            while (cursor < name.size() && name[cursor] >= '0' && name[cursor] <= '9') {
                if (++digits > 3) return false;
                value = value * 10 + static_cast<unsigned>(name[cursor++] - '0');
            }
            if (!digits || value > 255) return false;
            result |= static_cast<uint32_t>(value) << (part * 8);
            if (part < 3) {
                if (cursor == name.size() || name[cursor++] != '.') return false;
            }
        }
        if (cursor != name.size()) return false;
        address = result;
        return true;
    }
};
struct DnsIpv6Address { uint8_t bytes[16]{}; };

// DNS Record Types (RFC 1035 / RFC 3596)
inline constexpr uint16_t DNS_TYPE_A     = 0x0001;
inline constexpr uint16_t DNS_TYPE_NS    = 0x0002;
inline constexpr uint16_t DNS_TYPE_CNAME = 0x0005;
inline constexpr uint16_t DNS_TYPE_SOA   = 0x0006;
inline constexpr uint16_t DNS_TYPE_PTR   = 0x000C;
inline constexpr uint16_t DNS_TYPE_MX    = 0x000F;
inline constexpr uint16_t DNS_TYPE_TXT   = 0x0010;
inline constexpr uint16_t DNS_TYPE_AAAA  = 0x001C;
inline constexpr uint16_t DNS_TYPE_SRV   = 0x0021;
inline constexpr uint16_t DNS_TYPE_ANY   = 0x00FF;

// DNS Classes
inline constexpr uint16_t DNS_CLASS_IN   = 0x0001; // Internet

// DNS Response Codes (RCODE)
inline constexpr uint8_t DNS_RCODE_NOERROR  = 0;
inline constexpr uint8_t DNS_RCODE_FORMERR  = 1;
inline constexpr uint8_t DNS_RCODE_SERVFAIL = 2;
inline constexpr uint8_t DNS_RCODE_NXDOMAIN = 3;
inline constexpr uint8_t DNS_RCODE_NOTIMP   = 4;
inline constexpr uint8_t DNS_RCODE_REFUSED  = 5;

// Status codes
inline constexpr uint32_t DNS_STATUS_SUCCESS   = 0;
inline constexpr uint32_t DNS_ERROR_RCODE_NAME_ERROR = 9003;
inline constexpr uint32_t DNS_ERROR_RCODE_SERVER_FAILURE = 9002;
inline constexpr uint32_t DNS_ERROR_TIMEOUT = 1460;
inline constexpr uint32_t DNS_ERROR_INVALID_NAME     = 9005;
inline constexpr uint32_t DNS_ERROR_NO_MEMORY        = 14;
inline constexpr uint32_t DNS_ERROR_NOT_SUPPORTED    = 50;
inline constexpr uint32_t DNS_ERROR_INVALID_PARAMETER = 87;

#pragma pack(push, 2)

/**
 * @brief 12-byte RFC 1035 DNS Message Header.
 */
struct DnsHeader {
    uint16_t id{0};
    uint16_t flags{0};
    uint16_t qdcount{0};
    uint16_t ancount{0};
    uint16_t nscount{0};
    uint16_t arcount{0};

    [[nodiscard]] bool isQuery() const noexcept { return (dnsByteSwap16(flags) & 0x8000) == 0; }
    [[nodiscard]] bool isResponse() const noexcept { return (dnsByteSwap16(flags) & 0x8000) != 0; }
    [[nodiscard]] uint8_t getRcode() const noexcept { return static_cast<uint8_t>(dnsByteSwap16(flags) & 0x000F); }

    void setQuery(bool recursionDesired = true) noexcept {
        uint16_t f = 0x0000; // QR=0
        if (recursionDesired) f |= 0x0100; // RD=1
        flags = dnsByteSwap16(f);
    }

    void setResponse(uint8_t rcode = 0, bool authoritative = false) noexcept {
        uint16_t f = 0x8000; // QR=1
        f |= 0x0100;         // RD=1
        f |= 0x0080;         // RA=1
        if (authoritative) f |= 0x0400; // AA=1
        f |= (rcode & 0x000F);
        flags = dnsByteSwap16(f);
    }
};

#pragma pack(pop)

// Windows NT DNS_RECORD Structure
#pragma pack(push, 4)

struct DNS_A_DATA {
    uint32_t IpAddress{0};
};

struct DNS_AAAA_DATA {
    uint8_t Ip6Address[16]{0};
};

struct DNS_PTR_DATAW {
    wchar_t* pNameHost{nullptr};
};

struct DNS_RECORD_FLAGS {
    uint32_t Section : 2;
    uint32_t Delete : 1;
    uint32_t CharSet : 2;
    uint32_t Unused : 3;
    uint32_t Reserved : 24;
};

struct DNS_RECORDW {
    DNS_RECORDW* pNext{nullptr};
    const wchar_t* pName{nullptr};
    uint16_t wType{DNS_TYPE_A};
    uint16_t wDataLength{sizeof(DNS_A_DATA)};
    DNS_RECORD_FLAGS Flags{};
    uint32_t dwTtl{300};
    uint32_t dwReserved{0};
    union {
        DNS_A_DATA    A;
        DNS_AAAA_DATA AAAA;
        DNS_PTR_DATAW PTR;
    } Data{};
};

struct DNS_RECORDA {
    DNS_RECORDA* pNext{nullptr};
    const char* pName{nullptr};
    uint16_t wType{DNS_TYPE_A};
    uint16_t wDataLength{sizeof(DNS_A_DATA)};
    DNS_RECORD_FLAGS Flags{};
    uint32_t dwTtl{300};
    uint32_t dwReserved{0};
    union { DNS_A_DATA A; DNS_AAAA_DATA AAAA; } Data{};
};

#pragma pack(pop)

/**
 * @brief DNS Resolver Cache Entry.
 */
struct DnsCacheEntry {
    std::string name;
    uint16_t type{DNS_TYPE_A};
    std::vector<uint8_t> rdata;
    uint32_t ttlSeconds{300};
    std::chrono::steady_clock::time_point expiresAt;
    bool isStatic{false};

    [[nodiscard]] bool isExpired() const noexcept {
        if (isStatic) return false;
        return std::chrono::steady_clock::now() > expiresAt;
    }
};

/**
 * @brief Helper to encode RFC 1035 length-prefixed domain labels.
 * E.g. "www.google.com" -> "\x03www\x06google\x03com\x00"
 */
inline std::vector<uint8_t> EncodeDnsName(std::string_view name) {
    std::vector<uint8_t> encoded;
    if (name == ".") return {0};
    if (name.empty() || name.size() > 254 ||
        (name.size() == 254 && name.back() != '.')) return encoded;
    encoded.reserve(name.size() + 2);

    size_t start = 0;
    while (start < name.size()) {
        size_t dot = name.find('.', start);
        if (dot == std::string_view::npos) dot = name.size();
        size_t len = dot - start;
        if (len == 0 || len > 63) return {};
        encoded.push_back(static_cast<uint8_t>(len));
        encoded.insert(encoded.end(), name.begin() + start, name.begin() + start + len);
        start = dot + 1;
    }
    encoded.push_back(0); // Null label terminator
    return encoded;
}

/**
 * @brief Helper to decode RFC 1035 domain labels with compression pointer support.
 */
inline bool DecodeDnsNameChecked(std::span<const uint8_t> packet, size_t& offset,
                                 std::string& result) {
    result.clear();
    size_t cur = offset;
    bool jumped = false;
    size_t nextOffset = 0;
    size_t depth = 0;
    size_t wireLength = 1;

    while (cur < packet.size() && depth++ < 256) {
        uint8_t len = packet[cur];
        if (len == 0) {
            cur++;
            offset = jumped ? nextOffset : cur;
            return true;
        }

        // Pointer: top two bits set (0xC0)
        if ((len & 0xC0) == 0xC0) {
            if (cur + 1 >= packet.size()) return false;
            uint16_t ptr = ((len & 0x3F) << 8) | packet[cur + 1];
            if (ptr >= cur) return false;
            if (!jumped) {
                nextOffset = cur + 2;
                jumped = true;
            }
            cur = ptr;
            continue;
        }

        if ((len & 0xC0) != 0 || len > 63 || wireLength + len + 1 > 255)
            return false;
        wireLength += len + 1;
        cur++;
        if (len > packet.size() - cur) return false;
        if (!result.empty()) result.push_back('.');
        result.append(reinterpret_cast<const char*>(&packet[cur]), len);
        cur += len;
    }

    return false;
}

inline std::string DecodeDnsName(std::span<const uint8_t> packet, size_t& offset) {
    std::string result;
    if (!DecodeDnsNameChecked(packet, offset, result)) return {};
    return result;
}

/**
 * @brief Clean-Room Kernel/Userland DNS Resolver Engine.
 */
class DnsResolverEngine {
public:
    static DnsResolverEngine& get() {
        static DnsResolverEngine instance;
        return instance;
    }

    void resetForTest() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        seedStaticEntries();
    }

    /**
     * @brief Adds or updates a resolved DNS record in the resolver cache.
     */
    void addCacheEntry(
        std::string_view name,
        uint16_t type,
        std::span<const uint8_t> rdata,
        uint32_t ttlSeconds = 300,
        bool isStatic = false
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        addCacheEntryLocked(name, type, rdata, ttlSeconds, isStatic);
    }

    void addCacheEntryLocked(
        std::string_view name,
        uint16_t type,
        std::span<const uint8_t> rdata,
        uint32_t ttlSeconds = 300,
        bool isStatic = false
    ) {
        std::string key = makeKey(name, type);
        DnsCacheEntry entry;
        entry.name = std::string(name);
        entry.type = type;
        entry.rdata.assign(rdata.begin(), rdata.end());
        entry.ttlSeconds = ttlSeconds;
        entry.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
        entry.isStatic = isStatic;
        cache_[key] = std::move(entry);
    }

    /**
     * @brief Resolves an IPv4 address for a hostname via cache or query.
     */
    template <typename Address>
    [[nodiscard]] bool resolveA(std::string_view host, Address& outIp) {
        std::lock_guard<std::mutex> lock(mutex_);
        pruneExpiredLocked();

        // 1. Direct IP literal check
        uint32_t literal{};
        if (DnsIpv4Address::parse(host, literal)) {
            outIp = Address(literal);
            return true;
        }

        std::string key = makeKey(host, DNS_TYPE_A);
        auto it = cache_.find(key);
        if (it != cache_.end() && !it->second.isExpired() && it->second.rdata.size() >= 4) {
            uint32_t addr = 0;
            std::memcpy(&addr, it->second.rdata.data(), 4);
            outIp = Address(addr);
            return true;
        }

        return false;
    }

    /**
     * @brief Resolves an IPv6 address for a hostname via cache.
     */
    template <typename Address>
    [[nodiscard]] bool resolveAaaa(std::string_view host, Address& outIp) {
        std::lock_guard<std::mutex> lock(mutex_);
        pruneExpiredLocked();

        std::string key = makeKey(host, DNS_TYPE_AAAA);
        auto it = cache_.find(key);
        if (it != cache_.end() && !it->second.isExpired() && it->second.rdata.size() >= 16) {
            std::memcpy(outIp.bytes, it->second.rdata.data(), 16);
            return true;
        }
        return false;
    }

    /**
     * @brief Flushes all dynamic resolver cache records.
     */
    uint32_t flushCache() {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t flushed = 0;
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (!it->second.isStatic) {
                it = cache_.erase(it);
                flushed++;
            } else {
                ++it;
            }
        }
        return flushed;
    }

    [[nodiscard]] size_t getCacheSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }

    [[nodiscard]] std::vector<DnsCacheEntry> getCacheEntries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DnsCacheEntry> entries;
        entries.reserve(cache_.size());
        for (const auto& [_, entry] : cache_) {
            if (!entry.isExpired()) {
                entries.push_back(entry);
            }
        }
        return entries;
    }

    /**
     * @brief Crafts an RFC 1035 UDP DNS Query Message.
     */
    static std::vector<uint8_t> buildQueryPacket(uint16_t xid, std::string_view name, uint16_t qtype) {
        std::vector<uint8_t> packet;
        packet.resize(sizeof(DnsHeader));

        auto* hdr = reinterpret_cast<DnsHeader*>(packet.data());
        hdr->id = dnsByteSwap16(xid);
        hdr->setQuery(true);
        hdr->qdcount = dnsByteSwap16(1);
        hdr->ancount = 0;
        hdr->nscount = 0;
        hdr->arcount = 0;

        auto qname = EncodeDnsName(name);
        if (qname.empty()) return {};
        packet.insert(packet.end(), qname.begin(), qname.end());

        uint16_t typeClass[2] = {dnsByteSwap16(qtype), dnsByteSwap16(DNS_CLASS_IN)};
        const auto* tcBytes = reinterpret_cast<const uint8_t*>(typeClass);
        packet.insert(packet.end(), tcBytes, tcBytes + sizeof(typeClass));

        return packet;
    }

    /**
     * @brief Parses an RFC 1035 DNS Response packet.
     */
    static bool parseResponsePacket(
        std::span<const uint8_t> packet,
        uint16_t& outXid,
        uint8_t& outRcode,
        std::vector<DnsCacheEntry>& outAnswers
    ) {
        if (packet.size() < sizeof(DnsHeader)) return false;
        if ((packet[2] & 0xF8u) != 0x80u || (packet[2] & 0x02u) != 0) return false;
        const uint16_t xid = (static_cast<uint16_t>(packet[0]) << 8) | packet[1];
        const uint8_t rcode = packet[3] & 0x0Fu;

        uint16_t qdcount = (static_cast<uint16_t>(packet[4]) << 8) | packet[5];
        uint16_t ancount = (static_cast<uint16_t>(packet[6]) << 8) | packet[7];

        size_t offset = sizeof(DnsHeader);
        std::vector<DnsCacheEntry> answers;

        // Skip question records
        for (uint16_t q = 0; q < qdcount; ++q) {
            std::string name;
            if (!DecodeDnsNameChecked(packet, offset, name) ||
                packet.size() - offset < 4) return false;
            offset += 4; // Skip QTYPE and QCLASS
        }

        // Parse answer records
        for (uint16_t a = 0; a < ancount; ++a) {
            std::string name;
            if (!DecodeDnsNameChecked(packet, offset, name) ||
                packet.size() - offset < 10) return false;

            uint16_t atype = (static_cast<uint16_t>(packet[offset]) << 8) | packet[offset + 1];
            uint16_t aclass = (static_cast<uint16_t>(packet[offset + 2]) << 8) | packet[offset + 3];
            uint32_t ttl = (static_cast<uint32_t>(packet[offset + 4]) << 24) |
                           (static_cast<uint32_t>(packet[offset + 5]) << 16) |
                           (static_cast<uint32_t>(packet[offset + 6]) << 8) |
                           packet[offset + 7];
            uint16_t rdlen = (static_cast<uint16_t>(packet[offset + 8]) << 8) | packet[offset + 9];
            offset += 10;

            if (rdlen > packet.size() - offset) return false;
            if (aclass != DNS_CLASS_IN ||
                (atype == DNS_TYPE_A && rdlen != 4) ||
                (atype == DNS_TYPE_AAAA && rdlen != 16)) return false;

            DnsCacheEntry entry;
            entry.name = name;
            entry.type = atype;
            entry.ttlSeconds = ttl;
            entry.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);
            entry.rdata.assign(packet.data() + offset, packet.data() + offset + rdlen);
            answers.push_back(std::move(entry));
            offset += rdlen;
        }

        for (auto& answer : answers) outAnswers.push_back(std::move(answer));
        outXid = xid;
        outRcode = rcode;
        return true;
    }

private:
    DnsResolverEngine() {
        std::lock_guard<std::mutex> lock(mutex_);
        seedStaticEntries();
    }

    void seedStaticEntries() {
        // localhost -> 127.0.0.1
        uint32_t loopbackV4 = 0x0100007F; // 127.0.0.1
        addCacheEntryLocked("localhost", DNS_TYPE_A, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&loopbackV4), 4), 86400, true);

        // localhost -> ::1
        uint8_t loopbackV6[16]{0};
        loopbackV6[15] = 1;
        addCacheEntryLocked("localhost", DNS_TYPE_AAAA, std::span<const uint8_t>(loopbackV6, 16), 86400, true);

    }

    static std::string makeKey(std::string_view name, uint16_t type) {
        std::string s;
        s.reserve(name.size() + 8);
        for (char c : name) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        s.push_back('#');
        s.append(std::to_string(type));
        return s;
    }

    void pruneExpiredLocked() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end(); ) {
            if (!it->second.isStatic && now > it->second.expiresAt) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, DnsCacheEntry> cache_;
};

// Perform a bounded UDP/IPv4 DNS query using the configured resolver from the
// canonical AFD network configuration.  A reply is accepted only when its
// source tuple, transaction ID, flags, question, and record bounds match.
struct DnsWireAnswer { std::string owner; uint16_t type{}; uint32_t ttl{}; std::vector<uint8_t> data; };

inline uint32_t queryDnsRecords(std::string_view name, uint16_t queryType,
                                std::vector<DnsWireAnswer>& output) {
    using namespace ws2_32;
    std::vector<uint8_t> encoded;
    std::vector<uint8_t> request;
    try {
        encoded = EncodeDnsName(name);
        if (encoded.empty() || encoded.size() > 255) return DNS_ERROR_INVALID_NAME;
        request.resize(12 + encoded.size() + 4);
    } catch (...) { return DNS_ERROR_NO_MEMORY; }

    SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) return DNS_ERROR_RCODE_SERVER_FAILURE;
    const auto closeSocket = [&]() noexcept { (void)closesocket(socketHandle); };
    micant::shared::afd::NetworkConfigurationV1 configuration{};
    if (queryNetworkConfiguration(socketHandle, configuration) != micant::NtStatus::Success ||
        !(configuration.Flags & micant::shared::afd::NetworkConfigurationDnsValid) ||
        configuration.DnsServerNetworkOrder == 0) {
        closeSocket();
        return DNS_ERROR_RCODE_SERVER_FAILURE;
    }
    const uint32_t timeout = 2500;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0) {
        closeSocket();
        return DNS_ERROR_RCODE_SERVER_FAILURE;
    }

    static std::atomic<uint64_t> querySequence{1};
    const uint64_t tick = querySequence.fetch_add(1, std::memory_order_relaxed);
    uint32_t hash = 2166136261u;
    for (unsigned char ch : name) hash = (hash ^ static_cast<uint8_t>(std::tolower(ch))) * 16777619u;
    const uint16_t id = static_cast<uint16_t>((tick ^ (tick >> 32) ^ hash) | 1u);
    auto put16 = [](uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v); };
    auto get16 = [](const uint8_t* p) { return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]); };
    auto get32 = [](const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]; };
    put16(request.data(), id);
    put16(request.data() + 2, 0x0100); // RD
    put16(request.data() + 4, 1);
    std::memcpy(request.data() + 12, encoded.data(), encoded.size());
    put16(request.data() + 12 + encoded.size(), queryType);
    put16(request.data() + 14 + encoded.size(), DNS_CLASS_IN);

    sockaddr_in resolver{};
    resolver.sin_family = AF_INET;
    resolver.sin_addr.S_un.S_addr = configuration.DnsServerNetworkOrder;
    resolver.sin_port = htons(53);
    if (sendto(socketHandle, reinterpret_cast<const char*>(request.data()),
               static_cast<int>(request.size()), 0, reinterpret_cast<const sockaddr*>(&resolver), sizeof(resolver)) == SOCKET_ERROR) {
        closeSocket();
        return DNS_ERROR_RCODE_SERVER_FAILURE;
    }
    std::array<uint8_t, 4096> response{};
    sockaddr_in source{};
    int sourceLength = sizeof(source);
    const int received = recvfrom(socketHandle, reinterpret_cast<char*>(response.data()), response.size(), 0,
                                  reinterpret_cast<sockaddr*>(&source), &sourceLength);
    closeSocket();
    if (received == SOCKET_ERROR) return WSAGetLastError() == WSAETIMEDOUT ? DNS_ERROR_TIMEOUT : DNS_ERROR_RCODE_SERVER_FAILURE;
    if (sourceLength != sizeof(source) || source.sin_family != AF_INET ||
        source.sin_addr.S_un.S_addr != resolver.sin_addr.S_un.S_addr || ntohs(source.sin_port) != 53 || received < 12)
        return DNS_ERROR_RCODE_SERVER_FAILURE;
    std::vector<uint8_t> tcpResponse;
    if ((get16(response.data() + 2) & 0x0200u) != 0) {
        SOCKET tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (tcp == INVALID_SOCKET) return DNS_ERROR_RCODE_SERVER_FAILURE;
        const uint32_t tcpTimeout = 5000;
        if (setsockopt(tcp, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tcpTimeout), sizeof(tcpTimeout)) != 0 ||
            connect(tcp, reinterpret_cast<const sockaddr*>(&resolver), sizeof(resolver)) != 0) {
            (void)closesocket(tcp); return DNS_ERROR_RCODE_SERVER_FAILURE;
        }
        std::vector<uint8_t> framed;
        try { framed.resize(request.size() + 2); tcpResponse.resize(65535); }
        catch (...) { (void)closesocket(tcp); return DNS_ERROR_NO_MEMORY; }
        put16(framed.data(), static_cast<uint16_t>(request.size()));
        std::memcpy(framed.data() + 2, request.data(), request.size());
        size_t sent = 0;
        while (sent < framed.size()) {
            const int n = send(tcp, reinterpret_cast<const char*>(framed.data() + sent), static_cast<int>(framed.size() - sent), 0);
            if (n <= 0) { (void)closesocket(tcp); return DNS_ERROR_RCODE_SERVER_FAILURE; }
            sent += static_cast<size_t>(n);
        }
        uint8_t lengthBytes[2]{}; size_t got = 0;
        while (got < sizeof(lengthBytes)) {
            const int n = recv(tcp, reinterpret_cast<char*>(lengthBytes + got), static_cast<int>(sizeof(lengthBytes) - got), 0);
            if (n <= 0) { (void)closesocket(tcp); return DNS_ERROR_RCODE_SERVER_FAILURE; }
            got += static_cast<size_t>(n);
        }
        const size_t messageLength = get16(lengthBytes);
        if (messageLength < 12 || messageLength > tcpResponse.size()) { (void)closesocket(tcp); return DNS_ERROR_RCODE_SERVER_FAILURE; }
        got = 0;
        while (got < messageLength) {
            const int n = recv(tcp, reinterpret_cast<char*>(tcpResponse.data() + got), static_cast<int>(messageLength - got), 0);
            if (n <= 0) { (void)closesocket(tcp); return DNS_ERROR_RCODE_SERVER_FAILURE; }
            got += static_cast<size_t>(n);
        }
        (void)closesocket(tcp);
        tcpResponse.resize(messageLength);
    }
    const auto packet = tcpResponse.empty()
        ? std::span<const uint8_t>(response.data(), static_cast<size_t>(received))
        : std::span<const uint8_t>(tcpResponse.data(), tcpResponse.size());
    if (get16(packet.data()) != id || (get16(packet.data() + 2) & 0x8000u) == 0 ||
        (get16(packet.data() + 2) & 0x7800u) != 0 || get16(packet.data() + 4) != 1)
        return DNS_ERROR_RCODE_SERVER_FAILURE;
    const uint8_t rcode = static_cast<uint8_t>(get16(packet.data() + 2) & 0x0fu);
    if (rcode == DNS_RCODE_NXDOMAIN) return DNS_ERROR_RCODE_NAME_ERROR;
    if (rcode != DNS_RCODE_NOERROR) return DNS_ERROR_RCODE_SERVER_FAILURE;
    size_t offset = 12;
    std::string question;
    if (!DecodeDnsNameChecked(packet, offset, question) || offset + 4 > packet.size() ||
        get16(packet.data() + offset) != queryType || get16(packet.data() + offset + 2) != DNS_CLASS_IN)
        return DNS_ERROR_RCODE_SERVER_FAILURE;
    offset += 4;
    auto equalDnsName = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        return true;
    };
    if (!equalDnsName(question, name)) return DNS_ERROR_RCODE_SERVER_FAILURE;
    const uint16_t answers = get16(packet.data() + 6);
    for (uint16_t i = 0; i < answers; ++i) {
        std::string owner;
        if (!DecodeDnsNameChecked(packet, offset, owner) || offset + 10 > packet.size()) return DNS_ERROR_RCODE_SERVER_FAILURE;
        const uint16_t type = get16(packet.data() + offset);
        const uint16_t klass = get16(packet.data() + offset + 2);
        const uint32_t recordTtl = get32(packet.data() + offset + 4);
        const uint16_t dataLength = get16(packet.data() + offset + 8);
        offset += 10;
        if (dataLength > packet.size() - offset) return DNS_ERROR_RCODE_SERVER_FAILURE;
        if (type == queryType && klass == DNS_CLASS_IN && equalDnsName(owner, name) &&
            ((queryType == DNS_TYPE_A && dataLength == 4) || (queryType == DNS_TYPE_AAAA && dataLength == 16))) {
            DnsWireAnswer answer;
            answer.owner = std::move(owner);
            answer.type = type;
            answer.ttl = recordTtl;
            answer.data.assign(packet.begin() + offset, packet.begin() + offset + dataLength);
            output.push_back(std::move(answer));
        }
        offset += dataLength;
    }
    return output.empty() ? DNS_ERROR_RCODE_NAME_ERROR : DNS_STATUS_SUCCESS;
}

// ----------------------------------------------------------------------------
// Win32 C ABI Exports for dnsapi.dll
// ----------------------------------------------------------------------------

inline void WINAPI DnsRecordListFreeA(DNS_RECORDA* record) noexcept;
inline void WINAPI DnsRecordListFree(void* record, uint32_t freeType) noexcept;

inline uint32_t WINAPI DnsQuery_A(
    const char* pszName,
    uint16_t wType,
    uint32_t Options,
    void* pExtra,
    DNS_RECORDA** ppQueryResults,
    void** pReserved
) noexcept {
    if (!pszName || pExtra || pReserved) return DNS_ERROR_INVALID_PARAMETER;
    if (!ppQueryResults) return DNS_ERROR_NOT_SUPPORTED;
    *ppQueryResults = nullptr;
    if (Options) return DNS_ERROR_NOT_SUPPORTED;
    if (!*pszName) return DNS_ERROR_INVALID_NAME;

    if (wType != DNS_TYPE_A && wType != DNS_TYPE_AAAA) return DNS_ERROR_NOT_SUPPORTED;
    try {
        auto& resolver = DnsResolverEngine::get();
        std::vector<DnsWireAnswer> answers;
        if (wType == DNS_TYPE_A) {
            DnsIpv4Address cached;
            if (resolver.resolveA(pszName, cached)) {
                DnsWireAnswer a; a.owner = pszName; a.type = wType; a.ttl = 300; a.data.resize(4);
                std::memcpy(a.data.data(), &cached.addr, 4); answers.push_back(std::move(a));
            }
        } else {
            DnsIpv6Address cached;
            if (resolver.resolveAaaa(pszName, cached)) {
                DnsWireAnswer a; a.owner = pszName; a.type = wType; a.ttl = 300; a.data.assign(cached.bytes, cached.bytes + 16); answers.push_back(std::move(a));
            }
        }
        if (answers.empty()) {
            const uint32_t status = queryDnsRecords(pszName, wType, answers);
            if (status != DNS_STATUS_SUCCESS) return status;
        }
        DNS_RECORDA* tail = nullptr;
        for (const auto& answer : answers) {
            auto* record = new (std::nothrow) DNS_RECORDA{};
            if (!record) { DnsRecordListFreeA(*ppQueryResults); *ppQueryResults = nullptr; return DNS_ERROR_NO_MEMORY; }
            auto* owner = new (std::nothrow) uint8_t[answer.owner.size() + 1];
            if (!owner) { delete record; DnsRecordListFreeA(*ppQueryResults); *ppQueryResults = nullptr; return DNS_ERROR_NO_MEMORY; }
            std::memcpy(owner, answer.owner.c_str(), answer.owner.size() + 1);
            record->pName = reinterpret_cast<const char*>(owner); record->wType = answer.type; record->dwTtl = answer.ttl;
            if (answer.type == DNS_TYPE_A && answer.data.size() == 4) {
                record->wDataLength = sizeof(DNS_A_DATA); std::memcpy(&record->Data.A.IpAddress, answer.data.data(), 4);
                try { resolver.addCacheEntry(answer.owner, answer.type, answer.data, answer.ttl); } catch (...) {}
            } else if (answer.type == DNS_TYPE_AAAA && answer.data.size() == 16) {
                record->wDataLength = sizeof(DNS_AAAA_DATA); std::memcpy(record->Data.AAAA.Ip6Address, answer.data.data(), 16);
                try { resolver.addCacheEntry(answer.owner, answer.type, answer.data, answer.ttl); } catch (...) {}
            } else { delete[] owner; delete record; continue; }
            if (tail) tail->pNext = record; else *ppQueryResults = record;
            tail = record;
        }
        return *ppQueryResults ? DNS_STATUS_SUCCESS : DNS_ERROR_RCODE_NAME_ERROR;
    } catch (...) { DnsRecordListFreeA(*ppQueryResults); *ppQueryResults = nullptr; return DNS_ERROR_NO_MEMORY; }
}

inline uint32_t WINAPI DnsQuery_W(
    const wchar_t* pszName,
    uint16_t wType,
    uint32_t Options,
    void* pExtra,
    DNS_RECORDW** ppQueryResults,
    void** pReserved
) noexcept {
    if (!pszName || pExtra || pReserved) return DNS_ERROR_INVALID_PARAMETER;
    if (!ppQueryResults) return DNS_ERROR_INVALID_PARAMETER;
    *ppQueryResults = nullptr;
    if (Options) return DNS_ERROR_NOT_SUPPORTED;
    std::string narrowName;
    for (const wchar_t* p = pszName; *p; ++p) {
        // IDN conversion is not implemented; never truncate Unicode names
        // into unrelated ASCII names.
        if (static_cast<uint32_t>(*p) > 0x7f) {
            if (ppQueryResults) *ppQueryResults = nullptr;
            return DNS_ERROR_NOT_SUPPORTED;
        }
        narrowName.push_back(static_cast<char>(*p));
    }
    DNS_RECORDA* ansiRecords = nullptr;
    const uint32_t status = DnsQuery_A(narrowName.c_str(), wType, Options, pExtra, &ansiRecords, pReserved);
    if (status != DNS_STATUS_SUCCESS) return status;
    DNS_RECORDW* tail = nullptr;
    try {
        for (auto* ansi = ansiRecords; ansi; ansi = ansi->pNext) {
            auto* wide = new (std::nothrow) DNS_RECORDW{};
            if (!wide) { DnsRecordListFree(*ppQueryResults, 0); *ppQueryResults = nullptr; DnsRecordListFree(ansiRecords, 0); return DNS_ERROR_NO_MEMORY; }
            const size_t length = std::strlen(ansi->pName);
            auto* owner = new (std::nothrow) uint8_t[(length + 1) * sizeof(wchar_t)];
            if (!owner) { delete wide; DnsRecordListFree(*ppQueryResults, 0); *ppQueryResults = nullptr; DnsRecordListFree(ansiRecords, 0); return DNS_ERROR_NO_MEMORY; }
            auto* wideOwner = reinterpret_cast<wchar_t*>(owner);
            for (size_t i = 0; i <= length; ++i) wideOwner[i] = static_cast<unsigned char>(ansi->pName[i]);
            wide->pName = wideOwner; wide->wType = ansi->wType; wide->wDataLength = ansi->wDataLength; wide->dwTtl = ansi->dwTtl;
            if (ansi->wType == DNS_TYPE_A) wide->Data.A = ansi->Data.A;
            else if (ansi->wType == DNS_TYPE_AAAA) wide->Data.AAAA = ansi->Data.AAAA;
            if (tail) tail->pNext = wide; else *ppQueryResults = wide;
            tail = wide;
        }
    } catch (...) { DnsRecordListFree(*ppQueryResults, 0); *ppQueryResults = nullptr; DnsRecordListFree(ansiRecords, 0); return DNS_ERROR_NO_MEMORY; }
    DnsRecordListFree(ansiRecords, 0);
    return DNS_STATUS_SUCCESS;
}

inline void WINAPI DnsRecordListFree(void* pRecordList, uint32_t /*FreeType*/) noexcept {
    // A and W records have different name element widths.  The caller frees
    // through the typed wrappers below; retain the historical W ABI here.
    auto* record = static_cast<DNS_RECORDW*>(pRecordList);
    while (record) {
        auto* next = record->pNext;
        delete[] reinterpret_cast<uint8_t*>(const_cast<wchar_t*>(record->pName));
        delete record;
        record = next;
    }
}

inline void WINAPI DnsRecordListFreeA(DNS_RECORDA* record) noexcept { DnsRecordListFree(record, 0); }

inline uint32_t WINAPI DnsFlushResolverCache() noexcept {
    return DnsResolverEngine::get().flushCache();
}

#if !defined(MICANT_NATIVE_USERMODE)
inline void InitializeDnsApiSubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("dnsapi.dll", "DnsQuery_A", reinterpret_cast<void*>(DnsQuery_A));
    ldr.registerExport("dnsapi.dll", "DnsQuery_W", reinterpret_cast<void*>(DnsQuery_W));
    ldr.registerExport("dnsapi.dll", "DnsRecordListFree", reinterpret_cast<void*>(DnsRecordListFree));
    ldr.registerExport("dnsapi.dll", "DnsFlushResolverCache", reinterpret_cast<void*>(DnsFlushResolverCache));

    // Register SCM dnscache Service
    auto& scm = scm::ServiceControlManager::get();
    auto dnsSvc = std::make_shared<scm::ServiceRecord>();
    dnsSvc->serviceName = L"dnscache";
    dnsSvc->displayName = L"DNS Client";
    dnsSvc->serviceType = scm::SERVICE_WIN32_SHARE_PROCESS;
    dnsSvc->startType = scm::SERVICE_AUTO_START;
    dnsSvc->errorControl = scm::SERVICE_ERROR_NORMAL;
    dnsSvc->binaryPath = L"C:\\Windows\\System32\\svchost.exe -k NetworkService";
    dnsSvc->status.dwCurrentState = scm::SERVICE_RUNNING;
    scm.registerServiceRecord(dnsSvc);

    // Register Version Database
    auto& verDb = version::VersionDatabase::Instance();
    verDb.RegisterModule("dnsapi.dll", "10.0.26100.5900", "MicaNT DNS Client API");
}
#endif

} // namespace micant::dnsapi
