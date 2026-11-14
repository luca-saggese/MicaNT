#pragma once

#include <cstdint>
#include <string_view>
#include <vector>
#include <span>
#include <string>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/hal/boot.hpp"
#include "micant/km/hal/boot_contract.hpp"

namespace micant::uefi {

// Standard UEFI Status Codes
inline constexpr uint64_t EFI_SUCCESS              = 0;
inline constexpr uint64_t EFI_LOAD_ERROR           = 0x8000000000000001ULL;
inline constexpr uint64_t EFI_INVALID_PARAMETER    = 0x8000000000000002ULL;
inline constexpr uint64_t EFI_UNSUPPORTED          = 0x8000000000000003ULL;
inline constexpr uint64_t EFI_BAD_BUFFER_SIZE      = 0x8000000000000004ULL;
inline constexpr uint64_t EFI_BUFFER_TOO_SMALL     = 0x8000000000000005ULL;
inline constexpr uint64_t EFI_NOT_READY            = 0x8000000000000006ULL;
inline constexpr uint64_t EFI_DEVICE_ERROR         = 0x8000000000000007ULL;
inline constexpr uint64_t EFI_OUT_OF_RESOURCES     = 0x8000000000000009ULL;
inline constexpr uint64_t EFI_NOT_FOUND            = 0x800000000000000EULL;

using EfiHandle = void*;
using EfiStatus = uint64_t;

/**
 * @brief 128-bit Globally Unique Identifier (EFI_GUID).
 */
struct EfiGuid {
    uint32_t data1{0};
    uint16_t data2{0};
    uint16_t data3{0};
    uint8_t  data4[8]{0};

    [[nodiscard]] constexpr bool operator==(const EfiGuid& other) const noexcept {
        if (data1 != other.data1 || data2 != other.data2 || data3 != other.data3) return false;
        for (size_t i = 0; i < 8; ++i) {
            if (data4[i] != other.data4[i]) return false;
        }
        return true;
    }
};

// Well-known standard UEFI Protocol & Configuration GUIDs
inline constexpr EfiGuid EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};

inline constexpr EfiGuid ACPI_20_TABLE_GUID = {
    0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};

