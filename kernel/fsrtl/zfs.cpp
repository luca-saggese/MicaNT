/**
 * @file zfs.cpp
 @brief Luce OS Clean-Room ZFS (Zettabyte File System) Kernel Driver Subsystem (zfs.sys / ZfsDriver).
 *
 * Clean-Room ISO C++23. Sovereign MicaNT Kernel Architecture.
 * Executed in Kernel-Mode Ring 0 / Ring 1.
 */

#include "micant/km/drivers/fs/zfs.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/drivers/fs/fs.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#include "micant/um/servers/scm.hpp"

namespace micant::zfs {

void ZfsSubsystem::initialize() {
    std::lock_guard<std::mutex> lock(subMutex_);
    if (initialized_) return;

    // Create 3 virtual disk devices for the default resilient storage pool "tank"
    auto disk1Dev = std::make_shared<storage::RamDiskDevice>(L"\\Device\\Harddisk1\\Partition0", 64 * 1024 * 1024, 4096);
    auto disk2Dev = std::make_shared<storage::RamDiskDevice>(L"\\Device\\Harddisk2\\Partition0", 64 * 1024 * 1024, 4096);
    auto disk3Dev = std::make_shared<storage::RamDiskDevice>(L"\\Device\\Harddisk3\\Partition0", 64 * 1024 * 1024, 4096);

    auto disk1 = std::make_shared<DiskVdev>("c1t0d0", disk1Dev, 0x1111222233334444ULL);
    auto disk2 = std::make_shared<DiskVdev>("c1t1d0", disk2Dev, 0x5555666677778888ULL);
    auto disk3 = std::make_shared<DiskVdev>("c1t2d0", disk3Dev, 0x9999AAAABBBBCCCCULL);

    // Create a 3-Disk RAID-Z1 VDEV
    std::vector<std::shared_ptr<IVdev>> raidzDisks = {disk1, disk2, disk3};
    auto rootRaidz = std::make_shared<Raidz1Vdev>("raidz1-0", raidzDisks, 0xAAAA4242CCCC1337ULL);

    // Instantiate default storage pool "tank"
    auto tankPool = std::make_shared<ZfsPool>("tank", rootRaidz);
    pools_["tank"] = tankPool;

    // Create primary dataset "tank/data" mounted to Drive Z:
    tankPool->createDataset("tank/data");
    auto dataDs = tankPool->getDataset("tank/data");
    if (dataDs) {
        dataDs->setMountpoint("Z:");
        dataDs->createFile(L"README.txt", 0x20, false);
        const char readmeMsg[] = "MicaNT TitanZFS Subsystem\n"
                                 "Enterprise RAID-Z1 Fault-Tolerant Storage Pool (tank/data)\n"
                                 "256-Bit Fletcher-4 Checksums & Self-Healing Enabled.\n";
        uint64_t written = 0;
        dataDs->writeFile(L"README.txt", readmeMsg, sizeof(readmeMsg) - 1, 0, written);

        dataDs->createFile(L"PoolStats.log", 0x20, false);
        const char statsMsg[] = "[ZFS] Pool 'tank' ONLINE. 3 VDEVs active. 0 errors.\n";
        dataDs->writeFile(L"PoolStats.log", statsMsg, sizeof(statsMsg) - 1, 0, written);
    }

    primaryFs_ = std::make_shared<ZfsFileSystem>();
    primaryFs_->mount(tankPool, "tank/data");

    // Also mount into VirtualFileSystem as Drive Z:
    fs::VirtualFileSystem::get().setMountedZfs(primaryFs_);

    initialized_ = true;
}

NtStatus ZfsSubsystem::registerPool(std::shared_ptr<ZfsPool> pool) {
    if (!pool) return NtStatus::InvalidParameter;
    std::lock_guard<std::mutex> lock(subMutex_);
    if (pools_.contains(pool->getName())) return NtStatus::ObjectNameCollision;
    pools_[pool->getName()] = pool;
    return NtStatus::Success;
}

std::shared_ptr<ZfsPool> ZfsSubsystem::getPool(std::string_view name) const {
    std::lock_guard<std::mutex> lock(subMutex_);
    auto it = pools_.find(std::string(name));
    return (it != pools_.end()) ? it->second : nullptr;
}

std::vector<std::shared_ptr<ZfsPool>> ZfsSubsystem::getAllPools() const {
    std::lock_guard<std::mutex> lock(subMutex_);
    std::vector<std::shared_ptr<ZfsPool>> res;
    for (const auto& [_, p] : pools_) res.push_back(p);
    return res;
}

void ZfsSubsystem::destroyPool(std::string_view name) {
    std::lock_guard<std::mutex> lock(subMutex_);
    pools_.erase(std::string(name));
}

void RegisterZfsSubsystem() {
    // 1. Version Database Registration
    auto& vdb = micant::version::VersionDatabase::Instance();
    vdb.RegisterModule("zfs.sys", "10.0.26100.1", "Zettabyte File System Driver (TitanZFS)");
    vdb.RegisterModule("zpool.exe", "10.0.26100.1", "ZFS Storage Pool Configuration & Diagnostic CLI");
    vdb.RegisterModule("zfs.exe", "10.0.26100.1", "ZFS Dataset & Snapshot Management CLI");

    // 2. SCM Registration
    auto& scm = micant::scm::ServiceControlManager::get();
    auto rec = std::make_shared<micant::scm::ServiceRecord>();
    rec->serviceName = L"Zfs";
    rec->displayName = L"ZFS Storage Pool & RAID Driver";
    rec->serviceType = micant::scm::SERVICE_KERNEL_DRIVER;
    rec->startType = micant::scm::SERVICE_SYSTEM_START;
    rec->binaryPath = L"C:\\Windows\\System32\\drivers\\zfs.sys";
    rec->status.dwCurrentState = micant::scm::SERVICE_RUNNING;
    scm.registerServiceRecord(rec);

    // Initialize core subsystem and driver singletons
    ZfsSubsystem::get().initialize();
    ZfsDriver::get().initialize();
}

// Fast I/O Dispatch stubs
namespace {

bool ZfsFastIoCheckIfPossible(
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

bool ZfsFastIoRead(
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

bool ZfsFastIoWrite(
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

} // anonymous namespace

void ZfsDriver::initialize() {
    if (initialized_) return;

    driverObject_ = std::make_unique<io::DriverObject>();
    driverObject_->driverName = L"\\FileSystem\\Zfs";

    // Setup Fast I/O
    fastIoTable_.sizeOfFastIoDispatch = sizeof(io::FastIoDispatch);
    fastIoTable_.fastIoCheckIfPossible = ZfsFastIoCheckIfPossible;
    fastIoTable_.fastIoRead = ZfsFastIoRead;
    fastIoTable_.fastIoWrite = ZfsFastIoWrite;
    driverObject_->fastIoDispatch = &fastIoTable_;

    // Setup Major Functions
    driverObject_->setDispatch(io::IRP_MJ_CREATE, &ZfsDriver::dispatchCreate);
    driverObject_->setDispatch(io::IRP_MJ_CLOSE, &ZfsDriver::dispatchClose);
    driverObject_->setDispatch(io::IRP_MJ_READ, &ZfsDriver::dispatchRead);
    driverObject_->setDispatch(io::IRP_MJ_WRITE, &ZfsDriver::dispatchWrite);
    driverObject_->setDispatch(io::IRP_MJ_DEVICE_CONTROL, &ZfsDriver::dispatchDeviceControl);
    driverObject_->setDispatch(io::IRP_MJ_FILE_SYSTEM_CONTROL, &ZfsDriver::dispatchFileSystemControl);

    // Create \FileSystem\Zfs and \Device\Zfs
    auto fsDevice = io::IoManager::get().createDevice(
        driverObject_.get(),
        L"\\FileSystem\\Zfs",
        io::DeviceType::FileSystem
    );
    (void)fsDevice;

    auto ctrlDevice = io::IoManager::get().createDevice(
        driverObject_.get(),
        L"\\Device\\Zfs",
        io::DeviceType::FileSystem
    );
    (void)ctrlDevice;

    initialized_ = true;
}

NtStatus ZfsDriver::dispatchCreate(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 1; // FILE_OPENED
    return NtStatus::Success;
}

NtStatus ZfsDriver::dispatchClose(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 0;
    return NtStatus::Success;
}

NtStatus ZfsDriver::dispatchRead(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ZfsDriver::dispatchWrite(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    return NtStatus::Success;
}

NtStatus ZfsDriver::dispatchDeviceControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    irp->ioStatus.status = NtStatus::Success;
    irp->ioStatus.information = 0;
    return NtStatus::Success;
}

NtStatus ZfsDriver::dispatchFileSystemControl(io::DeviceObject* /*device*/, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;

    auto* stack = irp->getCurrentStack();
    const uint32_t fsctl = stack ? stack->parameters.fileSystemControl.fsControlCode : 0;
    void* buffer = irp->systemBuffer ? irp->systemBuffer : irp->userBuffer;

    switch (fsctl) {
    case FSCTL_ZFS_POOL_STATUS: {
        auto pools = ZfsSubsystem::get().getAllPools();
        if (buffer) {
            *static_cast<uint32_t*>(buffer) = static_cast<uint32_t>(pools.size());
        }
        irp->ioStatus.status = NtStatus::Success;
        irp->ioStatus.information = sizeof(uint32_t);
        return NtStatus::Success;
    }

    case FSCTL_ZFS_POOL_SCRUB: {
        auto pool = ZfsSubsystem::get().getPool("tank");
        if (pool) {
            pool->scrubPool();
            irp->ioStatus.status = NtStatus::Success;
        } else {
            irp->ioStatus.status = NtStatus::NoSuchDevice;
        }
        irp->ioStatus.information = 0;
        return irp->ioStatus.status;
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

} // namespace micant::zfs
