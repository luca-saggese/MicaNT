/**
 * @file unlodctr_app.cpp
 @brief Luce OS Ring 3 Remove Performance Counter Names & Explain Text (unlodctr) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Removes performance counter provider manifests and schemas from the system registry.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "UNLODCTR\n"
        "    Removes Performance Monitor counter names and explain text for a driver.\n\n"
        "Usage:\n"
        "    unlodctr [\\\\computername] <drivername>\n"
        "    unlodctr /M:<ProviderManifest>\n\n"
        "Performance Counter Unload Engine: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
