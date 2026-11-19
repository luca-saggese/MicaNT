/**
 * @file append_app.cpp
 * Luce OS Ring 3 Data Directory Search Appender (append) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Enables programs to open data files in specified directories as if they were in current directory.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleAppend() noexcept {
    const char* out =
        "[append C:\\MICANT\\System32;C:\\Data\\Templates /x:on /path:on]\n"
        "APPEND search list active:\n"
        "  1. C:\\MICANT\\System32\n"
        "  2. C:\\Data\\Templates\n"
        "Settings:\n"
        "  /X:ON    Appended directories applied to file searches and application execution.\n"
        "  /PATH:ON Search appended directories even if path specified in file request.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Allows programs to open data files in specified directories as if they were in the\n"
        "current directory.\n\n"
        "APPEND [[drive:]path[;...]] [/X[:ON | :OFF]] [/PATH:ON | /PATH:OFF] [/E]\n"
        "APPEND ;\n\n"
        "  [drive:]path Specifies a drive and directory to append.\n"
        "  /X:ON        Applies appended directories to file searches and\n"
        "               application execution.\n"
        "  /X:OFF       Applies appended directories only to requests to open files.\n"
        "               /X:OFF is the default setting.\n"
        "  /PATH:ON     Applies appended directories to file requests that already\n"
        "               specify a path. /PATH:ON is the default setting.\n"
        "  /PATH:OFF    Turns off the effect of /PATH:ON.\n"
        "  /E           Stores a copy of the appended directory list in an environment\n"
        "               variable named APPEND. /E may be used only the first time\n"
        "               you use APPEND after starting your system.\n"
        "  ;            Clears the appended directory list.\n\n";

    PrintStr(banner);
    ShowSampleAppend();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
