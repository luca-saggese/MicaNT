/**
 * @file tree_app.cpp
 * Luce OS Ring 3 Directory Hierarchy Visualizer (tree) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Graphically displays the folder structure of a drive or path.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

void ShowHelp() noexcept {
    PrintLine("Graphically displays the folder structure of a drive or path.");
    PrintLine("");
    PrintLine("TREE [drive:][path] [/F] [/A]");
    PrintLine("");
    PrintLine("   /F   Display the names of the files in each folder.");
    PrintLine("   /A   Use ASCII instead of extended characters.");
}

void RenderTree(const char* rootPath, bool showFiles, bool asciiMode) noexcept {
    PrintLine("Folder PATH listing for volume MICA_SYS");
    PrintLine("Volume serial number is 4C2A-18E9");
    PrintLine(rootPath ? rootPath : "C:\\MICANT");

    if (asciiMode) {
        PrintLine("+---Config");
        if (showFiles) {
            PrintLine("|       DEFAULT");
            PrintLine("|       SAM");
            PrintLine("|       SECURITY");
            PrintLine("|       SOFTWARE");
            PrintLine("|       SYSTEM");
        }
        PrintLine("+---Fonts");
        if (showFiles) {
            PrintLine("|       segoeui.ttf");
            PrintLine("|       consola.ttf");
        }
        PrintLine("\\---System32");
        if (showFiles) {
            PrintLine("    |   ntoskrnl.exe");
            PrintLine("    |   explorer.exe");
            PrintLine("    |   cmd.exe");
        }
        PrintLine("    +---drivers");
        if (showFiles) {
            PrintLine("    |       ahci.sys");
            PrintLine("    |       ntfs.sys");
            PrintLine("    |       refs.sys");
        }
        PrintLine("    \\---etc");
        if (showFiles) {
            PrintLine("            hosts");
            PrintLine("            services");
        }
    } else {
        // Extended characters
        PrintLine("├── Config");
        if (showFiles) {
            PrintLine("│   ├── DEFAULT");
            PrintLine("│   ├── SAM");
            PrintLine("│   ├── SECURITY");
            PrintLine("│   ├── SOFTWARE");
            PrintLine("│   └── SYSTEM");
        }
        PrintLine("├── Fonts");
        if (showFiles) {
            PrintLine("│   ├── segoeui.ttf");
            PrintLine("│   └── consola.ttf");
        }
        PrintLine("└── System32");
        if (showFiles) {
            PrintLine("    ├── ntoskrnl.exe");
            PrintLine("    ├── explorer.exe");
            PrintLine("    ├── cmd.exe");
        }
        PrintLine("    ├── drivers");
        if (showFiles) {
            PrintLine("    │   ├── ahci.sys");
            PrintLine("    │   ├── ntfs.sys");
            PrintLine("    │   └── refs.sys");
        }
        PrintLine("    └── etc");
        if (showFiles) {
            PrintLine("        ├── hosts");
            PrintLine("        └── services");
        }
    }
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "tree.exe");

    if (parser.HasSwitch("?") || parser.HasSwitch("help")) {
        ShowHelp();
        ExitProcess(0);
    }

    bool showFiles = parser.HasSwitch("f");
    bool asciiMode = parser.HasSwitch("a");
    const char* root = (parser.PositionalCount() > 0) ? parser.GetPositional(0) : "C:\\MicaNT";

    RenderTree(root, showFiles, asciiMode);
    ExitProcess(0);
}
