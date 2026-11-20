#include "micant/km/executive/ps.hpp"
#include "micant/km/core/ke.hpp"

#include "micant/km/core/syscalls.hpp"
#include "micant/km/core/sync.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/km/executive/section.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/perf_taskmgr.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/shared/context.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/native_process_abi.hpp"
#include "micant/km/win32k/freestanding.hpp"
#include <algorithm>
#include <cstring>

#if defined(MICANT_BOOTSTRAP_REACTOS_APP_TASKMGR)
extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;
#endif
#include <iterator>


#if defined(MICANT_FREESTANDING) && (defined(_M_X64) || defined(__x86_64__))
extern "C" uint32_t MicantAmd64XsaveMask;
extern "C" uint32_t MicantAmd64ExtendedSize;
#endif

#if defined(MICANT_BOOTSTRAP_CMD_TEST)
extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;
#define CMD_PS_TRACE(text) MicantNetbootSerialWrite(text)
#define CMD_PS_TRACE_VALUE(label, value) do { \
    MicantNetbootSerialWrite(label); \
    MicantNetbootSerialWriteNumber(static_cast<uint64_t>(value)); \
    MicantNetbootSerialWrite("\r\n"); \
} while (0)
#else
#define CMD_PS_TRACE(text) ((void)0)
#define CMD_PS_TRACE_VALUE(label, value) ((void)0)
#endif

#if defined(MICANT_BOOTSTRAP_CMD_TEST)
namespace micant::km::hid {
void DebugValidateInputQueues(const char* stage) noexcept;
}
#define CMD_PS_VALIDATE_HID(stage) micant::km::hid::DebugValidateInputQueues(stage)
#else
#define CMD_PS_VALIDATE_HID(stage) ((void)0)
#endif

namespace micant::sys {
NtStatus referenceProcessImageSection(Handle handle, ob::ObjectHeader*& reference) noexcept;
NtStatus mapBootstrapFileImageView(mm::ProcessAddressSpace& space, uintptr_t& base,
                                   section::SectionObject& image,
                                   ob::ObjectHeader* sectionReference) noexcept;
}

namespace micant::ps {
namespace {
constexpr size_t MinimumUserExceptionStackCommit = 24 * 1024;
constexpr size_t MinimumUserExceptionStackReserve =
    MinimumUserExceptionStackCommit + mm::PageSize4KB;
constexpr size_t UserThreadKernelStackSize = 64 * 1024;

// IDs share one namespace and are never reused by the bootstrap. Exhaustion
// fails creation instead of wrapping onto an existing process or thread.
std::atomic<uint32_t> nextClientId{4};
Handle allocateClientId() noexcept {
    uint32_t value = nextClientId.load(std::memory_order_relaxed);
    while (value != UINT32_MAX) {
        if (nextClientId.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
            return static_cast<Handle>(value);
    }
    return 0;
}

// Caller holds BSP interrupt exclusion, as for selectNextThread().
bool hasUsableIdleThread(const ke::KThread* current) noexcept {
    const auto* idle = ke::g_CurrentCpu.idleThread;
    return idle && idle != current && !idle->readyQueued &&
        (idle->state == ke::ThreadState::Initialized || idle->state == ke::ThreadState::Ready);
}

// The only freestanding dispatcher wait currently publishes this descriptor.
// A system thread's bootstrap critical region is the one APC-disable nesting
// level the termination callback can safely leave before exiting the thread.
bool canTerminateEventWait(const ke::KThread& thread) noexcept {
    const bool onlyInitialRegion = thread.systemInitialRegionActive && thread.kernelApcDisable == 1;
    return thread.eventWaitInProgress && thread.eventWaitCanTerminate &&
        thread.eventWaitObject && thread.eventWaitBlock && !thread.eventWaitCancelled &&
        (!thread.kernelApcDisable || onlyInitialRegion) && !thread.specialApcDisable &&
        !thread.normalKernelApcActive;
}
}

namespace detail {
// The queue owns no new reference: Ob-backed threads already hold a lifecycle
// reference until reap. All operations are serialized by BSP interrupt masking.
struct ThreadReaper {
    static inline EThread* head{};
    static inline EThread* tail{};
    static inline EProcess* processHead{};
    static inline EProcess* processTail{};
    static inline ob::ObjectHeader* deleteHead{};
    static inline ob::ObjectHeader* deleteTail{};
    static inline ke::KThread worker{};
    alignas(16) static inline uint8_t workerStack[65536]{};
    static inline bool workerInitialized{};
    static inline bool workerProcessing{};

    static void enqueue(EThread& thread) noexcept {
        if (!thread.objectHeader_ || thread.deferredQueued_) return;
        thread.deferredNext_ = nullptr;
        thread.deferredQueued_ = true;
        if (tail) tail->deferredNext_ = &thread; else head = &thread;
        tail = &thread;
    }
    static void enqueueProcess(EProcess& process, bool alreadyPinned = false) noexcept {
        // An in-flight worker will either finish cleanup or requeue its own
        // reference. A second queue entry would outlive the closed address
        // space and could leak the worker's transferred reference on retry.
        if (process.deferredQueued_ || process.deferredProcessing_) {
            if (alreadyPinned) __builtin_trap();
            return;
        }
        if (!process.objectHeader_) __builtin_trap();
        if (!alreadyPinned) process.objectHeader_->addReference();
        process.deferredNext_ = nullptr;
        process.deferredQueued_ = true;
        if (processTail) processTail->deferredNext_ = &process; else processHead = &process;
        processTail = &process;
    }
    // The caller takes responsibility for the queue's reference after
    // removing an item, and must dereference it outside BSP exclusion.
    static bool removeProcess(EProcess& process) noexcept {
        if (!process.deferredQueued_) return false;
        EProcess* previous{};
        for (auto* item = processHead; item; item = item->deferredNext_) {
            if (item != &process) { previous = item; continue; }
            if (previous) previous->deferredNext_ = item->deferredNext_; else processHead = item->deferredNext_;
            if (processTail == item) processTail = previous;
            item->deferredNext_ = nullptr;
            item->deferredQueued_ = false;
            return true;
        }
        __builtin_trap();
    }
    static void enqueueDelete(ob::ObjectHeader& header) noexcept {
        header.deferredDeleteNext = nullptr;
        if (deleteTail) deleteTail->deferredDeleteNext = &header; else deleteHead = &header;
        deleteTail = &header;
    }
    static void remove(EThread& thread) noexcept {
        if (!thread.deferredQueued_) return;
        EThread* previous{};
        for (auto* item = head; item; item = item->deferredNext_) {
            if (item != &thread) { previous = item; continue; }
            if (previous) previous->deferredNext_ = item->deferredNext_; else head = item->deferredNext_;
            if (tail == item) tail = previous;
            thread.deferredNext_ = nullptr;
            thread.deferredQueued_ = false;
            return;
        }
        __builtin_trap();
    }
    [[noreturn]] static void workerEntry(void*) noexcept {
        ke::KeLowerIrql(ke::PASSIVE_LEVEL);
        for (;;) {
            __asm__ __volatile__("cli" : : : "memory");
            workerProcessing = true;
            bool retry = false;
            size_t threadBudget{};
            for (auto* item = head; item; item = item->deferredNext_) ++threadBudget;
            while (head && threadBudget) {
                --threadBudget;
                auto* thread = head;
                remove(*thread);
                if (thread->thread_.get() == ke::KeGetCurrentThread()) {
                    enqueue(*thread);
                    retry = true;
                    continue;
                }
                // Pin the object before enabling interrupts: another caller
                // may race this worker and complete the reap first.
                auto* reference = thread->objectHeader_;
                if (!reference) __builtin_trap();
                reference->addReference();
                __asm__ __volatile__("sti" : : : "memory");
                const auto status = thread->reap();
                __asm__ __volatile__("cli" : : : "memory");
                const bool incomplete = status != NtStatus::Success && thread->thread_ != nullptr;
                if (incomplete) enqueue(*thread);
                // Drop the pin outside exclusion; its delete callback may run.
                __asm__ __volatile__("sti" : : : "memory");
                ob::ObDereferenceObject(reference);
                __asm__ __volatile__("cli" : : : "memory");
                if (incomplete) retry = true;
                // The object may have been destroyed by the last dereference.
            }
            size_t processBudget{};
            for (auto* item = processHead; item; item = item->deferredNext_) ++processBudget;
            while (processHead && processBudget) {
                --processBudget;
                auto* process = processHead;
                processHead = process->deferredNext_;
                if (!processHead) processTail = nullptr;
                process->deferredNext_ = nullptr;
                process->deferredQueued_ = false;
                process->deferredProcessing_ = true;
                process->synchronousCleanupRetry_ = false;
                auto* reference = process->objectHeader_;
                if (!reference) __builtin_trap();
                __asm__ __volatile__("sti" : : : "memory");
                const auto status = process->closeAddressSpace();
                if (status == NtStatus::Success) (void)process->terminationEvent().set();
                __asm__ __volatile__("cli" : : : "memory");
                process->deferredProcessing_ = false;
                if (status != NtStatus::Success) {
                    enqueueProcess(*process, true);
                    retry = true;
                } else {
                    __asm__ __volatile__("sti" : : : "memory");
                    ob::ObDereferenceObject(reference);
                    __asm__ __volatile__("cli" : : : "memory");
                }
            }
            size_t deleteBudget{};
            for (auto* item = deleteHead; item; item = item->deferredDeleteNext) ++deleteBudget;
            while (deleteHead && deleteBudget) {
                --deleteBudget;
                auto* header = deleteHead;
                deleteHead = header->deferredDeleteNext;
                if (!deleteHead) deleteTail = nullptr;
                header->deferredDeleteNext = nullptr;
                // The zero-reference header remains allocated until this
                // worker completes its potentially blocking delete callback.
                __asm__ __volatile__("sti" : : : "memory");
                if (isProcessObject(*header)) {
                    auto* process = header->as<EProcess>();
                    if (process->getDirectoryTableBase() &&
                        process->closeAddressSpace() != NtStatus::Success) {
                        // The delete callback cannot destroy a process whose
                        // VAD teardown is incomplete. Keep the zero-reference
                        // header owned by this queue and retry on a later
                        // scheduler boundary, outside lifecycle exclusion.
                        __asm__ __volatile__("cli" : : : "memory");
                        enqueueDelete(*header);
                        retry = true;
                        continue;
                    }
                }
                ob::ObFinishDelete(header);
                __asm__ __volatile__("cli" : : : "memory");
            }
            // Retry on a later scheduler boundary rather than spin on a
            // temporarily unavailable process or thread resource.
            workerProcessing = false;
            if ((!head && !processHead && !deleteHead) || retry) {
                if (!ke::detail::blockCurrentThread())
                    __asm__ __volatile__("sti; hlt; cli" : : : "memory");
            }
        }
    }

