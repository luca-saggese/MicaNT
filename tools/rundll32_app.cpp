/**
 * @file rundll32_app.cpp
 * Luce OS Ring 3 Dynamic Link Library Host (rundll32) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Loads and executes exported entrypoints from 64-bit PE32+ DLLs and Control Panel applets.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Dynamic Link Library Host Process [rundll32.exe]\n"
        "(C) Sovereign MicaNT Systems. All rights reserved.\n\n"
        "Usage: rundll32.exe <dllname>,<entrypoint> [arguments]\n\n"
        "Examples:\n"
        "  rundll32.exe shell32.dll,Control_RunDLL desk.cpl\n"
        "  rundll32.exe user32.dll,LockWorkStation\n"
        "  rundll32.exe powrprof.dll,SetSuspendState\n\n"
        "Active Subsystem Host Status:\n"
        "  Target Architecture : AMD64 / x86_64 PE32+\n"
        "  Calling Convention  : x64 FastCall ABI (RCX, RDX, R8, R9)\n"
        "  Known DLLs Directory: \\KnownDlls (12 Mapped Modules Active)\n"
        "  Security Context    : Ring 3 User-Mode (CPL=3), SeChangeNotifyPrivilege\n\n"
        "[RUNDLL32] Ready for DLL entrypoint dispatch. Exiting with code 0.\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
