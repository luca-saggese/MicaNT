#pragma once

#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#ifndef WINAPI
#if defined(_WIN32)
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#endif

#if defined(MICANT_UXTHEME_EXPORTS)
#define MICANT_UXTHEME_API __declspec(dllexport)
#elif defined(MICANT_UXTHEME_IMPORTS)
#define MICANT_UXTHEME_API __declspec(dllimport)
#else
#define MICANT_UXTHEME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* MicaThemeHandle;
typedef struct MicaThemeMargins {
    int32_t cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight;
} MicaThemeMargins;

MICANT_UXTHEME_API MicaThemeHandle WINAPI OpenThemeData(void*, const wchar_t*);
MICANT_UXTHEME_API int32_t WINAPI CloseThemeData(MicaThemeHandle);
MICANT_UXTHEME_API int32_t WINAPI GetThemeColor(MicaThemeHandle, int32_t, int32_t, int32_t, uint32_t*);
MICANT_UXTHEME_API int32_t WINAPI GetThemePartSize(MicaThemeHandle, void*, int32_t, int32_t,
                                                   void*, int32_t, void*);
MICANT_UXTHEME_API int32_t WINAPI GetThemeMargins(MicaThemeHandle, void*, int32_t, int32_t,
                                                  int32_t, const void*, MicaThemeMargins*);
MICANT_UXTHEME_API int32_t WINAPI GetThemeFont(MicaThemeHandle, void*, int32_t, int32_t,
                                               int32_t, void*);
MICANT_UXTHEME_API int32_t WINAPI GetThemeInt(MicaThemeHandle, int32_t, int32_t, int32_t, int32_t*);
MICANT_UXTHEME_API int32_t WINAPI GetThemeBool(MicaThemeHandle, int32_t, int32_t, int32_t, int32_t*);
MICANT_UXTHEME_API int32_t WINAPI GetThemePropertyOrigin(MicaThemeHandle, int32_t, int32_t,
                                                         int32_t, int32_t*);
MICANT_UXTHEME_API int32_t WINAPI GetThemeEnumValue(MicaThemeHandle, int32_t, int32_t, int32_t, int32_t*);
MICANT_UXTHEME_API uint32_t WINAPI GetThemeSysColor(MicaThemeHandle, int32_t);
MICANT_UXTHEME_API int32_t WINAPI BufferedPaintInit(void);
MICANT_UXTHEME_API int32_t WINAPI BufferedPaintUnInit(void);
MICANT_UXTHEME_API void* WINAPI BeginBufferedPaint(void*, const void*, int32_t,
                                                    const void*, void**);
MICANT_UXTHEME_API int32_t WINAPI EndBufferedPaint(void*, int32_t);
MICANT_UXTHEME_API int32_t WINAPI DrawThemeBackground(MicaThemeHandle, void*, int32_t, int32_t,
                                                      const void*, const void*);
MICANT_UXTHEME_API int32_t WINAPI DrawThemeBackgroundEx(MicaThemeHandle, void*, int32_t, int32_t,
                                                        const void*, const void*);
MICANT_UXTHEME_API int32_t WINAPI DrawThemeText(MicaThemeHandle, void*, int32_t, int32_t,
                                                 const wchar_t*, int32_t, uint32_t, uint32_t,
                                                 const void*);
MICANT_UXTHEME_API int32_t WINAPI GetThemeBackgroundContentRect(MicaThemeHandle, void*,
                                                                int32_t, int32_t,
                                                                const void*, void*);
MICANT_UXTHEME_API int32_t WINAPI GetThemeBackgroundExtent(MicaThemeHandle, void*,
                                                           int32_t, int32_t,
                                                           const void*, void*);
MICANT_UXTHEME_API int32_t WINAPI IsThemeActive(void);
MICANT_UXTHEME_API int32_t WINAPI IsAppThemed(void);
MICANT_UXTHEME_API void* WINAPI BeginBufferedAnimation(void*, void*, const void*, int32_t,
                                                       void*, void*, void**, void**);
MICANT_UXTHEME_API int32_t WINAPI EndBufferedAnimation(void*, int32_t);
MICANT_UXTHEME_API int32_t WINAPI BufferedPaintStopAllAnimations(void*);

#ifdef __cplusplus
}
#endif
