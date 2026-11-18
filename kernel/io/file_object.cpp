#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/ob.hpp"
#if defined(MICANT_FREESTANDING)
#include "micant/km/core/passive_work.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/km/executive/se.hpp"
#endif
#include <new>

namespace micant::sys { NtStatus referenceBootstrapEventForIo(Handle, ob::ObjectHeader*&) noexcept; }

namespace micant::io {
namespace {
ob::ObjectType g_fileType{};
ob::ObjectType g_completionType{};
FileOpenRoutine g_fileOpen{};
void* g_fileOpenContext{};
FileSystemDirectoryRoutine g_systemDirectory{};
void* g_systemDirectoryContext{};
std::atomic_flag g_fileBackendLock = ATOMIC_FLAG_INIT;

void lockFileBackend() noexcept {
    while (g_fileBackendLock.test_and_set(std::memory_order_acquire)) __asm__ __volatile__("pause");
}
void unlockFileBackend() noexcept { g_fileBackendLock.clear(std::memory_order_release); }

bool fileBackendAccessAllowed(const se::FreestandingSecurityDescriptor* descriptor,
                              uint32_t desiredAccess) noexcept {
    if (!descriptor) return true;
#if defined(MICANT_FREESTANDING)
    auto* current = ke::KeGetCurrentThread();
    auto* process = ke::KeGetCurrentProcess();
    if (!current || !current->owningThread || !process ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return false;
    se::FreestandingAccessToken* primary{};
    se::FreestandingAccessToken* impersonation{};
    ob::ObjectHeader* primaryReference{};
    ob::ObjectHeader* impersonationReference{};
    if (process->referencePrimaryToken(primary, primaryReference) != NtStatus::Success)
        return false;
    const auto impersonationStatus = current->owningThread->referenceImpersonationToken(
        impersonation, impersonationReference);
    if (impersonationStatus != NtStatus::Success) {
        se::SeDereferenceFreestandingToken(primaryReference);
        return false;
    }
    const auto* effective = se::SeGetEffectiveFreestandingToken(primary, impersonation);
    uint32_t granted{};
    const uint32_t requested = desiredAccess & 0x00000003u; // FILE_READ_DATA / FILE_WRITE_DATA
    const bool allowed = se::SeAccessCheckFreestanding(effective, *descriptor, requested, granted);
    if (impersonationReference) se::SeDereferenceFreestandingToken(impersonationReference);
    se::SeDereferenceFreestandingToken(primaryReference);
    return allowed && (granted & requested) == requested;
#else
    (void)desiredAccess;
    return false;
#endif
}

void deleteCompletionPort(ob::ObjectHeader* header) noexcept {
    header->as<IoCompletionPort>()->~IoCompletionPort();
}
void deleteFileObject(ob::ObjectHeader* header) noexcept {
    header->as<FileObject>()->~FileObject();
}
void closeFileHandle(ob::ObjectHeader* header, Handle) {
    if (header->handleCount.load(std::memory_order_acquire) == 0)
        header->as<FileObject>()->notifyLastHandleClosed();
}
#if defined(MICANT_FREESTANDING)
// Requests remain on the issuing thread's kernel stack until completion.  The
// scheduler is currently BSP-only; interrupt exclusion protects the list.
struct PendingRead {
    PendingRead* next{};
    FileObject* file{};
    ob::ObjectHeader* fileReference{};
    ke::KThread* thread{};
    void* process{};
    const void* statusBlock{};
    bool cancelled{};
};
PendingRead* pendingReads{};
AsyncRead* asyncReads{};
AsyncRead* completedReads{};
ke::passive_work::Item asyncCompletionWork{};
void finishAsyncReads(void*) noexcept;
void finishOwnedIrp(Irp*, void*) noexcept;
void freeOwnedIrp(Irp*, void*) noexcept;
void cancelOwnedRead(DeviceObject*, Irp* irp) {
    auto* request = reinterpret_cast<AsyncRead*>(irp);
    const auto previousIrql = irp->cancelIrql;
    IoReleaseCancelSpinLock(previousIrql);
    if (request && request->file && request->file->cancelReadRoutine())
        (void)request->file->cancelReadRoutine()(request->file->context(), request);
}
void cleanupAsyncRead(AsyncRead* request) noexcept {
#if defined(MICANT_BAREMETAL)
    if (request->completionPacket) delete static_cast<IoCompletionPort::Node*>(request->completionPacket);
#endif
    if (request->userApc) delete request->userApc;
    if (request->threadReference) ob::ObDereferenceObject(request->threadReference);
    if (request->completionReference) ob::ObDereferenceObject(request->completionReference);
    if (request->eventReference) ob::ObDereferenceObject(request->eventReference);
    if (request->processReference) ob::ObDereferenceObject(request->processReference);
    if (request->fileReference) ob::ObDereferenceObject(request->fileReference);
    delete[] request->buffer;
    delete request;
}
uint64_t disableReadInterrupts() noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
void restoreReadInterrupts(uint64_t flags) noexcept {
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
}
ob::ObjectHeader* deferredFileDeletes{};
ke::passive_work::Item fileDeleteWork{};
void finishDeferredFiles(void*) noexcept {
    for (;;) {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        auto* header = deferredFileDeletes;
        if (header) deferredFileDeletes = header->deferredDeleteNext;
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        if (!header) return;
        header->deferredDeleteNext = nullptr;
        ob::ObFinishDelete(header);
    }
}
bool deferFileDelete(ob::ObjectHeader* header) noexcept {
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const bool schedulable = (flags & (uint64_t{1} << 9)) != 0;
    if (ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL && !ke::g_CurrentCpu.dpcRoutineActive &&
        (schedulable || !hal::amd64::bootstrapSchedulerClockActive())) {
        if (schedulable) __asm__ __volatile__("sti" : : : "memory");
        return false;
    }
    header->deferredDeleteNext = deferredFileDeletes;
    deferredFileDeletes = header;
    fileDeleteWork.routine = finishDeferredFiles;
    if (ke::passive_work::detail::initialized && !fileDeleteWork.queued &&
        !ke::passive_work::queue(fileDeleteWork)) __builtin_trap();
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return true;
}
#endif
}

#if defined(MICANT_FREESTANDING)
bool IoDeferPassiveObjectDelete(ob::ObjectHeader* header) noexcept {
    return deferFileDelete(header);
}
#endif

void IoScheduleDeferredFileDeletes() noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (deferredFileDeletes && ke::passive_work::detail::initialized && !fileDeleteWork.queued &&
        !ke::passive_work::queue(fileDeleteWork)) __builtin_trap();
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
}

bool IoDeferredFileDeletesPending() noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const bool pending = deferredFileDeletes || fileDeleteWork.queued || fileDeleteWork.running;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return pending;
#else
    return false;
#endif
}

