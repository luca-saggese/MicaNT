/**
 * @file control_app.cpp
 @brief Luce OS Ring 3 Control Panel Application for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT Control Panel window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 680;
constexpr uint32_t WIN_H = 460;
alignas(4096) uint32_t s_cplBuffer[WIN_W * WIN_H];

void RenderControlPanel() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_cplBuffer, WIN_W, WIN_H, "Control Panel", "File   Edit   View   Tools   Help");

    // 2. Navigation / Search Bar
    FillRect(s_cplBuffer, WIN_W, 1, 49, WIN_W - 2, 28, PackBGRA(22, 30, 46));
    DrawRect(s_cplBuffer, WIN_W, 1, 76, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_cplBuffer, WIN_W, 12, 58, "[<-] [->]  Control Panel > All Control Panel Items", PackBGRA(200, 220, 245));
    DrawRect(s_cplBuffer, WIN_W, WIN_W - 180, 53, 168, 20, PackBGRA(40, 54, 78));
    DrawString(s_cplBuffer, WIN_W, WIN_W - 172, 58, "Search Control Panel", PackBGRA(120, 140, 170));

    // 3. Body Background
    FillRect(s_cplBuffer, WIN_W, 1, 77, WIN_W - 2, WIN_H - 99, PackBGRA(14, 18, 28));

    // Category Grid Header
    DrawString(s_cplBuffer, WIN_W, 20, 92, "Adjust your computer's settings", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, WIN_W - 130, 92, "View by: Category", PackBGRA(160, 180, 210));

    // Left Column Categories
    // Category 1: System and Security
    DrawRect(s_cplBuffer, WIN_W, 20, 116, 28, 28, PackBGRA(0, 180, 255), 2);
    DrawString(s_cplBuffer, WIN_W, 28, 124, "S", PackBGRA(0, 229, 255));
    DrawString(s_cplBuffer, WIN_W, 58, 116, "System and Security", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, 58, 130, "Review your computer's status (sysdm.cpl)", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, 58, 142, "Save backup copies with AHCI DMA Mirroring", PackBGRA(120, 140, 170));

    // Category 2: Network and Internet
    DrawRect(s_cplBuffer, WIN_W, 20, 166, 28, 28, PackBGRA(0, 200, 120), 2);
    DrawString(s_cplBuffer, WIN_W, 28, 174, "N", PackBGRA(100, 240, 160));
    DrawString(s_cplBuffer, WIN_W, 58, 166, "Network and Internet", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, 58, 180, "View network status and tasks (ncpa.cpl)", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, 58, 192, "Intel I210 Gigabit Ethernet (147.135.1.229)", PackBGRA(120, 140, 170));

    // Category 3: Hardware and Sound
    DrawRect(s_cplBuffer, WIN_W, 20, 216, 28, 28, PackBGRA(255, 180, 0), 2);
    DrawString(s_cplBuffer, WIN_W, 28, 224, "H", PackBGRA(255, 215, 0));
    DrawString(s_cplBuffer, WIN_W, 58, 216, "Hardware and Sound", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, 58, 230, "View devices and printers (devmgmt.msc)", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, 58, 242, "Adjust system volume and audio endpoints", PackBGRA(120, 140, 170));

    // Category 4: Programs
    DrawRect(s_cplBuffer, WIN_W, 20, 266, 28, 28, PackBGRA(200, 100, 255), 2);
    DrawString(s_cplBuffer, WIN_W, 28, 274, "P", PackBGRA(220, 160, 255));
    DrawString(s_cplBuffer, WIN_W, 58, 266, "Programs and Features", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, 58, 280, "Uninstall a program (appwiz.cpl)", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, 58, 292, "Turn MicaNT features on or off", PackBGRA(120, 140, 170));

    // Right Column Categories
    uint32_t rightColX = 350;

    // Category 5: User Accounts
    DrawRect(s_cplBuffer, WIN_W, rightColX, 116, 28, 28, PackBGRA(0, 180, 255), 2);
    DrawString(s_cplBuffer, WIN_W, rightColX + 8, 124, "U", PackBGRA(0, 229, 255));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 116, "User Accounts", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 130, "Change account type (NT AUTHORITY\\SYSTEM)", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 142, "Credential Manager and Security Tokens", PackBGRA(120, 140, 170));

    // Category 6: Appearance and Personalization
    DrawRect(s_cplBuffer, WIN_W, rightColX, 166, 28, 28, PackBGRA(255, 100, 150), 2);
    DrawString(s_cplBuffer, WIN_W, rightColX + 8, 174, "A", PackBGRA(255, 140, 180));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 166, "Appearance and Personalization", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 180, "Navigation properties and SurShell theme", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 192, "High-Contrast dark sovereign aesthetic", PackBGRA(120, 140, 170));

    // Category 7: Clock and Region
    DrawRect(s_cplBuffer, WIN_W, rightColX, 216, 28, 28, PackBGRA(0, 210, 200), 2);
    DrawString(s_cplBuffer, WIN_W, rightColX + 8, 224, "C", PackBGRA(80, 240, 230));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 216, "Clock and Region", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 230, "Set time and date (timedate.cpl)", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 242, "Change date, time, or number formats", PackBGRA(120, 140, 170));

    // Category 8: Ease of Access
    DrawRect(s_cplBuffer, WIN_W, rightColX, 266, 28, 28, PackBGRA(120, 180, 255), 2);
    DrawString(s_cplBuffer, WIN_W, rightColX + 8, 274, "E", PackBGRA(160, 210, 255));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 266, "Administrative Tools", PackBGRA(0, 210, 255));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 280, "Computer Management (compmgmt.msc)", PackBGRA(160, 180, 210));
    DrawString(s_cplBuffer, WIN_W, rightColX + 38, 292, "Performance Monitor and Event Viewer", PackBGRA(120, 140, 170));

    // 4. Status Bar
    FillRect(s_cplBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_cplBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));

    DrawString(s_cplBuffer, WIN_W, 12, WIN_H - 15, "Sovereign Control Panel | 8 Core Management Categories", PackBGRA(100, 240, 160));
    DrawString(s_cplBuffer, WIN_W, WIN_W - 170, WIN_H - 15, "All Subsystems OK", PackBGRA(0, 210, 255));

    PresentWindow(s_cplBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Control Panel (control.exe):");
    PrintLine("  Categories: System and Security, Network, Hardware, Programs, Accounts, Admin Tools");
    PrintLine("  Applets: sysdm.cpl, ncpa.cpl, appwiz.cpl, devmgmt.msc, compmgmt.msc");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderControlPanel();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
