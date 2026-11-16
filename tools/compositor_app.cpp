#include "micant/shared/compositor_surface.h"
#include "micant/shared/font8x8.hpp"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/pixel_composite.h"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"

#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<volatile uint8_t*>(destination);
    for (size_t index = 0; index < count; ++index)
        bytes[index] = static_cast<uint8_t>(value);
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* output = static_cast<volatile uint8_t*>(destination);
    const auto* input = static_cast<const volatile uint8_t*>(source);
    for (size_t index = 0; index < count; ++index) output[index] = input[index];
    return destination;
}

namespace {

uint32_t g_frame[MICANT_COMPOSITOR_SURFACE_PIXELS]{};
constexpr int64_t kWindowEffectRadiusLimit = 32;
constexpr int64_t kWindowShadowExtent = MICANT_COMPOSITOR_WINDOW_SHADOW_EXTENT;
constexpr int64_t kWindowBlurRadius = 2;
uint32_t g_windowCornerBackdrop[4][kWindowEffectRadiusLimit *
                                   kWindowEffectRadiusLimit]{};
uint32_t g_windowBlurBackdrop[MICANT_COMPOSITOR_SURFACE_PIXELS]{};
uint16_t g_windowBlurRowSums[kWindowBlurRadius * 2 + 1]
    [MICANT_COMPOSITOR_SCREEN_WIDTH][3]{};
MicaCompositorMetadataStore g_metadataSnapshot{};
bool g_presentFailureReported{};
bool g_hasPresentedFrame{};
uint64_t g_lastThemeGeneration{};

struct ScreenRect {
    int64_t left{}, top{}, right{}, bottom{};
};

ScreenRect g_composeClip{};
uint8_t g_windowDrawOpacity{255};
constexpr uint64_t kWindowAnimationDurationMs = 240;
constexpr uint64_t kAnimationFrameInterval100ns = 160000;

struct AnimationFrame {
    bool active{};
    int32_t x{}, y{};
    uint32_t width{}, height{};
    ScreenRect damage[2]{};
    size_t damageCount{};
};

struct AnimationRuntime {
    uint64_t window{}, generation{}, startMilliseconds{};
    ScreenRect lastBounds{};
    bool active{}, hasLastBounds{};
};

AnimationRuntime g_animationRuntime[MICANT_COMPOSITOR_SURFACE_CAPACITY]{};
AnimationFrame g_animationFrames[MICANT_COMPOSITOR_SURFACE_CAPACITY]{};
MicaCompositorMetadataStore g_effectProbeMetadata{};
MicaCompositorSurfaceInfo g_effectProbeSurfaces[2]{};
AnimationFrame g_effectProbeAnimations[MICANT_COMPOSITOR_SURFACE_CAPACITY]{};
uint32_t g_effectProbePixels[128u * 128u + 64u * 64u]{};

uint64_t animationClockMilliseconds() noexcept {
    const auto* shared = reinterpret_cast<volatile const micant::KUserSharedData*>(
        micant::UserSharedDataAddress);
    return __atomic_load_n(&shared->tickCountQuad, __ATOMIC_ACQUIRE);
}

ScreenRect surfaceBounds(const MicaCompositorSurfaceInfo& surface) noexcept {
    return {surface.x, surface.y,
        static_cast<int64_t>(surface.x) + surface.width,
        static_cast<int64_t>(surface.y) + surface.height};
}

void addAnimationDamage(AnimationFrame& frame, ScreenRect bounds) noexcept {
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom) return;
    if (frame.damageCount < sizeof(frame.damage) / sizeof(frame.damage[0]))
        frame.damage[frame.damageCount++] = bounds;
    else {
        auto& merged = frame.damage[0];
        merged.left = merged.left < bounds.left ? merged.left : bounds.left;
        merged.top = merged.top < bounds.top ? merged.top : bounds.top;
        merged.right = merged.right > bounds.right ? merged.right : bounds.right;
        merged.bottom = merged.bottom > bounds.bottom ? merged.bottom : bounds.bottom;
        frame.damageCount = 1;
    }
}

void buildAnimationFrames(const MicaCompositorMetadataStore& metadata,
                          uint64_t nowMilliseconds,
                          AnimationFrame* frames) noexcept {
    for (size_t index = 0; index < MICANT_COMPOSITOR_SURFACE_CAPACITY; ++index) {
        auto& frame = frames[index];
        frame = {};
        auto& runtime = g_animationRuntime[index];
        const auto& surface = metadata.surfaces[index];
        const bool windowPresent = surface.window &&
            (surface.flags & MICANT_COMPOSITOR_SURFACE_ACTIVE);
        if (!windowPresent || runtime.window != surface.window) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime = {};
        }
        if (!windowPresent ||
            !(surface.animationFlags & MICANT_COMPOSITOR_ANIMATION_ACTIVE)) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime.active = false;
            continue;
        }

        const auto& target = surface.animationTarget;
        if (target.left < 0 || target.top < 0 ||
            target.right > static_cast<int32_t>(MICANT_COMPOSITOR_SCREEN_WIDTH) ||
            target.bottom > static_cast<int32_t>(MICANT_COMPOSITOR_SCREEN_HEIGHT) ||
            target.left >= target.right || target.top >= target.bottom ||
            !surface.width || !surface.height) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime.window = surface.window;
            runtime.generation = surface.animationGeneration;
            runtime.active = false;
            continue;
        }

        if (runtime.window != surface.window ||
            runtime.generation != surface.animationGeneration) {
            if (runtime.active && runtime.hasLastBounds)
                addAnimationDamage(frame, runtime.lastBounds);
            runtime = {};
            runtime.window = surface.window;
            runtime.generation = surface.animationGeneration;
            runtime.startMilliseconds = nowMilliseconds;
            runtime.active = true;
        }
        if (!runtime.active) continue;

        const bool minimizing =
            (surface.animationFlags & MICANT_COMPOSITOR_ANIMATION_MINIMIZE) != 0;
        const ScreenRect windowRect = surfaceBounds(surface);
        const ScreenRect targetRect{target.left, target.top, target.right, target.bottom};
        const ScreenRect from = minimizing ? windowRect : targetRect;
        const ScreenRect to = minimizing ? targetRect : windowRect;
        const uint64_t elapsed = nowMilliseconds >= runtime.startMilliseconds
            ? nowMilliseconds - runtime.startMilliseconds : 0;
        const double linear = elapsed >= kWindowAnimationDurationMs ? 1.0 :
            static_cast<double>(elapsed) / kWindowAnimationDurationMs;
        const double eased = linear * linear * (3.0 - 2.0 * linear);
        const auto lerp = [eased](int64_t a, int64_t b) noexcept {
            return a + static_cast<int64_t>((b - a) * eased);
        };

        if (elapsed >= kWindowAnimationDurationMs) {
            if (runtime.hasLastBounds) addAnimationDamage(frame, runtime.lastBounds);
            addAnimationDamage(frame, to);
            runtime.active = false;
            runtime.hasLastBounds = false;
            continue;
        }

        const int64_t fromWidth = from.right - from.left;
        const int64_t toWidth = to.right - to.left;
        const int64_t fromHeight = from.bottom - from.top;
        const int64_t toHeight = to.bottom - to.top;
        const int64_t curveDelta = to.left + toWidth / 2 - from.left - fromWidth / 2;
        const int64_t curve = static_cast<int64_t>(
            static_cast<double>(curveDelta) * 0.10 * 4.0 * eased * (1.0 - eased));
        const int64_t swoop = static_cast<int64_t>(-18.0 * 4.0 * eased * (1.0 - eased));
        const int64_t left = lerp(from.left, to.left) + curve;
        const int64_t top = lerp(from.top, to.top) + swoop;
        const int64_t width = lerp(fromWidth, toWidth);
        const int64_t height = lerp(fromHeight, toHeight);
        frame.active = true;
        frame.x = static_cast<int32_t>(left);
        frame.y = static_cast<int32_t>(top);
        frame.width = static_cast<uint32_t>(width > 1 ? width : 1);
        frame.height = static_cast<uint32_t>(height > 1 ? height : 1);
        if (runtime.hasLastBounds) addAnimationDamage(frame, runtime.lastBounds);
        const ScreenRect current{left, top, left + width, top + height};
        addAnimationDamage(frame, current);
        runtime.lastBounds = current;
        runtime.hasLastBounds = true;
    }
}

void print(const char* text, size_t length) noexcept {
    (void)micant::sys::invokeUserService(0x25,
        reinterpret_cast<uint64_t>(text), length);
}

uint32_t blend(uint32_t source, uint32_t destination) noexcept {
    const uint32_t alpha = source >> 24;
    if (alpha == 255) return source;
    if (!alpha) return destination;
    const uint32_t inverse = 255 - alpha;
    const uint32_t red = (((source >> 16) & 0xFFu) * alpha +
                          ((destination >> 16) & 0xFFu) * inverse + 127) / 255;
    const uint32_t green = (((source >> 8) & 0xFFu) * alpha +
                            ((destination >> 8) & 0xFFu) * inverse + 127) / 255;
    const uint32_t blue = ((source & 0xFFu) * alpha +
                           (destination & 0xFFu) * inverse + 127) / 255;
    const uint32_t outAlpha = alpha + (((destination >> 24) * inverse + 127) / 255);
    return (outAlpha << 24) | (red << 16) | (green << 8) | blue;
}


