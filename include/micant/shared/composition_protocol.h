#pragma once

/*
 * Versioned, mode-neutral request records for the future Win32k/compositor
 * composition channel. These records contain opaque IDs and values only:
 * never a kernel virtual address or a user pointer. Defining this ABI does
 * not by itself create an IPC endpoint or grant access to any object.
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICANT_COMPOSITION_PROTOCOL_VERSION 1u
#define MICANT_COMPOSITION_MAX_DIMENSION 16384u
#define MICANT_COMPOSITION_MAX_SURFACE_BYTES (256u * 1024u * 1024u)
#define MICANT_COMPOSITION_MAX_WAIT_MILLISECONDS 60000u

enum {
    MICANT_COMPOSITION_FORMAT_BGRA8 = 1u,
    MICANT_COMPOSITION_OP_CREATE_SURFACE = 1u,
    MICANT_COMPOSITION_OP_RELEASE_SURFACE = 2u,
    MICANT_COMPOSITION_OP_ATTACH_WINDOW_SURFACE = 3u,
    MICANT_COMPOSITION_OP_DETACH_WINDOW_SURFACE = 4u,
    MICANT_COMPOSITION_OP_COMMIT = 5u,
    MICANT_COMPOSITION_OP_WAIT_FOR_FRAME = 6u,
    MICANT_COMPOSITION_OP_ACQUIRE_WINDOW_SURFACE = 7u
};

enum {
    MICANT_COMPOSITION_FLAG_NONE = 0u,
    MICANT_COMPOSITION_ATTACH_VISIBLE = 0x1u,
    MICANT_COMPOSITION_ATTACH_OPAQUE = 0x2u,
    MICANT_COMPOSITION_ATTACH_VALID_FLAGS = 0x3u
};

typedef struct MicaCompositionHandle {
    uint32_t index;
    uint32_t generation;
} MicaCompositionHandle;

typedef struct MicaCompositionRequestHeader {
    uint32_t size;
    uint32_t version;
    uint32_t operation;
    uint32_t flags;
    uint64_t requestSequence;
} MicaCompositionRequestHeader;

/* Fixed-size in/out packet. outputHandle must be zero on input. */
typedef struct MicaCompositionCreateSurfaceRequest {
    MicaCompositionRequestHeader header;
    uint32_t width;
    uint32_t height;
    uint32_t strideBytes;
    uint32_t pixelFormat;
    MicaCompositionHandle outputHandle;
} MicaCompositionCreateSurfaceRequest;

typedef struct MicaCompositionReleaseSurfaceRequest {
    MicaCompositionRequestHeader header;
    MicaCompositionHandle surface;
    uint32_t reserved;
} MicaCompositionReleaseSurfaceRequest;

/* windowId is an opaque, server-validated ID, not an HWND/kernel pointer. */
typedef struct MicaCompositionAttachWindowSurfaceRequest {
    MicaCompositionRequestHeader header;
    uint64_t windowId;
    MicaCompositionHandle surface;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t zOrder;
    uint32_t attachFlags;
} MicaCompositionAttachWindowSurfaceRequest;

typedef struct MicaCompositionDetachWindowSurfaceRequest {
    MicaCompositionRequestHeader header;
    uint64_t windowId;
} MicaCompositionDetachWindowSurfaceRequest;

/* transaction is an opaque server-issued transaction handle. */
typedef struct MicaCompositionCommitRequest {
    MicaCompositionRequestHeader header;
    MicaCompositionHandle transaction;
    uint64_t outputFrameSequence;
} MicaCompositionCommitRequest;

typedef struct MicaCompositionWaitForFrameRequest {
    MicaCompositionRequestHeader header;
    uint64_t frameSequence;
    uint32_t timeoutMilliseconds;
    uint32_t reserved;
} MicaCompositionWaitForFrameRequest;

/*
 * Resolves an already-created, caller-owned Win32k window surface to an
 * opaque generation-checked token. The token grants no extra access and
 * contains no address; the current process must still own the window.
 */
typedef struct MicaCompositionAcquireWindowSurfaceRequest {
    MicaCompositionRequestHeader header;
    uint64_t windowId;
    MicaCompositionHandle outputHandle;
    uint64_t contentGeneration;
    uint32_t width;
    uint32_t height;
    uint32_t strideBytes;
    uint32_t pixelFormat;
} MicaCompositionAcquireWindowSurfaceRequest;

static inline int MicaCompositionHandleIsValid(MicaCompositionHandle handle) {
    return handle.index != 0u && handle.generation != 0u;
}

static inline int MicaCompositionValidateSurfaceShape(uint32_t width,
                                                       uint32_t height,
                                                       uint32_t strideBytes,
                                                       uint32_t pixelFormat) {
    uint64_t rowBytes;
    uint64_t totalBytes;
    if (width == 0u || height == 0u ||
        width > MICANT_COMPOSITION_MAX_DIMENSION ||
        height > MICANT_COMPOSITION_MAX_DIMENSION ||
        pixelFormat != MICANT_COMPOSITION_FORMAT_BGRA8) {
        return 0;
    }
    rowBytes = (uint64_t)width * 4u;
    if ((uint64_t)strideBytes < rowBytes || strideBytes % 4u != 0u ||
        (uint64_t)strideBytes > MICANT_COMPOSITION_MAX_SURFACE_BYTES / height) {
        return 0;
    }
    totalBytes = (uint64_t)strideBytes * height;
    return totalBytes <= MICANT_COMPOSITION_MAX_SURFACE_BYTES;
}

