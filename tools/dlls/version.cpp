/**
 * @file version.cpp
 * @brief Explicit failure exports for the current Version.dll profile.
 *
 * Version resource parsing and file installation are not connected to the
 * Ring 3 filesystem/resource APIs yet. These exports never report success or
 * return fabricated version data.
 */

#include <windows.h>
#include <winver.h>


namespace {
constexpr DWORD kUnsupported = ERROR_CALL_NOT_IMPLEMENTED;
constexpr DWORD kCannotReadSource = VIF_CANNOTREADSRC;
}

extern "C" {

/** @brief Locate file-version metadata; unsupported until native file search is available. */
__declspec(dllexport) DWORD WINAPI VerFindFileA(DWORD, LPSTR, LPSTR, LPSTR, LPSTR,
                                                PUINT, LPSTR, PUINT) {
    SetLastError(kUnsupported);
    return kCannotReadSource;
}
/** @brief Locate file-version metadata using UTF-16 paths; see VerFindFileA. */
__declspec(dllexport) DWORD WINAPI VerFindFileW(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR,
                                                PUINT, LPWSTR, PUINT) {
    SetLastError(kUnsupported);
    return kCannotReadSource;
}
/** @brief Install a versioned file; fails because version resource and file installation services are unavailable. */
__declspec(dllexport) DWORD WINAPI VerInstallFileA(DWORD, LPSTR, LPSTR, LPSTR, LPSTR,
                                                   LPSTR, LPSTR, PUINT) {
    SetLastError(kUnsupported);
    return kCannotReadSource;
}
/** @brief Install a versioned file using UTF-16 paths; see VerInstallFileA. */
__declspec(dllexport) DWORD WINAPI VerInstallFileW(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR,
                                                   LPWSTR, LPWSTR, PUINT) {
    SetLastError(kUnsupported);
    return kCannotReadSource;
}

/** @brief Return the required version-resource buffer size; zero means failure and the reserved handle is cleared. */
__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR, LPDWORD handle) {
    if (handle) *handle = 0;
    SetLastError(kUnsupported);
    return 0;
}
/** @brief Return the required version-resource buffer size for a UTF-16 path; see GetFileVersionInfoSizeA. */
__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR, LPDWORD handle) {
    if (handle) *handle = 0;
    SetLastError(kUnsupported);
    return 0;
}
/** @brief Return the required version-resource buffer size with source flags; unsupported flags and file lookup fail. */
__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExA(DWORD, LPCSTR, LPDWORD handle) {
    if (handle) *handle = 0;
    SetLastError(kUnsupported);
    return 0;
}
/** @brief Return the required version-resource buffer size with source flags and UTF-16 path; see the A form. */
__declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExW(DWORD, LPCWSTR, LPDWORD handle) {
    if (handle) *handle = 0;
    SetLastError(kUnsupported);
    return 0;
}

/** @brief Retrieve version-resource bytes into caller storage; returns FALSE and leaves the buffer unchanged. */
__declspec(dllexport) BOOL WINAPI GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID) {
    SetLastError(kUnsupported);
    return FALSE;
}
/** @brief Retrieve version-resource bytes for a UTF-16 path; see GetFileVersionInfoA. */
__declspec(dllexport) BOOL WINAPI GetFileVersionInfoW(LPCWSTR, DWORD, DWORD, LPVOID) {
    SetLastError(kUnsupported);
    return FALSE;
}
/** @brief Retrieve version-resource bytes with source flags; returns FALSE until resource lookup is implemented. */
__declspec(dllexport) BOOL WINAPI GetFileVersionInfoExA(DWORD, LPCSTR, DWORD, DWORD, LPVOID) {
    SetLastError(kUnsupported);
    return FALSE;
}
/** @brief Retrieve version-resource bytes with source flags and UTF-16 path; see the A form. */
__declspec(dllexport) BOOL WINAPI GetFileVersionInfoExW(DWORD, LPCWSTR, DWORD, DWORD, LPVOID) {
    SetLastError(kUnsupported);
    return FALSE;
}

/** @brief Map a language identifier to a caller buffer; returns zero and clears available output on unsupported operation. */
__declspec(dllexport) DWORD WINAPI VerLanguageNameA(DWORD, LPSTR output, DWORD capacity) {
    if (output && capacity) output[0] = '\0';
    SetLastError(kUnsupported);
    return 0;
}
/** @brief Map a language identifier to a UTF-16 caller buffer; see VerLanguageNameA. */
__declspec(dllexport) DWORD WINAPI VerLanguageNameW(DWORD, LPWSTR output, DWORD capacity) {
    if (output && capacity) output[0] = L'\0';
    SetLastError(kUnsupported);
    return 0;
}

/** @brief Query a version block by ANSI sub-block path; clears outputs and returns FALSE until parsing exists. */
__declspec(dllexport) BOOL WINAPI VerQueryValueA(LPCVOID, LPCSTR, LPVOID* value, PUINT length) {
    if (value) *value = nullptr;
    if (length) *length = 0;
    SetLastError(kUnsupported);
    return FALSE;
}
/** @brief Query a version block by UTF-16 sub-block path; clears outputs and returns FALSE until parsing exists. */
__declspec(dllexport) BOOL WINAPI VerQueryValueW(LPCVOID, LPCWSTR, LPVOID* value, PUINT length) {
    if (value) *value = nullptr;
    if (length) *length = 0;
    SetLastError(kUnsupported);
    return FALSE;
}

} // extern "C"
