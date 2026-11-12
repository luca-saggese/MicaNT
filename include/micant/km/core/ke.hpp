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
namespace micant::ps { class EProcess; }
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

// Forward declarations
struct KThread;
struct KDPC;
struct KAPC;

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
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
}

[[nodiscard]] inline uint32_t KeGetCurrentProcessorNumber() noexcept {
    return g_CurrentCpu.processorNumber;
}

[[nodiscard]] inline KThread* KeGetCurrentThread() noexcept {
    return g_CurrentCpu.currentThread;
}


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
        if (restoreInterrupts) __asm__ __volatile__("sti" : : : "memory");
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
};

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

inline void KiRetireDpcList() {
    if (!g_CurrentCpu.dpcRoutineActive && DpcQueue::get().getQueuedCount() > 0) {
        DpcQueue::get().drainDpcs();
    }
}

inline void KeInitializeDpc(KDPC* dpc, PKDEFERRED_ROUTINE routine, void* deferredContext) {
    if (dpc) {
        dpc->routine = routine;
        dpc->deferredContext = deferredContext;
        dpc->systemArgument1 = nullptr;
        dpc->systemArgument2 = nullptr;
        dpc->importance = DpcImportance::Medium;
        dpc->targetProcessor = 0;
        dpc->inserted = false;
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
    CurrentApcEnvironment
};

using PKNORMAL_ROUTINE = void (*)(void* normalContext, void* sysArg1, void* sysArg2);
using PKKERNEL_ROUTINE = void (*)(KAPC* apc, PKNORMAL_ROUTINE* normalRoutine, void** normalContext, void** sysArg1, void** sysArg2);
using PKRUNDOWN_ROUTINE = void (*)(KAPC* apc);

struct KAPC {
    Handle targetTid{0};
    PKKERNEL_ROUTINE kernelRoutine{nullptr};
    PKRUNDOWN_ROUTINE rundownRoutine{nullptr};
    PKNORMAL_ROUTINE normalRoutine{nullptr};
    void* normalContext{nullptr};
    void* systemArgument1{nullptr};
    void* systemArgument2{nullptr};
    uint8_t apcMode{0}; // 0 = KernelMode, 1 = UserMode
    bool inserted{false};
    bool isSpecialKernelApc{false};
#if defined(MICANT_FREESTANDING)
    KAPC* queueNext{nullptr};
#endif
};

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
    void pop_front() noexcept {
        if (!head_) return;
        auto* previous = head_;
        head_ = previous->queueNext;
        if (!head_) tail_ = nullptr;
        previous->queueNext = nullptr;
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
};

static_assert(offsetof(KThreadContext, rip) == 0);
static_assert(offsetof(KThreadContext, rsp) == 8);
static_assert(offsetof(KThreadContext, mxcsr) == 88);
static_assert(offsetof(KThreadContext, xmm6To15) == 96);

#if defined(MICANT_FREESTANDING) && (defined(_M_X64) || defined(__x86_64__))
extern "C" bool MicantAmd64SwitchContext(KThreadContext*, KThreadContext*) noexcept;
#endif

/**
 * @brief Kernel Thread Object (KTHREAD).
 * Low-level execution context managed by the Ke dispatcher.
 */
struct KThread {
    Handle tid{0};
    Handle pid{0};
#if defined(MICANT_FREESTANDING)
    ps::EProcess* owningProcess{nullptr};
    KThread* processNext{nullptr};
    bool eventWaitInProgress{false};
    bool eventWaitCancelled{false};
    void* eventWaitObject{nullptr};
    void* eventWaitBlock{nullptr};
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
    uintptr_t userStackTop{0};
    uintptr_t tebAddress{0};

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
    bool kernelApcDisable{false};
    bool specialApcDisable{false};
    bool alertable{false};
    bool alerted{false};
#if defined(MICANT_FREESTANDING)
    KThread* readyNext{nullptr};
    bool readyQueued{false};
#endif
};

#if defined(MICANT_FREESTANDING)
namespace detail {

using BootstrapThreadEntry = void (*)(void*) noexcept;
extern "C" void MicantAmd64BootstrapThread() noexcept;

// The caller owns a writable, resident stack until the thread can no longer
// run. This bootstrap path is single-CPU and requires interrupts disabled.
[[nodiscard]] inline bool prepareBootstrapThread(
    KThread& thread, void* stack, size_t stackSize,
    BootstrapThreadEntry entry, void* argument) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(stack);
    if (!stack || !entry || stackSize < 256 ||
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
            if (static_cast<uint32_t>(priority) < currentPriority ||
                (static_cast<uint32_t>(priority) == currentPriority && !expired)) return nullptr;
            KThread* next = queue.head;
            queue.head = next->readyNext;
            if (!queue.head) queue.tail = nullptr;
            next->readyNext = nullptr;
            next->readyQueued = false;
            next->state = ThreadState::Standby;
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
        if (!current->readyQueued || current->state != ThreadState::Running ||
            selected->readyQueued || selected->state != ThreadState::Standby) return false;
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

    uint64_t rflags{}, hardwareCr3{};
    __asm__ __volatile__("pushfq; popq %0; movq %%cr3, %1" : "=r"(rflags), "=r"(hardwareCr3));
    if ((rflags & (uint64_t{1} << 9)) || !outgoing->directoryTableBase ||
        hardwareCr3 != outgoing->directoryTableBase || hardwareCr3 != g_CurrentCpu.currentCr3) {
        return false;
    }
    if (hardwareCr3 != incoming->directoryTableBase &&
        !hal::amd64::switchProcessPagingRootByPhysical(incoming->directoryTableBase)) return false;
    g_CurrentCpu.currentCr3 = incoming->directoryTableBase;
    hal::amd64::setBootstrapThreadEntryStack(incoming->kernelStackTop, incoming->tebAddress);

    if (outgoing->state == ThreadState::Running) {
        outgoing->state = ThreadState::Ready;
    }
    g_CurrentCpu.currentThread = incoming;
    g_CurrentCpu.contextSwitches++;
    incoming->state = ThreadState::Running;
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
                if (!scheduler.readyKThread(next)) __builtin_trap();
            }
        }
        KeLowerIrql(previous);
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}
}
#endif

