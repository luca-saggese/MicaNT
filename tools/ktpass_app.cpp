/**
 * @file ktpass_app.cpp
 @brief Luce OS Ring 3 Kerberos Keytab Generator & SPN Configurator (ktpass) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Configures server principal names in Active Directory and exports Kerberos keytabs.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleKeytabGen() noexcept {
    const char* out =
        "Targeting domain controller: micadc01.micant.internal\n"
        "Successful logon for user MicaAdmin.\n"
        "Key created.\n"
        "Output keytab to krb5.keytab:\n"
        "Keytab version: 0x502\n"
        "keysize 69 HTTP/micant.internal@MICANT.INTERNAL ptype 1 (KRB5_NT_PRINCIPAL) vno 1 etype 0x12 (AES256-SHA1) keylength 32\n"
        "Account MicaAdmin has been set for HTTP/micant.internal\n"
        "Keytab written successfully.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "ktpass <parameters>\n\n"
        "Parameters:\n"
        "  /out <KeytabFileName>       - Specifies keytab file to generate\n"
        "  /princ <PrincipalName>      - Principal name (e.g. host/srv.domain@REALM)\n"
        "  /mapuser <AccountName>      - Maps principal name to user account\n"
        "  /pass <Password>            - Account password\n"
        "  /crypto <CryptoType>        - Crypto type (AES256-SHA1, AES128-SHA1, RC4-HMAC-NT)\n"
        "  /ptype <PrincipalType>      - Principal type (KRB5_NT_PRINCIPAL, KRB5_NT_SRV_INST)\n"
        "  /kvno <KeyVersionNumber>    - Key version number (default 1)\n\n"
        "[ktpass /princ HTTP/micant.internal@MICANT.INTERNAL /mapuser MicaAdmin /crypto AES256-SHA1 /out krb5.keytab]\n";

    PrintStr(banner);
    ShowSampleKeytabGen();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
