#include "notification_area.hpp"
#include "../gui_common.hpp"

namespace micant::explorer {
namespace {


void fill(uint32_t* pixels, uint32_t width, uint32_t height, int32_t x, int32_t y,
          int32_t rectWidth, int32_t rectHeight, uint32_t color) noexcept {
    if (!pixels || rectWidth <= 0 || rectHeight <= 0) return;
    const int32_t left = x < 0 ? 0 : x;
    const int32_t top = y < 0 ? 0 : y;
    const int32_t right = x + rectWidth > static_cast<int32_t>(width)
        ? static_cast<int32_t>(width) : x + rectWidth;
    const int32_t bottom = y + rectHeight > static_cast<int32_t>(height)
        ? static_cast<int32_t>(height) : y + rectHeight;
    for (int32_t row = top; row < bottom; ++row) {
        uint32_t* destination = pixels + static_cast<size_t>(row) * width + left;
        for (int32_t column = left; column < right; ++column) destination[column - left] = color;
    }
}

void text(uint32_t* pixels, uint32_t width, uint32_t height, int32_t x, int32_t y,
          const char* value) noexcept {
    micant_gui::DrawStringClipped(pixels, width, height, x, y, value, 0xFFFFFFFFu);
}

} // namespace

void DrawNotificationArea(uint32_t* pixels, uint32_t width, uint32_t height,
                          const char* clockText) noexcept {
    if (!pixels || width < 122 || height < 32) return;
    const int32_t left = static_cast<int32_t>(width) - 122;
    fill(pixels, width, height, left, 5, 122, static_cast<int32_t>(height) - 5, 0xFF1D4F97);
    fill(pixels, width, height, left, 7, 1, static_cast<int32_t>(height) - 9, 0xFF193E7A);

    // System sound indicator.
    fill(pixels, width, height, left + 12, 17, 5, 8, 0xFFEFF5FC);
    fill(pixels, width, height, left + 17, 14, 4, 14, 0xFFEFF5FC);
    // Network indicator.
    fill(pixels, width, height, left + 34, 24, 4, 4, 0xFFF7F7F7);
    fill(pixels, width, height, left + 33, 20, 6, 3, 0xFFF7F7F7);
    fill(pixels, width, height, left + 31, 16, 10, 3, 0xFFF7F7F7);
    text(pixels, width, height, left + 50, 16, clockText ? clockText : "--:--");
}

NotificationAction HitTestNotificationArea(int32_t x, uint32_t width) noexcept {
    const int32_t left = static_cast<int32_t>(width) - 122;
    if (x >= left + 8 && x < left + 29) return NotificationAction::Volume;
    if (x >= left + 29 && x < left + 48) return NotificationAction::Network;
    if (x >= left + 48 && x < static_cast<int32_t>(width)) return NotificationAction::Clock;
    return NotificationAction::None;
}

} // namespace micant::explorer
