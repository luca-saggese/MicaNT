#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/syscall_numbers.hpp"

namespace micant::fs {
class FileObject;
}
namespace micant::hal::amd64 { struct UserSegmentState; }

namespace micant::sys {

// Standard x86-64 NT System Service Numbers (SSNs)
// Aligned with Windows 10/11 x64 dispatch indices
inline constexpr uint32_t SSN_NtCreateFile               = 0x0055;
inline constexpr uint32_t SSN_NtOpenFile                 = 0x0033;
inline constexpr uint32_t SSN_NtReadFile                 = 0x0006;
// Windows 10 1803–1909 service profile; later Windows profiles vary.
inline constexpr uint32_t SSN_NtCancelIoFileEx           = 0x0090;
inline constexpr uint32_t SSN_NtWriteFile                = 0x0008;
inline constexpr uint32_t SSN_NtDeviceIoControlFile       = 0x0007;
inline constexpr uint32_t SSN_NtClose                    = 0x000F;
inline constexpr uint32_t SSN_NtAllocateVirtualMemory    = 0x0018;
inline constexpr uint32_t SSN_NtFreeVirtualMemory        = 0x001E;
inline constexpr uint32_t SSN_NtProtectVirtualMemory     = 0x0050;
inline constexpr uint32_t SSN_NtFlushInstructionCache     = MICANT_SSN_NT_FLUSH_INSTRUCTION_CACHE;
inline constexpr uint32_t SSN_NtQueryVirtualMemory       = 0x0023;
inline constexpr uint32_t SSN_NtCreateProcessEx          = 0x00B4;
inline constexpr uint32_t SSN_NtTerminateProcess         = 0x002C;
inline constexpr uint32_t SSN_NtCreateThreadEx           = 0x00BD;
inline constexpr uint32_t SSN_NtTerminateThread          = 0x0053;
inline constexpr uint32_t SSN_NtResumeThread             = 0x0052;
inline constexpr uint32_t SSN_NtWaitForSingleObject      = 0x0004;
inline constexpr uint32_t SSN_NtContinue                 = 0x0043;
// Windows 10 1803–1909 service profile.
inline constexpr uint32_t SSN_NtOpenProcess = MICANT_SSN_NT_OPEN_PROCESS;
inline constexpr uint32_t SSN_NtOpenThread = MICANT_SSN_NT_OPEN_THREAD;
inline constexpr uint32_t SSN_NtReadVirtualMemory = MICANT_SSN_NT_READ_VIRTUAL_MEMORY;
inline constexpr uint32_t SSN_NtRaiseException           = MICANT_SSN_NT_RAISE_EXCEPTION;
inline constexpr uint32_t SSN_NtQuerySystemInformation   = 0x0036;
inline constexpr uint32_t SSN_NtQuerySystemTime          = 0x0047;

// Synchronization & IOCP SSNs
inline constexpr uint32_t SSN_NtCreateEvent              = 0x0048;
inline constexpr uint32_t SSN_NtSetEvent                 = 0x004E;
inline constexpr uint32_t SSN_NtResetEvent               = 0x004F;
inline constexpr uint32_t SSN_NtCreateMutant             = 0x00B2;
inline constexpr uint32_t SSN_NtReleaseMutant            = 0x001D;
inline constexpr uint32_t SSN_NtCreateIoCompletion       = 0x0164;
inline constexpr uint32_t SSN_NtSetIoCompletion          = 0x0165;
inline constexpr uint32_t SSN_NtRemoveIoCompletion       = 0x0009;

// Multi-Object Wait & Execution Delay SSNs
inline constexpr uint32_t SSN_NtWaitForMultipleObjects   = 0x005A;
inline constexpr uint32_t SSN_NtDelayExecution           = 0x0034;

// Kernel Timer SSNs
inline constexpr uint32_t SSN_NtCreateTimer              = 0x0057;
inline constexpr uint32_t SSN_NtSetTimer                 = 0x0078;
inline constexpr uint32_t SSN_NtCancelTimer              = 0x0077;

// Power Management & Shutdown SSN
inline constexpr uint32_t SSN_NtShutdownSystem           = 0x0118;

// Named Pipes & Mailslots SSNs
inline constexpr uint32_t SSN_NtCreateNamedPipeFile       = 0x0091;
inline constexpr uint32_t SSN_NtCreateMailslotFile        = 0x0092;

// Configuration Manager (Registry) SSNs
inline constexpr uint32_t SSN_NtCreateKey                = 0x0029;
inline constexpr uint32_t SSN_NtOpenKey                  = 0x0012;
inline constexpr uint32_t SSN_NtQueryValueKey            = 0x0016;
inline constexpr uint32_t SSN_NtSetValueKey              = 0x0060;

// Security Reference Monitor SSNs
inline constexpr uint32_t SSN_NtOpenProcessToken         = 0x00BE;
// MicaNT profile slot for the standard NtQueueApcThread native service.
inline constexpr uint32_t SSN_NtQueueApcThread            = 0x00BF;
inline constexpr uint32_t SSN_NtAccessCheck              = 0x0182;

// Advanced Local Procedure Call (ALPC) SSNs
inline constexpr uint32_t SSN_NtCreatePort               = 0x0093;
inline constexpr uint32_t SSN_NtConnectPort              = 0x0096;
inline constexpr uint32_t SSN_NtRequestWaitReplyPort     = 0x0022;

// Section & Memory Mapping SSNs
inline constexpr uint32_t SSN_NtCreateSection            = 0x004A;
inline constexpr uint32_t SSN_NtOpenSection              = 0x0037;
inline constexpr uint32_t SSN_NtMapViewOfSection         = 0x0028;
inline constexpr uint32_t SSN_NtUnmapViewOfSection       = 0x002A;

// File Information & Directory Query SSNs
inline constexpr uint32_t SSN_NtQueryInformationFile     = 0x0011;
inline constexpr uint32_t SSN_NtSetInformationFile       = 0x0027;
inline constexpr uint32_t SSN_NtQueryDirectoryFile       = 0x0035;

// Performance & Execution SSNs
inline constexpr uint32_t SSN_NtQueryPerformanceCounter  = 0x0031;
inline constexpr uint32_t SSN_NtYieldExecution           = 0x0046;
inline constexpr uint32_t SSN_NtQueryInformationProcess  = 0x0019;
inline constexpr uint32_t SSN_NtQueryInformationThread   = MICANT_SSN_NT_QUERY_INFORMATION_THREAD;
inline constexpr uint32_t SSN_NtAlertThread = MICANT_SSN_NT_ALERT_THREAD;
inline constexpr uint32_t SSN_NtTestAlert = MICANT_SSN_NT_TEST_ALERT;
inline constexpr uint32_t SSN_NtDuplicateObject = MICANT_SSN_NT_DUPLICATE_OBJECT;

/**
 * @brief Native NT Syscall Signatures in Modern C++23
 */

// Memory Management
NtStatus NtAllocateVirtualMemory(
    Handle processHandle,
    uintptr_t* baseAddress,
    uintptr_t zeroBits,
    size_t* regionSize,
    uint32_t allocationType,
    uint32_t protect
);

NtStatus NtFreeVirtualMemory(
    Handle processHandle,
    uintptr_t* baseAddress,
    size_t* regionSize,
    uint32_t freeType
);

NtStatus NtProtectVirtualMemory(
    Handle processHandle,
    uintptr_t* baseAddress,
    size_t* regionSize,
    uint32_t newProtect,
    uint32_t* oldProtect
);

NtStatus NtFlushInstructionCache(Handle processHandle, const void* baseAddress, size_t size);

// File & I/O
NtStatus NtCreateFile(
    Handle* fileHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    LargeInteger* allocationSize,
    uint32_t fileAttributes,
    uint32_t shareAccess,
    uint32_t createDisposition,
    uint32_t createOptions,
    void* eaBuffer,
    uint32_t eaLength
);

NtStatus NtOpenFile(
    Handle* fileHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    uint32_t shareAccess,
    uint32_t openOptions
);

NtStatus NtReadFile(
    Handle fileHandle,
    Handle event,
    void* apcRoutine,
    void* apcContext,
    IoStatusBlock* ioStatusBlock,
    void* buffer,
    uint32_t length,
    LargeInteger* byteOffset,
    uint32_t* key
);

NtStatus NtWriteFile(
    Handle fileHandle,
    Handle event,
    void* apcRoutine,
    void* apcContext,
    IoStatusBlock* ioStatusBlock,
    const void* buffer,
    uint32_t length,
    LargeInteger* byteOffset,
    uint32_t* key
);

NtStatus NtDeviceIoControlFile(
    Handle fileHandle,
    Handle event,
    void* apcRoutine,
    void* apcContext,
    IoStatusBlock* ioStatusBlock,
    uint32_t ioControlCode,
    const void* inputBuffer,
    uint32_t inputBufferLength,
    void* outputBuffer,
    uint32_t outputBufferLength
);

NtStatus NtCreateNamedPipeFile(
    Handle* fileHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    uint32_t shareAccess,
    uint32_t createDisposition,
    uint32_t createOptions,
    uint32_t namedPipeType,
    uint32_t readMode,
    uint32_t completionMode,
    uint32_t maximumInstances,
    uint32_t inboundQuota,
    uint32_t outboundQuota,
    LargeInteger* defaultTimeout
);

NtStatus NtCreateMailslotFile(
    Handle* fileHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    IoStatusBlock* ioStatusBlock,
    uint32_t createOptions,
    uint32_t mailslotQuota,
    uint32_t maxMessageSize,
    LargeInteger* readTimeout
);

NtStatus NtClose(Handle handle);
#if defined(MICANT_FREESTANDING)
extern "C" NtStatus ZwClose(Handle handle) noexcept;
#endif

fs::FileObject* LookupKernelFileObject(Handle handle);

// Process & Thread
NtStatus NtTerminateProcess(
    Handle processHandle,
    NtStatus exitStatus
);

#if defined(MICANT_FREESTANDING)
NtStatus NtTerminateThread(Handle threadHandle, NtStatus exitStatus);
// Internal continuation adapter. The public native ABI remains
// NtContinue(PCONTEXT, BOOLEAN); the bootstrap frame and XSAVE storage are
// supplied by the syscall entry rather than exposed to user mode.
NtStatus NtContinue(const void* context, bool testAlert, void* trapFrame,
                    void* floatingState, size_t floatingStateSize,
                    uint32_t xsaveMask, bool forExceptionRaise = false,
                    hal::amd64::UserSegmentState* userSegments = nullptr);
NtStatus NtRaiseException(const void* record, const void* context, bool firstChance,
                          void* trapFrame, void* floatingState, size_t floatingStateSize,
                          uint32_t xsaveMask, hal::amd64::UserSegmentState* userSegments = nullptr);
NtStatus NtResumeThread(Handle threadHandle, uint32_t* previousSuspendCount);
NtStatus NtOpenProcess(Handle*, uint32_t, ObjectAttributes*, ClientId*);
NtStatus NtOpenThread(Handle*, uint32_t, ObjectAttributes*, ClientId*);
NtStatus NtCreateThreadEx(Handle* threadHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle processHandle, uintptr_t startRoutine,
    uintptr_t argument, uint32_t flags, uintptr_t zeroBits, size_t stackSize,
    size_t maximumStackSize, void* attributeList);
// Dispatcher-only entry: the outer structures and attribute outputs have
// been captured in kernel storage; any nested security descriptor still
// belongs to user mode. Handle access uses the calling thread's previous mode.
NtStatus NtCreateThreadExFromKernel(Handle* threadHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle processHandle, uintptr_t startRoutine,
    uintptr_t argument, uint32_t flags, uintptr_t zeroBits, size_t stackSize,
    size_t maximumStackSize, void* attributeList);
NtStatus NtCreateProcessEx(Handle* processHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle parentProcess, uint32_t flags,
    Handle section, Handle debugPort, Handle exceptionPort, bool inJob);
NtStatus NtCreateProcessExFromKernel(Handle* processHandle, uint32_t desiredAccess,
    ObjectAttributes* attributes, Handle parentProcess, uint32_t flags,
    Handle section, Handle debugPort, Handle exceptionPort, bool inJob);
// The process syscall keeps its output in user memory while using captured
// attributes, so handle publication and copyout share one exclusion window.
NtStatus NtCreateProcessExCaptured(Handle* userProcessHandle, uint32_t desiredAccess,
    ObjectAttributes* capturedAttributes, Handle parentProcess, uint32_t flags,
    Handle section, Handle debugPort, Handle exceptionPort, bool inJob);
NtStatus NtCreateEvent(Handle* eventHandle, uint32_t desiredAccess,
                       ObjectAttributes* objectAttributes, uint32_t eventType,
                       bool initialState);
NtStatus NtSetEvent(Handle eventHandle, int32_t* previousState);
NtStatus NtQueueApcThread(Handle threadHandle, uintptr_t apcRoutine,
                          void* normalContext, void* systemArgument1,
                          void* systemArgument2);
#endif

NtStatus NtWaitForSingleObject(
    Handle handle,
    bool alertable,
    LargeInteger* timeout
);

NtStatus NtWaitForMultipleObjects(
    uint32_t count,
    const Handle* handles,
    WaitType waitType,
    bool alertable,
    LargeInteger* timeout
);

NtStatus NtDelayExecution(
    bool alertable,
    const LargeInteger* interval
);

NtStatus NtShutdownSystem(
    uint32_t action
);

NtStatus NtCreateTimer(
    Handle* timerHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    uint32_t timerType
);

NtStatus NtSetTimer(
    Handle timerHandle,
    LargeInteger* dueTime,
    void* timerApcRoutine,
    void* timerContext,
    bool resumeTimer,
    uint32_t period,
    bool* previousState
);

NtStatus NtCancelTimer(
    Handle timerHandle,
    bool* currentSignaledState
);

NtStatus NtQuerySystemInformation(
    uint32_t systemInformationClass,
    void* systemInformation,
    uint32_t systemInformationLength,
    uint32_t* returnLength
);

// Section Management
NtStatus NtCreateSection(
    Handle* sectionHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    LargeInteger* maximumSize,
    uint32_t sectionPageProtection,
    uint32_t allocationAttributes,
    Handle fileHandle
);

// Internal kernel callers pass kernel-resident structures. This entry point
// bypasses user probing explicitly.
NtStatus NtCreateSectionFromKernel(
    Handle* sectionHandle,
    uint32_t desiredAccess,
    ObjectAttributes* objectAttributes,
    LargeInteger* maximumSize,
    uint32_t sectionPageProtection,
    uint32_t allocationAttributes,
    Handle fileHandle
);

// Syscall dispatch has captured inputs in kernel storage, but output remains
// in user space and is published atomically with the handle-table entry.
NtStatus NtCreateSectionCaptured(
    Handle* userSectionHandle, uint32_t desiredAccess,
    ObjectAttributes* objectAttributes, LargeInteger* maximumSize,
    uint32_t sectionPageProtection, uint32_t allocationAttributes, Handle fileHandle);

NtStatus NtMapViewOfSection(
    Handle sectionHandle,
    Handle processHandle,
    uintptr_t* baseAddress,
    uintptr_t zeroBits,
    size_t commitSize,
    LargeInteger* sectionOffset,
    size_t* viewSize,
    uint32_t inheritDisposition,
    uint32_t allocationType,
    uint32_t win32Protect
);

NtStatus NtMapViewOfSectionFromKernel(
    Handle sectionHandle,
    Handle processHandle,
    uintptr_t* baseAddress,
    uintptr_t zeroBits,
    size_t commitSize,
    LargeInteger* sectionOffset,
    size_t* viewSize,
    uint32_t inheritDisposition,
    uint32_t allocationType,
    uint32_t win32Protect
);

NtStatus NtUnmapViewOfSection(
    Handle processHandle,
    uintptr_t baseAddress
);

// Internal failed-copyout rollback; a failed unmap remains pinned for retry.
NtStatus rollbackMappedViewFromKernel(Handle processHandle, uintptr_t baseAddress) noexcept;

// File Information & Directory Queries
NtStatus NtQueryInformationFile(
    Handle fileHandle,
    IoStatusBlock* ioStatusBlock,
    void* fileInformation,
    uint32_t length,
    FileInformationClass fileInformationClass
);

NtStatus NtSetInformationFile(
    Handle fileHandle,
    IoStatusBlock* ioStatusBlock,
    const void* fileInformation,
    uint32_t length,
    FileInformationClass fileInformationClass
);

NtStatus NtQueryDirectoryFile(
    Handle fileHandle,
    Handle event,
    void* apcRoutine,
    void* apcContext,
    IoStatusBlock* ioStatusBlock,
    void* fileInformation,
    uint32_t length,
    FileInformationClass fileInformationClass,
    bool returnSingleEntry,
    UnicodeString* fileName,
    bool restartScan
);

// Performance & Process Information
NtStatus NtQuerySystemTime(LargeInteger* systemTime) noexcept;
NtStatus NtQueryPerformanceCounter(
    LargeInteger* performanceCounter,
    LargeInteger* performanceFrequency
);

NtStatus NtYieldExecution();

NtStatus NtReadVirtualMemory(Handle processHandle, const void* baseAddress, void* buffer,
                             size_t length, size_t* bytesRead);

NtStatus NtQueryInformationProcess(
    Handle processHandle,
    ProcessInformationClass processInformationClass,
    void* processInformation,
    uint32_t processInformationLength,
    uint32_t* returnLength
);

NtStatus NtQueryInformationThread(
    Handle threadHandle,
    ThreadInformationClass threadInformationClass,
    void* threadInformation,
    uint32_t threadInformationLength,
    uint32_t* returnLength
);
NtStatus NtAlertThread(Handle threadHandle);
NtStatus NtTestAlert();

} // namespace micant::sys

