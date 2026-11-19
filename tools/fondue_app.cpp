/**
 * @file fondue_app.cpp
 * Luce OS Ring 3 Features on Demand User Experience Tool (fondue) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Downloads and installs optional Windows features on demand.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Features on Demand User Experience Tool (fondue.exe)\n\n"
        "Usage:\n"
        "  fondue.exe /enable-feature:<feature_name> [/caller-name:<name>] [/hide-ux:{all}]\n\n"
        "Parameters:\n"
        "  /enable-feature   Name of the optional feature to enable.\n"
        "  /caller-name      Name of application requesting feature installation.\n"
        "  /hide-ux          Hides progress dialogs during installation.\n\n"
        "Available Features on Demand:\n"
        "  - DirectPlay (Legacy DirectX 9/DirectDraw gaming layer)\n"
        "  - OpenSSH Client & Server\n"
        "  - MicaNT Subsystem for Linux (WSL2 Sovereign Guest)\n"
        "  - RSAT Active Directory Tools\n"
        "  - Graphics Tools (D3D12 SDK & PIX Capture Layer)\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
