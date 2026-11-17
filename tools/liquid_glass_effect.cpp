#include "liquid_glass_effect.hpp"
#include "micant/shared/pixel_composite.h"
#include <stddef.h>

namespace micant::compositor::glass {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr float kGoldenAngle = 2.39996323f;
constexpr uint32_t kBackdropMargin = Cache::BackdropMargin;

float minf(float a, float b) noexcept { return a < b ? a : b; }
float maxf(float a, float b) noexcept { return a > b ? a : b; }
float clampf(float v, float lo, float hi) noexcept {
    return minf(maxf(v, lo), hi);
}
float fract(float v) noexcept { return v - static_cast<int32_t>(v); }
float mixf(float a, float b, float t) noexcept { return a + (b - a) * t; }

float sqrtApprox(float value) noexcept {
    if (value <= 0.0f) return 0.0f;
    float result{};
    __asm__ __volatile__("sqrtss %1, %0" : "=x"(result) : "x"(value));
    return result;
}

float expNegative(float value) noexcept {
    if (value <= 0.0f) return 1.0f;
    if (value >= 40.0f) return 0.0f;
    constexpr float ln2 = 0.69314718056f;
    uint32_t whole = static_cast<uint32_t>(value / ln2);
    float x = value - static_cast<float>(whole) * ln2;
    const float x2 = x * x;
    const float x3 = x2 * x;
    const float x4 = x3 * x;
    const float polynomial = 1.0f - x + x2 * 0.5f - x3 * 0.16666667f +
        x4 * 0.04166667f;
    // `value < 40` bounds whole to 57, so this normal IEEE-754 power-of-two
    // scale exactly replaces the old repeated halving loop.
    union { uint32_t bits; float value; } scale{
        static_cast<uint32_t>(127u - whole) << 23};
    return polynomial * scale.value;
}

float logApprox(float value) noexcept {
    if (value <= 0.0f) return -32.0f;
    int32_t exponent = 0;
    while (value >= 1.0f) { value *= 0.5f; ++exponent; }
    while (value < 0.5f) { value *= 2.0f; --exponent; }
    const float m = value * 2.0f;
    const float z = (m - 1.0f) / (m + 1.0f);
    const float z2 = z * z;
    float series = z + z * z2 * (1.0f / 3.0f) + z * z2 * z2 * (1.0f / 5.0f) +
        z * z2 * z2 * z2 * (1.0f / 7.0f) + z * z2 * z2 * z2 * z2 * (1.0f / 9.0f);
    return 2.0f * series + static_cast<float>(exponent - 1) * 0.69314718056f;
}

float expApprox(float value) noexcept {
    return value < 0.0f ? expNegative(-value) :
        (expNegative(value) > 1.0e-12f ? 1.0f / expNegative(value) : 1.0e12f);
}

float powApprox(float value, float power) noexcept {
    if (value <= 0.0f) return 0.0f;
    return expApprox(logApprox(value) * power);
}

void sinCos(float angle, float& sine, float& cosine) noexcept {
    while (angle > kPi) angle -= kTau;
    while (angle < -kPi) angle += kTau;
    if (angle > kPi * 0.5f) angle = kPi - angle;
    else if (angle < -kPi * 0.5f) angle = -kPi - angle;
    const float x2 = angle * angle;
    const float x3 = angle * x2;
    const float x4 = x2 * x2;
    const float x5 = x3 * x2;
    const float x6 = x4 * x2;
    const float x7 = x5 * x2;
    sine = angle - x3 * (1.0f / 6.0f) + x5 * (1.0f / 120.0f) -
        x7 * (1.0f / 5040.0f);
    cosine = 1.0f - x2 * 0.5f + x4 * (1.0f / 24.0f) -
        x6 * (1.0f / 720.0f);
}

float smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = clampf((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float roundBox(float qx, float qy, float halfWidth, float halfHeight,
               float radius) noexcept {
    const float dx = (qx < 0.0f ? -qx : qx) - (halfWidth - radius);
    const float dy = (qy < 0.0f ? -qy : qy) - (halfHeight - radius);
    const float ox = maxf(dx, 0.0f), oy = maxf(dy, 0.0f);
    const float outside = sqrtApprox(ox * ox + oy * oy);
    const float inside = minf(maxf(dx, dy), 0.0f);
    return outside + inside - radius;
}

uint32_t average4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) noexcept {
    uint32_t result = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        const uint32_t total = ((a >> shift) & 255u) + ((b >> shift) & 255u) +
            ((c >> shift) & 255u) + ((d >> shift) & 255u);
        result |= ((total + 2u) / 4u) << shift;
    }
    return result;
}

uint32_t averageColor(uint32_t total[4], uint32_t count) noexcept {
    if (!count) return 0;
    return ((total[3] / count) << 24) | ((total[2] / count) << 16) |
        ((total[1] / count) << 8) | (total[0] / count);
}

int64_t max64(int64_t a, int64_t b) noexcept { return a > b ? a : b; }

uint32_t* blurHorizontal(uint32_t* source, uint32_t* target,
                         uint32_t width, uint32_t height,
                         uint32_t radius) noexcept {
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t sum[4]{};
            uint32_t count = 0;
            const uint32_t begin = x > radius ? x - radius : 0;
            const uint32_t end = x + radius < width ? x + radius : width - 1;
            for (uint32_t sx = begin; sx <= end; ++sx) {
                const uint32_t pixel = source[static_cast<size_t>(y) * width + sx];
                for (uint32_t ch = 0; ch < 4; ++ch) sum[ch] += (pixel >> (ch * 8)) & 255u;
                ++count;
            }
            target[static_cast<size_t>(y) * width + x] = averageColor(sum, count);
        }
    }
    return target;
}

