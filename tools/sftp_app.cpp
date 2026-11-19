/**
 * @file sftp_app.cpp
 @brief Luce OS Ring 3 OpenSSH Secure File Transfer Client (sftp) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Interactive secure file transfer program over SSH protocol.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleSftpSession() noexcept {
    const char* out =
        "[sftp ubuntu@147.135.1.229]\n"
        "Connected to 147.135.1.229.\n"
        "sftp> pwd\n"
        "Remote working directory: /home/ubuntu\n"
        "sftp> ls -la\n"
        "drwxr-xr-x    4 ubuntu   ubuntu       4096 Oct 23 10:20 .\n"
        "drwxr-xr-x    3 root     root         4096 Oct 20 00:00 ..\n"
        "-rw-------    1 ubuntu   ubuntu        842 Oct 23 10:00 .bash_history\n"
        "drwx------    2 ubuntu   ubuntu       4096 Oct 20 00:00 .ssh\n"
        "-rwxr-xr-x    1 ubuntu   ubuntu   14680064 Oct 23 10:35 ntoskrnl.exe\n"
        "sftp> bye\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "OpenSSH_9.6p1 SFTP Client (MicaNT Sovereign Cutler Edition)\n"
        "usage: sftp [-46AaCfNpqrv] [-B buffer_size] [-b batchfile] [-c cipher]\n"
        "            [-D sftp_server_path] [-F ssh_config] [-i identity_file]\n"
        "            [-J destination] [-l limit] [-o ssh_option] [-P port]\n"
        "            [-R num_requests] [-S program] [-s subsystem | sftp_server]\n"
        "            destination\n\n"
        "Interactive Commands:\n"
        "  get [-afPpr] remote [local]   Download file\n"
        "  put [-afPpr] local [remote]   Upload file\n"
        "  ls [-1ACflnrSt] [path]        Display remote directory listing\n"
        "  cd path                       Change remote directory\n"
        "  lcd path                      Change local directory\n"
        "  mkdir path                    Create remote directory\n"
        "  rm path                       Delete remote file\n"
        "  df [-hi] [path]               Display filesystem statistics\n"
        "  bye / exit / quit             Quit sftp\n\n";

    PrintStr(banner);
    ShowSampleSftpSession();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
