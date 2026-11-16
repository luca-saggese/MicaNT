#include "micant/km/core/ntos_internal.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/afd_io.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/shared/afd_socket_abi.hpp"
#include "micant/km/drivers/fs/fs.hpp"
#include "micant/km/drivers/fs/npfs.hpp"
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>
#include <cwchar>
#include <array>

namespace micant::sys {


static std::unordered_map<Handle, std::shared_ptr<fs::FileObject>> g_KernelFiles;
static std::unordered_map<Handle, std::shared_ptr<io::IoCompletionPort>> g_KernelIocpPorts;
static Handle g_NextFileHandle = 0x200;

static void CloseAfdHandleAfterCaptureFailure(Handle handle) noexcept {
#if defined(MICANT_FREESTANDING)
    ob::HandleTable* table{};
    Handle raw{};
    if (ob::detail::resolveHandle(handle, table, raw) == NtStatus::Success && table)
        (void)table->closeHandle(raw);
#else
    (void)internal::ObCloseGenericHandle(handle);
#endif
}

static std::shared_ptr<fs::FileObject> ReferenceKernelFile(Handle handle) {
    const auto it = g_KernelFiles.find(handle);
    return it == g_KernelFiles.end() ? nullptr : it->second;
}

static Handle InsertKernelFile(std::shared_ptr<fs::FileObject> file) {
    const Handle handle = g_NextFileHandle;
    g_NextFileHandle += 4;
    g_KernelFiles.emplace(handle, std::move(file));
    return handle;
}

namespace internal {

bool IoCloseKernelHandle(Handle h) {
    bool hadIocp = g_KernelIocpPorts.erase(h) > 0;
    std::shared_ptr<fs::FileObject> file;
    auto it = g_KernelFiles.find(h);
    if (it != g_KernelFiles.end()) { file = std::move(it->second); g_KernelFiles.erase(it); }
    if (file) {
        fs::VirtualFileSystem::get().closeFile(file.get());
        return true;
    }
    return hadIocp;
}

NtStatus NtCreateIoCompletionSys(const SyscallFrame& f) {
    auto* outHandle = reinterpret_cast<Handle*>(f.arg1);
    if (!outHandle) return NtStatus::InvalidParameter;

    uint32_t maxThreads = static_cast<uint32_t>(f.arg4);
    Handle h = ObAllocateExecutiveHandle();
    g_KernelIocpPorts[h] = std::make_shared<io::IoCompletionPort>(maxThreads);
    *outHandle = h;
    return NtStatus::Success;
}

NtStatus NtSetIoCompletionSys(const SyscallFrame& f) {
    Handle h = static_cast<Handle>(f.arg1);
    auto it = g_KernelIocpPorts.find(h);
    if (it == g_KernelIocpPorts.end()) return NtStatus::InvalidHandle;

    uint64_t key = f.arg2;
    uintptr_t overlapped = f.arg3;
    NtStatus status = static_cast<NtStatus>(f.arg4);
    uint32_t bytes = 0;
    if (f.stackArgs && f.stackArgCount >= 1) {
        bytes = static_cast<uint32_t>(f.stackArgs[0]);
    }

    return it->second->postCompletion(key, overlapped, status, bytes);
}

NtStatus NtRemoveIoCompletionSys(const SyscallFrame& f) {
    Handle h = static_cast<Handle>(f.arg1);
    auto it = g_KernelIocpPorts.find(h);
    if (it == g_KernelIocpPorts.end()) return NtStatus::InvalidHandle;

    auto* outKey = reinterpret_cast<uint64_t*>(f.arg2);
    auto* outOverlapped = reinterpret_cast<uintptr_t*>(f.arg3);
    auto* outIoStatus = reinterpret_cast<IoStatusBlock*>(f.arg4);
    auto* timeout = (f.stackArgs && f.stackArgCount >= 1) ? reinterpret_cast<LargeInteger*>(f.stackArgs[0]) : nullptr;

    uint64_t key = 0;
    uintptr_t ov = 0;
    IoStatusBlock iosb{};
    uint32_t ms = timeout ? static_cast<uint32_t>(timeout->quadPart / -10000) : 0xFFFFFFFF;

    NtStatus res = it->second->removeCompletion(key, ov, iosb, ms);
    if (outKey) *outKey = key;
    if (outOverlapped) *outOverlapped = ov;
    if (outIoStatus) *outIoStatus = iosb;
    return res;
}

} // namespace internal

fs::FileObject* LookupKernelFileObject(Handle handle) {
    const auto it = g_KernelFiles.find(handle);
    return it == g_KernelFiles.end() ? nullptr : it->second.get();
}

NtStatus NtCreateFile(
    Handle* fileHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    LargeInteger* /*allocationSize*/,
    uint32_t fileAttributes,
    uint32_t shareAccess,
    uint32_t createDisposition,
    uint32_t /*createOptions*/,
    void* eaBuffer,
    uint32_t eaLength
) {
    if (!fileHandle || !objectAttributes) {
        return NtStatus::InvalidParameter;
    }
    if (ex::ProbeForWrite(fileHandle, sizeof(*fileHandle), alignof(Handle)) != NtStatus::Success ||
        (ioStatusBlock && ex::ProbeForWrite(ioStatusBlock, sizeof(*ioStatusBlock), alignof(IoStatusBlock)) != NtStatus::Success))
        return NtStatus::AccessViolation;

    // AFD endpoints are opened through the standard file syscall and EA path;
    // keep their ownership in the same kernel file-handle table as VFS files.
    ObjectAttributes capturedAttributes{};
    UnicodeString capturedName{};
    std::array<wchar_t, 260> pathBuffer{};
    if (ex::ExSafeCopyFromUser(&capturedAttributes, objectAttributes, sizeof(capturedAttributes)) != NtStatus::Success ||
        !capturedAttributes.objectName || capturedAttributes.rootDirectory != 0) return NtStatus::AccessViolation;
    if (ex::ExSafeCopyFromUser(&capturedName, capturedAttributes.objectName, sizeof(capturedName)) != NtStatus::Success ||
        !capturedName.buffer || (capturedName.length % sizeof(wchar_t)) != 0 ||
        capturedName.length == 0 || capturedName.length > (pathBuffer.size() - 1) * sizeof(wchar_t)) return NtStatus::InvalidParameter;
    const size_t pathChars = capturedName.length / sizeof(wchar_t);
    if (ex::ExSafeCopyFromUser(pathBuffer.data(), capturedName.buffer, capturedName.length) != NtStatus::Success)
        return NtStatus::AccessViolation;
    pathBuffer[pathChars] = L'\0';

    std::shared_ptr<fs::FileObject> fileObj;
    const std::wstring_view path(pathBuffer.data(), pathChars);
    if (path == std::wstring_view(L"\\Device\\Afd\\Endpoint")) {
        if (createDisposition != fs::FILE_OPEN || fileAttributes != 0 || shareAccess != 0 ||
            capturedAttributes.securityDescriptor || capturedAttributes.securityQualityOfService ||
            (capturedAttributes.attributes & ~OBJ_CASE_INSENSITIVE) != 0) return NtStatus::NotSupported;
        if (!eaBuffer || eaLength == 0 || eaLength > 256) return NtStatus::InvalidParameter;
        std::array<uint8_t, 256> capturedEa{};
        if (ex::ExSafeCopyFromUser(capturedEa.data(), eaBuffer, eaLength) != NtStatus::Success)
            return NtStatus::AccessViolation;
        Handle handle{};
        NtStatus status = micant::km::afdwsk::IoAfdCreateEndpointHandle(&handle, path,
            desiredAccess, capturedEa.data(), eaLength);
        if (status != NtStatus::Success) return status;
        if (ex::ExSafeCopyToUser(fileHandle, &handle, sizeof(handle)) != NtStatus::Success) {
            CloseAfdHandleAfterCaptureFailure(handle);
            return NtStatus::AccessViolation;
        }
        if (ioStatusBlock) {
            IoStatusBlock iosb{NtStatus::Success, 2u};
            if (ex::ExSafeCopyToUser(ioStatusBlock, &iosb, sizeof(iosb)) != NtStatus::Success) {
                CloseAfdHandleAfterCaptureFailure(handle);
                return NtStatus::AccessViolation;
            }
        }
        return NtStatus::Success;
    } else {
    NtStatus status = fs::VirtualFileSystem::get().createOrOpenFile(
        path,
        desiredAccess,
        createDisposition,
        fileObj
    );

    if (!NT_SUCCESS(status)) {
        if (ioStatusBlock) {
            ioStatusBlock->status = status;
            ioStatusBlock->information = 0;
        }
        return status;
    }
    }

    const Handle h = InsertKernelFile(fileObj);
    if (ex::ExSafeCopyToUser(fileHandle, &h, sizeof(h)) != NtStatus::Success) {
        g_KernelFiles.erase(h);
        fs::VirtualFileSystem::get().closeFile(fileObj.get());
        return NtStatus::AccessViolation;
    }

    if (ioStatusBlock) {
        IoStatusBlock iosb{NtStatus::Success, (path == std::wstring_view(L"\\Device\\Afd\\Endpoint") || createDisposition == fs::FILE_CREATE) ? 2u : 1u};
        if (ex::ExSafeCopyToUser(ioStatusBlock, &iosb, sizeof(iosb)) != NtStatus::Success) return NtStatus::AccessViolation;
    }
    return NtStatus::Success;
}

NtStatus NtOpenFile(
    Handle* fileHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    uint32_t /*shareAccess*/,
    uint32_t /*openOptions*/
) {
    return NtCreateFile(
        fileHandle,
        desiredAccess,
        objectAttributes,
        ioStatusBlock,
        nullptr,
        fs::FILE_ATTRIBUTE_NORMAL,
        0,
        fs::FILE_OPEN,
        0,
        nullptr,
        0
    );
}

NtStatus NtCreateNamedPipeFile(
    Handle* fileHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    uint32_t /*shareAccess*/,
    uint32_t /*createDisposition*/,
    uint32_t /*createOptions*/,
    uint32_t namedPipeType,
    uint32_t readMode,
    uint32_t completionMode,
    uint32_t maximumInstances,
    uint32_t inboundQuota,
    uint32_t outboundQuota,
    LargeInteger* defaultTimeout
) {
    if (!fileHandle || !objectAttributes || !objectAttributes->objectName) {
        return NtStatus::InvalidParameter;
    }

    uint32_t pipeMode = (namedPipeType ? npfs::PIPE_TYPE_MESSAGE : npfs::PIPE_TYPE_BYTE) |
                        (readMode ? npfs::PIPE_READMODE_MESSAGE : npfs::PIPE_READMODE_BYTE) |
                        (completionMode ? npfs::PIPE_NOWAIT : npfs::PIPE_WAIT);

    uint32_t timeoutMs = defaultTimeout ? static_cast<uint32_t>(-(defaultTimeout->quadPart / 10000)) : 50;

    std::shared_ptr<npfs::NamedPipeInstance> pipeInst;
    NtStatus status = npfs::NamedPipeFileSystem::get().createNamedPipe(
        objectAttributes->objectName->view(),
        desiredAccess,
        pipeMode,
        maximumInstances ? maximumInstances : npfs::PIPE_UNLIMITED_INSTANCES,
        outboundQuota,
        inboundQuota,
        timeoutMs,
        pipeInst
    );

    if (!NT_SUCCESS(status)) {
        if (ioStatusBlock) {
            ioStatusBlock->status = status;
            ioStatusBlock->information = 0;
        }
        return status;
    }

    auto fileObj = fs::VirtualFileSystem::get().registerServerPipe(pipeInst, desiredAccess);
    Handle h = InsertKernelFile(fileObj);
    *fileHandle = h;

    if (ioStatusBlock) {
        ioStatusBlock->status = NtStatus::Success;
        ioStatusBlock->information = 2; // FILE_CREATED
    }
    return NtStatus::Success;
}

NtStatus NtCreateMailslotFile(
    Handle* fileHandle,
    uint32_t /*desiredAccess*/,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    uint32_t /*createOptions*/,
    uint32_t /*mailslotQuota*/,
    uint32_t maxMessageSize,
    LargeInteger* readTimeout
) {
    if (!fileHandle || !objectAttributes || !objectAttributes->objectName) {
        return NtStatus::InvalidParameter;
    }

    uint32_t timeoutMs = npfs::MAILSLOT_WAIT_FOREVER;
    if (readTimeout) {
        timeoutMs = (readTimeout->quadPart == -1) ? npfs::MAILSLOT_WAIT_FOREVER : static_cast<uint32_t>(-(readTimeout->quadPart / 10000));
    }

    std::shared_ptr<npfs::Mailslot> slot;
    NtStatus status = npfs::MailslotFileSystem::get().createMailslot(
        objectAttributes->objectName->view(),
        maxMessageSize,
        timeoutMs,
        slot
    );

    if (!NT_SUCCESS(status)) {
        if (ioStatusBlock) {
            ioStatusBlock->status = status;
            ioStatusBlock->information = 0;
        }
        return status;
    }

    auto fileObj = fs::VirtualFileSystem::get().registerServerMailslot(slot);
    const Handle h = InsertKernelFile(fileObj);
    *fileHandle = h;

    if (ioStatusBlock) {
        ioStatusBlock->status = NtStatus::Success;
        ioStatusBlock->information = 2; // FILE_CREATED
    }
    return NtStatus::Success;
}

NtStatus NtReadFile(
    Handle fileHandle,
    Handle /*event*/,
    void* /*apcRoutine*/,
    void* /*apcContext*/,
    IoStatusBlock* ioStatusBlock,
    void* buffer,
    uint32_t length,
    LargeInteger* byteOffset,
    uint32_t* /*key*/
) {
    auto fileObj = ReferenceKernelFile(fileHandle);
    if (!fileObj) return NtStatus::InvalidHandle;

    uint32_t bytesRead = 0;
    NtStatus status = fs::VirtualFileSystem::get().readFile(
        fileObj.get(),
        buffer,
        length,
        byteOffset,
        bytesRead
    );

    if (ioStatusBlock) {
        ioStatusBlock->status = status;
        ioStatusBlock->information = bytesRead;
    }
    return status;
}

NtStatus NtWriteFile(
    Handle fileHandle,
    Handle /*event*/,
    void* /*apcRoutine*/,
    void* /*apcContext*/,
    IoStatusBlock* ioStatusBlock,
    const void* buffer,
    uint32_t length,
    LargeInteger* byteOffset,
    uint32_t* /*key*/
) {
    if (fileHandle == 0x14 || fileHandle == 0x18 || fileHandle == static_cast<Handle>(-11) || fileHandle == static_cast<Handle>(-12)) {
        if (buffer && length > 0) {
            std::cout.write(reinterpret_cast<const char*>(buffer), length);
            std::cout.flush();
        }
        if (ioStatusBlock) {
            ioStatusBlock->status = NtStatus::Success;
            ioStatusBlock->information = length;
        }
        return NtStatus::Success;
    }

    auto fileObj = ReferenceKernelFile(fileHandle);
    if (!fileObj) return NtStatus::InvalidHandle;

    uint32_t bytesWritten = 0;
    NtStatus status = fs::VirtualFileSystem::get().writeFile(
        fileObj.get(),
        buffer,
        length,
        byteOffset,
        bytesWritten
    );

    if (ioStatusBlock) {
        ioStatusBlock->status = status;
        ioStatusBlock->information = bytesWritten;
    }
    return status;
}

NtStatus NtDeviceIoControlFile(
    Handle fileHandle,
    Handle event,
    void* apcRoutine,
    void* apcContext,
    IoStatusBlock* ioStatusBlock,
    uint32_t ioControlCode,
    const void* inputBuffer,
    uint32_t inputBufferLength,
    void* outputBuffer,
    uint32_t outputBufferLength
) {
    // Native Io file handles live in the current process Ob table. Reference
    // them before touching a device so concurrent NtClose cannot free the file.
    io::FileObject* deviceFile{};
    ob::ObjectHeader* deviceReference{};
    uint32_t requiredAccess = 0;
    if (ioControlCode == micant::shared::afd::IoctlReceive) requiredAccess = 0x0001;
    else if (ioControlCode == micant::shared::afd::IoctlBind ||
             ioControlCode == micant::shared::afd::IoctlConnect ||
             ioControlCode == micant::shared::afd::IoctlListen ||
             ioControlCode == micant::shared::afd::IoctlSend ||
             ioControlCode == micant::shared::afd::IoctlSendTo) requiredAccess = 0x0002;
    NtStatus referenceStatus = io::IoReferenceFileHandle(fileHandle, requiredAccess, deviceFile, deviceReference);
    if (referenceStatus == NtStatus::Success && micant::km::afdwsk::IoAfdIsEndpointFile(deviceFile)) {
        NtStatus afdStatus = NtStatus::InvalidParameter;
        if (event || apcRoutine || apcContext) {
            ob::ObDereferenceObject(deviceReference);
            return NtStatus::NotSupported;
        }
        using namespace micant::shared::afd;
        if (inputBufferLength <= MaximumControlBuffer && outputBufferLength <= MaximumControlBuffer &&
            (!inputBufferLength || inputBuffer) && (!outputBufferLength || outputBuffer)) {
            if ((inputBufferLength && ex::ProbeForRead(inputBuffer, inputBufferLength, 1) != NtStatus::Success) ||
                (outputBufferLength && ex::ProbeForWrite(outputBuffer, outputBufferLength, 1) != NtStatus::Success) ||
                (ioStatusBlock && ex::ProbeForWrite(ioStatusBlock, sizeof(*ioStatusBlock), alignof(IoStatusBlock)) != NtStatus::Success)) {
                afdStatus = NtStatus::AccessViolation;
            } else {
                try {
                    std::vector<uint8_t> input(inputBufferLength);
                    std::vector<uint8_t> output(outputBufferLength);
                    if (inputBufferLength && ex::ExSafeCopyFromUser(input.data(), inputBuffer, inputBufferLength) != NtStatus::Success) {
                        afdStatus = NtStatus::AccessViolation;
                    } else {
                        io::Irp afdIrp{};
                        afdIrp.majorFunction = io::IRP_MJ_DEVICE_CONTROL;
                        afdIrp.deviceObject = deviceFile->deviceObject();
                        afdIrp.fileObject = deviceFile;
                        afdIrp.systemBuffer = input.data();
                        afdIrp.userBuffer = output.data();
                        afdIrp.length = outputBufferLength;
                        afdIrp.byteOffset.lowPart = ioControlCode;
                        afdIrp.byteOffset.highPart = static_cast<int32_t>(inputBufferLength);
                        afdStatus = io::IoCallDriver(afdIrp.deviceObject, &afdIrp);
                        if (ioControlCode == micant::shared::afd::IoctlReceive &&
                            afdIrp.ioStatus.information > outputBufferLength) afdStatus = NtStatus::Unsuccessful;
                        else if (ioControlCode == micant::shared::afd::IoctlReceive && afdIrp.ioStatus.information &&
                            ex::ExSafeCopyToUser(outputBuffer, output.data(), afdIrp.ioStatus.information) != NtStatus::Success)
                            afdStatus = NtStatus::AccessViolation;
                        if (ioStatusBlock && ex::ExSafeCopyToUser(ioStatusBlock, &afdIrp.ioStatus, sizeof(afdIrp.ioStatus)) != NtStatus::Success)
                            afdStatus = NtStatus::AccessViolation;
                    }
                } catch (...) {
                    afdStatus = NtStatus::InsufficientResources;
                }
            }
        }
        ob::ObDereferenceObject(deviceReference);
        return afdStatus;
    }
    if (deviceReference) ob::ObDereferenceObject(deviceReference);
    auto fileObj = ReferenceKernelFile(fileHandle);
    if (!fileObj) return NtStatus::InvalidHandle;
    auto* devObj = fileObj->getDeviceObject();
    if (!devObj || !devObj->driverObject) {
        return NtStatus::InvalidDeviceRequest;
    }
    // VFS handles are legacy fs::FileObject instances, not Ob-owned
    // io::FileObject handles. Until the VFS open path is migrated to the
    // canonical Io backend ownership contract, forwarding this request would
    // pass an object with the wrong type and lifetime to the driver.
    (void)ioControlCode;
    (void)inputBuffer;
    (void)inputBufferLength;
    (void)outputBuffer;
    (void)outputBufferLength;
    if (ioStatusBlock) {
        IoStatusBlock status{NtStatus::InvalidDeviceRequest, 0};
        if (ex::ExSafeCopyToUser(ioStatusBlock, &status, sizeof(status)) != NtStatus::Success)
            return NtStatus::AccessViolation;
    }
    return NtStatus::InvalidDeviceRequest;
}

NtStatus NtQueryInformationFile(
    Handle fileHandle,
    IoStatusBlock* ioStatusBlock,
    void* fileInformation,
    uint32_t length,
    FileInformationClass fileInformationClass
) {
    if (!fileInformation) return NtStatus::InvalidParameter;
    auto fileObj = ReferenceKernelFile(fileHandle);
    if (!fileObj) return NtStatus::InvalidHandle;

    switch (fileInformationClass) {
        case FileInformationClass::FileStandardInformation: {
            if (length < sizeof(FileStandardInformation)) return NtStatus::InfoLengthMismatch;
            auto* info = reinterpret_cast<FileStandardInformation*>(fileInformation);
            info->allocationSize.quadPart = static_cast<int64_t>(fileObj->getFileSize());
            info->endOfFile.quadPart = static_cast<int64_t>(fileObj->getFileSize());
            info->numberOfLinks = 1;
            info->deletePending = false;
            info->directory = fileObj->isDirectory();
            if (ioStatusBlock) {
                ioStatusBlock->status = NtStatus::Success;
                ioStatusBlock->information = sizeof(FileStandardInformation);
            }
            return NtStatus::Success;
        }
        case FileInformationClass::FilePositionInformation: {
            if (length < sizeof(FilePositionInformation)) return NtStatus::InfoLengthMismatch;
            auto* info = reinterpret_cast<FilePositionInformation*>(fileInformation);
            info->currentByteOffset.quadPart = fileObj->getCurrentByteOffset();
            if (ioStatusBlock) {
                ioStatusBlock->status = NtStatus::Success;
                ioStatusBlock->information = sizeof(FilePositionInformation);
            }
            return NtStatus::Success;
        }
        case FileInformationClass::FileBasicInformation: {
            if (length < sizeof(FileBasicInformation)) return NtStatus::InfoLengthMismatch;
            auto* info = reinterpret_cast<FileBasicInformation*>(fileInformation);
            info->fileAttributes = fileObj->isDirectory() ? fs::FILE_ATTRIBUTE_DIRECTORY : fs::FILE_ATTRIBUTE_NORMAL;
            if (ioStatusBlock) {
                ioStatusBlock->status = NtStatus::Success;
                ioStatusBlock->information = sizeof(FileBasicInformation);
            }
            return NtStatus::Success;
        }
        default:
            return NtStatus::NotImplemented;
    }
}

NtStatus NtSetInformationFile(
    Handle fileHandle,
    IoStatusBlock* ioStatusBlock,
    const void* fileInformation,
    uint32_t length,
    FileInformationClass fileInformationClass
) {
    if (!fileInformation) return NtStatus::InvalidParameter;
    auto fileObj = ReferenceKernelFile(fileHandle);
    if (!fileObj) return NtStatus::InvalidHandle;

    switch (fileInformationClass) {
        case FileInformationClass::FilePositionInformation: {
            if (length < sizeof(FilePositionInformation)) return NtStatus::InfoLengthMismatch;
            auto* info = reinterpret_cast<const FilePositionInformation*>(fileInformation);
            fileObj->setCurrentByteOffset(info->currentByteOffset.quadPart);
            if (ioStatusBlock) {
                ioStatusBlock->status = NtStatus::Success;
                ioStatusBlock->information = sizeof(FilePositionInformation);
            }
            return NtStatus::Success;
        }
        case FileInformationClass::FileEndOfFileInformation: {
            if (length < sizeof(FileEndOfFileInformation)) return NtStatus::InfoLengthMismatch;
            auto* info = reinterpret_cast<const FileEndOfFileInformation*>(fileInformation);
            fileObj->getData().resize(static_cast<size_t>(info->endOfFile.quadPart));
            if (ioStatusBlock) {
                ioStatusBlock->status = NtStatus::Success;
                ioStatusBlock->information = sizeof(FileEndOfFileInformation);
            }
            return NtStatus::Success;
        }
        default:
            return NtStatus::NotImplemented;
    }
}

NtStatus NtQueryDirectoryFile(
    Handle fileHandle,
    Handle /*event*/,
    void* /*apcRoutine*/,
    void* /*apcContext*/,
    IoStatusBlock* ioStatusBlock,
    void* fileInformation,
    uint32_t length,
    FileInformationClass /*fileInformationClass*/,
    bool /*returnSingleEntry*/,
    UnicodeString* /*fileName*/,
    bool /*restartScan*/
) {
    if (!fileInformation || length < sizeof(FileBothDirInformation)) return NtStatus::InvalidParameter;
    auto fileObj = ReferenceKernelFile(fileHandle);
    if (!fileObj) return NtStatus::InvalidHandle;

    std::vector<fs::VirtualFileSystem::DirectoryEntry> entries;
    NtStatus status = fs::VirtualFileSystem::get().queryDirectory(fileObj->getFileName(), entries);
    if (!NT_SUCCESS(status)) return status;

    if (entries.empty()) {
        return NtStatus::NoMoreFiles;
    }

    auto* dirInfo = reinterpret_cast<FileBothDirInformation*>(fileInformation);
    const auto& entry = entries[0];
    dirInfo->nextEntryOffset = 0;
    dirInfo->fileAttributes = entry.attributes;
    dirInfo->endOfFile.quadPart = static_cast<int64_t>(entry.size);
    dirInfo->allocationSize.quadPart = static_cast<int64_t>(entry.size);
    dirInfo->fileNameLength = static_cast<uint32_t>(entry.name.size() * sizeof(wchar_t));
    std::wcsncpy(dirInfo->fileName, entry.name.c_str(), 259);

    if (ioStatusBlock) {
        ioStatusBlock->status = NtStatus::Success;
        ioStatusBlock->information = sizeof(FileBothDirInformation);
    }
    return NtStatus::Success;
}

} // namespace micant::sys
