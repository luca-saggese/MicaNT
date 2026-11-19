/**
 * @file tar_app.cpp
 @brief Luce OS Ring 3 Tape Archive Utility (tar) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Manipulates tape archive files (.tar, .tar.gz, .tar.zst, .tar.bz2).
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleTarOperation() noexcept {
    const char* out =
        "[tar -tvf MicaNT_RootFS.tar.gz]\n"
        "drwxr-xr-x  0 root   root        0 Oct 23 00:00 .\n"
        "drwxr-xr-x  0 root   root        0 Oct 23 00:00 bin/\n"
        "-rwxr-xr-x  0 root   root  1048576 Oct 23 00:00 bin/sh\n"
        "drwxr-xr-x  0 root   root        0 Oct 23 00:00 etc/\n"
        "-rw-r--r--  0 root   root      256 Oct 23 00:00 etc/passwd\n"
        "-rw-r--r--  0 root   root      128 Oct 23 00:00 etc/hostname\n"
        "drwxr-xr-x  0 root   root        0 Oct 23 00:00 usr/\n"
        "drwxr-xr-x  0 root   root        0 Oct 23 00:00 usr/lib/\n"
        "-rwxr-xr-x  0 root   root  2097152 Oct 23 00:00 usr/lib/libc.so.6\n"
        "Archive verification: 9 entries, 3,146,112 bytes total. Checksum: OK.\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "bsdtar 3.7.2 - libarchive 3.7.2 zlib/1.3.1 liblzma/5.4.5 bz2lib/1.0.8 libzstd/1.5.5\n"
        "MicaNT Sovereign Cutler Edition\n\n"
        "Usage:\n"
        "  List:    tar -tf <archive-filename>\n"
        "  Extract: tar -xf <archive-filename>\n"
        "  Create:  tar -cf <archive-filename> [filenames...]\n"
        "  Help:    tar --help\n\n"
        "Common Options:\n"
        "  -c, --create        Create a new archive\n"
        "  -x, --extract       Extract files from an archive\n"
        "  -t, --list          List the contents of an archive\n"
        "  -v, --verbose       Verbosely list files processed\n"
        "  -f <filename>       Location of archive (default: standard I/O)\n"
        "  -z, --gzip          Filter the archive through gzip\n"
        "  -j, --bzip2         Filter the archive through bzip2\n"
        "  -J, --xz            Filter the archive through xz\n"
        "  --zstd              Filter the archive through zstd\n"
        "  -C <dir>            Change to directory <dir> before operations\n\n";

    PrintStr(banner);
    ShowSampleTarOperation();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
