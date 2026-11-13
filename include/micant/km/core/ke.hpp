#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <atomic>
#include <array>
#include <deque>
#include <functional>
#include <chrono>
#include <optional>
#include <algorithm>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#if defined(MICANT_FREESTANDING)
#include "micant/km/hal/amd64_bootstrap.hpp"
namespace micant::ps { class EProcess; class EThread; }
#endif

namespace micant::ke {

// ============================================================================
// 1. Interrupt Request Levels (IRQL)
// ============================================================================
using KIRQL = uint8_t;

inline constexpr KIRQL PASSIVE_LEVEL      = 0;  // Normal user / kernel execution; paging allowed
inline constexpr KIRQL APC_LEVEL          = 1;  // Asynchronous Procedure Calls; paging allowed
inline constexpr KIRQL DISPATCH_LEVEL     = 2;  // Thread scheduler & DPCs; NO PAGE FAULTS ALLOWED
inline constexpr KIRQL DIRQL_MIN          = 3;  // Device Interrupts minimum
inline constexpr KIRQL DIRQL_MAX          = 26; // Device Interrupts maximum
inline constexpr KIRQL PROFILE_LEVEL      = 27; // Profiling timer
inline constexpr KIRQL CLOCK_LEVEL        = 28; // System clock timer
inline constexpr KIRQL IPI_LEVEL          = 29; // Inter-Processor Interrupt
inline constexpr KIRQL POWER_LEVEL        = 30; // Power failure interrupt
inline constexpr KIRQL HIGH_LEVEL         = 31; // Mask all interrupts
inline constexpr uint8_t KernelMode = 0;
inline constexpr uint8_t UserMode = 1;

// Forward declarations
struct KThread;
struct KDPC;
struct KAPC;
struct KAPC_STATE;

// ============================================================================
// 2. Processor Control Region (KPCR) & Processor Control Block (KPRCB)
// ============================================================================
/**
 * @brief Processor Control Region (KPCR) and KPRCB tracking per CPU core.
 * Clean-room implementation of Windows NT per-CPU executive structures.
 */
struct ProcessorControlBlock {
    uint32_t processorNumber{0};
    KIRQL currentIrql{PASSIVE_LEVEL};
    uint64_t interruptCount{0};
    uint64_t dpcCount{0};
    uint64_t apcCount{0};
    uint64_t contextSwitches{0};
#if defined(MICANT_FREESTANDING)
    uintptr_t currentCr3{0};
#else
    uintptr_t currentCr3{0x1000};
#endif
    KThread* currentThread{nullptr};
    KThread* nextThread{nullptr};
    KThread* idleThread{nullptr};
    bool dpcRoutineActive{false};
#if defined(MICANT_FREESTANDING)
    void (*serviceDeferredCleanup)() noexcept{nullptr};
#endif
};

// The bootstrap target is single-CPU and has no TLS runtime; hosted builds
// retain thread-local state for processor-context tests.
#if defined(MICANT_FREESTANDING)
inline ProcessorControlBlock g_CurrentCpu{
#else
inline thread_local ProcessorControlBlock g_CurrentCpu{
#endif
    .processorNumber = 0,
    .currentIrql = PASSIVE_LEVEL,
    .interruptCount = 0,
    .dpcCount = 0,
    .apcCount = 0,
    .contextSwitches = 0,
#if defined(MICANT_FREESTANDING)
    .currentCr3 = 0,
#else
    .currentCr3 = 0x1000,
#endif
    .currentThread = nullptr,
    .nextThread = nullptr,
    .idleThread = nullptr,
    .dpcRoutineActive = false
};

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
namespace detail {
// Return paths enter with IF clear and may still carry the temporary SYSCALL
// GS base and user data selectors. Deferred kernel callbacks must see the
// same thread GS and segments as ordinary kernel execution.
struct ReturnKernelContext {
    uint16_t ds{}, es{};
    bool swapped{};
    ReturnKernelContext() noexcept {
        __asm__ __volatile__("movw %%ds, %0; movw %%es, %1" : "=rm"(ds), "=rm"(es));
        swapped = hal::amd64::bootstrapSyscallGsActive();
        if (swapped) __asm__ __volatile__("swapgs" : : : "memory");
        __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es"
                             : : : "rax", "memory");
    }
    ~ReturnKernelContext() noexcept {
        __asm__ __volatile__("movw %0, %%ds; movw %1, %%es" : : "rm"(ds), "rm"(es) : "memory");
        if (swapped) __asm__ __volatile__("swapgs" : : : "memory");
    }
};
// Dispatch only at a boundary where the caller has finished changing state.
void dispatchHigherPriority() noexcept;
// Caller has interrupts disabled and may safely allow deferred work to run.
void servicePendingDpcs() noexcept;
void servicePendingKernelApcs(bool callerHadInterrupts = false) noexcept;
}
#endif

[[nodiscard]] inline KIRQL KeGetCurrentIrql() noexcept {
    return g_CurrentCpu.currentIrql;
}

inline KIRQL KfRaiseIrql(KIRQL newIrql) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
    KIRQL old = g_CurrentCpu.currentIrql;
    if (newIrql > old) {
        g_CurrentCpu.currentIrql = newIrql;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        hal::amd64::setBootstrapClockIrqlMask(newIrql);
#endif
    }
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
    return old;
}

inline void KeLowerIrql(KIRQL newIrql) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
    g_CurrentCpu.currentIrql = newIrql;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    hal::amd64::setBootstrapClockIrqlMask(newIrql);
    if (flags & (uint64_t{1} << 9)) {
        if (newIrql <= APC_LEVEL) {
            detail::servicePendingDpcs();
            detail::dispatchHigherPriority();
            if (newIrql == PASSIVE_LEVEL) detail::servicePendingKernelApcs(true);
        }
        __asm__ __volatile__("sti" : : : "memory");
    }
#endif
}

[[nodiscard]] inline uint32_t KeGetCurrentProcessorNumber() noexcept {
    return g_CurrentCpu.processorNumber;
}

// Executive-facing clock queries. These return false when the bare-metal HAL
// has no calibrated counter or UTC seed; callers must propagate that failure.
[[nodiscard]] bool KeQuerySystemTime100ns(uint64_t& systemTime100ns) noexcept;
[[nodiscard]] bool KeQueryUptime100ns(uint64_t& uptime100ns) noexcept;
[[nodiscard]] bool KeQueryPerformanceCounterValue(
    uint64_t& counter, uint64_t& frequency) noexcept;

[[nodiscard]] inline KThread* KeGetCurrentThread() noexcept {
    return g_CurrentCpu.currentThread;
}

#if defined(MICANT_FREESTANDING)
[[nodiscard]] ps::EProcess* KeGetCurrentProcess() noexcept;

extern "C" __declspec(dllexport) void KeStackAttachProcess(
    ps::EProcess* process, KAPC_STATE* apcState) noexcept;
extern "C" __declspec(dllexport) void KeUnstackDetachProcess(
    KAPC_STATE* apcState) noexcept;
namespace detail {
bool unwindCurrentProcessAttachments() noexcept;
}
#endif


// ============================================================================
// 3. Hardware Trap Frame (KTRAP_FRAME)
// ============================================================================
/**
 * @brief Hardware and Software Trap Frame (KTRAP_FRAME).
 * Exact clean-room mapping of the x86-64 NT kernel trap frame.
 */
struct KTrapFrame {
    // Microsoft x64 FastCall Home Locations (P1-P4)
    uint64_t p1Home{0};
    uint64_t p2Home{0};
    uint64_t p3Home{0};
    uint64_t p4Home{0};
    uint64_t p5{0};

    // Volatile / Scratch Registers
    uint64_t rax{0};
    uint64_t rcx{0};
    uint64_t rdx{0};
    uint64_t r8{0};
    uint64_t r9{0};
    uint64_t r10{0};
    uint64_t r11{0};

    // Preserved / Non-Volatile Callee-Saved Registers
    uint64_t rbx{0};
    uint64_t rbp{0};
    uint64_t rsi{0};
    uint64_t rdi{0};
    uint64_t r12{0};
    uint64_t r13{0};
    uint64_t r14{0};
    uint64_t r15{0};

    // Hardware Push Frame (saved by CPU upon interruption/fault)
    uint64_t rip{0};
    uint16_t segCs{0x33}; // Ring 3 User Code default
    uint16_t fill0{0};
    uint32_t fill1{0};
    uint32_t eFlags{0x202}; // IF (Interrupt Flag) enabled
    uint32_t fill2{0};
    uint64_t rsp{0};
    uint16_t segSs{0x2B}; // Ring 3 User Data default
    uint16_t fill3{0};
    uint32_t fill4{0};

    // Segment Registers
    uint16_t segDs{0x2B};
    uint16_t segEs{0x2B};
    uint16_t segFs{0x53};
    uint16_t segGs{0x2B};

    // Diagnostic & Fault Metadata
    uint64_t faultAddress{0};
    uint32_t errorCode{0};
    uint8_t  previousMode{1}; // 0 = KernelMode, 1 = UserMode
    uint8_t  exceptionActive{0};
    uint16_t mxCsr{0x1F80};   // Standard IEEE 754 SSE control/status
};

// ============================================================================
// 4. Kernel Spinlocks (KSPIN_LOCK)
// ============================================================================
/**
 * @brief Native NT Kernel Spinlock.
 * Acquiring raises IRQL to DISPATCH_LEVEL and spins atomically.
 * Releasing restores previous IRQL.
 */
class SpinLock {
public:
    SpinLock() = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    KIRQL acquire() noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
        KIRQL oldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        while (lockFlag_.test_and_set(std::memory_order_acquire)) {
#if defined(_M_X64) || defined(__x86_64__)
            _mm_pause();
#endif
        }
        ownerCpu_ = g_CurrentCpu.processorNumber;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        restoreInterrupts_ = (flags & (uint64_t{1} << 9)) != 0;
#endif
        return oldIrql;
    }

    void release(KIRQL oldIrql) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        const bool restoreInterrupts = restoreInterrupts_;
#endif
        ownerCpu_ = 0xFFFFFFFF;
        lockFlag_.clear(std::memory_order_release);
        KeLowerIrql(oldIrql);
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        if (restoreInterrupts) {
            if (oldIrql <= APC_LEVEL) {
                detail::servicePendingDpcs();
                detail::dispatchHigherPriority();
                if (oldIrql == PASSIVE_LEVEL) detail::servicePendingKernelApcs(true);
            }
            __asm__ __volatile__("sti" : : : "memory");
        }
#endif
    }

    [[nodiscard]] bool isLocked() const noexcept {
        return lockFlag_.test(std::memory_order_relaxed);
    }

private:
    std::atomic_flag lockFlag_ = ATOMIC_FLAG_INIT;
    uint32_t ownerCpu_{0xFFFFFFFF};
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    bool restoreInterrupts_{false};
#endif
};

/**
 * @brief RAII SpinLock Guard.
 */