uint32_t* blurVertical(uint32_t* source, uint32_t* target,
                       uint32_t width, uint32_t height,
                       uint32_t radius) noexcept {
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t sum[4]{};
            uint32_t count = 0;
            const uint32_t begin = y > radius ? y - radius : 0;
            const uint32_t end = y + radius < height ? y + radius : height - 1;
            for (uint32_t sy = begin; sy <= end; ++sy) {
                const uint32_t pixel = source[static_cast<size_t>(sy) * width + x];
                for (uint32_t ch = 0; ch < 4; ++ch) sum[ch] += (pixel >> (ch * 8)) & 255u;
                ++count;
            }
            target[static_cast<size_t>(y) * width + x] = averageColor(sum, count);
        }
    }
    return target;
}

Rect expandHorizontal(Rect rect, uint32_t radius, uint32_t width) noexcept {
    rect.left = rect.left > static_cast<int32_t>(radius)
        ? rect.left - static_cast<int32_t>(radius) : 0;
    const uint64_t right = static_cast<uint64_t>(rect.right) + radius;
    rect.right = static_cast<int32_t>(right < width ? right : width);
    return rect;
}

Rect expandVertical(Rect rect, uint32_t radius, uint32_t height) noexcept {
    rect.top = rect.top > static_cast<int32_t>(radius)
        ? rect.top - static_cast<int32_t>(radius) : 0;
    const uint64_t bottom = static_cast<uint64_t>(rect.bottom) + radius;
    rect.bottom = static_cast<int32_t>(bottom < height ? bottom : height);
    return rect;
}

void blurHorizontalRect(const uint32_t* source, uint32_t* target,
                        uint32_t width, uint32_t height, uint32_t radius,
                        Rect rect) noexcept {
    for (int32_t y = rect.top; y < rect.bottom; ++y) {
        for (int32_t x = rect.left; x < rect.right; ++x) {
            uint32_t sum[4]{};
            uint32_t count = 0;
            const uint32_t begin = static_cast<uint32_t>(x) > radius
                ? static_cast<uint32_t>(x) - radius : 0;
            const uint32_t end = static_cast<uint32_t>(x) + radius < width
                ? static_cast<uint32_t>(x) + radius : width - 1;
            for (uint32_t sx = begin; sx <= end; ++sx) {
                const uint32_t pixel = source[static_cast<size_t>(y) * width + sx];
                for (uint32_t ch = 0; ch < 4; ++ch)
                    sum[ch] += (pixel >> (ch * 8)) & 255u;
                ++count;
            }
            target[static_cast<size_t>(y) * width + x] = averageColor(sum, count);
        }
    }
    (void)height;
}

void blurVerticalRect(const uint32_t* source, uint32_t* target,
                      uint32_t width, uint32_t height, uint32_t radius,
                      Rect rect) noexcept {
    for (int32_t y = rect.top; y < rect.bottom; ++y) {
        for (int32_t x = rect.left; x < rect.right; ++x) {
            uint32_t sum[4]{};
            uint32_t count = 0;
            const uint32_t begin = static_cast<uint32_t>(y) > radius
                ? static_cast<uint32_t>(y) - radius : 0;
            const uint32_t end = static_cast<uint32_t>(y) + radius < height
                ? static_cast<uint32_t>(y) + radius : height - 1;
            for (uint32_t sy = begin; sy <= end; ++sy) {
                const uint32_t pixel = source[static_cast<size_t>(sy) * width + x];
                for (uint32_t ch = 0; ch < 4; ++ch)
                    sum[ch] += (pixel >> (ch * 8)) & 255u;
                ++count;
            }
            target[static_cast<size_t>(y) * width + x] = averageColor(sum, count);
        }
    }
}

