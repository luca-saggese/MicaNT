/**
 * @file svchost_app.cpp
 * @brief Sovereign Ring 3 Generic Host Process for Win32 Services (svchost.exe).
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Multi-service shared hosting container.
 */

#include <cstdint>
#include <cstddef>
#include "micant/shared/native_process_abi.hpp"

extern "C" __declspec(dllimport) int32_t NtTerminateProcess(void* processHandle,
                                                               int32_t exitStatus) noexcept;

namespace micant::services::dhcp {
uint32_t acquireLease(uint32_t timeoutMilliseconds, uint32_t* leaseSeconds) noexcept;
uint32_t resolveBootHost() noexcept;
}

namespace {

inline uint64_t DoSyscall(uint64_t ssn, uint64_t arg1 = 0, uint64_t arg2 = 0) noexcept {
    uint64_t res = 0;
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %1, %%rax\n\t"
        "movq %2, %%r10\n\t"
        "movq %3, %%rdx\n\t"
        "syscall\n\t"
        "movq %%rax, %0\n\t"
        : "=r"(res)
        : "r"(ssn), "r"(arg1), "r"(arg2)
        : "rax", "rdx", "r8", "r10", "rcx", "r11", "memory"
    );
#endif
    return res;
}

constexpr size_t StrLen(const char* s) noexcept {
    size_t len = 0;
    while (s && s[len] != '\0') ++len;
    return len;
}

inline void PrintLine(const char* s) noexcept {
    DoSyscall(0x25, reinterpret_cast<uint64_t>(s), StrLen(s));
}

bool Contains(const char* text, const char* token) noexcept {
    if (!text || !token || !*token) return false;
    const size_t tokenLength = StrLen(token);
    for (size_t i = 0; text[i]; ++i) {
        size_t j = 0;
        while (j < tokenLength && text[i + j] == token[j]) ++j;
        if (j == tokenLength) return true;
    }
    return false;
}

void PrintStatus(uint32_t status) noexcept {
    static constexpr char prefix[] = "DHCP acquisition failed; NTSTATUS 0x";
    static constexpr char hex[] = "0123456789ABCDEF";
    char line[sizeof(prefix) + 8]{};
    for (size_t i = 0; i + 1 < sizeof(prefix); ++i) line[i] = prefix[i];
    for (unsigned i = 0; i < 8; ++i) line[sizeof(prefix) - 1 + i] = hex[(status >> ((7 - i) * 4)) & 0xf];
    DoSyscall(0x25, reinterpret_cast<uint64_t>(line), sizeof(line) - 1);
}

} // namespace

extern "C" void _start(const char* = nullptr) {
    char commandLine[512]{};
    const char* cmdLine = micant::shared::native_process::currentCommandLineAscii(
        commandLine, sizeof(commandLine)) ? commandLine : nullptr;
    uint32_t serviceExitStatus{};
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Service Host Process (svchost.exe)");
    PrintLine("==================================");
    if (cmdLine && *cmdLine) {
        PrintLine("Group Parameter:    Passed via Command Line");
    } else {
        PrintLine("Group Parameter:    -k netsvcs (Network Services Container)");
    }
    if (!cmdLine || Contains(cmdLine, "netsvcs") ||
        Contains(cmdLine, "LocalServiceNetworkRestricted")) {
        uint32_t leaseSeconds{};
        const uint32_t status = micant::services::dhcp::acquireLease(2500, &leaseSeconds);
        if (status == 0) {
            PrintLine("DHCP lease committed through AFD.");
            if (cmdLine && Contains(cmdLine, "m10-test")) {
                const uint32_t dnsStatus = micant::services::dhcp::resolveBootHost();
                if (dnsStatus == 0) PrintLine("DNS A record for example.com resolved through Winsock/AFD.");
                else {
                    serviceExitStatus = dnsStatus;
                    PrintLine("DNS A resolution failed through Winsock/AFD.");
                }
            }
        } else {
            serviceExitStatus = status;
            PrintStatus(status);
        }
    }

    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}),
                             static_cast<int32_t>(serviceExitStatus));

    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
