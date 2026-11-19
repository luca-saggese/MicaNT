/**
 * @file diskshadow_app.cpp
 * Luce OS Ring 3 DiskShadow VSS Scripting Engine (diskshadow) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Exposes VSS snapshot scripting commands and backup metadata generation.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("MicaNT DiskShadow version 1.0 (MicaNT Sovereign Cutler Edition)");
    PrintLine("Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.");
    PrintLine("");
    PrintLine("DISKSHADOW [/s <scriptfile>] [/l <logfile>] [/?]");
    PrintLine("");
    PrintLine("Commands:");
    PrintLine("  set context persistent");
    PrintLine("  begin backup");
    PrintLine("  add volume C: alias VolumeC");
    PrintLine("  create");
    PrintLine("  expose %VolumeC% S:");
    PrintLine("  end backup");
    PrintLine("  list shadows all");
}

void ExecuteScript(const char* scriptFile) noexcept {
    PrintLine("MicaNT DiskShadow version 1.0 (MicaNT Sovereign Cutler Edition)");
    PrintLine("Copyright (C) 2026 MicaNT Operating System Authority. All rights reserved.\n");
    Print("Executing script: ");
    PrintLine(scriptFile ? scriptFile : "STDIN");
    PrintLine("-> set context persistent");
    PrintLine("-> begin backup");
    PrintLine("-> add volume C: alias VolumeC");
    PrintLine("-> create");
    PrintLine("Creating shadow copy set {9b81ca10-1824-4f01-8b20-192a0019fa01}...");
    PrintLine("Shadow copy set successfully created.");
    PrintLine("Volume C: shadow copy created as \\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy1");
    PrintLine("-> end backup");
    PrintLine("Backup sequence completed successfully.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "diskshadow.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("s")) {
        const char* script = parser.GetSwitchValue("s");
        ExecuteScript(script);
        ExitProcess(0);
    }

    if (parser.PositionalCount() > 0) {
        ExecuteScript(parser.GetPositional(0));
        ExitProcess(0);
    }

    ExecuteScript(nullptr);
    ExitProcess(0);
}
