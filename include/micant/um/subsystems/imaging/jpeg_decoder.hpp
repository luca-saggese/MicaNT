#pragma once

// Bounded baseline sequential JPEG decoder. This is user-mode-only code; the
// caller supplies the complete file bytes and owns the returned BGRA8 image.
#include "image_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace micant::um::imaging {
namespace jpeg_detail {

inline int32_t RoundToInt32(double value) noexcept {
    return static_cast<int32_t>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

struct HuffmanTable {
    std::array<int32_t, 17> minCode{};
    std::array<int32_t, 17> maxCode{};
    std::array<int32_t, 17> valueOffset{};
    std::array<uint8_t, 256> values{};
    uint16_t valueCount{};
    bool valid{};
};

struct Component {
    uint8_t id{}, h{}, v{}, quant{};
    uint8_t dcTable{}, acTable{};
    int32_t predictor{};
    uint32_t planeWidth{}, planeHeight{};
    uint8_t* plane{};
    size_t planeBytes{};
};

class EntropyReader {
public:
    EntropyReader(std::span<const uint8_t> bytes, size_t at) noexcept : bytes_(bytes), at_(at) {}

    bool bit(uint32_t& value) noexcept {
        if (bits_ == 0) {
            if (at_ >= bytes_.size()) return false;
            uint8_t byte = bytes_[at_++];
            if (byte == 0xff) {
                if (at_ >= bytes_.size() || bytes_[at_++] != 0x00) return false;
            }
            current_ = byte;
            bits_ = 8;
        }
        value = (current_ >> (bits_ - 1)) & 1u;
        --bits_;
        return true;
    }

    bool bits(uint32_t count, uint32_t& value) noexcept {
        value = 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t next{};
            if (!bit(next)) return false;
            value = (value << 1) | next;
        }
        return true;
    }

    bool marker(uint8_t& marker) noexcept {
        bits_ = 0;
        if (at_ >= bytes_.size() || bytes_[at_++] != 0xff) return false;
        while (at_ < bytes_.size() && bytes_[at_] == 0xff) ++at_;
        if (at_ >= bytes_.size()) return false;
        marker = bytes_[at_++];
        return marker != 0;
    }

    size_t offset() const noexcept { return at_; }

private:
    std::span<const uint8_t> bytes_;
    size_t at_{};
    uint8_t current_{};
    uint8_t bits_{};
};

class Parser {
public:
    Parser(std::span<const uint8_t> input, const ImageAllocator& allocator, ImageLimits limits) noexcept
        : input_(input), allocator_(allocator), limits_(limits) {}

    ~Parser() noexcept {
        for (size_t i = 0; i < componentCount_; ++i) {
            if (components_[i].plane) {
                allocator_.deallocate(allocator_.context, components_[i].plane, components_[i].planeBytes);
                components_[i].plane = nullptr;
            }
        }
    }

    ImageStatus decode(ImageFrame& output) noexcept {
        if (input_.size() > limits_.maxEncodedBytes) return ImageStatus::ImageTooLarge;
        if (input_.size() < 2) return ImageStatus::TruncatedInput;
        if (input_[0] != 0xff || input_[1] != 0xd8) return ImageStatus::UnknownFormat;
        size_t at = 2;
        bool haveFrame = false;
        bool haveScan = false;
        while (at < input_.size()) {
            uint8_t marker{};
            if (!nextMarker(at, marker)) return ImageStatus::TruncatedInput;
            if (marker == 0xd9) return ImageStatus::MalformedData;
            if (marker == 0xda) {
                if (!haveFrame) return ImageStatus::MalformedData;
                ImageStatus status = readScan(at);
                if (status != ImageStatus::Success) return status;
                haveScan = true;
                break;
            }
            if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
                return ImageStatus::MalformedData;
            uint16_t length{};
            if (!readU16(at, length)) return ImageStatus::TruncatedInput;
            if (length < 2 || static_cast<size_t>(length - 2) > input_.size() - at)
                return ImageStatus::TruncatedInput;
            const size_t end = at + length - 2;
            ImageStatus status = ImageStatus::Success;
            if (marker == 0xdb) status = readQuant(at, end);
            else if (marker == 0xc4) status = readHuffman(at, end);
            else if (marker == 0xc0) {
                if (haveFrame) return ImageStatus::MalformedData;
                status = readFrame(at, end);
                haveFrame = status == ImageStatus::Success;
            } else if (marker == 0xdd) {
                if (length != 4) return ImageStatus::MalformedData;
                restartInterval_ = static_cast<uint16_t>((input_[at] << 8) | input_[at + 1]);
            } else if (marker == 0xee) {
                readAdobeApp14(at, end);
            } else if (marker == 0xc2 || marker == 0xc1 || marker == 0xc3 ||
                       (marker >= 0xc5 && marker <= 0xcf && marker != 0xc8 && marker != 0xcc)) {
                return ImageStatus::UnsupportedFeature;
            }
            if (status != ImageStatus::Success) return status;
            at = end;
        }
        if (!haveScan) return ImageStatus::TruncatedInput;
        ImageStatus status = allocatePlanes();
        if (status != ImageStatus::Success) return status;
        status = decodeMcus(at);
        if (status != ImageStatus::Success) return status;
        uint8_t endMarker{};
        if (!entropy_->marker(endMarker) || endMarker != 0xd9) return ImageStatus::TruncatedInput;
        if (entropy_->offset() != input_.size()) return ImageStatus::MalformedData;
        return convert(output);
    }

private:
    bool nextMarker(size_t& at, uint8_t& marker) noexcept {
        if (at >= input_.size() || input_[at++] != 0xff) return false;
        while (at < input_.size() && input_[at] == 0xff) ++at;
        if (at >= input_.size()) return false;
        marker = input_[at++];
        return marker != 0;
    }

