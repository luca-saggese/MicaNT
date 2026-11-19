/**
 * @file cdfs.cpp
 @brief Luce OS Clean-Room CD-ROM / DVD (CDFS / UDFS) File System Runtime Subsystem.
 *
 * Clean-Room ISO C++23. Sovereign MicaNT Kernel Architecture.
 * Executed in Kernel-Mode Ring 0 / Ring 1.
 */

#include "micant/km/drivers/fs/cdfs.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/drivers/fs/fs.hpp"

namespace micant::cdfs {

// ============================================================================
// Fast I/O Dispatch Stubs
// ============================================================================
namespace {

bool CdfsFastIoCheckIfPossible(
    void* /*fileObject*/,
    io::LargeInteger* /*offset*/,
    uint32_t /*length*/,
    bool /*wait*/,
    uint32_t /*lockKey*/,
    bool checkForRead,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    // Write is impossible on optical media
    if (!checkForRead) {
        if (ioStatus) {
            ioStatus->status = NtStatus::MediaWriteProtected;
            ioStatus->information = 0;
        }
        return false;
    }
    if (ioStatus) {
        ioStatus->status = NtStatus::Success;
        ioStatus->information = 0;
    }
    return true;
}

bool CdfsFastIoRead(
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

bool CdfsFastIoWrite(
    void* /*fileObject*/,
    io::LargeInteger* /*offset*/,
    uint32_t /*length*/,
    bool /*wait*/,
    const void* /*buffer*/,
    io::IoStatusBlock* ioStatus,
    io::DeviceObject* /*dev*/
) noexcept {
    if (ioStatus) {
        ioStatus->status = NtStatus::MediaWriteProtected;
        ioStatus->information = 0;
    }
    return false;
}

bool CdfsFastIoQueryBasicInfo(
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

bool CdfsFastIoQueryStandardInfo(
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

} // namespace

// ============================================================================
// CdfsDriver Implementation (\FileSystem\Cdfs)
// ============================================================================

void CdfsDriver::initialize() {
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (initialized_) return;

    driverObject_ = std::make_unique<io::DriverObject>();
    driverObject_->driverName = L"\\FileSystem\\Cdfs";

    // Set Dispatch routines
    driverObject_->setDispatch(io::IRP_MJ_CREATE, &CdfsDriver::dispatchCreate);
    driverObject_->setDispatch(io::IRP_MJ_READ, &CdfsDriver::dispatchRead);
    driverObject_->setDispatch(io::IRP_MJ_WRITE, &CdfsDriver::dispatchWrite);
    driverObject_->setDispatch(io::IRP_MJ_CLOSE, &CdfsDriver::dispatchClose);
    driverObject_->setDispatch(io::IRP_MJ_DIRECTORY_CONTROL, &CdfsDriver::dispatchDirectoryControl);
    driverObject_->setDispatch(io::IRP_MJ_QUERY_INFORMATION, &CdfsDriver::dispatchQueryInformation);
    driverObject_->setDispatch(io::IRP_MJ_SET_INFORMATION, &CdfsDriver::dispatchSetInformation);
    driverObject_->setDispatch(io::IRP_MJ_FILE_SYSTEM_CONTROL, &CdfsDriver::dispatchFileSystemControl);
    driverObject_->setDispatch(io::IRP_MJ_DEVICE_CONTROL, &CdfsDriver::dispatchDeviceControl);
    driverObject_->setDispatch(io::IRP_MJ_SHUTDOWN, &CdfsDriver::dispatchShutdown);
    driverObject_->setDispatch(io::IRP_MJ_CLEANUP, &CdfsDriver::dispatchCleanup);

    // Fast I/O dispatch
    fastIoTable_.sizeOfFastIoDispatch       = sizeof(io::FastIoDispatch);
    fastIoTable_.fastIoCheckIfPossible      = CdfsFastIoCheckIfPossible;
    fastIoTable_.fastIoRead                 = CdfsFastIoRead;
    fastIoTable_.fastIoWrite                = CdfsFastIoWrite;
    fastIoTable_.fastIoQueryBasicInfo       = CdfsFastIoQueryBasicInfo;
    fastIoTable_.fastIoQueryStandardInfo    = CdfsFastIoQueryStandardInfo;

    driverObject_->fastIoDispatch = &fastIoTable_;
    initialized_ = true;
}

NtStatus CdfsDriver::registerVolume(std::shared_ptr<CdfsFileSystem> cdfsFs, std::wstring_view volumeName) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!cdfsFs) return NtStatus::InvalidParameter;

    mountedDrives_[std::wstring(volumeName)] = cdfsFs;
    return NtStatus::Success;
}

NtStatus CdfsDriver::mountDrive(std::wstring_view driveLetter, std::shared_ptr<CdfsFileSystem> cdfsFs) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!cdfsFs) return NtStatus::InvalidParameter;

