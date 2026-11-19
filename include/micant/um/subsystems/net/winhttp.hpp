// ============================================================================
// MicaNT: Windows HTTP Services Subsystem (winhttp.dll)
// (winhttp.hpp)
//
// Bounded synchronous HTTP/1.1 subset over the canonical Winsock/AFD path.
// HTTPS requires a readable provisioned ROOT store and a validated TLS peer;
// the absence of either is an explicit failure.
// ============================================================================

#pragma once

#include <cstdint>
#include <algorithm>
#include <climits>
#include <string_view>
#include <atomic>
#include <new>
#include <span>
#include "micant/shared/bounded_containers.hpp"
#include <type_traits>
#include <utility>
#if !defined(MICANT_NATIVE_USERMODE)
#include <memory>
#endif

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/tls13_client.hpp"
#include "micant/shared/afd_socket_abi.hpp"
#if defined(MICANT_NATIVE_USERMODE)
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/native_crypt32_api.hpp"
#endif
#if defined(MICANT_NATIVE_USERMODE)
#include "micant/shared/native_network_api.hpp"
#else
#include "micant/um/subsystems/net/ws2_32.hpp"
#include "micant/um/subsystems/net/dnsapi.hpp"
#endif
#if !defined(MICANT_NATIVE_USERMODE)
#include "micant/um/native/ldr.hpp"
#include "micant/um/servers/scm.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#endif

namespace micant::winhttp {

#if defined(MICANT_NATIVE_USERMODE)
inline bool loadSystemRootCertificates(
    micant::shared::BoundedVector<micant::shared::crypto::X509Certificate>& roots) {
    roots.clear();
    void* store = micant::native_crypt32::CertOpenSystemStoreW(0, L"ROOT");
    if (!store) return false;
    const micant::native_crypt32::CertificateContext* context = nullptr;
    size_t totalBytes = 0;
    while ((context = micant::native_crypt32::CertEnumCertificatesInStore(store, context)) != nullptr) {
        if (!context->encoded || context->encodedSize == 0 || context->encodedSize > 1024 * 1024 ||
            roots.size() == 512 || context->encodedSize > 16 * 1024 * 1024 - totalBytes) {
            micant::native_crypt32::CertCloseStore(store, 0);
            roots.clear();
            return false;
        }
        micant::shared::crypto::X509Certificate certificate{};
        if (!micant::shared::crypto::X509Certificate::parseDer(
                std::span<const uint8_t>(context->encoded, context->encodedSize), certificate) ||
            !certificate.isCertificateAuthority) {
            // A system store can contain roots outside the supported TLS
            // profile. They cannot authorize this peer, but must not prevent
            // a chain from reaching another, supported trust anchor.
            continue;
        }
        totalBytes += context->encodedSize;
        if (!roots.push_back(std::move(certificate))) {
            micant::native_crypt32::CertCloseStore(store, 0);
            roots.clear();
            return false;
        }
    }
    const bool closed = micant::native_crypt32::CertCloseStore(store, 0) != 0;
    return closed && !roots.empty();
}
#endif

using BOOL = int32_t;
using DWORD = uint32_t;
using HANDLE = void*;
using HINTERNET = void*;

// ============================================================================
// 1. WinHTTP Constants & Flags
// ============================================================================

inline constexpr uint32_t WINHTTP_ACCESS_TYPE_DEFAULT_PROXY   = 0;
inline constexpr uint32_t WINHTTP_ACCESS_TYPE_NO_PROXY        = 1;
inline constexpr uint32_t WINHTTP_ACCESS_TYPE_NAMED_PROXY     = 3;

inline constexpr uint32_t WINHTTP_FLAG_SECURE                 = 0x00800000;
inline constexpr uint32_t WINHTTP_FLAG_ASYNC                  = 0x10000000;

inline constexpr uint32_t WINHTTP_ADDREQFLAG_ADD             = 0x20000000;
inline constexpr uint32_t WINHTTP_ADDREQFLAG_REPLACE         = 0x80000000;

inline constexpr uint32_t WINHTTP_QUERY_STATUS_CODE           = 19;
inline constexpr uint32_t WINHTTP_QUERY_CONTENT_TYPE          = 1;
inline constexpr uint32_t WINHTTP_QUERY_CONTENT_LENGTH        = 5;
inline constexpr uint32_t WINHTTP_QUERY_RAW_HEADERS_CRLF      = 22;
inline constexpr uint32_t WINHTTP_QUERY_FLAG_NUMBER           = 0x20000000;

inline constexpr uint32_t WINHTTP_OPTION_SECURITY_FLAGS       = 31;
inline constexpr uint32_t SECURITY_FLAG_IGNORE_UNKNOWN_CA     = 0x00000100;
inline constexpr uint32_t SECURITY_FLAG_IGNORE_CERT_DATE_INVALID = 0x00002000;

// ============================================================================
// 2. WinHTTP Internal Handle Architecture
// ============================================================================

enum class HandleType {
    Session,
    Connection,
    Request
};

struct WinHttpTimeouts {
    uint32_t resolve{0}, connect{60000}, send{30000}, receive{30000};
};
struct WinHttpSession {
    micant::shared::UnicodeString userAgent;
    uint32_t accessType{WINHTTP_ACCESS_TYPE_NO_PROXY};
    micant::shared::UnicodeString proxy;
    micant::shared::UnicodeString proxyBypass;
    uint32_t flags{0};
    WinHttpTimeouts timeouts{};
};

struct WinHttpConnection {
    uintptr_t sessionHandle{0};
    micant::shared::UnicodeString serverName;
    uint16_t port{80};
};

struct WinHttpRequest {
    uintptr_t connectHandle{0};
    micant::shared::UnicodeString verb;
    micant::shared::UnicodeString objectName;
    micant::shared::UnicodeString version;
    uint32_t flags{0};
    uint32_t securityFlags{0};
    WinHttpTimeouts timeouts{};

    // Request Headers & Body
    micant::shared::BoundedVector<micant::shared::AnsiString> requestHeaders;
    micant::shared::BoundedVector<uint8_t> requestBody;

    // Response State
    uint32_t statusCode{0};
    micant::shared::AnsiString statusText;
    struct Header { micant::shared::AnsiString first; micant::shared::AnsiString second; };
    struct HeaderTable {
        Header entries[64]{};
        size_t count{};
        [[nodiscard]] bool contains(std::string_view key) const noexcept {
            for (size_t i = 0; i < count; ++i) if (entries[i].first == key) return true;
            return false;
        }
        bool emplace(const micant::shared::AnsiString& key, const micant::shared::AnsiString& value) {
            if (count == 64) return false;
            Header candidate{key, value};
            if (!micant::shared::isContainerGood(candidate.first) || !micant::shared::isContainerGood(candidate.second)) return false;
            entries[count] = std::move(candidate);
            if (!micant::shared::isContainerGood(entries[count].first) || !micant::shared::isContainerGood(entries[count].second)) return false;
            ++count;
            return true;
        }
        [[nodiscard]] const Header* find(std::string_view key, size_t occurrence = 0) const noexcept {
            for (size_t i = 0; i < count; ++i) if (entries[i].first == key) {
                if (occurrence == 0) return &entries[i];
                --occurrence;
            }
            return nullptr;
        }
        void clear() noexcept { for (size_t i = 0; i < count; ++i) entries[i] = {}; count = 0; }
    } responseHeaders;
    micant::shared::AnsiString rawHeadersCrlf;
    micant::shared::BoundedVector<uint8_t> responseBody;
    size_t readOffset{0};
    bool responseReceived{false};
};

class WinHttpHandleTable;

template <typename T>
class WinHttpHandleLease {
public:
    WinHttpHandleLease() noexcept = default;
    WinHttpHandleLease(WinHttpHandleTable* owner, size_t slot, uint32_t generation,
                       T* value) noexcept
        : owner_(owner), slot_(slot), generation_(generation), value_(value) {}
    WinHttpHandleLease(const WinHttpHandleLease&) = delete;
    WinHttpHandleLease& operator=(const WinHttpHandleLease&) = delete;
    WinHttpHandleLease(WinHttpHandleLease&& other) noexcept { moveFrom(other); }
    WinHttpHandleLease& operator=(WinHttpHandleLease&& other) noexcept {
        if (this != &other) { reset(); moveFrom(other); }
        return *this;
    }
    ~WinHttpHandleLease() { reset(); }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }
    [[nodiscard]] T* operator->() const noexcept { return value_; }
    [[nodiscard]] T* get() const noexcept { return value_; }
private:
    void moveFrom(WinHttpHandleLease& other) noexcept {
        owner_ = other.owner_; slot_ = other.slot_; generation_ = other.generation_;
        value_ = other.value_; other.owner_ = nullptr; other.value_ = nullptr;
    }
    void reset() noexcept;
    WinHttpHandleTable* owner_{};
    size_t slot_{};
    uint32_t generation_{};
    T* value_{};
};

