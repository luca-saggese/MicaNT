/**
 * @file compmgmt_app.cpp
 @brief Luce OS Ring 3 Computer Management Console (compmgmt.msc) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Computer Management window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 700;
constexpr uint32_t WIN_H = 460;
alignas(4096) uint32_t s_mgmtBuffer[WIN_W * WIN_H];

void RenderComputerManagement() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_mgmtBuffer, WIN_W, WIN_H, "Computer Management", "File   Action   View   Help");

    // 2. Toolbar
    FillRect(s_mgmtBuffer, WIN_W, 1, 49, WIN_W - 2, 22, PackBGRA(22, 30, 46));
    DrawRect(s_mgmtBuffer, WIN_W, 1, 70, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_mgmtBuffer, WIN_W, 10, 56, "[<-] [->]  [Refresh]  [Properties]  [Export List]  [Help]", PackBGRA(200, 220, 245));

    // 3. Body Split: Left Tree Pane (W = 230), Right Details Pane (W = 468)
    // Left Pane
    FillRect(s_mgmtBuffer, WIN_W, 1, 71, 230, WIN_H - 93, PackBGRA(16, 20, 32));
    DrawRect(s_mgmtBuffer, WIN_W, 231, 71, 1, WIN_H - 93, PackBGRA(35, 48, 70));

    // Left Pane Tree
    DrawString(s_mgmtBuffer, WIN_W, 8, 80, "v Computer Management (Local)", PackBGRA(0, 210, 255));
    DrawString(s_mgmtBuffer, WIN_W, 16, 100, "v System Tools", PackBGRA(180, 200, 230));
    DrawString(s_mgmtBuffer, WIN_W, 28, 118, "> Event Viewer (eventvwr.msc)", PackBGRA(240, 245, 255));
    DrawString(s_mgmtBuffer, WIN_W, 28, 136, "> Shared Folders", PackBGRA(160, 180, 210));
    DrawString(s_mgmtBuffer, WIN_W, 28, 154, "> Local Users and Groups", PackBGRA(160, 180, 210));
    DrawString(s_mgmtBuffer, WIN_W, 28, 172, "> Performance (perfmon.msc)", PackBGRA(240, 245, 255));
    DrawString(s_mgmtBuffer, WIN_W, 28, 190, "> Device Manager (devmgmt.msc)", PackBGRA(100, 240, 160));

    DrawString(s_mgmtBuffer, WIN_W, 16, 214, "v Storage", PackBGRA(180, 200, 230));
    DrawString(s_mgmtBuffer, WIN_W, 28, 232, "> Disk Management (diskmgmt.msc)", PackBGRA(240, 245, 255));

    DrawString(s_mgmtBuffer, WIN_W, 16, 256, "v Services and Applications", PackBGRA(180, 200, 230));
    DrawString(s_mgmtBuffer, WIN_W, 28, 274, "> Services (services.msc)", PackBGRA(240, 245, 255));
    DrawString(s_mgmtBuffer, WIN_W, 28, 292, "> WMI Control (winmgmt)", PackBGRA(160, 180, 210));

    // Right Details Pane
    FillRect(s_mgmtBuffer, WIN_W, 232, 71, WIN_W - 233, WIN_H - 93, PackBGRA(14, 18, 28));

    // Right Pane Header
    FillRect(s_mgmtBuffer, WIN_W, 232, 71, WIN_W - 233, 22, PackBGRA(20, 26, 40));
    DrawString(s_mgmtBuffer, WIN_W, 242, 78, "System Overview & Administrative Summary", PackBGRA(0, 210, 255));

    // Details Grid
    DrawRect(s_mgmtBuffer, WIN_W, 244, 102, WIN_W - 256, 114, PackBGRA(35, 48, 70));
    DrawString(s_mgmtBuffer, WIN_W, 254, 96, " System Node Specification ", PackBGRA(0, 210, 255));

    DrawString(s_mgmtBuffer, WIN_W, 254, 114, "Machine Name:      MICANT-XEON", PackBGRA(240, 245, 255));
    DrawString(s_mgmtBuffer, WIN_W, 254, 132, "Processor Silicon: Intel Xeon E3-1270 v6 @ 3.80GHz (8T)", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, 254, 150, "Physical Memory:   32,768 MB ECC DDR4-2400", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, 254, 168, "OS Version:        MicaNT Sovereign Workstation (x86_64)", PackBGRA(0, 229, 255));
    DrawString(s_mgmtBuffer, WIN_W, 254, 186, "Security Context:  NT AUTHORITY\\SYSTEM (S-1-5-18)", PackBGRA(255, 215, 0));
    DrawString(s_mgmtBuffer, WIN_W, 254, 204, "Subsystem Mode:    Dave Cutler NT Executive Ring 0 / Ring 3", PackBGRA(180, 200, 230));

    // Subsystem Health Grid
    DrawRect(s_mgmtBuffer, WIN_W, 244, 230, WIN_W - 256, 126, PackBGRA(35, 48, 70));
    DrawString(s_mgmtBuffer, WIN_W, 254, 224, " Sovereign Subsystem Health ", PackBGRA(0, 210, 255));

    DrawString(s_mgmtBuffer, WIN_W, 254, 242, "Storage:     AHCI SATA DMA Volume C: (128GB FAT32)  [HEALTHY]", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, 254, 260, "Network:     Intel I210 (147.135.1.229/24, 1 Gbps)  [ONLINE]", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, 254, 278, "Graphics:    UEFI GOP (1920x1080 32-bpp BGRA)       [ACCELERATED]", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, 254, 296, "SMP:         8/8 Cores Online & LAPIC 100Hz Armed   [SYNCHRONIZED]", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, 254, 314, "Services:    SCM (PID 704), RPC, EventLog, Lanman   [RUNNING]", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, 254, 332, "Security:    SRM Token SeDebug, SeShutdown, SeTcb   [PRIVILEGED]", PackBGRA(255, 215, 0));

    // 4. Status Bar
    FillRect(s_mgmtBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_mgmtBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_mgmtBuffer, WIN_W, 12, WIN_H - 15, "Computer Management: Connected to MICANT-XEON (Local).", PackBGRA(100, 240, 160));
    DrawString(s_mgmtBuffer, WIN_W, WIN_W - 140, WIN_H - 15, "MMC 3.0 Active", PackBGRA(0, 210, 255));

    PresentWindow(s_mgmtBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Computer Management Console (compmgmt.msc):");
    PrintLine("  System Tools: Event Viewer, Shared Folders, Performance, Device Manager");
    PrintLine("  Storage: Disk Management | Services: SCM, RPC, EventLog");
    PrintLine("  Target: MICANT-XEON (Local Supermicro X11SSH-F Silicon)");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderComputerManagement();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
