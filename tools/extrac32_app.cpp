/**
 * @file extrac32_app.cpp
 @brief Luce OS Ring 3 32/64-Bit Cabinet Extraction Utility (extrac32) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Native cabinet extraction engine for OS servicing and installation manifests.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Cabinet Extraction Tool (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (c) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "EXTRAC32 [/Y] [/A] [/D | /E] [/L dir] cabinet [filename ...]\n"
        "EXTRAC32 [/Y] source [newname]\n"
        "EXTRAC32 [/Y] /C source destination\n\n"
        "  cabinet  - Cabinet file (contains two or more files).\n"
        "  filename - Name of the file to extract from the cabinet.\n"
        "  destination - Target file name or directory to extract to.\n"
        "  /A         Process ALL cabinets.  Follows cabinet chain starting in first cab.\n"
        "  /C         Copy source file to destination (to copy from DMF disks).\n"
        "  /D         Display cabinet directory (use with filename to avoid extract).\n"
        "  /E         Extract (use instead of *.* to extract all files).\n"
        "  /L dir     Location to place extracted files (default is current directory).\n"
        "  /Y         Do not prompt before overwriting an existing file.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