class WinHttpHandleTable {
public:
    static WinHttpHandleTable& get() noexcept {
        // The native DLL has no C++ CRT or atexit registration. Construct the
        // table once in static storage and let explicit handle closes release
        // its contents; the process owns the storage until exit.
        alignas(WinHttpHandleTable) static unsigned char storage[sizeof(WinHttpHandleTable)]{};
        static std::atomic<WinHttpHandleTable*> instance{nullptr};
        static std::atomic_flag initialization = ATOMIC_FLAG_INIT;
        auto* table = instance.load(std::memory_order_acquire);
        if (!table) {
            while (initialization.test_and_set(std::memory_order_acquire)) __asm__ __volatile__("pause");
            table = instance.load(std::memory_order_relaxed);
            if (!table) {
                table = ::new (static_cast<void*>(storage)) WinHttpHandleTable{};
                instance.store(table, std::memory_order_release);
            }
            initialization.clear(std::memory_order_release);
        }
        return *table;
    }

    HINTERNET allocateSession(WinHttpSession&& value) noexcept { return allocate(HandleType::Session, &value); }
    HINTERNET allocateConnection(WinHttpConnection&& value) noexcept { return allocate(HandleType::Connection, &value); }
    HINTERNET allocateRequest(WinHttpRequest&& value) noexcept { return allocate(HandleType::Request, &value); }

    bool readTimeouts(HINTERNET handle, HandleType type, WinHttpTimeouts& values) noexcept {
        size_t index{}; uint32_t generation{};
        if (!decode(handle, index, generation)) return false;
        lock();
        const auto& slot = slots_[index];
        const bool valid = slot.used && !slot.closing && slot.generation == generation && slot.type == type;
        if (valid) values = type == HandleType::Session ? sessions_[index].timeouts : requests_[index].timeouts;
        unlock(); return valid;
    }
    bool writeTimeouts(HINTERNET handle, const WinHttpTimeouts& values) noexcept {
        size_t index{}; uint32_t generation{};
        if (!decode(handle, index, generation)) return false;
        lock();
        const auto& slot = slots_[index];
        const bool valid = slot.used && !slot.closing && slot.generation == generation &&
            (slot.type == HandleType::Session || slot.type == HandleType::Request);
        if (valid) {
            if (slot.type == HandleType::Session) sessions_[index].timeouts = values;
            else requests_[index].timeouts = values;
        }
        unlock(); return valid;
    }

    bool releaseHandle(HINTERNET handle) noexcept {
        size_t index{}; uint32_t generation{};
        if (!decode(handle, index, generation)) return false;
        lock();
        auto& slot = slots_[index];
        if (!slot.used || slot.generation != generation || slot.closing) { unlock(); return false; }
        const uintptr_t closingValue = encode(index, generation);
        if (slot.type == HandleType::Session) {
            uintptr_t connectionHandles[Capacity]{};
            size_t connectionCount = 0;
            for (size_t i = 0; i < Capacity; ++i) {
                auto& child = slots_[i];
                if (!child.used || child.closing || child.type != HandleType::Connection ||
                    connections_[i].sessionHandle != closingValue) continue;
                child.closing = true;
                connectionHandles[connectionCount++] = encode(i, child.generation);
            }
            for (size_t i = 0; i < Capacity; ++i) {
                auto& child = slots_[i];
                if (!child.used || child.closing || child.type != HandleType::Request) continue;
                for (size_t c = 0; c < connectionCount; ++c) {
                    if (requests_[i].connectHandle == connectionHandles[c]) {
                        child.closing = true;
                        break;
                    }
                }
            }
        } else if (slot.type == HandleType::Connection) {
            for (size_t i = 0; i < Capacity; ++i) {
                auto& child = slots_[i];
                if (child.used && !child.closing && child.type == HandleType::Request &&
                    requests_[i].connectHandle == closingValue) {
                    child.closing = true;
                }
            }
        }
        auto& closingSlot = slots_[index];
        if (closingSlot.used && closingSlot.generation == generation) {
            closingSlot.closing = true;
        }
        unlock();
        reclaimClosed();
        return true;
    }

    template <typename T>
    WinHttpHandleLease<T> getRecord(HINTERNET handle, HandleType expectedType) noexcept {
        size_t index{}; uint32_t generation{};
        if (!decode(handle, index, generation)) return {};
        lock();
        auto& slot = slots_[index];
        if (!slot.used || slot.closing || slot.generation != generation ||
            slot.type != expectedType || slot.references == UINT32_MAX) {
            unlock(); return {};
        }
        ++slot.references;
        T* value = nullptr;
        if constexpr (std::is_same_v<T, WinHttpSession>) value = &sessions_[index];
        else if constexpr (std::is_same_v<T, WinHttpConnection>) value = &connections_[index];
        else if constexpr (std::is_same_v<T, WinHttpRequest>) value = &requests_[index];
        unlock();
        return value ? WinHttpHandleLease<T>(this, index, generation, value) : WinHttpHandleLease<T>{};
    }

    void resetForTest() noexcept {
        lock();
        for (auto& slot : slots_) if (slot.used) slot.closing = true;
        unlock();
        reclaimClosed();
    }

private:
    WinHttpHandleTable() = default;
    friend class WinHttpHandleLease<WinHttpSession>;
    friend class WinHttpHandleLease<WinHttpConnection>;
    friend class WinHttpHandleLease<WinHttpRequest>;
    static constexpr size_t Capacity = 64;
    struct Slot { HandleType type{}; uint32_t generation{1}; uint32_t references{}; bool used{}; bool closing{}; bool reclaiming{}; };
    static constexpr uintptr_t HandleTag = uintptr_t{0x4D49} << 48;
    Slot slots_[Capacity]{};
    WinHttpSession sessions_[Capacity]{};
    WinHttpConnection connections_[Capacity]{};
    WinHttpRequest requests_[Capacity]{};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    void lock() noexcept { while (lock_.test_and_set(std::memory_order_acquire)) __asm__ __volatile__("pause"); }
    void unlock() noexcept { lock_.clear(std::memory_order_release); }
    static bool decode(HINTERNET handle, size_t& slot, uint32_t& generation) noexcept {
        const uintptr_t value = reinterpret_cast<uintptr_t>(handle);
        if ((value & 0xFFFF000000000000ULL) != HandleTag) return false;
        const uintptr_t low = value & 0xFF;
        if (!low || low > Capacity) return false;
        slot = static_cast<size_t>(low - 1);
        generation = static_cast<uint32_t>((value >> 8) & 0xFFFFFFFFu);
        return generation != 0;
    }
    static uintptr_t encode(size_t index, uint32_t generation) noexcept {
        return HandleTag | (static_cast<uintptr_t>(generation) << 8) | (index + 1);
    }
    HINTERNET allocate(HandleType type, void* source) noexcept {
        if (!source) return nullptr;
        lock();
        for (size_t i = 0; i < Capacity; ++i) {
            auto& slot = slots_[i];
            if (slot.used || slot.references) continue;
            if constexpr (sizeof(uintptr_t) == 8) {
                // Generation wrap is refused, so stale opaque handles never alias.
                if (slot.generation == UINT32_MAX) continue;
            }
            slot.type = type; slot.used = true; slot.closing = false;
            if (type == HandleType::Session) sessions_[i] = std::move(*static_cast<WinHttpSession*>(source));
            else if (type == HandleType::Connection) connections_[i] = std::move(*static_cast<WinHttpConnection*>(source));
            else requests_[i] = std::move(*static_cast<WinHttpRequest*>(source));
            const uintptr_t value = encode(i, slot.generation);
            unlock();
            return reinterpret_cast<HINTERNET>(value);
        }
        unlock();
        return nullptr;
    }
    void reclaimClosed() noexcept {
        for (;;) {
            lock();
            size_t index = Capacity;
            HandleType type{};
            for (size_t i = 0; i < Capacity; ++i) {
                auto& slot = slots_[i];
                if (slot.used && slot.closing && !slot.references && !slot.reclaiming) {
                    slot.reclaiming = true;
                    index = i; type = slot.type;
                    break;
                }
            }
            unlock();
            if (index == Capacity) return;
            // Native bounded strings release VM through syscalls. Keep the
            // slot unavailable while freeing outside the table spinlock, so
            // an APC or another thread cannot deadlock by reentering WinHTTP.
            if (type == HandleType::Session) { auto discarded = std::move(sessions_[index]); }
            else if (type == HandleType::Connection) { auto discarded = std::move(connections_[index]); }
            else if (type == HandleType::Request) { auto discarded = std::move(requests_[index]); }
            lock();
            auto& slot = slots_[index];
            slot.used = false; slot.closing = false; slot.reclaiming = false;
            slot.references = 0; slot.type = {};
            if (slot.generation != UINT32_MAX) ++slot.generation;
            unlock();
        }
    }
    void releaseLease(size_t index, uint32_t generation) noexcept {
        lock(); auto& slot = slots_[index];
        if (slot.used && slot.generation == generation && slot.references) {
            --slot.references;
        }
        unlock();
        reclaimClosed();
    }
};

