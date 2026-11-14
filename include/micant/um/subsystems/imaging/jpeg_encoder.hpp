#pragma once

// Baseline sequential 8-bit JPEG encoder. The implementation is bounded,
// allocator-backed and safe to compile without exception support.
#include "image_encoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace micant::um::imaging {
namespace jpeg_encode_detail {

inline constexpr std::array<uint8_t, 64> kZigzag{
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
    12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};

inline constexpr std::array<double, 64> kBasis{
    0.7071067811865475,0.9807852804032304,0.9238795325112867,0.8314696123025452,0.7071067811865476,0.5555702330196023,0.3826834323650898,0.1950903220161283,
    0.7071067811865475,0.8314696123025452,0.3826834323650898,-0.1950903220161282,-0.7071067811865475,-0.9807852804032304,-0.9238795325112868,-0.5555702330196022,
    0.7071067811865475,0.5555702330196023,-0.3826834323650897,-0.9807852804032304,-0.7071067811865477,0.1950903220161283,0.9238795325112865,0.8314696123025455,
    0.7071067811865475,0.1950903220161283,-0.9238795325112867,-0.5555702330196022,0.7071067811865474,0.8314696123025455,-0.3826834323650903,-0.9807852804032307,
    0.7071067811865475,-0.1950903220161282,-0.9238795325112868,0.5555702330196018,0.7071067811865474,-0.8314696123025451,0.3826834323650904,-0.9807852804032307,
    0.7071067811865475,-0.5555702330196022,-0.3826834323650903,0.9807852804032304,-0.7071067811865477,-0.1950903220161287,0.9238795325112865,-0.8314696123025455,
    0.7071067811865475,-0.8314696123025455,0.3826834323650896,0.1950903220161286,-0.7071067811865474,0.9807852804032307,-0.9238795325112865,0.5555702330196022,
    0.7071067811865475,-0.9807852804032304,0.9238795325112865,-0.8314696123025455,0.7071067811865474,-0.5555702330196022,0.3826834323650904,-0.1950903220161287};

inline constexpr std::array<uint8_t, 64> kLumaQuant{
    16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55,
    14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
    49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99};
inline constexpr std::array<uint8_t, 64> kChromaQuant{
    17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99,
    24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99};

inline int32_t roundToInt32(double value) noexcept {
    return static_cast<int32_t>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

struct HuffmanCode { uint16_t code{}; uint8_t length{}; };

class Writer {
public:
    Writer(uint8_t* bytes, size_t capacity) noexcept : bytes_(bytes), capacity_(capacity) {}

    bool byte(uint8_t value) noexcept {
        if (position_ >= capacity_) return false;
        bytes_[position_++] = value;
        return true;
    }
    bool word(uint16_t value) noexcept { return byte(static_cast<uint8_t>(value >> 8)) && byte(static_cast<uint8_t>(value)); }
    bool marker(uint8_t value) noexcept { return byte(0xff) && byte(value); }
    bool bytes(const uint8_t* data, size_t length) noexcept {
        for (size_t i = 0; i < length; ++i) if (!byte(data[i])) return false;
        return true;
    }
    bool bits(uint32_t value, uint8_t count) noexcept {
        for (uint8_t i = count; i > 0; --i) {
            accumulator_ = static_cast<uint8_t>((accumulator_ << 1) | ((value >> (i - 1)) & 1u));
            if (++bitCount_ == 8) {
                const uint8_t out = accumulator_;
                accumulator_ = 0; bitCount_ = 0;
                if (!byte(out) || (out == 0xff && !byte(0))) return false;
            }
        }
        return true;
    }
    bool flushBits() noexcept {
        if (bitCount_ == 0) return true;
        const uint8_t pad = static_cast<uint8_t>((1u << (8 - bitCount_)) - 1u);
        const uint8_t out = static_cast<uint8_t>((accumulator_ << (8 - bitCount_)) | pad);
        accumulator_ = 0; bitCount_ = 0;
        return byte(out) && (out != 0xff || byte(0));
    }
    size_t size() const noexcept { return position_; }

private:
    uint8_t* bytes_{};
    size_t capacity_{};
    size_t position_{};
    uint8_t accumulator_{};
    uint8_t bitCount_{};
};

inline void makeTables(uint8_t quality, std::array<uint8_t, 64>& luma,
                       std::array<uint8_t, 64>& chroma) noexcept {
    const uint32_t q = std::clamp<uint32_t>(quality, 1, 100);
    const uint32_t scale = q < 50 ? 5000 / q : 200 - 2 * q;
    for (size_t i = 0; i < 64; ++i) {
        luma[i] = static_cast<uint8_t>(std::clamp<uint32_t>((kLumaQuant[i] * scale + 50) / 100, 1, 255));
        chroma[i] = static_cast<uint8_t>(std::clamp<uint32_t>((kChromaQuant[i] * scale + 50) / 100, 1, 255));
    }
}

inline void buildDcCodes(std::array<HuffmanCode, 256>& codes) noexcept {
    codes = {};
    // Twelve length-4 symbols form a complete legal baseline DC table.
    for (uint16_t symbol = 0; symbol < 12; ++symbol)
        codes[symbol] = {symbol, 4};
}

inline void buildAcCodes(std::array<HuffmanCode, 256>& codes) noexcept {
    codes = {};
    uint16_t code = 0;
    codes[0x00] = {code++, 8};
    for (uint16_t run = 0; run < 16; ++run)
        for (uint16_t size = 1; size <= 10; ++size)
            codes[(run << 4) | size] = {code++, 8};
    codes[0xf0] = {code, 8};
}

inline bool emitCode(Writer& writer, const std::array<HuffmanCode, 256>& codes,
                     uint8_t symbol) noexcept {
    const HuffmanCode code = codes[symbol];
    return code.length != 0 && writer.bits(code.code, code.length);
}

inline bool emitValue(Writer& writer, int32_t value, uint8_t size) noexcept {
    if (size == 0) return true;
    const uint32_t encoded = value < 0
        ? static_cast<uint32_t>(value + static_cast<int32_t>((1u << size) - 1u))
        : static_cast<uint32_t>(value);
    return writer.bits(encoded, size);
}

inline uint8_t category(int32_t value) noexcept {
    uint32_t magnitude = value < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(value)) : static_cast<uint32_t>(value);
    uint8_t bits = 0;
    while (magnitude) { ++bits; magnitude >>= 1; }
    return bits;
}

inline bool encodeBlock(Writer& writer, const std::array<double, 64>& samples,
                        const std::array<uint8_t, 64>& quant,
                        const std::array<HuffmanCode, 256>& dcCodes,
                        const std::array<HuffmanCode, 256>& acCodes,
                        int32_t& previousDc) noexcept {
    std::array<double, 64> horizontal{};
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t u = 0; u < 8; ++u) {
            double sum = 0.0;
            for (uint32_t x = 0; x < 8; ++x) sum += samples[y * 8 + x] * kBasis[x * 8 + u];
            horizontal[y * 8 + u] = sum;
        }
    std::array<int32_t, 64> coefficients{};
    for (uint32_t v = 0; v < 8; ++v)
        for (uint32_t u = 0; u < 8; ++u) {
            double sum = 0.0;
            for (uint32_t y = 0; y < 8; ++y) sum += horizontal[y * 8 + u] * kBasis[y * 8 + v];
            const int32_t quantized = roundToInt32((sum * 0.25) / quant[v * 8 + u]);
            // Keep baseline AC categories at <=10; DC differences between
            // adjacent blocks consequently remain in the legal <=11 range.
            coefficients[v * 8 + u] = std::clamp(quantized, -1023, 1023);
        }
    const int32_t dc = coefficients[0];
    const int32_t difference = dc - previousDc;
    previousDc = dc;
    const uint8_t dcSize = category(difference);
    if (dcSize > 11 || !emitCode(writer, dcCodes, dcSize) || !emitValue(writer, difference, dcSize)) return false;