uint32_t sampleBilinearChannel(const Cache& cache, float logicalX,
                               float logicalY, uint32_t shift) noexcept {
    float x = logicalX * cache.downsample - 0.5f;
    float y = logicalY * cache.downsample - 0.5f;
    x = clampf(x, 0.0f, static_cast<float>(cache.sampleWidth - 1));
    y = clampf(y, 0.0f, static_cast<float>(cache.sampleHeight - 1));
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = x0 + 1 < cache.sampleWidth ? x0 + 1 : x0;
    const uint32_t y1 = y0 + 1 < cache.sampleHeight ? y0 + 1 : y0;
    const float fx = x - x0, fy = y - y0;
    const float a = mixf(static_cast<float>((cache.sample[
        static_cast<size_t>(y0) * cache.sampleWidth + x0] >> shift) & 255u),
        static_cast<float>((cache.sample[static_cast<size_t>(y0) *
        cache.sampleWidth + x1] >> shift) & 255u), fx);
    const float b = mixf(static_cast<float>((cache.sample[
        static_cast<size_t>(y1) * cache.sampleWidth + x0] >> shift) & 255u),
        static_cast<float>((cache.sample[static_cast<size_t>(y1) *
        cache.sampleWidth + x1] >> shift) & 255u), fx);
    return static_cast<uint32_t>(clampf(mixf(a, b, fy) + 0.5f, 0.0f, 255.0f));
}

float ign(float x, float y) noexcept {
    return fract(52.9829189f * fract(x * 0.06711056f + y * 0.00583715f));
}

float hash12(float x, float y) noexcept {
    float px = fract(x * 0.1031f), py = fract(y * 0.1031f),
          pz = fract(x * 0.1031f);
    const float dot = px * (py + 33.33f) + py * (pz + 33.33f) + pz * (px + 33.33f);
    px += dot; py += dot; pz += dot;
    return fract((px + py) * pz);
}

uint32_t packPremultiplied(float red, float green, float blue,
                           float alpha) noexcept {
    const uint32_t a = static_cast<uint32_t>(clampf(alpha * 255.0f + 0.5f, 0.0f, 255.0f));
    const uint32_t r = static_cast<uint32_t>(clampf(red * alpha * 255.0f + 0.5f, 0.0f, 255.0f));
    const uint32_t g = static_cast<uint32_t>(clampf(green * alpha * 255.0f + 0.5f, 0.0f, 255.0f));
    const uint32_t b = static_cast<uint32_t>(clampf(blue * alpha * 255.0f + 0.5f, 0.0f, 255.0f));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

} // namespace

bool IsValid(const MicaLiquidGlassDescriptor& d,
             uint32_t windowWidth, uint32_t windowHeight) noexcept {
    const auto in = [](float value, float low, float high) noexcept {
        return value >= low && value <= high;
    };
    return d.magic == MICANT_LIQUID_GLASS_MAGIC &&
        d.version == MICANT_LIQUID_GLASS_VERSION && d.size == sizeof(d) &&
        d.flags == MICANT_LIQUID_GLASS_ACTIVE && d.width && d.height &&
        d.width <= Cache::MaxCard && d.height <= Cache::MaxCard &&
        d.left >= 0 && d.top >= 0 &&
        static_cast<uint64_t>(d.left) + d.width <= windowWidth &&
        static_cast<uint64_t>(d.top) + d.height <= windowHeight &&
        d.generation && in(d.radius, 0.0f, 128.0f) &&
        in(d.bezel, 0.0f, 128.0f) && in(d.thickness, 0.0f, 128.0f) &&
        in(d.dispersion, 0.0f, 1.0f) && in(d.blurPx, 0.0f, 64.0f) &&
        in(d.tintMix, 0.0f, 1.0f) && in(d.saturation, 0.0f, 2.0f) &&
        in(d.dark, 0.0f, 1.0f) && in(d.sourceBlur, 0.0f, 32.0f) &&
        in(d.downsample, 0.5f, 1.0f) && in(d.pad, 0.0f, 32.0f) &&
        in(d.rim, 0.0f, 2.0f) && in(d.grain, 0.0f, 0.25f) &&
        in(d.magnify, 0.0f, 1.0f) && in(d.shadowAmt, 0.0f, 1.0f) &&
        in(d.rimTint, 0.0f, 1.0f) && in(d.ringW, -64.0f, 64.0f) &&
        in(d.flatInner, 0.0f, 1.0f);
}

bool NeedsRefresh(const Cache& cache, uint64_t window, uint64_t sceneGeneration,
                  int32_t screenLeft, int32_t screenTop,
                  const MicaLiquidGlassDescriptor& d) noexcept {
    return !cache.valid || cache.window != window ||
        cache.sceneGeneration != sceneGeneration ||
        cache.screenLeft != screenLeft || cache.screenTop != screenTop ||
        cache.width != d.width || cache.height != d.height ||
        cache.descriptorGeneration != d.generation || cache.downsample != d.downsample;
}