template <typename T>
void WinHttpHandleLease<T>::reset() noexcept {
    if (owner_) owner_->releaseLease(slot_, generation_);
    owner_ = nullptr; value_ = nullptr;
}

// ============================================================================
// 3. WinHTTP API Implementations conforming to win32metadata
// ============================================================================

inline HINTERNET WINAPI WinHttpOpen(
    const wchar_t* pszAgentW,
    DWORD dwAccessType,
    const wchar_t* pszProxyW,
    const wchar_t* pszProxyBypassW,
    DWORD dwFlags) noexcept
{
    if (dwAccessType != WINHTTP_ACCESS_TYPE_NO_PROXY ||
        (pszProxyW && *pszProxyW) || (pszProxyBypassW && *pszProxyBypassW) ||
        dwFlags != 0)
        return nullptr;
    WinHttpSession session{};
    if (pszAgentW) session.userAgent = pszAgentW;
    if (session.userAgent.size() > 512) return nullptr;
    for (wchar_t ch : session.userAgent)
        if (ch < 0x20 || ch > 0x7e) return nullptr;
    session.accessType = dwAccessType;
    if (pszProxyW) session.proxy = pszProxyW;
    if (pszProxyBypassW) session.proxyBypass = pszProxyBypassW;
    session.flags = dwFlags;
    if (!micant::shared::isContainerGood(session.userAgent) || !micant::shared::isContainerGood(session.proxy) || !micant::shared::isContainerGood(session.proxyBypass)) return nullptr;

    return WinHttpHandleTable::get().allocateSession(std::move(session));
}

inline HINTERNET WINAPI WinHttpConnect(
    HINTERNET hSession,
    const wchar_t* pswzServerName,
    uint16_t nServerPort,
    DWORD dwReserved) noexcept
{
    auto sess = WinHttpHandleTable::get().getRecord<WinHttpSession>(hSession, HandleType::Session);
    if (!sess || !pswzServerName || nServerPort == 0 || dwReserved != 0) return nullptr;

    WinHttpConnection conn{};
    conn.sessionHandle = reinterpret_cast<uintptr_t>(hSession);
    conn.serverName = pswzServerName;
    conn.port = nServerPort;
    if (!micant::shared::isContainerGood(conn.serverName) || conn.serverName.empty() || conn.serverName.size() > 253) return nullptr;

    return WinHttpHandleTable::get().allocateConnection(std::move(conn));
}

inline HINTERNET WINAPI WinHttpOpenRequest(
    HINTERNET hConnect,
    const wchar_t* pwszVerb,
    const wchar_t* pwszObjectName,
    const wchar_t* pwszVersion,
    const wchar_t* pwszReferrer,
    const wchar_t** ppwszAcceptTypes,
    DWORD dwFlags) noexcept
{
    auto conn = WinHttpHandleTable::get().getRecord<WinHttpConnection>(hConnect, HandleType::Connection);
    if (!conn || pwszReferrer || ppwszAcceptTypes || (dwFlags & ~WINHTTP_FLAG_SECURE) != 0 ||
        (pwszVersion && micant::shared::boundedWideCompare(pwszVersion, L"HTTP/1.1") != 0)) return nullptr;

    WinHttpRequest req{};
    req.connectHandle = reinterpret_cast<uintptr_t>(hConnect);
    auto session = WinHttpHandleTable::get().getRecord<WinHttpSession>(
        reinterpret_cast<HINTERNET>(conn->sessionHandle), HandleType::Session);
    if (!session) return nullptr;
    if (!WinHttpHandleTable::get().readTimeouts(
            reinterpret_cast<HINTERNET>(conn->sessionHandle), HandleType::Session, req.timeouts)) return nullptr;
    req.verb = pwszVerb ? pwszVerb : L"GET";
    if (req.verb.empty() || req.verb.size() > 32) return nullptr;
    for (wchar_t ch : req.verb) {
        const bool token = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'!' || ch == L'#' || ch == L'$' || ch == L'%' ||
            ch == L'&' || ch == L'\'' || ch == L'*' || ch == L'+' || ch == L'-' || ch == L'.' ||
            ch == L'^' || ch == L'_' || ch == L'`' || ch == L'|' || ch == L'~';
        if (!token) return nullptr;
    }
    req.objectName = pwszObjectName ? pwszObjectName : L"/";
    if (req.objectName.empty() || req.objectName.front() != L'/' || req.objectName.size() > 8192)
        return nullptr;
    for (wchar_t ch : req.objectName)
        if (ch < 0x21 || ch > 0x7e) return nullptr;
    req.version = pwszVersion ? pwszVersion : L"HTTP/1.1";
    req.flags = dwFlags;
    if (!micant::shared::isContainerGood(req.verb) || !micant::shared::isContainerGood(req.objectName) || !micant::shared::isContainerGood(req.version)) return nullptr;

    return WinHttpHandleTable::get().allocateRequest(std::move(req));
}

inline BOOL WINAPI WinHttpAddRequestHeaders(
    HINTERNET hRequest,
    const wchar_t* lpszHeaders,
    DWORD dwHeadersLength,
    DWORD dwModifiers) noexcept
{
    auto req = WinHttpHandleTable::get().getRecord<WinHttpRequest>(hRequest, HandleType::Request);
    if (!req || !lpszHeaders || (dwModifiers != 0 && dwModifiers != WINHTTP_ADDREQFLAG_ADD)) return 0;

    size_t len = (dwHeadersLength == static_cast<DWORD>(-1)) ? micant::shared::boundedWideLength(lpszHeaders) : dwHeadersLength;
    if (!len || len > 16 * 1024 || req->requestHeaders.size() >= 128) return 0;
    micant::shared::AnsiString headerAscii;
    for (size_t i = 0; i < len; ++i) {
        const wchar_t ch = lpszHeaders[i];
        if (ch < 0x20 && ch != L'\r' && ch != L'\n' && ch != L'\t') return 0;
        if (ch > 0x7e) return 0;
        headerAscii.push_back(static_cast<char>(ch));
    }
    if (!micant::shared::isContainerGood(headerAscii)) return 0;
    if (headerAscii.find("\r\n\r\n") != micant::shared::AnsiString::npos || headerAscii.find('\0') != micant::shared::AnsiString::npos)
        return 0;
    size_t lineStart = 0;
    while (lineStart < headerAscii.size()) {
        const size_t lineEnd = headerAscii.find("\r\n", lineStart);
        const size_t end = lineEnd == micant::shared::AnsiString::npos ? headerAscii.size() : lineEnd;
        if (end == lineStart) return 0;
        const size_t colon = headerAscii.find(':', lineStart);
        if (colon == micant::shared::AnsiString::npos || colon >= end || colon == lineStart) return 0;
        micant::shared::AnsiString name = headerAscii.substr(lineStart, colon - lineStart);
        for (char& c : name) {
            const bool token = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
                c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' ||
                c == '`' || c == '|' || c == '~';
            if (!token) return 0;
            c = static_cast<char>(micant::shared::asciiLower(c));
        }
        if (name == "host" || name == "connection" || name == "content-length" ||
            name == "transfer-encoding") return 0;
        for (size_t i = colon + 1; i < end; ++i)
            if (static_cast<unsigned char>(headerAscii[i]) < 0x20 && headerAscii[i] != '\t') return 0;
        if (lineEnd == micant::shared::AnsiString::npos) break;
        lineStart = lineEnd + 2;
    }
    req->requestHeaders.push_back(std::move(headerAscii));
    return req->requestHeaders.good() ? 1 : 0;
}

