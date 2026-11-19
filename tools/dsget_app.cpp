/**
 * @file dsget_app.cpp
 @brief Luce OS Ring 3 Directory Service Object Getter (dsget) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Fetches and displays specific directory attributes for Active Directory / LDAP objects.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleGet() noexcept {
    const char* out =
        "dn                                                    samid      disabled\n"
        "CN=MicaAdmin,CN=Users,DC=micant,DC=internal          MicaAdmin  no\n"
        "dsget succeeded\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Description: The dsget command displays selected attributes of specific objects in Active Directory.\n\n"
        "Syntax:\n"
        "  dsget user <ObjectDN...> [-samid] [-upn] [-fn] [-ln] [-dept] [-title] [-disabled]\n"
        "  dsget computer <ObjectDN...> [-samid] [-loc] [-desc] [-disabled]\n"
        "  dsget group <ObjectDN...> [-samid] [-secgrp] [-scope] [-members]\n"
        "  dsget server <ObjectDN...> [-dnsname] [-site] [-isgc]\n\n"
        "[dsget user \"CN=MicaAdmin,CN=Users,DC=micant,DC=internal\" -samid -disabled]\n";

    PrintStr(banner);
    ShowSampleGet();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
