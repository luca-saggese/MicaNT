// EHCI high-speed boot host. The canonical USB bus owns enumeration and
// class dispatch; this driver supplies root-port, control and bulk transport.
#include "micant/shared/boot_driver.hpp"
#include "micant/shared/usb_host_abi.hpp"
#include <cstring>

namespace {
using namespace micant::boot_driver;
using micant::shared::usb::HostServicesV1;
using micant::shared::usb::Trb;
constexpr int32_t kSuccess = 0;
constexpr int32_t kInvalidParameter = static_cast<int32_t>(0xC000000Du);
constexpr int32_t kNotSupported = static_cast<int32_t>(0xC00000BBu);
constexpr int32_t kDeviceNotReady = static_cast<int32_t>(0xC00000A3u);
constexpr int32_t kDeviceBusy = static_cast<int32_t>(0x80000011u);
constexpr uint64_t kDmaLimit = 0xffffffffull;
constexpr uint32_t kTerminate = 1;
// EHCI link-pointer type QH is 01b in bits 2:1; bit 0 is the terminate flag.
constexpr uint32_t kLinkQh = 1u << 1;
constexpr uint32_t kQtdActive = 1u << 7;
constexpr uint32_t kQtdHalted = 1u << 6;
constexpr uint32_t kQtdDataBufferError = 1u << 5;
constexpr uint32_t kQtdBabble = 1u << 4;
constexpr uint32_t kQtdTransactionError = 1u << 3;
constexpr uint32_t kQtdBytesMask = 0x7fff0000u;
constexpr uint32_t kPortConnect = 1u << 0;
constexpr uint32_t kPortConnectChange = 1u << 1;
constexpr uint32_t kPortEnable = 1u << 2;
constexpr uint32_t kPortEnableChange = 1u << 3;
constexpr uint32_t kPortOvercurrentChange = 1u << 5;
constexpr uint32_t kPortReset = 1u << 8;
constexpr uint32_t kPortPower = 1u << 12;
constexpr uint32_t kPortOwner = 1u << 13;
constexpr uint32_t kPortChangeMask = kPortConnectChange | kPortEnableChange |
                                     kPortOvercurrentChange;
constexpr uint32_t kRun = 1u;
constexpr uint32_t kReset = 1u << 1;
constexpr uint32_t kAsyncEnable = 1u << 5;
constexpr uint32_t kAsyncStatus = 1u << 15;
constexpr uint32_t kHalted = 1u << 12;
constexpr uint32_t kConfigured = 1u;
constexpr uint8_t kMaximumDevices = 8;
constexpr size_t kControlCapacity = 4096;
constexpr size_t kBulkCapacity = 64 * 1024;
constexpr size_t kQtdCount = 8;
constexpr uint8_t kPciHeaderTypeOffset = 0x0c;
constexpr uint8_t kPciClassOffset = 0x08;
constexpr uint8_t kPciBar0Offset = 0x10;

struct alignas(32) QueueHead {
    volatile uint32_t horizontalLink;
    volatile uint32_t endpointCharacteristics;
    volatile uint32_t endpointCapabilities;
    volatile uint32_t currentQtd;
    volatile uint32_t overlayNext;
    volatile uint32_t overlayAlternate;
    volatile uint32_t overlayToken;
    volatile uint32_t overlayBuffer[5];
    // EHCI 1.0 adds high dwords for each overlay buffer pointer. Even when
    // the controller is constrained to 32-bit DMA, these fields are part of
    // the hardware descriptor layout and determine the next-QH stride.
    volatile uint32_t overlayExtendedBuffer[5];
};
static_assert(sizeof(QueueHead) == 96);

struct alignas(32) QueueTransferDescriptor {
    volatile uint32_t next;
    volatile uint32_t alternate;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    // Keep the EHCI 1.0 extended buffer pointer fields in the descriptor;
    // high halves remain zero because allocations are below 4 GiB.
    volatile uint32_t extendedBuffer[5];
};
static_assert(sizeof(QueueTransferDescriptor) == 64);

struct DmaBlock {
    void* virtualAddress{};
    uint64_t deviceAddress{};
    uint64_t token{};
    size_t size{};
};
struct DeviceSlot {
    bool active{};
    uint8_t address{};
    uint16_t endpointZeroPacket{};
    uint8_t bulkIn{};
    uint8_t bulkOut{};
    uint16_t bulkInPacket{};
    uint16_t bulkOutPacket{};
};

const KernelServices* g_services{};
uint8_t g_pciBus{}, g_pciDevice{}, g_pciFunction{};
uint64_t g_claimToken{};
uint16_t g_originalPciCommand{};
bool g_pciCommandEnabled{};
volatile uint8_t* g_mmio{};
size_t g_mmioLength{};
size_t g_operationalOffset{};
uint8_t g_capabilityLength{};
uint8_t g_portCount{};
bool g_controllerPrepared{};
bool g_controllerRunning{};
bool g_controllerHealthy{};
uint8_t g_selectedAddress{};
uint32_t g_nextEventToken{0x1000};
DeviceSlot g_devices[kMaximumDevices + 1]{};
QueueHead* g_asyncHead{};
QueueHead* g_controlQh{};
QueueHead* g_bulkQh{};
QueueTransferDescriptor* g_qtds{};
DmaBlock g_asyncQhDma{}, g_controlQhDma{}, g_bulkQhDma{}, g_qtdDma{};
DmaBlock g_setupDma{}, g_controlDataDma{}, g_bulkDataDma{};
uint8_t g_controlData[kControlCapacity]{};
Trb g_events[16]{};
uint8_t g_eventHead{}, g_eventCount{};
uint32_t g_lastPortSc[256]{};
bool g_portScObserved[256]{};
HostServicesV1 g_host{};
uint64_t g_hostToken{};

void log(const char* message) noexcept {
    if (g_services && g_services->Log && message)
        g_services->Log(g_services->Context, message);
}

void logHex(const char* prefix, uint64_t value) noexcept {
    if (!prefix || !g_services || !g_services->Log) return;
    char message[112]{};
    size_t length{};
    while (prefix[length] && length + 18 < sizeof(message)) {
        message[length] = prefix[length];
        ++length;
    }
    static constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 60; shift >= 0 && length + 1 < sizeof(message); shift -= 4)
        message[length++] = digits[(value >> shift) & 0xf];
    message[length] = '\0';
    g_services->Log(g_services->Context, message);
}

