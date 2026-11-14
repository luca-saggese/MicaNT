#pragma once

// Bounded, allocator-owned output contract shared by native Ring-3 codecs.
#include "image_decoder.hpp"

#include <limits>

namespace micant::um::imaging {

struct EncodedImage {
    ImageBuffer bytes;
    size_t byteLength{};
    ImageFormat format{ImageFormat::Unknown};
};

struct ImageEncodeOptions {
    // JPEG quality in [1, 100]. Ignored by lossless encoders.
    uint8_t quality{90};
};

ImageStatus EncodeBmp(const ImageFrame&, EncodedImage&, const ImageAllocator&,
                      ImageLimits) noexcept;
ImageStatus EncodePng(const ImageFrame&, EncodedImage&, const ImageAllocator&,
                      ImageLimits) noexcept;
ImageStatus EncodeJpeg(const ImageFrame&, EncodedImage&, const ImageAllocator&,
                       ImageLimits, uint8_t quality) noexcept;

inline ImageStatus EncodeImage(const ImageFrame& input, ImageFormat format,
                               EncodedImage& output,
                               const ImageAllocator& allocator,
                               ImageEncodeOptions options = {},
                               ImageLimits limits = {}) noexcept {
    const uint64_t minimumStride = static_cast<uint64_t>(input.width) * 4u;
    if (input.format != PixelFormat::Bgra8Premultiplied || input.width == 0 ||
        input.height == 0 || !input.pixels.data() || minimumStride > std::numeric_limits<uint32_t>::max() ||
        input.stride < minimumStride)
        return ImageStatus::MalformedData;
    const uint64_t pixels = static_cast<uint64_t>(input.width) * input.height;
    if (pixels > limits.maxPixels ||
        static_cast<uint64_t>(input.stride) * input.height > input.pixels.size() ||
        static_cast<uint64_t>(input.stride) * input.height > limits.maxDecodedBytes)
        return ImageStatus::ImageTooLarge;
    const uint64_t sourceBytes = static_cast<uint64_t>(input.stride) * input.height;
    if (sourceBytes > limits.maxIntermediateBytes) return ImageStatus::ImageTooLarge;
    limits.maxIntermediateBytes -= sourceBytes;
    switch (format) {
        case ImageFormat::Bmp: return EncodeBmp(input, output, allocator, limits);
        case ImageFormat::Png: return EncodePng(input, output, allocator, limits);
        case ImageFormat::Jpeg: return EncodeJpeg(input, output, allocator, limits, options.quality);
        default: return ImageStatus::UnsupportedFeature;
    }
}

} // namespace micant::um::imaging

// Keep the core API self-contained for user-mode clients. These codec headers
// include this contract and are guarded against recursive inclusion.
#include "raster_encoder.hpp"
#include "jpeg_encoder.hpp"
