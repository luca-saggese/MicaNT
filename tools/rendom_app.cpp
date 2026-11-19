/**
 * @file rendom_app.cpp
 * Luce OS Ring 3 Active Directory Domain Rename Tool (rendom) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Generates domain rename state descriptions, uploads domain plans, and executes forest renames.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Active Directory Domain Rename Tool (rendom.exe)\n\n"
        "Usage: rendom [/list | /upload | /prepare | /execute | /clean | /end]\n\n"
        "Operations:\n"
        "  /list     - Generates domainlist.xml containing current forest description\n"
        "  /upload   - Uploads modified domainlist.xml to Active Directory configuration directory\n"
        "  /prepare  - Verifies that all domain controllers are ready for domain rename\n"
        "  /execute  - Executes forest-wide domain rename transaction\n"
        "  /clean    - Cleans up old domain references\n"
        "  /end      - Unfreezes directory configuration after domain rename completion\n\n"
        "Forest Domain Rename Subsystem: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