    static void service() noexcept {
        if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL || (!head && !processHead && !deleteHead) ||
            !hal::amd64::bootstrapSchedulerClockActive()) return;
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        if (!workerInitialized) {
            worker.directoryTableBase = hal::amd64::bootstrapKernelPagingRoot();
            worker.priority = ke::PRIORITY_NORMAL;
            if (!ke::detail::prepareBootstrapThread(worker, workerStack, sizeof(workerStack), workerEntry, nullptr))
                __builtin_trap();
            workerInitialized = true;
        }
        if (&worker != ke::KeGetCurrentThread() && !worker.readyQueued &&
            (worker.state == ke::ThreadState::Initialized || worker.state == ke::ThreadState::Waiting)) {
            if (!ke::PriorityScheduler::get().readyKThread(&worker)) __builtin_trap();
        }
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    }
};

bool deferredCleanupPending() noexcept {
    ob::detail::HandleTableGuard guard;
    return ThreadReaper::head || ThreadReaper::processHead ||
        ThreadReaper::deleteHead || ThreadReaper::workerProcessing;
}

bool deferProcessObjectDelete(ob::ObjectHeader* header) noexcept {
    if (!header) return false;
    // This is the zero-reference transition; remove the weak CID entry
    // before either synchronous deletion or deferred teardown can run.
    unregisterProcessObject(header);
    // Even after address-space cleanup, EProcess releases its primary token
    // on destruction; use the same IRQL rules as thread object deletion.
    auto* process = header->as<EProcess>();
    if (!process->getDirectoryTableBase())
        return deferThreadObjectDelete(header);
    if (!hal::amd64::bootstrapSchedulerClockActive() &&
        ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL && !ke::g_CurrentCpu.dpcRoutineActive) {
        // Bootstrap has no worker yet. Preserve the early synchronous path
        // when it succeeds, but retain the zero-reference object for retry
        // if Mm/handle cleanup cannot finish instead of invoking a deleting
        // destructor on a live address space.
        if (process->closeAddressSpace() == NtStatus::Success)
            return deferThreadObjectDelete(header);
    }
    ob::detail::HandleTableGuard guard;
    ThreadReaper::enqueueDelete(*header);
    ke::g_CurrentCpu.serviceDeferredCleanup = ThreadReaper::service;
    return true;
}

bool deferThreadObjectDelete(ob::ObjectHeader* header) noexcept {
    if (!header) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    if (ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL && !ke::g_CurrentCpu.dpcRoutineActive &&
        ((flags & (uint64_t{1} << 9)) || !hal::amd64::bootstrapSchedulerClockActive()))
        return false;
    // At zero references the Ob header remains allocated until the worker
    // finishes deletion. The destructor can release a security token, whose
    // own object callbacks must not run from a DPC or elevated IRQL.
    ob::detail::HandleTableGuard guard;
    ThreadReaper::enqueueDelete(*header);
    // Keep the zero-reference header resident even before worker startup;
    // once the scheduler clock is active the normal deferred-service hook
    // initializes and wakes the worker to finish this deletion.
    ke::g_CurrentCpu.serviceDeferredCleanup = ThreadReaper::service;
    return true;
}
}

EProcess::EProcess() = default;

EProcess::~EProcess() {
    if (primaryTokenReference_) {
        se::SeDereferenceFreestandingToken(primaryTokenReference_);
        primaryTokenReference_ = nullptr;
        primaryToken_ = nullptr;
    }
}

size_t EProcess::activeThreadCount() const noexcept {
    ob::detail::HandleTableGuard guard;
    size_t count{};
    for (auto* thread = threads_; thread; thread = thread->processNext)
        if (thread->state != ke::ThreadState::Terminated) ++count;
    return count;
}

NtStatus EProcess::initializeAddressSpace() noexcept {
    using namespace hal::amd64;
    ke::g_CurrentCpu.serviceDeferredCleanup = detail::ThreadReaper::service;
    if (root_.pfn || addressSpaceClosing_ || (objectHeader_ && processId_)) return NtStatus::InvalidParameter;
    const Handle processId = allocateClientId();
    if (!processId) return NtStatus::InsufficientResources;
    handles_.reset(new (std::nothrow_t{}) ob::HandleTable{});
    if (!handles_) return NtStatus::NoMemory;
    uint64_t interruptFlags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(interruptFlags) : : "memory");
    const bool rootCreated = createProcessPagingRoot(root_);
    if (interruptFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (!rootCreated) { handles_.reset(); return NtStatus::NoMemory; }
    const mm::ProcessAddressSpace::PagingBackend backend{
        &root_, mapBootstrapManagedPage, unmapBootstrapManagedPage,
        mapBootstrapProtectManagedPage, mapBootstrapManagedView, unmapBootstrapManagedView,
        mapBootstrapGuardManagedPage, replaceBootstrapManagedPage};
    if (!addressSpace_.configurePagingBackend(backend, BootstrapUserAddressBase, BootstrapUserAddressSize)) {
        if (!destroyProcessPagingRoot(root_)) __builtin_trap();
        handles_.reset();
        return NtStatus::Unsuccessful;
    }
    processId_ = processId;
    activeAttachReferences_ = 0;
    terminating_ = false;
    processPublished_ = false;
    synchronousCleanupRetry_ = false;
    exitStatus_ = NtStatus::Pending;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    (void)terminationEvent_.reset();
#endif
    imagesFinalized_ = false;
    userPeb_ = 0;
    userLoaderData_ = 0;
    userProcessParameters_ = 0;
    userApiSetMap_ = 0;
    userApiSetMapSize_ = 0;
    nativeImagePaths_ = {};
    processImagePath_ = {};
    processCommandLine_ = {};
    processParametersConfigured_ = false;
    imageSectionBacked_ = false;
    userTlsLayouts_.reset();
    userTlsCount_ = 0;
    return NtStatus::Success;
}

NtStatus EProcess::closeAddressSpace() noexcept {
    using namespace hal::amd64;
    uint64_t entryFlags{};
    uintptr_t observed{};
    __asm__ __volatile__("pushfq; popq %0; cli; movq %%cr3, %1"
                         : "=r"(entryFlags), "=r"(observed) : : "memory");
    if (!root_.pfn || attachedThreads_ || activeAttachReferences_ || addressSpaceClosing_ ||
        observed == getDirectoryTableBase()) {
        if (entryFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::InvalidParameter;
    }
    addressSpaceClosing_ = true;
    if (entryFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    // Handle deletion may invoke arbitrary object callbacks. Do it in the
    // caller's address space, before changing CR3 and outside Mm locks.
    handles_->closeAllHandles();
    NtStatus status = NtStatus::Success;
    bool done = false;
    while (!done) {
        uint64_t flags{};
        uintptr_t previous{};
        __asm__ __volatile__("pushfq; popq %0; cli; movq %%cr3, %1"
                             : "=r"(flags), "=r"(previous) : : "memory");
        mm::ProcessAddressSpace::DeferredOwnerRelease owner{};
        if (previous == getDirectoryTableBase()) status = NtStatus::InvalidParameter;
        else if (!switchProcessPagingRoot(&root_)) status = NtStatus::Unsuccessful;
        else {
            status = addressSpace_.closeOne(owner, done);
            if (!switchProcessPagingRootByPhysical(previous)) __builtin_trap();
        }
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        owner.releaseNow();
        if (status != NtStatus::Success) break;
    }
    if (status == NtStatus::Success) {
        win32k::freestanding::releaseProcess(processId_);
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        if (!destroyProcessPagingRoot(root_)) status = NtStatus::Unsuccessful;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    }
    if (status != NtStatus::Success) {
        uint64_t resetFlags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(resetFlags) : : "memory");
        addressSpaceClosing_ = false;
        if (resetFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    }
    userPeb_ = 0;
    userLoaderData_ = 0;
    userProcessParameters_ = 0;
    userApiSetMap_ = 0;
    userApiSetMapSize_ = 0;
    nativeImagePaths_ = {};
    processImagePath_ = {};
    processCommandLine_ = {};
    processParametersConfigured_ = false;
    imageSectionBacked_ = false;
    userTlsLayouts_.reset();
    userTlsCount_ = 0;
    images_.reset();
    ownedImageInputs_.reset();
    ownedImagePath_.reset();
    ownedCommandLine_.reset();
    handles_.reset();
    addressSpaceClosing_ = false;
    return NtStatus::Success;
}

NtStatus EProcess::attachThread(ke::KThread& thread) noexcept {
    ob::detail::HandleTableGuard guard;
    if (!root_.pfn || terminating_ || addressSpaceClosing_ || thread.owningProcess || thread.readyQueued ||
        (thread.state != ke::ThreadState::Initialized && thread.state != ke::ThreadState::Ready))
        return NtStatus::InvalidParameter;
    thread.owningProcess = this;
    constexpr uint32_t priorities[] = {0, 4, 8, 13, 24, 6, 10};
    if (processPriorityClass_ < std::size(priorities))
        thread.priority = priorities[processPriorityClass_];
    thread.directoryTableBase = getDirectoryTableBase();
    thread.processNext = threads_;
    threads_ = &thread;
    ++attachedThreads_;
    if (objectHeader_) objectHeader_->addReference();
    return NtStatus::Success;
}

void EProcess::setProcessPriorityClass(uint8_t priorityClass) noexcept {
    constexpr uint32_t priorities[] = {0, 4, 8, 13, 24, 6, 10};
    if (!priorityClass || priorityClass >= std::size(priorities)) return;
    processPriorityClass_ = priorityClass;
    auto& scheduler = ke::PriorityScheduler::get();
    for (auto* thread = threads_; thread; thread = thread->processNext) {
        const bool queued = thread->readyQueued && scheduler.removeReadyThread(thread);
        thread->priority = priorities[priorityClass];
        if (queued) {
            thread->state = ke::ThreadState::Ready;
            if (!scheduler.readyKThread(thread)) __builtin_trap();
        }
    }
}

NtStatus EProcess::referenceAddressSpaceForAttach(uintptr_t& directoryTableBase,
                                                   ob::ObjectHeader*& reference) noexcept {
    reference = nullptr;
    directoryTableBase = 0;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    NtStatus status = NtStatus::Success;
    if (!root_.pfn || !objectHeader_ || terminating_ || addressSpaceClosing_ ||
        activeAttachReferences_ == SIZE_MAX) {
        status = NtStatus::InvalidParameter;
    } else {
        reference = objectHeader_;
        reference->addReference();
        ++activeAttachReferences_;
        directoryTableBase = getDirectoryTableBase();
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return status;
}

void EProcess::releaseAddressSpaceAttachReference(ob::ObjectHeader* reference) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (!reference || !activeAttachReferences_) __builtin_trap();
    --activeAttachReferences_;
    const bool cleanupMayProceed = terminating_ && !activeAttachReferences_;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    ob::ObDereferenceObject(reference);
    // A terminating process may already be queued after its last thread
    // exited, but its cleanup could not proceed while an attach was active.
    // Wake the existing reaper once that final pin has gone away.
    if (cleanupMayProceed) detail::ThreadReaper::service();
}

void EProcess::onThreadTermination(NtStatus exitStatus) noexcept {
    if (!objectHeader_ || systemProcess_ || terminating_) return;
    for (auto* thread = threads_; thread; thread = thread->processNext)
        if (thread->state != ke::ThreadState::Terminated) return;
    terminating_ = true;
    exitStatus_ = exitStatus;
}

NtStatus EProcess::requestTermination(NtStatus exitStatus, ke::KThread* callingThread,
                                      bool terminateProcess) noexcept {
    // Caller holds BSP interrupt exclusion throughout the preflight and commit.
    if (!root_.pfn || terminating_) return NtStatus::ProcessIsTerminating;
    bool willHaveNext = ke::PriorityScheduler::get().getReadyThreadCount() ||
        hasUsableIdleThread(ke::KeGetCurrentThread());
    for (auto* thread = threads_; thread; thread = thread->processNext) {
        if (thread == callingThread || thread->state == ke::ThreadState::Terminated) continue;
        const bool waiting = thread->state == ke::ThreadState::Waiting;
        const bool ready = thread->state == ke::ThreadState::Ready && thread->readyQueued;
        const bool unstarted = thread->owningThread && !thread->owningThread->started_ &&
            thread->state == ke::ThreadState::Initialized && !thread->readyQueued;
        if (!thread->owningThread || (!unstarted && !waiting && !ready)) return NtStatus::NotImplemented;
        // A never-started system thread is safe to queue for target-context
        // cleanup: startThread observes terminationRequested and skips its
        // driver routine. A started system thread is accepted below only when
        // it is blocked in a cancellable Ke event wait.
        if (!thread->owningThread->started_ && (thread->kernelApcDisable ||
            thread->specialApcDisable || thread->normalKernelApcActive))
            return NtStatus::NotImplemented;
        // User waits have a termination return boundary that releases the
        // waited object before rundown. Kernel waits can hold unrelated
        // resources and must return through their owner normally.
        const bool cancellable = waiting && thread->eventWaitUserMode &&
            canTerminateEventWait(*thread);
        if (thread->owningThread->isSystemThread() && thread->owningThread->started_ && !cancellable)
            return NtStatus::AccessDenied;
        if (cancellable && thread->readyQueued) return NtStatus::Unsuccessful;
        if (unstarted || ready || cancellable) willHaveNext = true;
    }
    // A self-termination cannot commit without a runnable replacement stack.
    if (terminateProcess && callingThread && !willHaveNext) return NtStatus::Unsuccessful;
    // Queue never-started threads first. If that fails, rollback has not
    // disturbed event wait queues or their FIFO ordering.
    bool success = true;
    for (auto* thread = threads_; thread; thread = thread->processNext) {
        if (thread == callingThread || thread->state != ke::ThreadState::Initialized) continue;
        if (!ke::PriorityScheduler::get().readyKThread(thread)) { success = false; break; }
        thread->terminationQueuedByProcess = true;
    }
    if (!success) {
        for (auto* thread = threads_; thread; thread = thread->processNext) {
            if (thread == callingThread) continue;
            if (thread->terminationQueuedByProcess) {
                if (!ke::PriorityScheduler::get().removeReadyThread(thread)) __builtin_trap();
                thread->state = ke::ThreadState::Initialized;
                thread->terminationQueuedByProcess = false;
            }
        }
        return NtStatus::Unsuccessful;
    }
    // Every eligible waiter was preflighted while interrupts were excluded;
    // cancellation and ready insertion cannot fail without a broken invariant.
    for (auto* thread = threads_; thread; thread = thread->processNext) {
        if (thread == callingThread || thread->state != ke::ThreadState::Waiting ||
            !thread->eventWaitUserMode || !canTerminateEventWait(*thread)) continue;
        if (!static_cast<sync::detail::BootstrapEvent*>(thread->eventWaitObject)->cancelWaitForTermination(*thread))
            __builtin_trap();
    }
    if (terminateProcess) {
        terminating_ = true;
        exitStatus_ = exitStatus;
    }
    for (auto* thread = threads_; thread; thread = thread->processNext) {
        thread->terminationQueuedByProcess = false;
        if (!terminateProcess && thread == callingThread) continue;
        if (thread->state != ke::ThreadState::Terminated && !thread->terminationRequested) {
            thread->owningThread->requestedExitStatus_ = exitStatus;
            thread->terminationRequested = true;
        }
    }
    return NtStatus::Success;
}

NtStatus EProcess::terminateOtherThreads(NtStatus exitStatus) noexcept {
    ob::detail::HandleTableGuard guard;
    auto* current = ke::KeGetCurrentThread();
    if (!current || ke::KeGetCurrentProcess() != this ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    return requestTermination(exitStatus, current, false);
}

NtStatus EProcess::terminateCurrent(NtStatus exitStatus) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* current = ke::KeGetCurrentThread();
    CMD_PS_TRACE("[MicaNT PsTrace] terminateCurrent entered\r\n");
    NtStatus status = NtStatus::InvalidParameter;
    if (current && current->owningProcess == this && current->owningThread &&
        current->state == ke::ThreadState::Running && ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL) {
        if (current->kernelApcDisable || current->specialApcDisable || current->normalKernelApcActive)
            status = NtStatus::NotImplemented;
        else if (terminating_)
            status = NtStatus::Success;
        else
            status = requestTermination(exitStatus, current);
    }
    CMD_PS_TRACE("[MicaNT PsTrace] terminateCurrent request returned\r\n");
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (status != NtStatus::Success) return status;
    io::IoCancelProcessReads(*this);
    CMD_PS_TRACE("[MicaNT PsTrace] process read cancellation returned\r\n");
    return current->owningThread->exitCurrent(exitStatus_);
}

NtStatus EProcess::terminateOther(NtStatus exitStatus) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* current = ke::KeGetCurrentThread();
    NtStatus result{};
    if (!root_.pfn) result = NtStatus::ProcessIsTerminating;
    else if (!current || current->owningProcess == this || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        result = NtStatus::InvalidParameter;
    else if (terminating_ && synchronousCleanupRetry_ && !attachedThreads_ && !addressSpaceClosing_) {
        // A prior synchronous cleanup may have removed some resources before
        // failing. Retry the remaining teardown without undoing termination
        // or replacing its original exit status.
        synchronousCleanupRetry_ = false;
        if (hal::amd64::bootstrapSchedulerClockActive() && objectHeader_) {
            detail::ThreadReaper::enqueueProcess(*this);
            if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
            return NtStatus::Success;
        }
        const bool queuedReference = detail::ThreadReaper::removeProcess(*this);
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        result = closeAddressSpace();
        if (result != NtStatus::Success) {
            ob::detail::HandleTableGuard guard;
            synchronousCleanupRetry_ = true;
            if (objectHeader_) detail::ThreadReaper::enqueueProcess(*this);
        }
        if (result == NtStatus::Success) (void)terminationEvent_.set();
        if (queuedReference) ob::ObDereferenceObject(objectHeader_);
        return result;
    }
    else if (terminating_) result = NtStatus::ProcessIsTerminating;
    else if (attachedThreads_) result = requestTermination(exitStatus, nullptr);
    else {
        terminating_ = true;
        exitStatus_ = exitStatus;
        if (hal::amd64::bootstrapSchedulerClockActive() && objectHeader_) {
            detail::ThreadReaper::enqueueProcess(*this);
            result = NtStatus::Success;
        } else {
            // Early bootstrap has no worker. A non-Ob bootstrap process also
            // has no reference the worker could retain; close synchronously.
            if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
            result = closeAddressSpace();
            if (result != NtStatus::Success) {
                ob::detail::HandleTableGuard guard;
                synchronousCleanupRetry_ = true;
                if (objectHeader_) detail::ThreadReaper::enqueueProcess(*this);
            }
            if (result == NtStatus::Success) (void)terminationEvent_.set();
            return result;
        }
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

NtStatus EProcess::detachThread(ke::KThread& thread) noexcept {
    ob::ObjectHeader* owner{};
    {
        ob::detail::HandleTableGuard guard;
        if (thread.owningProcess != this || thread.readyQueued ||
            (thread.state != ke::ThreadState::Initialized && thread.state != ke::ThreadState::Terminated))
            return NtStatus::InvalidParameter;
        if (!attachedThreads_) __builtin_trap();
        ke::KThread** link = &threads_;
        while (*link && *link != &thread) link = &(*link)->processNext;
        if (!*link) __builtin_trap();
        *link = thread.processNext;
        thread.processNext = nullptr;
        --attachedThreads_;
        owner = objectHeader_;
        thread.owningProcess = nullptr;
        thread.directoryTableBase = 0;
    }
    // A final dereference can invoke a delete callback; do not hold the
    // process-list exclusion while it runs.
    if (owner) ob::ObDereferenceObject(owner);
    return NtStatus::Success;
}

EThread::~EThread() {
    if (impersonationTokenReference_) {
        se::SeDereferenceFreestandingToken(impersonationTokenReference_);
        impersonationTokenReference_ = nullptr;
        impersonationToken_ = nullptr;
    }
    if (thread_ || stack_ || process_ || reapProcessReference_ || processDetached_) __builtin_trap();
    while (retiredThreads_) {
        auto* thread = retiredThreads_;
        retiredThreads_ = thread->retiredNext;
        // The CID table is weak: after Ob's zero-reference transition a
        // lookup cannot acquire the object, but the retired KThread remains
        // resident until this final deletion removes its entry.
        if (objectOwned_) ke::detail::unregisterClientThread(*thread);
        delete thread;
    }
    if (retiredProcessReference_) {
        auto* reference = retiredProcessReference_;
        retiredProcessReference_ = nullptr;
        ob::ObDereferenceObject(reference);
    }
}

NtStatus EThread::initialize(EProcess& process, size_t stackSize,
                             StartRoutine start, void* argument) noexcept {
    if (objectOwned_ || thread_ || stack_ || process_ || !start || stackSize < 8192 || stackSize > 1024 * 1024)
        return NtStatus::InvalidParameter;
    const Handle threadId = allocateClientId();
    if (!threadId) return NtStatus::InsufficientResources;
    std::unique_ptr<ke::KThread> thread(new (std::nothrow_t{}) ke::KThread{});
    std::unique_ptr<uint8_t[]> stack(new (std::nothrow_t{}) uint8_t[stackSize]);
    if (!thread || !stack) return NtStatus::NoMemory;
    if (!ke::detail::prepareBootstrapThread(*thread, stack.get(), stackSize, startThread, this))
        return NtStatus::InvalidParameter;
    thread->tid = threadId;
    thread->pid = process.processId();
    thread->terminationNotify = signalTermination;
    thread->serviceTermination = serviceTermination;
    thread->terminationContext = this;
    thread->owningThread = this;
    // Publish the process membership and the owning EThread state as one
    // transition; termination must not schedule a partly initialized owner.
    ob::detail::HandleTableGuard guard;
    const auto status = process.attachThread(*thread);
    if (status != NtStatus::Success) return status;
    ke::detail::registerClientThread(*thread);
    ke::g_CurrentCpu.serviceDeferredCleanup = detail::ThreadReaper::service;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    (void)terminationEvent_.reset();
#endif
    process_ = &process;
    thread_ = std::move(thread);
    stack_ = std::move(stack);
    start_ = start;
    argument_ = argument;
    exitStatus_ = NtStatus::Pending;
    requestedExitStatus_ = NtStatus::Success;
    systemThread_ = false;
    return NtStatus::Success;
}

NtStatus EThread::initializeSystem(EProcess& process, size_t stackSize,
                                    SystemStartRoutine start, void* argument) noexcept {
    if (!start) return NtStatus::InvalidParameter;
    ob::detail::HandleTableGuard guard;
    const auto status = initialize(process, stackSize, startSystem, this);
    if (status == NtStatus::Success) {
        systemThread_ = true;
        systemStart_ = start;
        systemArgument_ = argument;
    }
    return status;
}

void EThread::startSystem(void* argument) noexcept {
    auto* self = static_cast<EThread*>(argument);
    if (!self || !self->systemStart_ || !self->thread_->systemInitialRegionActive) __builtin_trap();
    self->systemStart_(self->systemArgument_);
    // Drivers are expected to call PsTerminateSystemThread themselves.
    // A returning routine still exits through the same lifecycle path.
    if (self->thread_->systemInitialRegionActive) ke::KeLeaveCriticalRegion();
}

NtStatus EThread::terminateSystemThread(NtStatus status) noexcept {
    if (!systemStart_ || ke::KeGetCurrentThread() != thread_.get() ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    const bool leaveInitialRegion = thread_->systemInitialRegionActive;
    if (leaveInitialRegion) ke::KeLeaveCriticalRegion();
    const auto result = exitCurrent(status);
    if (leaveInitialRegion) {
        ke::KeEnterCriticalRegion();
        thread_->systemInitialRegionActive = true;
    }
    return result;
}

extern "C" __declspec(dllexport) NtStatus PsTerminateSystemThread(NtStatus exitStatus) noexcept {
    auto* current = ke::KeGetCurrentThread();
    if (!current || !current->owningThread) return NtStatus::InvalidParameter;
    return current->owningThread->terminateSystemThread(exitStatus);
}

NtStatus EThread::ready() noexcept {
    ob::detail::HandleTableGuard guard;
    if (reapInProgress_ || !thread_ || thread_->terminationRequested ||
        thread_->initialSuspendCount || thread_->state != ke::ThreadState::Initialized)
        return NtStatus::InvalidParameter;
    return ke::PriorityScheduler::get().readyKThread(thread_.get())
        ? NtStatus::Success : NtStatus::Unsuccessful;
}

NtStatus EThread::suspendInitially() noexcept {
    ob::detail::HandleTableGuard guard;
    if (!thread_ || started_ || reapInProgress_ || thread_->readyQueued ||
        thread_->terminationRequested || thread_->state != ke::ThreadState::Initialized ||
        thread_->initialSuspendCount) return NtStatus::InvalidParameter;
    thread_->initialSuspendCount = 1;
    return NtStatus::Success;
}

NtStatus EThread::resumeInitial(uint32_t& previousCount) noexcept {
    ob::detail::HandleTableGuard guard;
    if (!thread_) {
        if (!objectOwned_) return NtStatus::InvalidParameter;
        previousCount = 0;
        return NtStatus::Success;
    }
    if (reapInProgress_) return NtStatus::InvalidParameter;
    previousCount = thread_->initialSuspendCount;
    if (thread_->terminationRequested || thread_->state == ke::ThreadState::Terminated) {
        thread_->initialSuspendCount = 0;
        return NtStatus::Success;
    }
    if (!previousCount) return NtStatus::Success;
    if (started_ || thread_->state != ke::ThreadState::Initialized || thread_->readyQueued)
        return NtStatus::InvalidParameter;
    thread_->initialSuspendCount = 0;
    if (!ke::PriorityScheduler::get().readyKThread(thread_.get())) {
        thread_->initialSuspendCount = 1;
        return NtStatus::Unsuccessful;
    }
    return NtStatus::Success;
}

NtStatus EThread::initializeUser(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                                 size_t userStackSize, const pe::PeImageView64::TlsLayout* tls,
                                 bool interruptible) noexcept {
    return initializeUserWithTls(process, entry, kernelStackSize, userStackSize,
        tls ? std::span<const pe::PeImageView64::TlsLayout>{tls, 1} : std::span<const pe::PeImageView64::TlsLayout>{},
        interruptible);
}

NtStatus EThread::initializeUserWithTls(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                                        size_t userStackSize, std::span<const pe::PeImageView64::TlsLayout> tls,
                                        bool interruptible, size_t commitSize) noexcept {
    // The target CR3 and the newly attached thread must remain stable until
    // its TEB, stack and startup state have all been installed.
    ob::detail::HandleTableGuard guard;
    // A preemptible user thread must always have a scheduler-owned successor
    // when it exits, including after an unhandled user exception. The idle
    // thread is installed together with the bootstrap scheduler clock.
    if (interruptible && (!hal::amd64::bootstrapSchedulerClockActive() ||
                          !hasUsableIdleThread(ke::KeGetCurrentThread())))
        return NtStatus::DeviceNotReady;
    uintptr_t currentRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(currentRoot));
    if (currentRoot != process.getDirectoryTableBase() || !entry || userStackSize < mm::PageSize4KB ||
        userStackSize > SIZE_MAX - mm::PageMask4KB ||
        !hal::amd64::bootstrapUserRange(entry, 1, false, true)) return NtStatus::InvalidParameter;
    if (tls.size() > 16) return NtStatus::NotImplemented;
    for (const auto& layout : tls) {
        if (!layout.present || !layout.alignment || layout.alignment > 256 ||
            layout.initialData.size() > mm::PageSize4KB - 0x100 ||
            layout.zeroFill > mm::PageSize4KB - 0x100 - layout.initialData.size()) return NtStatus::NotImplemented;
        if (layout.callbackCount > layout.callbacks.size()) return NtStatus::NotImplemented;
        for (size_t i = 0; i < layout.callbackCount; ++i)
            if (!hal::amd64::bootstrapUserRange(layout.callbacks[i], 1, false, true))
                return NtStatus::NotImplemented;
    }
    size_t stackSize = (userStackSize + mm::PageMask4KB) & ~mm::PageMask4KB;
    if (!commitSize) commitSize = MinimumUserExceptionStackCommit;
    if (commitSize > SIZE_MAX - mm::PageMask4KB)
        return NtStatus::InvalidParameter;
    commitSize = (commitSize + mm::PageMask4KB) & ~mm::PageMask4KB;
    // Keep enough committed stack for ordinary exception dispatch and a
    // separate reservation below the moving guard for stack overflow.  A
    // guard at the very bottom cannot deliver a frame below the interrupted
    // RSP, even if the already committed stack above it is large enough.
    if (commitSize < MinimumUserExceptionStackCommit)
        commitSize = MinimumUserExceptionStackCommit;
    constexpr size_t emergencyReserve = MinimumUserExceptionStackCommit + mm::PageSize4KB;
    if (commitSize > SIZE_MAX - emergencyReserve) return NtStatus::InvalidParameter;
    const size_t minimumReserve = commitSize + emergencyReserve;
    if (stackSize < minimumReserve) stackSize = minimumReserve;
    auto status = initialize(process, kernelStackSize, startUser, this);
    if (status != NtStatus::Success) return status;
    auto& space = process.getAddressSpace();
    status = space.allocate(userStack_, stackSize, mm::MEM_RESERVE, mm::PAGE_READWRITE);
    if (status == NtStatus::Success) {
        uintptr_t committed = userStack_ + stackSize - commitSize;
        status = space.allocate(committed, commitSize, mm::MEM_COMMIT, mm::PAGE_READWRITE);
    }
    if (status == NtStatus::Success && commitSize < stackSize) {
        uintptr_t guardPage = userStack_ + stackSize - commitSize - mm::PageSize4KB;
        status = space.allocate(guardPage, mm::PageSize4KB, mm::MEM_COMMIT, mm::PAGE_READWRITE);
        if (status == NtStatus::Success) status = space.setStackGuard(guardPage, true);
        if (status == NtStatus::Success) userStackGuard_ = guardPage;
    }
    if (status == NtStatus::Success) {
        userStackLimit_ = userStack_ + stackSize - commitSize;
        userStackTop_ = userStack_ + stackSize;
    }
    if (status == NtStatus::Success)
        status = space.allocate(userTeb_, 2 * mm::PageSize4KB, mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
    if (status == NtStatus::Success && !tls.empty())
        status = space.allocate(userTls_, mm::PageSize4KB, mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
    if (status == NtStatus::Success && !tls.empty()) {
        size_t offset = 0x100;
        auto* vector = reinterpret_cast<uint64_t*>(userTls_);
        for (size_t i = 0; i < tls.size(); ++i) {
            const auto& layout = tls[i];
            if (layout.alignment & (layout.alignment - 1)) { status = NtStatus::InvalidParameter; break; }
            offset = (offset + layout.alignment - 1) & ~(layout.alignment - 1);
            const size_t length = layout.initialData.size() + layout.zeroFill;
            if (offset > mm::PageSize4KB || length > mm::PageSize4KB - offset) {
                status = NtStatus::NotImplemented;
                break;
            }
            vector[i] = userTls_ + offset;
            status = pe::PeImageView64::initializeTlsData(layout,
                std::span<uint8_t>{reinterpret_cast<uint8_t*>(userTls_ + offset), length});
            if (status != NtStatus::Success) break;
            offset += length;
        }
    }
    if (status != NtStatus::Success) {
        // Preserve the creation failure; the factory retains and queues any
        // remaining thread state if immediate rollback cannot finish.
        (void)reap();
        return status;
    }
    static_assert(sizeof(um::Teb64) <= 2 * mm::PageSize4KB);
    auto* teb = new (reinterpret_cast<void*>(userTeb_)) um::Teb64{};
    teb->self = userTeb_;
    teb->stackBase = userStack_ + stackSize;
    teb->stackLimit = userStack_ + stackSize - commitSize;
    teb->deallocationStack = userStack_;
    // Match the committed exception/dispatcher guarantee, including the
    // bounded NTDLL unwind stack and maximum supported XSAVE payload.
    teb->guaranteedStackBytes = static_cast<uint32_t>(MinimumUserExceptionStackCommit);
    teb->threadLocalStoragePointer = userTls_;
    teb->processEnvironmentBlock = process.userPebAddress();
    teb->uniqueProcessId = process.processId();
    teb->uniqueThreadId = thread_->tid;
    userEntry_ = entry;
    userInterruptible_ = interruptible;
    // Entering a PE image is ABI-equivalent to a call target: RSP is 8 mod 16
    // after the caller's return address. The bootstrap iret does not push that
    // return address, so reserve 136 bytes from the aligned stack top.
    userStackPointer_ = userStack_ + stackSize - 136;
    if (process.images_ && !process.imageStartupCallbacksStarted_ && process.images_->size()) {
        const auto* mainImage = process.images_->module(0);
        if (mainImage && mainImage->view.headers().optionalHeader.addressOfEntryPoint &&
            mainImage->base <= UINTPTR_MAX - mainImage->view.headers().optionalHeader.addressOfEntryPoint &&
            entry == mainImage->base + mainImage->view.headers().optionalHeader.addressOfEntryPoint) {
            constexpr size_t MaximumCallbacks = 16 * 64;
            std::array<pe::PeImageView64::TlsLayout, 16> layouts{};
            std::array<const um::NativeUserImageSet64::Module*, 16> modules{};
            size_t callbackCount{};
            for (size_t i = 0; i < process.images_->size(); ++i) {
                const auto* module = process.images_->module(i);
                if (!module) { status = NtStatus::InvalidImageFormat; break; }
                modules[i] = module;
                status = module->view.tlsLayout(module->immutableImage(), module->base, layouts[i]);
                if (status != NtStatus::Success) break;
                if (layouts[i].callbackCount > MaximumCallbacks - callbackCount) {
                    status = NtStatus::NotSupported;
                    break;
                }
                callbackCount += layouts[i].callbackCount;
            }
            if (status == NtStatus::Success && callbackCount) {
                constexpr size_t CallbackCodeBytes = 30;
                if (callbackCount > (SIZE_MAX - 16) / CallbackCodeBytes) {
                    status = NtStatus::InvalidParameter;
                } else {
                    const size_t codeSize = callbackCount * CallbackCodeBytes + 16;
                    std::unique_ptr<uint8_t[]> code(new (std::nothrow_t{}) uint8_t[codeSize]{});
                    if (!code) {
                        status = NtStatus::NoMemory;
                    } else {
                        size_t cursor{};
                        const auto byte = [&](uint8_t value) noexcept { code[cursor++] = value; };
                        const auto dword = [&](uint32_t value) noexcept {
                            for (unsigned i = 0; i < 4; ++i) byte(static_cast<uint8_t>(value >> (i * 8)));
                        };
                        const auto qword = [&](uint64_t value) noexcept {
                            for (unsigned i = 0; i < 8; ++i) byte(static_cast<uint8_t>(value >> (i * 8)));
                        };
                        byte(0x48); byte(0x83); byte(0xec); byte(0x28); // shadow space and call alignment
                        for (size_t i = 0; i < process.images_->size(); ++i) {
                            for (size_t j = 0; j < layouts[i].callbackCount; ++j) {
                                byte(0x48); byte(0xb9); qword(modules[i]->base); // RCX = DllHandle
                                byte(0xba); dword(1);                            // EDX = DLL_PROCESS_ATTACH
                                byte(0x45); byte(0x31); byte(0xc0);             // R8 = Reserved = nullptr
                                byte(0x48); byte(0xb8); qword(layouts[i].callbacks[j]);
                                byte(0xff); byte(0xd0);                         // call RAX
                            }
                        }
                        byte(0x48); byte(0x83); byte(0xc4); byte(0x28);
                        byte(0x48); byte(0xb8); qword(entry);
                        byte(0xff); byte(0xe0);                                 // tail-jump to PE entry
                        if (cursor != codeSize) {
                            status = NtStatus::Unsuccessful;
                        } else {
                            status = space.allocate(userStartupThunk_, codeSize,
                                mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
                            if (status == NtStatus::Success) {
                                std::memcpy(reinterpret_cast<void*>(userStartupThunk_), code.get(), codeSize);
                                status = space.protect(userStartupThunk_, codeSize, mm::PAGE_EXECUTE_READ, nullptr);
                                if (status == NtStatus::Success) {
                                    userStartupEntry_ = entry;
                                    userStartupCallbacks_ = true;
                                    userEntry_ = userStartupThunk_;
                                } else {
                                    (void)space.free(userStartupThunk_, 0, mm::MEM_RELEASE);
                                    userStartupThunk_ = 0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    thread_->tebAddress = userTeb_;
    thread_->previousUserMode = true;
    return NtStatus::Success;
}

NtStatus EThread::initializeUser(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                                 size_t userStackSize, bool interruptible) noexcept {
    if (!process.nativeImagesFinalized()) return NtStatus::InvalidParameter;
    return initializeUserWithTls(process, entry, kernelStackSize, userStackSize,
                                 process.userTlsLayouts(), interruptible);
}

NtStatus EThread::initializeUser(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                                 size_t reserveSize, size_t commitSize, bool interruptible) noexcept {
    if (!process.nativeImagesFinalized()) return NtStatus::InvalidParameter;
    return initializeUserWithTls(process, entry, kernelStackSize, reserveSize,
                                 process.userTlsLayouts(), interruptible, commitSize);
}

NtStatus EThread::configureUserStart(uintptr_t routine, uintptr_t argument) noexcept {
    ob::detail::HandleTableGuard guard;
    if (!thread_ || !userEntry_ || started_ || thread_->readyQueued || reapInProgress_ ||
        thread_->state != ke::ThreadState::Initialized) return NtStatus::InvalidParameter;
    userStartRoutine_ = routine;
    userStartArgument_ = argument;
    return NtStatus::Success;
}

NtStatus EThread::resolveUserStackGuard(uintptr_t address) noexcept {
    ob::detail::HandleTableGuard guard;
    if (!userStackGuard_ ||
        (address & ~static_cast<uintptr_t>(mm::PageMask4KB)) != userStackGuard_)
        return NtStatus::AccessViolation;
    uintptr_t currentRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(currentRoot));
    if (!thread_ || ke::KeGetCurrentThread() != thread_.get() || !process_ ||
        thread_->owningProcess != process_ ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL || currentRoot != process_->getDirectoryTableBase() ||
        !userStackGuard_)
        return NtStatus::GuardPageViolation;
    if (!hal::amd64::bootstrapUserRange(userTeb_, sizeof(um::Teb64), true)) return NtStatus::AccessViolation;
    auto& space = process_->getAddressSpace();
    if (userStackGuard_ < userStack_ + mm::PageSize4KB) return NtStatus::StackOverflow;
    const uintptr_t next = userStackGuard_ - mm::PageSize4KB;
    const auto* vad = space.findVad(next);
    if (!vad || vad != space.findVad(userStackGuard_) || vad->startingAddress != userStack_ || vad->isSectionView)
        return NtStatus::GuardPageViolation;
    // Stop stack growth while enough of the reservation remains below the
    // guard for the exception frame and dispatcher. Commit that emergency
    // area before reporting overflow; it must be below the interrupted RSP,
    // not on top of frames that are still active.
    constexpr size_t overflowGuarantee = MinimumUserExceptionStackCommit;
    const size_t remaining = static_cast<size_t>(userStackGuard_ - userStack_);
    if (remaining <= overflowGuarantee) {
        uintptr_t newlyCommitted[overflowGuarantee / mm::PageSize4KB]{};
        size_t newlyCount{};
        for (uintptr_t pageAddress = userStack_; pageAddress < userStackGuard_;
             pageAddress += mm::PageSize4KB) {
            bool committed = false;
            for (auto* page = vad->commitRanges.get(); page; page = page->next.get()) {
                if (page->firstVpn != (pageAddress >> mm::PageShift4KB)) continue;
                if (page->protection != mm::PAGE_READWRITE) {
                    while (newlyCount)
                        if (space.free(newlyCommitted[--newlyCount], mm::PageSize4KB, mm::MEM_DECOMMIT) != NtStatus::Success)
                            __builtin_trap();
                    return NtStatus::GuardPageViolation;
                }
                committed = true;
                break;
            }
            if (committed) continue;
            uintptr_t target = pageAddress;
            const auto commit = space.allocate(target, mm::PageSize4KB, mm::MEM_COMMIT, mm::PAGE_READWRITE);
            if (commit != NtStatus::Success) {
                while (newlyCount)
                    if (space.free(newlyCommitted[--newlyCount], mm::PageSize4KB, mm::MEM_DECOMMIT) != NtStatus::Success)
                        __builtin_trap();
                return commit;
            }
            newlyCommitted[newlyCount++] = pageAddress;
        }
        const auto unguard = space.setStackGuard(userStackGuard_, false);
        if (unguard != NtStatus::Success) {
            while (newlyCount)
                if (space.free(newlyCommitted[--newlyCount], mm::PageSize4KB, mm::MEM_DECOMMIT) != NtStatus::Success)
                    __builtin_trap();
            return unguard;
        }
        userStackLimit_ = userStack_;
        reinterpret_cast<um::Teb64*>(userTeb_)->stackLimit = userStackLimit_;
        userStackGuard_ = 0;
        return NtStatus::StackOverflow;
    }
    bool nextCommitted = false;
    for (auto* page = vad->commitRanges.get(); page; page = page->next.get()) {
        if (page->firstVpn != (next >> mm::PageShift4KB)) continue;
        if (page->protection != mm::PAGE_READWRITE) return NtStatus::GuardPageViolation;
        nextCommitted = true;
        break;
    }
    const bool exhausted = userStackGuard_ - userStack_ <= 2 * mm::PageSize4KB;
    if (nextCommitted && !exhausted) return NtStatus::GuardPageViolation;
    uintptr_t target = next;
    auto status = nextCommitted ? NtStatus::Success :
        space.allocate(target, mm::PageSize4KB, mm::MEM_COMMIT, mm::PAGE_READWRITE);
    if (status != NtStatus::Success) return status;
    if (!exhausted) status = space.setStackGuard(next, true);
    if (status == NtStatus::Success) status = space.setStackGuard(userStackGuard_, false);
    if (status != NtStatus::Success) {
        if (!nextCommitted && space.free(next, mm::PageSize4KB, mm::MEM_DECOMMIT) != NtStatus::Success)
            __builtin_trap();
        return status;
    }
    userStackLimit_ = exhausted ? next : userStackGuard_;
    reinterpret_cast<um::Teb64*>(userTeb_)->stackLimit = userStackLimit_;
    userStackGuard_ = exhausted ? 0 : next;
    return exhausted ? NtStatus::StackOverflow : NtStatus::Success;
}

NtStatus EThread::prepareUserException(uint32_t exceptionCode, uint32_t exceptionVector,
                                       uintptr_t address,
                                       uint64_t* hardwareFrame, uint64_t* savedRegisters,
                                       const void* floatingState, const void* capturedRecords,
                                       size_t capturedRecordCount, uintptr_t deliveryStackTop,
                                       const hal::amd64::UserSegmentState* savedSegments) noexcept {
    ob::detail::HandleTableGuard guard;
    if (!thread_ || ke::KeGetCurrentThread() != thread_.get() || !process_ ||
        thread_->owningProcess != process_ || !hardwareFrame || !savedRegisters ||
        !floatingState || (hardwareFrame[2] & 3) != 3 ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;

    struct ExceptionRecord64 {
        uint32_t code, flags;
        uint64_t nestedRecord, exceptionAddress;
        uint32_t parameterCount, alignment;
        uint64_t information[15];
    };
    static_assert(sizeof(ExceptionRecord64) == 0x98);
    static_assert(sizeof(micant::CONTEXT) == 0x4d0);
    static_assert(sizeof(micant::CONTEXT_EX) == 0x20);
    if ((capturedRecords == nullptr) != (capturedRecordCount == 0) || capturedRecordCount > 16)
        return NtStatus::InvalidParameter;
    const size_t recordCount = capturedRecordCount ? capturedRecordCount : 1;
    const size_t contextOffset = (sizeof(ExceptionRecord64) * recordCount + 15) & ~size_t{15};

    uint32_t xsaveMask{};
    uint32_t extendedSize{512};
#if defined(MICANT_FREESTANDING) && (defined(_M_X64) || defined(__x86_64__))
    xsaveMask = MicantAmd64XsaveMask;
    extendedSize = ke::MicantAmd64ExtendedSize;
#endif
    const bool hasExtendedXstate = (xsaveMask & ~uint32_t{3}) != 0;
    if (hasExtendedXstate && (extendedSize < 576 || extendedSize > 4096))
        return NtStatus::NotSupported;

    const auto* images = process_->nativeImages();
    uint64_t dispatcher{};
    if (!images || !process_->nativeImagesFinalized() ||
        images->resolve("ntdll.dll", "KiUserExceptionDispatcher", 0, dispatcher) != NtStatus::Success ||
        !hal::amd64::bootstrapUserRange(dispatcher, 1, false, true) ||
        !hal::amd64::bootstrapUserRange(userTeb_, sizeof(um::Teb64), true))
        return NtStatus::ProcedureNotFound;

    const uintptr_t stackTop = deliveryStackTop ? deliveryStackTop : static_cast<uintptr_t>(hardwareFrame[4]);
    // NTDLL's frame walker keeps one CONTEXT_EX/XSTATE snapshot while running
    // a language handler. Reserve bounded user stack for that snapshot plus
    // the handler's ABI home space and ordinary dispatcher calls.
    constexpr size_t dispatcherStackReserve = 16 * 1024;
    constexpr size_t xstateAlignmentSlack = 63;
    const size_t xstateLength = hasExtendedXstate ? extendedSize - 512 : 0;
    const size_t xstateOffsetSlack = hasExtendedXstate ? xstateAlignmentSlack : 1;
    const size_t fixedPayload = contextOffset + sizeof(micant::CONTEXT) +
                                sizeof(micant::CONTEXT_EX) + xstateOffsetSlack +
                                dispatcherStackReserve + 64;
    if (xstateLength > SIZE_MAX - fixedPayload) return NtStatus::StackOverflow;
    const size_t payloadSize = fixedPayload + xstateLength;
    if (payloadSize > SIZE_MAX - mm::PageMask4KB) return NtStatus::StackOverflow;
    const size_t guaranteeBytes = (payloadSize + mm::PageMask4KB) & ~mm::PageMask4KB;
    if (!userStack_ || userStackTop_ <= userStackLimit_ ||
        userStackLimit_ < userStack_ ||
        userStackLimit_ > UINTPTR_MAX - guaranteeBytes ||
        userStackTop_ - userStackLimit_ < guaranteeBytes || payloadSize > guaranteeBytes)
        return NtStatus::StackOverflow;
    const bool stackPointerInOwnedStack = stackTop >= userStackLimit_ && stackTop <= userStackTop_;
    uintptr_t frameAddress = stackPointerInOwnedStack && stackTop >= payloadSize
        ? (stackTop - payloadSize) & ~uintptr_t{15} : 0;
    const bool needsGuarantee = exceptionCode == static_cast<uint32_t>(NtStatus::StackOverflow) ||
        !frameAddress || frameAddress < userStackLimit_ ||
        !hal::amd64::bootstrapUserRange(frameAddress, payloadSize, true);
    if (needsGuarantee) {
        // Use the kernel-tracked committed bottom page; user-writable TEB
        // stack bounds and guaranteedStackBytes are advisory only.
        frameAddress = (userStackLimit_ + guaranteeBytes - payloadSize) & ~uintptr_t{15};
        if (frameAddress < userStackLimit_ || frameAddress > UINTPTR_MAX - payloadSize ||
            !stackPointerInOwnedStack || frameAddress + payloadSize > stackTop ||
            !hal::amd64::bootstrapUserRange(frameAddress, payloadSize, true))
            return NtStatus::StackOverflow;
    }

    auto* record = reinterpret_cast<ExceptionRecord64*>(frameAddress);
    auto* context = reinterpret_cast<micant::CONTEXT*>(frameAddress + contextOffset);
    auto* contextEx = reinterpret_cast<micant::CONTEXT_EX*>(
        reinterpret_cast<uintptr_t>(context) + sizeof(*context));
    *record = {};
    *contextEx = {};
    record->code = exceptionCode;
    record->exceptionAddress = exceptionCode == static_cast<uint32_t>(NtStatus::Breakpoint) &&
        hardwareFrame[1] ? hardwareFrame[1] - 1 : hardwareFrame[1];
    if (exceptionVector == 14 &&
        (exceptionCode == static_cast<uint32_t>(NtStatus::AccessViolation) ||
         exceptionCode == static_cast<uint32_t>(NtStatus::GuardPageViolation) ||
         exceptionCode == static_cast<uint32_t>(NtStatus::StackOverflow))) {
        // Only #PF supplies the architectural access type and faulting data
        // address. Other exceptions with STATUS_ACCESS_VIOLATION (for example
        // #GP/#SS) carry selector/control error codes in hardwareFrame[0].
        record->parameterCount = 2;
        record->information[0] = (hardwareFrame[0] & 16) ? 8 :
                                 (hardwareFrame[0] & 2) ? 1 : 0;
        record->information[1] = address;
    }
    if (capturedRecordCount) {
        std::memcpy(record, capturedRecords, capturedRecordCount * sizeof(ExceptionRecord64));
        for (size_t i = 0; i < capturedRecordCount; ++i)
            record[i].nestedRecord = i + 1 < capturedRecordCount
                ? reinterpret_cast<uintptr_t>(&record[i + 1]) : 0;
    }

    *context = {};
    // The interrupt wrapper preserves the architecturally enabled x87/SSE
    // state in an XSAVE area. CONTEXT contains the legacy x87/SSE component;
    // the upper AVX/AVX-512 components use the separate Windows XSTATE APIs.
    // Do not advertise debug-register state here: the trap frame does not
    // capture DR0-DR7 and restoring zeroes would silently change it.
    context->ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS |
        (hasExtendedXstate ? CONTEXT_XSTATE : 0);
    context->SegCs = static_cast<uint16_t>(hardwareFrame[2]);
    context->SegSs = static_cast<uint16_t>(hardwareFrame[5]);
    __asm__ __volatile__("movw %%ds, %0" : "=rm"(context->SegDs));
    __asm__ __volatile__("movw %%es, %0" : "=rm"(context->SegEs));
    __asm__ __volatile__("movw %%fs, %0" : "=rm"(context->SegFs));
    __asm__ __volatile__("movw %%gs, %0" : "=rm"(context->SegGs));
    if (savedSegments) {
        context->SegDs = savedSegments->ds; context->SegEs = savedSegments->es;
        context->SegFs = savedSegments->fs; context->SegGs = savedSegments->gs;
    }
    context->EFlags = static_cast<uint32_t>(hardwareFrame[3]);
    context->Rip = hardwareFrame[1];
    context->Rsp = hardwareFrame[4];
    context->R15 = savedRegisters[0]; context->R14 = savedRegisters[1];
    context->R13 = savedRegisters[2]; context->R12 = savedRegisters[3];
    context->R11 = savedRegisters[4]; context->R10 = savedRegisters[5];
    context->R9 = savedRegisters[6];  context->R8 = savedRegisters[7];
    context->Rdi = savedRegisters[8]; context->Rsi = savedRegisters[9];
    context->Rbp = savedRegisters[10]; context->Rbx = savedRegisters[11];
    context->Rdx = savedRegisters[12]; context->Rcx = savedRegisters[13];
    context->Rax = savedRegisters[14];
    std::memcpy(&context->FltSave, floatingState, sizeof(context->FltSave));
    context->MxCsr = context->FltSave.MxCsr;

    contextEx->All.Offset = -static_cast<int32_t>(sizeof(*context));
    contextEx->All.Length = static_cast<uint32_t>(sizeof(*context) + sizeof(*contextEx));
    contextEx->Legacy.Offset = -static_cast<int32_t>(sizeof(*context));
    contextEx->Legacy.Length = sizeof(*context);
    if (hasExtendedXstate) {
        const uintptr_t contextExAddress = reinterpret_cast<uintptr_t>(contextEx);
        const uintptr_t xstateAddress =
            (contextExAddress + sizeof(*contextEx) + 63) & ~uintptr_t{63};
        const size_t xstateOffset = xstateAddress - contextExAddress;
        contextEx->XState.Offset = static_cast<int32_t>(xstateOffset);
        contextEx->XState.Length = static_cast<uint32_t>(xstateLength);
        contextEx->All.Length = static_cast<uint32_t>(sizeof(*context) + xstateOffset + xstateLength);
        std::memcpy(reinterpret_cast<void*>(xstateAddress),
                    static_cast<const uint8_t*>(floatingState) + 512, xstateLength);
    } else {
        // Public CONTEXT_EX convention for a context without an XState chunk.
        contextEx->XState.Offset = static_cast<int32_t>(sizeof(*contextEx) + 1);
    }

    // KiUserExceptionDispatcher(Record, Context) receives the same two
    // pointers as RCX/RDX and a Windows AMD64 entry stack alignment.
    hardwareFrame[1] = dispatcher;
    hardwareFrame[3] = (hardwareFrame[3] & ~uint64_t{0x3000}) | 2;
    // The payload includes a variable XSTATE tail and alignment slack, so its
    // size itself need not be a multiple of 16. Enter NTDLL with the AMD64
    // ABI's RSP % 16 == 8, independently of the enabled XSTATE features.
    hardwareFrame[4] = ((frameAddress + payloadSize) & ~uintptr_t{15}) - 8;
    savedRegisters[12] = reinterpret_cast<uintptr_t>(context);
    savedRegisters[13] = reinterpret_cast<uintptr_t>(record);
    savedRegisters[14] = 0;
    return NtStatus::Success;
}

extern "C" uint32_t MicantPsResolveUserStackFault(uintptr_t address, uint64_t errorCode,
                                                   uint64_t savedCs) noexcept {
    // A stack guard is a non-present, user data access. Reserved-bit,
    // instruction-fetch and supervisor faults belong to the normal trap path.
    if ((savedCs & 3) != 3 || (errorCode & ~uint64_t{6}) || !(errorCode & 4))
        return static_cast<uint32_t>(NtStatus::AccessViolation);
    auto* thread = ke::KeGetCurrentThread();
    return static_cast<uint32_t>(thread && thread->owningThread
        ? thread->owningThread->resolveUserStackGuard(address) : NtStatus::AccessViolation);
}

extern "C" uint32_t MicantPsResolveUserPageFault(uintptr_t address,
                                                   uint64_t errorCode) noexcept {
    auto* thread = ke::KeGetCurrentThread();
    auto* process = ke::KeGetCurrentProcess();
    if (!thread || !process || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return static_cast<uint32_t>(NtStatus::AccessViolation);
    return static_cast<uint32_t>(process->getAddressSpace().handlePageFault(
        address, (errorCode & 0x02u) != 0, (errorCode & 0x04u) != 0,
        (errorCode & 0x10u) != 0));
}

extern "C" bool MicantPsPrepareUserException(uint32_t exceptionCode,
                                              uint32_t exceptionVector,
                                              uintptr_t address,
                                              uint64_t* hardwareFrame,
                                              uint64_t* savedRegisters,
                                              const void* floatingState) noexcept {
    auto* current = ke::KeGetCurrentThread();
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_TASKMGR)
    MicantNetbootSerialWrite("[MicaNT TaskmgrTrap] pid=");
    MicantNetbootSerialWriteNumber(current && current->owningProcess
        ? static_cast<uint64_t>(current->owningProcess->processId()) : 0);
    MicantNetbootSerialWrite(" code=");
    MicantNetbootSerialWriteNumber(exceptionCode);
    MicantNetbootSerialWrite(" vector=");
    MicantNetbootSerialWriteNumber(exceptionVector);
    MicantNetbootSerialWrite(" address=");
    MicantNetbootSerialWriteNumber(address);
    MicantNetbootSerialWrite(" rip=");
    MicantNetbootSerialWriteNumber(hardwareFrame ? hardwareFrame[1] : 0);
    MicantNetbootSerialWrite(" rsp=");
    MicantNetbootSerialWriteNumber(hardwareFrame ? hardwareFrame[4] : 0);
    MicantNetbootSerialWrite("\r\n");
#endif
    return current && current->owningThread &&
        current->owningThread->prepareUserException(exceptionCode, exceptionVector, address,
                                                     hardwareFrame, savedRegisters,
                                                     floatingState) == NtStatus::Success;
}

extern "C" bool MicantPsHandleUserStackFault(uintptr_t address, uint64_t errorCode,
                                              uint64_t savedCs) noexcept {
    return MicantPsResolveUserStackFault(address, errorCode, savedCs) ==
        static_cast<uint32_t>(NtStatus::Success);
}

void EThread::startUser(void* argument) noexcept {
    auto* self = static_cast<EThread*>(argument);
    if (!self || !self->userEntry_ || !self->userStackPointer_) __builtin_trap();
    if (self->userStartupCallbacks_) {
        if (!self->process_ || self->process_->imageStartupCallbacksStarted_) {
            self->userEntry_ = self->userStartupEntry_;
            self->userStartRoutine_ = self->userStartArgument_ = 0;
            self->userStartupCallbacks_ = false;
        } else {
            self->process_->imageStartupCallbacksStarted_ = true;
        }
    }
    hal::amd64::enterBootstrapUserThread(self->userEntry_, self->userStackPointer_,
        self->userInterruptible_, self->userStartRoutine_, self->userStartArgument_);
}

NtStatus EThread::releaseUserMemory() noexcept {
    if (!userStack_ && !userTeb_ && !userTls_ && !userStartupThunk_) return NtStatus::Success;
    uint64_t flags{};
    uintptr_t previous{};
    __asm__ __volatile__("pushfq; popq %0; cli; movq %%cr3, %1"
                         : "=r"(flags), "=r"(previous) : : "memory");
    const uintptr_t target = process_->getDirectoryTableBase();
    if (previous != target && !hal::amd64::switchProcessPagingRootByPhysical(target)) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::Unsuccessful;
    }
    auto& space = process_->getAddressSpace();
    NtStatus status = NtStatus::Success;
    for (auto* address : {&userStartupThunk_, &userTls_, &userTeb_, &userStack_}) {
        if (*address && (status = space.free(*address, 0, mm::MEM_RELEASE)) != NtStatus::Success) break;
        *address = 0;
    }
    if (previous != target && !hal::amd64::switchProcessPagingRootByPhysical(previous)) __builtin_trap();
    if (status == NtStatus::Success) {
        userEntry_ = userStackPointer_ = 0;
        userStartupEntry_ = 0;
        userStartupCallbacks_ = false;
        userStackGuard_ = 0;
        userStackLimit_ = userStackTop_ = 0;
        userStartRoutine_ = userStartArgument_ = 0;
        userInterruptible_ = false;
        thread_->tebAddress = 0;
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return status;
}

NtStatus EThread::cancelBeforeStart() noexcept {
    // Serialize queue inspection and the terminal transition with APC
    // insertion and scheduler operations on the bootstrap processor.
    ob::detail::HandleTableGuard guard;
    if (reapInProgress_ || !thread_ || started_ || thread_->terminationRequested || thread_.get() == ke::KeGetCurrentThread())
        return NtStatus::InvalidParameter;
    if (!thread_->kernelApcList.empty() || !thread_->userApcList.empty()) {
        // A queued APC belongs to the target. Run the bootstrap entry solely
        // for target-context rundown, rather than discarding it on the caller.
        if (thread_->kernelApcDisable || thread_->specialApcDisable || thread_->normalKernelApcActive || thread_->pendingNormalCount)
            return NtStatus::NotImplemented;
        if (thread_->state != ke::ThreadState::Initialized &&
            !(thread_->state == ke::ThreadState::Ready && thread_->readyQueued))
            return NtStatus::InvalidParameter;
        thread_->terminationRequested = true;
        if (thread_->state == ke::ThreadState::Initialized &&
            !ke::PriorityScheduler::get().readyKThread(thread_.get())) {
            thread_->terminationRequested = false;
            return NtStatus::Unsuccessful;
        }
        return NtStatus::Success;
    }
    if (thread_->state == ke::ThreadState::Ready) {
        if (!ke::PriorityScheduler::get().removeReadyThread(thread_.get()))
            return NtStatus::InvalidParameter;
    } else if (thread_->state != ke::ThreadState::Initialized || thread_->readyQueued) {
        return NtStatus::InvalidParameter;
    }
    exitStatus_ = NtStatus::Success;
    thread_->state = ke::ThreadState::Terminated;
    signalTermination(this);
    return NtStatus::Success;
}

NtStatus EThread::rollbackBeforeStart() noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    // The syscall owns BSP exclusion through the subsequent reap. Refuse a
    // caller that cannot provide that lifetime and scheduling guarantee.
    if (flags & (uint64_t{1} << 9)) return NtStatus::InvalidParameter;
    {
        ob::detail::HandleTableGuard guard;
        if (!thread_ || started_ || reapInProgress_ || thread_->terminationRequested ||
            !thread_->kernelApcList.empty() || !thread_->userApcList.empty())
            return NtStatus::InvalidParameter;
        if (thread_->state == ke::ThreadState::Ready) {
            if (!ke::PriorityScheduler::get().removeReadyThread(thread_.get()))
                return NtStatus::InvalidParameter;
            thread_->state = ke::ThreadState::Initialized;
        } else if (thread_->state != ke::ThreadState::Initialized || thread_->readyQueued)
            return NtStatus::InvalidParameter;
    }
    return reap();
}

void EThread::deferFailedCreationReap() noexcept {
    ob::detail::HandleTableGuard guard;
    if (!objectHeader_ || !thread_ || started_ || reapInProgress_ || thread_->readyQueued ||
        thread_->state != ke::ThreadState::Initialized ||
        !thread_->kernelApcList.empty() || !thread_->userApcList.empty())
        __builtin_trap();
    thread_->apcQueueable = false;
    thread_->state = ke::ThreadState::Terminated;
    detail::ThreadReaper::enqueue(*this);
    ke::g_CurrentCpu.serviceDeferredCleanup = detail::ThreadReaper::service;
}

NtStatus EThread::terminateOther(NtStatus status) noexcept {
    ob::detail::HandleTableGuard guard;
    if (!thread_) return objectOwned_ ? NtStatus::Success : NtStatus::InvalidParameter;
    if (thread_->state == ke::ThreadState::Terminated || thread_->terminationRequested)
        return NtStatus::Success;
    if (!process_ || reapInProgress_ || thread_.get() == ke::KeGetCurrentThread() ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    if (!started_ && (thread_->kernelApcDisable || thread_->specialApcDisable ||
                      thread_->normalKernelApcActive)) return NtStatus::NotImplemented;
    const bool cancellableWait = thread_->state == ke::ThreadState::Waiting &&
        thread_->eventWaitUserMode && canTerminateEventWait(*thread_);
    // A system thread has no general safe termination boundary. A user-mode
    // wait can be interrupted; a user thread in a kernel wait must instead
    // complete that wait and release its resources before servicing the
    // termination request on its normal return path.
    if (started_ && !userEntry_ && !cancellableWait) return NtStatus::NotImplemented;
    if (thread_->state == ke::ThreadState::Waiting) {
        if (cancellableWait) {
            auto* event = static_cast<sync::detail::BootstrapEvent*>(thread_->eventWaitObject);
            if (!event->cancelWaitForTermination(*thread_)) return NtStatus::Unsuccessful;
        }
    } else if (thread_->state == ke::ThreadState::Initialized && !thread_->readyQueued) {
        if (!ke::PriorityScheduler::get().readyKThread(thread_.get())) return NtStatus::Unsuccessful;
    } else if (thread_->state != ke::ThreadState::Ready || !thread_->readyQueued) {
        return NtStatus::NotImplemented;
    }
    requestedExitStatus_ = status;
    thread_->terminationRequested = true;
    return NtStatus::Success;
}

void EThread::signalTermination(void* argument) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    auto* self = static_cast<EThread*>(argument);
    if (self->process_) self->process_->onThreadTermination(self->exitStatus_);
    (void)self->terminationEvent_.set();
    detail::ThreadReaper::enqueue(*self);
#else
    (void)argument;
#endif
}

void EThread::serviceTermination(void* argument) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    auto* self = static_cast<EThread*>(argument);
    auto* current = ke::KeGetCurrentThread();
    if (!self || current != self->thread_.get() || !self->process_ ||
        !current->terminationRequested || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return;
    // System threads start in a critical region to suppress ordinary APCs.
    // The termination callback runs at a safe wait-return boundary, so leave
    // that owned region before exitCurrent checks APC-disable state.
    const bool leaveInitialRegion = current->systemInitialRegionActive;
    if (leaveInitialRegion) ke::KeLeaveCriticalRegion();
    const auto result = self->exitCurrent(self->requestedExitStatus_);
    if (leaveInitialRegion && result != NtStatus::Success &&
        current->state == ke::ThreadState::Running && !current->systemInitialRegionActive) {
        ke::KeEnterCriticalRegion();
        current->systemInitialRegionActive = true;
    }
#else
    (void)argument;
#endif
}

void EThread::startThread(void* argument) noexcept {
    auto* self = static_cast<EThread*>(argument);
    if (!self || !self->thread_ || ke::KeGetCurrentThread() != self->thread_.get()) __builtin_trap();
    self->started_ = true;
    ke::KeLowerIrql(ke::PASSIVE_LEVEL);
    if (self->systemStart_ && !self->thread_->terminationRequested) {
        ke::KeEnterCriticalRegion();
        self->thread_->systemInitialRegionActive = true;
    }
    // The runtime clock owns IRQ routing before new threads enable IF.
    // Early bootstrap probes retain their original interrupt-disabled mode.
    if (hal::amd64::bootstrapSchedulerClockActive()) __asm__ __volatile__("sti" : : : "memory");
    // A newly scheduled target may already have kernel APCs queued.
    ke::KiDeliverApc(0);
    // A cancelled, never-started thread enters only to run target-owned APC
    // cleanup; its original start routine must not execute.
    if (!self->thread_->terminationRequested) self->start_(self->argument_);
    if (self->thread_->systemInitialRegionActive) ke::KeLeaveCriticalRegion();
    const auto status = self->thread_->terminationRequested
        ? self->requestedExitStatus_ : NtStatus::Success;
    (void)self->exitCurrent(status);
    __builtin_trap(); // A terminated thread may never resume.
}

NtStatus EThread::exitCurrent(NtStatus status) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* current = ke::KeGetCurrentThread();
    CMD_PS_TRACE("[MicaNT PsTrace] exitCurrent entered\r\n");
    CMD_PS_VALIDATE_HID("EThread exitCurrent entry");
    if (!thread_ || current != thread_.get() || current->state != ke::ThreadState::Running ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL || current->kernelApcDisable ||
        current->specialApcDisable || current->normalKernelApcActive ||
        current->eventWaitInProgress || current->eventWaitObject) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::InvalidParameter;
    }
    const bool alreadyActive = current->terminationActive;
    const bool previouslyQueueable = current->apcQueueable;
    current->terminationActive = true;
    // Do not remove a runnable successor while APC callbacks run: those
    // callbacks may themselves wait for that thread.
    if (!ke::PriorityScheduler::get().getReadyThreadCount() && !hasUsableIdleThread(current)) {
        current->terminationActive = alreadyActive;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::Unsuccessful;
    }
    // Freeze insertion under BSP exclusion before callbacks can be
    // preempted; pending kernel APCs still drain in the target context.
    current->apcQueueable = false;
    bool rundown{};
    {
        // Self-termination can arrive directly from SYSCALL, whose active GS
        // addresses the syscall CPU record. Cancellation and APC callbacks
        // must instead run with the ordinary kernel thread GS and segments,
        // including while interrupts are enabled and another thread runs.
        ke::detail::ReturnKernelContext kernelContext;
        if ((flags & (uint64_t{1} << 9)) || hal::amd64::bootstrapSchedulerClockActive())
            __asm__ __volatile__("sti" : : : "memory");
        // Io pins each request and its owners until cancellation completes.
        io::IoCancelThreadReads(*this);
        CMD_PS_TRACE("[MicaNT PsTrace] thread read cancellation returned\r\n");
        CMD_PS_VALIDATE_HID("EThread after IoCancelThreadReads");
        rundown = current->apcRundownComplete || ke::detail::rundownCurrentApcs();
        CMD_PS_TRACE("[MicaNT PsTrace] APC rundown returned\r\n");
        CMD_PS_VALIDATE_HID("EThread after APC rundown");
        __asm__ __volatile__("cli" : : : "memory");
    }
    if (!rundown) {
        // No rundown callback ran if the preconditions were not met. In
        // particular, do not leave a live thread with its APC queue frozen.
        current->apcQueueable = previouslyQueueable;
        current->terminationActive = alreadyActive;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::NotImplemented;
    }
    ke::KfRaiseIrql(ke::DISPATCH_LEVEL);
    auto* next = ke::PriorityScheduler::get().selectNextThread();
    CMD_PS_TRACE("[MicaNT PsTrace] successor selection returned\r\n");
    CMD_PS_VALIDATE_HID("EThread after successor selection");
    if (next) {
        CMD_PS_TRACE_VALUE("[MicaNT PsTrace] next thread=", reinterpret_cast<uintptr_t>(next));
        CMD_PS_TRACE_VALUE("[MicaNT PsTrace] next rip=", next->context.rip);
        CMD_PS_TRACE_VALUE("[MicaNT PsTrace] next rsp=", next->context.rsp);
        CMD_PS_TRACE_VALUE("[MicaNT PsTrace] next ext=", next->context.extendedState);
        CMD_PS_TRACE_VALUE("[MicaNT PsTrace] next stack top=", next->kernelStackTop);
        CMD_PS_TRACE_VALUE("[MicaNT PsTrace] next exec top=", next->kernelExecutionStackTop);
    }
    if (next && current->kernelApcList.empty() && current->userApcList.empty() &&
        !current->queuedApcReferences && !current->pendingNormalCount &&
        !current->normalKernelApcActive && !current->apcRundownActive) {
        exitStatus_ = status;
        current->state = ke::ThreadState::Terminated;
        CMD_PS_TRACE("[MicaNT PsTrace] switching away from terminating thread\r\n");
        const bool switched = ke::KiSwapContext(current, next);
        CMD_PS_TRACE("[MicaNT PsTrace] terminating thread resumed after switch\r\n");
        CMD_PS_VALIDATE_HID("EThread resumed after context switch");
        if (switched) __builtin_trap();
    }
    // Rundown is irreversible: callbacks may have freed APC storage. A
    // missing successor or rejected context switch is a scheduler invariant
    // failure, not a recoverable exit that can return to the live thread.
    __builtin_trap();
#else
    (void)status;
    return NtStatus::NotImplemented;
#endif
}

NtStatus EThread::reap() noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (reapInProgress_ || !thread_ || !process_ || thread_->readyQueued ||
        thread_.get() == ke::KeGetCurrentThread() ||
        (thread_->state != ke::ThreadState::Initialized && thread_->state != ke::ThreadState::Terminated)) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::InvalidParameter;
    }
    if (!thread_->kernelApcList.empty() || !thread_->userApcList.empty() ||
        thread_->queuedApcReferences ||
        thread_->pendingNormalCount || thread_->normalKernelApcActive || thread_->apcRundownActive) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::NotImplemented;
    }
    const bool wasApcQueueable = thread_->apcQueueable;
    thread_->apcQueueable = false;
    reapInProgress_ = true;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    const auto fail = [this, wasApcQueueable](NtStatus status) noexcept {
        uint64_t localFlags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(localFlags) : : "memory");
        reapInProgress_ = false;
        if (thread_) thread_->apcQueueable = wasApcQueueable;
        if (localFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    };
    if (!processDetached_) {
        const auto cleanup = releaseUserMemory();
        if (cleanup != NtStatus::Success) return fail(cleanup);
        uint64_t detachFlags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(detachFlags) : : "memory");
        // Hold an extra reference across detach so process deletion and its
        // callbacks never run inside the lifecycle exclusion.
        auto* processReference = process_->objectHeader_;
        if (processReference) processReference->addReference();
        if (processReference && process_->isTerminating() && process_->attachedThreads_ == 1)
            reapProcessReference_ = processReference;
        const auto status = process_->detachThread(*thread_);
        if (status == NtStatus::Success) processDetached_ = true;
        if (status == NtStatus::Success && processReference && !reapProcessReference_) process_ = nullptr;
        if (detachFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        if (status != NtStatus::Success) {
            reapProcessReference_ = nullptr;
            if (processReference) ob::ObDereferenceObject(processReference);
            return fail(status);
        }
        if (processReference && !reapProcessReference_) {
            if (objectOwned_) retiredProcessReference_ = processReference;
            else ob::ObDereferenceObject(processReference);
        }
    }
    if (reapProcessReference_) {
        const auto status = process_->closeAddressSpace();
        if (status != NtStatus::Success) return fail(status);
        auto* reference = reapProcessReference_;
        reapProcessReference_ = nullptr;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        (void)process_->terminationEvent().set();
#endif
        process_ = nullptr;
        if (objectOwned_) retiredProcessReference_ = reference;
        else ob::ObDereferenceObject(reference);
    }
    uint64_t finalFlags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(finalFlags) : : "memory");
    detail::ThreadReaper::remove(*this);
    // An Ob-backed ETHREAD remains openable by CID while handles or other
    // references keep it alive, so its stopped KThread must stay resident
    // (unregistered at EThread destruction) until the object is deleted.
    // Bootstrap EThreads have no such Ob lifetime: nothing else can reference
    // the KThread after reap, so destroy it immediately and return its PFNs.
    if (!objectOwned_) {
        ke::detail::unregisterClientThread(*thread_);
        thread_.reset();
    } else {
        // KAPCs initialized for this exact KThread may be temporarily unqueued
        // while their caller still owns an EThread reference.
        thread_->state = ke::ThreadState::Terminated;
        thread_->retiredNext = retiredThreads_;
        retiredThreads_ = thread_.release();
    }
    stack_.reset();
    process_ = nullptr;
    start_ = nullptr;
    argument_ = nullptr;
    systemStart_ = nullptr;
    systemArgument_ = nullptr;
    started_ = false;
    processDetached_ = false;
    reapInProgress_ = false;
    // The last handle may already be closed. Do not touch this object after
    // releasing the lifecycle reference, since that may destroy it.
    // Keep the header as a weak pointer for CID lookup. tryAddReference()
    // refuses resurrection after the last reference is released.
    auto* owner = objectHeader_;
    if (finalFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (owner) ob::ObDereferenceObject(owner);
    return NtStatus::Success;
}

}

namespace micant::sys {
NtStatus NtContinue(const void* contextAddress, bool testAlert, void* trapFrameAddress,
                    void* floatingStateAddress, size_t floatingStateSize,
                    uint32_t xsaveMask, bool forExceptionRaise,
                    hal::amd64::UserSegmentState* userSegments) {
    using hal::amd64::SyscallFrame;
    (void)forExceptionRaise;
    auto* current = ke::KeGetCurrentThread();
    uintptr_t currentRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(currentRoot));
    if (!contextAddress || !trapFrameAddress || !floatingStateAddress || !current ||
        !current->previousUserMode || !current->owningThread || !current->owningProcess ||
        current->owningThread->kernelThread() != current ||
        current->apcStateIndex != ke::ApcStateIndexOriginal || current->attachedProcess ||
        currentRoot != current->owningProcess->getDirectoryTableBase() ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;

    auto* frame = static_cast<SyscallFrame*>(trapFrameAddress);
    if ((frame->cs & 3) != 3 || frame->cs != 0x33 || frame->ss != 0x2b)
        return NtStatus::InvalidParameter;
    if (floatingStateSize < 512 || floatingStateSize > 4096)
        return NtStatus::NotSupported;
    // KiContinue probes the user CONTEXT with ULONG alignment before copying
    // it. Keep misalignment distinct from an inaccessible user range.
    if (reinterpret_cast<uintptr_t>(contextAddress) & (alignof(uint32_t) - 1))
        return NtStatus::DatatypeMisalignment;

    constexpr uint32_t ContextAmd64 = 0x00100000;
    constexpr uint32_t ContextGroupMask = 0x0000ffff;
    constexpr uint32_t ContextAllowedGroups = 0x0000005f;
    alignas(64) micant::CONTEXT captured{};
    auto copyStatus = ex::ExSafeCopyFromUser(&captured, contextAddress,
                                              sizeof(captured), mm::UserMode);
    if (copyStatus != NtStatus::Success) return copyStatus;
    const uint32_t groups = captured.ContextFlags & ContextGroupMask;
    // The architecture bit identifies the CONTEXT layout; it is not an
    // exclusive tag in the upper half of ContextFlags. NT also carries
    // exception/service state bits there, and KeContextToTrapFrame ignores
    // those metadata bits while selecting the low-order register groups.
    if (!(captured.ContextFlags & ContextAmd64) ||
        (groups & ~ContextAllowedGroups))
        return NtStatus::InvalidParameter;
    if (groups & CONTEXT_DEBUG_REGISTERS) return NtStatus::NotSupported;

    if (groups & CONTEXT_CONTROL) {
        // KeContextToTrapFrame selects CS/SS with CONTROL and sanitizes
        // privileged RFLAGS bits rather than trusting them.
        // IRET must receive canonical user control state, but neither the
        // target instruction nor the stack page has to be mapped yet. A
        // later access to an absent or protected page faults in user mode.
        if (captured.SegCs != 0x33 || captured.SegSs != 0x2b ||
            captured.Rip > um::MM_HIGHEST_USER_ADDRESS || captured.Rsp > um::MM_HIGHEST_USER_ADDRESS)
            return NtStatus::InvalidParameter;
    }

    if (groups & CONTEXT_SEGMENTS) {
        // Only null or present DPL3, readable GDT selectors can be loaded
        // in kernel mode without #GP. LDT and privileged selectors are not
        // part of this AMD64 GDT and must never reach MOV Sreg in ring 0.
        const auto valid = [](uint16_t selector) noexcept {
            const auto index = static_cast<uint16_t>(selector & ~uint16_t{3});
            return index == 0 || index == 0x20 || index == 0x28 || index == 0x30;
        };
        if (!valid(captured.SegDs) || !valid(captured.SegEs) ||
            !valid(captured.SegFs) || !valid(captured.SegGs))
            return NtStatus::InvalidParameter;
        if (!userSegments) {
            uint16_t ds{}, es{}, fs{}, gs{};
            __asm__ __volatile__("movw %%ds, %0" : "=rm"(ds));
            __asm__ __volatile__("movw %%es, %0" : "=rm"(es));
            __asm__ __volatile__("movw %%fs, %0" : "=rm"(fs));
            __asm__ __volatile__("movw %%gs, %0" : "=rm"(gs));
            if (captured.SegDs != ds || captured.SegEs != es ||
                captured.SegFs != fs || captured.SegGs != gs) return NtStatus::NotSupported;
        }
    }

    alignas(64) uint8_t stagedXstate[4096]{};
    std::memcpy(stagedXstate, floatingStateAddress, floatingStateSize);
    uint32_t mxcsrMask{};
    std::memcpy(&mxcsrMask, stagedXstate + offsetof(micant::XMM_SAVE_AREA32, MxCsr_Mask),
                sizeof(mxcsrMask));
    if (!mxcsrMask) mxcsrMask = 0x0000ffbfu;
    if (groups & CONTEXT_FLOATING_POINT) {
        if ((captured.MxCsr & ~mxcsrMask) ||
            (captured.FltSave.ControlWord & ~uint16_t{0x1f7f}))
            return NtStatus::InvalidParameter;
        captured.FltSave.MxCsr = captured.MxCsr;
    }
    if (groups & CONTEXT_XSTATE) {
        if (!(xsaveMask & ~uint32_t{3}) || floatingStateSize < 576)
            return NtStatus::NotSupported;
        const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(contextAddress);
        if (sourceAddress > UINTPTR_MAX - sizeof(captured)) return NtStatus::AccessViolation;
        const uintptr_t contextExAddress = sourceAddress + sizeof(captured);
        micant::CONTEXT_EX contextEx{};
        copyStatus = ex::ExSafeCopyFromUser(&contextEx,
            reinterpret_cast<const void*>(contextExAddress), sizeof(contextEx), mm::UserMode);
        if (copyStatus != NtStatus::Success) return copyStatus;
        const int32_t expectedContextOffset = -static_cast<int32_t>(sizeof(captured));
        const size_t maximumTail = floatingStateSize - 512;
        const size_t xstateLength = contextEx.XState.Length;
        if (contextEx.All.Length < sizeof(captured) + sizeof(contextEx) ||
            contextEx.All.Offset != expectedContextOffset ||
            contextEx.Legacy.Offset != expectedContextOffset ||
            contextEx.Legacy.Length != sizeof(captured) ||
            contextEx.XState.Offset < static_cast<int32_t>(sizeof(contextEx)) ||
            xstateLength < sizeof(micant::XSTATE_HEADER) || xstateLength > maximumTail ||
            static_cast<uint64_t>(contextEx.XState.Offset) + xstateLength >
                contextEx.All.Length - sizeof(captured) ||
            contextExAddress > UINTPTR_MAX - static_cast<uint32_t>(contextEx.XState.Offset))
            return NtStatus::InvalidParameter;
        const uintptr_t xstateAddress = contextExAddress +
            static_cast<uint32_t>(contextEx.XState.Offset);
        if (xstateAddress > UINTPTR_MAX - xstateLength || (xstateAddress & 63) ||
            ex::ExSafeCopyFromUser(stagedXstate + 512,
                reinterpret_cast<const void*>(xstateAddress), xstateLength, mm::UserMode) != NtStatus::Success)
            return NtStatus::AccessViolation;
        micant::XSTATE_HEADER xstateHeader{};
        std::memcpy(&xstateHeader, stagedXstate + 512, sizeof(xstateHeader));
        if (xstateHeader.Mask & ~static_cast<uint64_t>(xsaveMask))
            return NtStatus::InvalidParameter;
        if (xstateHeader.CompactionMask) {
            constexpr uint64_t compactedLayout = uint64_t{1} << 63;
            if (!(xstateHeader.CompactionMask & compactedLayout) ||
                (xstateHeader.CompactionMask &
                    ~(compactedLayout | static_cast<uint64_t>(xsaveMask))) ||
                (xstateHeader.Mask &
                    ~(xstateHeader.CompactionMask & ~compactedLayout)))
                return NtStatus::InvalidParameter;
            // The bootstrap XSAVE buffers and current feature walker use the
            // standard CPUID offsets. A valid compacted context needs the
            // compacted CPUID offsets and remains explicitly unsupported.
            return NtStatus::NotSupported;
        }
        if (std::any_of(std::begin(xstateHeader.Reserved), std::end(xstateHeader.Reserved),
                        [](uint64_t value) { return value != 0; }))
            return NtStatus::InvalidParameter;
        // In the standard, non-compacted XSAVE layout each feature retains its
        // CPUID leaf 0xD subleaf offset. A subset context may end after its
        // highest included feature; every component marked present in
        // XSTATE_BV must still fit wholly inside that bounded prefix.
        for (uint32_t feature = 2; feature < 32; ++feature) {
            const uint32_t featureBit = uint32_t{1} << feature;
            if (!(xstateHeader.Mask & featureBit)) continue;
            uint32_t eax = 0x0d, ebx{}, ecx = feature, edx{};
            __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
            const size_t componentSize = eax;
            const size_t componentOffset = ebx;
            if (!componentSize || componentOffset < 576 ||
                componentOffset > floatingStateSize ||
                componentSize > floatingStateSize - componentOffset ||
                componentOffset - 512 > xstateLength ||
                componentSize > xstateLength - (componentOffset - 512))
                return NtStatus::InvalidParameter;
        }
    }

    // All user data and control targets are validated before the live return
    // frame or XSAVE image is changed. NtContinue success returns through this
    // IRET frame instead of resuming after the syscall instruction.
    SyscallFrame next = *frame;
    if (groups & CONTEXT_INTEGER) {
        next.r15 = captured.R15; next.r14 = captured.R14;
        next.r13 = captured.R13; next.r12 = captured.R12;
        next.r10 = captured.R10; next.r9 = captured.R9;
        next.r8 = captured.R8; next.rdx = captured.Rdx;
        next.rsi = captured.Rsi; next.rdi = captured.Rdi;
        next.rbp = captured.Rbp; next.rbx = captured.Rbx;
        next.rax = captured.Rax; next.rcx = captured.Rcx;
        next.r11 = captured.R11;
    }
    if (groups & CONTEXT_CONTROL) {
        next.rip = captured.Rip; next.rsp = captured.Rsp;
        // KeContextToTrapFrame forces the architectural reserved bit and IF
        // for user continuation; the syscall exit path still masks every
        // user-unsafe flag before IRET.
        next.flags = static_cast<uint64_t>(captured.EFlags) | 0x202;
        next.cs = captured.SegCs;
        next.ss = captured.SegSs;
    }
    if (groups & CONTEXT_FLOATING_POINT) {
        std::memcpy(stagedXstate, &captured.FltSave, sizeof(captured.FltSave));
        std::memcpy(stagedXstate + offsetof(micant::XMM_SAVE_AREA32, MxCsr_Mask),
                    &mxcsrMask, sizeof(mxcsrMask));
    }
    if (groups & (CONTEXT_FLOATING_POINT | CONTEXT_XSTATE))
        std::memcpy(floatingStateAddress, stagedXstate, floatingStateSize);
    *frame = next;
    if ((groups & CONTEXT_SEGMENTS) && userSegments) {
        userSegments->ds = captured.SegDs; userSegments->es = captured.SegEs;
        userSegments->fs = captured.SegFs; userSegments->gs = captured.SegGs;
    }

    // A handled fault inside an APC normally resumes below the interrupted
    // stack pointer; keep subsequent APCs queued in that case. The APC
    // An alerting NtContinue alone does not prove the callback completed: an
    // exception handler inside that callback may request an alertable return
    // while continuing on the same APC stack. Retire an in-flight frame only
    // when CONTROL restores the interrupted stack or unwinds beyond it.
    if (groups & CONTEXT_CONTROL) {
        while (current->userApcInFlightDepth && next.rsp >=
               current->userApcInterruptedRsp[current->userApcInFlightDepth - 1]) {
            current->userApcInterruptedRsp[--current->userApcInFlightDepth] = 0;
        }
        while (current->userRaiseDepth && next.rsp >=
               current->userRaiseStack[current->userRaiseDepth - 1]) {
            current->userRaiseStack[--current->userRaiseDepth] = 0;
        }
    }
    if (testAlert) (void)ke::KeTestAlertThread(ke::UserMode);
    return NtStatus::Success;
}

static NtStatus createThreadEx(Handle* threadHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle processHandle, uintptr_t startRoutine,
    uintptr_t argument, uint32_t flags, uintptr_t zeroBits, size_t stackSize,
    size_t maximumStackSize, void* attributeList, mm::KPROCESSOR_MODE argumentMode,
    mm::KPROCESSOR_MODE securityMode) {
    if (!threadHandle || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    const auto pointerMode = [argumentMode](const void*, size_t) noexcept { return argumentMode; };
    const auto withInterruptsDisabled = [](auto&& operation) noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const auto status = operation();
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    };
    const auto safeCopyIn = [&pointerMode, &withInterruptsDisabled](void* destination, const void* source,
                                           size_t length) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ExSafeCopyFromUser(destination, source, length, pointerMode(source, length));
        });
    };
    const auto safeCopyOut = [&pointerMode, &withInterruptsDisabled](void* destination, const void* source,
                                            size_t length) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ExSafeCopyToUser(destination, source, length, pointerMode(destination, length));
        });
    };
    const auto probeRead = [&pointerMode, &withInterruptsDisabled](const void* address,
        size_t size, size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForRead(address, size, alignment, pointerMode(address, size));
        });
    };
    const auto probeWrite = [&pointerMode, &withInterruptsDisabled](void* address,
        size_t size, size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForWrite(address, size, alignment, pointerMode(address, size));
        });
    };
    struct CapturedAttribute {
        uintptr_t attribute{};
        uintptr_t output{};
        size_t outputSize{};
        size_t* returnLength{};
        size_t returnedSize{};
    };
    std::array<CapturedAttribute, 2> capturedAttributes{};
    size_t attributeCount{};
    Handle capturedThreadHandle{};
    bool threadHandleOutputAttempted{};
    const auto threadHandleProbe = probeWrite(threadHandle, sizeof(*threadHandle), alignof(Handle));
    if (threadHandleProbe != NtStatus::Success) return threadHandleProbe;
    ObjectAttributes capturedObjectAttributes{};
    se::FreestandingSecurityDescriptor capturedSecurity{};
    const se::FreestandingSecurityDescriptor* objectSecurity{};
    if (attributes) {
        const auto attributeProbe = probeRead(attributes, sizeof(capturedObjectAttributes), alignof(uint32_t));
        if (attributeProbe != NtStatus::Success) return attributeProbe;
        const auto copyStatus = safeCopyIn(&capturedObjectAttributes, attributes, sizeof(capturedObjectAttributes));
        if (copyStatus != NtStatus::Success) return copyStatus;
        if (capturedObjectAttributes.length != sizeof(ObjectAttributes)) return NtStatus::InvalidParameter;
        if (capturedObjectAttributes.rootDirectory || capturedObjectAttributes.objectName ||
            capturedObjectAttributes.securityQualityOfService ||
            (capturedObjectAttributes.attributes & ~OBJ_INHERIT)) return NtStatus::NotImplemented;
        if (capturedObjectAttributes.securityDescriptor) {
            const auto securityStatus = se::SeCaptureFreestandingSecurityDescriptor(
                capturedObjectAttributes.securityDescriptor, securityMode, capturedSecurity);
            if (securityStatus != NtStatus::Success) return securityStatus;
            objectSecurity = &capturedSecurity;
        }
    }
    if ((flags & ~uint32_t{1}) || zeroBits) return NtStatus::NotImplemented;
    if (attributeList) {
        constexpr size_t prefix = offsetof(PsAttributeList, attributes);
        size_t totalLength{};
        const auto headerProbe = probeRead(attributeList, sizeof(totalLength), alignof(size_t));
        if (headerProbe != NtStatus::Success) return headerProbe;
        auto copyStatus = safeCopyIn(&totalLength, attributeList, sizeof(totalLength));
        if (copyStatus != NtStatus::Success) return copyStatus;
        if (totalLength < prefix || (totalLength - prefix) % sizeof(PsAttribute) ||
            (totalLength - prefix) / sizeof(PsAttribute) > capturedAttributes.size())
            return NtStatus::InvalidParameter;
        if (reinterpret_cast<uintptr_t>(attributeList) > UINTPTR_MAX - totalLength)
            return NtStatus::AccessViolation;
        const auto fullListProbe = probeRead(attributeList, totalLength, alignof(size_t));
        if (fullListProbe != NtStatus::Success) return fullListProbe;
        attributeCount = (totalLength - prefix) / sizeof(PsAttribute);
        std::array<PsAttribute, 2> entries{};
        bool clientIdSeen{};
        bool tebSeen{};
        if (attributeCount) {
            const auto* entrySource = reinterpret_cast<const uint8_t*>(attributeList) + prefix;
            copyStatus = safeCopyIn(entries.data(), entrySource, attributeCount * sizeof(PsAttribute));
            if (copyStatus != NtStatus::Success) return copyStatus;
        }
        for (size_t index = 0; index < attributeCount; ++index) {
            const auto& item = entries[index];
            if (!item.value) return NtStatus::InvalidParameter;
            auto& captured = capturedAttributes[index];
            captured.attribute = item.attribute;
            captured.output = item.value;
            captured.outputSize = item.size;
            captured.returnLength = item.returnLength;
            if (item.attribute == PS_ATTRIBUTE_CLIENT_ID && item.size == sizeof(ClientId) && !clientIdSeen) {
                captured.returnedSize = sizeof(ClientId);
                clientIdSeen = true;
            } else if (item.attribute == PS_ATTRIBUTE_TEB_ADDRESS && item.size == sizeof(uintptr_t) && !tebSeen) {
                captured.returnedSize = sizeof(uintptr_t);
                tebSeen = true;
            } else if (item.attribute == PS_ATTRIBUTE_CLIENT_ID || item.attribute == PS_ATTRIBUTE_TEB_ADDRESS) {
                return NtStatus::InvalidParameter;
            } else return NtStatus::NotImplemented;
            const auto valueProbe = probeWrite(reinterpret_cast<void*>(item.value), item.size,
                alignof(uintptr_t));
            if (valueProbe != NtStatus::Success) return valueProbe;
            if (item.returnLength) {
                const auto lengthProbe = probeWrite(item.returnLength, sizeof(size_t), alignof(size_t));
                if (lengthProbe != NtStatus::Success) return lengthProbe;
            }
        }
    }
    ps::EProcess* process{};
    ob::ObjectHeader* reference{};
    auto result = processHandle == InvalidHandleValue
        ? ps::detail::referenceCurrentProcess(process, reference)
        : ps::detail::referenceProcessHandle(processHandle, 0x0002, process, reference);
    if (result != NtStatus::Success) return result;
    Handle handle{};
    ps::EThread* thread{};
    ob::ObjectHeader* failedThreadHandle{};
    {
        ob::detail::HandleTableGuard guard;
        uintptr_t previousRoot{};
        __asm__ __volatile__("movq %%cr3, %0" : "=r"(previousRoot));
        bool switched = false;
        if (process->isTerminating()) result = NtStatus::ProcessIsTerminating;
        else if (!hal::amd64::bootstrapSchedulerClockActive() ||
                 !ps::hasUsableIdleThread(ke::KeGetCurrentThread())) result = NtStatus::DeviceNotReady;
        else if (!process->nativeImagesFinalized() || !process->nativeImages()) result = NtStatus::InvalidParameter;
        else if (!hal::amd64::switchProcessPagingRootByPhysical(process->getDirectoryTableBase()))
            result = NtStatus::Unsuccessful;
        else {
            switched = true;
            uint64_t entry{};
            const auto* images = process->nativeImages();
            for (size_t i = 0; i < images->size(); ++i) {
                const auto* module = images->module(i);
                if (!module || !hal::amd64::bootstrapUserRange(module->base,
                        module->view.headers().optionalHeader.sizeOfHeaders, false)) {
                    result = NtStatus::InvalidImageFormat;
                    break;
                }
            }
            const auto* image = images->module(0);
            if (!image) result = NtStatus::InvalidImageFormat;
            else if (result == NtStatus::Success) {
                const auto& optional = image->view.headers().optionalHeader;
                if (!maximumStackSize) maximumStackSize = optional.sizeOfStackReserve;
                if (!stackSize) stackSize = optional.sizeOfStackCommit;
                const size_t roundedReserve = maximumStackSize <= SIZE_MAX - mm::PageMask4KB
                    ? (maximumStackSize + mm::PageMask4KB) & ~mm::PageMask4KB : 0;
                const size_t roundedCommit = stackSize <= SIZE_MAX - mm::PageMask4KB
                    ? (stackSize + mm::PageMask4KB) & ~mm::PageMask4KB : SIZE_MAX;
                if (!stackSize || !maximumStackSize || stackSize > maximumStackSize ||
                    roundedReserve < ps::MinimumUserExceptionStackReserve ||
                    roundedCommit > roundedReserve - mm::PageSize4KB)
                    result = NtStatus::InvalidParameter;
                else result = images->resolve("ntdll.dll", "RtlUserThreadStart", 0, entry);
            }
            // NTDLL must be executable now. The requested user routine is
            // intentionally not probed: it may change before the new thread
            // runs, and a bad address faults in the target user process.
            if (result == NtStatus::Success && !hal::amd64::bootstrapUserRange(entry, 1, false, true))
                result = NtStatus::BadInitialPc;
            if (result == NtStatus::Success)
                result = ps::detail::createUserThreadHandle(*process, entry,
                    ps::UserThreadKernelStackSize,
                    maximumStackSize,
                    true, desiredAccess, handle, thread, stackSize,
                    attributes ? capturedObjectAttributes.attributes : 0, objectSecurity);
            if (result == NtStatus::Success) result = thread->configureUserStart(startRoutine, argument);
            if (result == NtStatus::Success && (flags & 1)) result = thread->suspendInitially();
        }
        if (switched && !hal::amd64::switchProcessPagingRootByPhysical(previousRoot)) __builtin_trap();
        if (result == NtStatus::Success) {
            const ClientId clientId{process->processId(), thread->kernelThread()->tid};
            const uintptr_t teb = thread->userTebAddress();
            for (size_t index = 0; index < attributeCount && result == NtStatus::Success; ++index) {
                auto& captured = capturedAttributes[index];
                const void* value = captured.attribute == PS_ATTRIBUTE_CLIENT_ID
                    ? static_cast<const void*>(&clientId) : static_cast<const void*>(&teb);
                result = safeCopyOut(reinterpret_cast<void*>(captured.output), value, captured.outputSize);
                if (result == NtStatus::Success && captured.returnLength) {
                    captured.returnedSize = captured.outputSize;
                    result = safeCopyOut(captured.returnLength, &captured.returnedSize, sizeof(size_t));
                }
            }
            if (result == NtStatus::Success) {
                capturedThreadHandle = handle;
                threadHandleOutputAttempted = true;
                result = safeCopyOut(threadHandle, &capturedThreadHandle, sizeof(capturedThreadHandle));
            }
        }
        if (result == NtStatus::Success && !(flags & 1)) result = thread->ready();
        if (result != NtStatus::Success && thread) {
            if (threadHandleOutputAttempted) {
                const Handle invalidHandle{};
                (void)safeCopyOut(threadHandle, &invalidHandle, sizeof(invalidHandle));
            }
            auto* failedThread = thread->kernelThread();
            if (!failedThread || failedThread->state != ke::ThreadState::Initialized ||
                failedThread->readyQueued || !failedThread->kernelApcList.empty() ||
                !failedThread->userApcList.empty()) __builtin_trap();
            // This thread was never published or started. Make it ineligible
            // for APC insertion and process termination before releasing BSP
            // exclusion; its lifecycle reference pins it through reap.
            failedThread->apcQueueable = false;
            failedThread->state = ke::ThreadState::Terminated;
            failedThreadHandle = ob::detail::currentHandleTable().takeHandle(handle);
            if (!failedThreadHandle) __builtin_trap();
        }
    }
    if (failedThreadHandle) {
        if (thread->reap() != NtStatus::Success) {
            // A transient Mm or process-cleanup failure must not turn an
            // otherwise recoverable create/copyout failure into a bugcheck.
            // The unpublished thread retains its lifecycle reference until
            // the canonical reaper can retry it at PASSIVE_LEVEL.
            ob::detail::HandleTableGuard guard;
            ps::detail::ThreadReaper::enqueue(*thread);
            ke::g_CurrentCpu.serviceDeferredCleanup = ps::detail::ThreadReaper::service;
        }
        (void)ob::ObFinishClose(failedThreadHandle, handle);
    }
    ob::ObDereferenceObject(reference);
    return result;
}

