#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::shared::usb {
inline constexpr uint32_t HostServicesVersion = 1;
struct Trb { uint64_t parameter; uint32_t status, control; };
struct HostServicesV1 {
    uint32_t Size, Version;
    void* Context;
    int32_t (*Start)(void*);
    int32_t (*Stop)(void*);
    int32_t (*Restart)(void*);
    uint8_t (*PortCount)(void*);
    int32_t (*ReadPort)(void*, uint8_t, uint32_t*);
    int32_t (*ResetPort)(void*, uint8_t, uint8_t*);
    uint8_t (*PortSlotType)(void*, uint8_t);
    int32_t (*PrepareAddressDevice)(void*, uint8_t, uint8_t, uint8_t, Trb*);
    int32_t (*SelectDevice)(void*, uint8_t);
    int32_t (*ForgetDevice)(void*, uint8_t);
    int32_t (*SubmitCommand)(void*, const Trb*, uint64_t*);
    int32_t (*SubmitControl)(void*, uint64_t, uint16_t, bool, uint64_t*);
    const uint8_t* (*ControlBuffer)(void*);
    int32_t (*PollEvent)(void*, Trb*, bool*);
    int32_t (*PrepareInterruptEndpoint)(void*, uint8_t, uint16_t, uint8_t, uint8_t, Trb*);
    int32_t (*SubmitInterrupt)(void*, uint64_t*);
    const uint8_t* (*InterruptBuffer)(void*);
    int32_t (*PrepareBulkEndpoints)(void*, uint8_t, uint16_t, uint8_t, uint16_t,
                                    uint8_t, Trb*);
    int32_t (*BulkTransfer)(void*, uint8_t, void*, size_t, size_t*, uint32_t);
    int32_t (*BulkResetRecovery)(void*, uint8_t, uint8_t, uint8_t);
    // Optional tail: SuperSpeed endpoint companion parameters.
    int32_t (*PrepareSuperSpeedInterrupt)(void*, uint8_t, uint16_t, uint8_t, uint8_t,
                                          uint8_t, uint16_t, Trb*);
    int32_t (*PrepareSuperSpeedBulk)(void*, uint8_t, uint16_t, uint8_t, uint8_t, uint16_t,
                                    uint8_t, uint8_t, Trb*);
    // Optional USB2 hub topology support, append-only after the V1 prefix.
    int32_t (*PrepareHubEndpoint)(void*, uint8_t, uint16_t, uint8_t, uint8_t,
                                  uint8_t, uint8_t, bool, Trb*);
    int32_t (*PrepareAddressDownstream)(void*, uint8_t, uint8_t, uint8_t, uint8_t,
                                       uint8_t, bool, Trb*);
    int32_t (*PrepareAddressTopology)(void*, uint8_t, uint8_t, uint8_t, uint32_t,
                                     uint8_t, uint8_t, bool, Trb*);
    int32_t (*PrepareEp0Packet)(void*, uint16_t, Trb*);
    int32_t (*PrepareSuperSpeedHubEndpoint)(void*, uint8_t, uint16_t, uint8_t, uint8_t,
                                             uint8_t, uint8_t, uint16_t, Trb*);
    int32_t (*BeginRootPortReset)(void*, uint8_t);
    int32_t (*PollRootPortReset)(void*, uint8_t, uint8_t*);
    int32_t (*AcknowledgeRootConnectionChange)(void*, uint8_t);
    // Optional diagnostic sink for bounded controller/bus lifecycle markers.
    void (*Log)(void*, const char*);
    // Optional controller-neutral path for EHCI and future host controllers.
    // AddressDevice performs the USB address-zero descriptor read and SET_ADDRESS;
    // the returned slot is the bus driver's opaque device identifier.
    int32_t (*AddressDevice)(void*, uint8_t, uint8_t, uint8_t*);
    int32_t (*ConfigureBulkEndpoints)(void*, uint8_t, uint8_t, uint16_t,
                                      uint8_t, uint16_t, uint8_t);
};
}
