#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <new>
#include <string_view>
#if defined(MICANT_FREESTANDING)
#include "micant/km/core/sync.hpp"
#else
#include <mutex>
#endif
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/io.hpp"

namespace micant::ps { class EThread; class EProcess; }
namespace micant::se { struct FreestandingSecurityDescriptor; }

namespace micant::io {
struct DeviceObject;

using FileReadRoutine = NtStatus (*)(void* context, uint64_t offset,
                                     void* buffer, size_t length,
                                     size_t* bytesRead) noexcept;
using FileWriteRoutine = NtStatus (*)(void* context, uint64_t offset,
                                      const void* buffer, size_t length,
                                      size_t* bytesWritten) noexcept;
using FileFlushRoutine = NtStatus (*)(void* context) noexcept;
using FileSizeRoutine = uint64_t (*)(void* context) noexcept;
using FileCommitCreateRoutine = NtStatus (*)(void* context) noexcept;
using FileReleaseRoutine = void (*)(void* context) noexcept;
using FileHandleCloseRoutine = void (*)(void* context) noexcept;
struct FileBackend;
struct AsyncRead;
using FileSubmitReadRoutine = NtStatus (*)(void*, AsyncRead*) noexcept;
using FileCancelReadRoutine = bool (*)(void*, AsyncRead*) noexcept;
using FileOpenRoutine = NtStatus (*)(void* context, std::u16string_view name,
                                     bool openIf, uint32_t desiredAccess,
                                     FileBackend& backend) noexcept;
using FileSystemDirectoryRoutine = NtStatus (*)(void* context, char16_t* output,
                                                size_t capacity, size_t& length) noexcept;

// The backend owns the storage/device state. A file object pins that backend
// until its final Ob reference is released and exposes bounded offset reads to Mm.
struct FileBackend {
    void* context{};
    uint64_t size{};
    std::u16string_view name{};
    FileReadRoutine read{};
    FileReleaseRoutine release{};
    bool stream{};
    FileHandleCloseRoutine lastHandleClosed{};
    FileHandleCloseRoutine cancelWake{};
    FileSubmitReadRoutine submitRead{};
    FileCancelReadRoutine cancelRead{};
    bool synchronous{};
    // Optional persistent backend write operation. Success means durable; an
    // absent callback is read-only.
    FileWriteRoutine write{};
    bool created{};
    FileSizeRoutine querySize{};
    const se::FreestandingSecurityDescriptor* securityDescriptor{};
    // Newly created files stay provisional until the caller publishes its
    // handle. release must abort an uncommitted creation.
    FileCommitCreateRoutine commitCreate{};
    // Optional filesystem stream lease preventing file content changes while
    // an image section holds a snapshot of that stream.
    NtStatus (*acquireImage)(void*) noexcept{};
    void (*releaseImage)(void*) noexcept{};
    NtStatus (*publishImageControl)(void*, void*, bool (*)(void*) noexcept,
                                    void (*)(void*) noexcept, void**) noexcept{};
    // Optional ordered backend flush. Missing means IoFlushBuffersFile fails
    // with NotSupported; success must include all lower-layer flushes.
    FileFlushRoutine flush{};
};

class FileObject {
public:
    explicit FileObject(FileBackend backend, DeviceObject* device = nullptr) noexcept
        : backend_(backend), creationPending_(backend.created), deviceObject_(device) {}
    ~FileObject() {
        if (completionReference_) ob::ObDereferenceObject(completionReference_);
        if (backend_.release) backend_.release(backend_.context);
    }
    FileObject(const FileObject&) = delete;
    FileObject& operator=(const FileObject&) = delete;

