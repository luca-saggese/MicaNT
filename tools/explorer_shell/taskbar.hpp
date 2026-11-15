#pragma once

#include <cstddef>
#include <cstdint>
#include "theme_palette.hpp"

namespace micant::explorer {

struct TaskbarButton {
    const char* title;
    uint32_t iconColor;
    bool active;
};

struct TaskbarButtonBounds {
    int32_t left{}, top{}, right{}, bottom{};
};

[[nodiscard]] bool GetTaskbarButtonBounds(uint32_t screenWidth,
    uint32_t screenHeight, uint32_t taskbarHeight, size_t buttonCount,
    size_t buttonIndex, TaskbarButtonBounds& bounds) noexcept;

// Paint the XP-style taskbar into the shell's desktop surface. The taskbar
// consumes a snapshot of top-level shell tasks and owns no window state.
void DrawTaskbar(uint32_t* pixels, uint32_t screenWidth, uint32_t screenHeight,
                 uint32_t taskbarHeight, const TaskbarButton* buttons,
                 size_t buttonCount, const char* clockText,
                 const ThemePalette& palette) noexcept;

} // namespace micant::explorer
