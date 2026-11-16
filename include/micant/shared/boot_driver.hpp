#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::shared::usb { struct HostServicesV1; struct ClassServicesV1; }
#include "micant/shared/ndis_adapter_abi.hpp"

// MicaNT's boot-start driver ABI. Images are ordinary PE32+ native .sys files
// loaded by BOOTX64.EFI and entered by the kernel after paging is established.
// Keep this contract POD-only so a driver never depends on the kernel's STL.
namespace micant::boot_driver {

inline constexpr uint32_t DRIVER_ABI_VERSION = 1;
inline constexpr uint32_t KERNEL_SERVICES_VERSION = 2;
inline constexpr uint32_t DMA_SERVICES_VERSION = 1;
inline constexpr uint32_t NETWORK_ADAPTER_SERVICES_VERSION = 1;
inline constexpr uint32_t MSI_INTERRUPT_SERVICES_VERSION = 1;
inline constexpr uint32_t DRIVER_FLAG_BOOT_START = 0x00000001;
inline constexpr uint32_t DRIVER_FLAG_DISPLAY = 0x00000002;

struct UnicodeString {
    uint16_t Length;
    uint16_t MaximumLength;
    wchar_t* Buffer;
};

struct DriverObject;
using DriverEntryRoutine = int32_t (*)(DriverObject*, const UnicodeString*);
using DriverUnloadRoutine = void (*)(DriverObject*);

struct PciBarRequestV1 {
    uint8_t BarIndex;
    uint8_t Flags;
    uint16_t Reserved;
    uint64_t MinimumLength;
};
struct PciBarResourceV1 {
    uint8_t BarIndex;
    uint8_t Reserved0[3];
    uint32_t Flags;
    uint32_t Reserved1;
    uint64_t PhysicalAddress;
    uint64_t Length;
};

struct MsiInterruptRouteV1 {
    uint32_t Size;
    uint32_t Version;
    uint64_t Token;
    uint64_t MessageAddress;
    uint32_t MessageData;
    uint8_t Vector;
    uint8_t Reserved[3];
};

struct FileBackendV1;
struct CrashDumpStorageV1 {
    uint32_t Size;
    uint32_t Version;
    void* Context;
    uint64_t CapacitySectors;
    bool (*Read)(void*, uint64_t, uint16_t, void*) noexcept;
    bool (*Write)(void*, uint64_t, uint16_t, const void*) noexcept;
    int32_t (*Flush)(void*) noexcept;
};

struct KernelServices {
    uint32_t Size;
    uint32_t Version;
    uint32_t BootWidth;
    uint32_t BootHeight;
    void* Context;
    uint64_t (*TranslateVirtualAddress)(void*, const void*);
    void* (*MapDeviceMemory)(void*, uint64_t, size_t);
    void (*Log)(void*, const char*);
    // Version 2 append-only generic HAL/Io services. Return an NTSTATUS value
    // as int32_t; absent hardware ownership/allocation support must fail.
    int32_t (*PciReadConfig32)(void*, uint16_t, uint16_t, uint16_t, uint16_t,
                               uint32_t*);
    int32_t (*PciWriteConfig32)(void*, uint16_t, uint16_t, uint16_t, uint16_t,
                                uint32_t);
    bool (*ReleaseDeviceMemory)(void*, void*, size_t);
    int32_t (*TranslateDmaRange)(void*, const void*, size_t, uint64_t*);
    int32_t (*QueryMonotonicClock)(void*, uint64_t*, uint64_t*);
    int32_t (*DelayMilliseconds)(void*, uint32_t);
    int32_t (*DelayMicroseconds)(void*, uint32_t);
    int32_t (*ClaimPciDevice)(void*, uint16_t, uint16_t, uint16_t, uint16_t,
        const PciBarRequestV1*, size_t, uint64_t*, PciBarResourceV1*, size_t*);
    int32_t (*ReleasePciDevice)(void*, uint64_t);
    int32_t (*CreateReadOnlyBlockDevice)(void*, uint64_t, uint32_t, void*,
        int32_t (*)(void*, uint64_t, uint32_t, void*, uint32_t*),
        void (*)(void*), uint32_t, uint64_t*);
    int32_t (*ReadBlockDevice)(void*, uint64_t, uint64_t, uint32_t, void*, uint32_t*);
    int32_t (*RegisterFileOpenRoutine)(void*,
        int32_t (*)(void*, const char16_t*, size_t, FileBackendV1*), void*);
    int32_t (*UnregisterFileOpenRoutine)(void*,
        int32_t (*)(void*, const char16_t*, size_t, FileBackendV1*), void*);
    int32_t (*OpenBackendFile)(void*, uint64_t*, uint32_t, const char16_t*, size_t);
    int32_t (*CreateBackendFile)(void*, uint64_t*, uint32_t, const FileBackendV1*);
    int32_t (*ReadFile)(void*, uint64_t, bool, uint64_t, void*, size_t, size_t*);
    int32_t (*CloseHandle)(void*, uint64_t);
    int32_t (*CloseBlockDevice)(void*, uint64_t);
    int32_t (*CloseFile)(void*, uint64_t);
    // Size-gated extension of V2. Its independent version permits older V2
    // images to keep using their unchanged prefix and version comparison.
    uint32_t DmaVersion;
    int32_t (*AllocateDmaBuffer)(void*, uint64_t, size_t, size_t, uint64_t,
                                 void**, uint64_t*, uint64_t*);
    int32_t (*FreeDmaBuffer)(void*, uint64_t);
    // Versioned boot-driver adapter registration. Handles are owned by the
    // registering driver context and must be released before driver unload.
    uint32_t NetworkAdapterVersion;
    int32_t (*RegisterNetworkAdapter)(void*, const shared::ndis::AdapterServicesV1*, uint64_t*);
    int32_t (*UnregisterNetworkAdapter)(void*, uint64_t);
    uint32_t UsbHostVersion;
    int32_t (*RegisterUsbHost)(void*, const shared::usb::HostServicesV1*, uint64_t*);
    int32_t (*UnregisterUsbHost)(void*, uint64_t);
    uint32_t FatFilesystemVersion;
    int32_t (*MountFatBlockDevice)(void*, uint64_t, uint64_t*);
    int32_t (*UnmountFatBlockDevice)(void*, uint64_t);
    // Optional USBD class registration; callbacks remain owned by the .sys.
    uint32_t UsbClassVersion;
    int32_t (*RegisterUsbClass)(void*, const shared::usb::ClassServicesV1*, uint64_t*);
    int32_t (*UnregisterUsbClass)(void*, uint64_t);
    // Optional writable block-device extension. Write must complete a cache
    // flush before Success; older V2 drivers retain the unchanged prefix.
    uint32_t WritableBlockDeviceVersion;
    int32_t (*CreateWritableBlockDevice)(void*, uint64_t, uint32_t, void*,
        int32_t (*)(void*, uint64_t, uint32_t, void*, uint32_t*),
        int32_t (*)(void*, uint64_t, uint32_t, const void*, uint32_t*),
        void (*)(void*), uint32_t, uint64_t*);
    int32_t (*WriteBlockDevice)(void*, uint64_t, uint64_t, uint32_t,
                                const void*, uint32_t*);
    // Read a bounded, relative path from one specific read-only FAT mount.
    // The filesystem validates the mount owner/token and does not guess a
    // global drive letter or select another device.
    int32_t (*ReadFatFileFromMount)(void*, uint64_t, const char16_t*, size_t,
                                    void*, size_t, size_t*);
    // Native single-BSP MSI vector ownership; the ISR callback must be
    // resident, bounded and safe at DIRQL. PCI capability programming remains
    // with the claiming bus driver.
    uint32_t MsiInterruptVersion;
    int32_t (*ConnectMsiInterrupt)(void*, void (*)(void*) noexcept, void*, MsiInterruptRouteV1*);
    int32_t (*DisconnectMsiInterrupt)(void*, uint64_t);
    // Optional durability extension. A flush callback is mandatory for these
    // devices; the legacy writable registration remains explicitly flush-less.
    uint32_t DurableBlockDeviceVersion;
    int32_t (*CreateDurableWritableBlockDevice)(void*, uint64_t, uint32_t, void*,
        int32_t (*)(void*, uint64_t, uint32_t, void*, uint32_t*),
        int32_t (*)(void*, uint64_t, uint32_t, const void*, uint32_t*),
        int32_t (*)(void*), void (*)(void*), uint32_t, uint64_t*);
    int32_t (*FlushBlockDevice)(void*, uint64_t);
    // Boot-start storage owner may publish a prevalidated reserved raw extent.
    // Callbacks must be bounded, allocation-free and fail without waiting if
    // the controller is already servicing another command.
    uint32_t CrashDumpStorageVersion;
    int32_t (*RegisterCrashDumpStorage)(void*, const CrashDumpStorageV1*, uint64_t*);
    int32_t (*UnregisterCrashDumpStorage)(void*, uint64_t);
};

inline constexpr size_t KERNEL_SERVICES_V1_SIZE = offsetof(KernelServices, PciReadConfig32);
inline constexpr size_t KERNEL_SERVICES_V2_SIZE = offsetof(KernelServices, DmaVersion);

struct FileBackendV1 {
    void* Context;
    uint64_t Size;
    const char16_t* Name;
    size_t NameLength;
    int32_t (*Read)(void*, uint64_t, void*, size_t, size_t*);
    void (*Release)(void*);
    // Optional write callback. A missing callback means the file is read-only.
    int32_t (*Write)(void*, uint64_t, const void*, size_t, size_t*);
};

using PresentFrameRoutine = bool (*)(void*, const uint32_t*, uint32_t, uint32_t,
                                     uint32_t, int32_t, int32_t);

struct CompositionLayer {
    uint64_t SurfaceId;
    const uint32_t* Pixels;
    uint32_t Width;
    uint32_t Height;
    uint32_t Pitch;
    int32_t X;
    int32_t Y;
    uint8_t Opacity;
    uint8_t Reserved[7];
};

using PresentCompositionRoutine = bool (*)(void*, const uint32_t*, uint32_t, uint32_t,
                                            uint32_t, const CompositionLayer*, uint32_t);

enum class DisplayBackend : uint32_t {
    Unknown = 0,
    Virtio2D = 1,
    Virgl = 2,
    VirglAccelerated = 3,
    Gop = 4,
};

struct DisplayDevice {
    uint32_t Size;
    uint32_t Version;
    uint32_t Width;
    uint32_t Height;
    uint32_t Pitch;
    uint32_t PixelFormat;
    void* Context;
    PresentFrameRoutine PresentFrame;
    PresentCompositionRoutine PresentComposition;
    DisplayBackend Backend;
};

struct DriverObject {
    uint32_t Size;
    uint32_t Version;
    uint32_t Flags;
    uint32_t Reserved;
    void* DriverStart;
    size_t DriverSize;
    void* HardwareResources; // KernelServices for this boot-start device driver.
    void* DriverExtension;
    void* DeviceExtension;
    DriverUnloadRoutine DriverUnload;
    DisplayDevice* Display;
};

inline constexpr int32_t STATUS_SUCCESS = 0;
inline constexpr int32_t STATUS_NOT_SUPPORTED = static_cast<int32_t>(0xC00000BBu);
inline constexpr int32_t STATUS_DEVICE_NOT_READY = static_cast<int32_t>(0xC00000A3u);
inline constexpr int32_t STATUS_INVALID_PARAMETER = static_cast<int32_t>(0xC000000Du);

} // namespace micant::boot_driver
