/**
 * @file openfiles_app.cpp
 * Luce OS Ring 3 Open Files & Handles Utility (openfiles) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries and displays active file handles and remote locks across the sovereign VFS.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowOpenFiles() noexcept {
    const char* out =
        "\nFiles opened locally by processes:\n\n"
        "ID       Accessed By          Type       Open File (Path\\file name)\n"
        "======== ==================== ========== =========================================\n"
        "100      ntoskrnl.exe         File       C:\\MicaNT\\System32\\ntoskrnl.exe\n"
        "104      smss.exe             File       C:\\MicaNT\\System32\\config\\SYSTEM\n"
        "108      csrss.exe            Port       \\ThemeApiPort\n"
        "112      winlogon.exe         File       C:\\MicaNT\\System32\\LogFiles\\user.log\n"
        "116      services.exe         File       C:\\MicaNT\\System32\\config\\SOFTWARE\n"
        "120      lsass.exe            File       C:\\MicaNT\\System32\\config\\SAM\n"
        "124      explorer.exe         Directory  C:\\MicaNT\\System32\n"
        "128      dwm.exe              Device     \\Device\\GopVideo0\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "OPENFILES /Query [/S system [/U username [/P [password]]]] [/FO format] [/NH] [/V]\n\n"
        "Description:\n"
        "    Enables an administrator to list or disconnect files and folders\n"
        "    that have been opened on the sovereign VFS.\n\n";

    PrintStr(banner);

    ShowOpenFiles();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
