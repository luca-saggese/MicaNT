#include "micant/km/drivers/usb/usb_host_baremetal.hpp"
#include "micant/km/drivers/usb/usb_baremetal.hpp"
#include "micant/km/core/passive_work.hpp"
#include "micant/km/drivers/input/kbdclass_contract.hpp"
#include "micant/km/drivers/input/mouclass_contract.hpp"
#include <atomic>

namespace micant::usb::baremetal {
namespace {
using shared::usb::Trb;
using Host = shared::usb::HostServicesV1;
using ClassServices = shared::usb::ClassServicesV1;
using Interface = shared::usb::InterfaceV1;
using Transfer = shared::usb::TransferServicesV1;
using TransferKind = shared::usb::TransferKind;
constexpr int32_t success = 0;
constexpr int32_t busy = static_cast<int32_t>(0x80000011u);
enum class Phase : uint8_t {
    Empty, RootReset, NeedEnable, WaitEnable, NeedAddress, WaitAddress, DeviceDescriptorPrefix,
    NeedEp0, WaitEp0, DeviceDescriptor,
    ConfigurationHeader, ConfigurationBody, SetConfiguration, ClassDescriptor,
    NeedEndpoint, WaitEndpoint, ClassControl, NeedInterrupt, InterruptRunning, HubDescriptor, HubPower, HubRePower,
    HubSetDepth, HubIdle, HubPortStatus, HubClearChange, HubReset, ClassInitialize,
    ClassReady, ClassRemoving, ClassRemoved, Disconnect, NeedDisable, WaitDisable, DrainDisabled, Quarantined
};
struct ClassSlot { const void* owner{}; uint64_t token{}; ClassServices services{}; };
struct Device {
    Phase phase{Phase::Empty};
    uint8_t port{}, speed{}, slot{}, parentHubSlot{}, downstreamPort{}, deviceProtocol{};
    uint8_t depth{}, ttHubSlot{}, ttPort{}, ep0Packet{};
    uint32_t route{};
    uint8_t hubPort{}, hubPorts{}, hubResetPort{}, hubThinkTime{}, hubChangeBit{};
    uint16_t hubStatus{}, hubChanges{}, hubObservedChanges{};
    bool removing{}, bulkDevice{}, hubDevice{}, hubMultiTt{}, disconnecting{};
    uint64_t hubNextPoll{}, hubResetDeadline{}, hubPowerDelay{};
    uint16_t controlLength{};
    uint64_t deadline{}, commandAddress{}, transferAddress{};
    Trb prepared{};
    HubInterface hub{};
    Interface interface{};
    Transfer transfer{};
    ClassSlot* classSlot{};
    void* classInstance{};
    Trb events[16]{};
    uint8_t eventHead{}, eventCount{};
};
struct Bus {
    const void* owner{};
    uint64_t token{};
    Host host{};
    ke::passive_work::PeriodicItem poll{};
    std::atomic_flag hostGate = ATOMIC_FLAG_INIT;
    std::atomic_flag unregisterGate = ATOMIC_FLAG_INIT;
    Device devices[4]{};
};
Bus bus{};
ClassSlot classes[4]{};
void hostLog(const char* message) noexcept {
    if (bus.host.Log) bus.host.Log(bus.host.Context, message);
}
uint64_t nextToken{1}, nextClassToken{1};
struct HostGuard {
    bool held{!bus.hostGate.test_and_set(std::memory_order_acquire)};
    ~HostGuard() { if (held) bus.hostGate.clear(std::memory_order_release); }
};
bool armDeadline(Device& d, uint64_t duration = 20'000'000) noexcept {
    uint64_t now{};
    if (!ke::KeQueryUptime100ns(now) || now > UINT64_MAX - duration) return false;
    d.deadline = now + duration;
    return true;
}
uint64_t setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) noexcept {
    return uint64_t{type} | (uint64_t{request} << 8) | (uint64_t{value} << 16) |
           (uint64_t{index} << 32) | (uint64_t{length} << 48);
}
bool select(const Device& d) noexcept {
    return d.slot && bus.host.SelectDevice(bus.host.Context, d.slot) == success;
}
bool beginControl(Device& d, uint8_t type, uint8_t request, uint16_t value,
                  uint16_t index, uint16_t length, Phase next) noexcept {
    if (!select(d) || !armDeadline(d) || bus.host.SubmitControl(bus.host.Context,
        setup(type, request, value, index, length), length, length != 0, &d.transferAddress) != success)
        return false;
    d.controlLength = length;
    d.phase = next;
    return true;
}
int32_t bulkTransfer(void*, uint8_t endpoint, void* buffer, size_t length,
                     size_t* actual, uint32_t timeout) {
    if (!actual) return static_cast<int32_t>(NtStatus::InvalidParameter);
    return bus.host.BulkTransfer(bus.host.Context, endpoint, buffer, length, actual, timeout);
}
int32_t bulkReset(void*, uint8_t interfaceNumber, uint8_t input, uint8_t output) {
    return bus.host.BulkResetRecovery(bus.host.Context, interfaceNumber, input, output);
}
bool beginBulk(void* context) {
    auto& d = *static_cast<Device*>(context);
    if (bus.hostGate.test_and_set(std::memory_order_acquire)) return false;
    if (!select(d)) { bus.hostGate.clear(std::memory_order_release); return false; }
    return true;
}
void endBulk(void*) { bus.hostGate.clear(std::memory_order_release); }
uint32_t submitKeyboard(void*, const void* records, uint32_t count) {
    if (!records || !count || count > 28) return 0;
    auto* first = const_cast<km::hid::KEYBOARD_INPUT_DATA*>(static_cast<const km::hid::KEYBOARD_INPUT_DATA*>(records));
    uint32_t consumed{};
    km::hid::KeyboardClassServiceCallback(nullptr, first, first + count, &consumed);
    return consumed <= count ? consumed : 0;
}
uint32_t submitMouse(void*, const void* records, uint32_t count) {
    if (!records || !count || count > 28) return 0;
    auto* first = const_cast<km::hid::MOUSE_INPUT_DATA*>(static_cast<const km::hid::MOUSE_INPUT_DATA*>(records));
    uint32_t consumed{};
    km::hid::MouseClassServiceCallback(nullptr, first, first + count, &consumed);
    return consumed <= count ? consumed : 0;
}
bool validInterface(const Interface& info, uint8_t speed) noexcept {
    if (info.Size < sizeof(Interface) || !info.Configuration || info.Alternate ||
        !(info.InEndpoint & 0x80u) || (info.InEndpoint & 0x70u) || !(info.InEndpoint & 15u) || !info.InPacket)
        return false;
    if (info.Kind == TransferKind::InterruptIn)
        return info.Interval && info.InPacket <= 64 && info.ReportDescriptorLength &&
               info.ReportDescriptorLength <= 4096 && (speed < 4 || info.InCompanion);
    if (info.Kind == TransferKind::BulkPair)
        return !(info.OutEndpoint & 0xf0u) && (info.OutEndpoint & 15u) && info.OutPacket &&
               (speed < 4 || (info.InCompanion && info.OutCompanion));
    return false;
}
bool selectClass(Device& d, const uint8_t* bytes, size_t length) noexcept {
    for (auto& slot : classes) {
        if (!slot.token) continue;
        Interface info{};
        const auto result = slot.services.MatchConfiguration(slot.services.Context, bytes, length, &info);
        if (result == static_cast<int32_t>(NtStatus::NotSupported)) continue;
        if (result != success || !validInterface(info, d.speed) ||
            (info.Kind == TransferKind::InterruptIn && (!slot.services.ValidateReportDescriptor || !slot.services.Report)))
            return false;
        d.transfer = {sizeof(Transfer), shared::usb::TransferServicesVersion, &d,
                      beginBulk, endBulk, bulkTransfer, bulkReset, submitKeyboard, submitMouse};
        void* instance{};
        const auto created = slot.services.Create(slot.services.Context, &info, &d.transfer,
                                                  static_cast<uint16_t>(&d - bus.devices), &instance);
        if (instance) { d.classSlot = &slot; d.classInstance = instance; d.interface = info; }
        if (created != success || !instance) return false;
        d.bulkDevice = info.Kind == TransferKind::BulkPair;
        hostLog(info.Kind == TransferKind::InterruptIn
            ? "USB host: matched HID interrupt interface"
            : "USB host: matched mass-storage bulk interface");
        return true;
    }
    return false;
}
bool enqueue(Device& d, const Trb& event) noexcept {
    if (d.eventCount == 16) { if (d.phase != Phase::ClassRemoved) d.phase = Phase::Disconnect; return false; }
    d.events[(d.eventHead + d.eventCount++) % 16] = event;
    return true;
}
bool pop(Device& d, Trb& event) noexcept {
    if (!d.eventCount) return false;
    event = d.events[d.eventHead];
    d.eventHead = static_cast<uint8_t>((d.eventHead + 1) % 16);
    --d.eventCount;
    return true;
}
bool commandWaitPhase(Phase phase) noexcept {
    return phase == Phase::WaitEnable || phase == Phase::WaitAddress || phase == Phase::WaitEp0 ||
           phase == Phase::WaitEndpoint || phase == Phase::WaitDisable;
}
void routeEvents() noexcept {
    for (unsigned budget = 0; budget < 128; ++budget) {
        Trb event{}; bool available{};
        if (bus.host.PollEvent(bus.host.Context, &event, &available) != success) {
            for (auto& d : bus.devices) if (d.phase != Phase::Empty && d.phase != Phase::ClassRemoved) d.phase = Phase::Disconnect;
            return;
        }
        if (!available) return;
        const uint32_t type = (event.control >> 10) & 63u;
        if (type == 33) {
            const uint64_t address = event.parameter & ~uint64_t{15};
            for (auto& d : bus.devices) if (commandWaitPhase(d.phase) && d.commandAddress == address) {
                (void)enqueue(d, event); break;
            }
        } else if (type == 32) {
            const uint8_t slot = static_cast<uint8_t>(event.control >> 24);
            for (auto& d : bus.devices) if (d.slot == slot && slot && d.phase != Phase::Empty) {
                (void)enqueue(d, event); break;
            }
        }
    }
}
void resetDevice(Device& d) noexcept {
    const bool wasDisconnecting = d.disconnecting;
    d.phase = Phase::Empty; d.port = d.speed = d.slot = 0;
    d.parentHubSlot = d.downstreamPort = d.deviceProtocol = 0;
    d.depth = d.ttHubSlot = d.ttPort = d.ep0Packet = 0; d.route = 0;
    d.hubPort = d.hubPorts = d.hubResetPort = d.hubThinkTime = d.hubChangeBit = 0;
    d.hubStatus = d.hubChanges = d.hubObservedChanges = 0;
    d.removing = d.bulkDevice = d.hubDevice = d.hubMultiTt = false;
    d.hubNextPoll = d.hubResetDeadline = d.hubPowerDelay = 0;
    d.controlLength = 0; d.deadline = d.commandAddress = d.transferAddress = 0;
    d.prepared = {}; d.hub = {}; d.interface = {}; d.transfer = {};
    d.classSlot = nullptr; d.classInstance = nullptr;
    d.eventHead = d.eventCount = 0;
    if (wasDisconnecting) hostLog("USB host: disconnect cleanup completed");
    d.disconnecting = false;
}
void markDisconnected(Device& d) noexcept {
    if (!d.disconnecting) {
        d.disconnecting = true;
        hostLog("USB host: disconnect detected; cleanup started");
    }
    d.phase = Phase::Disconnect;
}
void process(Device& d) noexcept {
    // Remove runs without hostGate; retire its class reference only after
    // reacquiring the gate used by unregisterClass.
    if (d.phase == Phase::ClassRemoved) {
        d.classInstance = nullptr; d.classSlot = nullptr; d.phase = Phase::Disconnect;
    }
    if (d.phase == Phase::Empty || d.phase == Phase::Quarantined ||
        d.phase == Phase::ClassInitialize || d.phase == Phase::ClassReady ||
        d.phase == Phase::ClassRemoving) return;
    if (d.phase == Phase::NeedInterrupt) {
        if (!select(d) || bus.host.SubmitInterrupt(bus.host.Context, &d.transferAddress) != success)
            d.phase = Phase::Disconnect;
        else d.phase = Phase::InterruptRunning;
    }
    if (d.phase == Phase::RootReset) {
        uint8_t speed{};
        const auto result = bus.host.PollRootPortReset(bus.host.Context, d.port, &speed);
        if (result == busy) return;
        if (result != success) { d.phase = Phase::Disconnect; return; }
        d.speed = speed;
        hostLog("USB host: root-port reset completed");
        d.phase = armDeadline(d) ? Phase::NeedEnable : Phase::Quarantined;
    }
    if (d.phase == Phase::NeedEnable && bus.host.AddressDevice) {
        uint8_t slot{};
        if (bus.host.AddressDevice(bus.host.Context, d.port, d.speed, &slot) != success || !slot) {
            d.phase = Phase::Disconnect;
            return;
        }
        d.slot = slot;
        d.ep0Packet = d.speed == 3 ? 64 : 8;
        if (!beginControl(d, 0x80, 6, 0x0100, 0, 18, Phase::DeviceDescriptor))
            d.phase = Phase::Disconnect;
        return;
    }
    if (d.phase == Phase::Disconnect) {
        if (d.hubDevice && d.slot) {
            bool childrenRemain = false;
            for (auto& child : bus.devices) if (child.phase != Phase::Empty && child.parentHubSlot == d.slot) {
                childrenRemain = true;
                if (child.phase == Phase::WaitEnable) child.removing = true;
                else if (child.phase != Phase::Quarantined && child.phase != Phase::ClassRemoved && child.phase != Phase::NeedDisable &&
                         child.phase != Phase::WaitDisable && child.phase != Phase::DrainDisabled)
                    markDisconnected(child);
            }
            if (childrenRemain) return;
        }
        if (d.classInstance) { d.phase = Phase::ClassRemoving; return; }
        if (!d.slot) { resetDevice(d); return; }
        if (bus.host.AddressDevice) {
            if (bus.host.ForgetDevice(bus.host.Context, d.slot) == success) resetDevice(d);
            else d.phase = Phase::Quarantined;
            return;
        }
        d.phase = Phase::NeedDisable;
        if (!armDeadline(d)) d.phase = Phase::Quarantined;
    }
    if (d.phase == Phase::NeedEnable || d.phase == Phase::NeedAddress ||
        d.phase == Phase::NeedEndpoint || d.phase == Phase::NeedEp0 || d.phase == Phase::NeedDisable) {
        Trb command = d.prepared;
        if (d.phase == Phase::NeedEnable) command = {0, 0, (9u << 10) |
            (uint32_t{bus.host.PortSlotType(bus.host.Context, d.port)} << 16)};
        if (d.phase == Phase::NeedDisable) command = {0, 0, (10u << 10) | (uint32_t{d.slot} << 24)};
        const auto result = bus.host.SubmitCommand(bus.host.Context, &command, &d.commandAddress);
        if (result == busy) return;
        if (result != success || !armDeadline(d)) { d.phase = Phase::Quarantined; return; }
        d.phase = d.phase == Phase::NeedEnable ? Phase::WaitEnable :
                  d.phase == Phase::NeedAddress ? Phase::WaitAddress :
                  d.phase == Phase::NeedEp0 ? Phase::WaitEp0 :
                  d.phase == Phase::NeedEndpoint ? Phase::WaitEndpoint : Phase::WaitDisable;
        return;
    }
    if (d.phase == Phase::DrainDisabled) {
        // All queued events belong to the slot just disabled; discard them
        // before its DMA context and slot can be reused.
        d.eventHead = d.eventCount = 0;
        if (bus.host.ForgetDevice(bus.host.Context, d.slot) == success) resetDevice(d);
        return;
    }
    Trb event{};
    while (pop(d, event)) {
        const uint32_t type = (event.control >> 10) & 63u;
        if (commandWaitPhase(d.phase)) {
            if (type != 33 || (event.parameter & ~uint64_t{15}) != d.commandAddress) continue;
            if ((event.status >> 24) != 1) { d.phase = Phase::Disconnect; return; }
            if (d.phase == Phase::WaitEnable) {
                d.slot = static_cast<uint8_t>(event.control >> 24);
                if (!d.slot) { d.phase = Phase::Quarantined; return; }
                if (d.removing) { markDisconnected(d); return; }
                const auto addressStatus = d.parentHubSlot ?
                    (bus.host.PrepareAddressTopology ? bus.host.PrepareAddressTopology(bus.host.Context,
                        d.slot, d.port, d.speed, d.route, d.ttHubSlot, d.ttPort, d.hubMultiTt, &d.prepared) :
                     (d.depth == 1 && (d.speed >= 3 || d.ttHubSlot == d.parentHubSlot) && bus.host.PrepareAddressDownstream ? bus.host.PrepareAddressDownstream(bus.host.Context,
                        d.slot, d.port, d.speed, d.parentHubSlot, d.downstreamPort, d.hubMultiTt, &d.prepared) : int32_t{-1})) :
                    bus.host.PrepareAddressDevice(bus.host.Context, d.slot, d.port, d.speed, &d.prepared);
                if (addressStatus != success) { d.phase = Phase::Disconnect; return; }
                d.phase = armDeadline(d) ? Phase::NeedAddress : Phase::Quarantined;
            } else if (d.phase == Phase::WaitAddress) {
                d.ep0Packet = 8;
                const bool prefix = d.speed == 1 && bus.host.PrepareEp0Packet;
                if (!beginControl(d, 0x80, 6, 0x0100, 0, prefix ? 8 : 18,
                                  prefix ? Phase::DeviceDescriptorPrefix : Phase::DeviceDescriptor)) d.phase = Phase::Disconnect;
            } else if (d.phase == Phase::WaitEp0) {
                if (!beginControl(d, 0x80, 6, 0x0100, 0, 18, Phase::DeviceDescriptor)) d.phase = Phase::Disconnect;
            } else if (d.phase == Phase::WaitDisable) {
                d.phase = Phase::DrainDisabled;
                if (!armDeadline(d)) d.phase = Phase::Quarantined;
            } else if (d.hubDevice) {
                d.hubPort = 1;
                if (!beginControl(d, 0x23, 3, 8, d.hubPort, 0, Phase::HubPower)) d.phase = Phase::Disconnect;
            } else if (d.interface.NeedsControl) {
                if (!beginControl(d, d.interface.ControlRequestType, d.interface.ControlRequest,
                                  d.interface.ControlValue, d.interface.ControlIndex, 0, Phase::ClassControl))
                    d.phase = Phase::Disconnect;
            } else d.phase = Phase::ClassInitialize;
            return;
        }
        if (type != 32 || (event.parameter & ~uint64_t{15}) != d.transferAddress ||
            static_cast<uint8_t>(event.control >> 24) != d.slot) continue;
        const uint32_t dci = (event.control >> 16) & 31u;
        if (d.phase == Phase::InterruptRunning) {
            if (dci != (d.interface.InEndpoint & 15u) * 2u + 1u) continue;
            const uint32_t completion = event.status >> 24, remaining = event.status & 0xffffffu;
            if ((completion != 1 && completion != 13) || remaining > d.interface.InPacket || !select(d)) {
                d.phase = Phase::Disconnect; return;
            }
            const auto* report = bus.host.InterruptBuffer(bus.host.Context);
            const size_t length = d.interface.InPacket - remaining;
            const bool delivered = d.classSlot && d.classInstance && d.classSlot->services.Report &&
                d.classSlot->services.Report(d.classInstance, report, length) == success;
            if (!delivered || bus.host.SubmitInterrupt(bus.host.Context, &d.transferAddress) != success)
                d.phase = Phase::Disconnect;
            return;
        }
        if (dci != 1) continue;
        if ((event.status >> 24) != 1 || (event.status & 0xffffffu) || !select(d)) {
            d.phase = Phase::Disconnect; return;
        }
        const auto* bytes = bus.host.ControlBuffer(bus.host.Context);
        if (!bytes) { d.phase = Phase::Disconnect; return; }
        if (d.phase == Phase::DeviceDescriptorPrefix) {
            const auto packet = bytes[7];
            if (bytes[0] < 18 || bytes[1] != 1 || (packet != 8 && packet != 16 && packet != 32 && packet != 64)) {
                d.phase = Phase::Disconnect;
            } else if (packet == 8) {
                if (!beginControl(d, 0x80, 6, 0x0100, 0, 18, Phase::DeviceDescriptor)) d.phase = Phase::Disconnect;
            } else if (bus.host.PrepareEp0Packet(bus.host.Context, packet, &d.prepared) != success) {
                d.phase = Phase::Disconnect;
            } else { d.ep0Packet = packet; d.phase = armDeadline(d) ? Phase::NeedEp0 : Phase::Quarantined; }
        } else if (d.phase == Phase::DeviceDescriptor) {
            d.deviceProtocol = bytes[6];
            if (bytes[0] < 18 || bytes[1] != 1 || (d.speed == 1 ? bytes[7] != d.ep0Packet :
                 bytes[7] != (d.speed >= 4 ? 9 : d.speed == 3 ? 64 : 8)) ||
                !beginControl(d, 0x80, 6, 0x0200, 0, 9, Phase::ConfigurationHeader)) d.phase = Phase::Disconnect;
        } else if (d.phase == Phase::ConfigurationHeader) {
            const uint16_t length = static_cast<uint16_t>(bytes[2] | (uint16_t{bytes[3]} << 8));
            if (bytes[0] < 9 || bytes[1] != 2 || length < 9 || length > 4096 ||
                !beginControl(d, 0x80, 6, 0x0200, 0, length, Phase::ConfigurationBody)) d.phase = Phase::Disconnect;
        } else if (d.phase == Phase::ConfigurationBody) {
            if ((d.speed == 1 || d.speed == 3 || d.speed == 4) && d.depth < 5 &&
                (d.speed == 4 ? bus.host.PrepareSuperSpeedHubEndpoint != nullptr : bus.host.PrepareHubEndpoint != nullptr) &&
                (bus.host.PrepareAddressTopology || (!d.parentHubSlot && bus.host.PrepareAddressDownstream)) &&
                findUsb2Hub(bytes, d.controlLength, d.hub, d.speed == 4)) {
                d.hubDevice = true;
                d.hubMultiTt = false; // Default alternate setting selects a single TT.
                if (!beginControl(d, 0x00, 9, d.hub.configuration, 0, 0, Phase::SetConfiguration)) d.phase = Phase::Disconnect;
            } else if (selectClass(d, bytes, d.controlLength)) {
                if (!beginControl(d, 0x00, 9, d.interface.Configuration, 0, 0, Phase::SetConfiguration)) d.phase = Phase::Disconnect;
            } else d.phase = Phase::Disconnect;
        } else if (d.phase == Phase::SetConfiguration) {
            if (d.hubDevice) {
                if (d.speed == 4) {
                    if (!beginControl(d, 0x20, 12, d.depth, 0, 0, Phase::HubSetDepth)) d.phase = Phase::Disconnect;
                } else if (!beginControl(d, 0xa0, 6, 0x2900, 0, 9, Phase::HubDescriptor)) d.phase = Phase::Disconnect;
            } else if (d.bulkDevice) {
                if (bus.host.ConfigureBulkEndpoints) {
                    const auto configured = bus.host.ConfigureBulkEndpoints(bus.host.Context, d.slot,
                        d.interface.InEndpoint, d.interface.InPacket,
                        d.interface.OutEndpoint, d.interface.OutPacket, d.speed);
                    if (configured == success) d.phase = Phase::ClassInitialize;
                    else d.phase = Phase::Disconnect;
                    return;
                }
                if ((d.speed >= 4 ?
                    (!d.interface.InCompanion || !d.interface.OutCompanion || !bus.host.PrepareSuperSpeedBulk ?
                        int32_t{-1} : bus.host.PrepareSuperSpeedBulk(bus.host.Context, d.interface.InEndpoint,
                        d.interface.InPacket, d.interface.InBurst, d.interface.OutEndpoint,
                        d.interface.OutPacket, d.interface.OutBurst, d.speed, &d.prepared)) :
                    bus.host.PrepareBulkEndpoints(bus.host.Context, d.interface.InEndpoint,
                        d.interface.InPacket, d.interface.OutEndpoint, d.interface.OutPacket,
                        d.speed, &d.prepared)) != success) d.phase = Phase::Disconnect;
                else d.phase = armDeadline(d) ? Phase::NeedEndpoint : Phase::Quarantined;
            } else if (!beginControl(d, d.interface.DescriptorRequestType, d.interface.DescriptorRequest,
                                     d.interface.DescriptorValue, d.interface.DescriptorIndex,
                                     d.interface.ReportDescriptorLength, Phase::ClassDescriptor)) d.phase = Phase::Disconnect;
        } else if (d.phase == Phase::HubSetDepth) {
            if (!beginControl(d, 0xa0, 6, 0x2a00, 0, 12, Phase::HubDescriptor)) d.phase = Phase::Disconnect;
        } else if (d.phase == Phase::HubDescriptor) {
            const uint16_t characteristics = static_cast<uint16_t>(bytes[3] | (uint16_t{bytes[4]} << 8));
            d.hubPorts = bytes[2]; d.hubThinkTime = d.speed == 3 ? static_cast<uint8_t>((characteristics >> 5) & 3u) : 0;
            d.hubPowerDelay = uint64_t{bytes[5]} * 20'000u;
            if (bytes[0] < (d.speed == 4 ? 12 : 7) || bytes[1] != (d.speed == 4 ? 0x2a : 0x29) ||
                !d.hubPorts || d.hubPorts > 15) { d.phase = Phase::Disconnect; return; }
            const auto hubStatus = d.speed == 4 ?
                bus.host.PrepareSuperSpeedHubEndpoint(bus.host.Context, d.hub.endpoint, d.hub.maxPacket,
                    d.hub.interval, d.speed, d.hubPorts, d.hub.maxBurst, d.hub.bytesPerInterval, &d.prepared) :
                bus.host.PrepareHubEndpoint(bus.host.Context, d.hub.endpoint, d.hub.maxPacket, d.hub.interval,
                    d.speed, d.hubPorts, d.hubThinkTime, d.hubMultiTt, &d.prepared);
            if (hubStatus != success) d.phase = Phase::Disconnect;
            else d.phase = armDeadline(d) ? Phase::NeedEndpoint : Phase::Quarantined;
        } else if (d.phase == Phase::HubPower) {
            if (++d.hubPort <= d.hubPorts) {
                if (!beginControl(d, 0x23, 3, 8, d.hubPort, 0, Phase::HubPower)) d.phase = Phase::Disconnect;
            } else {
                uint64_t now{};
                if (!ke::KeQueryUptime100ns(now) || d.hubPowerDelay > UINT64_MAX - now) d.phase = Phase::Disconnect;
                else { d.hubPort = 1; d.hubNextPoll = now + d.hubPowerDelay; d.deadline = 0; d.phase = Phase::HubIdle; }
            }
        } else if (d.phase == Phase::HubRePower) {
            uint64_t now{};
            if (!ke::KeQueryUptime100ns(now) || d.hubPowerDelay > UINT64_MAX - now) d.phase = Phase::Disconnect;
            else { d.hubNextPoll = now + d.hubPowerDelay; d.deadline = 0; d.phase = Phase::HubIdle; }
        } else if (d.phase == Phase::HubReset) {
            uint64_t now{};
            if (!ke::KeQueryUptime100ns(now) || now > UINT64_MAX - 10'000'000) d.phase = Phase::Disconnect;
            else { d.hubResetDeadline = now + 10'000'000; d.hubNextPoll = now + 1'000'000; d.deadline = 0; d.phase = Phase::HubIdle; }
        } else if (d.phase == Phase::HubPortStatus || d.phase == Phase::HubClearChange) {
            if (d.phase == Phase::HubPortStatus) {
                d.hubStatus = static_cast<uint16_t>(bytes[0] | (uint16_t{bytes[1]} << 8));
                d.hubChanges = static_cast<uint16_t>(bytes[2] | (uint16_t{bytes[3]} << 8));
                d.hubObservedChanges = d.hubChanges;
            } else {
                d.hubChanges &= static_cast<uint16_t>(~(1u << d.hubChangeBit));
            }
            const uint16_t supportedChanges = d.speed == 4 ? 0xf9u : 0x1fu;
            if (d.hubChanges & static_cast<uint16_t>(~supportedChanges)) {
                d.phase = Phase::Disconnect;
                return;
            }
            if (d.hubChanges & supportedChanges) {
                d.hubChangeBit = 0;
                while (!(d.hubChanges & supportedChanges & (1u << d.hubChangeBit))) ++d.hubChangeBit;
                const uint16_t selector = d.speed == 4 && d.hubChangeBit >= 5 ?
                    (d.hubChangeBit == 5 ? 29 : d.hubChangeBit == 6 ? 25 : 26) :
                    static_cast<uint16_t>(16u + d.hubChangeBit);
                if (!beginControl(d, 0x23, 1, selector,
                                  d.hubPort, 0, Phase::HubClearChange)) d.phase = Phase::Disconnect;
                return;
            }
            const uint16_t state = d.hubStatus;
            Device* child{};
            for (auto& other : bus.devices) if (other.phase != Phase::Empty &&
                other.parentHubSlot == d.slot && other.downstreamPort == d.hubPort) { child = &other; break; }
            if (child && (d.hubObservedChanges & 1u)) {
                // A disconnect/reconnect can happen between polls while CCS
                // remains set. Retire the old slot before addressing the new device.
                if (child->phase == Phase::WaitEnable) child->removing = true;
                else if (child->phase != Phase::Quarantined && child->phase != Phase::ClassRemoved && child->phase != Phase::NeedDisable &&
                         child->phase != Phase::WaitDisable && child->phase != Phase::DrainDisabled)
                    markDisconnected(*child);
                if (d.hubResetPort == d.hubPort) d.hubResetPort = 0;
            } else if (!(state & 1u) || (state & 8u)) {
                if (child && child->phase == Phase::WaitEnable) child->removing = true;
                else if (child && child->phase != Phase::Quarantined && child->phase != Phase::ClassRemoved && child->phase != Phase::NeedDisable &&
                         child->phase != Phase::WaitDisable && child->phase != Phase::DrainDisabled) markDisconnected(*child);
                if (d.hubResetPort == d.hubPort) d.hubResetPort = 0;
                if (!(state & 8u) && !(state & (d.speed == 4 ? 0x200u : 0x100u))) {
                    if (!beginControl(d, 0x23, 3, 8, d.hubPort, 0, Phase::HubRePower)) d.phase = Phase::Disconnect;
                    return;
                }
            } else if (!(state & (d.speed == 4 ? 0x200u : 0x100u))) {
                if (!beginControl(d, 0x23, 3, 8, d.hubPort, 0, Phase::HubRePower)) d.phase = Phase::Disconnect;
                return;
            } else if (!child && !d.hubResetPort) {
                d.hubResetPort = d.hubPort;
                if (!beginControl(d, 0x23, 3, 4, d.hubPort, 0, Phase::HubReset)) d.phase = Phase::Disconnect;
                return;
            } else if (!child && d.hubResetPort == d.hubPort) {
                uint64_t now{};
                if (!ke::KeQueryUptime100ns(now)) { d.phase = Phase::Disconnect; return; }
                if ((state & 2u) && !(state & 0x10u)) {
                    for (auto& candidate : bus.devices) if (candidate.phase == Phase::Empty) {
                        candidate.port = d.port; candidate.parentHubSlot = d.slot; candidate.downstreamPort = d.hubPort;
                        candidate.depth = static_cast<uint8_t>(d.depth + 1);
                        candidate.route = d.route | (uint32_t{d.hubPort} << (4u * d.depth));
                        candidate.speed = d.speed == 4 ? 4 : (state & 0x200u) ? 2 : (state & 0x400u) ? 3 : 1;
                        if (candidate.speed < 3) {
                            candidate.ttHubSlot = d.speed == 3 ? d.slot : d.ttHubSlot;
                            candidate.ttPort = d.speed == 3 ? d.hubPort : d.ttPort;
                            candidate.hubMultiTt = d.hubMultiTt;
                        }
                        candidate.phase = armDeadline(candidate) ? Phase::NeedEnable : Phase::Quarantined;
                        d.hubResetPort = 0; break;
                    }
                    if (d.hubResetPort && now >= d.hubResetDeadline) d.hubResetPort = 0;
                } else if (now >= d.hubResetDeadline) d.hubResetPort = 0;
            }
            if (!d.hubResetPort && ++d.hubPort > d.hubPorts) d.hubPort = 1;
            d.hubNextPoll = 0; d.deadline = 0; d.phase = Phase::HubIdle;
        } else if (d.phase == Phase::ClassDescriptor) {
            if (!d.classSlot || !d.classSlot->services.ValidateReportDescriptor ||
                d.classSlot->services.ValidateReportDescriptor(d.classInstance, bytes, d.controlLength) != success ||
                (d.speed >= 4 ?
                    (!d.interface.InCompanion || !bus.host.PrepareSuperSpeedInterrupt ? int32_t{-1} :
                        bus.host.PrepareSuperSpeedInterrupt(bus.host.Context, d.interface.InEndpoint, d.interface.InPacket,
                            d.interface.Interval, d.speed, d.interface.InBurst, d.interface.BytesPerInterval, &d.prepared)) :
                    bus.host.PrepareInterruptEndpoint(bus.host.Context, d.interface.InEndpoint, d.interface.InPacket,
                        d.interface.Interval, d.speed, &d.prepared)) != success) d.phase = Phase::Disconnect;
            else d.phase = armDeadline(d) ? Phase::NeedEndpoint : Phase::Quarantined;
        } else if (d.phase == Phase::ClassControl) {
            d.phase = Phase::ClassInitialize;
        }
        return;
    }
}
void service(void*) noexcept {
    if (!bus.token) return;
    {
        HostGuard guard;
        if (!guard.held) return;
        routeEvents();
        uint64_t now{};
        if (!ke::KeQueryUptime100ns(now)) return;
        if (bus.host.AcknowledgeRootConnectionChange) {
            for (uint16_t port = 1; port <= bus.host.PortCount(bus.host.Context); ++port) {
                uint32_t state{};
                if (bus.host.ReadPort(bus.host.Context, static_cast<uint8_t>(port), &state) != success ||
                    !(state & (1u << 17))) continue;
                bool owned = false;
                for (auto& d : bus.devices) if (d.phase != Phase::Empty && d.port == port) {
                    owned = true;
                    if (d.phase == Phase::WaitEnable) d.removing = true;
                    else if (d.phase != Phase::Quarantined && d.phase != Phase::ClassRemoved && d.phase != Phase::NeedDisable &&
                             d.phase != Phase::WaitDisable && d.phase != Phase::DrainDisabled)
                        markDisconnected(d);
                }
                if (owned) (void)bus.host.AcknowledgeRootConnectionChange(bus.host.Context, static_cast<uint8_t>(port));
            }
        }
        for (auto& d : bus.devices) {
            if (d.phase == Phase::Empty || d.phase == Phase::Quarantined) continue;
            if (d.port && d.phase != Phase::ClassRemoved && d.phase != Phase::Disconnect && d.phase != Phase::NeedDisable &&
                d.phase != Phase::WaitDisable && d.phase != Phase::DrainDisabled) {
                uint32_t portStatus{};
                if (bus.host.ReadPort(bus.host.Context, d.port, &portStatus) != success || !(portStatus & 1u)) {
                    if (d.phase == Phase::WaitEnable) d.removing = true;
                    else markDisconnected(d);
                }
            }
            if (d.phase == Phase::HubIdle && now >= d.hubNextPoll) {
                if (!beginControl(d, 0xa3, 0, 0, d.hubPort, 4, Phase::HubPortStatus))
                    d.phase = Phase::Disconnect;
            }
            if (d.phase != Phase::InterruptRunning && d.phase != Phase::HubIdle && d.phase != Phase::ClassReady &&
                d.phase != Phase::ClassInitialize && d.phase != Phase::ClassRemoving && d.phase != Phase::ClassRemoved && d.phase != Phase::Disconnect &&
                d.deadline && now >= d.deadline) {
                d.phase = d.phase == Phase::WaitDisable || d.phase == Phase::DrainDisabled || d.phase == Phase::WaitEnable
                    ? Phase::Quarantined : Phase::Disconnect;
            }
            process(d);
        }
        Device* free{};
        for (auto& d : bus.devices) if (d.phase == Phase::Empty) { free = &d; break; }
        if (free) {
            const uint8_t count = bus.host.PortCount(bus.host.Context);
            for (uint16_t port = 1; port <= count; ++port) {
                bool owned = false;
                for (const auto& d : bus.devices) if (d.phase != Phase::Empty && d.port == port) { owned = true; break; }
                if (owned) continue;
                uint32_t state{}; uint8_t speed{};
                if (bus.host.ReadPort(bus.host.Context, static_cast<uint8_t>(port), &state) != success || !(state & 1u)) continue;
                const bool asyncReset = bus.host.BeginRootPortReset && bus.host.PollRootPortReset;
                if (asyncReset ? bus.host.BeginRootPortReset(bus.host.Context, static_cast<uint8_t>(port)) != success :
                    bus.host.ResetPort(bus.host.Context, static_cast<uint8_t>(port), &speed) != success) continue;
                free->port = static_cast<uint8_t>(port); free->speed = speed;
                free->phase = armDeadline(*free) ? (asyncReset ? Phase::RootReset : Phase::NeedEnable) : Phase::Quarantined;
                hostLog("USB host: connected root port reset started");
                break;
            }
        }
    }
    // Class Start/Remove can issue synchronous transfers or release block
    // handles. Do not hold hostGate across those callbacks.
    for (auto& d : bus.devices) {
        if (!d.classSlot || !d.classInstance) continue;
        if (d.phase == Phase::ClassInitialize) {
            const auto status = d.classSlot->services.Start(d.classInstance);
            if (status == busy) continue;
            d.deadline = 0;
            d.phase = status == success ?
                (d.interface.Kind == TransferKind::InterruptIn ? Phase::NeedInterrupt : Phase::ClassReady) : Phase::Disconnect;
        } else if (d.phase == Phase::ClassRemoving) {
            const auto status = d.classSlot->services.Remove(d.classInstance);
            if (status == busy) continue;
            if (status != success) { d.phase = Phase::Quarantined; continue; }
            d.phase = Phase::ClassRemoved;
        }
    }

}
}

