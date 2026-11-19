/**
 * @file devmgmt_app.cpp
 * Luce OS Ring 3 Device Manager Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Device Manager window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_devBuffer[WIN_W * WIN_H];

void RenderDeviceManagerWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_devBuffer, WIN_W, WIN_H, "Device Manager", "File   Action   View   Help");

    // 2. Toolbar
    FillRect(s_devBuffer, WIN_W, 1, 49, WIN_W - 2, 22, PackBGRA(22, 30, 46));
    DrawRect(s_devBuffer, WIN_W, 1, 70, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_devBuffer, WIN_W, 10, 56, "[<-] [->]  [Refresh]  [Properties]  [Scan for hardware changes]", PackBGRA(200, 220, 245));

    // 3. Tree Area Background
    FillRect(s_devBuffer, WIN_W, 1, 71, WIN_W - 2, WIN_H - 93, PackBGRA(16, 20, 32));

    // Device Tree items
    DrawString(s_devBuffer, WIN_W, 10, 80, "v MICANT-XEON (Supermicro X11SSH-F)", PackBGRA(0, 210, 255));
    DrawString(s_devBuffer, WIN_W, 22, 98, "> Audio inputs and outputs", PackBGRA(180, 200, 230));
    DrawString(s_devBuffer, WIN_W, 22, 116, "v Computer", PackBGRA(180, 200, 230));
    DrawString(s_devBuffer, WIN_W, 36, 134, "ACPI x64-based PC (Supermicro X11SSH-F)", PackBGRA(240, 245, 255));
    DrawString(s_devBuffer, WIN_W, 22, 152, "v Display adapters", PackBGRA(180, 200, 230));
    DrawString(s_devBuffer, WIN_W, 36, 170, "UEFI GOP Linear Framebuffer (1920x1080 32-bpp BGRA)", PackBGRA(100, 240, 160));
    DrawString(s_devBuffer, WIN_W, 22, 188, "v Network adapters", PackBGRA(180, 200, 230));
    DrawString(s_devBuffer, WIN_W, 36, 206, "Intel(R) I210 Gigabit Network Connection (8086:1533)", PackBGRA(100, 240, 160));
    DrawString(s_devBuffer, WIN_W, 22, 224, "v Processors", PackBGRA(180, 200, 230));
    DrawString(s_devBuffer, WIN_W, 36, 242, "Intel(R) Xeon(R) CPU E3-1270 v6 @ 3.80GHz (8 Threads)", PackBGRA(100, 240, 160));
    DrawString(s_devBuffer, WIN_W, 22, 260, "v Storage controllers", PackBGRA(180, 200, 230));
    DrawString(s_devBuffer, WIN_W, 36, 278, "Intel(R) 200 Series Chipset Family SATA AHCI Controller", PackBGRA(100, 240, 160));

    // 4. Status Bar
    FillRect(s_devBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_devBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_devBuffer, WIN_W, 12, WIN_H - 15, "Status: All hardware drivers loaded and operational.", PackBGRA(100, 240, 160));
    DrawString(s_devBuffer, WIN_W, 460, WIN_H - 15, "14 Devices OK", PackBGRA(0, 210, 255));

    PresentWindow(s_devBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Device Manager (devmgmt.msc):");
    PrintLine("  System: MICANT-XEON (Supermicro X11SSH-F)");
    PrintLine("  Hardware: Intel Xeon E3-1270 v6 | Intel I210 | Intel AHCI | Intel xHCI");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderDeviceManagerWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