    uint32_t zeroRun = 0;
    for (uint32_t index = 1; index < 64; ++index) {
        const int32_t coefficient = coefficients[kZigzag[index]];
        if (coefficient == 0) { ++zeroRun; continue; }
        while (zeroRun >= 16) {
            if (!emitCode(writer, acCodes, 0xf0)) return false;
            zeroRun -= 16;
        }
        const uint8_t acSize = category(coefficient);
        if (acSize > 10 || !emitCode(writer, acCodes, static_cast<uint8_t>((zeroRun << 4) | acSize)) ||
            !emitValue(writer, coefficient, acSize)) return false;
        zeroRun = 0;
    }
    return zeroRun == 0 || emitCode(writer, acCodes, 0x00);
}

inline uint8_t channel(const ImageFrame& frame, uint32_t x, uint32_t y, size_t index) noexcept {
    const size_t offset = static_cast<size_t>(y) * frame.stride + static_cast<size_t>(x) * 4;
    // JPEG has no alpha channel. The shared pixels are premultiplied, so
    // retaining their color channels composites translucent pixels on black.
    return frame.pixels[offset + index];
}

inline double componentSample(const ImageFrame& frame, uint32_t x, uint32_t y, uint32_t channelIndex) noexcept {
    const double red = channel(frame, x, y, 2);
    const double green = channel(frame, x, y, 1);
    const double blue = channel(frame, x, y, 0);
    if (channelIndex == 0) return 0.299 * red + 0.587 * green + 0.114 * blue - 128.0;
    if (channelIndex == 1) return -0.168736 * red - 0.331264 * green + 0.5 * blue;
    return 0.5 * red - 0.418688 * green - 0.081312 * blue;
}

