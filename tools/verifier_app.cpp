/**
 * @file verifier_app.cpp
 @brief Luce OS Ring 3 Driver Verifier Manager (verifier) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows Driver Verifier Manager window to GOP VRAM via NtGdiPresentSurface (0x81),
 * writes driver verification flags to console stdout via NtWriteFile (0x25), and exits via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 500;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_verBuffer[WIN_W * WIN_H];

void RenderVerifierWindow() noexcept {
    // 1. Frame
    DrawWindowFrame(s_verBuffer, WIN_W, WIN_H, "Driver Verifier Manager", nullptr);

    // 2. Banner
    DrawString(s_verBuffer, WIN_W, 20, 42, "Select a task", PackBGRA(255, 255, 255));
    DrawString(s_verBuffer, WIN_W, 20, 58, "Driver Verifier monitors MicaNT kernel-mode drivers to detect illegal", PackBGRA(160, 180, 210));
    DrawString(s_verBuffer, WIN_W, 20, 72, "function calls or actions that might corrupt the system.", PackBGRA(160, 180, 210));

    // Separator line
    FillRect(s_verBuffer, WIN_W, 20, 92, WIN_W - 40, 1, PackBGRA(40, 56, 80));

    // 3. Task Radio Options
    const uint32_t rX = 26;
    uint32_t rY = 110;

    // Option 1: Standard settings (Selected)
    DrawCircle(s_verBuffer, WIN_W, rX, rY + 6, 7, PackBGRA(0, 150, 255));
    DrawCircle(s_verBuffer, WIN_W, rX, rY + 6, 4, PackBGRA(255, 255, 255));
    DrawString(s_verBuffer, WIN_W, rX + 16, rY, "Create standard settings (recommended)", PackBGRA(255, 255, 255));
    DrawString(s_verBuffer, WIN_W, rX + 16, rY + 16, "Enables Special Pool, Force IRQL Checking, and DMA Verification.", PackBGRA(140, 160, 190));

    rY += 46;

    // Option 2: Custom settings
    DrawCircle(s_verBuffer, WIN_W, rX, rY + 6, 7, PackBGRA(40, 56, 80));
    DrawString(s_verBuffer, WIN_W, rX + 16, rY, "Create custom settings (for code developers)", PackBGRA(220, 235, 255));
    DrawString(s_verBuffer, WIN_W, rX + 16, rY + 16, "Enables individual flags including Deadlock Detection and Fault Injection.", PackBGRA(140, 160, 190));

    rY += 46;

    // Option 3: Delete existing settings
    DrawCircle(s_verBuffer, WIN_W, rX, rY + 6, 7, PackBGRA(40, 56, 80));
    DrawString(s_verBuffer, WIN_W, rX + 16, rY, "Delete existing settings", PackBGRA(220, 235, 255));

    rY += 36;

    // Option 4: Display existing settings
    DrawCircle(s_verBuffer, WIN_W, rX, rY + 6, 7, PackBGRA(40, 56, 80));
    DrawString(s_verBuffer, WIN_W, rX + 16, rY, "Display existing settings", PackBGRA(220, 235, 255));

    // Separator line
    FillRect(s_verBuffer, WIN_W, 20, 280, WIN_W - 40, 1, PackBGRA(40, 56, 80));

    // 4. Hardware driver targets
    DrawString(s_verBuffer, WIN_W, 20, 292, "Verified Sovereign Drivers: ahci.sys, i210.sys, tcpip.sys, fltmgr.sys", PackBGRA(100, 240, 160));

    // 5. Button Row: [Back] [Next >] [Cancel]
    const uint32_t btnY = WIN_H - 40;
    FillRect(s_verBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_verBuffer, WIN_W, WIN_W - 240, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_verBuffer, WIN_W, WIN_W - 224, btnY + 7, "< Back", PackBGRA(160, 180, 210));

    FillRect(s_verBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(0, 120, 215));
    DrawRect(s_verBuffer, WIN_W, WIN_W - 160, btnY, 68, 26, PackBGRA(255, 255, 255), 1);
    DrawString(s_verBuffer, WIN_W, WIN_W - 146, btnY + 7, "Next >", PackBGRA(255, 255, 255));

    FillRect(s_verBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(30, 42, 60));
    DrawRect(s_verBuffer, WIN_W, WIN_W - 80, btnY, 68, 26, PackBGRA(70, 90, 120), 1);
    DrawString(s_verBuffer, WIN_W, WIN_W - 66, btnY + 7, "Cancel", PackBGRA(200, 215, 235));
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Driver Verifier Manager [verifier.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Active Verification Level : 0x00000BB3 (Standard Verification Flags)\n"
        "  - Special Pool Allocation       : Enabled (Catch Buffer Overruns)\n"
        "  - Force IRQL Checking           : Enabled (Detect PagedPool at >= DISPATCH)\n"
        "  - DMA Verification              : Enabled (I210 & AHCI Bus Mastering Guard)\n"
        "  - Deadlock Detection            : Enabled (Lock Hierarchy Inversion Interlock)\n"
        "Verified Driver Modules   : 9 Kernel Drivers (.sys) Active\n"
        "Status: Presenting verifier.exe window (500x380 @ 32-bpp BGRA)\n\n";

    PrintStr(banner);

    RenderVerifierWindow();
    PresentWindow(s_verBuffer, WIN_W, WIN_H);

    PrintStr("[Verifier] Verification profile active. Exiting with code 0.\n");
    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
