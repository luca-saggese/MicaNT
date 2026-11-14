// MicaNT boot-start VirtIO-GPU PCI display driver.
// Implements the VirtIO 1.0 modern PCI transport and 2D scanout protocol.
// The kernel owns window composition; this driver owns the device transport,
// guest-backed scanout resource, and presentation to the host display.

#include <cstddef>
#include <cstdint>
#include "micant/shared/boot_driver.hpp"

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(value);
    return destination;
}

namespace {

using namespace micant::boot_driver;

constexpr uint16_t kVirtioVendor = 0x1AF4;
constexpr uint16_t kVirtioGpuDevice = 0x1050;
constexpr uint8_t kPciCapabilityVendor = 0x09;
constexpr uint8_t kVirtioPciCapCommon = 1;
constexpr uint8_t kVirtioPciCapNotify = 2;
constexpr uint8_t kVirtioPciCapDevice = 4;
constexpr uint32_t kVirtioFeatureVersion1 = 1u;
constexpr uint32_t kVirtioGpuFeatureVirgl = 1u;
constexpr uint32_t kVirtioStatusAcknowledge = 1;
constexpr uint32_t kVirtioStatusDriver = 2;
constexpr uint32_t kVirtioStatusDriverOk = 4;
constexpr uint32_t kVirtioStatusFeaturesOk = 8;
constexpr uint16_t kQueueSize = 128;
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1080;
constexpr uint32_t kMaxBackingPages = (kMaxWidth * kMaxHeight * 4 + 4095) / 4096;
constexpr uint32_t kResourceId = 1;
constexpr uint32_t kFormatB8G8R8A8Unorm = 1;
constexpr uint16_t kDescNext = 1;
constexpr uint16_t kDescWrite = 2;

constexpr uint32_t kCmdGetDisplayInfo = 0x0100;
constexpr uint32_t kCmdResourceCreate2D = 0x0101;
constexpr uint32_t kCmdResourceUnref = 0x0102;
constexpr uint32_t kCmdSetScanout = 0x0103;
constexpr uint32_t kCmdResourceFlush = 0x0104;
constexpr uint32_t kCmdTransferToHost2D = 0x0105;
constexpr uint32_t kCmdResourceAttachBacking = 0x0106;
constexpr uint32_t kRespOkNoData = 0x1100;
constexpr uint32_t kRespOkDisplayInfo = 0x1101;
constexpr uint32_t kCmdCtxCreate = 0x0200;
constexpr uint32_t kCmdCtxAttachResource = 0x0202;
constexpr uint32_t kCmdResourceCreate3D = 0x0204;
constexpr uint32_t kCmdTransferToHost3D = 0x0205;
constexpr uint32_t kCmdSubmit3D = 0x0207;
constexpr uint32_t kVirglContextId = 1;
constexpr uint32_t kVirglTargetTexture2D = 2;
constexpr uint32_t kVirglBindDisplayTarget = 1u << 0;
constexpr uint32_t kVirglBindRenderTarget = 1u << 1;
constexpr uint32_t kVirglBindSamplerView = 1u << 3;
constexpr uint32_t kVirglFormatB8G8R8A8 = 1;
constexpr uint32_t kVirglCopyRegion = 17;
constexpr uint32_t kVirglOutputResourceId = 2;
constexpr uint32_t kMaxCompositionLayers = 32;

struct PciLocation { uint8_t bus, device, function; };
struct PciRegion { uint8_t bar; uint32_t offset, length, notifyMultiplier; uint64_t physicalBase; };
struct PciCaps { PciRegion common{}, notify{}, device{}; bool hasCommon{}, hasNotify{}, hasDevice{}; };
const KernelServices* g_activeServices{};
bool g_pciAccessFailed{};
uint64_t g_pciClaimToken{};

static inline void ioBarrier() { __asm__ __volatile__("mfence" : : : "memory"); }
static inline void cpuRelax() { __asm__ __volatile__("pause"); }

uint32_t pciRead(const PciLocation& loc, uint8_t offset) {
    uint32_t value = UINT32_MAX;
    if (!g_activeServices || !g_activeServices->PciReadConfig32 ||
        g_activeServices->PciReadConfig32(g_activeServices->Context, loc.bus,
            loc.device, loc.function, static_cast<uint16_t>(offset & 0xFCu), &value) != STATUS_SUCCESS)
        g_pciAccessFailed = true;
    return value;
}

void logHex(const KernelServices& services, const char* label, uint32_t value) {
    if (!services.Log) return;
    static constexpr char digits[] = "0123456789ABCDEF";
    char message[64]{};
    size_t out = 0;
    while (label[out] && out < sizeof(message) - 12) { message[out] = label[out]; ++out; }
    for (int shift = 28; shift >= 0; shift -= 4) message[out++] = digits[(value >> shift) & 0xFu];
    message[out] = '\0';
    services.Log(services.Context, message);
}

uint64_t pciBar(const PciLocation& loc, uint8_t barIndex) {
    const uint8_t offset = static_cast<uint8_t>(0x10 + barIndex * 4);
    const uint32_t low = pciRead(loc, offset);
    if (low & 1u) return 0;
    uint64_t base = low & ~0xFu;
    if (((low >> 1) & 3u) == 2u && barIndex < 5) {
        base |= static_cast<uint64_t>(pciRead(loc, static_cast<uint8_t>(offset + 4))) << 32;
    }
    return base;
}

bool findGpu(PciLocation& result, PciCaps& caps, const KernelServices& services) {
    g_activeServices = &services;
    g_pciAccessFailed = false;
    g_pciClaimToken = 0;
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t dev = 0; dev < 32; ++dev) {
            PciLocation loc{static_cast<uint8_t>(bus), static_cast<uint8_t>(dev), 0};
            if ((pciRead(loc, 0) & 0xFFFFu) != kVirtioVendor) continue;
            if ((pciRead(loc, 0) >> 16) != kVirtioGpuDevice) continue;
            if (services.Log) services.Log(services.Context, "virtio_gpu.sys: matched PCI vendor/device 1AF4:1050");

            result = loc;
            if ((pciRead(loc, 6) & (1u << 20)) == 0) {
                if (services.Log) services.Log(services.Context, "virtio_gpu.sys: PCI status reports no capability list");
                return true;
            }
            uint8_t cap = static_cast<uint8_t>(pciRead(loc, 0x34));
            for (uint32_t step = 0; cap >= 0x40 && cap <= 0xFC && step < 48; ++step) {
                const uint32_t capWord = pciRead(loc, cap);
                const uint8_t id = static_cast<uint8_t>(capWord & 0xFFu);
                const uint8_t next = static_cast<uint8_t>((capWord >> 8) & 0xFFu);
                const uint8_t capLength = static_cast<uint8_t>((capWord >> 16) & 0xFFu);
                const uint8_t type = static_cast<uint8_t>((capWord >> 24) & 0xFFu);
                if (id == kPciCapabilityVendor && capLength >= 16) {
                    const uint32_t barAndType = pciRead(loc, static_cast<uint8_t>(cap + 4));
                    const uint8_t bar = static_cast<uint8_t>(barAndType & 0xFFu);
                    const uint8_t cfgType = type; // cfg_type occupies byte 3 of the vendor capability header.
                    PciRegion region{bar, pciRead(loc, static_cast<uint8_t>(cap + 8)),
                                     pciRead(loc, static_cast<uint8_t>(cap + 12)), 0};
                    if (bar < 6 && pciBar(loc, bar) != 0) {
                        if (cfgType == kVirtioPciCapCommon) { caps.common = region; caps.hasCommon = true; }
                        else if (cfgType == kVirtioPciCapNotify && capLength >= 20) {
                            region.notifyMultiplier = pciRead(loc, static_cast<uint8_t>(cap + 16));
                            caps.notify = region; caps.hasNotify = true;
                        } else if (cfgType == kVirtioPciCapDevice) { caps.device = region; caps.hasDevice = true; }
                    } else if (services.Log) {
                        services.Log(services.Context, "virtio_gpu.sys: vendor capability references invalid BAR");
                    }
                } else if (id == kPciCapabilityVendor && services.Log) {
                    services.Log(services.Context, "virtio_gpu.sys: short vendor capability rejected");
                }
                if (next == 0 || next == cap) break;
                cap = next;
                (void)type;
            }
            if (g_pciAccessFailed || !caps.hasCommon || !caps.hasNotify) return false;

            PciBarRequestV1 requests[2]{};
            size_t requestCount = 0;
            const PciRegion* regions[] = {&caps.common, &caps.notify};
            for (const auto* region : regions) {
                const uint64_t required = static_cast<uint64_t>(region->offset) + region->length;
                if (!region->length || required > UINT32_MAX) return false;
                size_t index = 0;
                while (index < requestCount && requests[index].BarIndex != region->bar) ++index;
                if (index == requestCount) {
                    requests[requestCount++] = {region->bar, 0, 0, required};
                } else if (required > requests[index].MinimumLength) {
                    requests[index].MinimumLength = required;
                }
            }
            PciBarResourceV1 resources[2]{};
            size_t resourceCount = requestCount;
            if (!services.ClaimPciDevice) return false;
            const int32_t claimStatus = services.ClaimPciDevice(services.Context,
                0, loc.bus, loc.device, loc.function, requests, requestCount,
                &g_pciClaimToken, resources, &resourceCount);
            if (claimStatus != STATUS_SUCCESS) {
                g_pciClaimToken = 0;
                if (services.Log) services.Log(services.Context,
                    "virtio_gpu.sys: PCI PnP claim unavailable; controller left untouched");
                return false;
            }
            if (!g_pciClaimToken || resourceCount != requestCount) {
                if (g_pciClaimToken) (void)services.ReleasePciDevice(
                    services.Context, g_pciClaimToken);
                g_pciClaimToken = 0;
                return false;
            }
            for (size_t i = 0; i < requestCount; ++i) {
                if (resources[i].BarIndex != requests[i].BarIndex ||
                    !resources[i].PhysicalAddress ||
                    resources[i].Length < requests[i].MinimumLength) {
                    (void)services.ReleasePciDevice(services.Context, g_pciClaimToken);
                    g_pciClaimToken = 0;
                    return false;
                }
                PciRegion* resourceRegions[] = {&caps.common, &caps.notify};
                for (auto* target : resourceRegions) {
                    if (target->bar == resources[i].BarIndex)
                        target->physicalBase = resources[i].PhysicalAddress;
                }
            }

            return true;
        }
    }
    return false;
}

