#include "taskbar.hpp"
#include "notification_area.hpp"
#include "../gui_common.hpp"

namespace micant::explorer {
namespace {

using micant_gui::PackBGRA;

constexpr uint32_t kBlue = 0xFF245BAE;
constexpr uint32_t kBlueEdge = 0xFF5B9AE3;
constexpr uint32_t kBlueShadow = 0xFF1E468E;
constexpr uint32_t kTaskBlue = 0xFF3D76C7;
constexpr uint32_t kTaskActive = 0xFF1C4F91;

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
          const char* value, uint32_t color) noexcept {
    micant_gui::DrawStringClipped(pixels, width, height, x, y, value, color);
}

} // namespace

void DrawTaskbar(uint32_t* pixels, uint32_t screenWidth, uint32_t screenHeight,
                 uint32_t taskbarHeight, const TaskbarButton* buttons,
                 size_t buttonCount, const char* clockText) noexcept {
    if (!pixels || screenWidth == 0 || screenHeight == 0 || taskbarHeight < 32 ||
        taskbarHeight > screenHeight) return;
    const int32_t barY = static_cast<int32_t>(screenHeight - taskbarHeight);
    fill(pixels, screenWidth, screenHeight, 0, barY, screenWidth, taskbarHeight, kBlue);
    fill(pixels, screenWidth, screenHeight, 0, barY, screenWidth, 3, kBlueEdge);
    fill(pixels, screenWidth, screenHeight, 0, barY + 4, screenWidth, 1, kBlueShadow);

    // Start button: a compact four pane mark and label.
    fill(pixels, screenWidth, screenHeight, 0, barY + 3, 102, taskbarHeight - 3, 0xFF2D9331);
    fill(pixels, screenWidth, screenHeight, 0, barY + 3, 8, taskbarHeight - 3, 0xFF52B744);
    fill(pixels, screenWidth, screenHeight, 12, barY + 8, 24, 24, 0xFFE1EEF3);
    fill(pixels, screenWidth, screenHeight, 15, barY + 11, 8, 8, 0xFFEB3B2F);
    fill(pixels, screenWidth, screenHeight, 24, barY + 11, 8, 8, 0xFF4BA541);
    fill(pixels, screenWidth, screenHeight, 15, barY + 20, 8, 8, 0xFF357DCA);
    fill(pixels, screenWidth, screenHeight, 24, barY + 20, 8, 8, 0xFFFAC532);
    text(pixels, screenWidth, screenHeight, 42, barY + 15, "start", 0xFFFFFFFF);

    const int32_t taskRight = static_cast<int32_t>(screenWidth) - 126;
    const int32_t maxTaskWidth = taskRight > 112 ? (taskRight - 112) : 0;
    const int32_t taskWidth = buttonCount == 0 ? 146 :
        (maxTaskWidth / static_cast<int32_t>(buttonCount) < 146
            ? maxTaskWidth / static_cast<int32_t>(buttonCount) : 146);
    int32_t taskX = 112;
    for (size_t i = 0; buttons && i < buttonCount && taskWidth >= 64; ++i) {
        fill(pixels, screenWidth, screenHeight, taskX, barY + 8, taskWidth - 4, 25,
             buttons[i].active ? kTaskActive : kTaskBlue);
        fill(pixels, screenWidth, screenHeight, taskX + 1, barY + 9, taskWidth - 6, 1, 0xFF74A6E1);
        fill(pixels, screenWidth, screenHeight, taskX + 5, barY + 14, 13, 13,
             buttons[i].iconColor ? buttons[i].iconColor : PackBGRA(70, 130, 190));
        const int32_t textX = taskX + 24;
        const int32_t availableChars = (taskWidth - 31) / 8;
        char clipped[32]{};
        size_t length = 0;
        while (buttons[i].title && buttons[i].title[length] &&
               length < static_cast<size_t>(availableChars) && length < sizeof(clipped) - 1) {
            clipped[length] = buttons[i].title[length];
            ++length;
        }
        text(pixels, screenWidth, screenHeight, textX, barY + 16, clipped, 0xFFFFFFFF);
        taskX += taskWidth;
    }

    DrawNotificationArea(pixels, screenWidth, taskbarHeight, clockText);
}

} // namespace micant::explorer