inline bool writeHeaders(Writer& writer, uint32_t width, uint32_t height,
                         const std::array<uint8_t, 64>& luma,
                         const std::array<uint8_t, 64>& chroma) noexcept {
    if (!writer.marker(0xd8)) return false;
    // JFIF 1.01, no thumbnail.
    constexpr uint8_t jfif[]{0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00};
    if (!writer.marker(0xe0) || !writer.word(sizeof(jfif) + 2) || !writer.bytes(jfif, sizeof(jfif))) return false;
    if (!writer.marker(0xdb) || !writer.word(132) || !writer.byte(0x00)) return false;
    for (uint8_t index : kZigzag) if (!writer.byte(luma[index])) return false;
    if (!writer.byte(0x01)) return false;
    for (uint8_t index : kZigzag) if (!writer.byte(chroma[index])) return false;
    if (!writer.marker(0xc0) || !writer.word(17) || !writer.byte(8) ||
        !writer.word(static_cast<uint16_t>(height)) || !writer.word(static_cast<uint16_t>(width)) || !writer.byte(3)) return false;
    if (!writer.byte(1) || !writer.byte(0x11) || !writer.byte(0) ||
        !writer.byte(2) || !writer.byte(0x11) || !writer.byte(1) ||
        !writer.byte(3) || !writer.byte(0x11) || !writer.byte(1)) return false;

    // Compact fixed-length Huffman tables. All symbols used by baseline
    // DC/AC coding are present; unused AC size-zero symbols are omitted.
    if (!writer.marker(0xc4) || !writer.word(210) || !writer.byte(0x00)) return false;
    for (uint32_t i = 1; i <= 16; ++i) if (!writer.byte(i == 4 ? 12 : 0)) return false;
    for (uint8_t value = 0; value < 12; ++value) if (!writer.byte(value)) return false;
    if (!writer.byte(0x10)) return false;
    for (uint32_t i = 1; i <= 16; ++i) if (!writer.byte(i == 8 ? 162 : 0)) return false;
    if (!writer.byte(0x00)) return false;
    for (uint32_t run = 0; run < 16; ++run)
        for (uint32_t size = 1; size <= 10; ++size)
            if (!writer.byte(static_cast<uint8_t>((run << 4) | size))) return false;
    if (!writer.byte(0xf0)) return false;

    if (!writer.marker(0xda) || !writer.word(12) || !writer.byte(3) ||
        !writer.byte(1) || !writer.byte(0x00) ||
        !writer.byte(2) || !writer.byte(0x00) ||
        !writer.byte(3) || !writer.byte(0x00) ||
        !writer.byte(0) || !writer.byte(63) || !writer.byte(0)) return false;
    return true;
}

