/**
 * @file exfat.cpp
 @brief Luce OS Clean-Room Extended FAT (exFAT) File System Runtime Subsystem (exfat.sys / ExfatDriver).
 *
 * Clean-Room ISO C++23. Sovereign MicaNT Kernel Architecture.
 * Executed in Kernel-Mode Ring 0 / Ring 1.
 */

#include "micant/km/drivers/fs/exfat.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/drivers/fs/fs.hpp"

namespace micant::exfat {

// Fast I/O Dispatch stubs
namespace {

bool ExfatFastIoCheckIfPossible(
    void* /*fileObject*/,
    io::LargeInteger* /*offset*/,
    uint32_t /*length*/,
    bool /*wait*/,
    uint32_t /*lockKey*/,
    bool /*checkForRead*/,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    if (ioStatus) {
        ioStatus->status = NtStatus::Success;
        ioStatus->information = 0;
    }
    return true;
}

bool ExfatFastIoRead(
    void* /*fileObject*/,
    io::LargeInteger* /*offset*/,
    uint32_t length,
    bool /*wait*/,
    void* buffer,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    if (!buffer || !ioStatus) return false;
    ioStatus->status = NtStatus::Success;
    ioStatus->information = length;
    return true;
}

bool ExfatFastIoWrite(
    void* /*fileObject*/,
    io::LargeInteger* /*offset*/,
    uint32_t length,
    bool /*wait*/,
    const void* buffer,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    if (!buffer || !ioStatus) return false;
    ioStatus->status = NtStatus::Success;
    ioStatus->information = length;
    return true;
}

bool ExfatFastIoQueryBasicInfo(
    void* /*fileObject*/,
    bool /*wait*/,
    void* basicInfo,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    if (!basicInfo || !ioStatus) return false;
    std::memset(basicInfo, 0, 40);
    ioStatus->status = NtStatus::Success;
    ioStatus->information = 40;
    return true;
}

bool ExfatFastIoQueryStandardInfo(
    void* /*fileObject*/,
    bool /*wait*/,
    void* standardInfo,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    if (!standardInfo || !ioStatus) return false;
    std::memset(standardInfo, 0, 24);
    ioStatus->status = NtStatus::Success;
    ioStatus->information = 24;
    return true;
}

bool ExfatFastIoDeviceControl(
    void* /*fileObject*/,
    bool /*wait*/,
    void* /*inputBuffer*/,
    uint32_t /*inputLength*/,
    void* /*outputBuffer*/,
    uint32_t /*outputLength*/,
    uint32_t /*ioControlCode*/,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    if (ioStatus) {
        ioStatus->status = NtStatus::Success;
        ioStatus->information = 0;
    }
    return true;
}

} // namespace

void ExfatDriver::initialize() {
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (initialized_) return;

    driverObject_ = std::make_unique<io::DriverObject>();
    driverObject_->driverName = L"\\FileSystem\\ExFat";

    // Set Dispatch routines
    driverObject_->setDispatch(io::IRP_MJ_CREATE, &ExfatDriver::dispatchCreate);
    driverObject_->setDispatch(io::IRP_MJ_READ, &ExfatDriver::dispatchRead);
    driverObject_->setDispatch(io::IRP_MJ_WRITE, &ExfatDriver::dispatchWrite);
    driverObject_->setDispatch(io::IRP_MJ_CLOSE, &ExfatDriver::dispatchClose);
    driverObject_->setDispatch(io::IRP_MJ_DIRECTORY_CONTROL, &ExfatDriver::dispatchDirectoryControl);
    driverObject_->setDispatch(io::IRP_MJ_QUERY_INFORMATION, &ExfatDriver::dispatchQueryInformation);
    driverObject_->setDispatch(io::IRP_MJ_SET_INFORMATION, &ExfatDriver::dispatchSetInformation);
    driverObject_->setDispatch(io::IRP_MJ_FILE_SYSTEM_CONTROL, &ExfatDriver::dispatchFileSystemControl);
    driverObject_->setDispatch(io::IRP_MJ_DEVICE_CONTROL, &ExfatDriver::dispatchDeviceControl);
    driverObject_->setDispatch(io::IRP_MJ_SHUTDOWN, &ExfatDriver::dispatchShutdown);
    driverObject_->setDispatch(io::IRP_MJ_CLEANUP, &ExfatDriver::dispatchCleanup);

    // Set Fast I/O dispatch
    fastIoTable_.sizeOfFastIoDispatch       = sizeof(io::FastIoDispatch);
    fastIoTable_.fastIoCheckIfPossible      = ExfatFastIoCheckIfPossible;
    fastIoTable_.fastIoRead                 = ExfatFastIoRead;
    fastIoTable_.fastIoWrite                = ExfatFastIoWrite;
    fastIoTable_.fastIoQueryBasicInfo       = ExfatFastIoQueryBasicInfo;
    fastIoTable_.fastIoQueryStandardInfo    = ExfatFastIoQueryStandardInfo;
    fastIoTable_.fastIoDeviceControl        = ExfatFastIoDeviceControl;

    driverObject_->fastIoDispatch = &fastIoTable_;

    initialized_ = true;
}

NtStatus ExfatDriver::registerVolume(std::shared_ptr<ExfatFileSystem> exfatFs, std::wstring_view volumeName) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!exfatFs) return NtStatus::InvalidParameter;

    mountedDrives_[std::wstring(volumeName)] = exfatFs;
    return NtStatus::Success;
}

