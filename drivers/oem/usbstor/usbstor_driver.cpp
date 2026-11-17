// USB bulk-only mass-storage class. USBD owns host transfers; this image owns
// BOT/SCSI state and publishes a read-only generic Io block device.
#include "micant/shared/boot_driver.hpp"
#include "micant/shared/usb_class_abi.hpp"
#include "../usb_class_descriptors.hpp"
#include "usbstor_class.hpp"
#include <atomic>

namespace {
using namespace micant;
using namespace micant::boot_driver;
using namespace micant::shared::usb;
using micant::usb::baremetal::MassStorage;
using micant::usb::baremetal::BulkTransport;
constexpr int32_t kBusy = static_cast<int32_t>(0x80000011u);
struct Device {
    std::atomic<bool> used{false};
    InterfaceV1 interface{};
    TransferServicesV1 transfer{};
    MassStorage storage{};
    uint64_t block{}, mount{};
    bool initialized{};
    bool published{};
    bool fileVerified{};
};
Device devices[4]{};
const KernelServices* services{};
uint64_t registration{};
void logStatus(const char* prefix, uint32_t status) noexcept {
    if (!services || !services->Log || !prefix) return;
    char message[112]{};
    size_t length{};
    while (prefix[length] && length + 9 < sizeof(message)) {
        message[length] = prefix[length];
        ++length;
    }
    static constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        message[length++] = digits[(status >> shift) & 0xf];
    message[length] = '\0';
    services->Log(services->Context, message);
}
int32_t match(void*, const uint8_t* bytes, size_t size, InterfaceV1* output) {
    if (!output) return micant::boot_driver::STATUS_INVALID_PARAMETER;
    micant::usb::baremetal::StorageInterface storage{};
    if (!micant::usb::baremetal::findBulkStorage(bytes, size, storage) || storage.alternate)
        return micant::boot_driver::STATUS_NOT_SUPPORTED;
    if (services && services->Log)
        services->Log(services->Context,
            "usbstor.sys: matched USB Mass Storage BOT/SCSI interface");
    *output = {};
    output->Size = sizeof(*output); output->Kind = TransferKind::BulkPair;
    output->Configuration = storage.configuration; output->InterfaceNumber = storage.interfaceNumber;
    output->Alternate = storage.alternate; output->InEndpoint = storage.bulkIn;
    output->OutEndpoint = storage.bulkOut; output->InPacket = storage.inPacket; output->OutPacket = storage.outPacket;
    output->InBurst = storage.inBurst; output->OutBurst = storage.outBurst;
    output->InCompanion = storage.inCompanion; output->OutCompanion = storage.outCompanion;
    return micant::boot_driver::STATUS_SUCCESS;
}
int32_t create(void*, const InterfaceV1* info, const TransferServicesV1* transfer,
               uint16_t, void** result) {
    if (!info || !transfer || !result || info->Size < sizeof(*info) ||
        transfer->Size < sizeof(*transfer) || transfer->Version != TransferServicesVersion ||
        !transfer->Begin || !transfer->End || !transfer->BulkTransfer || !transfer->BulkReset ||
        info->Kind != TransferKind::BulkPair) return micant::boot_driver::STATUS_INVALID_PARAMETER;
    *result = nullptr;
    for (auto& d : devices) {
        bool expected = false;
        if (!d.used.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) continue;
        d.interface = *info; d.transfer = *transfer; d.block = d.mount = 0;
        d.initialized = d.published = d.fileVerified = false;
        *result = &d; return micant::boot_driver::STATUS_SUCCESS;
    }
    return kBusy;
}
NtStatus transfer(void* context, uint8_t endpoint, void* data, size_t length,
                  size_t& actual, uint32_t timeout) noexcept {
    auto& d = *static_cast<Device*>(context);
    return static_cast<NtStatus>(static_cast<uint32_t>(d.transfer.BulkTransfer(
        d.transfer.Context, endpoint, data, length, &actual, timeout)));
}
NtStatus reset(void* context, uint8_t interfaceNumber, uint8_t input, uint8_t output) noexcept {
    auto& d = *static_cast<Device*>(context);
    return static_cast<NtStatus>(static_cast<uint32_t>(d.transfer.BulkReset(
        d.transfer.Context, interfaceNumber, input, output)));
}
bool begin(void* context) noexcept {
    auto& d = *static_cast<Device*>(context); return d.transfer.Begin(d.transfer.Context);
}
void end(void* context) noexcept {
    auto& d = *static_cast<Device*>(context); d.transfer.End(d.transfer.Context);
}
int32_t read(void* context, uint64_t lba, uint32_t count, void* output, uint32_t* done) {
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<Device*>(context)->storage.read(lba, count, output, done)));
}
void release(void* context) { static_cast<Device*>(context)->storage.releaseBackend(); }
int32_t start(void* context) {
    auto* d = static_cast<Device*>(context);
    if (!d || !d->used.load(std::memory_order_acquire) || !services) return micant::boot_driver::STATUS_INVALID_PARAMETER;
    if (!d->initialized) {
        const BulkTransport transport{d, transfer, reset, 4096, begin, end};
        const auto result = d->storage.initialize(transport, d->interface.InterfaceNumber,
                                                   d->interface.InEndpoint, d->interface.OutEndpoint);
        if (result != NtStatus::Success) {
            if (services->Log) services->Log(services->Context,
                "usbstor.sys: BOT/SCSI probe or READ CAPACITY failed");
            logStatus("usbstor.sys: BOT/SCSI status=0x", static_cast<uint32_t>(result));
            return static_cast<int32_t>(static_cast<uint32_t>(result));
        }
        d->initialized = true;
    }
    if (!d->block) {
        uint64_t sectors{}; uint32_t sectorSize{};
        if (!d->storage.retainBackend(sectors, sectorSize)) return kBusy;
        const auto result = services->CreateReadOnlyBlockDevice(services->Context, sectors, sectorSize,
            d, read, release, 0x00120089, &d->block);
        if (result != micant::boot_driver::STATUS_SUCCESS) {
            if (services->Log) services->Log(services->Context,
                "usbstor.sys: canonical read-only block publication failed");
            logStatus("usbstor.sys: block publication status=0x", static_cast<uint32_t>(result));
            return result;
        }
    }
    if (!d->mount) {
        const auto result = services->MountFatBlockDevice(services->Context, d->block, &d->mount);
        if (result != micant::boot_driver::STATUS_SUCCESS) {
            if (services->Log) services->Log(services->Context,
                "usbstor.sys: canonical FAT32 mount failed");
            logStatus("usbstor.sys: FAT mount status=0x", static_cast<uint32_t>(result));
            return result;
        }
    }
    if (!d->published && services->Log) {
        services->Log(services->Context,
            "usbstor.sys: BOT/SCSI device mounted through canonical FAT block path");
        d->published = true;
    }
    if (!d->fileVerified && services->ReadFatFileFromMount) {
        static constexpr char16_t path[] = u"MicaNT\\System32\\micant-usb-proof.txt";
        static constexpr char expected[] = "MicaNT USB mass-storage FAT32 post-EBS proof v1\n";
        char contents[sizeof(expected)]{};
        size_t bytesRead{};
        const auto readStatus = services->ReadFatFileFromMount(services->Context, d->mount,
            path, sizeof(path) / sizeof(path[0]) - 1, contents, sizeof(contents), &bytesRead);
        if (readStatus == micant::boot_driver::STATUS_SUCCESS && bytesRead == sizeof(expected) - 1 &&
            std::memcmp(contents, expected, sizeof(expected) - 1) == 0) {
            d->fileVerified = true;
            if (services->Log) services->Log(services->Context,
                "usbstor.sys: verified known file bytes through USB block device and FAT32");
        } else if (services->Log) {
            services->Log(services->Context,
                "usbstor.sys: known FAT32 file read did not match the QEMU fixture");
        }
    }
    return micant::boot_driver::STATUS_SUCCESS;
}
int32_t remove(void* context) {
    auto* d = static_cast<Device*>(context);
    if (!d || !d->used.load(std::memory_order_acquire) || !services) return micant::boot_driver::STATUS_INVALID_PARAMETER;
    d->storage.detach();
    if (d->mount) {
        if (services->UnmountFatBlockDevice(services->Context, d->mount) != micant::boot_driver::STATUS_SUCCESS) return kBusy;
        d->mount = 0;
    }
    if (d->block) {
        if (services->CloseBlockDevice(services->Context, d->block) != micant::boot_driver::STATUS_SUCCESS) return kBusy;
        d->block = 0;
    }
    if (!d->storage.canDestroy()) return kBusy;
    d->initialized = d->published = d->fileVerified = false;
    d->interface = {}; d->transfer = {};
    d->used.store(false, std::memory_order_release);
    return micant::boot_driver::STATUS_SUCCESS;
}
void unload(DriverObject* driver) noexcept {
    if (!services) return;
    if (registration && services->UnregisterUsbClass(services->Context, registration) != micant::boot_driver::STATUS_SUCCESS) return;
    registration = 0; services = nullptr;
    if (driver) driver->DriverUnload = nullptr;
}
}
extern "C" int32_t DriverEntry(micant::boot_driver::DriverObject* driver,
                                const micant::boot_driver::UnicodeString*) {
    if (!driver || driver->Size < sizeof(*driver) || driver->Version != DRIVER_ABI_VERSION || !driver->HardwareResources)
        return micant::boot_driver::STATUS_INVALID_PARAMETER;
    const auto& s = *static_cast<const KernelServices*>(driver->HardwareResources);
    if (s.Size < sizeof(KernelServices) || s.Version != KERNEL_SERVICES_VERSION ||
        s.UsbClassVersion != ClassServicesVersion || !s.RegisterUsbClass || !s.UnregisterUsbClass ||
        !s.CreateReadOnlyBlockDevice || !s.CloseBlockDevice || !s.MountFatBlockDevice || !s.UnmountFatBlockDevice)
        return micant::boot_driver::STATUS_NOT_SUPPORTED;
    if (services || registration) return kBusy;
    static const ClassServicesV1 callbacks{sizeof(ClassServicesV1), ClassServicesVersion, &devices[0],
        match, create, nullptr, start, nullptr, remove};
    services = &s; driver->DriverUnload = unload;
    const auto result = s.RegisterUsbClass(s.Context, &callbacks, &registration);
    if (result != micant::boot_driver::STATUS_SUCCESS || !registration) { unload(driver); return result == micant::boot_driver::STATUS_SUCCESS ? micant::boot_driver::STATUS_DEVICE_NOT_READY : result; }
    return micant::boot_driver::STATUS_SUCCESS;
}