struct VirtqDesc { uint64_t address; uint32_t length; uint16_t flags; uint16_t next; };
struct VirtqAvail { volatile uint16_t flags, index; uint16_t ring[kQueueSize]; uint16_t usedEvent; };
struct VirtqUsedElement { uint32_t id, length; };
struct VirtqUsed { volatile uint16_t flags, index; VirtqUsedElement ring[kQueueSize]; uint16_t availEvent; };

struct CommandHeader { uint32_t type, flags; uint64_t fenceId; uint32_t contextId; uint8_t ringIndex, padding[3]; };
struct Rect { uint32_t x, y, width, height; };
struct CommandResourceCreate2D { CommandHeader header; uint32_t resourceId, format, width, height; };
struct CommandSetScanout { CommandHeader header; Rect rect; uint32_t scanoutId, resourceId; };
struct CommandTransfer2D { CommandHeader header; Rect rect; uint64_t offset; uint32_t resourceId, padding; };
struct CommandFlush { CommandHeader header; Rect rect; uint32_t resourceId, padding; };
struct CommandUnref { CommandHeader header; uint32_t resourceId, padding; };
struct CommandAttachPrefix { CommandHeader header; uint32_t resourceId, entries; };
struct MemoryEntry { uint64_t address; uint32_t length, padding; };
struct CommandDetachBacking { CommandHeader header; uint32_t resourceId, padding; };
struct Box3D { uint32_t x, y, z, width, height, depth; };
struct CommandCreate3D {
    CommandHeader header;
    uint32_t resourceId, target, format, bind, width, height;
    uint32_t depth, arraySize, lastLevel, samples, flags, padding;
};
struct CommandContextCreate { CommandHeader header; uint32_t nameLength, contextInit; char name[64]; };
struct CommandContextResource { CommandHeader header; uint32_t resourceId, padding; };
struct CommandTransfer3D {
    CommandHeader header; Box3D box; uint64_t offset;
    uint32_t resourceId, level, stride, layerStride;
};
struct CommandSubmit3D { CommandHeader header; uint32_t size, padding; };
struct ResponseHeader { uint32_t type, flags; uint64_t fenceId; uint32_t contextId; uint8_t ringIndex, padding[3]; };
struct DisplayInfo { Rect rect; uint32_t enabled, flags; };
struct ResponseDisplayInfo { ResponseHeader header; DisplayInfo scanouts[16]; };

