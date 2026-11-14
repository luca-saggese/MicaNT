#pragma once

// Bounded Ring-3 BMP and PNG encoders. These routines encode the shared
// premultiplied BGRA8 frame contract and publish output only on success.
#include "image_encoder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace micant::um::imaging {
namespace raster_encode_detail {

inline bool Mul(uint64_t a, uint64_t b, uint64_t& out) noexcept {
    if (a != 0 && b > UINT64_MAX / a) return false;
    out = a * b;
    return true;
}

inline bool ValidateSource(const ImageFrame& image, const ImageLimits& limits) noexcept {
    if (image.width == 0 || image.height == 0 ||
        image.format != PixelFormat::Bgra8Premultiplied || !image.pixels.data()) return false;
    const uint64_t minStride = static_cast<uint64_t>(image.width) * 4;
    uint64_t required{}, pixelCount{};
    return image.stride >= minStride && Mul(image.stride, image.height, required) &&
           required <= image.pixels.size() && required <= limits.maxDecodedBytes &&
           Mul(image.width, image.height, pixelCount) && pixelCount <= limits.maxPixels;
}

inline uint32_t CrcUpdate(uint32_t crc, uint8_t byte) noexcept {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320u & static_cast<uint32_t>(-static_cast<int32_t>(crc & 1)));
    return crc;
}
inline uint32_t Crc(std::span<const uint8_t> bytes) noexcept {
    uint32_t crc = 0xffffffffu;
    for (uint8_t value : bytes) crc = CrcUpdate(crc, value);
    return ~crc;
}
inline void PutLe16(uint8_t* out, uint16_t v) noexcept {
    out[0] = static_cast<uint8_t>(v); out[1] = static_cast<uint8_t>(v >> 8);
}
inline void PutLe32(uint8_t* out, uint32_t v) noexcept {
    out[0] = static_cast<uint8_t>(v); out[1] = static_cast<uint8_t>(v >> 8);
    out[2] = static_cast<uint8_t>(v >> 16); out[3] = static_cast<uint8_t>(v >> 24);
}
inline void PutBe32(uint8_t* out, uint32_t v) noexcept {
    out[0] = static_cast<uint8_t>(v >> 24); out[1] = static_cast<uint8_t>(v >> 16);
    out[2] = static_cast<uint8_t>(v >> 8); out[3] = static_cast<uint8_t>(v);
}
inline void PutChunkCrc(uint8_t* chunk, uint32_t length) noexcept {
    PutBe32(chunk + 8 + length, Crc({chunk + 4, static_cast<size_t>(length) + 4}));
}
inline uint8_t Unpremultiply(uint8_t channel, uint8_t alpha) noexcept {
    if (alpha == 0) return 0;
    return static_cast<uint8_t>(std::min(255u,
        (static_cast<uint32_t>(channel) * 255u + alpha / 2u) / alpha));
}

} // namespace raster_encode_detail

inline ImageStatus EncodeBmp(const ImageFrame& image, EncodedImage& output,
                             const ImageAllocator& allocator,
                             ImageLimits limits) noexcept {
    using namespace raster_encode_detail;
    if (!ValidateSource(image, limits)) return ImageStatus::MalformedData;
    if (image.width > INT32_MAX || image.height > INT32_MAX) return ImageStatus::ImageTooLarge;
    uint64_t rowBits{}, paddedRow{}, pixelBytes{}, fileBytes{};
    if (!Mul(image.width, 24, rowBits) || rowBits > UINT64_MAX - 31) return ImageStatus::ImageTooLarge;
    paddedRow = ((rowBits + 31) / 32) * 4;
    if (!Mul(paddedRow, image.height, pixelBytes) || pixelBytes > UINT32_MAX - 54ull ||
        !Mul(1, pixelBytes + 54, fileBytes) || fileBytes > limits.maxEncodedBytes ||
        fileBytes > limits.maxIntermediateBytes || fileBytes > SIZE_MAX)
        return ImageStatus::ImageTooLarge;

    EncodedImage encoded{};
    if (!encoded.bytes.allocate(static_cast<size_t>(fileBytes), allocator)) return ImageStatus::OutOfMemory;
    uint8_t* bytes = encoded.bytes.data();
    std::memset(bytes, 0, 54);
    bytes[0] = 'B'; bytes[1] = 'M';
    PutLe32(bytes + 2, static_cast<uint32_t>(fileBytes));
    PutLe32(bytes + 10, 54);
    PutLe32(bytes + 14, 40);
    PutLe32(bytes + 18, image.width);
    PutLe32(bytes + 22, image.height); // positive height: bottom-up rows
    PutLe16(bytes + 26, 1);
    PutLe16(bytes + 28, 24);
    PutLe32(bytes + 30, 0); // BI_RGB
    PutLe32(bytes + 34, static_cast<uint32_t>(pixelBytes));
    const size_t rowBytes = static_cast<size_t>(paddedRow);
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint32_t srcY = image.height - 1 - y;
        const uint8_t* src = image.pixels.data() + static_cast<size_t>(srcY) * image.stride;
        uint8_t* dst = bytes + 54 + static_cast<size_t>(y) * rowBytes;
        std::memset(dst, 0, rowBytes);
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint8_t* pixel = src + static_cast<size_t>(x) * 4;
            // BMP output is 24-bit and has no portable alpha channel. The
            // premultiplied RGB values therefore encode the image over black.
            dst[x * 3 + 0] = pixel[0];
            dst[x * 3 + 1] = pixel[1];
            dst[x * 3 + 2] = pixel[2];
        }
    }
    encoded.byteLength = static_cast<size_t>(fileBytes);
    encoded.format = ImageFormat::Bmp;
    output = std::move(encoded);
    return ImageStatus::Success;
}

