#include "micant/shared/user_syscall.hpp"

#include <cstdint>

extern "C" void _start() noexcept {
    // QEMU-only disposable process used to verify that Task Manager obtains
    // PROCESS_TERMINATE through NtOpenProcess and terminates by object handle.
    // Yield back to the scheduler without blocking in a kernel wait: remote
    // termination is supported at a runnable thread boundary.
    for (;;) (void)micant::sys::invokeUserService(0x0046); // NtYieldExecution
}