inline BOOL WINAPI WinHttpWriteData(
    HINTERNET hRequest,
    const void* lpBuffer,
    DWORD dwNumberOfBytesToWrite,
    DWORD* lpdwNumberOfBytesWritten) noexcept
{
    // This synchronous implementation transmits inside SendRequest and has
    // no live request stream afterwards. Reject streaming writes instead of
    // claiming bytes were sent after the socket has already been closed.
    (void)hRequest; (void)lpBuffer; (void)dwNumberOfBytesToWrite;
    if (lpdwNumberOfBytesWritten) *lpdwNumberOfBytesWritten = 0;
    return 0;
}

inline BOOL WINAPI WinHttpSetOption(
    HINTERNET hInternet,
    DWORD dwOption,
    void* lpBuffer,
    DWORD dwBufferLength) noexcept
{
    auto req = WinHttpHandleTable::get().getRecord<WinHttpRequest>(hInternet, HandleType::Request);
    if (req && dwOption == WINHTTP_OPTION_SECURITY_FLAGS && lpBuffer && dwBufferLength >= sizeof(DWORD)) {
        // Certificate-validation bypass flags are not implemented. Never
        // report that a requested security policy was accepted while silently
        // ignoring it.
        if (*static_cast<DWORD*>(lpBuffer) != 0) return 0;
        req->securityFlags = 0;
        return 1;
    }
    return 0;
}

inline BOOL WINAPI WinHttpSendRequest(
    HINTERNET hRequest,
    const wchar_t* lpszHeaders,
    DWORD dwHeadersLength,
    void* lpOptional,
    DWORD dwOptionalLength,
    DWORD dwTotalLength,
    uint64_t /*dwContext*/) noexcept
{
    auto req = WinHttpHandleTable::get().getRecord<WinHttpRequest>(hRequest, HandleType::Request);
    if (!req) return 0;

    if ((dwOptionalLength && !lpOptional) || dwTotalLength != dwOptionalLength ||
        dwOptionalLength > 8 * 1024 * 1024) return 0;
    if (lpszHeaders && !WinHttpAddRequestHeaders(hRequest, lpszHeaders, dwHeadersLength, 0)) return 0;
    req->requestBody.clear();
    if (dwOptionalLength) {
        const auto* body = static_cast<const uint8_t*>(lpOptional);
        req->requestBody.insert(req->requestBody.end(), body, body + dwOptionalLength);
        if (!req->requestBody.good()) return 0;
    }

    auto conn = WinHttpHandleTable::get().getRecord<WinHttpConnection>(
        reinterpret_cast<HINTERNET>(req->connectHandle), HandleType::Connection
    );
    if (!conn) return 0;
    auto session = WinHttpHandleTable::get().getRecord<WinHttpSession>(
        reinterpret_cast<HINTERNET>(conn->sessionHandle), HandleType::Session);
    if (!session) return 0;
    req->statusCode = 0;
    req->statusText.clear();
    req->responseHeaders.clear();
    req->rawHeadersCrlf.clear();
    req->responseBody.clear();
    req->readOffset = 0;
    req->responseReceived = false;

    // OpenRequest has already validated the bounded ASCII verb and target.
    micant::shared::AnsiString verbAscii(req->verb.begin(), req->verb.end());
    micant::shared::AnsiString pathAscii(req->objectName.begin(), req->objectName.end());
    micant::shared::AnsiString hostAscii;
    hostAscii.reserve(conn->serverName.size());
    for (wchar_t ch : conn->serverName) {
        const bool allowed = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-';
        if (!allowed) return 0;
        hostAscii.push_back(static_cast<char>(ch));
    }

    // Build raw HTTP request wire string
    micant::shared::AnsiString rawReq;
    auto appendDecimal = [&rawReq](uint64_t value) {
        char digits[20]{};
        size_t count = 0;
        do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
        while (count) rawReq.push_back(digits[--count]);
    };
    rawReq.append(std::string_view(verbAscii)); rawReq.push_back(' '); rawReq.append(std::string_view(pathAscii));
    rawReq.append(" HTTP/1.1\r\nHost: "); rawReq.append(std::string_view(hostAscii)); rawReq.push_back(':');
    appendDecimal(conn->port);
    rawReq.append("\r\nConnection: close\r\n");
    bool explicitUserAgent = false;
    for (const auto& header : req->requestHeaders) {
        size_t start = 0;
        while (start < header.size()) {
            const auto end = header.find("\r\n", start);
            const size_t limit = end == micant::shared::AnsiString::npos ? header.size() : end;
            static constexpr char name[] = "user-agent:";
            if (limit - start >= sizeof(name) - 1) {
                size_t i = 0;
                for (; i < sizeof(name) - 1; ++i)
                    if (micant::shared::asciiLower(header[start + i]) != name[i]) break;
                if (i == sizeof(name) - 1) explicitUserAgent = true;
            }
            if (end == micant::shared::AnsiString::npos) break;
            start = end + 2;
        }
    }
    if (!explicitUserAgent && !session->userAgent.empty()) {
        rawReq.append("User-Agent: ");
        for (wchar_t ch : session->userAgent) rawReq.push_back(static_cast<char>(ch));
        rawReq.append("\r\n");
    }
    for (const auto& h : req->requestHeaders) {
        rawReq.append(std::string_view(h));
        if (!h.ends_with("\r\n")) rawReq.append("\r\n");
    }
    if (!req->requestBody.empty()) {
        rawReq.append("Content-Length: "); appendDecimal(req->requestBody.size()); rawReq.append("\r\n");
    }
    rawReq.append("\r\n");
    if (!req->requestBody.empty())
        rawReq.append(reinterpret_cast<const char*>(req->requestBody.data()), req->requestBody.size());
    if (!micant::shared::isContainerGood(rawReq)) return 0;
    micant::shared::AnsiString rawResponse;

    const bool secure = (req->flags & WINHTTP_FLAG_SECURE) != 0;
    uint64_t tlsNowUnixSeconds = 0;
    if (secure) {
#if defined(MICANT_NATIVE_USERMODE)
        if (!native_nt::querySystemTimeUnixSeconds(tlsNowUnixSeconds)) return 0;
#else
        // Hosted adapters have no authoritative OS UTC/trust interface.
        return 0;
#endif
    }

    // Plain and secure HTTP both use the canonical Winsock -> AFD -> TCP path.
    using namespace micant::ws2_32;
    WinHttpTimeouts timeouts{};
    if (!WinHttpHandleTable::get().readTimeouts(hRequest, HandleType::Request, timeouts)) return 0;
#if defined(MICANT_NATIVE_USERMODE)
    WSADATA socketData{};
    if (WSAStartup(0x0202, &socketData) != 0) return 0;
    struct WinsockLease { ~WinsockLease() { (void)WSACleanup(); } } winsockLease;
#endif
    uint32_t ipv4 = inet_addr(hostAscii.c_str());
    if (ipv4 == INADDR_NONE && hostAscii != "255.255.255.255") {
        dnsapi::DNS_RECORDA* records = nullptr;
#if defined(MICANT_NATIVE_USERMODE)
        if (dnsapi::MicantDnsQueryWithTimeout(hostAscii.c_str(), dnsapi::DNS_TYPE_A,
                                              timeouts.resolve, &records) !=
#else
        if (dnsapi::DnsQuery_A(hostAscii.c_str(), dnsapi::DNS_TYPE_A, 0, nullptr, &records, nullptr) !=
#endif
            dnsapi::DNS_STATUS_SUCCESS) {
            dnsapi::DnsRecordListFreeA(records);
            return 0;
        }
        for (auto* record = records; record; record = record->pNext) {
            if (record->wType == dnsapi::DNS_TYPE_A) { ipv4 = record->Data.A.IpAddress; break; }
        }
        dnsapi::DnsRecordListFreeA(records);
        if (ipv4 == INADDR_NONE) return 0;
    }
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return 0;
    const auto closeSocket = [&]() { closesocket(socketHandle); };
    const uint32_t receiveTimeoutMs = timeouts.receive;
    const uint32_t sendTimeoutMs = timeouts.send;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&receiveTimeoutMs), sizeof(receiveTimeoutMs)) == SOCKET_ERROR ||
        setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&sendTimeoutMs), sizeof(sendTimeoutMs)) == SOCKET_ERROR) {
        closeSocket(); return 0;
    }
    const uint32_t connectTimeoutMs = timeouts.connect;
