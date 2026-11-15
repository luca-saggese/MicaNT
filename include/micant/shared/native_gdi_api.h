#pragma once

#include <stdint.h>
#include <wchar.h>

#ifndef WINAPI
#if defined(_WIN32)
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#endif

#if defined(MICANT_GDI32_IMPORTS)
#define MICANT_GDI32_API __declspec(dllimport)
#else
#define MICANT_GDI32_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

MICANT_GDI32_API void* WINAPI CreateCompatibleDC(void*);
MICANT_GDI32_API void* WINAPI CreateSolidBrush(uint32_t);
MICANT_GDI32_API int32_t WINAPI DeleteDC(void*);
MICANT_GDI32_API void* WINAPI CreateDIBSection(void*, const void*, uint32_t,
                                               void**, void*, uint32_t);
MICANT_GDI32_API void* WINAPI SelectObject(void*, void*);
MICANT_GDI32_API int32_t WINAPI DeleteObject(void*);
MICANT_GDI32_API void* WINAPI GetStockObject(int32_t);
MICANT_GDI32_API int32_t WINAPI FillRect(void*, const void*, void*);
MICANT_GDI32_API int32_t WINAPI BitBlt(void*, int32_t, int32_t, int32_t, int32_t,
                                        void*, int32_t, int32_t, uint32_t);
MICANT_GDI32_API uint32_t WINAPI SetTextColor(void*, uint32_t);
MICANT_GDI32_API int32_t WINAPI SetBkMode(void*, int32_t);
MICANT_GDI32_API int32_t WINAPI TextOutW(void*, int32_t, int32_t,
                                          const wchar_t*, int32_t);
MICANT_GDI32_API int32_t WINAPI GetTextExtentPoint32W(void*, const wchar_t*,
                                                       int32_t, void*);

#ifdef __cplusplus
}
#endif
