#pragma once

#include <cstdint>
#include <string_view>

namespace micant {

/**
 * @brief Standard Windows NT Status Code (NTSTATUS) representation.
 * 32-bit value structured as:
 *   Bits 31-30: Severity (00 = Success, 01 = Info, 10 = Warning, 11 = Error)
 *   Bit 29: Customer / Reserved
 *   Bit 28: Reserved
 *   Bits 27-16: Facility code
 *   Bits 15-0: Status code
 */
enum class NtStatus : uint32_t {
    // 0x00000000 - Success
    Success                          = 0x00000000,
    Wait0                            = 0x00000000,
    Wait1                            = 0x00000001,
    Wait2                            = 0x00000002,
    Wait3                            = 0x00000003,
    Wait63                           = 0x0000003F,
    Abandoned                        = 0x00000080,
    UserApc                          = 0x000000C0,
    Alerted                          = 0x00000101,
    Timeout                          = 0x00000102,
    Pending                          = 0x00000103,

    // Informational success: an image view was mapped away from its
    // preferred PE image base; its loader must apply base relocations.
    ImageNotAtBase                   = 0x40000003,

    // 0x80000000 - Warnings / Info
    GuardPageViolation               = 0x80000001,
    DatatypeMisalignment             = 0x80000002,
    Breakpoint                       = 0x80000003,
    SingleStep                       = 0x80000004,
    BufferOverflow                   = 0x80000005,
    NoMoreFiles                      = 0x80000006,
    NoMoreEntries                    = 0x8000001A,
    HandlesClosed                    = 0x8000000A,

