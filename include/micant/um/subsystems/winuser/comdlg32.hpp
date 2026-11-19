// ============================================================================
// MicaNT: Sovereign Operating System Executive
// include/micant/comdlg32.hpp - Windows Common Dialog Box Subsystem (comdlg32.dll)
//
// 100% Clean-Room Architecture authored from Microsoft win32metadata interface definitions.
// Zero proprietary code used or referenced. Google LLC v. Oracle America (2021).
// ============================================================================

#pragma once

#include <cstdint>
#include "micant/shared/ntdef.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::comdlg32 {

// Dialogs are not connected to the User32 window manager yet. Keep the Win32
// failure observable: FALSE plus CDERR_DIALOGFAILURE, never synthetic success
// or a fake HWND. The loader bridge currently keeps this state process-wide.
inline constexpr uint32_t kDialogFailure = 0xFFFF;
inline uint32_t g_extended_error = 0;

inline int32_t fail_dialog() noexcept {
    g_extended_error = kDialogFailure;
    return 0;
}

inline int32_t __stdcall PrintDlgW([[maybe_unused]] void*) noexcept {
    return fail_dialog();
}

inline int32_t __stdcall ChooseColorW([[maybe_unused]] void*) noexcept {
    return fail_dialog();
}

inline int32_t __stdcall GetOpenFileNameW([[maybe_unused]] void*) noexcept {
    return fail_dialog();
}

inline int32_t __stdcall GetSaveFileNameW([[maybe_unused]] void*) noexcept {
    return fail_dialog();
}

inline uint32_t __stdcall CommDlgExtendedError() noexcept {
    return g_extended_error;
}

inline void* __stdcall ReplaceTextW([[maybe_unused]] void* lpfr) noexcept {
    fail_dialog();
    return nullptr;
}

inline int32_t __stdcall PrintDlgExW([[maybe_unused]] void* pPDEX) noexcept {
    g_extended_error = kDialogFailure;
    return static_cast<int32_t>(0x80004001u); // E_NOTIMPL
}

inline int32_t __stdcall ChooseColorA([[maybe_unused]] void*) noexcept { return fail_dialog(); }
inline int32_t __stdcall ChooseFontA([[maybe_unused]] void*) noexcept { return fail_dialog(); }
inline int32_t __stdcall ChooseFontW([[maybe_unused]] void*) noexcept { return fail_dialog(); }
inline int32_t __stdcall GetOpenFileNameA([[maybe_unused]] void*) noexcept { return fail_dialog(); }
inline int32_t __stdcall GetSaveFileNameA([[maybe_unused]] void*) noexcept { return fail_dialog(); }
inline void* __stdcall FindTextW([[maybe_unused]] void*) noexcept {
    fail_dialog();
    return nullptr;
}

inline void InitializeComDlg32SubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("comdlg32.dll", "PrintDlgW", reinterpret_cast<void*>(PrintDlgW));
    ldr.registerExport("comdlg32.dll", "PrintDlgExW", reinterpret_cast<void*>(PrintDlgExW));
    ldr.registerExport("comdlg32.dll", "ChooseColorW", reinterpret_cast<void*>(ChooseColorW));
    ldr.registerExport("comdlg32.dll", "ChooseColorA", reinterpret_cast<void*>(ChooseColorA));
    ldr.registerExport("comdlg32.dll", "ChooseFontA", reinterpret_cast<void*>(ChooseFontA));
    ldr.registerExport("comdlg32.dll", "ChooseFontW", reinterpret_cast<void*>(ChooseFontW));
    ldr.registerExport("comdlg32.dll", "GetOpenFileNameW", reinterpret_cast<void*>(GetOpenFileNameW));
    ldr.registerExport("comdlg32.dll", "GetOpenFileNameA", reinterpret_cast<void*>(GetOpenFileNameA));
    ldr.registerExport("comdlg32.dll", "GetSaveFileNameW", reinterpret_cast<void*>(GetSaveFileNameW));
    ldr.registerExport("comdlg32.dll", "GetSaveFileNameA", reinterpret_cast<void*>(GetSaveFileNameA));
    ldr.registerExport("comdlg32.dll", "CommDlgExtendedError", reinterpret_cast<void*>(CommDlgExtendedError));
    ldr.registerExport("comdlg32.dll", "ReplaceTextW", reinterpret_cast<void*>(ReplaceTextW));
    ldr.registerExport("comdlg32.dll", "FindTextW", reinterpret_cast<void*>(FindTextW));
}

} // namespace micant::comdlg32