NtStatus ExfatDriver::mountDrive(std::wstring_view driveLetter, std::shared_ptr<ExfatFileSystem> exfatFs) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!exfatFs) return NtStatus::InvalidParameter;

    std::wstring dl(driveLetter);
    if (!dl.empty() && dl.back() != L':') {
        dl.push_back(L':');
    }
    mountedDrives_[dl] = exfatFs;

    // Integrate with VirtualFileSystem and IoManager
    auto volDev = io::IoManager::get().createDevice(
        driverObject_.get(),
        std::wstring(L"\\Device\\ExFat_") + dl[0],
        io::DeviceType::FileSystem
    );
    (void)volDev;

    return NtStatus::Success;
}

std::shared_ptr<ExfatFileSystem> ExfatDriver::getFileSystem(std::wstring_view driveLetter) const {
    std::lock_guard<std::mutex> lock(driverMutex_);
    std::wstring dl(driveLetter);
    if (!dl.empty() && dl.back() != L':') {
        dl.push_back(L':');
    }
    auto it = mountedDrives_.find(dl);
    if (it != mountedDrives_.end()) {
        return it->second;
    }
    return nullptr;
}

NtStatus ExfatDriver::dispatchCreate(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 1; // FILE_OPENED
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchRead(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchWrite(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchClose(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchDirectoryControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchQueryInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchSetInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchFileSystemControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;

    auto* stack = irp->getCurrentStack();
    const uint32_t fsctl = stack ? stack->parameters.fileSystemControl.fsControlCode : 0;
    void* buffer = irp->systemBuffer ? irp->systemBuffer : irp->userBuffer;

    switch (fsctl) {
    case 0x00090078: { // FSCTL_IS_VOLUME_DIRTY
        if (!buffer) {
            irp->ioStatus.status = NtStatus::BufferTooSmall;
            return NtStatus::BufferTooSmall;
        }
        *static_cast<uint32_t*>(buffer) = 0; // Not dirty
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(uint32_t);
        return NtStatus::Success;
    }

    case 0x0009007C: { // FSCTL_MARK_VOLUME_DIRTY
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = 0;
        return NtStatus::Success;
    }

    case 0x00090018: // FSCTL_LOCK_VOLUME
    case 0x0009001C: // FSCTL_UNLOCK_VOLUME
    case 0x00090020: { // FSCTL_DISMOUNT_VOLUME
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = 0;
        return NtStatus::Success;
    }

    default:
        irp->ioStatus.status = NtStatus::NotSupported;
        return NtStatus::NotSupported;
    }
}

NtStatus ExfatDriver::dispatchDeviceControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchShutdown(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ExfatDriver::dispatchCleanup(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

} // namespace micant::exfat
