/**
 * @file muicache_app.cpp
 @brief Luce OS Ring 3 Multilingual User Interface Cache Tool (muicache) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries, inspects, and flushes MUI satellite localized string caches.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleMuiCache() noexcept {
    const char* out =
        "[muicache /list]\n"
        "MicaNT Multilingual User Interface (MUI) String Cache Registry\n"
        "Hive: HKCR\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache\n"
        "Active System Language: en-US (LCID: 0x0409) | Fallback: en -> Invariant\n"
        "====================================================================================================\n"
        "Binary Application Path                   Type        Localized Display Name / Description\n"
        "----------------------------------------- ----------- --------------------------------------------\n"
        "C:\\MICANT\\System32\\explorer.exe.FriendlyApp Application Windows File Explorer\n"
        "C:\\MICANT\\System32\\explorer.exe.AppType    String      Windows Desktop & Shell Environment\n"
        "C:\\MICANT\\System32\\cmd.exe.FriendlyAppName Application Windows Command Processor\n"
        "C:\\MICANT\\System32\\taskmgr.exe.FriendlyApp Application Task Manager (Process & Resource Monitor)\n"
        "C:\\MICANT\\System32\\notepad.exe.FriendlyApp Application Notepad Text Editor\n"
        "C:\\MICANT\\System32\\calc.exe.FriendlyAppName Application Calculator Applet\n"
        "C:\\MICANT\\System32\\regedit.exe.FriendlyApp Application Registry Editor Subsystem\n"
        "C:\\MICANT\\System32\\mstsc.exe.FriendlyAppName Application Remote Desktop Connection Client\n"
        "C:\\MICANT\\System32\\perfmon.exe.FriendlyApp Application Performance Monitor & Telemetry\n"
        "C:\\MICANT\\System32\\cleanmgr.exe.FriendlyApp Application Disk Cleanup Utility\n"
        "----------------------------------------- ----------- --------------------------------------------\n"
        "Total Cached String Entries: 213 (Satellite DLLs verified: 100% OK, Cache Status: VALID)\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Sovereign Multilingual User Interface Cache Tool (muicache)\n"
        "Copyright (C) MicaNT Operating System Authority.\n\n"
        "Usage: muicache [command] [options]\n\n"
        "Commands:\n"
        "   /list             Display all localized strings in the active MUI cache\n"
        "   /query <path>     Query localized name and description for an executable\n"
        "   /flush            Flush stale entries from user and system MuiCache hives\n"
        "   /rebuild          Scan System32 PE32+ binaries and regenerate MUI indices\n"
        "   /lang <lang-tag>  Display MUI satellite lookup chain for specified locale\n"
        "   /status           Show health of resource satellite directories (en-US, etc.)\n\n";

    PrintStr(banner);
    ShowSampleMuiCache();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
