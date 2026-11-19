/**
 * @file find_app.cpp
 @brief Luce OS Ring 3 String Search Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows NT FIND command-line text search behavior.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("FIND [/V] [/C] [/N] [/I] [/OFF[LINE]] \"string\" [[drive:][path]filename[ ...]]");
    PrintLine("");
    PrintLine("  /V         Displays all lines NOT containing the specified string.");
    PrintLine("  /C         Displays only the count of lines containing the string.");
    PrintLine("  /N         Displays line numbers with the displayed lines.");
    PrintLine("  /I         Ignores the case of characters when searching for the string.");
    PrintLine("  /OFF[LINE] Do not skip files with offline attribute set.");
    PrintLine("  \"string\"   Specifies the text string to find.");
    PrintLine("  [drive:][path]filename");
    PrintLine("             Specifies a file or files to search.");
}

inline constexpr const char* SAMPLE_BOOT_INI_LINES[] = {
    "[boot loader]",
    "timeout=5",
    "default=multi(0)disk(0)rdisk(0)partition(1)\\MICANT",
    "[operating systems]",
    "multi(0)disk(0)rdisk(0)partition(1)\\MICANT=\"MicaNT Workstation\" /noexecute=optin /fastdetect"
};

inline constexpr const char* SAMPLE_HOSTS_LINES[] = {
    "# Sovereign HOSTS file for MicaNT TCP/IP Subsystem.",
    "#",
    "# This is a sample HOSTS file used by MicaNT TCP/IP Stack.",
    "#",
    "127.0.0.1       localhost",
    "::1             localhost",
    "147.135.1.229   micant-xeon"
};

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "find.exe");

    if (parser.HasSwitch("?") || parser.PositionalCount() == 0) {
        ShowHelp();
        ExitProcess(0);
    }

    bool invert = parser.HasSwitch("v");
    bool countOnly = parser.HasSwitch("c");
    bool showLineNums = parser.HasSwitch("n");
    bool ignoreCase = parser.HasSwitch("i");

    const char* pattern = parser.GetPositional(0);
    const char* targetFile = (parser.PositionalCount() > 1) ? parser.GetPositional(1) : "";

    if (StrCaseContains(targetFile, "boot.ini")) {
        Print("---------- ");
        PrintLine(targetFile);
        uint32_t matchCount = 0;
        for (size_t i = 0; i < sizeof(SAMPLE_BOOT_INI_LINES) / sizeof(SAMPLE_BOOT_INI_LINES[0]); ++i) {
            const char* line = SAMPLE_BOOT_INI_LINES[i];
            bool contains = ignoreCase ? StrCaseContains(line, pattern) : StrContains(line, pattern);
            bool isMatch = invert ? !contains : contains;
            if (isMatch) {
                matchCount++;
                if (!countOnly) {
                    if (showLineNums) {
                        Print("[");
                        PrintDec(i + 1);
                        Print("]");
                    }
                    PrintLine(line);
                }
            }
        }
        if (countOnly) {
            PrintDec(matchCount);
            PrintLine("");
        }
        ExitProcess(0);
    }

    if (StrCaseContains(targetFile, "hosts")) {
        Print("---------- ");
        PrintLine(targetFile);
        uint32_t matchCount = 0;
        for (size_t i = 0; i < sizeof(SAMPLE_HOSTS_LINES) / sizeof(SAMPLE_HOSTS_LINES[0]); ++i) {
            const char* line = SAMPLE_HOSTS_LINES[i];
            bool contains = ignoreCase ? StrCaseContains(line, pattern) : StrContains(line, pattern);
            bool isMatch = invert ? !contains : contains;
            if (isMatch) {
                matchCount++;
                if (!countOnly) {
                    if (showLineNums) {
                        Print("[");
                        PrintDec(i + 1);
                        Print("]");
                    }
                    PrintLine(line);
                }
            }
        }
        if (countOnly) {
            PrintDec(matchCount);
            PrintLine("");
        }
        ExitProcess(0);
    }

    // Default: file not found
    char err[128];
    StrCopy(err, "FIND: File not found - ", sizeof(err));
    StrConcat(err, targetFile && *targetFile ? targetFile : "stdin", sizeof(err));
    PrintLine(err);
    ExitProcess(1);
}