NtStatus FileObject::associateCompletion(ob::ObjectHeader* port, uintptr_t key) noexcept {
    if (!port || backend_.synchronous) return NtStatus::InvalidParameter;
#if defined(MICANT_FREESTANDING)
    ke::SpinLockGuard guard(completionLock_);
#else
    std::lock_guard<std::mutex> guard(completionLock_);
#endif
    if (completionReference_) return NtStatus::InvalidParameter;
    port->addReference(); completionKey_ = key; completionReference_ = port;
    return NtStatus::Success;
}
ob::ObjectHeader* FileObject::referenceCompletion(uintptr_t& key) noexcept {
#if defined(MICANT_FREESTANDING)
    ke::SpinLockGuard guard(completionLock_);
#else
    std::lock_guard<std::mutex> guard(completionLock_);
#endif
    auto* reference = completionReference_;
    if (reference) { reference->addReference(); key = completionKey_; }
    return reference;
}
NtStatus IoAssociateCompletionPort(Handle fileHandle, Handle portHandle, uintptr_t key) noexcept {
    FileObject* file{}; ob::ObjectHeader* fileReference{};
    auto status = IoReferenceFileHandle(fileHandle, 0, file, fileReference);
    if (status != NtStatus::Success) return status;
    ob::HandleTable* table{}; Handle raw{}; ob::ObjectHeader* portReference{};
    status = ob::detail::resolveHandle(portHandle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, 2, &g_completionType, &portReference);
    if (status == NtStatus::Success) {
        status = file->associateCompletion(portReference, key);
        ob::ObDereferenceObject(portReference);
    }
    ob::ObDereferenceObject(fileReference);
    return status;
}
NtStatus IoHasCompletionPort(Handle fileHandle, bool& associated) noexcept {
    associated = false;
    FileObject* file{}; ob::ObjectHeader* reference{};
    auto status = IoReferenceFileHandle(fileHandle, 1, file, reference);
    if (status != NtStatus::Success) return status;
    uintptr_t key{}; auto* port = file->referenceCompletion(key);
    associated = port != nullptr;
    if (port) ob::ObDereferenceObject(port);
    ob::ObDereferenceObject(reference);
    return NtStatus::Success;
}

NtStatus IoInitializeFileObjects() noexcept {
    g_completionType.typeId = ob::ObjectTypeId::IoCompletion;
    g_completionType.validAccessMask = 0x001F0003;
    g_completionType.genericMapping = {0x00120001, 0x00120002, 0x00120000, 0x001F0003};
    g_completionType.deleteProcedure = deleteCompletionPort;
    g_fileType.typeId = ob::ObjectTypeId::File;
    g_fileType.validAccessMask = 0x001F01FF;
    g_fileType.genericMapping = {0x00120089, 0x00120116, 0x001200A0, 0x001F01FF};
    g_fileType.deleteProcedure = deleteFileObject;
    g_fileType.closeProcedure = closeFileHandle;
#if defined(MICANT_FREESTANDING)
    g_fileType.deferDelete = deferFileDelete;
    g_completionType.deferDelete = deferFileDelete;
#endif
    return NtStatus::Success;
}

NtStatus IoCreateCompletionPort(Handle* handle, uint32_t access, uint32_t concurrency) noexcept {
    if (!handle) return NtStatus::InvalidParameter;
    const auto mapped = ob::MapGenericAccess(access, g_completionType.genericMapping);
    if (mapped & ~g_completionType.validAccessMask) return NtStatus::AccessDenied;
    // Scheduling concurrency is not yet implemented beyond the single BSP.
    if (concurrency > 1) return NtStatus::NotSupported;
    void* body{}; ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &g_completionType, nullptr,
                                     sizeof(IoCompletionPort), &body, &header);
    if (status != NtStatus::Success) return status;
    new (body) IoCompletionPort(concurrency);
    status = ob::detail::currentHandleTable().createHandle(header, *handle, mapped);
    ob::ObDereferenceObject(header);
    return status;
}
NtStatus IoPostCompletion(Handle handle, uintptr_t key, uintptr_t context,
                          NtStatus result, uintptr_t information) noexcept {
    ob::HandleTable* table{}; Handle raw{}; ob::ObjectHeader* reference{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, 2, &g_completionType, &reference);
    if (status != NtStatus::Success) return status;
    status = reference->as<IoCompletionPort>()->postCompletion(key, context, result, information);
    ob::ObDereferenceObject(reference);
    return status;
}
NtStatus IoRemoveCompletion(Handle handle, uintptr_t& key, uintptr_t& context,
                            IoStatusBlock& result, uint64_t timeout100ns) noexcept {
    ob::HandleTable* table{}; Handle raw{}; ob::ObjectHeader* reference{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, 1, &g_completionType, &reference);
    if (status != NtStatus::Success) return status;
    uint64_t completionKey{};
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    auto* thread = ke::KeGetCurrentThread();
    if (timeout100ns && (!thread || thread->eventWaitInProgress || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)) {
        ob::ObDereferenceObject(reference); return NtStatus::InvalidParameter;
    }
    if (timeout100ns) {
        thread->eventWaitInProgress = true;
        thread->eventWaitUserMode = thread->previousUserMode;
        // Kernel callers may retain unrelated resources across this wait.
        // Only a user syscall has the return boundary that can service a
        // pending termination after the completion-port reference is dropped.
        thread->eventWaitCanTerminate = thread->eventWaitUserMode;
    }
#endif
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    status = reference->as<IoCompletionPort>()->removeCompletionTimed(completionKey, context, result, timeout100ns);
#else
    if (timeout100ns != 0 && timeout100ns != UINT64_MAX) status = NtStatus::NotSupported;
    else status = reference->as<IoCompletionPort>()->removeCompletion(completionKey, context, result,
                                                                     timeout100ns ? 0xffffffffu : 0);
#endif
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    if (timeout100ns) {
        thread->eventWaitInProgress = false;
        thread->eventWaitUserMode = false;
        thread->eventWaitCanTerminate = false;
    }
#endif
    key = static_cast<uintptr_t>(completionKey);
    ob::ObDereferenceObject(reference);
    return status;
}

