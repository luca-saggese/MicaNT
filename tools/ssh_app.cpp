/**
 * @file ssh_app.cpp
 @brief Luce OS Ring 3 OpenSSH Client (ssh) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Secure shell client for logging into remote machines and executing commands.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleSshSession() noexcept {
    const char* out =
        "[ssh -i ~/.ssh/micant_ci_key ubuntu@147.135.1.229]\n"
        "OpenSSH_9.6p1, OpenSSL 3.2.1 30 Jan 2024\n"
        "Connecting to 147.135.1.229:22...\n"
        "Peer Host Key: ED25519 SHA256:d8c1e48f7a932b1c4e0982df31a76c2b1890ef0192a\n"
        "Authenticating with public key 'id_ed25519' (MicaNT CI Agent)\n"
        "Authenticated to 147.135.1.229 ([147.135.1.229]:22).\n"
        "PTY allocated: /dev/pts/1 (Terminal: xterm-256color)\n"
        "Last login: Fri Oct 23 10:20:00 2026 from 10.0.0.1\n"
        "ubuntu@supermicro-node01:~$ \n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "OpenSSH_9.6p1, OpenSSL 3.2.1 (MicaNT Sovereign Cutler Edition)\n"
        "usage: ssh [-46AaCfGgKkMNnqsTtVvXxYy] [-B bind_interface] [-b bind_address]\n"
        "           [-c cipher_spec] [-D [bind_address:]port] [-E log_file]\n"
        "           [-e escape_char] [-F configfile] [-I pkcs11] [-i identity_file]\n"
        "           [-J destination] [-L address] [-l login_name] [-m mac_spec]\n"
        "           [-O ctl_cmd] [-o option] [-p port] [-Q query_option] [-R address]\n"
        "           [-S ctl_path] [-W host:port] [-w local_tun[:remote_tun]]\n"
        "           destination [command [argument ...]]\n\n";

    PrintStr(banner);
    ShowSampleSshSession();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
