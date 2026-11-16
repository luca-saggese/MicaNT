#pragma once

#include "micant/km/core/ke.hpp"
#include "micant/km/core/clock_timer.hpp"

extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;

#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
namespace micant::sync::detail {

// Single-BSP dispatcher building block. This is not a public KEVENT or a
// Windows wait API: object references, priority boosts and SMP are not
// implemented here. The owner must keep the event alive until all waits end.
class BootstrapEvent {
public:
    explicit BootstrapEvent(bool notification, bool signaled = false) noexcept
        : notification_(notification), signaled_(signaled) {}
    BootstrapEvent(const BootstrapEvent&) = delete;
    BootstrapEvent& operator=(const BootstrapEvent&) = delete;

    ~BootstrapEvent() { if (head_) __builtin_trap(); }

    // Reinitialization requires the caller to exclude interrupts and waiters.
    void initialize(bool notification, bool signaled = false) noexcept {
        if (head_) __builtin_trap();
        notification_ = notification;
        signaled_ = signaled;
    }

    [[nodiscard]] bool readState() const noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const bool state = signaled_;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return state;
    }

    [[nodiscard]] NtStatus wait(bool poll = false, bool alertable = false,
                                uint64_t timeout100ns = 0) noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        NtStatus result = NtStatus::Unsuccessful;
        auto* current = ke::KeGetCurrentThread();
        if (current && current->state == ke::ThreadState::Running &&
            ke::KeGetCurrentIrql() <= ke::APC_LEVEL &&
            (!timeout100ns || ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL)) {
            // User APCs interrupt an alertable wait only when its wait mode
            // is UserMode. A kernel wait may be alertable for kernel APCs
            // without becoming a user-return boundary.
            current->alertable = alertable && current->eventWaitUserMode;
            if (ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL && !current->kernelApcList.empty()) {
                // The syscall entry normally has IF clear, even though an
                // APC kernel routine must be able to make hardware progress.
                // Preserve bootstrap's early IF state until the clock owns
                // interrupt routing and restore the wait boundary afterward.
                {
                    ke::detail::ReturnKernelContext kernelContext;
                    if ((flags & (uint64_t{1} << 9)) || hal::amd64::bootstrapSchedulerClockActive())
                        __asm__ __volatile__("sti" : : : "memory");
                    ke::KiDeliverApc(0);
                    __asm__ __volatile__("cli" : : : "memory");
                }
            }
            // Termination is completed by the owning Ke wait syscall after it
            // has dropped the waited object's reference and cleared its wait
            // registration. This event only unlinks and returns to that owner.
            if (current->terminationRequested && current->eventWaitCanTerminate) {
                result = NtStatus::UserApc;
            } else if (current->alertable && current->alerted[current->eventWaitUserMode ? ke::UserMode : ke::KernelMode]) {
                current->alerted[current->eventWaitUserMode ? ke::UserMode : ke::KernelMode] = false;
                result = NtStatus::Alerted;
            } else if (signaled_) {
                if (!notification_) signaled_ = false;
                result = NtStatus::Success;
            } else if (current->alertable && current->userApcList.hasEnvironment(current->apcStateIndex)) {
                current->userApcDeliveryPending = true;
                current->userApcDrainOnReturn = true;
                result = NtStatus::UserApc;
            } else if (poll) {
                result = NtStatus::Timeout;
            } else {
                Waiter waiter{current};
                waiter.event = this;
                ke::KDPC timeoutDpc{};
                ke::clock_timer::Deadline deadline{};
                bool timerArmed = false;
                if (timeout100ns) {
                    ke::KeInitializeDpc(&timeoutDpc, timeoutWaiter, &waiter);
                    timerArmed = ke::clock_timer::arm(deadline, timeoutDpc, timeout100ns);
                }
                current->eventWaitObject = this;
                current->eventWaitBlock = &waiter;
                // A kernel APC interrupts the wait only to run its callback;
                // the original wait resumes unless it was signaled/cancelled.
                if (ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL)
                    current->interruptWaitForApc = interruptForKernelApc;
                while (!timeout100ns || timerArmed) {
                    waiter.next = nullptr;
                    if (tail_) tail_->next = &waiter; else head_ = &waiter;
                    tail_ = &waiter;
                    if (!ke::detail::blockCurrentThread()) {
                        unlink(waiter);
                        break;
                    }
                    if (waiter.cancelled || waiter.satisfied || waiter.timedOut) {
                        result = waiter.cancelled ? (current->eventWaitAlerted ? NtStatus::Alerted : NtStatus::UserApc) :
                                 waiter.timedOut ? NtStatus::Timeout : NtStatus::Success;
                        break;
                    }
                    if (!waiter.apcInterrupted) __builtin_trap();
                    waiter.apcInterrupted = false;
                    if (current->alertable && current->userApcList.hasEnvironment(current->apcStateIndex)) {
                        current->userApcDeliveryPending = true;
                        current->userApcDrainOnReturn = true;
                        result = NtStatus::UserApc;
                        break;
                    }
                    {
                        ke::detail::ReturnKernelContext kernelContext;
                        if ((flags & (uint64_t{1} << 9)) || hal::amd64::bootstrapSchedulerClockActive())
                            __asm__ __volatile__("sti" : : : "memory");
                        ke::KiDeliverApc(0);
                        __asm__ __volatile__("cli" : : : "memory");
                    }
                    if (waiter.cancelled || current->eventWaitCancelled ||
                        (current->terminationRequested && current->eventWaitCanTerminate)) {
                        result = current->eventWaitAlerted ? NtStatus::Alerted : NtStatus::UserApc;
                        break;
                    }
                    if (waiter.satisfied) { result = NtStatus::Success; break; }
                    if (waiter.timedOut) { result = NtStatus::Timeout; break; }
                    // A signal may arrive while the waiter is detached for
                    // APC delivery; consume it before re-entering the queue.
                    if (signaled_) {
                        if (!notification_) signaled_ = false;
                        result = NtStatus::Success;
                        break;
                    }
                }
                if (timerArmed && !ke::clock_timer::cancelAndRemoveDpc(deadline)) __builtin_trap();
                current->interruptWaitForApc = nullptr;
                current->eventWaitObject = nullptr;
                current->eventWaitBlock = nullptr;
                current->eventWaitCancelled = false;
                current->eventWaitAlerted = false;
            }
            current->alertable = false;
            // A satisfied object or timeout must not discard an APC delivery
            // that was already pending before this wait. The queue remains
            // owned by Ke; only termination suppresses its user return.
            if (current->terminationRequested) {
                current->userApcDeliveryPending = false;
                current->userApcDrainOnReturn = false;
            }
        }
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return result;
    }

    [[nodiscard]] bool set() noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const bool previous = signaled_;
        signaled_ = true;
        while (head_) {
            Waiter* waiter = head_;
            head_ = waiter->next;
            if (!head_) tail_ = nullptr;
            waiter->next = nullptr;
            waiter->satisfied = true;
            auto* thread = waiter->thread;
            const bool registeredWait = thread && waiter->event == this &&
                thread->eventWaitObject == this && thread->eventWaitBlock == waiter;
            const bool alreadyReadyForThisWait = registeredWait &&
                thread != ke::KeGetCurrentThread() &&
                thread->state == ke::ThreadState::Ready && thread->readyQueued &&
                thread->eventWaitObject == this && thread->eventWaitBlock == waiter;
            if (!registeredWait || (!alreadyReadyForThisWait &&
                !ke::PriorityScheduler::get().readyKThread(thread))) {
                MicantNetbootSerialWrite("[MicaNT Ke] Event set rejected waiter thread=");
                MicantNetbootSerialWriteNumber(reinterpret_cast<uintptr_t>(thread));
                MicantNetbootSerialWrite(" current=");
                MicantNetbootSerialWriteNumber(reinterpret_cast<uintptr_t>(ke::KeGetCurrentThread()));
                MicantNetbootSerialWrite(" event=");
                MicantNetbootSerialWriteNumber(reinterpret_cast<uintptr_t>(this));
                MicantNetbootSerialWrite(" registered=");
                MicantNetbootSerialWriteNumber(registeredWait);
                MicantNetbootSerialWrite(" waiterEvent=");
                MicantNetbootSerialWriteNumber(waiter->event == this);
                MicantNetbootSerialWrite(" state=");
                MicantNetbootSerialWriteNumber(thread ? static_cast<uint32_t>(thread->state) : UINT32_MAX);
                MicantNetbootSerialWrite(" queued=");
                MicantNetbootSerialWriteNumber(thread && thread->readyQueued);
                MicantNetbootSerialWrite(" tid=");
                MicantNetbootSerialWriteNumber(thread ? thread->tid : 0);
                MicantNetbootSerialWrite(" waitObject=");
                MicantNetbootSerialWriteNumber(thread ? reinterpret_cast<uintptr_t>(thread->eventWaitObject) : 0);
                MicantNetbootSerialWrite(" waitBlock=");
                MicantNetbootSerialWriteNumber(thread ? reinterpret_cast<uintptr_t>(thread->eventWaitBlock) : 0);
                MicantNetbootSerialWrite("\r\n");
                __builtin_trap();
            }
            if (!notification_) { signaled_ = false; break; }
        }
        if (flags & (uint64_t{1} << 9)) {
            ke::detail::servicePendingDpcs();
            ke::detail::dispatchHigherPriority();
            ke::detail::servicePendingKernelApcs(true);
            __asm__ __volatile__("sti" : : : "memory");
        }
        return previous;
    }

    [[nodiscard]] bool reset() noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const bool previous = signaled_;
        signaled_ = false;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return previous;
    }

    // Caller holds the BSP dispatcher exclusion (interrupts disabled).
    [[nodiscard]] bool cancelWaitForTermination(ke::KThread& thread) noexcept {
        if (thread.eventWaitObject != this || !thread.eventWaitBlock ||
            thread.state != ke::ThreadState::Waiting || thread.eventWaitCancelled) return false;
        auto& waiter = *static_cast<Waiter*>(thread.eventWaitBlock);
        unlink(waiter);
        waiter.cancelled = true;
        thread.eventWaitCancelled = true;
        if (!ke::PriorityScheduler::get().readyKThread(&thread)) __builtin_trap();
        return true;
    }

    [[nodiscard]] bool cancelWaitForAlert(ke::KThread& thread) noexcept {
        if (thread.eventWaitObject != this || !thread.eventWaitBlock ||
            thread.state != ke::ThreadState::Waiting || thread.eventWaitCancelled) return false;
        auto& waiter = *static_cast<Waiter*>(thread.eventWaitBlock);
        unlink(waiter);
        waiter.cancelled = true;
        thread.eventWaitCancelled = true;
        thread.eventWaitAlerted = true;
        if (!ke::PriorityScheduler::get().readyKThread(&thread)) __builtin_trap();
        return true;
    }

    // Reinsert a cancelled wait if the terminating thread could not switch.
    [[nodiscard]] bool restoreCancelledWait(ke::KThread& thread) noexcept {
        if (thread.eventWaitObject != this || !thread.eventWaitBlock || !thread.eventWaitCancelled ||
            !ke::PriorityScheduler::get().removeReadyThread(&thread)) return false;
        auto& waiter = *static_cast<Waiter*>(thread.eventWaitBlock);
        waiter.cancelled = false;
        waiter.next = nullptr;
        if (tail_) tail_->next = &waiter; else head_ = &waiter;
        tail_ = &waiter;
        thread.eventWaitCancelled = false;
        thread.state = ke::ThreadState::Waiting;
        return true;
    }

