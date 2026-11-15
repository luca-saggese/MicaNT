#pragma once

#include <stdint.h>
#include "micant/shared/theme_snapshot.h"

#define MICANT_COMPOSITOR_SCREEN_WIDTH 1024u
#define MICANT_COMPOSITOR_SCREEN_HEIGHT 768u
#define MICANT_COMPOSITOR_SURFACE_CAPACITY 16u
#define MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY 16u
#define MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION 64u
#define MICANT_COMPOSITOR_CURSOR_MAX_PIXELS \
    (MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION * MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION)
#define MICANT_COMPOSITOR_SURFACE_PIXELS \
    (MICANT_COMPOSITOR_SCREEN_WIDTH * MICANT_COMPOSITOR_SCREEN_HEIGHT)
#define MICANT_COMPOSITOR_PIXEL_STORE_SIZE \
    ((uint64_t)MICANT_COMPOSITOR_SURFACE_CAPACITY * MICANT_COMPOSITOR_SURFACE_PIXELS * 4u)
#define MICANT_COMPOSITOR_METADATA_STORE_SIZE 0x10000u
/* Keep the shared views above the first image and bootstrap stack allocations. */
#define MICANT_COMPOSITOR_METADATA_BASE (((uint64_t)133u << 39) + 0x10000000u)
#define MICANT_COMPOSITOR_PIXELS_BASE (MICANT_COMPOSITOR_METADATA_BASE + 0x10000u)

#define MICANT_COMPOSITOR_STORE_MAGIC 0x4D434F4Du /* MCOM */
#define MICANT_COMPOSITOR_STORE_VERSION 4u

enum {
    MICANT_COMPOSITOR_SURFACE_ACTIVE = 0x01u,
    MICANT_COMPOSITOR_SURFACE_VISIBLE = 0x02u,
    MICANT_COMPOSITOR_SURFACE_ICONIC = 0x04u,
    MICANT_COMPOSITOR_SURFACE_FOREGROUND = 0x08u,
    MICANT_COMPOSITOR_SURFACE_MAXIMIZED = 0x10u,
    MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_PRESSED = 0x20u,
    MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_HOVER = 0x40u,
    MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_SHIFT = 8u,
    MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_MASK = 0x300u,
    MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_SHIFT = 10u,
    MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_MASK = 0xC00u,
    MICANT_COMPOSITOR_CURSOR_ACTIVE = 0x01u,
    MICANT_COMPOSITOR_CURSOR_VISIBLE = 0x02u,
    MICANT_COMPOSITOR_DIRTY_FULL = 0x01u,
    MICANT_COMPOSITOR_ANIMATION_ACTIVE = 0x01u,
    MICANT_COMPOSITOR_ANIMATION_MINIMIZE = 0x02u
};

typedef struct MicaCompositorDirtyRect {
    int32_t left, top, right, bottom;
} MicaCompositorDirtyRect;

typedef struct MicaCompositorCursorInfo {
    int32_t x, y;
    uint32_t width, height, pitch;
    uint32_t hotX, hotY;
    uint32_t flags;
    uint32_t pixels[MICANT_COMPOSITOR_CURSOR_MAX_PIXELS];
} MicaCompositorCursorInfo;

typedef struct MicaCompositorSurfaceInfo {
    uint64_t window;
    uint64_t ownerProcessId;
    int32_t x, y;
    uint32_t width, height, pitch;
    uint32_t zOrder;
    uint32_t flags;
    uint32_t pixelOffset;
    int32_t dirtyLeft, dirtyTop, dirtyRight, dirtyBottom;
    uint64_t contentGeneration;
    uint32_t activeWriters;
    uint32_t style;
    uint32_t animationFlags;
    MicaCompositorDirtyRect animationTarget;
    uint64_t animationGeneration;
    uint16_t title[64];
} MicaCompositorSurfaceInfo;

typedef struct MicaCompositorMetadataStore {
    uint32_t magic;
    uint32_t version;
    uint32_t screenWidth, screenHeight;
    uint32_t surfaceCapacity;
    uint32_t reserved[3];
    uint64_t themeGeneration;
    MicaThemeSnapshot theme;
    uint64_t frameSequence;
    MicaCompositorSurfaceInfo surfaces[MICANT_COMPOSITOR_SURFACE_CAPACITY];
    MicaCompositorCursorInfo cursor;
    uint64_t compositorAckSequence;
    uint32_t dirtyRegionCount;
    uint32_t dirtyFlags;
    MicaCompositorDirtyRect dirtyRegions[MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY];
} MicaCompositorMetadataStore;

#ifdef __cplusplus
static_assert(sizeof(MicaCompositorDirtyRect) == 16);
static_assert(sizeof(MicaCompositorSurfaceInfo) == 240);
static_assert(sizeof(MicaCompositorMetadataStore) <= MICANT_COMPOSITOR_METADATA_STORE_SIZE);
#endif
