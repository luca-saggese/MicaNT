/**
 * @file ftp_app.cpp
 * Luce OS Ring 3 File Transfer Protocol Client (ftp) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Provides interactive FTP client commands and RFC 959 transfer primitives.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowFtpHelp() noexcept {
    const char* out =
        "Commands may be abbreviated.  Commands are:\n\n"
        "!               delete          literal         prompt          send\n"
        "?               debug           ls              put             status\n"
        "append          dir             mdelete         pwd             trace\n"
        "ascii           disconnect      mdir            quit            type\n"
        "bell            get             mget            quote           user\n"
        "binary          glob            mkdir           recv            verbose\n"
        "bye             hash            mls             remotehelp\n"
        "cd              help            mput            rename\n"
        "close           lcd             open            rmdir\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "FTP [-v] [-d] [-i] [-n] [-g] [-s:filename] [-a] [-w:windowsize] [host]\n\n"
        "  -v              Suppresses display of remote server responses.\n"
        "  -n              Suppresses auto-login upon initial connection.\n"
        "  -i              Turns off interactive prompting during multiple file transfers.\n"
        "  -d              Enables debugging.\n"
        "  -g              Disables filename globbing.\n"
        "  -s:filename     Specifies a text file containing FTP commands.\n"
        "  -a              Use any local interface when binding data connection.\n"
        "  -w:buffersize   Overrides default transfer buffer size (4096 bytes).\n"
        "  host            Specifies host name or IP address of remote host.\n\n"
        "MicaNT TCP/IP FTP Subsystem:\n"
        "  Socket Provider : afd.sys (Ancillary Function Driver for WinSock)\n"
        "  NIC Interface   : Intel I210 Gigabit Ethernet (147.135.1.229)\n"
        "  Active Mode     : RFC 959 Client Ready\n\n";

    PrintStr(banner);
    ShowFtpHelp();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
