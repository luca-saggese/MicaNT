#pragma once

#include <cstdint>
#include <algorithm>

namespace micant::shared::graphics {

// Pixels are packed AARRGGBB with premultiplied color channels.
[[nodiscard]] inline constexpr uint32_t PremultipliedSourceOver(
    uint32_t source, uint32_t destination) noexcept {
    const uint32_t alpha = source >> 24;
    const uint32_t inverse = 255 - alpha;
    const auto blend = [&](unsigned shift) constexpr noexcept {
        const uint32_t sourceChannel = (source >> shift) & 0xff;
        const uint32_t destinationChannel = (destination >> shift) & 0xff;
        const uint32_t value = sourceChannel +
            (destinationChannel * inverse + 127) / 255;
        return value > 255 ? 255u : value;
    };
    const uint32_t destinationAlpha = destination >> 24;
    const uint32_t outputAlpha = alpha +
        (destinationAlpha * inverse + 127) / 255;
    return (std::min(outputAlpha, 255u) << 24) |
        (blend(16) << 16) | (blend(8) << 8) | blend(0);
}

} // namespace micant::shared::graphics
