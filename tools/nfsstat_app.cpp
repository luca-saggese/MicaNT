/**
 * @file nfsstat_app.cpp
 @brief Luce OS Ring 3 Network File System Statistics Utility (nfsstat) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Displays statistics for Network File System (NFS) and Remote Procedure Call (RPC) activity.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleNfsStats() noexcept {
    const char* out =
        "Client RPC statistics:\n"
        "calls      badcalls   retrans    badxid     timeouts   waits\n"
        "1284       0          0          0          0          0\n\n"
        "Client NFS v3 statistics:\n"
        "null       getattr    setattr    lookup     access     readlink\n"
        "0 (0%)     342 (26%)  12 (1%)    512 (39%)  280 (21%)  0 (0%)\n"
        "read       write      create     mkdir      symlink    mknod\n"
        "64 (5%)    50 (4%)    14 (1%)    4 (0%)     0 (0%)     0 (0%)\n"
        "remove     rmdir      rename     link       readdir    readdirplus\n"
        "2 (0%)     0 (0%)     0 (0%)     0 (0%)     2 (0%)     2 (0%)\n"
        "fsstat     fsinfo     pathconf   commit\n"
        "0 (0%)     2 (0%)     0 (0%)     0 (0%)\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Displays or resets statistics for Network File System (NFS) and RPC calls.\n\n"
        "NFSSTAT [-c] [-s] [-r] [-z]\n\n"
        "  -c      Display client statistics.\n"
        "  -s      Display server statistics.\n"
        "  -r      Display RPC statistics.\n"
        "  -z      Reset statistics to zero.\n\n"
        "[nfsstat -c -r]\n";

    PrintStr(banner);
    ShowSampleNfsStats();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