private:
    struct Waiter {
        ke::KThread* thread;
        Waiter* next{nullptr};
        bool satisfied{false};
        bool cancelled{false};
        bool apcInterrupted{false};
        bool timedOut{false};
        BootstrapEvent* event{};
    };
    static void timeoutWaiter(ke::KDPC*, void* context, void*, void*) {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        auto& waiter = *static_cast<Waiter*>(context);
        if (!waiter.cancelled && !waiter.satisfied && !waiter.timedOut) {
            auto* event = waiter.event;
            auto* thread = waiter.thread;
            if (waiter.apcInterrupted && event->signaled_) {
                if (!event->notification_) event->signaled_ = false;
                waiter.satisfied = true;
            } else {
                waiter.timedOut = true;
                if (event->contains(waiter)) {
                    event->unlink(waiter);
                    if (thread->state == ke::ThreadState::Waiting &&
                        thread->eventWaitObject == event && thread->eventWaitBlock == &waiter &&
                        !ke::PriorityScheduler::get().readyKThread(thread)) __builtin_trap();
                }
            }
        }
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    }
    static bool interruptForKernelApc(ke::KThread& thread) noexcept {
        if (!thread.eventWaitObject || !thread.eventWaitBlock ||
            thread.state != ke::ThreadState::Waiting || thread.eventWaitCancelled) return false;
        auto& event = *static_cast<BootstrapEvent*>(thread.eventWaitObject);
        auto& waiter = *static_cast<Waiter*>(thread.eventWaitBlock);
        event.unlink(waiter);
        waiter.apcInterrupted = true;
        if (!ke::PriorityScheduler::get().readyKThread(&thread)) __builtin_trap();
        return true;
    }
    void unlink(Waiter& waiter) noexcept {
        Waiter* previous = nullptr;
        for (Waiter* item = head_; item; item = item->next) {
            if (item == &waiter) {
                if (previous) previous->next = item->next; else head_ = item->next;
                if (tail_ == item) tail_ = previous;
                return;
            }
            previous = item;
        }
        __builtin_trap();
    }
    [[nodiscard]] bool contains(const Waiter& waiter) const noexcept {
        for (const Waiter* item = head_; item; item = item->next)
            if (item == &waiter) return true;
        return false;
    }
    bool notification_;
    bool signaled_;
    Waiter* head_{nullptr};
    Waiter* tail_{nullptr};
};
}
#endif