void logPortSc(uint8_t port, uint32_t value) noexcept {
    if (!g_services || !g_services->Log) return;
    char message[72] = "ehci.sys: root port ";
    size_t length = sizeof("ehci.sys: root port ") - 1;
    if (port >= 10) message[length++] = static_cast<char>('0' + port / 10);
    message[length++] = static_cast<char>('0' + port % 10);
    constexpr char suffix[] = " PORTSC=0x";
    for (size_t index = 0; index + 1 < sizeof(suffix); ++index)
        message[length++] = suffix[index];
    static constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        message[length++] = digits[(value >> shift) & 0xf];
    message[length] = '\0';
    g_services->Log(g_services->Context, message);
}

bool readConfig(uint8_t bus, uint8_t device, uint8_t function,
                uint16_t offset, uint32_t& value) noexcept {
    return g_services && g_services->PciReadConfig32 &&
        g_services->PciReadConfig32(g_services->Context, bus, device, function,
                                    offset, &value) == kSuccess;
}

bool writeConfig(uint8_t bus, uint8_t device, uint8_t function,
                 uint16_t offset, uint32_t value) noexcept {
    return g_services && g_services->PciWriteConfig32 &&
        g_services->PciWriteConfig32(g_services->Context, bus, device, function,
                                     offset, value) == kSuccess;
}

bool findEhci(uint32_t firstBdf, uint8_t& busOut, uint8_t& deviceOut,
              uint8_t& functionOut, uint64_t& barAddress) noexcept {
    for (uint32_t bus = 0; bus <= UINT8_MAX; ++bus) {
        for (uint32_t device = 0; device < 32; ++device) {
            uint32_t id{};
            if (!readConfig(static_cast<uint8_t>(bus), static_cast<uint8_t>(device), 0, 0, id))
                continue;
            if (static_cast<uint16_t>(id) == UINT16_MAX) continue;
            uint32_t header{};
            if (!readConfig(static_cast<uint8_t>(bus), static_cast<uint8_t>(device), 0,
                            kPciHeaderTypeOffset, header)) continue;
            const uint8_t functions = (header & (1u << 23)) ? 8 : 1;
            for (uint8_t function = 0; function < functions; ++function) {
                const uint32_t bdf = (bus << 8) | (device << 3) | function;
                if (bdf < firstBdf) continue;
                if (function && !readConfig(static_cast<uint8_t>(bus),
                        static_cast<uint8_t>(device), function, 0, id)) continue;
                if (static_cast<uint16_t>(id) == UINT16_MAX) continue;
                uint32_t classCode{};
                if (!readConfig(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                        function, kPciClassOffset, classCode)) continue;
                if ((classCode >> 24) != 0x0c || ((classCode >> 16) & 0xff) != 0x03 ||
                    ((classCode >> 8) & 0xff) != 0x20) continue;
                uint32_t low{};
                if (!readConfig(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                                function, kPciBar0Offset, low) || (low & 1u)) continue;
                const uint8_t type = static_cast<uint8_t>((low >> 1) & 3u);
                if (type != 0 && type != 2) continue;
                barAddress = low & ~uint32_t{0x0f};
                if (type == 2) {
                    uint32_t high{};
                    if (!readConfig(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                                    function, kPciBar0Offset + 4, high)) continue;
                    barAddress |= uint64_t{high} << 32;
                }
                if (!barAddress) continue;
                busOut = static_cast<uint8_t>(bus);
                deviceOut = static_cast<uint8_t>(device);
                functionOut = function;
                return true;
            }
        }
    }
    return false;
}

uint32_t readRegister(size_t offset) noexcept {
    if (!g_mmio || offset > g_mmioLength || sizeof(uint32_t) > g_mmioLength - offset) return 0;
    auto* address = reinterpret_cast<volatile uint32_t*>(
        const_cast<uint8_t*>(g_mmio) + offset);
    return *address;
}

bool writeRegister(size_t offset, uint32_t value) noexcept {
    if (!g_mmio || offset > g_mmioLength || sizeof(uint32_t) > g_mmioLength - offset) return false;
    auto* address = reinterpret_cast<volatile uint32_t*>(
        const_cast<uint8_t*>(g_mmio) + offset);
    *address = value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return true;
}

uint32_t readOperational(size_t offset) noexcept {
    return readRegister(g_operationalOffset + offset);
}
bool writeOperational(size_t offset, uint32_t value) noexcept {
    return writeRegister(g_operationalOffset + offset, value);
}

bool waitRegister(size_t offset, uint32_t mask, uint32_t expected,
                  uint32_t milliseconds) noexcept {
    if (!g_services || !g_services->DelayMicroseconds) return false;
    const uint32_t iterations = milliseconds * 10u;
    for (uint32_t i = 0; i < iterations; ++i) {
        if ((readOperational(offset) & mask) == expected) return true;
        if (g_services->DelayMicroseconds(g_services->Context, 100) != kSuccess) return false;
    }
    return (readOperational(offset) & mask) == expected;
}

bool allocateDma(DmaBlock& block, size_t size, size_t alignment) noexcept {
    if (!g_services || !g_services->AllocateDmaBuffer || !size || block.token) return false;
    void* virtualAddress{};
    uint64_t deviceAddress{}, token{};
    const auto status = g_services->AllocateDmaBuffer(g_services->Context, g_claimToken,
        size, alignment, kDmaLimit, &virtualAddress, &deviceAddress, &token);
    if (status != kSuccess || !virtualAddress || !token || deviceAddress > kDmaLimit ||
        size - 1 > kDmaLimit - deviceAddress || (deviceAddress & (alignment - 1))) {
        if (token) (void)g_services->FreeDmaBuffer(g_services->Context, token);
        return false;
    }
    block = {virtualAddress, deviceAddress, token, size};
    std::memset(virtualAddress, 0, size);
    return true;
}

bool releaseDma(DmaBlock& block) noexcept {
    if (!block.token) return true;
    if (!g_services || !g_services->FreeDmaBuffer ||
        g_services->FreeDmaBuffer(g_services->Context, block.token) != kSuccess) return false;
    block = {};
    return true;
}

