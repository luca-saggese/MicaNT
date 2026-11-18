#include "micant/km/drivers/audio/hdaudio_baremetal.hpp"

#if defined(MICANT_FREESTANDING)

#include "micant/km/executive/arbiter_baremetal.hpp"
#include "micant/km/executive/dma_baremetal.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/core/sync_baremetal.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/shared/boot_driver.hpp"
#include "micant/shared/native_audio_api.hpp"
#include "micant/shared/ntstatus.hpp"
#include <cstring>
#include <new>

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
constexpr uint32_t kInterruptControl = 0x20;
constexpr uint32_t kInterruptStatus = 0x24;
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
constexpr uint32_t kOutputStreamBase = 0x100; // QEMU SDO0 follows four SDI descriptors.
constexpr uint32_t kInputStreamBase = 0x80; // QEMU SDI0.
constexpr uint32_t kStreamStride = 0x20;
constexpr uint8_t kRingRun = 1u << 1;
constexpr uint8_t kRirbResponseInterruptEnable = 1u << 0;
constexpr uint8_t kRirbResponseInterrupt = 1u << 0;
constexpr size_t kDmaPageBytes = 4096;
constexpr size_t kMaximumRenderBytes = micant::shared::audio::MaximumWriteBytes;
constexpr uint32_t kStreamRun = 1u << 1;
constexpr uint32_t kStreamReset = 1u << 0;
constexpr uint32_t kStreamInterruptOnCompletion = 1u << 2;
constexpr uint32_t kStreamInterruptOnFifoError = 1u << 3;
constexpr uint32_t kStreamInterruptOnDescriptorError = 1u << 4;
constexpr uint8_t kStreamBufferCompletionStatus = 1u << 2;
constexpr uint32_t kBdlInterruptOnCompletion = 1u;
constexpr uint32_t kWidgetInputAmpPresent = 1u << 1;
constexpr uint32_t kAmpCapabilityStepsMask = 0x7fu << 8;
constexpr uint32_t kAmpCapabilityMute = 1u << 31;

struct BdlEntry { uint64_t address{}; uint32_t length{}, flags{}; };
static_assert(sizeof(BdlEntry) == 16);

struct Controller {
    uint16_t bus{}, device{}, function{};
    uint64_t claimToken{}, mappingToken{};
    uint8_t codecAddress{};
    uint16_t originalCommand{};
    uint8_t msiCapability{};
    uint16_t msiOriginalControl{};
    uint32_t msiOriginalAddressLow{}, msiOriginalAddressHigh{}, msiOriginalData{};
    uint64_t msiToken{};
    bool msi64Bit{};
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
    uint8_t audioFunctionGroup{}, outputDac{}, outputPin{}, outputConnection{};
    uint8_t inputAdc{}, inputPin{}, inputConnection{};
    uint32_t inputAmpCaps{};
    uint16_t outputFormat{0x0011};
    bool outputRouteReady{}, inputRouteReady{};
};

Controller g_controller{};
int g_controllerOwner{};
ob::ObjectType g_audioDeviceType{};
ob::ObjectHeader* g_audioDeviceReference{};
io::DeviceObject* g_audioDevice{};
std::atomic_flag g_renderInProgress = ATOMIC_FLAG_INIT;
Log g_log{};
uint64_t g_renderPcmToken{}, g_renderBdlToken{};
uint64_t g_renderPcmAddress{}, g_renderBdlAddress{};
void* g_renderPcm{};
void* g_renderBdl{};
bool g_renderFaulted{};
std::atomic_flag g_captureInProgress = ATOMIC_FLAG_INIT;
sync::detail::BootstrapEvent g_captureReady{true, false};
ke::KDPC g_captureDpc{};
std::atomic<uint32_t> g_captureInterruptStatus{};
std::atomic<bool> g_captureInterruptEnabled{};
std::atomic<uint32_t> g_captureIsrActive{};
uint64_t g_capturePcmToken{}, g_captureBdlToken{};
uint64_t g_capturePcmAddress{}, g_captureBdlAddress{};
void* g_capturePcm{};
void* g_captureBdl{};
bool g_captureFaulted{};
uint64_t g_captureFrameIndex{};
#if defined(MICANT_BOOTSTRAP_AUDIO_CAPTURE_TEST)
bool g_captureFailureInjected{};
#endif
#if defined(MICANT_BOOTSTRAP_AUDIO_CONTROLLER_TEST)
bool g_rollbackFailureInjected{};
#endif
#if defined(MICANT_BOOTSTRAP_AUDIO_RENDER_TEST)
bool g_renderFailureInjected{};
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

void captureDpcRoutine(ke::KDPC*, void*, void*, void*) {
    (void)g_captureReady.set();
}

