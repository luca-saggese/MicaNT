/**
 * @file thermal.cpp
 * Luce OS Clean-Room Windows NT ACPI Thermal Zone Class Driver Executive Implementation
 *
 * Connects the ACPI thermal zone class driver (acpitz.sys / thermal.sys) to the NT Object Manager
 * namespace (\Device\ThermalZone0), I/O Manager IRP packet hierarchy, and the Executive
 * Power Manager (micant::po::PowerManager).
 */

#include "micant/km/executive/thermal.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/po.hpp"

namespace micant::po::thermal {

static NtStatus ThermalDispatch(micant::io::DeviceObject* device, micant::io::Irp* irp) {
    (void)device;
    if (!irp) return NtStatus::InvalidParameter;

    auto* stack = micant::io::IoGetCurrentIrpStackLocation(irp);
    if (!stack) {
        irp->ioStatus.status = NtStatus::InvalidParameter;
        irp->ioStatus.information = 0;
        micant::io::IoCompleteRequest(irp, 0);
        return NtStatus::InvalidParameter;
    }

    switch (stack->majorFunction) {
        case micant::io::IRP_MJ_CREATE:
        case micant::io::IRP_MJ_CLOSE:
        case micant::io::IRP_MJ_CLEANUP: {
            irp->ioStatus.status = NtStatus::Success;
            irp->ioStatus.information = 0;
            micant::io::IoCompleteRequest(irp, 0);
            return NtStatus::Success;
        }

        case micant::io::IRP_MJ_DEVICE_CONTROL: {
            auto& therm = ThermalSubsystem::get();
            size_t bytesRet = 0;
            void* inBuf = irp->systemBuffer;
            void* outBuf = irp->userBuffer ? irp->userBuffer : irp->systemBuffer;

            NtStatus status = therm.handleIoctl(
                stack->parameters.deviceIoControl.ioControlCode,
                inBuf,
                stack->parameters.deviceIoControl.inputBufferLength,
                outBuf,
                stack->parameters.deviceIoControl.outputBufferLength,
                bytesRet
            );

            irp->ioStatus.status = status;
            irp->ioStatus.information = bytesRet;
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

static micant::io::DriverObject s_thermalDriver;
static micant::io::DeviceObject s_thermalDevice;
static micant::ob::ObjectHeader s_thermalHeader;

void InitializeThermalSubsystem() noexcept {
    auto& ons = micant::ob::ObjectNamespace::get();
    ons.initializeRoot();

    // 1. Setup Thermal Class Driver (acpitz.sys / thermal.sys)
    s_thermalDriver.driverName = L"\\Driver\\ThermalZone";
    s_thermalDriver.setDispatch(micant::io::IRP_MJ_CREATE, ThermalDispatch);
    s_thermalDriver.setDispatch(micant::io::IRP_MJ_CLOSE, ThermalDispatch);
    s_thermalDriver.setDispatch(micant::io::IRP_MJ_CLEANUP, ThermalDispatch);
    s_thermalDriver.setDispatch(micant::io::IRP_MJ_DEVICE_CONTROL, ThermalDispatch);

    // 2. Setup Device Object (\Device\ThermalZone0)
    s_thermalDevice.driverObject = &s_thermalDriver;
    s_thermalDevice.deviceType = micant::io::DeviceType::Unknown;
    s_thermalDevice.deviceName = L"\\Device\\ThermalZone0";
    s_thermalDevice.flags = micant::io::DO_BUFFERED_IO | micant::io::DO_POWER_PAGABLE;

    auto devDir = ons.getDirectory(L"\\Device");
    if (!devDir) {
        ons.createDirectory(L"\\Device");
        devDir = ons.getDirectory(L"\\Device");
    }

    const auto* devType = ons.getType(L"Device");
    s_thermalHeader.type = devType;
    s_thermalHeader.objectName = L"ThermalZone0";
    if (devDir) {
        devDir->insertObject(L"ThermalZone0", &s_thermalHeader);
    }

    // 3. Create Symbolic Link \DosDevices\ThermalZone0 -> \Device\ThermalZone0
    ons.createSymbolicLink(L"\\DosDevices\\ThermalZone0", L"\\Device\\ThermalZone0");

    // 4. Initial Subsystem Synchronization
    auto& therm = ThermalSubsystem::get();
    therm.resetToDefaultState();
}

} // namespace micant::po::thermal
