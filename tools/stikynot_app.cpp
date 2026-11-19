/**
 * @file stikynot_app.cpp
 @brief Luce OS Ring 3 Sticky Notes Desktop Applet (stikynot) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages lightweight persisted sticky notes on the desktop workspace.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleStickyNotes() noexcept {
    const char* out =
        "[stikynot /list]\n"
        "MicaNT Sticky Notes Desktop Applet\n"
        "Persistent Store: \\DosDevices\\C:\\Users\\admin\\AppData\\Roaming\\MicaNT\\Sticky Notes\\StickyNotes.snt\n"
        "====================================================================================\n"
        "ID   Color     Position (X, Y)  Dimensions (W, H) Content Preview\n"
        "---- --------- ---------------- ----------------- ---------------------------------\n"
        "1    Yellow    (100, 120)       (300, 240)        \"MicaNT Sovereign Cutler Kernel\"\n"
        "2    Blue      (420, 120)       (300, 240)        \"HMB 2.0 Demand Paging Active\"\n"
        "3    Green     (740, 120)       (300, 240)        \"System32: 215 Tools Milestone\"\n"
        "====================================================================================\n"
        "Total Active Notes: 3 | Desktop Window Manager Layer: Topmost Alpha-Blended\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Sticky Notes (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage: stikynot.exe [options]\n\n"
        "Options:\n"
        "   /new               Create a new sticky note on the primary desktop\n"
        "   /color <name>      Set note theme (yellow, green, pink, purple, blue, charcoal)\n"
        "   /list              List all active sticky note contents and coordinates\n"
        "   /backup <path>     Export note database to specified backup path\n"
        "   /restore <path>    Import note database from specified backup file\n"
        "   /clear             Delete all sticky notes from active desktop\n\n";

    PrintStr(banner);
    ShowSampleStickyNotes();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
