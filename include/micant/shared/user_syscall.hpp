#pragma once

#include <cstdint>

namespace micant::sys {

// Private service transport; service numbers are not a public Windows ABI.
inline uint64_t invokeUserService(uint64_t number, uint64_t first = 0,
                                 uint64_t second = 0) noexcept {
#if defined(__x86_64__)
    register uint64_t argument asm("r10") = first;
    asm volatile("syscall"
                 : "+a"(number), "+r"(argument), "+d"(second)
                 :
                 : "rcx", "r11", "memory", "cc");
    return number;
#else
    return 0xC00000BB; // STATUS_NOT_SUPPORTED
#endif
}

// Four-argument native services use the AMD64 syscall register convention:
// the first argument is moved to R10 by SYSCALL, followed by RDX, R8 and R9.
inline uint64_t invokeUserService4(uint64_t number, uint64_t first,
                                  uint64_t second, uint64_t third,
                                  uint64_t fourth) noexcept {
#if defined(__x86_64__)
    register uint64_t argument asm("r10") = first;
    register uint64_t argument3 asm("r8") = third;
    register uint64_t argument4 asm("r9") = fourth;
    asm volatile("syscall"
                 : "+a"(number), "+r"(argument), "+d"(second),
                   "+r"(argument3), "+r"(argument4)
                 :
                 : "rcx", "r11", "memory", "cc");
    return number;
#else
    (void)number; (void)first; (void)second; (void)third; (void)fourth;
    return 0xC00000BB; // STATUS_NOT_SUPPORTED
#endif
}

} // namespace micant::sys
