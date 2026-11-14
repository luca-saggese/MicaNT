#pragma once

// Shared content-based codec dispatch for Ring-3 imaging clients.
#include "bmp_png_decoder.hpp"
#include "jpeg_decoder.hpp"
#include "image_encoder.hpp"
#include "raster_encoder.hpp"
#include "jpeg_encoder.hpp"

namespace micant::um::imaging {

inline ImageStatus ProbeImage(std::span<const uint8_t> input,
                              ImageFormat& format) noexcept {
    format = ImageFormat::Unknown;
    if (input.size() >= 2 && input[0] == 'B' && input[1] == 'M') {
        format = ImageFormat::Bmp;
        return ImageStatus::Success;
    }
    if (input.size() >= 2 && input[0] == 0xff && input[1] == 0xd8) {
        format = ImageFormat::Jpeg;
        return ImageStatus::Success;
    }
    static constexpr uint8_t pngSignature[8]{137, 80, 78, 71, 13, 10, 26, 10};
    if (input.size() >= sizeof(pngSignature) &&
        std::equal(std::begin(pngSignature), std::end(pngSignature), input.begin())) {
        format = ImageFormat::Png;
        return ImageStatus::Success;
    }
    if (input.size() >= 4 &&
        ((input[0] == 'I' && input[1] == 'I' && input[2] == 42 && input[3] == 0) ||
         (input[0] == 'M' && input[1] == 'M' && input[2] == 0 && input[3] == 42))) {
        format = ImageFormat::Tiff;
        return ImageStatus::Success;
    }
    return input.empty() ? ImageStatus::TruncatedInput : ImageStatus::UnknownFormat;
}

inline ImageStatus DecodeImage(std::span<const uint8_t> input,
                               ImageFrame& output,
                               const ImageAllocator& allocator,
                               ImageLimits limits) noexcept {
    if (input.size() > limits.maxEncodedBytes) return ImageStatus::ImageTooLarge;
    ImageFormat format{};
    const ImageStatus probe = ProbeImage(input, format);
    if (probe != ImageStatus::Success) return probe;
    switch (format) {
        case ImageFormat::Bmp: return DecodeBmp(input, output, allocator, limits);
        case ImageFormat::Jpeg: return DecodeJpeg(input, output, allocator, limits);
        case ImageFormat::Png: return DecodePng(input, output, allocator, limits);
        case ImageFormat::Tiff: return ImageStatus::UnsupportedFeature;
        case ImageFormat::Unknown: return ImageStatus::UnknownFormat;
    }
    return ImageStatus::UnknownFormat;
}

} // namespace micant::um::imaging
