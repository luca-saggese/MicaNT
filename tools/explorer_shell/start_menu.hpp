#pragma once

#include <cstdint>
#include "theme_palette.hpp"

namespace micant::explorer {

enum class StartMenuAction : uint8_t {
    Dismiss,
    MyComputer,
    MyDocuments,
    ControlPanel,
    Network,
    RecycleBin,
    Search,
    Run,
    ThemeSettings,
    LogOff,
    PowerOff,
};

void DrawStartMenu(uint32_t* pixels, uint32_t width, uint32_t height,
                   const char* accountName, const ThemePalette& palette) noexcept;
StartMenuAction HitTestStartMenu(int32_t x, int32_t y,
                                 uint32_t width, uint32_t height) noexcept;

} // namespace micant::explorer
