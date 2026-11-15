#pragma once

#include "micant/um/subsystems/winuser/uxtheme.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::uxtheme {
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
    ldr.registerExportOrdinal("uxtheme.dll", 47, reinterpret_cast<void*>(DrawThemeBackgroundEx));
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
}
