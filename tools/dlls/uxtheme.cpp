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
