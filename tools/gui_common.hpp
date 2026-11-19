/**
 * @file gui_common.hpp
 @brief Luce OS Ring 3 Graphical Window Presentation Primitives for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Provides 32-bpp BGRA pixel canvas, bitmap font typography, window frame styling,
 * and direct GOP VRAM presentation via Dave Cutler NtGdiPresentSurface (0x81).
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "micant/shared/user_syscall.hpp"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/font8x8.hpp"

namespace micant_gui {

using micant::um::UserVideoInfo64;
using micant::um::UserPresentSurfaceParams64;

inline uint64_t DoSyscall(uint64_t ssn, uint64_t arg1 = 0, uint64_t arg2 = 0) noexcept {
    return micant::sys::invokeUserService(ssn, arg1, arg2);
}

constexpr size_t StrLen(const char* s) noexcept {
    size_t len = 0;
    while (s && s[len] != '\0') ++len;
    return len;
}

inline void PrintLine(const char* s) noexcept {
    DoSyscall(0x25, reinterpret_cast<uint64_t>(s), StrLen(s));
}

inline void PrintStr(const char* s) noexcept {
    DoSyscall(0x25, reinterpret_cast<uint64_t>(s), StrLen(s));
}

inline bool HasSubstr(const char* s, const char* sub) noexcept {
    if (!s || !sub) return false;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        size_t j = 0;
        while (sub[j] != '\0' && s[i + j] == sub[j]) ++j;
        if (sub[j] == '\0') return true;
    }
    return false;
}

[[nodiscard]] constexpr uint32_t PackBGRA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           (static_cast<uint32_t>(b));
}

// 8x8 font glyphs (ASCII 32 to 126)
using micant::shared::FONT_8X8;

inline void FillRect(uint32_t* buf, uint32_t bufW, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) noexcept {
    for (uint32_t r = 0; r < h; ++r) {
        uint32_t* row = buf + (y + r) * bufW + x;
        for (uint32_t c = 0; c < w; ++c) {
            row[c] = color;
        }
    }
}

inline void DrawRect(uint32_t* buf, uint32_t bufW, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thick = 1) noexcept {
    FillRect(buf, bufW, x, y, w, thick, color);
    FillRect(buf, bufW, x, y + h - thick, w, thick, color);
    FillRect(buf, bufW, x, y, thick, h, color);
    FillRect(buf, bufW, x + w - thick, y, thick, h, color);
}

inline void DrawLine(uint32_t* buf, uint32_t bufW, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) noexcept {
    int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    while (true) {
        if (x0 >= 0 && static_cast<uint32_t>(x0) < bufW && y0 >= 0) {
            buf[y0 * bufW + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

inline void DrawCircle(uint32_t* buf, uint32_t bufW, int32_t cx, int32_t cy, int32_t r, uint32_t color) noexcept {
    for (int32_t dy = -r; dy <= r; ++dy) {
        for (int32_t dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) {
                int32_t px = cx + dx;
                int32_t py = cy + dy;
                if (px >= 0 && static_cast<uint32_t>(px) < bufW && py >= 0) {
                    buf[py * bufW + px] = color;
                }
            }
        }
    }
}

inline void DrawChar(uint32_t* buf, uint32_t bufW, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg = 0) noexcept {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = FONT_8X8[c - 32];
    for (uint32_t r = 0; r < 8; ++r) {
        uint8_t bits = glyph[r];
        for (uint32_t col = 0; col < 8; ++col) {
            bool set = (bits & (0x80 >> col)) != 0;
            if (set) {
                buf[(y + r) * bufW + (x + col)] = fg;
            } else if ((bg >> 24) != 0) {
                buf[(y + r) * bufW + (x + col)] = bg;
            }
        }
    }
}

using TextRenderer = bool (*)(uint32_t*, uint32_t, uint32_t, int32_t, int32_t,
                              const char*, uint32_t) noexcept;
inline TextRenderer g_textRenderer{};

inline void SetTextRenderer(TextRenderer renderer) noexcept { g_textRenderer = renderer; }

inline void DrawStringClipped(uint32_t* buf, uint32_t bufW, uint32_t bufH,
                              int32_t x, int32_t y, const char* str, uint32_t fg) noexcept {
    if (!buf || !str) return;
    if (g_textRenderer && g_textRenderer(buf, bufW, bufH, x, y, str, fg)) return;
    if (x < 0 || y < 0) return;
    uint32_t cx = static_cast<uint32_t>(x), cy = static_cast<uint32_t>(y);
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\n') { cx = static_cast<uint32_t>(x); cy += 10; continue; }
        if (cx + 8 > bufW || cy + 8 > bufH) break;
        DrawChar(buf, bufW, cx, cy, str[i], fg);
        cx += 8;
    }
}

inline void DrawString(uint32_t* buf, uint32_t bufW, uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg = 0) noexcept {
    if (!str) return;
    uint32_t cx = x;
    uint32_t cy = y;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\n') {
            cx = x;
            cy += 10;
            continue;
        }
        DrawChar(buf, bufW, cx, cy, str[i], fg, bg);
        cx += 8;
    }
}

inline void DrawWindowFrame(uint32_t* buf, uint32_t winW, uint32_t winH, const char* title, const char* menuBar = nullptr) noexcept {
    // 1. Drop shadow & outer border
    FillRect(buf, winW, 0, 0, winW, winH, PackBGRA(18, 24, 38)); // Slate dark background
    DrawRect(buf, winW, 0, 0, winW, winH, PackBGRA(45, 65, 95), 1); // Accent border

    // 2. Title Bar (Height = 28)
    FillRect(buf, winW, 1, 1, winW - 2, 28, PackBGRA(26, 36, 54));
    FillRect(buf, winW, 1, 28, winW - 2, 1, PackBGRA(40, 56, 82));

    // Title Icon badge & Title Text
    FillRect(buf, winW, 8, 6, 16, 16, PackBGRA(0, 210, 255));
    DrawString(buf, winW, 30, 10, title, PackBGRA(240, 245, 255));

    // Minimize, Maximize, Close buttons
    uint32_t btnW = 24;
    uint32_t btnH = 18;
    uint32_t rightX = winW - 10;

    // Close [X] (Red on hover/standard)
    FillRect(buf, winW, rightX - btnW, 6, btnW, btnH, PackBGRA(196, 43, 28));
    DrawString(buf, winW, rightX - btnW + 8, 11, "X", PackBGRA(255, 255, 255));

    // Maximize [ ]
    FillRect(buf, winW, rightX - 2 * btnW - 4, 6, btnW, btnH, PackBGRA(34, 46, 68));
    DrawRect(buf, winW, rightX - 2 * btnW + 2, 10, 12, 10, PackBGRA(180, 200, 230), 1);

    // Minimize [_]
    FillRect(buf, winW, rightX - 3 * btnW - 8, 6, btnW, btnH, PackBGRA(34, 46, 68));
    FillRect(buf, winW, rightX - 3 * btnW - 2, 18, 12, 2, PackBGRA(180, 200, 230));

    // 3. Optional Menu Bar (Height = 20)
    if (menuBar && *menuBar) {
        FillRect(buf, winW, 1, 29, winW - 2, 20, PackBGRA(20, 28, 44));
        FillRect(buf, winW, 1, 48, winW - 2, 1, PackBGRA(35, 48, 70));
        DrawString(buf, winW, 10, 35, menuBar, PackBGRA(180, 200, 230));
    }
}

inline void PresentWindow(uint32_t* buf, uint32_t winW, uint32_t winH) noexcept {
    UserVideoInfo64 vInfo{};
    DoSyscall(0x80, reinterpret_cast<uint64_t>(&vInfo), 0);

    uint32_t scrW = vInfo.width > 0 ? vInfo.width : 1024;
    uint32_t scrH = vInfo.height > 0 ? vInfo.height : 768;

    UserPresentSurfaceParams64 params{};
    params.surfaceBufferVa = reinterpret_cast<uint64_t>(buf);
    params.width = winW;
    params.height = winH;
    params.pitch = winW;
    params.dstX = (scrW > winW) ? ((scrW - winW) / 2) : 0;
    params.dstY = (scrH > winH) ? ((scrH - winH) / 2) : 0;

    DoSyscall(0x81, reinterpret_cast<uint64_t>(&params), 0);
}

} // namespace micant_gui
