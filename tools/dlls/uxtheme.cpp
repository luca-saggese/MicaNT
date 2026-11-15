#include "micant/um/subsystems/winuser/uxtheme.hpp"

extern "C" {

bool DllMain(void*, unsigned long, void*) noexcept { return true; }

__declspec(dllexport) void* WINAPI OpenThemeData(void* hwnd, const wchar_t* classList) noexcept {
    return micant::uxtheme::OpenThemeData(hwnd, classList);
}
__declspec(dllexport) void* WINAPI OpenThemeDataForDpi(void* hwnd, const wchar_t* classList,
                                                        uint32_t dpi) noexcept {
    return micant::uxtheme::OpenThemeDataForDpi(hwnd, classList, dpi);
}
__declspec(dllexport) int32_t WINAPI CloseThemeData(void* theme) noexcept {
    return micant::uxtheme::CloseThemeData(theme);
}
__declspec(dllexport) int32_t WINAPI GetThemeColor(void* theme, int32_t part, int32_t state,
                                                   int32_t property, uint32_t* color) noexcept {
    return micant::uxtheme::GetThemeColor(theme, part, state, property, color);
}
__declspec(dllexport) int32_t WINAPI GetThemeFont(void* theme, void* dc, int32_t part,
        int32_t state, int32_t property, void* font) noexcept {
    return micant::uxtheme::GetThemeFont(theme, dc, part, state, property, font);
}
__declspec(dllexport) int32_t WINAPI GetThemePartSize(void* theme, void* dc, int32_t part,
                                                      int32_t state, void* rect, int32_t sizeType,
                                                      void* size) noexcept {
    return micant::uxtheme::GetThemePartSize(theme, dc, part, state, rect, sizeType, size);
}
__declspec(dllexport) int32_t WINAPI GetThemeMargins(void* theme, void* dc, int32_t part,
                                                     int32_t state, int32_t property,
                                                     const void* rect,
                                                     micant::uxtheme::MARGINS* margins) noexcept {
    return micant::uxtheme::GetThemeMargins(theme, dc, part, state, property, rect, margins);
}
__declspec(dllexport) int32_t WINAPI GetThemeInt(void* theme, int32_t part, int32_t state,
                                                 int32_t property, int32_t* value) noexcept {
    return micant::uxtheme::GetThemeInt(theme, part, state, property, value);
}
__declspec(dllexport) int32_t WINAPI DrawThemeBackground(void* theme, void* dc, int32_t part,
                                                         int32_t state, const void* rect,
                                                         const void* clip) noexcept {
    return micant::uxtheme::DrawThemeBackground(theme, dc, part, state, rect, clip);
}
__declspec(dllexport) int32_t WINAPI GetThemeBackgroundContentRect(void* theme, void* dc,
        int32_t part, int32_t state, const void* bounds, void* content) noexcept {
    return micant::uxtheme::GetThemeBackgroundContentRect(theme, dc, part, state, bounds, content);
}
__declspec(dllexport) int32_t WINAPI GetThemeBackgroundExtent(void* theme, void* dc,
        int32_t part, int32_t state, const void* content, void* extent) noexcept {
    return micant::uxtheme::GetThemeBackgroundExtent(theme, dc, part, state, content, extent);
}
__declspec(dllexport) int32_t WINAPI DrawThemeParentBackground(void* hwnd, void* dc,
        const void* rect) noexcept {
    return micant::uxtheme::DrawThemeParentBackground(hwnd, dc, rect);
}
__declspec(dllexport) int32_t WINAPI DrawThemeText(void* theme, void* dc, int32_t part,
        int32_t state, const wchar_t* text, int32_t charCount, uint32_t flags,
        uint32_t flags2, const void* rect) noexcept {
    return micant::uxtheme::DrawThemeText(theme, dc, part, state, text, charCount,
                                          flags, flags2, rect);
}
__declspec(dllexport) int32_t WINAPI DrawThemeTextEx(void* theme, void* dc, int32_t part,
        int32_t state, const wchar_t* text, int32_t charCount, uint32_t flags,
        void* rect, void* options) noexcept {
    return micant::uxtheme::DrawThemeTextEx(theme, dc, part, state, text, charCount,
                                            flags, rect, options);
}
__declspec(dllexport) int32_t WINAPI SetWindowTheme(void* hwnd, const wchar_t* subAppName,
        const wchar_t* subIdList) noexcept {
    return micant::uxtheme::SetWindowTheme(hwnd, subAppName, subIdList);
}
__declspec(dllexport) int32_t WINAPI EnableThemeDialogTexture(void* hwnd,
        uint32_t flags) noexcept {
    return micant::uxtheme::EnableThemeDialogTexture(hwnd, flags);
}
__declspec(dllexport) int32_t WINAPI GetThemeTransitionDuration(void* theme, int32_t part,
        int32_t fromState, int32_t toState, int32_t property, uint32_t* duration) noexcept {
    return micant::uxtheme::GetThemeTransitionDuration(theme, part, fromState,
                                                       toState, property, duration);
}
__declspec(dllexport) void* WINAPI BeginBufferedAnimation(void* hwnd, void* targetDc,
        const void* targetRect, int32_t format, void* paintParams, void* animationParams,
        void** fromDc, void** toDc) noexcept {
    return micant::uxtheme::BeginBufferedAnimation(hwnd, targetDc, targetRect, format,
        paintParams, animationParams, fromDc, toDc);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintRenderAnimation(void* hwnd,
        void* targetDc) noexcept {
    return micant::uxtheme::BufferedPaintRenderAnimation(hwnd, targetDc);
}
__declspec(dllexport) uint32_t WINAPI GetThemeSysColor(void* theme, int32_t colorId) noexcept {
    return micant::uxtheme::GetThemeSysColor(theme, colorId);
}
__declspec(dllexport) int32_t WINAPI GetThemeSysFont(void* theme, int32_t fontId,
        void* font) noexcept {
    return micant::uxtheme::GetThemeSysFont(theme, fontId, font);
}
__declspec(dllexport) int32_t WINAPI GetThemeBackgroundRegion(void* theme, void* dc,
        int32_t part, int32_t state, const void* rect, void** region) noexcept {
    return micant::uxtheme::GetThemeBackgroundRegion(theme, dc, part, state, rect, region);
}
__declspec(dllexport) int32_t WINAPI GetThemeBool(void* theme, int32_t part, int32_t state,
        int32_t property, int32_t* value) noexcept {
    return micant::uxtheme::GetThemeBool(theme, part, state, property, value);
}
__declspec(dllexport) int32_t WINAPI GetThemePropertyOrigin(void* theme, int32_t part,
        int32_t state, int32_t property, int32_t* origin) noexcept {
    return micant::uxtheme::GetThemePropertyOrigin(theme, part, state, property, origin);
}
__declspec(dllexport) int32_t WINAPI GetThemeEnumValue(void* theme, int32_t part,
        int32_t state, int32_t property, int32_t* value) noexcept {
    return micant::uxtheme::GetThemeEnumValue(theme, part, state, property, value);
}
__declspec(dllexport) int32_t WINAPI GetCurrentThemeName(wchar_t* themeFile,
        int32_t maxThemeChars, wchar_t* colorName, int32_t maxColorChars,
        wchar_t* sizeName, int32_t maxSizeChars) noexcept {
    return micant::uxtheme::GetCurrentThemeName(themeFile, maxThemeChars, colorName,
                                               maxColorChars, sizeName, maxSizeChars);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintInit() noexcept {
    return micant::uxtheme::BufferedPaintInit();
}
__declspec(dllexport) int32_t WINAPI BufferedPaintUnInit() noexcept {
    return micant::uxtheme::BufferedPaintUnInit();
}
__declspec(dllexport) void* WINAPI BeginBufferedPaint(void* targetDc, const void* targetRect,
        int32_t format, void* paintParams, void** paintDc) noexcept {
    return micant::uxtheme::BeginBufferedPaint(targetDc, targetRect, format,
                                               paintParams, paintDc);
}
__declspec(dllexport) int32_t WINAPI EndBufferedPaint(void* buffer,
        int32_t updateTarget) noexcept {
    return micant::uxtheme::EndBufferedPaint(buffer, updateTarget);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintSetAlpha(void* buffer,
        const void* targetRect, uint8_t alpha) noexcept {
    return micant::uxtheme::BufferedPaintSetAlpha(buffer, targetRect, alpha);
}
__declspec(dllexport) int32_t WINAPI DrawThemeBackgroundEx(void* theme, void* dc, int32_t part,
        int32_t state, const void* rect, const void* options) noexcept {
    return micant::uxtheme::DrawThemeBackgroundEx(theme, dc, part, state, rect, options);
}
__declspec(dllexport) int32_t WINAPI IsThemeActive() noexcept {
    return micant::uxtheme::IsThemeActive();
}
__declspec(dllexport) int32_t WINAPI IsAppThemed() noexcept {
    return micant::uxtheme::IsAppThemed();
}
__declspec(dllexport) int32_t WINAPI IsThemePartDefined(void* theme, int32_t part,
                                                       int32_t state) noexcept {
    return micant::uxtheme::IsThemePartDefined(theme, part, state);
}
__declspec(dllexport) int32_t WINAPI IsThemeBackgroundPartiallyTransparent(void* theme,
        int32_t part, int32_t state) noexcept {
    return micant::uxtheme::IsThemeBackgroundPartiallyTransparent(theme, part, state);
}
__declspec(dllexport) int32_t WINAPI EndBufferedAnimation(void* animation,
        int32_t updateTarget) noexcept {
    return micant::uxtheme::EndBufferedAnimation(animation, updateTarget);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintStopAllAnimations(void* hwnd) noexcept {
    return micant::uxtheme::BufferedPaintStopAllAnimations(hwnd);
}

}
