/**
 * @file regedit_app.cpp
 @brief Luce OS Ring 3 Registry Editor Utility for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Renders authentic Windows NT / SurShell Registry Editor window to GOP VRAM via NtGdiPresentSurface (0x81),
 * formats output via NtWriteFile (0x25), and exits cleanly via NtTerminateProcess (0x01).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t WIN_W = 580;
constexpr uint32_t WIN_H = 380;
alignas(4096) uint32_t s_regBuffer[WIN_W * WIN_H];

void RenderRegistryEditorWindow() noexcept {
    // 1. Frame & Title & Menu Bar
    DrawWindowFrame(s_regBuffer, WIN_W, WIN_H, "Registry Editor", "File   Edit   View   Favorites   Help");

    // 2. Address Bar (Height = 22)
    FillRect(s_regBuffer, WIN_W, 1, 49, WIN_W - 2, 22, PackBGRA(16, 22, 34));
    DrawRect(s_regBuffer, WIN_W, 1, 70, WIN_W - 2, 1, PackBGRA(35, 48, 70));
    DrawString(s_regBuffer, WIN_W, 10, 56, "Computer\\HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\ahci", PackBGRA(200, 225, 255));

    // 3. Split Panes: Left Tree (width 210), Right Values (remaining)
    constexpr uint32_t treeW = 210;
    FillRect(s_regBuffer, WIN_W, 1, 71, treeW, WIN_H - 93, PackBGRA(14, 18, 28));
    DrawRect(s_regBuffer, WIN_W, treeW + 1, 71, 1, WIN_H - 93, PackBGRA(35, 48, 70));

    // Left Tree View items
    DrawString(s_regBuffer, WIN_W, 10, 78, "> Computer", PackBGRA(180, 200, 230));
    DrawString(s_regBuffer, WIN_W, 20, 94, "> HKEY_CLASSES_ROOT", PackBGRA(160, 180, 210));
    DrawString(s_regBuffer, WIN_W, 20, 110, "> HKEY_CURRENT_USER", PackBGRA(160, 180, 210));
    DrawString(s_regBuffer, WIN_W, 20, 126, "v HKEY_LOCAL_MACHINE", PackBGRA(0, 210, 255));
    DrawString(s_regBuffer, WIN_W, 36, 142, "> HARDWARE", PackBGRA(160, 180, 210));
    DrawString(s_regBuffer, WIN_W, 36, 158, "> SAM", PackBGRA(160, 180, 210));
    DrawString(s_regBuffer, WIN_W, 36, 174, "> SECURITY", PackBGRA(160, 180, 210));
    DrawString(s_regBuffer, WIN_W, 36, 190, "> SOFTWARE", PackBGRA(160, 180, 210));
    DrawString(s_regBuffer, WIN_W, 36, 206, "v SYSTEM", PackBGRA(0, 210, 255));
    DrawString(s_regBuffer, WIN_W, 48, 222, "v CurrentControlSet", PackBGRA(0, 210, 255));
    DrawString(s_regBuffer, WIN_W, 60, 238, "v Services", PackBGRA(0, 210, 255));

    // Highlighted key: ahci
    FillRect(s_regBuffer, WIN_W, 58, 252, treeW - 60, 18, PackBGRA(28, 48, 80));
    DrawString(s_regBuffer, WIN_W, 72, 256, "ahci", PackBGRA(255, 255, 255));

    // Right Values Pane
    FillRect(s_regBuffer, WIN_W, treeW + 2, 71, WIN_W - treeW - 3, WIN_H - 93, PackBGRA(16, 20, 32));

    // Values Column Header
    FillRect(s_regBuffer, WIN_W, treeW + 2, 71, WIN_W - treeW - 3, 20, PackBGRA(22, 28, 44));
    DrawRect(s_regBuffer, WIN_W, treeW + 2, 90, WIN_W - treeW - 3, 1, PackBGRA(35, 48, 70));

    DrawString(s_regBuffer, WIN_W, treeW + 10, 77, "Name", PackBGRA(140, 165, 200));
    DrawString(s_regBuffer, WIN_W, treeW + 110, 77, "Type", PackBGRA(140, 165, 200));
    DrawString(s_regBuffer, WIN_W, treeW + 200, 77, "Data", PackBGRA(140, 165, 200));

    // Values Rows
    const struct ValRow {
        const char* name;
        const char* type;
        const char* data;
    } vrows[] = {
        {"(Default)",     "REG_SZ",     "(value not set)"},
        {"DriverName",    "REG_SZ",     "ahci.sys"},
        {"Start",         "REG_DWORD",  "0x00000000 (0)"},
        {"Type",          "REG_DWORD",  "0x00000001 (1)"},
        {"ErrorControl",  "REG_DWORD",  "0x00000003 (3)"},
        {"ImagePath",     "REG_EXPAND", "System32\\drivers\\ahci.sys"},
    };

    uint32_t vy = 95;
    for (size_t i = 0; i < 6; ++i) {
        DrawString(s_regBuffer, WIN_W, treeW + 10, vy + 4, vrows[i].name, PackBGRA(230, 240, 255));
        DrawString(s_regBuffer, WIN_W, treeW + 110, vy + 4, vrows[i].type, PackBGRA(180, 200, 230));
        DrawString(s_regBuffer, WIN_W, treeW + 200, vy + 4, vrows[i].data, PackBGRA(100, 240, 160));
        vy += 20;
    }

    // 4. Status Bar
    FillRect(s_regBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 21, PackBGRA(24, 32, 48));
    DrawRect(s_regBuffer, WIN_W, 1, WIN_H - 22, WIN_W - 2, 1, PackBGRA(38, 52, 76));
    DrawString(s_regBuffer, WIN_W, 12, WIN_H - 15, "Computer\\HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\ahci", PackBGRA(180, 200, 230));

    PresentWindow(s_regBuffer, WIN_W, WIN_H);
}

} // namespace

extern "C" void _start(const char* /*cmdLine*/ = nullptr) {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(0x11, csVal & 3, 0);

    PrintLine("Sovereign Registry Editor (regedit.exe):");
    PrintLine("  Hive Target: HKLM\\SYSTEM (8KB regf Active)");
    PrintLine("  Key Path: HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\ahci");
    PrintLine("  Window presented to GOP VRAM at center coordinates.");

    RenderRegistryEditorWindow();

    DoSyscall(0x01, 0, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}
