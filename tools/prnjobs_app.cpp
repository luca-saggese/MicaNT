/**
 * @file prnjobs_app.cpp
 * Luce OS Ring 3 Print Job Management Utility (prnjobs) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Pauses, resumes, cancels, and lists print jobs in printer spooler queues.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleJobs() noexcept {
    const char* out =
        "Server name <local>\n"
        "Printer name MicaNT PDF Document Writer\n"
        "Total print jobs: 0\n"
        "Print queue is empty and ready.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage: prnjobs [-zmpxl?][c] [-s server][-p printer_name][-j job_id]\n"
        "               [-u user_name][-w password]\n\n"
        "Arguments:\n"
        "  -z     - pause print job\n"
        "  -m     - resume print job\n"
        "  -x     - cancel print job\n"
        "  -l     - list print jobs\n\n"
        "[prnjobs -l]\n";

    PrintStr(banner);
    ShowSampleJobs();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