NtStatus IoRegisterFileOpenRoutine(FileOpenRoutine open, void* context) noexcept {
    if (!open) return NtStatus::InvalidParameter;
    lockFileBackend();
    if (g_fileOpen) { unlockFileBackend(); return NtStatus::Conflict; }
    g_fileOpenContext = context;
    g_fileOpen = open;
    unlockFileBackend();
    return NtStatus::Success;
}

NtStatus IoUnregisterFileOpenRoutine(FileOpenRoutine open, void* context) noexcept {
    if (!open) return NtStatus::InvalidParameter;
    lockFileBackend();
    if (g_fileOpen != open || g_fileOpenContext != context) {
        unlockFileBackend();
        return NtStatus::ObjectNameNotFound;
    }
    g_fileOpen = nullptr;
    g_fileOpenContext = nullptr;
    unlockFileBackend();
    return NtStatus::Success;
}

NtStatus IoRegisterSystemDirectoryRoutine(FileSystemDirectoryRoutine query, void* context) noexcept {
    if (!query) return NtStatus::InvalidParameter;
    lockFileBackend();
    if (g_systemDirectory) { unlockFileBackend(); return NtStatus::Conflict; }
    g_systemDirectory = query;
    g_systemDirectoryContext = context;
    unlockFileBackend();
    return NtStatus::Success;
}

NtStatus IoQuerySystemDirectory(char16_t* output, size_t capacity, size_t& length) noexcept {
    length = 0;
    if (!output || !capacity) return NtStatus::InvalidParameter;
    lockFileBackend();
    const auto query = g_systemDirectory;
    auto* context = g_systemDirectoryContext;
    unlockFileBackend();
    return query ? query(context, output, capacity, length) : NtStatus::DeviceNotReady;
}

NtStatus IoQueryDirectoryFile(Handle fileHandle, bool restartScan,
                              FileBothDirInformation* output) noexcept {
    if (!output) return NtStatus::InvalidParameter;
    FileObject* file{};
    ob::ObjectHeader* reference{};
    auto status = IoReferenceFileHandle(fileHandle, 0x0001u, file, reference);
    if (status != NtStatus::Success) return status;
    if (!file->isDirectory()) status = NtStatus::NotADirectory;
    else {
        if (restartScan) file->setDirectoryQueryIndex(0);
        const size_t index = file->directoryQueryIndex();
        status = file->queryDirectory(index, output);
        if (status == NtStatus::Success) file->setDirectoryQueryIndex(index + 1);
    }
    ob::ObDereferenceObject(reference);
    return status;
}

NtStatus IoOpenBackendFile(Handle* fileHandle, uint32_t desiredAccess,
                           std::u16string_view name, bool openIf,
                           bool* created, bool deferCreate,
                           uint32_t createOptions) noexcept {
    if (created) *created = false;
    if (!fileHandle || name.empty()) return NtStatus::InvalidParameter;
    *fileHandle = 0;
    FileOpenRoutine open{};
    void* context{};
    lockFileBackend();
    open = g_fileOpen;
    context = g_fileOpenContext;
    unlockFileBackend();
    if (!open) return NtStatus::NoSuchDevice;
    FileBackend backend{};
    const uint32_t mappedAccess = ob::MapGenericAccess(desiredAccess, g_fileType.genericMapping);
    auto status = open(context, name, openIf, mappedAccess, backend);
    if (status != NtStatus::Success) return status;
    constexpr uint32_t fileDirectoryFile = 0x0001u;
    constexpr uint32_t fileNonDirectoryFile = 0x0040u;
    if ((createOptions & fileDirectoryFile) &&
        (createOptions & fileNonDirectoryFile)) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::InvalidParameter;
    }
    if ((createOptions & fileDirectoryFile) && !backend.directory) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::NotADirectory;
    }
    if ((createOptions & fileNonDirectoryFile) && backend.directory) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::FileIsADirectory;
    }
    if (!backend.name.size()) backend.name = name;
    if (!backend.read || !backend.release ||
        (backend.created && !backend.commitCreate)) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::InvalidParameter;
    }
    if (!fileBackendAccessAllowed(backend.securityDescriptor, mappedAccess)) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::AccessDenied;
    }
    status = IoCreateBackendFile(fileHandle, desiredAccess, backend);
    if (status == NtStatus::Success && backend.created && !deferCreate) {
        status = IoCommitCreatedFile(*fileHandle);
        if (status != NtStatus::Success) {
            ob::HandleTable* table{}; Handle raw{};
            if (ob::detail::resolveHandle(*fileHandle, table, raw) != NtStatus::Success ||
                table->closeHandle(raw) != NtStatus::Success) __builtin_trap();
            *fileHandle = 0;
        }
    }
    if (status == NtStatus::Success && created) *created = backend.created;
    return status;
}

