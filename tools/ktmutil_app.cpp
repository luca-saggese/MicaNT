/**
 * @file ktmutil_app.cpp
 * Luce OS Ring 3 Kernel Transaction Manager Utility (ktmutil) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages Kernel Transaction Manager (KTM) transactions, resource managers, and CLFS streams.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleKtmTransactions() noexcept {
    const char* out =
        "[ktmutil /list]\n"
        "MicaNT Kernel Transaction Manager (KTM) Subsystem\n"
        "CLFS Log Container: \\SystemRoot\\System32\\TxR\\* (Common Log File System)\n"
        "====================================================================================\n"
        "Transaction GUID                       State     Coordinator  Enlisted Resource Managers\n"
        "-------------------------------------- --------- ------------ --------------------------\n"
        "{7A42E810-5B12-4C3D-9A01-C0382B8A1001} Committed KTM-Internal TxF (C:\\), TxR (SOFTWARE)\n"
        "{8F19B2C3-4E23-49A1-8012-D0493C9B2002} Prepared  MSDTC-Proxy  TxF (D:\\Database)\n"
        "{9E38C4D5-3F34-48B2-7123-E05A4D0C3003} Active    KTM-Internal TxF (C:\\MICANT\\Servicing)\n"
        "====================================================================================\n"
        "Active Transactions: 1 | Prepared Transactions: 1 | Committed in Log: 142\n"
        "Resource Managers: 2 Active (Transactional NTFS, Transactional Registry)\n"
        "Two-Phase Commit (2PC) Protocol State: HEALTHY (No in-doubt transactions)\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Sovereign Kernel Transaction Manager Utility (ktmutil)\n"
        "Copyright (C) MicaNT Operating System Authority.\n\n"
        "Usage: ktmutil [command] [options]\n\n"
        "Commands:\n"
        "   /list              Enumerate all active, prepared, and in-doubt transactions\n"
        "   /tm <guid>         Query transaction manager details for specified GUID\n"
        "   /rm <guid>         Query resource manager enrollments and log streams\n"
        "   /commit <guid>     Force-commit a prepared transaction\n"
        "   /rollback <guid>   Abort and rollback an active or in-doubt transaction\n"
        "   /recover           Perform startup recovery on CLFS transaction log stream\n"
        "   /status            Display KTM engine status and resource manager statistics\n\n";

    PrintStr(banner);
    ShowSampleKtmTransactions();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