void captureMsiRoutine(void* context) noexcept {
    auto* controller = static_cast<Controller*>(context);
    if (!controller) return;
    g_captureIsrActive.fetch_add(1, std::memory_order_acq_rel);
    if (g_captureInterruptEnabled.load(std::memory_order_acquire)) {
        const auto* stream = controller->registers + kInputStreamBase;
        const uint8_t status = stream[0x03];
        if (status & (kStreamBufferCompletionStatus | (1u << 3) | (1u << 4))) {
            const_cast<volatile uint8_t*>(stream)[0x03] = status & 0x1cu;
            g_captureInterruptStatus.fetch_or(status, std::memory_order_acq_rel);
            (void)ke::KeInsertQueueDpc(&g_captureDpc);
        }
    }
    g_captureIsrActive.fetch_sub(1, std::memory_order_release);
}

bool enableCaptureMsi(Log log) noexcept {
    auto& controller = g_controller;
    if (controller.msiToken) {
        g_captureInterruptStatus.store(0, std::memory_order_release);
        g_captureInterruptEnabled.store(true, std::memory_order_release);
        return true;
    }
    uint32_t statusCommand{}, pointerWord{};
    if (!readConfig(controller.bus, controller.device, controller.function, 4, statusCommand) ||
        !(statusCommand & (1u << 20)) ||
        !readConfig(controller.bus, controller.device, controller.function, 0x34, pointerWord)) {
        if (log) log("[MicaNT Audio] PCI MSI capability list unavailable\r\n");
        return false;
    }
    uint8_t capability = static_cast<uint8_t>(pointerWord & 0xfcu);
    uint32_t capabilityWord{};
    bool found = false;
    for (uint32_t index = 0; capability && index < 48; ++index) {
        if (capability < 0x40 || capability > 0xfc || (capability & 3u) ||
            !readConfig(controller.bus, controller.device, controller.function,
                        capability, capabilityWord)) {
            if (log) log("[MicaNT Audio] PCI capability chain invalid during MSI search\r\n");
            return false;
        }
        if ((capabilityWord & 0xffu) == 0x05u) { found = true; break; }
        capability = static_cast<uint8_t>((capabilityWord >> 8) & 0xfcu);
    }
    if (!found) {
        if (log) log("[MicaNT Audio] PCI function has no MSI capability\r\n");
        return false;
    }
    const uint16_t control = static_cast<uint16_t>(capabilityWord >> 16);
    if (control & 1u) {
        if (log) log("[MicaNT Audio] PCI MSI is already enabled by another owner\r\n");
        return false; // Never replace an MSI route owned by firmware.
    }
    const bool address64 = (control & (1u << 7)) != 0;
    uint32_t addressLow{}, addressHigh{}, messageData{};
    if (!readConfig(controller.bus, controller.device, controller.function, capability + 4, addressLow))
        return false;
    if (address64) {
        if (!readConfig(controller.bus, controller.device, controller.function, capability + 8, addressHigh) ||
            !readConfig(controller.bus, controller.device, controller.function, capability + 12, messageData))
            return false;
    } else if (!readConfig(controller.bus, controller.device, controller.function, capability + 8, messageData)) {
        return false;
    }
    hal::amd64::BootstrapMsiMessage message{};
    if (!hal::amd64::connectBootstrapMsiInterrupt(captureMsiRoutine, &controller, message)) {
        if (log) log("[MicaNT Audio] HAL MSI vector reservation failed\r\n");
        return false;
    }
    controller.msiCapability = capability;
    controller.msiOriginalControl = control;
    controller.msiOriginalAddressLow = addressLow;
    controller.msiOriginalAddressHigh = addressHigh;
    controller.msiOriginalData = messageData;
    controller.msi64Bit = address64;
    controller.msiToken = message.token;
    const bool programmed = writeConfig(controller.bus, controller.device, controller.function,
            capability + 4, static_cast<uint32_t>(message.address)) &&
        (!address64 || writeConfig(controller.bus, controller.device, controller.function,
            capability + 8, static_cast<uint32_t>(message.address >> 32))) &&
        writeConfig(controller.bus, controller.device, controller.function,
            capability + (address64 ? 12 : 8), message.data & 0xffffu) &&
        writeConfig(controller.bus, controller.device, controller.function, capability,
            (capabilityWord & 0x0000ffffu) |
            (uint32_t{static_cast<uint16_t>((control & ~uint16_t{0x0070u}) | 1u)} << 16));
    if (!programmed) {
        (void)writeConfig(controller.bus, controller.device, controller.function,
                           capability + 4, addressLow);
        if (address64) (void)writeConfig(controller.bus, controller.device, controller.function,
                                         capability + 8, addressHigh);
        (void)writeConfig(controller.bus, controller.device, controller.function,
                           capability + (address64 ? 12 : 8), messageData);
        (void)hal::amd64::disconnectBootstrapMsiInterrupt(message.token);
        controller.msiToken = 0;
        if (log) log("[MicaNT Audio] PCI MSI message programming failed\r\n");
        return false;
    }
    g_captureInterruptStatus.store(0, std::memory_order_release);
    g_captureInterruptEnabled.store(true, std::memory_order_release);
    if (log) log("[MicaNT Audio] Capture MSI route enabled\r\n");
    return true;
}