NtStatus NtCreateThreadEx(Handle* threadHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle processHandle, uintptr_t startRoutine,
    uintptr_t argument, uint32_t flags, uintptr_t zeroBits, size_t stackSize,
    size_t maximumStackSize, void* attributeList) {
    const auto* caller = ke::KeGetCurrentThread();
    const auto mode = caller && caller->previousUserMode ? mm::UserMode : mm::KernelMode;
    return createThreadEx(threadHandle, desiredAccess, attributes, processHandle, startRoutine,
                          argument, flags, zeroBits, stackSize, maximumStackSize, attributeList, mode, mode);
}

NtStatus NtCreateThreadExFromKernel(Handle* threadHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle processHandle, uintptr_t startRoutine,
    uintptr_t argument, uint32_t flags, uintptr_t zeroBits, size_t stackSize,
    size_t maximumStackSize, void* attributeList) {
    return createThreadEx(threadHandle, desiredAccess, attributes, processHandle, startRoutine,
                          argument, flags, zeroBits, stackSize, maximumStackSize, attributeList,
                          mm::KernelMode, mm::UserMode);
}

static NtStatus resumeThread(Handle threadHandle, uint32_t* previousSuspendCount,
                             mm::KPROCESSOR_MODE argumentMode) {
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (previousSuspendCount) {
        const auto probe = ex::ProbeForWrite(previousSuspendCount, sizeof(*previousSuspendCount),
                                             alignof(uint32_t), argumentMode);
        if (probe != NtStatus::Success) {
            if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
            return probe;
        }
    }
    ps::EThread* thread{};
    ob::ObjectHeader* reference{};
    const auto status = threadHandle == static_cast<Handle>(-2)
        ? ps::detail::referenceCurrentThread(thread, reference)
        : ps::detail::referenceThreadHandle(threadHandle, 0x0002, thread, reference);
    if (status != NtStatus::Success) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    }
    uint32_t previous{};
    auto result = thread->resumeInitial(previous);
    if (result == NtStatus::Success && previousSuspendCount)
        result = ex::ExSafeCopyToUser(previousSuspendCount, &previous, sizeof(previous), argumentMode);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    ob::ObDereferenceObject(reference);
    return result;
}

