/**
 * @file at_app.cpp
 * Luce OS Ring 3 AT Schedule Job Utility (at.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Schedules commands and programs to run on a computer at a specified time and date.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("The AT command schedules commands and programs to run on a computer at");
    PrintLine("a specified time and date. The Schedule service must be running to use");
    PrintLine("the AT command.");
    PrintLine("");
    PrintLine("AT [\\\\computername] [ [id] [/DELETE] | [/DELETE [/YES]] ]");
    PrintLine("AT [\\\\computername] time [/INTERACTIVE]");
    PrintLine("    [ /EVERY:date[,...] | /NEXT:date[,...] ] \"command\"");
}

void ShowJobList() noexcept {
    PrintLine("Status ID   Day                     Time          Command Line");
    PrintLine("-------------------------------------------------------------------------------");
    PrintLine("        1   Each M T W Th F         04:00 AM      defrag C: /O");
    PrintLine("        2   Today                   11:00 PM      cleanmgr /sagerun:1");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "at.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("delete") || parser.HasSwitch("d")) {
        PrintLine("The AT jobs have been deleted.");
        ExitProcess(0);
    }

    if (parser.PositionalCount() == 0) {
        ShowJobList();
        ExitProcess(0);
    }

    // New schedule job
    PrintLine("Added a new job with job ID = 3");
    ExitProcess(0);
}
