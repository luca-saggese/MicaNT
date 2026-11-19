/**
 * @file kernelbase.cpp
 * @brief KernelBase Win32 exports backed by the native syscall boundary.
 */

#include <windows.h>
#include <cstddef>
#include <cstdint>

using NTSTATUS = LONG;

struct UNICODE_STRING;

struct OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    UNICODE_STRING* ObjectName;
    ULONG Attributes;
    void* SecurityDescriptor;
    void* SecurityQualityOfService;
};

struct CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
};

static_assert(sizeof(OBJECT_ATTRIBUTES) == 48);
static_assert(sizeof(CLIENT_ID) == 16);

extern "C" {
__declspec(dllimport) NTSTATUS NTAPI NtQueryPerformanceCounter(
    LARGE_INTEGER*, LARGE_INTEGER*);
__declspec(dllimport) NTSTATUS NTAPI NtOpenProcess(
    HANDLE*, ACCESS_MASK, OBJECT_ATTRIBUTES*, CLIENT_ID*);
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);
__declspec(dllimport) NTSTATUS NTAPI NtAllocateVirtualMemory(
    HANDLE, void**, ULONG_PTR, SIZE_T*, ULONG, ULONG);
__declspec(dllimport) NTSTATUS NTAPI NtFreeVirtualMemory(
    HANDLE, void**, SIZE_T*, ULONG);

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* value) {
    if (!value) return FALSE;
    return NtQueryPerformanceCounter(value, nullptr) >= 0;
}

__declspec(dllexport) BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* value) {
    if (!value) return FALSE;
    LARGE_INTEGER counter{};
    return NtQueryPerformanceCounter(&counter, value) >= 0;
}

__declspec(dllexport) HANDLE WINAPI OpenProcess(
    DWORD desiredAccess, BOOL inheritHandle, DWORD processId) {
    HANDLE process = nullptr;
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    if (inheritHandle) attributes.Attributes = 0x00000002u; // OBJ_INHERIT
    CLIENT_ID clientId{};
    clientId.UniqueProcess = reinterpret_cast<HANDLE>(
        static_cast<uintptr_t>(processId));
    const NTSTATUS status = NtOpenProcess(
        &process, desiredAccess, &attributes, &clientId);
    return status >= 0 ? process : nullptr;
}

__declspec(dllexport) BOOL WINAPI TerminateProcess(HANDLE process, UINT exitCode) {
    return NtTerminateProcess(process, static_cast<NTSTATUS>(exitCode)) >= 0;
}

__declspec(dllexport) LPVOID WINAPI VirtualAllocEx(
    HANDLE process, LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect) {
    if (!process || !size) return nullptr;
    void* base = address;
    SIZE_T regionSize = size;
    const NTSTATUS status = NtAllocateVirtualMemory(
        process, &base, 0, &regionSize, allocationType, protect);
    return status >= 0 ? base : nullptr;
}

__declspec(dllexport) BOOL WINAPI VirtualFreeEx(
    HANDLE process, LPVOID address, SIZE_T size, DWORD freeType) {
    if (!process || !address) return FALSE;
    void* base = address;
    SIZE_T regionSize = size;
    return NtFreeVirtualMemory(process, &base, &regionSize, freeType) >= 0;
}

} // extern "C"