bool disableCaptureMsi() noexcept {
    auto& controller = g_controller;
    if (!controller.msiToken) return true;
    g_captureInterruptEnabled.store(false, std::memory_order_release);
    for (uint32_t wait = 0; wait < 1000000; ++wait)
        if (g_captureIsrActive.load(std::memory_order_acquire) == 0) break;
    if (g_captureIsrActive.load(std::memory_order_acquire) != 0) return false;
    // Keep the HAL vector and its controller context installed for the whole
    // controller lifetime. INTCTL and the ISR gate are quiesced per request;
    // disconnecting here could leave a pending MSI targeting an empty IDT slot.
    return true;
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

bool releaseCodecRingDma(Log log) noexcept {
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
    return true;
}

bool stopCodecRings(Log log, bool releaseDma = true) noexcept {
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
    g_controller.ringsRunning = false;
    return !releaseDma || releaseCodecRingDma(log);
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

bool sendVerb(uint8_t codec, uint8_t node, uint16_t verb, uint16_t payload,
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
                      uint32_t& observed, uint8_t* firstTarget = nullptr) noexcept {
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
        const uint32_t returned = length - index < perVerb ? length - index : perVerb;
        const uint32_t fieldBits = longForm ? 16 : 8;
        const uint32_t fieldMask = longForm ? 0x7fffu : 0x7fu;
        for (uint32_t field = 0; field < returned; ++field) {
            const uint32_t target = (entries >> (field * fieldBits)) & fieldMask;
            const bool range = (entries >> (field * fieldBits + fieldBits - 1)) & 1u;
            if (target > 127 || range) return false; // no compressed ranges in this bounded path
            if (firstTarget && index + field == 0) *firstTarget = static_cast<uint8_t>(target);
        }
        observed += returned;
    }
    return true;
}

bool configureOutputRoute(Log log) noexcept {
    auto& controller = g_controller;
    if (controller.outputRouteReady) return true;
    uint32_t ignored{};
    if (!sendVerb(controller.codecAddress, controller.audioFunctionGroup, 0x0705, 0, ignored) ||
        !sendVerb(controller.codecAddress, controller.outputPin, 0x0701, 0, ignored) ||
        !sendVerb(controller.codecAddress, controller.outputPin, 0x0707, 0x40, ignored) ||
        !sendVerb(controller.codecAddress, controller.outputDac, 0x0706, 0x10, ignored) ||
        !sendVerb(controller.codecAddress, controller.outputDac, 0x0200,
                  controller.outputFormat, ignored)) {
        if (log) log("[MicaNT HDA] Codec output route configuration failed\r\n");
        return false;
    }
    controller.outputRouteReady = true;
    return true;
}

bool configureInputRoute(Log log) noexcept {
    auto& controller = g_controller;
    if (controller.inputRouteReady) return true;
    if (!controller.inputAdc || !controller.inputPin ||
        controller.inputConnection != controller.inputPin) return false;
    uint32_t ignored{};
    if (!sendVerb(controller.codecAddress, controller.audioFunctionGroup,
                  0x0705, 0, ignored) ||
        !sendVerb(controller.codecAddress, controller.inputPin, 0x0701, 0, ignored) ||
        !sendVerb(controller.codecAddress, controller.inputPin, 0x0707, 0x20, ignored) ||
        !sendVerb(controller.codecAddress, controller.inputAdc, 0x0706, 0x10, ignored) ||
        !sendVerb(controller.codecAddress, controller.inputAdc, 0x0200,
                  controller.outputFormat, ignored)) {
        if (log) log("[MicaNT HDA] Codec input route configuration failed\r\n");
        return false;
    }
    controller.inputRouteReady = true;
    if (log) log("[MicaNT HDA] Codec ADC/pin input route configured\r\n");
    return true;
}

bool stopOutputStream(volatile uint8_t* streamRegisters) noexcept {
    reg32(streamRegisters, 0x00) &= ~uint32_t{kStreamRun};
    for (uint32_t i = 0; i < 10000000; ++i)
        if (!(reg32(streamRegisters, 0x00) & kStreamRun)) return true;
    return false;
}

NtStatus submitPcm(void*, uint64_t, const void* buffer, size_t length,
                   size_t* bytesWritten) noexcept {
    if (!bytesWritten || (!buffer && length)) return NtStatus::InvalidParameter;
    *bytesWritten = 0;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceRequest;
    if (!length) return NtStatus::Success;
    if (length > kMaximumRenderBytes ||
        length % micant::shared::audio::BytesPerFrame != 0)
        return NtStatus::InvalidParameter;
    if (!g_controller.ready || !g_controller.codecDiscovered ||
        !g_controller.outputDac || !g_controller.outputPin)
        return NtStatus::DeviceNotReady;
    if (g_renderFaulted) return NtStatus::DeviceNotReady;
    if (g_renderInProgress.test_and_set(std::memory_order_acquire))
        return NtStatus::DeviceBusy;
    struct RenderRelease { ~RenderRelease() { g_renderInProgress.clear(std::memory_order_release); } } renderRelease;
    const Log log = g_log;
    if (!startCodecRings(log) || !configureOutputRoute(log)) {
        g_renderFaulted = true;
        return NtStatus::DeviceNotReady;
    }

    const uint64_t highest = (g_controller.gcap & 1u) ? UINT64_MAX : UINT32_MAX;
    uint64_t bdlAddress{}, pcmAddress{};
    NtStatus status = NtStatus::Success;
    if (!g_renderBdlToken && !g_renderPcmToken) {
        status = io::dma::allocate(&g_controllerOwner, g_controller.claimToken,
            kDmaPageBytes, 128, highest, &g_renderBdl, &g_renderBdlAddress,
            &g_renderBdlToken);
        if (status != NtStatus::Success) {
            g_renderFaulted = true;
            if (log) log("[MicaNT HDA] BDL DMA allocation failed\r\n");
            return status;
        }
        status = io::dma::allocate(&g_controllerOwner, g_controller.claimToken,
            kMaximumRenderBytes, kDmaPageBytes, highest, &g_renderPcm,
            &g_renderPcmAddress, &g_renderPcmToken);
        if (status != NtStatus::Success) {
            g_renderFaulted = true; // Retain BDL ownership; HAL mappings release in LIFO order.
            if (log) log("[MicaNT HDA] PCM DMA allocation failed; BDL retained\r\n");
            return status;
        }
    } else if (!g_renderBdlToken || !g_renderPcmToken) {
        g_renderFaulted = true;
        return NtStatus::DeviceNotReady;
    }
    pcmAddress = g_renderPcmAddress;
    bdlAddress = g_renderBdlAddress;
    std::memcpy(g_renderPcm, buffer, length);
    auto* bdl = static_cast<BdlEntry*>(g_renderBdl);
    bdl[0] = {pcmAddress, static_cast<uint32_t>(length), kBdlInterruptOnCompletion};
    __atomic_thread_fence(__ATOMIC_RELEASE);

    volatile uint8_t* stream = g_controller.registers + kOutputStreamBase;
#if defined(MICANT_BOOTSTRAP_AUDIO_RENDER_TEST)
    const bool injectStartFailure = !g_renderFailureInjected;
    if (injectStartFailure) {
        g_renderFailureInjected = true;
        status = NtStatus::DeviceNotReady;
    }
#endif
    if (kOutputStreamBase + kStreamStride > g_controller.registerLength) {
        status = NtStatus::DeviceNotReady;
    } else {
        reg32(stream, 0x00) = kStreamReset;
        bool resetObserved = false;
        for (uint32_t i = 0; i < 1000000; ++i) {
            if (reg32(stream, 0x00) & kStreamReset) { resetObserved = true; break; }
        }
        if (!resetObserved) status = NtStatus::DeviceNotReady;
        else {
            reg32(stream, 0x00) = 0;
            bool resetCleared = false;
            for (uint32_t i = 0; i < 1000000; ++i) {
                if (!(reg32(stream, 0x00) & kStreamReset)) { resetCleared = true; break; }
            }
            if (!resetCleared) status = NtStatus::DeviceNotReady;
        }
    }
    if (status == NtStatus::Success) {
        reg32(stream, 0x18) = static_cast<uint32_t>(bdlAddress);
        reg32(stream, 0x1c) = static_cast<uint32_t>(bdlAddress >> 32);
        reg32(stream, 0x08) = static_cast<uint32_t>(length); // cyclic buffer length
        reg16(stream, 0x0c) = 0; // one BDL entry, last valid index zero
        reg16(stream, 0x12) = g_controller.outputFormat;
        stream[0x03] = 0x1cu; // clear stale BCIS/FIFOE/DESE status
        reg32(stream, 0x00) = (1u << 20) | kStreamInterruptOnCompletion | kStreamRun;
        bool completed = false;
        for (uint32_t i = 0; i < 100000000; ++i) {
            if (stream[0x03] & kStreamBufferCompletionStatus) { completed = true; break; }
        }
        if (!completed) status = NtStatus::IoTimeout;
        else if (log) log("[MicaNT Audio] PCM stream completion verified\r\n");
    }

    const bool streamStopped = stopOutputStream(stream);
    if (!streamStopped) {
        g_renderFaulted = true;
        if (log) log("[MicaNT HDA] Output stream stop timed out; DMA pages retained\r\n");
        return NtStatus::DeviceBusy;
    }
    if (log) log("[MicaNT HDA] Output stream stopped\r\n");
    stream[0x03] = 0x1cu;
    if (!stopCodecRings(log, false)) { g_renderFaulted = true; return NtStatus::DeviceBusy; }
    if (g_renderPcmToken) {
        const auto releaseStatus = io::dma::release(&g_controllerOwner, g_renderPcmToken, dmaStopped);
        if (releaseStatus != NtStatus::Success) { g_renderFaulted = true; return releaseStatus; }
        g_renderPcmToken = 0; g_renderPcm = nullptr; g_renderPcmAddress = 0;
    }
    if (g_renderBdlToken) {
        const auto releaseStatus = io::dma::release(&g_controllerOwner, g_renderBdlToken, dmaStopped);
        if (releaseStatus != NtStatus::Success) { g_renderFaulted = true; return releaseStatus; }
        g_renderBdlToken = 0; g_renderBdl = nullptr; g_renderBdlAddress = 0;
    }
    if (!releaseCodecRingDma(log)) { g_renderFaulted = true; return NtStatus::DeviceBusy; }
#if defined(MICANT_BOOTSTRAP_AUDIO_RENDER_TEST)
    if (injectStartFailure && status == NtStatus::DeviceNotReady && log)
        log("[MicaNT Audio] Injected render failure released all DMA resources\r\n");
#endif
    if (status == NtStatus::Success) *bytesWritten = length;
    return status;
}

NtStatus capturePcm(void*, uint64_t, void* buffer, size_t length,
                    size_t* bytesRead) noexcept {
    if (!bytesRead || (!buffer && length)) return NtStatus::InvalidParameter;
    *bytesRead = 0;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidDeviceRequest;
    if (length < sizeof(micant::shared::audio::CapturePacketHeader) + micant::shared::audio::BytesPerFrame ||
        length > micant::shared::audio::MaximumReadBytes ||
        (length - sizeof(micant::shared::audio::CapturePacketHeader)) % micant::shared::audio::BytesPerFrame)
        return NtStatus::InvalidParameter;
    if (!g_controller.ready || !g_controller.codecDiscovered ||
        !g_controller.inputAdc || !g_controller.inputPin)
        return NtStatus::DeviceNotReady;
    if (g_captureFaulted) return NtStatus::DeviceNotReady;
    if (g_captureInProgress.test_and_set(std::memory_order_acquire)) return NtStatus::DeviceBusy;
    struct CaptureRelease { ~CaptureRelease() { g_captureInProgress.clear(std::memory_order_release); } } guard;
    const Log log = g_log;
    if (!startCodecRings(log)) {
        g_captureFaulted = true;
        return NtStatus::DeviceNotReady;
    }
    if (!configureInputRoute(log)) {
        (void)stopCodecRings(log);
        g_captureFaulted = true;
        return NtStatus::DeviceNotReady;
    }
    const size_t pcmBytes = length - sizeof(micant::shared::audio::CapturePacketHeader);
    const size_t allocationBytes = (pcmBytes + kDmaPageBytes - 1) & ~(kDmaPageBytes - 1);
    const uint64_t highest = (g_controller.gcap & 1u) ? UINT64_MAX : UINT32_MAX;
    NtStatus status = NtStatus::Success;
    bool injectedFailureThisRead = false;
    if (!g_captureBdlToken && !g_capturePcmToken) {
        status = io::dma::allocate(&g_controllerOwner, g_controller.claimToken,
            kDmaPageBytes, 128, highest, &g_captureBdl, &g_captureBdlAddress,
            &g_captureBdlToken);
        if (status != NtStatus::Success) {
            (void)stopCodecRings(log);
            return status;
        }
        status = io::dma::allocate(&g_controllerOwner, g_controller.claimToken,
            allocationBytes, kDmaPageBytes, highest, &g_capturePcm,
            &g_capturePcmAddress, &g_capturePcmToken);
        if (status != NtStatus::Success) {
            if (!stopCodecRings(log) ||
                io::dma::release(&g_controllerOwner, g_captureBdlToken, dmaStopped) != NtStatus::Success) {
                g_captureFaulted = true;
                return NtStatus::DeviceBusy;
            }
            g_captureBdlToken = 0; g_captureBdl = nullptr; g_captureBdlAddress = 0;
            return status;
        }
    } else if (!g_captureBdlToken || !g_capturePcmToken) {
        g_captureFaulted = true;
        return NtStatus::DeviceNotReady;
    }
    if (log) log("[MicaNT Audio] Capture DMA ring and PCM buffer allocated\r\n");

    std::memset(g_capturePcm, 0, allocationBytes);
    auto* bdl = static_cast<BdlEntry*>(g_captureBdl);
    bdl[0] = {g_capturePcmAddress, static_cast<uint32_t>(pcmBytes), kBdlInterruptOnCompletion};
    __atomic_thread_fence(__ATOMIC_RELEASE);
    volatile uint8_t* stream = g_controller.registers + kInputStreamBase;
    bool streamStopped = false;
    bool resetObserved = false, resetCleared = false;
    if (log) log("[MicaNT Audio] Resetting input stream descriptor\r\n");
    if (kInputStreamBase + kStreamStride > g_controller.registerLength ||
        ((g_controller.gcap >> 8) & 0x0fu) == 0) {
        status = NtStatus::DeviceNotReady;
    } else {
        // SDnCTL is only three bytes wide; byte writes keep SDnSTS at +3
        // out of the reset handshake (it is a separate W1C register).
        stream[0] = static_cast<uint8_t>(kStreamReset);
        for (uint32_t i = 0; i < 1000000; ++i)
            if (stream[0] & kStreamReset) { resetObserved = true; break; }
        if (!resetObserved) status = NtStatus::DeviceNotReady;
        else {
            stream[0] = 0;
            for (uint32_t i = 0; i < 1000000; ++i)
                if (!(stream[0] & kStreamReset)) { resetCleared = true; break; }
            if (!resetCleared) status = NtStatus::DeviceNotReady;
        }
    }
    if (log) log("[MicaNT Audio] Input stream descriptor reset handshake finished\r\n");
    if (log) {
        log("[MicaNT Audio] Input stream setup status=");
        logHex(log, static_cast<uint32_t>(status));
        log(" reset-set="); logHex(log, resetObserved);
        log(" reset-clear="); logHex(log, resetCleared);
        log(" ctl="); logHex(log, static_cast<uint32_t>(stream[0]));
        log(" sts="); logHex(log, static_cast<uint32_t>(stream[3]));
        log(" gcap="); logHex(log, g_controller.gcap);
        log(" bar-bytes="); logHex(log, g_controller.registerLength);
        log("\r\n");
    }
    if (status == NtStatus::Success) {
#if defined(MICANT_BOOTSTRAP_AUDIO_CAPTURE_TEST)
        if (!g_captureFailureInjected) {
            g_captureFailureInjected = true;
            injectedFailureThisRead = true;
            status = NtStatus::DeviceNotReady;
        }
#endif
    }
    if (status == NtStatus::Success) {
        if (log) log("[MicaNT Audio] Reserving capture completion event and MSI\r\n");
        ke::KeInitializeDpc(&g_captureDpc, captureDpcRoutine, nullptr);
        (void)g_captureReady.reset();
        if (!enableCaptureMsi(log))
            status = NtStatus::NotSupported;
    }
    if (status == NtStatus::Success) {
        reg32(stream, 0x18) = static_cast<uint32_t>(g_captureBdlAddress);
        reg32(stream, 0x1c) = static_cast<uint32_t>(g_captureBdlAddress >> 32);
        reg32(stream, 0x08) = static_cast<uint32_t>(pcmBytes);
        reg16(stream, 0x0c) = 0;
        reg16(stream, 0x12) = g_controller.outputFormat;
        stream[0x03] = 0x1cu;
        reg32(g_controller.registers, kInterruptControl) = 0xc0000001u;
        reg32(stream, 0x00) = (1u << 20) | kStreamInterruptOnCompletion |
            kStreamInterruptOnFifoError | kStreamInterruptOnDescriptorError | kStreamRun;
        if (log) log("[MicaNT Audio] Capture SDI0 DMA started; waiting for completion event\r\n");
        status = g_captureReady.wait(false, false, 10'000'000);
        if (log) log("[MicaNT Audio] Capture completion wait returned\r\n");
        if (status == NtStatus::Success) {
            const uint32_t interruptStatus = g_captureInterruptStatus.exchange(0, std::memory_order_acq_rel);
            if (interruptStatus & (1u << 3 | 1u << 4)) status = NtStatus::DeviceDataError;
            else if (!(interruptStatus & kStreamBufferCompletionStatus)) status = NtStatus::DeviceNotReady;
            else status = NtStatus::Success;
        } else if (status == NtStatus::Timeout) status = NtStatus::IoTimeout;
    }
    streamStopped = stopOutputStream(stream);
    if (!streamStopped) {
        g_captureFaulted = true;
        if (log) log("[MicaNT HDA] Input stream stop timed out; capture DMA retained\r\n");
        return NtStatus::DeviceBusy;
    }
    stream[0x03] = 0x1cu;
    reg32(g_controller.registers, kInterruptControl) = 0;
    if (!disableCaptureMsi()) { g_captureFaulted = true; return NtStatus::DeviceBusy; }
    if (!stopCodecRings(log, false)) { g_captureFaulted = true; return NtStatus::DeviceBusy; }
    micant::shared::audio::CapturePacketHeader packet{};
    if (status == NtStatus::Success) {
        uint64_t frequency{};
        if (!ke::KeQueryPerformanceCounterValue(packet.firstFramePerformanceCounter, frequency) || !frequency) {
            status = NtStatus::DeviceNotReady;
        } else {
            packet.firstFrame = g_captureFrameIndex;
            packet.pcmBytes = static_cast<uint32_t>(pcmBytes);
            if ((g_controller.inputAmpCaps & kAmpCapabilityStepsMask) != 0)
                packet.flags |= micant::shared::audio::CaptureFlagInputGainControl;
            if (g_controller.inputAmpCaps & kAmpCapabilityMute)
                packet.flags |= micant::shared::audio::CaptureFlagInputMuteControl;
            const uint64_t capturedFrames = pcmBytes / micant::shared::audio::BytesPerFrame;
            const uint64_t duration = capturedFrames * frequency / micant::shared::audio::SampleRate;
            if (packet.firstFramePerformanceCounter < duration) status = NtStatus::DeviceNotReady;
            else {
                packet.firstFramePerformanceCounter -= duration;
                std::memcpy(buffer, &packet, sizeof(packet));
                std::memcpy(static_cast<uint8_t*>(buffer) + sizeof(packet), g_capturePcm, pcmBytes);
            }
        }
    }
    const auto pcmRelease = io::dma::release(&g_controllerOwner, g_capturePcmToken, dmaStopped);
    if (pcmRelease != NtStatus::Success) { g_captureFaulted = true; return pcmRelease; }
    g_capturePcmToken = 0; g_capturePcm = nullptr; g_capturePcmAddress = 0;
    const auto bdlRelease = io::dma::release(&g_controllerOwner, g_captureBdlToken, dmaStopped);
    if (bdlRelease != NtStatus::Success) { g_captureFaulted = true; return bdlRelease; }
    g_captureBdlToken = 0; g_captureBdl = nullptr; g_captureBdlAddress = 0;
    if (!releaseCodecRingDma(log)) { g_captureFaulted = true; return NtStatus::DeviceBusy; }
#if defined(MICANT_BOOTSTRAP_AUDIO_CAPTURE_TEST)
    if (injectedFailureThisRead && status == NtStatus::DeviceNotReady && log)
        log("[MicaNT Audio] Injected capture setup failure released all DMA resources\r\n");
#endif
    if (status != NtStatus::Success) return status;
    g_captureFrameIndex += pcmBytes / micant::shared::audio::BytesPerFrame;
    *bytesRead = sizeof(packet) + pcmBytes;
    return NtStatus::Success;
}

void releaseAudioFile(void* context) noexcept { delete static_cast<uint8_t*>(context); }
void deleteAudioDevice(ob::ObjectHeader* header) noexcept {
    header->as<io::DeviceObject>()->~DeviceObject();
}

} // namespace

