#include "micant/shared/user_syscall.hpp"

#include <cstdint>

extern "C" void _start() noexcept {
    // QEMU-only disposable process used to verify that Task Manager obtains
    // PROCESS_TERMINATE through NtOpenProcess and terminates by object handle.
    // Burn a bounded user-mode interval so the 100 Hz Ke accounting source has
    // a measurable Ring 3 sample, then block briefly between CPU bursts. This
    // keeps the fixture schedulable and lets QEMU validate process/thread time
    // before Task Manager terminates it.
    for (uint64_t spin = 0; spin < 20'000'000; ++spin)
        __asm__ __volatile__("pause" : : : "memory");
    constexpr int64_t interval100ns = -100'000; // 10 ms
    for (;;) (void)micant::sys::invokeUserService(0x0034, 0,
        reinterpret_cast<uint64_t>(&interval100ns)); // NtDelayExecution
}