NtStatus registerClass(const void* owner, const ClassServices& services, uint64_t& token) noexcept {
    token = 0;
    if (!owner || services.Size < sizeof(ClassServices) ||
        services.Version != shared::usb::ClassServicesVersion || !services.Context ||
        !services.MatchConfiguration || !services.Create || !services.Start || !services.Remove ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    HostGuard guard;
    if (!guard.held || !nextClassToken) return NtStatus::DeviceBusy;
    for (auto& slot : classes) if (!slot.token) {
        slot.owner = owner; slot.services = services; slot.token = nextClassToken++; token = slot.token;
        return NtStatus::Success;
    }
    return NtStatus::InsufficientResources;
}
NtStatus unregisterClass(const void* owner, uint64_t token) noexcept {
    if (!owner || !token || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    HostGuard guard;
    if (!guard.held) return NtStatus::DeviceBusy;
    for (auto& slot : classes) if (slot.owner == owner && slot.token == token) {
        for (const auto& d : bus.devices) if (d.classSlot == &slot) return NtStatus::DeviceBusy;
        slot = {}; return NtStatus::Success;
    }
    return NtStatus::InvalidHandle;
}
NtStatus unregisterClassesForOwner(const void* owner) noexcept {
    if (!owner || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    HostGuard guard;
    if (!guard.held) return NtStatus::DeviceBusy;
    for (const auto& slot : classes) if (slot.owner == owner && slot.token)
        for (const auto& d : bus.devices) if (d.classSlot == &slot) return NtStatus::DeviceBusy;
    for (auto& slot : classes) if (slot.owner == owner) slot = {};
    return NtStatus::Success;
}

NtStatus registerHost(const void* owner, const Host& host, uint64_t& token) noexcept {
    token = 0;
    const bool genericHost = host.Size >= sizeof(Host) &&
        host.AddressDevice && host.ConfigureBulkEndpoints;
    const bool xhciHost = host.Size >= offsetof(Host, PrepareSuperSpeedInterrupt) &&
        host.PortSlotType && host.PrepareAddressDevice && host.SubmitCommand &&
        host.PrepareInterruptEndpoint && host.SubmitInterrupt && host.InterruptBuffer &&
        host.PrepareBulkEndpoints;
    if (!owner || host.Size < offsetof(Host, PrepareSuperSpeedInterrupt) ||
        host.Version != shared::usb::HostServicesVersion ||
        !host.Start || !host.Stop || !host.Restart || !host.PortCount || !host.ReadPort ||
        !host.ResetPort || !host.SelectDevice || !host.ForgetDevice || !host.SubmitControl ||
        !host.ControlBuffer || !host.PollEvent || !host.BulkTransfer ||
        !host.BulkResetRecovery || (!genericHost && !xhciHost) ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (bus.token || !nextToken) {
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return NtStatus::DeviceBusy;
    }
    bus.owner = owner; bus.host = {};
    const size_t copied = host.Size >= sizeof(Host) ? sizeof(Host) :
        host.Size >= offsetof(Host, AcknowledgeRootConnectionChange) ? offsetof(Host, AcknowledgeRootConnectionChange) :
        host.Size >= offsetof(Host, BeginRootPortReset) ? offsetof(Host, BeginRootPortReset) :
        host.Size >= offsetof(Host, PrepareSuperSpeedHubEndpoint) ? offsetof(Host, PrepareSuperSpeedHubEndpoint) :
        host.Size >= offsetof(Host, PrepareAddressTopology) ? offsetof(Host, PrepareAddressTopology) :
        host.Size >= offsetof(Host, PrepareHubEndpoint) ? offsetof(Host, PrepareHubEndpoint) :
        offsetof(Host, PrepareSuperSpeedInterrupt);
    std::memcpy(&bus.host, &host, copied);
    bus.token = nextToken++; token = bus.token;
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (host.Start(host.Context) != success) {
        if (host.Stop(host.Context) != success) return NtStatus::DeviceBusy;
        bus.owner = nullptr; bus.token = 0; bus.host = {}; token = 0;
        return NtStatus::DeviceNotReady;
    }
    return NtStatus::Success;
}
NtStatus startEnumeration() noexcept {
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    HostGuard guard;
    if (!guard.held) return NtStatus::DeviceBusy;
    if (!bus.token) return NtStatus::Success;
    if (bus.poll.deadline.inserted || bus.poll.dpc.inserted || bus.poll.work.queued || bus.poll.work.running)
        return NtStatus::DeviceBusy;
    return ke::passive_work::startPeriodic(bus.poll, service, nullptr, 200'000) ?
        NtStatus::Success : NtStatus::DeviceNotReady;
}
NtStatus stopEnumeration() noexcept {
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    HostGuard guard;
    if (!guard.held) return NtStatus::DeviceBusy;
    if (!bus.token) return NtStatus::Success;
    if (!bus.poll.deadline.inserted && !bus.poll.dpc.inserted &&
        !bus.poll.work.queued && !bus.poll.work.running) return NtStatus::Success;
    return ke::passive_work::stopPeriodic(bus.poll) ? NtStatus::Success : NtStatus::DeviceBusy;
}
NtStatus unregisterHost(const void* owner, uint64_t token) noexcept {
    if (!owner || !token || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    if (bus.unregisterGate.test_and_set(std::memory_order_acquire)) return NtStatus::DeviceBusy;
    struct UnregisterGuard { ~UnregisterGuard() { bus.unregisterGate.clear(std::memory_order_release); } } guard;
    if (bus.owner != owner || bus.token != token) return NtStatus::InvalidHandle;
    if (!ke::passive_work::stopPeriodic(bus.poll)) return NtStatus::DeviceBusy;
    for (auto& d : bus.devices) {
        if (!d.classInstance) continue;
        if (d.phase != Phase::ClassRemoved) {
            if (!d.classSlot || d.classSlot->services.Remove(d.classInstance) != success) return NtStatus::DeviceBusy;
            d.phase = Phase::ClassRemoved;
        }
    }
    HostGuard hostGuard;
    if (!hostGuard.held || bus.host.Stop(bus.host.Context) != success) return NtStatus::DeviceBusy;
    bus.owner = nullptr; bus.token = 0; bus.host = {};
    for (auto& d : bus.devices) resetDevice(d);
    return NtStatus::Success;
}
}
