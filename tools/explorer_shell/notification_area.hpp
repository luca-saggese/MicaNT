#pragma once

#include <cstdint>
#include "theme_palette.hpp"

namespace micant::explorer {

enum class NotificationAction : uint8_t { None, Volume, Network, Clock };

void DrawNotificationArea(uint32_t* pixels, uint32_t width, uint32_t height,
                          const char* clockText, const ThemePalette& palette) noexcept;
NotificationAction HitTestNotificationArea(int32_t x, uint32_t width) noexcept;

} // namespace micant::explorer
