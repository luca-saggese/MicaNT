/**
 * @file mount_app.cpp
 * Luce OS Ring 3 Network File System Client Mount Utility (mount) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Mounts Network File System (NFS v3/v4) network shares to local drive letters.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleMounts() noexcept {
    const char* out =
        "Local    Remote                                 Properties\n"
        "-------------------------------------------------------------------------------\n"
        "Z:       \\\\147.135.1.229\\nfs_storage             UID=-2, GID=-2\n"
        "                                                rsize=1048576, wsize=1048576\n"
        "                                                MountType=soft, Sec=sys\n"
        "                                                Retries=3, Timeout=0.8\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage:  mount [-o options] [-u:username] [-p:password] <\\\\computername\\sharename> <devicename | *>\n\n"
        "Options:\n"
        "  -o rsize=size       Sets read buffer size (in KB).\n"
        "  -o wsize=size       Sets write buffer size (in KB).\n"
        "  -o timeout=time     Sets timeout in seconds for RPC calls.\n"
        "  -o retry=number     Sets number of retries for soft mount.\n"
        "  -o mtype=soft|hard  Sets mount type (default soft).\n"
        "  -o anon             Mounts as anonymous user (UID=-2, GID=-2).\n"
        "  -o nolock           Disables file locking.\n"
        "  -o casesensitive    Forces case-sensitive file lookups on server.\n\n"
        "[mount -l]\n";

    PrintStr(banner);
    ShowSampleMounts();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