NtStatus IoCreateBackendFile(Handle* fileHandle, uint32_t desiredAccess,
                             const FileBackend& backend) noexcept {
    constexpr uint32_t fileReadOnlyAccess = 0x001200A9; // FILE_GENERIC_READ | FILE_EXECUTE
    constexpr uint32_t fileWriteData = 0x00000002;
    const uint32_t mappedAccess = ob::MapGenericAccess(desiredAccess, g_fileType.genericMapping);
    if (backend.securityDescriptor && !fileBackendAccessAllowed(backend.securityDescriptor, mappedAccess)) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::AccessDenied;
    }
    if (!fileHandle || !backend.read ||
        (backend.created && !backend.commitCreate) ||
        (mappedAccess & ~g_fileType.validAccessMask) ||
        (mappedAccess & ~(fileReadOnlyAccess | fileWriteData)) ||
        ((mappedAccess & fileWriteData) && !backend.write)) {
        if (backend.release) backend.release(backend.context);
        return (mappedAccess & fileWriteData) && !backend.write
            ? NtStatus::MediaWriteProtected : NtStatus::InvalidParameter;
    }
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &g_fileType, nullptr,
                                     sizeof(FileObject), &body, &header);
    if (status != NtStatus::Success) {
        if (backend.release) backend.release(backend.context);
        return status;
    }
    auto* created = new (body) FileObject(backend);
    status = created->initialize();
    if (status != NtStatus::Success) {
        ob::ObDereferenceObject(header);
        return status;
    }
    status = ob::detail::currentHandleTable().createHandle(header, *fileHandle, mappedAccess);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus IoCreateDeviceFile(Handle* fileHandle, uint32_t desiredAccess,
                            DeviceObject* deviceObject, const FileBackend& backend) noexcept {
    const uint32_t mappedAccess = ob::MapGenericAccess(desiredAccess, g_fileType.genericMapping);
    if (!fileHandle || !deviceObject || !backend.context || !backend.release ||
        (mappedAccess & ~g_fileType.validAccessMask) ||
        (mappedAccess & ~0x0012008bu)) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::InvalidParameter;
    }
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &g_fileType, nullptr,
                                     sizeof(FileObject), &body, &header);
    if (status != NtStatus::Success) {
        if (backend.release) backend.release(backend.context);
        return status;
    }
    auto* created = new (body) FileObject(backend, deviceObject);
    status = created->initialize();
    if (status != NtStatus::Success) {
        ob::ObDereferenceObject(header);
        return status;
    }
    status = ob::detail::currentHandleTable().createHandle(header, *fileHandle, mappedAccess);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus IoReferenceFileHandle(Handle fileHandle, uint32_t desiredAccess,
                               FileObject*& file, ob::ObjectHeader*& reference) noexcept {
    file = nullptr;
    reference = nullptr;
    ob::HandleTable* table{};
    Handle raw{};
    auto status = ob::detail::resolveHandle(fileHandle, table, raw);
    if (status != NtStatus::Success) return status;
    status = ob::ObReferenceObjectByHandle(raw, *table, desiredAccess, &g_fileType, &reference);
    if (status == NtStatus::Success) file = reference->as<FileObject>();
    return status;
}

NtStatus IoCommitCreatedFile(Handle fileHandle) noexcept {
    FileObject* file{}; ob::ObjectHeader* reference{};
    const auto status = IoReferenceFileHandle(fileHandle, 0, file, reference);
    if (status != NtStatus::Success) return status;
    const auto result = file->commitCreate();
    ob::ObDereferenceObject(reference);
    return result;
}

NtStatus IoReadFileHandle(Handle fileHandle, bool useCurrentOffset, uint64_t offset,
                          void* buffer, size_t length, size_t* bytesRead, const void* requestStatus) noexcept {
    if (!bytesRead || (!buffer && length)) return NtStatus::InvalidParameter;
    *bytesRead = 0;
    FileObject* file{};
    ob::ObjectHeader* reference{};
    auto status = IoReferenceFileHandle(fileHandle, 0x0001, file, reference);
    if (status != NtStatus::Success) return status;
    if (file->isStream() && !useCurrentOffset) {
        ob::ObDereferenceObject(reference);
        return NtStatus::InvalidParameter;
    }
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    auto* current = file->isStream() ? ke::KeGetCurrentThread() : nullptr;
    if (file->isStream()) {
        if (!current || current->eventWaitInProgress || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) {
            ob::ObDereferenceObject(reference);
            return NtStatus::InvalidParameter;
        }
        current->eventWaitInProgress = true;
        current->eventWaitUserMode = current->previousUserMode;
        // Only a backend that exposes the cancellation/wake contract can be
        // interrupted here. Other stream waits must unwind normally.
        current->eventWaitCanTerminate = current->eventWaitUserMode && file->canCancelRead();
    }
#endif
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    PendingRead request{};
    if (current) {
        request.file = file;
        request.fileReference = reference;
        request.thread = current;
        request.process = ke::KeGetCurrentProcess();
        request.statusBlock = requestStatus;
        const auto flags = disableReadInterrupts();
        auto* process = static_cast<ps::EProcess*>(request.process);
        if (!process || process->isTerminating() || current->terminationRequested) {
            current->eventWaitInProgress = false;
            current->eventWaitUserMode = false;
            current->eventWaitCanTerminate = false;
            restoreReadInterrupts(flags);
            ob::ObDereferenceObject(reference);
            return process && process->isTerminating() ? NtStatus::ProcessIsTerminating : NtStatus::ThreadIsTerminating;
        }
        request.next = pendingReads; pendingReads = &request;
        restoreReadInterrupts(flags);
    }
#endif
    status = useCurrentOffset ? file->readCurrent(buffer, length, bytesRead)
                              : file->read(offset, buffer, length, bytesRead);
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    if (current) {
        const auto flags = disableReadInterrupts();
        auto** link = &pendingReads;
        while (*link && *link != &request) link = &(*link)->next;
        if (*link) *link = request.next;
        restoreReadInterrupts(flags);
        current->eventWaitInProgress = false;
        current->eventWaitUserMode = false;
        current->eventWaitCanTerminate = false;
    }
#endif
    ob::ObDereferenceObject(reference);
    return status;
}

