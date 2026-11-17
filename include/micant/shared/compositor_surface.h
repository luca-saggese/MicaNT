#pragma once

#include <stdint.h>
#include "micant/shared/theme_snapshot.h"

#define MICANT_COMPOSITOR_STORE_MAGIC 0x4D434F4Du /* MCOM */
#define MICANT_COMPOSITOR_STORE_VERSION 14u

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
    uint64_t pixelsAddress;
    uint64_t pixelSizeBytes;
} MicaCompositorCursorInfo;

typedef struct MicaCompositorSurfaceInfo {
    uint64_t window;
    uint64_t parentWindow;
    uint64_t ownerProcessId;
    int32_t x, y;
    uint32_t width, height, pitch;
    uint32_t zOrder;
    uint32_t flags;
    uint32_t surfaceSourceX;
    uint32_t surfaceSourceY;
    uint64_t surfaceAddress;
    uint64_t surfaceSizeBytes;
    int32_t dirtyLeft, dirtyTop, dirtyRight, dirtyBottom;
    uint64_t contentGeneration;
    uint32_t activeWriters;
    uint32_t style;
    uint32_t animationFlags;
    MicaCompositorDirtyRect animationTarget;
    uint64_t animationGeneration;
    uint32_t iconWidth, iconHeight, iconStride;
    uint64_t iconGeneration;
    uint64_t iconAddress;
    uint64_t iconSizeBytes;
    uint32_t titleWidth, titleHeight, titleStride;
    uint64_t titleGeneration;
    uint64_t titleAddress;
    uint64_t titleSizeBytes;
} MicaCompositorSurfaceInfo;

typedef struct MicaCompositorMetadataStore {
    uint32_t magic;
    uint32_t version;
    uint32_t screenWidth, screenHeight;
    uint32_t surfaceEntryCount;
    uint32_t reserved[3];
    uint64_t themeGeneration;
    MicaThemeSnapshot theme;
    MicaThemeCaptionImageSet captionImageSet;
    uint64_t frameSequence;
    MicaCompositorSurfaceInfo* surfaces;
    uint64_t surfaceArraySizeBytes;
    MicaCompositorCursorInfo cursor;
    uint64_t compositorAckSequence;
    uint32_t dirtyRegionCount;
    uint32_t dirtyFlags;
    MicaCompositorDirtyRect* dirtyRegions;
    uint64_t dirtyRegionArraySizeBytes;
} MicaCompositorMetadataStore;

#ifdef __cplusplus
static_assert(sizeof(MicaCompositorDirtyRect) == 16);
static_assert(sizeof(MicaCompositorSurfaceInfo) == 224);
#endif
