#include "micant/km/drivers/audio/hdaudio_baremetal.hpp"

#if defined(MICANT_FREESTANDING)

#include "micant/km/executive/arbiter_baremetal.hpp"
#include "micant/km/executive/dma_baremetal.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/shared/boot_driver.hpp"
#include "micant/shared/ntstatus.hpp"

namespace micant::km::audio::hda {
namespace {

constexpr uint32_t kPciMultifunction = 1u << 23;
constexpr uint16_t kPciMemoryEnable = 1u << 1;
constexpr uint16_t kPciBusMasterEnable = 1u << 2;
constexpr uint16_t kClassMultimedia = 0x04;
constexpr uint16_t kSubclassHda = 0x03;
constexpr size_t kHdaRegisterWindow = 0x100;
constexpr uint32_t kGctl = 0x08;
constexpr uint32_t kGctlControllerReset = 1;
constexpr uint32_t kGcap = 0x00;
constexpr uint32_t kVersion = 0x02;
constexpr uint32_t kStateStatus = 0x0e;
constexpr uint32_t kCorbBaseLow = 0x40;
constexpr uint32_t kCorbBaseHigh = 0x44;
constexpr uint32_t kCorbWritePointer = 0x48;
constexpr uint32_t kCorbReadPointer = 0x4a;
constexpr uint32_t kCorbControl = 0x4c;
constexpr uint32_t kCorbSize = 0x4e;
constexpr uint32_t kRirbBaseLow = 0x50;
constexpr uint32_t kRirbBaseHigh = 0x54;
constexpr uint32_t kRirbWritePointer = 0x58;
constexpr uint32_t kRirbControl = 0x5c;
constexpr uint32_t kRirbSize = 0x5e;
constexpr uint32_t kRirbStatus = 0x5d;
constexpr uint8_t kRingRun = 1u << 1;
constexpr uint8_t kRirbResponseInterruptEnable = 1u << 0;
constexpr uint8_t kRirbResponseInterrupt = 1u << 0;
constexpr size_t kDmaPageBytes = 4096;

struct Controller {
    uint16_t bus{}, device{}, function{};
    uint64_t claimToken{}, mappingToken{};
    uint16_t originalCommand{};
    volatile uint8_t* registers{};
    size_t registerLength{};
    uint32_t gcap{};
    bool claimed{}, commandChanged{};
    bool ready{};
    void* corb{};
    uint64_t corbAddress{}, corbDmaToken{};
    uint32_t corbEntries{}, corbWrite{};
    void* rirb{};
    uint64_t rirbAddress{}, rirbDmaToken{};
    uint32_t rirbEntries{};
    bool ringsRunning{}, busMasterEnabled{}, codecDiscovered{};
};

Controller g_controller{};
int g_controllerOwner{};
#if defined(MICANT_BOOTSTRAP_AUDIO_CONTROLLER_TEST)
bool g_rollbackFailureInjected{};
#endif

bool readConfig(uint16_t bus, uint16_t device, uint16_t function,
                uint16_t offset, uint32_t& value) noexcept {
    return hal::amd64::readPciConfig32(bus, device, function, offset, value) ==
        hal::amd64::PciConfigAccessResult::Success;
}

bool writeConfig(uint16_t bus, uint16_t device, uint16_t function,
                 uint16_t offset, uint32_t value) noexcept {
    return hal::amd64::writePciConfig32(bus, device, function, offset, value) ==
        hal::amd64::PciConfigAccessResult::Success;
}

void logHex(Log log, uint64_t value) noexcept {
    if (!log) return;
    constexpr char digits[] = "0123456789ABCDEF";
    char text[19]{'0', 'x'};
    for (unsigned i = 0; i < 16; ++i)
        text[i + 2] = digits[(value >> ((15 - i) * 4)) & 0xfu];
    log(text);
}

NtStatus useMeasuredBar(uint16_t bus, uint16_t device, uint16_t function,
                        uint8_t index, boot_driver::PciBarResourceV1& out) noexcept {
    // PCI inventory is immutable after boot enumeration. Do not probe by
    // writing all-ones to a BAR here: firmware may have left decode enabled.
    for (size_t i = 0; i < arb::firmwareBarCount; ++i) {
        const auto& bar = arb::firmwareBars[i];
        if (bar.bus != bus || bar.device != device || bar.function != function ||
            bar.index != index) continue;
        if (!bar.length || bar.ioSpace || bar.address > UINT64_MAX - (bar.length - 1))
            return NtStatus::DeviceNotReady;
        out = {};
        out.BarIndex = index;
        out.PhysicalAddress = bar.address;
        out.Length = bar.length;
        return NtStatus::Success;
    }
    return NtStatus::DeviceNotReady;
}

bool dmaStopped(uint16_t bus, uint16_t device, uint16_t function) noexcept {
    uint32_t command{};
    return readConfig(bus, device, function, 4, command) && !(command & (1u << 2));
}

void rollback(Controller& controller, Log log) noexcept {
    if (controller.registers) {
        const bool unmapped = hal::amd64::releaseBootstrapDeviceMemory(
            const_cast<uint8_t*>(controller.registers), controller.registerLength);
        if (!unmapped && log) log("[MicaNT HDA] Rollback could not unmap BAR MMIO\r\n");
        controller.registers = nullptr;
    }
    if (controller.mappingToken) {
        const bool released = arb::releaseMapping(&g_controllerOwner, controller.mappingToken);
        if (!released && log) log("[MicaNT HDA] Rollback could not release mapping reference\r\n");
        controller.mappingToken = 0;
    }
    if (controller.commandChanged) {
        if (!writeConfig(controller.bus, controller.device, controller.function, 4,
                         controller.originalCommand & ~kPciBusMasterEnable) && log)
            log("[MicaNT HDA] Rollback could not restore PCI command register\r\n");
        controller.commandChanged = false;
    }
    if (controller.claimed) {
        const auto status = arb::release(&g_controllerOwner, controller.claimToken, dmaStopped);
        if (status != NtStatus::Success) {
            if (log) log("[MicaNT HDA] Rollback could not release PCI claim\r\n");
        } else {
            controller.claimed = false;
        }
    }
}

bool findController(uint16_t& busOut, uint16_t& deviceOut, uint16_t& functionOut) noexcept {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint16_t device = 0; device < 32; ++device) {
            uint32_t id{}, header{};
            if (!readConfig(bus, device, 0, 0, id)) return false;
            if ((id & 0xffffu) == 0xffffu) continue;
            if (!readConfig(bus, device, 0, 0x0c, header)) return false;
            const uint16_t functionCount = (header & kPciMultifunction) ? 8 : 1;
            for (uint16_t function = 0; function < functionCount; ++function) {
                uint32_t functionId{}, classRevision{};
                if (!readConfig(bus, device, function, 0, functionId) ||
                    !readConfig(bus, device, function, 8, classRevision)) return false;
                if ((functionId & 0xffffu) == 0xffffu) continue;
                const uint16_t baseClass = static_cast<uint16_t>(classRevision >> 24);
                const uint16_t subclass = static_cast<uint16_t>((classRevision >> 16) & 0xffu);
                if (baseClass != kClassMultimedia || subclass != kSubclassHda) continue;
                busOut = bus;
                deviceOut = device;
                functionOut = function;
                return true;
            }
        }
    }
    return false;
}

