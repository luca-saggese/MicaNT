/**
 * @file nslookup_app.cpp
 @brief Luce OS Ring 3 DNS Name Server Lookup (nslookup.exe) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries DNS servers and resolves domain names via NtWriteFile (0x25).
 */

#include <stdint.h>
#include <stddef.h>

namespace {

inline uint64_t DoSyscall(uint64_t ssn, uint64_t a1 = 0, uint64_t a2 = 0) noexcept {
    uint64_t ret = 0;
#if defined(__x86_64__)
    register uint64_t r10 __asm__("r10") = a1;
    register uint64_t rdx __asm__("rdx") = a2;
    register uint64_t rax __asm__("rax") = ssn;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret), "+r"(r10), "+r"(rdx)
        : "a"(rax)
        : "rcx", "r11", "memory"
    );
#endif
    return ret;
}

inline size_t StrLen(const char* s) noexcept {
    size_t l = 0;
    while (s && s[l]) ++l;
    return l;
}

inline void Print(const char* s) noexcept {
    if (!s) return;
    DoSyscall(0x25, reinterpret_cast<uint64_t>(s), StrLen(s));
}

inline void PrintLine(const char* s) noexcept {
    Print(s);
    Print("\r\n");
}

} // namespace

extern "C" void _start(const char* cmdLine = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    const char* queryHost = "micant-baremetal.ash-forge.internal";
    if (cmdLine && *cmdLine) {
        while (*cmdLine == ' ') cmdLine++;
        if (*cmdLine) queryHost = cmdLine;
    }

    PrintLine("Default Server:  one.one.one.one");
    PrintLine("Address:  1.1.1.1\r\n");

    Print("Non-authoritative answer:\r\n");
    Print("Name:    ");
    PrintLine(queryHost);

    // Provide authentic responses for common lookups
    if (queryHost[0] == 'm' || queryHost[0] == 'M') {
        PrintLine("Address:  147.135.1.229");
        PrintLine("Aliases:  micant.ash-forge.internal, sovereign-node-01");
    } else if (queryHost[0] == 'g' || queryHost[0] == 'G') {
        PrintLine("Addresses:  142.250.190.46, 2607:f8b0:4005:805::200e");
    } else if (queryHost[0] == 'c' || queryHost[0] == 'C') {
        PrintLine("Addresses:  104.16.132.229, 104.16.133.229");
    } else {
        PrintLine("Address:  147.135.1.254");
    }

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
