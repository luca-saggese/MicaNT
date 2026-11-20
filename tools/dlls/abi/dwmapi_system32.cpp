/**
 * @file dwmapi_system32.cpp
 * @brief Freestanding dwmapi.dll exports.
 */
#include <cstdint>

namespace {
constexpr int32_t kEInvalidArg = static_cast<int32_t>(0x80070057u);
constexpr int32_t kENotImpl = static_cast<int32_t>(0x80004001u);

constexpr uint32_t kDwmAttributeNcRenderingEnabled = 1;
constexpr uint32_t kDwmAttributeNcRenderingPolicy = 2;
constexpr uint32_t kDwmAttributeTransitionsForceDisabled = 3;
constexpr uint32_t kDwmAttributeAllowNcPaint = 4;
constexpr uint32_t kDwmAttributeCaptionButtonBounds = 5;
constexpr uint32_t kDwmAttributeNonClientRtlLayout = 6;
constexpr uint32_t kDwmAttributeForceIconicRepresentation = 7;
constexpr uint32_t kDwmAttributeFlip3dPolicy = 8;
constexpr uint32_t kDwmAttributeExtendedFrameBounds = 9;
constexpr uint32_t kDwmAttributeHasIconicBitmap = 10;
constexpr uint32_t kDwmAttributeDisallowPeek = 11;
constexpr uint32_t kDwmAttributeExcludedFromPeek = 12;
constexpr uint32_t kDwmAttributeCloak = 13;
constexpr uint32_t kDwmAttributeCloaked = 14;
constexpr uint32_t kDwmAttributeFreezeRepresentation = 15;
constexpr uint32_t kDwmAttributePassiveUpdateMode = 16;
constexpr uint32_t kDwmAttributeUseHostBackdropBrush = 17;
constexpr uint32_t kDwmAttributeUseImmersiveDarkMode = 20;
constexpr uint32_t kDwmAttributeWindowCornerPreference = 33;
constexpr uint32_t kDwmAttributeBorderColor = 34;
constexpr uint32_t kDwmAttributeCaptionColor = 35;
constexpr uint32_t kDwmAttributeTextColor = 36;
constexpr uint32_t kDwmAttributeVisibleFrameBorderThickness = 37;
constexpr uint32_t kDwmAttributeSystemBackdropType = 38;
constexpr uint32_t kDwmAttributeRedirectionBitmapAlpha = 39;
constexpr uint32_t kDwmAttributeBorderMargins = 40;

constexpr uint32_t kBoolSize = sizeof(int32_t);
constexpr uint32_t kEnumSize = sizeof(uint32_t);
constexpr uint32_t kRectSize = 4u * sizeof(int32_t);

uint32_t getWindowAttributeSize(uint32_t attribute) noexcept {
    switch (attribute) {
    case kDwmAttributeNcRenderingEnabled:
    case kDwmAttributeCloaked:
    case kDwmAttributeVisibleFrameBorderThickness:
    case kDwmAttributeSystemBackdropType:
        return kEnumSize;
    case kDwmAttributeCaptionButtonBounds:
    case kDwmAttributeExtendedFrameBounds:
        return kRectSize;
    default:
        return 0;
    }
}

uint32_t setWindowAttributeSize(uint32_t attribute) noexcept {
    switch (attribute) {
    case kDwmAttributeNcRenderingPolicy:
    case kDwmAttributeTransitionsForceDisabled:
    case kDwmAttributeAllowNcPaint:
    case kDwmAttributeNonClientRtlLayout:
    case kDwmAttributeForceIconicRepresentation:
    case kDwmAttributeFlip3dPolicy:
    case kDwmAttributeHasIconicBitmap:
    case kDwmAttributeDisallowPeek:
    case kDwmAttributeExcludedFromPeek:
    case kDwmAttributeCloak:
    case kDwmAttributeFreezeRepresentation:
    case kDwmAttributePassiveUpdateMode:
    case kDwmAttributeUseHostBackdropBrush:
    case kDwmAttributeUseImmersiveDarkMode:
    case kDwmAttributeWindowCornerPreference:
    case kDwmAttributeBorderColor:
    case kDwmAttributeCaptionColor:
    case kDwmAttributeTextColor:
    case kDwmAttributeSystemBackdropType:
    case kDwmAttributeRedirectionBitmapAlpha:
        return kEnumSize;
    case kDwmAttributeBorderMargins:
        return kRectSize;
    default:
        // Includes NCRENDERING_ENABLED, which Microsoft documents as a
        // getter-only attribute, and values outside the public enum.
        return 0;
    }
}
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
__declspec(dllexport) int32_t DwmGetWindowAttribute(void* hwnd, uint32_t attribute, void* value, uint32_t bytes) {
    const uint32_t expectedBytes = getWindowAttributeSize(attribute);
    if (!hwnd || !value || !expectedBytes || bytes != expectedBytes)
        return kEInvalidArg;
    return kENotImpl;
}
__declspec(dllexport) int32_t DwmSetWindowAttribute(void* hwnd, uint32_t attribute, const void* value, uint32_t bytes) {
    const uint32_t expectedBytes = setWindowAttributeSize(attribute);
    if (!hwnd || !value || !expectedBytes || bytes != expectedBytes)
        return kEInvalidArg;
    return kENotImpl;
}
__declspec(dllexport) int32_t DwmGetCompositionTimingInfo(void*, void* info) {
    return info ? kENotImpl : kEInvalidArg;
}
}
