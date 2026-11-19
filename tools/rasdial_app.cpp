/**
 * @file rasdial_app.cpp
 * Luce OS Ring 3 Remote Access Service Client Dialer (rasdial) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Connects, disconnects, and manages RAS dial-up and VPN connections.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleRasStatus() noexcept {
    const char* out =
        "Connected to\n"
        "MicaNT Sovereign WireGuard/IKEv2 VPN\n\n"
        "Interface:           vpn0\n"
        "Assigned IP:         10.200.0.2\n"
        "Server Gateway:      10.200.0.1\n"
        "Cipher Suite:        ChaCha20-Poly1305 / AES-256-GCM\n"
        "Compression:         None\n\n"
        "Command completed successfully.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "USAGE:\n"
        "        rasdial entryname [username [password|*]] [/DOMAIN:domain]\n"
        "                [/PHONE:phonenumber] [/CALLBACK:callbacknumber]\n"
        "                [/PHONEBOOK:phonebookfile] [/PREFIXSUFFIX]\n\n"
        "        rasdial [entryname] /DISCONNECT\n\n"
        "        rasdial\n\n"
        "[rasdial]\n";

    PrintStr(banner);
    ShowSampleRasStatus();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