void Render(Cache& cache, uint32_t* frame, uint32_t screenWidth,
            uint32_t screenHeight, uint64_t window, uint64_t sceneGeneration,
            int32_t windowLeft, int32_t windowTop,
            const MicaLiquidGlassDescriptor& d) noexcept {
    const bool hadPriorCapture = cache.valid && cache.raw &&
        cache.captureWidth == d.width + kBackdropMargin * 2u &&
        cache.captureHeight == d.height + kBackdropMargin * 2u;
    cache.valid = false;
    cache.changedBackdropPixels = 0;
    cache.changedBackdropLeft = cache.changedBackdropTop = UINT32_MAX;
    cache.changedBackdropRight = cache.changedBackdropBottom = 0;
    if (!cache.raw || !cache.sample || !cache.scratch || !cache.output ||
        !frame || !screenWidth || !screenHeight ||
        !IsValid(d, static_cast<uint32_t>(windowLeft < 0 ? 0 : screenWidth),
                    static_cast<uint32_t>(windowTop < 0 ? 0 : screenHeight))) return;
    const int32_t glassLeft = windowLeft + d.left;
    const int32_t glassTop = windowTop + d.top;
    const bool partialEligible = hadPriorCapture && cache.window == window &&
        cache.screenLeft == glassLeft && cache.screenTop == glassTop &&
        cache.width == d.width && cache.height == d.height &&
        cache.descriptorGeneration == d.generation && cache.downsample == d.downsample;
    const int32_t captureLeft = glassLeft - static_cast<int32_t>(kBackdropMargin);
    const int32_t captureTop = glassTop - static_cast<int32_t>(kBackdropMargin);
    const uint32_t captureWidth = d.width + kBackdropMargin * 2;
    const uint32_t captureHeight = d.height + kBackdropMargin * 2;
    if (captureWidth > Cache::MaxCapture || captureHeight > Cache::MaxCapture) return;
    for (uint32_t y = 0; y < captureHeight; ++y) {
        const int64_t sy = static_cast<int64_t>(captureTop) + y;
        const uint32_t cy = static_cast<uint32_t>(sy < 0 ? 0 :
            sy >= screenHeight ? screenHeight - 1 : sy);
        for (uint32_t x = 0; x < captureWidth; ++x) {
            const int64_t sx = static_cast<int64_t>(captureLeft) + x;
            const uint32_t cx = static_cast<uint32_t>(sx < 0 ? 0 :
                sx >= screenWidth ? screenWidth - 1 : sx);
            const uint32_t pixel = frame[static_cast<size_t>(cy) * screenWidth + cx];
            if (hadPriorCapture && cache.raw[static_cast<size_t>(y) * captureWidth + x] != pixel) {
                ++cache.changedBackdropPixels;
                if (x < cache.changedBackdropLeft) cache.changedBackdropLeft = x;
                if (y < cache.changedBackdropTop) cache.changedBackdropTop = y;
                if (x + 1 > cache.changedBackdropRight) cache.changedBackdropRight = x + 1;
                if (y + 1 > cache.changedBackdropBottom) cache.changedBackdropBottom = y + 1;
            }
            cache.raw[static_cast<size_t>(y) * captureWidth + x] = pixel;
        }
    }
    cache.downsample = d.downsample;
    cache.captureWidth = captureWidth;
    cache.captureHeight = captureHeight;
    cache.sampleWidth = static_cast<uint32_t>(captureWidth * d.downsample + 0.5f);
    cache.sampleHeight = static_cast<uint32_t>(captureHeight * d.downsample + 0.5f);
    if (!cache.sampleWidth || !cache.sampleHeight ||
        cache.sampleWidth > Cache::MaxCapture || cache.sampleHeight > Cache::MaxCapture)
        return;
    const uint32_t boxRadius = static_cast<uint32_t>(d.sourceBlur * 0.58f + 0.5f);
    Rect changedSample{};
    const bool incrementalBlur = partialEligible && cache.changedBackdropPixels && boxRadius;
    if (incrementalBlur) {
        // The downsampled source is retained separately from its blurred copy.
        // A changed raw backdrop rectangle maps to this bounded source region;
        // each horizontal/vertical box pass expands its output by one radius.
        const uint32_t left = static_cast<uint32_t>(cache.changedBackdropLeft * d.downsample);
        const uint32_t top = static_cast<uint32_t>(cache.changedBackdropTop * d.downsample);
        const uint32_t right = static_cast<uint32_t>(cache.changedBackdropRight * d.downsample + 1.0f);
        const uint32_t bottom = static_cast<uint32_t>(cache.changedBackdropBottom * d.downsample + 1.0f);
        changedSample = {
            static_cast<int32_t>(left ? left - 1 : 0),
            static_cast<int32_t>(top ? top - 1 : 0),
            static_cast<int32_t>(right < cache.sampleWidth ? right + 1 : cache.sampleWidth),
            static_cast<int32_t>(bottom < cache.sampleHeight ? bottom + 1 : cache.sampleHeight)};
    } else {
        changedSample = {0, 0, static_cast<int32_t>(cache.sampleWidth),
                         static_cast<int32_t>(cache.sampleHeight)};
    }
    for (int32_t y = changedSample.top; y < changedSample.bottom; ++y) {
        for (int32_t x = changedSample.left; x < changedSample.right; ++x) {
            if (d.downsample < 0.75f) {
                const uint32_t sx = static_cast<uint32_t>(x) * 2;
                const uint32_t sy = static_cast<uint32_t>(y) * 2;
                const uint32_t x1 = sx + 1 < captureWidth ? sx + 1 : sx;
                const uint32_t y1 = sy + 1 < captureHeight ? sy + 1 : sy;
                cache.downsampled[static_cast<size_t>(y) * cache.sampleWidth + x] = average4(
                    cache.raw[static_cast<size_t>(sy) * captureWidth + sx],
                    cache.raw[static_cast<size_t>(sy) * captureWidth + x1],
                    cache.raw[static_cast<size_t>(y1) * captureWidth + sx],
                    cache.raw[static_cast<size_t>(y1) * captureWidth + x1]);
            } else {
                cache.downsampled[static_cast<size_t>(y) * cache.sampleWidth + x] =
                    cache.raw[static_cast<size_t>(y) * captureWidth + x];
            }
        }
    }
    if (incrementalBlur) {
        Rect affected = changedSample;
        const uint32_t* blurSource = cache.downsampled;
        for (uint32_t pass = 0; pass < 3; ++pass) {
            const Rect horizontal = expandHorizontal(affected, boxRadius,
                                                     cache.sampleWidth);
            blurHorizontalRect(blurSource, cache.scratch, cache.sampleWidth,
                               cache.sampleHeight, boxRadius, horizontal);
            const Rect verticalOutput = expandVertical(horizontal, boxRadius,
                                                       cache.sampleHeight);
            blurVerticalRect(cache.scratch, cache.sample, cache.sampleWidth,
                             cache.sampleHeight, boxRadius, verticalOutput);
            affected = verticalOutput;
            blurSource = cache.sample;
        }
    } else {
        for (size_t index = 0; index < static_cast<size_t>(cache.sampleWidth) *
             cache.sampleHeight; ++index)
            cache.sample[index] = cache.downsampled[index];
    }
    if (boxRadius && !incrementalBlur) {
        for (uint32_t pass = 0; pass < 3; ++pass) {
            blurHorizontal(cache.sample, cache.scratch, cache.sampleWidth,
                           cache.sampleHeight, boxRadius);
            blurVertical(cache.scratch, cache.sample, cache.sampleWidth,
                         cache.sampleHeight, boxRadius);
        }
    }

    const float halfWidth = d.width * 0.5f, halfHeight = d.height * 0.5f;
    const float radius = minf(d.radius, minf(halfWidth, halfHeight));
    const float bezelWidth = maxf(d.bezel, 1.0f) * 1.45f;
    struct GlassTap {
        float radius{};
        float sine{};
        float cosine{};
        float weight{};
    };
    // Source blur already supplies a broad, three-pass diffusion. Use fewer
    // glass refraction taps in that case; retaining 24 taps only adds
    // redundant sampling work on the heavily blurred backdrop.
    const uint32_t tapCount = d.sourceBlur >= 8.0f ? 8u : 24u;
    GlassTap taps[24]{};
    for (uint32_t tap = 0; tap < tapCount; ++tap) {
        const float fi = static_cast<float>(tap) + 0.5f;
        taps[tap].radius = sqrtApprox(fi / static_cast<float>(tapCount));
        sinCos(fi * kGoldenAngle, taps[tap].sine, taps[tap].cosine);
        taps[tap].weight = expNegative(2.2f * taps[tap].radius * taps[tap].radius);
    }
    uint32_t outputLeft = 0, outputTop = 0;
    uint32_t outputRight = d.width, outputBottom = d.height;
    if (partialEligible && cache.changedBackdropPixels && cache.changedBackdropRight >
            cache.changedBackdropLeft && cache.changedBackdropBottom >
            cache.changedBackdropTop) {
        const uint32_t boxRadius = static_cast<uint32_t>(d.sourceBlur * 0.58f + 0.5f);
        const float sampleRadius = static_cast<float>(boxRadius * 3u) / d.downsample + 2.0f;
        const float refractionRadius = d.thickness * 1.35f * 1.5f *
            (1.0f + d.dispersion);
        const float tapRadius = maxf(d.blurPx, 0.5f) * 1.5f;
        const float dirtyMargin = sampleRadius + refractionRadius + tapRadius + 2.0f;
        // Expand by the source blur kernel, refraction and tap footprint, then
        // clip to the card. Large or extreme descriptors naturally become a
        // full redraw, while small backdrop updates stay local.
        {
            const float left = static_cast<float>(cache.changedBackdropLeft) -
                kBackdropMargin - dirtyMargin;
            const float right = static_cast<float>(cache.changedBackdropRight) -
                kBackdropMargin + dirtyMargin;
            const float yScale = 1.0f - 1.5f * d.magnify;
            const float sourceTop = static_cast<float>(cache.changedBackdropTop) -
                kBackdropMargin - dirtyMargin;
            const float sourceBottom = static_cast<float>(cache.changedBackdropBottom) -
                kBackdropMargin + dirtyMargin;
            const float yA = (sourceTop - halfHeight) / yScale + halfHeight;
            const float yB = (sourceBottom - halfHeight) / yScale + halfHeight;
            const float top = yA < yB ? yA : yB;
            const float bottom = yA > yB ? yA : yB;
            outputLeft = static_cast<uint32_t>(clampf(left, 0.0f,
                static_cast<float>(d.width)));
            outputRight = static_cast<uint32_t>(clampf(right + 1.0f, 0.0f,
                static_cast<float>(d.width)));
            outputTop = static_cast<uint32_t>(clampf(top, 0.0f,
                static_cast<float>(d.height)));
            outputBottom = static_cast<uint32_t>(clampf(bottom + 1.0f, 0.0f,
                static_cast<float>(d.height)));
            if (outputRight <= outputLeft || outputBottom <= outputTop) {
                // The changed backdrop lies outside the card's complete blur,
                // refraction and tap footprint. Keep the existing output; a
                // full-card render here turns harmless edge updates (such as
                // the debug HUD above the card) into a large CPU spike.
                cache.valid = true;
                cache.window = window;
                cache.sceneGeneration = sceneGeneration;
                cache.descriptorGeneration = d.generation;
                cache.screenLeft = glassLeft;
                cache.screenTop = glassTop;
                cache.width = d.width;
                cache.height = d.height;
                return;
            }
        }
    }
    for (uint32_t y = outputTop; y < outputBottom; ++y) {
        for (uint32_t x = outputLeft; x < outputRight; ++x) {
            const float px = d.left + static_cast<float>(x) + 0.5f;
            const float py = d.top + static_cast<float>(y) + 0.5f;
            // The output buffer already begins at the glass bounds. Keep the
            // rounded card centered in that buffer; pad is backdrop sampling
            // room, not an inset that shifts and clips the visible glass.
            const float qx = static_cast<float>(x) + 0.5f - halfWidth;
            const float qy = static_cast<float>(y) + 0.5f - halfHeight;
            const float sd = roundBox(qx, qy, halfWidth, halfHeight, radius);
            const float coverage = clampf(0.5f - sd, 0.0f, 1.0f);
            const size_t destination = static_cast<size_t>(y) * d.width + x;
            if (coverage <= 0.0f) { cache.output[destination] = 0; continue; }
            if (sd > 1.0f) {
                const float shadowDistance = roundBox(qx, qy - 3.0f,
                    halfWidth, halfHeight, radius);
                const float shadow = 0.30f * d.shadowAmt *
                    (1.0f - smoothstep(-2.0f, 9.0f, shadowDistance));
                cache.output[destination] = packPremultiplied(0, 0, 0, shadow);
                continue;
            }

            const float t = maxf(-sd, 0.0f);
            const float dx = (qx < 0.0f ? -qx : qx) - (halfWidth - radius);
            const float dy = (qy < 0.0f ? -qy : qy) - (halfHeight - radius);
            const float sgx = qx >= 0.0f ? 1.0f : -1.0f;
            const float sgy = qy >= 0.0f ? 1.0f : -1.0f;
            const float mx = maxf(dx, 0.0f), my = maxf(dy, 0.0f);
            const float ml = sqrtApprox(mx * mx + my * my);
            float gx{}, gy{};
            if (ml > 1.0e-4f) { gx = mx / ml * sgx; gy = my / ml * sgy; }
            else if (dx > dy) gx = sgx;
            else gy = sgy;
            const float u = clampf(t / bezelWidth, 0.0f, 1.0f);
            const float om = 1.0f - u, om2 = om * om, om4 = om2 * om2;
            const float numerator = om2 * om;
            const float denominator = powApprox(maxf(1.0f - om4, 1.0e-3f), 0.75f);
            const float slope = minf(numerator / maxf(denominator, 1.0e-4f), 7.0f);
            const float nxRaw = gx * slope, nyRaw = gy * slope;
            const float normalLength = sqrtApprox(nxRaw * nxRaw + nyRaw * nyRaw + 1.0f);
            const float nx = nxRaw / normalLength, ny = nyRaw / normalLength;
            const float nz = 1.0f / normalLength;
            const float eta = 1.0f / 1.5f;
            const float refractK = maxf(0.0f, 1.0f - eta * eta * (1.0f - nz * nz));
            const float refractScale = eta * nz - sqrtApprox(refractK);
            const float rz = -eta + eta * nz * nz - sqrtApprox(refractK) * nz;
            const float divide = maxf(-rz, 0.2f);
            const float dispX = (refractScale * nx / divide) * d.thickness * 1.35f;
            const float dispY = (refractScale * ny / divide) * d.thickness * 1.35f;

            const float sy = (static_cast<float>(y) + 0.5f - halfHeight) *
                (1.0f - d.magnify * 1.5f) + halfHeight;
            const float blurRadius = d.blurPx * mixf(0.45f, 1.0f,
                smoothstep(0.0f, bezelWidth, t));
            const float tapRadius = maxf(blurRadius, 0.5f) * 1.5f;
            const float dispersion = d.dispersion * 0.6f;
            const float rotation = ign(static_cast<float>(glassLeft + x),
                                       static_cast<float>(glassTop + y)) * kTau;
            float rotSin{}, rotCos{};
            sinCos(rotation, rotSin, rotCos);
            float red = 0, green = 0, blue = 0, weightSum = 0;
            for (uint32_t tap = 0; tap < tapCount; ++tap) {
                const auto& sample = taps[tap];
                const float ox = (sample.cosine * rotCos - sample.sine * rotSin) *
                    sample.radius * tapRadius;
                const float oy = (sample.sine * rotCos + sample.cosine * rotSin) *
                    sample.radius * tapRadius;
                const uint32_t cr = sampleBilinearChannel(cache,
                    static_cast<float>(x) + kBackdropMargin + ox + dispX * (1.0f + dispersion),
                    sy + kBackdropMargin + oy + dispY * (1.0f + dispersion), 16);
                const uint32_t cg = sampleBilinearChannel(cache,
                    static_cast<float>(x) + kBackdropMargin + ox + dispX,
                    sy + kBackdropMargin + oy + dispY, 8);
                const uint32_t cb = sampleBilinearChannel(cache,
                    static_cast<float>(x) + kBackdropMargin + ox + dispX * (1.0f - dispersion),
                    sy + kBackdropMargin + oy + dispY * (1.0f - dispersion), 0);
                red += static_cast<float>(cr) * sample.weight;
                green += static_cast<float>(cg) * sample.weight;
                blue += static_cast<float>(cb) * sample.weight;
                weightSum += sample.weight;
            }
            const uint32_t alphaChannel = sampleBilinearChannel(cache,
                static_cast<float>(x) + kBackdropMargin, sy + kBackdropMargin, 24);
            const float baseAlpha = static_cast<float>(alphaChannel) / 255.0f;
            const float inverseAlpha = 1.0f / maxf(baseAlpha, 1.0e-3f);
            float colorR = red / maxf(weightSum, 1.0e-3f) / 255.0f * inverseAlpha;
            float colorG = green / maxf(weightSum, 1.0e-3f) / 255.0f * inverseAlpha;
            float colorB = blue / maxf(weightSum, 1.0e-3f) / 255.0f * inverseAlpha;
            const float luminance = colorR * 0.2126f + colorG * 0.7152f + colorB * 0.0722f;
            colorR = mixf(luminance, colorR, d.saturation);
            colorG = mixf(luminance, colorG, d.saturation);
            colorB = mixf(luminance, colorB, d.saturation);
            const float tint = d.dark;
            colorR = mixf(colorR, 1.0f - 0.91f * tint, d.tintMix);
            colorG = mixf(colorG, 1.0f - 0.91f * tint, d.tintMix);
            colorB = mixf(colorB, 1.0f - 0.90f * tint, d.tintMix);
            const float lift = mixf(0.055f, 0.020f, d.dark);
            colorR += lift; colorG += lift; colorB += lift;
            const float ty = clampf((qy + halfHeight) / d.height, 0.0f, 1.0f);
            const float sheen = (1.0f - ty) * mixf(0.040f, 0.030f, d.dark);
            colorR += sheen; colorG += sheen; colorB += sheen;
            // A broad, slanted environment highlight makes the face read as
            // polished glass instead of a tinted blur. The narrow band is
            // the reflected light source; the wider lobe softens its edge.
            const float reflectionAxis = qy + qx * 0.28f + halfHeight * 0.54f;
            const float reflectionBand = expNegative(
                (reflectionAxis / 4.5f) * (reflectionAxis / 4.5f));
            const float reflectionGlow = expNegative(
                (reflectionAxis / 17.0f) * (reflectionAxis / 17.0f));
            const float reflectionX = (qx + halfWidth * 0.18f) /
                (halfWidth * 0.92f);
            const float reflectionEnds = expNegative(reflectionX * reflectionX *
                                                       reflectionX * reflectionX);
            const float reflectionCornerX = (qx + halfWidth * 0.38f) /
                (halfWidth * 0.58f);
            const float reflectionCornerY = (qy + halfHeight * 0.45f) /
                (halfHeight * 0.26f);
            const float reflectionCorner = expNegative(
                reflectionCornerX * reflectionCornerX +
                reflectionCornerY * reflectionCornerY);
            const float reflection = d.rim * reflectionEnds *
                (0.40f * reflectionBand + 0.17f * reflectionGlow) +
                d.rim * 0.22f * reflectionCorner;
            colorR += (1.0f - colorR) * reflection;
            colorG += (1.0f - colorG) * reflection;
            colorB += (1.0f - colorB) * reflection;
            const float innerK = 1.0f - d.flatInner;
            const float innerOne = 1.0f - 0.12f * expNegative(t / 6.0f) *
                clampf(gy, 0.0f, 1.0f) * innerK;
            const float innerTwo = 1.0f - 0.10f *
                expNegative(t / (bezelWidth * 0.45f)) * innerK;
            colorR *= innerOne * innerTwo;
            colorG *= innerOne * innerTwo;
            colorB *= innerOne * innerTwo;
            const float rimR = mixf(0.96f, clampf(colorR * 1.5f + 0.12f, 0.0f, 1.0f), d.rimTint);
            const float rimG = mixf(0.98f, clampf(colorG * 1.5f + 0.12f, 0.0f, 1.0f), d.rimTint);
            const float rimB = mixf(1.00f, clampf(colorB * 1.5f + 0.12f, 0.0f, 1.0f), d.rimTint);
            const float lightLength = sqrtApprox(0.55f * 0.55f + 0.83f * 0.83f);
            const float facing = gx * (-0.55f / lightLength) + gy * (-0.83f / lightLength);
            const float spec = powApprox(maxf(facing, 0.0f), 1.6f) +
                0.55f * powApprox(maxf(-facing, 0.0f), 2.2f);
            const float hair = expNegative(t / 1.1f);
            const float wide = expNegative(t / 5.0f);
            const float edge = (hair * (0.18f + 0.78f * spec) +
                wide * 0.10f * (0.30f + spec)) * d.rim * mixf(0.85f, 1.0f, d.dark);
            colorR += edge * rimR; colorG += edge * rimG; colorB += edge * rimB;
            const float meniscusX = (t - bezelWidth * 0.5f) / (bezelWidth * 0.2f + 1.0f);
            const float meniscus = expNegative(meniscusX * meniscusX) *
                0.07f * (0.35f + spec) * d.rim;
            colorR += meniscus * rimR; colorG += meniscus * rimG; colorB += meniscus * rimB;
            if (d.ringW > 0.0f) {
                const float ring = 1.0f - smoothstep(d.ringW * 0.5f,
                    d.ringW * 0.5f + 1.0f, t);
                const float mix = ring * (0.55f + 0.35f *
                    clampf(facing * 0.5f + 0.5f, 0.0f, 1.0f));
                colorR = mixf(colorR, rimR, mix);
                colorG = mixf(colorG, rimG, mix);
                colorB = mixf(colorB, rimB, mix);
            }
            const float noise = hash12(static_cast<float>(glassLeft + x),
                                       static_cast<float>(glassTop + y)) +
                hash12(static_cast<float>(glassLeft + x) * 1.7f + 17.0f,
                       static_cast<float>(glassTop + y) * 1.7f + 17.0f) - 1.0f;
            colorR += noise * d.grain;
            colorG += noise * d.grain;
            colorB += noise * d.grain;
            const float glassAlpha = coverage * baseAlpha;
            cache.output[destination] = packPremultiplied(
                clampf(colorR, 0.0f, 1.0f), clampf(colorG, 0.0f, 1.0f),
                clampf(colorB, 0.0f, 1.0f), glassAlpha);
        }
    }
    cache.valid = true;
    cache.window = window;
    cache.sceneGeneration = sceneGeneration;
    cache.descriptorGeneration = d.generation;
    cache.screenLeft = glassLeft;
    cache.screenTop = glassTop;
    cache.width = d.width;
    cache.height = d.height;
}