void fillRect(int64_t left, int64_t top, int64_t right, int64_t bottom,
              uint32_t color, uint32_t width, uint32_t height) noexcept {
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right > width ? width : right;
    bottom = bottom > height ? height : bottom;
    left = left < g_composeClip.left ? g_composeClip.left : left;
    top = top < g_composeClip.top ? g_composeClip.top : top;
    right = right > g_composeClip.right ? g_composeClip.right : right;
    bottom = bottom > g_composeClip.bottom ? g_composeClip.bottom : bottom;
    if (left >= right || top >= bottom) return;
    const size_t rowWidth = static_cast<size_t>(right - left);
    for (int64_t y = top; y < bottom; ++y) {
        auto* row = g_frame + static_cast<size_t>(y) * width +
            static_cast<size_t>(left);
        const uint32_t colorAlpha = color >> 24;
        const uint32_t alpha = (colorAlpha * g_windowDrawOpacity + 127u) / 255u;
        if (alpha == 255) {
            for (size_t x = 0; x < rowWidth; ++x) row[x] = color;
        } else if (alpha) {
            const uint32_t source = (alpha << 24) | (color & 0x00FFFFFFu);
            for (size_t x = 0; x < rowWidth; ++x)
                row[x] = blend(source, row[x]);
        }
    }
}

uint32_t applyPremultipliedOpacity(uint32_t pixel, uint8_t opacity) noexcept {
    const auto scale = [opacity](uint32_t channel) noexcept {
        return (channel * opacity + 127u) / 255u;
    };
    return (scale(pixel >> 24) << 24) |
        (scale((pixel >> 16) & 0xFFu) << 16) |
        (scale((pixel >> 8) & 0xFFu) << 8) |
        scale(pixel & 0xFFu);
}

void captureWindowBlurBackdrop(uint32_t width, uint32_t height) noexcept {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t index = 0; index < pixelCount; ++index)
        g_windowBlurBackdrop[index] = g_frame[index];
}

void applyWindowBackdropBlur(int64_t left, int64_t top,
                             int64_t right, int64_t bottom,
                             uint8_t opacity, uint32_t width,
                             uint32_t height) noexcept {
    left = left < g_composeClip.left ? g_composeClip.left : left;
    top = top < g_composeClip.top ? g_composeClip.top : top;
    right = right > g_composeClip.right ? g_composeClip.right : right;
    bottom = bottom > g_composeClip.bottom ? g_composeClip.bottom : bottom;
    if (left >= right || top >= bottom || !opacity || opacity == 255) return;

    const auto captureHorizontal = [&](int64_t screenY, size_t rowSlot) noexcept {
        if (screenY < 0) screenY = 0;
        if (screenY >= height) screenY = static_cast<int64_t>(height) - 1;
        for (int64_t x = left; x < right; ++x) {
            uint32_t red = 0, green = 0, blue = 0;
            for (int64_t dx = -kWindowBlurRadius; dx <= kWindowBlurRadius; ++dx) {
                int64_t sampleX = x + dx;
                if (sampleX < 0) sampleX = 0;
                if (sampleX >= width) sampleX = static_cast<int64_t>(width) - 1;
                const uint32_t pixel = g_windowBlurBackdrop[
                    static_cast<size_t>(screenY) * width +
                    static_cast<size_t>(sampleX)];
                red += (pixel >> 16) & 0xFFu;
                green += (pixel >> 8) & 0xFFu;
                blue += pixel & 0xFFu;
            }
            auto& row = g_windowBlurRowSums[rowSlot][x];
            row[0] = static_cast<uint16_t>(red);
            row[1] = static_cast<uint16_t>(green);
            row[2] = static_cast<uint16_t>(blue);
        }
    };

    constexpr size_t kernel = static_cast<size_t>(kWindowBlurRadius * 2 + 1);
    size_t head = 0;
    for (size_t row = 0; row < kernel; ++row)
        captureHorizontal(top - kWindowBlurRadius + static_cast<int64_t>(row), row);

    for (int64_t y = top; y < bottom; ++y) {
        for (int64_t x = left; x < right; ++x) {
            uint32_t red = 0, green = 0, blue = 0;
            for (size_t row = 0; row < kernel; ++row) {
                const auto& sums = g_windowBlurRowSums[(head + row) % kernel][x];
                red += sums[0];
                green += sums[1];
                blue += sums[2];
            }
            constexpr uint32_t sampleCount = static_cast<uint32_t>(kernel * kernel);
            const uint32_t blurred = 0xFF000000u |
                (((red + sampleCount / 2) / sampleCount) << 16) |
                (((green + sampleCount / 2) / sampleCount) << 8) |
                ((blue + sampleCount / 2) / sampleCount);
            const size_t destination = static_cast<size_t>(y) * width +
                static_cast<size_t>(x);
            const uint32_t tint = (static_cast<uint32_t>(opacity) << 24) |
                (blurred & 0x00FFFFFFu);
            g_frame[destination] = blend(tint, g_frame[destination]);
        }
        if (y + 1 < bottom) {
            head = (head + 1) % kernel;
            captureHorizontal(y + kWindowBlurRadius + 1,
                              (head + kernel - 1) % kernel);
        }
    }
}

void blendWindowShadow(int64_t left, int64_t top, int64_t right, int64_t bottom,
                       uint32_t width, uint32_t height) noexcept {
    // Shadow work is bounded to six strips around the window, not its area.
    const auto draw = [&](int64_t x, int64_t y, int64_t dx, int64_t dy) noexcept {
        if (x < g_composeClip.left || x >= g_composeClip.right ||
            y < g_composeClip.top || y >= g_composeClip.bottom ||
            x < 0 || x >= width || y < 0 || y >= height) return;
        const int64_t distanceSquared = dx * dx + dy * dy;
        const uint32_t alpha = distanceSquared < 4 ? 44u :
            distanceSquared < 9 ? 36u : distanceSquared < 16 ? 28u :
            distanceSquared < 25 ? 20u : distanceSquared < 36 ? 12u : 0u;
        if (!alpha) return;
        const size_t destination = static_cast<size_t>(y) * width +
            static_cast<size_t>(x);
        g_frame[destination] = blend(alpha << 24, g_frame[destination]);
    };
    for (int64_t y = top - kWindowShadowExtent; y < top; ++y) {
        const int64_t dy = top - y;
        for (int64_t x = left - kWindowShadowExtent;
             x < right + kWindowShadowExtent; ++x) {
            const int64_t dx = x < left ? left - x :
                x >= right ? x - right + 1 : 0;
            draw(x, y, dx, dy);
        }
    }
    for (int64_t y = bottom; y < bottom + kWindowShadowExtent; ++y) {
        const int64_t dy = y - bottom + 1;
        for (int64_t x = left - kWindowShadowExtent;
             x < right + kWindowShadowExtent; ++x) {
            const int64_t dx = x < left ? left - x :
                x >= right ? x - right + 1 : 0;
            draw(x, y, dx, dy);
        }
    }
    for (int64_t y = top; y < bottom; ++y) {
        for (int64_t x = left - kWindowShadowExtent; x < left; ++x)
            draw(x, y, left - x, 0);
        for (int64_t x = right; x < right + kWindowShadowExtent; ++x)
            draw(x, y, x - right + 1, 0);
    }
}

void saveWindowCornerBackdrop(int64_t left, int64_t top,
                              int64_t right, int64_t bottom,
                              int64_t radius, uint32_t width,
                              uint32_t height) noexcept {
    for (int64_t y = 0; y < radius; ++y) {
        for (int64_t x = 0; x < radius; ++x) {
            const size_t offset = static_cast<size_t>(
                y * kWindowEffectRadiusLimit + x);
            const int64_t px[4]{left + x, right - radius + x,
                                left + x, right - radius + x};
            const int64_t py[4]{top + y, top + y,
                                bottom - radius + y, bottom - radius + y};
            for (size_t corner = 0; corner < 4; ++corner) {
                g_windowCornerBackdrop[corner][offset] =
                    px[corner] >= g_composeClip.left &&
                    px[corner] < g_composeClip.right &&
                    py[corner] >= g_composeClip.top &&
                    py[corner] < g_composeClip.bottom &&
                    px[corner] >= 0 && px[corner] < width &&
                    py[corner] >= 0 && py[corner] < height
                    ? g_frame[static_cast<size_t>(py[corner]) * width +
                              static_cast<size_t>(px[corner])] : 0;
            }
        }
    }
}