struct LayerResource {
    uint64_t surfaceId{};
    uint64_t pixelsVa{};
    uint32_t width{}, height{}, pitch{}, resourceId{};
    uint64_t lastUsedFrame{};
    bool active{};
};

struct alignas(4096) DmaPayload {
    VirtqDesc descriptors[kQueueSize]{};
    alignas(4096) VirtqAvail available{};
    alignas(4096) VirtqUsed used{};
    alignas(4096) uint8_t frame[kMaxWidth * kMaxHeight * 4]{};
    alignas(16) uint8_t command[sizeof(CommandAttachPrefix) + kMaxBackingPages * sizeof(MemoryEntry)]{};
    alignas(16) ResponseHeader response{};
};

static_assert(sizeof(CommandHeader) == 24);
static_assert(sizeof(CommandCreate3D) == 72);
static_assert(sizeof(CommandContextCreate) == 96);
static_assert(sizeof(CommandTransfer3D) == 72);
static_assert(sizeof(CommandSubmit3D) == 32);

struct alignas(4096) DeviceState {
    DmaPayload* dma{};
    uint64_t dmaToken{};
    uint64_t dmaAddress{};
    uint32_t originalPciCommand{};
    uint32_t width{}, height{}, pitch{}, pages{};
    uint64_t framePhysicalPages[kMaxBackingPages]{};
    volatile uint8_t* common{};
    volatile uint8_t* notify{};
    uint16_t queueSize{};
    uint16_t lastUsed{};
    uint16_t notifyOffset{};
    uint32_t notifyMultiplier{};
    uint32_t nextResourceId{100};
    uint64_t compositionFrame{};
    const KernelServices* services{};
    LayerResource layers[kMaxCompositionLayers]{};
    volatile uint32_t queueLock{};
    bool virglOffered{};
    bool virglActive{};
    bool initialized{};
};

DeviceState g_device{};

bool allocateDma(const KernelServices& services, uint64_t claim) {
    if (services.DmaVersion != DMA_SERVICES_VERSION || !services.AllocateDmaBuffer ||
        !services.FreeDmaBuffer) {
        if (services.Log) services.Log(services.Context, "virtio_gpu.sys: DMA service ABI unavailable");
        return false;
    }
    void* memory{};
    const int32_t status = services.AllocateDmaBuffer(services.Context, claim, sizeof(DmaPayload),
            alignof(DmaPayload), UINT64_MAX, &memory, &g_device.dmaAddress,
            &g_device.dmaToken);
    if (status != STATUS_SUCCESS || !memory || !g_device.dmaAddress || !g_device.dmaToken) {
        if (services.Log) {
            logHex(services, "virtio_gpu.sys: AllocateDmaBuffer status ",
                   static_cast<uint32_t>(status));
        }
        return false;
    }
    g_device.dma = static_cast<DmaPayload*>(memory);
    return true;
}

template <typename T> T mmioRead(volatile uint8_t* base, size_t offset) {
    return *reinterpret_cast<volatile T*>(base + offset);
}
template <typename T> void mmioWrite(volatile uint8_t* base, size_t offset, T value) {
    *reinterpret_cast<volatile T*>(base + offset) = value;
}

uint64_t physicalAddress(const void* address, size_t length) {
    if (g_device.dma && address && length) {
        const auto base = reinterpret_cast<uintptr_t>(g_device.dma);
        const auto value = reinterpret_cast<uintptr_t>(address);
        if (value >= base && value - base <= sizeof(DmaPayload) &&
            length <= sizeof(DmaPayload) - (value - base))
            return g_device.dmaAddress + (value - base);
    }
    uint64_t physical{};
    return g_device.services && g_device.services->TranslateDmaRange &&
        g_device.services->TranslateDmaRange(g_device.services->Context, address,
            length, &physical) == STATUS_SUCCESS ? physical : 0;
}

