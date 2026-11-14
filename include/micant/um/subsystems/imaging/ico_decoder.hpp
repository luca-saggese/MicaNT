#pragma once

// Bounded Ring-3 ICO reader. ICO directory records are parsed bytewise and
// image payloads are decoded into the same premultiplied BGRA representation
// used by the other imaging clients.
#include "bmp_png_decoder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <span>

namespace micant::um::imaging {

struct IconDirectoryEntry {
    uint32_t width{};
    uint32_t height{};
    uint8_t colorCount{};
    uint16_t planes{};
    uint16_t bitCount{};
    uint32_t byteCount{};
    uint32_t imageOffset{};
};

struct IconDirectory {
    std::span<const uint8_t> bytes{};
    uint16_t count{};
    uint32_t directoryBytes{};

    [[nodiscard]] bool entry(uint16_t index, IconDirectoryEntry& out) const noexcept {
        if (index >= count || directoryBytes > bytes.size()) return false;
        const size_t offset = 6u + static_cast<size_t>(index) * 16u;
        if (offset > directoryBytes || directoryBytes - offset < 16) return false;
        const uint8_t* p = bytes.data() + offset;
        out.width = p[0] ? p[0] : 256;
        out.height = p[1] ? p[1] : 256;
        out.colorCount = p[2];
        out.planes = detail::ReadLe16(p + 4);
        out.bitCount = detail::ReadLe16(p + 6);
        out.byteCount = detail::ReadLe32(p + 8);
        out.imageOffset = detail::ReadLe32(p + 12);
        return true;
    }

    [[nodiscard]] std::span<const uint8_t> image(uint16_t index) const noexcept {
        IconDirectoryEntry value{};
        if (!entry(index, value) || value.imageOffset < directoryBytes ||
            value.imageOffset > bytes.size() ||
            value.byteCount > bytes.size() - value.imageOffset) return {};
        return bytes.subspan(value.imageOffset, value.byteCount);
    }
};

inline ImageStatus ParseIconDirectory(std::span<const uint8_t> input,
                                      IconDirectory& output,
                                      ImageLimits limits = {}) noexcept {
    if (input.size() > limits.maxEncodedBytes) return ImageStatus::ImageTooLarge;
    if (input.size() < 6) return ImageStatus::TruncatedInput;
    if (detail::ReadLe16(input.data()) != 0 || detail::ReadLe16(input.data() + 2) != 1)
        return ImageStatus::MalformedData;
    const uint16_t count = detail::ReadLe16(input.data() + 4);
    if (!count || count > 1024) return ImageStatus::MalformedData;
    const uint64_t tableBytes = 6ull + static_cast<uint64_t>(count) * 16ull;
    if (tableBytes > input.size()) return ImageStatus::TruncatedInput;

    uint64_t totalPayload = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = input.data() + 6u + static_cast<size_t>(i) * 16u;
        const uint32_t length = detail::ReadLe32(p + 8);
        const uint32_t offset = detail::ReadLe32(p + 12);
        const uint32_t width = p[0] ? p[0] : 256;
        const uint32_t height = p[1] ? p[1] : 256;
        uint64_t pixels{};
        if (!length || offset < tableBytes || offset > input.size() ||
            length > input.size() - offset)
            return ImageStatus::MalformedData;
        if (!detail::CheckedMul(width, height, pixels) || pixels > limits.maxPixels)
            return ImageStatus::ImageTooLarge;
        if (totalPayload > limits.maxEncodedBytes - length) return ImageStatus::ImageTooLarge;
        totalPayload += length;
        for (uint16_t j = 0; j < i; ++j) {
            const uint8_t* q = input.data() + 6u + static_cast<size_t>(j) * 16u;
            const uint64_t otherStart = detail::ReadLe32(q + 12);
            const uint64_t otherEnd = otherStart + detail::ReadLe32(q + 8);
            if (offset < otherEnd && otherStart < static_cast<uint64_t>(offset) + length)
                return ImageStatus::MalformedData;
        }
    }
    output = {input, count, static_cast<uint32_t>(tableBytes)};
    return ImageStatus::Success;
}

