#pragma once

#include <cstddef>
#include <cstdint>
#include "micant/shared/ntdef.hpp"

// ABI-neutral LPC wire structures. This header is shared by Ring 0 and Ring
// 3; it deliberately has no dependency on kernel Executive headers.
namespace micant::lpc {

enum class PortMessageType : uint16_t {
    LpcRequest           = 1,
    LpcReply             = 2,
    LpcDatagram          = 3,
    LpcConnectionRequest = 4,
    LpcClientDied        = 5,
    LpcPortClosed        = 6,
    LpcErrorEvent        = 7,
    LpcConnectionReply   = 8,
    LpcCanceled          = 9
};

struct alignas(8) PortMessage {
    struct {
        uint16_t dataLength{0};
        uint16_t totalLength{sizeof(PortMessage)};
    } u1;
    struct {
        uint16_t type{static_cast<uint16_t>(PortMessageType::LpcRequest)};
        uint16_t dataInfoOffset{0};
    } u2;
    ClientId clientId{};
    uint32_t messageId{0};
    uint32_t alignmentPadding{0};
    union {
        uintptr_t clientViewSize{0};
        uint32_t callbackId;
    };
};

static_assert(offsetof(PortMessage, clientId) == 8);
static_assert(offsetof(PortMessage, messageId) == 24);
static_assert(offsetof(PortMessage, clientViewSize) == 32);
static_assert(sizeof(PortMessage) == 40);

} // namespace micant::lpc
