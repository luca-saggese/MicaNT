/**
 * @file imm32.cpp
 * @brief IMM32 DLL implementation, split from the former combined
 *        User32/GDI32/IMM32 translation unit.
 */

#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"
#include "micant/shared/win32k_client.h"

#include <algorithm>
#include <array>


#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" int32_t DllMain(void*, unsigned long, void*) noexcept { return 1; }
#endif

namespace micant::um::winnt_user32_gdi32_imm32_abi {

int32_t Gdi32_GetDeviceCaps(void*, int32_t) noexcept;


namespace {
void updateAnimatedCursor() noexcept {}

template <typename T> T Win32kProc(const char* name) noexcept {
#define MICA_WIN32K_PROC(exportName) if (std::strcmp(name, #exportName) == 0) return reinterpret_cast<T>(&exportName)
    MICA_WIN32K_PROC(MicaWin32kGetSystemMetrics);
    MICA_WIN32K_PROC(MicaWin32kCreateWindowExW);
    MICA_WIN32K_PROC(MicaWin32kDestroyWindow);
    MICA_WIN32K_PROC(MicaWin32kShowWindow);
    MICA_WIN32K_PROC(MicaWin32kSetWindowPos);
    MICA_WIN32K_PROC(MicaWin32kPostMessageW);
    MICA_WIN32K_PROC(MicaWin32kSendMessageW);
    MICA_WIN32K_PROC(MicaWin32kPostQuitMessage);
    MICA_WIN32K_PROC(MicaWin32kPeekMessageW);
    MICA_WIN32K_PROC(MicaWin32kGetMessageW);
    MICA_WIN32K_PROC(MicaWin32kGetClientRect);
    MICA_WIN32K_PROC(MicaWin32kGetWindowRect);
    MICA_WIN32K_PROC(MicaWin32kAdjustWindowRectEx);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
    MICA_WIN32K_PROC(MicaWin32kDebugInputTrace);
#endif
    MICA_WIN32K_PROC(MicaWin32kEnumWindows);
    MICA_WIN32K_PROC(MicaWin32kGetDlgItem);
    MICA_WIN32K_PROC(MicaWin32kGetWindowTextW);
    MICA_WIN32K_PROC(MicaWin32kIsWindowVisible);
    MICA_WIN32K_PROC(MicaWin32kIsIconic);
    MICA_WIN32K_PROC(MicaWin32kGetForegroundWindow);
    MICA_WIN32K_PROC(MicaWin32kSetFocus);
    MICA_WIN32K_PROC(MicaWin32kGetFocus);
    MICA_WIN32K_PROC(MicaWin32kGetCapture);
    MICA_WIN32K_PROC(MicaWin32kSetCapture);
    MICA_WIN32K_PROC(MicaWin32kReleaseCapture);
    MICA_WIN32K_PROC(MicaWin32kGetCursorPos);
    MICA_WIN32K_PROC(MicaWin32kGetClipCursor);
    MICA_WIN32K_PROC(MicaWin32kSetCursorPos);
    MICA_WIN32K_PROC(MicaWin32kClipCursor);
    MICA_WIN32K_PROC(MicaWin32kShowCursor);
    MICA_WIN32K_PROC(MicaWin32kSetCursor);
    MICA_WIN32K_PROC(MicaWin32kGetCursor);
    MICA_WIN32K_PROC(MicaWin32kGetCursorInfo);
    MICA_WIN32K_PROC(MicaWin32kSetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kSetWindowIconData);
    MICA_WIN32K_PROC(MicaWin32kGetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kSetWindowTitleSurface);
    MICA_WIN32K_PROC(MicaWin32kRegisterClassW);
    MICA_WIN32K_PROC(MicaWin32kDispatchMessageW);
    MICA_WIN32K_PROC(MicaWin32kSetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kGetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kDefWindowProcW);
    MICA_WIN32K_PROC(MicaWin32kGetDC);
    MICA_WIN32K_PROC(MicaWin32kGetDCSurface);
    MICA_WIN32K_PROC(MicaWin32kReleaseDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleDC);
    MICA_WIN32K_PROC(MicaWin32kDeleteDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleBitmap);
    MICA_WIN32K_PROC(MicaWin32kSelectObject);
    MICA_WIN32K_PROC(MicaWin32kBitBlt);
    MICA_WIN32K_PROC(MicaWin32kPresentDC);
    MICA_WIN32K_PROC(MicaWin32kPresentDCRect);
    MICA_WIN32K_PROC(MicaWin32kDeleteObject);
    MICA_WIN32K_PROC(MicaWin32kGetStockObject);
    MICA_WIN32K_PROC(MicaWin32kCreateDibSection);
    MICA_WIN32K_PROC(MicaWin32kFillRect);
#undef MICA_WIN32K_PROC
    return nullptr;
}


} // namespace


// imm32.dll implementations
void* Imm32_ImmGetContext(void* hWnd) noexcept {
    (void)hWnd;
    return nullptr;
}

int32_t Imm32_ImmReleaseContext(void* /*hWnd*/, void* /*hIMC*/) noexcept {
    return 0;
}

void* Imm32_ImmAssociateContext(void* /*hWnd*/, void* /*hIMC*/) noexcept {
    return nullptr;
}

int32_t Imm32_ImmAssociateContextEx(void* /*hWnd*/, void* /*hIMC*/, uint32_t /*dwFlags*/) noexcept {
    return 0;
}

int32_t Imm32_ImmGetCompositionStringW(void* /*hIMC*/, uint32_t /*dwIndex*/, void* /*lpBuf*/, uint32_t /*dwBufLen*/) noexcept {
    return -2; // IMM_ERROR_GENERAL: no composition context provider is connected.
}

int32_t Imm32_ImmSetCompositionWindow(void* /*hIMC*/, const void* /*lpCompForm*/) noexcept {
    return 0;
}

int32_t Imm32_ImmGetOpenStatus(void* /*hIMC*/) noexcept {
    return 0;
}

int32_t Imm32_ImmSetOpenStatus(void* /*hIMC*/, int32_t /*fOpen*/) noexcept {
    return 0;
}

} // namespace micant::um::winnt_user32_gdi32_imm32_abi

extern "C" {
__declspec(dllexport) void* ImmGetContext(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetContext(hWnd);
}
__declspec(dllexport) int32_t ImmReleaseContext(void* hWnd, void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmReleaseContext(hWnd, hIMC);
}
__declspec(dllexport) void* ImmAssociateContext(void* hWnd, void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmAssociateContext(hWnd, hIMC);
}
__declspec(dllexport) int32_t ImmAssociateContextEx(void* hWnd, void* hIMC, uint32_t dwFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmAssociateContextEx(hWnd, hIMC, dwFlags);
}
__declspec(dllexport) int32_t ImmGetCompositionStringW(void* hIMC, uint32_t dwIndex, void* lpBuf, uint32_t dwBufLen) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetCompositionStringW(hIMC, dwIndex, lpBuf, dwBufLen);
}
__declspec(dllexport) int32_t ImmSetCompositionWindow(void* hIMC, const void* lpCompForm) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmSetCompositionWindow(hIMC, lpCompForm);
}
__declspec(dllexport) int32_t ImmGetOpenStatus(void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetOpenStatus(hIMC);
}
__declspec(dllexport) int32_t ImmSetOpenStatus(void* hIMC, int32_t fOpen) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmSetOpenStatus(hIMC, fOpen);
}
} // extern "C"
