#include "micant/shared/compositor_surface.h"
#include "micant/shared/font8x8.hpp"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/pixel_composite.h"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"

#include <stddef.h>
#include <stdint.h>

namespace {

uint32_t g_frame[MICANT_COMPOSITOR_SURFACE_PIXELS]{};
MicaCompositorMetadataStore g_metadataSnapshot{};
bool g_presentFailureReported{};
bool g_hasPresentedFrame{};
uint64_t g_lastThemeGeneration{};

struct ScreenRect {
    int64_t left{}, top{}, right{}, bottom{};
};

ScreenRect g_composeClip{};
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
        for (size_t x = 0; x < rowWidth; ++x) row[x] = color;
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
                g_frame[static_cast<size_t>(py) * width + static_cast<size_t>(px)] = color;
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
                   const AnimationFrame* animationFrames) noexcept {
    const uint32_t width = metadata.screenWidth;
    const uint32_t height = metadata.screenHeight;
    if (width != MICANT_COMPOSITOR_SCREEN_WIDTH ||
        height != MICANT_COMPOSITOR_SCREEN_HEIGHT ||
        metadata.surfaceCapacity > MICANT_COMPOSITOR_SURFACE_CAPACITY ||
        clip.left < 0 || clip.top < 0 || clip.right > width || clip.bottom > height ||
        clip.left >= clip.right || clip.top >= clip.bottom) return false;
    g_composeClip = clip;

    const bool themeValid = MicaThemeSnapshotIsValid(&metadata.theme, sizeof(metadata.theme));
    const auto* normalHeaderbar = themeValid
        ? &metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT] : nullptr;
    const auto* inactiveHeaderbar = themeValid
        ? &metadata.theme.styles[13u * MICANT_THEME_STATE_COUNT + 7u] : nullptr;
    const auto* normalButton = themeValid ? &metadata.theme.styles[0] : nullptr;

    fillRect(clip.left, clip.top, clip.right, clip.bottom, 0xFF1C3A5Cu,
             width, height);

    struct OrderedSurface {
        const MicaCompositorSurfaceInfo* info{};
        size_t index{};
    };
    OrderedSurface ordered[MICANT_COMPOSITOR_SURFACE_CAPACITY]{};
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
            surface.pixelOffset > MICANT_COMPOSITOR_PIXEL_STORE_SIZE / sizeof(uint32_t) ||
            static_cast<uint64_t>(surface.pitch) * surface.height >
                MICANT_COMPOSITOR_PIXEL_STORE_SIZE / sizeof(uint32_t) - surface.pixelOffset)
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
        constexpr uint32_t kWsChild = 0x40000000u;
        const bool topLevel = (surface.style & kWsChild) == 0;
        const int64_t sourceFrame = !topLevel ? 0 :
            ((surface.style & kWsThickFrame) ? 5 :
             ((surface.style & (kWsBorder | kWsDlgFrame)) ? 1 : 0));
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
            if (captionBackgroundStyle &&
                (captionBackgroundStyle->flags & MICANT_THEME_HAS_BACKGROUND)) {
                captionColor = captionBackgroundStyle->background | 0xFF000000u;
                frameColor = (captionBorderStyle &&
                    (captionBorderStyle->flags & MICANT_THEME_HAS_BORDER))
                    ? captionBorderStyle->border | 0xFF000000u
                    : (foreground ? 0xFF9B9B9Bu : 0xFFB0B0B0u);
                buttonColor = captionColor;
                closeColor = captionColor;
            }
            if (captionForegroundStyle &&
                (captionForegroundStyle->flags & MICANT_THEME_HAS_FOREGROUND))
                titleColor = captionForegroundStyle->foreground | 0xFF000000u;
            if (normalButton && (normalButton->flags & MICANT_THEME_HAS_BACKGROUND)) {
                buttonColor = normalButton->background | 0xFF000000u;
                closeColor = buttonColor;
            }
            fillRect(windowLeft, windowTop, windowRight, windowBottom,
                     frameColor, width, height);
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
                    if (buttonStyle &&
                        (buttonStyle->flags & MICANT_THEME_HAS_BORDER)) {
                        const uint32_t borderColor = blend(
                            applyOpacity(buttonStyle->border), background);
                        fillRect(buttonLeft, buttonTop, buttonRight, buttonBottom,
                                 borderColor, width, height);
                        fillRect(buttonLeft + 1, buttonTop + 1, buttonRight - 1,
                                 buttonBottom - 1, background, width, height);
                    } else {
                        fillRect(buttonLeft, buttonTop, buttonRight, buttonBottom,
                                 background, width, height);
                    }
                    return glyph;
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
                    drawControlIcon(1, false, closeLeft - 58, iconTop,
                                    minimizeGlyph, width, height);
                    drawControlIcon(2, maximized, closeLeft - 29, iconTop,
                                    maximizeGlyph, width, height);
                    drawControlIcon(3, false, closeLeft, iconTop,
                                    closeGlyph, width, height);

                    int64_t titleX = windowLeft + frame + 9;
                    const int64_t titleLimit = closeLeft - 66;
                    const int64_t titleY = windowTop + frame + (caption - 8) / 2;
                    for (size_t character = 0; character < 63 && surface.title[character] &&
                         titleX + 8 <= titleLimit; ++character, titleX += 8)
                        drawGlyph(titleX, titleY, surface.title[character],
                                  titleColor, width, height);
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
                    if ((source[x] >> 24) == 255u) {
                        const size_t runStart = x++;
                        while (x < pixelCount && (source[x] >> 24) == 255u) ++x;
                        for (size_t copy = runStart; copy < x; ++copy)
                            destination[copy] = source[copy];
                    } else {
                        const uint32_t alpha = source[x] >> 24;
                        if (alpha) destination[x] = blend(source[x], destination[x]);
                        ++x;
                    }
                }
            } else {
                for (size_t x = 0; x < pixelCount; ++x) {
                    const int64_t localX = clipLeft + static_cast<int64_t>(x) - left;
                    const size_t sourceX = static_cast<size_t>(localX) *
                        static_cast<size_t>(sourceClientWidth) /
                        static_cast<size_t>(clientWidth);
                    const uint32_t source = surfaces[sourceRow + sourceX];
                    const uint32_t alpha = source >> 24;
                    if (alpha == 255u) destination[x] = source;
                    else if (alpha) destination[x] = blend(source, destination[x]);
                }
            }
        }
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
    bool fullRedraw = !g_hasPresentedFrame ||
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
    for (size_t index = 0; index < metadata.dirtyRegionCount; ++index) {
        const auto& dirty = metadata.dirtyRegions[index];
        const ScreenRect region{dirty.left, dirty.top, dirty.right, dirty.bottom};
        damage.regions[index] = region;
    }
    return true;
}

bool composeDamageRegions(const MicaCompositorMetadataStore& metadata,
                          const uint32_t* surfaces,
                          const AnimationFrame* frames,
                          const DamageSet& damage) noexcept {
    for (size_t index = 0; index < damage.count; ++index)
        if (!composeRegion(metadata, surfaces, damage.regions[index], frames))
            return false;
    return true;
}

bool presentDamage(uint32_t screenWidth, const DamageSet& damage) noexcept {
    for (size_t index = 0; index < damage.count; ++index)
        if (!presentRegion(screenWidth, damage.regions[index])) return false;
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
    constexpr char startupMessage[] = "[MicaNT Compositor] Ring 3 compositor entered\r\n";
    print(startupMessage, sizeof(startupMessage) - 1);
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
            composed = composeDamageRegions(snapshot, pixels, animationFrames, damage) &&
                presentDamage(snapshot.screenWidth, damage);
        if (!composed) {
            constexpr ScreenRect fullScreen{
                0, 0, MICANT_COMPOSITOR_SCREEN_WIDTH, MICANT_COMPOSITOR_SCREEN_HEIGHT};
            composed = composeRegion(snapshot, pixels, fullScreen, animationFrames) &&
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
