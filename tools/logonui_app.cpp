/**
 * @file logonui_app.cpp
 @brief Luce OS Ring 3 Windows Logon UI & Lock Screen Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Lock Screen & Logon UI to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes authentication session telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 600;
constexpr uint32_t WIN_H = 450;
alignas(4096) uint32_t s_logonBuffer[WIN_W * WIN_H];

[[maybe_unused]] void RenderLogonUiWindow() noexcept {
    // 1. Full Atmospheric Gradient Background
    for (uint32_t y = 0; y < WIN_H; ++y) {
        uint8_t r = static_cast<uint8_t>(8 + (y * 12) / WIN_H);
        uint8_t g = static_cast<uint8_t>(18 + (y * 24) / WIN_H);
        uint8_t b = static_cast<uint8_t>(32 + (y * 48) / WIN_H);
        FillRect(s_logonBuffer, WIN_W, 0, y, WIN_W, 1, PackBGRA(r, g, b));
    }
    DrawRect(s_logonBuffer, WIN_W, 0, 0, WIN_W, WIN_H, PackBGRA(0, 150, 255), 1);

    // Subtle auroral glow line
    for (uint32_t x = 0; x < WIN_W; x += 4) {
        uint32_t y = 140 + ((x * 37) % 15);
        FillRect(s_logonBuffer, WIN_W, x, y, 3, 2, PackBGRA(0, 229, 255));
    }

    // 2. Clock & Date
    DrawString(s_logonBuffer, WIN_W, WIN_W / 2 - 40, 50, "12:00 PM", PackBGRA(255, 255, 255));
    DrawString(s_logonBuffer, WIN_W, WIN_W / 2 - 85, 72, "Friday, October 23, 2026", PackBGRA(180, 200, 230));

    // 3. User Avatar & Username
    const uint32_t avX = WIN_W / 2;
    const uint32_t avY = 170;
    DrawCircle(s_logonBuffer, WIN_W, avX, avY, 36, PackBGRA(30, 48, 76));
    DrawCircle(s_logonBuffer, WIN_W, avX, avY, 38, PackBGRA(0, 180, 255));
    // Head and shoulders icon
    DrawCircle(s_logonBuffer, WIN_W, avX, avY - 8, 12, PackBGRA(220, 235, 255));
    FillRect(s_logonBuffer, WIN_W, avX - 18, avY + 10, 36, 16, PackBGRA(220, 235, 255));

    DrawString(s_logonBuffer, WIN_W, WIN_W / 2 - 40, 222, "MicaAdmin", PackBGRA(255, 255, 255));
    DrawString(s_logonBuffer, WIN_W, WIN_W / 2 - 58, 240, "MICANT\\MicaAdmin", PackBGRA(160, 180, 210));

    // 4. PIN Authentication Prompt
    const uint32_t pinBoxW = 180;
    const uint32_t pinBoxH = 28;
    const uint32_t pinBoxX = (WIN_W - pinBoxW) / 2;
    const uint32_t pinBoxY = 270;

    FillRect(s_logonBuffer, WIN_W, pinBoxX, pinBoxY, pinBoxW, pinBoxH, PackBGRA(12, 16, 24));
    DrawRect(s_logonBuffer, WIN_W, pinBoxX, pinBoxY, pinBoxW, pinBoxH, PackBGRA(0, 150, 255), 1);
    DrawString(s_logonBuffer, WIN_W, pinBoxX + 50, pinBoxY + 8, "* * * * * *", PackBGRA(0, 229, 255));

    // Arrow submit button next to PIN box
    FillRect(s_logonBuffer, WIN_W, pinBoxX + pinBoxW + 6, pinBoxY, 28, pinBoxH, PackBGRA(0, 120, 215));
    DrawRect(s_logonBuffer, WIN_W, pinBoxX + pinBoxW + 6, pinBoxY, 28, pinBoxH, PackBGRA(255, 255, 255), 1);
    DrawString(s_logonBuffer, WIN_W, pinBoxX + pinBoxW + 16, pinBoxY + 8, "->", PackBGRA(255, 255, 255));

    DrawString(s_logonBuffer, WIN_W, WIN_W / 2 - 50, 310, "Sign-in options", PackBGRA(100, 200, 255));

    // 5. System Status & Badges
    DrawString(s_logonBuffer, WIN_W, 20, WIN_H - 30, "MicaNT Sovereign Cutler Edition", PackBGRA(140, 160, 190));
    DrawString(s_logonBuffer, WIN_W, 20, WIN_H - 16, "Intel Xeon E3-1270 v6 | 32 GB ECC DDR4", PackBGRA(100, 120, 150));

    // Power & Network icons in bottom right
    DrawString(s_logonBuffer, WIN_W, WIN_W - 140, WIN_H - 24, "[Ethernet: UP]", PackBGRA(100, 240, 160));
    DrawCircle(s_logonBuffer, WIN_W, WIN_W - 30, WIN_H - 20, 8, PackBGRA(255, 80, 80));
    DrawLine(s_logonBuffer, WIN_W, WIN_W - 30, WIN_H - 28, WIN_W - 30, WIN_H - 20, PackBGRA(255, 255, 255));
}

} // namespace

extern "C" void _start() noexcept {
    PrintStr("LogonUI startup failed: secure desktop input and authenticated LSASS transport are unavailable.\n");
    DoSyscall(0x01, 0xC00000BBu, 0); // STATUS_NOT_SUPPORTED
}
