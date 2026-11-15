#pragma once

#include <stddef.h>
#include <stdint.h>
#include "micant/shared/compositor_surface.h"

#if defined(MICANT_WIN32K_EXPORTS)
#define MICANT_WIN32K_API __declspec(dllexport)
#elif defined(MICANT_WIN32K_IMPORTS)
#define MICANT_WIN32K_API __declspec(dllimport)
#else
#define MICANT_WIN32K_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MicaWin32kRect { int32_t left, top, right, bottom; } MicaWin32kRect;
typedef struct MicaWin32kPoint { int32_t x, y; } MicaWin32kPoint;
typedef struct MicaWin32kInputEvent {
    int32_t x, y;
    uint32_t buttons;
    uint32_t sequence;
} MicaWin32kInputEvent;
typedef struct MicaWin32kSystemTime {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    uint16_t reserved;
} MicaWin32kSystemTime;
typedef struct MicaWin32kKeyboardEvent {
    uint16_t virtualKey;
    uint8_t character;
    uint8_t flags; /* bit 0: key down, bit 1: Shift, bit 2: Caps Lock */
    uint32_t sequence;
} MicaWin32kKeyboardEvent;
typedef struct MicaWin32kMessage {
    void* hwnd;
    uint32_t message;
    uintptr_t wParam;
    intptr_t lParam;
    uint32_t time;
    MicaWin32kPoint pt;
} MicaWin32kMessage;
typedef struct MicaWin32kWindowClassEx {
    uint32_t cbSize, style;
    void* wndProc;
    int32_t classExtra, windowExtra;
    void* instance;
    void* icon;
    void* cursor;
    void* background;
    const wchar_t* menuName;
    const wchar_t* className;
    void* smallIcon;
} MicaWin32kWindowClassEx;
typedef struct MicaWin32kCreateStruct {
    void* createParams;
    void* instance;
    void* menu;
    void* parent;
    int32_t height, width, y, x;
    int32_t style;
    const wchar_t* title;
    const wchar_t* className;
    uint32_t exStyle;
} MicaWin32kCreateStruct;
typedef struct MicaWin32kBitmapInfoHeader {
    uint32_t size;
    int32_t width, height;
    uint16_t planes, bitCount;
    uint32_t compression, imageSize;
    int32_t xPixelsPerMeter, yPixelsPerMeter;
    uint32_t colorsUsed, colorsImportant;
} MicaWin32kBitmapInfoHeader;

#define MICANT_WIN32K_REQUEST_VERSION 1u
#define MICANT_WIN32K_TEXT_CAPACITY 128u
#define MICANT_WIN32K_WINDOW_CAPACITY 64u

enum {
    MICANT_WIN32K_OP_REGISTER_CLASS = 1,
    MICANT_WIN32K_OP_CREATE_WINDOW,
    MICANT_WIN32K_OP_DESTROY_WINDOW,
    MICANT_WIN32K_OP_SHOW_WINDOW,
    MICANT_WIN32K_OP_SET_WINDOW_POS,
    MICANT_WIN32K_OP_POST_MESSAGE,
    MICANT_WIN32K_OP_SEND_MESSAGE,
    MICANT_WIN32K_OP_POST_QUIT,
    MICANT_WIN32K_OP_PEEK_MESSAGE,
    MICANT_WIN32K_OP_GET_MESSAGE,
    MICANT_WIN32K_OP_GET_CLIENT_RECT,
    MICANT_WIN32K_OP_GET_WINDOW_RECT,
    MICANT_WIN32K_OP_ENUM_WINDOWS,
    MICANT_WIN32K_OP_GET_WINDOW_TEXT,
    MICANT_WIN32K_OP_IS_WINDOW_VISIBLE,
    MICANT_WIN32K_OP_IS_ICONIC,
    MICANT_WIN32K_OP_GET_FOREGROUND_WINDOW,
    MICANT_WIN32K_OP_SET_WINDOW_LONG,
    MICANT_WIN32K_OP_GET_WINDOW_LONG,
    MICANT_WIN32K_OP_DEF_WINDOW_PROC,
    MICANT_WIN32K_OP_PUMP_INPUT,
    MICANT_WIN32K_OP_GET_WINDOW_SURFACE,
    MICANT_WIN32K_OP_PRESENT_WINDOW,
    MICANT_WIN32K_OP_SET_FOCUS,
    MICANT_WIN32K_OP_SET_WINDOW_ICON,
    MICANT_WIN32K_OP_GET_WINDOW_ICON,
    MICANT_WIN32K_OP_GET_CURSOR_POS,
    MICANT_WIN32K_OP_GET_CURSOR_CLIP,
    MICANT_WIN32K_OP_SET_CURSOR_POS,
    MICANT_WIN32K_OP_SET_CURSOR_CLIP,
    MICANT_WIN32K_OP_SHOW_CURSOR,
    MICANT_WIN32K_OP_GET_CURSOR_HANDLE,
    MICANT_WIN32K_OP_GET_CURSOR_INFO,
    MICANT_WIN32K_OP_SET_THEME_SNAPSHOT,
    MICANT_WIN32K_OP_GET_THEME_SNAPSHOT
};

