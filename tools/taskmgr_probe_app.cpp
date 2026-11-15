#include "micant/shared/user_syscall.hpp"

#include <cstdint>

extern "C" void _start() noexcept {
    // QEMU-only disposable process used to verify that Task Manager obtains
    // PROCESS_TERMINATE through NtOpenProcess and terminates by object handle.
    // Remain in user mode until the timer accounts at least one process/thread
    // CPU sample and Task Manager opens this process with PROCESS_TERMINATE.
    // A bounded burst could finish between 100 Hz accounting ticks and leave
    // the self-test waiting forever for a CPU delta that can no longer occur.
    for (;;) __asm__ __volatile__("pause" : : : "memory");
}
