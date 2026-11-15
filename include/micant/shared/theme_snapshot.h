#pragma once

#include <stdint.h>
#include <stddef.h>

#define MICANT_THEME_SNAPSHOT_MAGIC 0x4D48544Du /* MTHM */
#define MICANT_THEME_SNAPSHOT_VERSION 1u
#define MICANT_THEME_CONTROL_COUNT 15u
#define MICANT_THEME_STATE_COUNT 8u
#define MICANT_THEME_STYLE_COUNT (MICANT_THEME_CONTROL_COUNT * MICANT_THEME_STATE_COUNT)

enum MicaThemeStyleFlags {
    MICANT_THEME_HAS_FOREGROUND = 1u << 0,
    MICANT_THEME_HAS_BACKGROUND = 1u << 1,
    MICANT_THEME_HAS_BORDER = 1u << 2,
    MICANT_THEME_HAS_RADIUS = 1u << 3,
    MICANT_THEME_HAS_BORDER_WIDTH = 1u << 4,
    MICANT_THEME_HAS_MIN_WIDTH = 1u << 5,
    MICANT_THEME_HAS_MIN_HEIGHT = 1u << 6,
    MICANT_THEME_HAS_PADDING = 1u << 7,
    MICANT_THEME_HAS_FONT_SIZE = 1u << 8,
    MICANT_THEME_HAS_OPACITY = 1u << 9,
};

typedef struct MicaThemeStyleSnapshot {
    uint32_t foreground; /* 0xAARRGGBB */
    uint32_t background;
    uint32_t border;
    uint16_t borderRadius;
    uint16_t borderWidth;
    uint16_t minWidth;
    uint16_t minHeight;
    uint16_t padding[4]; /* top, right, bottom, left */
    uint16_t fontSize;
    uint8_t opacity;
    uint8_t reserved;
    uint32_t flags;
} MicaThemeStyleSnapshot;

typedef struct MicaThemeSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t totalSize;
    uint16_t controlCount;
    uint16_t stateCount;
    uint32_t unsupportedRules;
    uint32_t unsupportedProperties;
    MicaThemeStyleSnapshot styles[MICANT_THEME_STYLE_COUNT];
} MicaThemeSnapshot;

#ifdef __cplusplus
static_assert(sizeof(MicaThemeStyleSnapshot) == 36);
static_assert(sizeof(MicaThemeSnapshot) ==
              24 + sizeof(MicaThemeStyleSnapshot) * MICANT_THEME_STYLE_COUNT);

inline bool MicaThemeSnapshotIsValid(const MicaThemeSnapshot* snapshot,
                                     size_t byteLength) noexcept {
    constexpr uint32_t knownFlags = (1u << 10) - 1u;
    if (!snapshot || byteLength != sizeof(MicaThemeSnapshot) ||
        snapshot->magic != MICANT_THEME_SNAPSHOT_MAGIC ||
        snapshot->version != MICANT_THEME_SNAPSHOT_VERSION ||
        snapshot->headerSize != offsetof(MicaThemeSnapshot, styles) ||
        snapshot->totalSize != sizeof(MicaThemeSnapshot) ||
        snapshot->controlCount != MICANT_THEME_CONTROL_COUNT ||
        snapshot->stateCount != MICANT_THEME_STATE_COUNT) return false;
    for (const auto& style : snapshot->styles)
        if (style.flags & ~knownFlags) return false;
    return true;
}
#endif