bool allocateSchedules() noexcept {
    if (!allocateDma(g_asyncQhDma, sizeof(QueueHead), 32) ||
        !allocateDma(g_controlQhDma, sizeof(QueueHead), 32) ||
        !allocateDma(g_bulkQhDma, sizeof(QueueHead), 32) ||
        !allocateDma(g_qtdDma, sizeof(QueueTransferDescriptor) * kQtdCount, 32) ||
        !allocateDma(g_setupDma, 4096, 4096) ||
        !allocateDma(g_controlDataDma, kControlCapacity, 4096) ||
        !allocateDma(g_bulkDataDma, kBulkCapacity, 4096)) return false;
    g_asyncHead = static_cast<QueueHead*>(g_asyncQhDma.virtualAddress);
    g_controlQh = static_cast<QueueHead*>(g_controlQhDma.virtualAddress);
    g_bulkQh = static_cast<QueueHead*>(g_bulkQhDma.virtualAddress);
    g_qtds = static_cast<QueueTransferDescriptor*>(g_qtdDma.virtualAddress);
    const uint32_t head = static_cast<uint32_t>(g_asyncQhDma.deviceAddress);
    const uint32_t control = static_cast<uint32_t>(g_controlQhDma.deviceAddress);
    const uint32_t bulk = static_cast<uint32_t>(g_bulkQhDma.deviceAddress);
    g_asyncHead->horizontalLink = control | kLinkQh;
    g_asyncHead->endpointCharacteristics = (1u << 15) | (2u << 12) | (64u << 16);
    g_asyncHead->endpointCapabilities = 0;
    g_asyncHead->overlayNext = kTerminate;
    g_asyncHead->overlayAlternate = kTerminate;
    g_controlQh->horizontalLink = bulk | kLinkQh;
    // Control endpoints use the QH-managed data toggle (DTC=0).
    g_controlQh->endpointCharacteristics = (2u << 12) | (64u << 16);
    g_controlQh->endpointCapabilities = 0;
    g_controlQh->overlayNext = kTerminate;
    g_controlQh->overlayAlternate = kTerminate;
    g_bulkQh->horizontalLink = head | kLinkQh;
    g_bulkQh->endpointCharacteristics = (2u << 12) | (512u << 16);
    g_bulkQh->endpointCapabilities = 0;
    g_bulkQh->overlayNext = kTerminate;
    g_bulkQh->overlayAlternate = kTerminate;
    (void)control;
    return true;
}

bool stopSchedule() noexcept {
    if (!g_controllerRunning) return true;
    uint32_t command = readOperational(0x00);
    command &= ~(kRun | kAsyncEnable);
    if (!writeOperational(0x00, command) ||
        !waitRegister(0x04, kAsyncStatus, 0, 100) ||
        !waitRegister(0x04, kHalted, kHalted, 100)) return false;
    g_controllerRunning = false;
    return true;
}

bool startSchedule() noexcept {
    if (!g_controllerPrepared || !g_asyncHead || !g_asyncQhDma.deviceAddress ||
        g_asyncQhDma.deviceAddress > UINT32_MAX) return false;
    // EHCI operational offsets are relative to CAPLENGTH: PERIODICLISTBASE
    // is 0x14 and ASYNCLISTADDR is 0x18. The previous 0x1c write targeted a
    // reserved register, leaving the firmware's old schedule active in QEMU.
    if (!writeOperational(0x14, 0) ||
        !writeOperational(0x18, static_cast<uint32_t>(g_asyncQhDma.deviceAddress)) ||
        !writeOperational(0x40, kConfigured) ||
        !writeOperational(0x08, 0x3fu) ||
        !writeOperational(0x04, 0x3fu)) {
        log("ehci.sys: schedule register setup failed");
        return false;
    }
    uint32_t command = readOperational(0x00);
    command &= ~(kReset | (3u << 2));
    command |= kRun | kAsyncEnable | (8u << 16);
    if (!writeOperational(0x00, command)) {
        log("ehci.sys: USBCMD schedule start write failed");
        return false;
    }
    if (!waitRegister(0x04, kHalted, 0, 100)) {
        logHex("ehci.sys: controller remained halted, USBSTS=0x", readOperational(0x04));
        return false;
    }
    if (!waitRegister(0x04, kAsyncStatus, kAsyncStatus, 100)) {
        logHex("ehci.sys: async schedule status did not assert, USBSTS=0x", readOperational(0x04));
        return false;
    }
    g_controllerRunning = true;
    logHex("ehci.sys: USBCMD=0x", readOperational(0x00));
    logHex("ehci.sys: USBSTS=0x", readOperational(0x04));
    logHex("ehci.sys: ASYNCLISTADDR=0x", readOperational(0x18));
    logHex("ehci.sys: async-head link=0x", g_asyncHead->horizontalLink);
    return true;
}

bool resetController() noexcept {
    if (!g_controllerPrepared || !g_services || !g_services->DelayMicroseconds) return false;
    uint32_t command = readOperational(0x00) & ~(kRun | kAsyncEnable);
    if (!writeOperational(0x00, command) || !waitRegister(0x04, kHalted, kHalted, 100)) return false;
    if (!writeOperational(0x00, command | kReset)) return false;
    bool resetDone{};
    for (uint32_t i = 0; i < 1000; ++i) {
        if (!(readOperational(0x00) & kReset)) { resetDone = true; break; }
        if (g_services->DelayMicroseconds(g_services->Context, 1000) != kSuccess) return false;
    }
    if (!resetDone) return false;
    g_controllerRunning = false;
    return true;
}

bool setupOwnership() noexcept {
    // HCCPARAMS is in the EHCI MMIO capability registers. Its EECP field
    // points into PCI configuration space at USBLEGSUP/USBLEGCTLSTS.
    const uint32_t hcc = readRegister(0x08);
    uint8_t capability = static_cast<uint8_t>((hcc >> 8) & 0xff);
    for (unsigned hops = 0; capability && hops < 48; ++hops) {
        if (capability < 0x40 || capability > 0xfc || (capability & 3u)) return false;
        uint32_t header{};
        if (!readConfig(g_pciBus, g_pciDevice, g_pciFunction, capability, header)) return false;
        const uint8_t id = static_cast<uint8_t>(header & 0xff);
        const uint8_t next = static_cast<uint8_t>((header >> 8) & 0xff);
        if (id == 1) {
            if (!writeConfig(g_pciBus, g_pciDevice, g_pciFunction, capability,
                             header | (1u << 24))) return false;
            for (unsigned wait = 0; wait < 1000; ++wait) {
                if (!readConfig(g_pciBus, g_pciDevice, g_pciFunction, capability, header)) return false;
                if (!(header & (1u << 16))) return true;
                if (!g_services->DelayMicroseconds ||
                    g_services->DelayMicroseconds(g_services->Context, 1000) != kSuccess)
                    return false;
            }
            return false;
        }
        if (next && next <= capability) return false;
        capability = next;
    }
    return true;
}

