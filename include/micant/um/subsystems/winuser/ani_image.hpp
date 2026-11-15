#pragma once

// Bounded RIFF ACON directory parser. It exposes immutable embedded CUR
// payloads and timing metadata; frame decoding and timer scheduling stay in
// Ring 3 callers.
#include <array>
#include <cstdint>
#include <span>

namespace micant::um::winuser {

enum class AniStatus : uint8_t { Success, Truncated, Malformed, Unsupported, TooLarge };

struct AniStep {
    uint16_t frameIndex{};
    uint32_t durationJiffies{};
};

struct AniFileView {
    static constexpr size_t MaxFrames = 64;
    static constexpr size_t MaxSteps = 256;
    std::array<std::span<const uint8_t>, MaxFrames> frames{};
    std::array<AniStep, MaxSteps> steps{};
    uint16_t frameCount{};
    uint16_t stepCount{};
    uint32_t declaredFrames{};
    uint32_t declaredSteps{};
    uint32_t defaultRateJiffies{6};
    uint32_t flags{};
    uint32_t totalBytes{};
};

namespace ani_detail {
inline uint32_t le32(std::span<const uint8_t> b, size_t p) noexcept {
    return uint32_t(b[p]) | (uint32_t(b[p+1]) << 8) |
        (uint32_t(b[p+2]) << 16) | (uint32_t(b[p+3]) << 24);
}
inline bool tag(std::span<const uint8_t> b, size_t p, const char (&v)[5]) noexcept {
    return p <= b.size() && b.size() - p >= 4 &&
        b[p] == uint8_t(v[0]) && b[p+1] == uint8_t(v[1]) &&
        b[p+2] == uint8_t(v[2]) && b[p+3] == uint8_t(v[3]);
}
inline bool validCur(std::span<const uint8_t> b) noexcept {
    if (b.size() < 6 || uint16_t(b[0] | (uint16_t(b[1]) << 8)) != 0 ||
        uint16_t(b[2] | (uint16_t(b[3]) << 8)) != 2) return false;
    const uint16_t count = static_cast<uint16_t>(b[4] | (uint16_t(b[5]) << 8));
    if (!count || count > 1024 || uint64_t(count) * 16 + 6 > b.size()) return false;
    const size_t tableEnd = 6 + size_t(count) * 16;
    for (uint16_t i = 0; i < count; ++i) {
        const size_t e = 6 + size_t(i) * 16;
        const uint32_t length = le32(b, e + 8), offset = le32(b, e + 12);
        if (!length || offset < tableEnd || offset > b.size() || length > b.size() - offset)
            return false;
        const uint32_t width = b[e] ? b[e] : 256, height = b[e+1] ? b[e+1] : 256;
        const uint16_t hotX = uint16_t(b[e+4] | (uint16_t(b[e+5]) << 8));
        const uint16_t hotY = uint16_t(b[e+6] | (uint16_t(b[e+7]) << 8));
        if (hotX >= width || hotY >= height) return false;
    }
    return true;
}
}

inline AniStatus ParseAniFile(std::span<const uint8_t> bytes, AniFileView& output,
                              uint64_t maxEncodedBytes = 64ull * 1024 * 1024) noexcept {
    output = {};
    if (bytes.size() > maxEncodedBytes) return AniStatus::TooLarge;
    if (bytes.size() < 12) return AniStatus::Truncated;
    if (!ani_detail::tag(bytes, 0, "RIFF") || !ani_detail::tag(bytes, 8, "ACON"))
        return AniStatus::Malformed;
    const uint32_t riffPayload = ani_detail::le32(bytes, 4);
    if (riffPayload < 4 || uint64_t(riffPayload) + 8 != bytes.size()) return AniStatus::Malformed;
    const size_t end = bytes.size();
    bool haveHeader = false, haveRate = false, haveSequence = false;
    std::array<uint32_t, AniFileView::MaxSteps> rates{};
    std::array<uint32_t, AniFileView::MaxSteps> sequence{};
    size_t rateCount = 0, sequenceCount = 0;
    for (size_t pos = 12; pos < end;) {
        if (end - pos < 8) return AniStatus::Truncated;
        const uint32_t length = ani_detail::le32(bytes, pos + 4);
        const size_t payload = pos + 8;
        if (length > end - payload) return AniStatus::Truncated;
        const size_t padded = size_t(length) + (length & 1u);
        if (padded > end - payload) return AniStatus::Truncated;
        if (ani_detail::tag(bytes, pos, "anih")) {
            if (haveHeader || length < 36) return AniStatus::Malformed;
            const auto chunk = bytes.subspan(payload, length);
            if (ani_detail::le32(chunk, 0) < 36) return AniStatus::Malformed;
            output.declaredFrames = ani_detail::le32(chunk, 4);
            output.declaredSteps = ani_detail::le32(chunk, 8);
            output.defaultRateJiffies = ani_detail::le32(chunk, 28);
            output.flags = ani_detail::le32(chunk, 32);
            if (!output.declaredFrames || !output.declaredSteps ||
                output.declaredFrames > AniFileView::MaxFrames ||
                output.declaredSteps > AniFileView::MaxSteps) return AniStatus::TooLarge;
            if ((output.flags & ~3u) || !(output.flags & 1u)) return AniStatus::Unsupported;
            if (!output.defaultRateJiffies) output.defaultRateJiffies = 6;
            haveHeader = true;
        } else if (ani_detail::tag(bytes, pos, "rate") || ani_detail::tag(bytes, pos, "seq ")) {
            const bool isRate = ani_detail::tag(bytes, pos, "rate");
            bool& seen = isRate ? haveRate : haveSequence;
            size_t& count = isRate ? rateCount : sequenceCount;
            auto& values = isRate ? rates : sequence;
            if (seen || !length || (length & 3u)) return AniStatus::Malformed;
            count = length / 4;
            if (count > AniFileView::MaxSteps) return AniStatus::TooLarge;
            for (size_t i = 0; i < count; ++i) values[i] = ani_detail::le32(bytes, payload + i * 4);
            seen = true;
        } else if (ani_detail::tag(bytes, pos, "LIST")) {
            if (length < 4) return AniStatus::Malformed;
            if (ani_detail::tag(bytes, payload, "fram")) {
                for (size_t child = payload + 4; child < payload + length;) {
                    if (payload + length - child < 8) return AniStatus::Truncated;
                    const uint32_t childLength = ani_detail::le32(bytes, child + 4);
                    const size_t childPayload = child + 8;
                    if (childLength > payload + length - childPayload) return AniStatus::Truncated;
                    const size_t childPadded = size_t(childLength) + (childLength & 1u);
                    if (childPadded > payload + length - childPayload) return AniStatus::Truncated;
                    if (ani_detail::tag(bytes, child, "icon")) {
                        if (!childLength || output.frameCount == AniFileView::MaxFrames)
                            return AniStatus::TooLarge;
                        const auto frame = bytes.subspan(childPayload, childLength);
                        if (!ani_detail::validCur(frame)) return AniStatus::Malformed;
                        output.frames[output.frameCount++] = frame;
                    }
                    child = childPayload + childPadded;
                }
            }
        }
        pos = payload + padded;
    }
    if (!haveHeader || !output.frameCount || output.frameCount != output.declaredFrames ||
        (haveRate && rateCount != output.declaredSteps) ||
        (haveSequence && sequenceCount != output.declaredSteps)) return AniStatus::Malformed;
    output.stepCount = static_cast<uint16_t>(output.declaredSteps);
    for (uint16_t i = 0; i < output.stepCount; ++i) {
        const uint32_t frame = haveSequence ? sequence[i] : i % output.frameCount;
        const uint32_t rate = haveRate ? rates[i] : output.defaultRateJiffies;
        if (frame >= output.frameCount || !rate) return AniStatus::Malformed;
        output.steps[i] = {static_cast<uint16_t>(frame), rate};
    }
    output.totalBytes = static_cast<uint32_t>(bytes.size());
    return AniStatus::Success;
}

} // namespace micant::um::winuser