inline bool addChecked(uint64_t left, uint64_t right, uint64_t& result) noexcept {
    if (right > std::numeric_limits<uint64_t>::max() - left) return false;
    result = left + right;
    return true;
}
inline bool multiplyChecked(uint64_t left, uint64_t right, uint64_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left) return false;
    result = left * right;
    return true;
}

} // namespace jpeg_encode_detail

inline ImageStatus EncodeJpeg(const ImageFrame& input, EncodedImage& output,
                              const ImageAllocator& allocator, ImageLimits limits,
                              uint8_t quality) noexcept {
    using namespace jpeg_encode_detail;
    if (input.width == 0 || input.height == 0 || !input.pixels.data() ||
        input.format != PixelFormat::Bgra8Premultiplied)
        return ImageStatus::MalformedData;
    const uint64_t minStride = static_cast<uint64_t>(input.width) * 4;
    const uint64_t sourceBytes = static_cast<uint64_t>(input.stride) * input.height;
    const uint64_t pixelCount = static_cast<uint64_t>(input.width) * input.height;
    if (input.width > 65535 || input.height > 65535 || minStride > UINT32_MAX || input.stride < minStride ||
        sourceBytes > input.pixels.size() || sourceBytes > limits.maxDecodedBytes || pixelCount > limits.maxPixels)
        return ImageStatus::ImageTooLarge;
    if (quality == 0 || quality > 100) return ImageStatus::MalformedData;
    if (!allocator.allocate || !allocator.deallocate) return ImageStatus::OutOfMemory;

    const uint64_t columns = (static_cast<uint64_t>(input.width) + 7) / 8;
    const uint64_t rows = (static_cast<uint64_t>(input.height) + 7) / 8;
    uint64_t blockCount{}, entropyCapacity{}, capacity{};
    if (!multiplyChecked(columns, rows, blockCount) || !multiplyChecked(blockCount, 3, blockCount) ||
        !multiplyChecked(blockCount, 300, entropyCapacity) || !addChecked(entropyCapacity, 1024, capacity) ||
        capacity > limits.maxEncodedBytes || capacity > limits.maxIntermediateBytes ||
        capacity > std::numeric_limits<size_t>::max())
        return ImageStatus::ImageTooLarge;

    EncodedImage encoded{};
    if (!encoded.bytes.allocate(static_cast<size_t>(capacity), allocator)) return ImageStatus::OutOfMemory;
    Writer writer(encoded.bytes.data(), encoded.bytes.size());
    std::array<uint8_t, 64> luma{}, chroma{};
    makeTables(quality, luma, chroma);
    if (!writeHeaders(writer, input.width, input.height, luma, chroma)) return ImageStatus::ImageTooLarge;

    std::array<HuffmanCode, 256> dcCodes{}, acCodes{};
    buildDcCodes(dcCodes);
    buildAcCodes(acCodes);
    std::array<int32_t, 3> previousDc{};
    std::array<double, 64> samples{};
    for (uint32_t blockY = 0; blockY < rows; ++blockY) {
        for (uint32_t blockX = 0; blockX < columns; ++blockX) {
            for (uint32_t component = 0; component < 3; ++component) {
                for (uint32_t y = 0; y < 8; ++y) {
                    const uint32_t sourceY = std::min<uint32_t>(blockY * 8 + y, input.height - 1);
                    for (uint32_t x = 0; x < 8; ++x) {
                        const uint32_t sourceX = std::min<uint32_t>(blockX * 8 + x, input.width - 1);
                        samples[y * 8 + x] = componentSample(input, sourceX, sourceY, component);
                    }
                }
                const auto& quant = component == 0 ? luma : chroma;
                if (!encodeBlock(writer, samples, quant, dcCodes, acCodes, previousDc[component]))
                    return ImageStatus::ImageTooLarge;
            }
        }
    }
    if (!writer.flushBits() || !writer.marker(0xd9)) return ImageStatus::ImageTooLarge;
    encoded.byteLength = writer.size();
    encoded.format = ImageFormat::Jpeg;
    output = std::move(encoded);
    return ImageStatus::Success;
}

} // namespace micant::um::imaging
