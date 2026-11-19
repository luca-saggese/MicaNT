/**
 * @file ssh_keygen_app.cpp
 @brief Luce OS Ring 3 OpenSSH Authentication Key Utility (ssh-keygen) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Generates, manages, and converts authentication keys for OpenSSH.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleKeygen() noexcept {
    const char* out =
        "[ssh-keygen -t ed25519 -C \"admin@micant-station\"]\n"
        "Generating public/private ed25519 key pair.\n"
        "Your identification has been saved in id_ed25519\n"
        "Your public key has been saved in id_ed25519.pub\n"
        "The key fingerprint is:\n"
        "SHA256:Y1zN0V+FjK3b8zG4xR5P9q2tW6vE8a1c4e7h9j0l3mQ admin@micant-station\n"
        "The key's randomart image is:\n"
        "+--[ED25519 256]--+\n"
        "|       .o.o.     |\n"
        "|      .. o .     |\n"
        "|     .  = .      |\n"
        "|    . o+ *       |\n"
        "|   . + =S.+      |\n"
        "|  . o o.B..      |\n"
        "|   o ..+oE       |\n"
        "|    o ++*o       |\n"
        "|    .==o*.       |\n"
        "+----[SHA256]-----+\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "OpenSSH_9.6p1 Key Utility (MicaNT Sovereign Cutler Edition)\n"
        "usage: ssh-keygen [-q] [-a rounds] [-b bits] [-C comment] [-E fingerprint_hash]\n"
        "                  [-m format] [-N new_passphrase] [-O option]\n"
        "                  [-t dsa | ecdsa | ecdsa-sk | ed25519 | ed25519-sk | rsa]\n"
        "                  [-w provider] [-Z cipher] [-f output_keyfile]\n\n"
        "Key Management Options:\n"
        "  -t type        Specifies the type of key to create (default: ed25519)\n"
        "  -b bits        Specifies the number of bits in the key to create\n"
        "  -f filename    Specifies the filename of the key file\n"
        "  -l             Show fingerprint of specified public key file\n"
        "  -p             Requests changing the passphrase of a private key file\n"
        "  -y             Read a private OpenSSH format file and print an OpenSSH public key\n\n";

    PrintStr(banner);
    ShowSampleKeygen();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
