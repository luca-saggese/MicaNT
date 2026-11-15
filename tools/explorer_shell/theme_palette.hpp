#pragma once

#include <cstdint>

namespace micant::explorer {

struct ThemePalette {
    uint32_t surface;
    uint32_t surfaceAlt;
    uint32_t foreground;
    uint32_t secondaryForeground;
    uint32_t separator;
    uint32_t accent;
    uint32_t accentForeground;
    uint32_t selected;
};

} // namespace micant::explorer