class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& lock) noexcept
        : lock_(lock), previousIrql_(lock_.acquire()) {}

    ~SpinLockGuard() noexcept {
        lock_.release(previousIrql_);
    }

    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

    [[nodiscard]] KIRQL getPreviousIrql() const noexcept { return previousIrql_; }

private:
    SpinLock& lock_;
    KIRQL previousIrql_;
};

// ============================================================================
// 5. Deferred Procedure Calls (KDPC)
// ============================================================================
enum class DpcImportance : uint32_t {
    Low,
    Medium,
    High,
    HighPriority
};

using PKDEFERRED_ROUTINE = void (*)(KDPC* dpc, void* deferredContext, void* sysArg1, void* sysArg2);

struct KDPC {
    PKDEFERRED_ROUTINE routine{nullptr};
    void* deferredContext{nullptr};
    void* systemArgument1{nullptr};
    void* systemArgument2{nullptr};
    DpcImportance importance{DpcImportance::Medium};
    uint32_t targetProcessor{0};
    bool inserted{false};
#if defined(MICANT_FREESTANDING)
    KDPC* queueNext{nullptr};
#endif
};

#if defined(MICANT_FREESTANDING)
// Single-BSP intrusive queue. Queue manipulation keeps interrupts disabled;
// callbacks run at DISPATCH_LEVEL and may requeue their own DPC after removal.
class DpcQueue {
public:
    static DpcQueue& get() noexcept {
        static constinit DpcQueue instance;
        return instance;
    }

    bool queueDpc(KDPC* dpc, void* arg1 = nullptr, void* arg2 = nullptr) noexcept {
        if (!dpc || !dpc->routine || dpc->targetProcessor != 0) return false;
        SpinLockGuard guard(lock_);
        if (dpc->inserted) return false;
        dpc->systemArgument1 = arg1;
        dpc->systemArgument2 = arg2;
        dpc->inserted = true;
        dpc->queueNext = nullptr;
        if (dpc->importance == DpcImportance::High || dpc->importance == DpcImportance::HighPriority) {
            dpc->queueNext = head_;
            head_ = dpc;
            if (!tail_) tail_ = dpc;
        } else {
            if (tail_) tail_->queueNext = dpc;
            else head_ = dpc;
            tail_ = dpc;
        }
        ++count_;
        return true;
    }

    size_t drainDpcs(bool mayEnableInterrupts = false) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
        if (g_CurrentCpu.dpcRoutineActive || KeGetCurrentIrql() > DISPATCH_LEVEL) {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
            return 0;
        }
        const KIRQL oldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        g_CurrentCpu.dpcRoutineActive = true;
        size_t executed = 0;
        for (;;) {
            PKDEFERRED_ROUTINE routine{};
            KDPC* dpc{};
            void *context{}, *arg1{}, *arg2{};
            {
                SpinLockGuard guard(lock_);
                dpc = head_;
                if (dpc) {
                    head_ = dpc->queueNext;
                    if (!head_) tail_ = nullptr;
                    dpc->queueNext = nullptr;
                    dpc->inserted = false;
                    --count_;
                    routine = dpc->routine;
                    context = dpc->deferredContext;
                    arg1 = dpc->systemArgument1;
                    arg2 = dpc->systemArgument2;
                }
            }
            if (!dpc) break;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            if (mayEnableInterrupts || (flags & (uint64_t{1} << 9)))
                __asm__ __volatile__("sti" : : : "memory");
#endif
            routine(dpc, context, arg1, arg2);
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            __asm__ __volatile__("cli" : : : "memory");
#endif
            ++g_CurrentCpu.dpcCount;
            ++executed;
        }
        g_CurrentCpu.dpcRoutineActive = false;
        KeLowerIrql(oldIrql);
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
        return executed;
    }

    bool removeDpc(KDPC* dpc) noexcept {
        if (!dpc) return false;
        SpinLockGuard guard(lock_);
        KDPC* previous{};
        for (auto* entry = head_; entry; entry = entry->queueNext) {
            if (entry != dpc) { previous = entry; continue; }
            if (previous) previous->queueNext = entry->queueNext;
            else head_ = entry->queueNext;
            if (tail_ == entry) tail_ = previous;
            entry->queueNext = nullptr;
            entry->inserted = false;
            --count_;
            return true;
        }
        return false;
    }

    [[nodiscard]] size_t getQueuedCount() noexcept {
        SpinLockGuard guard(lock_);
        return count_;
    }

private:
    DpcQueue() = default;
    SpinLock lock_;
    KDPC* head_{};
    KDPC* tail_{};
    size_t count_{};
};
#else
class DpcQueue {
public:
    static DpcQueue& get() {
        static DpcQueue instance;
        return instance;
    }

    bool queueDpc(KDPC* dpc, void* arg1 = nullptr, void* arg2 = nullptr) {
        if (!dpc || !dpc->routine) return false;
        SpinLockGuard guard(lock_);
        if (dpc->inserted) return false;

        dpc->systemArgument1 = arg1;
        dpc->systemArgument2 = arg2;
        dpc->inserted = true;
        entries_.push_back(dpc);
        return true;
    }

    size_t drainDpcs() {
        std::vector<KDPC*> toExecute;
        {
            SpinLockGuard guard(lock_);
            toExecute.swap(entries_);
            for (auto* dpc : toExecute) {
                dpc->inserted = false;
            }
        }

        if (toExecute.empty()) return 0;

        // DPCs execute strictly at DISPATCH_LEVEL
        KIRQL oldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        g_CurrentCpu.dpcRoutineActive = true;

        for (auto* dpc : toExecute) {
            dpc->routine(dpc, dpc->deferredContext, dpc->systemArgument1, dpc->systemArgument2);
            g_CurrentCpu.dpcCount++;
        }

        g_CurrentCpu.dpcRoutineActive = false;
        KeLowerIrql(oldIrql);
        return toExecute.size();
    }

    bool removeDpc(KDPC* dpc) {
        if (!dpc || !dpc->inserted) return false;
        SpinLockGuard guard(lock_);
        auto it = std::find(entries_.begin(), entries_.end(), dpc);
        if (it != entries_.end()) {
            entries_.erase(it);
            dpc->inserted = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] size_t getQueuedCount() const {
        return entries_.size();
    }

private:
    DpcQueue() = default;
    SpinLock lock_;
    std::vector<KDPC*> entries_;
};
#endif

inline void KiRetireDpcList() {
#if defined(MICANT_FREESTANDING)
    if (!g_CurrentCpu.dpcRoutineActive) DpcQueue::get().drainDpcs();
#else
    if (!g_CurrentCpu.dpcRoutineActive && DpcQueue::get().getQueuedCount() > 0) {
        DpcQueue::get().drainDpcs();
    }
#endif
}

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
namespace detail {
inline void servicePendingDpcs() noexcept {
    if (KeGetCurrentIrql() <= APC_LEVEL && !g_CurrentCpu.dpcRoutineActive) {
        ReturnKernelContext kernelContext;
        DpcQueue::get().drainDpcs(true);
    }
}
}
#endif

inline void KeInitializeDpc(KDPC* dpc, PKDEFERRED_ROUTINE routine, void* deferredContext) {
    if (dpc) {
        dpc->routine = routine;
        dpc->deferredContext = deferredContext;
        dpc->systemArgument1 = nullptr;
        dpc->systemArgument2 = nullptr;
        dpc->importance = DpcImportance::Medium;
        dpc->targetProcessor = 0;
        dpc->inserted = false;
#if defined(MICANT_FREESTANDING)
        dpc->queueNext = nullptr;
#endif
    }
}

inline bool KeInsertQueueDpc(KDPC* dpc, void* systemArgument1 = nullptr, void* systemArgument2 = nullptr) {
    return DpcQueue::get().queueDpc(dpc, systemArgument1, systemArgument2);
}

inline bool KeRemoveQueueDpc(KDPC* dpc) {
    return DpcQueue::get().removeDpc(dpc);
}

// ============================================================================
// 6. Asynchronous Procedure Calls (KAPC)
// ============================================================================
enum class ApcEnvironment : uint8_t {
    OriginalApcEnvironment,
    AttachedApcEnvironment,
    CurrentApcEnvironment,
    InsertApcEnvironment
};

inline constexpr uint8_t ApcStateIndexOriginal = 0;
inline constexpr uint8_t ApcStateIndexAttached = 1;
inline constexpr uint8_t ApcStateIndexInsert = 0xFF;
inline constexpr uint8_t ApcStateIndexInvalid = 0xFE;

using PKNORMAL_ROUTINE = void (*)(void* normalContext, void* sysArg1, void* sysArg2);
using PKKERNEL_ROUTINE = void (*)(KAPC* apc, PKNORMAL_ROUTINE* normalRoutine, void** normalContext, void** sysArg1, void** sysArg2);
using PKRUNDOWN_ROUTINE = void (*)(KAPC* apc);

struct KAPC {
    Handle targetTid{0};
    // The canonical initializer borrows this exact target until insertion;
    // insertion converts it to a queue-owned pin. Legacy TID callers resolve
    // by ID at insertion for compatibility.
    KThread* targetThread{nullptr};
    bool targetThreadBorrowed{false};
    bool exactTargetRequired{false};
    PKKERNEL_ROUTINE kernelRoutine{nullptr};
    PKRUNDOWN_ROUTINE rundownRoutine{nullptr};
    PKNORMAL_ROUTINE normalRoutine{nullptr};
    void* normalContext{nullptr};
    void* systemArgument1{nullptr};
    void* systemArgument2{nullptr};
    uint8_t apcMode{0}; // 0 = KernelMode, 1 = UserMode
    // Resolved APC-state index (Original/Attached) or Insert sentinel.
    uint8_t apcStateIndex{ApcStateIndexInsert};
    bool inserted{false};
    bool isSpecialKernelApc{false};
    bool targetReferenceHeld{false};
#if defined(MICANT_FREESTANDING)
    KAPC* queueNext{nullptr};
#endif
};

// WDK KAPC_STATE layout. The canonical APC queues remain in KThread; this
// caller-owned state is the attach/detach token and preserves the public AMD64
// size and alignment expected by driver stack allocations.
struct KAPC_STATE_LIST_ENTRY {
    KAPC_STATE_LIST_ENTRY* Flink{};
    KAPC_STATE_LIST_ENTRY* Blink{};
};
struct KAPC_STATE {
    KAPC_STATE_LIST_ENTRY ApcListHead[2]{};
    void* Process{};
    bool KernelApcInProgress{};
    bool KernelApcPending{};
    bool UserApcPending{};
};
static_assert(sizeof(KAPC_STATE) == 48 && alignof(KAPC_STATE) == 8);

#if defined(MICANT_FREESTANDING)
// Intrusive FIFO for resident APC objects; its owner serializes operations.
class ApcQueue {
public:
    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }
    [[nodiscard]] KAPC* front() const noexcept { return head_; }
    void push_back(KAPC* apc) noexcept {
        apc->queueNext = nullptr;
        if (tail_) tail_->queueNext = apc; else head_ = apc;
        tail_ = apc;
    }
    // Preserve one intrusive queue. Delivery selects only APCs associated
    // with the target's active APC state and gives special APCs priority.
    void pushKernel(KAPC* apc) noexcept {
        push_back(apc);
    }
    void pop_front() noexcept {
        if (!head_) return;
        auto* previous = head_;
        head_ = previous->queueNext;
        if (!head_) tail_ = nullptr;
        previous->queueNext = nullptr;
    }
    [[nodiscard]] bool remove(KAPC* apc) noexcept {
        KAPC* previous{};
        for (auto* current = head_; current; current = current->queueNext) {
            if (current != apc) { previous = current; continue; }
            if (previous) previous->queueNext = current->queueNext;
            else head_ = current->queueNext;
            if (tail_ == current) tail_ = previous;
            current->queueNext = nullptr;
            return true;
        }
        return false;
    }
    [[nodiscard]] bool hasSpecial(uint8_t environmentIndex) const noexcept {
        for (auto* apc = head_; apc; apc = apc->queueNext)
            if (apc->apcStateIndex == environmentIndex && apc->isSpecialKernelApc) return true;
        return false;
    }
    [[nodiscard]] bool hasEnvironment(uint8_t environmentIndex) const noexcept {
        for (auto* apc = head_; apc; apc = apc->queueNext)
            if (apc->apcStateIndex == environmentIndex) return true;
        return false;
    }
    // Special APCs are eligible when normal APCs are disabled and take
    // precedence over normal APCs within the selected environment.
    [[nodiscard]] KAPC* takeEligible(bool normalDisabled, uint8_t environmentIndex) noexcept {
        KAPC* special{};
        KAPC* specialPrev{};
        KAPC* normal{};
        KAPC* normalPrev{};
        KAPC* previous{};
        for (auto* apc = head_; apc; apc = apc->queueNext) {
            if (apc->apcStateIndex == environmentIndex) {
                if (apc->isSpecialKernelApc && !special) { special = apc; specialPrev = previous; }
                else if (!apc->isSpecialKernelApc && !normal) { normal = apc; normalPrev = previous; }
            }
            previous = apc;
        }
        auto* selected = special ? special : (!normalDisabled ? normal : nullptr);
        auto* selectedPrevious = special ? specialPrev : normalPrev;
        if (!selected) return nullptr;
        if (selectedPrevious) selectedPrevious->queueNext = selected->queueNext;
        else head_ = selected->queueNext;
        if (tail_ == selected) tail_ = selectedPrevious;
        selected->queueNext = nullptr;
        return selected;
    }
    [[nodiscard]] KAPC* takeOtherEnvironment(uint8_t environmentIndex) noexcept {
        KAPC* previous{};
        for (auto* apc = head_; apc; apc = apc->queueNext) {
            if (apc->apcStateIndex == environmentIndex) { previous = apc; continue; }
            if (previous) previous->queueNext = apc->queueNext;
            else head_ = apc->queueNext;
            if (tail_ == apc) tail_ = previous;
            apc->queueNext = nullptr;
            return apc;
        }
        return nullptr;
    }
    [[nodiscard]] KAPC* takeEnvironment(uint8_t environmentIndex) noexcept {
        KAPC* previous{};
        for (auto* apc = head_; apc; apc = apc->queueNext) {
            if (apc->apcStateIndex != environmentIndex) { previous = apc; continue; }
            if (previous) previous->queueNext = apc->queueNext;
            else head_ = apc->queueNext;
            if (tail_ == apc) tail_ = previous;
            apc->queueNext = nullptr;
            return apc;
        }
        return nullptr;
    }
private:
    KAPC* head_{nullptr};
    KAPC* tail_{nullptr};
};
#endif

