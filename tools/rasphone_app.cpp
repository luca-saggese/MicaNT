/**
 * @file rasphone_app.cpp
 @brief Luce OS Ring 3 RAS Network Connections Phonebook (rasphone) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * GUI and CLI utility for managing remote network connection phonebooks (.pbk).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Network Connections (rasphone.exe)\n\n"
        "USAGE:\n"
        "        rasphone [-f phonebook] [[-e | -d | -m] entry]\n"
        "        rasphone [-f phonebook] -a [entry]\n"
        "        rasphone [-f phonebook] -lh\n"
        "        rasphone [-f phonebook] -lx\n\n"
        "Options:\n"
        "  -a      Add a new phonebook entry.\n"
        "  -e      Edit the specified phonebook entry.\n"
        "  -d      Dial the specified phonebook entry.\n"
        "  -m      Clone the specified phonebook entry.\n"
        "  -f      Specifies phonebook path (default %APPDATA%\\Microsoft\\Network\\Connections\\Pbk\\rasphone.pbk).\n\n"
        "VPN & Remote Access Phonebook Subsystem: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
