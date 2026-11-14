#include "desktop.hpp"
#include "../gui_common.hpp"

namespace micant::explorer {
namespace {

using micant_gui::PackBGRA;

enum class Icon : uint8_t { Computer, Documents, Network, RecycleBin };

uint32_t color(uint8_t red, uint8_t green, uint8_t blue) noexcept {
    return PackBGRA(red, green, blue);
}

void fill(uint32_t* pixels, uint32_t width, uint32_t height, int32_t x, int32_t y,
          int32_t rectWidth, int32_t rectHeight, uint32_t value) noexcept {
    if (!pixels || rectWidth <= 0 || rectHeight <= 0) return;
    const int32_t left = x < 0 ? 0 : x;
    const int32_t top = y < 0 ? 0 : y;
    const int32_t right = x + rectWidth > static_cast<int32_t>(width)
        ? static_cast<int32_t>(width) : x + rectWidth;
    const int32_t bottom = y + rectHeight > static_cast<int32_t>(height)
        ? static_cast<int32_t>(height) : y + rectHeight;
    for (int32_t row = top; row < bottom; ++row) {
        uint32_t* destination = pixels + static_cast<size_t>(row) * width + left;
        for (int32_t column = left; column < right; ++column) destination[column - left] = value;
    }
}

void text(uint32_t* pixels, uint32_t width, uint32_t height, int32_t x, int32_t y,
          const char* value, uint32_t foreground) noexcept {
    micant_gui::DrawStringClipped(pixels, width, height, x, y, value, foreground);
}

void drawIcon(uint32_t* pixels, uint32_t width, uint32_t height,
              int32_t x, int32_t y, Icon kind) noexcept {
    fill(pixels, width, height, x + 4, y + 5, 34, 28, color(0, 0, 0));
    if (kind == Icon::Computer) {
        fill(pixels, width, height, x + 5, y + 2, 29, 19, color(28, 93, 170));
        fill(pixels, width, height, x + 8, y + 5, 23, 13, color(117, 199, 244));
        fill(pixels, width, height, x + 1, y + 22, 37, 8, color(210, 218, 225));
        fill(pixels, width, height, x + 4, y + 24, 31, 3, color(94, 107, 121));
    } else if (kind == Icon::Documents) {
        fill(pixels, width, height, x + 7, y + 3, 25, 27, color(240, 197, 59));
        fill(pixels, width, height, x + 11, y + 8, 16, 2, color(255, 255, 240));
        fill(pixels, width, height, x + 11, y + 13, 16, 2, color(255, 255, 240));
        fill(pixels, width, height, x + 11, y + 18, 12, 2, color(255, 255, 240));
    } else if (kind == Icon::Network) {
        fill(pixels, width, height, x + 5, y + 6, 29, 23, color(232, 236, 240));
        fill(pixels, width, height, x + 8, y + 9, 23, 17, color(59, 129, 196));
        fill(pixels, width, height, x + 11, y + 12, 17, 11, color(128, 202, 240));
    } else {
        fill(pixels, width, height, x + 8, y + 5, 24, 25, color(195, 210, 221));
        fill(pixels, width, height, x + 11, y + 9, 18, 17, color(238, 243, 247));
    }
}

void drawDesktopIcon(uint32_t* pixels, uint32_t width, uint32_t height,
                     int32_t y, Icon icon, const char* label) noexcept {
    drawIcon(pixels, width, height, 22, y, icon);
    text(pixels, width, height, 16, y + 36, label, color(255, 255, 255));
}

} // namespace

void DrawDesktop(uint32_t* pixels, uint32_t width, uint32_t height,
                 const uint32_t* wallpaper) noexcept {
    if (!pixels || width == 0 || height == 0) return;
    if (wallpaper) {
        const size_t count = static_cast<size_t>(width) * height;
        for (size_t index = 0; index < count; ++index) pixels[index] = wallpaper[index];
    } else {
        // Safe fallback when the configured or packaged image cannot be read
        // or decoded. Keep the desktop deterministic and usable without a
        // partially initialized wallpaper surface.
        const uint32_t fallback = color(28, 58, 92);
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t* row = pixels + static_cast<size_t>(y) * width;
            for (uint32_t x = 0; x < width; ++x) row[x] = fallback;
        }
    }
    drawDesktopIcon(pixels, width, height, 28, Icon::Computer, "My Computer");
    drawDesktopIcon(pixels, width, height, 124, Icon::Documents, "My Documents");
    drawDesktopIcon(pixels, width, height, 220, Icon::Network, "Network Places");
    drawDesktopIcon(pixels, width, height, 316, Icon::RecycleBin, "Recycle Bin");
}

DesktopAction HitTestDesktop(int32_t x, int32_t y) noexcept {
    if (x < 0 || x >= 170) return DesktopAction::None;
    if (y >= 28 && y < 112) return DesktopAction::MyComputer;
    if (y >= 124 && y < 208) return DesktopAction::MyDocuments;
    if (y >= 220 && y < 304) return DesktopAction::Network;
    if (y >= 316 && y < 400) return DesktopAction::RecycleBin;
    return DesktopAction::None;
}

} // namespace micant::explorer
