// ============================================================================
// MicaNT: Windows Theme and Visual Styles Subsystem (uxtheme.dll)
//
// Modern Clean-Room Implementation in pure ISO C++23.
// Provides UxTheme visual styling, buffered animation, and dark mode hooks.
// ============================================================================

#pragma once

#include <cstdint>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/theme_snapshot.h"
#include "micant/shared/win32k_client.h"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/winuser/user32.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::uxtheme {

using LPCWSTR = const wchar_t*;
using HTHEME = void*;
using HPAINTBUFFER = void*;
using HANAnimation = void*;

// UxTheme handles are process-local tokens. Each open handle pins the immutable
// snapshot it was opened against, so a later theme change cannot invalidate a
// query already in progress.
namespace detail {
struct ThemeHandle {
    MicaThemeSnapshot snapshot{};
    uint32_t generation{1};
    uint8_t control{};
    bool occupied{};
};
inline std::array<ThemeHandle, 64> handles{};
inline MicaThemeSnapshot activeSnapshot{};
inline uint32_t nextGeneration{1};

constexpr win32::HRESULT kInvalidArgument = static_cast<win32::HRESULT>(0x80070057u);
constexpr win32::HRESULT kInvalidHandle = static_cast<win32::HRESULT>(0x80070006u);
constexpr win32::HRESULT kNotFound = static_cast<win32::HRESULT>(0x80070490u);
constexpr win32::HRESULT kNotImplemented = static_cast<win32::HRESULT>(0x80004001u);

inline bool publish(const MicaThemeSnapshot& snapshot) noexcept {
    if (!MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot))) return false;
    activeSnapshot = snapshot;
    return true;
}
inline void* token(size_t slot, uint32_t generation) noexcept {
    const uintptr_t value = (static_cast<uintptr_t>(generation) << 8) | (slot + 1);
    return reinterpret_cast<void*>(value);
}
inline ThemeHandle* lookup(HTHEME handle) noexcept {
    const uintptr_t value = reinterpret_cast<uintptr_t>(handle);
    const uintptr_t encodedSlot = value & 0xFFu;
    const uint32_t generation = static_cast<uint32_t>(value >> 8);
    if (!encodedSlot || encodedSlot > handles.size() || !generation) return nullptr;
    auto& entry = handles[encodedSlot - 1];
    return entry.occupied && entry.generation == generation ? &entry : nullptr;
}
inline bool classControl(LPCWSTR classes, uint8_t& control) noexcept {
    if (!classes) return false;
    // GTK-independent mapping to the control order in theme_snapshot.h.
    constexpr std::pair<std::wstring_view, uint8_t> names[] = {
        {L"BUTTON", 0}, {L"EDIT", 1}, {L"CHECKBOX", 2}, {L"RADIO", 3},
        {L"COMBOBOX", 4}, {L"MENU", 5}, {L"TAB", 6}, {L"PROGRESS", 7},
        {L"SCROLLBAR", 8}, {L"TRACKBAR", 9}, {L"TOOLBAR", 10},
        {L"STATUS", 11}, {L"TOOLTIP", 12}, {L"HEADER", 13}, {L"WINDOW", 14},
    };
    const wchar_t* begin = classes;
    while (*begin) {
        while (*begin == L' ' || *begin == L'\t' || *begin == L';') ++begin;
        const wchar_t* end = begin;
        while (*end && *end != L';') ++end;
        const std::wstring_view candidate(begin, static_cast<size_t>(end - begin));
        for (const auto& name : names) {
            if (candidate.size() != name.first.size()) continue;
            bool equal = true;
            for (size_t i = 0; i < candidate.size(); ++i) {
                wchar_t c = candidate[i];
                if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
                if (c != name.first[i]) { equal = false; break; }
            }
            if (equal) { control = name.second; return true; }
        }
        begin = end;
    }
    return false;
}
inline uint8_t stateIndex(uint8_t control, int state) noexcept {
    if (state == 1) return 0;
    if (control == 2 || control == 3) {
        switch (state) {
            case 2: return 1;
            case 3: return 2;
            case 4: return 4;
            case 5: case 6: return 5;
            case 7: return 2;
            case 8: return 4;
            default: return 0xFF;
        }
    }
    // Map the documented common-control state groups into the theme snapshot.
    switch (state) {
        case 2: return 1; // hot
        case 3: return 2; // pressed
        case 4: return 4; // disabled
        case 5: return (control == 1 || control == 4) ? 3 : 0; // edit/combo focused
        case 7: return 6; // selected
        default: return 0xFF;
    }
}
inline const MicaThemeStyleSnapshot* style(const ThemeHandle& handle, int state) noexcept {
    const uint8_t stateSlot = stateIndex(handle.control, state);
    if (stateSlot >= MICANT_THEME_STATE_COUNT) return nullptr;
    const size_t index = static_cast<size_t>(handle.control) * MICANT_THEME_STATE_COUNT + stateSlot;
    return index < MICANT_THEME_STYLE_COUNT ? &handle.snapshot.styles[index] : nullptr;
}
}

