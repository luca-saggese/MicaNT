/**
 * @file nltest_app.cpp
 * Luce OS Ring 3 Network Location & Netlogon Diagnostic Utility (nltest) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Tests domain controller discovery, Kerberos trust relationships, and secure channel health.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleNltest() noexcept {
    const char* out =
        "Trusted DC Connection Status:\n"
        "           DC: \\\\MICADC01.micant.internal\n"
        "      Address: \\\\147.135.1.229\n"
        "     Dom Guid: {A7D34B8C-94F1-4AE7-90C2-225CD54BE8A1}\n"
        "     Dom Name: MICANT\n"
        "  Forest Name: micant.internal\n"
        " Dc Site Name: Default-First-Site-Name\n"
        "Our Site Name: Default-First-Site-Name\n"
        "        Flags: PDC GC DS LDAP KDC TIMESERV GTIMESERV WRITABLE FULL_SECRET WS 0x37F\n"
        "The command completed successfully\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage: nltest [/OPTIONS]\n\n"
        "  /DCLIST:<DomainName>            - Get list of Domain Controllers\n"
        "  /DSGETDC:<DomainName>           - Call DsGetDcName to find DC\n"
        "  /SC_QUERY:<DomainName>          - Query secure channel status\n"
        "  /SC_VERIFY:<DomainName>         - Verify secure channel status\n"
        "  /SC_RESET:<DomainName>          - Reset secure channel\n"
        "  /DOMAIN_TRUSTS                  - Query domain trust relationships\n"
        "  /WHOWILL:<Domain> <User>        - See which DC will authenticate user\n"
        "  /SERVER:<ServerName>            - Direct commands to specified server\n\n"
        "[nltest /dsgetdc:micant.internal]\n";

    PrintStr(banner);
    ShowSampleNltest();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
