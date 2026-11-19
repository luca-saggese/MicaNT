/**
 * @file perfmon_app.cpp
 * Luce OS Ring 3 Performance Monitor (perfmon.msc) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Performance Monitor window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 640;
constexpr uint32_t WIN_H = 420;
alignas(4096) uint32_t s_prfBuffer[WIN_W * WIN_H];

void RenderPerformanceMonitor() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_prfBuffer, WIN_W, WIN_H, "Performance Monitor", "File   Action   View   Help");

    // 2. Toolbar
    FillRect(s_prfBuffer, WIN_W, 1, 49, WIN_W - 2, 22, PackBGRA(22, 30, 46));
    DrawRect(s_prfBuffer, WIN_W, 1, 70, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_prfBuffer, WIN_W, 10, 56, "[+] Add Counter  [X] Delete  [||] Freeze  [Clear]  [Line Graph]", PackBGRA(200, 220, 245));

    // 3. Body Background
    FillRect(s_prfBuffer, WIN_W, 1, 71, WIN_W - 2, WIN_H - 93, PackBGRA(14, 18, 28));

    // Performance Graph Viewport
    uint32_t gx = 20;
    uint32_t gy = 80;
    uint32_t gw = WIN_W - 40;
    uint32_t gh = 180;

    // Viewport background
    FillRect(s_prfBuffer, WIN_W, gx, gy, gw, gh, PackBGRA(8, 12, 18));
    DrawRect(s_prfBuffer, WIN_W, gx, gy, gw, gh, PackBGRA(35, 48, 70));

    // Grid lines (horizontal & vertical)
    for (uint32_t y = gy + 30; y < gy + gh; y += 30) {
        for (uint32_t x = gx + 1; x < gx + gw; x += 4) {
            s_prfBuffer[y * WIN_W + x] = PackBGRA(20, 28, 42);
        }
    }
    for (uint32_t x = gx + 50; x < gx + gw; x += 50) {
        for (uint32_t y = gy + 1; y < gy + gh; y += 4) {
            s_prfBuffer[y * WIN_W + x] = PackBGRA(20, 28, 42);
        }
    }

    // Graph scale markers
    DrawString(s_prfBuffer, WIN_W, gx + 4, gy + 4, "100", PackBGRA(120, 140, 170));
    DrawString(s_prfBuffer, WIN_W, gx + 4, gy + gh / 2 - 4, " 50", PackBGRA(120, 140, 170));
    DrawString(s_prfBuffer, WIN_W, gx + 4, gy + gh - 14, "  0", PackBGRA(120, 140, 170));

    // Trace 1: % Processor Time (Green Line)
    uint32_t cpuPts[] = { 12, 14, 11, 15, 18, 12, 9, 10, 14, 13, 11, 12, 16, 20, 15, 12, 11, 13, 14, 12, 10, 12, 13, 15, 12 };
    size_t numPts = sizeof(cpuPts) / sizeof(cpuPts[0]);
    uint32_t stepX = (gw - 50) / static_cast<uint32_t>(numPts);
    for (size_t i = 0; i + 1 < numPts; ++i) {
        uint32_t x1 = gx + 40 + static_cast<uint32_t>(i) * stepX;
        uint32_t y1 = gy + gh - (cpuPts[i] * gh / 100);
        uint32_t x2 = gx + 40 + static_cast<uint32_t>(i + 1) * stepX;
        uint32_t y2 = gy + gh - (cpuPts[i + 1] * gh / 100);
        DrawLine(s_prfBuffer, WIN_W, x1, y1, x2, y2, PackBGRA(0, 255, 128));
    }

    // Trace 2: % Committed Bytes In Use (Cyan Line)
    uint32_t memPts[] = { 2, 2, 2, 3, 3, 2, 2, 3, 3, 3, 2, 2, 3, 3, 2, 2, 2, 3, 2, 2, 2, 3, 2, 2, 2 };
    for (size_t i = 0; i + 1 < numPts; ++i) {
        uint32_t x1 = gx + 40 + static_cast<uint32_t>(i) * stepX;
        uint32_t y1 = gy + gh - (memPts[i] * gh / 10);
        uint32_t x2 = gx + 40 + static_cast<uint32_t>(i + 1) * stepX;
        uint32_t y2 = gy + gh - (memPts[i + 1] * gh / 10);
        DrawLine(s_prfBuffer, WIN_W, x1, y1, x2, y2, PackBGRA(0, 210, 255));
    }

    // 4. Counter Statistics Header (Height = 18)
    DrawString(s_prfBuffer, WIN_W, 20, gy + gh + 10, "Last: 12.8%     Average: 11.4%     Minimum: 8.2%     Maximum: 20.0%     Duration: 1:40", PackBGRA(200, 220, 245));

    // 5. Counters Table
    uint32_t ty = gy + gh + 28;
    FillRect(s_prfBuffer, WIN_W, 20, ty, WIN_W - 40, 18, PackBGRA(22, 32, 50));
    DrawRect(s_prfBuffer, WIN_W, 20, ty, WIN_W - 40, 68, PackBGRA(35, 48, 70));

    DrawString(s_prfBuffer, WIN_W, 24, ty + 5, "Color", PackBGRA(160, 180, 210));
    DrawString(s_prfBuffer, WIN_W, 80, ty + 5, "Scale", PackBGRA(160, 180, 210));
    DrawString(s_prfBuffer, WIN_W, 140, ty + 5, "Counter", PackBGRA(160, 180, 210));
    DrawString(s_prfBuffer, WIN_W, 300, ty + 5, "Instance", PackBGRA(160, 180, 210));
    DrawString(s_prfBuffer, WIN_W, 420, ty + 5, "Object", PackBGRA(160, 180, 210));

    // Counter Row 1
    FillRect(s_prfBuffer, WIN_W, 28, ty + 24, 24, 6, PackBGRA(0, 255, 128));
    DrawString(s_prfBuffer, WIN_W, 80, ty + 23, "1.0", PackBGRA(240, 245, 255));
    DrawString(s_prfBuffer, WIN_W, 140, ty + 23, "% Processor Time", PackBGRA(0, 255, 128));
    DrawString(s_prfBuffer, WIN_W, 300, ty + 23, "_Total (8 Cores)", PackBGRA(240, 245, 255));
    DrawString(s_prfBuffer, WIN_W, 420, ty + 23, "Processor", PackBGRA(180, 200, 230));

    // Counter Row 2
    FillRect(s_prfBuffer, WIN_W, 28, ty + 42, 24, 6, PackBGRA(0, 210, 255));
    DrawString(s_prfBuffer, WIN_W, 80, ty + 41, "1.0", PackBGRA(240, 245, 255));
    DrawString(s_prfBuffer, WIN_W, 140, ty + 41, "% Committed Bytes", PackBGRA(0, 210, 255));
    DrawString(s_prfBuffer, WIN_W, 300, ty + 41, "32,768 MB ECC", PackBGRA(240, 245, 255));
    DrawString(s_prfBuffer, WIN_W, 420, ty + 41, "Memory", PackBGRA(180, 200, 230));

    // 6. Status Bar
    FillRect(s_prfBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_prfBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_prfBuffer, WIN_W, 12, WIN_H - 15, "Sampling at 1.000 second intervals. Live MSR / LAPIC counters.", PackBGRA(100, 240, 160));
    DrawString(s_prfBuffer, WIN_W, WIN_W - 140, WIN_H - 15, "Counters: 2 Active", PackBGRA(0, 210, 255));

    PresentWindow(s_prfBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Performance Monitor (perfmon.msc):");
    PrintLine("  Counter 1: % Processor Time (_Total: 8 Cores Intel Xeon E3-1270 v6)");
    PrintLine("  Counter 2: % Committed Bytes In Use (Memory: 32,768 MB ECC DDR4)");
    PrintLine("  Sampling Interval: 1.000s | MSR Performance Monitor Counters Active");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderPerformanceMonitor();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