void applyWindowCornerMask(int64_t left, int64_t top,
                           int64_t right, int64_t bottom,
                           int64_t radius, uint32_t width,
                           uint32_t height) noexcept {
    const int64_t radiusSquared = radius * radius;
    for (int64_t y = 0; y < radius; ++y) {
        for (int64_t x = 0; x < radius; ++x) {
            const int64_t leftCenterX = 2 * x + 1 - 2 * radius;
            const int64_t rightCenterX = 2 * x + 1;
            const int64_t topCenterY = 2 * y + 1 - 2 * radius;
            const int64_t bottomCenterY = 2 * y + 1;
            const int64_t centerX[4]{leftCenterX, rightCenterX,
                                     leftCenterX, rightCenterX};
            const int64_t centerY[4]{topCenterY, topCenterY,
                                     bottomCenterY, bottomCenterY};
            const size_t offset = static_cast<size_t>(
                y * kWindowEffectRadiusLimit + x);
            const int64_t px[4]{left + x, right - radius + x,
                                left + x, right - radius + x};
            const int64_t py[4]{top + y, top + y,
                                bottom - radius + y, bottom - radius + y};
            for (size_t corner = 0; corner < 4; ++corner) {
                if (centerX[corner] * centerX[corner] +
                    centerY[corner] * centerY[corner] <= 4 * radiusSquared)
                    continue;
                if (px[corner] < g_composeClip.left ||
                    px[corner] >= g_composeClip.right ||
                    py[corner] < g_composeClip.top ||
                    py[corner] >= g_composeClip.bottom ||
                    px[corner] < 0 || px[corner] >= width ||
                    py[corner] < 0 || py[corner] >= height) continue;
                g_frame[static_cast<size_t>(py[corner]) * width +
                        static_cast<size_t>(px[corner])] =
                    g_windowCornerBackdrop[corner][offset];
            }
        }
    }
}

void drawGlyph(int64_t x, int64_t y, uint32_t character, uint32_t color,
               uint32_t width, uint32_t height) noexcept {
    if (character < 32 || character > 126) character = '?';
    const auto* rows = micant::shared::FONT_8X8[character - 32];
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            if (!(rows[row] & (1u << (7 - column)))) continue;
            const int64_t px = x + column, py = y + row;
            if (px >= g_composeClip.left && px < g_composeClip.right &&
                py >= g_composeClip.top && py < g_composeClip.bottom &&
                px < width && py < height)
            {
                auto& destination = g_frame[static_cast<size_t>(py) * width +
                    static_cast<size_t>(px)];
                const uint32_t alpha =
                    ((color >> 24) * g_windowDrawOpacity + 127u) / 255u;
                if (alpha == 255) destination = color;
                else if (alpha) destination = blend(
                    (alpha << 24) | (color & 0x00FFFFFFu), destination);
            }
        }
    }
}

void drawIconPixel(int64_t x, int64_t y, uint32_t color,
                   uint32_t width, uint32_t height) noexcept {
    fillRect(x, y, x + 2, y + 2, color, width, height);
}

void drawControlIcon(uint8_t kind, bool maximized, int64_t left,
                     int64_t top, uint32_t color,
                     uint32_t width, uint32_t height) noexcept {
    if (kind == 1) { // Minimize: a crisp vector bar.
        fillRect(left + 8, top + 15, left + 18, top + 17, color, width, height);
    } else if (kind == 2 && !maximized) { // Maximize: single outlined window.
        fillRect(left + 7, top + 6, left + 17, top + 8, color, width, height);
        fillRect(left + 7, top + 8, left + 9, top + 17, color, width, height);
        fillRect(left + 15, top + 8, left + 17, top + 17, color, width, height);
        fillRect(left + 7, top + 15, left + 17, top + 17, color, width, height);
    } else if (kind == 2) { // Restore: two offset, overlapping window outlines.
        fillRect(left + 9, top + 5, left + 17, top + 7, color, width, height);
        fillRect(left + 15, top + 7, left + 17, top + 14, color, width, height);
        fillRect(left + 6, top + 8, left + 14, top + 10, color, width, height);
        fillRect(left + 6, top + 10, left + 8, top + 17, color, width, height);
        fillRect(left + 12, top + 10, left + 14, top + 17, color, width, height);
        fillRect(left + 6, top + 15, left + 14, top + 17, color, width, height);
    } else if (kind == 3) { // Close: a hand-drawn, two-pixel diagonal cross.
        for (int64_t step = 0; step < 10; ++step) {
            drawIconPixel(left + 9 + step, top + 6 + step, color, width, height);
            drawIconPixel(left + 18 - step, top + 6 + step, color, width, height);
        }
    }
}

