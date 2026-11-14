#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/hal/boot_contract.hpp"

namespace micant::boot {

// Standard NT Loader Memory Types
enum class LoaderMemoryType : uint32_t {
    LoaderExceptionBlock            = 0,
    LoaderSystemBlock               = 1,
    LoaderFree                      = 2,
    LoaderBad                       = 3,
    LoaderLoadedProgram             = 4,
    LoaderFirmwareTemporary         = 5,
    LoaderFirmwarePermanent         = 6,
    LoaderOsloaderHeap              = 7,
    LoaderOsloaderStack             = 8,
    LoaderSystemCode                = 9,
    LoaderHalCode                   = 10,
    LoaderBootDriver                = 11,
    LoaderConsoleInDriver           = 12,
    LoaderConsoleOutDriver          = 13,
    LoaderStartupPdrPage            = 14,
    LoaderRegistryData              = 15,
    LoaderMemoryData                = 16,
    LoaderNlsData                   = 17,
    LoaderSpecialMemory             = 18,
    LoaderReserve                   = 19
};

/**
 * @brief Memory Allocation Descriptor passed by UEFI / winload.efi
 */
struct MemoryAllocationDescriptor {
    LoaderMemoryType memoryType{LoaderMemoryType::LoaderFree};
    uint64_t basePage{0};
    uint64_t pageCount{0};
};

/**
 * @brief Loaded Module Descriptor in Loader Parameter Block
 */
struct BootModuleDescriptor {
    std::wstring baseDllName;
    std::wstring fullDllName;
    uintptr_t dllBase{0};
    size_t sizeOfImage{0};
    uintptr_t entryPoint{0};
    uint32_t moduleFlags{0};
};

/**
 * @brief Linear Framebuffer Video Descriptor (from UEFI GOP).
 */
struct FramebufferDescriptor {
    uint64_t physicalBase{0};
    uint64_t size{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pixelsPerScanLine{0};
    uint32_t pixelFormat{1}; // 1 = Blue-Green-Red-Reserved 32-bit color
};

/**
 * @brief NT Loader Parameter Block (LOADER_PARAMETER_BLOCK)
 * Clean-room modern C++23 firmware boot handover specification.
 */
struct LoaderParameterBlock {
    std::wstring arcBootDeviceName;
    std::wstring arcHalDeviceName;
    std::wstring ntBootPathName;
    std::wstring ntHalPathName;
    std::string loadOptions;

    std::vector<MemoryAllocationDescriptor> memoryDescriptors;
    std::vector<BootModuleDescriptor> bootModules;

    FramebufferDescriptor framebuffer{};
    uint64_t acpiTablePhysicalAddress{0};
    uint32_t osMajorVersion{10};
    uint32_t osMinorVersion{0};
    uint32_t osBuildNumber{26100};

    [[nodiscard]] bool hasOption(std::string_view opt) const noexcept {
        return loadOptions.find(opt) != std::string::npos;
    }

    [[nodiscard]] uint64_t getTotalMemoryBytes() const noexcept {
        uint64_t totalPages = 0;
        for (const auto& desc : memoryDescriptors) {
            totalPages += desc.pageCount;
        }
        return totalPages * 4096; // 4 KB pages
    }

    [[nodiscard]] uint64_t getFreeMemoryBytes() const noexcept {
        uint64_t freePages = 0;
        for (const auto& desc : memoryDescriptors) {
            if (desc.memoryType == LoaderMemoryType::LoaderFree) {
                freePages += desc.pageCount;
            }
        }
        return freePages * 4096;
    }
};

/**
 * @brief Helper to generate a default UEFI boot block for MicaNT.
 */
inline LoaderParameterBlock createDefaultUefiBootBlock() {
    LoaderParameterBlock lpb;
    lpb.arcBootDeviceName = L"multi(0)disk(0)rdisk(0)partition(1)";
    lpb.arcHalDeviceName  = L"multi(0)disk(0)rdisk(0)partition(1)";
    lpb.ntBootPathName    = L"\\Windows\\";
    lpb.ntHalPathName     = L"\\Windows\\System32\\";
    lpb.loadOptions       = "/DEBUG /DEBUGPORT=COM1 /BAUDRATE=115200 /NOEXECUTE=ALWAYS /ZERO_TELEMETRY=1";

    // Memory map
    // 0x000000 - 0x100000: Firmware / Low memory
    lpb.memoryDescriptors.push_back({
        .memoryType = LoaderMemoryType::LoaderFirmwareTemporary,
        .basePage = 0,
        .pageCount = 256
    });
    // 0x100000 - 0x2000000: Kernel & Hal code (31 MB)
    lpb.memoryDescriptors.push_back({
        .memoryType = LoaderMemoryType::LoaderSystemCode,
        .basePage = 256,
        .pageCount = 7936
    });
    // 0x2000000 - 0x40000000: Free Physical RAM (1024 MB)
    lpb.memoryDescriptors.push_back({
        .memoryType = LoaderMemoryType::LoaderFree,
        .basePage = 8192,
        .pageCount = 253952
    });

    // Boot modules
    lpb.bootModules.push_back({
        .baseDllName = L"ntoskrnl.exe",
        .fullDllName = L"\\Windows\\System32\\ntoskrnl.exe",
        .dllBase = 0xFFFFF80000000000ULL,
        .sizeOfImage = 12 * 1024 * 1024,
        .entryPoint = 0xFFFFF80000100000ULL
    });

    lpb.bootModules.push_back({
        .baseDllName = L"hal.dll",
        .fullDllName = L"\\Windows\\System32\\hal.dll",
        .dllBase = 0xFFFFF80001000000ULL,
        .sizeOfImage = 2 * 1024 * 1024,
        .entryPoint = 0xFFFFF80001010000ULL
    });

    lpb.bootModules.push_back({
        .baseDllName = L"fastfat.sys",
        .fullDllName = L"\\Windows\\System32\\drivers\\fastfat.sys",
        .dllBase = 0xFFFFF80001200000ULL,
        .sizeOfImage = 1 * 1024 * 1024,
        .entryPoint = 0xFFFFF80001208000ULL
    });

    // This helper has no firmware memory map backing ACPI physical addresses.
    // Keep the field empty so ACPI uses its explicit synthetic test setup.
    lpb.acpiTablePhysicalAddress = 0;

    lpb.framebuffer = {
        .physicalBase = 0x00000000E0000000ULL,
        .size = 1920 * 1080 * 4,
        .width = 1920,
        .height = 1080,
        .pixelsPerScanLine = 1920,
        .pixelFormat = 1 // PixelBlueGreenRedReserved8BitPerColor
    };

    return lpb;
}

} // namespace micant::boot

#if defined(MICANT_FREESTANDING)
extern "C" micant::NtStatus KiSystemStartup(
    const micant::boot::UefiKernelHandoffV3* handoff
) noexcept;
#else
extern "C" micant::NtStatus KiSystemStartup(
    micant::boot::LoaderParameterBlock* lpb,
    const char* execCmd = nullptr,
    bool launchInteractive = false
);
#endif
