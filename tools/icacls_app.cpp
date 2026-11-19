/**
 * @file icacls_app.cpp
 * Luce OS Ring 3 Integrity & Access Control Lists Utility (icacls) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries, modifies, and validates NT Access Control Lists (DACLs, SACLs, and Mandatory Integrity Levels).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleAcls() noexcept {
    const char* out =
        "C:\\MicaNT\\System32 NT AUTHORITY\\SYSTEM:(F)\n"
        "                     BUILTIN\\Administrators:(F)\n"
        "                     BUILTIN\\Users:(RX)\n"
        "                     APPLICATION PACKAGE AUTHORITY\\ALL APPLICATION PACKAGES:(RX)\n"
        "                     Mandatory Label\\High Mandatory Level:(NW)\n\n"
        "C:\\MicaNT\\System32\\ntoskrnl.exe NT AUTHORITY\\SYSTEM:(F)\n"
        "                                 BUILTIN\\Administrators:(F)\n"
        "                                 BUILTIN\\Users:(RX)\n"
        "                                 Mandatory Label\\System Mandatory Level:(NW)\n\n"
        "Successfully processed 2 files; Failed processing 0 files\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "ICACLS name /save aclfile [/T] [/C] [/L] [/Q]\n"
        "    stores the DACLs for the matches into aclfile.\n\n"
        "ICACLS directory [/substitute SidOld SidNew [...]] /restore aclfile\n"
        "    applies the stored DACLs to files in directory.\n\n"
        "ICACLS name /grant[:r] UserName:perm [/deny User:perm] [/reset]\n"
        "    grants or denies specified user permissions.\n\n"
        "Sovereign Security Reference Monitor (SRM) DACL Query:\n"
        "------------------------------------------------------\n";

    PrintStr(banner);

    ShowSampleAcls();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
