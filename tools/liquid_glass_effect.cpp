#include "liquid_glass_effect.hpp"
#include "micant/shared/pixel_composite.h"
#include <stddef.h>

namespace micant::compositor::glass {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;
constexpr float kGoldenAngle = 2.39996323f;
constexpr uint32_t kBackdropMargin = 72;

float minf(float a, float b) noexcept { return a < b ? a : b; }
float maxf(float a, float b) noexcept { return a > b ? a : b; }
float clampf(float v, float lo, float hi) noexcept {
    return minf(maxf(v, lo), hi);
}
float fract(float v) noexcept { return v - static_cast<int32_t>(v); }
float mixf(float a, float b, float t) noexcept { return a + (b - a) * t; }

float sqrtApprox(float value) noexcept {
    if (value <= 0.0f) return 0.0f;
    float result = value > 1.0f ? value : 1.0f;
    for (uint32_t i = 0; i < 7; ++i)
        result = 0.5f * (result + value / result);
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
    float result = 1.0f - x + x2 * 0.5f - x3 * 0.16666667f + x4 * 0.04166667f;
    while (whole--) result *= 0.5f;
    return result;
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

uint32_t sampleBilinear(const Cache& cache, float logicalX,
                        float logicalY) noexcept {
    float x = logicalX * cache.downsample - 0.5f;
    float y = logicalY * cache.downsample - 0.5f;
    x = clampf(x, 0.0f, static_cast<float>(cache.sampleWidth - 1));
    y = clampf(y, 0.0f, static_cast<float>(cache.sampleHeight - 1));
    const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = x0 + 1 < cache.sampleWidth ? x0 + 1 : x0;
    const uint32_t y1 = y0 + 1 < cache.sampleHeight ? y0 + 1 : y0;
    const float fx = x - x0, fy = y - y0;
    const uint32_t pixels[4]{
        cache.sample[static_cast<size_t>(y0) * cache.sampleWidth + x0],
        cache.sample[static_cast<size_t>(y0) * cache.sampleWidth + x1],
        cache.sample[static_cast<size_t>(y1) * cache.sampleWidth + x0],
        cache.sample[static_cast<size_t>(y1) * cache.sampleWidth + x1]};
    uint32_t result = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        const float a = mixf(static_cast<float>((pixels[0] >> shift) & 255u),
                             static_cast<float>((pixels[1] >> shift) & 255u), fx);
        const float b = mixf(static_cast<float>((pixels[2] >> shift) & 255u),
                             static_cast<float>((pixels[3] >> shift) & 255u), fx);
        const uint32_t channel = static_cast<uint32_t>(clampf(mixf(a, b, fy) + 0.5f,
                                                               0.0f, 255.0f));
        result |= channel << shift;
    }
    return result;
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
    if (!frame || !screenWidth || !screenHeight ||
        !IsValid(d, static_cast<uint32_t>(windowLeft < 0 ? 0 : screenWidth),
                    static_cast<uint32_t>(windowTop < 0 ? 0 : screenHeight))) return;
    const int32_t glassLeft = windowLeft + d.left;
    const int32_t glassTop = windowTop + d.top;
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
            cache.raw[static_cast<size_t>(y) * captureWidth + x] =
                frame[static_cast<size_t>(cy) * screenWidth + cx];
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
    for (uint32_t y = 0; y < cache.sampleHeight; ++y) {
        for (uint32_t x = 0; x < cache.sampleWidth; ++x) {
            if (d.downsample < 0.75f) {
                const uint32_t sx = x * 2, sy = y * 2;
                const uint32_t x1 = sx + 1 < captureWidth ? sx + 1 : sx;
                const uint32_t y1 = sy + 1 < captureHeight ? sy + 1 : sy;
                cache.sample[static_cast<size_t>(y) * cache.sampleWidth + x] = average4(
                    cache.raw[static_cast<size_t>(sy) * captureWidth + sx],
                    cache.raw[static_cast<size_t>(sy) * captureWidth + x1],
                    cache.raw[static_cast<size_t>(y1) * captureWidth + sx],
                    cache.raw[static_cast<size_t>(y1) * captureWidth + x1]);
            } else {
                cache.sample[static_cast<size_t>(y) * cache.sampleWidth + x] =
                    cache.raw[static_cast<size_t>(y) * captureWidth + x];
            }
        }
    }
    const uint32_t boxRadius = static_cast<uint32_t>(d.sourceBlur * 0.58f + 0.5f);
    if (boxRadius) {
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
    for (uint32_t y = 0; y < d.height; ++y) {
        for (uint32_t x = 0; x < d.width; ++x) {
            const float px = d.left + static_cast<float>(x) + 0.5f;
            const float py = d.top + static_cast<float>(y) + 0.5f;
            const float qx = px - d.pad - halfWidth;
            const float qy = py - d.pad - halfHeight;
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

            const float sy = (py - d.pad - halfHeight) *
                (1.0f - d.magnify * 1.5f) + d.pad + halfHeight;
            const float blurRadius = d.blurPx * mixf(0.45f, 1.0f,
                smoothstep(0.0f, bezelWidth, t));
            const float tapRadius = maxf(blurRadius, 0.5f) * 1.5f;
            const float dispersion = d.dispersion * 0.6f;
            const float rotation = ign(static_cast<float>(glassLeft + x),
                                       static_cast<float>(glassTop + y)) * kTau;
            float red = 0, green = 0, blue = 0, weightSum = 0;
            for (uint32_t tap = 0; tap < 24; ++tap) {
                const float fi = static_cast<float>(tap) + 0.5f;
                const float r = sqrtApprox(fi / 24.0f);
                float baseSin{}, baseCos{}, rotSin{}, rotCos{};
                sinCos(fi * kGoldenAngle, baseSin, baseCos);
                sinCos(rotation, rotSin, rotCos);
                const float ox = (baseCos * rotCos - baseSin * rotSin) * r * tapRadius;
                const float oy = (baseSin * rotCos + baseCos * rotSin) * r * tapRadius;
                const float weight = expNegative(2.2f * r * r);
                const uint32_t cr = sampleBilinear(cache,
                    px - d.pad + kBackdropMargin + ox + dispX * (1.0f + dispersion),
                    sy - d.pad + kBackdropMargin + oy + dispY * (1.0f + dispersion));
                const uint32_t cg = sampleBilinear(cache,
                    px - d.pad + kBackdropMargin + ox + dispX,
                    sy - d.pad + kBackdropMargin + oy + dispY);
                const uint32_t cb = sampleBilinear(cache,
                    px - d.pad + kBackdropMargin + ox + dispX * (1.0f - dispersion),
                    sy - d.pad + kBackdropMargin + oy + dispY * (1.0f - dispersion));
                red += static_cast<float>((cr >> 16) & 255u) * weight;
                green += static_cast<float>((cg >> 8) & 255u) * weight;
                blue += static_cast<float>(cb & 255u) * weight;
                weightSum += weight;
            }
            const uint32_t alphaPixel = sampleBilinear(cache,
                px - d.pad + kBackdropMargin, sy - d.pad + kBackdropMargin);
            const float baseAlpha = static_cast<float>(alphaPixel >> 24) / 255.0f;
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
    if (!cache.valid || !frame) return;
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