NtStatus IoWriteFileHandle(Handle fileHandle, bool useCurrentOffset, uint64_t offset,
                           const void* buffer, size_t length, size_t* bytesWritten) noexcept {
    if (!bytesWritten || (!buffer && length)) return NtStatus::InvalidParameter;
    *bytesWritten = 0;
    FileObject* file{};
    ob::ObjectHeader* reference{};
    auto status = IoReferenceFileHandle(fileHandle, 0x0002, file, reference);
    if (status != NtStatus::Success) return status;
    if (file->isStream() && !useCurrentOffset) {
        ob::ObDereferenceObject(reference);
        return NtStatus::InvalidParameter;
    }
    status = useCurrentOffset ? file->writeCurrent(buffer, length, bytesWritten)
                              : file->write(offset, buffer, length, bytesWritten);
    ob::ObDereferenceObject(reference);
    return status;
}

NtStatus IoFlushBuffersFile(Handle fileHandle) noexcept {
    FileObject* file{};
    ob::ObjectHeader* reference{};
    const NtStatus status = IoReferenceFileHandle(fileHandle, 0x0002, file, reference);
    if (status != NtStatus::Success) return status;
    const NtStatus flushStatus = file->flush();
    ob::ObDereferenceObject(reference);
    return flushStatus;
}

#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
namespace {
void finishAsyncReads(void*) noexcept {
    for (;;) {
        const auto flags = disableReadInterrupts();
        auto** link = &completedReads;
        while (*link && (*link)->cancellationPins) link = &(*link)->completedNext;
        auto* request = *link;
        bool needsUnwind = false;
        if (request) {
            *link = request->completedNext;
            request->completedNext = nullptr;
            request->completionQueued = false;
            needsUnwind = !request->completionUnwound;
            if (needsUnwind) ++request->cancellationPins;
        }
        restoreReadInterrupts(flags);
        if (!request) return;
        if (needsUnwind) {
            request->irp.ioStatus = request->result;
            // The owner hook queues finalization after the last completion
            // frame, including a later driver call after MORE_PROCESSING_REQUIRED.
            (void)IoCompleteRequest(&request->irp);
            const auto releaseFlags = disableReadInterrupts();
            if (!request->cancellationPins) __builtin_trap();
            --request->cancellationPins;
            if (request->completionQueued && !request->cancellationPins && !asyncCompletionWork.queued &&
                !ke::passive_work::queue(asyncCompletionWork)) __builtin_trap();
            restoreReadInterrupts(releaseFlags);
            continue;
        }
        auto* process = static_cast<ps::EProcess*>(request->process);
        auto* thread = ke::KeGetCurrentThread();
        const auto depth = thread ? thread->processAttachDepth : 0;
        ke::KAPC_STATE state{};
        ke::KeStackAttachProcess(process, &state);
        const bool attached = thread && thread->processAttachDepth == depth + 1 &&
                              ke::KeGetCurrentProcess() == process;
        if (!attached) {
            request->result = {NtStatus::Cancelled, 0};
        } else {
            // The bootstrap probes require the address space to stay stable
            // between probing and copy. The scheduler is BSP-only.
            const auto copyFlags = disableReadInterrupts();
            if (request->result.information &&
                ex::ExSafeCopyToUser(request->userBuffer, request->buffer,
                                     request->result.information) != NtStatus::Success) {
                request->result.status = NtStatus::AccessViolation;
                request->result.information = 0;
            }
            (void)ex::ExSafeCopyToUser(request->userStatus, &request->result, sizeof(request->result));
            restoreReadInterrupts(copyFlags);
            ke::KeUnstackDetachProcess(&state);
        }
        if (request->eventReference)
            (void)request->eventReference->as<sync::detail::BootstrapEvent>()->set();
        if (attached && request->userApc && request->threadReference) {
            const auto apcFlags = disableReadInterrupts();
            auto* target = request->threadReference->as<ps::EThread>()->kernelThread();
            ke::KeInitializeApc(request->userApc, target, ke::ApcEnvironment::OriginalApcEnvironment,
                ke::passive_work::ownedUserApcKernel, ke::passive_work::ownedUserApcRundown,
                reinterpret_cast<ke::PKNORMAL_ROUTINE>(request->userApcRoutine), ke::UserMode,
                reinterpret_cast<void*>(request->userApcContext));
            if (ke::KeInsertQueueApc(request->userApc, request->userStatus, nullptr))
                request->userApc = nullptr; // Ke now owns the queued APC.
            restoreReadInterrupts(apcFlags);
        }
        if (request->completionPacket) {
            auto* packet = static_cast<IoCompletionPort::Node*>(request->completionPacket);
            request->completionPacket = nullptr;
            request->completionReference->as<IoCompletionPort>()->postReservedCompletion(
                packet, request->completionKey, request->completionContext,
                request->result.status, request->result.information);
        }
        cleanupAsyncRead(request);
    }
}
}
#endif