bool initializeController(Log log) noexcept {
    g_log = log;
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
    candidate.gcap = gcap;
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
    bool outputDacFound = false, inputAdcFound = false;
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
        if (codecCount == 1) g_controller.codecAddress = codec;
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
            if (!g_controller.audioFunctionGroup) g_controller.audioFunctionGroup = group;
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
                if (log) {
                    log("[MicaNT HDA] Widget NID="); logHex(log, widget);
                    log(" caps="); logHex(log, caps);
                    log(" type="); logHex(log, type);
                    log("\r\n");
                }
                if (type == 0u && !outputDacFound) {
                    g_controller.outputDac = widget;
                    outputDacFound = true;
                }
                if (type == 1u && !inputAdcFound) {
                    g_controller.inputAdc = widget;
                    inputAdcFound = true;
                    if (caps & kWidgetInputAmpPresent) {
                        if (!getParameter(codec, widget, 0x0d, g_controller.inputAmpCaps)) {
                            if (log) log("[MicaNT HDA] ADC amplifier capability query failed\r\n");
                            valid = false;
                            break;
                        }
                        if (log) {
                            log("[MicaNT HDA] ADC input amplifier capabilities=");
                            logHex(log, g_controller.inputAmpCaps);
                            log("\r\n");
                        }
                    }
                }
                uint8_t firstConnection{};
                if (type == 4u) {
                    uint32_t pinCaps{}, defaultConfig{};
                    if (!getParameter(codec, widget, 0x0c, pinCaps) || !pinCaps ||
                        !sendVerb(codec, widget, 0x0f1c, 0, defaultConfig)) {
                        if (log) log("[MicaNT HDA] Pin capability/default configuration query failed\r\n");
                        valid = false;
                        break;
                    }
                    ++pinCount;
                    if ((pinCaps & (1u << 5)) && !g_controller.inputPin) {
                        g_controller.inputPin = widget;
                        if (log) {
                            log("[MicaNT HDA] Capture pin NID="); logHex(log, widget);
                            log(" caps="); logHex(log, pinCaps);
                            log(" default="); logHex(log, defaultConfig);
                            log("\r\n");
                        }
                    }
                    if ((pinCaps & (1u << 4)) && !g_controller.outputPin) {
                        g_controller.outputPin = widget;
                        if (log) {
                            log("[MicaNT HDA] Render pin NID="); logHex(log, widget);
                            log(" caps="); logHex(log, pinCaps);
                            log(" default="); logHex(log, defaultConfig);
                            log("\r\n");
                        }
                    }
                }
                if (caps & (1u << 8)) {
                    const uint32_t previous = connectionCount;
                    if (!queryConnections(codec, widget, caps, connectionCount,
                                          (type == 4u || type == 1u) ? &firstConnection : nullptr)) {
                        if (log) log("[MicaNT HDA] Widget connection list rejected or timed out\r\n");
                        valid = false;
                        break;
                    }
                    if (connectionCount - previous > 32) valid = false;
                    if (type == 4u && g_controller.outputPin == widget)
                        g_controller.outputConnection = firstConnection;
                    if (type == 1u && g_controller.inputAdc == widget)
                        g_controller.inputConnection = firstConnection;
                }
            }
        }
    }

    if (!codecCount || !functionGroupCount || !widgetCount || !pinCount) valid = false;
    const bool outputRoute = g_controller.outputDac && g_controller.outputPin &&
        g_controller.outputConnection == g_controller.outputDac;
    const bool inputRoute = g_controller.inputAdc && g_controller.inputPin &&
        g_controller.inputConnection == g_controller.inputPin;
    if (!outputRoute && !inputRoute) {
        if (log) {
            log("[MicaNT HDA] No bounded render or capture codec route found DAC=");
            logHex(log, g_controller.outputDac);
            log(" pin="); logHex(log, g_controller.outputPin);
            log(" connection="); logHex(log, g_controller.outputConnection);
            log(" ADC="); logHex(log, g_controller.inputAdc);
            log(" input-pin="); logHex(log, g_controller.inputPin);
            log(" input-connection="); logHex(log, g_controller.inputConnection);
            log("\r\n");
        }
        valid = false;
    }
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

