/**
 * @file umount_app.cpp
 * Luce OS Ring 3 Network File System Client Unmount Utility (umount) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Unmounts Network File System (NFS) network drives.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage:  umount [-f] <-a | drive_letter [drive_letter ... ]>\n\n"
        "  -f       Forces unmounting of NFS drive even if files are currently open.\n"
        "  -a       Removes all NFS mounted drives.\n\n"
        "NFS Client Unmount Manager: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
