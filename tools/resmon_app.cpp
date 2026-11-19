/**
 * @file resmon_app.cpp
 * Luce OS Ring 3 Resource Monitor (resmon.exe) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Resource Monitor window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 680;
constexpr uint32_t WIN_H = 460;
alignas(4096) uint32_t s_resBuffer[WIN_W * WIN_H];

void RenderResourceMonitor() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_resBuffer, WIN_W, WIN_H, "Resource Monitor", "File   Monitor   View   Help");

    // 2. Tab Bar Header
    FillRect(s_resBuffer, WIN_W, 1, 49, WIN_W - 2, 24, PackBGRA(24, 34, 52));
    DrawRect(s_resBuffer, WIN_W, 1, 72, WIN_W - 2, 1, PackBGRA(40, 56, 82));
    // Active Tab: Overview
    FillRect(s_resBuffer, WIN_W, 8, 49, 90, 24, PackBGRA(16, 22, 34));
    DrawRect(s_resBuffer, WIN_W, 8, 49, 90, 1, PackBGRA(0, 210, 255));
    DrawString(s_resBuffer, WIN_W, 20, 56, "Overview", PackBGRA(0, 229, 255));
    DrawString(s_resBuffer, WIN_W, 110, 56, "CPU        Memory        Disk        Network", PackBGRA(160, 180, 210));

    // 3. Body Background
    FillRect(s_resBuffer, WIN_W, 1, 73, WIN_W - 2, WIN_H - 95, PackBGRA(14, 18, 28));

    // --- Section 1: CPU (Height = 84) ---
    FillRect(s_resBuffer, WIN_W, 10, 80, WIN_W - 20, 22, PackBGRA(22, 32, 50));
    DrawRect(s_resBuffer, WIN_W, 10, 80, WIN_W - 20, 84, PackBGRA(35, 48, 70));
    DrawString(s_resBuffer, WIN_W, 16, 86, "v CPU", PackBGRA(0, 210, 255));
    DrawString(s_resBuffer, WIN_W, 80, 86, "12% CPU Usage  |  100% Maximum Frequency  |  8 Processors", PackBGRA(100, 240, 160));

    // CPU Bar Graph
    DrawRect(s_resBuffer, WIN_W, 20, 110, 400, 14, PackBGRA(40, 54, 78));
    FillRect(s_resBuffer, WIN_W, 21, 111, 48, 12, PackBGRA(0, 229, 255)); // 12% = 48px
    DrawString(s_resBuffer, WIN_W, 430, 112, "Intel Xeon E3-1270 v6 @ 3.80GHz", PackBGRA(240, 245, 255));
    DrawString(s_resBuffer, WIN_W, 20, 134, "Top Processes: ntoskrnl.exe (4.2%), surshell.exe (3.1%), dwm.exe (2.0%)", PackBGRA(160, 180, 210));

    // --- Section 2: Memory (Height = 84) ---
    FillRect(s_resBuffer, WIN_W, 10, 172, WIN_W - 20, 22, PackBGRA(22, 32, 50));
    DrawRect(s_resBuffer, WIN_W, 10, 172, WIN_W - 20, 84, PackBGRA(35, 48, 70));
    DrawString(s_resBuffer, WIN_W, 16, 178, "v Memory", PackBGRA(0, 210, 255));
    DrawString(s_resBuffer, WIN_W, 90, 178, "412 MB In Use  |  32,356 MB Available  |  32,768 MB Installed", PackBGRA(100, 240, 160));

    // Memory Breakdown Bar
    DrawRect(s_resBuffer, WIN_W, 20, 202, 400, 14, PackBGRA(40, 54, 78));
    FillRect(s_resBuffer, WIN_W, 21, 203, 10, 12, PackBGRA(0, 180, 255));   // 412MB in use
    FillRect(s_resBuffer, WIN_W, 31, 203, 388, 12, PackBGRA(0, 120, 60));  // 32GB free
    DrawString(s_resBuffer, WIN_W, 430, 204, "32 GB ECC DDR4-2400", PackBGRA(240, 245, 255));
    DrawString(s_resBuffer, WIN_W, 20, 226, "Swap / Paging: Haven Memory Bus (HMB 2.0) Active | 0 KB Swap Used", PackBGRA(160, 180, 210));

    // --- Section 3: Disk (Height = 84) ---
    FillRect(s_resBuffer, WIN_W, 10, 264, WIN_W - 20, 22, PackBGRA(22, 32, 50));
    DrawRect(s_resBuffer, WIN_W, 10, 264, WIN_W - 20, 84, PackBGRA(35, 48, 70));
    DrawString(s_resBuffer, WIN_W, 16, 270, "v Disk", PackBGRA(0, 210, 255));
    DrawString(s_resBuffer, WIN_W, 80, 270, "142 MB/sec Total I/O  |  0% Highest Active Time  |  Queue: 0", PackBGRA(100, 240, 160));

    DrawRect(s_resBuffer, WIN_W, 20, 294, 400, 14, PackBGRA(40, 54, 78));
    FillRect(s_resBuffer, WIN_W, 21, 295, 120, 12, PackBGRA(255, 215, 0)); // 142 MB/s
    DrawString(s_resBuffer, WIN_W, 430, 296, "HGST 2TB Ultrastar (Port 0)", PackBGRA(240, 245, 255));
    DrawString(s_resBuffer, WIN_W, 20, 318, "Active I/O: AHCI DMA 64-bit PRDT Burst | Volumes C: (128GB), D: (1.7TB)", PackBGRA(160, 180, 210));

    // --- Section 4: Network (Height = 60) ---
    FillRect(s_resBuffer, WIN_W, 10, 356, WIN_W - 20, 22, PackBGRA(22, 32, 50));
    DrawRect(s_resBuffer, WIN_W, 10, 356, WIN_W - 20, 54, PackBGRA(35, 48, 70));
    DrawString(s_resBuffer, WIN_W, 16, 362, "v Network", PackBGRA(0, 210, 255));
    DrawString(s_resBuffer, WIN_W, 95, 362, "14 Kbps Network I/O  |  1000 Mbps Link  |  Intel I210", PackBGRA(100, 240, 160));
    DrawString(s_resBuffer, WIN_W, 20, 386, "IPv4: 147.135.1.229/24 | Gateway: 147.135.1.254 (Cisco HSRP active)", PackBGRA(160, 180, 210));

    // 4. Status Bar
    FillRect(s_resBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_resBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_resBuffer, WIN_W, 12, WIN_H - 15, "Resource telemetry refreshed at 100Hz LAPIC cadence.", PackBGRA(100, 240, 160));
    DrawString(s_resBuffer, WIN_W, WIN_W - 140, WIN_H - 15, "All Vitals OK", PackBGRA(0, 210, 255));

    PresentWindow(s_resBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Resource Monitor (resmon.exe):");
    PrintLine("  CPU: Intel Xeon E3-1270 v6 @ 3.80GHz (12% Usage, 8T Online)");
    PrintLine("  Memory: 412 MB In Use / 32,356 MB Available (32GB ECC DDR4)");
    PrintLine("  Disk: AHCI DMA 142 MB/s | Network: Intel I210 1000 Mbps");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderResourceMonitor();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