bool composeRegion(const MicaCompositorMetadataStore& metadata,
                   const uint32_t* surfaces, ScreenRect clip,
                   const AnimationFrame* animationFrames,
                   const uint32_t* decorPixels) noexcept {
    const uint32_t width = metadata.screenWidth;
    const uint32_t height = metadata.screenHeight;
    if (width != MICANT_COMPOSITOR_SCREEN_WIDTH ||
        height != MICANT_COMPOSITOR_SCREEN_HEIGHT ||
        metadata.surfaceCapacity > MICANT_COMPOSITOR_SURFACE_CAPACITY ||
        clip.left < 0 || clip.top < 0 || clip.right > width || clip.bottom > height ||
        clip.left >= clip.right || clip.top >= clip.bottom) return false;
    g_composeClip = clip;
    g_windowDrawOpacity = 255;

    const bool themeValid = MicaThemeSnapshotIsValid(&metadata.theme, sizeof(metadata.theme));
    const auto* normalHeaderbar = themeValid
        ? &metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT] : nullptr;
    const auto* inactiveHeaderbar = themeValid
        ? &metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT + 7u] : nullptr;
    const auto* normalButton = themeValid ? &metadata.theme.styles[0] : nullptr;
    const auto* windowStyle = themeValid
        ? &metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT] : nullptr;
    const bool blurRequested = windowStyle &&
        (windowStyle->flags & MICANT_THEME_HAS_BACKDROP_BLUR) &&
        (windowStyle->flags & MICANT_THEME_HAS_OPACITY) &&
        windowStyle->opacity > 0 && windowStyle->opacity < 255;
    if (blurRequested && (clip.left != 0 || clip.top != 0 ||
        clip.right != width || clip.bottom != height)) return false;

    fillRect(clip.left, clip.top, clip.right, clip.bottom, 0xFF1C3A5Cu,
             width, height);

    struct OrderedSurface {
        const MicaCompositorSurfaceInfo* info;
        size_t index;
    };
    OrderedSurface ordered[MICANT_COMPOSITOR_SURFACE_CAPACITY];
    constexpr uint32_t kWsChild = 0x40000000u;
    size_t count = 0;
    for (size_t index = 0; index < metadata.surfaceCapacity; ++index) {
        const auto& surface = metadata.surfaces[index];
        if ((surface.flags & (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                              MICANT_COMPOSITOR_SURFACE_VISIBLE)) !=
                (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                 MICANT_COMPOSITOR_SURFACE_VISIBLE) ||
            ((surface.flags & MICANT_COMPOSITOR_SURFACE_ICONIC) &&
             !animationFrames[index].active) ||
            !surface.window || !surface.width || !surface.height ||
            surface.width > width || surface.height > height ||
            surface.pitch < surface.width ||
            surface.pixelOffset > MICANT_COMPOSITOR_SURFACE_PIXEL_CAPACITY ||
            static_cast<uint64_t>(surface.pitch) * surface.height >
                MICANT_COMPOSITOR_SURFACE_PIXEL_CAPACITY - surface.pixelOffset)
            continue;
        size_t insertion = count;
        while (insertion && ordered[insertion - 1].info->zOrder > surface.zOrder) {
            ordered[insertion] = ordered[insertion - 1];
            --insertion;
        }
        ordered[insertion] = {&surface, index};
        ++count;
    }

    for (size_t index = 0; index < count; ++index) {
        const auto& surface = *ordered[index].info;
        const auto& animation = animationFrames[ordered[index].index];
        constexpr uint32_t kWsBorder = 0x00800000u;
        constexpr uint32_t kWsDlgFrame = 0x00400000u;
        constexpr uint32_t kWsCaption = kWsBorder | kWsDlgFrame;
        constexpr uint32_t kWsThickFrame = 0x00040000u;
        const bool topLevel = (surface.style & kWsChild) == 0;
        // Child HWND pixels with a parent are folded into the top-level
        // window's client pass below. Parentless child-style probe surfaces
        // remain ordinary layers for the compositor self-check.
        if (!topLevel && surface.parentWindow) continue;
        const bool validWindowStyle = topLevel && windowStyle && windowStyle->flags;
        const int64_t themedFrame = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_BORDER_WIDTH) &&
            windowStyle->borderWidth >= 1 && windowStyle->borderWidth <= 16
                ? windowStyle->borderWidth : 0;
        const int64_t sourceFrame = !topLevel ? 0 :
            ((surface.style & kWsThickFrame) ? std::max<int64_t>(themedFrame, 5) :
             ((surface.style & (kWsBorder | kWsDlgFrame))
                ? (themedFrame ? themedFrame : 1) : 0));
        const int64_t sourceCaption = topLevel && (surface.style & kWsCaption)
            ? (normalHeaderbar && (normalHeaderbar->flags & MICANT_THEME_HAS_MIN_HEIGHT) &&
               normalHeaderbar->minHeight >= 24 && normalHeaderbar->minHeight <= 96
                ? normalHeaderbar->minHeight : 28)
            : 0;
        const int64_t windowLeft = animation.active ? animation.x : surface.x;
        const int64_t windowTop = animation.active ? animation.y : surface.y;
        const int64_t windowWidth = animation.active ? animation.width : surface.width;
        const int64_t windowHeight = animation.active ? animation.height : surface.height;
        const int64_t windowRight = windowLeft + windowWidth;
        const int64_t windowBottom = windowTop + windowHeight;
        const int64_t frame = animation.active && surface.width
            ? (sourceFrame * windowWidth + surface.width / 2) / surface.width
            : sourceFrame;
        const int64_t caption = animation.active && surface.height
            ? (sourceCaption * windowHeight + surface.height / 2) / surface.height
            : sourceCaption;
        const int64_t left = windowLeft + frame;
        const int64_t top = windowTop + frame + caption;
        const int64_t clientWidth = windowWidth - frame * 2;
        const int64_t clientHeight = windowHeight - frame * 2 - caption;
        const int64_t sourceClientWidth = surface.width - sourceFrame * 2;
        const int64_t sourceClientHeight = surface.height - sourceFrame * 2 - sourceCaption;
        if (clientWidth <= 0 || clientHeight <= 0) continue;

        const uint8_t opacity = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_OPACITY)
            ? windowStyle->opacity : 255u;
        const bool blurWindow = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_BACKDROP_BLUR) &&
            (windowStyle->flags & MICANT_THEME_HAS_OPACITY) &&
            opacity > 0 && opacity < 255;
        int64_t radius = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_RADIUS)
            ? windowStyle->borderRadius : 0;
        // WhiteSur's decoration selector specifies 12px corners, but Snapshot
        // v2 has no decoration control. Keep Base square and use the CSS value
        // only when the active theme provides a real headerbar style.
        const bool hasThemedHeaderbar = themeValid && normalHeaderbar &&
            (normalHeaderbar->flags & MICANT_THEME_HAS_BACKGROUND);
        if (!radius && hasThemedHeaderbar &&
            !(surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED))
            radius = 12;
        if (surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED) radius = 0;
        if (!topLevel || !frame) radius = 0;
        if (radius > kWindowEffectRadiusLimit) radius = kWindowEffectRadiusLimit;
        if (radius * 2 > windowWidth) radius = windowWidth / 2;
        if (radius * 2 > windowHeight) radius = windowHeight / 2;
        if (blurWindow) captureWindowBlurBackdrop(width, height);
        if (radius > 0) {
            blendWindowShadow(windowLeft, windowTop, windowRight, windowBottom,
                              width, height);
            saveWindowCornerBackdrop(windowLeft, windowTop, windowRight,
                                     windowBottom, radius, width, height);
        }
        g_windowDrawOpacity = opacity;

        if (frame || caption) {
            const bool foreground = (surface.flags & MICANT_COMPOSITOR_SURFACE_FOREGROUND) != 0;
            const auto* headerbarStyle = foreground ? normalHeaderbar : inactiveHeaderbar;
            if (!headerbarStyle || !headerbarStyle->flags) headerbarStyle = normalHeaderbar;
            const auto* captionBackgroundStyle = headerbarStyle &&
                (headerbarStyle->flags & MICANT_THEME_HAS_BACKGROUND)
                    ? headerbarStyle : normalHeaderbar;
            const auto* captionForegroundStyle = headerbarStyle &&
                (headerbarStyle->flags & MICANT_THEME_HAS_FOREGROUND)
                    ? headerbarStyle : normalHeaderbar;
            const auto* captionBorderStyle = headerbarStyle &&
                (headerbarStyle->flags & MICANT_THEME_HAS_BORDER)
                    ? headerbarStyle : normalHeaderbar;
            uint32_t frameColor = foreground ? 0xFF334B66u : 0xFF778492u;
            uint32_t captionColor = foreground ? 0xFF245987u : 0xFF596775u;
            uint32_t titleColor = foreground ? 0xFFF5F8FCu : 0xFFDCE3EAu;
            uint32_t buttonColor = foreground ? 0xFF376D9Au : 0xFF75818Cu;
            uint32_t closeColor = foreground ? 0xFFC84753u : 0xFF916A70u;
            const uint32_t bodyColor = validWindowStyle &&
                (windowStyle->flags & MICANT_THEME_HAS_BACKGROUND)
                ? windowStyle->background : 0xFFFFFFFFu;
            if (captionBackgroundStyle &&
                (captionBackgroundStyle->flags & MICANT_THEME_HAS_BACKGROUND)) {
                captionColor = captionBackgroundStyle->background;
                const uint32_t captionLuma =
                    (((captionColor >> 16) & 0xFFu) * 299u +
                     ((captionColor >> 8) & 0xFFu) * 587u +
                     (captionColor & 0xFFu) * 114u) / 1000u;
                frameColor = (validWindowStyle &&
                    (windowStyle->flags & MICANT_THEME_HAS_BORDER))
                    ? windowStyle->border
                    : (captionBorderStyle &&
                    (captionBorderStyle->flags & MICANT_THEME_HAS_BORDER))
                    ? captionBorderStyle->border
                    : blend((captionLuma >= 128u ? 31u : 32u) << 24,
                            bodyColor);
                buttonColor = captionColor;
                closeColor = captionColor;
            }
            if (captionForegroundStyle &&
                (captionForegroundStyle->flags & MICANT_THEME_HAS_FOREGROUND))
                titleColor = captionForegroundStyle->foreground;
            if (normalButton && (normalButton->flags & MICANT_THEME_HAS_BACKGROUND)) {
                buttonColor = normalButton->background;
                closeColor = buttonColor;
            }
            if (frame) {
                const bool hasWindowStrokeWidth = validWindowStyle &&
                    (windowStyle->flags & MICANT_THEME_HAS_BORDER_WIDTH) &&
                    windowStyle->borderWidth >= 1 && windowStyle->borderWidth <= 16;
                const bool hasHeaderbarStrokeWidth = captionBorderStyle &&
                    (captionBorderStyle->flags & MICANT_THEME_HAS_BORDER_WIDTH) &&
                    captionBorderStyle->borderWidth >= 1 &&
                    captionBorderStyle->borderWidth <= 16;
                const int64_t stroke = std::min<int64_t>(frame,
                    hasWindowStrokeWidth ? windowStyle->borderWidth :
                    hasHeaderbarStrokeWidth ? captionBorderStyle->borderWidth : 1);
                // The resize inset remains five pixels, but only the theme's
                // border width is drawn as a stroke. Its remaining area joins
                // the titlebar or client fill instead of becoming a dark slab.
                fillRect(windowLeft, windowTop, windowRight,
                         windowTop + frame, captionColor, width, height);
                fillRect(windowLeft, windowBottom - frame, windowRight,
                         windowBottom, bodyColor, width, height);
                fillRect(windowLeft, windowTop + frame, windowLeft + frame,
                         windowBottom - frame, bodyColor, width, height);
                fillRect(windowRight - frame, windowTop + frame, windowRight,
                         windowBottom - frame, bodyColor, width, height);
                // Resolve translucent theme borders over their adjacent themed
                // surface before drawing the visible stroke.
                const uint32_t bodyStroke = (frameColor & 0xFF000000u) == 0xFF000000u
                    ? frameColor : blend(frameColor, bodyColor);
                const uint32_t captionStroke =
                    (frameColor & 0xFF000000u) == 0xFF000000u
                    ? frameColor : blend(frameColor, captionColor);
                fillRect(windowLeft, windowTop, windowRight,
                         windowTop + stroke, captionStroke, width, height);
                fillRect(windowLeft, windowBottom - stroke, windowRight,
                         windowBottom, bodyStroke, width, height);
                fillRect(windowLeft, windowTop + stroke, windowLeft + stroke,
                         windowBottom - stroke, bodyStroke, width, height);
                fillRect(windowRight - stroke, windowTop + stroke, windowRight,
                         windowBottom - stroke, bodyStroke, width, height);
            }
            if (caption) {
                fillRect(windowLeft + frame, windowTop + frame,
                         windowRight - frame, windowTop + frame + caption,
                         captionColor, width, height);

                const int64_t closeLeft = windowRight - frame - 30;
                const int64_t buttonTop = windowTop + frame + 3;
                const int64_t buttonBottom = windowTop + frame + caption - 3;
                const auto drawCaptionButton = [&](uint8_t button,
                    int64_t buttonLeft, int64_t buttonRight) noexcept {
                    const bool pressed =
                        (surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_PRESSED) &&
                        ((surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_MASK) >>
                         MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_SHIFT) == button;
                    const bool hovered =
                        (surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_HOVER) &&
                        ((surface.flags & MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_MASK) >>
                         MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_SHIFT) == button;
                    const bool inactive = !foreground;
                    const uint32_t imageState = pressed ? 2u : hovered
                        ? (inactive ? 4u : 1u) : (inactive ? 3u : 0u);
                    const uint32_t imageIndex = (button == 3 ? 0u :
                        button == 1 ? 5u :
                        ((surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED) ? 15u : 10u)) +
                        imageState;
                    bool drewImage = false;
                    const MicaThemeCaptionImage* captionImage = imageIndex <
                        MICANT_THEME_CAPTION_IMAGE_COUNT
                            && metadata.captionImageSet.generation ==
                                metadata.themeGeneration
                            ? &metadata.captionImageSet.images[imageIndex] : nullptr;
                    const bool hasCaptionImage = captionImage && captionImage->width &&
                        captionImage->height &&
                        captionImage->stride >= captionImage->width &&
                        captionImage->width <= MICANT_THEME_CAPTION_IMAGE_SIZE &&
                        captionImage->height <= MICANT_THEME_CAPTION_IMAGE_SIZE;
                    const uint32_t state = pressed ? 2u : hovered ? 1u :
                        foreground ? 0u : 7u;
                    const auto* buttonStyle = themeValid
                        ? &metadata.theme.styles[state] : nullptr;
                    if (!buttonStyle || !buttonStyle->flags) buttonStyle = normalButton;
                    const auto applyOpacity = [&](uint32_t color) noexcept {
                        uint32_t alpha = color >> 24;
                        if (buttonStyle &&
                            (buttonStyle->flags & MICANT_THEME_HAS_OPACITY))
                            alpha = (alpha * buttonStyle->opacity + 127u) / 255u;
                        return (alpha << 24) | (color & 0x00FFFFFFu);
                    };
                    uint32_t background = button == 3 ? closeColor : buttonColor;
                    if (buttonStyle &&
                        (buttonStyle->flags & MICANT_THEME_HAS_BACKGROUND))
                        background = blend(applyOpacity(buttonStyle->background),
                                           captionColor);
                    uint32_t glyph = titleColor;
                    if (buttonStyle &&
                        (buttonStyle->flags & MICANT_THEME_HAS_FOREGROUND))
                        glyph = blend(applyOpacity(buttonStyle->foreground),
                                       captionColor);
                    if (!hasCaptionImage && buttonStyle &&
                        (buttonStyle->flags & MICANT_THEME_HAS_BORDER)) {
                        const uint32_t borderColor = blend(
                            applyOpacity(buttonStyle->border), background);
                        fillRect(buttonLeft, buttonTop, buttonRight, buttonBottom,
                                 borderColor, width, height);
                        fillRect(buttonLeft + 1, buttonTop + 1, buttonRight - 1,
                                 buttonBottom - 1, background, width, height);
                    } else if (!hasCaptionImage) {
                        fillRect(buttonLeft, buttonTop, buttonRight, buttonBottom,
                                 background, width, height);
                    }
                    if (hasCaptionImage) {
                        const int64_t imageLeft = buttonLeft +
                            (buttonRight - buttonLeft - captionImage->width) / 2;
                        const int64_t imageTop = windowTop + frame +
                            (caption - captionImage->height) / 2;
                        for (uint32_t row = 0; row < captionImage->height; ++row) {
                            const int64_t y = imageTop + row;
                            if (y < g_composeClip.top || y >= g_composeClip.bottom ||
                                y < 0 || y >= height) continue;
                            for (uint32_t column = 0; column < captionImage->width; ++column) {
                                const int64_t x = imageLeft + column;
                                if (x < g_composeClip.left || x >= g_composeClip.right ||
                                    x < 0 || x >= width) continue;
                                const uint32_t source = applyPremultipliedOpacity(
                                    metadata.captionImageSet.pixels[imageIndex][
                                        static_cast<size_t>(row) * captionImage->stride + column],
                                    opacity);
                                if (source >> 24) {
                                    const size_t destination = static_cast<size_t>(y) * width +
                                        static_cast<size_t>(x);
                                    g_frame[destination] =
                                        micant::shared::graphics::PremultipliedSourceOver(
                                            source, g_frame[destination]);
                                }
                            }
                        }
                        drewImage = true;
                    }
                    return drewImage ? 0u : glyph;
                };
                if (caption >= 12 && windowWidth >= 96) {
                    const uint32_t minimizeGlyph = drawCaptionButton(
                        1, closeLeft - 58, closeLeft - 30);
                    const uint32_t maximizeGlyph = drawCaptionButton(
                        2, closeLeft - 29, closeLeft - 1);
                    const uint32_t closeGlyph = drawCaptionButton(
                        3, closeLeft, windowRight - frame - 2);

                    const int64_t iconTop = windowTop + frame + (caption - 22) / 2;
                    const bool maximized = (surface.flags & MICANT_COMPOSITOR_SURFACE_MAXIMIZED) != 0;
                    if (minimizeGlyph) drawControlIcon(1, false, closeLeft - 58, iconTop,
                                                       minimizeGlyph, width, height);
                    if (maximizeGlyph) drawControlIcon(2, maximized, closeLeft - 29, iconTop,
                                                       maximizeGlyph, width, height);
                    if (closeGlyph) drawControlIcon(3, false, closeLeft, iconTop,
                                                   closeGlyph, width, height);

                    const int64_t titleLimit = closeLeft - 66;
                    const size_t surfaceSlot = ordered[index].index;
                    const uint64_t iconSlotBegin = MICANT_COMPOSITOR_ICON_PIXEL_OFFSET +
                        static_cast<uint64_t>(surfaceSlot) * MICANT_COMPOSITOR_ICON_TYPES *
                            MICANT_COMPOSITOR_ICON_MAX_PIXELS;
                    const uint64_t iconSlotEnd = iconSlotBegin +
                        static_cast<uint64_t>(MICANT_COMPOSITOR_ICON_TYPES) *
                            MICANT_COMPOSITOR_ICON_MAX_PIXELS;
                    const uint64_t iconEnd = static_cast<uint64_t>(surface.iconPixelOffset) +
                        (surface.iconHeight ? static_cast<uint64_t>(surface.iconHeight - 1) *
                            surface.iconStride + surface.iconWidth : 0);
                    const bool validIcon = surface.iconGeneration &&
                        surface.iconWidth && surface.iconHeight &&
                        surface.iconWidth <= MICANT_COMPOSITOR_ICON_MAX_DIMENSION &&
                        surface.iconHeight <= MICANT_COMPOSITOR_ICON_MAX_DIMENSION &&
                        surface.iconStride >= surface.iconWidth &&
                        surface.iconPixelOffset >= iconSlotBegin && iconEnd <= iconSlotEnd &&
                        iconEnd <= MICANT_COMPOSITOR_DECOR_PIXEL_STORE_SIZE / sizeof(uint32_t);
                    int64_t titleX = windowLeft + frame + 9;
                    if (validIcon) {
                        const int64_t iconLeft = titleX;
                        const int64_t iconTop = windowTop + frame +
                            (caption - static_cast<int64_t>(surface.iconHeight)) / 2;
                        for (uint32_t row = 0; row < surface.iconHeight; ++row) {
                            const int64_t y = iconTop + row;
                            if (y < g_composeClip.top || y >= g_composeClip.bottom ||
                                y < 0 || y >= height) continue;
                            for (uint32_t column = 0; column < surface.iconWidth; ++column) {
                                const int64_t x = iconLeft + column;
                                if (x >= titleLimit || x < g_composeClip.left ||
                                    x >= g_composeClip.right || x < 0 || x >= width) continue;
                                const uint32_t source = decorPixels[surface.iconPixelOffset +
                                    static_cast<size_t>(row) * surface.iconStride + column];
                                const uint32_t sourceAlpha = source >> 24;
                                const uint32_t effectiveAlpha =
                                    (sourceAlpha * opacity + 127u) / 255u;
                                if (effectiveAlpha)
                                    g_frame[static_cast<size_t>(y) * width +
                                        static_cast<size_t>(x)] = blend(
                                            (effectiveAlpha << 24) |
                                                (source & 0x00FFFFFFu),
                                            g_frame[static_cast<size_t>(y) * width +
                                                static_cast<size_t>(x)]);
                            }
                        }
                        titleX += surface.iconWidth + 8;
                    }

                    const uint64_t titleSlotBegin = MICANT_COMPOSITOR_TITLE_PIXEL_OFFSET +
                        static_cast<uint64_t>(surfaceSlot) * MICANT_COMPOSITOR_TITLE_MAX_PIXELS;
                    const uint64_t titleSlotEnd = titleSlotBegin +
                        MICANT_COMPOSITOR_TITLE_MAX_PIXELS;
                    const uint64_t titleEnd = static_cast<uint64_t>(surface.titlePixelOffset) +
                        (surface.titleHeight ? static_cast<uint64_t>(surface.titleHeight - 1) *
                            surface.titleStride + surface.titleWidth : 0);
                    const bool validTitle = surface.titleGeneration && surface.titleWidth &&
                        surface.titleHeight &&
                        surface.titleWidth <= MICANT_COMPOSITOR_TITLE_MAX_WIDTH &&
                        surface.titleHeight <= MICANT_COMPOSITOR_TITLE_MAX_HEIGHT &&
                        surface.titleStride >= surface.titleWidth &&
                        surface.titlePixelOffset == titleSlotBegin &&
                        titleEnd <= titleSlotEnd &&
                        titleEnd <= MICANT_COMPOSITOR_DECOR_PIXEL_STORE_SIZE / sizeof(uint32_t);
                    if (validTitle) {
                        const int64_t titleTop = windowTop + frame +
                            (caption - static_cast<int64_t>(surface.titleHeight)) / 2;
                        for (uint32_t row = 0; row < surface.titleHeight; ++row) {
                            const int64_t y = titleTop + row;
                            if (y < g_composeClip.top || y >= g_composeClip.bottom ||
                                y < 0 || y >= height) continue;
                            for (uint32_t column = 0; column < surface.titleWidth; ++column) {
                                const int64_t x = titleX + column;
                                if (x >= titleLimit) break;
                                if (x < g_composeClip.left || x >= g_composeClip.right ||
                                    x < 0 || x >= width) continue;
                                const uint32_t coverage = decorPixels[surface.titlePixelOffset +
                                    static_cast<size_t>(row) * surface.titleStride + column] >> 24;
                                if (!coverage) continue;
                                const uint32_t effectiveCoverage =
                                    (coverage * opacity + 127u) / 255u;
                                const uint32_t glyph = (effectiveCoverage << 24) |
                                    (titleColor & 0x00FFFFFFu);
                                const size_t destination = static_cast<size_t>(y) * width +
                                    static_cast<size_t>(x);
                                g_frame[destination] = blend(glyph, g_frame[destination]);
                            }
                        }
                    } else {
                        const int64_t titleY = windowTop + frame + (caption - 8) / 2;
                        for (size_t character = 0; character < 63 && surface.title[character] &&
                             titleX + 8 <= titleLimit; ++character, titleX += 8)
                            drawGlyph(titleX, titleY, surface.title[character],
                                      titleColor, width, height);
                    }
                }
            }
        }

        const int64_t clipLeft = left < clip.left ? clip.left : left;
        const int64_t clipTop = top < clip.top ? clip.top : top;
        const int64_t clientRight = left + clientWidth;
        const int64_t clientBottom = top + clientHeight;
        const int64_t right = clientRight < clip.right ? clientRight : clip.right;
        const int64_t bottom = clientBottom < clip.bottom ? clientBottom : clip.bottom;
        if (right <= clipLeft || bottom <= clipTop) continue;
        const bool hasClientBackground = validWindowStyle &&
            (windowStyle->flags & MICANT_THEME_HAS_BACKGROUND);
        if (hasClientBackground || !blurWindow) {
            const uint32_t clientBackground = hasClientBackground
                ? windowStyle->background : 0xFFFFFFFFu;
            fillRect(left, top, clientRight, clientBottom,
                     clientBackground, width, height);
        }
        if (blurWindow)
            applyWindowBackdropBlur(left, top, clientRight, clientBottom,
                                    opacity, width, height);
        for (int64_t y = clipTop; y < bottom; ++y) {
            const size_t sourceY = animation.active
                ? static_cast<size_t>(y - top) * static_cast<size_t>(sourceClientHeight) /
                    static_cast<size_t>(clientHeight)
                : static_cast<size_t>(y - top);
            const size_t sourceRow = surface.pixelOffset + sourceY * surface.pitch;
            const size_t pixelCount = static_cast<size_t>(right - clipLeft);
            uint32_t* destination = g_frame + static_cast<size_t>(y) * width +
                static_cast<size_t>(clipLeft);
            if (!animation.active) {
                const size_t sourceX = static_cast<size_t>(clipLeft - left);
                const uint32_t* source = surfaces + sourceRow + sourceX;
                size_t x = 0;
                while (x < pixelCount) {
                    if ((source[x] >> 24) == 255u && opacity == 255u) {
                        const size_t runStart = x++;
                        while (x < pixelCount && (source[x] >> 24) == 255u) ++x;
                        for (size_t copy = runStart; copy < x; ++copy)
                            destination[copy] = source[copy];
                    } else {
                        uint32_t pixel = applyPremultipliedOpacity(source[x], opacity);
                        if (pixel >> 24)
                            destination[x] = micant::shared::graphics::PremultipliedSourceOver(
                                pixel, destination[x]);
                        ++x;
                    }
                }
            } else {
                for (size_t x = 0; x < pixelCount; ++x) {
                    const int64_t localX = clipLeft + static_cast<int64_t>(x) - left;
                    const size_t sourceX = static_cast<size_t>(localX) *
                        static_cast<size_t>(sourceClientWidth) /
                        static_cast<size_t>(clientWidth);
                    const uint32_t source = applyPremultipliedOpacity(
                        surfaces[sourceRow + sourceX], opacity);
                    if (source >> 24)
                        destination[x] = micant::shared::graphics::PremultipliedSourceOver(
                            source, destination[x]);
                }
            }
        }
        const int64_t parentClientRight = left + clientWidth;
        const int64_t parentClientBottom = top + clientHeight;
        for (size_t childIndex = 0; childIndex < count; ++childIndex) {
            const auto& child = *ordered[childIndex].info;
            if (!(child.style & kWsChild) || !child.parentWindow ||
                child.ownerProcessId != surface.ownerProcessId ||
                !(child.flags & MICANT_COMPOSITOR_SURFACE_VISIBLE)) continue;
            const MicaCompositorSurfaceInfo* ancestor = &child;
            for (size_t depth = 0; ancestor && ancestor->parentWindow &&
                 depth < MICANT_COMPOSITOR_SURFACE_CAPACITY; ++depth) {
                const uint64_t parentHandle = ancestor->parentWindow;
                ancestor = nullptr;
                for (size_t lookup = 0; lookup < count; ++lookup) {
                    if (ordered[lookup].info->window == parentHandle) {
                        ancestor = ordered[lookup].info;
                        break;
                    }
                }
            }
            if (!ancestor || ancestor->window != surface.window) continue;
            const int64_t childLeft = child.x;
            const int64_t childTop = child.y;
            const int64_t childRight = childLeft + child.width;
            const int64_t childBottom = childTop + child.height;
            const int64_t drawLeft = std::max({childLeft, left, clip.left});
            const int64_t drawTop = std::max({childTop, top, clip.top});
            const int64_t drawRight = std::min({childRight, parentClientRight, clip.right});
            const int64_t drawBottom = std::min({childBottom, parentClientBottom, clip.bottom});
            if (drawRight <= drawLeft || drawBottom <= drawTop) continue;
            for (int64_t y = drawTop; y < drawBottom; ++y) {
                const size_t sourceRow = child.pixelOffset +
                    static_cast<size_t>(y - childTop) * child.pitch;
                uint32_t* destination = g_frame + static_cast<size_t>(y) * width +
                    static_cast<size_t>(drawLeft);
                for (int64_t x = drawLeft; x < drawRight; ++x) {
                    const uint32_t source = surfaces[sourceRow +
                        static_cast<size_t>(x - childLeft)];
                    if (source >> 24)
                        destination[x - drawLeft] =
                            micant::shared::graphics::PremultipliedSourceOver(
                                source, destination[x - drawLeft]);
                }
            }
        }
        if (radius > 0)
            applyWindowCornerMask(windowLeft, windowTop, windowRight, windowBottom,
                                  radius, width, height);
        g_windowDrawOpacity = 255;
    }

    const auto& cursor = metadata.cursor;
    if ((cursor.flags & (MICANT_COMPOSITOR_CURSOR_ACTIVE | MICANT_COMPOSITOR_CURSOR_VISIBLE)) ==
            (MICANT_COMPOSITOR_CURSOR_ACTIVE | MICANT_COMPOSITOR_CURSOR_VISIBLE) &&
        cursor.width && cursor.height && cursor.width <= MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION &&
        cursor.height <= MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION && cursor.pitch >= cursor.width &&
        cursor.pitch <= MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION &&
        cursor.hotX < cursor.width && cursor.hotY < cursor.height) {
        const int64_t originX = static_cast<int64_t>(cursor.x) - cursor.hotX;
        const int64_t originY = static_cast<int64_t>(cursor.y) - cursor.hotY;
        for (uint32_t sy = 0; sy < cursor.height; ++sy) {
            const int64_t dy = originY + sy;
            if (dy < clip.top || dy >= clip.bottom) continue;
            for (uint32_t sx = 0; sx < cursor.width; ++sx) {
                const int64_t dx = originX + sx;
                if (dx < clip.left || dx >= clip.right) continue;
                const uint32_t source = cursor.pixels[static_cast<size_t>(sy) * cursor.pitch + sx];
                const size_t destination = static_cast<size_t>(dy) * width + static_cast<size_t>(dx);
                g_frame[destination] = micant::shared::graphics::PremultipliedSourceOver(
                    source, g_frame[destination]);
            }
        }
    }

    return true;
}

