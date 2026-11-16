#pragma once

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/native_process_abi.hpp"

// Mode-neutral declarations for the public Ring 3 -> NTDLL boundary. Native
// user DLLs include this header instead of Executive/dispatcher headers.
namespace micant::native_nt {

inline constexpr uint32_t TokenUserInformationClass = 1u;
inline constexpr micant::Handle CurrentProcessTokenPseudoHandle =
    static_cast<micant::Handle>(-4);

// Windows TOKEN_USER / SID_AND_ATTRIBUTES ABI. The SID bytes are returned
// immediately after NativeTokenUser by NtQueryInformationToken(TokenUser).
struct NativeSidAndAttributes {
    void* sid{};
    uint32_t attributes{};
};
struct NativeTokenUser {
    NativeSidAndAttributes user{};
};
static_assert(sizeof(NativeSidAndAttributes) == 16);
static_assert(sizeof(NativeTokenUser) == 16);

#if defined(MICANT_NATIVE_USERMODE) && defined(_WIN32)
#define MICANT_NT_API_IMPORT __declspec(dllimport)
#else
#define MICANT_NT_API_IMPORT
#endif

extern "C" {
MICANT_NT_API_IMPORT micant::NtStatus NtClose(micant::Handle handle) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtQueryInformationToken(
    micant::Handle tokenHandle, uint32_t informationClass,
    void* information, uint32_t informationLength,
    uint32_t* returnLength) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtCreateThreadEx(
    micant::Handle* threadHandle, uint32_t desiredAccess,
    micant::ObjectAttributes* attributes, micant::Handle processHandle,
    void* startRoutine, void* argument, uint32_t createFlags,
    uintptr_t zeroBits, size_t stackSize, size_t maximumStackSize,
    void* attributeList) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtCreateUserProcess(
    micant::Handle* processHandle, micant::Handle* threadHandle,
    uint32_t processDesiredAccess, uint32_t threadDesiredAccess,
    micant::ObjectAttributes* processObjectAttributes,
    micant::ObjectAttributes* threadObjectAttributes,
    uint32_t processFlags, uint32_t threadFlags,
    void* processParameters,
    micant::shared::native_process::ProcessCreateInfo64* createInfo,
    micant::PsAttributeList* attributeList) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtQuerySystemTime(micant::LargeInteger* systemTime) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtQueryPerformanceCounter(
    micant::LargeInteger* counter, micant::LargeInteger* frequency) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtDelayExecution(
    uint8_t alertable, const micant::LargeInteger* delayInterval) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtAllocateVirtualMemory(
    micant::Handle processHandle, void** baseAddress, uint64_t zeroBits,
    size_t* regionSize, uint32_t allocationType, uint32_t protect) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtFreeVirtualMemory(
    micant::Handle processHandle, void** baseAddress, size_t* regionSize,
    uint32_t freeType) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtProtectVirtualMemory(
    micant::Handle processHandle, void** baseAddress, size_t* regionSize,
    uint32_t newProtect, uint32_t* oldProtect) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtFlushInstructionCache(
    micant::Handle processHandle, const void* baseAddress, size_t size) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtReadFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock, void* buffer,
    uint32_t length, micant::LargeInteger* byteOffset, uint32_t* key) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtWriteFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock, const void* buffer,
    uint32_t length, micant::LargeInteger* byteOffset, uint32_t* key) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtFlushBuffersFile(
    micant::Handle fileHandle, micant::IoStatusBlock* ioStatusBlock) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtCreateFile(
    micant::Handle* fileHandle, uint32_t desiredAccess,
    micant::ObjectAttributes* objectAttributes, micant::IoStatusBlock* ioStatusBlock,
    micant::LargeInteger* allocationSize, uint32_t fileAttributes,
    uint32_t shareAccess, uint32_t createDisposition, uint32_t createOptions,
    void* eaBuffer, uint32_t eaLength) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtQueryInformationFile(
    micant::Handle fileHandle, micant::IoStatusBlock* ioStatusBlock,
    void* fileInformation, uint32_t length,
    micant::FileInformationClass fileInformationClass) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtQueryDirectoryFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock,
    void* fileInformation, uint32_t length,
    micant::FileInformationClass fileInformationClass,
    bool returnSingleEntry, micant::UnicodeString* fileName,
    bool restartScan) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus NtDeviceIoControlFile(
    micant::Handle fileHandle, micant::Handle event, void* apcRoutine,
    void* apcContext, micant::IoStatusBlock* ioStatusBlock,
    uint32_t ioControlCode, const void* inputBuffer, uint32_t inputBufferLength,
    void* outputBuffer, uint32_t outputBufferLength) noexcept;

// Private, boot-service-only LuceOS crash recovery interface. A null/zero-size
// read queries the required serialized LUCDMP size and pending dump ID;
// STATUS_BUFFER_TOO_SMALL reports the size. A successful read copies the
// bounded pending image. ACK releases the reserved copy only for the matching
// dump ID and must be issued after the exported file has been flushed.
MICANT_NT_API_IMPORT micant::NtStatus MicantReadPendingCrashDump(
    void* buffer, uint32_t capacity, uint32_t* bytesRequired,
    uint64_t* dumpId) noexcept;
MICANT_NT_API_IMPORT micant::NtStatus MicantAcknowledgeCrashDump(
    uint64_t dumpId) noexcept;
}

#undef MICANT_NT_API_IMPORT

inline bool querySystemTimeUnixSeconds(uint64_t& unixSeconds) noexcept {
    micant::LargeInteger time{};
    if (NtQuerySystemTime(&time) != micant::NtStatus::Success || time.QuadPart < 0) return false;
    constexpr uint64_t NtToUnixEpoch100ns = 116444736000000000ULL;
    const uint64_t ticks = static_cast<uint64_t>(time.QuadPart);
    if (ticks < NtToUnixEpoch100ns) return false;
    unixSeconds = (ticks - NtToUnixEpoch100ns) / 10000000ULL;
    return true;
}

} // namespace micant::native_nt