volatile uint16_t& reg16(volatile uint8_t* registers, uint32_t offset) noexcept {
    return *reinterpret_cast<volatile uint16_t*>(registers + offset);
}
volatile uint32_t& reg32(volatile uint8_t* registers, uint32_t offset) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(registers + offset);
}

bool selectRingSize(volatile uint8_t* registers, uint32_t offset,
                    uint32_t& entries) noexcept {
    const uint8_t capabilities = registers[offset] & 0x70u;
    uint8_t selection{};
    if (capabilities & 0x40u) { entries = 256; selection = 2; }
    else if (capabilities & 0x20u) { entries = 16; selection = 1; }
    else if (capabilities & 0x10u) { entries = 2; selection = 0; }
    else return false;
    registers[offset] = static_cast<uint8_t>(capabilities | selection);
    return true;
}

bool stopCodecRings(Log log) noexcept {
    if (!g_controller.ringsRunning && !g_controller.rirbDmaToken &&
        !g_controller.corbDmaToken) return true;
    g_controller.registers[kCorbControl] = 0;
    g_controller.registers[kRirbControl] = 0;
    bool stopped = false;
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (!(g_controller.registers[kCorbControl] & kRingRun) &&
            !(g_controller.registers[kRirbControl] & kRingRun)) {
            stopped = true;
            break;
        }
    }
    uint32_t command{};
    const bool busMasterDisabled = readConfig(g_controller.bus, g_controller.device,
        g_controller.function, 4, command) &&
        writeConfig(g_controller.bus, g_controller.device, g_controller.function, 4,
                    (command & 0xffffu) & ~uint32_t{kPciBusMasterEnable});
    if (!stopped || !busMasterDisabled) {
        if (log) log("[MicaNT HDA] CORB/RIRB stop failed; DMA pages retained\r\n");
        return false;
    }
    g_controller.busMasterEnabled = false;

    if (g_controller.rirbDmaToken) {
        const auto status = io::dma::release(&g_controllerOwner,
                                              g_controller.rirbDmaToken, dmaStopped);
        if (status != NtStatus::Success) {
            if (log) log("[MicaNT HDA] RIRB DMA release failed; resources retained\r\n");
            return false;
        }
        g_controller.rirbDmaToken = 0;
        g_controller.rirb = nullptr;
    }
    if (g_controller.corbDmaToken) {
        const auto status = io::dma::release(&g_controllerOwner,
                                              g_controller.corbDmaToken, dmaStopped);
        if (status != NtStatus::Success) {
            if (log) log("[MicaNT HDA] CORB DMA release failed; resources retained\r\n");
            return false;
        }
        g_controller.corbDmaToken = 0;
        g_controller.corb = nullptr;
    }
    g_controller.ringsRunning = false;
    return true;
}

