/**
 * @file advapi32.cpp
 * @brief Explicitly unsupported Advapi32 exports for the direct stage path.
 *
 * The canonical `advapi32_boot` target uses the native-backed ABI wrapper.
 * This stage-path compatibility image has no registry, token, or SCM service;
 * it must therefore fail instead of returning fabricated handles or data.
 */

#include <windows.h>
#include <winreg.h>
#include <winsvc.h>
#include <processthreadsapi.h>
#include <cstdint>

namespace {
constexpr LSTATUS kNotImplemented = ERROR_CALL_NOT_IMPLEMENTED;

void set_last_error(DWORD error) noexcept {
#if defined(__x86_64__)
    uint64_t teb = 0;
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (teb) *reinterpret_cast<DWORD*>(teb + 0x68) = error;
#else
    (void)error;
#endif
}

BOOL fail_boolean_api() noexcept {
    set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
}

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) LSTATUS WINAPI RegOpenKeyExW(
    HKEY, LPCWSTR, DWORD, REGSAM, PHKEY) { return kNotImplemented; }

__declspec(dllexport) LSTATUS WINAPI RegOpenKeyExA(
    HKEY, LPCSTR, DWORD, REGSAM, PHKEY) { return kNotImplemented; }

__declspec(dllexport) LSTATUS WINAPI RegCreateKeyExW(
    HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM,
    LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD) { return kNotImplemented; }

__declspec(dllexport) LSTATUS WINAPI RegQueryValueExW(
    HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return kNotImplemented; }

__declspec(dllexport) LSTATUS WINAPI RegQueryValueExA(
    HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return kNotImplemented; }

__declspec(dllexport) LSTATUS WINAPI RegSetValueExW(
    HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD) { return kNotImplemented; }

__declspec(dllexport) LSTATUS WINAPI RegCloseKey(HKEY) { return kNotImplemented; }

__declspec(dllexport) BOOL WINAPI OpenProcessToken(HANDLE, DWORD, PHANDLE) {
    return fail_boolean_api();
}

__declspec(dllexport) BOOL WINAPI GetTokenInformation(
    HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD) {
    return fail_boolean_api();
}

__declspec(dllexport) SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR, LPCWSTR, DWORD) {
    set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
    return nullptr;
}

__declspec(dllexport) SC_HANDLE WINAPI OpenSCManagerA(LPCSTR, LPCSTR, DWORD) {
    set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
    return nullptr;
}

__declspec(dllexport) SC_HANDLE WINAPI OpenServiceW(SC_HANDLE, LPCWSTR, DWORD) {
    set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
    return nullptr;
}

__declspec(dllexport) SC_HANDLE WINAPI OpenServiceA(SC_HANDLE, LPCSTR, DWORD) {
    set_last_error(ERROR_CALL_NOT_IMPLEMENTED);
    return nullptr;
}

__declspec(dllexport) BOOL WINAPI StartServiceW(
    SC_HANDLE, DWORD, LPCWSTR*) { return fail_boolean_api(); }

__declspec(dllexport) BOOL WINAPI CloseServiceHandle(SC_HANDLE) { return fail_boolean_api(); }

__declspec(dllexport) BOOL WINAPI QueryServiceStatus(
    SC_HANDLE, LPSERVICE_STATUS) { return fail_boolean_api(); }

} // extern "C"
