#include "wallpaper.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace micant::um::imaging {
namespace {

bool checkedSurfaceSize(uint32_t width, uint32_t height, uint64_t limit,
                       uint32_t& stride, size_t& bytes) noexcept {
    if (width == 0 || height == 0) return false;
    const uint64_t row = static_cast<uint64_t>(width) * 4;
    if (height > std::numeric_limits<uint64_t>::max() / row) return false;
    const uint64_t total = row * height;
    if (row > std::numeric_limits<uint32_t>::max() || total > limit ||
        total > std::numeric_limits<size_t>::max()) return false;
    stride = static_cast<uint32_t>(row);
    bytes = static_cast<size_t>(total);
    return true;
}

bool validSource(const ImageFrame& source) noexcept {
    if (source.width == 0 || source.height == 0 ||
        source.format != PixelFormat::Bgra8Premultiplied) return false;
    const uint64_t minimumStride = static_cast<uint64_t>(source.width) * 4;
    if (source.stride < minimumStride) return false;
    const uint64_t required = static_cast<uint64_t>(source.stride) * source.height;
    return required <= source.pixels.size();
}

uint8_t blend(uint8_t foreground, uint8_t background, uint8_t alpha) noexcept {
    const uint32_t value = static_cast<uint32_t>(foreground) +
        (static_cast<uint32_t>(background) * (255u - alpha) + 127u) / 255u;
    return static_cast<uint8_t>(std::min(value, 255u));
}

void setBackground(uint8_t* pixel, uint32_t bgra) noexcept {
    pixel[0] = static_cast<uint8_t>(bgra);
    pixel[1] = static_cast<uint8_t>(bgra >> 8);
    pixel[2] = static_cast<uint8_t>(bgra >> 16);
    pixel[3] = 255;
}

void copyOpaquePixel(uint8_t* destination, const uint8_t* source,
                     uint32_t background) noexcept {
    const uint8_t alpha = source[3];
    destination[0] = blend(source[0], static_cast<uint8_t>(background), alpha);
    destination[1] = blend(source[1], static_cast<uint8_t>(background >> 8), alpha);
    destination[2] = blend(source[2], static_cast<uint8_t>(background >> 16), alpha);
    destination[3] = 255;
}

} // namespace

ImageStatus ComposeWallpaper(const ImageFrame* source,
                             uint32_t displayWidth,
                             uint32_t displayHeight,
                             WallpaperStyle style,
                             uint32_t backgroundBgra,
                             const ImageAllocator& allocator,
                             ImageFrame& destination,
                             uint64_t maxOutputBytes) noexcept {
    uint32_t outputStride{};
    size_t outputBytes{};
    if (!checkedSurfaceSize(displayWidth, displayHeight, maxOutputBytes,
                            outputStride, outputBytes))
        return ImageStatus::ImageTooLarge;
    if (style != WallpaperStyle::Fill && style != WallpaperStyle::Fit &&
        style != WallpaperStyle::Stretch && style != WallpaperStyle::Center &&
        style != WallpaperStyle::Tile)
        return ImageStatus::UnsupportedFeature;
    if (source && !validSource(*source)) return ImageStatus::MalformedData;

    ImageFrame completed{};
    if (!completed.pixels.allocate(outputBytes, allocator)) return ImageStatus::OutOfMemory;
    completed.width = displayWidth;
    completed.height = displayHeight;
    completed.stride = outputStride;
    completed.format = PixelFormat::Bgra8Premultiplied;
    auto* pixels = completed.pixels.data();
    for (size_t offset = 0; offset < outputBytes; offset += 4)
        setBackground(pixels + offset, backgroundBgra);

    if (source) {
        const uint32_t sw = source->width, sh = source->height;
        const uint32_t dw = displayWidth, dh = displayHeight;
        const double sx = static_cast<double>(dw) / sw;
        const double sy = static_cast<double>(dh) / sh;
        double scaleX = sx, scaleY = sy;
        double cropX = 0.0, cropY = 0.0;
        int64_t centeredX = (static_cast<int64_t>(dw) - sw) / 2;
        int64_t centeredY = (static_cast<int64_t>(dh) - sh) / 2;

        if (style == WallpaperStyle::Fill) {
            const double scale = std::max(sx, sy);
            const double visibleWidth = dw / scale;
            const double visibleHeight = dh / scale;
            cropX = (sw - visibleWidth) * 0.5;
            cropY = (sh - visibleHeight) * 0.5;
            scaleX = scaleY = scale;
        }

        for (uint32_t y = 0; y < dh; ++y) {
            for (uint32_t x = 0; x < dw; ++x) {
                uint32_t sourceX{}, sourceY{};
                bool draw = true;
                if (style == WallpaperStyle::Tile) {
                    sourceX = x % sw;
                    sourceY = y % sh;
                } else if (style == WallpaperStyle::Center) {
                    const int64_t imageX = static_cast<int64_t>(x) - centeredX;
                    const int64_t imageY = static_cast<int64_t>(y) - centeredY;
                    if (imageX < 0 || imageY < 0 || imageX >= sw || imageY >= sh) {
                        draw = false;
                    } else {
                        sourceX = static_cast<uint32_t>(imageX);
                        sourceY = static_cast<uint32_t>(imageY);
                    }
                } else if (style == WallpaperStyle::Fit) {
                    const double scale = std::min(sx, sy);
                    const uint32_t renderedWidth = static_cast<uint32_t>(sw * scale);
                    const uint32_t renderedHeight = static_cast<uint32_t>(sh * scale);
                    const uint32_t originX = (dw - std::min(dw, renderedWidth)) / 2;
                    const uint32_t originY = (dh - std::min(dh, renderedHeight)) / 2;
                    if (x < originX || y < originY ||
                        x - originX >= renderedWidth || y - originY >= renderedHeight) {
                        draw = false;
                    } else {
                        sourceX = std::min(sw - 1, static_cast<uint32_t>((x - originX) / scale));
                        sourceY = std::min(sh - 1, static_cast<uint32_t>((y - originY) / scale));
                    }
                } else {
                    const double mapX = (style == WallpaperStyle::Fill)
                        ? ((static_cast<double>(x) + 0.5) / scaleX + cropX)
                        : (static_cast<double>(x) + 0.5) / scaleX;
                    const double mapY = (style == WallpaperStyle::Fill)
                        ? ((static_cast<double>(y) + 0.5) / scaleY + cropY)
                        : (static_cast<double>(y) + 0.5) / scaleY;
                    sourceX = std::min(sw - 1, static_cast<uint32_t>(mapX));
                    sourceY = std::min(sh - 1, static_cast<uint32_t>(mapY));
                }
                if (!draw) continue;
                const size_t sourceOffset = static_cast<size_t>(sourceY) * source->stride +
                                            static_cast<size_t>(sourceX) * 4;
                const size_t destinationOffset = static_cast<size_t>(y) * outputStride +
                                                 static_cast<size_t>(x) * 4;
                copyOpaquePixel(pixels + destinationOffset,
                                source->pixels.data() + sourceOffset,
                                backgroundBgra);
            }
        }
    }

    destination = std::move(completed);
    return ImageStatus::Success;
}

} // namespace micant::um::imaging