int32_t startHost(void* context);

bool mapAndPrepare() noexcept {
    uint8_t bus{}, device{}, function{};
    uint64_t barAddress{};
    uint32_t cursor{};
    bool candidateFound{};
    while (findEhci(cursor, bus, device, function, barAddress)) {
        candidateFound = true;
        bool attachedDevice{};
        log("ehci.sys: matching PCI EHCI function found");
        logHex("ehci.sys: candidate PCI BDF=0x",
               (uint32_t{bus} << 8) | (uint32_t{device} << 3) | function);
        cursor = (uint32_t{bus} << 8) | (uint32_t{device} << 3) | function;
        ++cursor;
        PciBarRequestV1 request{0, 0, 0, 0x1000};
        size_t count = 1;
        uint64_t claim{};
        PciBarResourceV1 resource{};
        const auto status = g_services->ClaimPciDevice(g_services->Context, 0,
            bus, device, function, &request, 1, &claim, &resource, &count);
        if (status != kSuccess || !claim || count != 1 || resource.BarIndex != 0 ||
            resource.PhysicalAddress != barAddress || resource.Length < 0x1000 ||
            resource.Length > 16u * 1024u * 1024u) {
            log("ehci.sys: PCI BAR0 claim or validated resource check failed");
            if (claim) (void)g_services->ReleasePciDevice(g_services->Context, claim);
            continue;
        }
        g_pciBus = bus; g_pciDevice = device; g_pciFunction = function;
        g_claimToken = claim; g_mmioLength = static_cast<size_t>(resource.Length);
        uint32_t pciCommand{};
        uint32_t capability{}, structural{}, ports{};
        uint16_t revision{};
        if (!readConfig(bus, device, function, 4, pciCommand) ||
            !writeConfig(bus, device, function, 4,
                         (pciCommand & 0xffffu & ~uint32_t{4}) | 2u)) {
            log("ehci.sys: PCI memory decode enable failed");
            goto rollback;
        }
        g_originalPciCommand = static_cast<uint16_t>(pciCommand);
        g_pciCommandEnabled = true;
        g_mmio = static_cast<volatile uint8_t*>(g_services->MapDeviceMemory(
            g_services->Context, resource.PhysicalAddress, g_mmioLength));
        capability = g_mmio ? readRegister(0) : 0;
        structural = g_mmio ? readRegister(4) : 0;
        ports = structural & 0xf;
        revision = static_cast<uint16_t>(capability >> 16);
        if (!g_mmio) {
            log("ehci.sys: BAR0 MMIO mapping failed");
            goto rollback;
        }
        g_capabilityLength = static_cast<uint8_t>(capability & 0xff);
        if (g_capabilityLength < 0x10 || revision < 0x0100 || !ports || ports > 15 ||
            g_capabilityLength > g_mmioLength ||
            g_capabilityLength + 0x44 + ports * sizeof(uint32_t) > g_mmioLength) {
            log("ehci.sys: EHCI capability length, revision, port count, or BAR size invalid");
            goto rollback;
        }
        g_operationalOffset = g_capabilityLength;
        g_portCount = static_cast<uint8_t>(ports);
        if (!setupOwnership()) {
            log("ehci.sys: firmware ownership handoff failed; controller left untouched");
            goto rollback;
        }
        g_controllerPrepared = true;
        if (!resetController()) {
            log("ehci.sys: controller halt/reset failed");
            goto rollback;
        }
        // Root-port connection state is independent of the async schedule.
        // Inspect it while halted so an unused EHCI function is never started
        // merely to discover that its ports are empty.
        for (uint8_t port = 1; port <= g_portCount; ++port) {
            const uint32_t portStatus = readOperational(0x44u +
                static_cast<uint32_t>(port - 1) * sizeof(uint32_t));
            logPortSc(port, portStatus);
            attachedDevice |= (portStatus & kPortConnect) != 0;
        }
        if (!attachedDevice) {
            log("ehci.sys: no connected device on this controller; trying next EHCI function");
            goto rollback;
        }
        if (!allocateSchedules()) {
            log("ehci.sys: low-address DMA schedule allocation failed");
            goto rollback;
        }
        if (!writeConfig(bus, device, function, 4,
                         (pciCommand & 0xffffu) | 6u)) {
            log("ehci.sys: PCI bus-master enable rejected after DMA allocation");
            goto rollback;
        }
        g_controllerHealthy = true;
        // The candidate was reset above. Start its schedule directly instead
        // of resetting a second time after DMA setup and device detection.
        if (!startSchedule()) {
            log("ehci.sys: candidate start status=0xC00000A3");
            logHex("ehci.sys: candidate USBCMD=0x", readOperational(0x00));
            logHex("ehci.sys: candidate USBSTS=0x", readOperational(0x04));
            log("ehci.sys: candidate controller could not start its schedules");
            goto rollback;
        }
        return true;
rollback:
        if (g_controllerRunning) (void)stopSchedule();
        if (g_controllerPrepared && !g_controllerRunning) {
            uint32_t command = readOperational(0x00);
            (void)writeOperational(0x00, command & ~(kRun | kAsyncEnable));
            (void)waitRegister(0x04, kHalted, kHalted, 100);
        }
        // The kernel DMA provider refuses to release mappings while PCI
        // memory decode or bus mastering is enabled. Quiesce the function
        // before releasing schedules so a rejected first candidate cannot
        // strand buffers needed by the next EHCI function.
        if (g_claimToken && g_pciCommandEnabled) {
            if (!writeConfig(g_pciBus, g_pciDevice, g_pciFunction, 4,
                             g_originalPciCommand & ~uint16_t{7})) {
                log("ehci.sys: PCI function could not be disabled during candidate rollback");
                return false;
            }
            g_pciCommandEnabled = false;
        }
        const bool dmaReleased = releaseDma(g_bulkDataDma) &&
            releaseDma(g_controlDataDma) && releaseDma(g_setupDma) &&
            releaseDma(g_qtdDma) && releaseDma(g_bulkQhDma) &&
            releaseDma(g_controlQhDma) && releaseDma(g_asyncQhDma);
        if (!dmaReleased) {
            log("ehci.sys: DMA buffers could not be released during candidate rollback");
            return false;
        }
        g_asyncHead = g_controlQh = g_bulkQh = nullptr; g_qtds = nullptr;
        g_controllerPrepared = false; g_controllerHealthy = false; g_controllerRunning = false;
        if (g_mmio) {
            (void)g_services->ReleaseDeviceMemory(g_services->Context,
                                                   const_cast<uint8_t*>(g_mmio), g_mmioLength);
            g_mmio = nullptr;
        }
        if (g_claimToken) {
            (void)g_services->ReleasePciDevice(g_services->Context, g_claimToken);
            g_claimToken = 0;
        }
    }
    if (!candidateFound) log("ehci.sys: PCI class 0C/03/20 controller not found");
    return false;
}

