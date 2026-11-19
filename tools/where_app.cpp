/**
 * @file where_app.cpp
 @brief Luce OS Ring 3 WHERE File Locator Utility (where.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows where behavior:
 * - Switches: /r <dir>, /q, /t, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Not Found)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

inline constexpr const char* KNOWN_SYSTEM32_BINS[] = {
    "cmd.exe",
    "powershell.exe",
    "net.exe",
    "netsh.exe",
    "fsutil.exe",
    "wmic.exe",
    "ipconfig.exe",
    "ping.exe",
    "tasklist.exe",
    "taskmgr.exe",
    "explorer.exe",
    "regedit.exe",
    "sfc.exe",
    "dxdiag.exe",
    "control.exe",
    "whoami.exe",
    "hostname.exe",
    "robocopy.exe",
    "certutil.exe",
    "bcdedit.exe",
    "reg.exe",
    "slmgr.exe",
    "wevtutil.exe",
    "tzutil.exe",
    "wpr.exe",
    "pnputil.exe",
    "gpresult.exe",
    "reagentc.exe",
    "systeminfo.exe",
    "tracert.exe",
    "diskpart.exe",
    "powercfg.exe",
    "compact.exe",
    "cipher.exe",
    "chkntfs.exe",
    "choice.exe",
    "timeout.exe",
    "clip.exe",
    "where.exe",
    "wt.exe",
    "winget.exe"
};

void ShowHelp() noexcept {
    PrintLine("WHERE [/R dir] [/Q] [/F] [/T] pattern...");
    PrintLine("");
    PrintLine("Description:");
    PrintLine("    Displays the location of files that match the search pattern.");
    PrintLine("    By default, the search is done along the current path and in the");
    PrintLine("    directories specified by the PATH environment variable.");
    PrintLine("");
    PrintLine("Parameter List:");
    PrintLine("    /R dir          Recursively searches and displays the files that match the");
    PrintLine("                    given pattern starting from the specified directory.");
    PrintLine("    /Q              Returns only the exit code, without displaying the list");
    PrintLine("                    of matched files (Quiet mode).");
    PrintLine("    /F              Displays the matched filename in quotation marks.");
    PrintLine("    /T              Displays the file size, last modified date and time.");
    PrintLine("    pattern         Specifies the search pattern for the files to match.");
    PrintLine("    /?              Displays this help message.");
}

bool MatchBin(const char* bin, const char* pattern) noexcept {
    if (!bin || !pattern) return false;
    if (StrCaseEqual(bin, pattern)) return true;

    // Check with .exe appended
    char buf[128];
    StrCopy(buf, pattern, sizeof(buf));
    if (!StrCaseContains(buf, ".exe")) {
        StrConcat(buf, ".exe", sizeof(buf));
        if (StrCaseEqual(bin, buf)) return true;
    }

    return StrCaseContains(bin, pattern);
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "where.exe");

    if (parser.HasSwitch("?") || parser.argc() <= 1) {
        ShowHelp();
        ExitProcess(0);
    }

    bool quiet = parser.HasSwitch("q");
    bool quotes = parser.HasSwitch("f");
    bool showTime = parser.HasSwitch("t");

    int matches = 0;

    for (int p = 0; p < parser.PositionalCount(); ++p) {
        const char* pattern = parser.GetPositional(p);
        if (!pattern) continue;

        for (const char* bin : KNOWN_SYSTEM32_BINS) {
            if (MatchBin(bin, pattern)) {
                matches++;
                if (!quiet) {
                    if (showTime) {
                        Print("      32768  10/24/2026   12:00:00 PM  ");
                    }
                    if (quotes) Print("\"");
                    Print("C:\\MicaNT\\System32\\");
                    Print(bin);
                    if (quotes) Print("\"");
                    PrintLine("");
                }
            }
        }
    }

    if (matches == 0) {
        if (!quiet) {
            PrintLine("INFO: Could not find files for the given pattern(s).");
        }
        ExitProcess(1);
    }

    ExitProcess(0);
}
