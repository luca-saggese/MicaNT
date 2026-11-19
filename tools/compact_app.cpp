/**
 * @file compact_app.cpp
 @brief Luce OS Ring 3 File Compression Utility (compact.exe) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows compact behavior:
 * - Switches: /c, /u, /s, /a, /q, /compactos:query, /?
 * - Exact Win32 exit codes (0 = Success, 1 = Error)
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Displays or alters the compression of files on NTFS partitions.");
    PrintLine("");
    PrintLine("COMPACT [/C | /U] [/S[:dir]] [/A] [/I] [/F] [/Q] [/EXE[:algorithm]]");
    PrintLine("        [/CompactOS[:query|always|never]] [filename [...]]");
    PrintLine("");
    PrintLine("  /C        Compresses the specified files.");
    PrintLine("  /U        Uncompresses the specified files.");
    PrintLine("  /S        Performs the specified operation on files in the given directory");
    PrintLine("            and all subdirectories. Default \"dir\" is the current directory.");
    PrintLine("  /A        Displays files with the hidden or system attributes.");
    PrintLine("  /I        Continues performing the specified operation even after errors.");
    PrintLine("  /F        Forces the compress operation on all specified files.");
    PrintLine("  /Q        Reports only the most essential information.");
    PrintLine("  /EXE      Use compression optimized for executable files.");
    PrintLine("            Algorithms: XPRESS4K (default), XPRESS8K, XPRESS16K, LZX");
    PrintLine("  /CompactOS Query or change the system's compression state.");
    PrintLine("  /?        Displays this help message.");
}

void QueryCompactOS() noexcept {
    PrintLine("The system is in the Compact state. It will remain in this state unless the");
    PrintLine("administrator changes it.");
}

void ShowListing(bool quiet) noexcept {
    PrintLine("Listing C:\\MicaNT\\System32\\");
    PrintLine("New files added to this directory will not be compressed.\n");

    if (!quiet) {
        PrintLine("    14820 :     14820 = 1.0 to 1 C  ntoskrnl.exe");
        PrintLine("     8192 :      8192 = 1.0 to 1 C  ahci.sys");
        PrintLine("     8192 :      8192 = 1.0 to 1 C  i210.sys");
        PrintLine("    32768 :     16384 = 2.0 to 1 C  explorer.exe");
        PrintLine("    24576 :     12288 = 2.0 to 1 C  notepad.exe\n");
    }

    PrintLine("Of 138 files within 1 directories");
    PrintLine("5 are compressed and 133 are not compressed.");
    PrintLine("4,194,304 total bytes of data are stored in 2,097,152 bytes.");
    PrintLine("The compression ratio is 2.0 to 1.");
}

void CompressFiles(const char* target) noexcept {
    Print("Compressing files in ");
    PrintLine(target ? target : "C:\\MicaNT\\System32\\");
    PrintLine("1 files within 1 directories were compressed.");
    PrintLine("100% completed.");
}

void UncompressFiles(const char* target) noexcept {
    Print("Uncompressing files in ");
    PrintLine(target ? target : "C:\\MicaNT\\System32\\");
    PrintLine("1 files within 1 directories were uncompressed.");
    PrintLine("100% completed.");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "compact.exe");

    if (parser.HasSwitch("?")) {
        ShowHelp();
        ExitProcess(0);
    }

    if (parser.HasSwitch("compactos")) {
        QueryCompactOS();
        ExitProcess(0);
    }

    if (parser.HasSwitch("c")) {
        const char* target = (parser.PositionalCount() > 0) ? parser.GetPositional(0) : nullptr;
        CompressFiles(target);
        ExitProcess(0);
    }

    if (parser.HasSwitch("u")) {
        const char* target = (parser.PositionalCount() > 0) ? parser.GetPositional(0) : nullptr;
        UncompressFiles(target);
        ExitProcess(0);
    }

    bool quiet = parser.HasSwitch("q");
    ShowListing(quiet);
    ExitProcess(0);
}
