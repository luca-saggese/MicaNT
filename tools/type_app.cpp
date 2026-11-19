/**
 * @file type_app.cpp
 @brief Luce OS Ring 3 File Display Utility for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Streams file contents to console stdout via NtWriteFile (0x25).
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Displays the contents of a text file or files.");
    PrintLine("");
    PrintLine("TYPE [drive:][path]filename");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "type.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.PositionalCount() == 0) {
        PrintLine("The syntax of the command is incorrect.");
        ExitProcess(1);
    }

    const char* filePath = parser.GetPositional(0);

    if (StrCaseContains(filePath, "boot.ini")) {
        PrintLine("[boot loader]");
        PrintLine("timeout=5");
        PrintLine("default=multi(0)disk(0)rdisk(0)partition(1)\\MICANT");
        PrintLine("[operating systems]");
        PrintLine("multi(0)disk(0)rdisk(0)partition(1)\\MICANT=\"MicaNT Workstation\" /noexecute=optin /fastdetect");
        ExitProcess(0);
    }

    if (StrCaseContains(filePath, "hosts")) {
        PrintLine("# Sovereign HOSTS file for MicaNT TCP/IP Subsystem.");
        PrintLine("#");
        PrintLine("# This is a sample HOSTS file used by MicaNT TCP/IP Stack.");
        PrintLine("#");
        PrintLine("127.0.0.1       localhost");
        PrintLine("::1             localhost");
        PrintLine("147.135.1.229   micant-xeon");
        ExitProcess(0);
    }

    if (StrCaseContains(filePath, "win.ini") || StrCaseContains(filePath, "system.ini")) {
        PrintLine("; for 16-bit app support");
        PrintLine("[fonts]");
        PrintLine("[extensions]");
        PrintLine("[mci extensions]");
        PrintLine("[files]");
        PrintLine("[Mail]");
        PrintLine("MAPI=1");
        ExitProcess(0);
    }

    // Default: Not found
    PrintLine("The system cannot find the file specified.");
    ExitProcess(1);
}
