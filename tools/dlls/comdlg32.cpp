/**
 * @file comdlg32.cpp
 * @brief Common Dialog ABI exports for MicaNT System32.
 *
 * The dialog service is not connected to User32 yet. Boolean dialog entry
 * points therefore fail with CDERR_DIALOGFAILURE; they never report that a
 * user accepted a dialog. No C++ exception crosses the Win32 ABI.
 */

#include <windows.h>
#include <commdlg.h>
#include <cderr.h>

namespace {

// CommDlgExtendedError reports the most recent failure in this DLL. The
// current single-process loader has no thread-local runtime contract, so this
// state is process-wide until the native thread-local storage path is wired.
DWORD g_extended_error = 0;

BOOL fail_dialog() {
    g_extended_error = CDERR_DIALOGFAILURE;
    return FALSE;
}

} // namespace

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI ChooseColorA(LPCHOOSECOLORA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI ChooseColorW(LPCHOOSECOLORW) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI ChooseFontA(LPCHOOSEFONTA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI ChooseFontW(LPCHOOSEFONTW) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI PrintDlgA(LPPRINTDLGA) {
    return fail_dialog();
}

__declspec(dllexport) BOOL WINAPI PrintDlgW(LPPRINTDLGW) {
    return fail_dialog();
}

__declspec(dllexport) HRESULT WINAPI PrintDlgExA(LPPRINTDLGEXA) {
    g_extended_error = CDERR_DIALOGFAILURE;
    return E_NOTIMPL;
}

__declspec(dllexport) HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW) {
    g_extended_error = CDERR_DIALOGFAILURE;
    return E_NOTIMPL;
}

__declspec(dllexport) HWND WINAPI FindTextA(LPFINDREPLACEA) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) HWND WINAPI FindTextW(LPFINDREPLACEW) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) HWND WINAPI ReplaceTextA(LPFINDREPLACEA) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) HWND WINAPI ReplaceTextW(LPFINDREPLACEW) {
    fail_dialog();
    return nullptr;
}

__declspec(dllexport) DWORD WINAPI CommDlgExtendedError() {
    return g_extended_error;
}

} // extern "C"