inline ImageStatus SelectIconDirectoryEntry(const IconDirectory& directory,
                                            uint32_t width, uint32_t height,
                                            uint16_t preferredBitCount,
                                            uint16_t& selected) noexcept {
    if (!directory.count || width == 0 || height == 0) return ImageStatus::MalformedData;
    uint64_t bestScore = UINT64_MAX;
    bool found = false;
    for (uint16_t i = 0; i < directory.count; ++i) {
        IconDirectoryEntry item{};
        if (!directory.entry(i, item)) return ImageStatus::MalformedData;
        // Prefer the smallest image that covers the request; only upscale if
        // every available image is smaller. Depth then stabilizes ties.
        const uint64_t scalePenalty = (item.width < width || item.height < height)
            ? 1'000'000ull + (static_cast<uint64_t>(width - std::min(width, item.width)) +
                               height - std::min(height, item.height)) * 1024ull
            : (static_cast<uint64_t>(item.width - width) + item.height - height) * 1024ull;
        const uint64_t depthPenalty = preferredBitCount
            ? (item.bitCount >= preferredBitCount ? item.bitCount - preferredBitCount
                                                  : preferredBitCount - item.bitCount)
            : 0;
        const uint64_t score = scalePenalty + depthPenalty;
        if (!found || score < bestScore) { found = true; bestScore = score; selected = i; }
    }
    return found ? ImageStatus::Success : ImageStatus::MalformedData;
}

inline ImageStatus DecodeIcoDib(std::span<const uint8_t> input, ImageFrame& output,
                                const ImageAllocator& allocator,
                                ImageLimits limits = {}) noexcept {
    using namespace detail;
    if (input.size() > limits.maxEncodedBytes) return ImageStatus::ImageTooLarge;
    if (input.size() < 40) return ImageStatus::TruncatedInput;
    const uint32_t headerSize = ReadLe32(input.data());
    if (headerSize < 40 || headerSize > input.size()) return ImageStatus::MalformedData;
    const int32_t signedWidth = static_cast<int32_t>(ReadLe32(input.data() + 4));
    const int32_t combinedHeight = static_cast<int32_t>(ReadLe32(input.data() + 8));
    const uint16_t planes = ReadLe16(input.data() + 12);
    const uint16_t bpp = ReadLe16(input.data() + 14);
    const uint32_t compression = ReadLe32(input.data() + 16);
    const uint32_t colorsUsed = ReadLe32(input.data() + 32);
    if (signedWidth <= 0 || combinedHeight <= 0 || planes != 1 || compression != 0)
        return compression ? ImageStatus::UnsupportedFeature : ImageStatus::MalformedData;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)
        return ImageStatus::UnsupportedFeature;
    const uint32_t width = static_cast<uint32_t>(signedWidth);
    const uint32_t totalHeight = static_cast<uint32_t>(combinedHeight);
    if (totalHeight & 1u) return ImageStatus::MalformedData;
    const uint32_t height = totalHeight / 2;
    uint32_t dstStride{}; size_t dstBytes{};
    if (!FrameSize(width, height, limits, dstStride, dstBytes)) return ImageStatus::ImageTooLarge;

    uint64_t xorBits{}, xorStride{}, xorBytes{}, andBits{}, andStride{}, andBytes{};
    if (!CheckedMul(width, bpp, xorBits) || xorBits > UINT64_MAX - 31 ||
        !CheckedMul(width, 1, andBits) || andBits > UINT64_MAX - 31)
        return ImageStatus::ImageTooLarge;
    xorStride = ((xorBits + 31) / 32) * 4;
    andStride = ((andBits + 31) / 32) * 4;
    if (!CheckedMul(xorStride, height, xorBytes) || !CheckedMul(andStride, height, andBytes))
        return ImageStatus::ImageTooLarge;
    const uint32_t paletteCount = bpp <= 8 ? (colorsUsed ? colorsUsed : (1u << bpp)) : 0;
    if (paletteCount > 256) return ImageStatus::MalformedData;
    uint64_t paletteBytes = static_cast<uint64_t>(paletteCount) * 4;
    uint64_t xorOffset = static_cast<uint64_t>(headerSize) + paletteBytes;
    if (xorOffset > input.size() || xorBytes > input.size() - xorOffset ||
        andBytes > input.size() - xorOffset - xorBytes)
        return ImageStatus::TruncatedInput;
    const uint8_t* palette = input.data() + headerSize;
    const uint8_t* xorData = input.data() + xorOffset;
    const uint8_t* andData = xorData + xorBytes;

    ImageFrame frame{};
    frame.width = width; frame.height = height; frame.stride = dstStride;
    frame.sourceFormat = ImageFormat::Bmp;
    frame.sourceColor = ImageColorModel::Rgb; frame.bitsPerChannel = 8;
    if (!frame.pixels.allocate(dstBytes, allocator)) return ImageStatus::OutOfMemory;
    bool hasAlpha = false;
    if (bpp == 32) {
        for (uint32_t y = 0; y < height && !hasAlpha; ++y) {
            const uint8_t* row = xorData + static_cast<size_t>(height - 1 - y) * xorStride;
            for (uint32_t x = 0; x < width; ++x) if (row[x * 4 + 3]) { hasAlpha = true; break; }
        }
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = xorData + static_cast<size_t>(height - 1 - y) * xorStride;
        const uint8_t* mask = andData + static_cast<size_t>(height - 1 - y) * andStride;
        uint8_t* dst = frame.pixels.data() + static_cast<size_t>(y) * dstStride;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t b{}, g{}, r{}, alpha = 255;
            if (bpp == 32) {
                b = src[x * 4]; g = src[x * 4 + 1]; r = src[x * 4 + 2];
                if (hasAlpha) alpha = src[x * 4 + 3];
            } else if (bpp == 24) {
                b = src[x * 3]; g = src[x * 3 + 1]; r = src[x * 3 + 2];
            } else {
                uint32_t index{};
                if (bpp == 8) index = src[x];
                else if (bpp == 4) index = (src[x / 2] >> ((x & 1) ? 0 : 4)) & 0xf;
                else index = (src[x / 8] >> (7 - (x & 7))) & 1;
                if (index >= paletteCount) return ImageStatus::MalformedData;
                b = palette[index * 4]; g = palette[index * 4 + 1]; r = palette[index * 4 + 2];
            }
            const bool transparent = (mask[x / 8] & (0x80u >> (x & 7))) != 0;
            if (!hasAlpha && transparent) alpha = 0;
            dst[x * 4] = Premul(b, alpha); dst[x * 4 + 1] = Premul(g, alpha);
            dst[x * 4 + 2] = Premul(r, alpha); dst[x * 4 + 3] = alpha;
        }
    }
    frame.sourceHadAlpha = hasAlpha;
    output = std::move(frame);
    return ImageStatus::Success;
}

