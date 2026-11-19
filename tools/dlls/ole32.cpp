/**
 * @file ole32.cpp
 * @brief Public COM/OLE ABI exports for MicaNT System32.
 *
 * COM apartment management, class activation and task allocation are not yet
 * connected to a user-mode COM runtime. The exports return explicit failure
 * where their ABI permits it; no successful HRESULT or fake interface is
 * manufactured. C++ exceptions never cross the COM ABI.
 */

#include <windows.h>
#include <objbase.h>

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) HRESULT WINAPI CoInitialize(LPVOID) {
    return E_NOTIMPL;
}

__declspec(dllexport) HRESULT WINAPI CoInitializeEx(LPVOID, DWORD) {
    return E_NOTIMPL;
}

__declspec(dllexport) void WINAPI CoUninitialize() {
    // No apartment state can be uninitialized until apartment setup exists.
}

__declspec(dllexport) HRESULT WINAPI CoCreateInstance(
    REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID* object) {
    if (object) *object = nullptr;
    return E_NOTIMPL;
}

__declspec(dllexport) LPVOID WINAPI CoTaskMemAlloc(SIZE_T) {
    return nullptr;
}

__declspec(dllexport) void WINAPI CoTaskMemFree(LPVOID) {
    // The allocator is unavailable; this DLL has not issued task allocations.
}

__declspec(dllexport) LPVOID WINAPI CoTaskMemRealloc(LPVOID, SIZE_T) {
    return nullptr;
}

__declspec(dllexport) HRESULT WINAPI StringFromCLSID(REFCLSID, LPOLESTR* value) {
    if (value) *value = nullptr;
    return E_NOTIMPL;
}

__declspec(dllexport) HRESULT WINAPI CLSIDFromString(LPCOLESTR, LPCLSID) {
    return E_NOTIMPL;
}

} // extern "C"
