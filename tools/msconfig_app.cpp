/**
 * @file msconfig_app.cpp
 @brief Luce OS Ring 3 System Configuration Utility (msconfig.exe) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT msconfig window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 620;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_mscBuffer[WIN_W * WIN_H];

void RenderMsConfig() noexcept {
    // 1. Frame & Title & Property Tabs
    DrawWindowFrame(s_mscBuffer, WIN_W, WIN_H, "System Configuration", "General   Boot   Services   Startup   Tools");

    // 2. Tab Bar Header
    FillRect(s_mscBuffer, WIN_W, 1, 49, WIN_W - 2, 24, PackBGRA(24, 34, 52));
    DrawRect(s_mscBuffer, WIN_W, 1, 72, WIN_W - 2, 1, PackBGRA(40, 56, 82));
    // Active Tab: General
    FillRect(s_mscBuffer, WIN_W, 8, 49, 90, 24, PackBGRA(16, 22, 34));
    DrawRect(s_mscBuffer, WIN_W, 8, 49, 90, 1, PackBGRA(0, 210, 255));
    DrawString(s_mscBuffer, WIN_W, 24, 56, "General", PackBGRA(0, 229, 255));
    DrawString(s_mscBuffer, WIN_W, 110, 56, "Boot        Services        Startup        Tools", PackBGRA(160, 180, 210));

    // 3. Tab Body Area
    FillRect(s_mscBuffer, WIN_W, 1, 73, WIN_W - 2, WIN_H - 95, PackBGRA(16, 20, 32));

    // Startup Selection Box
    DrawRect(s_mscBuffer, WIN_W, 20, 88, WIN_W - 40, 140, PackBGRA(35, 48, 70));
    DrawString(s_mscBuffer, WIN_W, 30, 82, " Startup selection ", PackBGRA(0, 210, 255));

    // Option 1: Normal startup (Radio selected)
    DrawCircle(s_mscBuffer, WIN_W, 36, 110, 6, PackBGRA(0, 210, 255));
    DrawCircle(s_mscBuffer, WIN_W, 36, 110, 3, PackBGRA(255, 255, 255));
    DrawString(s_mscBuffer, WIN_W, 52, 106, "Normal startup", PackBGRA(240, 245, 255));
    DrawString(s_mscBuffer, WIN_W, 68, 122, "Load all device drivers, kernel modules, and sovereign subsystem services", PackBGRA(160, 180, 210));

    // Option 2: Diagnostic startup
    DrawCircle(s_mscBuffer, WIN_W, 36, 146, 6, PackBGRA(40, 54, 78));
    DrawString(s_mscBuffer, WIN_W, 52, 142, "Diagnostic startup", PackBGRA(180, 200, 230));
    DrawString(s_mscBuffer, WIN_W, 68, 158, "Load basic devices and services only (Safe Mode minimal)", PackBGRA(120, 140, 170));

    // Option 3: Selective startup
    DrawCircle(s_mscBuffer, WIN_W, 36, 182, 6, PackBGRA(40, 54, 78));
    DrawString(s_mscBuffer, WIN_W, 52, 178, "Selective startup", PackBGRA(180, 200, 230));
    DrawString(s_mscBuffer, WIN_W, 68, 194, "[X] Load system services   [X] Load startup items   [X] Use original boot configuration", PackBGRA(120, 140, 170));

    // Boot & Environment Summary Box
    DrawRect(s_mscBuffer, WIN_W, 20, 240, WIN_W - 40, 104, PackBGRA(35, 48, 70));
    DrawString(s_mscBuffer, WIN_W, 30, 234, " Boot Configuration & NVRAM ", PackBGRA(0, 210, 255));

    DrawString(s_mscBuffer, WIN_W, 32, 254, "Default OS Entry:", PackBGRA(160, 180, 210));
    DrawString(s_mscBuffer, WIN_W, 190, 254, "MicaNT Cutler Edition (\\EFI\\MicaNT\\BOOTX64.EFI)", PackBGRA(100, 240, 160));

    DrawString(s_mscBuffer, WIN_W, 32, 272, "NVRAM BootCurrent:", PackBGRA(160, 180, 210));
    DrawString(s_mscBuffer, WIN_W, 190, 272, "Boot000B (MicaNT Cutler Edition on sdb2)", PackBGRA(240, 245, 255));

    DrawString(s_mscBuffer, WIN_W, 32, 290, "Kernel SystemRoot:", PackBGRA(160, 180, 210));
    DrawString(s_mscBuffer, WIN_W, 190, 290, "C:\\MicaNT\\System32\\ntoskrnl.exe", PackBGRA(240, 245, 255));

    DrawString(s_mscBuffer, WIN_W, 32, 308, "Boot Timeout:", PackBGRA(160, 180, 210));
    DrawString(s_mscBuffer, WIN_W, 190, 308, "5 seconds (boot.ini & UEFI NVRAM synchronized)", PackBGRA(255, 215, 0));

    // Bottom Action Buttons
    FillRect(s_mscBuffer, WIN_W, WIN_W - 270, WIN_H - 32, 75, 22, PackBGRA(34, 46, 68));
    DrawString(s_mscBuffer, WIN_W, WIN_W - 250, WIN_H - 25, "OK", PackBGRA(240, 245, 255));

    FillRect(s_mscBuffer, WIN_W, WIN_W - 185, WIN_H - 32, 75, 22, PackBGRA(34, 46, 68));
    DrawString(s_mscBuffer, WIN_W, WIN_W - 173, WIN_H - 25, "Cancel", PackBGRA(240, 245, 255));

    FillRect(s_mscBuffer, WIN_W, WIN_W - 100, WIN_H - 32, 75, 22, PackBGRA(28, 38, 56));
    DrawString(s_mscBuffer, WIN_W, WIN_W - 88, WIN_H - 25, "Apply", PackBGRA(120, 140, 170));

    PresentWindow(s_mscBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign System Configuration Utility (msconfig.exe):");
    PrintLine("  Startup: Normal startup (All drivers & services enabled)");
    PrintLine("  Default OS: MicaNT Cutler Edition (Boot000B on sdb2)");
    PrintLine("  Kernel: C:\\MicaNT\\System32\\ntoskrnl.exe | Timeout: 5s");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderMsConfig();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
