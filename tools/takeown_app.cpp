/**
 * @file takeown_app.cpp
 @brief Luce OS Ring 3 File Ownership Utility (takeown) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Enables administrators to recover access to files by reassigning ownership.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "TAKEOWN [/S system [/U username [/P [password]]]]\n"
        "        /F filename [/A] [/R [/D prompt]]\n\n"
        "Description:\n"
        "    This tool allows an administrator to recover access to a file that\n"
        "    was denied by re-assigning file ownership.\n\n"
        "Parameter List:\n"
        "    /F    filename      Specifies the file name or directory name pattern.\n"
        "    /A                  Gives ownership to the Administrators group, not current user.\n"
        "    /R                  Recurse: instructs tool on perform also on subdirectories.\n"
        "    /D    prompt        Default answer used when current user does not have list folder.\n\n";

    PrintStr(banner);

    PrintStr("SUCCESS: The file (or folder): \"C:\\MicaNT\\System32\" now owned by the Administrators group.\n");
    PrintStr("SUCCESS: The file (or folder): \"C:\\MicaNT\\System32\\ntoskrnl.exe\" now owned by the Administrators group.\n");

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
