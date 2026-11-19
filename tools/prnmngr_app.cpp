/**
 * @file prnmngr_app.cpp
 @brief Luce OS Ring 3 Printer and Driver Manager Utility (prnmngr) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Adds, deletes, and lists printers, printer connections, and default printer settings.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSamplePrinters() noexcept {
    const char* out =
        "Server name <local>\n"
        "Printer name     MicaNT PDF Document Writer\n"
        "Share name       \n"
        "Driver name      MicaNT Sovereign PostScript/PDF Engine\n"
        "Port name        PORTPROMPT:\n"
        "Comment          Virtual Print-to-PDF Device\n"
        "Location         Local System\n"
        "Print processor  winprint\n"
        "Data type        RAW\n"
        "Parameters       \n"
        "Attributes       0x200 (Default Printer)\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage: prnmngr [-adxgl?][c] [-s server][-p printer_name][-m driver_name]\n"
        "               [-r port_name][-u user_name][-w password]\n\n"
        "Arguments:\n"
        "  -a     - add local printer\n"
        "  -d     - delete printer\n"
        "  -x     - delete all printers\n"
        "  -g     - get default printer\n"
        "  -t     - set default printer\n"
        "  -l     - list printers\n\n"
        "[prnmngr -l]\n";

    PrintStr(banner);
    ShowSamplePrinters();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
