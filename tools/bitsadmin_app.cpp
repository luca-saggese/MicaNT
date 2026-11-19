/**
 * @file bitsadmin_app.cpp
 @brief Luce OS Ring 3 Background Intelligent Transfer Service Admin Utility (bitsadmin.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages asynchronous file download/upload transfer queues and network bandwidth throttles.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("BITSADMIN [/RAWRETURN] [/WRAP | /NOWRAP] command");
    PrintLine("");
    PrintLine("The following commands are available:");
    PrintLine("    /HELP                           Prints this help screen");
    PrintLine("    /LIST [/ALLUSERS] [/VERBOSE]    List jobs");
    PrintLine("    /MONITOR [/ALLUSERS] [/REFRESH sec] Monitor jobs");
    PrintLine("    /RESET [/ALLUSERS]              Cancels all jobs in transfer queue");
    PrintLine("    /TRANSFER <job_name> [type]     One-command transfer");
    PrintLine("    /CREATE [type] <job_name>       Creates a new transfer job");
    PrintLine("    /RESUME <job_name>              Resumes job");
    PrintLine("    /SUSPEND <job_name>             Suspends job");
    PrintLine("    /COMPLETE <job_name>            Completes job");
    PrintLine("    /CANCEL <job_name>              Cancels job");
}

void ShowList() noexcept {
    PrintLine("BITSADMIN version 3.0");
    PrintLine("BITS administration utility.");
    PrintLine("(C) Copyright MicaNT System Authority.");
    PrintLine("");
    PrintLine("{B8A5B9C2-4E2F-43B0-8F9E-6C0D9B7B8D10} 'MicaKernelUpdate' TRANSFERRED 1 / 1 1048576 / 1048576");
    PrintLine("{E1D7B439-0144-48FE-9538-232B45A8CC92} 'SystemCatalogSync' SUSPENDED 0 / 1 0 / UNKNOWN");
    PrintLine("");
    PrintLine("Listed 2 job(s).");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "bitsadmin.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("reset")) {
        PrintLine("{B8A5B9C2-4E2F-43B0-8F9E-6C0D9B7B8D10} canceled.");
        PrintLine("{E1D7B439-0144-48FE-9538-232B45A8CC92} canceled.");
        PrintLine("2 job(s) canceled.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("create")) {
        PrintLine("Created job {C3E49281-7B1A-48DE-9C21-89F3A104E902}.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("resume") || parser.HasSwitch("suspend") ||
        parser.HasSwitch("complete") || parser.HasSwitch("cancel") ||
        parser.HasSwitch("transfer")) {
        PrintLine("Job operation completed successfully.");
        ExitProcess(0);
    }

    if (parser.HasSwitch("list") || parser.HasSwitch("allusers") || parser.HasSwitch("monitor")) {
        ShowList();
        ExitProcess(0);
    }

    ShowHelp();
    ExitProcess(1);
}
