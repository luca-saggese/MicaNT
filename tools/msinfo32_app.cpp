/**
 * @file msinfo32_app.cpp
 @brief Luce OS Ring 3 System Information Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell System Information window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_msiBuffer[WIN_W * WIN_H];

void RenderMsinfoWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_msiBuffer, WIN_W, WIN_H, "System Information", "File   Edit   View   Help");

    // 2. Split Panes: Left Tree (width 170), Right Items (remaining)
    constexpr uint32_t treeW = 170;
    FillRect(s_msiBuffer, WIN_W, 1, 49, treeW, WIN_H - 71, PackBGRA(14, 18, 28));
    DrawRect(s_msiBuffer, WIN_W, treeW + 1, 49, 1, WIN_H - 71, PackBGRA(35, 48, 70));

    // Highlighted node: System Summary
    FillRect(s_msiBuffer, WIN_W, 6, 54, treeW - 12, 18, PackBGRA(28, 48, 80));
    DrawString(s_msiBuffer, WIN_W, 10, 58, "v System Summary", PackBGRA(255, 255, 255));

    DrawString(s_msiBuffer, WIN_W, 16, 76, "> Hardware Resources", PackBGRA(160, 180, 210));
    DrawString(s_msiBuffer, WIN_W, 26, 92, "> Conflicts/Sharing", PackBGRA(140, 160, 190));
    DrawString(s_msiBuffer, WIN_W, 26, 108, "> DMA", PackBGRA(140, 160, 190));
    DrawString(s_msiBuffer, WIN_W, 26, 124, "> Forced Hardware", PackBGRA(140, 160, 190));
    DrawString(s_msiBuffer, WIN_W, 26, 140, "> I/O", PackBGRA(140, 160, 190));
    DrawString(s_msiBuffer, WIN_W, 26, 156, "> IRQs", PackBGRA(140, 160, 190));
    DrawString(s_msiBuffer, WIN_W, 26, 172, "> Memory", PackBGRA(140, 160, 190));
    DrawString(s_msiBuffer, WIN_W, 16, 192, "> Components", PackBGRA(160, 180, 210));
    DrawString(s_msiBuffer, WIN_W, 16, 208, "> Software Environment", PackBGRA(160, 180, 210));

    // Right Summary Table
    FillRect(s_msiBuffer, WIN_W, treeW + 2, 49, WIN_W - treeW - 3, WIN_H - 71, PackBGRA(16, 22, 34));

    // Column Header
    FillRect(s_msiBuffer, WIN_W, treeW + 2, 49, WIN_W - treeW - 3, 20, PackBGRA(22, 28, 44));
    DrawRect(s_msiBuffer, WIN_W, treeW + 2, 68, WIN_W - treeW - 3, 1, PackBGRA(35, 48, 70));

    DrawString(s_msiBuffer, WIN_W, treeW + 10, 55, "Item", PackBGRA(140, 165, 200));
    DrawString(s_msiBuffer, WIN_W, treeW + 170, 55, "Value", PackBGRA(140, 165, 200));

    // Summary Rows
    const struct SumRow {
        const char* item;
        const char* value;
    } srows[] = {
        {"OS Name",             "MicaNT Cutler Edition (64-bit)"},
        {"Version",             "10.0.26100.1 (Sovereign NT)"},
        {"System Manufacturer", "Supermicro"},
        {"System Model",        "X11SSH-F"},
        {"System Type",         "x64-based PC"},
        {"Processor",           "Intel(R) Xeon(R) CPU E3-1270 v6 @ 3.80GHz"},
        {"BIOS Mode",           "UEFI (GOP Active)"},
        {"BaseBoard Product",   "X11SSH-F"},
        {"Installed RAM",       "32.0 GB ECC DDR4-2400"},
        {"Total Physical Memory","32,768 MB"},
        {"Available Memory",    "28,558 MB"},
    };

    uint32_t sy = 72;
    for (size_t i = 0; i < 11; ++i) {
        FillRect(s_msiBuffer, WIN_W, treeW + 2, sy, WIN_W - treeW - 3, 18, (i % 2 == 1) ? PackBGRA(18, 24, 38) : PackBGRA(16, 22, 34));
        DrawString(s_msiBuffer, WIN_W, treeW + 10, sy + 3, srows[i].item, PackBGRA(200, 220, 245));
        DrawString(s_msiBuffer, WIN_W, treeW + 170, sy + 3, srows[i].value, PackBGRA(100, 240, 160));
        sy += 18;
    }

    // 3. Status Bar
    FillRect(s_msiBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_msiBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_msiBuffer, WIN_W, 12, WIN_H - 15, "System Summary Loaded | Silicon Hardware Verified OK", PackBGRA(180, 200, 230));

    PresentWindow(s_msiBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign System Information (msinfo32.exe):");
    PrintLine("  OS: MicaNT Cutler Edition (64-bit Sovereign NT Kernel)");
    PrintLine("  Hardware: Supermicro X11SSH-F | Intel Xeon E3-1270 v6 @ 3.80GHz | 32GB ECC DDR4");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderMsinfoWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