// ============================================================================
// 7. Thread Execution State & Hardware Context Frame
// ============================================================================
enum class ThreadState : uint8_t {
    Initialized   = 0,
    Ready         = 1,
    Running       = 2,
    Standby       = 3,
    Terminated    = 4,
    Waiting       = 5,
    Transition    = 6,
    DeferredReady = 7
};

struct KThreadContext {
    uint64_t rip{0};
    uint64_t rsp{0};
    uint64_t rflags{0x202};
    uint64_t rbx{0};
    uint64_t rbp{0};
    uint64_t rsi{0};
    uint64_t rdi{0};
    uint64_t r12{0};
    uint64_t r13{0};
    uint64_t r14{0};
    uint64_t r15{0};
    uint32_t mxcsr{0x1F80};
    uint16_t x87ControlWord{0x037F};
    uint16_t reserved{0};
    alignas(16) std::array<uint8_t, 160> xmm6To15{};
#if defined(MICANT_FREESTANDING)
    // Saved state resides on this suspended thread's resident kernel stack.
    uintptr_t extendedState{0};
#endif
};

static_assert(offsetof(KThreadContext, rip) == 0);
static_assert(offsetof(KThreadContext, rsp) == 8);
static_assert(offsetof(KThreadContext, mxcsr) == 88);
static_assert(offsetof(KThreadContext, xmm6To15) == 96);

#if defined(MICANT_FREESTANDING) && (defined(_M_X64) || defined(__x86_64__))
extern "C" bool MicantAmd64SwitchContext(KThreadContext*, KThreadContext*) noexcept;
extern "C" bool MicantAmd64ConfigureExtendedState() noexcept;
extern "C" uint32_t MicantAmd64ExtendedSize;
static_assert(offsetof(KThreadContext, extendedState) == 256);
#endif

/**
 * @brief Kernel Thread Object (KTHREAD).
 * Low-level execution context managed by the Ke dispatcher.
 */
struct KThread {
    Handle tid{0};
    Handle pid{0};
    // 0 is the original APC state; 1 is the attached APC state. The selector
    // is per-thread, while both states share the canonical APC queues.
    uint8_t apcStateIndex{ApcStateIndexOriginal};
#if defined(MICANT_FREESTANDING)
    ps::EProcess* owningProcess{nullptr};
    ps::EProcess* attachedProcess{nullptr};
    struct ProcessAttachFrame {
        KAPC_STATE* callerState{};
        ps::EProcess* previousProcess{};
        ps::EProcess* targetProcess{};
        void* targetReference{};
        uintptr_t previousRoot{};
        uint8_t previousApcStateIndex{ApcStateIndexOriginal};
        bool noOp{};
    };
    // Bounded storage avoids allocation while switching address spaces.
    std::array<ProcessAttachFrame, 16> processAttachFrames{};
    uint8_t processAttachDepth{};
    ps::EThread* owningThread{nullptr};
    KThread* processNext{nullptr};
    KThread* clientIdNext{nullptr};
    // Owned by EThread after reap until that object's lifetime ends. A
    // detached KAPC may still hold a borrowed exact-target pointer.
    KThread* retiredNext{nullptr};
    bool eventWaitInProgress{false};
    bool eventWaitUserMode{false};
    bool eventWaitCanTerminate{false};
    bool eventWaitCancelled{false};
    bool eventWaitAlerted{false};
    bool terminationRequested{false};
    bool terminationQueuedByProcess{false};
    bool terminationActive{false};
    // Initial suspension is separate from a dispatcher wait: the thread has
    // not executed yet and becomes runnable only when its count reaches zero.
    uint8_t initialSuspendCount{0};
    void (*serviceTermination)(void*) noexcept{nullptr};
    void* eventWaitObject{nullptr};
    void* eventWaitBlock{nullptr};
    bool (*interruptWaitForApc)(KThread&) noexcept{nullptr};
    // Called with BSP interrupts disabled only after a terminating switch is
    // committed. The lifecycle owner keeps the callback context resident.
    void (*terminationNotify)(void*) noexcept{nullptr};
    void* terminationContext{nullptr};
#endif
    ThreadState state{ThreadState::Initialized};
    uint32_t basePriority{8};
    uint32_t priority{8};
    int32_t quantumRemaining{6};
    uint64_t totalCyclesExecuted{0};
    std::string name;

    // Address space and stacks
#if defined(MICANT_FREESTANDING)
    uintptr_t directoryTableBase{0};
#else
    uintptr_t directoryTableBase{0x1000};
#endif
    uintptr_t kernelStackTop{0};
    uintptr_t kernelStackBase{0};
#if defined(MICANT_FREESTANDING)
    uintptr_t kernelExecutionStackTop{0};
#endif
    uintptr_t userStackTop{0};
    uintptr_t tebAddress{0};
#if defined(MICANT_FREESTANDING)
    // A suspended SYSCALL has already exchanged user and kernel GS bases.
    bool syscallGsActive{false};
    bool previousUserMode{false};
    hal::amd64::ThreadSegmentState segments{};
#endif

    // Registers & Trap Frame
    KThreadContext context{};
    KTrapFrame* trapFrame{nullptr};

    // APC Queues (Kernel-Mode & User-Mode)
#if defined(MICANT_FREESTANDING)
    ApcQueue kernelApcList;
    ApcQueue userApcList;
#else
    std::deque<KAPC*> kernelApcList;
    std::deque<KAPC*> userApcList;
#endif
    uint16_t kernelApcDisable{0};
    uint16_t specialApcDisable{0};
    // Ke pins this thread once per queued APC until queue removal.
    uint32_t queuedApcReferences{0};
#if defined(MICANT_FREESTANDING)
    bool systemInitialRegionActive{false};
#endif
#if defined(MICANT_FREESTANDING)
    bool normalKernelApcActive{false};
    bool apcQueueable{true};
    bool apcRundownActive{false};
    bool apcRundownComplete{false};
    struct PendingNormalApc {
        PKNORMAL_ROUTINE routine{};
        void* context{};
        void* argument1{};
        void* argument2{};
        uint8_t apcStateIndex{ApcStateIndexOriginal};
    };
    std::array<PendingNormalApc, 4> pendingNormalApcs{};
    size_t pendingNormalHead{0};
    size_t pendingNormalCount{0};
#endif
    bool alertable{false};
    std::array<bool, 2> alerted{};
    bool userApcDeliveryPending{false};
    bool userApcDrainOnReturn{false};
#if defined(MICANT_FREESTANDING)
    // A user normal routine is active until its dispatcher continues the
    // interrupted context or an unwind leaves that callback's stack.
    uint8_t userApcInFlightDepth{0};
    std::array<uintptr_t, 16> userApcInterruptedRsp{};
    uint8_t userRaiseDepth{0};
    std::array<uintptr_t, 16> userRaiseStack{};
#endif
#if defined(MICANT_FREESTANDING)
    KThread* readyNext{nullptr};
    bool readyQueued{false};
#endif
};

#if defined(MICANT_FREESTANDING)
[[nodiscard]] inline ps::EProcess* KeGetCurrentProcess() noexcept {
    auto* thread = KeGetCurrentThread();
    return thread ? (thread->attachedProcess ? thread->attachedProcess : thread->owningProcess) : nullptr;
}
#endif

