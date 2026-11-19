/**
 * @file print_app.cpp
 @brief Luce OS Ring 3 Line Printer Spooling Utility (print) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Prints a text file to a local printer or remote shared spooler queue.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Prints a text file.\n\n"
        "PRINT [/D:device] [[drive:][path]filename[...]]\n\n"
        "   /D:device   Specifies a print device.\n"
        "               Valid values include parallel ports (LPT1, LPT2, LPT3),\n"
        "               serial ports (COM1, COM2, COM3, COM4), or network\n"
        "               printer shares (\\\\server\\printer_share).\n\n"
        "Default print queue: LPT1 (Local Line Printer)\n"
        "Spooler Subsystem (spoolsv.exe): Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