bool runCommand(const void* request, uint32_t requestBytes, ResponseHeader* response,
                uint32_t responseBytes, uint32_t expectedResponse) {
    if (!g_device.initialized && !g_device.common) return false;
    while (__atomic_exchange_n(&g_device.queueLock, 1u, __ATOMIC_ACQUIRE) != 0) cpuRelax();
    const uint64_t requestPa = physicalAddress(request, requestBytes);
    const uint64_t responsePa = physicalAddress(response, responseBytes);
    if (!requestPa || !responsePa || requestBytes == 0 || responseBytes == 0) {
        __atomic_store_n(&g_device.queueLock, 0u, __ATOMIC_RELEASE);
        return false;
    }
    g_device.dma->response.type = 0;

    const uint16_t first = 0;
    const uint16_t second = 1;
    g_device.dma->descriptors[first] = {requestPa, requestBytes, kDescNext, second};
    g_device.dma->descriptors[second] = {responsePa, responseBytes, kDescWrite, 0};
    const uint64_t descPa = physicalAddress(g_device.dma->descriptors, sizeof(g_device.dma->descriptors));
    const uint64_t availPa = physicalAddress(&g_device.dma->available, sizeof(g_device.dma->available));
    const uint64_t usedPa = physicalAddress(&g_device.dma->used, sizeof(g_device.dma->used));
    if (!descPa || !availPa || !usedPa) {
        __atomic_store_n(&g_device.queueLock, 0u, __ATOMIC_RELEASE);
        return false;
    }

    const uint16_t slot = static_cast<uint16_t>(g_device.dma->available.index % g_device.queueSize);
    g_device.dma->available.ring[slot] = first;
    ioBarrier();
    g_device.dma->available.index = static_cast<uint16_t>(g_device.dma->available.index + 1);
    ioBarrier();
    mmioWrite<uint16_t>(g_device.notify,
        static_cast<size_t>(g_device.notifyOffset) * g_device.notifyMultiplier, 0);

    for (uint32_t spin = 0; spin < 20000000u; ++spin) {
        ioBarrier();
        if (g_device.dma->used.index != g_device.lastUsed) {
            g_device.lastUsed = g_device.dma->used.index;
            ioBarrier();
            const bool succeeded = response->type == expectedResponse;
            __atomic_store_n(&g_device.queueLock, 0u, __ATOMIC_RELEASE);
            return succeeded;
        }
        cpuRelax();
    }
    (void)descPa; (void)availPa; (void)usedPa;
    __atomic_store_n(&g_device.queueLock, 0u, __ATOMIC_RELEASE);
    return false;
}

void initHeader(CommandHeader& header, uint32_t type) {
    header = {};
    header.type = type;
}

bool sendNoData(const void* request, uint32_t requestSize) {
    return runCommand(request, requestSize, &g_device.dma->response,
                      sizeof(g_device.dma->response), kRespOkNoData);
}

bool initializeQueue(const PciLocation& location, const PciCaps& caps,
                     const KernelServices& services) {
    const uint64_t commonPhysical = caps.common.physicalBase + caps.common.offset;
    const uint64_t notifyPhysical = caps.notify.physicalBase + caps.notify.offset;
    g_device.common = static_cast<volatile uint8_t*>(services.MapDeviceMemory(
        services.Context, commonPhysical, caps.common.length));
    g_device.notify = static_cast<volatile uint8_t*>(services.MapDeviceMemory(
        services.Context, notifyPhysical, caps.notify.length));
    // The device-specific PCI configuration BAR may be a 64-bit VRAM aperture
    // above the kernel's current direct MMIO window; 2D command transport does
    // not access it, so only map common configuration and queue notifications.
    if (!g_device.common || !g_device.notify) {
        if (services.Log) {
            services.Log(services.Context, "virtio_gpu.sys: PCI BAR MMIO mapping failed");
        }
        return false;
    }

    mmioWrite<uint8_t>(g_device.common, 20, 0); // reset device status
    for (uint32_t spin = 0; spin < 1000000u && mmioRead<uint8_t>(g_device.common, 20) != 0; ++spin) cpuRelax();
    mmioWrite<uint8_t>(g_device.common, 20, kVirtioStatusAcknowledge);
    mmioWrite<uint8_t>(g_device.common, 20, kVirtioStatusAcknowledge | kVirtioStatusDriver);

    mmioWrite<uint32_t>(g_device.common, 0, 0);
    const uint32_t featuresLow = mmioRead<uint32_t>(g_device.common, 4);
    mmioWrite<uint32_t>(g_device.common, 0, 1);
    const uint32_t featuresHigh = mmioRead<uint32_t>(g_device.common, 4);
    if ((featuresHigh & kVirtioFeatureVersion1) == 0) {
        if (services.Log) logHex(services, "virtio_gpu.sys: offered high feature bits ", featuresHigh);
        return false;
    }

    // VIRGL is optional: negotiate it when offered, while retaining VERSION_1
    // and the 2D path so hosts without virglrenderer still work.
    g_device.virglOffered = (featuresLow & kVirtioGpuFeatureVirgl) != 0;
    mmioWrite<uint32_t>(g_device.common, 8, 0);
    mmioWrite<uint32_t>(g_device.common, 12, g_device.virglOffered ? kVirtioGpuFeatureVirgl : 0);
    mmioWrite<uint32_t>(g_device.common, 8, 1);
    mmioWrite<uint32_t>(g_device.common, 12, kVirtioFeatureVersion1);
    mmioWrite<uint8_t>(g_device.common, 20,
        kVirtioStatusAcknowledge | kVirtioStatusDriver | kVirtioStatusFeaturesOk);
    if ((mmioRead<uint8_t>(g_device.common, 20) & kVirtioStatusFeaturesOk) == 0) {
        if (services.Log) services.Log(services.Context, "virtio_gpu.sys: device rejected negotiated feature set");
        return false;
    }

    mmioWrite<uint16_t>(g_device.common, 22, 0); // control queue
    const uint16_t offeredSize = mmioRead<uint16_t>(g_device.common, 24);
    if (offeredSize < 2) {
        if (services.Log) logHex(services, "virtio_gpu.sys: control queue size ", offeredSize);
        return false;
    }
    g_device.queueSize = offeredSize < kQueueSize ? offeredSize : kQueueSize;
    mmioWrite<uint16_t>(g_device.common, 24, g_device.queueSize);
    g_device.notifyOffset = mmioRead<uint16_t>(g_device.common, 30);
    g_device.notifyMultiplier = caps.notify.notifyMultiplier;

    const uint64_t descPa = physicalAddress(g_device.dma->descriptors, sizeof(g_device.dma->descriptors));
    const uint64_t availPa = physicalAddress(&g_device.dma->available, sizeof(g_device.dma->available));
    const uint64_t usedPa = physicalAddress(&g_device.dma->used, sizeof(g_device.dma->used));
    if (!descPa || !availPa || !usedPa) {
        if (services.Log) services.Log(services.Context, "virtio_gpu.sys: queue ring address translation failed");
        return false;
    }
    mmioWrite<uint64_t>(g_device.common, 32, descPa);
    mmioWrite<uint64_t>(g_device.common, 40, availPa);
    mmioWrite<uint64_t>(g_device.common, 48, usedPa);
    ioBarrier();
    mmioWrite<uint16_t>(g_device.common, 28, 1);
    mmioWrite<uint8_t>(g_device.common, 20,
        kVirtioStatusAcknowledge | kVirtioStatusDriver | kVirtioStatusFeaturesOk | kVirtioStatusDriverOk);
    g_device.services = &services;
    g_device.initialized = true;
    if (services.Log) services.Log(services.Context, "virtio_gpu.sys: control queue ready");
    return true;
}