bool startCodecRings(Log log) noexcept {
    auto& controller = g_controller;
    if (controller.ringsRunning) return true;
    const uint64_t highest = (controller.gcap & 1u) ? UINT64_MAX : UINT32_MAX;
    uint64_t corbToken{}, rirbToken{};
    uint64_t corbAddress{}, rirbAddress{};
    void* corb{};
    void* rirb{};
    auto status = io::dma::allocate(&g_controllerOwner, controller.claimToken,
        kDmaPageBytes, 128, highest, &corb, &corbAddress, &corbToken);
    if (status != NtStatus::Success) {
        if (log) log("[MicaNT HDA] CORB DMA allocation failed\r\n");
        return false;
    }
    status = io::dma::allocate(&g_controllerOwner, controller.claimToken,
        kDmaPageBytes, 128, highest, &rirb, &rirbAddress, &rirbToken);
    if (status != NtStatus::Success) {
        (void)io::dma::release(&g_controllerOwner, corbToken, dmaStopped);
        if (log) log("[MicaNT HDA] RIRB DMA allocation failed\r\n");
        return false;
    }

    uint32_t corbEntries{}, rirbEntries{};
    if (!selectRingSize(controller.registers, kCorbSize, corbEntries) ||
        !selectRingSize(controller.registers, kRirbSize, rirbEntries) ||
        corbAddress > highest || rirbAddress > highest) {
        (void)io::dma::release(&g_controllerOwner, rirbToken, dmaStopped);
        (void)io::dma::release(&g_controllerOwner, corbToken, dmaStopped);
        if (log) log("[MicaNT HDA] CORB/RIRB capabilities or DMA address rejected\r\n");
        return false;
    }

    controller.corb = corb;
    controller.corbAddress = corbAddress;
    controller.corbDmaToken = corbToken;
    controller.corbEntries = corbEntries;
    controller.corbWrite = 0;
    controller.rirb = rirb;
    controller.rirbAddress = rirbAddress;
    controller.rirbDmaToken = rirbToken;
    controller.rirbEntries = rirbEntries;

    g_controller.registers[kCorbControl] = 0;
    g_controller.registers[kRirbControl] = 0;
    reg32(g_controller.registers, kCorbBaseLow) = static_cast<uint32_t>(corbAddress);
    reg32(g_controller.registers, kCorbBaseHigh) = static_cast<uint32_t>(corbAddress >> 32);
    reg32(g_controller.registers, kRirbBaseLow) = static_cast<uint32_t>(rirbAddress);
    reg32(g_controller.registers, kRirbBaseHigh) = static_cast<uint32_t>(rirbAddress >> 32);
    reg16(g_controller.registers, kCorbWritePointer) = 0;
    reg16(g_controller.registers, 0x5a) = 1;

    // CORBRP reset is a two-step handshake on controllers that do not
    // self-clear: wait for hardware to acknowledge RST, then clear it.
    reg16(g_controller.registers, kCorbReadPointer) = 0x8000u;
    bool corbResetAcknowledged = false;
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (reg16(g_controller.registers, kCorbReadPointer) & 0x8000u) {
            corbResetAcknowledged = true;
            break;
        }
    }
    if (!corbResetAcknowledged) {
        if (log) {
            log("[MicaNT HDA] CORBRP reset acknowledgement missing, value=");
            logHex(log, reg16(g_controller.registers, kCorbReadPointer));
            log("\r\n");
        }
        (void)io::dma::release(&g_controllerOwner, rirbToken, dmaStopped);
        (void)io::dma::release(&g_controllerOwner, corbToken, dmaStopped);
        controller.rirbDmaToken = controller.corbDmaToken = 0;
        controller.rirb = controller.corb = nullptr;
        if (log) log("[MicaNT HDA] CORB/RIRB pointer reset timed out\r\n");
        return false;
    }
    reg16(g_controller.registers, kCorbReadPointer) = 0;
    reg16(g_controller.registers, kRirbWritePointer) = 0x8000u;
    for (uint32_t i = 0; i < 1000000; ++i) {
        if ((reg16(g_controller.registers, kRirbWritePointer) & 0x8000u) == 0) break;
        if (i == 999999) {
            (void)io::dma::release(&g_controllerOwner, rirbToken, dmaStopped);
            (void)io::dma::release(&g_controllerOwner, corbToken, dmaStopped);
            controller.rirbDmaToken = controller.corbDmaToken = 0;
            controller.rirb = controller.corb = nullptr;
            if (log) log("[MicaNT HDA] RIRBWP reset timed out\r\n");
            return false;
        }
    }
    for (uint32_t i = 0; i < 1000000; ++i) {
        if ((reg16(g_controller.registers, kCorbReadPointer) & 0x8000u) == 0 &&
            (reg16(g_controller.registers, kCorbReadPointer) & 0xffu) == 0) break;
        if (i == 999999) {
            (void)io::dma::release(&g_controllerOwner, rirbToken, dmaStopped);
            (void)io::dma::release(&g_controllerOwner, corbToken, dmaStopped);
            controller.rirbDmaToken = controller.corbDmaToken = 0;
            controller.rirb = controller.corb = nullptr;
            if (log) log("[MicaNT HDA] CORBRP did not clear after reset\r\n");
            return false;
        }
    }

    uint32_t command{};
    if (!readConfig(controller.bus, controller.device, controller.function, 4, command) ||
        !writeConfig(controller.bus, controller.device, controller.function, 4,
                     (command & 0xffffu) | kPciMemoryEnable | kPciBusMasterEnable)) {
        (void)io::dma::release(&g_controllerOwner, rirbToken, dmaStopped);
        (void)io::dma::release(&g_controllerOwner, corbToken, dmaStopped);
        controller.rirbDmaToken = controller.corbDmaToken = 0;
        controller.rirb = controller.corb = nullptr;
        if (log) log("[MicaNT HDA] PCI Bus Master enable failed after DMA ownership\r\n");
        return false;
    }
    controller.busMasterEnabled = true;
    // Acknowledge each polled response below. This keeps controllers that
    // gate CORB processing on RINTCNT accepting the next codec verb.
    controller.registers[kRirbControl] =
        kRingRun | kRirbResponseInterruptEnable;
    controller.registers[kCorbControl] = kRingRun;
    controller.ringsRunning = true;
    for (uint32_t i = 0; i < 1000000; ++i) {
        if ((controller.registers[kRirbControl] & kRingRun) &&
            (controller.registers[kCorbControl] & kRingRun)) return true;
    }
    (void)stopCodecRings(log);
    if (log) log("[MicaNT HDA] CORB/RIRB start timed out\r\n");
    return false;
}