NtStatus NtResumeThread(Handle threadHandle, uint32_t* previousSuspendCount) {
    const auto* caller = ke::KeGetCurrentThread();
    const auto mode = caller && caller->previousUserMode ? mm::UserMode : mm::KernelMode;
    return resumeThread(threadHandle, previousSuspendCount, mode);
}

static NtStatus openClientObject(Handle* output, uint32_t desiredAccess,
    ObjectAttributes* attributes, ClientId* clientId, bool threadObject) {
    if (!output || !attributes || !clientId || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    const auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    ObjectAttributes capturedAttributes{};
    ClientId capturedId{};
    Handle handle{};
    ob::ObjectHeader* failedHandle{};
    NtStatus result{};
    {
        ob::detail::HandleTableGuard guard;
        result = ex::ProbeForWrite(output, sizeof(*output), alignof(Handle), mode);
        if (result == NtStatus::Success)
            result = ex::ProbeForRead(attributes, sizeof(*attributes), alignof(uint32_t), mode);
        if (result == NtStatus::Success)
            result = ex::ExSafeCopyFromUser(&capturedAttributes, attributes, sizeof(capturedAttributes), mode);
        if (result == NtStatus::Success && capturedAttributes.length != sizeof(ObjectAttributes))
            result = NtStatus::InvalidParameter;
        if (result == NtStatus::Success && (capturedAttributes.rootDirectory || capturedAttributes.objectName ||
            capturedAttributes.securityDescriptor || capturedAttributes.securityQualityOfService ||
            (capturedAttributes.attributes & ~OBJ_INHERIT))) result = NtStatus::NotSupported;
        if (result == NtStatus::Success)
            result = ex::ProbeForRead(clientId, sizeof(*clientId), alignof(uint32_t), mode);
        if (result == NtStatus::Success)
            result = ex::ExSafeCopyFromUser(&capturedId, clientId, sizeof(capturedId), mode);
        if (result == NtStatus::Success)
            result = threadObject
                ? ps::detail::openThreadByClientId(capturedId, desiredAccess, capturedAttributes.attributes, handle)
                : ps::detail::openProcessByClientId(capturedId, desiredAccess, capturedAttributes.attributes, handle);
        if (result == NtStatus::Success) {
            result = ex::ExSafeCopyToUser(output, &handle, sizeof(handle), mode);
            if (result != NtStatus::Success)
                failedHandle = ob::detail::currentHandleTable().takeHandle(handle);
        }
    }
    if (failedHandle) (void)ob::ObFinishClose(failedHandle, handle);
    return result;
}

NtStatus NtOpenProcess(Handle* output, uint32_t access, ObjectAttributes* attributes, ClientId* id) {
    return openClientObject(output, access, attributes, id, false);
}
NtStatus NtOpenThread(Handle* output, uint32_t access, ObjectAttributes* attributes, ClientId* id) {
    return openClientObject(output, access, attributes, id, true);
}

static NtStatus createProcessEx(Handle* processHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle parentProcess, uint32_t flags,
    Handle section, Handle debugPort, Handle exceptionPort, bool inJob,
    mm::KPROCESSOR_MODE inputMode, mm::KPROCESSOR_MODE outputMode,
    mm::KPROCESSOR_MODE securityMode, std::u16string_view imagePath = {},
    std::u16string_view commandLine = {}, std::u16string_view currentDirectory = {}) {
    if (!processHandle || !parentProcess || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    const auto withInterruptsDisabled = [](auto&& operation) noexcept {
        uint64_t interruptFlags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(interruptFlags) : : "memory");
        const auto status = operation();
        if (interruptFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    };
    const auto safeCopyIn = [&](void* destination, const void* source, size_t length) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ExSafeCopyFromUser(destination, source, length, inputMode);
        });
    };
    const auto safeCopyOut = [&](void* destination, const void* source, size_t length) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ExSafeCopyToUser(destination, source, length, outputMode);
        });
    };
    const auto probeWrite = [&](void* destination, size_t length, size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForWrite(destination, length, alignment, outputMode);
        });
    };
    const auto probeRead = [&](const void* source, size_t length, size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForRead(source, length, alignment, inputMode);
        });
    };
    const auto outputProbe = probeWrite(processHandle, sizeof(*processHandle), alignof(Handle));
    if (outputProbe != NtStatus::Success) return outputProbe;
    ObjectAttributes capturedAttributes{};
    se::FreestandingSecurityDescriptor capturedSecurity{};
    const se::FreestandingSecurityDescriptor* objectSecurity{};
    if (attributes) {
        const auto attributeProbe = probeRead(attributes, sizeof(capturedAttributes), alignof(uint32_t));
        if (attributeProbe != NtStatus::Success) return attributeProbe;
        const auto captureStatus = safeCopyIn(&capturedAttributes, attributes, sizeof(capturedAttributes));
        if (captureStatus != NtStatus::Success) return captureStatus;
        if (capturedAttributes.length != sizeof(ObjectAttributes)) return NtStatus::InvalidParameter;
        if (capturedAttributes.rootDirectory || capturedAttributes.objectName ||
            capturedAttributes.securityQualityOfService ||
            (capturedAttributes.attributes & ~OBJ_INHERIT)) return NtStatus::NotImplemented;
        if (capturedAttributes.securityDescriptor) {
            const auto securityStatus = se::SeCaptureFreestandingSecurityDescriptor(
                capturedAttributes.securityDescriptor, securityMode, capturedSecurity);
            if (securityStatus != NtStatus::Success) return securityStatus;
            objectSecurity = &capturedSecurity;
        }
        attributes = &capturedAttributes;
    }
    constexpr uint32_t inheritHandles = 0x00000004;
    if ((flags & ~inheritHandles) || debugPort || exceptionPort || inJob) return NtStatus::NotImplemented;
    ps::EProcess* parent{};
    ob::ObjectHeader* parentReference{};
    const auto status = parentProcess == InvalidHandleValue
        ? ps::detail::referenceCurrentProcess(parent, parentReference)
        : ps::detail::referenceProcessHandle(parentProcess, 0x0080, parent, parentReference);
    if (status != NtStatus::Success) return status;
    ob::ObjectHeader* imageSectionReference{};
    section::SectionObject* imageSection{};
    if (section) {
        const auto sectionStatus = referenceProcessImageSection(section, imageSectionReference);
        if (sectionStatus != NtStatus::Success) {
            ob::ObDereferenceObject(parentReference);
            return sectionStatus;
        }
        imageSection = imageSectionReference->as<section::SectionObject>();
    }
    NtStatus result = NtStatus::NotImplemented;
    {
        ob::detail::HandleTableGuard guard;
        if (parent->isTerminating()) result = NtStatus::ProcessIsTerminating;
        else result = NtStatus::Success;
    }

    struct ParentAddressSpacePin {
        ps::EProcess* process{};
        ob::ObjectHeader* reference{};
        uintptr_t root{};
        ~ParentAddressSpacePin() {
            if (reference) process->releaseAddressSpaceAttachReference(reference);
        }
    } parentAddressSpace;
    if (result == NtStatus::Success) {
        parentAddressSpace.process = parent;
        result = parent->referenceAddressSpaceForAttach(parentAddressSpace.root,
                                                          parentAddressSpace.reference);
        if (result != NtStatus::Success)
            result = parent->isTerminating() ? NtStatus::ProcessIsTerminating : result;
        // Pin the parent even with an explicit image: token inheritance and
        // optional handle-table inheritance must not race address-space and
        // handle-table teardown while file-backed image loading can wait.
    }

    ps::EProcess* child{};
    ob::ObjectHeader* childReference{};
    Handle handle{};
    uint32_t grantedAccess{};
    if (result == NtStatus::Success)
        result = ps::detail::createProcessObject(child, childReference, objectSecurity, desiredAccess, &grantedAccess);
    if (result == NtStatus::Success) {
        result = ps::detail::inheritProcessSecurityContext(*child, *parent);
    }
    if (result == NtStatus::Success && imageSection && !imagePath.empty()) {
        result = child->setNativeProcessParameters(imagePath, commandLine, currentDirectory);
    }
    if (result == NtStatus::Success && imageSection) {
        if (!hal::amd64::bootstrapSchedulerClockActive()) {
            // Before scheduling starts there is no attachable execution
            // context. Keep the bootstrap-only path under BSP exclusion; a
            // backing that needs interrupts will return its own failure.
            result = withInterruptsDisabled([&]() noexcept {
                uintptr_t originalRoot{};
                __asm__ __volatile__("movq %%cr3, %0" : "=r"(originalRoot));
                NtStatus imageStatus = NtStatus::Unsuccessful;
                if (hal::amd64::switchProcessPagingRootByPhysical(child->getDirectoryTableBase()))
                    imageStatus = child->initializeMainImageFromSection(*imageSection, imageSectionReference);
                if (!hal::amd64::switchProcessPagingRootByPhysical(originalRoot)) __builtin_trap();
                return imageStatus;
            });
        } else {
            auto* thread = ke::KeGetCurrentThread();
            ke::KAPC_STATE attachState{};
            const size_t previousDepth = thread ? thread->processAttachDepth : 0;
            const uintptr_t originalRoot = thread ? thread->directoryTableBase : 0;
            if (!thread || !originalRoot) {
                result = NtStatus::InvalidDeviceState;
            } else {
                ke::KeStackAttachProcess(child, &attachState);
                if (thread->processAttachDepth != previousDepth + 1) {
                    result = NtStatus::Unsuccessful;
                } else {
                    if (thread->attachedProcess != child ||
                        thread->directoryTableBase != child->getDirectoryTableBase())
                        result = NtStatus::Unsuccessful;
                    else {
                        result = child->initializeMainImageFromSection(*imageSection, imageSectionReference);
                    }
                    ke::KeUnstackDetachProcess(&attachState);
                    if (thread->processAttachDepth != previousDepth ||
                        thread->directoryTableBase != originalRoot) __builtin_trap();
                }
            }
        }
    } else if (result == NtStatus::Success) {
        result = withInterruptsDisabled([&]() noexcept {
            uintptr_t originalRoot{};
            __asm__ __volatile__("movq %%cr3, %0" : "=r"(originalRoot));
            auto cloneStatus = parent->getAddressSpace().clonePrivateTo(child->getAddressSpace(),
                parentAddressSpace.root, child->getDirectoryTableBase(),
                hal::amd64::switchProcessPagingRootByPhysical);
            if (cloneStatus == NtStatus::Success) {
                if (!hal::amd64::switchProcessPagingRootByPhysical(child->getDirectoryTableBase()))
                    cloneStatus = NtStatus::Unsuccessful;
                else {
                    cloneStatus = child->cloneNativeMetadata(*parent);
                }
            }
            if (!hal::amd64::switchProcessPagingRootByPhysical(originalRoot)) __builtin_trap();
            return cloneStatus;
        });
    }
    if (result == NtStatus::Success && (flags & inheritHandles))
        result = child->getHandleTable().inheritHandlesFrom(parent->getHandleTable());
    ob::ObjectHeader* failedHandle{};
    if (result == NtStatus::Success) {
        // Do not expose a handle in the caller's table across a scheduler
        // boundary before its output has been copied. On a failed copy,
        // retire the entry under the same exclusion and invoke close/delete
        // callbacks only after interrupts have been restored.
        result = withInterruptsDisabled([&]() noexcept {
            if (parent->isTerminating()) return NtStatus::ProcessIsTerminating;
            auto publish = ps::detail::publishProcessHandle(*child, childReference, grantedAccess,
                attributes ? attributes->attributes : 0, handle);
            if (publish != NtStatus::Success) return publish;
            publish = safeCopyOut(processHandle, &handle, sizeof(handle));
            if (publish != NtStatus::Success) {
                failedHandle = ob::detail::currentHandleTable().takeHandle(handle);
                if (!failedHandle) __builtin_trap();
            }
            return publish;
        });
    }
    if (failedHandle && ob::ObFinishClose(failedHandle, handle) != NtStatus::Success)
        __builtin_trap();
    if (childReference) ob::ObDereferenceObject(childReference);
    if (imageSectionReference) ob::ObDereferenceObject(imageSectionReference);
    ob::ObDereferenceObject(parentReference);
    return result;
}

