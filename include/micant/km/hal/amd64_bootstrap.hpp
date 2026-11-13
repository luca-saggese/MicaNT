#pragma once

#include <cstddef>
#include <cstdint>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/system_time.hpp"

namespace micant::hal::amd64 {

// Serialized PCI Configuration Mechanism #1 access. These primitives only
// read or write configuration space; they do not claim devices or reserve
// BAR/IRQ/DMA resources. A failed parameter check performs no port I/O.
enum class PciConfigAccessResult : uint8_t {
    Success,
    InvalidParameter,
    UnsupportedPlatform,
};
enum class PciBarProbeResult : uint8_t { Success, Invalid, Absent, Active, RestoreFailed };
struct PciBarInfo { uint64_t address{}, length{}; bool ioSpace{}; };
struct BootstrapMsiMessage { uint64_t address{}, token{}; uint32_t data{}; uint8_t vector{}; };
using BootstrapMsiRoutine = void (*)(void*) noexcept;
// Probe only a quiescent function. The config lock covers sizing and restore.
[[nodiscard]] PciBarProbeResult probeQuiescentPciBar(
    uint16_t bus, uint16_t device, uint16_t function, uint8_t index, PciBarInfo& bar) noexcept;
[[nodiscard]] PciConfigAccessResult readPciConfig32(
    uint16_t bus, uint16_t device, uint16_t function, uint16_t registerOffset,
    uint32_t& value) noexcept;
[[nodiscard]] PciConfigAccessResult writePciConfig32(
    uint16_t bus, uint16_t device, uint16_t function, uint16_t registerOffset,
    uint32_t value) noexcept;
// Single-BSP message-signaled interrupt owner for freestanding boot drivers.
// The route is installed before the caller programs PCI MSI and must be
// disconnected only after the device has disabled its message generation.
[[nodiscard]] bool connectBootstrapMsiInterrupt(BootstrapMsiRoutine routine,
    void* context, BootstrapMsiMessage& message) noexcept;
[[nodiscard]] bool disconnectBootstrapMsiInterrupt(uint64_t token) noexcept;

// Layout of the bootstrap SYSCALL entry's saved user register frame. The
// syscall continuation service updates this frame before the common IRETQ.
struct SyscallFrame {
    uint64_t r15, r14, r13, r12, r10, r9, r8, rdx;
    uint64_t rsi, rdi, rbp, rbx, rax;
    uint64_t rcx, r11, rip, cs, flags, rsp, ss;
};
// Private state associated with a syscall entry; it is not part of the
// public NT CONTEXT or the assembly-visible IRET frame.
struct UserSegmentState {
    uint16_t ds{}, es{}, fs{}, gs{};
    uint64_t fsBase{}, userGsBase{};
};
struct ThreadSegmentState {
    uint16_t ds{0x18}, es{0x18}, fs{}, gs{};
    uint64_t fsBase{}, userGsBase{};
    bool saved{};
};
inline constexpr uint64_t BootstrapNtContinueApplied = 0x4D4943414E54434EULL;
static_assert(sizeof(SyscallFrame) == 160);
static_assert(offsetof(SyscallFrame, rax) == 96);
static_assert(offsetof(SyscallFrame, rcx) == 104);
static_assert(offsetof(SyscallFrame, r11) == 112);
static_assert(offsetof(SyscallFrame, rip) == 120);

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
bool mapBootstrapGuardManagedPage(void* context, uintptr_t address, bool guarded) noexcept;
bool replaceBootstrapManagedPage(void* context, uintptr_t address, uint64_t expectedPfn,
                                 uint64_t replacementPfn, uint32_t protection) noexcept;
// Deliberate faults are restricted to the QEMU TCG bootstrap diagnostic.
bool bootstrapQemuFaultTestsAvailable() noexcept;
bool verifyBootstrapManagedPageFault(uintptr_t address, bool write,
                                     bool present) noexcept;
uint64_t translateBootstrapAddress(const void* address) noexcept;
// Returns the first physical byte only when the complete non-empty range is
// mapped by the bootstrap page tables to contiguous physical pages within the
// bootstrap physical-address mask. The caller must keep the kernel-owned
// buffer alive and stable for the transfer; this does not pin pages, allocate
// DMA memory, create an IOMMU mapping, or make arbitrary user buffers safe.
// Zero means invalid, unmapped, non-contiguous, overflowing, or unsupported.
uint64_t translateBootstrapDmaRange(const void* address, size_t size) noexcept;
void* mapBootstrapDeviceMemory(uint64_t physical, size_t size) noexcept;
void* mapBootstrapDmaMemory(uint64_t physical, size_t size) noexcept;
[[nodiscard]] bool protectBootstrapIommuRegisters(uint64_t physical, size_t size,
                                                   const void* ownerMapping) noexcept;
void commitBootstrapIommuProtection() noexcept;
// Release the most recent bootstrap MMIO mapping; callers serialize with IRQs disabled.
bool releaseBootstrapDeviceMemory(void* mapping, size_t size) noexcept;
// Caller validates the firmware memory type; the temporary mapping is removed
// before returning and requires the bootstrap paging root with IRQs disabled.
bool copyBootstrapFirmwareMemory(uint64_t physical, void* destination, size_t size) noexcept;

// Runs isolated CPL=3 probes with checked #BP/#PF returns and page protections.
// This validates privilege transitions, not a process loader or Win32 session.
bool verifyRing3Transition() noexcept;
bool verifyManagedExecutablePage() noexcept;
// Temporary BSP-only xAPIC timer-source diagnostic; leaves interrupts disabled.
bool verifyBootstrapTimerSource() noexcept;
// Includes scheduler preemption and user-mode preemption checks.
bool verifyBootstrapTimerInterrupt() noexcept;
// Starts the bounded BSP local-APIC scheduler clock after the diagnostic restores
// controller state. Legacy PIC and other local vectors remain masked.
bool startBootstrapSchedulerClock() noexcept;
bool verifyBootstrapSchedulerClock() noexcept;
bool bootstrapSchedulerClockFailed() noexcept;
bool bootstrapSchedulerClockActive() noexcept;
uintptr_t bootstrapKernelPagingRoot() noexcept;
// Installs the BSP IRQ12 gate and leaves the IOAPIC route masked.
bool prepareBootstrapPs2Interrupt(uint32_t& gsi) noexcept;
// Synchronize the BSP's Ke IRQL with the local APIC priority classes.
void setBootstrapClockIrqlMask(uint8_t irql) noexcept;
uint64_t bootstrapSchedulerClockTicks() noexcept;

enum class BootstrapClockStatus : uint8_t {
    Success,
    ClockUnavailable,
    CalibrationFailed,
    NotBootstrapProcessor,
    InvalidIrql,
    InvalidParameter,
    CounterOverflow,
    UnsupportedPlatform,
};
struct BootstrapDeadline {
    uint64_t counter{};
};
// Calibrates invariant TSC from CPUID.15H or five PIT channel-2 intervals.
// A validated firmware Stall calibration may be installed first as fallback.
// Call at PASSIVE_LEVEL on the BSP with interrupts disabled. The PIT path
// restores the speaker gate but reprograms channel 2; callers must reserve it
// for bootstrap calibration. The scheduler/APIC clock is not changed.
[[nodiscard]] BootstrapClockStatus initializeBootstrapHighResolutionClock() noexcept;
[[nodiscard]] BootstrapClockStatus setBootstrapTscCalibrationHint(
    uint64_t frequencyHz) noexcept;
// Returns the calibrated BSP invariant-TSC counter/frequency. Query is
// non-blocking and usable at any IRQL on the BSP after successful calibration.
[[nodiscard]] BootstrapClockStatus queryBootstrapClock(
    uint64_t& counter, uint64_t& frequency) noexcept;
// Installs a UTC FILETIME value against the calibrated monotonic counter.
// The system clock is initialized once; an unknown/local firmware time must
// not be passed as UTC. Queries fail until both a calibrated counter and a
// validated UTC seed are available.
[[nodiscard]] BootstrapClockStatus initializeBootstrapSystemTime(
    uint64_t utcTime100ns) noexcept;
[[nodiscard]] BootstrapClockStatus queryBootstrapSystemTime(
    uint64_t& utcTime100ns) noexcept;
[[nodiscard]] BootstrapClockStatus queryBootstrapUptime(
    uint64_t& uptime100ns) noexcept;
[[nodiscard]] BootstrapClockStatus queryBootstrapPerformanceCounter(
    uint64_t& counter, uint64_t& frequency) noexcept;
[[nodiscard]] BootstrapClockStatus initializeBootstrapKuserSharedData() noexcept;
[[nodiscard]] BootstrapClockStatus updateBootstrapKuserSharedData() noexcept;
[[nodiscard]] micant::KUserSharedData* bootstrapKuserSharedData() noexcept;
[[nodiscard]] bool bootstrapKuserSharedDataReady() noexcept;
[[nodiscard]] bool mapBootstrapKuserSharedDataForRoot(uint64_t rootPhysical) noexcept;

enum class BootstrapRtcStatus : uint8_t {
    Success,
    Timeout,
    InvalidData,
    UnsupportedPlatform,
    InvalidIrql,
};
// Reads a stable CMOS RTC sample. The RTC does not carry a UTC offset, so this
// API returns civil fields only and never labels the result UTC.
[[nodiscard]] BootstrapRtcStatus readBootstrapRtc(
    micant::shared::time::SystemTimeFields& fields) noexcept;
// Creates a bounded absolute deadline on the calibrated TSC timebase. Timeout
// must be 1..10,000 ms; the deadline is rejected if its addition overflows.
[[nodiscard]] BootstrapClockStatus makeBootstrapDeadline(
    uint32_t timeoutMilliseconds, BootstrapDeadline& deadline) noexcept;
// Reports whether an absolute deadline has expired. A deadline remains valid
// only on the BSP and calibration instance that created it.
[[nodiscard]] BootstrapClockStatus isBootstrapDeadlineExpired(
    const BootstrapDeadline& deadline, bool& expired) noexcept;
// Busy-waits to an absolute deadline; no scheduler wait or yield occurs.
// Millisecond waits require PASSIVE_LEVEL. This has no independent watchdog
// if TSC stops or the CPU is suspended after the initial query.
[[nodiscard]] BootstrapClockStatus delayBootstrapMilliseconds(
    uint32_t milliseconds) noexcept;
[[nodiscard]] bool verifyBootstrapClockTimeouts() noexcept;
// KeStallExecutionProcessor-like bounded busy stall: at most 50 microseconds,
// callable through DISPATCH_LEVEL on the calibrated BSP. It does not promise
// precision better than the calibrated TSC/virtual-machine time source.
[[nodiscard]] BootstrapClockStatus delayBootstrapMicroseconds(
    uint32_t microseconds) noexcept;

struct UserServiceArguments {
    uint64_t number, first, second, third, fourth, userStack;
    // Kernel-only continuation plumbing for NtContinue. These point to the
    // active entry frame and its saved XSAVE area on the current syscall stack.
    void* trapFrame{};
    void* floatingState{};
    size_t floatingStateSize{};
    uint32_t xsaveMask{};
    UserSegmentState* userSegments{};
};
using UserServiceRoutine = uint64_t (*)(const UserServiceArguments&) noexcept;
bool initializeBootstrapSyscalls(UserServiceRoutine dispatch) noexcept;
// Single-BSP context-switch hook; zero selects the bootstrap entry stacks.
void setBootstrapThreadEntryStack(uintptr_t stackTop, uintptr_t userTeb,
                                  bool syscallGsActive = false, uintptr_t stackBase = 0,
                                  uintptr_t executionStackTop = 0) noexcept;
bool bootstrapSyscallGsActive() noexcept;
void saveBootstrapThreadSegments(ThreadSegmentState& state, bool syscallGsActive) noexcept;
void restoreBootstrapThreadSegments(const ThreadSegmentState& state) noexcept;
bool bootstrapStackHasHeadroom(uintptr_t stackPointer, uintptr_t threadBase,
                               uintptr_t threadTop, size_t required,
                               bool terminatedThread = false) noexcept;
uintptr_t bootstrapThreadSyscallStack() noexcept;
// Enter an already mapped user context in the current process. On success
// this does not return; caller must provide a valid thread-owned kernel stack.
[[noreturn]] void enterBootstrapUserThread(uintptr_t entry, uintptr_t userStack,
                                           bool interruptible = false,
                                           uintptr_t startRoutine = 0,
                                           uintptr_t startArgument = 0) noexcept;
bool bootstrapUserRange(uint64_t address, size_t size, bool writable,
                        bool executable = false) noexcept;
bool bootstrapKernelRange(uint64_t address, size_t size, bool writable) noexcept;
bool verifyBootstrapSyscalls() noexcept;
bool mapBootstrapUserMemory(uint64_t address, const void* backing, size_t size,
                            bool writable, bool executable) noexcept;
bool verifyNativeImageLoading() noexcept;

} // namespace micant::hal::amd64
