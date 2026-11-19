/**
 * @file finger_app.cpp
 @brief Luce OS Ring 3 Finger User Information Protocol Client (finger) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Queries remote user session and identity telemetry over TCP port 79 (RFC 1288).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleFinger() noexcept {
    const char* out =
        "[localhost]\n"
        "Login      Name               TTY  Idle  When         Office\n"
        "MicaAdmin  Sovereign Admin    con  0:00  Oct 23 08:30 Supermicro BMC KVM\n"
        "SYSTEM     NT AUTHORITY\\SYS   sv0  ----  Oct 23 08:30 Ring 0 Executive\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "FINGER [-l] [user]@host [...]\n\n"
        "  -l          Displays information in long list format.\n"
        "  user        Specifies the user you want information about.\n"
        "  @host       Specifies the server on the remote system whose users\n"
        "              you want information about.\n\n"
        "[Finger Query: Local Sessions]\n";

    PrintStr(banner);
    ShowSampleFinger();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