#if defined(MICANT_NATIVE_USERMODE)
    IoStatusBlock connectStatus{};
    if (native_nt::NtDeviceIoControlFile(static_cast<Handle>(socketHandle), 0, nullptr, nullptr,
            &connectStatus, micant::shared::afd::IoctlSetConnectTimeout, &connectTimeoutMs,
            sizeof(connectTimeoutMs), nullptr, 0) != NtStatus::Success) {
#else
    if (micant::ws2_32::afdControl(socketHandle, micant::shared::afd::IoctlSetConnectTimeout,
            &connectTimeoutMs, sizeof(connectTimeoutMs)) != NtStatus::Success) {
#endif
        closeSocket(); return 0;
    }
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(conn->port);
    remote.sin_addr.S_un.S_addr = ipv4;
    if (connect(socketHandle, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == SOCKET_ERROR) {
        closeSocket(); return 0;
    }
    const auto sendAll = [&](const uint8_t* bytes, size_t length) {
        size_t offset = 0;
        while (offset < length) {
            const int count = send(socketHandle, reinterpret_cast<const char*>(bytes + offset),
                static_cast<int>(std::min<size_t>(length - offset, static_cast<size_t>(INT32_MAX))), 0);
            if (count <= 0) return false;
            offset += static_cast<size_t>(count);
        }
        return true;
    };
    const auto receiveExact = [&](uint8_t* bytes, size_t length) {
        size_t offset = 0;
        while (offset < length) {
            const int count = recv(socketHandle, reinterpret_cast<char*>(bytes + offset),
                static_cast<int>(std::min<size_t>(length - offset, static_cast<size_t>(INT32_MAX))), 0);
            if (count <= 0) return false;
            offset += static_cast<size_t>(count);
        }
        return true;
    };

    constexpr size_t MaximumResponseBytes = 8 * 1024 * 1024;
    micant::shared::BoundedVector<char> responseBuffer(16 * 1024);
    if (!responseBuffer.good()) { closeSocket(); return 0; }
    if (secure) {
        micant::shared::BoundedVector<micant::shared::crypto::X509Certificate> trustRoots;
#if defined(MICANT_NATIVE_USERMODE)
        if (!loadSystemRootCertificates(trustRoots)) { closeSocket(); return 0; }
#else
        closeSocket(); return 0;
#endif
        micant::shared::tls13::Tls13ClientSession tls;
        const auto clientHello = tls.generateClientHello(hostAscii);
        if (clientHello.empty() || !clientHello.good() || !sendAll(clientHello.data(), clientHello.size())) {
            closeSocket(); return 0;
        }
        bool serverFlightComplete = false;
        for (size_t recordIndex = 0; recordIndex < 128 && !serverFlightComplete; ++recordIndex) {
            std::array<uint8_t, 5> header{};
            if (!receiveExact(header.data(), header.size())) { closeSocket(); return 0; }
            const size_t recordLength = (static_cast<size_t>(header[3]) << 8) | header[4];
            if (recordLength > 16384 + 256) { closeSocket(); return 0; }
            micant::shared::BoundedVector<uint8_t> record(header.begin(), header.end());
            if (!record.resize(5 + recordLength) || !record.good()) { closeSocket(); return 0; }
            if (!receiveExact(record.data() + 5, recordLength)) { closeSocket(); return 0; }
            serverFlightComplete = tls.processServerHandshake(record, trustRoots,
                static_cast<int64_t>(tlsNowUnixSeconds));
            if (tls.isFailed()) { closeSocket(); return 0; }
        }
        if (!serverFlightComplete) { closeSocket(); return 0; }
        micant::shared::BoundedVector<uint8_t> clientFinished;
        if (!tls.createClientFinished(clientFinished) || !sendAll(clientFinished.data(), clientFinished.size())) {
            tls.confirmClientFinishedTransmitted(false); closeSocket(); return 0;
        }
        tls.confirmClientFinishedTransmitted(true);
        if (!tls.isEstablished()) { closeSocket(); return 0; }
        size_t requestOffset = 0;
        while (requestOffset < rawReq.size()) {
            const size_t count = std::min<size_t>(16384, rawReq.size() - requestOffset);
            const auto encrypted = tls.encryptAppData(std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(rawReq.data() + requestOffset), count));
            if (encrypted.empty() || !sendAll(encrypted.data(), encrypted.size())) { closeSocket(); return 0; }
            requestOffset += count;
        }
        bool receivedCloseNotify = false;
        micant::shared::BoundedVector<uint8_t> postHandshakeBytes;
        while (rawResponse.size() < MaximumResponseBytes) {
            std::array<uint8_t, 5> header{};
            if (!receiveExact(header.data(), header.size())) break;
            const size_t recordLength = (static_cast<size_t>(header[3]) << 8) | header[4];
            if (recordLength > 16384 + 256) { closeSocket(); return 0; }
            micant::shared::BoundedVector<uint8_t> record(header.begin(), header.end());
            if (!record.resize(5 + recordLength) || !record.good()) { closeSocket(); return 0; }
            if (!receiveExact(record.data() + 5, recordLength)) { closeSocket(); return 0; }
            uint8_t innerType = 0;
            micant::shared::BoundedVector<uint8_t> plaintext;
            if (!tls.decryptRecord(record, innerType, plaintext)) { closeSocket(); return 0; }
            if (innerType == micant::shared::tls13::CONTENT_TYPE_APPLICATION_DATA) {
                if (!postHandshakeBytes.empty()) { closeSocket(); return 0; }
                if (plaintext.size() > MaximumResponseBytes - rawResponse.size()) { closeSocket(); return 0; }
                rawResponse.append(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
                if (!micant::shared::isContainerGood(rawResponse)) { closeSocket(); return 0; }
            } else if (innerType == micant::shared::tls13::CONTENT_TYPE_ALERT) {
                // TLS 1.3 ignores the legacy alert level; close_notify is
                // identified by its description after record authentication.
                if (!postHandshakeBytes.empty() || plaintext.size() != 2 || plaintext[1] != 0) {
                    closeSocket(); return 0;
                }
                receivedCloseNotify = true;
                break;
            } else if (innerType == micant::shared::tls13::CONTENT_TYPE_HANDSHAKE) {
                // NewSessionTicket messages may be fragmented at any record
                // boundary. Retain incomplete messages and discard complete
                // tickets; session resumption is not enabled by this client.
                constexpr size_t MaximumPostHandshakeBytes = 1024 * 1024;
                if (plaintext.size() > MaximumPostHandshakeBytes - postHandshakeBytes.size()) {
                    closeSocket(); return 0;
                }
                postHandshakeBytes.insert(postHandshakeBytes.end(), plaintext.begin(), plaintext.end());
                if (!postHandshakeBytes.good()) { closeSocket(); return 0; }
                size_t consumed = 0;
                while (postHandshakeBytes.size() - consumed >= 4) {
                    const auto* message = postHandshakeBytes.data() + consumed;
                    const size_t size = 4 + (static_cast<size_t>(message[1]) << 16) +
                        (static_cast<size_t>(message[2]) << 8) + message[3];
                    if (size > MaximumPostHandshakeBytes) { closeSocket(); return 0; }
                    if (size > postHandshakeBytes.size() - consumed) break;
                    const auto body = std::span<const uint8_t>(message + 4, size - 4);
                    if (message[0] == 24) { // TLS 1.3 KeyUpdate
                        // A key change must finish at a record boundary.
                        if (consumed + size != postHandshakeBytes.size()) { closeSocket(); return 0; }
                        micant::shared::BoundedVector<uint8_t> reply;
                        if (!tls.processServerKeyUpdate(body, reply)) { closeSocket(); return 0; }
                        if (!reply.empty()) {
                            const bool sent = sendAll(reply.data(), reply.size());
                            if (!tls.confirmKeyUpdateTransmitted(sent)) { closeSocket(); return 0; }
                        }
                        consumed += size;
                        continue;
                    }
                    if (message[0] != 4) { closeSocket(); return 0; }
                    if (body.size() < 13) { closeSocket(); return 0; }
                    size_t cursor = 8; // ticket_lifetime and ticket_age_add
                    const size_t nonceLength = body[cursor++];
                    if (nonceLength > body.size() - cursor) { closeSocket(); return 0; }
                    cursor += nonceLength;
                    if (body.size() - cursor < 2) { closeSocket(); return 0; }
                    const size_t ticketLength = (static_cast<size_t>(body[cursor]) << 8) | body[cursor + 1];
                    cursor += 2;
                    if (!ticketLength || ticketLength > body.size() - cursor) { closeSocket(); return 0; }
                    cursor += ticketLength;
                    if (body.size() - cursor < 2) { closeSocket(); return 0; }
                    const size_t extensionLength = (static_cast<size_t>(body[cursor]) << 8) | body[cursor + 1];
                    cursor += 2;
                    if (extensionLength != body.size() - cursor) { closeSocket(); return 0; }
                    while (cursor < body.size()) {
                        if (body.size() - cursor < 4) { closeSocket(); return 0; }
                        const size_t valueLength = (static_cast<size_t>(body[cursor + 2]) << 8) | body[cursor + 3];
                        cursor += 4;
                        if (valueLength > body.size() - cursor) { closeSocket(); return 0; }
                        cursor += valueLength;
                    }
                    consumed += size;
                }
                if (consumed) postHandshakeBytes.erase(postHandshakeBytes.begin(),
                    postHandshakeBytes.begin() + static_cast<std::ptrdiff_t>(consumed));
            } else { closeSocket(); return 0; }
        }
        if (!receivedCloseNotify) { closeSocket(); return 0; }
        const auto closeNotify = tls.createCloseNotify();
        if (closeNotify.empty() || !closeNotify.good() ||
            !sendAll(closeNotify.data(), closeNotify.size())) { closeSocket(); return 0; }
        closeSocket();
    } else {
        if (!sendAll(reinterpret_cast<const uint8_t*>(rawReq.data()), rawReq.size())) {
            closeSocket(); return 0;
        }
        while (rawResponse.size() < MaximumResponseBytes) {
            const size_t capacity = std::min(responseBuffer.size(), MaximumResponseBytes - rawResponse.size());
            const int count = recv(socketHandle, responseBuffer.data(), static_cast<int>(capacity), 0);
            if (count == 0) break;
            if (count < 0) { closeSocket(); return 0; }
            rawResponse.append(responseBuffer.data(), static_cast<size_t>(count));
            if (!micant::shared::isContainerGood(rawResponse)) { closeSocket(); return 0; }
            const size_t headerEnd = rawResponse.find("\r\n\r\n");
            if (headerEnd != micant::shared::AnsiString::npos) {
                if (headerEnd > MaximumResponseBytes - 4) { closeSocket(); return 0; }
                const size_t statusEnd = rawResponse.find("\r\n");
                if (statusEnd != micant::shared::AnsiString::npos && statusEnd >= 12) {
                    const std::string_view status(rawResponse.data(), statusEnd);
                    if (req->verb == L"HEAD" || status.starts_with("HTTP/1.1 204") ||
                        status.starts_with("HTTP/1.0 204") || status.starts_with("HTTP/1.1 304") ||
                        status.starts_with("HTTP/1.0 304")) break;
                }
                size_t scan = rawResponse.find("\r\n") + 2;
                bool foundLength = false;
                size_t expectedLength = 0;
                while (scan < headerEnd) {
                    const size_t lineEnd = rawResponse.find("\r\n", scan);
                    if (lineEnd == micant::shared::AnsiString::npos || lineEnd > headerEnd) break;
                    const std::string_view line(rawResponse.data() + scan, lineEnd - scan);
                    const size_t colon = line.find(':');
                    if (colon != std::string_view::npos && colon != 0) {
                        micant::shared::AnsiString key(std::string_view(line.data(), colon));
                        for (char& c : key) c = static_cast<char>(micant::shared::asciiLower(c));
                        if (key == "content-length") {
                            if (foundLength) { closeSocket(); return 0; }
                            foundLength = true;
                            size_t value = colon + 1;
                            while (value < line.size() && (line[value] == ' ' || line[value] == '\t')) ++value;
                            if (value == line.size()) { closeSocket(); return 0; }
                            for (; value < line.size(); ++value) {
                                const char c = line[value];
                                if (c < '0' || c > '9' || expectedLength >
                                    (MaximumResponseBytes - static_cast<size_t>(c - '0')) / 10) {
                                    closeSocket(); return 0;
                                }
                                expectedLength = expectedLength * 10 + static_cast<size_t>(c - '0');
                            }
                            if (expectedLength > MaximumResponseBytes - headerEnd - 4) {
                                closeSocket(); return 0;
                            }
                        }
                    }
                    scan = lineEnd + 2;
                }
                if (foundLength && rawResponse.size() >= headerEnd + 4 + expectedLength) break;
            }
        }
        closeSocket();
    }
    if (rawResponse.size() == MaximumResponseBytes) return 0;

    if (rawResponse.empty()) return 0;
    const size_t firstCrlf = rawResponse.find("\r\n");
    const size_t headerEnd = rawResponse.find("\r\n\r\n");
    if (firstCrlf == micant::shared::AnsiString::npos || headerEnd == micant::shared::AnsiString::npos || firstCrlf > headerEnd) return 0;

    const std::string_view statusLine(rawResponse.data(), firstCrlf);
    if (!(statusLine.starts_with("HTTP/1.0 ") || statusLine.starts_with("HTTP/1.1 ")) ||
        statusLine.size() < 12) return 0;
    const size_t codeOffset = 9;
    if (statusLine[codeOffset] < '1' || statusLine[codeOffset] > '9' ||
        statusLine[codeOffset + 1] < '0' || statusLine[codeOffset + 1] > '9' ||
        statusLine[codeOffset + 2] < '0' || statusLine[codeOffset + 2] > '9' ||
        (statusLine.size() > codeOffset + 3 && statusLine[codeOffset + 3] != ' ')) return 0;
    req->statusCode = static_cast<uint32_t>((statusLine[codeOffset] - '0') * 100 +
        (statusLine[codeOffset + 1] - '0') * 10 + (statusLine[codeOffset + 2] - '0'));
    if (statusLine.size() > codeOffset + 4)
        req->statusText.assign(std::string_view(statusLine.data() + codeOffset + 4,
                                                statusLine.size() - codeOffset - 4));

    req->rawHeadersCrlf.assign(rawResponse.data(), headerEnd + 4);
    bool hasContentLength = false;
    size_t contentLength = 0;
    bool chunked = false;
    size_t pos = firstCrlf + 2;
    while (pos < headerEnd) {
        const size_t nextCrlf = rawResponse.find("\r\n", pos);
        if (nextCrlf == micant::shared::AnsiString::npos || nextCrlf > headerEnd || nextCrlf == pos) return 0;
        const std::string_view line(rawResponse.data() + pos, nextCrlf - pos);
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0 || line.front() == ' ' || line.front() == '\t') return 0;
        micant::shared::AnsiString key(std::string_view(line.data(), colon));
        for (char& c : key) c = static_cast<char>(micant::shared::asciiLower(c));
        size_t valueStart = colon + 1;
        while (valueStart < line.size() && (line[valueStart] == ' ' || line[valueStart] == '\t')) ++valueStart;
        size_t valueEnd = line.size();
        while (valueEnd > valueStart && (line[valueEnd - 1] == ' ' || line[valueEnd - 1] == '\t')) --valueEnd;
        const micant::shared::AnsiString value(std::string_view(line.data() + valueStart,
                                                                 valueEnd - valueStart));
        // Preserve repeated fields such as Set-Cookie. Framing headers remain
        // single-valued so ambiguous Content-Length/Transfer-Encoding cannot
        // be accepted by the bounded parser.
        if (((key == "content-length" || key == "transfer-encoding") &&
             req->responseHeaders.contains(key)) || !req->responseHeaders.emplace(key, value)) return 0;
        if (key == "content-length") {
            if (value.empty()) return 0;
            size_t parsed = 0;
            for (char c : value) {
                if (c < '0' || c > '9' || parsed > (SIZE_MAX - static_cast<size_t>(c - '0')) / 10) return 0;
                parsed = parsed * 10 + static_cast<size_t>(c - '0');
            }
            hasContentLength = true;
            contentLength = parsed;
        } else if (key == "transfer-encoding") {
            micant::shared::AnsiString coding = value;
            for (char& c : coding) c = static_cast<char>(micant::shared::asciiLower(c));
            if (coding != "chunked") return 0;
            chunked = true;
        }
        pos = nextCrlf + 2;
    }
    if (hasContentLength && chunked) return 0;
    const size_t bodyStart = headerEnd + 4;
    const size_t wireBodyLength = rawResponse.size() - bodyStart;
    // HEAD, 204 and 304 responses have no message body. In particular the
    // Content-Length of a HEAD/304 response describes a hypothetical GET,
    // not bytes that must be present on this connection.
    const bool bodyForbidden = verbAscii == "HEAD" || req->statusCode == 204 || req->statusCode == 304;
    if (bodyForbidden) {
        if (wireBodyLength != 0) return 0;
    } else if (chunked) {
        size_t cursor = bodyStart;
        for (;;) {
            const size_t lineEnd = rawResponse.find("\r\n", cursor);
            if (lineEnd == micant::shared::AnsiString::npos) return 0;
            size_t sizeEnd = rawResponse.find(';', cursor);
            if (sizeEnd == micant::shared::AnsiString::npos || sizeEnd > lineEnd) sizeEnd = lineEnd;
            if (sizeEnd == cursor) return 0;
            size_t chunkLength = 0;
            for (size_t i = cursor; i < sizeEnd; ++i) {
                const char c = rawResponse[i];
                const unsigned digit = c >= '0' && c <= '9' ? static_cast<unsigned>(c - '0') :
                    c >= 'a' && c <= 'f' ? static_cast<unsigned>(c - 'a' + 10) :
                    c >= 'A' && c <= 'F' ? static_cast<unsigned>(c - 'A' + 10) : 16;
                if (digit == 16 || chunkLength > (SIZE_MAX - digit) / 16) return 0;
                chunkLength = chunkLength * 16 + digit;
            }
            cursor = lineEnd + 2;
            if (!chunkLength) {
                const size_t trailerEnd = rawResponse.find("\r\n\r\n", cursor);
                if (cursor + 2 == rawResponse.size() && std::memcmp(rawResponse.data() + cursor, "\r\n", 2) == 0) break;
                if (trailerEnd == micant::shared::AnsiString::npos || trailerEnd + 4 != rawResponse.size()) return 0;
                break;
            }
            if (chunkLength > MaximumResponseBytes - req->responseBody.size() ||
                chunkLength > rawResponse.size() - cursor || rawResponse.size() - cursor - chunkLength < 2 ||
                std::memcmp(rawResponse.data() + cursor + chunkLength, "\r\n", 2) != 0) return 0;
            req->responseBody.insert(req->responseBody.end(), rawResponse.data() + cursor,
                rawResponse.data() + cursor + chunkLength);
            if (!req->responseBody.good()) return 0;
            cursor += chunkLength + 2;
        }
    } else if (hasContentLength) {
        if (contentLength > MaximumResponseBytes || wireBodyLength != contentLength) return 0;
        req->responseBody.assign(reinterpret_cast<const uint8_t*>(rawResponse.data() + bodyStart),
            reinterpret_cast<const uint8_t*>(rawResponse.data() + bodyStart + contentLength));
        if (!req->responseBody.good()) return 0;
    } else {
        if (wireBodyLength > MaximumResponseBytes) return 0;
        req->responseBody.assign(reinterpret_cast<const uint8_t*>(rawResponse.data() + bodyStart),
            reinterpret_cast<const uint8_t*>(rawResponse.data() + bodyStart + wireBodyLength));
        if (!req->responseBody.good()) return 0;
    }

    req->responseReceived = true;
    return 1;
}