    bool readU16(size_t& at, uint16_t& value) const noexcept {
        if (input_.size() - std::min(at, input_.size()) < 2) return false;
        value = static_cast<uint16_t>((input_[at] << 8) | input_[at + 1]);
        at += 2;
        return true;
    }

    void readAdobeApp14(size_t at, size_t end) noexcept {
        constexpr std::array<uint8_t, 5> signature{'A', 'd', 'o', 'b', 'e'};
        if (end - at >= 12 && std::equal(signature.begin(), signature.end(), input_.begin() + static_cast<std::ptrdiff_t>(at))) {
            adobeApp14_ = true;
            adobeTransform_ = input_[at + 11];
        }
    }

    ImageStatus readQuant(size_t at, size_t end) noexcept {
        while (at < end) {
            const uint8_t info = input_[at++];
            const uint8_t precision = info >> 4;
            const uint8_t index = info & 0x0f;
            if (precision != 0 || index >= quant_.size()) return ImageStatus::UnsupportedFeature;
            if (end - at < 64) return ImageStatus::MalformedData;
            for (uint32_t i = 0; i < 64; ++i) {
                const uint8_t value = input_[at++];
                if (value == 0) return ImageStatus::MalformedData;
                quant_[index][zigzag_[i]] = value;
            }
            quantValid_[index] = true;
        }
        return at == end ? ImageStatus::Success : ImageStatus::MalformedData;
    }

    ImageStatus readHuffman(size_t at, size_t end) noexcept {
        while (at < end) {
            if (end - at < 17) return ImageStatus::MalformedData;
            const uint8_t info = input_[at++];
            const uint8_t cls = info >> 4, index = info & 0x0f;
            if (cls > 1 || index >= 4) return ImageStatus::MalformedData;
            HuffmanTable& table = cls ? ac_[index] : dc_[index];
            uint32_t count = 0;
            for (uint32_t length = 1; length <= 16; ++length) {
                const uint8_t n = input_[at++];
                counts_[length] = n;
                count += n;
            }
            if (count == 0 || count > 256 || count > end - at) return ImageStatus::MalformedData;
            table = {};
            uint32_t code = 0, value = 0;
            for (uint32_t length = 1; length <= 16; ++length) {
                if (counts_[length]) {
                    table.minCode[length] = static_cast<int32_t>(code);
                    table.maxCode[length] = static_cast<int32_t>(code + counts_[length] - 1);
                    table.valueOffset[length] = static_cast<int32_t>(value) - static_cast<int32_t>(code);
                    code += counts_[length];
                    value += counts_[length];
                } else {
                    table.maxCode[length] = -1;
                }
                code <<= 1;
                if (code > (1u << (length + 1))) return ImageStatus::MalformedData;
            }
            table.valueCount = static_cast<uint16_t>(count);
            std::copy_n(input_.begin() + static_cast<std::ptrdiff_t>(at), count, table.values.begin());
            at += count;
            table.valid = true;
        }
        return at == end ? ImageStatus::Success : ImageStatus::MalformedData;
    }

