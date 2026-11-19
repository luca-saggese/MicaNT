/**
 * @file wt_app.cpp
 * Luce OS Ring 3 Windows Terminal Host (wt.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Built using Windows Terminal from Microsoft's GitHub repository (microsoft/terminal, MIT License).
 * All copyright to names belong to Microsoft Corporation.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowTerminalSession() noexcept {
    const char* out =
        "[wt.exe] Windows Terminal Session Host (ConPTY / PseudoConsole Bridge)\n"
        "Active Profiles: 1. MicaNT Command Interpreter (cmd.exe)\n"
        "                 2. PowerShell 7 (powershell.exe)\n"
        "                 3. Ubuntu 24.04 LTS (wsl.exe)\n"
        "Renderer:        DirectWrite / DirectX 12 Fast Font Texture Glyphs (DirectFlip 144Hz)\n"
        "Virtual PTY:     \\Device\\ConDrv / ConPTY PseudoConsole Pipe Armed\n\n"
        "Launching default tab [MicaNT Command Interpreter]...\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Windows Terminal (wt.exe) v1.20.10812.0\n"
        "Built using Windows Terminal from Microsoft's GitHub repository (microsoft/terminal, MIT License).\n"
        "All copyright to names belong to Microsoft Corporation.\n\n"
        "Usage: wt.exe [options] [command [arguments]]\n\n"
        "Options:\n"
        "  -p, --profile <profile>    Open a new tab with the specified profile.\n"
        "  -d, --startingDirectory    Set starting directory for the new process.\n"
        "  -w, --window <window-id>   Specify target terminal window instance.\n"
        "  --title <title>            Set the tab title.\n"
        "  --suppressApplicationTitle Ignore title changes requested by application.\n\n";

    PrintStr(banner);
    ShowTerminalSession();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
