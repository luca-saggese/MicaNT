// Native Ring 3 TCP probe used by the controlled QEMU networking peer.
#define MICANT_NATIVE_USERMODE 1
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "micant/shared/native_process_abi.hpp"

// This freestanding probe deliberately links without a hosted CRT. Keep the
// small memory primitives it needs local to the executable.
extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<volatile unsigned char*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<unsigned char>(value);
    return destination;
}

extern "C" int memcmp(const void* left, const void* right, size_t count) {
    const auto* a = static_cast<const volatile unsigned char*>(left);
    const auto* b = static_cast<const volatile unsigned char*>(right);
    for (size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) return static_cast<int>(a[i]) - static_cast<int>(b[i]);
    }
    return 0;
}

extern "C" __declspec(dllimport) int WSAStartup(uint16_t, void*) noexcept;
extern "C" __declspec(dllimport) int WSACleanup() noexcept;
extern "C" __declspec(dllimport) uintptr_t socket(int, int, int) noexcept;
extern "C" __declspec(dllimport) int closesocket(uintptr_t) noexcept;
extern "C" __declspec(dllimport) int connect(uintptr_t, const void*, int) noexcept;
extern "C" __declspec(dllimport) int bind(uintptr_t, const void*, int) noexcept;
extern "C" __declspec(dllimport) int listen(uintptr_t, int) noexcept;
extern "C" __declspec(dllimport) uintptr_t accept(uintptr_t, void*, int*) noexcept;
extern "C" __declspec(dllimport) int send(uintptr_t, const char*, int, int) noexcept;
extern "C" __declspec(dllimport) int recv(uintptr_t, char*, int, int) noexcept;
extern "C" __declspec(dllimport) int setsockopt(uintptr_t, int, int, const char*, int) noexcept;

namespace {
constexpr uintptr_t InvalidSocket = ~uintptr_t{0};
constexpr int SocketError = -1;
constexpr int AfInet = 2, SockStream = 1, IpProtoTcp = 6;
constexpr int SolSocket = 0xffff, SoReceiveTimeout = 0x1006;
constexpr char Request[] = "MICANT-M10-PING";
constexpr char Response[] = "MICANT-M10-PONG";
constexpr char AcceptRequest[] = "MICA-ACCEPT-PING\n";
constexpr char AcceptResponse[] = "MICA-ACCEPT-PONG\n";

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

bool parseNumber(const char*& cursor, uint32_t maximum, uint32_t& result) noexcept {
    if (*cursor < '0' || *cursor > '9') return false;
    result = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        const uint32_t digit = static_cast<uint32_t>(*cursor++ - '0');
        if (result > (maximum - digit) / 10) return false;
        result = result * 10 + digit;
    }
    return result <= maximum;
}

bool parseEndpoint(const char* command, uint32_t& address, uint16_t& port) noexcept {
    if (!command) return false;
    const char* p = command;
    while (*p && *p != ' ') ++p;
    while (*p == ' ') ++p;
    uint32_t octets[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        if (!parseNumber(p, 255, octets[i])) return false;
        if (i != 3) { if (*p++ != '.') return false; }
    }
    if (*p++ != ' ') return false;
    uint32_t parsedPort{};
    if (!parseNumber(p, 65535, parsedPort) || !parsedPort) return false;
    while (*p == ' ') ++p;
    if (*p) return false;
    address = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
    port = static_cast<uint16_t>((parsedPort << 8) | (parsedPort >> 8));
    return true;
}

[[noreturn]] void finish(uint32_t status) noexcept {
    (void)syscall(0x2c, ~uint64_t{0}, status);
    for (;;) __asm__ __volatile__("pause");
}
}

extern "C" void _start(const char* = nullptr) noexcept {
    print("M10 TCP probe started\n");
    char commandLine[128]{};
    const char* command = micant::shared::native_process::currentCommandLineAscii(
        commandLine, sizeof(commandLine)) ? commandLine : nullptr;
    uint32_t address{};
    uint16_t port{};
    if (!parseEndpoint(command, address, port)) {
        print("M10 TCP probe: invalid endpoint arguments\n");
        finish(2);
    }

    struct WsaData {
        uint16_t version, highVersion, maxSockets, maxUdpDatagram;
        char* vendorInfo;
        char description[257], status[129];
    } startup{};
    if (WSAStartup(0x0202, &startup) != 0) {
        print("M10 TCP probe: WSAStartup failed\n");
        finish(3);
    }

    const uintptr_t endpoint = socket(AfInet, SockStream, IpProtoTcp);
    if (endpoint == InvalidSocket) {
        print("M10 TCP probe: socket creation failed\n");
        (void)WSACleanup();
        finish(4);
    }

    uint32_t timeout = 5000;
    struct SockaddrIn { int16_t family; uint16_t port; uint32_t address; uint8_t zero[8]; } remote{
        AfInet, port, address, {}};
    bool ok = setsockopt(endpoint, SolSocket, SoReceiveTimeout,
                         reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 &&
              connect(endpoint, &remote, sizeof(remote)) == 0;
    if (ok) {
        size_t sent{};
        while (sent < sizeof(Request) - 1) {
            const int count = send(endpoint, Request + sent,
                static_cast<int>(sizeof(Request) - 1 - sent), 0);
            if (count <= 0) { ok = false; break; }
            sent += static_cast<size_t>(count);
        }
    }
    char response[sizeof(Response) - 1]{};
    if (ok) {
        size_t received{};
        while (received < sizeof(response)) {
            const int count = recv(endpoint, response + received,
                static_cast<int>(sizeof(response) - received), 0);
            if (count <= 0) { ok = false; break; }
            received += static_cast<size_t>(count);
        }
        ok = ok && std::memcmp(response, Response, sizeof(response)) == 0;
    }
    if (ok) {
        print("M10 TCP client exchange passed\n");
        constexpr uint16_t GuestAcceptPort = 18081;
        const uintptr_t listener = socket(AfInet, SockStream, IpProtoTcp);
        const uint16_t networkPort = static_cast<uint16_t>((GuestAcceptPort << 8) |
                                                            (GuestAcceptPort >> 8));
        const SockaddrIn local{AfInet, networkPort, 0, {}};
        ok = listener != InvalidSocket && bind(listener, &local, sizeof(local)) == 0 &&
             listen(listener, 1) == 0;
        if (ok) {
            print("MicaNT guest TCP listener ready.\n");
            const uintptr_t accepted = accept(listener, nullptr, nullptr);
            ok = accepted != InvalidSocket;
            char request[sizeof(AcceptRequest) - 1]{};
            size_t received{};
            while (ok && received < sizeof(request)) {
                const int count = recv(accepted, request + received,
                    static_cast<int>(sizeof(request) - received), 0);
                if (count <= 0) { ok = false; break; }
                received += static_cast<size_t>(count);
            }
            ok = ok && std::memcmp(request, AcceptRequest, sizeof(request)) == 0;
            if (ok) ok = send(accepted, AcceptResponse, sizeof(AcceptResponse) - 1, 0) ==
                static_cast<int>(sizeof(AcceptResponse) - 1);
            if (accepted != InvalidSocket) (void)closesocket(accepted);
        }
        if (listener != InvalidSocket) (void)closesocket(listener);
    }
    (void)closesocket(endpoint);
    (void)WSACleanup();
    if (ok) {
        print("M10 TCP listen/accept exchange passed\n");
        finish(0);
    }
    print("M10 TCP client or listen/accept payload validation failed\n");
    finish(5);
}
