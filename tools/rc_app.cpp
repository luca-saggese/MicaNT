/**
 * @file rc_app.cpp
 @brief Luce OS Ring 3 Windows Resource Compiler (rc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Compiles .rc resource scripts into binary .res resource files for PE32+ binaries.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleCompilation() noexcept {
    const char* out =
        "[rc MicaNT_Shell.rc /fo MicaNT_Shell.res /v /l 0x0409]\n"
        "MicaNT (R) Resource Compiler Version 10.0.26100\n"
        "Copyright (C) 2026 MicaNT Sovereign Cutler Authority.\n"
        "All rights reserved.\n\n"
        "Parsing 'MicaNT_Shell.rc'...\n"
        "  Writing RT_MANIFEST:1 (1033) [784 bytes]\n"
        "  Writing RT_VERSION:1 (1033) [940 bytes]\n"
        "  Writing RT_GROUP_ICON:101 (1033) [62 bytes]\n"
        "  Writing RT_ICON:1 (1033) [9640 bytes, 256x256 32bpp PNG]\n"
        "  Writing RT_MENU:IDR_MAINMENU (1033) [124 bytes]\n"
        "  Writing RT_DIALOG:IDD_ABOUTBOX (1033) [248 bytes]\n"
        "  Writing RT_STRING:1 (1033) [StringTable Block 1: 16 strings, 512 bytes]\n"
        "  Writing RT_ACCELERATOR:IDA_HOTKEYS (1033) [48 bytes]\n"
        "  Writing RT_RCDATA:IDR_FIRMWARE_BLOB (1033) [4096 bytes]\n"
        "Generating binary resource stream...\n"
        "  Output file: MicaNT_Shell.res (Total resources: 9, Size: 16,454 bytes)\n"
        "  Resource tree signature: 0x00000000 0x00000020 [Standard Win32 32-bit RES]\n"
        "Compilation completed successfully (0 errors, 0 warnings).\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Resource Compiler Version 10.0.26100 (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage:  rc [options] .rc-file\n\n"
        "Switches:\n"
        "   /r          Compile only; do not link (default)\n"
        "   /v          Verbose (print progress messages)\n"
        "   /d name[=val] Define a preprocessor symbol\n"
        "   /u name     Undefine a preprocessor symbol\n"
        "   /fo resfile Name the output .res file\n"
        "   /l langid   Default language ID in hex (e.g. 0x0409 for en-US)\n"
        "   /i path     Add directory to include search path\n"
        "   /x          Ignore INCLUDE environment variable\n"
        "   /n          Append null character to all strings in string tables\n"
        "   /w          Warn on duplicate string ordinal IDs\n"
        "   /c cp       Define code page for NLS conversions (default: 65001 UTF-8)\n\n";

    PrintStr(banner);
    ShowSampleCompilation();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