bool presentRegion(uint32_t screenWidth, ScreenRect clip) noexcept {
    micant::um::UserPresentSurfaceParams64 present{};
    present.surfaceBufferVa = reinterpret_cast<uint64_t>(
        g_frame + static_cast<size_t>(clip.top) * screenWidth + clip.left);
    present.dstX = static_cast<uint32_t>(clip.left);
    present.dstY = static_cast<uint32_t>(clip.top);
    present.width = static_cast<uint32_t>(clip.right - clip.left);
    present.height = static_cast<uint32_t>(clip.bottom - clip.top);
    present.pitch = screenWidth;
    const uint64_t status = micant::sys::invokeUserService(
        0x81, reinterpret_cast<uint64_t>(&present));
    if (status != 0) {
        if (!g_presentFailureReported) {
            constexpr char failed[] = "[MicaNT Compositor] Frame present failed\r\n";
            print(failed, sizeof(failed) - 1);
            g_presentFailureReported = true;
        }
        return false;
    }
    return true;
}

struct DamageSet {
    ScreenRect regions[MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY]{};
    size_t count{};
};

void addDamageRegion(DamageSet& damage, ScreenRect incoming) noexcept {
    incoming.left = incoming.left < 0 ? 0 : incoming.left;
    incoming.top = incoming.top < 0 ? 0 : incoming.top;
    incoming.right = incoming.right > MICANT_COMPOSITOR_SCREEN_WIDTH
        ? MICANT_COMPOSITOR_SCREEN_WIDTH : incoming.right;
    incoming.bottom = incoming.bottom > MICANT_COMPOSITOR_SCREEN_HEIGHT
        ? MICANT_COMPOSITOR_SCREEN_HEIGHT : incoming.bottom;
    if (incoming.left >= incoming.right || incoming.top >= incoming.bottom) return;
    for (size_t index = 0; index < damage.count;) {
        auto& existing = damage.regions[index];
        const bool separated = incoming.right < existing.left ||
            existing.right < incoming.left || incoming.bottom < existing.top ||
            existing.bottom < incoming.top;
        if (separated) {
            ++index;
            continue;
        }
        incoming.left = incoming.left < existing.left ? incoming.left : existing.left;
        incoming.top = incoming.top < existing.top ? incoming.top : existing.top;
        incoming.right = incoming.right > existing.right ? incoming.right : existing.right;
        incoming.bottom = incoming.bottom > existing.bottom ? incoming.bottom : existing.bottom;
        damage.regions[index] = damage.regions[--damage.count];
        index = 0;
    }
    if (damage.count == MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY) {
        damage.regions[0] = {0, 0, MICANT_COMPOSITOR_SCREEN_WIDTH,
                             MICANT_COMPOSITOR_SCREEN_HEIGHT};
        damage.count = 1;
        return;
    }
    damage.regions[damage.count++] = incoming;
}

