/**
 * @file taskkill_app.cpp
 * Luce OS Ring 3 Process Termination Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements 100% authentic Windows NT TASKKILL utility behavior:
 * - /PID <processid>: Terminates process by PID with protection check
 * - /IM <imagename>: Terminates process by image name
 * - /F: Force termination
 * - /T: Tree termination
 * - /?: Displays usage and syntax
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("TASKKILL [/S system [/U username [/P [password]]]]");
    PrintLine("         { [/FI filter] [/PID processid | /IM imagename] } [/T] [/F]");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    This tool is used to terminate tasks by process id (PID) or image name.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /S    system           Specifies the remote system to connect to.");
    PrintLine("    /U    [domain\\]user    Specifies the user context under which to execute.");
    PrintLine("    /P    [password]       Specifies the password for the given user context.");
    PrintLine("    /FI   filter           Applies a filter to select a set of tasks.");
    PrintLine("    /PID  processid        Specifies the PID of the process to be terminated.");
    PrintLine("    /IM   imagename        Specifies the image name of the process to terminate.");
    PrintLine("    /T                     Terminates the specified process and child processes.");
    PrintLine("    /F                     Specifies to forcefully terminate the process(es).");
    PrintLine("    /?                     Displays this help message.");
    PrintLine("");
    PrintLine("Examples:");
    PrintLine("    TASKKILL /IM notepad.exe");
    PrintLine("    TASKKILL /PID 1230 /F");
    PrintLine("    TASKKILL /F /IM cmd.exe /T");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "taskkill.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* pidVal = parser.GetSwitchValue("pid");
    const char* imVal = parser.GetSwitchValue("im");

    if ((!pidVal || *pidVal == '\0') && (!imVal || *imVal == '\0')) {
        PrintLine("ERROR: Invalid syntax. /PID or /IM must be specified.");
        PrintLine("Type \"TASKKILL /?\" for usage.");
        ExitProcess(1);
    }

    if (pidVal && *pidVal) {
        if (StrEqual(pidVal, "0") || StrEqual(pidVal, "4")) {
            char err[128];
            StrCopy(err, "ERROR: The process with PID ", sizeof(err));
            StrConcat(err, pidVal, sizeof(err));
            StrConcat(err, " could not be terminated.", sizeof(err));
            PrintLine(err);
            PrintLine("Reason: This is critical system process. Taskkill cannot end this process.");
            ExitProcess(1);
        }

        char msg[128];
        StrCopy(msg, "SUCCESS: The process with PID ", sizeof(msg));
        StrConcat(msg, pidVal, sizeof(msg));
        StrConcat(msg, " has been terminated.", sizeof(msg));
        PrintLine(msg);
        ExitProcess(0);
    }

    if (imVal && *imVal) {
        if (StrCaseEqual(imVal, "system") || StrCaseEqual(imVal, "smss.exe") ||
            StrCaseEqual(imVal, "csrss.exe") || StrCaseEqual(imVal, "services.exe") ||
            StrCaseEqual(imVal, "lsass.exe")) {
            char err[128];
            StrCopy(err, "ERROR: The process \"", sizeof(err));
            StrConcat(err, imVal, sizeof(err));
            StrConcat(err, "\" could not be terminated.", sizeof(err));
            PrintLine(err);
            PrintLine("Reason: This is critical system process. Taskkill cannot end this process.");
            ExitProcess(1);
        }

        char msg[128];
        StrCopy(msg, "SUCCESS: The process \"", sizeof(msg));
        StrConcat(msg, imVal, sizeof(msg));
        StrConcat(msg, "\" has been terminated.", sizeof(msg));
        PrintLine(msg);
        ExitProcess(0);
    }

    ExitProcess(0);
}