    ImageStatus readFrame(size_t at, size_t end) noexcept {
        if (end - at < 6) return ImageStatus::MalformedData;
        if (input_[at++] != 8) return ImageStatus::UnsupportedFeature;
        height_ = static_cast<uint32_t>((input_[at] << 8) | input_[at + 1]); at += 2;
        width_ = static_cast<uint32_t>((input_[at] << 8) | input_[at + 1]); at += 2;
        const uint8_t count = input_[at++];
        if (width_ == 0 || height_ == 0 || (count != 1 && count != 3) || end - at != count * 3)
            return ImageStatus::MalformedData;
        uint64_t pixels{};
        if (!mul(width_, height_, pixels) || pixels > limits_.maxPixels || pixels > limits_.maxDecodedBytes / 4)
            return ImageStatus::ImageTooLarge;
        componentCount_ = count;
        components_ = {};
        uint8_t maxH = 0, maxV = 0;
        for (size_t componentIndex = 0; componentIndex < componentCount_; ++componentIndex) {
            auto& component = components_[componentIndex];
            component.id = input_[at++];
            const uint8_t sampling = input_[at++];
            component.h = sampling >> 4;
            component.v = sampling & 0x0f;
            component.quant = input_[at++];
            if (component.h == 0 || component.h > 4 || component.v == 0 || component.v > 4 || component.quant >= 4)
                return ImageStatus::MalformedData;
            for (size_t priorIndex = 0; priorIndex < componentIndex; ++priorIndex)
                if (components_[priorIndex].id == component.id) return ImageStatus::MalformedData;
            maxH = std::max(maxH, component.h);
            maxV = std::max(maxV, component.v);
        }
        maxH_ = maxH; maxV_ = maxV;
        if (count == 3 && (components_[0].h != maxH || components_[0].v != maxV))
            return ImageStatus::UnsupportedFeature;
        if (count == 3 && adobeApp14_ && adobeTransform_ != 1)
            return ImageStatus::UnsupportedFeature;
        return ImageStatus::Success;
    }

    ImageStatus readScan(size_t& at) noexcept {
        uint16_t length{};
        if (!readU16(at, length)) return ImageStatus::TruncatedInput;
        if (length < 6 || length - 2 > input_.size() - at) return ImageStatus::TruncatedInput;
        const size_t end = at + length - 2;
        const uint8_t count = input_[at++];
        if (count != componentCount_ || end - at != count * 2 + 3) return ImageStatus::UnsupportedFeature;
        if (count == 3 && adobeApp14_ && adobeTransform_ != 1)
            return ImageStatus::UnsupportedFeature;
        std::array<bool, 3> seen{};
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t id = input_[at++], tables = input_[at++];
            if ((tables & 0xccu) != 0) return ImageStatus::MalformedData;
            size_t index = 0;
            while (index < componentCount_ && components_[index].id != id) ++index;
            if (index == componentCount_) return ImageStatus::MalformedData;
            if (seen[index]) return ImageStatus::MalformedData;
            seen[index] = true;
            components_[index].dcTable = tables >> 4;
            components_[index].acTable = tables & 0x0f;
            if (components_[index].dcTable >= 4 || components_[index].acTable >= 4) return ImageStatus::MalformedData;
        }
        const uint8_t start = input_[at++], finish = input_[at++], approximation = input_[at++];
        if (start != 0 || finish != 63 || approximation != 0) return ImageStatus::UnsupportedFeature;
        if (!std::all_of(seen.begin(), seen.begin() + count, [](bool b) { return b; })) return ImageStatus::MalformedData;
        for (size_t i = 0; i < componentCount_; ++i) {
            const auto& component = components_[i];
            if (!dc_[component.dcTable].valid || !ac_[component.acTable].valid || !quantValid_[component.quant])
                return ImageStatus::MalformedData;
        }
        at = end;
        entropy_ = EntropyReader(input_, at);
        return ImageStatus::Success;
    }