    [[nodiscard]] uint64_t size() const noexcept {
        return backend_.querySize ? backend_.querySize(backend_.context) : backend_.size;
    }
    [[nodiscard]] NtStatus flush() const noexcept {
        if (!backend_.flush) return NtStatus::NotSupported;
#if defined(MICANT_FREESTANDING)
        if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceRequest;
#endif
        return backend_.flush(backend_.context);
    }
    [[nodiscard]] bool isStream() const noexcept { return backend_.stream; }
    [[nodiscard]] bool supportsImageControl() const noexcept {
        return backend_.acquireImage && backend_.releaseImage && backend_.publishImageControl;
    }
    [[nodiscard]] NtStatus acquireImage() noexcept {
        if (backend_.acquireImage && backend_.releaseImage)
            return backend_.acquireImage(backend_.context);
        return backend_.write ? NtStatus::NotSupported : NtStatus::Success;
    }
    void releaseImage() noexcept {
        if (backend_.acquireImage && backend_.releaseImage)
            backend_.releaseImage(backend_.context);
    }
    [[nodiscard]] NtStatus publishImageControl(void* candidate, bool (*retain)(void*) noexcept,
                                               void (*release)(void*) noexcept, void** result) noexcept {
        if (!backend_.publishImageControl) { *result = candidate; return NtStatus::Success; }
        return backend_.publishImageControl(backend_.context, candidate, retain, release, result);
    }
    void notifyLastHandleClosed() noexcept {
        bool expected = false;
        if (backend_.lastHandleClosed && closeNotified_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            backend_.lastHandleClosed(backend_.context);
    }
    NtStatus associateCompletion(ob::ObjectHeader* port, uintptr_t key) noexcept;
    ob::ObjectHeader* referenceCompletion(uintptr_t& key) noexcept;
    [[nodiscard]] bool isSynchronous() const noexcept { return backend_.synchronous; }
    [[nodiscard]] FileSubmitReadRoutine submitReadRoutine() const noexcept { return backend_.submitRead; }
    [[nodiscard]] FileCancelReadRoutine cancelReadRoutine() const noexcept { return backend_.cancelRead; }
    [[nodiscard]] bool canCancelRead() const noexcept { return backend_.cancelWake != nullptr; }
    void wakeCancelledRead() noexcept { if (backend_.cancelWake) backend_.cancelWake(backend_.context); }
    [[nodiscard]] DeviceObject* deviceObject() const noexcept { return deviceObject_; }
    NtStatus commitCreate() noexcept {
        if (!creationPending_.load(std::memory_order_acquire)) return NtStatus::Success;
        if (!backend_.commitCreate) return NtStatus::InvalidDeviceState;
        const auto status = backend_.commitCreate(backend_.context);
        if (status == NtStatus::Success) creationPending_.store(false, std::memory_order_release);
        return status;
    }
    [[nodiscard]] void* context() const noexcept { return backend_.context; }
    [[nodiscard]] std::u16string_view name() const noexcept { return {name_.get(), nameLength_}; }
    NtStatus initialize() noexcept {
        if (backend_.name.size() > SIZE_MAX / sizeof(char16_t) - 1) return NtStatus::BufferTooSmall;
        if (backend_.name.empty()) return NtStatus::Success;
        name_.reset(new (std::nothrow_t{}) char16_t[backend_.name.size() + 1]);
        if (!name_) return NtStatus::NoMemory;
        for (size_t i = 0; i < backend_.name.size(); ++i) name_[i] = backend_.name[i];
        name_[backend_.name.size()] = 0;
        nameLength_ = backend_.name.size();
        return NtStatus::Success;
    }
    NtStatus read(uint64_t offset, void* buffer, size_t length,
                  size_t* bytesRead) const noexcept {
        if (!bytesRead || (!buffer && length) || !backend_.read)
            return NtStatus::InvalidParameter;
        *bytesRead = 0;
        if (creationPending_.load(std::memory_order_acquire)) return NtStatus::DeviceNotReady;
        if (!length) return NtStatus::Success;
        if (backend_.stream) {
            const auto status = backend_.read(backend_.context, 0, buffer, length, bytesRead);
            if (*bytesRead > length) { *bytesRead = 0; return NtStatus::Unsuccessful; }
            return status;
        }
        const uint64_t currentSize = size();
        if (offset >= currentSize) return NtStatus::EndOfFile;
        const uint64_t available = currentSize - offset;
        const size_t boundedLength = available < length ? static_cast<size_t>(available) : length;
        const auto status = backend_.read(backend_.context, offset, buffer, boundedLength, bytesRead);
        if (*bytesRead > boundedLength) { *bytesRead = 0; return NtStatus::Unsuccessful; }
        if (status != NtStatus::Success && status != NtStatus::EndOfFile) return status;
        return *bytesRead < length ? NtStatus::EndOfFile : NtStatus::Success;
    }
    NtStatus write(uint64_t offset, const void* buffer, size_t length,
                   size_t* bytesWritten) noexcept {
        if (!bytesWritten || (!buffer && length)) return NtStatus::InvalidParameter;
        *bytesWritten = 0;
        if (creationPending_.load(std::memory_order_acquire)) return NtStatus::DeviceNotReady;
        if (!backend_.write) return NtStatus::MediaWriteProtected;
        if (length > UINT64_MAX - offset) return NtStatus::InvalidParameter;
        const auto status = backend_.write(backend_.context, offset, buffer, length, bytesWritten);
        if (*bytesWritten > length) { *bytesWritten = 0; return NtStatus::Unsuccessful; }
        if (status == NtStatus::Success && *bytesWritten != length) return NtStatus::Unsuccessful;
        return status;
    }
    NtStatus writeCurrent(const void* buffer, size_t length,
                          size_t* bytesWritten) noexcept {
        if (!bytesWritten || (!buffer && length)) return NtStatus::InvalidParameter;
        *bytesWritten = 0;
#if defined(MICANT_FREESTANDING)
        if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceRequest;
        const auto waitStatus = currentOffsetGate_.wait();
        if (waitStatus != NtStatus::Success) return waitStatus;
        struct GateRelease {
            sync::detail::BootstrapEvent& gate;
            ~GateRelease() { (void)gate.set(); }
        } release{currentOffsetGate_};
#else
        std::lock_guard<std::mutex> guard(currentOffsetMutex_);
#endif
        const auto status = write(currentOffset_, buffer, length, bytesWritten);
        if (*bytesWritten > UINT64_MAX - currentOffset_) {
            *bytesWritten = 0;
            return NtStatus::Unsuccessful;
        }
        if (status == NtStatus::Success) currentOffset_ += *bytesWritten;
        return status;
    }
    NtStatus readCurrent(void* buffer, size_t length, size_t* bytesRead) noexcept {
        if (!bytesRead || (!buffer && length)) return NtStatus::InvalidParameter;
        *bytesRead = 0;
        if (backend_.stream) return read(0, buffer, length, bytesRead);
#if defined(MICANT_FREESTANDING)
        if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceRequest;
        const auto waitStatus = currentOffsetGate_.wait();
        if (waitStatus != NtStatus::Success) return waitStatus;
        struct GateRelease {
            sync::detail::BootstrapEvent& gate;
            ~GateRelease() { (void)gate.set(); }
        } release{currentOffsetGate_};
#else
        std::lock_guard<std::mutex> guard(currentOffsetMutex_);
#endif
        const auto status = read(currentOffset_, buffer, length, bytesRead);
        if (!backend_.stream && *bytesRead > UINT64_MAX - currentOffset_) {
            *bytesRead = 0;
            return NtStatus::Unsuccessful;
        }
        if (!backend_.stream) currentOffset_ += *bytesRead;
        return status;
    }

private:
    FileBackend backend_{};
    std::atomic<bool> creationPending_{false};
    ob::ObjectHeader* completionReference_{};
    uintptr_t completionKey_{};
#if defined(MICANT_FREESTANDING)
    ke::SpinLock completionLock_{};
#else
    std::mutex completionLock_{};
#endif
    DeviceObject* deviceObject_{};
    std::atomic<bool> closeNotified_{false};
    std::unique_ptr<char16_t[]> name_{};
    size_t nameLength_{};
    uint64_t currentOffset_{};
#if defined(MICANT_FREESTANDING)
    sync::detail::BootstrapEvent currentOffsetGate_{false, true};
#else
    std::mutex currentOffsetMutex_;
#endif
};

// Driver-owned linkage is valid only while the request is pending in a device
// queue. Io retains the file, process, event, and storage until completion.
struct AsyncRead {
    Irp irp{};
    AsyncRead* driverNext{};
    AsyncRead* pendingNext{};
    AsyncRead* completedNext{};
    FileObject* file{};
    ob::ObjectHeader* fileReference{};
    ob::ObjectHeader* processReference{};
    ob::ObjectHeader* threadReference{};
    ob::ObjectHeader* eventReference{};
    ke::KAPC* userApc{};
    uintptr_t userApcRoutine{};
    uintptr_t userApcContext{};
    ob::ObjectHeader* completionReference{};
    void* completionPacket{};
    uintptr_t completionKey{};
    uintptr_t completionContext{};
    void* process{};
    void* userBuffer{};
    IoStatusBlock* userStatus{};
    uint8_t* buffer{};
    size_t length{};
    IoStatusBlock result{};
    bool pending{};
    // Protected by BSP interrupt exclusion. A cancellation pin prevents the
    // completion worker from handing the IRP to a potentially freeing
    // completion routine until the backend cancel call returns.
    bool cancelDispatched{};
    uint32_t cancellationPins{};
    bool submitting{};
    bool cancelAfterSubmit{};
    bool completionUnwound{};
    bool completionQueued{};
};
void IoCompleteAsyncRead(AsyncRead* request, NtStatus status, size_t information) noexcept;
NtStatus IoSubmitAsyncRead(Handle handle, Handle event, uintptr_t apcRoutine, uintptr_t completionContext,
                           void* userBuffer, size_t length, IoStatusBlock* userStatus) noexcept;
NtStatus IoAssociateCompletionPort(Handle file, Handle port, uintptr_t key) noexcept;
NtStatus IoHasCompletionPort(Handle file, bool& associated) noexcept;

NtStatus IoInitializeFileObjects() noexcept;
NtStatus IoCreateCompletionPort(Handle* handle, uint32_t access, uint32_t concurrency) noexcept;
NtStatus IoPostCompletion(Handle handle, uintptr_t key, uintptr_t context,
                          NtStatus status, uintptr_t information) noexcept;
NtStatus IoRemoveCompletion(Handle handle, uintptr_t& key, uintptr_t& context,
                            IoStatusBlock& status, uint64_t timeout100ns) noexcept;
NtStatus IoRegisterFileOpenRoutine(FileOpenRoutine open, void* context) noexcept;
void IoScheduleDeferredFileDeletes() noexcept;
bool IoDeferredFileDeletesPending() noexcept;
#if defined(MICANT_FREESTANDING)
// Transfer a zero-reference Ob header to Io's resident passive delete queue.
// The object type's delete procedure is invoked by ObFinishDelete.
bool IoDeferPassiveObjectDelete(ob::ObjectHeader* header) noexcept;
#endif
NtStatus IoUnregisterFileOpenRoutine(FileOpenRoutine open, void* context) noexcept;
NtStatus IoRegisterSystemDirectoryRoutine(FileSystemDirectoryRoutine query, void* context) noexcept;
NtStatus IoQuerySystemDirectory(char16_t* output, size_t capacity, size_t& length) noexcept;
NtStatus IoOpenBackendFile(Handle* fileHandle, uint32_t desiredAccess,
                           std::u16string_view name, bool openIf = false,
                           bool* created = nullptr, bool deferCreate = false) noexcept;
NtStatus IoCommitCreatedFile(Handle fileHandle) noexcept;
NtStatus IoCreateBackendFile(Handle* fileHandle, uint32_t desiredAccess,
                             const FileBackend& backend) noexcept;
NtStatus IoCreateDeviceFile(Handle* fileHandle, uint32_t desiredAccess,
                            DeviceObject* deviceObject, const FileBackend& backend) noexcept;
NtStatus IoReferenceFileHandle(Handle fileHandle, uint32_t desiredAccess,
                               FileObject*& file, ob::ObjectHeader*& reference) noexcept;
NtStatus IoReadFileHandle(Handle fileHandle, bool useCurrentOffset, uint64_t offset,
                          void* buffer, size_t length, size_t* bytesRead,
                          const void* requestStatus = nullptr) noexcept;
NtStatus IoWriteFileHandle(Handle fileHandle, bool useCurrentOffset, uint64_t offset,
                           const void* buffer, size_t length, size_t* bytesWritten) noexcept;
// Synchronous file-buffer flush used by the native NtFlushBuffersFile service.
NtStatus IoFlushBuffersFile(Handle fileHandle) noexcept;
NtStatus IoCancelFileReads(Handle fileHandle, const void* requestStatus) noexcept;
bool IoCurrentReadCancelled() noexcept;
// Request cancellation of owned reads; IRPs and backend references remain
// owned by Io until the driver actually completes each request.
void IoCancelThreadReads(ps::EThread& thread) noexcept;
void IoCancelProcessReads(ps::EProcess& process) noexcept;
void IoCancelTerminatingProcessReads(ps::EProcess& process, ps::EThread* exceptThread) noexcept;

} // namespace micant::io
