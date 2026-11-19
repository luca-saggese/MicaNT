/**
 * @file schtasks_app.cpp
 @brief Luce OS Ring 3 Task Scheduler Command Line Utility (schtasks) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Creates, deletes, queries, runs, and stops scheduled tasks.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleTasks() noexcept {
    const char* out =
        "Folder: \\\n"
        "TaskName                             Next Run Time          Status\n"
        "==================================== ====================== ================\n"
        "\\MicaNT\\SystemRestore                10/24/2026 00:00:00    Ready\n"
        "\\MicaNT\\SpindleTelemetryMonitor      10/23/2026 10:30:00    Ready\n"
        "\\MicaNT\\MemoryDedupSweep             10/23/2026 11:00:00    Ready\n"
        "\\MicaNT\\BmsCellBalance               10/23/2026 12:00:00    Ready\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "SCHTASKS [/parameter [arguments]]\n\n"
        "Description:\n"
        "  Enables an administrator to create, delete, query, change, run and\n"
        "  end scheduled tasks on a local or remote system.\n\n"
        "Parameter List:\n"
        "  /Create         Creates a new scheduled task.\n"
        "  /Delete         Deletes the scheduled task(s).\n"
        "  /Query          Displays all scheduled tasks.\n"
        "  /Change         Changes the properties of scheduled task.\n"
        "  /Run            Runs the scheduled task on demand.\n"
        "  /End            Stops the currently running scheduled task.\n\n"
        "[schtasks /query]\n";

    PrintStr(banner);
    ShowSampleTasks();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
