#pragma once

#include <cstdint>

namespace micant::explorer {

enum class DesktopAction : uint8_t { None, MyComputer, MyDocuments, Network, RecycleBin };

struct DesktopIconImage {
    const uint32_t* pixels{};
    uint32_t width{};
    uint32_t height{};
    uint32_t stride{};
};

struct DesktopIconSet {
    DesktopIconImage computer{};
    DesktopIconImage documents{};
    DesktopIconImage network{};
    DesktopIconImage recycleBin{};
};

void DrawDesktop(uint32_t* pixels, uint32_t width, uint32_t height,
                 const uint32_t* wallpaper = nullptr,
                 const DesktopIconSet* icons = nullptr) noexcept;
DesktopAction HitTestDesktop(int32_t x, int32_t y) noexcept;

} // namespace micant::explorer