uint32_t portOffset(uint8_t port) noexcept {
    return 0x44u + static_cast<uint32_t>(port - 1) * 4u;
}

uint32_t endpointCharacteristics(uint8_t address, uint8_t endpoint,
                                 uint16_t packet, bool dataToggleControl) noexcept {
    return (address & 0x7fu) | (uint32_t{endpoint & 0x0fu} << 8) |
           (2u << 12) | (dataToggleControl ? 1u << 14 : 0) |
           (uint32_t{packet & 0x7ffu} << 16);
}

void fillQtdBuffers(QueueTransferDescriptor& qtd, uint64_t address) noexcept {
    for (unsigned index = 0; index < 5; ++index) {
        qtd.buffer[index] = static_cast<uint32_t>(address);
        address = (address & ~uint64_t{0xfff}) + 0x1000;
    }
}

uint32_t qtdToken(size_t length, uint8_t pid, bool toggle, bool interrupt) noexcept {
    return kQtdActive | (3u << 10) | (uint32_t{pid & 3u} << 8) |
           (static_cast<uint32_t>(length) << 16) |
           (toggle ? 1u << 31 : 0) | (interrupt ? 1u << 15 : 0);
}

bool quiesceAsyncSchedule() noexcept {
    uint32_t command = readOperational(0x00);
    if (!(command & kAsyncEnable)) return true;
    if (!writeOperational(0x00, command & ~kAsyncEnable) ||
        !waitRegister(0x04, kAsyncStatus, 0, 100)) return false;
    command = readOperational(0x00);
    if (!writeOperational(0x00, command | kAsyncEnable) ||
        !waitRegister(0x04, kAsyncStatus, kAsyncStatus, 100)) return false;
    return true;
}

bool runQtds(QueueHead& qh, uint64_t firstQtd, uint32_t qtds,
             uint32_t timeoutMilliseconds) noexcept {
    if (!g_controllerHealthy || !g_controllerRunning || !qtds || qtds >= kQtdCount) return false;
    qh.overlayNext = static_cast<uint32_t>(firstQtd);
    qh.overlayAlternate = kTerminate;
    qh.currentQtd = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const uint32_t maxPolls = timeoutMilliseconds > 10000 ? 100000 : timeoutMilliseconds * 100;
    bool complete{};
    bool transferFault{};
    for (uint32_t poll = 0; poll < maxPolls; ++poll) {
        complete = true;
        for (uint32_t index = 0; index < qtds; ++index) {
            const uint32_t token = g_qtds[index].token;
            if (token & kQtdActive) { complete = false; break; }
            if (token & (kQtdHalted | kQtdDataBufferError | kQtdBabble | kQtdTransactionError)) {
                logHex("ehci.sys: qTD error token=0x", token);
                logHex("ehci.sys: USBSTS=0x", readOperational(0x04));
                complete = false;
                transferFault = true;
                break;
            }
        }
        if (complete) break;
        if (transferFault) break;
        if (g_services->DelayMicroseconds(g_services->Context, 10) != kSuccess) break;
    }
    if (complete) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        return true;
    }
    if (qh.overlayNext != kTerminate) {
        logHex("ehci.sys: qTD timeout token=0x", g_qtds[0].token);
        logHex("ehci.sys: qTD current=0x", qh.currentQtd);
        logHex("ehci.sys: USBSTS=0x", readOperational(0x04));
    }
    qh.overlayNext = kTerminate;
    if (!quiesceAsyncSchedule()) g_controllerHealthy = false;
    for (uint32_t index = 0; index < qtds; ++index) {
        g_qtds[index].token &= ~kQtdActive;
        g_qtds[index].next = kTerminate;
        g_qtds[index].alternate = kTerminate;
    }
    return false;
}

bool runControl(uint8_t address, uint16_t packet, uint64_t setupPacket,
                size_t length, bool input, size_t& actual) noexcept {
    actual = 0;
    if (!g_controllerHealthy || !g_controlQh || !g_qtds || length > kControlCapacity ||
        (length && !g_controlDataDma.virtualAddress) || packet != 64) return false;
    auto* setupBytes = static_cast<uint8_t*>(g_setupDma.virtualAddress);
    std::memcpy(setupBytes, &setupPacket, sizeof(setupPacket));
    auto* data = static_cast<uint8_t*>(g_controlDataDma.virtualAddress);
    if (length && !input) std::memcpy(data, g_controlData, length);
    std::memset(g_qtds, 0, sizeof(QueueTransferDescriptor) * 3);
    const uint64_t setupPhysical = g_setupDma.deviceAddress;
    const uint64_t dataPhysical = g_controlDataDma.deviceAddress;
    const uint64_t qtdPhysical = g_qtdDma.deviceAddress;
    const uint32_t dataQtdCount = length ? 1 : 0;
    const uint32_t statusIndex = 1 + dataQtdCount;
    const uint32_t qtdCount = statusIndex + 1;
    g_controlQh->endpointCharacteristics = endpointCharacteristics(address, 0, packet, false);
    g_controlQh->overlayToken = 0;
    g_qtds[0].next = static_cast<uint32_t>(qtdPhysical + sizeof(QueueTransferDescriptor));
    g_qtds[0].alternate = kTerminate;
    g_qtds[0].token = qtdToken(8, 2, false, false);
    fillQtdBuffers(g_qtds[0], setupPhysical);
    if (length) {
        auto& dataQtd = g_qtds[1];
        dataQtd.next = static_cast<uint32_t>(qtdPhysical + statusIndex * sizeof(QueueTransferDescriptor));
        dataQtd.alternate = dataQtd.next;
        dataQtd.token = qtdToken(length, input ? 1 : 0, true, false);
        fillQtdBuffers(dataQtd, dataPhysical);
    }
    auto& statusQtd = g_qtds[statusIndex];
    statusQtd.next = kTerminate;
    statusQtd.alternate = kTerminate;
    statusQtd.token = qtdToken(0, input ? 0 : 1, true, true);
    fillQtdBuffers(statusQtd, dataPhysical);
    if (!runQtds(*g_controlQh, qtdPhysical, qtdCount, 1000)) return false;
    if (length) {
        const uint32_t token = g_qtds[1].token;
        const size_t remaining = (token & kQtdBytesMask) >> 16;
        if (remaining > length) return false;
        actual = length - remaining;
        if (input) std::memcpy(g_controlData, data, actual);
    }
    return true;
}

