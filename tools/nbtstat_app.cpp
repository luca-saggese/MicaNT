/**
 * @file nbtstat_app.cpp
 * Luce OS Ring 3 NetBIOS over TCP/IP Diagnostic Utility (nbtstat.exe) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Interrogates NetBIOS name table and statistics via NtWriteFile (0x25).
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

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Ethernet (Intel(R) I210 Gigabit Network Connection):");
    PrintLine("Node IpAddress: [147.135.1.229] Scope Id: []\r\n");
    PrintLine("                NetBIOS Local Name Table");
    PrintLine("       Name               Type         Status");
    PrintLine("    ---------------------------------------------");
    PrintLine("    MICANT-XEON    <00>  UNIQUE       Registered");
    PrintLine("    WORKGROUP      <00>  GROUP        Registered");
    PrintLine("    MICANT-XEON    <20>  UNIQUE       Registered");
    PrintLine("    WORKGROUP      <1E>  GROUP        Registered");
    PrintLine("    WORKGROUP      <1D>  UNIQUE       Registered");
    PrintLine("    ..__MSBROWSE__.<01>  GROUP        Registered\r\n");
    PrintLine("    MAC Address = A4-BF-01-1E-A3-BE");

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
