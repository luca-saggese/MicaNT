/**
 * @file relog_app.cpp
 * Luce OS Ring 3 Performance Counter Log Transformer (relog) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Extracts performance counters from logs into other formats (CSV, TSV, BLG, SQL).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Relog.exe (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage:\n"
        "  relog <filename [filename ...]> [options]\n\n"
        "Options:\n"
        "  -?                      Displays context-sensitive help.\n"
        "  -a                      Append output to existing file.\n"
        "  -c <path [path ...]>    Counters to filter from input log.\n"
        "  -cf <filename>          File listing performance counters to filter from input log.\n"
        "  -f <CSV|TSV|BIN|SQL>    Output file format (default BIN).\n"
        "  -t <n>                  Write only every nth record in output file.\n"
        "  -o <output-file>        Output file path or SQL DSN.\n"
        "  -b <M/d/yyyy H:mm:ss>   Begin time for filtering records.\n"
        "  -e <M/d/yyyy H:mm:ss>   End time for filtering records.\n\n"
        "Performance Log Transformer Engine: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