bool enqueueEvent(uint8_t slot, uint64_t address, uint8_t dci = 1) noexcept {
    if (g_eventCount == 16 || !slot) return false;
    const uint8_t index = static_cast<uint8_t>((g_eventHead + g_eventCount) % 16);
    g_events[index] = {address, 1u << 24,
        (32u << 10) | (uint32_t{dci} << 16) | (uint32_t{slot} << 24)};
    ++g_eventCount;
    return true;
}

uint64_t nextEventAddress() noexcept {
    const uint64_t result = g_nextEventToken;
    g_nextEventToken += 16;
    if (g_nextEventToken < 0x1000 || g_nextEventToken > 0xfffffff0u) g_nextEventToken = 0x1000;
    return result;
}

int32_t startHost(void*) {
    if (!g_controllerPrepared || !g_controllerHealthy) return kDeviceNotReady;
    if (g_controllerRunning) return kSuccess;
    std::memset(g_devices, 0, sizeof(g_devices));
    g_selectedAddress = 0;
    g_eventHead = g_eventCount = 0;
    if (!resetController()) {
        log("ehci.sys: controller reset failed while starting host");
        return kDeviceNotReady;
    }
    if (!startSchedule()) {
        log("ehci.sys: async schedule failed to become active");
        g_controllerHealthy = false;
        return kDeviceNotReady;
    }
    log("ehci.sys: EHCI async schedule active; USB 2.0 high-speed root ports ready");
    return kSuccess;
}

int32_t stopHost(void*) {
    if (!g_controllerPrepared) return kSuccess;
    if (!stopSchedule()) return kDeviceBusy;
    std::memset(g_devices, 0, sizeof(g_devices));
    g_selectedAddress = 0;
    return kSuccess;
}

int32_t restartHost(void* context) {
    const auto stopped = stopHost(context);
    return stopped == kSuccess ? startHost(context) : stopped;
}

uint8_t portCount(void*) { return g_portCount; }

int32_t readPort(void*, uint8_t port, uint32_t* state) {
    if (!state || !g_controllerRunning || !port || port > g_portCount) return kInvalidParameter;
    const uint32_t value = readOperational(portOffset(port));
    if (!g_portScObserved[port] || g_lastPortSc[port] != value) {
        g_portScObserved[port] = true;
        g_lastPortSc[port] = value;
        logPortSc(port, value);
    }
    *state = ((value & kPortConnect) ? 1u : 0u) |
             ((value & kPortConnectChange) ? 1u << 17 : 0u);
    return kSuccess;
}

int32_t resetPort(void*, uint8_t port, uint8_t* speed) {
    if (!speed || !g_controllerRunning || !port || port > g_portCount) return kInvalidParameter;
    const uint32_t offset = portOffset(port);
    uint32_t value = readOperational(offset);
    if (!(value & kPortConnect)) return kDeviceNotReady;
    if (!writeOperational(offset, (value & ~kPortChangeMask) | kPortPower | kPortReset) ||
        g_services->DelayMilliseconds(g_services->Context, 55) != kSuccess) return kDeviceNotReady;
    value = readOperational(offset);
    if (!writeOperational(offset, (value & ~kPortChangeMask & ~kPortReset) | kPortPower) ||
        g_services->DelayMilliseconds(g_services->Context, 10) != kSuccess) return kDeviceNotReady;
    value = readOperational(offset);
    if (!(value & kPortConnect) || !(value & kPortEnable)) {
        if ((readRegister(4) >> 12) & 0xf) {
            (void)writeOperational(offset, (value & ~kPortChangeMask) | kPortOwner | kPortPower);
            log("ehci.sys: low/full-speed device assigned to EHCI companion; no companion driver is registered");
        }
        return kNotSupported;
    }
    logHex("ehci.sys: reset PORTSC=0x", value);
    *speed = 3; // USB high-speed; full/low-speed ports belong to a companion HCI.
    (void)writeOperational(offset, value & ~kPortChangeMask);
    return kSuccess;
}

int32_t acknowledgeRootChange(void*, uint8_t port) {
    if (!g_controllerRunning || !port || port > g_portCount) return kInvalidParameter;
    const uint32_t value = readOperational(portOffset(port));
    const uint32_t changes = value & kPortChangeMask;
    return changes ? (writeOperational(portOffset(port), value & ~kPortChangeMask) ? kSuccess : kDeviceNotReady)
                   : kSuccess;
}

int32_t selectDevice(void*, uint8_t slot) {
    if (!slot || slot > kMaximumDevices || !g_devices[slot].active || !g_controllerRunning)
        return kDeviceNotReady;
    g_selectedAddress = slot;
    return kSuccess;
}

