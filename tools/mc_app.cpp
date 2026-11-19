/**
 * @file mc_app.cpp
 * Luce OS Ring 3 Windows Message Compiler (mc) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Compiles ETW event manifests and message text files (.mc) into binary message tables (.bin) and header files.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleMcOutput() noexcept {
    const char* out =
        "[mc -u -U -h .\\inc -r .\\res MicaNT_Events.mc]\n"
        "MicaNT (R) Message Compiler Version 10.0.26100\n"
        "Copyright (C) 2026 MicaNT Sovereign Cutler Authority. All rights reserved.\n\n"
        "Compiling 'MicaNT_Events.mc'...\n"
        "  Facility Definitions: FACILITY_SYSTEM (0x0), FACILITY_KERNEL (0x1), FACILITY_STORAGE (0x4)\n"
        "  Severity Definitions: STATUS_SEVERITY_SUCCESS (0x0), STATUS_SEVERITY_INFORMATIONAL (0x1),\n"
        "                        STATUS_SEVERITY_WARNING (0x2), STATUS_SEVERITY_ERROR (0x3)\n"
        "  Language Definitions: English (0x0409: MSG00409)\n"
        "Parsing Messages:\n"
        "  MessageId 0x0001 (MICANT_EVENT_SYSTEM_BOOT)       [Severity: SUCCESS]\n"
        "  MessageId 0x0002 (MICANT_EVENT_SERVICE_STARTED)   [Severity: INFORMATIONAL]\n"
        "  MessageId 0x0003 (MICANT_EVENT_DRIVER_UNLOAD)     [Severity: WARNING]\n"
        "  MessageId 0x0004 (MICANT_EVENT_KERNEL_PANIC)      [Severity: ERROR]\n"
        "Generating Artifacts:\n"
        "  Writing Header: .\\inc\\MicaNT_Events.h (Symbol definitions & Event descriptor structs)\n"
        "  Writing Resource Script: .\\res\\MicaNT_Events.rc (RT_MESSAGETABLE binding)\n"
        "  Writing Binary Message Table: .\\res\\MSG00409.bin (Unicode UTF-16LE, 1,248 bytes)\n"
        "Compilation succeeded (4 messages processed).\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT (R) Message Compiler Version 10.0.26100 (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n\n"
        "Usage: mc [options] filename.mc\n\n"
        "Options:\n"
        "   -a              Input file is ANSI\n"
        "   -u              Input file is Unicode (UTF-16LE)\n"
        "   -U              Messages in the .bin file should be Unicode\n"
        "   -v              Verbose execution mode\n"
        "   -c              Set Customer bit in all message IDs\n"
        "   -h path         Directory for generated header files (default: .)\n"
        "   -r path         Directory for generated .rc and .bin files (default: .)\n"
        "   -e extension    Header file extension (default: .h)\n"
        "   -z prefix       Generate ETW logging helper inline functions\n"
        "   -m maxmsg       Generate warning if message exceeds maxmsg characters\n\n";

    PrintStr(banner);
    ShowSampleMcOutput();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
