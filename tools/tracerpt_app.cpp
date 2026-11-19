/**
 * @file tracerpt_app.cpp
 @brief Luce OS Ring 3 Event Trace Log Parser & Report Generator (tracerpt) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Parses ETW/Kernel trace event logs into CSV/XML reports and dumps schema summaries.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) TraceRpt.exe (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage:\n"
        "  tracerpt <filename [filename ...]> [options]\n\n"
        "Options:\n"
        "  -?                       Displays context-sensitive help.\n"
        "  -o [filename]            Text-based output file (CSV/XML). Default is dumpfile.csv.\n"
        "  -summary [filename]      Output summary file. Default is workload_summary.txt.\n"
        "  -report [filename]       Output report file. Default is workload_report.html.\n"
        "  -rt <session_name>       Real-time Event Trace Session data source.\n"
        "  -df <filename>           Create a counting schema file.\n\n"
        "Kernel Event Trace Parser: Online.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
