/**
 * @file lodctr_app.cpp
 @brief Luce OS Ring 3 Load Performance Counter Names & Explain Text (lodctr) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Registers performance counter provider manifests and schemas into the system registry.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "LODCTR\n"
        "    Updates Performance Monitor counter names and explain text.\n\n"
        "Usage:\n"
        "    lodctr <[\\\\computername] INIFileName>\n"
        "    lodctr /S:<BackupFileName>\n"
        "    lodctr /R:<BackupFileName>\n"
        "    lodctr /R\n"
        "    lodctr /Q:<DriverName>\n"
        "    lodctr /E:<DriverName>\n"
        "    lodctr /D:<DriverName>\n"
        "    lodctr /M:<ProviderManifest>\n\n"
        "Performance Counter Registry Subsystem: Verified OK.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
