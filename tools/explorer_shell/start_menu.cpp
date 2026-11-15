#include "start_menu.hpp"
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
          const char* value, uint32_t color) noexcept {
    micant_gui::DrawStringClipped(pixels, width, height, x, y, value, color);
}

} // namespace

void DrawStartMenu(uint32_t* pixels, uint32_t width, uint32_t height,
                   const char* accountName, const ThemePalette& palette) noexcept {
    if (!pixels || width < 250 || height < 180) return;
    fill(pixels, width, height, 0, 0, width, height, palette.surface);
    fill(pixels, width, height, 0, 0, width, 50, palette.accent);
    fill(pixels, width, height, 8, 8, 34, 34, 0xFFEBF6FA);
    fill(pixels, width, height, 12, 12, 12, 12, 0xFFE44130);
    fill(pixels, width, height, 25, 12, 12, 12, 0xFF4DA441);
    fill(pixels, width, height, 12, 25, 12, 12, 0xFF377CCE);
    fill(pixels, width, height, 25, 25, 12, 12, 0xFFF2C02A);
    text(pixels, width, height, 53, 19, accountName ? accountName : "MicaNT User", palette.accentForeground);

    const int32_t divider = static_cast<int32_t>(width) * 3 / 5;
    const int32_t footerHeight = 42;
    const int32_t bodyHeight = static_cast<int32_t>(height) - 50 - footerHeight;
    fill(pixels, width, height, 0, 50, divider, bodyHeight, palette.surface);
    fill(pixels, width, height, divider, 50, static_cast<int32_t>(width) - divider,
         bodyHeight, palette.surfaceAlt);

    static constexpr const char* left[] = {
        "Internet", "E-mail", "----------------", "My Documents", "My Pictures",
        "My Music", "My Computer", "Control Panel", "Connect To", "Printers",
        "Help and Support", "Search", "Run..."
    };
    for (size_t i = 0; i < sizeof(left) / sizeof(left[0]); ++i) {
        const int32_t y = 63 + static_cast<int32_t>(i) * 23;
        if (left[i][0] == '-') fill(pixels, width, height, 12, y + 8, divider - 24, 1, palette.separator);
        else text(pixels, width, height, 18, y, left[i], palette.foreground);
    }
    static constexpr const char* right[] = {
        "Recent Documents", "My Pictures", "My Music", "My Computer", "Control Panel",
        "Theme Appearance", "Connect To", "Printers and Faxes", "Help and Support",
        "Search", "Run..."
    };
    for (size_t i = 0; i < sizeof(right) / sizeof(right[0]); ++i)
        text(pixels, width, height, divider + 11, 69 + static_cast<int32_t>(i) * 25,
             right[i], palette.secondaryForeground);

    fill(pixels, width, height, 0, static_cast<int32_t>(height) - footerHeight,
         width, footerHeight, palette.accent);
    text(pixels, width, height, 18, static_cast<int32_t>(height) - 25, "Log Off", palette.accentForeground);
    text(pixels, width, height, divider + 9, static_cast<int32_t>(height) - 25,
         "Turn Off Computer", palette.accentForeground);
}

StartMenuAction HitTestStartMenu(int32_t x, int32_t y,
                                 uint32_t width, uint32_t height) noexcept {
    if (x < 0 || y < 0 || x >= static_cast<int32_t>(width) ||
        y >= static_cast<int32_t>(height)) return StartMenuAction::Dismiss;
    if (y >= static_cast<int32_t>(height) - 42) {
        return x < static_cast<int32_t>(width) * 3 / 5
            ? StartMenuAction::LogOff : StartMenuAction::PowerOff;
    }
    if (y < 50) return StartMenuAction::Dismiss;

    const int32_t divider = static_cast<int32_t>(width) * 3 / 5;
    if (x < divider) {
        const int32_t row = (y - 63) / 23;
        switch (row) {
            case 3: case 4: case 5: return StartMenuAction::MyDocuments;
            case 6: return StartMenuAction::MyComputer;
            case 7: return StartMenuAction::ControlPanel;
            case 8: return StartMenuAction::Network;
            case 11: return StartMenuAction::Search;
            case 12: return StartMenuAction::Run;
            default: return StartMenuAction::Dismiss;
        }
    }

    const int32_t row = (y - 69) / 25;
    switch (row) {
        case 0: return StartMenuAction::MyDocuments;
        case 1: case 2: return StartMenuAction::MyDocuments;
        case 3: return StartMenuAction::MyComputer;
        case 4: return StartMenuAction::ControlPanel;
        case 5: return StartMenuAction::ThemeSettings;
        case 6: return StartMenuAction::Network;
        case 9: return StartMenuAction::Search;
        case 10: return StartMenuAction::Run;
        default: return StartMenuAction::Dismiss;
    }
}

} // namespace micant::explorer
