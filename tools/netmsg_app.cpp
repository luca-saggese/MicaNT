/**
 * @file netmsg_app.cpp
 @brief Luce OS Ring 3 Network & System Error Message Catalog (netmsg) for MicaNT System32.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Implements authentic Windows NT NET HELPMSG error resolver for Win32 and NTSTATUS codes.
 */

#include "cli_common.hpp"

namespace {

using namespace micant_cli;

struct ErrorEntry {
    uint32_t    code;
    const char* symbol;
    const char* message;
};

inline constexpr ErrorEntry WIN32_ERRORS[] = {
    { 0,    "ERROR_SUCCESS",                        "The operation completed successfully." },
    { 1,    "ERROR_INVALID_FUNCTION",               "Incorrect function." },
    { 2,    "ERROR_FILE_NOT_FOUND",                 "The system cannot find the file specified." },
    { 3,    "ERROR_PATH_NOT_FOUND",                 "The system cannot find the path specified." },
    { 4,    "ERROR_TOO_MANY_OPEN_FILES",            "The system cannot open the file." },
    { 5,    "ERROR_ACCESS_DENIED",                  "Access is denied." },
    { 6,    "ERROR_INVALID_HANDLE",                 "The handle is invalid." },
    { 8,    "ERROR_NOT_ENOUGH_MEMORY",              "Not enough storage is available to process this command." },
    { 14,   "ERROR_OUTOFMEMORY",                    "Not enough storage is available to complete this operation." },
    { 15,   "ERROR_INVALID_DRIVE",                  "The system cannot find the drive specified." },
    { 19,   "ERROR_WRITE_PROTECT",                  "The media is write protected." },
    { 21,   "ERROR_NOT_READY",                      "The device is not ready." },
    { 23,   "ERROR_CRC",                            "Data error (cyclic redundancy check)." },
    { 29,   "ERROR_WRITE_FAULT",                    "The system cannot write to the specified device." },
    { 30,   "ERROR_READ_FAULT",                     "The system cannot read from the specified device." },
    { 32,   "ERROR_SHARING_VIOLATION",              "The process cannot access the file because it is being used by another process." },
    { 50,   "ERROR_NOT_SUPPORTED",                  "The request is not supported." },
    { 53,   "ERROR_BAD_NETPATH",                    "The network path was not found." },
    { 67,   "ERROR_BAD_NET_NAME",                   "The network name cannot be found." },
    { 80,   "ERROR_FILE_EXISTS",                    "The file exists." },
    { 87,   "ERROR_INVALID_PARAMETER",              "The parameter is incorrect." },
    { 112,  "ERROR_DISK_FULL",                      "There is not enough space on the disk." },
    { 123,  "ERROR_INVALID_NAME",                   "The filename, directory name, or volume label syntax is incorrect." },
    { 1058, "ERROR_SERVICE_DISABLED",               "The service cannot be started, either because it is disabled or because it has no enabled devices associated with it." },
    { 1060, "ERROR_SERVICE_DOES_NOT_EXIST",         "The specified service does not exist as an installed service." },
    { 1062, "ERROR_SERVICE_NOT_ACTIVE",             "The service has not been started." },
    { 1068, "ERROR_SERVICE_DEPENDENCY_FAIL",        "The dependency service or group failed to start." },
    { 1223, "ERROR_CANCELLED",                      "The operation was canceled by the user." },
    { 1460, "ERROR_TIMEOUT",                        "This operation returned because the timeout period expired." },
    { 2182, "NET_ERROR_SERVICE_ALREADY_RUNNING",    "The requested service has already been started." }
};

inline constexpr ErrorEntry NTSTATUS_ERRORS[] = {
    { 0x00000000U, "STATUS_SUCCESS",                     "The operation completed successfully." },
    { 0xC0000001U, "STATUS_UNSUCCESSFUL",                "{Operation Failed} The requested operation was unsuccessful." },
    { 0xC0000002U, "STATUS_NOT_IMPLEMENTED",             "{Not Implemented} The requested operation is not implemented." },
    { 0xC0000005U, "STATUS_ACCESS_VIOLATION",            "The instruction at 0x%p referenced memory at 0x%p. The memory could not be %s." },
    { 0xC0000008U, "STATUS_INVALID_HANDLE",              "An invalid HANDLE was specified." },
    { 0xC0000017U, "STATUS_NO_MEMORY",                   "{Not Enough Quota} Not enough virtual memory or paging file quota is available to complete the specified operation." },
    { 0xC0000022U, "STATUS_ACCESS_DENIED",               "{Access Denied} A process has requested access to an object, but has not been granted those access rights." },
    { 0xC0000034U, "STATUS_OBJECT_NAME_NOT_FOUND",       "Object Name not found." },
    { 0xC000003AU, "STATUS_OBJECT_PATH_NOT_FOUND",       "{Path Not Found} The path %hs does not exist." },
    { 0xC000007BU, "STATUS_INVALID_IMAGE_FORMAT",        "{Bad Image} %hs is either not designed to run on Windows or it contains an error." },
    { 0xC000009AU, "STATUS_INSUFFICIENT_RESOURCES",      "Insufficient system resources exist to complete the API." },
    { 0xC0000135U, "STATUS_DLL_NOT_FOUND",               "{Unable To Locate Component} This application has failed to start because %hs was not found." },
    { 0xC0000142U, "STATUS_DLL_INIT_FAILED",             "{DLL Initialization Failed} Initialization of the dynamic link library %hs failed." },
    { 0xC000021AU, "STATUS_SYSTEM_PROCESS_TERMINATED",   "{Fatal System Error} The %hs system process terminated unexpectedly." }
};

void ShowHelp() noexcept {
    PrintLine("Explains the reason for a network or system error.");
    PrintLine("");
    PrintLine("NET HELPMSG <message#>");
    PrintLine("NETMSG <error_code>");
    PrintLine("");
    PrintLine("Examples:");
    PrintLine("    netmsg 2");
    PrintLine("    netmsg 5");
    PrintLine("    netmsg 0xC0000005");
}

} // namespace

extern "C" void _start(const char* rawCmdLine = nullptr) {
    VerifyRing3Cpl();

    CommandLineParser parser(rawCmdLine, "netmsg.exe");

    if (parser.HasSwitch("?") || parser.PositionalCount() == 0) {
        ShowHelp();
        ExitProcess(0);
    }

    const char* arg = parser.GetPositional(0);
    uint64_t codeVal = ParseUInt64(arg);
    uint32_t code = static_cast<uint32_t>(codeVal);

    // 1. Check NTSTATUS errors first if high bit set
    if (code >= 0xC0000000U) {
        for (const auto& entry : NTSTATUS_ERRORS) {
            if (entry.code == code) {
                PrintLine(entry.message);
                ExitProcess(0);
            }
        }
    }

    // 2. Check Win32 errors
    for (const auto& entry : WIN32_ERRORS) {
        if (entry.code == code) {
            PrintLine(entry.message);
            ExitProcess(0);
        }
    }

    // 3. Fallback: Check NTSTATUS table regardless
    for (const auto& entry : NTSTATUS_ERRORS) {
        if (entry.code == code) {
            PrintLine(entry.message);
            ExitProcess(0);
        }
    }

    // Not found
    PrintLine("No explanation is available for this message code.");
    ExitProcess(0);
}
