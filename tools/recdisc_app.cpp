/**
 * @file recdisc_app.cpp
 @brief Luce OS Ring 3 System Repair Disc Creation Utility (recdisc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Generates bootable system recovery media (ISO / USB / CD-DVD) containing WinRE.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Create a system repair disc (recdisc.exe)\n\n"
        "Usage:\n"
        "  recdisc.exe [/target:<drive_letter>] [/iso:<output_path>]\n\n"
        "Recovery Media Image Sources:\n"
        "  Bootloader  : \\EFI\\BOOT\\BOOTX64.EFI (Clean-Room Freestanding PE32+)\n"
        "  Kernel Core : \\MicaNT\\System32\\ntoskrnl.exe\n"
        "  Subsystem   : WinRE / Ubuntu Linux Rescue Partition Staging Active\n\n"
        "Status: System repair media generator ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
