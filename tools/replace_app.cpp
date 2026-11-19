/**
 * @file replace_app.cpp
 @brief Luce OS Ring 3 File Replacement Utility (replace) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Replaces files in the destination directory or adds new files from source.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleReplace() noexcept {
    const char* out =
        "[replace C:\\Updates\\*.dll C:\\MICANT\\System32 /u /r]\n"
        "Comparing source and destination file versions...\n"
        "Replacing C:\\MICANT\\System32\\fltlib.dll\n"
        "Replacing C:\\MICANT\\System32\\wintrust.dll\n"
        "Replacing C:\\MICANT\\System32\\netapi32.dll\n"
        "3 files replaced successfully.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Replaces files.\n\n"
        "REPLACE [drive1:][path1]filename [drive2:][path2] [/A] [/P] [/R] [/S] [/W] [/U]\n\n"
        "  [drive1:][path1]filename Specifies the source file or files.\n"
        "  [drive2:][path2]         Specifies the directory where files are to be replaced.\n"
        "  /A                       Adds new files to destination directory. Cannot use with\n"
        "                           /S or /U switches.\n"
        "  /P                       Prompts for confirmation before replacing a file or adding\n"
        "                           a source file.\n"
        "  /R                       Replaces read-only files as well as unprotected files.\n"
        "  /S                       Replaces files in all subdirectories of the destination\n"
        "                           directory. Cannot use with the /A switch.\n"
        "  /W                       Waits for you to insert a disk before beginning.\n"
        "  /U                       Replaces (updates) only files that are older than source\n"
        "                           files. Cannot use with the /A switch.\n\n";

    PrintStr(banner);
    ShowSampleReplace();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