bool createScanout(uint32_t width, uint32_t height) {
    ResponseDisplayInfo displayInfo{};
    CommandHeader displayRequest{};
    initHeader(displayRequest, kCmdGetDisplayInfo);
    if (!runCommand(&displayRequest, sizeof(displayRequest), &displayInfo.header,
                    sizeof(displayInfo), kRespOkDisplayInfo)) return false;
    if (displayInfo.scanouts[0].enabled == 0 || displayInfo.scanouts[0].rect.width == 0 ||
        displayInfo.scanouts[0].rect.height == 0) return false;

    // GET_DISPLAY_INFO reports the firmware's current scanout, not a maximum
    // mode. VirtIO permits SET_SCANOUT to resize the host output to the new
    // resource rectangle, so do not clamp a useful desktop mode to a tiny UEFI
    // boot logo mode (QEMU commonly starts at 267x200).
    if (width < 640 || height < 480) {
        width = 1024;
        height = 768;
    }
    g_device.width = width > 0 && width <= kMaxWidth ? width : kMaxWidth;
    g_device.height = height > 0 && height <= kMaxHeight ? height : kMaxHeight;
    if (g_device.width == 0 || g_device.height == 0) return false;
    g_device.pitch = g_device.width;
    const uint32_t frameBytes = g_device.width * g_device.height * 4;
    g_device.pages = (frameBytes + 4095) / 4096;
    if (g_device.pages > kMaxBackingPages) return false;

    for (uint32_t page = 0; page < g_device.pages; ++page) {
        const uint64_t pa = physicalAddress(g_device.dma->frame + static_cast<size_t>(page) * 4096, 4096);
        if (!pa) return false;
        g_device.framePhysicalPages[page] = pa & ~0xFFFULL;
    }

    CommandResourceCreate2D create{};
    initHeader(create.header, kCmdResourceCreate2D);
    create.resourceId = kResourceId;
    create.format = kFormatB8G8R8A8Unorm;
    create.width = g_device.width;
    create.height = g_device.height;
    if (!sendNoData(&create, sizeof(create))) return false;

    auto* attach = reinterpret_cast<CommandAttachPrefix*>(g_device.dma->command);
    initHeader(attach->header, kCmdResourceAttachBacking);
    attach->resourceId = kResourceId;
    attach->entries = g_device.pages;
    auto* entries = reinterpret_cast<MemoryEntry*>(g_device.dma->command + sizeof(CommandAttachPrefix));
    uint32_t remaining = frameBytes;
    for (uint32_t page = 0; page < g_device.pages; ++page) {
        entries[page] = {g_device.framePhysicalPages[page], 4096, 0};
        remaining = remaining > 4096 ? remaining - 4096 : 0;
    }
    if (!sendNoData(g_device.dma->command, sizeof(CommandAttachPrefix) + g_device.pages * sizeof(MemoryEntry))) {
        CommandUnref unref{};
        initHeader(unref.header, kCmdResourceUnref);
        unref.resourceId = kResourceId;
        sendNoData(&unref, sizeof(unref));
        return false;
    }

    CommandSetScanout scanout{};
    initHeader(scanout.header, kCmdSetScanout);
    scanout.rect = {0, 0, g_device.width, g_device.height};
    scanout.scanoutId = 0;
    scanout.resourceId = kResourceId;
    if (!sendNoData(&scanout, sizeof(scanout))) return false;

    CommandTransfer2D transfer{};
    initHeader(transfer.header, kCmdTransferToHost2D);
    transfer.rect = {0, 0, g_device.width, g_device.height};
    transfer.resourceId = kResourceId;
    if (!sendNoData(&transfer, sizeof(transfer))) return false;
    CommandFlush flush{};
    initHeader(flush.header, kCmdResourceFlush);
    flush.rect = {0, 0, g_device.width, g_device.height};
    flush.resourceId = kResourceId;
    return sendNoData(&flush, sizeof(flush));
}

bool attachResourceBacking(uint32_t resourceId, const uint32_t* pixels,
                           uint32_t width, uint32_t height, uint32_t pitch) {
    if (!pixels || !width || !height || pitch < width || width > kMaxWidth ||
        height > kMaxHeight || pitch > kMaxWidth) return false;
    const uint64_t bytes = static_cast<uint64_t>(pitch) * height * sizeof(uint32_t);
    const uint64_t pixelVa = reinterpret_cast<uint64_t>(pixels);
    const uint64_t firstPageVa = pixelVa & ~0xFFFULL;
    const uint32_t firstOffset = static_cast<uint32_t>(pixelVa & 0xFFFULL);
    const uint32_t pageCount = static_cast<uint32_t>((firstOffset + bytes + 4095) / 4096);
    if (pageCount == 0 || pageCount > kMaxBackingPages) return false;

    auto* attach = reinterpret_cast<CommandAttachPrefix*>(g_device.dma->command);
    initHeader(attach->header, kCmdResourceAttachBacking);
    attach->resourceId = resourceId;
    attach->entries = pageCount;
    auto* entries = reinterpret_cast<MemoryEntry*>(g_device.dma->command + sizeof(CommandAttachPrefix));
    for (uint32_t page = 0; page < pageCount; ++page) {
        const auto* va = reinterpret_cast<const void*>(firstPageVa + static_cast<uint64_t>(page) * 4096);
        const uint64_t pa = physicalAddress(va, 4096);
        if (!pa) return false;
        entries[page] = {pa & ~0xFFFULL, 4096, 0};
    }
    return sendNoData(g_device.dma->command,
        sizeof(CommandAttachPrefix) + pageCount * sizeof(MemoryEntry));
}

