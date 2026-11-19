/**
 * @file oleaut32.cpp
 * @brief Automation string and VARIANT exports for MicaNT.
 *
 * BSTR allocation and general VARIANT ownership are not yet connected to a
 * user-mode allocator. The exports use explicit failure where their ABI
 * permits it and never return fabricated allocation success.
 */

#include <windows.h>
#include <oleauto.h>

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) BSTR WINAPI SysAllocString(const OLECHAR*) {
    return nullptr;
}

__declspec(dllexport) BSTR WINAPI SysAllocStringLen(const OLECHAR*, UINT) {
    return nullptr;
}

__declspec(dllexport) void WINAPI SysFreeString(BSTR) {
    // No allocator is attached to this DLL, so it has no owned BSTR to free.
}

__declspec(dllexport) UINT WINAPI SysStringLen(BSTR value) {
    if (!value) return 0;
    const auto* byteLength = reinterpret_cast<const UINT*>(
        reinterpret_cast<const BYTE*>(value) - sizeof(UINT));
    return *byteLength / sizeof(OLECHAR);
}

__declspec(dllexport) UINT WINAPI SysStringByteLen(BSTR value) {
    if (!value) return 0;
    const auto* byteLength = reinterpret_cast<const UINT*>(
        reinterpret_cast<const BYTE*>(value) - sizeof(UINT));
    return *byteLength;
}

__declspec(dllexport) void WINAPI VariantInit(VARIANTARG* value) {
    if (value) {
        VARIANTARG empty{};
        empty.vt = VT_EMPTY;
        *value = empty;
    }
}

__declspec(dllexport) HRESULT WINAPI VariantClear(VARIANTARG* value) {
    if (!value) return E_INVALIDARG;
    if (value->vt == VT_EMPTY || value->vt == VT_NULL) {
        VariantInit(value);
        return S_OK;
    }
    return E_NOTIMPL;
}

__declspec(dllexport) HRESULT WINAPI VariantCopy(VARIANTARG*, VARIANTARG*) {
    return E_NOTIMPL;
}

} // extern "C"
