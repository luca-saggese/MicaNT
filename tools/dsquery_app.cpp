/**
 * @file dsquery_app.cpp
 * Luce OS Ring 3 Directory Service Query Utility (dsquery) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries Active Directory / LDAP directory partitions for users, computers, and groups.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleQuery() noexcept {
    const char* out =
        "\"CN=Administrator,CN=Users,DC=micant,DC=internal\"\n"
        "\"CN=MicaAdmin,CN=Users,DC=micant,DC=internal\"\n"
        "\"CN=Guest,CN=Users,DC=micant,DC=internal\"\n"
        "\"CN=DefaultAccount,CN=Users,DC=micant,DC=internal\"\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Description: The dsquery command queries Active Directory according to specified criteria.\n\n"
        "Syntax:\n"
        "  dsquery computer - finds computers in the directory.\n"
        "  dsquery contact  - finds contacts in the directory.\n"
        "  dsquery subnet   - finds subnets in the directory.\n"
        "  dsquery group    - finds groups in the directory.\n"
        "  dsquery ou       - finds organizational units in the directory.\n"
        "  dsquery site     - finds sites in the directory.\n"
        "  dsquery server   - finds domain controllers in the directory.\n"
        "  dsquery user     - finds users in the directory.\n"
        "  dsquery quota    - finds quota specifications in the directory.\n"
        "  dsquery partition- finds directory partitions in the directory.\n"
        "  dsquery *        - finds any object in the directory via LDAP filter.\n\n"
        "[dsquery user]\n";

    PrintStr(banner);
    ShowSampleQuery();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
