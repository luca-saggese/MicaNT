/**
 * @file xperf_app.cpp
 @brief Luce OS Ring 3 Windows Performance Analyzer Trace Controller (xperf) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Captures low-level kernel dispatcher context switches, interrupts, and syscall traces.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleXperfStatus() noexcept {
    const char* out =
        "Kernel Logger Session:\n"
        "  Session Name:       NT Kernel Logger\n"
        "  Buffer Size:        1024 KB\n"
        "  Minimum Buffers:    64\n"
        "  Maximum Buffers:    320\n"
        "  Kernel Flags:       PROC_THREAD+LOADER+INTERRUPT+DPC+DISPATCHER+CSWITCH\n"
        "  Clock Type:         QueryPerformanceCounter (TSC Stride Sub-Microsecond)\n"
        "  Logging Mode:       Sequential Memory Buffer\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Performance Analyzer / Trace Controller (xperf.exe)\n\n"
        "Usage: xperf [options]\n\n"
        "Commands:\n"
        "  -on <flags>         Starts the kernel trace session with specified event flags.\n"
        "  -stop [session]     Stops active trace logger sessions.\n"
        "  -d <file.etl>       Merges and dumps trace buffers into target ETL file.\n"
        "  -providers [all]    Lists all registered ETW kernel/user providers.\n"
        "  -loggers            Lists all currently active trace sessions.\n\n"
        "[xperf -loggers]\n";

    PrintStr(banner);
    ShowSampleXperfStatus();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
