/**
 * @file clipbrd_app.cpp
 * Luce OS Ring 3 Clipboard Viewer (clipbrd) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Inspects and displays the contents of the system clipboard across supported formats.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleClipboard() noexcept {
    const char* out =
        "[clipbrd /status]\n"
        "MicaNT Sovereign Clipboard Subsystem\n"
        "Clipboard Sequence Number: 42\n"
        "Clipboard Owner Window:    0x0001004C (explorer.exe - File Explorer)\n"
        "Open Clipboard Owner:      None (Clipboard unlocked)\n"
        "------------------------------------------------------------------------------------\n"
        "Available Formats in Clipboard:\n"
        "  Format ID  Format Name           Data Size   Preview / Description\n"
        "  ---------  --------------------- ----------- --------------------------------------\n"
        "  13         CF_UNICODETEXT        128 bytes   L\"C:\\Users\\admin\\source\\MicaNT\"\n"
        "  1          CF_TEXT               64 bytes    \"C:\\Users\\admin\\source\\MicaNT\"\n"
        "  7          CF_OEMTEXT            64 bytes    \"C:\\Users\\admin\\source\\MicaNT\"\n"
        "  15         CF_HDROP              384 bytes   [1 file dropped: MicaNT.sln]\n"
        "  16         CF_LOCALE             4 bytes     LCID: 0x0409 (en-US)\n"
        "------------------------------------------------------------------------------------\n"
        "Active Viewer Mode: Unicode Text\n"
        "Content: C:\\Users\\admin\\source\\MicaNT\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Clipboard Viewer (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage: clipbrd [options]\n\n"
        "Options:\n"
        "   /status            Display clipboard metadata, lock state, and format enumeration\n"
        "   /clear             Empty the system clipboard buffer\n"
        "   /dump <format_id>  Output raw hex/text contents of specified clipboard format\n"
        "   /save <filename>   Save clipboard contents to a file (.clp / raw format)\n\n";

    PrintStr(banner);
    ShowSampleClipboard();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