NtStatus openAudioDevice(Handle* handle, uint32_t desiredAccess) noexcept {
    if (!handle) return NtStatus::InvalidParameter;
    *handle = 0;
    if (!g_controller.ready || !g_controller.codecDiscovered || g_renderFaulted)
        return NtStatus::DeviceNotReady;
    if ((desiredAccess & 0x0003u) != 0x0002u || (desiredAccess & ~0x0012008bu))
        return NtStatus::AccessDenied;
    if (!g_audioDevice) {
        g_audioDeviceType.typeId = ob::ObjectTypeId::Device;
        g_audioDeviceType.deleteProcedure = deleteAudioDevice;
        void* body{};
        const auto status = ob::ObCreateObject(ob::KernelMode, &g_audioDeviceType, nullptr,
            sizeof(io::DeviceObject), &body, &g_audioDeviceReference);
        if (status != NtStatus::Success) return status;
        g_audioDevice = new (body) io::DeviceObject{};
        g_audioDevice->deviceType = io::DeviceType::Unknown;
    }
    auto* fileContext = new (std::nothrow_t{}) uint8_t{};
    if (!fileContext) return NtStatus::NoMemory;
    io::FileBackend backend{};
    backend.context = fileContext;
    backend.name = u"\\Device\\HdaAudio0";
    backend.release = releaseAudioFile;
    backend.stream = true;
    backend.write = submitPcm;
    backend.synchronous = true;
    return io::IoCreateDeviceFile(handle, desiredAccess, g_audioDevice, backend);
}

