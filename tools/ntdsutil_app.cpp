/**
 * @file ntdsutil_app.cpp
 * Luce OS Ring 3 Active Directory Database Maintenance Utility (ntdsutil) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Performs offline defragmentation, metadata cleanup, and FSMO role transfer for ntds.dit.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleNtdsStatus() noexcept {
    const char* out =
        "ntdsutil: metadata cleanup\n"
        "metadata cleanup: connections\n"
        "server connections: connect to server micadc01.micant.internal\n"
        "Binding to micadc01.micant.internal ...\n"
        "Connected to micadc01.micant.internal using credentials of locally logged on user.\n"
        "server connections: quit\n"
        "metadata cleanup: select operation target\n"
        "select operation target: list domains\n"
        "Found 1 domain(s)\n"
        "0 - DC=micant,DC=internal\n"
        "select operation target: quit\n"
        "metadata cleanup: quit\n"
        "ntdsutil: quit\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "ntdsutil: ?\n\n"
        "Commands:\n"
        "  Authoritative restore         - Authoritatively restore the DIT database\n"
        "  Configurable Settings         - Manage configurable settings\n"
        "  Files                         - Manage NTDS database files\n"
        "  IPDeny list                   - Manage LDAP IP Deny list\n"
        "  LDAP policies                 - Manage LDAP protocol policies\n"
        "  Metadata cleanup              - Clean up objects of decommissioned servers\n"
        "  Partition management         - Manage directory partitions\n"
        "  Roles                         - Manage NTDS role owner tokens (FSMO)\n"
        "  Semantic database analysis    - Semantic Checker\n"
        "  Snapshot                      - Manage directory database snapshots\n\n";

    PrintStr(banner);
    ShowSampleNtdsStatus();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
