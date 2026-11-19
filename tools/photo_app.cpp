/**
 * @file photo_app.cpp
 @brief Luce OS Ring 3 Photo & Image Viewer Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Photos / Windows Photo Viewer window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes image telemetry to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 560;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_photoBuffer[WIN_W * WIN_H];

void RenderPhotoViewerWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_photoBuffer, WIN_W, WIN_H, "Photos - wallpaper_default.bmp", "File  View  Tools  Help");

    // 2. Toolbar Row (Y=52 to 78)
    FillRect(s_photoBuffer, WIN_W, 1, 50, WIN_W - 2, 28, PackBGRA(20, 28, 44));
    FillRect(s_photoBuffer, WIN_W, 1, 78, WIN_W - 2, 1, PackBGRA(35, 48, 70));

    const char* tools[7] = { "[<<]", "[>>]", "[+]", "[-]", "[1:1]", "[Fit]", "[Rotate]" };
    uint32_t tX = 16;
    for (int i = 0; i < 7; ++i) {
        DrawString(s_photoBuffer, WIN_W, tX, 58, tools[i], PackBGRA(200, 220, 245));
        tX += 60;
    }

    // 3. Central Image Viewport (Checkerboard backdrop + Image test pattern)
    const uint32_t vpX = 16;
    const uint32_t vpY = 86;
    const uint32_t vpW = WIN_W - 32;
    const uint32_t vpH = WIN_H - 120;

    // Viewport background
    FillRect(s_photoBuffer, WIN_W, vpX, vpY, vpW, vpH, PackBGRA(12, 16, 24));
    DrawRect(s_photoBuffer, WIN_W, vpX, vpY, vpW, vpH, PackBGRA(40, 55, 80), 1);

    // Inner Image Box: 380 x 210
    const uint32_t imgW = 380;
    const uint32_t imgH = 200;
    const uint32_t imgX = vpX + (vpW - imgW) / 2;
    const uint32_t imgY = vpY + (vpH - imgH) / 2;

    // Gradient background representing loaded wallpaper image
    for (uint32_t y = 0; y < imgH; ++y) {
        uint8_t r = static_cast<uint8_t>(20 + (y * 50) / imgH);
        uint8_t g = static_cast<uint8_t>(40 + (y * 80) / imgH);
        uint8_t b = static_cast<uint8_t>(100 + (y * 140) / imgH);
        FillRect(s_photoBuffer, WIN_W, imgX, imgY + y, imgW, 1, PackBGRA(r, g, b));
    }
    DrawRect(s_photoBuffer, WIN_W, imgX, imgY, imgW, imgH, PackBGRA(200, 220, 255), 1);

    // Sun / Moon disk
    DrawCircle(s_photoBuffer, WIN_W, imgX + 80, imgY + 60, 24, PackBGRA(255, 230, 120));

    // Mountain silhouettes
    DrawLine(s_photoBuffer, WIN_W, imgX, imgY + 160, imgX + 120, imgY + 90, PackBGRA(30, 45, 75));
    DrawLine(s_photoBuffer, WIN_W, imgX + 120, imgY + 90, imgX + 240, imgY + 170, PackBGRA(30, 45, 75));
    DrawLine(s_photoBuffer, WIN_W, imgX + 200, imgY + 170, imgX + 300, imgY + 80, PackBGRA(20, 35, 60));
    DrawLine(s_photoBuffer, WIN_W, imgX + 300, imgY + 80, imgX + 380, imgY + 150, PackBGRA(20, 35, 60));

    DrawString(s_photoBuffer, WIN_W, imgX + 100, imgY + imgH - 24, "MicaNT Sovereign Wallpaper (1920x1080 BMP)", PackBGRA(255, 255, 255));

    // 4. Status Bar at bottom
    FillRect(s_photoBuffer, WIN_W, 1, WIN_H - 26, WIN_W - 2, 25, PackBGRA(15, 20, 30));
    FillRect(s_photoBuffer, WIN_W, 1, WIN_H - 26, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_photoBuffer, WIN_W, 16, WIN_H - 18, "1920 x 1080 x 32bpp | 8,294,400 bytes | Zoom: 25% (Fit)", PackBGRA(160, 180, 210));
    DrawString(s_photoBuffer, WIN_W, WIN_W - 120, WIN_H - 18, "1 of 12 images", PackBGRA(160, 180, 210));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Sovereign Photo & Image Viewer [Version 10.0.26040]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Image Subsystem        : GDI / DirectFlip 32-bpp BGRA Blitter\n"
        "Supported Formats      : Windows Bitmap (.bmp), DIB, PNG, Raw Framebuffer\n"
        "Active Image           : C:\\MicaNT\\Web\\Wallpaper\\wallpaper_default.bmp\n"
        "Resolution             : 1920 x 1080 (32 bits per pixel)\n"
        "Memory Footprint       : 8,294,400 bytes VRAM surface buffer\n"
        "Status: Presenting photo.exe window (560x420 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderPhotoViewerWindow();
    PresentWindow(s_photoBuffer, WIN_W, WIN_H);

    PrintStr("[Photos] Image surface rendered and blitted to GOP. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
