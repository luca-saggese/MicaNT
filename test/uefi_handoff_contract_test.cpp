#include <array>
#include <cassert>
#include <cstring>

#include "micant/km/hal/uefi.hpp"

int main() {
    using namespace micant;
    assert(uefi::isReclaimableAfterExitBootServices(
        uefi::EfiMemoryType::EfiConventionalMemory));
    assert(uefi::isReclaimableAfterExitBootServices(
        uefi::EfiMemoryType::EfiBootServicesCode));
    assert(uefi::isReclaimableAfterExitBootServices(
        uefi::EfiMemoryType::EfiBootServicesData));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiLoaderCode));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiLoaderData));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiRuntimeServicesCode));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiRuntimeServicesData));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiACPIReclaimMemory));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiACPIMemoryNVS));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiMemoryMappedIO));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiMemoryMappedIOPortSpace));
    assert(!uefi::isReclaimableAfterExitBootServices(uefi::EfiMemoryType::EfiUnusableMemory));

    std::array<uefi::EfiMemoryDescriptor, 2> memoryMap{};
    const auto empty = uefi::BuildKernelHandoffFromUefi(nullptr, nullptr, {});
    assert(empty.version == 3 && empty.size == 120);
    assert(empty.framebuffer.physicalBase == 0);
    assert(empty.displayDriver.base == 0 && empty.displayDriver.size == 0 &&
           empty.displayDriver.entryPoint == 0);
    assert(empty.userImageFile.base == 0 && empty.userImageFile.size == 0);

    uefi::EfiGraphicsOutputModeInformation info{};
    info.horizontalResolution = 1024;
    info.verticalResolution = 768;
    info.pixelsPerScanLine = 1024;
    info.pixelFormat = uefi::EfiGraphicsPixelFormat::PixelBlueGreenRedReserved8BitPerColor;
    uefi::EfiGraphicsOutputProtocolMode mode{};
    mode.info = &info;
    mode.frameBufferBase = 0xE0000000;
    mode.frameBufferSize = 1024 * 768 * 4;
    uefi::EfiGraphicsOutputProtocol gop{};
    gop.mode = &mode;

    auto handoff = uefi::BuildKernelHandoffFromUefi(nullptr, &gop, memoryMap);
    assert(handoff.signature == boot::UEFI_KERNEL_HANDOFF_SIGNATURE);
    assert(handoff.memoryMapAddress == reinterpret_cast<uint64_t>(memoryMap.data()));
    assert(handoff.memoryMapSize == sizeof(memoryMap));
    assert(handoff.memoryMapDescriptorSize == sizeof(memoryMap[0]));
    assert(handoff.framebuffer.physicalBase == mode.frameBufferBase);
    assert(handoff.framebuffer.size == mode.frameBufferSize);
    assert(handoff.framebuffer.width == 1024 && handoff.framebuffer.height == 768);
    assert(handoff.framebuffer.pixelsPerScanLine == 1024);
    assert(handoff.framebuffer.pixelFormat == 1);
    handoff.displayDriver = {0x100000, 0x20000, 0x101000};
    handoff.userImageFile = {0x200000, 0x18000};
    std::array<std::byte, sizeof(handoff)> bytes{};
    std::memcpy(bytes.data(), &handoff, sizeof(handoff));
    boot::UefiKernelHandoffV3 restored{};
    std::memcpy(&restored, bytes.data(), bytes.size());
    assert(restored.displayDriver.base == handoff.displayDriver.base);
    assert(restored.displayDriver.size == handoff.displayDriver.size);
    assert(restored.displayDriver.entryPoint == handoff.displayDriver.entryPoint);
    assert(restored.userImageFile.base == handoff.userImageFile.base);
    assert(restored.userImageFile.size == handoff.userImageFile.size);
}