NtStatus openCaptureDevice(Handle* handle, uint32_t desiredAccess) noexcept {
    if (!handle) return NtStatus::InvalidParameter;
    *handle = 0;
    if (!g_controller.ready || !g_controller.codecDiscovered ||
        !g_controller.inputAdc || !g_controller.inputPin || g_captureFaulted)
        return NtStatus::DeviceNotReady;
    if ((desiredAccess & 0x0001u) == 0 || (desiredAccess & ~0x00120089u))
        return NtStatus::AccessDenied;
    if (!g_audioDevice) {
        g_audioDeviceType.typeId = ob::ObjectTypeId::Device;
        g_audioDeviceType.deleteProcedure = deleteAudioDevice;
        void* body{};
        const auto status = ob::ObCreateObject(ob::KernelMode, &g_audioDeviceType, nullptr,
            sizeof(io::DeviceObject), &body, &g_audioDeviceReference);
        if (status != NtStatus::Success) return status;
        g_audioDevice = new (body) io::DeviceObject{};
        g_audioDevice->deviceType = io::DeviceType::Unknown;
    }
    auto* fileContext = new (std::nothrow_t{}) uint8_t{};
    if (!fileContext) return NtStatus::NoMemory;
    io::FileBackend backend{};
    backend.context = fileContext;
    backend.name = micant::shared::audio::HdaCaptureDeviceName;
    backend.read = capturePcm;
    backend.release = releaseAudioFile;
    backend.stream = true;
    backend.synchronous = true;
    return io::IoCreateDeviceFile(handle, desiredAccess, g_audioDevice, backend);
}

} // namespace micant::km::audio::hda

#endif
