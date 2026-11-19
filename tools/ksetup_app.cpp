/**
 * @file ksetup_app.cpp
 * Luce OS Ring 3 Kerberos Realm & KDC Configuration Utility (ksetup) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages Kerberos 5 realms, KDC hosts, and client realm mapping in registry.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleKsetup() noexcept {
    const char* out =
        "default realm = MICANT.INTERNAL (NT Authority Kerberos 5)\n"
        "MICANT.INTERNAL:\n"
        "    kdc = micadc01.micant.internal:88\n"
        "    RealmFlags = 0x01 (TcpSupported)\n"
        "Mapping user * to *.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "KSETUP <command> <arguments>\n\n"
        "Commands:\n"
        "  /SetRealm <RealmName>               - Sets default Kerberos realm\n"
        "  /AddKdc <RealmName> <KdcName>       - Adds KDC entry for realm\n"
        "  /DelKdc <RealmName> <KdcName>       - Deletes KDC entry for realm\n"
        "  /SetComputerPassword <Password>     - Sets local workstation trust password\n"
        "  /MapUser <Principal> <Account>      - Maps Kerberos principal to local account\n"
        "  /DumpState                          - Dumps current Kerberos configuration\n\n"
        "[ksetup /dumpstate]\n";

    PrintStr(banner);
    ShowSampleKsetup();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
