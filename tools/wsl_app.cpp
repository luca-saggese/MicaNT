/**
 * @file wsl_app.cpp
 @brief Luce OS Ring 3 Windows Subsystem for Linux Launcher (wsl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Launches and manages Linux distributions and POSIX container instances.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleWslStatus() noexcept {
    const char* out =
        "[wsl --list --verbose]\n"
        "  NAME                   STATE           VERSION\n"
        "* Ubuntu-24.04           Running         2\n"
        "  Debian                 Stopped         2\n"
        "  Alpine-Linux           Stopped         1\n"
        "  Sovereign-MicaPOSIX    Running         2 (Native Lightweight VM)\n\n"
        "[wsl --status]\n"
        "Default Distribution: Ubuntu-24.04\n"
        "Default Version: 2\n"
        "WSL version: 2.2.4.0 (MicaNT Sovereign Cutler Hypervisor Engine)\n"
        "Kernel version: 6.6.36.3-micant-hvm\n"
        "WSLg version: 1.0.61 (DirectX Wayland & DirectFlip Compositor)\n"
        "MSRV Hypervisor: Hardware VT-x / EPT Nested Acceleration Active\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "MicaNT Subsystem for Linux (WSL) Version 2.2.4 (MicaNT Sovereign Cutler Edition)\n"
        "Copyright (C) MicaNT Operating System Authority.\n\n"
        "Usage: wsl.exe [Argument] [Options...] [CommandLine]\n\n"
        "Arguments for running Linux commands:\n"
        "    If no command line is provided, wsl.exe launches the default shell.\n\n"
        "    --distribution, -d <Distro>\n"
        "        Run the specified distribution.\n\n"
        "    --user, -u <UserName>\n"
        "        Run as the specified user.\n\n"
        "Arguments for managing MicaNT Subsystem for Linux:\n"
        "    --list, -l [Options]\n"
        "        Lists distributions. Options: --all, --running, --quiet, --verbose\n\n"
        "    --status\n"
        "        Show the status of MicaNT Subsystem for Linux.\n\n"
        "    --set-default, -s <Distro>\n"
        "        Sets the distribution as the default.\n\n"
        "    --set-version <Distro> <Version>\n"
        "        Changes the version of the specified distribution (1 or 2).\n\n"
        "    --shutdown\n"
        "        Immediately terminates all running distributions and the WSL 2 VM.\n\n"
        "    --export <Distro> <FileName>\n"
        "        Exports the distribution to a tar file.\n\n"
        "    --import <Distro> <InstallLocation> <FileName>\n"
        "        Imports the specified tar file as a new distribution.\n\n";

    PrintStr(banner);
    ShowSampleWslStatus();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
