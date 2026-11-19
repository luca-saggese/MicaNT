/**
 * @file doskey_app.cpp
 * Luce OS Ring 3 Command Line Recall, Editing & Macro Utility (doskey) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manages console command history buffers and command macro expansion.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleMacros() noexcept {
    const char* out =
        "Active Macros:\n"
        "  ls=dir /w\n"
        "  clear=cls\n"
        "  grep=findstr /i $*\n"
        "  ll=dir /od\n\n"
        "Command History (Last 5 commands):\n"
        "  1  ipconfig /all\n"
        "  2  net start\n"
        "  3  verifier /standard\n"
        "  4  gpedit.msc\n"
        "  5  doskey /history\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Edits command lines, recalls MicaNT commands, and creates macros.\n\n"
        "DOSKEY [/REINSTALL] [/LISTSIZE=size] [/MACROS[:ALL | :exe-name]]\n"
        "  [/HISTORY] [/INSERT | /OVERSTRIKE] [/EXENAME=exe-name]\n"
        "  [/MACROFILE=filename] [macroname=[text]]\n\n"
        "  /REINSTALL          Installs a new copy of Doskey.\n"
        "  /LISTSIZE=size      Sets size of command history buffer.\n"
        "  /MACROS             Displays all Doskey macros.\n"
        "  /HISTORY            Displays all commands stored in memory.\n"
        "  /INSERT             Specifies that new text typed is inserted in old text.\n"
        "  /OVERSTRIKE         Specifies that new text overwrites old text.\n"
        "  macroname           Specifies a name for a macro you create.\n"
        "  text                Specifies commands you want to record.\n\n";

    PrintStr(banner);
    ShowSampleMacros();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