int32_t addressDevice(void*, uint8_t port, uint8_t speed, uint8_t* slotOut) {
    if (!slotOut || !g_controllerRunning || speed != 3 || !port || port > g_portCount)
        return kNotSupported;
    uint8_t slot{};
    for (uint8_t candidate = 1; candidate <= kMaximumDevices; ++candidate)
        if (!g_devices[candidate].active) { slot = candidate; break; }
    if (!slot) return kDeviceBusy;
    uint8_t prefix[8]{};
    logHex("ehci.sys: control QH link=0x", g_controlQh->horizontalLink);
    logHex("ehci.sys: control QH endpoint=0x", g_controlQh->endpointCharacteristics);
    logHex("ehci.sys: control QTD physical=0x", g_qtdDma.deviceAddress);
    const uint64_t getDescriptor = uint64_t{0x80} | (uint64_t{6} << 8) |
        (uint64_t{0x0100} << 16) | (uint64_t{8} << 48);
    size_t actual{};
    if (!runControl(0, 64, getDescriptor, sizeof(prefix), true, actual) ||
        actual != sizeof(prefix)) {
        log("ehci.sys: address-zero device descriptor transfer failed");
        return kDeviceNotReady;
    }
    // runControl copies successful IN data from the DMA buffer into the
    // controller's shared control buffer; validate the bytes actually read.
    std::memcpy(prefix, g_controlData, sizeof(prefix));
    if (prefix[0] < 8 || prefix[1] != 1 || prefix[7] != 64) {
        log("ehci.sys: address-zero device descriptor is invalid for high-speed USB");
        return kDeviceNotReady;
    }
    const uint64_t setAddress = uint64_t{5} << 8 | (uint64_t{slot} << 16);
    if (!runControl(0, 64, setAddress, 0, false, actual) || actual != 0 ||
        g_services->DelayMilliseconds(g_services->Context, 2) != kSuccess) {
        log("ehci.sys: address-zero SET_ADDRESS transfer failed");
        return kDeviceNotReady;
    }
    g_devices[slot] = {true, slot, 64, 0, 0, 0, 0};
    *slotOut = slot;
    g_selectedAddress = slot;
    log("ehci.sys: high-speed USB device addressed through EHCI control schedule");
    return kSuccess;
}

int32_t forgetDevice(void*, uint8_t slot) {
    if (!slot || slot > kMaximumDevices) return kInvalidParameter;
    if (!g_devices[slot].active) return kSuccess;
    if (g_selectedAddress == slot) g_selectedAddress = 0;
    g_devices[slot] = {};
    g_bulkQh->endpointCharacteristics = (2u << 12) | (512u << 16);
    g_bulkQh->overlayNext = kTerminate;
    g_bulkQh->overlayToken = 0;
    return kSuccess;
}

int32_t setBulkEndpoints(void*, uint8_t slot, uint8_t input, uint16_t inPacket,
                         uint8_t output, uint16_t outPacket, uint8_t speed) {
    if (!slot || slot > kMaximumDevices || !g_devices[slot].active || speed != 3 ||
        !(input & 0x80) || !(input & 15) || (output & 0xf0) || !(output & 15) ||
        !inPacket || inPacket > 512 || !outPacket || outPacket > 512) return kInvalidParameter;
    auto& device = g_devices[slot];
    device.bulkIn = input; device.bulkOut = output;
    device.bulkInPacket = inPacket; device.bulkOutPacket = outPacket;
    g_bulkQh->endpointCharacteristics = endpointCharacteristics(slot, output & 0x0f,
                                                                 outPacket, false);
    g_bulkQh->overlayToken = 0;
    g_bulkQh->overlayNext = kTerminate;
    g_bulkQh->overlayAlternate = kTerminate;
    g_selectedAddress = slot;
    return kSuccess;
}

int32_t submitControl(void*, uint64_t setupPacket, uint16_t length,
                      bool input, uint64_t* eventAddress) {
    if (!eventAddress || !g_selectedAddress || g_selectedAddress > kMaximumDevices ||
        !g_devices[g_selectedAddress].active || length > kControlCapacity ||
        (((setupPacket & 0x80u) != 0) != input)) return kInvalidParameter;
    size_t actual{};
    const auto packet = g_devices[g_selectedAddress].endpointZeroPacket;
    if (!runControl(g_selectedAddress, packet, setupPacket, length, input, actual)) {
        log("ehci.sys: control qTD schedule failed");
        return kDeviceNotReady;
    }
    if (actual != length) {
        log("ehci.sys: control transfer returned a short response");
        return kDeviceNotReady;
    }
    *eventAddress = nextEventAddress();
    return enqueueEvent(g_selectedAddress, *eventAddress) ? kSuccess : kDeviceBusy;
}

const uint8_t* controlBuffer(void*) { return g_controlData; }

int32_t pollEvent(void*, Trb* event, bool* available) {
    if (!event || !available || !g_controllerRunning || !g_controllerHealthy)
        return kDeviceNotReady;
    *available = g_eventCount != 0;
    if (!*available) return kSuccess;
    *event = g_events[g_eventHead];
    g_eventHead = static_cast<uint8_t>((g_eventHead + 1) % 16);
    --g_eventCount;
    return kSuccess;
}

int32_t bulkTransfer(void*, uint8_t endpoint, void* buffer, size_t length,
                     size_t* actual, uint32_t timeoutMilliseconds) {
    if (!actual) return kInvalidParameter;
    *actual = 0;
    if (!g_controllerRunning || !g_controllerHealthy || !g_selectedAddress ||
        g_selectedAddress > kMaximumDevices || !g_devices[g_selectedAddress].active ||
        !buffer || !length || length > kBulkCapacity) return kInvalidParameter;
    auto& device = g_devices[g_selectedAddress];
    const bool input = (endpoint & 0x80u) != 0;
    if ((input && endpoint != device.bulkIn) || (!input && endpoint != device.bulkOut))
        return kInvalidParameter;
    auto* dma = static_cast<uint8_t*>(g_bulkDataDma.virtualAddress);
    const uint64_t dmaPhysical = g_bulkDataDma.deviceAddress;
    const uint16_t packet = input ? device.bulkInPacket : device.bulkOutPacket;
    const uint32_t timeout = timeoutMilliseconds ? timeoutMilliseconds : 1000;
    size_t offset{};
    while (offset < length) {
        const size_t chunk = (length - offset) > 16384 ? 16384 : length - offset;
        if (!input) std::memcpy(dma, static_cast<const uint8_t*>(buffer) + offset, chunk);
        auto& qtd = g_qtds[0];
        qtd.next = kTerminate; qtd.alternate = kTerminate;
        qtd.token = qtdToken(chunk, input ? 1 : 0, false, true);
        fillQtdBuffers(qtd, dmaPhysical);
        g_bulkQh->endpointCharacteristics = endpointCharacteristics(g_selectedAddress,
            endpoint & 0x0f, packet, false);
        g_bulkQh->overlayToken &= 1u << 31;
        if (!runQtds(*g_bulkQh, g_qtdDma.deviceAddress, 1, timeout)) return kDeviceNotReady;
        const size_t remaining = (qtd.token & kQtdBytesMask) >> 16;
        if (remaining > chunk) return kDeviceNotReady;
        const size_t transferred = chunk - remaining;
        if (input && transferred)
            std::memcpy(static_cast<uint8_t*>(buffer) + offset, dma, transferred);
        offset += transferred;
        *actual = offset;
        if (transferred < chunk) break;
    }
    return kSuccess;
}

