/**
 * @file certreq_app.cpp
 * Luce OS Ring 3 Certificate Request & Enrollment Utility (certreq) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 * Requests, submits, accepts, and retrieves X.509 certificates from Certificate Authorities.
 */

#include "gui_common.hpp"

namespace {

using namespace micant_gui;

} // namespace

extern "C" void _start() noexcept {
    const char* banner =
        "Usage:\n"
        "  certreq -?                            -- Display this usage message\n"
        "  certreq [-New] [Options] [PolicyFileIn [RequestFileOut]]\n"
        "                                        -- Create new request from INF\n"
        "  certreq -Submit [Options] [RequestFileIn [CertFileOut [CertChainFileOut [FullResponseFileOut]]]]\n"
        "                                        -- Submit request to CA\n"
        "  certreq -Retrieve [Options] RequestId [CertFileOut [CertChainFileOut [FullResponseFileOut]]]\n"
        "                                        -- Retrieve response from CA\n"
        "  certreq -Accept [Options] [CertChainFileIn | FullResponseFileIn | CertFileIn]\n"
        "                                        -- Accept and install response\n"
        "  certreq -Policy [Options] [PolicyFileIn [RequestFileOut]]\n"
        "                                        -- Verify request policy\n"
        "  certreq -Sign [Options] [RequestFileIn [RequestFileOut]]\n"
        "                                        -- Sign request with certificate\n\n"
        "Certificate Request Processor: Ready.\n\n";

    PrintStr(banner);

    DoSyscall(0x01, 0, 0); // NtTerminateProcess(0)
}
