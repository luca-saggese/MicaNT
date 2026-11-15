#include "micant/shared/compositor_surface.h"
#include "micant/shared/font8x8.hpp"
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
    for (int64_t y = top; y < bottom; ++y)
        for (int64_t x = left; x < right; ++x)
            g_frame[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = color;
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
                   const uint32_t* surfaces, ScreenRect clip) noexcept {
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

    const MicaCompositorSurfaceInfo* ordered[MICANT_COMPOSITOR_SURFACE_CAPACITY]{};
    size_t count = 0;
    for (size_t index = 0; index < metadata.surfaceCapacity; ++index) {
        const auto& surface = metadata.surfaces[index];
        if ((surface.flags & (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                              MICANT_COMPOSITOR_SURFACE_VISIBLE)) !=
                (MICANT_COMPOSITOR_SURFACE_ACTIVE |
                 MICANT_COMPOSITOR_SURFACE_VISIBLE) ||
            (surface.flags & MICANT_COMPOSITOR_SURFACE_ICONIC) ||
            !surface.window || !surface.width || !surface.height ||
            surface.width > width || surface.height > height ||
            surface.pitch < surface.width ||
            surface.pixelOffset > MICANT_COMPOSITOR_PIXEL_STORE_SIZE / sizeof(uint32_t) ||
            static_cast<uint64_t>(surface.pitch) * surface.height >
                MICANT_COMPOSITOR_PIXEL_STORE_SIZE / sizeof(uint32_t) - surface.pixelOffset)
            continue;
        size_t insertion = count;
        while (insertion && ordered[insertion - 1]->zOrder > surface.zOrder) {
            ordered[insertion] = ordered[insertion - 1];
            --insertion;
        }
        ordered[insertion] = &surface;
        ++count;
    }

    for (size_t index = 0; index < count; ++index) {
        const auto& surface = *ordered[index];
        constexpr uint32_t kWsBorder = 0x00800000u;
        constexpr uint32_t kWsDlgFrame = 0x00400000u;
        constexpr uint32_t kWsCaption = kWsBorder | kWsDlgFrame;
        constexpr uint32_t kWsThickFrame = 0x00040000u;
        constexpr uint32_t kWsChild = 0x40000000u;
        const bool topLevel = (surface.style & kWsChild) == 0;
        const int64_t frame = !topLevel ? 0 :
            ((surface.style & kWsThickFrame) ? 5 :
             ((surface.style & (kWsBorder | kWsDlgFrame)) ? 1 : 0));
        const int64_t caption = topLevel && (surface.style & kWsCaption)
            ? (normalHeaderbar && (normalHeaderbar->flags & MICANT_THEME_HAS_MIN_HEIGHT) &&
               normalHeaderbar->minHeight >= 24 && normalHeaderbar->minHeight <= 96
                ? normalHeaderbar->minHeight : 28)
            : 0;
        const int64_t windowLeft = surface.x;
        const int64_t windowTop = surface.y;
        const int64_t windowRight = windowLeft + surface.width;
        const int64_t windowBottom = windowTop + surface.height;
        const int64_t left = windowLeft + frame;
        const int64_t top = windowTop + frame + caption;
        const int64_t clientWidth = surface.width - frame * 2;
        const int64_t clientHeight = surface.height - frame * 2 - caption;
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
                                closeGlyph,
                                width, height);

                int64_t titleX = windowLeft + frame + 9;
                const int64_t titleLimit = closeLeft - 66;
                const int64_t titleY = windowTop + frame + (caption - 8) / 2;
                for (size_t character = 0; character < 63 && surface.title[character] &&
                     titleX + 8 <= titleLimit; ++character, titleX += 8)
                    drawGlyph(titleX, titleY, surface.title[character],
                              titleColor, width, height);
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
            const size_t sourceY = static_cast<size_t>(y - top);
            const size_t sourceRow = surface.pixelOffset + sourceY * surface.pitch;
            const size_t destinationRow = static_cast<size_t>(y) * width;
            for (int64_t x = clipLeft; x < right; ++x) {
                const size_t sourceX = static_cast<size_t>(x - left);
                const size_t destination = destinationRow + static_cast<size_t>(x);
                g_frame[destination] = blend(surfaces[sourceRow + sourceX],
                                             g_frame[destination]);
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

bool composeDamage(const MicaCompositorMetadataStore& metadata,
                   const uint32_t* surfaces, DamageSet& damage) noexcept {
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
        return composeRegion(metadata, surfaces, fullScreen);
    }

    damage.count = metadata.dirtyRegionCount;
    for (size_t index = 0; index < metadata.dirtyRegionCount; ++index) {
        const auto& dirty = metadata.dirtyRegions[index];
        const ScreenRect region{dirty.left, dirty.top, dirty.right, dirty.bottom};
        damage.regions[index] = region;
        if (!composeRegion(metadata, surfaces, region)) return false;
    }
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
    const auto waitForUpdate = [&](uint64_t observed) noexcept {
        (void)micant::sys::invokeUserService(0x83, observed);
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
        if (!sequence || (sequence & 1u) ||
            sequence == previous) {
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
                    !(surface.flags & MICANT_COMPOSITOR_SURFACE_ICONIC) &&
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
        bool composed = composeDamage(snapshot, pixels, damage);
        if (__atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) != sequence)
            continue;
        if (composed) composed = presentDamage(snapshot.screenWidth, damage);
        if (!composed) {
            constexpr ScreenRect fullScreen{
                0, 0, MICANT_COMPOSITOR_SCREEN_WIDTH, MICANT_COMPOSITOR_SCREEN_HEIGHT};
            composed = composeRegion(snapshot, pixels, fullScreen) &&
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
    }
}