bool sendVerb(uint8_t codec, uint8_t node, uint16_t verb, uint8_t payload,
              uint32_t& response) noexcept {
    auto& controller = g_controller;
    const uint32_t mask = controller.corbEntries - 1;
    const uint32_t next = (controller.corbWrite + 1) & mask;
    bool slotReady = false;
    for (uint32_t i = 0; i < 1000000; ++i) {
        if ((reg16(controller.registers, kCorbReadPointer) & mask) != next) {
            slotReady = true;
            break;
        }
    }
    if (!slotReady) return false;
    const uint32_t oldResponsePointer = reg16(controller.registers, kRirbWritePointer) &
                                        (controller.rirbEntries - 1);
    const uint32_t command = (uint32_t{codec} << 28) | (uint32_t{node} << 20) |
                             (uint32_t{verb} << 8) | payload;
    static_cast<volatile uint32_t*>(controller.corb)[next] = command;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    controller.corbWrite = next;
    reg16(controller.registers, kCorbWritePointer) = static_cast<uint16_t>(next);

    const uint32_t responseMask = controller.rirbEntries - 1;
    for (uint32_t i = 0; i < 1000000; ++i) {
        const uint32_t pointer = reg16(controller.registers, kRirbWritePointer) & responseMask;
        if (pointer == oldResponsePointer) continue;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint64_t entry = static_cast<volatile uint64_t*>(controller.rirb)[pointer];
        const uint32_t extended = static_cast<uint32_t>(entry >> 32);
        if (controller.registers[kRirbStatus] & kRirbResponseInterrupt)
            controller.registers[kRirbStatus] = kRirbResponseInterrupt;
        if (extended & (1u << 4)) continue; // unsolicited response
        if ((extended & 0xfu) != codec) return false;
        response = static_cast<uint32_t>(entry);
        return response != UINT32_MAX;
    }
    return false;
}

