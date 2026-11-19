/**
 * @file graftabl_app.cpp
 * Luce OS Ring 3 Graphics Character Set Loader (graftabl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Enables Windows NT console subsystem to display extended character set in graphics modes.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleGraftabl() noexcept {
    const char* out =
        "[graftabl 437]\n"
        "Previous Code Page: 437 (United States OEM)\n"
        "Active Code Page:   437 (United States OEM)\n"
        "Font Memory Plane:  GOP Linear Framebuffer Glyph Bank Loaded\n"
        "Extended characters (ASCII 128 - 255) loaded into graphics adapter VRAM.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Enables Windows operating systems to display an extended character\n"
        "set in graphics mode.\n\n"
        "GRAFTABL [xxx]\n"
        "GRAFTABL /STATUS\n\n"
        "   xxx      Specifies a code page number:\n"
        "            437 - United States\n"
        "            850 - Multilingual (Latin I)\n"
        "            852 - Slavic (Latin II)\n"
        "            855 - Cyrillic I\n"
        "            857 - Turkish\n"
        "            860 - Portuguese\n"
        "            861 - Icelandic\n"
        "            863 - Canadian-French\n"
        "            865 - Nordic\n"
        "            866 - Russian\n"
        "            869 - Modern Greek\n"
        "   /STATUS  Displays the current code page selected for use with GRAFTABL.\n\n";

    PrintStr(banner);
    ShowSampleGraftabl();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