inline constexpr EfiGuid ACPI_10_TABLE_GUID = {
    0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

inline constexpr EfiGuid SMBIOS3_TABLE_GUID = {
    0xf2fd1544, 0x9794, 0x4a2c, { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 }
};

inline constexpr EfiGuid SMBIOS_TABLE_GUID = {
    0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

inline constexpr EfiGuid EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID = {
    0x3874286e, 0x9c48, 0x11d2, { 0x8e, 0x4f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

inline constexpr EfiGuid EFI_SIMPLE_POINTER_PROTOCOL_GUID = {
    0x31878c87, 0x0b75, 0x11d5, { 0x9a, 0x4f, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

inline constexpr EfiGuid EFI_BLOCK_IO_PROTOCOL_GUID = {
    0x964e5b21, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

inline constexpr EfiGuid EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

inline constexpr EfiGuid EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5b1b4a61, 0x69a6, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

/**
 * @brief Standard UEFI Table Header.
 */
struct EfiTableHeader {
    uint64_t signature;
    uint32_t revision;
    uint32_t headerSize;
    uint32_t crc32;
    uint32_t reserved;
};

/**
 * @brief UEFI Graphics Pixel Formats.
 */
enum class EfiGraphicsPixelFormat : uint32_t {
    PixelRedGreenBlueReserved8BitPerColor  = 0,
    PixelBlueGreenRedReserved8BitPerColor  = 1,
    PixelBitMask                           = 2,
    PixelBltOnly                           = 3,
    PixelFormatMax                         = 4
};

/**
 * @brief Mode information returned by GOP.
 */
struct EfiGraphicsOutputModeInformation {
    uint32_t version{0};
    uint32_t horizontalResolution{0};
    uint32_t verticalResolution{0};
    EfiGraphicsPixelFormat pixelFormat{EfiGraphicsPixelFormat::PixelBlueGreenRedReserved8BitPerColor};
    struct {
        uint32_t redMask;
        uint32_t greenMask;
        uint32_t blueMask;
        uint32_t reservedMask;
    } pixelInformation{};
    uint32_t pixelsPerScanLine{0};
};

/**
 * @brief GOP Mode status structure.
 */
struct EfiGraphicsOutputProtocolMode {
    uint32_t maxMode{1};
    uint32_t mode{0};
    EfiGraphicsOutputModeInformation* info{nullptr};
    uint64_t sizeOfInfo{sizeof(EfiGraphicsOutputModeInformation)};
    uint64_t frameBufferBase{0};
    uint64_t frameBufferSize{0};
};

/**
 * @brief Graphics Output Protocol (GOP) Interface.
 */
struct EfiGraphicsOutputProtocol {
    EfiStatus (*queryMode)(
        EfiGraphicsOutputProtocol* self,
        uint32_t modeNumber,
        uint64_t* sizeOfInfo,
        EfiGraphicsOutputModeInformation** info
    ){nullptr};
    EfiStatus (*setMode)(
        EfiGraphicsOutputProtocol* self,
        uint32_t modeNumber
    ){nullptr};
    void* blt{nullptr};
    EfiGraphicsOutputProtocolMode* mode{nullptr};
};

/**
 * @brief System Configuration Table entry (for ACPI RSDP, SMBIOS).
 */
struct EfiConfigurationTable {
    EfiGuid vendorGuid{};
    void*   vendorTable{nullptr};
};

/**
 * @brief UEFI Simple Text Output Protocol (for early firmware console).
 */
struct EfiSimpleTextOutputProtocol {
    void* reset{nullptr};
    EfiStatus (*outputString)(EfiSimpleTextOutputProtocol* self, const wchar_t* string){nullptr};
    void* testString{nullptr};
    void* queryMode{nullptr};
    void* setMode{nullptr};
    void* setAttribute{nullptr};
    EfiStatus (*clearScreen)(EfiSimpleTextOutputProtocol* self){nullptr};
    void* setCursorPosition{nullptr};
    void* enableCursor{nullptr};
    void* mode{nullptr};
};

/**
 * @brief UEFI Input Key structure (EFI_INPUT_KEY).
 */
struct EfiInputKey {
    uint16_t scanCode{0};
    char16_t unicodeChar{0};
};

/**
 * @brief UEFI Simple Text Input Protocol (EFI_SIMPLE_TEXT_INPUT_PROTOCOL).
 */
struct EfiSimpleTextInputProtocol {
    EfiStatus (*reset)(EfiSimpleTextInputProtocol* self, bool extendedVerification){nullptr};
    EfiStatus (*readKeyStroke)(EfiSimpleTextInputProtocol* self, EfiInputKey* key){nullptr};
    void* waitForKey{nullptr};
};

/**
 * @brief UEFI Simple Pointer State structure (EFI_SIMPLE_POINTER_STATE).
 */
struct EfiSimplePointerState {
    int32_t relativeMovementX{0};
    int32_t relativeMovementY{0};
    int32_t relativeMovementZ{0};
    bool    leftButton{false};
    bool    rightButton{false};
};

/**
 * @brief UEFI Simple Pointer Mode structure (EFI_SIMPLE_POINTER_MODE).
 */
struct EfiSimplePointerMode {
    uint64_t resolutionX{1};
    uint64_t resolutionY{1};
    uint64_t resolutionZ{1};
    bool     leftButton{false};
    bool     rightButton{false};
};

/**
 * @brief UEFI Simple Pointer Protocol (EFI_SIMPLE_POINTER_PROTOCOL).
 */
struct EfiSimplePointerProtocol {
    EfiStatus (*reset)(EfiSimplePointerProtocol* self, bool extendedVerification){nullptr};
    EfiStatus (*getState)(EfiSimplePointerProtocol* self, EfiSimplePointerState* state){nullptr};
    void*                 waitForInput{nullptr};
    EfiSimplePointerMode* mode{nullptr};
};

/**
 * @brief UEFI Absolute Pointer Protocol (EFI_ABSOLUTE_POINTER_PROTOCOL) for IPMI / KVM virtual USB touch/tablet.
 */
inline constexpr EfiGuid EFI_ABSOLUTE_POINTER_PROTOCOL_GUID = {
    0x8d59d32b, 0xc655, 0x4ae9, { 0x9b, 0x15, 0xf2, 0x59, 0x04, 0x99, 0x2a, 0x43 }
};

struct EfiAbsolutePointerMode {
    uint64_t absoluteMinX{0};
    uint64_t absoluteMinY{0};
    uint64_t absoluteMinZ{0};
    uint64_t absoluteMaxX{0};
    uint64_t absoluteMaxY{0};
    uint64_t absoluteMaxZ{0};
    uint32_t attributes{0};
};

struct EfiAbsolutePointerState {
    uint64_t currentX{0};
    uint64_t currentY{0};
    uint64_t currentZ{0};
    uint32_t activeButtons{0};
};

struct EfiAbsolutePointerProtocol {
    EfiStatus (*reset)(EfiAbsolutePointerProtocol* self, bool extendedVerification){nullptr};
    EfiStatus (*getState)(EfiAbsolutePointerProtocol* self, EfiAbsolutePointerState* state){nullptr};
    void*                   waitForInput{nullptr};
    EfiAbsolutePointerMode* mode{nullptr};
};

/**
 * @brief UEFI Block I/O Media structure (EFI_BLOCK_IO_MEDIA).
 */
struct EfiBlockIoMedia {
    uint32_t mediaId{0};
    bool     removableMedia{false};
    bool     mediaPresent{true};
    bool     logicalPartition{false};
    bool     readOnly{false};
    bool     writeCaching{false};
    uint32_t blockSize{512};
    uint32_t ioAlign{0};
    uint64_t lastBlock{0};
    uint64_t lowestAlignedLba{0};
    uint32_t logicalBlocksPerPhysicalBlock{1};
    uint32_t optimalTransferLengthGranularity{0};
};

/**
 * @brief UEFI Block I/O Protocol (EFI_BLOCK_IO_PROTOCOL) for raw physical disk and partition access.
 */
struct EfiBlockIoProtocol {
    uint64_t revision{0x00010000};
    EfiBlockIoMedia* media{nullptr};
    EfiStatus (*reset)(EfiBlockIoProtocol* self, bool extendedVerification){nullptr};
    EfiStatus (*readBlocks)(
        EfiBlockIoProtocol* self,
        uint32_t mediaId,
        uint64_t lba,
        uint64_t bufferSize,
        void* buffer
    ){nullptr};
    EfiStatus (*writeBlocks)(
        EfiBlockIoProtocol* self,
        uint32_t mediaId,
        uint64_t lba,
        uint64_t bufferSize,
        const void* buffer
    ){nullptr};
    EfiStatus (*flushBlocks)(EfiBlockIoProtocol* self){nullptr};
};

// UEFI File Open Modes
inline constexpr uint64_t EFI_FILE_MODE_READ   = 0x0000000000000001ULL;
inline constexpr uint64_t EFI_FILE_MODE_WRITE  = 0x0000000000000002ULL;
inline constexpr uint64_t EFI_FILE_MODE_CREATE = 0x8000000000000000ULL;

struct EfiFileProtocol;
struct EfiSystemTable;

/**
 * @brief UEFI File Protocol (EFI_FILE_PROTOCOL) for reading/writing files on FAT32/EFI volumes.
 */
struct EfiFileProtocol {
    uint64_t revision{0x00010000};
    EfiStatus (*open)(EfiFileProtocol* self, EfiFileProtocol** newHandle, const wchar_t* fileName, uint64_t openMode, uint64_t attributes){nullptr};
    EfiStatus (*close)(EfiFileProtocol* self){nullptr};
    EfiStatus (*deleteFile)(EfiFileProtocol* self){nullptr};
    EfiStatus (*read)(EfiFileProtocol* self, uint64_t* bufferSize, void* buffer){nullptr};
    EfiStatus (*write)(EfiFileProtocol* self, uint64_t* bufferSize, const void* buffer){nullptr};
    EfiStatus (*getPosition)(EfiFileProtocol* self, uint64_t* position){nullptr};
    EfiStatus (*setPosition)(EfiFileProtocol* self, uint64_t position){nullptr};
    EfiStatus (*getInfo)(EfiFileProtocol* self, const EfiGuid* informationType, uint64_t* bufferSize, void* buffer){nullptr};
    EfiStatus (*setInfo)(EfiFileProtocol* self, const EfiGuid* informationType, uint64_t bufferSize, const void* buffer){nullptr};
    EfiStatus (*flush)(EfiFileProtocol* self){nullptr};
};

/**
 * @brief UEFI Simple File System Protocol (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL).
 */
struct EfiSimpleFileSystemProtocol {
    uint64_t revision{0x00010000};
    EfiStatus (*openVolume)(EfiSimpleFileSystemProtocol* self, EfiFileProtocol** root){nullptr};
};

/**
 * @brief UEFI Loaded Image Protocol (EFI_LOADED_IMAGE_PROTOCOL).
 */
struct EfiLoadedImageProtocol {
    uint32_t revision{0x00010000};
    EfiHandle parentHandle{nullptr};
    EfiSystemTable* systemTable{nullptr};
    EfiHandle deviceHandle{nullptr};
    void* filePath{nullptr};
    void* reserved{nullptr};
    uint32_t loadOptionsSize{0};
    void* loadOptions{nullptr};
    void* imageBase{nullptr};
    uint64_t imageSize{0};
    EfiMemoryType imageCodeType{EfiMemoryType::EfiLoaderCode};
    EfiMemoryType imageDataType{EfiMemoryType::EfiLoaderData};
    EfiStatus (*unload)(EfiHandle imageHandle){nullptr};
};


/**
 * @brief UEFI Boot Services Table.
 */
inline constexpr uint32_t BY_ALL_HANDLES = 0;
inline constexpr uint32_t BY_REGISTER_NOTIFY = 1;
inline constexpr uint32_t BY_PROTOCOL = 2;

struct EfiBootServices {
    EfiTableHeader hdr;

    // Task Priority Services
    void* raiseTPL;
    void* restoreTPL;

    // Memory Services
    void* allocatePages;
    void* freePages;
    EfiStatus (*getMemoryMap)(
        uint64_t* memoryMapSize,
        EfiMemoryDescriptor* memoryMap,
        uint64_t* mapKey,
        uint64_t* descriptorSize,
        uint32_t* descriptorVersion
    );
    void* allocatePool;
    EfiStatus (*freePool)(void* buffer);

    // Event & Timer Services
    void* createEvent;
    void* setTimer;
    void* waitForEvent;
    void* signalEvent;
    void* closeEvent;
    void* checkEvent;

    // Protocol Handler Services
    void* installProtocolInterface;
    void* reinstallProtocolInterface;
    void* uninstallProtocolInterface;
    EfiStatus (*handleProtocol)(EfiHandle handle, const EfiGuid* protocol, void** interface);
    void* reserved;
    void* registerProtocolNotify;
    void* locateHandle;
    void* locateDevicePath;
    void* installConfigurationTable;

    // Image Services
    EfiStatus (*loadImage)(
        uint8_t bootPolicy,
        EfiHandle parentImageHandle,
        void* devicePath,
        void* sourceBuffer,
        uint64_t sourceSize,
        EfiHandle* imageHandle
    );
    EfiStatus (*startImage)(
        EfiHandle imageHandle,
        uint64_t* exitDataSize,
        wchar_t** exitData
    );
    void* exit;
    void* unloadImage;
    EfiStatus (*exitBootServices)(EfiHandle imageHandle, uint64_t mapKey);

    // Miscellaneous Services
    void* getNextMonotonicCount;
    EfiStatus (*stall)(uint64_t microseconds);
    void* setWatchdogTimer;

    // Driver Support Services
    void* connectController;
    void* disconnectController;

    // Open/Close Protocol Services
    void* openProtocol;
    void* closeProtocol;
    void* openProtocolInformation;

    // Library Services
    void* protocolsPerHandle;
    EfiStatus (*locateHandleBuffer)(
        uint32_t searchType,
        const EfiGuid* protocol,
        void* searchKey,
        uint64_t* noHandles,
        EfiHandle** buffer
    );
    EfiStatus (*locateProtocol)(const EfiGuid* protocol, void* registration, void** interface);
};

/**
 * @brief UEFI Time structure (EFI_TIME) returned by RuntimeServices->GetTime.
 */
#ifndef MICANT_UEFI_EFI_TIME_DEFINED
#define MICANT_UEFI_EFI_TIME_DEFINED
struct EfiTime {
    uint16_t Year{2026};
    uint8_t  Month{10};
    uint8_t  Day{13};
    uint8_t  Hour{12};
    uint8_t  Minute{0};
    uint8_t  Second{0};
    uint8_t  Pad1{0};
    uint32_t Nanosecond{0};
    int16_t  TimeZone{0};
    uint8_t  Daylight{0};
    uint8_t  Pad2{0};
};
#endif

struct EfiTimeCapabilities {
    uint32_t resolution{1};
    uint32_t accuracy{0};
    bool     setsToZero{false};
};

/**
 * @brief UEFI Runtime Services Table (EFI_RUNTIME_SERVICES).
 */
struct EfiRuntimeServices {
    EfiTableHeader hdr;

    // Time Services
    EfiStatus (*getTime)(EfiTime* time, EfiTimeCapabilities* capabilities){nullptr};
    EfiStatus (*setTime)(EfiTime* time){nullptr};
    void* getWakeupTime{nullptr};
    void* setWakeupTime{nullptr};

    // Virtual Memory Services
    void* setVirtualAddressMap{nullptr};
    void* convertPointer{nullptr};

    // Variable Services
    void* getVariable{nullptr};
    void* getNextVariableName{nullptr};
    void* setVariable{nullptr};

    // Miscellaneous Runtime Services
    void* getNextHighMonotonicCount{nullptr};
    void (*resetSystem)(uint32_t resetType, EfiStatus resetStatus, uint64_t dataSize, void* resetData){nullptr};
};

/**
 * @brief UEFI System Table (primary pointer passed into EFI application entry point).
 */
struct EfiSystemTable {
    EfiTableHeader hdr;
    wchar_t* firmwareVendor;
    uint32_t firmwareRevision;
    EfiHandle consoleInHandle;
    EfiSimpleTextInputProtocol* conIn;
    EfiHandle consoleOutHandle;
    EfiSimpleTextOutputProtocol* conOut;
    EfiHandle standardErrorHandle;
    EfiSimpleTextOutputProtocol* stdErr;
    EfiRuntimeServices* runtimeServices;
    EfiBootServices* bootServices;
    uint64_t numberOfTableEntries;
    EfiConfigurationTable* configurationTable;
};

// ============================================================================
// Firmware Translation: UEFI -> MicaNT LOADER_PARAMETER_BLOCK
// ============================================================================

inline boot::LoaderMemoryType EfiMemoryTypeToNt(EfiMemoryType type) noexcept {
    switch (type) {
        case EfiMemoryType::EfiConventionalMemory:
            return boot::LoaderMemoryType::LoaderFree;
        case EfiMemoryType::EfiLoaderCode:
            return boot::LoaderMemoryType::LoaderSystemCode;
        case EfiMemoryType::EfiLoaderData:
            return boot::LoaderMemoryType::LoaderOsloaderHeap;
        case EfiMemoryType::EfiBootServicesCode:
        case EfiMemoryType::EfiBootServicesData:
            return boot::LoaderMemoryType::LoaderFirmwareTemporary;
        case EfiMemoryType::EfiRuntimeServicesCode:
        case EfiMemoryType::EfiRuntimeServicesData:
            return boot::LoaderMemoryType::LoaderFirmwarePermanent;
        case EfiMemoryType::EfiACPIReclaimMemory:
        case EfiMemoryType::EfiACPIMemoryNVS:
        case EfiMemoryType::EfiMemoryMappedIO:
            return boot::LoaderMemoryType::LoaderSpecialMemory;
        case EfiMemoryType::EfiUnusableMemory:
            return boot::LoaderMemoryType::LoaderBad;
        default:
            return boot::LoaderMemoryType::LoaderReserve;
    }
}

/**
 * @brief Clean-room translation of UEFI firmware parameters into NT LOADER_PARAMETER_BLOCK.
 */
inline boot::LoaderParameterBlock BuildLpbFromUefi(
    const EfiSystemTable* systemTable,
    const EfiGraphicsOutputProtocol* gop,
    std::span<const EfiMemoryDescriptor> memoryMap,
    std::string_view loadOptions = "/DEBUG /DEBUGPORT=COM1 /BAUDRATE=115200 /NOEXECUTE=ALWAYS /ZERO_TELEMETRY=1"
) {
    boot::LoaderParameterBlock lpb;
    lpb.arcBootDeviceName = L"multi(0)disk(0)rdisk(0)partition(1)";
    lpb.arcHalDeviceName  = L"multi(0)disk(0)rdisk(0)partition(1)";
    lpb.ntBootPathName    = L"\\Windows\\";
    lpb.ntHalPathName     = L"\\Windows\\System32\\";
    lpb.loadOptions       = std::string(loadOptions);

    // 1. Ingest GOP linear framebuffer
    if (gop && gop->mode && gop->mode->info) {
        lpb.framebuffer.physicalBase     = gop->mode->frameBufferBase;
        lpb.framebuffer.size             = gop->mode->frameBufferSize;
        lpb.framebuffer.width            = gop->mode->info->horizontalResolution;
        lpb.framebuffer.height           = gop->mode->info->verticalResolution;
        lpb.framebuffer.pixelsPerScanLine= gop->mode->info->pixelsPerScanLine;
        lpb.framebuffer.pixelFormat      = static_cast<uint32_t>(gop->mode->info->pixelFormat);
    }

    // 2. Discover ACPI 2.0 / 1.0 Root System Description Pointer (RSDP)
    if (systemTable && systemTable->configurationTable) {
        for (uint64_t i = 0; i < systemTable->numberOfTableEntries; ++i) {
            const auto& entry = systemTable->configurationTable[i];
            if (entry.vendorGuid == ACPI_20_TABLE_GUID) {
                lpb.acpiTablePhysicalAddress = reinterpret_cast<uint64_t>(entry.vendorTable);
                break;
            }
        }
        if (lpb.acpiTablePhysicalAddress == 0) {
            for (uint64_t i = 0; i < systemTable->numberOfTableEntries; ++i) {
                const auto& entry = systemTable->configurationTable[i];
                if (entry.vendorGuid == ACPI_10_TABLE_GUID) {
                    lpb.acpiTablePhysicalAddress = reinterpret_cast<uint64_t>(entry.vendorTable);
                    break;
                }
            }
        }
    }

    // 3. Translate UEFI Memory Map to NT Memory Allocation Descriptors
    for (const auto& desc : memoryMap) {
        lpb.memoryDescriptors.push_back({
            .memoryType = EfiMemoryTypeToNt(static_cast<EfiMemoryType>(desc.type)),
            .basePage   = desc.physicalStart / 4096,
            .pageCount  = desc.numberOfPages
        });
    }

    // 4. Default Core Kernel Boot Modules
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

    return lpb;
}

inline boot::UefiKernelHandoffV3 BuildKernelHandoffFromUefi(
    const EfiSystemTable* systemTable,
    const EfiGraphicsOutputProtocol* gop,
    std::span<const EfiMemoryDescriptor> memoryMap
) noexcept {
    boot::UefiKernelHandoffV3 handoff{};
    handoff.signature = boot::UEFI_KERNEL_HANDOFF_SIGNATURE;
    handoff.size = sizeof(handoff);
    handoff.version = boot::UEFI_KERNEL_HANDOFF_VERSION;
    handoff.memoryMapAddress = reinterpret_cast<uint64_t>(memoryMap.data());
    handoff.memoryMapSize = memoryMap.size_bytes();
    handoff.memoryMapDescriptorSize = sizeof(EfiMemoryDescriptor);
    handoff.memoryMapDescriptorVersion = 1;

    if (gop && gop->mode && gop->mode->info) {
        handoff.framebuffer = {
            .physicalBase = gop->mode->frameBufferBase,
            .size = gop->mode->frameBufferSize,
            .width = gop->mode->info->horizontalResolution,
            .height = gop->mode->info->verticalResolution,
            .pixelsPerScanLine = gop->mode->info->pixelsPerScanLine,
            .pixelFormat = static_cast<uint32_t>(gop->mode->info->pixelFormat),
        };
    }

    if (systemTable && systemTable->configurationTable) {
        for (uint64_t i = 0; i < systemTable->numberOfTableEntries; ++i) {
            const auto& entry = systemTable->configurationTable[i];
            if (entry.vendorGuid == ACPI_20_TABLE_GUID) {
                handoff.acpiTablePhysicalAddress =
                    reinterpret_cast<uint64_t>(entry.vendorTable);
                break;
            }
        }
        if (handoff.acpiTablePhysicalAddress == 0) {
            for (uint64_t i = 0; i < systemTable->numberOfTableEntries; ++i) {
                const auto& entry = systemTable->configurationTable[i];
                if (entry.vendorGuid == ACPI_10_TABLE_GUID) {
                    handoff.acpiTablePhysicalAddress =
                        reinterpret_cast<uint64_t>(entry.vendorTable);
                    break;
                }
            }
        }
    }

    return handoff;
}

} // namespace micant::uefi