static inline int MicaCompositionValidateRequestHeader(
    const MicaCompositionRequestHeader* header,
    uint32_t expectedOperation,
    uint32_t expectedSize) {
    return header != 0 && header->size == expectedSize &&
           header->version == MICANT_COMPOSITION_PROTOCOL_VERSION &&
           header->operation == expectedOperation && header->flags == 0u &&
           header->requestSequence != 0u;
}

static inline int MicaCompositionValidateCreateSurfaceRequest(
    const MicaCompositionCreateSurfaceRequest* request) {
    MicaCompositionHandle empty = {0u, 0u};
    if (request == 0 || !MicaCompositionValidateRequestHeader(
            &request->header, MICANT_COMPOSITION_OP_CREATE_SURFACE,
            (uint32_t)sizeof(*request)) ||
        request->outputHandle.index != empty.index ||
        request->outputHandle.generation != empty.generation) {
        return 0;
    }
    return MicaCompositionValidateSurfaceShape(request->width, request->height,
        request->strideBytes, request->pixelFormat);
}

static inline int MicaCompositionValidateReleaseSurfaceRequest(
    const MicaCompositionReleaseSurfaceRequest* request) {
    return request != 0 && MicaCompositionValidateRequestHeader(
               &request->header, MICANT_COMPOSITION_OP_RELEASE_SURFACE,
               (uint32_t)sizeof(*request)) &&
           MicaCompositionHandleIsValid(request->surface) &&
           request->reserved == 0u;
}

static inline int MicaCompositionValidateAttachWindowSurfaceRequest(
    const MicaCompositionAttachWindowSurfaceRequest* request) {
    return request != 0 && MicaCompositionValidateRequestHeader(
               &request->header, MICANT_COMPOSITION_OP_ATTACH_WINDOW_SURFACE,
               (uint32_t)sizeof(*request)) &&
           request->windowId != 0u &&
           MicaCompositionHandleIsValid(request->surface) &&
           request->width != 0u && request->height != 0u &&
           request->width <= MICANT_COMPOSITION_MAX_DIMENSION &&
           request->height <= MICANT_COMPOSITION_MAX_DIMENSION &&
           (int64_t)request->x + request->width <= INT32_MAX &&
           (int64_t)request->y + request->height <= INT32_MAX &&
           (request->attachFlags & ~MICANT_COMPOSITION_ATTACH_VALID_FLAGS) == 0u;
}

static inline int MicaCompositionValidateDetachWindowSurfaceRequest(
    const MicaCompositionDetachWindowSurfaceRequest* request) {
    return request != 0 && MicaCompositionValidateRequestHeader(
               &request->header, MICANT_COMPOSITION_OP_DETACH_WINDOW_SURFACE,
               (uint32_t)sizeof(*request)) && request->windowId != 0u;
}

static inline int MicaCompositionValidateCommitRequest(
    const MicaCompositionCommitRequest* request) {
    return request != 0 && MicaCompositionValidateRequestHeader(
               &request->header, MICANT_COMPOSITION_OP_COMMIT,
               (uint32_t)sizeof(*request)) &&
           MicaCompositionHandleIsValid(request->transaction) &&
           request->outputFrameSequence == 0u;
}

static inline int MicaCompositionValidateWaitForFrameRequest(
    const MicaCompositionWaitForFrameRequest* request) {
    return request != 0 && MicaCompositionValidateRequestHeader(
               &request->header, MICANT_COMPOSITION_OP_WAIT_FOR_FRAME,
               (uint32_t)sizeof(*request)) && request->frameSequence != 0u &&
           request->timeoutMilliseconds <= MICANT_COMPOSITION_MAX_WAIT_MILLISECONDS &&
           request->reserved == 0u;
}

static inline int MicaCompositionValidateAcquireWindowSurfaceRequest(
    const MicaCompositionAcquireWindowSurfaceRequest* request) {
    return request != 0 && MicaCompositionValidateRequestHeader(
               &request->header, MICANT_COMPOSITION_OP_ACQUIRE_WINDOW_SURFACE,
               (uint32_t)sizeof(*request)) && request->windowId != 0u &&
           request->outputHandle.index == 0u &&
           request->outputHandle.generation == 0u &&
           request->contentGeneration == 0u && request->width == 0u &&
           request->height == 0u && request->strideBytes == 0u &&
           request->pixelFormat == 0u;
}

#ifdef __cplusplus
} /* extern "C" */

static_assert(sizeof(MicaCompositionHandle) == 8);
static_assert(sizeof(MicaCompositionRequestHeader) == 24);
static_assert(offsetof(MicaCompositionRequestHeader, requestSequence) == 16);
static_assert(sizeof(MicaCompositionCreateSurfaceRequest) == 48);
static_assert(sizeof(MicaCompositionReleaseSurfaceRequest) == 40);
static_assert(sizeof(MicaCompositionAttachWindowSurfaceRequest) == 64);
static_assert(sizeof(MicaCompositionDetachWindowSurfaceRequest) == 32);
static_assert(sizeof(MicaCompositionCommitRequest) == 40);
static_assert(sizeof(MicaCompositionWaitForFrameRequest) == 40);
static_assert(sizeof(MicaCompositionAcquireWindowSurfaceRequest) == 64);
#endif
