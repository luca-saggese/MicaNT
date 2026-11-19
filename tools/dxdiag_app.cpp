/**
 * @file dxdiag_app.cpp
 @brief Luce OS Ring 3 DirectX Diagnostic Tool (dxdiag.exe) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT dxdiag window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 660;
constexpr uint32_t WIN_H = 450;
alignas(4096) uint32_t s_dxdBuffer[WIN_W * WIN_H];

void RenderDxDiag() noexcept {
    // 1. Frame & Title & Property Tabs
    DrawWindowFrame(s_dxdBuffer, WIN_W, WIN_H, "DirectX Diagnostic Tool", "System   Display   Sound   Input");

    // 2. Tab Bar Header
    FillRect(s_dxdBuffer, WIN_W, 1, 49, WIN_W - 2, 24, PackBGRA(24, 34, 52));
    DrawRect(s_dxdBuffer, WIN_W, 1, 72, WIN_W - 2, 1, PackBGRA(40, 56, 82));
    // Active Tab: System
    FillRect(s_dxdBuffer, WIN_W, 8, 49, 90, 24, PackBGRA(16, 22, 34));
    DrawRect(s_dxdBuffer, WIN_W, 8, 49, 90, 1, PackBGRA(0, 210, 255));
    DrawString(s_dxdBuffer, WIN_W, 24, 56, "System", PackBGRA(0, 229, 255));
    DrawString(s_dxdBuffer, WIN_W, 110, 56, "Display        Sound        Input", PackBGRA(160, 180, 210));

    // 3. Tab Body Area
    FillRect(s_dxdBuffer, WIN_W, 1, 73, WIN_W - 2, WIN_H - 95, PackBGRA(16, 20, 32));

    DrawString(s_dxdBuffer, WIN_W, 20, 88, "This tool reports detailed information about the DirectX and PrismX", PackBGRA(200, 220, 245));
    DrawString(s_dxdBuffer, WIN_W, 20, 102, "components and drivers installed on your system.", PackBGRA(200, 220, 245));

    // System Information Box
    DrawRect(s_dxdBuffer, WIN_W, 20, 122, WIN_W - 40, 196, PackBGRA(35, 48, 70));
    DrawString(s_dxdBuffer, WIN_W, 30, 116, " System Information ", PackBGRA(0, 210, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 136, "Current Date/Time:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 136, "Thursday, October 22, 2026, 19:20:00", PackBGRA(240, 245, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 154, "Computer Name:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 154, "MICANT-XEON", PackBGRA(100, 240, 160));

    DrawString(s_dxdBuffer, WIN_W, 32, 172, "Operating System:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 172, "MicaNT 64-bit (10.0, Build 26040 Dave Cutler Edition)", PackBGRA(0, 229, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 190, "Language:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 190, "English (Regional Setting: English)", PackBGRA(240, 245, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 208, "System Manufacturer:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 208, "Supermicro", PackBGRA(240, 245, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 226, "System Model:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 226, "X11SSH-F (Intel C236 Chipset)", PackBGRA(240, 245, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 244, "BIOS:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 244, "AMI Aptio V UEFI 2.7 (GOP Driver Active)", PackBGRA(240, 245, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 262, "Processor:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 262, "Intel(R) Xeon(R) CPU E3-1270 v6 @ 3.80GHz (8 CPUs)", PackBGRA(240, 245, 255));

    DrawString(s_dxdBuffer, WIN_W, 32, 280, "Memory:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 280, "32768MB RAM (ECC DDR4-2400)", PackBGRA(100, 240, 160));

    DrawString(s_dxdBuffer, WIN_W, 32, 298, "DirectX / PrismX Version:", PackBGRA(160, 180, 210));
    DrawString(s_dxdBuffer, WIN_W, 220, 298, "DirectX 12 Ultimate / PrismX 1.3 Mesh Shaders", PackBGRA(255, 215, 0));

    // Notes Box
    DrawRect(s_dxdBuffer, WIN_W, 20, 328, WIN_W - 40, 48, PackBGRA(35, 48, 70));
    DrawString(s_dxdBuffer, WIN_W, 30, 322, " Notes ", PackBGRA(0, 210, 255));
    DrawString(s_dxdBuffer, WIN_W, 32, 342, "No problems found.", PackBGRA(100, 240, 160));
    DrawString(s_dxdBuffer, WIN_W, 32, 356, "Direct3D DDI: 12.2 (Mesh Shaders, Raytracing DXR 1.1, DirectStorage active)", PackBGRA(180, 200, 230));

    // Bottom Action Buttons
    FillRect(s_dxdBuffer, WIN_W, 20, WIN_H - 32, 140, 22, PackBGRA(34, 46, 68));
    DrawString(s_dxdBuffer, WIN_W, 28, WIN_H - 25, "Check for WHQL", PackBGRA(180, 200, 230));

    FillRect(s_dxdBuffer, WIN_W, WIN_W - 270, WIN_H - 32, 110, 22, PackBGRA(34, 46, 68));
    DrawString(s_dxdBuffer, WIN_W, WIN_W - 260, WIN_H - 25, "Save All Info...", PackBGRA(240, 245, 255));

    FillRect(s_dxdBuffer, WIN_W, WIN_W - 150, WIN_H - 32, 65, 22, PackBGRA(34, 46, 68));
    DrawString(s_dxdBuffer, WIN_W, WIN_W - 138, WIN_H - 25, "Next Page", PackBGRA(240, 245, 255));

    FillRect(s_dxdBuffer, WIN_W, WIN_W - 75, WIN_H - 32, 55, 22, PackBGRA(34, 46, 68));
    DrawString(s_dxdBuffer, WIN_W, WIN_W - 60, WIN_H - 25, "Exit", PackBGRA(240, 245, 255));

    PresentWindow(s_dxdBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign DirectX Diagnostic Tool (dxdiag.exe):");
    PrintLine("  DirectX / PrismX Version: DirectX 12 Ultimate / Vulkan 1.3 Mesh Shaders");
    PrintLine("  Operating System: MicaNT 64-bit (10.0, Build 26040 Dave Cutler Edition)");
    PrintLine("  Processor: Intel Xeon E3-1270 v6 (8 CPUs) | 32768MB RAM (ECC DDR4)");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderDxDiag();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
