/**
 * @file sysdm_app.cpp
 @brief Luce OS Ring 3 System Properties Applet (sysdm.cpl) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT System Properties window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 440;
alignas(4096) uint32_t s_sysdmBuffer[WIN_W * WIN_H];

void RenderSystemProperties() noexcept {
    // 1. Frame & Title & Property Tabs
    DrawWindowFrame(s_sysdmBuffer, WIN_W, WIN_H, "System Properties", "Computer Name   Hardware   Advanced   Protection   Remote");

    // 2. Tab Bar Header
    FillRect(s_sysdmBuffer, WIN_W, 1, 49, WIN_W - 2, 24, PackBGRA(24, 34, 52));
    DrawRect(s_sysdmBuffer, WIN_W, 1, 72, WIN_W - 2, 1, PackBGRA(40, 56, 82));
    // Active Tab: Computer Name
    FillRect(s_sysdmBuffer, WIN_W, 8, 49, 130, 24, PackBGRA(16, 22, 34));
    DrawRect(s_sysdmBuffer, WIN_W, 8, 49, 130, 1, PackBGRA(0, 210, 255));
    DrawString(s_sysdmBuffer, WIN_W, 16, 56, "Computer Name", PackBGRA(0, 229, 255));
    DrawString(s_sysdmBuffer, WIN_W, 150, 56, "Hardware    Advanced    System Protection", PackBGRA(160, 180, 210));

    // 3. Tab Body Area
    FillRect(s_sysdmBuffer, WIN_W, 1, 73, WIN_W - 2, WIN_H - 95, PackBGRA(16, 20, 32));

    DrawString(s_sysdmBuffer, WIN_W, 20, 88, "MicaNT uses the following information to identify your computer", PackBGRA(200, 220, 245));
    DrawString(s_sysdmBuffer, WIN_W, 20, 102, "on the network and domain environment.", PackBGRA(200, 220, 245));

    // Computer Description Group
    DrawRect(s_sysdmBuffer, WIN_W, 20, 122, WIN_W - 40, 110, PackBGRA(35, 48, 70));
    DrawString(s_sysdmBuffer, WIN_W, 30, 116, " Computer Identity ", PackBGRA(0, 210, 255));

    DrawString(s_sysdmBuffer, WIN_W, 36, 138, "Computer description:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 138, "Sovereign Workstation (Xeon E3-1270 v6)", PackBGRA(240, 245, 255));

    DrawString(s_sysdmBuffer, WIN_W, 36, 160, "Full computer name:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 160, "MICANT-XEON", PackBGRA(100, 240, 160));

    DrawString(s_sysdmBuffer, WIN_W, 36, 182, "DNS Hostname:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 182, "micant-baremetal.ash-forge.internal", PackBGRA(240, 245, 255));

    DrawString(s_sysdmBuffer, WIN_W, 36, 204, "Workgroup:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 204, "WORKGROUP", PackBGRA(240, 245, 255));

    // Performance & Hardware Specification Group
    DrawRect(s_sysdmBuffer, WIN_W, 20, 244, WIN_W - 40, 116, PackBGRA(35, 48, 70));
    DrawString(s_sysdmBuffer, WIN_W, 30, 238, " System Specification ", PackBGRA(0, 210, 255));

    DrawString(s_sysdmBuffer, WIN_W, 36, 260, "Operating System:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 260, "MicaNT Sovereign Workstation 2026", PackBGRA(0, 229, 255));

    DrawString(s_sysdmBuffer, WIN_W, 36, 280, "Processor:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 280, "Intel(R) Xeon(R) CPU E3-1270 v6 @ 3.80GHz", PackBGRA(240, 245, 255));

    DrawString(s_sysdmBuffer, WIN_W, 36, 300, "Installed RAM:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 300, "32.0 GB (ECC DDR4-2400 Single-Bit Correction)", PackBGRA(100, 240, 160));

    DrawString(s_sysdmBuffer, WIN_W, 36, 320, "System type:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 320, "64-bit operating system, x64-based processor", PackBGRA(240, 245, 255));

    DrawString(s_sysdmBuffer, WIN_W, 36, 340, "Paging Engine:", PackBGRA(160, 180, 210));
    DrawString(s_sysdmBuffer, WIN_W, 210, 340, "Haven Memory Bus (HMB 2.0) Active", PackBGRA(255, 200, 80));

    // Bottom Action Buttons
    FillRect(s_sysdmBuffer, WIN_W, WIN_W - 270, WIN_H - 32, 75, 22, PackBGRA(34, 46, 68));
    DrawString(s_sysdmBuffer, WIN_W, WIN_W - 250, WIN_H - 25, "OK", PackBGRA(240, 245, 255));

    FillRect(s_sysdmBuffer, WIN_W, WIN_W - 185, WIN_H - 32, 75, 22, PackBGRA(34, 46, 68));
    DrawString(s_sysdmBuffer, WIN_W, WIN_W - 173, WIN_H - 25, "Cancel", PackBGRA(240, 245, 255));

    FillRect(s_sysdmBuffer, WIN_W, WIN_W - 100, WIN_H - 32, 75, 22, PackBGRA(28, 38, 56));
    DrawString(s_sysdmBuffer, WIN_W, WIN_W - 88, WIN_H - 25, "Apply", PackBGRA(120, 140, 170));

    PresentWindow(s_sysdmBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign System Properties (sysdm.cpl):");
    PrintLine("  Computer: MICANT-XEON (Workgroup: WORKGROUP)");
    PrintLine("  Silicon: Intel Xeon E3-1270 v6 @ 3.80GHz | 32GB ECC DDR4");
    PrintLine("  OS: MicaNT Sovereign Workstation 2026 (64-bit)");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderSystemProperties();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
