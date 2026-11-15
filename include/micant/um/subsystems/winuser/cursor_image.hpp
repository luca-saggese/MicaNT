#pragma once

// CUR directory and hotspot handling layered on the existing bounded ICO
// DIB/PNG decoders. The image payload format is shared; only the directory
// type and two hotspot fields differ between ICO and CUR.
#include "micant/um/subsystems/imaging/ico_decoder.hpp"

namespace micant::um::winuser {

struct CursorImage {
    imaging::ImageFrame image{};
    uint16_t hotX{};
    uint16_t hotY{};
};

inline imaging::ImageStatus DecodeCursorImage(
    std::span<const uint8_t> file, uint32_t requestedWidth,
    uint32_t requestedHeight, CursorImage& output,
    const imaging::ImageAllocator& allocator,
    imaging::ImageLimits limits = {}) noexcept {
    using namespace imaging;
    output = {};
    if (file.size() > limits.maxEncodedBytes) return ImageStatus::ImageTooLarge;
    if (file.size() < 6) return ImageStatus::TruncatedInput;
    if (detail::ReadLe16(file.data()) != 0 || detail::ReadLe16(file.data() + 2) != 2)
        return ImageStatus::MalformedData;
    const uint16_t count = detail::ReadLe16(file.data() + 4);
    if (!count || count > 1024) return ImageStatus::MalformedData;
    const uint64_t tableBytes = 6ull + static_cast<uint64_t>(count) * 16;
    if (tableBytes > file.size()) return ImageStatus::TruncatedInput;

    IconDirectory directory{file, count, static_cast<uint32_t>(tableBytes)};
    uint64_t totalPayload = 0;
    for (uint16_t i = 0; i < count; ++i) {
        IconDirectoryEntry entry{};
        if (!directory.entry(i, entry) || !entry.byteCount ||
            entry.imageOffset < tableBytes || entry.imageOffset > file.size() ||
            entry.byteCount > file.size() - entry.imageOffset)
            return ImageStatus::MalformedData;
        uint64_t pixels{};
        if (!detail::CheckedMul(entry.width, entry.height, pixels) || pixels > limits.maxPixels)
            return ImageStatus::ImageTooLarge;
        if (totalPayload > limits.maxEncodedBytes - entry.byteCount)
            return ImageStatus::ImageTooLarge;
        totalPayload += entry.byteCount;
        for (uint16_t j = 0; j < i; ++j) {
            IconDirectoryEntry previous{};
            if (!directory.entry(j, previous)) return ImageStatus::MalformedData;
            const uint64_t a0 = entry.imageOffset, a1 = a0 + entry.byteCount;
            const uint64_t b0 = previous.imageOffset, b1 = b0 + previous.byteCount;
            if (a0 < b1 && b0 < a1) return ImageStatus::MalformedData;
        }
    }

    uint16_t selected{};
    const uint32_t wantedWidth = requestedWidth ? requestedWidth : 32;
    const uint32_t wantedHeight = requestedHeight ? requestedHeight : 32;
    uint64_t bestScore = UINT64_MAX;
    bool found = false;
    for (uint16_t i = 0; i < count; ++i) {
        IconDirectoryEntry candidate{};
        if (!directory.entry(i, candidate)) return ImageStatus::MalformedData;
        const uint64_t shortfall = (candidate.width < wantedWidth
            ? wantedWidth - candidate.width : 0) +
            (candidate.height < wantedHeight ? wantedHeight - candidate.height : 0);
        const uint64_t excess = (candidate.width > wantedWidth
            ? candidate.width - wantedWidth : 0) +
            (candidate.height > wantedHeight ? candidate.height - wantedHeight : 0);
        const uint64_t score = shortfall ? (1ull << 48) + shortfall * 4096 : excess * 4096;
        if (!found || score < bestScore) { found = true; bestScore = score; selected = i; }
    }
    if (!found) return ImageStatus::MalformedData;
    IconDirectoryEntry entry{};
    if (!directory.entry(selected, entry)) return ImageStatus::MalformedData;
    const uint8_t* record = file.data() + 6 + static_cast<size_t>(selected) * 16;
    const uint16_t hotX = detail::ReadLe16(record + 4);
    const uint16_t hotY = detail::ReadLe16(record + 6);
    ImageFrame decoded{};
    const auto status = DecodeIcoImage(directory.image(selected), entry, decoded,
                                       allocator, limits);
    if (status != ImageStatus::Success) return status;
    if (decoded.width != entry.width || decoded.height != entry.height ||
        hotX >= decoded.width || hotY >= decoded.height)
        return ImageStatus::MalformedData;
    output.image = std::move(decoded);
    output.hotX = hotX;
    output.hotY = hotY;
    return ImageStatus::Success;
}

} // namespace micant::um::winuser
