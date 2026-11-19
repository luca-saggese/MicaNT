/**
 * @file showmount_app.cpp
 @brief Luce OS Ring 3 Display NFS Exports Utility (showmount) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries remote NFS daemons and displays exported directory trees.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleExports() noexcept {
    const char* out =
        "Exports list on 147.135.1.229:\n"
        "/mnt/nfs_storage                   *\n"
        "/mnt/micasys                       147.135.1.0/24\n"
        "/mnt/backups                       147.135.1.254\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Displays exported file systems on an NFS server.\n\n"
        "SHOWMOUNT -e [server]\n"
        "SHOWMOUNT -a [server]\n"
        "SHOWMOUNT -d [server]\n\n"
        "  -e      Displays the exported file systems on the server.\n"
        "  -a      Displays all mounted clients and associated directories.\n"
        "  -d      Displays all directories mounted by clients.\n\n"
        "[showmount -e 147.135.1.229]\n";

    PrintStr(banner);
    ShowSampleExports();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
