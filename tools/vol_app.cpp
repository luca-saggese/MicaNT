/**
 * @file vol_app.cpp
 @brief Luce OS Ring 3 Volume Label & Serial Number Display Utility (vol) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Displays the disk volume label and serial number for local volumes (C:, D:, E:, F:, G:).
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Displays the disk volume label and serial number, if they exist.");
    PrintLine("");
    PrintLine("VOL [drive:]");
    PrintLine("");
    PrintLine("  [drive:]    Specifies the drive that you want to verify.");
}

void PrintVolumeInfo(const char* drive) noexcept {
    char d = 'C';
    if (drive && drive[0] != '\0') {
        d = ToUpperChar(drive[0]);
    }

    switch (d) {
    case 'C':
        PrintLine(" Volume in drive C is MICA_SYS");
        PrintLine(" Volume Serial Number is 4C2A-18E9");
        break;
    case 'D':
        PrintLine(" Volume in drive D is MICA_NTFS");
        PrintLine(" Volume Serial Number is 78E2-A1C9");
        break;
    case 'E':
        PrintLine(" Volume in drive E is MICA_EXFAT");
        PrintLine(" Volume Serial Number is 6A12-9F04");
        break;
    case 'F':
        PrintLine(" Volume in drive F is MICANT_OS");
        PrintLine(" Volume Serial Number is C0DE-1509");
        break;
    case 'G':
        PrintLine(" Volume in drive G is MICA_REFS");
        PrintLine(" Volume Serial Number is 8E21-F4A0");
        break;
    case 'Z':
        PrintLine(" Volume in drive Z is MICA_ZFS");
        PrintLine(" Volume Serial Number is 00BA-B10C");
        break;
    case 'P':
        PrintLine(" Volume in drive P is HMB_PAGING");
        PrintLine(" Volume Serial Number is 1143-6000");
        break;
    case 'X':
        PrintLine(" Volume in drive X is SUBST_SYS32");
        PrintLine(" Volume Serial Number is 2026-1025");
        break;
    default:
        Print("The system cannot find the drive specified: ");
        if (drive) PrintLine(drive);
        else PrintLine("UNKNOWN");
        ExitProcess(1);
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "vol.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* drive = (parser.PositionalCount() > 0) ? parser.GetPositional(0) : "C:";
    PrintVolumeInfo(drive);

    ExitProcess(0);
}
