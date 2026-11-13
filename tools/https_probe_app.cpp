// Native Ring 3 WinHTTP/TLS acceptance probe for the controlled M10 QEMU peer.
#define MICANT_NATIVE_USERMODE 1
#include <cstdint>
#include <cstddef>

extern "C" __declspec(dllimport) void* __stdcall WinHttpOpen(
    const wchar_t*, uint32_t, const wchar_t*, const wchar_t*, uint32_t) noexcept;
extern "C" __declspec(dllimport) void* __stdcall WinHttpConnect(
    void*, const wchar_t*, uint16_t, uint32_t) noexcept;
extern "C" __declspec(dllimport) void* __stdcall WinHttpOpenRequest(
    void*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*,
    const wchar_t**, uint32_t) noexcept;
extern "C" __declspec(dllimport) int32_t __stdcall WinHttpSendRequest(
    void*, const wchar_t*, uint32_t, void*, uint32_t, uint32_t, uint64_t) noexcept;
extern "C" __declspec(dllimport) int32_t __stdcall WinHttpReceiveResponse(void*, void*) noexcept;
extern "C" __declspec(dllimport) int32_t __stdcall WinHttpQueryHeaders(
    void*, uint32_t, const wchar_t*, void*, uint32_t*, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t __stdcall WinHttpQueryDataAvailable(
    void*, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t __stdcall WinHttpReadData(
    void*, void*, uint32_t, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t __stdcall WinHttpSetTimeouts(
    void*, int32_t, int32_t, int32_t, int32_t) noexcept;
extern "C" __declspec(dllimport) int32_t __stdcall WinHttpCloseHandle(void*) noexcept;

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<volatile uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(value);
    return destination;
}

extern "C" int memcmp(const void* left, const void* right, size_t count) {
    const auto* a = static_cast<const volatile uint8_t*>(left);
    const auto* b = static_cast<const volatile uint8_t*>(right);
    for (size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return static_cast<int>(a[i]) - static_cast<int>(b[i]);
    return 0;
}

namespace {
constexpr uint32_t AccessNoProxy = 1;
constexpr uint32_t QueryStatusCode = 19;
constexpr uint32_t QueryFlagNumber = 0x20000000;
constexpr uint32_t SecureFlag = 0x00800000;
constexpr uint16_t TrustedPeerPort = 18443;
constexpr uint16_t UnknownIssuerPeerPort = 18444;
constexpr uint16_t ExpiredCertificatePeerPort = 18445;
constexpr char ExpectedBody[] = "MICA-M10-HTTPS-OK\n";
constexpr size_t MaximumBodySize = 4096;

uint64_t syscall(uint64_t number, uint64_t first = 0, uint64_t second = 0) noexcept {
    uint64_t result{};
    __asm__ __volatile__("movq %1, %%rax; movq %2, %%r10; movq %3, %%rdx; syscall; movq %%rax, %0"
        : "=r"(result) : "r"(number), "r"(first), "r"(second)
        : "rax", "rdx", "r8", "r10", "rcx", "r11", "memory");
    return result;
}

void print(const char* text) noexcept {
    size_t length{};
    while (text[length]) ++length;
    (void)syscall(0x25, reinterpret_cast<uint64_t>(text), length);
}

[[noreturn]] void finish(uint32_t status) noexcept {
    (void)syscall(0x2c, ~uint64_t{0}, status);
    for (;;) __asm__ __volatile__("pause");
}

bool closeHandle(void*& handle) noexcept {
    if (!handle) return true;
    void* closing = handle;
    handle = nullptr;
    return WinHttpCloseHandle(closing) != 0;
}

bool runTrustedPayload() noexcept {
    void* session = WinHttpOpen(L"MicaNT-M10-HTTPS-Probe/1", AccessNoProxy,
                                nullptr, nullptr, 0);
    if (!session) return false;
    void* connection = nullptr;
    void* request = nullptr;
    bool ok = WinHttpSetTimeouts(session, 5000, 10000, 10000, 10000) != 0;
    if (ok) connection = WinHttpConnect(session, L"host", TrustedPeerPort, 0);
    ok = ok && connection;
    if (ok) request = WinHttpOpenRequest(connection, L"GET", L"/m10-payload",
                                         L"HTTP/1.1", nullptr, nullptr, SecureFlag);
    ok = ok && request;
    if (ok) ok = WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) != 0;
    if (ok) ok = WinHttpReceiveResponse(request, nullptr) != 0;

    uint32_t statusCode = 0;
    uint32_t statusLength = sizeof(statusCode);
    if (ok) ok = WinHttpQueryHeaders(request, QueryStatusCode | QueryFlagNumber,
                                     nullptr, &statusCode, &statusLength, nullptr) != 0 &&
                 statusLength == sizeof(statusCode) && statusCode == 200;

    uint8_t body[MaximumBodySize]{};
    size_t bodyLength = 0;
    while (ok) {
        uint32_t available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
        if (!available) break;
        if (available > sizeof(body) - bodyLength) { ok = false; break; }
        uint32_t received = 0;
        if (!WinHttpReadData(request, body + bodyLength, available, &received) ||
            !received || received > available) { ok = false; break; }
        bodyLength += received;
    }
    ok = ok && bodyLength == sizeof(ExpectedBody) - 1 &&
        memcmp(body, ExpectedBody, sizeof(ExpectedBody) - 1) == 0;

    const bool requestClosed = closeHandle(request);
    const bool connectionClosed = closeHandle(connection);
    const bool sessionClosed = closeHandle(session);
    return ok && requestClosed && connectionClosed && sessionClosed;
}

bool rejectsTlsPeer(const wchar_t* host, uint16_t port) noexcept {
    void* session = WinHttpOpen(L"MicaNT-M10-HTTPS-Probe/1", AccessNoProxy,
                                nullptr, nullptr, 0);
    if (!session) return false;
    bool ok = WinHttpSetTimeouts(session, 5000, 10000, 10000, 10000) != 0;
    void* connection = ok ? WinHttpConnect(session, host, port, 0) : nullptr;
    void* request = connection ? WinHttpOpenRequest(connection, L"GET", L"/m10-payload",
        L"HTTP/1.1", nullptr, nullptr, SecureFlag) : nullptr;
    const bool rejected = request && !WinHttpSendRequest(request, nullptr, 0,
        nullptr, 0, 0, 0);
    const bool requestClosed = closeHandle(request);
    const bool connectionClosed = closeHandle(connection);
    const bool sessionClosed = closeHandle(session);
    return rejected && requestClosed && connectionClosed && sessionClosed;
}

bool rejectsMismatchedHost() noexcept {
    // The trusted request first proves the name resolves and the peer is live.
    // Its gateway IP reaches the same QEMU user-network peer but cannot match
    // the certificate's DNS subject alternative name.
    return rejectsTlsPeer(L"10.0.2.2", TrustedPeerPort);
}

bool rejectsUnknownIssuer() noexcept {
    return rejectsTlsPeer(L"host", UnknownIssuerPeerPort);
}

bool rejectsExpiredCertificate() noexcept {
    return rejectsTlsPeer(L"host", ExpiredCertificatePeerPort);
}
}

extern "C" void _start() noexcept {
    if (!runTrustedPayload()) {
        print("M10 HTTPS probe failed: trusted payload or cleanup\n");
        finish(1);
    }
    print("M10 HTTPS trusted payload passed\n");
    if (!rejectsMismatchedHost()) {
        print("M10 HTTPS probe failed: mismatched host was not rejected\n");
        finish(2);
    }
    print("M10 HTTPS mismatched host rejected\n");
    if (!rejectsUnknownIssuer()) {
        print("M10 HTTPS probe failed: unknown issuer was not rejected\n");
        finish(3);
    }
    print("M10 HTTPS unknown issuer rejected\n");
    if (!rejectsExpiredCertificate()) {
        print("M10 HTTPS probe failed: expired certificate was not rejected\n");
        finish(4);
    }
    print("M10 HTTPS expired certificate rejected\n");
    print("M10 HTTPS probe passed\n");
    finish(0);
}
