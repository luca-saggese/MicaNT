#include "micant/shared/boot_driver.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "micant/km/drivers/storage/partition_table.hpp"

namespace {

using namespace micant::boot_driver;

constexpr int32_t STATUS_NO_SUCH_DEVICE = static_cast<int32_t>(0xC000000Eu);
constexpr int32_t STATUS_NO_MEMORY = static_cast<int32_t>(0xC0000017u);
constexpr int32_t STATUS_END_OF_FILE = static_cast<int32_t>(0xC0000011u);
constexpr int32_t STATUS_OBJECT_NAME_NOT_FOUND = static_cast<int32_t>(0xC0000034u);
constexpr int32_t STATUS_OBJECT_PATH_NOT_FOUND = static_cast<int32_t>(0xC000003Au);
constexpr int32_t STATUS_UNRECOGNIZED_VOLUME = static_cast<int32_t>(0xC000014Fu);
constexpr int32_t STATUS_DEVICE_DATA_ERROR = static_cast<int32_t>(0xC000009Cu);
constexpr int32_t STATUS_DEVICE_BUSY = static_cast<int32_t>(0x80000011u);
constexpr int32_t STATUS_INVALID_DEVICE_REQUEST = static_cast<int32_t>(0xC0000010u);
constexpr int32_t STATUS_FILE_IS_A_DIRECTORY = static_cast<int32_t>(0xC00000BAu);

constexpr uint32_t kSectorSize = 512;
constexpr uint64_t kMinimumHbaRegistersSize = 0x100;
constexpr uint64_t kMaximumHbaRegistersSize = 0x1100;
constexpr uint32_t kMaximumSectorsPerCommand = 128;
constexpr size_t kFileContextCount = 32;
constexpr size_t kMaximumPath = 256;
constexpr uint32_t kFileReadData = 0x00000001;
constexpr uint32_t kFileWriteData = 0x00000002;
constexpr uint32_t kFileSynchronize = 0x00100000;

struct __attribute__((packed)) FisRegH2D {
    uint8_t Type, Flags, Command, FeatureLow;
    uint8_t Lba0, Lba1, Lba2, Device;
    uint8_t Lba3, Lba4, Lba5, FeatureHigh;
    uint8_t CountLow, CountHigh, Icc, Control;
    uint8_t Reserved[4];
};

struct __attribute__((packed)) PrdtEntry {
    uint32_t Dba;
    uint32_t Dbau;
    uint32_t Reserved;
    uint32_t ByteCountAndInterrupt;
};

struct __attribute__((packed, aligned(128))) CommandTable {
    uint8_t CommandFis[64];
    uint8_t AtapiCommand[16];
    uint8_t Reserved[48];
    PrdtEntry Prdt;
};

struct __attribute__((packed)) CommandHeader {
    uint16_t Flags;
    uint16_t PrdtLength;
    uint32_t BytesTransferred;
    uint32_t TableBase;
    uint32_t TableBaseUpper;
    uint32_t Reserved[4];
};

struct alignas(4096) DmaStorage {
    alignas(1024) CommandHeader commandList[32];
    alignas(256) uint8_t receivedFis[256];
    alignas(128) CommandTable commandTable;
    alignas(4096) uint8_t data[65536];
};
DmaStorage* g_dma{};
micant::storage::partition_table::Extent g_partitionExtents[128]{};
uint8_t g_partitionScratch[8192]{};

struct DriverState {
    DriverObject* Driver{};
    const KernelServices* Services{};
    uint64_t PciClaimToken{};
    uint16_t Segment{};
    uint16_t Bus{};
    uint16_t Device{};
    uint16_t Function{};
    uint32_t OriginalPciCommand{};
    bool PciCommandChanged{};
    bool ControllerQuiesced{};
    uint32_t OriginalGhc{};
    bool GhcChanged{};
    PciBarResourceV1 Bar{};
    volatile uint32_t* Hba{};
    uint64_t HbaMappedLength{};
    volatile uint32_t* Port{};
    uint8_t PortNumber{0xFF};
    uint64_t DiskSectors{};
    uint8_t FlushCommand{};
    uint64_t CommandListDma{};
    uint64_t ReceivedFisDma{};
    uint64_t CommandTableDma{};
    uint64_t DataBufferDma{};
    uint64_t DmaToken{};
    uint64_t BlockDeviceHandle{};
    uint64_t MountToken{};
    bool Claimed{};
    bool Initialized{};
    bool Published{};
};

DriverState g_state{};
int32_t g_controllerStatus{STATUS_NO_SUCH_DEVICE};
std::atomic_flag g_ioLock = ATOMIC_FLAG_INIT;

void spinLock(std::atomic_flag& lock) noexcept {
    while (lock.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ __volatile__("pause");
#endif
    }
}

void spinUnlock(std::atomic_flag& lock) noexcept {
    lock.clear(std::memory_order_release);
}

uint16_t loadLe16(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t loadLe32(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t loadLe64(const uint8_t* bytes) noexcept {
    return static_cast<uint64_t>(loadLe32(bytes)) |
           (static_cast<uint64_t>(loadLe32(bytes + 4)) << 32);
}

bool pciRead(uint16_t bus, uint16_t device, uint16_t function,
             uint16_t offset, uint32_t& value) noexcept {
    const auto* services = g_state.Services;
    return services && services->PciReadConfig32 &&
           services->PciReadConfig32(services->Context, bus, device,
               function, offset, &value) == STATUS_SUCCESS;
}

bool pciWrite(uint16_t bus, uint16_t device, uint16_t function,
              uint16_t offset, uint32_t value) noexcept {
    const auto* services = g_state.Services;
    return services && services->PciWriteConfig32 &&
           services->PciWriteConfig32(services->Context, bus, device,
               function, offset, value) == STATUS_SUCCESS;
}

bool queryClock(uint64_t& ticks, uint64_t& frequency) noexcept {
    const auto* services = g_state.Services;
    return services && services->QueryMonotonicClock &&
        services->QueryMonotonicClock(services->Context, &ticks, &frequency) == STATUS_SUCCESS &&
        frequency != 0;
}

bool waitRegister(volatile uint32_t* reg, uint32_t mask, uint32_t expected,
                  uint32_t timeoutMs) noexcept {
    uint64_t start{}, frequency{};
    if (!reg || !timeoutMs || !queryClock(start, frequency)) return false;
    const uint64_t ticksPerMs = frequency / 1000u;
    if (ticksPerMs && ticksPerMs > UINT64_MAX / timeoutMs) return false;
    const uint64_t timeoutTicks = ticksPerMs ? ticksPerMs * timeoutMs : 1;
    for (unsigned polls = 0; polls < 100000; ++polls) {
        if ((*reg & mask) == expected) return true;
        uint64_t now{}, nowFrequency{};
        if (!queryClock(now, nowFrequency) || nowFrequency != frequency || now < start ||
            now - start >= timeoutTicks) return false;
        if (g_state.Services->DelayMicroseconds(g_state.Services->Context, 50) != STATUS_SUCCESS)
            return false;
    }
    return false;
}

bool stopPort(volatile uint32_t* port) noexcept {
    if (!port) return false;
    port[0x18 / 4] &= ~uint32_t{1};
    if (!waitRegister(&port[0x18 / 4], uint32_t{1} << 15, 0, 500)) return false;
    port[0x18 / 4] &= ~(uint32_t{1} << 4);
    return waitRegister(&port[0x18 / 4], uint32_t{1} << 14, 0, 500);
}

bool startPort(volatile uint32_t* port) noexcept {
    if (!waitRegister(&port[0x18 / 4], uint32_t{1} << 15, 0, 500)) return false;
    port[0x18 / 4] |= uint32_t{1} << 4;
    port[0x18 / 4] |= uint32_t{1};
    return true;
}

bool executeCommand(uint8_t command, uint64_t lba, uint16_t sectors) noexcept {
    const bool flush = command == 0xE7 || command == 0xEA;
    if (!g_state.Port || (!sectors && !flush) || sectors > kMaximumSectorsPerCommand) return false;
    auto& header = g_dma->commandList[0];
    header = {};
    header.Flags = static_cast<uint16_t>(5 | (command == 0x35 ? (1u << 6) : 0u));
    header.PrdtLength = sectors ? 1 : 0;
    header.TableBase = static_cast<uint32_t>(g_state.CommandTableDma);
    header.TableBaseUpper = static_cast<uint32_t>(g_state.CommandTableDma >> 32);

    g_dma->commandTable = {};
    auto& prdt = g_dma->commandTable.Prdt;
    if (sectors) {
        const size_t bytes = static_cast<size_t>(sectors) * kSectorSize;
        prdt.Dba = static_cast<uint32_t>(g_state.DataBufferDma);
        prdt.Dbau = static_cast<uint32_t>(g_state.DataBufferDma >> 32);
        prdt.ByteCountAndInterrupt = static_cast<uint32_t>(bytes - 1) | (uint32_t{1} << 31);
    }

    auto* fis = reinterpret_cast<FisRegH2D*>(g_dma->commandTable.CommandFis);
    fis->Type = 0x27;
    fis->Flags = 0x80;
    fis->Command = command;
    fis->Lba0 = static_cast<uint8_t>(lba);
    fis->Lba1 = static_cast<uint8_t>(lba >> 8);
    fis->Lba2 = static_cast<uint8_t>(lba >> 16);
    fis->Device = 0x40;
    fis->Lba3 = static_cast<uint8_t>(lba >> 24);
    fis->Lba4 = static_cast<uint8_t>(lba >> 32);
    fis->Lba5 = static_cast<uint8_t>(lba >> 40);
    fis->CountLow = static_cast<uint8_t>(sectors);
    fis->CountHigh = static_cast<uint8_t>(sectors >> 8);

#if defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("sfence" ::: "memory");
#endif
    g_state.Port[0x10 / 4] = 0xFFFFFFFFu;
    g_state.Port[0x38 / 4] = 1;
    if (!waitRegister(&g_state.Port[0x38 / 4], 1, 0, 500)) return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    const uint32_t taskFile = g_state.Port[0x20 / 4];
    return (taskFile & 0x21u) == 0;
}

bool readSectors(uint64_t lba, uint16_t count, void* destination) noexcept {
    if (!destination || !count || count > kMaximumSectorsPerCommand ||
        lba >= g_state.DiskSectors || count > g_state.DiskSectors - lba) return false;
    if (!executeCommand(0x25, lba, count)) return false; // READ DMA EXT
    if (destination != g_dma->data)
        std::memcpy(destination, g_dma->data, static_cast<size_t>(count) * kSectorSize);
    return true;
}

bool readPartitionSectors(void*, uint64_t lba, uint16_t count, void* destination) noexcept {
    return readSectors(lba, count, destination);
}

bool hasFat32BootSector(const uint8_t* boot, uint64_t volumeSectors) noexcept {
    if (!boot) return false;
    const uint16_t bytesPerSector = loadLe16(boot + 11);
    const uint8_t sectorsPerCluster = boot[13];
    const uint16_t reservedSectors = loadLe16(boot + 14);
    const uint8_t fatCount = boot[16];
    const uint32_t totalSectors = loadLe32(boot + 32);
    const uint32_t fatSectors = loadLe32(boot + 36);
    const uint32_t rootCluster = loadLe32(boot + 44);
    return boot[510] == 0x55 && boot[511] == 0xAA &&
        bytesPerSector == kSectorSize && sectorsPerCluster &&
        (sectorsPerCluster & (sectorsPerCluster - 1)) == 0 &&
        reservedSectors && fatCount && fatCount <= 2 &&
        !loadLe16(boot + 22) && totalSectors && fatSectors &&
        rootCluster >= 2 && totalSectors <= volumeSectors;
}

bool hasSupportedFatVolume() noexcept {
    using namespace micant::storage::partition_table;
    Result result{};
    const Device disk{nullptr, &readPartitionSectors, g_state.DiskSectors, kSectorSize,
                      g_partitionScratch, sizeof(g_partitionScratch)};
    if (discover(disk, g_partitionExtents,
                 sizeof(g_partitionExtents) / sizeof(g_partitionExtents[0]), result) != Status::Success)
        return false;
    if (result.kind == TableKind::None ||
        (result.kind == TableKind::Mbr && result.count == 0))
        return readSectors(0, 1, g_dma->data) &&
               hasFat32BootSector(g_dma->data, g_state.DiskSectors);
    static constexpr uint8_t basicDataGuid[16] = {
        0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,
        0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
    static constexpr uint8_t efiSystemGuid[16] = {
        0x28,0x73,0x2A,0xC1,0xF8,0x1F,0xD2,0x11,
        0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
    for (size_t i = 0; i < result.count; ++i) {
        const Extent& p = g_partitionExtents[i];
        bool candidate{};
        if (result.kind == TableKind::Mbr) candidate = p.mbrType == 0x0B || p.mbrType == 0x0C || p.mbrType == 0xEF;
        else candidate = std::memcmp(p.typeGuid, basicDataGuid, 16) == 0 ||
                         std::memcmp(p.typeGuid, efiSystemGuid, 16) == 0;
        if (candidate && readSectors(p.firstLba, 1, g_dma->data) &&
            hasFat32BootSector(g_dma->data, p.sectorCount))
            return true;
    }
    return false;
}

bool writeSectors(uint64_t lba, uint16_t count, const void* source) noexcept {
    if (!source || !count || count > kMaximumSectorsPerCommand ||
        lba >= g_state.DiskSectors || count > g_state.DiskSectors - lba) return false;
    if (source != g_dma->data)
        std::memcpy(g_dma->data, source, static_cast<size_t>(count) * kSectorSize);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return executeCommand(0x35, lba, count); // WRITE DMA EXT
}

bool identifyDisk(uint64_t& sectors) noexcept {
    std::memset(g_dma->data, 0, kSectorSize);
    if (!executeCommand(0xEC, 0, 1)) return false;
    const uint16_t commandSet = loadLe16(g_dma->data + 166);
    // The provider uses 48-bit LBA DMA reads and writes. Refuse devices that
    // do not advertise valid 48-bit LBA support.
    if ((commandSet & 0xC400u) != 0x4400u) return false;
    const uint16_t legacyCommands = loadLe16(g_dma->data + 164);
    if (commandSet & (1u << 13)) g_state.FlushCommand = 0xEA;
    else if (legacyCommands & (1u << 5)) g_state.FlushCommand = 0xE7;
    else return false;
    sectors = loadLe64(g_dma->data + 200);
    return sectors != 0 && sectors <= (uint64_t{1} << 48);
}

bool initializeController() noexcept {
    auto* services = const_cast<KernelServices*>(g_state.Services);
    if (!services || !services->ClaimPciDevice || !services->ReleasePciDevice ||
        !services->PciReadConfig32 || !services->PciWriteConfig32 ||
        !services->MapDeviceMemory || !services->ReleaseDeviceMemory ||
        services->Size < offsetof(KernelServices, FreeDmaBuffer) + sizeof(services->FreeDmaBuffer) ||
        services->DmaVersion != 1 || !services->AllocateDmaBuffer || !services->FreeDmaBuffer ||
        !services->QueryMonotonicClock ||
        !services->DelayMicroseconds) {
        g_controllerStatus = STATUS_INVALID_DEVICE_REQUEST;
        return false;
    }

    bool found{};
    int32_t lastClaimFailure = STATUS_NO_SUCH_DEVICE;
    for (uint16_t bus = 0; bus <= UINT8_MAX && !found; ++bus) {
      for (uint16_t device = 0; device < 32 && !found; ++device) {
        for (uint16_t function = 0; function < 8; ++function) {
            uint32_t vendorDevice{}, classCode{};
            if (!pciRead(bus, device, function, 0x00, vendorDevice) ||
                static_cast<uint16_t>(vendorDevice) == 0xFFFF ||
                !pciRead(bus, device, function, 0x08, classCode)) continue;
            const uint8_t progIf = static_cast<uint8_t>((classCode >> 8) & 0xFF);
            const uint8_t subclass = static_cast<uint8_t>((classCode >> 16) & 0xFF);
            const uint8_t baseClass = static_cast<uint8_t>((classCode >> 24) & 0xFF);
            if (baseClass != 1 || subclass != 6 || progIf != 1) continue;

            PciBarRequestV1 request{5, 0, 0, kMinimumHbaRegistersSize};
            size_t resourceCount = 1;
            uint64_t token{};
            PciBarResourceV1 resource{};
            const int32_t claimStatus = services->ClaimPciDevice(
                services->Context, 0, bus, device, function, &request, 1,
                &token, &resource, &resourceCount);
            if (claimStatus != STATUS_SUCCESS) {
                services->Log(services->Context,
                    "AHCI initialization skipped: canonical PCI resource claim rejected the controller");
                if (claimStatus == STATUS_DEVICE_BUSY)
                    services->Log(services->Context,
                        "AHCI PCI claim failed: BAR overlaps a reserved or claimed range");
                else if (claimStatus == STATUS_DEVICE_NOT_READY)
                    services->Log(services->Context,
                        "AHCI PCI claim failed: inventory or BAR length is unavailable");
                else if (claimStatus == STATUS_INVALID_PARAMETER)
                    services->Log(services->Context,
                        "AHCI PCI claim failed: BAR request was rejected as invalid");
                lastClaimFailure = claimStatus;
                if (claimStatus == STATUS_NOT_SUPPORTED) {
                    g_controllerStatus = claimStatus;
                    return false;
                }
                continue;
            }
            if (token) {
                g_state.PciClaimToken = token;
                g_state.Claimed = true;
                g_state.Device = device;
                g_state.Function = function;
                g_state.Bus = bus;
            }
            if (!token || resourceCount != 1) {
                g_controllerStatus = STATUS_NOT_SUPPORTED;
                return false;
            }
            if (resource.BarIndex != 5 || resource.PhysicalAddress == 0 ||
                resource.Length < kMinimumHbaRegistersSize ||
                resource.PhysicalAddress > UINT64_MAX - kMinimumHbaRegistersSize) {
                g_controllerStatus = STATUS_INVALID_PARAMETER;
                return false;
            }
            g_state.Bar = resource;
            found = true;
            break;
        }
      }
    }
    if (!found) {
        g_controllerStatus = lastClaimFailure;
        return false;
    }

    if (!pciRead(g_state.Bus, g_state.Device, g_state.Function, 0x04,
                 g_state.OriginalPciCommand)) {
        g_controllerStatus = STATUS_DEVICE_DATA_ERROR;
        return false;
    }
    const uint32_t memoryEnabledCommand =
        (g_state.OriginalPciCommand & 0x0000FFFFu) | 2u;
    if ((g_state.OriginalPciCommand & 0x0000FFFFu) != memoryEnabledCommand) {
        if (!pciWrite(g_state.Bus, g_state.Device, g_state.Function, 0x04,
                      memoryEnabledCommand)) {
            g_controllerStatus = STATUS_DEVICE_DATA_ERROR;
            return false;
        }
        g_state.PciCommandChanged = true;
    }

    const uint64_t mappedLength = g_state.Bar.Length < kMaximumHbaRegistersSize
        ? g_state.Bar.Length : kMaximumHbaRegistersSize;
    g_state.Hba = static_cast<volatile uint32_t*>(services->MapDeviceMemory(
        services->Context, g_state.Bar.PhysicalAddress, mappedLength));
    if (!g_state.Hba) {
        services->Log(services->Context, "AHCI initialization stopped: BAR5 MMIO mapping failed");
        g_controllerStatus = STATUS_DEVICE_NOT_READY;
        return false;
    }
    g_state.HbaMappedLength = mappedLength;

    const uint32_t portsImplemented = g_state.Hba[3];
    for (uint8_t portIndex = 0; portIndex < 32; ++portIndex) {
        if ((portsImplemented & (uint32_t{1} << portIndex)) == 0) continue;
        const uint64_t requiredLength = 0x100 + (static_cast<uint64_t>(portIndex) + 1) * 0x80;
        if (requiredLength > mappedLength) {
            services->Log(services->Context,
                "AHCI initialization stopped: BAR5 is too small for implemented ports");
            g_controllerStatus = STATUS_DEVICE_NOT_READY;
            return false;
        }
    }

    for (uint8_t portIndex = 0; portIndex < 32; ++portIndex) {
        if ((portsImplemented & (uint32_t{1} << portIndex)) == 0) continue;
        auto* port = reinterpret_cast<volatile uint32_t*>(
            reinterpret_cast<uintptr_t>(g_state.Hba) + 0x100 + portIndex * 0x80);
        if (!stopPort(port)) {
            services->Log(services->Context,
                "AHCI takeover stopped: an implemented port engine did not quiesce");
            g_controllerStatus = STATUS_DEVICE_NOT_READY;
            return false;
        }
    }
    g_state.ControllerQuiesced = true;
    const uint32_t quiescedCommand = memoryEnabledCommand & ~uint32_t{4};
    if (quiescedCommand != memoryEnabledCommand) {
        if (!pciWrite(g_state.Bus, g_state.Device, g_state.Function, 0x04,
                      quiescedCommand)) {
            g_controllerStatus = STATUS_DEVICE_DATA_ERROR;
            return false;
        }
        g_state.PciCommandChanged = true;
    }

    const bool supports64 = (g_state.Hba[0] & (uint32_t{1} << 31)) != 0;
    void* cpu{};
    uint64_t dma{};
    const uint64_t limit = supports64 ? UINT64_MAX : UINT32_MAX;
    if (services->AllocateDmaBuffer(services->Context, g_state.PciClaimToken,
            sizeof(DmaStorage), alignof(DmaStorage), limit, &cpu, &dma,
            &g_state.DmaToken) != STATUS_SUCCESS || !cpu || !dma || !g_state.DmaToken ||
        (reinterpret_cast<uintptr_t>(cpu) & (alignof(DmaStorage) - 1)) ||
        (dma & (alignof(DmaStorage) - 1)) ||
        dma > limit || sizeof(DmaStorage) - 1 > limit - dma) {
        g_controllerStatus = STATUS_NOT_SUPPORTED;
        return false;
    }
    g_dma = static_cast<DmaStorage*>(cpu);
    g_state.CommandListDma = dma + offsetof(DmaStorage, commandList);
    g_state.ReceivedFisDma = dma + offsetof(DmaStorage, receivedFis);
    g_state.CommandTableDma = dma + offsetof(DmaStorage, commandTable);
    g_state.DataBufferDma = dma + offsetof(DmaStorage, data);
    if (!pciWrite(g_state.Bus, g_state.Device, g_state.Function, 0x04,
                  (g_state.OriginalPciCommand & 0x0000FFFFu) | 6u)) {
        g_controllerStatus = STATUS_DEVICE_DATA_ERROR;
        return false;
    }
    g_state.PciCommandChanged = true;

    g_state.OriginalGhc = g_state.Hba[1];
    if ((g_state.OriginalGhc & (1u << 31)) == 0) {
        g_state.Hba[1] = g_state.OriginalGhc | (1u << 31);
        g_state.GhcChanged = true;
    }
    for (uint8_t portIndex = 0; portIndex < 32; ++portIndex) {
        if ((portsImplemented & (uint32_t{1} << portIndex)) == 0) continue;
        auto* port = reinterpret_cast<volatile uint32_t*>(
            reinterpret_cast<uintptr_t>(g_state.Hba) + 0x100 + portIndex * 0x80);
        const uint32_t ssts = port[0x28 / 4];
        if ((ssts & 0x0Fu) != 3 || ((ssts >> 8) & 0x0Fu) != 1 ||
            port[0x24 / 4] != 0x00000101u) continue;
        g_dma->commandList[0] = {};
        port[0x00 / 4] = static_cast<uint32_t>(g_state.CommandListDma);
        port[0x04 / 4] = static_cast<uint32_t>(g_state.CommandListDma >> 32);
        port[0x08 / 4] = static_cast<uint32_t>(g_state.ReceivedFisDma);
        port[0x0C / 4] = static_cast<uint32_t>(g_state.ReceivedFisDma >> 32);
        port[0x10 / 4] = 0xFFFFFFFFu;
        port[0x30 / 4] = 0xFFFFFFFFu;
        g_state.Port = port;
        if (!startPort(port)) {
            services->Log(services->Context, "AHCI initialization stopped: SATA port could not be started");
            return false;
        }
        uint64_t sectors{};
        if (!identifyDisk(sectors)) {
            services->Log(services->Context,
                "AHCI initialization stopped: SATA IDENTIFY failed or disk lacks required LBA48/flush support");
            if (!stopPort(port)) return false;
            g_state.Port = nullptr;
            continue;
        }
        g_state.DiskSectors = sectors;
        if (!hasSupportedFatVolume()) {
            services->Log(services->Context,
                "AHCI skipped SATA disk: no validated FAT32 superfloppy or partition found");
            if (!stopPort(port)) return false;
            g_state.Port = nullptr;
            g_state.DiskSectors = 0;
            continue;
        }
        g_state.PortNumber = portIndex;
        g_state.Initialized = true;
        g_controllerStatus = STATUS_SUCCESS;
        return true;
    }
    services->Log(services->Context, "AHCI initialization stopped: no implemented port matched SATA link/signature");
    g_controllerStatus = STATUS_DEVICE_NOT_READY;
    return false;
}

bool drainPciTransactions() noexcept {
    uint32_t status{}, capabilities{};
    if (!pciRead(g_state.Bus, g_state.Device, g_state.Function, 0x04, status) ||
        !(status & (1u << 20)) ||
        !pciRead(g_state.Bus, g_state.Device, g_state.Function, 0x34, capabilities)) return false;
    uint8_t pointer = static_cast<uint8_t>(capabilities);
    uint16_t deviceStatusOffset{};
    for (unsigned count = 0; pointer && count < 48; ++count) {
        if (pointer < 0x40 || pointer > 0xf4 || (pointer & 3)) return false;
        uint32_t capability{};
        if (!pciRead(g_state.Bus, g_state.Device, g_state.Function, pointer, capability)) return false;
        if ((capability & 0xffu) == 0x10u) {
            deviceStatusOffset = static_cast<uint16_t>(pointer + 8);
            break;
        }
        pointer = static_cast<uint8_t>(capability >> 8);
    }
    if (!deviceStatusOffset) return false;
    uint64_t start{}, frequency{};
    if (!queryClock(start, frequency) || frequency < 10) return false;
    for (unsigned polls = 0; polls < 10000; ++polls) {
        uint32_t value{};
        if (!pciRead(g_state.Bus, g_state.Device, g_state.Function, deviceStatusOffset, value)) return false;
        if (!(value & (1u << 21))) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            return true;
        }
        uint64_t now{}, currentFrequency{};
        if (!queryClock(now, currentFrequency) || currentFrequency != frequency ||
            now < start || now - start >= frequency / 10) return false;
        if (g_state.Services->DelayMicroseconds(g_state.Services->Context, 50) != STATUS_SUCCESS)
            return false;
    }
    return false;
}

bool cleanupController() noexcept {
    if (!g_state.Services) return true;
    if (g_state.MountToken) {
        if (g_state.Services->UnmountFatBlockDevice(g_state.Services->Context, g_state.MountToken) != STATUS_SUCCESS) return false;
        g_state.MountToken = 0;
    }
    if (g_state.BlockDeviceHandle) {
        if (g_state.Services->CloseHandle(g_state.Services->Context, g_state.BlockDeviceHandle) != STATUS_SUCCESS) return false;
        g_state.BlockDeviceHandle = 0;
    }
    if (g_state.Port) {
        if (!stopPort(g_state.Port)) return false;
        g_state.Port = nullptr;
    }
    if (g_state.Hba && g_state.GhcChanged) {
        const uint32_t currentGhc = g_state.Hba[1];
        g_state.Hba[1] = (currentGhc & ~(uint32_t{1} << 31)) |
                         (g_state.OriginalGhc & (uint32_t{1} << 31));
        g_state.GhcChanged = false;
    }
    if (g_state.PciCommandChanged) {
        uint32_t restoreCommand = g_state.OriginalPciCommand & 0x0000FFFFu;
        if (g_state.ControllerQuiesced) restoreCommand &= ~uint32_t{7};
        if (!pciWrite(g_state.Bus, g_state.Device, g_state.Function, 0x04,
                      restoreCommand)) return false;
        uint32_t command{};
        if (!pciRead(g_state.Bus, g_state.Device, g_state.Function, 0x04, command) ||
            (command & 0x0000FFFFu) != restoreCommand) return false;
        if (g_state.DmaToken && !drainPciTransactions()) return false;
        g_state.PciCommandChanged = false;
    }
    if (g_state.DmaToken) {
        if (g_state.Services->FreeDmaBuffer(g_state.Services->Context, g_state.DmaToken) != STATUS_SUCCESS)
            return false;
        g_state.DmaToken = 0;
        g_dma = nullptr;
    }
    if (g_state.Hba && g_state.Services->ReleaseDeviceMemory) {
        if (!g_state.Services->ReleaseDeviceMemory(g_state.Services->Context,
                const_cast<uint32_t*>(g_state.Hba),
                static_cast<size_t>(g_state.HbaMappedLength))) return false;
        g_state.Hba = nullptr;
        g_state.HbaMappedLength = 0;
    }
    if (g_state.Claimed && g_state.Services->ReleasePciDevice) {
        if (g_state.Services->ReleasePciDevice(g_state.Services->Context,
                g_state.PciClaimToken) != STATUS_SUCCESS) return false;
        g_state.Claimed = false;
    }
    g_state.Initialized = false;
    return true;
}

void unloadFailedDriver(DriverObject*) noexcept {
    (void)cleanupController();
}

void preserveFailedCleanup(DriverObject* driverObject) noexcept {
    if (cleanupController()) return;
    driverObject->DeviceExtension = &g_state;
    driverObject->DriverUnload = &unloadFailedDriver;
    g_state.Services->Log(g_state.Services->Context,
        "AHCI cleanup could not quiesce/release hardware; resources retained");
}

int32_t readDiskBlocks(void* context, uint64_t lba, uint32_t count,
                       void* buffer, uint32_t* blocksRead) noexcept {
    if (!blocksRead) return STATUS_INVALID_PARAMETER;
    *blocksRead = 0;
    if (!context || !buffer || count == 0) return STATUS_INVALID_PARAMETER;
    auto& state = *static_cast<DriverState*>(context);
    if (!state.Initialized || lba >= state.DiskSectors || count > state.DiskSectors - lba)
        return STATUS_END_OF_FILE;
    auto* output = static_cast<uint8_t*>(buffer);
    spinLock(g_ioLock);
    while (*blocksRead < count) {
        const uint32_t batch = (count - *blocksRead) > kMaximumSectorsPerCommand
            ? kMaximumSectorsPerCommand : (count - *blocksRead);
        if (!readSectors(lba + *blocksRead, static_cast<uint16_t>(batch), g_dma->data)) {
            spinUnlock(g_ioLock);
            return STATUS_DEVICE_DATA_ERROR;
        }
        std::memcpy(output + static_cast<size_t>(*blocksRead) * kSectorSize,
                    g_dma->data, static_cast<size_t>(batch) * kSectorSize);
        *blocksRead += batch;
    }
    spinUnlock(g_ioLock);
    return STATUS_SUCCESS;
}

int32_t writeDiskBlocks(void* context, uint64_t lba, uint32_t count,
                        const void* buffer, uint32_t* blocksWritten) noexcept {
    if (!blocksWritten) return STATUS_INVALID_PARAMETER;
    *blocksWritten = 0;
    if (!context || !buffer || count == 0) return STATUS_INVALID_PARAMETER;
    auto& state = *static_cast<DriverState*>(context);
    if (!state.Initialized || lba >= state.DiskSectors || count > state.DiskSectors - lba)
        return STATUS_END_OF_FILE;
    auto* input = static_cast<const uint8_t*>(buffer);
    spinLock(g_ioLock);
    while (*blocksWritten < count) {
        const uint32_t batch = (count - *blocksWritten) > kMaximumSectorsPerCommand
            ? kMaximumSectorsPerCommand : (count - *blocksWritten);
        if (!writeSectors(lba + *blocksWritten, static_cast<uint16_t>(batch),
                input + static_cast<size_t>(*blocksWritten) * kSectorSize)) {
            spinUnlock(g_ioLock);
            return STATUS_DEVICE_DATA_ERROR;
        }
        *blocksWritten += batch;
    }
    if (!g_state.FlushCommand || !executeCommand(g_state.FlushCommand, 0, 0)) {
        spinUnlock(g_ioLock);
        return STATUS_DEVICE_DATA_ERROR;
    }
    spinUnlock(g_ioLock);
    return STATUS_SUCCESS;
}

int32_t flushDisk(void* context) noexcept {
    if (!context) return STATUS_INVALID_PARAMETER;
    auto& state = *static_cast<DriverState*>(context);
    if (!state.Initialized || !g_state.FlushCommand) return STATUS_NOT_SUPPORTED;
    spinLock(g_ioLock);
    const bool ok = executeCommand(g_state.FlushCommand, 0, 0);
    spinUnlock(g_ioLock);
    return ok ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
}

void releaseBlockContext(void*) noexcept {
    // The backend context is static image state, so the generic Io object has
    // no allocation to release here. Published callbacks pin this boot image.
}

bool validServices(const KernelServices* services) noexcept {
    return services && services->Size >= sizeof(KernelServices) &&
        services->Version == KERNEL_SERVICES_VERSION && services->Log &&
        services->PciReadConfig32 && services->PciWriteConfig32 &&
        services->MapDeviceMemory && services->ReleaseDeviceMemory &&
        services->DmaVersion == 1 && services->AllocateDmaBuffer && services->FreeDmaBuffer &&
        services->QueryMonotonicClock &&
        services->DelayMicroseconds && services->ClaimPciDevice &&
        services->ReleasePciDevice && services->WritableBlockDeviceVersion == 1 &&
        services->DurableBlockDeviceVersion == 1 && services->CreateDurableWritableBlockDevice &&
        services->FlushBlockDevice && services->CreateWritableBlockDevice && services->WriteBlockDevice &&
        services->ReadBlockDevice && services->FatFilesystemVersion == 1 &&
        services->MountFatBlockDevice && services->UnmountFatBlockDevice && services->CloseHandle;
}

} // namespace

extern "C" int32_t DriverEntry(DriverObject* driverObject,
                               const UnicodeString*) noexcept {
    if (!driverObject || driverObject->Size < sizeof(DriverObject) ||
        driverObject->Version != DRIVER_ABI_VERSION) return STATUS_INVALID_PARAMETER;
    auto* services = static_cast<const KernelServices*>(driverObject->HardwareResources);
    if (!validServices(services)) {
        if (services && services->Log)
            services->Log(services->Context,
                "AHCI/FAT32 start refused: KernelServices V2 PnP/DMA/Io callbacks unavailable");
        return STATUS_NOT_SUPPORTED;
    }

    g_state = {};
    g_controllerStatus = STATUS_NO_SUCH_DEVICE;
    g_state.Driver = driverObject;
    g_state.Services = services;
    if (!initializeController()) {
        preserveFailedCleanup(driverObject);
        services->Log(services->Context, "AHCI claim, BAR, DMA or SATA initialization failed");
        return g_controllerStatus;
    }
    const int32_t blockStatus = services->CreateDurableWritableBlockDevice(
        services->Context, g_state.DiskSectors, kSectorSize, &g_state,
        &readDiskBlocks, &writeDiskBlocks, &flushDisk, &releaseBlockContext,
        kFileReadData | kFileWriteData | kFileSynchronize,
        &g_state.BlockDeviceHandle);
    if (blockStatus != STATUS_SUCCESS || !g_state.BlockDeviceHandle) {
        const int32_t failureStatus = blockStatus == STATUS_SUCCESS
            ? STATUS_DEVICE_DATA_ERROR : blockStatus;
        if (g_state.BlockDeviceHandle && services->CloseHandle(
                services->Context, g_state.BlockDeviceHandle) == STATUS_SUCCESS) {
            g_state.BlockDeviceHandle = 0;
        } else if (g_state.BlockDeviceHandle) {
            driverObject->DeviceExtension = &g_state;
            driverObject->DriverUnload = &unloadFailedDriver;
        }
        if (!driverObject->DriverUnload) preserveFailedCleanup(driverObject);
        return failureStatus;
    }

    const int32_t mountStatus = services->MountFatBlockDevice(
        services->Context, g_state.BlockDeviceHandle, &g_state.MountToken);
    if (mountStatus != STATUS_SUCCESS || !g_state.MountToken) {
        preserveFailedCleanup(driverObject);
        return mountStatus == STATUS_SUCCESS ? STATUS_DEVICE_DATA_ERROR : mountStatus;
    }

    g_state.Published = true;
    driverObject->DeviceExtension = &g_state;
    driverObject->DriverUnload = &unloadFailedDriver;
    services->Log(services->Context, "AHCI writable block device mounted by FsRtl");
    return STATUS_SUCCESS;
}
