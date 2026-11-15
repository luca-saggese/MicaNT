#include "micant/shared/compositor_surface.h"
#include "micant/shared/font8x8.hpp"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"

#include <stddef.h>
#include <stdint.h>

namespace {

uint32_t g_frame[MICANT_COMPOSITOR_SURFACE_PIXELS]{};
bool g_presentFailureReported{};

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
            if (px >= 0 && px < width && py >= 0 && py < height)
                g_frame[static_cast<size_t>(py) * width + static_cast<size_t>(px)] = color;
        }
    }
}

bool compose(const MicaCompositorMetadataStore& metadata,
             const uint32_t* surfaces) noexcept {
    const uint32_t width = metadata.screenWidth;
    const uint32_t height = metadata.screenHeight;
    if (width != MICANT_COMPOSITOR_SCREEN_WIDTH ||
        height != MICANT_COMPOSITOR_SCREEN_HEIGHT ||
        metadata.surfaceCapacity > MICANT_COMPOSITOR_SURFACE_CAPACITY) return false;

    for (size_t pixel = 0; pixel < MICANT_COMPOSITOR_SURFACE_PIXELS; ++pixel)
        g_frame[pixel] = 0xFF1C3A5Cu;

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
        const int64_t caption = topLevel && (surface.style & kWsCaption) ? 28 : 0;
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
            fillRect(windowLeft, windowTop, windowRight, windowBottom,
                     0xFF374E6Cu, width, height);
            if (caption) {
                const uint32_t titleColor = 0xFF244F7Eu;
                fillRect(windowLeft + frame, windowTop + frame,
                         windowRight - frame, windowTop + frame + caption,
                         titleColor, width, height);

                const int64_t closeLeft = windowRight - frame - 30;
                const int64_t buttonTop = windowTop + frame + 3;
                const int64_t buttonBottom = windowTop + frame + caption - 3;
                fillRect(closeLeft, buttonTop, windowRight - frame - 2,
                         buttonBottom, 0xFFB84D58u, width, height);
                fillRect(closeLeft - 29, buttonTop, closeLeft - 1,
                         buttonBottom, 0xFF385C82u, width, height);
                fillRect(closeLeft - 58, buttonTop, closeLeft - 30,
                         buttonBottom, 0xFF385C82u, width, height);

                const int64_t glyphY = windowTop + frame + (caption - 8) / 2;
                fillRect(closeLeft - 21, glyphY + 4, closeLeft - 9,
                         glyphY + 6, 0xFFF5F7FAu, width, height);
                fillRect(closeLeft - 50, glyphY - 5, closeLeft - 40,
                         glyphY + 5, 0xFFF5F7FAu, width, height);
                fillRect(closeLeft - 48, glyphY - 3, closeLeft - 42,
                         glyphY + 3, 0xFF385C82u, width, height);
                for (int64_t diagonal = 0; diagonal < 9; ++diagonal) {
                    fillRect(closeLeft + 10 + diagonal, glyphY - 4 + diagonal,
                             closeLeft + 12 + diagonal, glyphY - 2 + diagonal,
                             0xFFFFFFFFu, width, height);
                    fillRect(closeLeft + 18 - diagonal, glyphY - 4 + diagonal,
                             closeLeft + 20 - diagonal, glyphY - 2 + diagonal,
                             0xFFFFFFFFu, width, height);
                }

                int64_t titleX = windowLeft + frame + 9;
                const int64_t titleLimit = closeLeft - 66;
                for (size_t character = 0; character < 63 && surface.title[character] &&
                     titleX + 8 <= titleLimit; ++character, titleX += 8)
                    drawGlyph(titleX, glyphY, surface.title[character],
                              0xFFF4F6FAu, width, height);
            }
        }

        const int64_t clipLeft = left < 0 ? 0 : left;
        const int64_t clipTop = top < 0 ? 0 : top;
        const int64_t right = left + clientWidth < width ? left + clientWidth : width;
        const int64_t bottom = top + clientHeight < height ? top + clientHeight : height;
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

    micant::um::UserPresentSurfaceParams64 present{};
    present.surfaceBufferVa = reinterpret_cast<uint64_t>(g_frame);
    present.width = width;
    present.height = height;
    present.pitch = width;
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
    for (;;) {
        const uint64_t sequence = __atomic_load_n(&metadata->frameSequence,
                                                   __ATOMIC_ACQUIRE);
        if (metadata->magic != MICANT_COMPOSITOR_STORE_MAGIC ||
            metadata->version != MICANT_COMPOSITOR_STORE_VERSION) {
            if (!invalidHeaderReported) {
                constexpr char invalid[] = "[MicaNT Compositor] Shared metadata header invalid\r\n";
                print(invalid, sizeof(invalid) - 1);
                invalidHeaderReported = true;
            }
            __asm__ __volatile__("pause");
            continue;
        }
        if (!sequence || (sequence & 1u) ||
            sequence == previous) {
            __asm__ __volatile__("pause");
            continue;
        }
        if (!metadataObserved) {
            constexpr char observed[] = "[MicaNT Compositor] Shared frame metadata observed\r\n";
            print(observed, sizeof(observed) - 1);
            metadataObserved = true;
        }
        if (compose(*metadata, pixels) &&
            __atomic_load_n(&metadata->frameSequence, __ATOMIC_ACQUIRE) == sequence) {
            previous = sequence;
            if (!firstPresentationReported) {
                constexpr char presented[] = "[MicaNT Compositor] First shared frame presented\r\n";
                print(presented, sizeof(presented) - 1);
                firstPresentationReported = true;
            }
        } else __asm__ __volatile__("pause");
    }
}