inline bool PublishActiveThemeSnapshot(const MicaThemeSnapshot& snapshot) noexcept {
    return detail::publish(snapshot);
}

inline HTHEME OpenThemeData(win32::HWND /*hwnd*/, LPCWSTR pszClassList) noexcept {
    MicaThemeSnapshot current{};
    if (MicaWin32kGetThemeSnapshot(&current)) detail::publish(current);
    if (!MicaThemeSnapshotIsValid(&detail::activeSnapshot, sizeof(detail::activeSnapshot))) return nullptr;
    uint8_t control{};
    if (!detail::classControl(pszClassList, control)) return nullptr;
    for (size_t i = 0; i < detail::handles.size(); ++i) {
        auto& entry = detail::handles[i];
        if (entry.occupied) continue;
        uint32_t generation = ++detail::nextGeneration;
        if (!generation) generation = ++detail::nextGeneration;
        entry.snapshot = detail::activeSnapshot;
        entry.control = control;
        entry.generation = generation;
        entry.occupied = true;
        return detail::token(i, generation);
    }
    return nullptr;
}

inline win32::HRESULT CloseThemeData(HTHEME hTheme) noexcept {
    auto* entry = detail::lookup(hTheme);
    if (!entry) return detail::kInvalidHandle;
    entry->occupied = false;
    entry->snapshot = {};
    return 0;
}

inline win32::HRESULT GetThemeColor(HTHEME hTheme, int iPartId, int iStateId, int iPropId, win32::DWORD* pColor) noexcept {
    if (!pColor) return detail::kInvalidArgument;
    auto* handle = detail::lookup(hTheme);
    if (!handle) return detail::kInvalidHandle;
    if (iPartId != 1) return detail::kNotImplemented;
    const auto* style = detail::style(*handle, iStateId);
    if (!style) return detail::kNotFound;
    uint32_t color{};
    if (iPropId == 3803 && (style->flags & MICANT_THEME_HAS_FOREGROUND)) color = style->foreground;
    else if (iPropId == 3802 && (style->flags & MICANT_THEME_HAS_BACKGROUND)) color = style->background;
    else if (iPropId == 3801 && (style->flags & MICANT_THEME_HAS_BORDER)) color = style->border;
    else return detail::kNotFound;
    *pColor = ((color >> 16) & 0xFFu) | (color & 0xFF00u) | ((color & 0xFFu) << 16);
    return 0;
}

inline win32::HRESULT GetThemeFont(HTHEME /*hTheme*/, void* /*hdc*/, int /*iPartId*/, int /*iStateId*/, int /*iPropId*/, void* /*pFont*/) noexcept {
    return detail::kNotImplemented;
}