    // 0xC0000000 - Errors
    Unsuccessful                     = 0xC0000001,
    NotImplemented                   = 0xC0000002,
    PartialCopy                      = 0x8000000D,
    InvalidInfoClass                 = 0xC0000003,
    InfoLengthMismatch               = 0xC0000004,
    AccessViolation                  = 0xC0000005,
    InPageError                      = 0xC0000006,
    PagefileQuota                    = 0xC0000007,
    InvalidHandle                    = 0xC0000008,
    MutantNotOwned                   = 0xC0000046,
    MutantLimitExceeded              = 0xC0000191,
    HandleNotClosable                = 0xC0000235,
    BadInitialStack                  = 0xC0000009,
    BadInitialPc                     = 0xC000000A,
    InvalidCid                       = 0xC000000B,
    InvalidParameter                 = 0xC000000D,
    InvalidParameter4                = 0xC00000F2,
    Retry                            = 0xC000022D,
    StackOverflow                    = 0xC00000FD,
    NoSuchDevice                     = 0xC000000E,
    NoSuchFile                       = 0xC000000F,
    InvalidDeviceRequest             = 0xC0000010,
    InvalidDeviceState               = 0xC0000184,
    EndOfFile                        = 0xC0000011,
    MoreProcessingRequired           = 0xC0000016,
    NoMemory                         = 0xC0000017,
    Conflict                         = 0xC0000018,
    NotMappedView                    = 0xC0000019,
    InvalidViewSize                  = 0xC000001F,
    InvalidFileForSection            = 0xC0000020,
    InsufficientResources            = 0xC000009A,
    IntegerOverflow                  = 0xC0000095,
    IllegalInstruction               = 0xC000001D,
    AccessDenied                     = 0xC0000022,
    InvalidAcl                       = 0xC0000077,
    InvalidSid                       = 0xC0000078,
    InvalidSecurityDescriptor        = 0xC0000079,
    BufferTooSmall                   = 0xC0000023,
    WmiGuidNotFound                  = 0xC0000295,
    WmiInstanceNotFound              = 0xC0000296,
    WmiItemIdNotFound                = 0xC0000297,
    ObjectTypeMismatch               = 0xC0000024,
    CircularReference                = 0xC000002A,
    ObjectNameNotFound               = 0xC0000034,
    ObjectNameCollision              = 0xC0000035,
    ObjectNameInvalid                = 0xC0000033,
    ObjectNameExists                 = 0x40000000,
    ObjectPathNotFound               = 0xC000003A,
    ObjectPathSyntaxBad              = 0xC000003B,
    LinkEvaluationCapExceeded        = 0xC00002EB,
    SectionTooBig                    = 0xC0000040,
    InvalidPageProtection            = 0xC0000045,
    InvalidOwner                     = 0xC000005A,
    SectionProtection                = 0xC000004E,
    PortConnectionRefused            = 0xC0000041,
    IoTimeout                        = 0xC00000B5,
    ConnectionReset                  = 0xC000020D,
    ServerNotRunning                 = 0xC0000042,
    SharingViolation                 = 0xC0000043,
    NoSuchProcess                    = 0xC0000043,
    QuotaExceeded                    = 0xC0000044,
    PagefileQuotaExceeded            = 0xC000006D,
    ProcessIsTerminating             = 0xC000010A,
    ProcessAborted                   = 0xC000010B,
    PrivilegeNotHeld                 = 0xC0000061,
    ProcedureNotFound                = 0xC000007A,
    ResourceDataNotFound             = 0xC0000089,
    FileIsADirectory                 = 0xC00000BA,
    DirectoryNotEmpty                = 0xC0000101,
    NotADirectory                    = 0xC0000103,
    DevicePowerFailure               = 0xC000009E,
    PowerStateInvalid                = 0xC00002D3,
    SuspendCountExceeded             = 0xC000004A,
    ThreadIsTerminating              = 0xC000004B,
    CantTerminateSelf                = 0xC00000DB,
    DeviceBusy                       = 0x80000011,
    SystemShutdown                   = 0xC000009F,
    MediaWriteProtected              = 0xC00000A2,
    DeviceNotReady                   = 0xC00000A3,
    DeviceDataError                  = 0xC000009C,
    BadImpersonationLevel            = 0xC00000A5,
    DiskFull                         = 0xC000007F,
    UnrecognizedVolume               = 0xC00000DB,
    InvalidParameter1                = 0xC00000EF,
    InvalidParameter2                = 0xC00000F0,
    InvalidParameter3                = 0xC00000F1,
    MailslotNotFound                 = 0xC0000055,
    PipeNotAvailable                 = 0xC00000AC,
    PipeBusy                         = 0xC00000AD,
    PipeDisconnected                 = 0xC00000B0,
    PipeClosing                      = 0xC00000B1,
    PipeConnected                    = 0xC00000B2,
    PipeListening                    = 0xC00000B3,
    PipeBroken                       = 0xC000014B,
    VolumeNotMounted                 = 0xC0000078,
    VolumeTooSmall                   = 0xC0000287,
    DeviceError                      = 0xC00000E0,
    FileCorrupted                    = 0xC0000102,
    DataChecksumError                = 0xC00003FD,
    Cancelled                        = 0xC0000120,
    VariableNotFound                 = 0xC0000100,
    NotFound                         = 0xC0000225,
    NotSupported                     = 0xC00000BB,
    ProcessNotInJob                  = 0xC0000123,

    // Security, Authentication & SAM Subsystem Codes
    UserExists                       = 0xC0000063,
    NoSuchUser                       = 0xC0000064,
    WrongPassword                    = 0xC000006A,
    LogonFailure                     = 0xC000006D,
    AccountRestriction               = 0xC000006E,
    PasswordExpired                  = 0xC0000071,
    AccountDisabled                  = 0xC0000072,
    NoSuchAlias                      = 0xC0000073,
    MemberInAlias                    = 0xC0000075,
    MemberNotInAlias                 = 0xC0000076,
    NoSuchLogonSession               = 0xC00000EE,
    LogonTypeNotGranted              = 0xC000015B,
    AccountLockedOut                 = 0xC0000234,

    // File System Runtime Library (FsRtl) Locking, Oplock & Notification Codes
    OplockBreakInProgress            = 0x00000108,
    NotifyCleanup                    = 0x0000010B,
    NotifyEnumDir                    = 0x0000010C,
    FileLockConflict                 = 0xC0000054,
    LockNotGranted                   = 0xC0000055,
    RangeNotLocked                   = 0xC000007E,
    OplockNotGranted                 = 0xC00000E2,
    InvalidOplockProtocol            = 0xC00000E3,

