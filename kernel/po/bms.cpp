/**
 * @file bms.cpp
 @brief Luce OS Clean-Room Windows NT Battery Management System (BMS) Executive Implementation
 *
 * Connects the smart battery class driver to the NT Object Manager namespace (\Device\Battery0),
 * I/O Manager IRP packet hierarchy, and the Executive Power Manager (micant::po::PowerManager).
 */

#include "micant/km/executive/bms.hpp"
#include "micant/km/drivers/acpi/cmbatt.hpp"
#include "micant/km/drivers/bus/battclass.hpp"
#include "micant/km/drivers/bus/acpi.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/po.hpp"

namespace micant::po::bms {

static micant::io::DeviceObject s_batteryDevice;
static micant::io::DeviceObject* s_batteryLowerDevice{};
static bool s_batteryClassAttached{};

static NtStatus BattcDispatch(micant::io::DeviceObject* device, micant::io::Irp* irp) {
    if (!device || !irp) return NtStatus::InvalidParameter;

    auto* stack = micant::io::IoGetCurrentIrpStackLocation(irp);
    if (!stack) {
        irp->ioStatus.status = NtStatus::InvalidParameter;
        irp->ioStatus.information = 0;
        micant::io::IoCompleteRequest(irp, 0);
        return NtStatus::InvalidParameter;
    }

    switch (stack->majorFunction) {
        case micant::io::IRP_MJ_PNP: {
            if (!s_batteryLowerDevice || !s_batteryLowerDevice->driverObject) {
                irp->ioStatus.status = NtStatus::NoSuchDevice;
                return NtStatus::NoSuchDevice;
            }
            irp->deviceObject = s_batteryLowerDevice;
            stack->deviceObject = s_batteryLowerDevice;
            const NtStatus status = s_batteryLowerDevice->driverObject->dispatch(s_batteryLowerDevice, irp);
            if (stack->minorFunction == micant::pnp::IRP_MN_REMOVE_DEVICE) {
                device->deviceExtension = nullptr;
                s_batteryClassAttached = false;
                s_batteryLowerDevice = nullptr;
            }
            return status;
        }
        case micant::io::IRP_MJ_CREATE:
        case micant::io::IRP_MJ_CLOSE:
        case micant::io::IRP_MJ_CLEANUP: {
            irp->ioStatus.status = NtStatus::Success;
            irp->ioStatus.information = 0;
            micant::io::IoCompleteRequest(irp, 0);
            return NtStatus::Success;
        }

        case micant::io::IRP_MJ_DEVICE_CONTROL: {
            NtStatus status = BatteryClassIoctl(device->deviceExtension, irp);
            micant::io::IoCompleteRequest(irp, 0);
            return status;
        }

        default: {
            irp->ioStatus.status = NtStatus::NotImplemented;
            irp->ioStatus.information = 0;
            micant::io::IoCompleteRequest(irp, 0);
            return NtStatus::NotImplemented;
        }
    }
}

static micant::io::DriverObject s_battcDriver;
static micant::ob::ObjectHeader s_batteryHeader;

void InitializeBatterySubsystem() noexcept {
    // ACPI enumerates its firmware PDO; cmbatt attaches and registers its
    // battery miniport through the battery class contract before publication.
    (void)micant::km::bus::acpi::InitializeAcpiBusDriver();
    auto* const batteryPdo = micant::km::bus::acpi::AcpiBusEnumerator::Instance().batteryPdo();
    void* const batteryClassData = BatteryClassRegistry::Instance().handleForPdo(batteryPdo);

    auto& ons = micant::ob::ObjectNamespace::get();
    ons.initializeRoot();

    // 1. Setup Battery Class Driver (battc.sys)
    s_battcDriver.driverName = L"\\Driver\\BattC";
    s_battcDriver.setDispatch(micant::io::IRP_MJ_CREATE, BattcDispatch);
    s_battcDriver.setDispatch(micant::io::IRP_MJ_CLOSE, BattcDispatch);
    s_battcDriver.setDispatch(micant::io::IRP_MJ_CLEANUP, BattcDispatch);
    s_battcDriver.setDispatch(micant::io::IRP_MJ_DEVICE_CONTROL, BattcDispatch);
    s_battcDriver.setDispatch(micant::io::IRP_MJ_PNP, BattcDispatch);

    // 2. Setup Device Object (\Device\Battery0)
    if (batteryClassData) {
        if (!s_batteryClassAttached) {
            s_batteryDevice.driverObject = &s_battcDriver;
            s_batteryDevice.deviceType = micant::io::DeviceType::Battery;
            s_batteryDevice.deviceName = L"\\Device\\Battery0";
            s_batteryDevice.deviceExtension = batteryClassData;
            s_batteryDevice.flags = micant::io::DO_BUFFERED_IO | micant::io::DO_POWER_PAGABLE;
            s_batteryLowerDevice = batteryPdo ? batteryPdo->attachedDevice : nullptr;
            s_batteryClassAttached = batteryPdo &&
                micant::io::IoAttachDeviceToDeviceStack(&s_batteryDevice, batteryPdo);
        }

        auto devDir = ons.getDirectory(L"\\Device");
        if (!devDir) {
            ons.createDirectory(L"\\Device");
            devDir = ons.getDirectory(L"\\Device");
        }

        const auto* devType = ons.getType(L"Device");
        s_batteryHeader.type = devType;
        s_batteryHeader.objectName = L"Battery0";
        if (devDir) devDir->insertObject(L"Battery0", &s_batteryHeader);

    // 3. Create Symbolic Link \DosDevices\Battery0 -> \Device\Battery0
        ons.createSymbolicLink(L"\\DosDevices\\Battery0", L"\\Device\\Battery0");
    } else {
        SmartBatterySubsystem::get().updateFromAcpi(false, {}, {}, {}, {}, {});
    }

    // 4. Synchronize Battery State into Executive PowerManager
    auto& po = micant::po::PowerManager::get();
    auto& bms = SmartBatterySubsystem::get();
    const auto& bStatus = bms.getStatus();
    const auto& bInfo = bms.getInformation();

    SystemBatteryState sysBat{};
    sysBat.acOnLine = (bStatus.powerState & BATTERY_POWER_ON_LINE) != 0;
    sysBat.batteryPresent = bms.isBatteryPresent();
    sysBat.charging = (bStatus.powerState & BATTERY_CHARGING) != 0;
    sysBat.discharging = (bStatus.powerState & BATTERY_DISCHARGING) != 0;
    sysBat.maxCapacity = bInfo.fullChargedCapacity;
    sysBat.remainingCapacity = bStatus.capacity;
    sysBat.rate = bStatus.rate;
    sysBat.estimatedTime = BATTERY_UNKNOWN_TIME;
    if (bStatus.rate < 0 && bStatus.rate != BATTERY_UNKNOWN_RATE) {
        const uint64_t dischargeRate = static_cast<uint64_t>(-
            static_cast<int64_t>(bStatus.rate));
        const uint64_t seconds = static_cast<uint64_t>(bStatus.capacity) * 3600U / dischargeRate;
        sysBat.estimatedTime = static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX - 1U));
    }

    po.updateBatteryState(sysBat);
}

} // namespace micant::po::bms