inline win32::HRESULT GetThemePartSize(HTHEME hTheme, void* /*hdc*/, int iPartId, int iStateId, void* /*prc*/, int eSize, void* psz) noexcept {
    if (!psz || eSize < 0 || eSize > 2) return detail::kInvalidArgument;
    if (eSize != 0) return detail::kNotImplemented; // TS_MIN only
    auto* handle = detail::lookup(hTheme);
    if (!handle) return detail::kInvalidHandle;
    if (iPartId != 1) return detail::kNotImplemented;
    const auto* style = detail::style(*handle, iStateId);
    if (!style) return detail::kNotFound;
    int32_t width = (style->flags & MICANT_THEME_HAS_MIN_WIDTH) ? style->minWidth : 0;
    int32_t height = (style->flags & MICANT_THEME_HAS_MIN_HEIGHT) ? style->minHeight : 0;
    if (style->flags & MICANT_THEME_HAS_PADDING) {
        width += style->padding[1] + style->padding[3];
        height += style->padding[0] + style->padding[2];
    }
    if (!width && !height) return detail::kNotFound;
    auto* size = reinterpret_cast<int32_t*>(psz);
    size[0] = width;
    size[1] = height;
    return 0;
}

inline win32::HRESULT GetThemeBackgroundContentRect(HTHEME hTheme, void* /*hdc*/, int iPartId, int iStateId, const void* pBoundingRect, void* pContentRect) noexcept {
    if (!pBoundingRect || !pContentRect) return detail::kInvalidArgument;
    auto* handle = detail::lookup(hTheme);
    if (!handle) return detail::kInvalidHandle;
    if (iPartId != 1) return detail::kNotImplemented;
    const auto* style = detail::style(*handle, iStateId);
    if (!style || !(style->flags & MICANT_THEME_HAS_PADDING)) return detail::kNotFound;
    const auto& bounds = *static_cast<const user32::RECT*>(pBoundingRect);
    const int64_t left = int64_t(bounds.left) + style->padding[3];
    const int64_t top = int64_t(bounds.top) + style->padding[0];
    const int64_t right = int64_t(bounds.right) - style->padding[1];
    const int64_t bottom = int64_t(bounds.bottom) - style->padding[2];
    constexpr int64_t min = std::numeric_limits<int32_t>::min();
    constexpr int64_t max = std::numeric_limits<int32_t>::max();
    if (right < left || bottom < top || left < min || left > max || top < min || top > max ||
        right < min || right > max || bottom < min || bottom > max) return detail::kInvalidArgument;
    *static_cast<user32::RECT*>(pContentRect) = {
        static_cast<int32_t>(left), static_cast<int32_t>(top),
        static_cast<int32_t>(right), static_cast<int32_t>(bottom)};
    return 0;
}

inline win32::HRESULT DrawThemeBackground(HTHEME /*hTheme*/, void* /*hdc*/, int /*iPartId*/, int /*iStateId*/, const void* /*pRect*/, const void* /*pClipRect*/) noexcept {
    return detail::kNotImplemented;
}

inline win32::HRESULT DrawThemeParentBackground(win32::HWND /*hwnd*/, void* /*hdc*/, const void* /*prc*/) noexcept {
    return detail::kNotImplemented;
}

inline win32::HRESULT DrawThemeTextEx(HTHEME /*hTheme*/, void* /*hdc*/, int /*iPartId*/, int /*iStateId*/, LPCWSTR /*pszText*/, int /*iCharCount*/, win32::DWORD /*dwFlags*/, void* /*pRect*/, void* /*pOptions*/) noexcept {
    return detail::kNotImplemented;
}

inline win32::HRESULT SetWindowTheme(win32::HWND /*hwnd*/, LPCWSTR /*pszSubAppName*/, LPCWSTR /*pszSubIdList*/) noexcept {
    return detail::kNotImplemented;
}

inline win32::HRESULT EnableThemeDialogTexture(win32::HWND /*hwnd*/, win32::DWORD /*dwFlags*/) noexcept {
    return detail::kNotImplemented;
}