// ============================================================================
// 10. APC Delivery Engine (KiDeliverApc)
// ============================================================================
/**
 * @brief Initialize a Kernel Asynchronous Procedure Call (KeInitializeApc).
 */
inline void KeInitializeApc(
    KAPC* apc,
    Handle targetTid,
    PKKERNEL_ROUTINE kernelRoutine,
    PKRUNDOWN_ROUTINE rundownRoutine,
    PKNORMAL_ROUTINE normalRoutine,
    uint8_t apcMode,
    void* normalContext
) {
    if (!apc) return;
    apc->targetTid = targetTid;
    apc->kernelRoutine = kernelRoutine;
    apc->rundownRoutine = rundownRoutine;
    apc->normalRoutine = normalRoutine;
    apc->apcMode = apcMode;
    apc->normalContext = normalContext;
    apc->inserted = false;
    apc->isSpecialKernelApc = (normalRoutine == nullptr);
}

/**
 * @brief Queue an APC to target thread (KeInsertQueueApc).
 */
inline bool KeInsertQueueApc(KAPC* apc, void* sysArg1 = nullptr, void* sysArg2 = nullptr) {
    auto* curr = g_CurrentCpu.currentThread;
    if (!apc || apc->inserted || !curr) return false;
    apc->systemArgument1 = sysArg1;
    apc->systemArgument2 = sysArg2;
    apc->inserted = true;

    if (apc->apcMode == 0) {
        curr->kernelApcList.push_back(apc);
    } else {
        curr->userApcList.push_back(apc);
    }
    return true;
}

/**
 * @brief Deliver pending APCs to the currently running thread (KiDeliverApc).
 */
inline void KiDeliverApc(uint8_t deliveryMode) {
    auto* thread = g_CurrentCpu.currentThread;
    if (!thread) return;

    // 1. Deliver Special and Normal Kernel-Mode APCs
    if (deliveryMode == 0) {
        while (!thread->kernelApcList.empty()) {
            auto* apc = thread->kernelApcList.front();
            thread->kernelApcList.pop_front();
            apc->inserted = false;

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

    // 2. Deliver User-Mode APCs if thread is in an alertable wait at PASSIVE_LEVEL
    if (deliveryMode == 1 || thread->alertable) {
        if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
            while (!thread->userApcList.empty()) {
                auto* apc = thread->userApcList.front();
                thread->userApcList.pop_front();
                apc->inserted = false;

                if (apc->kernelRoutine) {
                    PKNORMAL_ROUTINE norm = apc->normalRoutine;
                    void* ctx = apc->normalContext;
                    void* a1 = apc->systemArgument1;
                    void* a2 = apc->systemArgument2;
                    apc->kernelRoutine(apc, &norm, &ctx, &a1, &a2);
                }
                if (apc->normalRoutine) {
                    apc->normalRoutine(apc->normalContext, apc->systemArgument1, apc->systemArgument2);
                }
                g_CurrentCpu.apcCount++;
            }
            thread->alertable = false;
        }
    }
}

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

inline thread_local bool t_kernelApcDisable{false};
inline thread_local bool t_specialApcDisable{false};

/**
 * @brief Enter a Critical Region (disables normal kernel APCs for current thread).
 */
inline void KeEnterCriticalRegion() noexcept {
    t_kernelApcDisable = true;
    if (auto* thread = KeGetCurrentThread()) {
        thread->kernelApcDisable = true;
    }
}

/**
 * @brief Leave a Critical Region (re-enables normal kernel APCs for current thread).
 */
inline void KeLeaveCriticalRegion() noexcept {
    t_kernelApcDisable = false;
    if (auto* thread = KeGetCurrentThread()) {
        thread->kernelApcDisable = false;
    }
}

/**
 * @brief Enter a Guarded Region (disables special kernel APCs for current thread).
 */
inline void KeEnterGuardedRegion() noexcept {
    t_specialApcDisable = true;
    if (auto* thread = KeGetCurrentThread()) {
        thread->specialApcDisable = true;
    }
}

/**
 * @brief Leave a Guarded Region (re-enables special kernel APCs for current thread).
 */
inline void KeLeaveGuardedRegion() noexcept {
    t_specialApcDisable = false;
    if (auto* thread = KeGetCurrentThread()) {
        thread->specialApcDisable = false;
    }
}

/**
 * @brief Check if APC delivery is disabled (Critical/Guarded region or IRQL >= APC_LEVEL).
 */
[[nodiscard]] inline bool KeAreApcsDisabled() noexcept {
    if (KeGetCurrentIrql() >= APC_LEVEL) return true;
    if (t_kernelApcDisable || t_specialApcDisable) return true;
    if (auto* thread = KeGetCurrentThread()) {
        return thread->kernelApcDisable || thread->specialApcDisable;
    }
    return false;
}

} // namespace micant::ke
