#include "micant/km/executive/ps.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/core/sync.hpp"
#include "micant/km/executive/usermode.hpp"

namespace micant::ps {

EProcess::EProcess() = default;
EProcess::~EProcess() = default;

NtStatus EProcess::initializeAddressSpace() noexcept {
    using namespace hal::amd64;
    if (root_.pfn) return NtStatus::InvalidParameter;
    handles_.reset(new (std::nothrow_t{}) ob::HandleTable{});
    if (!handles_) return NtStatus::NoMemory;
    if (!createProcessPagingRoot(root_)) { handles_.reset(); return NtStatus::NoMemory; }
    const mm::ProcessAddressSpace::PagingBackend backend{
        &root_, mapBootstrapManagedPage, unmapBootstrapManagedPage,
        mapBootstrapProtectManagedPage, mapBootstrapManagedView, unmapBootstrapManagedView};
    if (!addressSpace_.configurePagingBackend(backend, BootstrapManagedBase, BootstrapManagedSize)) {
        if (!destroyProcessPagingRoot(root_)) __builtin_trap();
        handles_.reset();
        return NtStatus::Unsuccessful;
    }
    terminating_ = false;
    exitStatus_ = NtStatus::Pending;
    imagesFinalized_ = false;
    userPeb_ = 0;
    userLoaderData_ = 0;
    userProcessParameters_ = 0;
    nativeImagePaths_ = {};
    processImagePath_ = {};
    processCommandLine_ = {};
    processParametersConfigured_ = false;
    userTlsLayouts_.reset();
    userTlsCount_ = 0;
    return NtStatus::Success;
}

NtStatus EProcess::closeAddressSpace() noexcept {
    using namespace hal::amd64;
    if (!root_.pfn || attachedThreads_) return NtStatus::InvalidParameter;
    uintptr_t previous{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(previous));
    if (previous == getDirectoryTableBase()) return NtStatus::InvalidParameter;
    if (!switchProcessPagingRoot(&root_)) return NtStatus::Unsuccessful;
    handles_->closeAllHandles();
    const NtStatus status = addressSpace_.close();
    if (!switchProcessPagingRootByPhysical(previous)) __builtin_trap();
    if (status != NtStatus::Success) return status;
    userPeb_ = 0;
    userLoaderData_ = 0;
    userProcessParameters_ = 0;
    nativeImagePaths_ = {};
    processImagePath_ = {};
    processCommandLine_ = {};
    processParametersConfigured_ = false;
    userTlsLayouts_.reset();
    userTlsCount_ = 0;
    images_.reset();
    if (!destroyProcessPagingRoot(root_)) return NtStatus::Unsuccessful;
    handles_.reset();
    return NtStatus::Success;
}

NtStatus EProcess::attachThread(ke::KThread& thread) noexcept {
    if (!root_.pfn || terminating_ || thread.owningProcess || thread.readyQueued ||
        (thread.state != ke::ThreadState::Initialized && thread.state != ke::ThreadState::Ready))
        return NtStatus::InvalidParameter;
    thread.owningProcess = this;
    thread.directoryTableBase = getDirectoryTableBase();
    thread.processNext = threads_;
    threads_ = &thread;
    ++attachedThreads_;
    return NtStatus::Success;
}

NtStatus EProcess::terminateCurrent(NtStatus exitStatus) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto* current = ke::KeGetCurrentThread();
    NtStatus result = NtStatus::InvalidParameter;
    const auto previous = ke::KeGetCurrentIrql();
    if (current && current->owningProcess == this && current->state == ke::ThreadState::Running &&
        previous <= ke::APC_LEVEL) {
        size_t otherActive = 0;
        bool supported = true;
        for (auto* thread = threads_; thread; thread = thread->processNext) {
            if (thread == current || thread->state == ke::ThreadState::Terminated) continue;
            const bool readyWaiter = thread->state == ke::ThreadState::Ready && thread->readyQueued;
            const bool blockedWaiter = !terminating_ && thread->state == ke::ThreadState::Waiting &&
                thread->eventWaitObject && thread->eventWaitBlock;
            if (!thread->eventWaitInProgress || (!readyWaiter && !blockedWaiter)) {
                supported = false;
                break;
            }
            ++otherActive;
        }
        if (!supported) {
            result = NtStatus::NotImplemented;
        } else {
            ke::KfRaiseIrql(ke::DISPATCH_LEVEL);
            auto& scheduler = ke::PriorityScheduler::get();
            const bool wasTerminating = terminating_;
            const NtStatus oldStatus = exitStatus_;
            bool cancelled = true;
            for (auto* thread = threads_; thread; thread = thread->processNext) {
                if (thread != current && thread->state == ke::ThreadState::Waiting &&
                    !static_cast<sync::detail::BootstrapEvent*>(thread->eventWaitObject)->cancelWaitForTermination(*thread)) {
                    cancelled = false;
                    break;
                }
            }
            auto* next = cancelled ? scheduler.selectNextThread() : nullptr;
            if (next) {
                if (otherActive && (next->owningProcess != this || !next->eventWaitInProgress)) {
                    next->state = ke::ThreadState::Ready;
                    if (!scheduler.readyKThread(next)) __builtin_trap();
                    result = NtStatus::NotImplemented;
                } else {
                    if (!terminating_) { terminating_ = true; exitStatus_ = exitStatus; }
                    current->state = ke::ThreadState::Terminated;
                    if (ke::KiSwapContext(current, next)) __builtin_trap();
                    current->state = ke::ThreadState::Running;
                    terminating_ = wasTerminating;
                    exitStatus_ = oldStatus;
                    next->state = ke::ThreadState::Ready;
                    if (!scheduler.readyKThread(next)) __builtin_trap();
                }
            }
            if (!wasTerminating) {
                for (auto* thread = threads_; thread; thread = thread->processNext) {
                    if (thread->eventWaitCancelled &&
                        !static_cast<sync::detail::BootstrapEvent*>(thread->eventWaitObject)->restoreCancelledWait(*thread))
                        __builtin_trap();
                }
            }
            ke::KeLowerIrql(previous);
            if (result != NtStatus::NotImplemented) result = NtStatus::Unsuccessful;
        }
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

NtStatus EProcess::detachThread(ke::KThread& thread) noexcept {
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
    thread.owningProcess = nullptr;
    thread.directoryTableBase = 0;
    return NtStatus::Success;
}

EThread::~EThread() {
    if (thread_ || stack_ || process_) __builtin_trap();
}

NtStatus EThread::initialize(EProcess& process, size_t stackSize,
                             StartRoutine start, void* argument) noexcept {
    if (thread_ || stack_ || process_ || !start || stackSize < 4096 || stackSize > 1024 * 1024)
        return NtStatus::InvalidParameter;
    std::unique_ptr<ke::KThread> thread(new (std::nothrow_t{}) ke::KThread{});
    std::unique_ptr<uint8_t[]> stack(new (std::nothrow_t{}) uint8_t[stackSize]);
    if (!thread || !stack) return NtStatus::NoMemory;
    if (!ke::detail::prepareBootstrapThread(*thread, stack.get(), stackSize, startThread, this))
        return NtStatus::InvalidParameter;
    const auto status = process.attachThread(*thread);
    if (status != NtStatus::Success) return status;
    process_ = &process;
    thread_ = std::move(thread);
    stack_ = std::move(stack);
    start_ = start;
    argument_ = argument;
    return NtStatus::Success;
}

NtStatus EThread::ready() noexcept {
    if (!thread_ || thread_->state != ke::ThreadState::Initialized) return NtStatus::InvalidParameter;
    return ke::PriorityScheduler::get().readyKThread(thread_.get())
        ? NtStatus::Success : NtStatus::Unsuccessful;
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
                                        bool interruptible) noexcept {
    uintptr_t currentRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(currentRoot));
    if (currentRoot != process.getDirectoryTableBase() || !entry || userStackSize < mm::PageSize4KB ||
        userStackSize > 1024 * 1024 || userStackSize > SIZE_MAX - mm::PageMask4KB ||
        !hal::amd64::bootstrapUserRange(entry, 1, false, true)) return NtStatus::InvalidParameter;
    if (tls.size() > 16) return NtStatus::NotImplemented;
    for (const auto& layout : tls) {
        if (!layout.present || layout.callbackCount || !layout.alignment || layout.alignment > 256 ||
            layout.initialData.size() > mm::PageSize4KB - 0x100 ||
            layout.zeroFill > mm::PageSize4KB - 0x100 - layout.initialData.size()) return NtStatus::NotImplemented;
    }
    const size_t stackSize = (userStackSize + mm::PageMask4KB) & ~mm::PageMask4KB;
    auto status = initialize(process, kernelStackSize, startUser, this);
    if (status != NtStatus::Success) return status;
    auto& space = process.getAddressSpace();
    status = space.allocate(userStack_, stackSize, mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
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
        if (reap() != NtStatus::Success) return NtStatus::Unsuccessful;
        return status;
    }
    static_assert(sizeof(um::Teb64) <= 2 * mm::PageSize4KB);
    auto* teb = new (reinterpret_cast<void*>(userTeb_)) um::Teb64{};
    teb->self = userTeb_;
    teb->stackBase = userStack_ + stackSize;
    teb->stackLimit = userStack_;
    teb->threadLocalStoragePointer = userTls_;
    teb->processEnvironmentBlock = process.userPebAddress();
    userEntry_ = entry;
    userInterruptible_ = interruptible;
    userStackPointer_ = userStack_ + stackSize - 128;
    thread_->tebAddress = userTeb_;
    return NtStatus::Success;
}

NtStatus EThread::initializeUser(EProcess& process, uintptr_t entry, size_t kernelStackSize,
                                 size_t userStackSize, bool interruptible) noexcept {
    if (!process.nativeImagesFinalized()) return NtStatus::InvalidParameter;
    return initializeUserWithTls(process, entry, kernelStackSize, userStackSize,
                                 process.userTlsLayouts(), interruptible);
}

void EThread::startUser(void* argument) noexcept {
    auto* self = static_cast<EThread*>(argument);
    if (!self || !self->userEntry_ || !self->userStackPointer_) __builtin_trap();
    hal::amd64::enterBootstrapUserThread(self->userEntry_, self->userStackPointer_, self->userInterruptible_);
}

NtStatus EThread::releaseUserMemory() noexcept {
    if (!userStack_ && !userTeb_ && !userTls_) return NtStatus::Success;
    uintptr_t previous{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(previous));
    const uintptr_t target = process_->getDirectoryTableBase();
    if (previous != target && !hal::amd64::switchProcessPagingRootByPhysical(target)) return NtStatus::Unsuccessful;
    auto& space = process_->getAddressSpace();
    NtStatus status = NtStatus::Success;
    for (auto* address : {&userTls_, &userTeb_, &userStack_}) {
        if (*address && (status = space.free(*address, 0, mm::MEM_RELEASE)) != NtStatus::Success) break;
        *address = 0;
    }
    if (previous != target && !hal::amd64::switchProcessPagingRootByPhysical(previous)) __builtin_trap();
    if (status == NtStatus::Success) {
        userEntry_ = userStackPointer_ = 0;
        userInterruptible_ = false;
        thread_->tebAddress = 0;
    }
    return status;
}

NtStatus EThread::cancelBeforeStart() noexcept {
    if (!thread_ || started_ || thread_.get() == ke::KeGetCurrentThread())
        return NtStatus::InvalidParameter;
    if (thread_->state == ke::ThreadState::Ready) {
        if (!ke::PriorityScheduler::get().removeReadyThread(thread_.get()))
            return NtStatus::InvalidParameter;
    } else if (thread_->state != ke::ThreadState::Initialized || thread_->readyQueued) {
        return NtStatus::InvalidParameter;
    }
    thread_->state = ke::ThreadState::Terminated;
    return NtStatus::Success;
}

void EThread::startThread(void* argument) noexcept {
    auto* self = static_cast<EThread*>(argument);
    if (!self || !self->thread_ || ke::KeGetCurrentThread() != self->thread_.get()) __builtin_trap();
    self->started_ = true;
    ke::KeLowerIrql(ke::PASSIVE_LEVEL);
    self->start_(self->argument_);
    __asm__ __volatile__("cli" : : : "memory");
    ke::KfRaiseIrql(ke::DISPATCH_LEVEL);
    auto* next = ke::PriorityScheduler::get().selectNextThread();
    if (!next) __builtin_trap();
    self->thread_->state = ke::ThreadState::Terminated;
    if (!ke::KiSwapContext(self->thread_.get(), next)) __builtin_trap();
    __builtin_trap(); // A terminated thread may never resume.
}

NtStatus EThread::reap() noexcept {
    if (!thread_ || !process_ || thread_->readyQueued || thread_.get() == ke::KeGetCurrentThread() ||
        (thread_->state != ke::ThreadState::Initialized && thread_->state != ke::ThreadState::Terminated))
        return NtStatus::InvalidParameter;
    if (!thread_->kernelApcList.empty() || !thread_->userApcList.empty()) return NtStatus::NotImplemented;
    const auto cleanup = releaseUserMemory();
    if (cleanup != NtStatus::Success) return cleanup;
    const auto status = process_->detachThread(*thread_);
    if (status != NtStatus::Success) return status;
    thread_.reset();
    stack_.reset();
    process_ = nullptr;
    start_ = nullptr;
    argument_ = nullptr;
    started_ = false;
    return NtStatus::Success;
}
}

namespace micant::sys {
NtStatus NtTerminateProcess(Handle processHandle, NtStatus exitStatus) {
    if (processHandle != InvalidHandleValue) return NtStatus::NotImplemented;
    auto* current = ke::KeGetCurrentThread();
    if (!current || !current->owningProcess) return NtStatus::InvalidParameter;
    return current->owningProcess->terminateCurrent(exitStatus);
}
}