inline win32::HRESULT GetThemeTransitionDuration(HTHEME /*hTheme*/, int /*iPartId*/, int /*iStateIdFrom*/, int /*iStateIdTo*/, int /*iPropId*/, win32::DWORD* pdwDuration) noexcept {
    if (pdwDuration) *pdwDuration = 0;
    return detail::kNotImplemented;
}

inline HANAnimation BeginBufferedAnimation(win32::HWND /*hwnd*/, void* /*hdcTarget*/, const void* /*rcTarget*/, int /*dwFormat*/, void* /*pPaintParams*/, void* /*pAnimationParams*/, void* /*phdcFrom*/, void* /*phdcTo*/) noexcept {
    return nullptr;
}

inline win32::HRESULT EndBufferedAnimation(HANAnimation /*hbpAnimation*/, win32::BOOL /*fUpdateTarget*/) noexcept {
    return 0;
}

inline win32::BOOL BufferedPaintRenderAnimation(win32::HWND /*hwnd*/, void* /*hdcTarget*/) noexcept {
    return win32::FALSE;
}

inline win32::HRESULT BufferedPaintStopAllAnimations(win32::HWND /*hwnd*/) noexcept {
    return 0;
}

inline win32::HRESULT GetThemeBackgroundExtent(HTHEME hTheme, void* /*hdc*/, int iPartId, int iStateId, const void* pContentRect, void* pExtentRect) noexcept {
    if (!pContentRect || !pExtentRect) return detail::kInvalidArgument;
    auto* handle = detail::lookup(hTheme);
    if (!handle) return detail::kInvalidHandle;
    if (iPartId != 1) return detail::kNotImplemented;
    const auto* style = detail::style(*handle, iStateId);
    if (!style || !(style->flags & MICANT_THEME_HAS_PADDING)) return detail::kNotFound;
    const auto& content = *static_cast<const user32::RECT*>(pContentRect);
    const int64_t left = int64_t(content.left) - style->padding[3];
    const int64_t top = int64_t(content.top) - style->padding[0];
    const int64_t right = int64_t(content.right) + style->padding[1];
    const int64_t bottom = int64_t(content.bottom) + style->padding[2];
    constexpr int64_t min = std::numeric_limits<int32_t>::min();
    constexpr int64_t max = std::numeric_limits<int32_t>::max();
    if (left < min || left > max || top < min || top > max || right < min || right > max ||
        bottom < min || bottom > max) return detail::kInvalidArgument;
    *static_cast<user32::RECT*>(pExtentRect) = {
        static_cast<int32_t>(left), static_cast<int32_t>(top),
        static_cast<int32_t>(right), static_cast<int32_t>(bottom)};
    return 0;
}

inline win32::DWORD GetThemeSysColor(HTHEME /*hTheme*/, int /*iColorId*/) noexcept {
    return 0xFFFFFFFFu; // CLR_INVALID: no fabricated system-color value
}

inline win32::HRESULT GetThemeSysFont(HTHEME /*hTheme*/, int /*iFontId*/, void* /*plf*/) noexcept {
    return detail::kNotImplemented;
}

struct MARGINS {
    int32_t cxLeftWidth{2};
    int32_t cxRightWidth{2};
    int32_t cyTopHeight{2};
    int32_t cyBottomHeight{2};
};

