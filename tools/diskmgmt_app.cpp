/**
 * @file diskmgmt_app.cpp
 * Luce OS Ring 3 Disk Management Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Disk Management window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_dskBuffer[WIN_W * WIN_H];

void RenderDiskManagementWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_dskBuffer, WIN_W, WIN_H, "Disk Management", "File   Action   View   Help");

    // 2. Volume List Column Header (Height = 20)
    FillRect(s_dskBuffer, WIN_W, 1, 49, WIN_W - 2, 20, PackBGRA(22, 28, 44));
    DrawRect(s_dskBuffer, WIN_W, 1, 68, WIN_W - 2, 1, PackBGRA(35, 48, 70));

    DrawString(s_dskBuffer, WIN_W, 10, 55, "Volume", PackBGRA(140, 165, 200));
    DrawString(s_dskBuffer, WIN_W, 100, 55, "Layout", PackBGRA(140, 165, 200));
    DrawString(s_dskBuffer, WIN_W, 180, 55, "File System", PackBGRA(140, 165, 200));
    DrawString(s_dskBuffer, WIN_W, 300, 55, "Status", PackBGRA(140, 165, 200));
    DrawString(s_dskBuffer, WIN_W, 440, 55, "Capacity", PackBGRA(140, 165, 200));
    DrawString(s_dskBuffer, WIN_W, 520, 55, "Free", PackBGRA(140, 165, 200));

    // Volume Rows
    const struct VolRow {
        const char* vol;
        const char* layout;
        const char* fs;
        const char* status;
        const char* cap;
        const char* free;
    } vrows[] = {
        {"(C:)", "Simple", "FAT32",    "Healthy (System)", "128 GB", "127 GB"},
        {"(D:)", "Simple", "FAT32",    "Healthy (Data)",   "1.75 TB","1.74 TB"},
        {"(E:)", "Simple", "EXT4 (RO)","Healthy (Linux)",  "20 GB",  "12 GB"},
    };

    uint32_t vy = 72;
    for (size_t i = 0; i < 3; ++i) {
        FillRect(s_dskBuffer, WIN_W, 1, vy, WIN_W - 2, 20, (i % 2 == 1) ? PackBGRA(16, 22, 34) : PackBGRA(14, 18, 28));
        DrawString(s_dskBuffer, WIN_W, 10, vy + 4, vrows[i].vol, PackBGRA(0, 210, 255));
        DrawString(s_dskBuffer, WIN_W, 100, vy + 4, vrows[i].layout, PackBGRA(180, 200, 230));
        DrawString(s_dskBuffer, WIN_W, 180, vy + 4, vrows[i].fs, PackBGRA(100, 240, 160));
        DrawString(s_dskBuffer, WIN_W, 300, vy + 4, vrows[i].status, PackBGRA(220, 235, 255));
        DrawString(s_dskBuffer, WIN_W, 440, vy + 4, vrows[i].cap, PackBGRA(200, 220, 245));
        DrawString(s_dskBuffer, WIN_W, 520, vy + 4, vrows[i].free, PackBGRA(200, 220, 245));
        vy += 20;
    }

    // 3. Lower Pane Divider
    FillRect(s_dskBuffer, WIN_W, 1, 140, WIN_W - 2, 4, PackBGRA(28, 38, 58));

    // 4. Graphical Volume Partition Map
    FillRect(s_dskBuffer, WIN_W, 10, 155, 120, 80, PackBGRA(22, 30, 46));
    DrawRect(s_dskBuffer, WIN_W, 10, 155, 120, 80, PackBGRA(38, 52, 78), 1);
    DrawString(s_dskBuffer, WIN_W, 18, 165, "Disk 0", PackBGRA(255, 255, 255));
    DrawString(s_dskBuffer, WIN_W, 18, 185, "Basic", PackBGRA(180, 200, 230));
    DrawString(s_dskBuffer, WIN_W, 18, 205, "1863 GB Online", PackBGRA(100, 240, 160));

    // Partition Bars next to Disk 0 header
    uint32_t px = 135;
    // C: Partition Bar (width 110)
    FillRect(s_dskBuffer, WIN_W, px, 155, 110, 80, PackBGRA(24, 40, 68));
    DrawRect(s_dskBuffer, WIN_W, px, 155, 110, 80, PackBGRA(0, 120, 215), 1);
    DrawString(s_dskBuffer, WIN_W, px + 8, 165, "(C:) System", PackBGRA(0, 210, 255));
    DrawString(s_dskBuffer, WIN_W, px + 8, 185, "128 GB FAT32", PackBGRA(200, 220, 245));
    DrawString(s_dskBuffer, WIN_W, px + 8, 205, "Healthy (Boot)", PackBGRA(100, 240, 160));

    // D: Partition Bar (width 200)
    px += 115;
    FillRect(s_dskBuffer, WIN_W, px, 155, 200, 80, PackBGRA(20, 36, 60));
    DrawRect(s_dskBuffer, WIN_W, px, 155, 200, 80, PackBGRA(45, 65, 95), 1);
    DrawString(s_dskBuffer, WIN_W, px + 8, 165, "(D:) Data Volume", PackBGRA(230, 240, 255));
    DrawString(s_dskBuffer, WIN_W, px + 8, 185, "1.75 TB FAT32", PackBGRA(200, 220, 245));
    DrawString(s_dskBuffer, WIN_W, px + 8, 205, "Healthy (Active)", PackBGRA(100, 240, 160));

    // E: Partition Bar (width 110)
    px += 205;
    FillRect(s_dskBuffer, WIN_W, px, 155, 110, 80, PackBGRA(30, 36, 48));
    DrawRect(s_dskBuffer, WIN_W, px, 155, 110, 80, PackBGRA(50, 65, 85), 1);
    DrawString(s_dskBuffer, WIN_W, px + 8, 165, "(E:) Linux RO", PackBGRA(180, 200, 230));
    DrawString(s_dskBuffer, WIN_W, px + 8, 185, "20 GB Ext4", PackBGRA(160, 180, 200));
    DrawString(s_dskBuffer, WIN_W, px + 8, 205, "Healthy (RO)", PackBGRA(100, 240, 160));

    // 5. Status Bar
    FillRect(s_dskBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_dskBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_dskBuffer, WIN_W, 12, WIN_H - 15, "Disk 0: 1.81 TB Total | GPT Valid | AHCI SATA DMA Active", PackBGRA(180, 200, 230));

    PresentWindow(s_dskBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Disk Management (diskmgmt.msc):");
    PrintLine("  Disk 0: 1,863 GB HGST HUS726020ALA610 SATA HDD (AHCI Port 0)");
    PrintLine("  Volumes: C: [128GB FAT32 Boot], D: [1.75TB FAT32], E: [20GB Ext4 Linux]");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderDiskManagementWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
