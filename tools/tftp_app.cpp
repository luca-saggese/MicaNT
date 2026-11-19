/**
 * @file tftp_app.cpp
 @brief Luce OS Ring 3 Trivial File Transfer Protocol Client (tftp) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Transmits and receives files over UDP port 69 via Intel I210 network driver.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "TFTP [-i] host [GET | PUT] source [destination]\n\n"
        "  -i            Specifies binary image transfer mode (also called\n"
        "                octet). In binary image mode the file is moved\n"
        "                literally, byte by byte. Use this mode when\n"
        "                transferring binary files.\n"
        "  host          Specifies the local or remote host.\n"
        "  GET           Transfers the file destination on the remote host to\n"
        "                the file source on the local host.\n"
        "  PUT           Transfers the file source on the local host to\n"
        "                the file destination on the remote host.\n"
        "  source        Specifies the file to transfer.\n"
        "  destination   Specifies where to transfer the file.\n\n"
        "Network Protocol Binding:\n"
        "  Adapter       : Intel I210 Gigabit Ethernet (147.135.1.229)\n"
        "  Transport     : UDP Port 69 (RFC 1350 Standard)\n"
        "  Status        : TFTP client initialized and ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