bool collectDamage(const MicaCompositorMetadataStore& metadata,
                   DamageSet& damage) noexcept {
    constexpr ScreenRect fullScreen{
        0, 0, MICANT_COMPOSITOR_SCREEN_WIDTH, MICANT_COMPOSITOR_SCREEN_HEIGHT};
    const bool blurEnabled =
        MicaThemeSnapshotIsValid(&metadata.theme, sizeof(metadata.theme)) &&
        (metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].flags &
         MICANT_THEME_HAS_BACKDROP_BLUR) &&
        (metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].flags &
         MICANT_THEME_HAS_OPACITY) &&
        metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].opacity > 0 &&
        metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT].opacity < 255;
    bool fullRedraw = blurEnabled || !g_hasPresentedFrame ||
        (metadata.dirtyFlags & MICANT_COMPOSITOR_DIRTY_FULL) != 0 ||
        (metadata.dirtyFlags & ~MICANT_COMPOSITOR_DIRTY_FULL) != 0 ||
        metadata.themeGeneration != g_lastThemeGeneration ||
        metadata.dirtyRegionCount > MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY;
    if (!fullRedraw) {
        for (size_t index = 0; index < metadata.dirtyRegionCount; ++index) {
            const auto& dirty = metadata.dirtyRegions[index];
            if (dirty.left < 0 || dirty.top < 0 ||
                dirty.right > static_cast<int32_t>(MICANT_COMPOSITOR_SCREEN_WIDTH) ||
                dirty.bottom > static_cast<int32_t>(MICANT_COMPOSITOR_SCREEN_HEIGHT) ||
                dirty.left >= dirty.right || dirty.top >= dirty.bottom) {
                fullRedraw = true;
                break;
            }
        }
    }

    if (fullRedraw) {
        damage.regions[0] = fullScreen;
        damage.count = 1;
        return true;
    }

    damage.count = metadata.dirtyRegionCount;
    const bool themeValid =
        MicaThemeSnapshotIsValid(&metadata.theme, sizeof(metadata.theme));
    const auto& windowStyle =
        metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT];
    const auto& headerbarStyle =
        metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT];
    const bool shadowEnabled = themeValid &&
        (((windowStyle.flags & MICANT_THEME_HAS_RADIUS) &&
          windowStyle.borderRadius != 0) ||
         (headerbarStyle.flags & MICANT_THEME_HAS_BACKGROUND));
    for (size_t index = 0; index < metadata.dirtyRegionCount; ++index) {
        const auto& dirty = metadata.dirtyRegions[index];
        const int64_t margin = shadowEnabled ? kWindowShadowExtent : 0;
        const ScreenRect region{
            dirty.left > margin ? dirty.left - margin : 0,
            dirty.top > margin ? dirty.top - margin : 0,
            dirty.right + margin < MICANT_COMPOSITOR_SCREEN_WIDTH
                ? dirty.right + margin : MICANT_COMPOSITOR_SCREEN_WIDTH,
            dirty.bottom + margin < MICANT_COMPOSITOR_SCREEN_HEIGHT
                ? dirty.bottom + margin : MICANT_COMPOSITOR_SCREEN_HEIGHT};
        damage.regions[index] = region;
    }
    return true;
}