int32_t bulkReset(void*, uint8_t interfaceNumber, uint8_t input, uint8_t output) {
    if (!g_selectedAddress || !g_devices[g_selectedAddress].active) return kDeviceNotReady;
    const uint64_t resetRequest = uint64_t{0x21} | (uint64_t{0xff} << 8) |
        (uint64_t{interfaceNumber} << 32);
    size_t actual{};
    if (!runControl(g_selectedAddress, g_devices[g_selectedAddress].endpointZeroPacket,
                    resetRequest, 0, false, actual)) return kDeviceNotReady;
    const auto clearHalt = [&](uint8_t endpoint) noexcept {
        const uint64_t clear = uint64_t{0x02} | (uint64_t{1} << 8) |
            (uint64_t{endpoint} << 32);
        return runControl(g_selectedAddress, g_devices[g_selectedAddress].endpointZeroPacket,
                          clear, 0, false, actual);
    };
    if (!clearHalt(input) || !clearHalt(output)) return kDeviceNotReady;
    g_bulkQh->overlayToken = 0;
    return kSuccess;
}

void unloadEhci(DriverObject* driver) noexcept;
void logAndSetDriverUnload(DriverObject* driver) noexcept {
    if (driver) driver->DriverUnload = unloadEhci;
}

void unloadEhci(DriverObject* driver) noexcept {
    if (g_hostToken && g_services && g_services->UnregisterUsbHost) {
        if (g_services->UnregisterUsbHost(g_services->Context, g_hostToken) != kSuccess) return;
        g_hostToken = 0;
    } else if (g_controllerPrepared && !stopHost(nullptr)) return;
    if (g_pciCommandEnabled && g_services) {
        if (!writeConfig(g_pciBus, g_pciDevice, g_pciFunction, 4,
                         g_originalPciCommand & ~uint16_t{7})) return;
        g_pciCommandEnabled = false;
    }
    if (!releaseDma(g_bulkDataDma) || !releaseDma(g_controlDataDma) ||
        !releaseDma(g_setupDma) || !releaseDma(g_qtdDma) ||
        !releaseDma(g_bulkQhDma) || !releaseDma(g_controlQhDma) ||
        !releaseDma(g_asyncQhDma)) return;
    g_asyncHead = g_controlQh = g_bulkQh = nullptr; g_qtds = nullptr;
    if (g_mmio && g_services && g_services->ReleaseDeviceMemory(
            g_services->Context, const_cast<uint8_t*>(g_mmio), g_mmioLength) != true) return;
    g_mmio = nullptr; g_mmioLength = 0; g_controllerPrepared = false;
    if (g_claimToken && g_services && g_services->ReleasePciDevice(
            g_services->Context, g_claimToken) != kSuccess) return;
    g_claimToken = 0; g_services = nullptr;
    if (driver) driver->DriverUnload = nullptr;
}

} // namespace

extern "C" int32_t DriverEntry(micant::boot_driver::DriverObject* driver,
                                const micant::boot_driver::UnicodeString*) {
    if (!driver || driver->Size < sizeof(*driver) ||
        driver->Version != DRIVER_ABI_VERSION || !driver->HardwareResources)
        return kInvalidParameter;
    const auto& services = *static_cast<const KernelServices*>(driver->HardwareResources);
    if (services.Size < sizeof(KernelServices) || services.Version != KERNEL_SERVICES_VERSION ||
        !services.PciReadConfig32 || !services.PciWriteConfig32 ||
        !services.ClaimPciDevice || !services.ReleasePciDevice ||
        !services.MapDeviceMemory || !services.ReleaseDeviceMemory ||
        services.DmaVersion != DMA_SERVICES_VERSION || !services.AllocateDmaBuffer ||
        !services.FreeDmaBuffer || !services.DelayMicroseconds || !services.DelayMilliseconds ||
        services.UsbHostVersion != micant::shared::usb::HostServicesVersion ||
        !services.RegisterUsbHost || !services.UnregisterUsbHost)
        return kNotSupported;
    if (g_services || g_claimToken || g_hostToken) return kDeviceBusy;
    g_services = &services;
    if (!mapAndPrepare()) {
        log("ehci.sys: no usable PCI EHCI controller or safe DMA schedule resources");
        g_services = nullptr;
        return kNotSupported;
    }
    logAndSetDriverUnload(driver);
    g_host = {};
    g_host.Size = sizeof(g_host); g_host.Version = micant::shared::usb::HostServicesVersion;
    g_host.Context = nullptr; g_host.Start = startHost; g_host.Stop = stopHost;
    g_host.Restart = restartHost; g_host.PortCount = portCount; g_host.ReadPort = readPort;
    g_host.ResetPort = resetPort; g_host.SelectDevice = selectDevice;
    g_host.ForgetDevice = forgetDevice; g_host.SubmitControl = submitControl;
    g_host.ControlBuffer = controlBuffer; g_host.PollEvent = pollEvent;
    g_host.BulkTransfer = bulkTransfer; g_host.BulkResetRecovery = bulkReset;
    g_host.AcknowledgeRootConnectionChange = acknowledgeRootChange;
    g_host.Log = [](void*, const char* message) { log(message); };
    g_host.AddressDevice = addressDevice;
    g_host.ConfigureBulkEndpoints = setBulkEndpoints;
    const auto status = services.RegisterUsbHost(services.Context, &g_host, &g_hostToken);
    if (status != kSuccess || !g_hostToken) {
        unloadEhci(driver);
        return status == kSuccess ? kDeviceNotReady : status;
    }
    log("ehci.sys: PCI EHCI controller claimed and registered with canonical USB bus");
    return kSuccess;
}
