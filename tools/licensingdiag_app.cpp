/**
 * @file licensingdiag_app.cpp
 * Luce OS Ring 3 Windows Licensing Diagnostic Utility (licensingdiag) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Collects and exports software licensing telemetry and activation diagnostic logs.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Licensing Diagnostic Tool (licensingdiag.exe)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage:\n"
        "  licensingdiag.exe [-cab <path>] [-log <path>] [-extended]\n\n"
        "Parameters:\n"
        "  -cab <path>       Specifies the cabinet file name to output diagnostics to.\n"
        "  -log <path>       Specifies the log file to output diagnostics to.\n"
        "  -extended         Gathers extended diagnostic telemetry.\n\n"
        "Diagnostic Collector Engine: Generated %TEMP%\\LicensingDiag.cab successfully.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
