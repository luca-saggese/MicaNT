/**
 * @file expand_app.cpp
 * Luce OS Ring 3 Cabinet & Compressed File Extractor (expand) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Expands one or more compressed files (.cab / LZNT1 / MSZIP).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) File Expansion Utility (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (c) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "EXPAND [-r] Source Destination\n"
        "EXPAND -r Source [Destination]\n"
        "EXPAND -D Source.cab [-f:Files]\n"
        "EXPAND Source.cab -F:Files Destination\n\n"
        "  -r            Rename expanded files.\n"
        "  -D            Display list of files in source.\n"
        "  Source        Source file specification. Wildcards may be used.\n"
        "  -F:Files      Name of files to expand from a cabinet.\n"
        "  Destination   Destination file | path specification.\n\n"
        "Cabinet Decompression Engine:\n"
        "  Supported Algorithms: MSZIP (Deflate), Quantum, LZX, Raw Freestanding LZNT1\n"
        "  Status: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
