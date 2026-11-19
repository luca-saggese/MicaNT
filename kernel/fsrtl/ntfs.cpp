/**
 * @file ntfs.cpp
 @brief Luce OS Clean-Room NTFS File System Runtime Subsystem (ntfs.sys / NtfsDriver).
 *
 * Clean-Room ISO C++23. Sovereign MicaNT Kernel Architecture.
 * Executed in Kernel-Mode Ring 0 / Ring 1.
 */

#include "micant/km/drivers/fs/ntfs.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/drivers/fs/fs.hpp"

namespace micant::ntfs {

// Fast I/O Dispatch stubs
namespace {

bool NtfsFastIoCheckIfPossible(
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

bool NtfsFastIoRead(
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

bool NtfsFastIoWrite(
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

bool NtfsFastIoQueryBasicInfo(
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

bool NtfsFastIoQueryStandardInfo(
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

bool NtfsFastIoDeviceControl(
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

void NtfsDriver::initialize() {
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (initialized_) return;

    driverObject_ = std::make_unique<io::DriverObject>();
    driverObject_->driverName = L"\\FileSystem\\Ntfs";

    // Set Dispatch routines
    driverObject_->setDispatch(io::IRP_MJ_CREATE, &NtfsDriver::dispatchCreate);
    driverObject_->setDispatch(io::IRP_MJ_READ, &NtfsDriver::dispatchRead);
    driverObject_->setDispatch(io::IRP_MJ_WRITE, &NtfsDriver::dispatchWrite);
    driverObject_->setDispatch(io::IRP_MJ_CLOSE, &NtfsDriver::dispatchClose);
    driverObject_->setDispatch(io::IRP_MJ_DIRECTORY_CONTROL, &NtfsDriver::dispatchDirectoryControl);
    driverObject_->setDispatch(io::IRP_MJ_QUERY_INFORMATION, &NtfsDriver::dispatchQueryInformation);
    driverObject_->setDispatch(io::IRP_MJ_SET_INFORMATION, &NtfsDriver::dispatchSetInformation);
    driverObject_->setDispatch(io::IRP_MJ_FILE_SYSTEM_CONTROL, &NtfsDriver::dispatchFileSystemControl);
    driverObject_->setDispatch(io::IRP_MJ_DEVICE_CONTROL, &NtfsDriver::dispatchDeviceControl);
    driverObject_->setDispatch(io::IRP_MJ_SHUTDOWN, &NtfsDriver::dispatchShutdown);
    driverObject_->setDispatch(io::IRP_MJ_CLEANUP, &NtfsDriver::dispatchCleanup);

    // Set Fast I/O dispatch
    fastIoTable_.sizeOfFastIoDispatch       = sizeof(io::FastIoDispatch);
    fastIoTable_.fastIoCheckIfPossible      = NtfsFastIoCheckIfPossible;
    fastIoTable_.fastIoRead                 = NtfsFastIoRead;
    fastIoTable_.fastIoWrite                = NtfsFastIoWrite;
    fastIoTable_.fastIoQueryBasicInfo       = NtfsFastIoQueryBasicInfo;
    fastIoTable_.fastIoQueryStandardInfo    = NtfsFastIoQueryStandardInfo;
    fastIoTable_.fastIoDeviceControl        = NtfsFastIoDeviceControl;

    driverObject_->fastIoDispatch = &fastIoTable_;

    initialized_ = true;
}

NtStatus NtfsDriver::registerVolume(std::shared_ptr<NtfsFileSystem> ntfsFs, std::wstring_view volumeName) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!ntfsFs) return NtStatus::InvalidParameter;

    mountedDrives_[std::wstring(volumeName)] = ntfsFs;
    return NtStatus::Success;
}

NtStatus NtfsDriver::mountDrive(std::wstring_view driveLetter, std::shared_ptr<NtfsFileSystem> ntfsFs) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!ntfsFs) return NtStatus::InvalidParameter;

    std::wstring dl(driveLetter);
    if (!dl.empty() && dl.back() != L':') {
        dl.push_back(L':');
    }
    mountedDrives_[dl] = ntfsFs;

    // Integrate with VirtualFileSystem and IoManager
    auto volDev = io::IoManager::get().createDevice(
        driverObject_.get(),
        std::wstring(L"\\Device\\Ntfs_") + dl[0],
        io::DeviceType::FileSystem
    );
    (void)volDev;

    return NtStatus::Success;
}

std::shared_ptr<NtfsFileSystem> NtfsDriver::getFileSystem(std::wstring_view driveLetter) const {
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

NtStatus NtfsDriver::dispatchCreate(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 1; // FILE_OPENED
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchRead(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchWrite(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchClose(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchDirectoryControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchQueryInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchSetInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchFileSystemControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;

    auto* stack = irp->getCurrentStack();
    const uint32_t fsctl = stack ? stack->parameters.fileSystemControl.fsControlCode : 0;
    void* buffer = irp->systemBuffer ? irp->systemBuffer : irp->userBuffer;

    switch (fsctl) {
    case FSCTL_GET_NTFS_VOLUME_DATA: {
        if (!buffer) {
            irp->ioStatus.status = NtStatus::BufferTooSmall;
            return NtStatus::BufferTooSmall;
        }
        auto* vol = static_cast<NTFS_VOLUME_DATA_BUFFER*>(buffer);
        vol->volumeSerialNumber = 0x78E2A1C978E2A1C9ULL;
        vol->numberSectors = 268435456ULL;
        vol->totalClusters = 33554432ULL;
        vol->freeClusters = 30320520ULL;
        vol->totalReserved = 2048;
        vol->bytesPerSector = NTFS_SECTOR_SIZE;
        vol->bytesPerCluster = DEFAULT_CLUSTER_SIZE;
        vol->bytesPerFileRecordSegment = MFT_RECORD_SIZE;
        vol->clustersPerFileRecordSegment = 0;
        vol->mftValidDataLength = 1024 * 1024;
        vol->mftStartLcn = 4;
        vol->mft2StartLcn = 16;
        vol->mftZoneStart = 4;
        vol->mftZoneEnd = 4194304;

        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(NTFS_VOLUME_DATA_BUFFER);
        return NtStatus::Success;
    }

    case FSCTL_GET_NTFS_FILE_RECORD: {
        if (!buffer) {
            irp->ioStatus.status = NtStatus::BufferTooSmall;
            return NtStatus::BufferTooSmall;
        }
        auto* in = static_cast<const NTFS_FILE_RECORD_INPUT_BUFFER*>(buffer);
        auto* out = static_cast<NTFS_FILE_RECORD_OUTPUT_BUFFER*>(buffer);
        uint64_t frn = in->fileReferenceNumber;

        out->fileReferenceNumber = frn;
        out->fileRecordLength = MFT_RECORD_SIZE;
        std::memset(out->fileRecordBuffer, 0, sizeof(out->fileRecordBuffer));

        auto* hdr = reinterpret_cast<MftRecordHeader*>(out->fileRecordBuffer);
        hdr->magic = NTFS_FILE_SIGNATURE;
        hdr->usaOffset = 0x30;
        hdr->usaCount = 3;
        hdr->lsn = 100;
        hdr->sequenceNumber = 1;
        hdr->hardLinkCount = 1;
        hdr->firstAttributeOffset = 0x38;
        hdr->flags = (frn == 5 || frn == 11) ? 0x0003 : 0x0001; // Root/Extend is DIRECTORY
        hdr->bytesInUse = 1024;
        hdr->bytesAllocated = 1024;
        hdr->recordNumber = static_cast<uint32_t>(frn);

        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER);
        return NtStatus::Success;
    }

    case FSCTL_IS_VOLUME_DIRTY: {
        if (!buffer) {
            irp->ioStatus.status = NtStatus::BufferTooSmall;
            return NtStatus::BufferTooSmall;
        }
        *static_cast<uint32_t*>(buffer) = 0; // Not dirty
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(uint32_t);
        return NtStatus::Success;
    }

    case FSCTL_MARK_VOLUME_DIRTY: {
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = 0;
        return NtStatus::Success;
    }

    case FSCTL_LOCK_VOLUME:
    case FSCTL_UNLOCK_VOLUME:
    case FSCTL_DISMOUNT_VOLUME: {
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = 0;
        return NtStatus::Success;
    }

    case FSCTL_ENUM_USN_DATA:
    case FSCTL_READ_USN_JOURNAL: {
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = 0;
        return NtStatus::Success;
    }

    default:
        irp->ioStatus.status = NtStatus::NotSupported;
        return NtStatus::NotSupported;
    }
}

NtStatus NtfsDriver::dispatchDeviceControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchShutdown(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus NtfsDriver::dispatchCleanup(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

} // namespace micant::ntfs
