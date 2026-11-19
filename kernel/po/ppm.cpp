/**
 * @file ppm.cpp
 * Luce OS Clean-Room Windows NT Processor Power Management (PPM) Executive Implementation
 *
 * Connects the Intel Speed Shift (HWP) class driver to the NT Object Manager namespace
 * (\Device\Processor0..\Device\Processor7), I/O Manager IRP packet hierarchy, and the
 * Executive Power Manager (micant::po::PowerManager).
 */

#include "micant/km/executive/ppm.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/po.hpp"

namespace micant::po::ppm {

static NtStatus PpmDispatch(micant::io::DeviceObject* device, micant::io::Irp* irp) {
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
            auto& ppm = ProcessorPowerManager::get();
            size_t bytesRet = 0;
            void* inBuf = irp->systemBuffer;
            void* outBuf = irp->userBuffer ? irp->userBuffer : irp->systemBuffer;

            NtStatus status = ppm.handleIoctl(
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

        case micant::io::IRP_MJ_POWER: {
            // Power state change notification for processor
            irp->ioStatus.status = NtStatus::Success;
            irp->ioStatus.information = 0;
            micant::io::IoCompleteRequest(irp, 0);
            return NtStatus::Success;
        }

        default: {
            irp->ioStatus.status = NtStatus::NotImplemented;
            irp->ioStatus.information = 0;
            micant::io::IoCompleteRequest(irp, 0);
            return NtStatus::NotImplemented;
        }
    }
}

static micant::io::DriverObject s_ppmDriver;
static micant::io::DeviceObject s_processorDevices[ProcessorPowerManager::MAX_PROCESSORS];
static micant::ob::ObjectHeader s_processorHeaders[ProcessorPowerManager::MAX_PROCESSORS];

void InitializeProcessorPowerSubsystem() noexcept {
    auto& ons = micant::ob::ObjectNamespace::get();
    ons.initializeRoot();

    // 1. Setup PPM Driver (intelppm.sys)
    s_ppmDriver.driverName = L"\\Driver\\IntelPPM";
    s_ppmDriver.setDispatch(micant::io::IRP_MJ_CREATE, PpmDispatch);
    s_ppmDriver.setDispatch(micant::io::IRP_MJ_CLOSE, PpmDispatch);
    s_ppmDriver.setDispatch(micant::io::IRP_MJ_CLEANUP, PpmDispatch);
    s_ppmDriver.setDispatch(micant::io::IRP_MJ_DEVICE_CONTROL, PpmDispatch);
    s_ppmDriver.setDispatch(micant::io::IRP_MJ_POWER, PpmDispatch);

    auto devDir = ons.getDirectory(L"\\Device");
    if (!devDir) {
        ons.createDirectory(L"\\Device");
        devDir = ons.getDirectory(L"\\Device");
    }

    const auto* devType = ons.getType(L"Device");

    // 2. Enumerate & Register \Device\Processor0..\Device\Processor7
    for (size_t i = 0; i < ProcessorPowerManager::MAX_PROCESSORS; ++i) {
        std::wstring nodeName = L"Processor" + std::to_wstring(i);
        std::wstring fullPath = L"\\Device\\" + nodeName;

        s_processorDevices[i].driverObject = &s_ppmDriver;
        s_processorDevices[i].deviceType = micant::io::DeviceType::Unknown;
        s_processorDevices[i].deviceName = fullPath;
        s_processorDevices[i].flags = micant::io::DO_BUFFERED_IO | micant::io::DO_POWER_PAGABLE;

        s_processorHeaders[i].type = devType;
        s_processorHeaders[i].objectName = nodeName;
        if (devDir) {
            devDir->insertObject(nodeName, &s_processorHeaders[i]);
        }
    }

    // 3. Initial PPM governor state synchronization
    auto& ppm = ProcessorPowerManager::get();
    ppm.resetToDefaultState();
}

} // namespace micant::po::ppm
