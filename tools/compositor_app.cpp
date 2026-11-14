#include "micant/shared/compositor_surface.h"
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
        const int64_t left = surface.x < 0 ? 0 : surface.x;
        const int64_t top = surface.y < 0 ? 0 : surface.y;
        const int64_t right = static_cast<int64_t>(surface.x) + surface.width < width
            ? static_cast<int64_t>(surface.x) + surface.width : width;
        const int64_t bottom = static_cast<int64_t>(surface.y) + surface.height < height
            ? static_cast<int64_t>(surface.y) + surface.height : height;
        if (right <= left || bottom <= top) continue;
        for (int64_t y = top; y < bottom; ++y) {
            const size_t sourceY = static_cast<size_t>(y - surface.y);
            const size_t sourceRow = surface.pixelOffset + sourceY * surface.pitch;
            const size_t destinationRow = static_cast<size_t>(y) * width;
            for (int64_t x = left; x < right; ++x) {
                const size_t sourceX = static_cast<size_t>(x - surface.x);
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