#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
namespace {
void queueOwnedIrpFinalization(AsyncRead* request, bool freedByDriver) noexcept {
    if (!request) return;
    const auto flags = disableReadInterrupts();
    if (!request->completionUnwound) {
        if (request->pending) {
            auto** pending = &asyncReads;
            while (*pending && *pending != request) pending = &(*pending)->pendingNext;
            if (*pending) *pending = request->pendingNext;
            request->pending = false;
            ke::KIRQL oldIrql{};
            IoAcquireCancelSpinLock(&oldIrql);
            (void)IoSetCancelRoutine(&request->irp, nullptr);
            IoReleaseCancelSpinLock(oldIrql);
        }
        if (freedByDriver) {
            request->irp.ownerComplete = nullptr;
            request->result = {NtStatus::Cancelled, 0};
        } else {
            request->result = request->irp.ioStatus;
            if (request->result.information > request->length)
                request->result = {NtStatus::InvalidParameter, 0};
        }
        request->completionUnwound = true;
        if (!request->completionQueued) {
            request->completedNext = completedReads;
            completedReads = request;
            request->completionQueued = true;
        }
        asyncCompletionWork.routine = finishAsyncReads;
        if (!asyncCompletionWork.queued && !ke::passive_work::queue(asyncCompletionWork)) __builtin_trap();
    }
    restoreReadInterrupts(flags);
}

void finishOwnedIrp(Irp*, void* context) noexcept {
    queueOwnedIrpFinalization(static_cast<AsyncRead*>(context), false);
}

void freeOwnedIrp(Irp*, void* context) noexcept {
    // A driver that frees its owned IRP stops the operation; Io still owns
    // the enclosing allocation and performs the notification/cleanup.
    queueOwnedIrpFinalization(static_cast<AsyncRead*>(context), true);
}
}
#endif

void IoCompleteAsyncRead(AsyncRead* request, NtStatus status, size_t information) noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    if (!request) return;
    const auto flags = disableReadInterrupts();
    if (!request->pending) { restoreReadInterrupts(flags); return; }
    auto** link = &asyncReads;
    while (*link && *link != request) link = &(*link)->pendingNext;
    if (*link != request) { restoreReadInterrupts(flags); return; }
    *link = request->pendingNext;
    request->pending = false;
    ke::KIRQL oldIrql{};
    IoAcquireCancelSpinLock(&oldIrql);
    (void)IoSetCancelRoutine(&request->irp, nullptr);
    IoReleaseCancelSpinLock(oldIrql);
    if (information > request->length) { status = NtStatus::InvalidParameter; information = 0; }
    request->result = {status, information};
    request->completedNext = completedReads; completedReads = request;
    request->completionQueued = true;
    asyncCompletionWork.routine = finishAsyncReads;
    if (!asyncCompletionWork.queued && !ke::passive_work::queue(asyncCompletionWork)) __builtin_trap();
    restoreReadInterrupts(flags);
#else
    (void)request; (void)status; (void)information;
#endif
}

