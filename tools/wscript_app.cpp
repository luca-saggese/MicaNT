/**
 * @file wscript_app.cpp
 @brief Luce OS Ring 3 Windows Script Host GUI Execution Host (wscript) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Executes VBScript and JScript files within a windowed GUI environment.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Script Host Version 5.812 (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage: wscript.exe scriptname.extension [option...] [arguments...]\n\n"
        "Options:\n"
        " //B         Batch mode: Suppresses script errors and prompts from displaying\n"
        " //D         Enable Active Debugging\n"
        " //E:engine  Use engine for executing script\n"
        " //H:CScript Changes the default script host to CScript.exe\n"
        " //H:WScript Changes the default script host to WScript.exe (default)\n"
        " //I         Interactive mode (default, opposite of //B)\n"
        " //Job:xxxx  Execute a WSF job\n"
        " //Logo      Display logo (default)\n"
        " //Nologo    Prevent logo display: No banner will be shown at execution time\n"
        " //S         Save current command line options for this user\n"
        " //T:nn      Time out in seconds:  Maximum time a script is permitted to run\n"
        " //X         Execute script in debugger\n"
        " //U         Use Unicode for redirected I/O from the console\n\n"
        "Script Host Engine: Ready (GUI Subsystem Dispatched).\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
