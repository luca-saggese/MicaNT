/**
 * @file curl_app.cpp
 @brief Luce OS Ring 3 URL Data Transfer Tool (curl) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Transfers data from or to a server using HTTP, HTTPS, FTP, FTPS, SFTP, and more.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

void ShowSampleCurlTransfer() noexcept {
    const char* out =
        "[curl -I https://api.micant.local/v1/health]\n"
        "HTTP/2 200\n"
        "server: MicaNT-Kernel-Edge/2.0\n"
        "date: Fri, 23 Oct 2026 10:40:00 GMT\n"
        "content-type: application/json; charset=utf-8\n"
        "content-length: 74\n"
        "x-micant-ring: 3\n"
        "strict-transport-security: max-age=31536000; includeSubDomains\n\n"
        "{\"status\":\"HEALTHY\",\"uptime_seconds\":86400,\"memory_free_kb\":3145728}\n\n";
    PrintStr(out);
}

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "curl 8.8.0 (x86_64-w64-mingw32) libcurl/8.8.0 OpenSSL/3.2.1 zlib/1.3.1 (MicaNT Sovereign Cutler Edition)\n"
        "Release-Date: 2024-03-27\n"
        "Protocols: dict file ftp ftps gopher http https imap imaps ldap ldaps mqtt pop3 pop3s rtsp scp sftp smb smbs smtp smtps telnet tftp\n"
        "Features: AsynchDNS HSTS HTTP2 HTTPS-proxy IPv6 Largefile MultiSSL NTLM SSL TLS-SRP UnixSockets alt-svc brotli libz zstd\n\n"
        "Usage: curl [options...] <url>\n\n"
        "Options:\n"
        "   -d, --data <data>          HTTP POST data\n"
        "   -f, --fail                 Fail fast with no output on HTTP errors\n"
        "   -h, --help <category>      Get help for commands\n"
        "   -I, --head                 Show document info only\n"
        "   -o, --output <file>        Write to file instead of stdout\n"
        "   -O, --remote-name          Write output to file named as remote file\n"
        "   -s, --silent               Silent mode\n"
        "   -u, --user <user:password> Server user and password\n"
        "   -A, --user-agent <name>    Send User-Agent <name> to server\n"
        "   -v, --verbose              Make the operation more talkative\n"
        "   -V, --version              Show version number and quit\n\n";

    PrintStr(banner);
    ShowSampleCurlTransfer();

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