NtStatus IoSubmitAsyncRead(Handle handle, Handle event, uintptr_t apcRoutine, uintptr_t completionContext,
                           void* userBuffer, size_t length, IoStatusBlock* userStatus) noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    if (!userStatus || (!userBuffer && length) || length > UINT32_MAX ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    // Completion and cancellation callbacks require the passive worker and
    // a schedulable context; do not create an IRP in early bootstrap where
    // neither can be driven safely.
    if (!hal::amd64::bootstrapSchedulerClockActive() || !ke::passive_work::isInitialized())
        return NtStatus::NotSupported;
    struct SchedulableWindow {
        bool restoreDisabled{};
        SchedulableWindow() noexcept {
            uint64_t flags{};
            __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
            restoreDisabled = !(flags & (uint64_t{1} << 9));
            if (restoreDisabled) __asm__ __volatile__("sti" : : : "memory");
        }
        ~SchedulableWindow() noexcept {
            if (restoreDisabled) __asm__ __volatile__("cli" : : : "memory");
        }
    } schedulable;
    auto* request = new (std::nothrow_t{}) AsyncRead{};
    if (!request) return NtStatus::NoMemory;
    auto status = IoReferenceFileHandle(handle, 1, request->file, request->fileReference);
    if (status != NtStatus::Success) { cleanupAsyncRead(request); return status; }
    if (request->file->isSynchronous() || !request->file->submitReadRoutine() || !request->file->cancelReadRoutine()) {
        cleanupAsyncRead(request); return NtStatus::NotSupported;
    }
    ps::EProcess* process{};
    status = ps::detail::referenceCurrentProcess(process, request->processReference);
    if (status != NtStatus::Success) { cleanupAsyncRead(request); return status; }
    request->process = process;
    // Every asynchronous read belongs to its issuing thread, even if it has
    // no APC. Keep the object, rather than a borrowed KThread, until driver
    // completion so termination can cancel it safely after thread reap.
    ps::EThread* issuingThreadObject{};
    status = ps::detail::referenceCurrentThread(issuingThreadObject, request->threadReference);
    if (status != NtStatus::Success) { cleanupAsyncRead(request); return status; }
    if (event) {
        status = sys::referenceBootstrapEventForIo(event, request->eventReference);
        if (status != NtStatus::Success) { cleanupAsyncRead(request); return status; }
    }
    request->completionReference = request->file->referenceCompletion(request->completionKey);
    request->completionContext = completionContext;
    if (apcRoutine) {
        if (request->completionReference) { cleanupAsyncRead(request); return NtStatus::InvalidParameter; }
        // The normal APC below targets the original user APC environment.
        // An attached kernel thread cannot publish pointers from the attached
        // address space into that original environment.
        auto* issuingThread = ke::KeGetCurrentThread();
        if (!issuingThread || issuingThread->owningProcess != process ||
            issuingThread->apcStateIndex != ke::ApcStateIndexOriginal) {
            cleanupAsyncRead(request); return NtStatus::NotSupported;
        }
        // User APC code can be unmapped or changed after submission; let the
        // user exception dispatcher handle an invalid callback on delivery.
        request->userApc = new (std::nothrow_t{}) ke::KAPC{};
        if (!request->userApc) { cleanupAsyncRead(request); return NtStatus::NoMemory; }
        request->userApcRoutine = apcRoutine; request->userApcContext = completionContext;
    }
    if (request->completionReference && completionContext) {
        request->completionPacket = IoCompletionPort::reserveCompletion();
        if (!request->completionPacket) { cleanupAsyncRead(request); return NtStatus::NoMemory; }
    }
    if (!request->eventReference && !request->completionPacket && !request->userApc) {
        cleanupAsyncRead(request); return NtStatus::InvalidParameter;
    }
    request->length = length; request->userBuffer = userBuffer; request->userStatus = userStatus;
    request->irp.majorFunction = IRP_MJ_READ;
    request->irp.fileObject = request->file;
    request->irp.ownerComplete = finishOwnedIrp;
    request->irp.ownerFree = freeOwnedIrp;
    request->irp.ownerContext = request;
    request->irp.deviceObject = request->file->deviceObject();
    request->irp.length = static_cast<uint32_t>(length);
    if (length) {
        request->buffer = new (std::nothrow_t{}) uint8_t[length];
        if (!request->buffer) { cleanupAsyncRead(request); return NtStatus::NoMemory; }
        request->irp.systemBuffer = request->buffer;
    }
    const IoStatusBlock initial{NtStatus::Pending, 0};
    const auto flags = disableReadInterrupts();
    auto* issuingKernelThread = issuingThreadObject->kernelThread();
    if (!issuingKernelThread || issuingKernelThread->terminationRequested || process->isTerminating()) {
        const auto failure = process->isTerminating() ? NtStatus::ProcessIsTerminating : NtStatus::ThreadIsTerminating;
        restoreReadInterrupts(flags);
        cleanupAsyncRead(request);
        return failure;
    }
    if (ex::ExSafeCopyToUser(userStatus, &initial, sizeof(initial)) != NtStatus::Success) {
        restoreReadInterrupts(flags); cleanupAsyncRead(request); return NtStatus::AccessViolation;
    }
    if (request->eventReference)
        (void)request->eventReference->as<sync::detail::BootstrapEvent>()->reset();
    IoMarkIrpPending(&request->irp);
    request->pending = true; request->pendingNext = asyncReads; asyncReads = request;
    request->submitting = true;
    ++request->cancellationPins; // submit callback may complete the IRP inline
    ke::KIRQL cancelIrql{};
    IoAcquireCancelSpinLock(&cancelIrql);
    (void)IoSetCancelRoutine(&request->irp, cancelOwnedRead);
    IoReleaseCancelSpinLock(cancelIrql);
    restoreReadInterrupts(flags);
    // The syscall entry may have IF clear. GS/segments are already in the
    // kernel convention, and the clock/worker were checked above.
    if (!(flags & (uint64_t{1} << 9))) __asm__ __volatile__("sti" : : : "memory");
    status = request->file->submitReadRoutine()(request->file->context(), request);
    __asm__ __volatile__("cli" : : : "memory");
    request->submitting = false;
    bool immediateFailure = false;
    if (status != NtStatus::Pending && request->pending) {
        // A backend reporting success must also have completed the request;
        // otherwise there is no valid transfer result to publish.
        if (NT_SUCCESS(status)) status = NtStatus::InvalidDeviceState;
        auto** link = &asyncReads;
        while (*link && *link != request) link = &(*link)->pendingNext;
        if (*link) *link = request->pendingNext;
        request->pending = false;
        IoAcquireCancelSpinLock(&cancelIrql);
        (void)IoSetCancelRoutine(&request->irp, nullptr);
        IoReleaseCancelSpinLock(cancelIrql);
        immediateFailure = true;
    }
    const bool cancelAfterSubmit = request->cancelAfterSubmit && request->pending;
    if (cancelAfterSubmit) {
        __asm__ __volatile__("sti" : : : "memory");
        (void)IoCancelIrp(&request->irp);
        __asm__ __volatile__("cli" : : : "memory");
    }
    if (!request->cancellationPins) __builtin_trap();
    --request->cancellationPins;
    if (request->completionQueued && !request->cancellationPins && !asyncCompletionWork.queued &&
        !ke::passive_work::queue(asyncCompletionWork)) __builtin_trap();
    if (immediateFailure) {
        const IoStatusBlock failure{status, 0};
        if (ex::ExSafeCopyToUser(userStatus, &failure, sizeof(failure)) != NtStatus::Success)
            status = NtStatus::AccessViolation;
    }
    restoreReadInterrupts(flags);
    if (immediateFailure) {
        const bool temporaryInterrupts = !(flags & (uint64_t{1} << 9));
        if (temporaryInterrupts) __asm__ __volatile__("sti" : : : "memory");
        if (request->eventReference)
            (void)request->eventReference->as<sync::detail::BootstrapEvent>()->set();
        cleanupAsyncRead(request);
        if (temporaryInterrupts) __asm__ __volatile__("cli" : : : "memory");
        return status;
    }
    return NtStatus::Pending;
#else
    (void)handle; (void)event; (void)apcRoutine; (void)completionContext; (void)userBuffer; (void)length; (void)userStatus;
    return NtStatus::NotSupported;
#endif
}

bool IoCurrentReadCancelled() noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    const auto flags = disableReadInterrupts();
    auto* thread = ke::KeGetCurrentThread();
    bool cancelled = false;
    for (auto* request = pendingReads; request; request = request->next)
        if (request->thread == thread) { cancelled = request->cancelled; break; }
    restoreReadInterrupts(flags);
    return cancelled;
#else
    return false;
#endif
}

