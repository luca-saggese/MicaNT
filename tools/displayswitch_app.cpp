/**
 * @file displayswitch_app.cpp
 @brief Luce OS Ring 3 Presentation Display Switcher (displayswitch) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Switches multi-monitor presentation display topologies (clone, extend, internal, external).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleDisplaySwitch() noexcept {
    const char* out =
        "[displayswitch /status]\n"
        "MicaNT Desktop Window Manager (DWM) Display Topology Manager\n"
        "Active Adapter: PrismX DEC PRISM-64 Graphics Accelerator (PCI 00:02.0)\n"
        "Active Topology: EXTEND (Multi-Monitor Desktop Span)\n"
        "------------------------------------------------------------------------------------\n"
        "Detected Display Targets:\n"
        "  Target 0 (Internal):  1920x1080 @ 144Hz (Primary, Rect: [0, 0, 1920, 1080], GOP/eDP)\n"
        "  Target 1 (External):  3840x2160 @ 60Hz  (Secondary, Rect: [1920, 0, 5760, 2160], DP-1)\n"
        "------------------------------------------------------------------------------------\n"
        "Hardware DirectFlip: Active (Zero-copy scanout buffer swap, 0 dropped frames)\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Presentation Display Switcher (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage: displayswitch.exe [/internal | /clone | /extend | /external | /status]\n\n"
        "Switches:\n"
        "   /internal          Disconnect secondary outputs; display on primary screen only\n"
        "   /clone             Duplicate the primary desktop content across all connected screens\n"
        "   /extend            Extend the desktop surface across all connected display outputs\n"
        "   /external          Display on external monitor only; power off internal panel\n"
        "   /status            Query active multi-monitor topology and connected display devices\n\n";

    PrintStr(banner);
    ShowSampleDisplaySwitch();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