typedef struct MicaWin32kRequest {
    uint32_t size;
    uint32_t version;
    uint32_t operation;
    uint32_t flags;
    uint64_t result;
    uint64_t args[8];
    uint64_t messageHwnd;
    uint64_t messageWParam;
    int64_t messageLParam;
    uint32_t messageId;
    uint32_t messageTime;
    int32_t messageX, messageY;
    MicaWin32kRect rect;
    uint16_t className[MICANT_WIN32K_TEXT_CAPACITY];
    uint16_t title[MICANT_WIN32K_TEXT_CAPACITY];
    uint16_t text[MICANT_WIN32K_TEXT_CAPACITY];
    uint64_t windowHandles[MICANT_WIN32K_WINDOW_CAPACITY];
} MicaWin32kRequest;

// Private client operation used by User32 DrawIconEx after validating the
// HICON and staging its immutable BGRA pixels in a GDI DIB section.
#define MICANT_WIN32K_ROP_ALPHA_SOURCE_OVER 0x4D494341u

#if defined(__cplusplus)
static_assert(sizeof(MicaWin32kRequest) == 1424);
static_assert(offsetof(MicaWin32kRequest, args) == 24);
static_assert(offsetof(MicaWin32kRequest, windowHandles) == 912);
#endif

MICANT_WIN32K_API void* MicaWin32kCreateWindowExW(uint32_t, const wchar_t*, const wchar_t*, uint32_t,
    int32_t, int32_t, int32_t, int32_t, void*, void*, void*, void*);
