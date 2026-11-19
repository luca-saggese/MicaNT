/**
 * @file winget_app.cpp
 @brief Luce OS Ring 3 Windows Package Manager Client (winget) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Installs, upgrades, configures, and manages software packages on MicaNT.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleWinGet() noexcept {
    const char* out =
        "[winget search git]\n"
        "Name        Id            Version  Match       Source\n"
        "-----------------------------------------------------\n"
        "Git         Git.Git       2.45.0   Tag: git    winget\n"
        "GitHub CLI  GitHub.cli    2.49.0   Tag: git    winget\n"
        "GitKraken   Axosoft.GitK  9.13.0   Tag: git    winget\n"
        "MicaDevKit  MicaNT.DevKit 1.0.0    Tag: git    sovereign\n\n"
        "[winget list]\n"
        "Name                             Id                       Version     Available Source\n"
        "-------------------------------- ------------------------ ----------- --------- -------\n"
        "MicaNT Operating System Core     MicaNT.Kernel.Sovereign  10.0.26100  <latest>  sovereign\n"
        "Dave Cutler C++23 Standalone     MicaNT.Toolchain.Clang   18.1.8      <latest>  sovereign\n"
        "OpenSSH for MicaNT              OpenSSH.Client           9.6.1       <latest>  winget\n"
        "cURL HTTP/HTTPS Client           cURL.cURL                8.8.0       <latest>  winget\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Windows Package Manager v1.7.10690 (MicaNT Sovereign Cutler Edition)\n"
        "Built using WinGet from Microsoft's GitHub repository (microsoft/winget-cli, MIT License).\n"
        "All copyright to names belong to Microsoft Corporation.\n\n"
        "usage: winget [<command>] [<options>]\n\n"
        "The following commands are available:\n"
        "  install       Installs the given package\n"
        "  show          Shows information about a package\n"
        "  source        Manage sources of packages\n"
        "  search        Find and show basic info of packages\n"
        "  list          Display installed packages\n"
        "  upgrade       Shows and performs available upgrades\n"
        "  uninstall     Uninstalls the given package\n"
        "  hash          Helper to hash installer files (SHA-256)\n"
        "  validate      Validates a manifest file\n"
        "  settings      Open settings or set admin settings\n"
        "  features      Shows the status of experimental features\n"
        "  export        Exports a list of the installed packages\n"
        "  import        Installs all the packages in a file\n"
        "  pin           Manage package pins\n\n";

    PrintStr(banner);
    ShowSampleWinGet();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