inline ImageStatus DecodeIcoImage(std::span<const uint8_t> input,
                                  const IconDirectoryEntry& entry,
                                  ImageFrame& output,
                                  const ImageAllocator& allocator,
                                  ImageLimits limits = {}) noexcept {
    static constexpr std::array<uint8_t, 8> pngSignature{137, 80, 78, 71, 13, 10, 26, 10};
    if (input.size() < 8) return ImageStatus::TruncatedInput;
    if (std::equal(pngSignature.begin(), pngSignature.end(), input.begin()))
        return DecodePng(input, output, allocator, limits);
    return DecodeIcoDib(input, output, allocator, limits);
}

inline ImageStatus DecodeIcon(std::span<const uint8_t> input, uint32_t width,
                              uint32_t height, ImageFrame& output,
                              const ImageAllocator& allocator,
                              ImageLimits limits = {}) noexcept {
    IconDirectory directory{};
    ImageStatus status = ParseIconDirectory(input, directory, limits);
    if (status != ImageStatus::Success) return status;
    uint16_t index{};
    status = SelectIconDirectoryEntry(directory, width, height, 32, index);
    if (status != ImageStatus::Success) return status;
    IconDirectoryEntry entry{};
    if (!directory.entry(index, entry)) return ImageStatus::MalformedData;
    status = DecodeIcoImage(directory.image(index), entry, output, allocator, limits);
    if (status != ImageStatus::Success) return status;
    if (output.width != entry.width || output.height != entry.height)
        return ImageStatus::MalformedData;
    return ImageStatus::Success;
}

} // namespace micant::um::imaging
