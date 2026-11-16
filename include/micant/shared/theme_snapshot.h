#pragma once

#include <stdint.h>
#include <stddef.h>

#define MICANT_THEME_SNAPSHOT_MAGIC 0x4D48544Du /* MTHM */
#define MICANT_THEME_SNAPSHOT_VERSION 2u
#define MICANT_THEME_CONTROL_COUNT 15u
#define MICANT_THEME_STATE_COUNT 8u
#define MICANT_THEME_STYLE_COUNT (MICANT_THEME_CONTROL_COUNT * MICANT_THEME_STATE_COUNT)
#define MICANT_THEME_CAPTION_IMAGE_SIZE 16u
#define MICANT_THEME_CAPTION_IMAGE_PIXELS \
    (MICANT_THEME_CAPTION_IMAGE_SIZE * MICANT_THEME_CAPTION_IMAGE_SIZE)
#define MICANT_THEME_CAPTION_IMAGE_COUNT 20u

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
    MICANT_THEME_HAS_FONT_WEIGHT = 1u << 10,
    MICANT_THEME_HAS_FONT_STYLE = 1u << 11,
    MICANT_THEME_HAS_FONT_FAMILY = 1u << 12,
    MICANT_THEME_HAS_BACKDROP_BLUR = 1u << 13,
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
    uint16_t fontWeight;
    uint8_t opacity;
    uint8_t fontItalic;
    uint16_t reserved;
    char fontFamily[32]; /* bounded ASCII family name, empty when inherited */
    uint32_t flags;
} MicaThemeStyleSnapshot;

typedef struct MicaThemeCaptionImage {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t reserved;
} MicaThemeCaptionImage;

/* Separate bounded payload for caption images. This is deliberately not part
   of MicaThemeSnapshot, which remains lightweight style metadata. */
typedef struct MicaThemeCaptionImageSet {
    uint64_t generation;
    MicaThemeCaptionImage images[MICANT_THEME_CAPTION_IMAGE_COUNT];
    uint32_t pixels[MICANT_THEME_CAPTION_IMAGE_COUNT]
                 [MICANT_THEME_CAPTION_IMAGE_PIXELS]; /* premultiplied BGRA */
} MicaThemeCaptionImageSet;

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
static_assert(sizeof(MicaThemeStyleSnapshot) == 72);
static_assert(sizeof(MicaThemeCaptionImage) == 8);
static_assert(sizeof(MicaThemeSnapshot) ==
              24 + sizeof(MicaThemeStyleSnapshot) * MICANT_THEME_STYLE_COUNT);
static_assert(sizeof(MicaThemeCaptionImageSet) == 8 +
              MICANT_THEME_CAPTION_IMAGE_COUNT * sizeof(MicaThemeCaptionImage) +
              MICANT_THEME_CAPTION_IMAGE_COUNT * MICANT_THEME_CAPTION_IMAGE_PIXELS * 4u);

inline bool MicaThemeSnapshotIsValid(const MicaThemeSnapshot* snapshot,
                                     size_t byteLength) noexcept {
    constexpr uint32_t knownFlags = (1u << 14) - 1u;
    if (!snapshot || byteLength != sizeof(MicaThemeSnapshot) ||
        snapshot->magic != MICANT_THEME_SNAPSHOT_MAGIC ||
        snapshot->version != MICANT_THEME_SNAPSHOT_VERSION ||
        snapshot->headerSize != offsetof(MicaThemeSnapshot, styles) ||
        snapshot->totalSize != sizeof(MicaThemeSnapshot) ||
        snapshot->controlCount != MICANT_THEME_CONTROL_COUNT ||
        snapshot->stateCount != MICANT_THEME_STATE_COUNT) return false;
    for (const auto& style : snapshot->styles) {
        if (style.flags & ~knownFlags || style.reserved || style.fontItalic > 1) return false;
        if ((style.flags & MICANT_THEME_HAS_FONT_WEIGHT) &&
            (style.fontWeight < 1 || style.fontWeight > 1000)) return false;
        if (!(style.flags & MICANT_THEME_HAS_FONT_STYLE) && style.fontItalic) return false;
        bool familyTerminated = false;
        for (char character : style.fontFamily) {
            if (!character) familyTerminated = true;
            else if (familyTerminated || !((character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') || character == ' ' ||
                character == '-' || character == '_')) return false;
        }
        if ((style.flags & MICANT_THEME_HAS_FONT_FAMILY) ?
            (!style.fontFamily[0] || !familyTerminated) : style.fontFamily[0] != 0) return false;
    }
    return true;
}

inline bool MicaThemeCaptionImageSetIsValid(const MicaThemeCaptionImageSet* set,
                                           size_t byteLength) noexcept {
    if (!set || byteLength != sizeof(MicaThemeCaptionImageSet)) return false;
    for (size_t index = 0; index < MICANT_THEME_CAPTION_IMAGE_COUNT; ++index) {
        const auto& image = set->images[index];
        if (image.reserved || image.width > MICANT_THEME_CAPTION_IMAGE_SIZE ||
            image.height > MICANT_THEME_CAPTION_IMAGE_SIZE ||
            (image.width == 0) != (image.height == 0) ||
            (image.width && (image.stride < image.width ||
                             image.stride > MICANT_THEME_CAPTION_IMAGE_SIZE)) ||
            (!image.width && image.stride)) return false;
        for (uint32_t y = 0; y < MICANT_THEME_CAPTION_IMAGE_SIZE; ++y) {
            for (uint32_t x = 0; x < MICANT_THEME_CAPTION_IMAGE_SIZE; ++x) {
                const uint32_t pixel = set->pixels[index][
                    y * MICANT_THEME_CAPTION_IMAGE_SIZE + x];
                if (x < image.width && y < image.height) {
                    const uint32_t alpha = pixel >> 24;
                    if (((pixel >> 16) & 0xFFu) > alpha ||
                        ((pixel >> 8) & 0xFFu) > alpha || (pixel & 0xFFu) > alpha)
                        return false;
                } else if (pixel) {
                    return false;
                }
            }
        }
    }
    return true;
}
#endif