inline BOOL WINAPI WinHttpReceiveResponse(HINTERNET hRequest, void* /*lpReserved*/) noexcept {
    auto req = WinHttpHandleTable::get().getRecord<WinHttpRequest>(hRequest, HandleType::Request);
    if (!req) return 0;
    return req->responseReceived ? 1 : 0;
}

inline BOOL WINAPI WinHttpQueryHeaders(
    HINTERNET hRequest,
    DWORD dwInfoLevel,
    const wchar_t* pwszName,
    void* lpBuffer,
    DWORD* lpdwBufferLength,
    DWORD* lpdwIndex) noexcept
{
    auto req = WinHttpHandleTable::get().getRecord<WinHttpRequest>(hRequest, HandleType::Request);
    if (!req || !req->responseReceived || !lpdwBufferLength) return 0;

    if (dwInfoLevel == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER)) {
        if (!lpBuffer || *lpdwBufferLength < sizeof(DWORD)) {
            *lpdwBufferLength = sizeof(DWORD);
            return 0;
        }
        *static_cast<DWORD*>(lpBuffer) = req->statusCode;
        *lpdwBufferLength = sizeof(DWORD);
        return 1;
    } else if (dwInfoLevel == WINHTTP_QUERY_RAW_HEADERS_CRLF) {
        DWORD needBytes = static_cast<DWORD>((req->rawHeadersCrlf.size() + 1) * sizeof(wchar_t));
        if (!lpBuffer || *lpdwBufferLength < needBytes) {
            *lpdwBufferLength = needBytes;
            return 0;
        }
        auto* wbuf = static_cast<wchar_t*>(lpBuffer);
        for (size_t i = 0; i < req->rawHeadersCrlf.size(); ++i) {
            wbuf[i] = static_cast<wchar_t>(req->rawHeadersCrlf[i]);
        }
        wbuf[req->rawHeadersCrlf.size()] = L'\0';
        *lpdwBufferLength = needBytes;
        return 1;
    } else if (pwszName) {
        micant::shared::AnsiString headerName(pwszName, pwszName + micant::shared::boundedWideLength(pwszName));
        for (char& c : headerName) c = static_cast<char>(micant::shared::asciiLower(c));
        const size_t occurrence = lpdwIndex ? *lpdwIndex : 0;
        const auto* it = req->responseHeaders.find(headerName, occurrence);
        if (it) {
            DWORD needBytes = static_cast<DWORD>((it->second.size() + 1) * sizeof(wchar_t));
            if (!lpBuffer || *lpdwBufferLength < needBytes) {
                *lpdwBufferLength = needBytes;
                return 0;
            }
            auto* wbuf = static_cast<wchar_t*>(lpBuffer);
            for (size_t i = 0; i < it->second.size(); ++i) {
                wbuf[i] = static_cast<wchar_t>(it->second[i]);
            }
            wbuf[it->second.size()] = L'\0';
            *lpdwBufferLength = needBytes;
            if (lpdwIndex) ++*lpdwIndex;
            return 1;
        }
    }
    return 0;
}