bool transferResource3D(uint32_t resourceId, const uint32_t* pixels,
                        uint32_t width, uint32_t height, uint32_t pitch) {
    CommandTransfer3D transfer{};
    initHeader(transfer.header, kCmdTransferToHost3D);
    transfer.header.contextId = kVirglContextId;
    transfer.box = {0, 0, 0, width, height, 1};
    transfer.offset = reinterpret_cast<uint64_t>(pixels) & 0xFFFULL;
    transfer.resourceId = resourceId;
    transfer.stride = pitch * sizeof(uint32_t);
    transfer.layerStride = transfer.stride * height;
    return sendNoData(&transfer, sizeof(transfer));
}

bool createResource3D(uint32_t resourceId, uint32_t width, uint32_t height,
                      const uint32_t* pixels, uint32_t pitch) {
    CommandCreate3D create{};
    initHeader(create.header, kCmdResourceCreate3D);
    create.resourceId = resourceId;
    create.target = kVirglTargetTexture2D;
    create.format = kVirglFormatB8G8R8A8;
    create.bind = kVirglBindDisplayTarget | kVirglBindRenderTarget | kVirglBindSamplerView;
    create.width = width;
    create.height = height;
    create.depth = 1;
    create.arraySize = 1;
    if (!sendNoData(&create, sizeof(create))) return false;
    if (!attachResourceBacking(resourceId, pixels, width, height, pitch)) return false;

    CommandContextResource attach{};
    initHeader(attach.header, kCmdCtxAttachResource);
    attach.header.contextId = kVirglContextId;
    attach.resourceId = resourceId;
    return sendNoData(&attach, sizeof(attach));
}

void destroyResource3D(uint32_t resourceId) {
    CommandContextResource detach{};
    initHeader(detach.header, 0x0203); // VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE
    detach.header.contextId = kVirglContextId;
    detach.resourceId = resourceId;
    sendNoData(&detach, sizeof(detach));
    CommandUnref unref{};
    initHeader(unref.header, kCmdResourceUnref);
    unref.resourceId = resourceId;
    sendNoData(&unref, sizeof(unref));
}

bool setupVirgl() {
    if (!g_device.virglOffered) return false;
    CommandContextCreate context{};
    initHeader(context.header, kCmdCtxCreate);
    context.header.contextId = kVirglContextId;
    constexpr char contextName[] = "MicaNT Desktop Compositor";
    context.nameLength = sizeof(contextName) - 1;
    for (size_t i = 0; i < sizeof(contextName); ++i) context.name[i] = contextName[i];
    if (!sendNoData(&context, sizeof(context))) return false;

    if (!createResource3D(kVirglOutputResourceId, g_device.width, g_device.height,
                          reinterpret_cast<const uint32_t*>(g_device.dma->frame), g_device.pitch)) {
        destroyResource3D(kVirglOutputResourceId);
        return false;
    }
    if (!transferResource3D(kVirglOutputResourceId,
            reinterpret_cast<const uint32_t*>(g_device.dma->frame), g_device.width, g_device.height, g_device.pitch)) {
        destroyResource3D(kVirglOutputResourceId);
        return false;
    }
    CommandSetScanout scanout{};
    initHeader(scanout.header, kCmdSetScanout);
    scanout.rect = {0, 0, g_device.width, g_device.height};
    scanout.scanoutId = 0;
    scanout.resourceId = kVirglOutputResourceId;
    if (!sendNoData(&scanout, sizeof(scanout))) {
        destroyResource3D(kVirglOutputResourceId);
        return false;
    }
    g_device.virglActive = true;
    if (g_device.services->Log)
        g_device.services->Log(g_device.services->Context, "virtio_gpu.sys: VirGL 3D compositor enabled");
    return true;
}

bool submitCopyRegion(uint32_t sourceResource, uint32_t srcX, uint32_t srcY,
                      uint32_t dstX, uint32_t dstY, uint32_t width, uint32_t height) {
    if (!width || !height) return true;
    auto* submit = reinterpret_cast<CommandSubmit3D*>(g_device.dma->command);
    initHeader(submit->header, kCmdSubmit3D);
    submit->header.contextId = kVirglContextId;
    submit->size = 14 * sizeof(uint32_t); // 1 VirGL header dword + 13 payload dwords.
    auto* words = reinterpret_cast<uint32_t*>(g_device.dma->command + sizeof(CommandSubmit3D));
    words[0] = kVirglCopyRegion | (13u << 16);
    words[1] = kVirglOutputResourceId;
    words[2] = 0;
    words[3] = dstX; words[4] = dstY; words[5] = 0;
    words[6] = sourceResource;
    words[7] = 0;
    words[8] = srcX; words[9] = srcY; words[10] = 0;
    words[11] = width; words[12] = height; words[13] = 1;
    ioBarrier();
    return sendNoData(g_device.dma->command, sizeof(CommandSubmit3D) + submit->size);
}

