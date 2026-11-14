#pragma once

#include <cstdint>

namespace micant::explorer {

enum class DesktopAction : uint8_t { None, MyComputer, MyDocuments, Network, RecycleBin };

void DrawDesktop(uint32_t* pixels, uint32_t width, uint32_t height,
                 const uint32_t* wallpaper = nullptr) noexcept;
DesktopAction HitTestDesktop(int32_t x, int32_t y) noexcept;

} // namespace micant::explorer