namespace {
bool cancelOwnedReads(ps::EThread* ownerThread, ps::EProcess* ownerProcess,
                      ps::EThread* exceptThread, bool onlyTerminating,
                      FileObject* targetFile, const void* statusBlock) noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    bool found = false;
    // Syscall entry can reach Ps with IF clear. Once the scheduler clock is
    // active, allow backend callbacks to block and restore the caller's IF
    // on exit. Before scheduler startup there are no schedulable I/O workers.
    uint64_t entryFlags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(entryFlags));
    const bool enableInterrupts = !(entryFlags & (uint64_t{1} << 9)) &&
        ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL && hal::amd64::bootstrapSchedulerClockActive();
    if (enableInterrupts) __asm__ __volatile__("sti" : : : "memory");
    // Each synchronous request lives on its issuing thread's stack and pins
    // the file. Mark one at a time under BSP exclusion, then invoke the
    // backend wake callback without holding a spinlock or borrowing the
    // request after its thread can resume.
    for (;;) {
        FileObject* file{};
        ob::ObjectHeader* reference{};
        const auto flags = disableReadInterrupts();
        auto* target = ownerThread ? ownerThread->kernelThread() : nullptr;
        auto* excluded = exceptThread ? exceptThread->kernelThread() : nullptr;
        for (auto* request = pendingReads; request; request = request->next) {
            if (request->cancelled || !request->file->canCancelRead() ||
                (ownerThread && request->thread != target) ||
                (exceptThread && request->thread == excluded) ||
                (onlyTerminating && !request->thread->terminationRequested) ||
                (ownerProcess && request->process != ownerProcess) ||
                (targetFile && request->file != targetFile) ||
                (statusBlock && request->statusBlock != statusBlock)) continue;
            request->cancelled = true;
            found = true;
            file = request->file;
            reference = request->fileReference;
            if (!reference) __builtin_trap();
            reference->addReference();
            break;
        }
        restoreReadInterrupts(flags);
        if (!file) break;
        file->wakeCancelledRead();
        ob::ObDereferenceObject(reference);
    }

    // Pin each request under BSP exclusion, then drop exclusion before
    // calling the driver. Completion can race the cancellation, but the
    // completion worker will not hand a pinned IRP to a completion routine
    // that might free it. The backend owns completion, never this loop.
    for (;;) {
        const auto flags = disableReadInterrupts();
        AsyncRead* selected{};
        for (auto* request = asyncReads; request; request = request->pendingNext) {
            const bool matchesThread = !ownerThread || (request->threadReference &&
                request->threadReference->as<ps::EThread>() == ownerThread);
            auto* requestOwner = request->threadReference
                ? request->threadReference->as<ps::EThread>() : nullptr;
            auto* requestKernelThread = requestOwner ? requestOwner->kernelThread() : nullptr;
            if (!request->pending || request->cancelDispatched || !matchesThread ||
                (exceptThread && requestOwner == exceptThread) ||
                (onlyTerminating && requestKernelThread && !requestKernelThread->terminationRequested) ||
                (ownerProcess && request->process != ownerProcess) ||
                (targetFile && request->file != targetFile) ||
                (statusBlock && request->userStatus != statusBlock)) continue;
            request->cancelDispatched = true;
            if (request->submitting) {
                // The backend has not yet finished publishing its queue
                // entry. The submitting thread retains its pin and issues
                // cancellation after submit returns.
                request->cancelAfterSubmit = true;
                found = true;
                continue;
            }
            ++request->cancellationPins;
            selected = request;
            break;
        }
        restoreReadInterrupts(flags);
        if (!selected) break;
        // IoCancelIrp owns the cancel spinlock only until cancelOwnedRead
        // releases it; the backend callback itself runs without that lock.
        if (IoCancelIrp(&selected->irp)) found = true;
        const auto releaseFlags = disableReadInterrupts();
        if (!selected->cancellationPins) __builtin_trap();
        --selected->cancellationPins;
        if (!selected->pending && !selected->cancellationPins && !asyncCompletionWork.queued &&
            !ke::passive_work::queue(asyncCompletionWork)) __builtin_trap();
        restoreReadInterrupts(releaseFlags);
    }
    if (enableInterrupts) __asm__ __volatile__("cli" : : : "memory");
    return found;
#else
    (void)ownerThread; (void)ownerProcess; (void)exceptThread; (void)onlyTerminating;
    (void)targetFile; (void)statusBlock;
    return false;
#endif
}
}

void IoCancelThreadReads(ps::EThread& thread) noexcept { (void)cancelOwnedReads(&thread, nullptr, nullptr, false, nullptr, nullptr); }
void IoCancelProcessReads(ps::EProcess& process) noexcept { (void)cancelOwnedReads(nullptr, &process, nullptr, false, nullptr, nullptr); }
void IoCancelTerminatingProcessReads(ps::EProcess& process, ps::EThread* exceptThread) noexcept {
    (void)cancelOwnedReads(nullptr, &process, exceptThread, true, nullptr, nullptr);
}

NtStatus IoCancelFileReads(Handle handle, const void* statusBlock) noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL)
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    FileObject* file{}; ob::ObjectHeader* reference{};
    const auto status = IoReferenceFileHandle(handle, 0, file, reference);
    if (status != NtStatus::Success) return status;
    auto* current = ke::KeGetCurrentThread();
    if (!current || !ke::KeGetCurrentProcess()) { ob::ObDereferenceObject(reference); return NtStatus::InvalidParameter; }
    const bool found = cancelOwnedReads(nullptr, ke::KeGetCurrentProcess(), nullptr, false, file, statusBlock);
    ob::ObDereferenceObject(reference);
    return found ? NtStatus::Success : NtStatus::NotFound;
#else
    (void)handle; (void)statusBlock; return NtStatus::NotSupported;
#endif
}
} // namespace micant::io

namespace micant::sys {
NtStatus NtFlushBuffersFile(Handle fileHandle, IoStatusBlock* ioStatusBlock) noexcept {
    const NtStatus status = io::IoFlushBuffersFile(fileHandle);
    if (ioStatusBlock) {
        ioStatusBlock->status = status;
        ioStatusBlock->information = 0;
    }
    return status;
}
} // namespace micant::sys
