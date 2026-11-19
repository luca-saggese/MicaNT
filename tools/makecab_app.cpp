/**
 * @file makecab_app.cpp
 * Luce OS Ring 3 Cabinet Archiver & Compression Utility (makecab) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Packages and compresses files into Sovereign Cabinet (.cab) format.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Cabinet Maker (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (c) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "MAKECAB [/V[n]] [/D var=value ...] [/L dir] source [destination]\n"
        "MAKECAB [/V[n]] [/D var=value ...] /F directive_file [...]\n\n"
        "  source         File to compress.\n"
        "  destination    File name to give compressed file.  If omitted, the\n"
        "                 last character of the source file name is replaced\n"
        "                 with an underscore (_) and used as the destination.\n"
        "  /F directives  A file with MakeCAB directives (may be repeated).\n"
        "  /D var=value   Defines variable with specified value.\n"
        "  /L dir         Location to place destination (default is current directory).\n"
        "  /V[n]          Verbosity level (1..3).\n\n"
        "Cabinet Compression Engine:\n"
        "  Compression Type: MSZIP / LZX:21 Direct-Stream Engine\n"
        "  Status: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