NtStatus NtCreateProcessEx(Handle* processHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle parentProcess, uint32_t flags,
    Handle section, Handle debugPort, Handle exceptionPort, bool inJob) {
    const auto* caller = ke::KeGetCurrentThread();
    const auto mode = caller && caller->previousUserMode ? mm::UserMode : mm::KernelMode;
    return createProcessEx(processHandle, desiredAccess, attributes, parentProcess, flags,
                           section, debugPort, exceptionPort, inJob, mode, mode, mode);
}

NtStatus NtCreateProcessExFromKernel(Handle* processHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle parentProcess, uint32_t flags,
    Handle section, Handle debugPort, Handle exceptionPort, bool inJob) {
    return createProcessEx(processHandle, desiredAccess, attributes, parentProcess, flags,
                           section, debugPort, exceptionPort, inJob, mm::KernelMode, mm::KernelMode, mm::KernelMode);
}

NtStatus NtCreateProcessExCaptured(Handle* userProcessHandle, uint32_t desiredAccess,
    ObjectAttributes* capturedAttributes, Handle parentProcess, uint32_t flags,
    Handle section, Handle debugPort, Handle exceptionPort, bool inJob) {
    return createProcessEx(userProcessHandle, desiredAccess, capturedAttributes, parentProcess, flags,
                           section, debugPort, exceptionPort, inJob, mm::KernelMode, mm::UserMode, mm::UserMode);
}

