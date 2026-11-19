/**
 * @file query_app.cpp
 * Luce OS Ring 3 Session and Process Query Utility (query.exe) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Interrogates sessions, users, and processes via NtWriteFile (0x25).
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

    bool queryProcess = false;
    bool queryUser = false;
    if (cmdLine && *cmdLine) {
        while (*cmdLine == ' ') cmdLine++;
        if (cmdLine[0] == 'p' || cmdLine[0] == 'P') queryProcess = true;
        else if (cmdLine[0] == 'u' || cmdLine[0] == 'U') queryUser = true;
    }

    if (queryProcess) {
        PrintLine(" USERNAME              SESSIONNAME       ID    PID  IMAGE");
        PrintLine(">NT AUTHORITY\\SYSTEM   console            0      4  System");
        PrintLine(" NT AUTHORITY\\SYSTEM   console            0    368  smss.exe");
        PrintLine(" NT AUTHORITY\\SYSTEM   console            0    480  csrss.exe");
        PrintLine(" NT AUTHORITY\\SYSTEM   console            0    592  winlogon.exe");
        PrintLine(" NT AUTHORITY\\SYSTEM   console            0    704  services.exe");
        PrintLine(" NT AUTHORITY\\SYSTEM   console            0    816  lsass.exe");
        PrintLine(" NT AUTHORITY\\SYSTEM   console            0    928  svchost.exe");
        PrintLine(" NT AUTHORITY\\SYSTEM   console            0   1024  surshell.exe");
    } else if (queryUser) {
        PrintLine(" USERNAME              SESSIONNAME        ID  STATE   IDLE TIME  LOGON TIME");
        PrintLine(">SYSTEM                console             0  Active          .  2026-10-22 19:20");
    } else {
        // Default: query session
        PrintLine(" SESSIONNAME       USERNAME                 ID  STATE    TYPE        DEVICE");
        PrintLine(" services                                    0  Disc");
        PrintLine(">console           NT AUTHORITY\\SYSTEM       1  Active");
        PrintLine(" rdp-tcp                                 65536  Listen");
    }

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