void Composite(const Cache& cache, uint32_t* frame, uint32_t screenWidth,
               uint32_t screenHeight, Rect clip) noexcept {
    if (!cache.valid || !cache.output || !frame) return;
    const int64_t right = static_cast<int64_t>(cache.screenLeft) + cache.width;
    const int64_t bottom = static_cast<int64_t>(cache.screenTop) + cache.height;
    const int64_t left = max64(cache.screenLeft, clip.left);
    const int64_t top = max64(cache.screenTop, clip.top);
    const int64_t drawRight = right < clip.right ? right : clip.right;
    const int64_t drawBottom = bottom < clip.bottom ? bottom : clip.bottom;
    for (int64_t y = top; y < drawBottom; ++y) {
        if (y < 0 || y >= screenHeight) continue;
        for (int64_t x = left; x < drawRight; ++x) {
            if (x < 0 || x >= screenWidth) continue;
            const uint32_t source = cache.output[
                static_cast<size_t>(y - cache.screenTop) * cache.width +
                static_cast<size_t>(x - cache.screenLeft)];
            if (!(source >> 24)) continue;
            auto& destination = frame[static_cast<size_t>(y) * screenWidth +
                                      static_cast<size_t>(x)];
            destination = micant::shared::graphics::PremultipliedSourceOver(
                source, destination);
        }
    }
}

} // namespace micant::compositor::glass