    // User-Mode Debugging Executive (Dbgk / DEBUG_OBJECT / DbgUi) Status Codes
    DbgExceptionHandled              = 0x00010001,
    DbgContinue                      = 0x00010002,
    DbgReplyLater                    = 0x40010001,
    DbgTerminateThread               = 0x40010003,
    DbgTerminateProcess              = 0x40010004,
    DbgExceptionNotHandled           = 0x80010001,
    PortNotSet                       = 0xC0000353,
    DebuggerInactive                 = 0xC0000354,
    DebugAttachFailed                = 0xC0000399,

    // Kernel Transaction Manager (KTM / Tm* / CLFS) Status Codes (Facility 0x19)
    TransactionInvalidMarshallBuffer = 0xC0190001,
    TransactionInvalidType           = 0xC0190002,
    TransactionNotRequested          = 0xC0190003,
    TransactionAlreadyAborted        = 0xC0190004,
    TransactionAlreadyCommitted      = 0xC0190005,
    TransactionPropagationFailed     = 0xC0190006,
    TransactionNotJoined             = 0xC0190007,
    TransactionSuperiorExists        = 0xC0190008,
    TransactionNotActive             = 0xC0190009,
    TransactionRequestNotValid       = 0xC019000A,

    // Hardware Resource Arbiter (Arb* / Rtl*RangeList) & WDK .sys Loader Status Codes
    InvalidImageFormat               = 0xC000007B,
    RangeListConflict                = 0xC000034F
};

// ============================================================================
// Standard NTSTATUS Integral Codes (32-bit signed NT-style constants)
// ============================================================================
using NTSTATUS = int32_t;
inline constexpr NTSTATUS STATUS_SUCCESS                = 0x00000000;
inline constexpr NTSTATUS DBG_CONTINUE                  = 0x00010002;
inline constexpr NTSTATUS STATUS_BREAKPOINT             = static_cast<NTSTATUS>(0x80000003);
inline constexpr NTSTATUS STATUS_SINGLE_STEP            = static_cast<NTSTATUS>(0x80000004);
inline constexpr NTSTATUS DBG_EXCEPTION_NOT_HANDLED     = static_cast<NTSTATUS>(0x80010001);
inline constexpr NTSTATUS STATUS_UNSUCCESSFUL           = static_cast<NTSTATUS>(0xC0000001);
inline constexpr NTSTATUS STATUS_NOT_IMPLEMENTED        = static_cast<NTSTATUS>(0xC0000002);
inline constexpr NTSTATUS STATUS_INVALID_HANDLE         = static_cast<NTSTATUS>(0xC0000008);
inline constexpr NTSTATUS STATUS_INVALID_PARAMETER      = static_cast<NTSTATUS>(0xC000000D);
inline constexpr NTSTATUS STATUS_ACCESS_DENIED          = static_cast<NTSTATUS>(0xC0000022);
inline constexpr NTSTATUS STATUS_BUFFER_TOO_SMALL       = static_cast<NTSTATUS>(0xC0000023);
inline constexpr NTSTATUS STATUS_OBJECT_TYPE_MISMATCH   = static_cast<NTSTATUS>(0xC0000024);
inline constexpr NTSTATUS STATUS_CIRCULAR_REFERENCE     = static_cast<NTSTATUS>(0xC000002A);
inline constexpr NTSTATUS STATUS_OBJECT_NAME_NOT_FOUND  = static_cast<NTSTATUS>(0xC0000034);
inline constexpr NTSTATUS STATUS_OBJECT_NAME_COLLISION  = static_cast<NTSTATUS>(0xC0000035);
inline constexpr NTSTATUS STATUS_PORT_CONNECTION_REFUSED = static_cast<NTSTATUS>(0xC0000041);
inline constexpr NTSTATUS STATUS_LINK_EVALUATION_CAP_EXCEEDED = static_cast<NTSTATUS>(0xC00002EB);
inline constexpr NTSTATUS STATUS_NOT_SUPPORTED          = static_cast<NTSTATUS>(0xC00000BB);
inline constexpr NTSTATUS STATUS_PRIVILEGE_NOT_HELD      = static_cast<NTSTATUS>(0xC0000061);
inline constexpr NTSTATUS STATUS_BAD_IMPERSONATION_LEVEL = static_cast<NTSTATUS>(0xC00000A5);
inline constexpr NTSTATUS STATUS_USER_EXISTS            = static_cast<NTSTATUS>(0xC0000063);
inline constexpr NTSTATUS STATUS_NO_SUCH_USER           = static_cast<NTSTATUS>(0xC0000064);
inline constexpr NTSTATUS STATUS_WRONG_PASSWORD         = static_cast<NTSTATUS>(0xC000006A);
inline constexpr NTSTATUS STATUS_LOGON_FAILURE          = static_cast<NTSTATUS>(0xC000006D);
inline constexpr NTSTATUS STATUS_ACCOUNT_RESTRICTION    = static_cast<NTSTATUS>(0xC000006E);
inline constexpr NTSTATUS STATUS_PASSWORD_EXPIRED       = static_cast<NTSTATUS>(0xC0000071);
inline constexpr NTSTATUS STATUS_ACCOUNT_DISABLED       = static_cast<NTSTATUS>(0xC0000072);
inline constexpr NTSTATUS STATUS_NO_SUCH_ALIAS          = static_cast<NTSTATUS>(0xC0000073);
inline constexpr NTSTATUS STATUS_MEMBER_IN_ALIAS        = static_cast<NTSTATUS>(0xC0000075);
inline constexpr NTSTATUS STATUS_MEMBER_NOT_IN_ALIAS    = static_cast<NTSTATUS>(0xC0000076);
inline constexpr NTSTATUS STATUS_NO_SUCH_LOGON_SESSION  = static_cast<NTSTATUS>(0xC00000EE);
inline constexpr NTSTATUS STATUS_LOGON_TYPE_NOT_GRANTED = static_cast<NTSTATUS>(0xC000015B);
inline constexpr NTSTATUS STATUS_INSUFFICIENT_RESOURCES = static_cast<NTSTATUS>(0xC000009A);
inline constexpr NTSTATUS STATUS_ACCOUNT_LOCKED_OUT     = static_cast<NTSTATUS>(0xC0000234);
inline constexpr NTSTATUS STATUS_NO_MEMORY              = static_cast<NTSTATUS>(0xC0000017);
inline constexpr NTSTATUS STATUS_NOT_FOUND              = static_cast<NTSTATUS>(0xC0000225);
inline constexpr NTSTATUS STATUS_PENDING                = static_cast<NTSTATUS>(0x00000103);
inline constexpr NTSTATUS STATUS_NO_MORE_ENTRIES        = static_cast<NTSTATUS>(0x8000001A);
inline constexpr NTSTATUS STATUS_DATA_ERROR             = static_cast<NTSTATUS>(0xC000003E);
inline constexpr NTSTATUS STATUS_DEVICE_POWER_FAILURE   = static_cast<NTSTATUS>(0xC000009E);
inline constexpr NTSTATUS STATUS_POWER_STATE_INVALID    = static_cast<NTSTATUS>(0xC00002D3);
inline constexpr NTSTATUS STATUS_SUSPEND_COUNT_EXCEEDED = static_cast<NTSTATUS>(0xC000004A);
inline constexpr NTSTATUS STATUS_DEVICE_BUSY            = static_cast<NTSTATUS>(0x80000011);
inline constexpr NTSTATUS STATUS_DEVICE_NOT_READY       = static_cast<NTSTATUS>(0xC00000A3);
inline constexpr NTSTATUS STATUS_SYSTEM_SHUTDOWN        = static_cast<NTSTATUS>(0xC000009F);
inline constexpr NTSTATUS STATUS_INVALID_DEVICE_STATE   = static_cast<NTSTATUS>(0xC0000184);
inline constexpr NTSTATUS STATUS_NO_SUCH_DEVICE         = static_cast<NTSTATUS>(0xC000000E);
inline constexpr NTSTATUS STATUS_TIMEOUT                = static_cast<NTSTATUS>(0x00000102);
inline constexpr NTSTATUS STATUS_CANCELLED              = static_cast<NTSTATUS>(0xC0000120);
inline constexpr NTSTATUS STATUS_MEDIA_WRITE_PROTECTED  = static_cast<NTSTATUS>(0xC00000A2);
inline constexpr NTSTATUS STATUS_UNRECOGNIZED_VOLUME    = static_cast<NTSTATUS>(0xC00000DB);
inline constexpr NTSTATUS STATUS_NO_SUCH_FILE           = static_cast<NTSTATUS>(0xC000000F);
inline constexpr NTSTATUS STATUS_END_OF_FILE            = static_cast<NTSTATUS>(0xC0000011);
inline constexpr NTSTATUS STATUS_NOT_A_DIRECTORY        = static_cast<NTSTATUS>(0xC0000103);
inline constexpr NTSTATUS STATUS_DISK_CORRUPT_ERROR     = static_cast<NTSTATUS>(0xC0000032);
inline constexpr NTSTATUS STATUS_DATA_CHECKSUM_ERROR     = static_cast<NTSTATUS>(0xC00003FD);
inline constexpr NTSTATUS STATUS_DEVICE_DATA_ERROR      = static_cast<NTSTATUS>(0xC00000E0);

[[nodiscard]] constexpr NtStatus STATUS_WAIT_N(uint32_t index) noexcept {
    return static_cast<NtStatus>(static_cast<uint32_t>(NtStatus::Wait0) + index);
}

// Standard NT macro semantics evaluated constexpr
[[nodiscard]] constexpr bool NT_SUCCESS(NtStatus status) noexcept {
    return static_cast<int32_t>(status) >= 0;
}

[[nodiscard]] constexpr bool NT_SUCCESS(int32_t status) noexcept {
    return status >= 0;
}

[[nodiscard]] constexpr bool NT_INFORMATION(NtStatus status) noexcept {
    return (static_cast<uint32_t>(status) >> 30) == 1;
}

[[nodiscard]] constexpr bool NT_WARNING(NtStatus status) noexcept {
    return (static_cast<uint32_t>(status) >> 30) == 2;
}

[[nodiscard]] constexpr bool NT_ERROR(NtStatus status) noexcept {
    return (static_cast<uint32_t>(status) >> 30) == 3;
}

[[nodiscard]] constexpr std::string_view NtStatusToString(NtStatus status) noexcept {
    switch (status) {
        case NtStatus::Success: return "STATUS_SUCCESS";
        case NtStatus::Timeout: return "STATUS_TIMEOUT";
        case NtStatus::Pending: return "STATUS_PENDING";
        case NtStatus::Alerted: return "STATUS_ALERTED";
        case NtStatus::Breakpoint: return "STATUS_BREAKPOINT";
        case NtStatus::SingleStep: return "STATUS_SINGLE_STEP";
        case NtStatus::DatatypeMisalignment: return "STATUS_DATATYPE_MISALIGNMENT";
        case NtStatus::StackOverflow: return "STATUS_STACK_OVERFLOW";
        case NtStatus::BufferOverflow: return "STATUS_BUFFER_OVERFLOW";
        case NtStatus::NoMoreFiles: return "STATUS_NO_MORE_FILES";
        case NtStatus::Unsuccessful: return "STATUS_UNSUCCESSFUL";
        case NtStatus::NotImplemented: return "STATUS_NOT_IMPLEMENTED";
        case NtStatus::NotSupported: return "STATUS_NOT_SUPPORTED";
        case NtStatus::AccessViolation: return "STATUS_ACCESS_VIOLATION";
        case NtStatus::IllegalInstruction: return "STATUS_ILLEGAL_INSTRUCTION";
        case NtStatus::InvalidHandle: return "STATUS_INVALID_HANDLE";
        case NtStatus::HandleNotClosable: return "STATUS_HANDLE_NOT_CLOSABLE";
        case NtStatus::InvalidParameter: return "STATUS_INVALID_PARAMETER";
        case NtStatus::NoSuchFile: return "STATUS_NO_SUCH_FILE";
        case NtStatus::NoMemory: return "STATUS_NO_MEMORY";
        case NtStatus::InsufficientResources: return "STATUS_INSUFFICIENT_RESOURCES";
        case NtStatus::IntegerOverflow: return "STATUS_INTEGER_OVERFLOW";
        case NtStatus::AccessDenied: return "STATUS_ACCESS_DENIED";
        case NtStatus::BufferTooSmall: return "STATUS_BUFFER_TOO_SMALL";
        case NtStatus::ObjectTypeMismatch: return "STATUS_OBJECT_TYPE_MISMATCH";
        case NtStatus::CircularReference: return "STATUS_CIRCULAR_REFERENCE";
        case NtStatus::ObjectNameNotFound: return "STATUS_OBJECT_NAME_NOT_FOUND";
        case NtStatus::ObjectNameCollision: return "STATUS_OBJECT_NAME_COLLISION";
        case NtStatus::LinkEvaluationCapExceeded: return "STATUS_LINK_EVALUATION_CAP_EXCEEDED";
        case NtStatus::PortConnectionRefused: return "STATUS_PORT_CONNECTION_REFUSED";
        case NtStatus::IoTimeout: return "STATUS_IO_TIMEOUT";
        case NtStatus::ConnectionReset: return "STATUS_CONNECTION_RESET";
        case NtStatus::ObjectPathNotFound: return "STATUS_OBJECT_PATH_NOT_FOUND";
        case NtStatus::ProcedureNotFound: return "STATUS_PROCEDURE_NOT_FOUND";
        case NtStatus::InvalidFileForSection: return "STATUS_INVALID_FILE_FOR_SECTION";
        case NtStatus::ProcessIsTerminating: return "STATUS_PROCESS_IS_TERMINATING";
        case NtStatus::ProcessAborted: return "STATUS_PROCESS_ABORTED";
        case NtStatus::DeviceNotReady: return "STATUS_DEVICE_NOT_READY";
        case NtStatus::DiskFull: return "STATUS_DISK_FULL";
        case NtStatus::UnrecognizedVolume: return "STATUS_UNRECOGNIZED_VOLUME";
        case NtStatus::MailslotNotFound: return "STATUS_MAILSLOT_NOT_FOUND";
        case NtStatus::PipeNotAvailable: return "STATUS_PIPE_NOT_AVAILABLE";
        case NtStatus::PipeBusy: return "STATUS_PIPE_BUSY";
        case NtStatus::PipeDisconnected: return "STATUS_PIPE_DISCONNECTED";
        case NtStatus::PipeClosing: return "STATUS_PIPE_CLOSING";
        case NtStatus::PipeConnected: return "STATUS_PIPE_CONNECTED";
        case NtStatus::PipeListening: return "STATUS_PIPE_LISTENING";
        case NtStatus::PipeBroken: return "STATUS_PIPE_BROKEN";
        case NtStatus::VolumeNotMounted: return "STATUS_VOLUME_NOT_MOUNTED";
        case NtStatus::VolumeTooSmall: return "STATUS_VOLUME_TOO_SMALL";
        case NtStatus::DeviceError: return "STATUS_DEVICE_DATA_ERROR";
        case NtStatus::FileCorrupted: return "STATUS_FILE_CORRUPT_ERROR";
        case NtStatus::DataChecksumError: return "STATUS_DATA_CHECKSUM_ERROR";
        case NtStatus::Cancelled: return "STATUS_CANCELLED";
        case NtStatus::NoMoreEntries: return "STATUS_NO_MORE_ENTRIES";
        case NtStatus::VariableNotFound: return "STATUS_VARIABLE_NOT_FOUND";
        case NtStatus::ProcessNotInJob: return "STATUS_PROCESS_NOT_IN_JOB";
        default: return "STATUS_UNKNOWN";
    }
}

} // namespace micant
