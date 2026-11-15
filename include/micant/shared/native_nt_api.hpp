#pragma once

#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"

// Mode-neutral declarations for the public Ring 3 -> NTDLL boundary. Native
// user DLLs include this header instead of Executive/dispatcher headers.
namespace micant::native_nt {

#if defined(MICANT_NATIVE_USERMODE) && defined(_WIN32)
#define MICANT_NT_API_IMPORT __declspec(dllimport)
#else
#define MICANT_NT_API_IMPORT
#endif

extern "C" {
MICANT_NT_API_IMPORT micant::NtStatus NtClose(micant::Handle handle) noexcept;
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