bool getParameter(uint8_t codec, uint8_t node, uint8_t parameter,
                  uint32_t& response) noexcept {
    return sendVerb(codec, node, 0x0f00, parameter, response);
}

bool queryConnections(uint8_t codec, uint8_t node, uint32_t widgetCaps,
                      uint32_t& observed) noexcept {
    if (!(widgetCaps & (1u << 8))) return true;
    uint32_t lengthWord{};
    if (!getParameter(codec, node, 0x0e, lengthWord)) return false;
    const uint32_t length = lengthWord & 0x7fu;
    const bool longForm = (lengthWord & 0x80u) != 0;
    if (!length || length > 32) return false;
    const uint32_t perVerb = longForm ? 2 : 4;
    for (uint32_t index = 0; index < length; index += perVerb) {
        uint32_t entries{};
        if (!sendVerb(codec, node, 0x0f02, static_cast<uint8_t>(index), entries)) return false;
        if (entries == UINT32_MAX) return false;
        observed += length - index < perVerb ? length - index : perVerb;
    }
    return true;
}

} // namespace

bool initializeController(Log log) noexcept {
    if (g_controller.registers) {
        if (log) log("[MicaNT HDA] Controller already initialized\r\n");
        return g_controller.ready;
    }
    if (log) log("[MicaNT HDA] Scanning live PCI config for class 04/03\r\n");

    Controller candidate{};
    if (!findController(candidate.bus, candidate.device, candidate.function)) {
        if (log) log("[MicaNT HDA] No HDA PCI function found\r\n");
        return false;
    }
    uint32_t pciId{};
    if (!readConfig(candidate.bus, candidate.device, candidate.function, 0, pciId)) {
        if (log) log("[MicaNT HDA] PCI identity read failed\r\n");
        return false;
    }
    if (log) {
        log("[MicaNT HDA] Live PCI BDF=");
        logHex(log, (uint64_t{candidate.bus} << 16) |
                    (uint64_t{candidate.device} << 8) | candidate.function);
        log(" ID=");
        logHex(log, pciId);
        log("\r\n");
    }

    const boot_driver::PciBarRequestV1 request{0, 0, 0, kHdaRegisterWindow};
    boot_driver::PciBarResourceV1 resource{};
    size_t resourceCount = 1;
    uint64_t token{};
    const auto claimStatus = arb::claim(
        &g_controllerOwner, 0, candidate.bus, candidate.device, candidate.function,
        &request, 1, &token, &resource, &resourceCount, useMeasuredBar);
    if (claimStatus != NtStatus::Success || resourceCount != 1) {
        if (log) log("[MicaNT HDA] PCI BAR claim failed; controller left untouched\r\n");
        return false;
    }
    candidate.claimToken = token;
    candidate.claimed = true;
    candidate.registerLength = static_cast<size_t>(resource.Length);
    if (candidate.registerLength < kHdaRegisterWindow ||
        candidate.registerLength > 0x100000 || resource.PhysicalAddress > UINT64_MAX -
            (candidate.registerLength - 1)) {
        rollback(candidate, log);
        if (log) log("[MicaNT HDA] BAR range rejected\r\n");
        return false;
    }
    if (log) {
        log("[MicaNT HDA] BAR0 physical=");
        logHex(log, resource.PhysicalAddress);
        log(" length=");
        logHex(log, candidate.registerLength);
        log("\r\n");
    }

    uint32_t command{};
    if (!readConfig(candidate.bus, candidate.device, candidate.function, 4, command)) {
        rollback(candidate, log);
        if (log) log("[MicaNT HDA] PCI command read failed\r\n");
        return false;
    }
    candidate.originalCommand = static_cast<uint16_t>(command);
    const uint16_t ownedCommand = static_cast<uint16_t>(
        (candidate.originalCommand | kPciMemoryEnable) & ~kPciBusMasterEnable);
    if (ownedCommand != candidate.originalCommand) {
        if (!writeConfig(candidate.bus, candidate.device, candidate.function, 4,
                         ownedCommand)) {
            rollback(candidate, log);
            if (log) log("[MicaNT HDA] PCI decode enable or Bus Master disable failed\r\n");
            return false;
        }
        candidate.commandChanged = true;
    }
    candidate.mappingToken = arb::retainMapping(&g_controllerOwner,
        resource.PhysicalAddress, candidate.registerLength);
    if (!candidate.mappingToken) {
        rollback(candidate, log);
        if (log) log("[MicaNT HDA] BAR mapping ownership rejected\r\n");
        return false;
    }
    candidate.registers = static_cast<volatile uint8_t*>(
        hal::amd64::mapBootstrapDeviceMemory(resource.PhysicalAddress, candidate.registerLength));
    if (!candidate.registers) {
        rollback(candidate, log);
        if (log) log("[MicaNT HDA] BAR MMIO map failed\r\n");
        return false;
    }

    const uint32_t gcap = *reinterpret_cast<volatile uint16_t*>(candidate.registers + kGcap);
    const uint8_t major = candidate.registers[kVersion + 1];
    const uint8_t minor = candidate.registers[kVersion];
    const uint32_t streamCount = ((gcap >> 12) & 0xfu) + ((gcap >> 8) & 0xfu) +
                                 ((gcap >> 3) & 0x1fu);
    if (!streamCount || major == 0 || major > 2) {
        rollback(candidate, log);
        if (log) log("[MicaNT HDA] HDA capabilities/version invalid\r\n");
        return false;
    }
    if (log) {
        log("[MicaNT HDA] GCAP=");
        logHex(log, gcap);
        log(" version=");
        logHex(log, (uint64_t{major} << 8) | minor);
        log("\r\n");
    }

    auto* gctl = reinterpret_cast<volatile uint32_t*>(candidate.registers + kGctl);
    *gctl &= ~kGctlControllerReset;
    bool resetCleared = false;
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (!(*gctl & kGctlControllerReset)) { resetCleared = true; break; }
    }
    if (resetCleared) {
        for (uint32_t delay = 0; delay < 100000; ++delay)
            __asm__ __volatile__("pause" : : : "memory");
        *gctl |= kGctlControllerReset;
    }
    bool resetComplete = false;
    if (resetCleared) {
        for (uint32_t i = 0; i < 1000000; ++i) {
            if (*gctl & kGctlControllerReset) { resetComplete = true; break; }
        }
    }
    if (!resetComplete) {
        // Avoid restoring decode or releasing the BAR while the controller may
        // still be transitioning. Keep the claimed mapping for a later retry.
        g_controller = candidate;
        if (log) log("[MicaNT HDA] Controller reset timed out; resources retained\r\n");
        return false;
    }