inline win32::BOOL IsThemeActive() noexcept {
    MicaThemeSnapshot current{};
    if (MicaWin32kGetThemeSnapshot(&current)) detail::publish(current);
    return MicaThemeSnapshotIsValid(&detail::activeSnapshot, sizeof(detail::activeSnapshot))
        ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL IsAppThemed() noexcept {
    return IsThemeActive();
}

inline win32::HRESULT GetThemeMargins(HTHEME hTheme, void* /*hdc*/, int iPartId, int iStateId, int iPropId, const void* /*prc*/, MARGINS* pMargins) noexcept {
    if (!pMargins) return detail::kInvalidArgument;
    auto* handle = detail::lookup(hTheme);
    if (!handle) return detail::kInvalidHandle;
    if (iPartId != 1) return detail::kNotImplemented;
    if (iPropId != 3602) return detail::kNotFound; // TMT_CONTENTMARGINS
    const auto* style = detail::style(*handle, iStateId);
    if (!style || !(style->flags & MICANT_THEME_HAS_PADDING)) return detail::kNotFound;
    pMargins->cyTopHeight = style->padding[0];
    pMargins->cxRightWidth = style->padding[1];
    pMargins->cyBottomHeight = style->padding[2];
    pMargins->cxLeftWidth = style->padding[3];
    return 0;
}

inline win32::HRESULT GetThemeInt(HTHEME hTheme, int iPartId, int iStateId, int iPropId, int* piVal) noexcept {
    if (!piVal) return detail::kInvalidArgument;
    auto* handle = detail::lookup(hTheme);
    if (!handle) return detail::kInvalidHandle;
    if (iPartId != 1) return detail::kNotImplemented;
    const auto* style = detail::style(*handle, iStateId);
    if (!style) return detail::kNotFound;
    if (iPropId == 2403 && (style->flags & MICANT_THEME_HAS_BORDER_WIDTH)) {
        *piVal = style->borderWidth; // TMT_BORDERSIZE
        return 0;
    }
    if ((iPropId == 2404 || iPropId == 2405) && (style->flags & MICANT_THEME_HAS_RADIUS)) {
        *piVal = style->borderRadius; // TMT_ROUNDCORNERWIDTH / HEIGHT
        return 0;
    }
    return detail::kNotFound;
}

inline win32::HRESULT DrawThemeText(HTHEME /*hTheme*/, void* /*hdc*/, int /*iPartId*/, int /*iStateId*/, LPCWSTR /*pszText*/, int /*iCharCount*/, win32::DWORD /*dwTextFlags*/, win32::DWORD /*dwTextFlags2*/, const void* /*pRect*/) noexcept {
    return detail::kNotImplemented;
}

inline win32::BOOL IsThemeBackgroundPartiallyTransparent(HTHEME hTheme, int iPartId, int iStateId) noexcept {
    auto* handle = detail::lookup(hTheme);
    if (!handle || iPartId != 1) return win32::FALSE;
    const auto* style = detail::style(*handle, iStateId);
    return style && (style->flags & MICANT_THEME_HAS_BACKGROUND) &&
           (style->background >> 24) < 255u ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL IsThemePartDefined(HTHEME hTheme, int iPartId, int iStateId) noexcept {
    auto* handle = detail::lookup(hTheme);
    if (!handle || iPartId != 1) return win32::FALSE;
    const auto* style = detail::style(*handle, iStateId);
    return style && style->flags ? win32::TRUE : win32::FALSE;
}

inline win32::HRESULT WINAPI GetThemeBackgroundRegion(HTHEME /*hTheme*/, void* /*hdc*/, int32_t /*iPartId*/, int32_t /*iStateId*/, const void* /*pRect*/, void** pRegion) noexcept {
    if (pRegion) *pRegion = nullptr;
    return detail::kNotImplemented;
}

inline win32::BOOL WINAPI ThemeOrdinal47(HTHEME /*hTheme*/, int32_t /*iPartId*/, int32_t /*iStateId*/, void* /*pRect*/) noexcept {
    return win32::FALSE;
}

inline win32::HRESULT WINAPI GetThemeBool(HTHEME /*hTheme*/, int32_t /*iPartId*/, int32_t /*iStateId*/, int32_t /*iPropId*/, win32::BOOL* pfVal) noexcept {
    if (pfVal) *pfVal = win32::FALSE;
    return detail::kNotImplemented;
}

inline win32::HRESULT WINAPI GetThemePropertyOrigin(HTHEME /*hTheme*/, int32_t /*iPartId*/, int32_t /*iStateId*/, int32_t /*iPropId*/, int32_t* pOrigin) noexcept {
    if (pOrigin) *pOrigin = 4; // PO_NOTFOUND
    return detail::kNotImplemented;
}

inline win32::HRESULT WINAPI GetThemeEnumValue(HTHEME /*hTheme*/, int32_t /*iPartId*/, int32_t /*iStateId*/, int32_t /*iPropId*/, int32_t* piVal) noexcept {
    if (piVal) *piVal = 0;
    return detail::kNotImplemented;
}

inline win32::HRESULT WINAPI GetCurrentThemeName(wchar_t* pszThemeFileName, int32_t cchMaxNameChars, wchar_t* pszColorBuff, int32_t cchMaxColorChars, wchar_t* pszSizeBuff, int32_t cchMaxSizeChars) noexcept {
    if (pszThemeFileName && cchMaxNameChars > 0) pszThemeFileName[0] = L'\0';
    if (pszColorBuff && cchMaxColorChars > 0) pszColorBuff[0] = L'\0';
    if (pszSizeBuff && cchMaxSizeChars > 0) pszSizeBuff[0] = L'\0';
    return detail::kNotImplemented;
}

inline win32::HRESULT WINAPI BufferedPaintInit() noexcept { return detail::kNotImplemented; }
inline win32::HRESULT WINAPI BufferedPaintUnInit() noexcept { return detail::kNotImplemented; }
inline HPAINTBUFFER WINAPI BeginBufferedPaint(void* /*hdcTarget*/, const void* /*prcTarget*/, int32_t /*dwFormat*/, void* /*pPaintParams*/, void** phdc) noexcept {
    if (phdc) *phdc = nullptr;
    return nullptr;
}
inline win32::HRESULT WINAPI EndBufferedPaint(HPAINTBUFFER /*hBufferedPaint*/, int32_t /*fUpdateTarget*/) noexcept { return detail::kNotImplemented; }
inline win32::HRESULT WINAPI BufferedPaintSetAlpha(HPAINTBUFFER /*hBufferedPaint*/, const void* /*prcTarget*/, uint8_t /*alpha*/) noexcept { return detail::kNotImplemented; }
inline HTHEME WINAPI OpenThemeDataForDpi(win32::HWND hwnd, LPCWSTR pszClassList, uint32_t dpi) noexcept {
    if (dpi != 96) return nullptr;
    return OpenThemeData(hwnd, pszClassList);
}

inline void InitializeUxThemeSubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();

    ldr.registerExport("uxtheme.dll", "OpenThemeData", reinterpret_cast<void*>(OpenThemeData));
    ldr.registerExport("uxtheme.dll", "CloseThemeData", reinterpret_cast<void*>(CloseThemeData));
    ldr.registerExport("uxtheme.dll", "GetThemeColor", reinterpret_cast<void*>(GetThemeColor));
    ldr.registerExport("uxtheme.dll", "GetThemeFont", reinterpret_cast<void*>(GetThemeFont));
    ldr.registerExport("uxtheme.dll", "GetThemePartSize", reinterpret_cast<void*>(GetThemePartSize));
    ldr.registerExport("uxtheme.dll", "GetThemeBackgroundContentRect", reinterpret_cast<void*>(GetThemeBackgroundContentRect));
    ldr.registerExport("uxtheme.dll", "DrawThemeBackground", reinterpret_cast<void*>(DrawThemeBackground));
    ldr.registerExport("uxtheme.dll", "DrawThemeParentBackground", reinterpret_cast<void*>(DrawThemeParentBackground));
    ldr.registerExport("uxtheme.dll", "DrawThemeTextEx", reinterpret_cast<void*>(DrawThemeTextEx));
    ldr.registerExport("uxtheme.dll", "SetWindowTheme", reinterpret_cast<void*>(SetWindowTheme));
    ldr.registerExport("uxtheme.dll", "EnableThemeDialogTexture", reinterpret_cast<void*>(EnableThemeDialogTexture));
    ldr.registerExport("uxtheme.dll", "GetThemeTransitionDuration", reinterpret_cast<void*>(GetThemeTransitionDuration));
    ldr.registerExport("uxtheme.dll", "BeginBufferedAnimation", reinterpret_cast<void*>(BeginBufferedAnimation));
    ldr.registerExport("uxtheme.dll", "EndBufferedAnimation", reinterpret_cast<void*>(EndBufferedAnimation));
    ldr.registerExport("uxtheme.dll", "BufferedPaintRenderAnimation", reinterpret_cast<void*>(BufferedPaintRenderAnimation));
    ldr.registerExport("uxtheme.dll", "BufferedPaintStopAllAnimations", reinterpret_cast<void*>(BufferedPaintStopAllAnimations));
    ldr.registerExport("uxtheme.dll", "GetThemeBackgroundExtent", reinterpret_cast<void*>(GetThemeBackgroundExtent));
    ldr.registerExport("uxtheme.dll", "GetThemeSysColor", reinterpret_cast<void*>(GetThemeSysColor));
    ldr.registerExport("uxtheme.dll", "GetThemeSysFont", reinterpret_cast<void*>(GetThemeSysFont));
    ldr.registerExport("uxtheme.dll", "IsThemePartDefined", reinterpret_cast<void*>(IsThemePartDefined));
    ldr.registerExport("uxtheme.dll", "IsThemeActive", reinterpret_cast<void*>(IsThemeActive));
    ldr.registerExport("uxtheme.dll", "IsAppThemed", reinterpret_cast<void*>(IsAppThemed));
    ldr.registerExport("uxtheme.dll", "GetThemeMargins", reinterpret_cast<void*>(GetThemeMargins));
    ldr.registerExport("uxtheme.dll", "GetThemeInt", reinterpret_cast<void*>(GetThemeInt));
    ldr.registerExport("uxtheme.dll", "DrawThemeText", reinterpret_cast<void*>(DrawThemeText));
    ldr.registerExport("uxtheme.dll", "IsThemeBackgroundPartiallyTransparent", reinterpret_cast<void*>(IsThemeBackgroundPartiallyTransparent));
    ldr.registerExport("uxtheme.dll", "GetThemeBackgroundRegion", reinterpret_cast<void*>(GetThemeBackgroundRegion));
    ldr.registerExportOrdinal("uxtheme.dll", 47, reinterpret_cast<void*>(ThemeOrdinal47));
    ldr.registerExport("uxtheme.dll", "GetThemeBool", reinterpret_cast<void*>(GetThemeBool));
    ldr.registerExport("uxtheme.dll", "GetThemePropertyOrigin", reinterpret_cast<void*>(GetThemePropertyOrigin));
    ldr.registerExport("uxtheme.dll", "GetThemeEnumValue", reinterpret_cast<void*>(GetThemeEnumValue));
    ldr.registerExport("uxtheme.dll", "GetCurrentThemeName", reinterpret_cast<void*>(GetCurrentThemeName));
    ldr.registerExport("uxtheme.dll", "BufferedPaintInit", reinterpret_cast<void*>(BufferedPaintInit));
    ldr.registerExport("uxtheme.dll", "BufferedPaintUnInit", reinterpret_cast<void*>(BufferedPaintUnInit));
    ldr.registerExport("uxtheme.dll", "BeginBufferedPaint", reinterpret_cast<void*>(BeginBufferedPaint));
    ldr.registerExport("uxtheme.dll", "EndBufferedPaint", reinterpret_cast<void*>(EndBufferedPaint));
    ldr.registerExport("uxtheme.dll", "BufferedPaintSetAlpha", reinterpret_cast<void*>(BufferedPaintSetAlpha));
    ldr.registerExport("uxtheme.dll", "OpenThemeDataForDpi", reinterpret_cast<void*>(OpenThemeDataForDpi));
}

} // namespace micant::uxtheme