inline BOOL WINAPI WinHttpQueryDataAvailable(HINTERNET hRequest, DWORD* lpdwNumberOfBytesAvailable) noexcept {
    auto req = WinHttpHandleTable::get().getRecord<WinHttpRequest>(hRequest, HandleType::Request);
    if (!req || !req->responseReceived || !lpdwNumberOfBytesAvailable) return 0;

    size_t unread = (req->readOffset < req->responseBody.size()) 
        ? (req->responseBody.size() - req->readOffset) 
        : 0;
    *lpdwNumberOfBytesAvailable = static_cast<DWORD>(unread);
    return 1;
}

inline BOOL WINAPI WinHttpReadData(
    HINTERNET hRequest,
    void* lpBuffer,
    DWORD dwNumberOfBytesToRead,
    DWORD* lpdwNumberOfBytesRead) noexcept
{
    auto req = WinHttpHandleTable::get().getRecord<WinHttpRequest>(hRequest, HandleType::Request);
    if (!req || !req->responseReceived || !lpBuffer || !lpdwNumberOfBytesRead) return 0;

    size_t unread = (req->readOffset < req->responseBody.size()) 
        ? (req->responseBody.size() - req->readOffset) 
        : 0;
    size_t toCopy = std::min(static_cast<size_t>(dwNumberOfBytesToRead), unread);

    if (toCopy > 0) {
        micant::shared::boundedMemoryCopy(lpBuffer, req->responseBody.data() + req->readOffset, toCopy);
        req->readOffset += toCopy;
    }
    *lpdwNumberOfBytesRead = static_cast<DWORD>(toCopy);
    return 1;
}