// The caller must still hold the target's lifecycle protection. Release this
// before invoking any APC callback, since it may destroy the KAPC itself.
inline void releaseQueuedApcReference(KAPC& apc, KThread& target) noexcept {
    if (!apc.targetReferenceHeld || apc.targetThread != &target ||
        !target.queuedApcReferences) __builtin_trap();
    apc.targetReferenceHeld = false;
    // A removed APC may be queued again. As with KeInitializeApc, the
    // caller owns the thread lifetime whenever the APC is not queued.
    if (apc.exactTargetRequired) {
        apc.targetThread = &target;
        apc.targetThreadBorrowed = true;
    } else {
        apc.targetThread = nullptr;
        apc.targetThreadBorrowed = false;
    }
    --target.queuedApcReferences;
}

#if defined(MICANT_FREESTANDING)
namespace detail {

// BSP-only registry of process-owned contexts. Ob-backed terminated threads
// retain their CID until their thread object is deleted; bootstrap-only
// contexts leave the registry at reap. All operations exclude interrupts.
inline KThread* clientIdThreads{};
inline void registerClientThread(KThread& thread) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
    if (!thread.tid || thread.clientIdNext) __builtin_trap();
    for (auto* entry = clientIdThreads; entry; entry = entry->clientIdNext)
        if (entry == &thread || entry->tid == thread.tid) __builtin_trap();
    thread.clientIdNext = clientIdThreads;
    clientIdThreads = &thread;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
}
inline void unregisterClientThread(KThread& thread) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
    auto** link = &clientIdThreads;
    while (*link && *link != &thread) link = &(*link)->clientIdNext;
    if (!*link) __builtin_trap();
    *link = thread.clientIdNext;
    thread.clientIdNext = nullptr;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
}
// Caller must hold the BSP exclusion through the resulting queue operation.
[[nodiscard]] inline KThread* findClientThread(Handle tid) noexcept {
    if (!tid) return nullptr;
    for (auto* entry = clientIdThreads; entry; entry = entry->clientIdNext)
        if (entry->tid == tid) return entry;
    return nullptr;
}


using BootstrapThreadEntry = void (*)(void*) noexcept;
extern "C" void MicantAmd64BootstrapThread() noexcept;
extern "C" [[noreturn]] void MicantAmd64BootstrapThreadDispatch(
    BootstrapThreadEntry entry, void* argument) noexcept;

// The caller owns a writable, resident stack until the thread can no longer
// run. This bootstrap path is single-CPU and requires interrupts disabled.
[[nodiscard]] inline bool prepareBootstrapThread(
    KThread& thread, void* stack, size_t stackSize,
    BootstrapThreadEntry entry, void* argument) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(stack);
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    if (!MicantAmd64ConfigureExtendedState()) return false;
    const size_t minimumStack = 8192 + 2 * MicantAmd64ExtendedSize;
#else
    constexpr size_t minimumStack = 256;
#endif
    if (!stack || !entry || stackSize < minimumStack ||
        base > UINTPTR_MAX - stackSize ||
        thread.state != ThreadState::Initialized || thread.readyQueued ||
        &thread == KeGetCurrentThread() || thread.context.rip || thread.context.rsp ||
        thread.kernelStackBase || thread.kernelStackTop) {
        return false;
    }
    const uintptr_t top = (base + stackSize) & ~uintptr_t{15};
    if (top < base || top - base < 40) return false;

    KThreadContext context{};
    context.rip = reinterpret_cast<uintptr_t>(&MicantAmd64BootstrapThread);
    context.rsp = top - 32;
    context.rflags = 0x2;
    context.r12 = reinterpret_cast<uintptr_t>(entry);
    context.r13 = reinterpret_cast<uintptr_t>(argument);
    thread.kernelStackBase = base;
    thread.kernelStackTop = top;
    thread.kernelExecutionStackTop = top;
    thread.context = context;
    return true;
}

} // namespace detail
#endif

// ============================================================================
// 8. 32-Queue Priority Thread Scheduler & Preemption Engine
// ============================================================================
inline constexpr uint32_t PRIORITY_LOWEST         = 0;
inline constexpr uint32_t PRIORITY_IDLE           = 0;
inline constexpr uint32_t PRIORITY_NORMAL         = 8;
inline constexpr uint32_t PRIORITY_HIGH           = 13;
inline constexpr uint32_t PRIORITY_REALTIME_MIN   = 16;
inline constexpr uint32_t PRIORITY_REALTIME_MAX   = 31;
inline constexpr uint32_t NUM_PRIORITY_LEVELS     = 32;

inline constexpr uint32_t DEFAULT_QUANTUM_WORKSTATION = 6;
inline constexpr uint32_t DEFAULT_QUANTUM_SERVER      = 36;

struct ScheduledThreadEntry {
    Handle tid{0};
    Handle pid{0};
    uint32_t basePriority{PRIORITY_NORMAL};
    uint32_t currentPriority{PRIORITY_NORMAL};
    int32_t quantumRemaining{DEFAULT_QUANTUM_WORKSTATION};
    uint64_t totalCyclesExecuted{0};
    std::string name;
    KThread* threadObject{nullptr};
};

class PriorityScheduler {
public:
#if defined(MICANT_FREESTANDING)
    // Bootstrap-only: callers must keep hardware interrupts disabled.
    static PriorityScheduler& get() noexcept {
        static constinit PriorityScheduler instance;
        return instance;
    }

    [[nodiscard]] bool readyKThread(KThread* thread) noexcept {
        if (!thread) return false;
        SpinLockGuard guard(lock_);
        if (thread == KeGetCurrentThread() || thread->readyQueued ||
            (thread->state != ThreadState::Initialized &&
             thread->state != ThreadState::Ready &&
             thread->state != ThreadState::Waiting)) {
            return false;
        }
        const uint32_t priority = std::min(thread->priority, PRIORITY_REALTIME_MAX);
        thread->priority = priority;
        thread->state = ThreadState::Ready;
        thread->readyNext = nullptr;
        thread->readyQueued = true;
        auto& queue = runQueues_[priority];
        if (queue.tail) queue.tail->readyNext = thread;
        else queue.head = thread;
        queue.tail = thread;
        ++totalReadyThreads_;
        return true;
    }