LayerResource* findOrCreateLayer(const micant::boot_driver::CompositionLayer& layer) {
    LayerResource* slot = nullptr;
    for (auto& resource : g_device.layers) {
        if (resource.active && resource.surfaceId == layer.SurfaceId) { slot = &resource; break; }
        if (!resource.active && !slot) slot = &resource;
    }
    if (!slot) {
        slot = &g_device.layers[0];
        for (auto& resource : g_device.layers) {
            if (resource.lastUsedFrame < slot->lastUsedFrame) slot = &resource;
        }
        destroyResource3D(slot->resourceId);
        *slot = {};
    }
    if (!slot) return nullptr;
    const uint64_t pixelVa = reinterpret_cast<uint64_t>(layer.Pixels);
    if (!slot->active || slot->width != layer.Width || slot->height != layer.Height ||
        slot->pitch != layer.Pitch) {
        if (slot->active) destroyResource3D(slot->resourceId);
        *slot = {};
        slot->surfaceId = layer.SurfaceId;
        slot->resourceId = g_device.nextResourceId++;
        slot->width = layer.Width;
        slot->height = layer.Height;
        slot->pitch = layer.Pitch;
        if (!createResource3D(slot->resourceId, layer.Width, layer.Height, layer.Pixels, layer.Pitch)) {
            destroyResource3D(slot->resourceId);
            *slot = {};
            return nullptr;
        }
        slot->pixelsVa = pixelVa;
        slot->lastUsedFrame = g_device.compositionFrame;
        slot->active = true;
        return slot;
    }
    if (slot->pixelsVa != pixelVa) {
        CommandDetachBacking detach{};
        initHeader(detach.header, 0x0107); // VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING
        detach.resourceId = slot->resourceId;
        if (!sendNoData(&detach, sizeof(detach)) ||
            !attachResourceBacking(slot->resourceId, layer.Pixels, layer.Width, layer.Height, layer.Pitch))
            return nullptr;
        slot->pixelsVa = pixelVa;
    }
    slot->lastUsedFrame = g_device.compositionFrame;
    return slot;
}

bool presentComposition(void*, const uint32_t* base, uint32_t width, uint32_t height,
                        uint32_t pitch, const micant::boot_driver::CompositionLayer* layers,
                        uint32_t layerCount) {
    if (!g_device.virglActive || !base || (layerCount && !layers) || width != g_device.width ||
        height != g_device.height || pitch < width || layerCount > kMaxCompositionLayers) return false;
    ++g_device.compositionFrame;
    for (uint32_t row = 0; row < height; ++row) {
        const uint32_t* source = base + static_cast<size_t>(row) * pitch;
        uint32_t* destination = reinterpret_cast<uint32_t*>(g_device.dma->frame) +
            static_cast<size_t>(row) * g_device.pitch;
        for (uint32_t col = 0; col < width; ++col) destination[col] = source[col];
    }
    ioBarrier();
    if (!transferResource3D(kVirglOutputResourceId,
            reinterpret_cast<const uint32_t*>(g_device.dma->frame), width, height, g_device.pitch)) return false;

    for (uint32_t index = 0; index < layerCount; ++index) {
        const auto& layer = layers[index];
        if (!layer.Pixels || !layer.Width || !layer.Height || layer.Pitch < layer.Width ||
            layer.Opacity != 0xFF || layer.Width > kMaxWidth || layer.Height > kMaxHeight) return false;
        const int64_t sourceX = layer.X < 0 ? -static_cast<int64_t>(layer.X) : 0;
        const int64_t sourceY = layer.Y < 0 ? -static_cast<int64_t>(layer.Y) : 0;
        const int64_t destinationX = layer.X > 0 ? layer.X : 0;
        const int64_t destinationY = layer.Y > 0 ? layer.Y : 0;
        int64_t copyWidth = static_cast<int64_t>(layer.Width) - sourceX;
        int64_t copyHeight = static_cast<int64_t>(layer.Height) - sourceY;
        if (destinationX + copyWidth > width) copyWidth = width - destinationX;
        if (destinationY + copyHeight > height) copyHeight = height - destinationY;
        if (copyWidth <= 0 || copyHeight <= 0) continue;

        LayerResource* resource = findOrCreateLayer(layer);
        if (!resource || !transferResource3D(resource->resourceId, layer.Pixels,
                layer.Width, layer.Height, layer.Pitch) ||
            !submitCopyRegion(resource->resourceId, static_cast<uint32_t>(sourceX),
                static_cast<uint32_t>(sourceY), static_cast<uint32_t>(destinationX),
                static_cast<uint32_t>(destinationY), static_cast<uint32_t>(copyWidth),
                static_cast<uint32_t>(copyHeight))) return false;
    }
    return true;
}

bool presentFrame(void*, const uint32_t* pixels, uint32_t width, uint32_t height,
                  uint32_t pitch, int32_t dstX, int32_t dstY) {
    if (!g_device.initialized || !pixels || width == 0 || height == 0 || pitch < width) return false;
    const int64_t srcLeft = dstX < 0 ? -static_cast<int64_t>(dstX) : 0;
    const int64_t srcTop = dstY < 0 ? -static_cast<int64_t>(dstY) : 0;
    const int64_t left = dstX > 0 ? dstX : 0;
    const int64_t top = dstY > 0 ? dstY : 0;
    int64_t copyWidth = static_cast<int64_t>(width) - srcLeft;
    int64_t copyHeight = static_cast<int64_t>(height) - srcTop;
    if (left + copyWidth > g_device.width) copyWidth = g_device.width - left;
    if (top + copyHeight > g_device.height) copyHeight = g_device.height - top;
    if (copyWidth <= 0 || copyHeight <= 0) return false;
    for (int64_t row = 0; row < copyHeight; ++row) {
        const uint32_t* src = pixels + (srcTop + row) * pitch + srcLeft;
        uint32_t* dst = reinterpret_cast<uint32_t*>(g_device.dma->frame) + (top + row) * g_device.pitch + left;
        for (int64_t col = 0; col < copyWidth; ++col) dst[col] = src[col];
    }
    ioBarrier();
    if (g_device.virglActive) {
        return transferResource3D(kVirglOutputResourceId,
            reinterpret_cast<const uint32_t*>(g_device.dma->frame), g_device.width,
            g_device.height, g_device.pitch);
    }
    CommandTransfer2D transfer{};
    initHeader(transfer.header, kCmdTransferToHost2D);
    transfer.rect = {0, 0, g_device.width, g_device.height};
    transfer.resourceId = kResourceId;
    if (!sendNoData(&transfer, sizeof(transfer))) return false;
    CommandFlush flush{};
    initHeader(flush.header, kCmdResourceFlush);
    flush.rect = {0, 0, g_device.width, g_device.height};
    flush.resourceId = kResourceId;
    return sendNoData(&flush, sizeof(flush));
}

} // namespace

