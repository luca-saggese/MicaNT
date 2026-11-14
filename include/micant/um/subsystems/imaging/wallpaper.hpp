#pragma once

#include "image_decoder.hpp"

#include <cstddef>
#include <cstdint>

namespace micant::um::imaging {

enum class WallpaperStyle : uint32_t {
    Fill,
    Fit,
    Stretch,
    Center,
    Tile,
};

// Composes one immutable premultiplied BGRA8 image into an opaque display-sized
// BGRA8 surface. The destination is replaced only after the full render succeeds.
// A null source selects the supplied solid background color.
[[nodiscard]] ImageStatus ComposeWallpaper(
    const ImageFrame* source,
    uint32_t displayWidth,
    uint32_t displayHeight,
    WallpaperStyle style,
    uint32_t backgroundBgra,
    const ImageAllocator& allocator,
    ImageFrame& destination,
    uint64_t maxOutputBytes = 256ull * 1024 * 1024) noexcept;

} // namespace micant::um::imaging
