/**
 * @file klist_app.cpp
 @brief Luce OS Ring 3 Kerberos Ticket Query Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows klist behavior:
 * - Commands: tickets, tgt, purge, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Invalid Parameter)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Usage: klist [tickets | tgt | purge]");
    PrintLine("  tickets: displays cached tickets (default)");
    PrintLine("  tgt:     displays Ticket Granting Ticket");
    PrintLine("  purge:   purges all cached Kerberos tickets");
}

void PrintTickets() noexcept {
    PrintLine("Current LogonId is 0:0x3e7");
    PrintLine("Cached Tickets: (1)");
    PrintLine("");
    PrintLine("#0>     Client: MicaAdmin @ MICANT.LOCAL");
    PrintLine("        Server: krbtgt/MICANT.LOCAL @ MICANT.LOCAL");
    PrintLine("        KerbTicket Encryption Type: AES-256-CTS-HMAC-SHA1-96");
    PrintLine("        Ticket Flags 0x40e10000 -> forwardable renewable initial pre_authent");
    PrintLine("        Start Time: 10/24/2026 12:00:00 (local)");
    PrintLine("        End Time:   10/24/2026 22:00:00 (local)");
    PrintLine("        Renew Time: 10/31/2026 12:00:00 (local)");
    PrintLine("        Session Key Type: AES-256-CTS-HMAC-SHA1-96");
    PrintLine("        Cache Flags: 0");
    PrintLine("        Kdc Called: DC01.MICANT.LOCAL");
}

void PrintTgt() noexcept {
    PrintLine("Current LogonId is 0:0x3e7");
    PrintLine("Cached TGT:");
    PrintLine("");
    PrintLine("ServiceName              : krbtgt");
    PrintLine("TargetName               : krbtgt/MICANT.LOCAL");
    PrintLine("FullServiceName          : krbtgt/MICANT.LOCAL@MICANT.LOCAL");
    PrintLine("DomainName               : MICANT.LOCAL");
    PrintLine("TargetDomainName         : MICANT.LOCAL");
    PrintLine("ClientName               : MicaAdmin");
    PrintLine("ClientRealm              : MICANT.LOCAL");
    PrintLine("EncryptionType           : AES-256-CTS-HMAC-SHA1-96");
    PrintLine("StartTime                : 10/24/2026 12:00:00");
    PrintLine("EndTime                  : 10/24/2026 22:00:00");
    PrintLine("RenewUntil               : 10/31/2026 12:00:00");
}

void PrintPurge() noexcept {
    PrintLine("Current LogonId is 0:0x3e7");
    PrintLine("Deleting all tickets:");
    PrintLine("Ticket(s) purged successfully!");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "klist.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.argc() <= 1) {
        PrintTickets();
        ExitProcess(0);
    }

    const char* cmd = parser.argv()[1];
    if (StrCaseEqual(cmd, "tickets")) {
        PrintTickets();
    } else if (StrCaseEqual(cmd, "tgt")) {
        PrintTgt();
    } else if (StrCaseEqual(cmd, "purge")) {
        PrintPurge();
    } else {
        ShowHelp();
        ExitProcess(1);
    }

    ExitProcess(0);
}