extern "C" int32_t DriverEntry(micant::boot_driver::DriverObject* driver,
                                const micant::boot_driver::UnicodeString*) {
    using namespace micant::boot_driver;
    if (!driver || driver->Size < sizeof(DriverObject) ||
        driver->Version != DRIVER_ABI_VERSION || !driver->HardwareResources || !driver->Display) {
        return STATUS_INVALID_PARAMETER;
    }
    const auto& services = *static_cast<const KernelServices*>(driver->HardwareResources);
    if (services.Size < sizeof(KernelServices) ||
        services.Version != KERNEL_SERVICES_VERSION || !services.PciReadConfig32 ||
        !services.PciWriteConfig32 || !services.ClaimPciDevice ||
        !services.ReleasePciDevice || !services.TranslateDmaRange ||
        !services.MapDeviceMemory || !services.ReleaseDeviceMemory ||
        services.DmaVersion != DMA_SERVICES_VERSION || !services.AllocateDmaBuffer ||
        !services.FreeDmaBuffer)
        return STATUS_INVALID_PARAMETER;
    g_device.services = &services;
    PciLocation location{};
    PciCaps caps{};
    if (!findGpu(location, caps, services)) {
        if (services.Log) services.Log(services.Context,
            "virtio_gpu.sys: no safe controller start; PCI claim/DMA backend unavailable or no GPU");
        return STATUS_NOT_SUPPORTED;
    }
    if (!allocateDma(services, g_pciClaimToken)) {
        (void)services.ReleasePciDevice(services.Context, g_pciClaimToken);
        g_pciClaimToken = 0;
        if (services.Log) services.Log(services.Context,
            "virtio_gpu.sys: claimed controller has no safe DMA buffer");
        return STATUS_DEVICE_NOT_READY;
    }
    if (services.PciReadConfig32(services.Context, location.bus, location.device,
            location.function, 0x04, &g_device.originalPciCommand) != STATUS_SUCCESS ||
        services.PciWriteConfig32(services.Context, location.bus, location.device,
            location.function, 0x04,
            (g_device.originalPciCommand & 0x0000FFFFu) | 0x00000006u) != STATUS_SUCCESS) {
        if (services.Log) services.Log(services.Context,
            "virtio_gpu.sys: PCI memory/bus-master enable failed");
        (void)services.FreeDmaBuffer(services.Context, g_device.dmaToken);
        g_device.dma = nullptr;
        g_device.dmaToken = 0;
        (void)services.ReleasePciDevice(services.Context, g_pciClaimToken);
        g_pciClaimToken = 0;
        return STATUS_DEVICE_NOT_READY;
    }
    if (!caps.hasCommon || !caps.hasNotify || !caps.hasDevice || !services.MapDeviceMemory) {
        if (services.Log) {
            if (!caps.hasCommon) services.Log(services.Context, "virtio_gpu.sys: PCI common configuration capability missing");
            if (!caps.hasNotify) services.Log(services.Context, "virtio_gpu.sys: PCI queue notification capability missing");
            if (!caps.hasDevice) services.Log(services.Context, "virtio_gpu.sys: PCI device configuration capability missing");
            if (!services.MapDeviceMemory) services.Log(services.Context, "virtio_gpu.sys: kernel MMIO mapping service missing");
        }
        return STATUS_NOT_SUPPORTED;
    }
    if (!initializeQueue(location, caps, services)) {
        (void)services.PciWriteConfig32(services.Context, location.bus, location.device,
            location.function, 0x04,
            (g_device.originalPciCommand & 0x0000FFFFu) & ~0x00000004u);
        (void)services.FreeDmaBuffer(services.Context, g_device.dmaToken);
        g_device.dma = nullptr;
        g_device.dmaToken = 0;
        (void)services.ReleasePciDevice(services.Context, g_pciClaimToken);
        g_pciClaimToken = 0;
        if (services.Log) services.Log(services.Context, "virtio_gpu.sys: modern PCI transport initialization failed");
        return STATUS_DEVICE_NOT_READY;
    }
    const uint32_t requestedWidth = services.BootWidth;
    const uint32_t requestedHeight = services.BootHeight;
    if (!createScanout(requestedWidth, requestedHeight)) {
        g_device.initialized = false;
        mmioWrite<uint8_t>(g_device.common, 20, 0);
        (void)services.PciWriteConfig32(services.Context, location.bus, location.device,
            location.function, 0x04,
            (g_device.originalPciCommand & 0x0000FFFFu) & ~0x00000004u);
        (void)services.FreeDmaBuffer(services.Context, g_device.dmaToken);
        g_device.dma = nullptr;
        g_device.dmaToken = 0;
        (void)services.ReleasePciDevice(services.Context, g_pciClaimToken);
        g_pciClaimToken = 0;
        if (services.Log) services.Log(services.Context, "virtio_gpu.sys: 2D resource or scanout setup failed");
        return STATUS_DEVICE_NOT_READY;
    }
    const bool hasVirglCompositor = setupVirgl();
    if (!hasVirglCompositor && services.Log) {
        services.Log(services.Context, g_device.virglOffered
            ? "virtio_gpu.sys: VirGL initialization failed; using VirtIO 2D scanout"
            : "virtio_gpu.sys: VirGL not offered; using VirtIO 2D scanout");
    }

    driver->Flags |= DRIVER_FLAG_DISPLAY;
    driver->DeviceExtension = &g_device;
    driver->Display->Size = sizeof(DisplayDevice);
    driver->Display->Version = DRIVER_ABI_VERSION;
    driver->Display->Width = g_device.width;
    driver->Display->Height = g_device.height;
    driver->Display->Pitch = g_device.pitch;
    driver->Display->PixelFormat = kFormatB8G8R8A8Unorm;
    driver->Display->Context = &g_device;
    driver->Display->PresentFrame = presentFrame;
    driver->Display->PresentComposition = hasVirglCompositor ? presentComposition : nullptr;
    if (services.Log) services.Log(services.Context, "virtio_gpu.sys: VirtIO 1.0 2D scanout initialized");
    return STATUS_SUCCESS;
}
