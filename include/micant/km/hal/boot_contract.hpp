#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "micant/shared/system_time.hpp"

namespace micant::uefi {

enum class EfiMemoryType : uint32_t {
    EfiReservedMemoryType          = 0,
    EfiLoaderCode                  = 1,
    EfiLoaderData                  = 2,
    EfiBootServicesCode            = 3,
    EfiBootServicesData            = 4,
    EfiRuntimeServicesCode         = 5,
    EfiRuntimeServicesData         = 6,
    EfiConventionalMemory          = 7,
    EfiUnusableMemory              = 8,
    EfiACPIReclaimMemory           = 9,
    EfiACPIMemoryNVS               = 10,
    EfiMemoryMappedIO              = 11,
    EfiMemoryMappedIOPortSpace     = 12,
    EfiPalCode                     = 13,
    EfiPersistentMemory            = 14,
    EfiMaxMemoryType               = 15
};

struct EfiMemoryDescriptor {
    uint32_t type{0};
    uint32_t pad{0};
    uint64_t physicalStart{0};
    uint64_t virtualStart{0};
    uint64_t numberOfPages{0};
    uint64_t attribute{0};
};

static_assert(sizeof(EfiMemoryDescriptor) == 40);
static_assert(offsetof(EfiMemoryDescriptor, physicalStart) == 8);
static_assert(offsetof(EfiMemoryDescriptor, numberOfPages) == 24);
static_assert(std::is_trivially_copyable_v<EfiMemoryDescriptor>);

} // namespace micant::uefi

namespace micant::boot {

inline constexpr uint64_t UEFI_KERNEL_HANDOFF_SIGNATURE = 0x4D4943414E54424FULL;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_VERSION = 3;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_VERSION_OEM_MODULES = 4;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_VERSION_SYSTEM_TIME = 5;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_VERSION_CLOCK_CALIBRATION = 6;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_VERSION_PCI_RESOURCES = 7;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_VERSION_DYNAMIC_PFN = 8;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_MAX_PCI_BARS = 256;
inline constexpr uint32_t UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES = 8;
inline constexpr uint32_t BOOT_MODULE_FLAG_BOOT_START_DRIVER = 0x00000001;
inline constexpr uint32_t BOOT_MODULE_FLAG_DISPLAY_DRIVER = 0x00000002;

struct KernelFramebufferV1 {
    uint64_t physicalBase;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixelsPerScanLine;
    uint32_t pixelFormat;
};

struct KernelBootImageV1 {
    uint64_t base;
    uint64_t size;
    uint64_t entryPoint;
};

struct KernelBootFileV1 {
    uint64_t base;
    uint64_t size;
};

struct UefiKernelHandoffV3 {
    uint64_t signature;
    uint32_t size;
    uint32_t version;
    uint64_t memoryMapAddress;
    uint64_t memoryMapSize;
    uint32_t memoryMapDescriptorSize;
    uint32_t memoryMapDescriptorVersion;
    KernelFramebufferV1 framebuffer;
    uint64_t acpiTablePhysicalAddress;
    KernelBootImageV1 displayDriver;
    KernelBootFileV1 userImageFile;
};

struct KernelBootModuleV1 {
    KernelBootImageV1 image{};
    uint32_t flags{};
    uint32_t reserved{};
    char baseName[32]{}; // NUL-terminated ASCII basename, e.g. "xhci.sys".
};

struct UefiKernelHandoffV4 {
    // Keep V3 as the first member: V3 startup consumers can safely inspect the
    // common prefix, while version-aware consumers validate and read modules.
    UefiKernelHandoffV3 legacy{};
    uint32_t moduleCount{};
    uint32_t moduleCapacity{UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES};
    KernelBootModuleV1 modules[UEFI_KERNEL_HANDOFF_MAX_OEM_MODULES]{};
};

struct UefiKernelHandoffV5 {
    // Preserve the complete V4 OEM module prefix and append a validated clock
    // sample; V3/V4 consumers can still inspect their original prefixes.
    UefiKernelHandoffV4 oem{};
    micant::shared::boot_time::BootTimeSampleV1 systemTime{};
};

struct UefiKernelHandoffV6 {
    // V5 remains an unchanged prefix; calibration is an append-only extension.
    UefiKernelHandoffV5 time{};
    micant::shared::boot_time::BootClockCalibrationV1 clockCalibration{};
};

struct FirmwarePciBarV1 {
    uint16_t segment{}, bus{};
    uint8_t device{}, function{}, index{}, type{}; // type 0 memory, 1 I/O
    uint64_t address{}, length{};
};
struct UefiKernelHandoffV7 {
    UefiKernelHandoffV6 clock{};
    uint32_t pciBarCount{}, pciBarCapacity{UEFI_KERNEL_HANDOFF_MAX_PCI_BARS};
    uint32_t pciInventoryComplete{}, reserved{};
    FirmwarePciBarV1 pciBars[UEFI_KERNEL_HANDOFF_MAX_PCI_BARS]{};
};
struct UefiKernelHandoffV8 {
    UefiKernelHandoffV7 pci{};
    uint64_t pfnMetadataAddress{};
    uint64_t pfnMetadataSize{};
    uint64_t availableRangesAddress{};
    uint64_t availableRangesCapacity{};
};
static_assert(sizeof(FirmwarePciBarV1) == 24);
static_assert(offsetof(UefiKernelHandoffV7, clock) == 0);
static_assert(offsetof(UefiKernelHandoffV8, pci) == 0);
static_assert(offsetof(UefiKernelHandoffV8, pfnMetadataAddress) == sizeof(UefiKernelHandoffV7));

static_assert(std::is_standard_layout_v<UefiKernelHandoffV3>);
static_assert(std::is_trivially_copyable_v<UefiKernelHandoffV3>);
static_assert(sizeof(UefiKernelHandoffV3) == 120);
static_assert(offsetof(UefiKernelHandoffV3, displayDriver) == 80);
static_assert(offsetof(UefiKernelHandoffV3, userImageFile) == 104);
static_assert(std::is_standard_layout_v<KernelBootModuleV1>);
static_assert(std::is_trivially_copyable_v<KernelBootModuleV1>);
static_assert(sizeof(KernelBootModuleV1) == 64);
static_assert(std::is_standard_layout_v<UefiKernelHandoffV4>);
static_assert(std::is_trivially_copyable_v<UefiKernelHandoffV4>);
static_assert(offsetof(UefiKernelHandoffV4, legacy) == 0);
static_assert(offsetof(UefiKernelHandoffV4, moduleCount) == sizeof(UefiKernelHandoffV3));
static_assert(offsetof(UefiKernelHandoffV4, modules) == 128);
static_assert(sizeof(UefiKernelHandoffV4) == 640);
static_assert(std::is_standard_layout_v<UefiKernelHandoffV5>);
static_assert(std::is_trivially_copyable_v<UefiKernelHandoffV5>);
static_assert(offsetof(UefiKernelHandoffV5, oem) == 0);
static_assert(offsetof(UefiKernelHandoffV5, systemTime) == sizeof(UefiKernelHandoffV4));
static_assert(sizeof(UefiKernelHandoffV5) == 664);
static_assert(std::is_standard_layout_v<UefiKernelHandoffV6>);
static_assert(std::is_trivially_copyable_v<UefiKernelHandoffV6>);
static_assert(offsetof(UefiKernelHandoffV6, time) == 0);
static_assert(offsetof(UefiKernelHandoffV6, clockCalibration) == sizeof(UefiKernelHandoffV5));
static_assert(sizeof(UefiKernelHandoffV6) == 680);

} // namespace micant::boot