    // Withdraw only a queued thread. Transition is an internal handoff to
    // its lifecycle owner, not termination or a Windows termination API.
    // The owner must keep the thread alive and serialize further transitions.
    [[nodiscard]] bool removeReadyThread(KThread* thread) noexcept {
        if (!thread) return false;
        SpinLockGuard guard(lock_);
        if (!thread->readyQueued || thread == KeGetCurrentThread()) return false;
        // Priority may have changed since insertion, so search every queue.
        for (auto& queue : runQueues_) {
            KThread* previous = nullptr;
            for (KThread* current = queue.head; current; current = current->readyNext) {
                if (current != thread) {
                    previous = current;
                    continue;
                }
                if (previous) previous->readyNext = current->readyNext;
                else queue.head = current->readyNext;
                if (queue.tail == current) queue.tail = previous;
                current->readyNext = nullptr;
                current->readyQueued = false;
                current->state = ThreadState::Transition;
                --totalReadyThreads_;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] KThread* selectNextThread() noexcept {
        SpinLockGuard guard(lock_);
        for (int priority = PRIORITY_REALTIME_MAX; priority >= 0; --priority) {
            auto& queue = runQueues_[static_cast<size_t>(priority)];
            if (!queue.head) continue;
            KThread* thread = queue.head;
            queue.head = thread->readyNext;
            if (!queue.head) queue.tail = nullptr;
            thread->readyNext = nullptr;
            thread->readyQueued = false;
            thread->state = ThreadState::Standby;
            --totalReadyThreads_;
            return thread;
        }
        auto* idle = g_CurrentCpu.idleThread;
        if (idle && idle != KeGetCurrentThread() && !idle->readyQueued &&
            (idle->state == ThreadState::Initialized || idle->state == ThreadState::Ready)) {
            idle->state = ThreadState::Standby;
            return idle;
        }
        return nullptr;
    }

    // Select only a strictly higher-priority runnable thread, preserving the
    // current quantum. Idle yields to any runnable thread, including priority 0.
    [[nodiscard]] KThread* selectHigherPriority(KThread* current) noexcept {
        if (!current || current != KeGetCurrentThread()) return nullptr;
        SpinLockGuard guard(lock_);
        if (current->state != ThreadState::Running || current->readyQueued) return nullptr;
        const uint32_t currentPriority = std::min(current->priority, PRIORITY_REALTIME_MAX);
        for (int priority = PRIORITY_REALTIME_MAX; priority >= 0; --priority) {
            auto& queue = runQueues_[static_cast<size_t>(priority)];
            if (!queue.head) continue;
            if (current != g_CurrentCpu.idleThread && static_cast<uint32_t>(priority) <= currentPriority)
                return nullptr;
            KThread* next = queue.head;
            queue.head = next->readyNext;
            if (!queue.head) queue.tail = nullptr;
            next->readyNext = nullptr;
            next->readyQueued = false;
            next->state = ThreadState::Standby;
            if (current == g_CurrentCpu.idleThread) {
                --totalReadyThreads_;
                return next;
            }
            auto& currentQueue = runQueues_[currentPriority];
            current->readyNext = nullptr;
            current->readyQueued = true;
            if (currentQueue.tail) currentQueue.tail->readyNext = current;
            else currentQueue.head = current;
            currentQueue.tail = current;
            return next;
        }
        return nullptr;
    }

    // Called on the BSP at each clock tick with interrupts disabled. Higher
    // priorities preempt at the tick; equal priorities rotate at quantum end.
    // cancelQuantumPreemption reverses a rejected context switch.
    [[nodiscard]] KThread* selectOnClockTick(KThread* current) noexcept {
        if (!current || current != KeGetCurrentThread()) return nullptr;
        SpinLockGuard guard(lock_);
        if (current->state != ThreadState::Running || current->readyQueued) return nullptr;
        const bool expired = current->quantumRemaining <= 1;
        if (expired) current->quantumRemaining = DEFAULT_QUANTUM_WORKSTATION;
        else --current->quantumRemaining;
        const uint32_t currentPriority = std::min(current->priority, PRIORITY_REALTIME_MAX);
        for (int priority = PRIORITY_REALTIME_MAX; priority >= 0; --priority) {
            auto& queue = runQueues_[static_cast<size_t>(priority)];
            if (!queue.head) continue;
            if (current != g_CurrentCpu.idleThread &&
                (static_cast<uint32_t>(priority) < currentPriority ||
                 (static_cast<uint32_t>(priority) == currentPriority && !expired))) return nullptr;
            KThread* next = queue.head;
            queue.head = next->readyNext;
            if (!queue.head) queue.tail = nullptr;
            next->readyNext = nullptr;
            next->readyQueued = false;
            next->state = ThreadState::Standby;
            if (current == g_CurrentCpu.idleThread) {
                --totalReadyThreads_;
                return next;
            }
            auto& currentQueue = runQueues_[currentPriority];
            current->readyNext = nullptr;
            current->readyQueued = true;
            if (currentQueue.tail) currentQueue.tail->readyNext = current;
            else currentQueue.head = current;
            currentQueue.tail = current;
            return next;
        }
        return nullptr;
    }

    [[nodiscard]] bool cancelQuantumPreemption(KThread* current, KThread* selected) noexcept {
        if (!current || !selected || current != KeGetCurrentThread()) return false;
        SpinLockGuard guard(lock_);
        if (current->state != ThreadState::Running ||
            selected->readyQueued || selected->state != ThreadState::Standby) return false;
        if (current == g_CurrentCpu.idleThread && !current->readyQueued) {
            auto& target = runQueues_[std::min(selected->priority, PRIORITY_REALTIME_MAX)];
            selected->readyNext = target.head;
            target.head = selected;
            if (!target.tail) target.tail = selected;
            selected->readyQueued = true;
            selected->state = ThreadState::Ready;
            ++totalReadyThreads_;
            return true;
        }
        if (!current->readyQueued) return false;
        for (auto& queue : runQueues_) {
            KThread* previous = nullptr;
            for (auto* entry = queue.head; entry; entry = entry->readyNext) {
                if (entry != current) { previous = entry; continue; }
                if (previous) previous->readyNext = entry->readyNext;
                else queue.head = entry->readyNext;
                if (queue.tail == entry) queue.tail = previous;
                current->readyNext = nullptr;
                current->readyQueued = false;
                auto& target = runQueues_[std::min(selected->priority, PRIORITY_REALTIME_MAX)];
                selected->readyNext = target.head;
                target.head = selected;
                if (!target.tail) target.tail = selected;
                selected->readyQueued = true;
                selected->state = ThreadState::Ready;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] size_t getReadyThreadCount() noexcept {
        SpinLockGuard guard(lock_);
        return totalReadyThreads_;
    }

private:
    struct ReadyQueue {
        KThread* head{nullptr};
        KThread* tail{nullptr};
    };
    constexpr PriorityScheduler() = default;
    SpinLock lock_;
    std::array<ReadyQueue, NUM_PRIORITY_LEVELS> runQueues_{};
    size_t totalReadyThreads_{0};
#else
    static PriorityScheduler& get() {
        static PriorityScheduler instance;
        return instance;
    }

    void readyThread(ScheduledThreadEntry thread) {
        SpinLockGuard guard(lock_);
        uint32_t prio = std::min(thread.currentPriority, PRIORITY_REALTIME_MAX);
        if (thread.threadObject) {
            thread.threadObject->state = ThreadState::Ready;
            thread.threadObject->priority = prio;
        }
        runQueues_[prio].push_back(std::move(thread));
        totalReadyThreads_++;
    }

    void readyKThread(KThread* kthread) {
        if (!kthread) return;
        ScheduledThreadEntry entry{
            .tid = kthread->tid,
            .pid = kthread->pid,
            .basePriority = kthread->basePriority,
            .currentPriority = kthread->priority,
            .quantumRemaining = kthread->quantumRemaining,
            .totalCyclesExecuted = kthread->totalCyclesExecuted,
            .name = kthread->name,
            .threadObject = kthread
        };
        readyThread(std::move(entry));
    }

    /**
     * @brief Select next highest priority runnable thread (KiSelectNextThread).
     */
    [[nodiscard]] std::optional<ScheduledThreadEntry> selectNextThread() {
        SpinLockGuard guard(lock_);
        for (int p = PRIORITY_REALTIME_MAX; p >= 0; --p) {
            if (!runQueues_[p].empty()) {
                auto selected = std::move(runQueues_[p].front());
                runQueues_[p].pop_front();
                totalReadyThreads_--;
                g_CurrentCpu.contextSwitches++;
                if (selected.threadObject) {
                    selected.threadObject->state = ThreadState::Standby;
                }
                return selected;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Clock tick quantum expiration & dynamic priority decay.
     */
    void clockTick(ScheduledThreadEntry& runningThread) {
        runningThread.quantumRemaining--;
        runningThread.totalCyclesExecuted += 1000;

        if (runningThread.quantumRemaining <= 0) {
            // Real-time priorities do NOT decay
            if (runningThread.currentPriority < PRIORITY_REALTIME_MIN) {
                if (runningThread.currentPriority > runningThread.basePriority) {
                    runningThread.currentPriority--; // Priority decay
                }
            }
            runningThread.quantumRemaining = DEFAULT_QUANTUM_WORKSTATION;
            readyThread(runningThread);
        }
    }

    /**
     * @brief Boost thread priority upon I/O event completion or unwait.
     */
    void boostPriority(ScheduledThreadEntry& thread, uint32_t boostAmount) {
        if (thread.currentPriority < PRIORITY_REALTIME_MIN) {
            thread.currentPriority = std::min(
                thread.currentPriority + boostAmount,
                PRIORITY_REALTIME_MIN - 1
            );
            if (thread.threadObject) {
                thread.threadObject->priority = thread.currentPriority;
            }
        }
    }

    /**
     * @brief Anti-Starvation Scanner (KiScanReadyQueues).
     * Temporarily boosts threads in ready queues 1-15 that have not run recently.
     */
    size_t scanForStarvation() {
        SpinLockGuard guard(lock_);
        size_t boosted = 0;
        for (uint32_t p = 1; p < PRIORITY_NORMAL; ++p) {
            if (!runQueues_[p].empty()) {
                auto thread = std::move(runQueues_[p].front());
                runQueues_[p].pop_front();
                thread.currentPriority = PRIORITY_REALTIME_MIN - 1; // Boost to 15
                thread.quantumRemaining = DEFAULT_QUANTUM_WORKSTATION * 2;
                runQueues_[thread.currentPriority].push_back(std::move(thread));
                boosted++;
            }
        }
        return boosted;
    }

    [[nodiscard]] size_t getReadyThreadCount() const noexcept {
        return totalReadyThreads_;
    }

private:
    PriorityScheduler() : totalReadyThreads_(0) {}
    mutable SpinLock lock_;
    std::array<std::deque<ScheduledThreadEntry>, NUM_PRIORITY_LEVELS> runQueues_;
    std::atomic<size_t> totalReadyThreads_;
#endif
};

// ============================================================================
// 9. Hardware Context Switcher (KiSwapContext & KiSwapThread)
// ============================================================================
/**
 * @brief Cooperative AMD64 context switch for the bootstrap single-CPU path.
 * The freestanding backend requires DISPATCH_LEVEL, interrupts disabled, and
 * registered page-table roots. The hosted backend preserves its state-model behavior.
 *
 * @param outgoing Thread yielding the processor
 * @param incoming Thread taking ownership of the processor
 * @return true if context switch completed successfully
 */
inline bool KiSwapContext(KThread* outgoing, KThread* incoming, bool prequeued = false) {
    if (!incoming) return false;

#if defined(MICANT_FREESTANDING) && (defined(_M_X64) || defined(__x86_64__))
    if (!outgoing || outgoing == incoming ||
        KeGetCurrentThread() != outgoing || outgoing->readyQueued != prequeued ||
        (prequeued && outgoing->state != ThreadState::Running) ||
        (outgoing->state != ThreadState::Running &&
         outgoing->state != ThreadState::Waiting &&
         outgoing->state != ThreadState::Terminated) ||
        g_CurrentCpu.currentIrql != DISPATCH_LEVEL ||
        (incoming->state != ThreadState::Initialized &&
         incoming->state != ThreadState::Ready &&
         incoming->state != ThreadState::Standby) ||
        incoming->readyQueued ||
        incoming->context.rip == 0 || incoming->context.rsp == 0 ||
        !incoming->directoryTableBase) {
        return false;
    }

    if (!MicantAmd64ConfigureExtendedState()) return false;

    uint64_t rflags{}, hardwareCr3{}, stackPointer{};
    __asm__ __volatile__("pushfq; popq %0; movq %%cr3, %1; movq %%rsp, %2"
                         : "=r"(rflags), "=r"(hardwareCr3), "=r"(stackPointer));
    if ((rflags & (uint64_t{1} << 9)) || !outgoing->directoryTableBase ||
        hardwareCr3 != outgoing->directoryTableBase || hardwareCr3 != g_CurrentCpu.currentCr3) {
        return false;
    }
    // The extended-state switch frame is resident on this stack. Keep room
    // for it and the compiler's call frame rather than writing past the base.
    if (!hal::amd64::bootstrapStackHasHeadroom(stackPointer, outgoing->kernelStackBase,
                                               outgoing->kernelExecutionStackTop,
                                               1024 + MicantAmd64ExtendedSize,
                                               outgoing->state == ThreadState::Terminated)) return false;
    if (hardwareCr3 != incoming->directoryTableBase &&
        !hal::amd64::switchProcessPagingRootByPhysical(incoming->directoryTableBase)) return false;
    g_CurrentCpu.currentCr3 = incoming->directoryTableBase;
    outgoing->syscallGsActive = hal::amd64::bootstrapSyscallGsActive();
    hal::amd64::saveBootstrapThreadSegments(outgoing->segments, outgoing->syscallGsActive);
    hal::amd64::restoreBootstrapThreadSegments(incoming->segments);
    const uintptr_t incomingUserGs = incoming->segments.saved
        ? incoming->segments.userGsBase : incoming->tebAddress;
    hal::amd64::setBootstrapThreadEntryStack(incoming->kernelStackTop, incomingUserGs,
                                            incoming->syscallGsActive, incoming->kernelStackBase,
                                            incoming->kernelExecutionStackTop);

    if (outgoing->state == ThreadState::Running) {
        outgoing->state = ThreadState::Ready;
    }
    g_CurrentCpu.currentThread = incoming;
    g_CurrentCpu.contextSwitches++;
    incoming->state = ThreadState::Running;
    if (outgoing->state == ThreadState::Terminated && outgoing->terminationNotify)
        outgoing->terminationNotify(outgoing->terminationContext);
    return MicantAmd64SwitchContext(&outgoing->context, &incoming->context);
#else
    (void)prequeued;
    // 1. In Windows NT, context switching occurs strictly at DISPATCH_LEVEL
    KIRQL currentIrql = KeGetCurrentIrql();
    bool raisedIrql = false;
    if (currentIrql < DISPATCH_LEVEL) {
        KfRaiseIrql(DISPATCH_LEVEL);
        raisedIrql = true;
    }

    // 2. Save Outgoing Thread Context
    if (outgoing) {
        if (outgoing->state == ThreadState::Running) {
            outgoing->state = ThreadState::Ready;
        }
        // Save register state
        outgoing->context.rsp = outgoing->context.rsp;
        outgoing->context.rip = outgoing->context.rip;
    }

    // 3. Switch Address Space (CR3) if Crossing Process Boundary (KiSwapProcess)
    if (incoming->directoryTableBase != 0 && incoming->directoryTableBase != g_CurrentCpu.currentCr3) {
        g_CurrentCpu.currentCr3 = incoming->directoryTableBase;
#if defined(_M_X64) || defined(__x86_64__)
        // Hardware page table reload
        _mm_sfence();
#endif
    }

    // 4. Update Per-CPU KPCR / KPRCB State
    g_CurrentCpu.currentThread = incoming;
    g_CurrentCpu.contextSwitches++;
    incoming->state = ThreadState::Running;

    // 5. Restore IRQL if raised
    if (raisedIrql) {
        KeLowerIrql(currentIrql);
    }
    return true;
#endif
}

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
namespace detail {
// Caller reaches a scheduler-safe boundary with interrupts disabled. The
// resident idle context marks that the runtime dispatcher has been installed.
inline void dispatchHigherPriority() noexcept {
    if (!g_CurrentCpu.idleThread || KeGetCurrentIrql() > APC_LEVEL) return;
    auto* current = KeGetCurrentThread();
    if (!current || current->state != ThreadState::Running) return;
    const auto oldIrql = KfRaiseIrql(DISPATCH_LEVEL);
    auto& scheduler = PriorityScheduler::get();
    if (auto* next = scheduler.selectHigherPriority(current)) {
        if (!KiSwapContext(current, next, current != g_CurrentCpu.idleThread) &&
            !scheduler.cancelQuantumPreemption(current, next)) __builtin_trap();
    }
    KeLowerIrql(oldIrql);
}

// Bootstrap dispatcher primitive; callers arrange the wait condition and wake
// the thread through the canonical ready queue. No timeout/APC semantics yet.
inline bool blockCurrentThread() noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* current = KeGetCurrentThread();
    const KIRQL previous = KeGetCurrentIrql();
    bool result = false;
    if (current && current->state == ThreadState::Running && previous <= APC_LEVEL) {
        KfRaiseIrql(DISPATCH_LEVEL);
        auto& scheduler = PriorityScheduler::get();
        if (auto* next = scheduler.selectNextThread()) {
            current->state = ThreadState::Waiting;
            result = KiSwapContext(current, next);
            if (!result) {
                current->state = ThreadState::Running;
                next->state = ThreadState::Ready;
                if (next != g_CurrentCpu.idleThread && !scheduler.readyKThread(next)) __builtin_trap();
            }
        }
        KeLowerIrql(previous);
    }
    if (flags & (uint64_t{1} << 9)) {
        servicePendingDpcs();
        dispatchHigherPriority();
        servicePendingKernelApcs(true);
        __asm__ __volatile__("sti" : : : "memory");
    }
    return result;
}
}
#endif

// ============================================================================
// 10. APC Delivery Engine (KiDeliverApc)
// ============================================================================
// Internal compatibility overload for existing TID-based callers. New Ke
// code should use the canonical KThread plus ApcEnvironment overload below.
inline void KeInitializeApc(
    KAPC* apc,
    Handle targetTid,
    PKKERNEL_ROUTINE kernelRoutine,
    PKRUNDOWN_ROUTINE rundownRoutine,
    PKNORMAL_ROUTINE normalRoutine,
    uint8_t apcMode,
    void* normalContext
) {
    // Reinitializing a queued APC would corrupt its queue and steal its target
    // pin. KeInitializeApc has no status return, so leave it untouched.
    if (!apc || apc->inserted || apc->targetReferenceHeld) return;
    uint8_t environmentIndex = ApcStateIndexInsert;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (auto* target = detail::findClientThread(targetTid);
        target && target->apcStateIndex <= ApcStateIndexAttached)
        environmentIndex = target->apcStateIndex;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
    apc->targetTid = targetTid;
    apc->targetThread = nullptr;
    apc->targetThreadBorrowed = false;
    apc->exactTargetRequired = false;
    apc->apcStateIndex = environmentIndex;
    apc->kernelRoutine = kernelRoutine;
    apc->rundownRoutine = rundownRoutine;
    apc->normalRoutine = normalRoutine;
    apc->apcMode = apcMode;
    apc->normalContext = normalContext;
    apc->inserted = false;
    apc->isSpecialKernelApc = (normalRoutine == nullptr);
    apc->targetReferenceHeld = false;
}

// Canonical KeInitializeApc form. Environment selection is stored on the APC
// and resolved against the target's active state at insertion for the Insert
// environment. The target is borrowed until insertion; insertion then pins it.
inline void KeInitializeApc(
    KAPC* apc,
    KThread* targetThread,
    ApcEnvironment targetEnvironment,
    PKKERNEL_ROUTINE kernelRoutine,
    PKRUNDOWN_ROUTINE rundownRoutine,
    PKNORMAL_ROUTINE normalRoutine,
    uint8_t apcMode,
    void* normalContext
) {
    if (!apc || apc->inserted || apc->targetReferenceHeld) return;
    uint8_t environmentIndex = ApcStateIndexInvalid;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
    if (targetThread) {
        switch (targetEnvironment) {
        case ApcEnvironment::OriginalApcEnvironment:
            environmentIndex = ApcStateIndexOriginal;
            break;
        case ApcEnvironment::AttachedApcEnvironment:
            // The attached APC state is a distinct, dormant state even when
            // the target currently executes in its original environment.
            environmentIndex = ApcStateIndexAttached;
            break;
        case ApcEnvironment::CurrentApcEnvironment:
            if (targetThread->apcStateIndex <= ApcStateIndexAttached)
                environmentIndex = targetThread->apcStateIndex;
            break;
        case ApcEnvironment::InsertApcEnvironment:
            environmentIndex = ApcStateIndexInsert;
            break;
        }
    }
    apc->targetTid = targetThread ? targetThread->tid : 0;
    apc->targetThread = targetThread;
    apc->targetThreadBorrowed = targetThread != nullptr;
    apc->exactTargetRequired = true;
    apc->apcStateIndex = environmentIndex;
    apc->kernelRoutine = kernelRoutine;
    apc->rundownRoutine = rundownRoutine;
    apc->normalRoutine = normalRoutine;
    // NT makes a null NormalRoutine a special kernel APC; mode and context
    // have no meaning for that class.
    apc->apcMode = normalRoutine ? apcMode : 0;
    apc->normalContext = normalRoutine ? normalContext : nullptr;
    apc->inserted = false;
    apc->isSpecialKernelApc = normalRoutine == nullptr;
    apc->targetReferenceHeld = false;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
}

/**
 * @brief Queue an APC to target thread (KeInsertQueueApc).
 */
inline bool KeInsertQueueApc(KAPC* apc, void* sysArg1 = nullptr, void* sysArg2 = nullptr) {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* curr = g_CurrentCpu.currentThread;
    auto* target = !apc ? nullptr : apc->exactTargetRequired
        ? (apc->targetThreadBorrowed ? apc->targetThread : nullptr)
        : curr && apc->targetTid == curr->tid ? curr : detail::findClientThread(apc->targetTid);
    if (!apc || apc->inserted || apc->targetReferenceHeld || !target || !target->apcQueueable ||
        target->apcRundownActive || target->apcRundownComplete || target->terminationRequested ||
        target->tid != apc->targetTid || target->state == ThreadState::Terminated ||
        target->state == ThreadState::Transition || apc->apcMode > 1) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    const uint8_t environmentIndex = apc->apcStateIndex == ApcStateIndexInsert
        ? target->apcStateIndex : apc->apcStateIndex;
    if (environmentIndex > ApcStateIndexAttached || target->apcStateIndex > ApcStateIndexAttached) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    if (target->queuedApcReferences == UINT32_MAX) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    ++target->queuedApcReferences;
    apc->targetThread = target;
    apc->targetThreadBorrowed = false;
    apc->targetReferenceHeld = true;
    apc->apcStateIndex = environmentIndex;
    apc->systemArgument1 = sysArg1;
    apc->systemArgument2 = sysArg2;
    apc->inserted = true;
    if (apc->apcMode == 0) {
        target->kernelApcList.pushKernel(apc);
        if (environmentIndex == target->apcStateIndex && target != curr && target->interruptWaitForApc && !target->specialApcDisable &&
            (apc->isSpecialKernelApc || (!target->kernelApcDisable && !target->normalKernelApcActive)))
            (void)target->interruptWaitForApc(*target);
    } else {
        target->userApcList.push_back(apc);
        if (environmentIndex == target->apcStateIndex && target->alertable) {
            target->userApcDeliveryPending = true;
            if (target != curr && target->interruptWaitForApc)
                (void)target->interruptWaitForApc(*target);
        }
    }
    if (flags & (uint64_t{1} << 9)) {
        if (target == curr && environmentIndex == target->apcStateIndex) detail::servicePendingKernelApcs(true);
        __asm__ __volatile__("sti" : : : "memory");
    }
    return true;
#else
    auto* curr = g_CurrentCpu.currentThread;
    auto* target = apc && apc->exactTargetRequired
        ? (apc->targetThreadBorrowed ? apc->targetThread : nullptr)
        : curr;
    const uint8_t environmentIndex = apc && apc->apcStateIndex == ApcStateIndexInsert && curr
        ? curr->apcStateIndex : apc ? apc->apcStateIndex : ApcStateIndexInvalid;
    if (!apc || apc->inserted || apc->targetReferenceHeld || !curr || target != curr ||
        target->tid != apc->targetTid || apc->apcMode > 1 ||
        environmentIndex > ApcStateIndexAttached || curr->apcStateIndex > ApcStateIndexAttached ||
        curr->queuedApcReferences == UINT32_MAX) return false;
    ++curr->queuedApcReferences;
    apc->targetThread = curr;
    apc->targetThreadBorrowed = false;
    apc->targetReferenceHeld = true;
    apc->apcStateIndex = environmentIndex;
    apc->systemArgument1 = sysArg1;
    apc->systemArgument2 = sysArg2;
    apc->inserted = true;

    if (apc->apcMode == 0) {
        curr->kernelApcList.push_back(apc);
    } else {
        curr->userApcList.push_back(apc);
    }
    return true;
#endif
}

// Remove an APC only while it is still linked. Once delivery or rundown has
// dequeued it, the callback owns the next transition and this returns FALSE.
inline bool KeRemoveQueueApc(KAPC* apc) noexcept {
    if (!apc) return false;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (!apc->inserted || !apc->targetReferenceHeld || !apc->targetThread) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    auto* target = apc->targetThread;
    const bool removed = apc->apcMode == 0
        ? target->kernelApcList.remove(apc)
        : apc->apcMode == 1 ? target->userApcList.remove(apc) : false;
    if (!removed) __builtin_trap();
    apc->inserted = false;
    releaseQueuedApcReference(*apc, *target);
    if (apc->apcMode == 1 && !target->userApcList.hasEnvironment(target->apcStateIndex)) {
        target->userApcDeliveryPending = false;
        target->userApcDrainOnReturn = false;
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return true;
#else
    if (!apc->inserted || !apc->targetReferenceHeld || !apc->targetThread) return false;
    auto* target = apc->targetThread;
    auto& queue = apc->apcMode == 0 ? target->kernelApcList : target->userApcList;
    const auto it = std::find(queue.begin(), queue.end(), apc);
    if (it == queue.end()) __builtin_trap();
    queue.erase(it);
    apc->inserted = false;
    releaseQueuedApcReference(*apc, *target);
    return true;
#endif
}

// Test and consume the current thread's alert state. ReactOS/NT also uses a
// UserMode test to notice a user APC that was queued while the thread was not
// alertable; mark that active APC environment pending for the next user return.
inline bool KeTestAlertThread(uint8_t alertMode) noexcept {
    if (alertMode > UserMode || KeGetCurrentIrql() > DISPATCH_LEVEL) return false;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#endif
    auto* thread = KeGetCurrentThread();
    bool previousAlerted{};
    if (thread) {
#if defined(MICANT_FREESTANDING)
        const bool userApcQueued = thread->userApcList.hasEnvironment(thread->apcStateIndex);
#else
        const bool userApcQueued = std::any_of(thread->userApcList.begin(), thread->userApcList.end(),
            [thread](const KAPC* apc) { return apc && apc->apcStateIndex == thread->apcStateIndex; });
#endif
        previousAlerted = thread->alerted[alertMode];
        if (previousAlerted) {
            thread->alerted[alertMode] = false;
        } else if (alertMode == UserMode &&
                   thread->apcStateIndex <= ApcStateIndexAttached &&
                   userApcQueued) {
            thread->userApcDeliveryPending = true;
            thread->userApcDrainOnReturn = true;
        }
    }
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
    return previousAlerted;
}

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
inline void KiDeliverApc(uint8_t deliveryMode);
namespace detail {
// Run on the terminating thread at PASSIVE_LEVEL. Freeze queue insertion
// before delivering kernel APCs, then rundown user APCs. KAPCs are borrowed:
// dequeue and release target ownership before a callback can destroy storage.
inline bool rundownCurrentApcs() noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* thread = KeGetCurrentThread();
    if (!thread || thread->apcRundownActive || thread->apcRundownComplete ||
        thread->state != ThreadState::Running || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        thread->apcStateIndex > ApcStateIndexAttached ||
        thread->kernelApcDisable || thread->specialApcDisable || thread->normalKernelApcActive) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    const bool wasQueueable = thread->apcQueueable;
    const bool callbacksMayEnableInterrupts =
        (flags & (uint64_t{1} << 9)) || hal::amd64::bootstrapSchedulerClockActive();
    thread->apcQueueable = false;
    thread->apcRundownActive = true;

    // A terminating thread must release every attached address-space pin
    // before the process reaper can close those roots. The caller-owned state
    // tokens remain on this thread's active kernel stack until rundown begins.
    if (!unwindCurrentProcessAttachments()) {
        thread->apcRundownActive = false;
        thread->apcQueueable = wasQueueable;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }

    // Special kernel APCs are delivered at APC_LEVEL even while normal kernel
    // APCs are disabled; normal kernel APCs and their normal routines run only
    // when APC disable state permits it. With the preconditions above,
    // KiDeliverApc drains both classes and pending normal callbacks for the
    // currently active APC state.
    if (callbacksMayEnableInterrupts)
        __asm__ __volatile__("sti" : : : "memory");
    KiDeliverApc(0);
    __asm__ __volatile__("cli" : : : "memory");
    const uint8_t activeEnvironment = thread->apcStateIndex;
    if (thread->kernelApcList.hasEnvironment(activeEnvironment) || thread->pendingNormalCount ||
        thread->normalKernelApcActive) {
        thread->apcRundownActive = false;
        thread->apcQueueable = wasQueueable;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }

    // APCs queued for a non-current attachment state cannot execute their
    // kernel/normal routines in this process context. Termination removes
    // them and invokes their optional rundown callback instead.
    while (auto* apc = thread->kernelApcList.takeOtherEnvironment(activeEnvironment)) {
        apc->inserted = false;
        releaseQueuedApcReference(*apc, *thread);
        auto rundown = apc->rundownRoutine;
        if (rundown) {
            if (callbacksMayEnableInterrupts) __asm__ __volatile__("sti" : : : "memory");
            rundown(apc);
            __asm__ __volatile__("cli" : : : "memory");
        }
    }

    // User APC rundown callbacks execute at PASSIVE_LEVEL without queue or
    // dispatcher locks held. Removing each entry first makes rundown exactly
    // once even if its callback destroys or reuses the APC storage.
    while (!thread->userApcList.empty()) {
        auto* apc = thread->userApcList.front();
        thread->userApcList.pop_front();
        apc->inserted = false;
        releaseQueuedApcReference(*apc, *thread);
        auto rundown = apc->rundownRoutine;
        if (rundown) {
            if (callbacksMayEnableInterrupts) __asm__ __volatile__("sti" : : : "memory");
            rundown(apc); // The callback may destroy the APC.
            __asm__ __volatile__("cli" : : : "memory");
        }
    }
    thread->apcRundownActive = false;
    thread->apcRundownComplete = true;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return true;
}
// Kept for existing internal callers; rundown now drains eligible kernel APC
// work before applying the user APC rundown contract.
inline bool rundownCurrentUserApcs() noexcept { return rundownCurrentApcs(); }
}
#endif

/**
 * @brief Deliver pending APCs to the currently running thread (KiDeliverApc).
 */
inline void KiDeliverApc(uint8_t deliveryMode) {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    // User APC delivery needs a user-mode return frame and is not handled here.
    if (deliveryMode != 0) return;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* thread = KeGetCurrentThread();
    if (!thread || KeGetCurrentIrql() != PASSIVE_LEVEL || thread->specialApcDisable ||
        thread->apcStateIndex > ApcStateIndexAttached) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return;
    }
    for (;;) {
        if (thread->specialApcDisable) break;
        const uint8_t activeEnvironment = thread->apcStateIndex;
        const bool specialQueued = thread->kernelApcList.hasSpecial(activeEnvironment);
        size_t pendingOffset = thread->pendingNormalCount;
        if (!specialQueued && !thread->kernelApcDisable && !thread->normalKernelApcActive) {
            for (size_t offset = 0; offset < thread->pendingNormalCount; ++offset) {
                const size_t index = (thread->pendingNormalHead + offset) % thread->pendingNormalApcs.size();
                if (thread->pendingNormalApcs[index].apcStateIndex == activeEnvironment) {
                    pendingOffset = offset;
                    break;
                }
            }
        }
        if (pendingOffset < thread->pendingNormalCount) {
            const size_t pendingIndex = (thread->pendingNormalHead + pendingOffset) %
                                        thread->pendingNormalApcs.size();
            const auto pending = thread->pendingNormalApcs[pendingIndex];
            if (!pendingOffset) {
                thread->pendingNormalHead = (thread->pendingNormalHead + 1) % thread->pendingNormalApcs.size();
            } else {
                for (size_t offset = pendingOffset; offset + 1 < thread->pendingNormalCount; ++offset) {
                    const size_t destination = (thread->pendingNormalHead + offset) %
                                               thread->pendingNormalApcs.size();
                    const size_t source = (thread->pendingNormalHead + offset + 1) %
                                          thread->pendingNormalApcs.size();
                    thread->pendingNormalApcs[destination] = thread->pendingNormalApcs[source];
                }
            }
            --thread->pendingNormalCount;
            thread->normalKernelApcActive = true;
            if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
            pending.routine(pending.context, pending.argument1, pending.argument2);
            __asm__ __volatile__("cli" : : : "memory");
            thread->normalKernelApcActive = false;
            continue;
        }
        // Capacity limits normal APCs only; special APCs remain eligible.
        // A normal kernel routine can release its APC and supply a callback,
        // so reserve a slot before invoking that routine.
        const bool normalDisabled = thread->kernelApcDisable || thread->normalKernelApcActive ||
            thread->pendingNormalCount == thread->pendingNormalApcs.size();
        auto* apc = thread->kernelApcList.takeEligible(normalDisabled, activeEnvironment);
        if (!apc) break;
        const bool special = apc->isSpecialKernelApc;
        const uint8_t apcEnvironment = apc->apcStateIndex;
        apc->inserted = false;
        releaseQueuedApcReference(*apc, *thread);
        auto kernelRoutine = apc->kernelRoutine;
        PKNORMAL_ROUTINE normalRoutine = apc->normalRoutine;
        void* normalContext = apc->normalContext;
        void* arg1 = apc->systemArgument1;
        void* arg2 = apc->systemArgument2;
        const KIRQL old = KfRaiseIrql(APC_LEVEL);
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        if (kernelRoutine) kernelRoutine(apc, &normalRoutine, &normalContext, &arg1, &arg2);
        __asm__ __volatile__("cli" : : : "memory");
        KeLowerIrql(old);
        ++g_CurrentCpu.apcCount;
        if (!special && normalRoutine) {
            const size_t tail = (thread->pendingNormalHead + thread->pendingNormalCount) %
                                thread->pendingNormalApcs.size();
            thread->pendingNormalApcs[tail] = {normalRoutine, normalContext, arg1, arg2, apcEnvironment};
            ++thread->pendingNormalCount;
        }
        if (thread->specialApcDisable) break;
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#elif defined(MICANT_FREESTANDING)
    auto* thread = g_CurrentCpu.currentThread;
    if (!thread) return;

    // 1. Deliver Special and Normal Kernel-Mode APCs
    if (deliveryMode == 0) {
        while (!thread->kernelApcList.empty()) {
            auto* apc = thread->kernelApcList.front();
            thread->kernelApcList.pop_front();
            apc->inserted = false;
            releaseQueuedApcReference(*apc, *thread);

            PKNORMAL_ROUTINE normalRoutine = apc->normalRoutine;
            void* normalContext = apc->normalContext;
            void* sysArg1 = apc->systemArgument1;
            void* sysArg2 = apc->systemArgument2;

            // Kernel routine runs at APC_LEVEL
            KIRQL old = KfRaiseIrql(APC_LEVEL);
            if (apc->kernelRoutine) {
                apc->kernelRoutine(apc, &normalRoutine, &normalContext, &sysArg1, &sysArg2);
            }
            KeLowerIrql(old);

            // If normal routine exists, run at PASSIVE_LEVEL
            if (normalRoutine && !thread->kernelApcDisable) {
                normalRoutine(normalContext, sysArg1, sysArg2);
            }
            g_CurrentCpu.apcCount++;
        }
    }

    // User APC callbacks must never run on this kernel path. The canonical
    // queue is drained into a checked CPL3 frame by KePrepareUserApcDelivery.
#else
    // Hosted Ke tests model user delivery with ordinary callbacks. This branch
    // is excluded from the freestanding kernel and is not an NT execution path.
    auto* thread = g_CurrentCpu.currentThread;
    if (!thread || KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    if (deliveryMode == 0) {
        while (!thread->kernelApcList.empty()) {
            auto* apc = thread->kernelApcList.front();
            thread->kernelApcList.pop_front();
            apc->inserted = false;
            releaseQueuedApcReference(*apc, *thread);
            PKNORMAL_ROUTINE normalRoutine = apc->normalRoutine;
            void* normalContext = apc->normalContext;
            void* argument1 = apc->systemArgument1;
            void* argument2 = apc->systemArgument2;
            const KIRQL old = KfRaiseIrql(APC_LEVEL);
            if (apc->kernelRoutine)
                apc->kernelRoutine(apc, &normalRoutine, &normalContext, &argument1, &argument2);
            KeLowerIrql(old);
            if (normalRoutine && !thread->kernelApcDisable)
                normalRoutine(normalContext, argument1, argument2);
            ++g_CurrentCpu.apcCount;
        }
    }
    if ((deliveryMode == 1 || thread->alertable) && !thread->kernelApcDisable &&
        !thread->specialApcDisable) {
        while (!thread->userApcList.empty()) {
            auto* apc = thread->userApcList.front();
            thread->userApcList.pop_front();
            apc->inserted = false;
            releaseQueuedApcReference(*apc, *thread);
            if (apc->kernelRoutine) {
                PKNORMAL_ROUTINE normalRoutine = apc->normalRoutine;
                void* normalContext = apc->normalContext;
                void* argument1 = apc->systemArgument1;
                void* argument2 = apc->systemArgument2;
                apc->kernelRoutine(apc, &normalRoutine, &normalContext, &argument1, &argument2);
                if (normalRoutine) normalRoutine(normalContext, argument1, argument2);
            }
            ++g_CurrentCpu.apcCount;
        }
        thread->alertable = false;
    }
#endif
}

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
struct UserApcDelivery {
    uintptr_t routine{};
    void* normalContext{};
    void* systemArgument1{};
    void* systemArgument2{};
};
inline constexpr size_t UserApcDeliveryBatchCapacity = 32;

// Claim a bounded FIFO batch of user APCs for a checked user return. Queue
// removal and target-pin release happen under BSP exclusion; kernel routines
// execute afterward at APC_LEVEL without the queue lock held.
inline bool KePrepareUserApcDelivery(UserApcDelivery* deliveries, size_t capacity,
                                     size_t& deliveryCount) noexcept {
    deliveryCount = 0;
    if (!deliveries || !capacity) return false;
    for (size_t i = 0; i < capacity; ++i) deliveries[i] = {};
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* thread = KeGetCurrentThread();
    if (!thread || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        thread->kernelApcDisable || thread->specialApcDisable ||
        thread->apcStateIndex > ApcStateIndexAttached ||
        (!thread->alertable && !thread->userApcDrainOnReturn) ||
        !thread->userApcList.hasEnvironment(thread->apcStateIndex)) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    const auto environment = thread->apcStateIndex;
    // Trap/syscall exits enter with IF clear even though the interrupted user
    // context normally had interrupts enabled. Once the clock owns IRQ
    // routing, user-APC kernel routines must run with interrupts enabled at
    // APC_LEVEL; otherwise a routine that relies on hardware progress can
    // deadlock the entire processor.
    const bool callbacksMayEnableInterrupts =
        (flags & (uint64_t{1} << 9)) || hal::amd64::bootstrapSchedulerClockActive();
    thread->userApcDeliveryPending = false;
    // An alertable wait has been interrupted and will complete with
    // STATUS_USER_APC. Claim callbacks only as they are delivered to user
    // mode; later APCs must remain queued if this callback faults or exits.
    thread->alertable = false;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");

    size_t processedCount{};
    while (processedCount < UserApcDeliveryBatchCapacity && deliveryCount < capacity) {
        __asm__ __volatile__("cli" : : : "memory");
        auto* apc = thread->userApcList.takeEnvironment(environment);
        if (!apc) break;
        if (!apc->inserted || !apc->targetReferenceHeld || apc->targetThread != thread ||
            apc->apcMode != UserMode || !apc->normalRoutine) __builtin_trap();
        apc->inserted = false;
        releaseQueuedApcReference(*apc, *thread);
        ++processedCount;
        auto kernelRoutine = apc->kernelRoutine;
        PKNORMAL_ROUTINE normalRoutine = apc->normalRoutine;
        void* normalContext = apc->normalContext;
        void* argument1 = apc->systemArgument1;
        void* argument2 = apc->systemArgument2;
        if (kernelRoutine) {
            const auto oldIrql = KfRaiseIrql(APC_LEVEL);
            // The syscall entry temporarily uses the bootstrap syscall GS
            // base. As in the schedulable syscall paths, callbacks must use
            // the ordinary thread GS convention instead, and restore the
            // entry convention before returning through the syscall epilogue.
            const bool swapToKernelGs = hal::amd64::bootstrapSyscallGsActive();
            uint16_t previousDs{}, previousEs{};
            __asm__ __volatile__("movw %%ds, %0; movw %%es, %1"
                                 : "=rm"(previousDs), "=rm"(previousEs));
            if (swapToKernelGs) __asm__ __volatile__("swapgs" : : : "memory");
            __asm__ __volatile__("movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es"
                                 : : : "rax", "memory");
            if (callbacksMayEnableInterrupts) __asm__ __volatile__("sti" : : : "memory");
            kernelRoutine(apc, &normalRoutine, &normalContext, &argument1, &argument2);
            // Do not allow lowering IRQL to dispatch a nested return path
            // before this APC's saved callback parameters are committed.
            __asm__ __volatile__("cli" : : : "memory");
            __asm__ __volatile__("movw %0, %%ds; movw %1, %%es"
                                 : : "rm"(previousDs), "rm"(previousEs) : "memory");
            if (swapToKernelGs) __asm__ __volatile__("swapgs" : : : "memory");
            KeLowerIrql(oldIrql);
        }
        if (normalRoutine) {
            deliveries[deliveryCount++] = {reinterpret_cast<uintptr_t>(normalRoutine),
                                           normalContext, argument1, argument2};
        }
        __asm__ __volatile__("cli" : : : "memory");
    }
    __asm__ __volatile__("cli" : : : "memory");
    const bool moreActive = thread->userApcList.hasEnvironment(environment);
    thread->userApcDrainOnReturn = moreActive;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return deliveryCount != 0;
}
#endif

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
namespace detail {
// IF is disabled by the caller; enable it for callbacks and restore it on exit.
inline void servicePendingKernelApcs(bool callerHadInterrupts) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    ReturnKernelContext kernelContext;
    if (callerHadInterrupts || hal::amd64::bootstrapSchedulerClockActive())
        __asm__ __volatile__("sti" : : : "memory");
    KiDeliverApc(0);
    __asm__ __volatile__("cli" : : : "memory");
    if (g_CurrentCpu.serviceDeferredCleanup) g_CurrentCpu.serviceDeferredCleanup();
}

// Called only when the kernel has finished the interrupted user operation and
// holds no syscall-local references or locks. IF is disabled on entry.
inline void serviceUserReturnTermination() noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    auto* current = KeGetCurrentThread();
    if (!current || !current->terminationRequested || current->terminationActive ||
        !current->serviceTermination || current->eventWaitObject || current->eventWaitInProgress ||
        current->kernelApcDisable || current->specialApcDisable || current->normalKernelApcActive ||
        g_CurrentCpu.dpcRoutineActive) return;
    current->terminationActive = true;
    ReturnKernelContext kernelContext;
    if (hal::amd64::bootstrapSchedulerClockActive())
        __asm__ __volatile__("sti" : : : "memory");
    current->serviceTermination(current->terminationContext);
    __asm__ __volatile__("cli" : : : "memory");
    current->terminationActive = false;
}
}
#endif

/**
 * @brief Software Interrupt Dispatcher (Vector 0x2F / DISPATCH_LEVEL & APC_LEVEL handler).
 * Called by interrupt exit stubs and thread preemption epilogues.
 */
inline void KiDispatchSoftwareInterrupt(KIRQL targetLevel) {
    if (targetLevel == DISPATCH_LEVEL) {
        KiRetireDpcList();
    } else if (targetLevel == APC_LEVEL) {
        KiDeliverApc(0);
    }
}

// ============================================================================
// 11. Critical and Guarded Region Execution Boundaries
// ============================================================================

#if !defined(MICANT_FREESTANDING)
inline thread_local bool t_kernelApcDisable{false};
inline thread_local bool t_specialApcDisable{false};
#endif

/**
 * @brief Enter a Critical Region (disables normal kernel APCs for current thread).
 */
inline void KeEnterCriticalRegion() noexcept {
#if !defined(MICANT_FREESTANDING)
    t_kernelApcDisable = true;
#endif
    if (auto* thread = KeGetCurrentThread()) {
        if (thread->kernelApcDisable == UINT16_MAX) __builtin_trap();
        ++thread->kernelApcDisable;
    }
}

/**
 * @brief Leave a Critical Region (re-enables normal kernel APCs for current thread).
 */
inline void KeLeaveCriticalRegion() noexcept {
#if !defined(MICANT_FREESTANDING)
    t_kernelApcDisable = false;
#endif
    if (auto* thread = KeGetCurrentThread()) {
        if (!thread->kernelApcDisable) __builtin_trap();
        --thread->kernelApcDisable;
#if defined(MICANT_FREESTANDING)
        if (!thread->kernelApcDisable) thread->systemInitialRegionActive = false;
#endif
    }
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (flags & (uint64_t{1} << 9)) {
        detail::servicePendingKernelApcs(true);
        __asm__ __volatile__("sti" : : : "memory");
    }
#endif
}

/**
 * @brief Enter a Guarded Region (disables special kernel APCs for current thread).
 */
inline void KeEnterGuardedRegion() noexcept {
#if !defined(MICANT_FREESTANDING)
    t_specialApcDisable = true;
#endif
    if (auto* thread = KeGetCurrentThread()) {
        if (thread->specialApcDisable == UINT16_MAX) __builtin_trap();
        ++thread->specialApcDisable;
    }
}

/**
 * @brief Leave a Guarded Region (re-enables special kernel APCs for current thread).
 */
inline void KeLeaveGuardedRegion() noexcept {
#if !defined(MICANT_FREESTANDING)
    t_specialApcDisable = false;
#endif
    if (auto* thread = KeGetCurrentThread()) {
        if (!thread->specialApcDisable) __builtin_trap();
        --thread->specialApcDisable;
    }
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (flags & (uint64_t{1} << 9)) {
        detail::servicePendingKernelApcs(true);
        __asm__ __volatile__("sti" : : : "memory");
    }
#endif
}

/**
 * @brief Check if APC delivery is disabled (Critical/Guarded region or IRQL >= APC_LEVEL).
 */
[[nodiscard]] inline bool KeAreApcsDisabled() noexcept {
    if (KeGetCurrentIrql() >= APC_LEVEL) return true;
#if !defined(MICANT_FREESTANDING)
    if (t_kernelApcDisable || t_specialApcDisable) return true;
#endif
    if (auto* thread = KeGetCurrentThread()) {
        return thread->kernelApcDisable || thread->specialApcDisable;
    }
    return false;
}

} // namespace micant::ke
