/**
 * @file refs.cpp
 @brief Luce OS Clean-Room Resilient File System (ReFS v3.x) Runtime Subsystem.
 *
 * Clean-Room ISO C++23. Sovereign MicaNT Kernel Architecture.
 * Executed in Kernel-Mode Ring 0 / Ring 1.
 */

#include "micant/km/drivers/fs/refs.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/drivers/fs/fs.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#include "micant/um/servers/scm.hpp"

namespace micant::refs {

void RegisterRefsSubsystem() {
    // 1. Version Database Registration
    auto& vdb = micant::version::VersionDatabase::Instance();
    vdb.RegisterModule("refs.sys", "10.0.26100.1", "Resilient File System Driver (TitanReFS)");
    vdb.RegisterModule("refsutil.exe", "10.0.26100.1", "ReFS Volume Maintenance & Salvage Utility");

    // 2. SCM Registration
    auto& scm = micant::scm::ServiceControlManager::get();
    auto rec = std::make_shared<micant::scm::ServiceRecord>();
    rec->serviceName = L"ReFS";
    rec->displayName = L"Resilient File System Driver";
    rec->serviceType = micant::scm::SERVICE_KERNEL_DRIVER;
    rec->startType = micant::scm::SERVICE_BOOT_START;
    rec->binaryPath = L"C:\\Windows\\System32\\drivers\\refs.sys";
    rec->status.dwCurrentState = micant::scm::SERVICE_RUNNING;
    scm.registerServiceRecord(rec);

    // Initialize core subsystem singleton
    RefsSubsystem::get().initialize();
}

// Fast I/O Dispatch stubs
namespace {

bool RefsFastIoCheckIfPossible(
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

bool RefsFastIoRead(
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

bool RefsFastIoWrite(
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

bool RefsFastIoQueryBasicInfo(
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

bool RefsFastIoQueryStandardInfo(
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

void RefsDriver::initialize() {
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (initialized_) return;

    driverObject_ = std::make_unique<io::DriverObject>();
    driverObject_->driverName = L"\\FileSystem\\ReFS";

    // Set Dispatch routines
    driverObject_->setDispatch(io::IRP_MJ_CREATE, &RefsDriver::dispatchCreate);
    driverObject_->setDispatch(io::IRP_MJ_READ, &RefsDriver::dispatchRead);
    driverObject_->setDispatch(io::IRP_MJ_WRITE, &RefsDriver::dispatchWrite);
    driverObject_->setDispatch(io::IRP_MJ_CLOSE, &RefsDriver::dispatchClose);
    driverObject_->setDispatch(io::IRP_MJ_DIRECTORY_CONTROL, &RefsDriver::dispatchDirectoryControl);
    driverObject_->setDispatch(io::IRP_MJ_QUERY_INFORMATION, &RefsDriver::dispatchQueryInformation);
    driverObject_->setDispatch(io::IRP_MJ_SET_INFORMATION, &RefsDriver::dispatchSetInformation);
    driverObject_->setDispatch(io::IRP_MJ_FILE_SYSTEM_CONTROL, &RefsDriver::dispatchFileSystemControl);
    driverObject_->setDispatch(io::IRP_MJ_DEVICE_CONTROL, &RefsDriver::dispatchDeviceControl);
    driverObject_->setDispatch(io::IRP_MJ_SHUTDOWN, &RefsDriver::dispatchShutdown);
    driverObject_->setDispatch(io::IRP_MJ_CLEANUP, &RefsDriver::dispatchCleanup);

    // Fast I/O table
    fastIoTable_.sizeOfFastIoDispatch       = sizeof(io::FastIoDispatch);
    fastIoTable_.fastIoCheckIfPossible      = RefsFastIoCheckIfPossible;
    fastIoTable_.fastIoRead                 = RefsFastIoRead;
    fastIoTable_.fastIoWrite                = RefsFastIoWrite;
    fastIoTable_.fastIoQueryBasicInfo       = RefsFastIoQueryBasicInfo;
    fastIoTable_.fastIoQueryStandardInfo    = RefsFastIoQueryStandardInfo;

    driverObject_->fastIoDispatch = &fastIoTable_;
    initialized_ = true;
}

NtStatus RefsDriver::registerVolume(std::shared_ptr<RefsFileSystem> refsFs, std::wstring_view volumeName) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!refsFs) return NtStatus::InvalidParameter;

    mountedDrives_[std::wstring(volumeName)] = refsFs;
    return NtStatus::Success;
}

NtStatus RefsDriver::mountDrive(std::wstring_view driveLetter, std::shared_ptr<RefsFileSystem> refsFs) {
    initialize();
    std::lock_guard<std::mutex> lock(driverMutex_);
    if (!refsFs) return NtStatus::InvalidParameter;

    std::wstring dl(driveLetter);
    if (!dl.empty() && dl.back() != L':') {
        dl.push_back(L':');
    }
    mountedDrives_[dl] = refsFs;

    auto volDev = io::IoManager::get().createDevice(
        driverObject_.get(),
        std::wstring(L"\\Device\\ReFS_") + dl[0],
        io::DeviceType::FileSystem
    );
    (void)volDev;

    return NtStatus::Success;
}

std::shared_ptr<RefsFileSystem> RefsDriver::getFileSystem(std::wstring_view driveLetter) const {
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

NtStatus RefsDriver::dispatchCreate(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 1;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchRead(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchWrite(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchClose(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchDirectoryControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchQueryInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchSetInformation(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchFileSystemControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;

    auto* stack = irp->getCurrentStack();
    const uint32_t fsctl = stack ? stack->parameters.fileSystemControl.fsControlCode : 0;
    void* buffer = irp->systemBuffer ? irp->systemBuffer : irp->userBuffer;

    switch (fsctl) {
    case FSCTL_GET_REFS_VOLUME_DATA: {
        if (!buffer) return NtStatus::BufferTooSmall;
        auto* data = static_cast<REFS_VOLUME_DATA_BUFFER*>(buffer);
        data->volumeSerialNumber = 0x8E21F4A0B9C371D5ULL;
        data->bytesPerSector = 512;
        data->bytesPerCluster = 4096;
        data->majorVersion = REFS_MAJOR_VERSION_3;
        data->minorVersion = REFS_MINOR_VERSION_4;
        data->rootDirectoryTableId = REFS_TABLE_ROOT_DIRECTORY;
        data->isIntegrityEnabled = 1;
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(REFS_VOLUME_DATA_BUFFER);
        return NtStatus::Success;
    }

    case FSCTL_DUPLICATE_EXTENTS_TO_FILE: {
        // Fast Block Cloning dispatch
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = 0;
        return NtStatus::Success;
    }

    case 0x00090078: { // FSCTL_IS_VOLUME_DIRTY
        if (!buffer) return NtStatus::BufferTooSmall;
        *static_cast<uint32_t*>(buffer) = 0; // Not dirty
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(uint32_t);
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

NtStatus RefsDriver::dispatchDeviceControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchShutdown(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus RefsDriver::dispatchCleanup(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (irp) irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

} // namespace micant::refs