    ImageStatus allocatePlanes() noexcept {
        uint64_t total = 0;
        const uint32_t mcuW = maxH_ * 8, mcuH = maxV_ * 8;
        const uint64_t columns = (static_cast<uint64_t>(width_) + mcuW - 1) / mcuW;
        const uint64_t rows = (static_cast<uint64_t>(height_) + mcuH - 1) / mcuH;
        uint64_t blocksPerMcu = 0;
        for (const auto& c : components_) blocksPerMcu += static_cast<uint64_t>(c.h) * c.v;
        uint64_t mcuCount{}, totalBlocks{};
        if (!mul(columns, rows, mcuCount) || !mul(mcuCount, blocksPerMcu, totalBlocks) ||
            totalBlocks > kMaxIdctBlocks)
            return ImageStatus::ImageTooLarge;
        for (size_t componentIndex = 0; componentIndex < componentCount_; ++componentIndex) {
            auto& c = components_[componentIndex];
            const uint64_t pw = columns * c.h * 8, ph = rows * c.v * 8;
            uint64_t bytes{};
            if (pw > std::numeric_limits<uint32_t>::max() || ph > std::numeric_limits<uint32_t>::max() ||
                !mul(pw, ph, bytes) ||
                bytes > limits_.maxIntermediateBytes - std::min(total, limits_.maxIntermediateBytes))
                return ImageStatus::ImageTooLarge;
            total += bytes;
            c.planeWidth = static_cast<uint32_t>(pw);
            c.planeHeight = static_cast<uint32_t>(ph);
            c.planeBytes = static_cast<size_t>(bytes);
            if (!allocator_.allocate || !allocator_.deallocate) return ImageStatus::OutOfMemory;
            c.plane = static_cast<uint8_t*>(allocator_.allocate(allocator_.context, c.planeBytes));
            if (!c.plane) return ImageStatus::OutOfMemory;
            std::fill_n(c.plane, c.planeBytes, uint8_t{0});
        }
        const uint64_t outputBytes = static_cast<uint64_t>(width_) * height_ * 4;
        if (outputBytes > limits_.maxDecodedBytes || outputBytes > std::numeric_limits<size_t>::max())
            return ImageStatus::ImageTooLarge;
        return ImageStatus::Success;
    }

    bool decodeSymbol(const HuffmanTable& table, uint8_t& value) noexcept {
        if (!table.valid) return false;
        int32_t code = 0;
        for (uint32_t length = 1; length <= 16; ++length) {
            uint32_t b{};
            if (!entropy_->bit(b)) return false;
            code = (code << 1) | static_cast<int32_t>(b);
            if (table.maxCode[length] >= 0 && code <= table.maxCode[length]) {
                const int32_t index = code + table.valueOffset[length];
                if (index < 0 || index >= table.valueCount) return false;
                value = table.values[static_cast<size_t>(index)];
                return true;
            }
        }
        return false;
    }

    static int32_t extend(uint32_t value, uint32_t bits) noexcept {
        if (bits == 0) return 0;
        const uint32_t threshold = 1u << (bits - 1);
        return value < threshold ? static_cast<int32_t>(value) - static_cast<int32_t>((1u << bits) - 1) : static_cast<int32_t>(value);
    }

    bool decodeBlock(Component& c, uint8_t* destination) noexcept {
        std::array<int32_t, 64> coefficients{};
        uint8_t symbol{};
        uint32_t value{};
        const HuffmanTable& dct = dc_[c.dcTable];
        const HuffmanTable& act = ac_[c.acTable];
        if (!decodeSymbol(dct, symbol) || symbol > 11 || !entropy_->bits(symbol, value)) return false;
        const int64_t nextPredictor = static_cast<int64_t>(c.predictor) + extend(value, symbol);
        if (nextPredictor < std::numeric_limits<int32_t>::min() || nextPredictor > std::numeric_limits<int32_t>::max())
            return false;
        c.predictor = static_cast<int32_t>(nextPredictor);
        const int64_t dcCoefficient = nextPredictor * quant_[c.quant][0];
        if (dcCoefficient < std::numeric_limits<int32_t>::min() || dcCoefficient > std::numeric_limits<int32_t>::max())
            return false;
        coefficients[0] = static_cast<int32_t>(dcCoefficient);
        uint32_t k = 1;
        while (k < 64) {
            if (!decodeSymbol(act, symbol)) return false;
            if (symbol == 0) break;
            const uint32_t run = symbol >> 4, size = symbol & 0x0f;
            if (size == 0) {
                if (run != 15 || k + 16 > 64) return false;
                k += 16;
                continue;
            }
            if (size > 10 || k + run >= 64 || !entropy_->bits(size, value)) return false;
            k += run;
            const uint32_t natural = zigzag_[k++];
            const int64_t coefficient = static_cast<int64_t>(extend(value, size)) * quant_[c.quant][natural];
            if (coefficient < std::numeric_limits<int32_t>::min() || coefficient > std::numeric_limits<int32_t>::max())
                return false;
            coefficients[natural] = static_cast<int32_t>(coefficient);
        }
        idct(coefficients, destination);
        return true;
    }