inline BOOL WINAPI WinHttpCloseHandle(HINTERNET hInternet) noexcept {
    return WinHttpHandleTable::get().releaseHandle(hInternet) ? 1 : 0;
}

inline BOOL WINAPI WinHttpGetDefaultProxyConfiguration(void* /*pConfig*/) noexcept {
    return 0;
}

inline BOOL WINAPI WinHttpCrackUrl(
    const wchar_t* pwszUrl,
    uint32_t dwUrlLength,
    uint32_t /*dwFlags*/,
    void* /*lpUrlComponents*/) noexcept
{
    (void)pwszUrl;
    (void)dwUrlLength;
    return 0;
}

inline BOOL WINAPI WinHttpGetIEProxyConfigForCurrentUser(void* /*pProxyConfig*/) noexcept { return 0; }
inline BOOL WINAPI WinHttpSetTimeouts(HINTERNET handle, int32_t resolve, int32_t connect,
                                     int32_t send, int32_t receive) noexcept {
    if (resolve < -1 || connect < -1 || send < -1 || receive < -1) return 0;
#if !defined(MICANT_NATIVE_USERMODE)
    // The hosted resolver has no per-query deadline interface.
    return 0;
#endif
    const WinHttpTimeouts values{resolve <= 0 ? 0u : static_cast<uint32_t>(resolve),
        connect <= 0 ? 0u : static_cast<uint32_t>(connect),
        send <= 0 ? 0u : static_cast<uint32_t>(send),
        receive <= 0 ? 0u : static_cast<uint32_t>(receive)};
    return WinHttpHandleTable::get().writeTimeouts(handle, values) ? 1 : 0;
}
inline void* WINAPI WinHttpSetStatusCallback(HINTERNET, void*, DWORD, uint64_t) noexcept { return nullptr; }
inline BOOL WINAPI WinHttpQueryAuthSchemes(HINTERNET, DWORD*, DWORD*, DWORD*) noexcept { return 0; }
inline BOOL WINAPI WinHttpGetProxyForUrl(HINTERNET, const wchar_t*, void*, void*) noexcept { return 0; }
inline BOOL WINAPI WinHttpSetCredentials(HINTERNET, DWORD, DWORD, const wchar_t*, const wchar_t*, void*) noexcept { return 0; }
inline BOOL WINAPI WinHttpQueryOption(HINTERNET, DWORD, void*, DWORD*) noexcept { return 0; }

// ============================================================================
// 4. SCM Service & Version Registration for winhttp.dll
// ============================================================================

inline void InitializeWinHttpSubsystemExports() {
#if !defined(MICANT_NATIVE_USERMODE)
    // 1. SCM Registration for WinHttpAutoProxySvc / winhttp service
    auto& scm = scm::ServiceControlManager::get();
    auto winhttpSvc = std::make_shared<scm::ServiceRecord>();
    winhttpSvc->serviceName   = L"winhttp";
    winhttpSvc->displayName   = L"Windows HTTP Services Subsystem";
    winhttpSvc->serviceType   = scm::SERVICE_WIN32_SHARE_PROCESS;
    winhttpSvc->startType     = scm::SERVICE_AUTO_START;
    winhttpSvc->errorControl  = scm::SERVICE_ERROR_NORMAL;
    winhttpSvc->binaryPath    = L"C:\\Windows\\System32\\svchost.exe -k LocalServiceNetwork";
    winhttpSvc->status.dwCurrentState = scm::SERVICE_RUNNING;
    scm.registerServiceRecord(winhttpSvc);

    // 2. Register Module in VersionDatabase
    auto& verDb = version::VersionDatabase::Instance();
    verDb.RegisterModule("winhttp.dll", "10.0.26100.5900", "MicaNT Windows HTTP Services Transport Library");

    // 3. Register Exports in JanusLDR
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("winhttp.dll", "WinHttpGetDefaultProxyConfiguration", reinterpret_cast<void*>(WinHttpGetDefaultProxyConfiguration));
    ldr.registerExport("winhttp.dll", "WinHttpCrackUrl", reinterpret_cast<void*>(WinHttpCrackUrl));
    ldr.registerExport("winhttp.dll", "WinHttpGetIEProxyConfigForCurrentUser", reinterpret_cast<void*>(WinHttpGetIEProxyConfigForCurrentUser));
    ldr.registerExport("winhttp.dll", "WinHttpSetTimeouts", reinterpret_cast<void*>(WinHttpSetTimeouts));
    ldr.registerExport("winhttp.dll", "WinHttpSetStatusCallback", reinterpret_cast<void*>(WinHttpSetStatusCallback));
    ldr.registerExport("winhttp.dll", "WinHttpConnect", reinterpret_cast<void*>(WinHttpConnect));
    ldr.registerExport("winhttp.dll", "WinHttpReceiveResponse", reinterpret_cast<void*>(WinHttpReceiveResponse));
    ldr.registerExport("winhttp.dll", "WinHttpQueryAuthSchemes", reinterpret_cast<void*>(WinHttpQueryAuthSchemes));
    ldr.registerExport("winhttp.dll", "WinHttpGetProxyForUrl", reinterpret_cast<void*>(WinHttpGetProxyForUrl));
    ldr.registerExport("winhttp.dll", "WinHttpReadData", reinterpret_cast<void*>(WinHttpReadData));
    ldr.registerExport("winhttp.dll", "WinHttpCloseHandle", reinterpret_cast<void*>(WinHttpCloseHandle));
    ldr.registerExport("winhttp.dll", "WinHttpQueryHeaders", reinterpret_cast<void*>(WinHttpQueryHeaders));
    ldr.registerExport("winhttp.dll", "WinHttpOpenRequest", reinterpret_cast<void*>(WinHttpOpenRequest));
    ldr.registerExport("winhttp.dll", "WinHttpAddRequestHeaders", reinterpret_cast<void*>(WinHttpAddRequestHeaders));
    ldr.registerExport("winhttp.dll", "WinHttpOpen", reinterpret_cast<void*>(WinHttpOpen));
    ldr.registerExport("winhttp.dll", "WinHttpWriteData", reinterpret_cast<void*>(WinHttpWriteData));
    ldr.registerExport("winhttp.dll", "WinHttpSetCredentials", reinterpret_cast<void*>(WinHttpSetCredentials));
    ldr.registerExport("winhttp.dll", "WinHttpQueryDataAvailable", reinterpret_cast<void*>(WinHttpQueryDataAvailable));
    ldr.registerExport("winhttp.dll", "WinHttpSetOption", reinterpret_cast<void*>(WinHttpSetOption));
    ldr.registerExport("winhttp.dll", "WinHttpSendRequest", reinterpret_cast<void*>(WinHttpSendRequest));
    ldr.registerExport("winhttp.dll", "WinHttpQueryOption", reinterpret_cast<void*>(WinHttpQueryOption));
#endif
}

} // namespace micant::winhttp