    std::wstring dl(driveLetter);
    if (!dl.empty() && dl.back() != L':') {
        dl.push_back(L':');
    }
    mountedDrives_[dl] = cdfsFs;

    auto volDev = io::IoManager::get().createDevice(
        driverObject_.get(),
        std::wstring(L"\\Device\\CdRom_") + dl[0],
        io::DeviceType::CdRomFileSystem
    );
    (void)volDev;

    return NtStatus::Success;
}

std::shared_ptr<CdfsFileSystem> CdfsDriver::getFileSystem(std::wstring_view driveLetter) const {
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

NtStatus CdfsDriver::dispatchCreate(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 1; // FILE_OPENED
    return NtStatus::Success;
}

NtStatus CdfsDriver::dispatchRead(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus CdfsDriver::dispatchWrite(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::MediaWriteProtected; // 0xC00000A2
    irp->ioStatus.information = 0;
    return NtStatus::MediaWriteProtected;
}

NtStatus CdfsDriver::dispatchClose(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus CdfsDriver::dispatchDirectoryControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus CdfsDriver::dispatchQueryInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus CdfsDriver::dispatchSetInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) {
        irp->ioStatus.status = NtStatus::MediaWriteProtected;
        return NtStatus::MediaWriteProtected;
    }
    return NtStatus::MediaWriteProtected;
}

NtStatus CdfsDriver::dispatchFileSystemControl(io::DeviceObject* /*device*/, io::Irp* irp) {
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
        *static_cast<uint32_t*>(buffer) = 0; // Optical media is never dirty
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(uint32_t);
        return NtStatus::Success;
    }

    case 0x00090018: // FSCTL_LOCK_VOLUME
    case 0x0009001C: // FSCTL_UNLOCK_VOLUME
    case 0x00090020: // FSCTL_DISMOUNT_VOLUME
    case 0x00090028: { // FSCTL_IS_VOLUME_MOUNTED
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = 0;
        return NtStatus::Success;
    }

    default:
        irp->ioStatus.status = NtStatus::NotSupported;
        return NtStatus::NotSupported;
    }
}

NtStatus CdfsDriver::dispatchDeviceControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus CdfsDriver::dispatchShutdown(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus CdfsDriver::dispatchCleanup(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

// ============================================================================
// UdfsDriver Implementation (\FileSystem\Udfs)
// ============================================================================

void UdfsDriver::initialize() {
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (initialized_) return;

    driverObject_ = std::make_unique<io::DriverObject>();
    driverObject_->driverName = L"\\FileSystem\\Udfs";

    driverObject_->setDispatch(io::IRP_MJ_CREATE, &UdfsDriver::dispatchCreate);
    driverObject_->setDispatch(io::IRP_MJ_READ, &UdfsDriver::dispatchRead);
    driverObject_->setDispatch(io::IRP_MJ_WRITE, &UdfsDriver::dispatchWrite);
    driverObject_->setDispatch(io::IRP_MJ_CLOSE, &UdfsDriver::dispatchClose);
    driverObject_->setDispatch(io::IRP_MJ_FILE_SYSTEM_CONTROL, &UdfsDriver::dispatchFileSystemControl);

    fastIoTable_.sizeOfFastIoDispatch       = sizeof(io::FastIoDispatch);
    fastIoTable_.fastIoCheckIfPossible      = CdfsFastIoCheckIfPossible;
    fastIoTable_.fastIoRead                 = CdfsFastIoRead;
    fastIoTable_.fastIoWrite                = CdfsFastIoWrite;

    driverObject_->fastIoDispatch = &fastIoTable_;
    initialized_ = true;
}

NtStatus UdfsDriver::registerVolume(std::shared_ptr<CdfsFileSystem> cdfsFs, std::wstring_view volumeName) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!cdfsFs) return NtStatus::InvalidParameter;

    mountedDrives_[std::wstring(volumeName)] = cdfsFs;
    return NtStatus::Success;
}

NtStatus UdfsDriver::mountDrive(std::wstring_view driveLetter, std::shared_ptr<CdfsFileSystem> cdfsFs) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!cdfsFs) return NtStatus::InvalidParameter;

    std::wstring dl(driveLetter);
    if (!dl.empty() && dl.back() != L':') {
        dl.push_back(L':');
    }
    mountedDrives_[dl] = cdfsFs;
    return NtStatus::Success;
}

std::shared_ptr<CdfsFileSystem> UdfsDriver::getFileSystem(std::wstring_view driveLetter) const {
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

NtStatus UdfsDriver::dispatchCreate(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 1;
    return NtStatus::Success;
}

NtStatus UdfsDriver::dispatchRead(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus UdfsDriver::dispatchWrite(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::MediaWriteProtected;
    return NtStatus::MediaWriteProtected;
}

NtStatus UdfsDriver::dispatchClose(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus UdfsDriver::dispatchFileSystemControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

} // namespace micant::cdfs