MICANT_WIN32K_API int32_t MicaWin32kDestroyWindow(void*);
MICANT_WIN32K_API int32_t MicaWin32kShowWindow(void*, int32_t);
MICANT_WIN32K_API int32_t MicaWin32kSetWindowPos(void*, void*, int32_t, int32_t, int32_t, int32_t, uint32_t);
MICANT_WIN32K_API int32_t MicaWin32kPostMessageW(void*, uint32_t, uintptr_t, intptr_t);
MICANT_WIN32K_API intptr_t MicaWin32kSendMessageW(void*, uint32_t, uintptr_t, intptr_t);
MICANT_WIN32K_API void MicaWin32kPostQuitMessage(int32_t);
MICANT_WIN32K_API uint16_t MicaWin32kRegisterClassW(const wchar_t*, void*);
MICANT_WIN32K_API intptr_t MicaWin32kDispatchMessageW(const MicaWin32kMessage*);
MICANT_WIN32K_API intptr_t MicaWin32kSetWindowLongPtrW(void*, int32_t, intptr_t);
MICANT_WIN32K_API intptr_t MicaWin32kGetWindowLongPtrW(void*, int32_t);
MICANT_WIN32K_API intptr_t MicaWin32kDefWindowProcW(void*, uint32_t, uintptr_t, intptr_t);
MICANT_WIN32K_API void* MicaWin32kGetDC(void*);
MICANT_WIN32K_API int32_t MicaWin32kReleaseDC(void*, void*);
MICANT_WIN32K_API void* MicaWin32kCreateCompatibleDC(void*);
MICANT_WIN32K_API int32_t MicaWin32kDeleteDC(void*);
MICANT_WIN32K_API void* MicaWin32kCreateCompatibleBitmap(void*, int32_t, int32_t);
MICANT_WIN32K_API void* MicaWin32kSelectObject(void*, void*);
MICANT_WIN32K_API int32_t MicaWin32kBitBlt(void*, int32_t, int32_t, int32_t, int32_t, void*, int32_t, int32_t, uint32_t);
MICANT_WIN32K_API int32_t MicaWin32kPresentDC(void*);
MICANT_WIN32K_API int32_t MicaWin32kDeleteObject(void*);
MICANT_WIN32K_API void* MicaWin32kGetStockObject(int32_t);
MICANT_WIN32K_API void* MicaWin32kCreateDibSection(void*, int32_t, int32_t, void**);
MICANT_WIN32K_API int32_t MicaWin32kSetWindowIcon(void*, uint32_t, uint64_t, uint64_t*);
MICANT_WIN32K_API uint64_t MicaWin32kGetWindowIcon(void*, uint32_t);
MICANT_WIN32K_API int32_t MicaWin32kPeekMessageW(MicaWin32kMessage*, void*, uint32_t, uint32_t, uint32_t);
MICANT_WIN32K_API int32_t MicaWin32kGetMessageW(MicaWin32kMessage*, void*, uint32_t, uint32_t);
MICANT_WIN32K_API int32_t MicaWin32kGetClientRect(void*, MicaWin32kRect*);
MICANT_WIN32K_API int32_t MicaWin32kGetWindowRect(void*, MicaWin32kRect*);
MICANT_WIN32K_API int32_t MicaWin32kEnumWindows(void*, intptr_t);
MICANT_WIN32K_API int32_t MicaWin32kGetWindowTextW(void*, wchar_t*, int32_t);
MICANT_WIN32K_API int32_t MicaWin32kIsWindowVisible(void*);
MICANT_WIN32K_API int32_t MicaWin32kIsIconic(void*);
MICANT_WIN32K_API void* MicaWin32kGetForegroundWindow(void);
MICANT_WIN32K_API void* MicaWin32kSetFocus(void*);
MICANT_WIN32K_API int32_t MicaWin32kGetCursorPos(MicaWin32kPoint*);
MICANT_WIN32K_API int32_t MicaWin32kGetClipCursor(MicaWin32kRect*);
MICANT_WIN32K_API int32_t MicaWin32kSetCursorPos(int32_t, int32_t);
MICANT_WIN32K_API int32_t MicaWin32kClipCursor(const MicaWin32kRect*);
MICANT_WIN32K_API int32_t MicaWin32kShowCursor(int32_t);
MICANT_WIN32K_API void* MicaWin32kSetCursor(void*, const uint32_t*, uint32_t,
    uint32_t, uint32_t, uint32_t, uint32_t);
MICANT_WIN32K_API void* MicaWin32kGetCursor(void);

typedef struct MicaWin32kCursorImage {
    uint32_t size, version;
    uint64_t cursorHandle;
    uint32_t width, height, pitch, hotX, hotY;
    uint32_t pixels[MICANT_COMPOSITOR_CURSOR_MAX_PIXELS];
    uint64_t previousCursorHandle;
} MicaWin32kCursorImage;

typedef struct MicaWin32kCursorInfo {
    uint32_t size, flags;
    uint64_t cursorHandle;
    MicaWin32kPoint position;
} MicaWin32kCursorInfo;

#define MICANT_WIN32K_CURSOR_IMAGE_VERSION 1u
MICANT_WIN32K_API int32_t MicaWin32kApplyCursorImage(MicaWin32kCursorImage*);
MICANT_WIN32K_API int32_t MicaWin32kGetCursorInfo(MicaWin32kCursorInfo*);
MICANT_WIN32K_API int32_t MicaWin32kSetThemeSnapshot(const MicaThemeSnapshot*);
MICANT_WIN32K_API int32_t MicaWin32kGetThemeSnapshot(MicaThemeSnapshot*);

#ifdef __cplusplus
}
#endif
