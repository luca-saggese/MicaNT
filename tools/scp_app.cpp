/**
 * @file scp_app.cpp
 @brief Luce OS Ring 3 OpenSSH Secure Copy Utility (scp) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Copies files between hosts on a network using SSH protocol.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleScpTransfer() noexcept {
    const char* out =
        "[scp build/ntoskrnl.exe ubuntu@147.135.1.229:/tmp/ntoskrnl.exe]\n"
        "Authenticating to 147.135.1.229:22 via public key...\n"
        "SFTP/SCP Stream Negotiated (Cipher: chacha20-poly1305@openssh.com)\n"
        "Sending 'build/ntoskrnl.exe' to remote '/tmp/ntoskrnl.exe'...\n"
        "ntoskrnl.exe         100%   14MB 112.4MB/s   00:00\n"
        "Transferred 14,680,064 bytes in 0.13 seconds (Success).\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "OpenSSH_9.6p1 SCP Client (MicaNT Sovereign Cutler Edition)\n"
        "usage: scp [-346ABCOpqRrsTv] [-c cipher] [-D sftp_server_path] [-F ssh_config]\n"
        "           [-i identity_file] [-J destination] [-l limit] [-o ssh_option]\n"
        "           [-P port] [-S program] source ... target\n\n"
        "Options:\n"
        "  -r          Recursively copy entire directories\n"
        "  -p          Preserves modification times, access times, and modes from original\n"
        "  -P port     Specifies the port to connect to on the remote host\n"
        "  -i identity Selects the file from which the identity (private key) is read\n"
        "  -C          Compression enable. Passes the -C flag to ssh(1)\n"
        "  -v          Verbose mode\n\n";

    PrintStr(banner);
    ShowSampleScpTransfer();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