inline ImageStatus EncodePng(const ImageFrame& image, EncodedImage& output,
                             const ImageAllocator& allocator,
                             ImageLimits limits) noexcept {
    using namespace raster_encode_detail;
    if (!ValidateSource(image, limits)) return ImageStatus::MalformedData;
    uint64_t pixelRow{}, rawBytes{}, blockCount{}, deflateBytes{}, zlibBytes{}, totalBytes{};
    if (!Mul(image.width, 4, pixelRow) || !Mul(pixelRow + 1, image.height, rawBytes) ||
        rawBytes == 0 || rawBytes > SIZE_MAX) return ImageStatus::ImageTooLarge;
    blockCount = (rawBytes + 65534) / 65535;
    if (!Mul(blockCount, 5, deflateBytes) || deflateBytes > UINT64_MAX - rawBytes)
        return ImageStatus::ImageTooLarge;
    deflateBytes += rawBytes;
    if (deflateBytes > UINT64_MAX - 6) return ImageStatus::ImageTooLarge;
    zlibBytes = deflateBytes + 6; // zlib header and Adler-32
    if (zlibBytes > UINT32_MAX || zlibBytes > UINT64_MAX - 57) return ImageStatus::ImageTooLarge;
    totalBytes = zlibBytes + 57; // PNG signature, IHDR, IDAT, and IEND
    if (totalBytes > limits.maxEncodedBytes || totalBytes > limits.maxIntermediateBytes ||
        totalBytes > SIZE_MAX) return ImageStatus::ImageTooLarge;

    EncodedImage encoded{};
    if (!encoded.bytes.allocate(static_cast<size_t>(totalBytes), allocator)) return ImageStatus::OutOfMemory;
    uint8_t* out = encoded.bytes.data();
    static constexpr uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::memcpy(out, signature, sizeof(signature));

    // IHDR chunk (8-byte length/type, 13-byte payload, 4-byte CRC).
    PutBe32(out + 8, 13); std::memcpy(out + 12, "IHDR", 4);
    PutBe32(out + 16, image.width); PutBe32(out + 20, image.height);
    out[24] = 8; out[25] = 6; // 8-bit RGBA
    out[26] = out[27] = out[28] = 0;
    PutChunkCrc(out + 8, 13);

    const size_t idat = 33;
    PutBe32(out + idat, static_cast<uint32_t>(zlibBytes));
    std::memcpy(out + idat + 4, "IDAT", 4);
    size_t cursor = idat + 8;
    out[cursor++] = 0x78; out[cursor++] = 0x01; // zlib, no compression
    uint64_t rawOffset = 0;
    uint32_t adlerA = 1, adlerB = 0;
    while (rawOffset < rawBytes) {
        const uint16_t length = static_cast<uint16_t>(std::min<uint64_t>(65535, rawBytes - rawOffset));
        const bool final = rawOffset + length == rawBytes;
        out[cursor++] = final ? 1 : 0; // BFINAL, BTYPE=00, byte aligned
        PutLe16(out + cursor, length); cursor += 2;
        PutLe16(out + cursor, static_cast<uint16_t>(~length)); cursor += 2;
        for (uint32_t i = 0; i < length; ++i) {
            const uint64_t rawIndex = rawOffset + i;
            const uint64_t rowLength = pixelRow + 1;
            const uint32_t y = static_cast<uint32_t>(rawIndex / rowLength);
            const uint32_t column = static_cast<uint32_t>(rawIndex % rowLength);
            uint8_t value{};
            if (column == 0) {
                value = 0; // PNG filter None
            } else {
                const uint32_t pixelIndex = (column - 1) / 4;
                const uint32_t channel = (column - 1) % 4;
                const uint8_t* src = image.pixels.data() + static_cast<size_t>(y) * image.stride +
                                     static_cast<size_t>(pixelIndex) * 4;
                const uint8_t alpha = image.sourceHadAlpha ? src[3] : 255;
                if (channel == 0) value = Unpremultiply(src[2], alpha);      // R
                else if (channel == 1) value = Unpremultiply(src[1], alpha); // G
                else if (channel == 2) value = Unpremultiply(src[0], alpha); // B
                else value = alpha;
            }
            out[cursor++] = value;
            adlerA += value; if (adlerA >= 65521) adlerA -= 65521;
            adlerB += adlerA; if (adlerB >= 65521) adlerB -= 65521;
        }
        rawOffset += length;
    }
    PutBe32(out + cursor, (adlerB << 16) | adlerA); cursor += 4;
    if (cursor != idat + 8 + zlibBytes) return ImageStatus::MalformedData;
    PutChunkCrc(out + idat, static_cast<uint32_t>(zlibBytes));

    const size_t iend = idat + 12 + static_cast<size_t>(zlibBytes);
    PutBe32(out + iend, 0); std::memcpy(out + iend + 4, "IEND", 4);
    PutBe32(out + iend + 8, Crc({out + iend + 4, 4}));
    encoded.byteLength = static_cast<size_t>(totalBytes);
    encoded.format = ImageFormat::Png;
    output = std::move(encoded);
    return ImageStatus::Success;
}

} // namespace micant::um::imaging
