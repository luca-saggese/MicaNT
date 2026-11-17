#pragma once

#include <stdint.h>

#define MICANT_LIQUID_GLASS_MAGIC 0x5341474Cu /* LGAS */
#define MICANT_LIQUID_GLASS_VERSION 1u

enum {
    MICANT_LIQUID_GLASS_ACTIVE = 0x01u
};

/* Mode-neutral descriptor published by Win32k and consumed by the Ring 3
 * compositor. The pixel renderer stays in Ring 3; Win32k owns validation,
 * lifetime and invalidation only. All color channels are premultiplied BGRA.
 */
typedef struct MicaLiquidGlassDescriptor {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t flags;
    int32_t left;
    int32_t top;
    uint32_t width;
    uint32_t height;
    float radius;
    float bezel;
    float thickness;
    float dispersion;
    float blurPx;
    float tintMix;
    float saturation;
    float dark;
    float sourceBlur;
    float downsample;
    float pad;
    float rim;
    float grain;
    float magnify;
    float shadowAmt;
    float rimTint;
    float ringW;
    float flatInner;
    uint64_t generation;
} MicaLiquidGlassDescriptor;

#if defined(__cplusplus)
static_assert(sizeof(MicaLiquidGlassDescriptor) == 112);
#endif