NtStatus NtCreateUserProcess(Handle* userProcessHandle, Handle* userThreadHandle,
    uint32_t processDesiredAccess, uint32_t threadDesiredAccess,
    ObjectAttributes* userProcessAttributes, ObjectAttributes* userThreadAttributes,
    uint32_t processFlags, uint32_t threadFlags, void* userProcessParameters,
    shared::native_process::ProcessCreateInfo64* userCreateInfo,
    PsAttributeList* userAttributeList) {
    using namespace shared::native_process;
    constexpr uint32_t ProcessCreateFlagsInheritHandles = 0x00000004u;
    constexpr uint32_t ThreadCreateFlagsCreateSuspended = 0x00000001u;
    constexpr size_t MaximumPathCharacters = 2048;
    const auto* caller = ke::KeGetCurrentThread();
    if (!caller || !caller->previousUserMode || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidDeviceState;

    const auto withInterruptsDisabled = [](auto&& operation) noexcept {
        uint64_t interruptFlags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(interruptFlags) : : "memory");
        const auto result = operation();
        if (interruptFlags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return result;
    };
    const auto safeCopyIn = [&](void* destination, const void* source, size_t size) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ExSafeCopyFromUser(destination, source, size, mm::UserMode);
        });
    };
    const auto safeCopyOut = [&](void* destination, const void* source, size_t size) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ExSafeCopyToUser(destination, source, size, mm::UserMode);
        });
    };
    const auto probeRead = [&](const void* address, size_t size, size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForRead(address, size, alignment, mm::UserMode);
        });
    };
    const auto probeWrite = [&](void* address, size_t size, size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForWrite(address, size, alignment, mm::UserMode);
        });
    };
    const auto overlaps = [](uintptr_t left, size_t leftSize,
                             uintptr_t right, size_t rightSize) noexcept {
        if (left > UINTPTR_MAX - leftSize || right > UINTPTR_MAX - rightSize) return true;
        return left < right + rightSize && right < left + leftSize;
    };

    if (!userProcessHandle || !userThreadHandle || !userProcessParameters || !userCreateInfo ||
        processDesiredAccess == 0 || threadDesiredAccess == 0 ||
        (processFlags & ~ProcessCreateFlagsInheritHandles) ||
        (threadFlags & ~ThreadCreateFlagsCreateSuspended)) return NtStatus::InvalidParameter;
    if (userProcessHandle == userThreadHandle ||
        overlaps(reinterpret_cast<uintptr_t>(userProcessHandle), sizeof(Handle),
                 reinterpret_cast<uintptr_t>(userThreadHandle), sizeof(Handle)) ||
        overlaps(reinterpret_cast<uintptr_t>(userProcessHandle), sizeof(Handle),
                 reinterpret_cast<uintptr_t>(userCreateInfo), sizeof(*userCreateInfo)) ||
        overlaps(reinterpret_cast<uintptr_t>(userThreadHandle), sizeof(Handle),
                 reinterpret_cast<uintptr_t>(userCreateInfo), sizeof(*userCreateInfo)))
        return NtStatus::InvalidParameter;

    auto status = probeWrite(userProcessHandle, sizeof(Handle), alignof(Handle));
    if (status == NtStatus::Success)
        status = probeWrite(userThreadHandle, sizeof(Handle), alignof(Handle));
    if (status == NtStatus::Success)
        status = probeRead(userCreateInfo, sizeof(*userCreateInfo), alignof(uint64_t));
    if (status == NtStatus::Success)
        status = probeWrite(userCreateInfo, sizeof(*userCreateInfo), alignof(uint64_t));
    if (status != NtStatus::Success) return status;

    ProcessCreateInfo64 createInfo{};
    status = safeCopyIn(&createInfo, userCreateInfo, sizeof(createInfo));
    if (status != NtStatus::Success) return status;
    if (createInfo.Size != sizeof(createInfo) || createInfo.State != CreateState::Initial)
        return NtStatus::InvalidParameter;
    if (createInfo.Data.Initial.InitFlags || createInfo.Data.Initial.AdditionalFileAccess)
        return NtStatus::NotSupported;

    if (userAttributeList) {
        // This implementation supports no process attributes. In particular,
        // callers that need image-name, parent, token, or output ClientId
        // attributes must receive an explicit failure.
        size_t totalLength{};
        status = probeRead(userAttributeList, sizeof(totalLength), alignof(size_t));
        if (status == NtStatus::Success)
            status = safeCopyIn(&totalLength, userAttributeList, sizeof(totalLength));
        if (status != NtStatus::Success) return status;
        return NtStatus::NotSupported;
    }

    ObjectAttributes processAttributes{};
    if (userProcessAttributes) {
        status = probeRead(userProcessAttributes, sizeof(processAttributes), alignof(uint32_t));
        if (status == NtStatus::Success)
            status = safeCopyIn(&processAttributes, userProcessAttributes, sizeof(processAttributes));
        if (status != NtStatus::Success) return status;
        if (processAttributes.length != sizeof(processAttributes) || processAttributes.rootDirectory ||
            processAttributes.objectName || processAttributes.securityDescriptor ||
            processAttributes.securityQualityOfService ||
            (processAttributes.attributes & ~OBJ_INHERIT)) return NtStatus::NotSupported;
    }
    ObjectAttributes threadAttributes{};
    if (userThreadAttributes) {
        status = probeRead(userThreadAttributes, sizeof(threadAttributes), alignof(uint32_t));
        if (status == NtStatus::Success)
            status = safeCopyIn(&threadAttributes, userThreadAttributes, sizeof(threadAttributes));
        if (status != NtStatus::Success) return status;
        if (threadAttributes.length != sizeof(threadAttributes) || threadAttributes.rootDirectory ||
            threadAttributes.objectName || threadAttributes.securityDescriptor ||
            threadAttributes.securityQualityOfService ||
            (threadAttributes.attributes & ~OBJ_INHERIT)) return NtStatus::NotSupported;
    }

    ProcessParametersPrefix64 parameters{};
    status = probeRead(userProcessParameters, sizeof(parameters), alignof(uint64_t));
    if (status == NtStatus::Success)
        status = safeCopyIn(&parameters, userProcessParameters, sizeof(parameters));
    if (status != NtStatus::Success) return status;

    const auto captureString = [&](const UnicodeString64& source,
                                   std::unique_ptr<char16_t[]>& storage,
                                   size_t& characters, bool required) noexcept -> NtStatus {
        characters = 0;
        storage.reset();
        if ((source.Length & 1) || source.Length > source.MaximumLength ||
            source.Length > MaximumPathCharacters * sizeof(char16_t) ||
            (source.Length && !source.Buffer) || (!source.Length && required))
            return NtStatus::InvalidParameter;
        if (!source.Length) return NtStatus::Success;
        auto copyStatus = probeRead(source.Buffer, source.Length, alignof(char16_t));
        if (copyStatus != NtStatus::Success) return copyStatus;
        characters = source.Length / sizeof(char16_t);
        storage.reset(new (std::nothrow_t{}) char16_t[characters + 1]);
        if (!storage) return NtStatus::NoMemory;
        copyStatus = safeCopyIn(storage.get(), source.Buffer, source.Length);
        if (copyStatus != NtStatus::Success) {
            storage.reset();
            return copyStatus;
        }
        for (size_t i = 0; i < characters; ++i) {
            if (storage[i] == 0) return NtStatus::InvalidParameter;
            if (storage[i] == u'/') storage[i] = u'\\';
        }
        storage[characters] = 0;
        return NtStatus::Success;
    };

    std::unique_ptr<char16_t[]> imagePathStorage;
    std::unique_ptr<char16_t[]> commandLineStorage;
    std::unique_ptr<char16_t[]> currentDirectoryStorage;
    size_t imagePathLength{}, commandLineLength{}, currentDirectoryLength{};
    status = captureString(parameters.ImagePathName, imagePathStorage, imagePathLength, true);
    if (status == NtStatus::Success)
        status = captureString(parameters.CommandLine, commandLineStorage, commandLineLength, false);
    if (status == NtStatus::Success)
        status = captureString(parameters.CurrentDirectoryDosPath, currentDirectoryStorage,
                               currentDirectoryLength, false);
    if (status != NtStatus::Success) return status;
    if (parameters.CurrentDirectoryHandle || parameters.DllPath.Length)
        return NtStatus::NotSupported;
    if (imagePathLength < 3 || imagePathStorage[1] != u':' || imagePathStorage[2] != u'\\')
        return NtStatus::ObjectPathSyntaxBad;
    if (currentDirectoryLength && (currentDirectoryLength < 3 ||
        currentDirectoryStorage[1] != u':' || currentDirectoryStorage[2] != u'\\'))
        return NtStatus::ObjectPathSyntaxBad;
    if (!commandLineLength) {
        commandLineStorage.reset(new (std::nothrow_t{}) char16_t[imagePathLength + 1]);
        if (!commandLineStorage) return NtStatus::NoMemory;
        std::memcpy(commandLineStorage.get(), imagePathStorage.get(),
                    (imagePathLength + 1) * sizeof(char16_t));
        commandLineLength = imagePathLength;
    }

    const auto imagePath = std::u16string_view(imagePathStorage.get(), imagePathLength);
    const auto commandLine = std::u16string_view(commandLineStorage.get(), commandLineLength);
    const auto currentDirectory = std::u16string_view(currentDirectoryStorage.get(), currentDirectoryLength);
    Handle fileHandle{};
    status = io::IoOpenBackendFile(&fileHandle, 0x0021u, imagePath);
    if (status != NtStatus::Success) {
        createInfo.State = CreateState::FailOnFileOpen;
        const auto copyStatus = safeCopyOut(userCreateInfo, &createInfo, sizeof(createInfo));
        return copyStatus == NtStatus::Success ? status : copyStatus;
    }

    Handle sectionHandle{};
    status = NtCreateSectionFromKernel(&sectionHandle, section::SECTION_ALL_ACCESS, nullptr,
        nullptr, mm::PAGE_EXECUTE_READ, section::SEC_FILE | section::SEC_IMAGE, fileHandle);
    if (status != NtStatus::Success) {
        createInfo.State = CreateState::FailOnSectionCreate;
        createInfo.Data.FailSection.FileHandle = fileHandle;
        const auto copyStatus = safeCopyOut(userCreateInfo, &createInfo, sizeof(createInfo));
        if (copyStatus == NtStatus::Success) return status; // Ownership is returned in CreateInfo.
        (void)NtClose(fileHandle);
        return copyStatus;
    }

    if (currentDirectoryLength) {
        Handle directoryHandle{};
        status = io::IoOpenBackendFile(&directoryHandle, 0x0001u, currentDirectory);
        if (status == NtStatus::Success) {
            FileBothDirInformation firstEntry{};
            status = io::IoQueryDirectoryFile(directoryHandle, true, &firstEntry);
            if (status == NtStatus::NoMoreFiles) status = NtStatus::Success;
            const auto closeStatus = NtClose(directoryHandle);
            if (status == NtStatus::Success && closeStatus != NtStatus::Success) status = closeStatus;
        }
        if (status != NtStatus::Success) {
            (void)NtClose(sectionHandle);
            (void)NtClose(fileHandle);
            return status;
        }
    }

    Handle processHandle{};
    status = createProcessEx(&processHandle, processDesiredAccess,
        userProcessAttributes ? &processAttributes : nullptr, InvalidHandleValue,
        processFlags, sectionHandle, 0, 0, false,
        mm::KernelMode, mm::KernelMode, mm::KernelMode, imagePath, commandLine, currentDirectory);
    if (status != NtStatus::Success) {
        (void)NtClose(sectionHandle);
        (void)NtClose(fileHandle);
        return status;
    }

    ps::EProcess* child{};
    ob::ObjectHeader* childReference{};
    status = ps::detail::referenceProcessHandle(processHandle, 0, child, childReference);
    uintptr_t startRoutine{};
    if (status == NtStatus::Success && child && child->nativeImages()) {
        const auto* mainImage = child->nativeImages()->module(0);
        if (mainImage && mainImage->view.headers().optionalHeader.addressOfEntryPoint &&
            mainImage->view.executableRva(mainImage->view.headers().optionalHeader.addressOfEntryPoint) &&
            mainImage->base <= UINTPTR_MAX - mainImage->view.headers().optionalHeader.addressOfEntryPoint)
            startRoutine = mainImage->base + mainImage->view.headers().optionalHeader.addressOfEntryPoint;
        else status = NtStatus::InvalidImageFormat;
    } else if (status == NtStatus::Success) status = NtStatus::InvalidImageFormat;
    if (childReference) {
        ob::ObDereferenceObject(childReference);
        childReference = nullptr;
    }

    Handle threadHandle{};
    ClientId clientId{};
    if (status == NtStatus::Success) {
        PsAttributeList threadAttributesForClient{};
        threadAttributesForClient.totalLength = sizeof(size_t) + sizeof(PsAttribute);
        threadAttributesForClient.attributes[0] = PsAttribute{
            .attribute = PS_ATTRIBUTE_CLIENT_ID,
            .size = sizeof(clientId),
            .value = reinterpret_cast<uintptr_t>(&clientId),
            .returnLength = nullptr
        };
        status = NtCreateThreadExFromKernel(&threadHandle, threadDesiredAccess,
            userThreadAttributes ? &threadAttributes : nullptr, processHandle,
            startRoutine, 0, ThreadCreateFlagsCreateSuspended, 0, 0, 0,
            &threadAttributesForClient);
    }
    if (status == NtStatus::Success && !(threadFlags & ThreadCreateFlagsCreateSuspended))
        status = NtResumeThread(threadHandle, nullptr);

    if (status != NtStatus::Success) {
        if (threadHandle) (void)NtClose(threadHandle);
        (void)NtTerminateProcess(processHandle, status);
        (void)NtClose(processHandle);
        (void)NtClose(sectionHandle);
        (void)NtClose(fileHandle);
        createInfo.State = CreateState::Initial;
        (void)safeCopyOut(userCreateInfo, &createInfo, sizeof(createInfo));
        return status;
    }

    status = ps::detail::referenceProcessHandle(processHandle, 0, child, childReference);
    if (status == NtStatus::Success && child) {
        createInfo.State = CreateState::Success;
        createInfo.Data.Success.FileHandle = fileHandle;
        createInfo.Data.Success.SectionHandle = sectionHandle;
        createInfo.Data.Success.UserProcessParametersNative = child->userProcessParametersAddress();
        createInfo.Data.Success.PebAddressNative = child->userPebAddress();
        ob::ObDereferenceObject(childReference);
        childReference = nullptr;
    } else {
        if (childReference) ob::ObDereferenceObject(childReference);
        (void)NtClose(threadHandle);
        (void)NtTerminateProcess(processHandle, status);
        (void)NtClose(processHandle);
        (void)NtClose(sectionHandle);
        (void)NtClose(fileHandle);
        return status == NtStatus::Success ? NtStatus::InvalidHandle : status;
    }

    status = safeCopyOut(userProcessHandle, &processHandle, sizeof(processHandle));
    if (status == NtStatus::Success)
        status = safeCopyOut(userThreadHandle, &threadHandle, sizeof(threadHandle));
    if (status == NtStatus::Success)
        status = safeCopyOut(userCreateInfo, &createInfo, sizeof(createInfo));
    if (status != NtStatus::Success) {
        const Handle zero{};
        (void)safeCopyOut(userProcessHandle, &zero, sizeof(zero));
        (void)safeCopyOut(userThreadHandle, &zero, sizeof(zero));
        createInfo.State = CreateState::Initial;
        createInfo.Data.Success.FileHandle = 0;
        createInfo.Data.Success.SectionHandle = 0;
        (void)safeCopyOut(userCreateInfo, &createInfo, sizeof(createInfo));
        (void)NtClose(threadHandle);
        (void)NtTerminateProcess(processHandle, status);
        (void)NtClose(processHandle);
        (void)NtClose(sectionHandle);
        (void)NtClose(fileHandle);
        return status;
    }
    return NtStatus::Success;
}

