#include "micant/shared/composition_protocol.h"

#include <cassert>
#include <cstdint>

int main() {
    MicaCompositionCreateSurfaceRequest create{};
    create.header.size = sizeof(create);
    create.header.version = MICANT_COMPOSITION_PROTOCOL_VERSION;
    create.header.operation = MICANT_COMPOSITION_OP_CREATE_SURFACE;
    create.header.requestSequence = 1;
    create.width = 640;
    create.height = 480;
    create.strideBytes = 640u * 4u;
    create.pixelFormat = MICANT_COMPOSITION_FORMAT_BGRA8;

    assert(MicaCompositionValidateRequestHeader(
        &create.header, MICANT_COMPOSITION_OP_CREATE_SURFACE, sizeof(create)));
    assert(MicaCompositionValidateSurfaceShape(create.width, create.height,
        create.strideBytes, create.pixelFormat));
    assert(MicaCompositionValidateCreateSurfaceRequest(&create));
    assert(!MicaCompositionHandleIsValid(create.outputHandle));

    create.header.size--;
    assert(!MicaCompositionValidateRequestHeader(
        &create.header, MICANT_COMPOSITION_OP_CREATE_SURFACE, sizeof(create)));
    create.header.size = sizeof(create);
    create.header.flags = 1;
    assert(!MicaCompositionValidateRequestHeader(
        &create.header, MICANT_COMPOSITION_OP_CREATE_SURFACE, sizeof(create)));

    assert(!MicaCompositionValidateSurfaceShape(0, 1, 4,
        MICANT_COMPOSITION_FORMAT_BGRA8));
    assert(!MicaCompositionValidateSurfaceShape(
        MICANT_COMPOSITION_MAX_DIMENSION + 1u, 1, 4,
        MICANT_COMPOSITION_FORMAT_BGRA8));
    assert(!MicaCompositionValidateSurfaceShape(4, 4, 12,
        MICANT_COMPOSITION_FORMAT_BGRA8));
    assert(!MicaCompositionValidateSurfaceShape(4, 4, 16,
        MICANT_COMPOSITION_FORMAT_BGRA8 + 1u));
    assert(!MicaCompositionValidateSurfaceShape(16384, 16384, 65536,
        MICANT_COMPOSITION_FORMAT_BGRA8));

    MicaCompositionAttachWindowSurfaceRequest attach{};
    attach.header = {sizeof(attach), MICANT_COMPOSITION_PROTOCOL_VERSION,
        MICANT_COMPOSITION_OP_ATTACH_WINDOW_SURFACE, 0u, 2u};
    attach.windowId = 1;
    attach.surface = {3u, 7u};
    attach.x = INT32_MAX - 1;
    attach.y = -20;
    attach.width = 4;
    attach.height = 20;
    attach.attachFlags = MICANT_COMPOSITION_ATTACH_VISIBLE;
    assert(!MicaCompositionValidateAttachWindowSurfaceRequest(&attach));
    attach.x = 10;
    assert(MicaCompositionValidateAttachWindowSurfaceRequest(&attach));
    attach.attachFlags = 0x80000000u;
    assert(!MicaCompositionValidateAttachWindowSurfaceRequest(&attach));

    MicaCompositionAcquireWindowSurfaceRequest acquire{};
    acquire.header = {sizeof(acquire), MICANT_COMPOSITION_PROTOCOL_VERSION,
        MICANT_COMPOSITION_OP_ACQUIRE_WINDOW_SURFACE, 0u, 3u};
    acquire.windowId = 9;
    assert(MicaCompositionValidateAcquireWindowSurfaceRequest(&acquire));
    acquire.outputHandle = {2u, 4u};
    assert(!MicaCompositionValidateAcquireWindowSurfaceRequest(&acquire));

    assert(MicaCompositionHandleIsValid({1u, 1u}));
    assert(!MicaCompositionHandleIsValid({1u, 0u}));
    assert(!MicaCompositionHandleIsValid({0u, 1u}));
    return 0;
}