#if defined(MICANT_BOOTSTRAP_AUDIO_CONTROLLER_TEST)
    if (!g_rollbackFailureInjected) {
        g_rollbackFailureInjected = true;
        rollback(candidate, log);
        if (log) log("[MicaNT HDA] Injected post-reset failure rolled back BAR mapping, Bus Master, and PCI claim\r\n");
        return initializeController(log);
    }
#endif

    g_controller = candidate;
    g_controller.ready = true;
    if (log) log("[MicaNT HDA] PCI 04/03 controller claimed, BAR0 mapped, reset complete\r\n");
    return true;
}

bool discoverCodecTopology(Log log) noexcept {
    if (!g_controller.ready || !g_controller.registers) {
        if (log) log("[MicaNT HDA] Codec discovery requires a ready controller\r\n");
        return false;
    }
    if (g_controller.codecDiscovered) return true;
    if (!startCodecRings(log)) return false;

    bool valid = true;
    uint32_t present{};
    for (uint32_t i = 0; i < 1000000; ++i) {
        present = reg16(g_controller.registers, kStateStatus) & 0x7fffu;
        if (present) break;
    }
    if (!present) {
        if (log) log("[MicaNT HDA] No codec reported present after reset\r\n");
        valid = false;
    }

    uint32_t codecCount{}, functionGroupCount{}, widgetCount{}, pinCount{}, connectionCount{};
    for (uint8_t codec = 0; valid && codec < 15; ++codec) {
        if (!(present & (1u << codec))) continue;
        uint32_t vendorId{}, rootSubnodes{};
        const bool vendorReceived = getParameter(codec, 0, 0x00, vendorId);
        const bool subnodesReceived = vendorReceived && vendorId &&
            getParameter(codec, 0, 0x04, rootSubnodes);
        if (!vendorReceived || !vendorId || !subnodesReceived) {
            if (log) {
                log("[MicaNT HDA] Codec identity/subnode transaction failed codec=");
                logHex(log, codec);
                log(" STATESTS=");
                logHex(log, present);
                log(" CORBRP=");
                logHex(log, reg16(g_controller.registers, kCorbReadPointer));
                log(" CORBWP=");
                logHex(log, reg16(g_controller.registers, kCorbWritePointer));
                log(" CORBSTS=");
                logHex(log, g_controller.registers[0x4d]);
                log(" CORBCTL=");
                logHex(log, g_controller.registers[kCorbControl]);
                log(" RIRBWP=");
                logHex(log, reg16(g_controller.registers, kRirbWritePointer));
                log(" RIRBSTS=");
                logHex(log, g_controller.registers[0x5d]);
                log(" vendor=");
                logHex(log, vendorId);
                log(" subnodes=");
                logHex(log, rootSubnodes);
                log(" vendor-ok=");
                logHex(log, vendorReceived);
                log(" subnodes-ok=");
                logHex(log, subnodesReceived);
                log("\r\n");
            }
            valid = false;
            break;
        }
        const uint32_t firstGroup = rootSubnodes & 0xffu;
        const uint32_t groupCount = (rootSubnodes >> 16) & 0xffu;
        if (!groupCount || groupCount > 32 || firstGroup + groupCount > 256) {
            if (log) {
                log("[MicaNT HDA] Codec function-group range rejected subnodes=");
                logHex(log, rootSubnodes);
                log("\r\n");
            }
            valid = false;
            break;
        }
        ++codecCount;
        if (log) {
            log("[MicaNT HDA] Codec address=");
            logHex(log, codec);
            log(" vendor/device=");
            logHex(log, vendorId);
            log(" function-groups=");
            logHex(log, groupCount);
            log("\r\n");
        }

        for (uint32_t groupOffset = 0; valid && groupOffset < groupCount; ++groupOffset) {
            const auto group = static_cast<uint8_t>(firstGroup + groupOffset);
            uint32_t groupType{};
            if (!getParameter(codec, group, 0x05, groupType)) {
                if (log) log("[MicaNT HDA] Function-group type query failed\r\n");
                valid = false;
                break;
            }
            if ((groupType & 0xffu) != 1u) continue; // audio function group
            ++functionGroupCount;
            uint32_t formats{}, subnodes{};
            if (!getParameter(codec, group, 0x0a, formats) || !formats ||
                !getParameter(codec, group, 0x04, subnodes)) {
                if (log) log("[MicaNT HDA] Audio function-group capabilities query failed\r\n");
                valid = false;
                break;
            }
            const uint32_t firstWidget = subnodes & 0xffu;
            const uint32_t widgets = (subnodes >> 16) & 0xffu;
            if (!widgets || widgets > 64 || firstWidget + widgets > 256) {
                if (log) log("[MicaNT HDA] Audio widget range rejected\r\n");
                valid = false;
                break;
            }
            for (uint32_t widgetOffset = 0; valid && widgetOffset < widgets; ++widgetOffset) {
                const auto widget = static_cast<uint8_t>(firstWidget + widgetOffset);
                uint32_t caps{};
                if (!getParameter(codec, widget, 0x09, caps)) {
                    if (log) log("[MicaNT HDA] Widget capability transaction failed\r\n");
                    valid = false;
                    break;
                }
                ++widgetCount;
                const uint32_t type = (caps >> 20) & 0xfu;
                if (type == 4u) {
                    uint32_t pinCaps{}, defaultConfig{};
                    if (!getParameter(codec, widget, 0x0c, pinCaps) || !pinCaps ||
                        !sendVerb(codec, widget, 0x0f1c, 0, defaultConfig)) {
                        if (log) log("[MicaNT HDA] Pin capability/default configuration query failed\r\n");
                        valid = false;
                        break;
                    }
                    ++pinCount;
                }
                if (caps & (1u << 8)) {
                    const uint32_t previous = connectionCount;
                    if (!queryConnections(codec, widget, caps, connectionCount)) {
                        if (log) log("[MicaNT HDA] Widget connection list rejected or timed out\r\n");
                        valid = false;
                        break;
                    }
                    if (connectionCount - previous > 32) valid = false;
                }
            }
        }
    }

    if (!codecCount || !functionGroupCount || !widgetCount || !pinCount) valid = false;
    if (!valid) {
        (void)stopCodecRings(log);
        if (log) log("[MicaNT HDA] Codec topology discovery failed explicitly\r\n");
        return false;
    }
    g_controller.codecDiscovered = true;
    if (log) {
        log("[MicaNT HDA] CORB/RIRB codec topology verified codecs=");
        logHex(log, codecCount);
        log(" function-groups=");
        logHex(log, functionGroupCount);
        log(" widgets=");
        logHex(log, widgetCount);
        log(" pins=");
        logHex(log, pinCount);
        log(" connections=");
        logHex(log, connectionCount);
        log("\r\n");
    }
    return true;
}

} // namespace micant::km::audio::hda

#endif