// The syscall enters with the caller's address space active.  Capture and
// publish user pointers under BSP exclusion; an Ob reference pins the target.
NtStatus NtQueryInformationProcess(Handle handle, ProcessInformationClass informationClass,
                                   void* information, uint32_t length, uint32_t* returnLength) {
    auto* thread = ke::KeGetCurrentThread();
    const auto mode = thread && thread->previousUserMode ? mm::UserMode : mm::KernelMode;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    NtStatus result = NtStatus::Success;
    const bool queryVm = informationClass == ProcessInformationClass::ProcessVmCounters;
    const bool queryPriority = informationClass == ProcessInformationClass::ProcessPriorityClass;
    const bool queryAffinity = informationClass == ProcessInformationClass::ProcessAffinityMask;
    const uint32_t required = queryVm
        ? (length == sizeof(ProcessVmCountersEx) ? sizeof(ProcessVmCountersEx)
                                                 : sizeof(ProcessVmCounters))
        : queryPriority ? sizeof(ProcessPriorityClassInformation)
        : queryAffinity ? sizeof(uintptr_t)
        : sizeof(ProcessBasicInformation);
    if (returnLength) result = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
    if (result == NtStatus::Success && !queryVm && !queryPriority && !queryAffinity &&
        informationClass != ProcessInformationClass::ProcessBasicInformation)
        result = NtStatus::InvalidInfoClass;
    if (result == NtStatus::Success &&
        (queryVm ? (length != sizeof(ProcessVmCounters) && length != sizeof(ProcessVmCountersEx))
         : queryPriority || queryAffinity ? length != required : length < required))
        result = NtStatus::InfoLengthMismatch;
    if (result == NtStatus::Success) result = ex::ProbeForWrite(information, required, alignof(uintptr_t), mode);
    ps::EProcess* process{};
    ob::ObjectHeader* reference{};
    if (result == NtStatus::Success) {
        if (handle == InvalidHandleValue) result = ps::detail::referenceCurrentProcess(process, reference);
        else {
            if (queryVm) {
                result = ps::detail::referenceProcessHandle(handle, 0x0400, process, reference);
            } else {
                result = ps::detail::referenceProcessHandle(handle, 0x1000, process, reference);
                if (result == NtStatus::AccessDenied)
                    result = ps::detail::referenceProcessHandle(handle, 0x0400, process, reference);
            }
        }
    }
    if (result == NtStatus::Success) {
        if (queryVm) {
            mm::ProcessMemoryAccounting memory{};
            if (!process->getAddressSpace().queryProcessMemoryAccounting(memory)) {
                result = NtStatus::NotSupported;
            }
            if (memory.virtualBytes > UINTPTR_MAX || memory.committedBytes > UINTPTR_MAX ||
                memory.peakVirtualBytes > UINTPTR_MAX ||
                memory.peakResidentPages > UINTPTR_MAX / mm::PageSize4KB ||
                memory.residentPages > UINTPTR_MAX / mm::PageSize4KB ||
                memory.peakCommitBytes > UINTPTR_MAX || memory.privateCommitBytes > UINTPTR_MAX) {
                result = NtStatus::NotSupported;
            } else if (result == NtStatus::Success) {
                ProcessVmCounters counters{};
                counters.peakVirtualSize = static_cast<uintptr_t>(memory.peakVirtualBytes);
                counters.virtualSize = static_cast<uintptr_t>(memory.virtualBytes);
                counters.pageFaultCount = memory.pageFaultCount;
                counters.peakWorkingSetSize = static_cast<uintptr_t>(memory.peakResidentPages * mm::PageSize4KB);
                counters.workingSetSize = static_cast<uintptr_t>(memory.residentPages * mm::PageSize4KB);
                counters.pagefileUsage = static_cast<uintptr_t>(memory.committedBytes);
                counters.peakPagefileUsage = static_cast<uintptr_t>(memory.peakCommitBytes);
                if (length == sizeof(ProcessVmCountersEx)) {
                    ProcessVmCountersEx extended{};
                    extended.counters = counters;
                    extended.privateUsage = static_cast<uintptr_t>(memory.privateCommitBytes);
                    result = ex::ExSafeCopyToUser(information, &extended, sizeof(extended), mode);
                } else {
                    result = ex::ExSafeCopyToUser(information, &counters, sizeof(counters), mode);
                }
            }
        } else if (queryPriority) {
            ProcessPriorityClassInformation value{};
            value.priorityClass = process->processPriorityClass();
            result = ex::ExSafeCopyToUser(information, &value, sizeof(value), mode);
        } else if (queryAffinity) {
            const uintptr_t value = process->processAffinityMask();
            result = ex::ExSafeCopyToUser(information, &value, sizeof(value), mode);
        } else {
            ProcessBasicInformation basic{};
            basic.exitStatus = process->exitStatus();
            basic.pebBaseAddress = process->userPebAddress();
            basic.affinityMask = process->processAffinityMask();
            constexpr int32_t basePriorities[] = {0, 4, 8, 13, 24, 6, 10};
            const auto priorityClass = process->processPriorityClass();
            basic.basePriority = priorityClass < std::size(basePriorities)
                ? basePriorities[priorityClass] : ke::PRIORITY_NORMAL;
            basic.uniqueProcessId = process->processId();
            basic.inheritedFromUniqueProcessId = process->inheritedFromProcessId();
            result = ex::ExSafeCopyToUser(information, &basic, sizeof(basic), mode);
        }
    }
    if (returnLength && (result == NtStatus::Success || result == NtStatus::InfoLengthMismatch)) {
        const auto copy = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
        if (copy != NtStatus::Success) result = copy;
    }
    if (reference) ob::ObDereferenceObject(reference);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

NtStatus NtSetInformationProcess(Handle handle, ProcessInformationClass informationClass,
                                 const void* information, uint32_t length) {
    auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const bool setPriority = informationClass == ProcessInformationClass::ProcessPriorityClass;
    const bool setAffinity = informationClass == ProcessInformationClass::ProcessAffinityMask;
    const uint32_t required = setPriority ? sizeof(ProcessPriorityClassInformation)
        : sizeof(uintptr_t);
    NtStatus result = NtStatus::Success;
    if (!setPriority && !setAffinity) result = NtStatus::InvalidInfoClass;
    if (result == NtStatus::Success && length != required) result = NtStatus::InfoLengthMismatch;
    if (result == NtStatus::Success)
        result = ex::ProbeForRead(information, required,
            setPriority ? alignof(ProcessPriorityClassInformation) : alignof(uintptr_t), mode);

    ProcessPriorityClassInformation priority{};
    uintptr_t affinity{};
    if (result == NtStatus::Success) {
        result = setPriority
            ? ex::ExSafeCopyFromUser(&priority, information, sizeof(priority), mode)
            : ex::ExSafeCopyFromUser(&affinity, information, sizeof(affinity), mode);
    }
    if (result == NtStatus::Success && setPriority &&
        (priority.foreground > 1 || priority.priorityClass < 1 || priority.priorityClass > 6))
        result = NtStatus::InvalidParameter;

    ps::EProcess* process{};
    ob::ObjectHeader* reference{};
    if (result == NtStatus::Success) {
        result = handle == InvalidHandleValue
            ? ps::detail::referenceCurrentProcess(process, reference)
            : ps::detail::referenceProcessHandle(handle, 0x0200u, process, reference);
    }
    if (result == NtStatus::Success && setPriority && priority.priorityClass == 4) {
        auto* caller = current ? current->owningProcess : nullptr;
        se::FreestandingAccessToken* token{};
        ob::ObjectHeader* tokenReference{};
        if (!caller || caller->referencePrimaryToken(token, tokenReference) != NtStatus::Success ||
            !token || (token->EnabledPrivileges & (uint64_t{1} << 14)) == 0) {
            if (tokenReference) ob::ObDereferenceObject(tokenReference);
            result = NtStatus::PrivilegeNotHeld;
        } else {
            ob::ObDereferenceObject(tokenReference);
        }
    }
    if (result == NtStatus::Success && setAffinity) {
        const auto snapshot = km::perf::QueryProcessorPerformanceSnapshot();
        if (!snapshot.processorCount || snapshot.processorCount > 64)
            result = NtStatus::NotSupported;
        else {
            const uintptr_t activeMask = snapshot.processorCount == 64
                ? UINT64_MAX : ((uintptr_t{1} << snapshot.processorCount) - 1);
            if (!affinity || (affinity & ~activeMask)) result = NtStatus::InvalidParameter;
        }
    }
    if (result == NtStatus::Success) {
        if (setPriority) process->setProcessPriorityClass(priority.priorityClass);
        else process->setProcessAffinityMask(affinity);
    }
    if (reference) ob::ObDereferenceObject(reference);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

NtStatus NtQueryInformationToken(Handle tokenHandle, uint32_t informationClass,
                                 void* information, uint32_t length,
                                 uint32_t* returnLength) {
    auto* thread = ke::KeGetCurrentThread();
    const auto mode = thread && thread->previousUserMode ? mm::UserMode : mm::KernelMode;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");

    NtStatus result = NtStatus::Success;
    if (!returnLength) result = NtStatus::InvalidParameter;
    else result = ex::ProbeForWrite(returnLength, sizeof(*returnLength),
                                    alignof(uint32_t), mode);
    // NT callers must not observe a stale required size on any failing query.
    // Clear it once the pointer has been validated; publish the required size
    // below only after the information class and token have been validated.
    if (result == NtStatus::Success) {
        const uint32_t noLength = 0;
        result = ex::ExSafeCopyToUser(returnLength, &noLength, sizeof(noLength), mode);
    }
    if (result == NtStatus::Success && informationClass != 1u) {
        result = NtStatus::InvalidInfoClass;
    }

    ps::EProcess* process{};
    ob::ObjectHeader* processReference{};
    se::FreestandingAccessToken* token{};
    ob::ObjectHeader* tokenReference{};
    if (result == NtStatus::Success) {
        if (tokenHandle != static_cast<Handle>(-4)) result = NtStatus::InvalidHandle;
        else {
            result = ps::detail::referenceCurrentProcess(process, processReference);
            if (result == NtStatus::Success)
                result = process->referencePrimaryToken(token, tokenReference);
        }
    }

    uint32_t required = 0;
    std::array<uint8_t, sizeof(micant::native_nt::NativeTokenUser) + 8 + 8 * sizeof(uint32_t)> staged{};
    if (result == NtStatus::Success) {
        if (!token || token->UserSid.Revision != 1 ||
            token->UserSid.SubAuthorityCount > std::size(token->UserSid.SubAuthority)) {
            result = NtStatus::InvalidParameter;
        } else {
            constexpr uint32_t sidHeaderSize = 8;
            const uint32_t sidBytes = sidHeaderSize +
                static_cast<uint32_t>(token->UserSid.SubAuthorityCount) * sizeof(uint32_t);
            required = sizeof(micant::native_nt::NativeTokenUser) + sidBytes;
            if (length < required) {
                result = NtStatus::BufferTooSmall;
            } else if (!information ||
                       reinterpret_cast<uintptr_t>(information) >
                           UINTPTR_MAX - sizeof(micant::native_nt::NativeTokenUser)) {
                result = NtStatus::InvalidParameter;
            } else {
                micant::native_nt::NativeTokenUser tokenUser{};
                tokenUser.user.sid = reinterpret_cast<void*>(
                    reinterpret_cast<uintptr_t>(information) + sizeof(tokenUser));
                std::memcpy(staged.data(), &tokenUser, sizeof(tokenUser));
                auto* sid = staged.data() + sizeof(tokenUser);
                // SID's public wire layout has an 8-byte fixed header. The
                // kernel SidValue has naturally aligned SubAuthority storage,
                // so serialize each field instead of copying its padded struct.
                std::memcpy(sid, &token->UserSid.Revision, 1);
                std::memcpy(sid + 1, &token->UserSid.SubAuthorityCount, 1);
                std::memcpy(sid + 2, token->UserSid.IdentifierAuthority, 6);
                for (uint32_t index = 0; index < token->UserSid.SubAuthorityCount; ++index)
                    std::memcpy(sid + sidHeaderSize + index * sizeof(uint32_t),
                                &token->UserSid.SubAuthority[index], sizeof(uint32_t));
                result = ex::ProbeForWrite(information, required,
                    alignof(micant::native_nt::NativeTokenUser), mode);
                if (result == NtStatus::Success)
                    result = ex::ExSafeCopyToUser(information, staged.data(), required, mode);
            }
        }
    }

    if (returnLength && required) {
        const auto copy = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
        if (copy != NtStatus::Success) result = copy;
    }
    if (tokenReference) ob::ObDereferenceObject(tokenReference);
    if (processReference) ob::ObDereferenceObject(processReference);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

NtStatus NtQueryInformationThread(Handle handle, ThreadInformationClass informationClass,
                                  void* information, uint32_t length, uint32_t* returnLength) {
    auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    constexpr uint32_t required = sizeof(ThreadBasicInformation);
    NtStatus status = NtStatus::Success;
    if (returnLength) status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
    if (status == NtStatus::Success && informationClass != ThreadInformationClass::ThreadBasicInformation)
        status = NtStatus::InvalidInfoClass;
    if (status == NtStatus::Success && length < required) status = NtStatus::InfoLengthMismatch;
    if (status == NtStatus::Success)
        status = ex::ProbeForWrite(information, required, alignof(uintptr_t), mode);

    ps::EThread* thread{};
    ke::KThread* currentThread{};
    ob::ObjectHeader* reference{};
    if (status == NtStatus::Success) {
        if (handle == static_cast<Handle>(-2)) {
            currentThread = ke::KeGetCurrentThread();
            status = ps::detail::referenceCurrentThread(thread, reference);
            // Internal bootstrap threads can precede publication of an Ob
            // thread object. The current-thread pseudo handle still denotes
            // the live KTHREAD and can be queried without taking a reference.
            if (status == NtStatus::InvalidHandle && currentThread && currentThread->tid) {
                status = NtStatus::Success;
            }
        } else status = ps::detail::referenceThreadHandle(handle, 0x0040, thread, reference);
    }
    if (status == NtStatus::Success) {
        ThreadBasicInformation basic{};
        basic.exitStatus = thread ? thread->exitStatus() : NtStatus::Pending;
        basic.tebBaseAddress = thread ? thread->userTebAddress() : currentThread->tebAddress;
        basic.clientId = thread ? ClientId{thread->ownerProcessId(), thread->threadId()}
                                : ClientId{currentThread->pid, currentThread->tid};
        basic.affinityMask = 1;
        basic.priority = thread && thread->kernelThread()
            ? thread->kernelThread()->priority : currentThread->priority;
        basic.basePriority = basic.priority;
        status = ex::ExSafeCopyToUser(information, &basic, sizeof(basic), mode);
    }
    if (returnLength && (status == NtStatus::Success || status == NtStatus::InfoLengthMismatch)) {
        const auto copy = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
        if (copy != NtStatus::Success) status = copy;
    }
    if (reference) ob::ObDereferenceObject(reference);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return status;
}

NtStatus NtReadVirtualMemory(Handle handle, const void* baseAddress, void* buffer,
                             size_t length, size_t* bytesRead) {
    auto* thread = ke::KeGetCurrentThread();
    const auto mode = thread && thread->previousUserMode ? mm::UserMode : mm::KernelMode;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    NtStatus result = bytesRead ? ex::ProbeForWrite(bytesRead, sizeof(*bytesRead), alignof(size_t), mode)
                                : NtStatus::Success;
    ps::EProcess* process{};
    ob::ObjectHeader* reference{};
    if (result == NtStatus::Success && length)
        result = handle == InvalidHandleValue ? ps::detail::referenceCurrentProcess(process, reference)
            : ps::detail::referenceProcessHandle(handle, 0x0010, process, reference);
    const auto source = reinterpret_cast<uintptr_t>(baseAddress);
    const auto destination = reinterpret_cast<uintptr_t>(buffer);
    if (result == NtStatus::Success && length &&
        (source > UINTPTR_MAX - (length - 1) || destination > UINTPTR_MAX - (length - 1)))
        result = NtStatus::AccessViolation;
    size_t copied = 0;
    uint8_t staging[4096];
    // A same-process overlap must be copied backwards to preserve the source.
    const bool reverse = process && process == ke::KeGetCurrentProcess() && destination > source &&
                         destination - source < length;
    while (result == NtStatus::Success && copied < length) {
        const size_t remaining = length - copied;
        size_t chunk = remaining < sizeof(staging) ? remaining : sizeof(staging);
        size_t offset = reverse ? remaining - chunk : copied;
        if (!reverse) {
            const size_t sourcePage = 4096 - ((source + offset) & 4095);
            const size_t destinationPage = 4096 - ((destination + offset) & 4095);
            if (chunk > sourcePage) chunk = sourcePage;
            if (chunk > destinationPage) chunk = destinationPage;
        } else {
            const size_t sourcePage = ((source + remaining - 1) & 4095) + 1;
            const size_t destinationPage = ((destination + remaining - 1) & 4095) + 1;
            if (chunk > sourcePage) chunk = sourcePage;
            if (chunk > destinationPage) chunk = destinationPage;
            offset = remaining - chunk;
        }
        ke::KAPC_STATE attach{};
        const auto depth = thread ? thread->processAttachDepth : 0;
        if (!thread) { result = NtStatus::InvalidParameter; break; }
        ke::KeStackAttachProcess(process, &attach);
        if (thread->processAttachDepth != depth + 1) { result = NtStatus::ProcessIsTerminating; break; }
        result = ex::ExSafeCopyFromUser(staging, reinterpret_cast<const void*>(source + offset), chunk, mm::UserMode);
        ke::KeUnstackDetachProcess(&attach);
        if (thread->processAttachDepth != depth) __builtin_trap();
        if (result == NtStatus::Success)
            result = ex::ExSafeCopyToUser(reinterpret_cast<void*>(destination + offset), staging, chunk, mode);
        if (result == NtStatus::Success) copied += chunk;
        // Do not monopolize the BSP across an arbitrarily large read.
        if ((flags & (uint64_t{1} << 9)) && result == NtStatus::Success && copied < length)
            __asm__ __volatile__("sti; nop; cli" : : : "memory");
    }
    if (copied && result != NtStatus::Success) result = NtStatus::PartialCopy;
    if (bytesRead) {
        const auto copy = ex::ExSafeCopyToUser(bytesRead, &copied, sizeof(copied), mode);
        if (copy != NtStatus::Success) result = copy;
    }
    if (reference) ob::ObDereferenceObject(reference);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

NtStatus NtFlushInstructionCache(Handle handle, const void* baseAddress, size_t size) {
    auto* thread = ke::KeGetCurrentThread();
    if (!thread || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL || ke::g_CurrentCpu.processorNumber != 0)
        return NtStatus::InvalidDeviceState;
    const auto address = reinterpret_cast<uintptr_t>(baseAddress);
    if (baseAddress && !size) return NtStatus::Success;
    if (baseAddress && (address > UINTPTR_MAX - (size - 1) ||
        (thread->previousUserMode && (address > um::MM_HIGHEST_USER_ADDRESS ||
         size - 1 > um::MM_HIGHEST_USER_ADDRESS - address)))) return NtStatus::AccessViolation;

    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    ps::EProcess* process{};
    ob::ObjectHeader* reference{};
    auto result = handle == InvalidHandleValue ? ps::detail::referenceCurrentProcess(process, reference)
        : ps::detail::referenceProcessHandle(handle, 0x0020, process, reference); // PROCESS_VM_WRITE
    ke::KAPC_STATE attach{};
    const size_t depth = thread->processAttachDepth;
    bool attached = false;
    if (result == NtStatus::Success && process != ke::KeGetCurrentProcess()) {
        ke::KeStackAttachProcess(process, &attach);
        attached = thread->processAttachDepth == depth + 1;
        if (!attached) result = NtStatus::ProcessIsTerminating;
    }
    if (result == NtStatus::Success) {
        // This production backend currently schedules only the BSP. x86-64
        // coherent instruction/data caches plus a serializing CPUID complete
        // the instruction-stream synchronization on that processor.
        uint32_t eax{}, ebx{}, ecx{}, edx{};
        __asm__ __volatile__("mfence; cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx) : : "memory");
    }
    if (attached) {
        ke::KeUnstackDetachProcess(&attach);
        if (thread->processAttachDepth != depth) __builtin_trap();
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (reference) ob::ObDereferenceObject(reference);
    return result;
}

NtStatus NtTerminateThread(Handle threadHandle, NtStatus exitStatus) {
    constexpr uint32_t ThreadTerminateAccess = 0x0001;
    auto* current = ke::KeGetCurrentThread();
    if (!threadHandle) {
        if (!current || !current->owningThread) return NtStatus::InvalidHandle;
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        NtStatus result = NtStatus::CantTerminateSelf;
        // The NULL-handle rule uses the current APC process, which may be
        // different from the thread's owner during KeStackAttachProcess.
        auto* currentProcess = ke::KeGetCurrentProcess();
        if (currentProcess && currentProcess->activeThreadCount() != 1)
            result = current->owningThread->exitCurrent(exitStatus);
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return result;
    }
    if (threadHandle == static_cast<Handle>(-2)) {
        if (!current || !current->owningThread) return NtStatus::InvalidHandle;
        return current->owningThread->exitCurrent(exitStatus);
    }
    ob::ObjectHeader* reference{};
    ps::EThread* target{};
    const auto status = ps::detail::referenceThreadHandle(threadHandle, ThreadTerminateAccess,
                                                           target, reference);
    if (status != NtStatus::Success) return status;
    const bool self = current && current->owningThread == target;
    if (self) {
        // A running thread's lifecycle reference pins it across self-exit.
        ob::ObDereferenceObject(reference);
        return target->exitCurrent(exitStatus);
    }
    const auto result = target->isSystemThread() ? NtStatus::AccessDenied : target->terminateOther(exitStatus);
    if (result == NtStatus::Success) io::IoCancelThreadReads(*target);
    ob::ObDereferenceObject(reference);
    return result;
}

namespace {
NtStatus terminateCurrentProcessPseudoHandle(NtStatus exitStatus) noexcept {
    auto* current = ke::KeGetCurrentThread();
    if (!current ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    // The current-process pseudo-handle does not pass through a handle table.
    // The running thread pins its owning process, including bootstrap process
    // objects which have not been published through Ob.
    auto* process = ke::KeGetCurrentProcess();
    if (!process) return NtStatus::InvalidParameter;
    if (process->isSystemProcess()) {
        return NtStatus::AccessDenied;
    }

    // The current-process pseudo-handle requests termination including the
    // caller. Ordinary handles go through the PROCESS_TERMINATE check.
    if (process == current->owningProcess) return process->terminateCurrent(exitStatus);
    // An attached thread still belongs to its original process. Terminate
    // the attached process as a remote target; the attach reference keeps
    // its root alive until the caller detaches.
    const auto result = process->terminateOther(exitStatus);
    if (result == NtStatus::Success) io::IoCancelProcessReads(*process);
    return result;
}
}

NtStatus NtTerminateProcess(Handle processHandle, NtStatus exitStatus) {
    constexpr uint32_t ProcessTerminateAccess = 0x0001;
    if (!processHandle) {
        auto* current = ke::KeGetCurrentThread();
        if (!current ||
            ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
        auto* process = ke::KeGetCurrentProcess();
        if (!process) return NtStatus::InvalidParameter;
        if (process->isSystemProcess()) return NtStatus::AccessDenied;
        // NULL terminates the other threads without setting the process exit
        // state or terminating the caller; the pseudo-handle is different.
        const auto result = process->terminateOtherThreads(exitStatus);
        if (result == NtStatus::Success)
            io::IoCancelTerminatingProcessReads(*process, current->owningThread);
        return result;
    }
    if (processHandle == InvalidHandleValue)
        return terminateCurrentProcessPseudoHandle(exitStatus);
    auto* current = ke::KeGetCurrentThread();
    ob::ObjectHeader* reference{};
    ps::EProcess* process{};
    const auto status = ps::detail::referenceProcessHandle(processHandle, ProcessTerminateAccess, process, reference);
    if (status != NtStatus::Success) return status;
    if (process->isSystemProcess()) {
        ob::ObDereferenceObject(reference);
        return NtStatus::AccessDenied;
    }
    const bool isCurrent = current && current->owningProcess == process;
    if (isCurrent) {
        // The attached current thread pins its process until deferred reap.
        ob::ObDereferenceObject(reference);
        return process->terminateCurrent(exitStatus);
    }
    const auto result = process->terminateOther(exitStatus);
    if (result == NtStatus::Success) io::IoCancelProcessReads(*process);
    ob::ObDereferenceObject(reference);
    return result;
}
}