bool composeDamageRegions(const MicaCompositorMetadataStore& metadata,
                          const uint32_t* surfaces,
                          const AnimationFrame* frames,
                          const uint32_t* decorPixels,
                          const DamageSet& damage) noexcept {
    for (size_t index = 0; index < damage.count; ++index)
        if (!composeRegion(metadata, surfaces, damage.regions[index], frames,
                           decorPixels))
            return false;
    return true;
}

bool presentDamage(uint32_t screenWidth, const DamageSet& damage) noexcept {
    for (size_t index = 0; index < damage.count; ++index)
        if (!presentRegion(screenWidth, damage.regions[index])) return false;
    return true;
}

bool verifyWindowEffectsRasterizer() noexcept {
    auto& metadata = g_effectProbeMetadata;
    // These bounded probe buffers are static-storage objects and start zeroed.
    metadata.magic = MICANT_COMPOSITOR_STORE_MAGIC;
    metadata.version = MICANT_COMPOSITOR_STORE_VERSION;
    metadata.screenWidth = MICANT_COMPOSITOR_SCREEN_WIDTH;
    metadata.screenHeight = MICANT_COMPOSITOR_SCREEN_HEIGHT;
    metadata.surfaceCapacity = 2;
    metadata.theme.magic = MICANT_THEME_SNAPSHOT_MAGIC;
    metadata.theme.version = MICANT_THEME_SNAPSHOT_VERSION;
    metadata.theme.headerSize = static_cast<uint16_t>(offsetof(MicaThemeSnapshot, styles));
    metadata.theme.totalSize = sizeof(MicaThemeSnapshot);
    metadata.theme.controlCount = MICANT_THEME_CONTROL_COUNT;
    metadata.theme.stateCount = MICANT_THEME_STATE_COUNT;
    auto& style = metadata.theme.styles[14u * MICANT_THEME_STATE_COUNT];
    style.flags = MICANT_THEME_HAS_RADIUS | MICANT_THEME_HAS_OPACITY |
        MICANT_THEME_HAS_BACKDROP_BLUR;
    style.borderRadius = 8;
    style.opacity = 128;

    auto& underlay = g_effectProbeSurfaces[0];
    underlay = {};
    underlay.window = 1;
    underlay.x = 90;
    underlay.y = 90;
    underlay.width = underlay.height = 100;
    underlay.pitch = 100;
    underlay.zOrder = 1;
    underlay.style = 0x40000000u; // child: no frame, no window effects.
    underlay.flags = MICANT_COMPOSITOR_SURFACE_ACTIVE |
        MICANT_COMPOSITOR_SURFACE_VISIBLE;
    metadata.surfaces[0] = underlay;

    auto& surface = g_effectProbeSurfaces[1];
    surface = {};
    surface.window = 2;
    surface.x = 100;
    surface.y = 100;
    surface.width = surface.height = surface.pitch = 64;
    surface.pixelOffset = 128u * 128u;
    surface.zOrder = 2;
    surface.style = 0x00040000u | 0x00800000u | 0x00400000u;
    surface.flags = MICANT_COMPOSITOR_SURFACE_ACTIVE |
        MICANT_COMPOSITOR_SURFACE_VISIBLE |
        MICANT_COMPOSITOR_SURFACE_FOREGROUND;
    metadata.surfaces[1] = surface;
    for (auto& pixel : g_effectProbePixels) pixel = 0;
    for (uint32_t y = 0; y < 100; ++y)
        for (uint32_t x = 0; x < 100; ++x)
            g_effectProbePixels[static_cast<size_t>(y) * 100 + x] =
                ((x + y) & 1u) ? 0xFFFFFFFFu : 0xFF000000u;
    for (uint32_t pixel = 0; pixel < 64u * 64u; ++pixel)
        g_effectProbePixels[128u * 128u + pixel] = 0x80808080u;
    constexpr uint32_t background = 0xFF000000u;
    constexpr ScreenRect clip{0, 0, MICANT_COMPOSITOR_SCREEN_WIDTH,
                              MICANT_COMPOSITOR_SCREEN_HEIGHT};
    if (!composeRegion(metadata, g_effectProbePixels, clip,
                       g_effectProbeAnimations, nullptr)) return false;
    const uint32_t blurredBackdrop = blend(0x807A7A7Au, background);
    const uint32_t expectedClient =
        micant::shared::graphics::PremultipliedSourceOver(
            0x40404040u, blurredBackdrop);
    const uint32_t unblurredClient =
        micant::shared::graphics::PremultipliedSourceOver(
            0x40404040u, background);
    const uint32_t expectedShadow = blend(0x2C000000u, 0xFFFFFFFFu);
    const size_t corner = 100u * MICANT_COMPOSITOR_SCREEN_WIDTH + 100u;
    const size_t client = 140u * MICANT_COMPOSITOR_SCREEN_WIDTH + 120u;
    const size_t shadow = 99u * MICANT_COMPOSITOR_SCREEN_WIDTH + 120u;
    if (g_frame[corner] != 0xFF000000u) {
        constexpr char message[] = "[MicaNT Compositor] Rounded corner pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }
    if (g_frame[client] != expectedClient || expectedClient == unblurredClient) {
        constexpr char message[] = "[MicaNT Compositor] Window opacity pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }
    if (g_frame[shadow] != expectedShadow) {
        constexpr char message[] = "[MicaNT Compositor] Window shadow pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }

    // An empty/invalid theme snapshot style retains the opaque square Base path.
    style.flags = 0;
    style.borderRadius = 0;
    style.opacity = 255;
    if (!composeRegion(metadata, g_effectProbePixels, clip,
                       g_effectProbeAnimations, nullptr)) return false;
    if (g_frame[corner] != 0xFF334B66u) {
        constexpr char message[] = "[MicaNT Compositor] Base window pixel mismatch\r\n";
        print(message, sizeof(message) - 1);
        return false;
    }
    return true;
}

bool copyStableMetadata(const MicaCompositorMetadataStore* source,
                        MicaCompositorMetadataStore& destination,
                        uint64_t& sequence) noexcept {
    sequence = __atomic_load_n(&source->frameSequence, __ATOMIC_ACQUIRE);
    if (sequence & 1u) return false;
    const auto* src = reinterpret_cast<const volatile uint8_t*>(source);
    auto* dst = reinterpret_cast<uint8_t*>(&destination);
    for (size_t index = 0; index < sizeof(destination); ++index)
        dst[index] = src[index];
    return __atomic_load_n(&source->frameSequence, __ATOMIC_ACQUIRE) == sequence;
}

} // namespace

extern "C" void _start() noexcept {
    const auto* metadata = reinterpret_cast<const MicaCompositorMetadataStore*>(
        MICANT_COMPOSITOR_METADATA_BASE);
    const auto* pixels = reinterpret_cast<const uint32_t*>(
        MICANT_COMPOSITOR_PIXELS_BASE);
    const auto* decorPixels = reinterpret_cast<const uint32_t*>(
        MICANT_COMPOSITOR_DECOR_PIXELS_BASE);
    constexpr char startupMessage[] = "[MicaNT Compositor] Ring 3 compositor entered\r\n";
    print(startupMessage, sizeof(startupMessage) - 1);
    constexpr char effectPassed[] =
        "[MicaNT Compositor] Window opacity, radius, shadow and Base pixels verified\r\n";
    constexpr char effectFailed[] =
        "[MicaNT Compositor] Window software effect pixel probe failed\r\n";
    const bool effectsPassed = verifyWindowEffectsRasterizer();
    const char* effectMessage = effectsPassed ? effectPassed : effectFailed;
    const size_t effectMessageLength = effectsPassed
        ? sizeof(effectPassed) - 1 : sizeof(effectFailed) - 1;
    print(effectMessage, effectMessageLength);
    micant::um::UserVideoInfo64 displayInfo{};
    if (micant::sys::invokeUserService(0x80,
            reinterpret_cast<uint64_t>(&displayInfo)) == 0) {
        const auto transport = static_cast<micant::um::DisplayTransportBackend>(
            displayInfo.transportBackend);
        const auto composition = static_cast<micant::um::DisplayCompositionBackend>(
            displayInfo.compositionBackend);
        if (transport == micant::um::DisplayTransportBackend::Virtio2D &&
            composition == micant::um::DisplayCompositionBackend::Software) {
            constexpr char message[] = "[MicaNT Compositor] Backend: SOFTWARE_COMPOSITOR + VIRTIO_2D_SCANOUT\r\n";
            print(message, sizeof(message) - 1);
        } else if (transport == micant::um::DisplayTransportBackend::Virgl &&
                   composition == micant::um::DisplayCompositionBackend::VirglAccelerated) {
            constexpr char message[] = "[MicaNT Compositor] Backend: VIRGL_ACCELERATED\r\n";
            print(message, sizeof(message) - 1);
        } else if (transport == micant::um::DisplayTransportBackend::Virgl) {
            constexpr char message[] = "[MicaNT Compositor] Backend: VirGL available; software composition active\r\n";
            print(message, sizeof(message) - 1);
        } else if (transport == micant::um::DisplayTransportBackend::Gop) {
            constexpr char message[] = "[MicaNT Compositor] Backend: GOP_FALLBACK + SOFTWARE_COMPOSITOR\r\n";
            print(message, sizeof(message) - 1);
        } else {
            constexpr char message[] = "[MicaNT Compositor] Backend query returned unknown transport\r\n";
            print(message, sizeof(message) - 1);
        }
    } else {
        constexpr char unavailable[] = "[MicaNT Compositor] Backend capability query failed\r\n";
        print(unavailable, sizeof(unavailable) - 1);
    }
    uint64_t previous = UINT64_MAX;
    bool metadataObserved = false;
    bool invalidHeaderReported = false;
    bool firstPresentationReported = false;
    const auto waitForUpdate = [&](uint64_t observed,
                                   uint64_t timeout100ns = 0) noexcept {
        (void)micant::sys::invokeUserService(0x83, observed, timeout100ns);
    };
    for (;;) {
        uint64_t sequence{};
        if (!copyStableMetadata(metadata, g_metadataSnapshot, sequence)) {
            waitForUpdate(sequence);
            continue;
        }
        const auto& snapshot = g_metadataSnapshot;
        if (snapshot.magic != MICANT_COMPOSITOR_STORE_MAGIC ||
            snapshot.version != MICANT_COMPOSITOR_STORE_VERSION) {
            if (!invalidHeaderReported) {
                constexpr char invalid[] = "[MicaNT Compositor] Shared metadata header invalid\r\n";
                print(invalid, sizeof(invalid) - 1);
                invalidHeaderReported = true;
            }
            waitForUpdate(sequence);
            continue;
        }
        if (!sequence || (sequence & 1u)) {
            waitForUpdate(sequence);
            continue;
        }
        auto* animationFrames = g_animationFrames;
        buildAnimationFrames(snapshot, animationClockMilliseconds(), animationFrames);
        bool animationActive = false;
        bool animationDamagePending = false;
        for (size_t index = 0; index < MICANT_COMPOSITOR_SURFACE_CAPACITY; ++index) {
            const auto& frame = animationFrames[index];
            animationActive |= frame.active;
            animationDamagePending |= frame.damageCount != 0;
        }
        const bool newMetadata = sequence != previous;
        if (!newMetadata && !animationDamagePending) {
            waitForUpdate(sequence);
            continue;
        }
        bool surfaceWriteInProgress = false;
        if (snapshot.surfaceCapacity <= MICANT_COMPOSITOR_SURFACE_CAPACITY) {
            for (size_t index = 0; index < snapshot.surfaceCapacity; ++index) {
                const auto& surface = snapshot.surfaces[index];
                if ((surface.flags & (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                                      MICANT_COMPOSITOR_SURFACE_VISIBLE)) ==
                        (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                         MICANT_COMPOSITOR_SURFACE_VISIBLE) &&
                    (!(surface.flags & MICANT_COMPOSITOR_SURFACE_ICONIC) ||
                     (surface.animationFlags & MICANT_COMPOSITOR_ANIMATION_ACTIVE)) &&
                    surface.activeWriters) {
                    surfaceWriteInProgress = true;
                    break;
                }
            }
        }
        if (surfaceWriteInProgress) {
            waitForUpdate(sequence);
            continue;
        }
        if (!metadataObserved) {
            constexpr char observed[] = "[MicaNT Compositor] Shared frame metadata observed\r\n";
            print(observed, sizeof(observed) - 1);
            metadataObserved = true;
        }
        DamageSet damage{};
        bool composed = !newMetadata || collectDamage(snapshot, damage);
        for (size_t animationIndex = 0;
             animationIndex < MICANT_COMPOSITOR_SURFACE_CAPACITY; ++animationIndex) {
            const auto& frame = animationFrames[animationIndex];
            for (size_t index = 0; index < frame.damageCount; ++index)
                addDamageRegion(damage, frame.damage[index]);
        }
        if (__atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) != sequence)
            continue;
        if (composed)
            composed = composeDamageRegions(snapshot, pixels, animationFrames,
                                             decorPixels, damage) &&
                presentDamage(snapshot.screenWidth, damage);
        if (!composed) {
            constexpr ScreenRect fullScreen{
                0, 0, MICANT_COMPOSITOR_SCREEN_WIDTH, MICANT_COMPOSITOR_SCREEN_HEIGHT};
            composed = composeRegion(snapshot, pixels, fullScreen, animationFrames,
                                     decorPixels) &&
                presentRegion(snapshot.screenWidth, fullScreen);
        }
        if (__atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) == sequence) {
            previous = sequence;
            if (composed && !firstPresentationReported) {
                constexpr char presented[] = "[MicaNT Compositor] First shared frame presented\r\n";
                print(presented, sizeof(presented) - 1);
                firstPresentationReported = true;
            }
            if (composed) {
                auto* writableMetadata = const_cast<MicaCompositorMetadataStore*>(metadata);
                __atomic_store_n(&writableMetadata->compositorAckSequence,
                                 sequence, __ATOMIC_RELEASE);
                g_hasPresentedFrame = true;
                g_lastThemeGeneration = snapshot.themeGeneration;
            }
        }
        if (animationActive &&
            __atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) == sequence)
            waitForUpdate(sequence, kAnimationFrameInterval100ns);
    }
}