    static void idct(const std::array<int32_t, 64>& in, uint8_t* out) noexcept {
        std::array<double, 64> horizontal{};
        for (uint32_t v = 0; v < 8; ++v) {
            for (uint32_t x = 0; x < 8; ++x) {
                double sum = 0.0;
                for (uint32_t u = 0; u < 8; ++u)
                    sum += in[v * 8 + u] * idctBasis_[x * 8 + u];
                horizontal[v * 8 + x] = sum;
            }
        }
        for (uint32_t y = 0; y < 8; ++y) {
            for (uint32_t x = 0; x < 8; ++x) {
                double sum = 0.0;
                for (uint32_t v = 0; v < 8; ++v)
                    sum += horizontal[v * 8 + x] * idctBasis_[y * 8 + v];
                const int32_t sample = RoundToInt32(sum * 0.25 + 128.0);
                out[y * 8 + x] = static_cast<uint8_t>(std::clamp(sample, 0, 255));
            }
        }
    }

    ImageStatus decodeMcus(size_t at) noexcept {
        entropy_.emplace(input_, at);
        const uint32_t mcuW = maxH_ * 8, mcuH = maxV_ * 8;
        const uint32_t columns = (width_ + mcuW - 1) / mcuW;
        const uint32_t rows = (height_ + mcuH - 1) / mcuH;
        const uint64_t totalMcus = static_cast<uint64_t>(columns) * rows;
        uint64_t mcuIndex = 0;
        uint8_t expectedRestart = 0xd0;
        for (uint32_t my = 0; my < rows; ++my) {
            for (uint32_t mx = 0; mx < columns; ++mx) {
                for (size_t componentIndex = 0; componentIndex < componentCount_; ++componentIndex) {
                    auto& c = components_[componentIndex];
                    for (uint32_t by = 0; by < c.v; ++by) {
                        for (uint32_t bx = 0; bx < c.h; ++bx) {
                            const uint32_t x = (mx * c.h + bx) * 8;
                            const uint32_t y = (my * c.v + by) * 8;
                            uint8_t block[64];
                            if (!decodeBlock(c, block)) return ImageStatus::TruncatedInput;
                            for (uint32_t row = 0; row < 8; ++row)
                                std::copy_n(block + row * 8, 8, c.plane + static_cast<size_t>(y + row) * c.planeWidth + x);
                        }
                    }
                }
                ++mcuIndex;
                if (restartInterval_ != 0 && mcuIndex < totalMcus && mcuIndex % restartInterval_ == 0) {
                    uint8_t marker{};
                    if (!entropy_->marker(marker)) return ImageStatus::TruncatedInput;
                    if (marker != expectedRestart) return ImageStatus::MalformedData;
                    expectedRestart = static_cast<uint8_t>(expectedRestart == 0xd7 ? 0xd0 : expectedRestart + 1);
                    for (size_t i = 0; i < componentCount_; ++i) components_[i].predictor = 0;
                }
            }
        }
        return ImageStatus::Success;
    }

