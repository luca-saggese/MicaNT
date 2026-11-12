#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::hal::amd64 {

bool initializeBootstrapAddressSpace(void (*log)(const char*) noexcept) noexcept;
// BSP-only root ownership while IRQs are disabled. The root must be closed
// before its owner is destroyed and must not be copied.
struct ProcessPagingRoot {
    uint64_t pfn{0};
    ProcessPagingRoot* next{nullptr};
    ProcessPagingRoot() = default;
    ~ProcessPagingRoot();
    ProcessPagingRoot(const ProcessPagingRoot&) = delete;
    ProcessPagingRoot& operator=(const ProcessPagingRoot&) = delete;
};
bool createProcessPagingRoot(ProcessPagingRoot& root) noexcept;
bool verifyProcessPagingRootRollback() noexcept;
bool switchProcessPagingRoot(const ProcessPagingRoot* root) noexcept;
bool switchProcessPagingRootByPhysical(uintptr_t physicalRoot) noexcept;
bool destroyProcessPagingRoot(ProcessPagingRoot& root) noexcept;
// IRQ-disabled, single-CPU bootstrap test of PFN-backed paging and reclamation.
bool verifyBootstrapPageTables() noexcept;
// Bootstrap CR3 only; callbacks require the BSP with interrupts disabled.
inline constexpr uintptr_t BootstrapManagedBase = (uintptr_t{128} << 39) + 0x20000000;
inline constexpr size_t BootstrapManagedSize = 16 * 1024 * 1024;
bool mapBootstrapManagedPage(void* context, uintptr_t address, uint64_t pfn,
                             uint32_t protection) noexcept;
bool mapBootstrapManagedView(void* context, uintptr_t address, const uint64_t* pfns,
                             size_t count, uint32_t protection) noexcept;
bool unmapBootstrapManagedView(void* context, uintptr_t address, size_t count) noexcept;
bool verifyBootstrapManagedViewRollback() noexcept;
bool unmapBootstrapManagedPage(void* context, uintptr_t address) noexcept;
bool mapBootstrapProtectManagedPage(void* context, uintptr_t address,
                                    uint32_t protection) noexcept;
// Deliberate faults are restricted to the QEMU TCG bootstrap diagnostic.
bool bootstrapQemuFaultTestsAvailable() noexcept;
bool verifyBootstrapManagedPageFault(uintptr_t address, bool write,
                                     bool present) noexcept;
uint64_t translateBootstrapAddress(const void* address) noexcept;
void* mapBootstrapDeviceMemory(uint64_t physical, size_t size) noexcept;

// Runs isolated CPL=3 probes with checked #BP/#PF returns and page protections.
// This validates privilege transitions, not a process loader or Win32 session.
bool verifyRing3Transition() noexcept;
bool verifyManagedExecutablePage() noexcept;
// Temporary BSP-only xAPIC timer diagnostic; leaves interrupts disabled.
bool verifyBootstrapTimerInterrupt() noexcept;
// Starts the bounded BSP xAPIC scheduler clock after the diagnostic restores
// controller state. Legacy PIC and other local vectors remain masked.
bool startBootstrapSchedulerClock() noexcept;
bool verifyBootstrapSchedulerClock() noexcept;
bool bootstrapSchedulerClockFailed() noexcept;
// BSP xAPIC clock priority mask; no other interrupt vectors are routed yet.
void setBootstrapClockIrqlMask(uint8_t irql) noexcept;
uint64_t bootstrapSchedulerClockTicks() noexcept;

struct UserServiceArguments {
    uint64_t number, first, second, third, fourth, userStack;
};
using UserServiceRoutine = uint64_t (*)(const UserServiceArguments&) noexcept;
bool initializeBootstrapSyscalls(UserServiceRoutine dispatch) noexcept;
// Single-BSP context-switch hook; zero selects the bootstrap entry stacks.
void setBootstrapThreadEntryStack(uintptr_t stackTop, uintptr_t userTeb) noexcept;
uintptr_t bootstrapThreadSyscallStack() noexcept;
// Enter an already mapped user context in the current process. On success
// this does not return; caller must provide a valid thread-owned kernel stack.
[[noreturn]] void enterBootstrapUserThread(uintptr_t entry, uintptr_t userStack,
                                           bool interruptible = false) noexcept;
bool bootstrapUserRange(uint64_t address, size_t size, bool writable,
                        bool executable = false) noexcept;
bool bootstrapKernelRange(uint64_t address, size_t size, bool writable) noexcept;
bool verifyBootstrapSyscalls() noexcept;
bool mapBootstrapUserMemory(uint64_t address, const void* backing, size_t size,
                            bool writable, bool executable) noexcept;
bool verifyNativeImageLoading() noexcept;

} // namespace micant::hal::amd64
