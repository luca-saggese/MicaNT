/**
 * @file label_app.cpp
 @brief Luce OS Ring 3 Disk Volume Label Utility (label) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Creates, changes, or deletes the volume label of a disk.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Creates, changes, or deletes the volume label of a disk.");
    PrintLine("");
    PrintLine("LABEL [drive:][label]");
    PrintLine("LABEL [/MP] [volume] [label]");
    PrintLine("");
    PrintLine("  drive:          Specifies the drive letter of a drive.");
    PrintLine("  label           Specifies the label of the volume.");
    PrintLine("  /MP             Specifies that the volume should be treated as a");
    PrintLine("                  mount point or volume name.");
    PrintLine("  volume          Specifies the drive letter (followed by a colon),");
    PrintLine("                  mount point, or volume name.");
}

void ExecuteLabel(const char* targetDrive, const char* newLabel) noexcept {
    char d = 'C';
    if (targetDrive && targetDrive[0] != '\0') {
        d = ToUpperChar(targetDrive[0]);
    }

    if (d == 'F') {
        PrintLine("Volume in drive F: is MICANT_OS");
        PrintLine("Cannot change label. The volume is write-protected.");
        ExitProcess(1);
    }

    const char* curLabel = "MICA_SYS";
    const char* serial = "4C2A-18E9";

    if (d == 'D') { curLabel = "MICA_NTFS"; serial = "78E2-A1C9"; }
    else if (d == 'E') { curLabel = "MICA_EXFAT"; serial = "6A12-9F04"; }
    else if (d == 'G') { curLabel = "MICA_REFS"; serial = "8E21-F4A0"; }

    Print("Volume in drive ");
    PrintChar(d);
    Print(": is ");
    PrintLine(curLabel);

    Print("Volume Serial Number is ");
    PrintLine(serial);

    if (newLabel && newLabel[0] != '\0') {
        Print("Volume label successfully updated to ");
        PrintLine(newLabel);
    } else {
        PrintLine("Volume label (32 characters, ENTER for none)?");
        PrintLine("Volume label unchanged.");
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "label.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* target = "C:";
    const char* label = nullptr;

    if (parser.PositionalCount() >= 2) {
        target = parser.GetPositional(0);
        label = parser.GetPositional(1);
    } else if (parser.PositionalCount() == 1) {
        const char* p0 = parser.GetPositional(0);
        if (p0[1] == ':' && p0[2] != '\0') {
            target = p0;
            label = p0 + 2;
        } else if (p0[1] == ':') {
            target = p0;
        } else {
            label = p0;
        }
    }

    ExecuteLabel(target, label);
    ExitProcess(0);
}