    ImageStatus convert(ImageFrame& output) noexcept {
        ImageFrame decoded{};
        decoded.width = width_; decoded.height = height_;
        decoded.stride = width_ * 4;
        decoded.sourceFormat = ImageFormat::Jpeg;
        decoded.sourceColor = componentCount_ == 1 ? ImageColorModel::Gray : ImageColorModel::YCbCr;
        decoded.bitsPerChannel = 8;
        decoded.sourceHadAlpha = false;
        const size_t outputBytes = static_cast<size_t>(decoded.stride) * height_;
        if (!decoded.pixels.allocate(outputBytes, allocator_)) return ImageStatus::OutOfMemory;
        const Component& yComp = components_[0];
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const int32_t yy = sample(yComp, x, y);
                int32_t r = yy, g = yy, b = yy;
                if (componentCount_ == 3) {
                    const int32_t cb = sample(components_[1], x, y) - 128;
                    const int32_t cr = sample(components_[2], x, y) - 128;
                    r = yy + ((91881 * cr + 32768) >> 16);
                    g = yy - ((22554 * cb + 46802 * cr + 32768) >> 16);
                    b = yy + ((116130 * cb + 32768) >> 16);
                }
                const size_t offset = static_cast<size_t>(y) * decoded.stride + static_cast<size_t>(x) * 4;
                decoded.pixels[offset + 0] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                decoded.pixels[offset + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                decoded.pixels[offset + 2] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                decoded.pixels[offset + 3] = 255;
            }
        }
        output = std::move(decoded);
        return ImageStatus::Success;
    }

    int32_t sample(const Component& c, uint32_t x, uint32_t y) const noexcept {
        const uint32_t sx = static_cast<uint32_t>((static_cast<uint64_t>(x) * c.h) / maxH_);
        const uint32_t sy = static_cast<uint32_t>((static_cast<uint64_t>(y) * c.v) / maxV_);
        return c.plane[static_cast<size_t>(sy) * c.planeWidth + sx];
    }

    static bool mul(uint64_t a, uint64_t b, uint64_t& result) noexcept {
        if (a && b > std::numeric_limits<uint64_t>::max() / a) return false;
        result = a * b;
        return true;
    }

    inline static constexpr std::array<uint8_t, 64> zigzag_{
        0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
        12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
        35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
        58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
    static constexpr uint64_t kMaxIdctBlocks = 65536;
    inline static constexpr std::array<double, 64> idctBasis_{
        0.7071067811865475,0.9807852804032304,0.9238795325112867,0.8314696123025452,0.7071067811865476,0.5555702330196023,0.3826834323650898,0.1950903220161283,
        0.7071067811865475,0.8314696123025452,0.3826834323650898,-0.1950903220161282,-0.7071067811865475,-0.9807852804032304,-0.9238795325112868,-0.5555702330196022,
        0.7071067811865475,0.5555702330196023,-0.3826834323650897,-0.9807852804032304,-0.7071067811865477,0.1950903220161283,0.9238795325112865,0.8314696123025455,
        0.7071067811865475,0.1950903220161283,-0.9238795325112867,-0.5555702330196022,0.7071067811865474,0.8314696123025455,-0.3826834323650903,-0.9807852804032307,
        0.7071067811865475,-0.1950903220161282,-0.9238795325112868,0.5555702330196018,0.7071067811865474,-0.8314696123025451,0.3826834323650904,-0.9807852804032307,
        0.7071067811865475,-0.5555702330196022,-0.3826834323650903,0.9807852804032304,-0.7071067811865477,-0.1950903220161287,0.9238795325112865,-0.8314696123025455,
        0.7071067811865475,-0.8314696123025455,0.3826834323650896,0.1950903220161286,-0.7071067811865474,0.9807852804032307,-0.9238795325112865,0.5555702330196022,
        0.7071067811865475,-0.9807852804032304,0.9238795325112865,-0.8314696123025455,0.7071067811865474,-0.5555702330196022,0.3826834323650904,-0.1950903220161287};
    std::span<const uint8_t> input_;
    const ImageAllocator& allocator_;
    ImageLimits limits_;
    uint32_t width_{}, height_{};
    uint8_t maxH_{}, maxV_{};
    uint16_t restartInterval_{};
    bool adobeApp14_{};
    uint8_t adobeTransform_{};
    std::array<std::array<uint8_t, 64>, 4> quant_{};
    std::array<bool, 4> quantValid_{};
    std::array<HuffmanTable, 4> dc_{}, ac_{};
    std::array<uint8_t, 17> counts_{};
    std::array<Component, 3> components_{};
    size_t componentCount_{};
    std::optional<EntropyReader> entropy_;
};

} // namespace jpeg_detail

inline ImageStatus ProbeJpeg(std::span<const uint8_t> input, ImageFormat& format) noexcept {
    if (input.size() >= 2 && input[0] == 0xff && input[1] == 0xd8) {
        format = ImageFormat::Jpeg;
        return ImageStatus::Success;
    }
    return ImageStatus::UnknownFormat;
}

inline ImageStatus DecodeJpeg(std::span<const uint8_t> input, ImageFrame& output,
                              const ImageAllocator& allocator, ImageLimits limits = {}) noexcept {
    ImageFormat format{};
    const ImageStatus probe = ProbeJpeg(input, format);
    if (probe != ImageStatus::Success) return input.size() < 2 ? ImageStatus::TruncatedInput : probe;
    jpeg_detail::Parser parser(input, allocator, limits);
    return parser.decode(output);
}

} // namespace micant::um::imaging
