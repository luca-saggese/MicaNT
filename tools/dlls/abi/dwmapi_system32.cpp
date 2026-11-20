/**
 * @file dwmapi_system32.cpp
 * @brief Freestanding dwmapi.dll exports.
 */
#include <cstdint>

namespace {
constexpr int32_t kEInvalidArg = static_cast<int32_t>(0x80070057u);
constexpr int32_t kENotImpl = static_cast<int32_t>(0x80004001u);
}

extern "C" {
__declspec(dllexport) int32_t DwmIsCompositionEnabled(int32_t* enabled) {
    return enabled ? kENotImpl : kEInvalidArg;
}
__declspec(dllexport) int32_t DwmExtendFrameIntoClientArea(void* hwnd, const void* margins) {
    return hwnd && margins ? kENotImpl : kEInvalidArg;
}
__declspec(dllexport) int32_t DwmEnableBlurBehindWindow(void* hwnd, const void* blur) {
    return hwnd && blur ? kENotImpl : kEInvalidArg;
}
__declspec(dllexport) int32_t DwmFlush() { return kENotImpl; }
__declspec(dllexport) int32_t DwmGetWindowAttribute(void* hwnd, uint32_t, void* value, uint32_t bytes) {
    return hwnd && value && bytes ? kENotImpl : kEInvalidArg;
}
__declspec(dllexport) int32_t DwmSetWindowAttribute(void* hwnd, uint32_t, const void* value, uint32_t bytes) {
    return hwnd && value && bytes ? kENotImpl : kEInvalidArg;
}
__declspec(dllexport) int32_t DwmGetCompositionTimingInfo(void*, void* info) {
    return info ? kENotImpl : kEInvalidArg;
}
}
