/**
 * @file dwmapi_system32.cpp
 * @brief Freestanding dwmapi.dll exports.
 */
#include <cstdint>

#include "micant/shared/user_syscall.hpp"
#include "micant/shared/win32k_client.h"

namespace {
constexpr int32_t kEInvalidArg = static_cast<int32_t>(0x80070057u);
constexpr int32_t kENotImpl = static_cast<int32_t>(0x80004001u);
constexpr int32_t kSOk = 0;
constexpr uint32_t kStatusSuccess = 0x00000000u;
constexpr uint32_t kStatusTimeout = 0x00000102u;
constexpr uint32_t kStatusDeviceNotReady = 0xC00000A3u;
constexpr uint32_t kStatusProcessIsTerminating = 0xC000010Au;
constexpr uint32_t kErrorTimeout = 1460u;
constexpr uint32_t kErrorNotReady = 21u;
constexpr uint32_t kErrorProcessAborted = 1067u;
constexpr uint32_t kCompositionProbeTimeoutMilliseconds = 1000u;
uint64_t g_dwmRequestSequence{};

int32_t statusToHresult(uint32_t status) noexcept {
    if (status == kStatusSuccess) return kSOk;
    if (status == kStatusTimeout)
        return static_cast<int32_t>(0x80070000u | kErrorTimeout);
    if (status == kStatusDeviceNotReady)
        return static_cast<int32_t>(0x80070000u | kErrorNotReady);
    if (status == kStatusProcessIsTerminating)
        return static_cast<int32_t>(0x80070000u | kErrorProcessAborted);
    // HRESULT_FROM_NT preserves the failing NT status in HRESULT form.
    return static_cast<int32_t>((status & 0x80000000u)
        ? (status | 0x10000000u) : 0x80004005u);
}

uint64_t nextDwmRequestSequence() noexcept {
    for (;;) {
        const uint64_t sequence = __atomic_add_fetch(
            &g_dwmRequestSequence, 1, __ATOMIC_RELAXED);
        if (sequence) return sequence;
    }
}

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
    if (!enabled) return kEInvalidArg;

    // Composition is enabled only after the canonical compositor has
    // published a frame and Win32k has acknowledged it. Keep the output
    // untouched when that live readiness check fails.
    MicaCompositionFlushLatestFrameRequest request{};
    request.header.size = sizeof(request);
    request.header.version = MICANT_COMPOSITION_PROTOCOL_VERSION;
    request.header.operation = MICANT_COMPOSITION_OP_FLUSH_LATEST_FRAME;
    request.header.requestSequence = nextDwmRequestSequence();
    request.timeoutMilliseconds = kCompositionProbeTimeoutMilliseconds;
    const auto result = static_cast<uint32_t>(micant::sys::invokeUserService(
        MICANT_WIN32K_SERVICE_WAIT_FOR_COMPOSITOR_FRAME,
        reinterpret_cast<uint64_t>(&request)));
    if (result != kStatusSuccess) return statusToHresult(result);
    *enabled = 1;
    return kSOk;
}
__declspec(dllexport) int32_t DwmExtendFrameIntoClientArea(void* hwnd, const void* margins) {
    return hwnd && margins ? kENotImpl : kEInvalidArg;
}
__declspec(dllexport) int32_t DwmEnableBlurBehindWindow(void* hwnd, const void* blur) {
    return hwnd && blur ? kENotImpl : kEInvalidArg;
}
__declspec(dllexport) int32_t DwmFlush() {
    MicaCompositionFlushLatestFrameRequest request{};
    request.header.size = sizeof(request);
    request.header.version = MICANT_COMPOSITION_PROTOCOL_VERSION;
    request.header.operation = MICANT_COMPOSITION_OP_FLUSH_LATEST_FRAME;
    request.header.requestSequence = nextDwmRequestSequence();
    request.timeoutMilliseconds = MICANT_COMPOSITION_MAX_WAIT_MILLISECONDS;
    const auto result = static_cast<uint32_t>(micant::sys::invokeUserService(
        MICANT_WIN32K_SERVICE_WAIT_FOR_COMPOSITOR_FRAME,
        reinterpret_cast<uint64_t>(&request)));
    return statusToHresult(result);
}
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
    if (attribute != kDwmAttributeTransitionsForceDisabled)
        return kENotImpl;

    MicaCompositionSetWindowTransitionsRequest request{};
    request.header.size = sizeof(request);
    request.header.version = MICANT_COMPOSITION_PROTOCOL_VERSION;
    request.header.operation =
        MICANT_COMPOSITION_OP_SET_WINDOW_TRANSITIONS_FORCEDISABLED;
    request.header.requestSequence = nextDwmRequestSequence();
    request.windowId = reinterpret_cast<uintptr_t>(hwnd);
    request.attribute = MICANT_DWM_WINDOW_ATTRIBUTE_TRANSITIONS_FORCEDISABLED;
    request.value = *static_cast<const int32_t*>(value);
    const auto result = static_cast<uint32_t>(MicaWin32kCompositionRequest(
        &request, sizeof(request)));
    return statusToHresult(result);
}
__declspec(dllexport) int32_t DwmGetCompositionTimingInfo(void*, void* info) {
    return info ? kENotImpl : kEInvalidArg;
}
}
