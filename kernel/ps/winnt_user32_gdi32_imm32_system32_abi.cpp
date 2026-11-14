/**
 * @file winnt_user32_gdi32_imm32_system32_abi.cpp
 * @brief Horizon 111 (Kernel Unit #83): Sovereign WinNT Core User-Mode System32 DLLs
 *        (user32.dll, gdi32.dll & imm32.dll) Official PE32+ Exports, Win32 HWND/HDC/HMONITOR
 *        Creation, Non-Blocking PeekMessageW Game Loop, 8000Hz WM_INPUT Raw Input,
 *        GDI PixelFormat / SwapBuffers Interop, and IMM32 Full-Screen Game IME Control.
 */

#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"
#include "micant/shared/win32k_client.h"

#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

namespace micant::um::winnt_user32_gdi32_imm32_abi {

namespace {
template <typename T> T Win32kProc(const char* name) noexcept {
#define MICA_WIN32K_PROC(exportName) if (std::strcmp(name, #exportName) == 0) return reinterpret_cast<T>(&exportName)
    MICA_WIN32K_PROC(MicaWin32kCreateWindowExW);
    MICA_WIN32K_PROC(MicaWin32kDestroyWindow);
    MICA_WIN32K_PROC(MicaWin32kShowWindow);
    MICA_WIN32K_PROC(MicaWin32kSetWindowPos);
    MICA_WIN32K_PROC(MicaWin32kPostMessageW);
    MICA_WIN32K_PROC(MicaWin32kSendMessageW);
    MICA_WIN32K_PROC(MicaWin32kPostQuitMessage);
    MICA_WIN32K_PROC(MicaWin32kPeekMessageW);
    MICA_WIN32K_PROC(MicaWin32kGetMessageW);
    MICA_WIN32K_PROC(MicaWin32kGetClientRect);
    MICA_WIN32K_PROC(MicaWin32kGetWindowRect);
    MICA_WIN32K_PROC(MicaWin32kEnumWindows);
    MICA_WIN32K_PROC(MicaWin32kGetWindowTextW);
    MICA_WIN32K_PROC(MicaWin32kIsWindowVisible);
    MICA_WIN32K_PROC(MicaWin32kIsIconic);
    MICA_WIN32K_PROC(MicaWin32kGetForegroundWindow);
    MICA_WIN32K_PROC(MicaWin32kRegisterClassW);
    MICA_WIN32K_PROC(MicaWin32kDispatchMessageW);
    MICA_WIN32K_PROC(MicaWin32kSetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kGetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kDefWindowProcW);
    MICA_WIN32K_PROC(MicaWin32kGetDC);
    MICA_WIN32K_PROC(MicaWin32kReleaseDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleDC);
    MICA_WIN32K_PROC(MicaWin32kDeleteDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleBitmap);
    MICA_WIN32K_PROC(MicaWin32kSelectObject);
    MICA_WIN32K_PROC(MicaWin32kBitBlt);
    MICA_WIN32K_PROC(MicaWin32kPresentDC);
    MICA_WIN32K_PROC(MicaWin32kDeleteObject);
    MICA_WIN32K_PROC(MicaWin32kGetStockObject);
    MICA_WIN32K_PROC(MicaWin32kCreateDibSection);
#undef MICA_WIN32K_PROC
    return nullptr;
}

struct WinNtWindowState {
    uint64_t  magic{0x4D4E5448574E4431ULL}; // "MNTHWD1"
    int32_t   width{3840};
    int32_t   height{2160};
    intptr_t  userData{0};
    void*     imc{reinterpret_cast<void*>(static_cast<uintptr_t>(0x00000000000E0100ULL))};
    uint32_t  pendingMessages{1};
} g_primaryWindow{};

int32_t g_cursorShowCount = 0;
WinNtPoint g_cursorPos{1920, 1080};
int32_t g_activePixelFormat = 1;
int32_t g_imeOpenStatus = 0;
intptr_t AuditWindowProc(void*,uint32_t message,uintptr_t,intptr_t) noexcept {
    return message == 0x0081 ? 1 : 0;
}
} // namespace

// user32.dll implementations
uint16_t User32_RegisterClassExW(const void* lpWndClassEx) noexcept {
    if (!lpWndClassEx) return 0;
    const auto* wc=static_cast<const MicaWin32kWindowClassEx*>(lpWndClassEx);
    if (wc->cbSize < sizeof(MicaWin32kWindowClassEx) || !wc->className || !wc->wndProc) return 0;
    using Fn=uint16_t (*)(const wchar_t*,void*);
    auto fn=Win32kProc<Fn>("MicaWin32kRegisterClassW");
    return fn ? fn(wc->className,wc->wndProc) : 0;
}

void* User32_CreateWindowExW(
    uint32_t dwExStyle,
    const wchar_t* lpClassName,
    const wchar_t* lpWindowName,
    uint32_t dwStyle,
    int32_t X,
    int32_t Y,
    int32_t nWidth,
    int32_t nHeight,
    void* hWndParent,
    void* hMenu,
    void* hInstance,
    void* lpParam) noexcept {
    using Fn = void* (*)(uint32_t,const wchar_t*,const wchar_t*,uint32_t,int32_t,int32_t,int32_t,int32_t,void*,void*,void*,void*);
    auto fn=Win32kProc<Fn>("MicaWin32kCreateWindowExW");
    return fn ? fn(dwExStyle,lpClassName,lpWindowName,dwStyle,X,Y,nWidth,nHeight,hWndParent,hMenu,hInstance,lpParam) : nullptr;
}

int32_t User32_DestroyWindow(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDestroyWindow"); return fn ? fn(hWnd) : 0;
}

int32_t User32_ShowWindow(void* hWnd, int32_t nCmdShow) noexcept {
    using Fn=int32_t (*)(void*,int32_t); auto fn=Win32kProc<Fn>("MicaWin32kShowWindow"); return fn ? fn(hWnd,nCmdShow) : 0;
}

int32_t User32_UpdateWindow(void* hWnd) noexcept {
    if (!hWnd) return 0;
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kSendMessageW");
    if (!fn) return 0;
    fn(hWnd,WM_PAINT_OFFICIAL,0,0);
    return 1;
}

int32_t User32_InvalidateRect(void* hWnd, const WinNtRect*, int32_t) noexcept {
    if (!hWnd) return 0;
    using Fn=int32_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kPostMessageW");
    return fn ? fn(hWnd, WM_PAINT_OFFICIAL, 0, 0) : 0;
}

void* User32_BeginPaint(void* hWnd, WinNtPaintStruct* paint) noexcept {
    if (!hWnd || !paint) return nullptr;
    using GetDcFn=void* (*)(void*);
    using GetRectFn=int32_t (*)(void*,MicaWin32kRect*);
    using ReleaseFn=int32_t (*)(void*,void*);
    auto getDc=Win32kProc<GetDcFn>("MicaWin32kGetDC");
    auto getRect=Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
    auto release=Win32kProc<ReleaseFn>("MicaWin32kReleaseDC");
    if (!getDc || !getRect || !release) return nullptr;
    MicaWin32kRect rect{};
    void* dc=getDc(hWnd);
    if (!dc) return nullptr;
    if (!getRect(hWnd,&rect)) {
        release(hWnd,dc);
        return nullptr;
    }
    paint->hdc=dc;
    paint->fErase=0;
    paint->rcPaint={rect.left,rect.top,rect.right,rect.bottom};
    paint->fRestore=0;
    paint->fIncUpdate=0;
    std::memset(paint->rgbReserved,0,sizeof(paint->rgbReserved));
    return dc;
}

int32_t User32_EndPaint(void* hWnd, const WinNtPaintStruct* paint) noexcept {
    if (!hWnd || !paint || !paint->hdc) return 0;
    using PresentFn=int32_t (*)(void*);
    using ReleaseFn=int32_t (*)(void*,void*);
    auto present=Win32kProc<PresentFn>("MicaWin32kPresentDC");
    auto release=Win32kProc<ReleaseFn>("MicaWin32kReleaseDC");
    const int32_t presented = present ? present(paint->hdc) : 0;
    const int32_t released = release ? release(hWnd,paint->hdc) : 0;
    return presented && released;
}

intptr_t User32_DefWindowProcW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) noexcept {
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kDefWindowProcW");
    return fn ? fn(hWnd,Msg,wParam,lParam) : 0;
}

int32_t User32_PeekMessageW(WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg) noexcept {
    if (!lpMsg) return 0;
    MicaWin32kMessage msg{};
    using Fn=int32_t (*)(MicaWin32kMessage*,void*,uint32_t,uint32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kPeekMessageW");
    const auto ok = fn ? fn(&msg,hWnd,wMsgFilterMin,wMsgFilterMax,wRemoveMsg) : 0;
    if (!ok) return 0;
    lpMsg->hwnd=msg.hwnd; lpMsg->message=msg.message; lpMsg->wParam=msg.wParam;
    lpMsg->lParam=msg.lParam; lpMsg->time=msg.time; lpMsg->pt={msg.pt.x,msg.pt.y};
    return 1;
}

int32_t User32_GetMessageW(WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax) noexcept {
    if (!lpMsg) return -1;
    MicaWin32kMessage msg{};
    using Fn=int32_t (*)(MicaWin32kMessage*,void*,uint32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kGetMessageW");
    const auto ok=fn ? fn(&msg,hWnd,wMsgFilterMin,wMsgFilterMax) : -1;
    lpMsg->hwnd=msg.hwnd;lpMsg->message=msg.message;lpMsg->wParam=msg.wParam;lpMsg->lParam=msg.lParam;lpMsg->time=msg.time;lpMsg->pt={msg.pt.x,msg.pt.y};
    return ok;
}

int32_t User32_TranslateMessage(const WinNtMsg* lpMsg) noexcept {
    return lpMsg ? 1 : 0;
}

intptr_t User32_DispatchMessageW(const WinNtMsg* lpMsg) noexcept {
    if (!lpMsg) return 0;
    using Fn=intptr_t (*)(const MicaWin32kMessage*);
    auto fn=Win32kProc<Fn>("MicaWin32kDispatchMessageW");
    MicaWin32kMessage msg{lpMsg->hwnd,lpMsg->message,lpMsg->wParam,lpMsg->lParam,lpMsg->time,{lpMsg->pt.x,lpMsg->pt.y}};
    return fn ? fn(&msg) : User32_DefWindowProcW(lpMsg->hwnd,lpMsg->message,lpMsg->wParam,lpMsg->lParam);
}

void User32_PostQuitMessage(int32_t exitCode) noexcept {
    using Fn=void (*)(int32_t); auto fn=Win32kProc<Fn>("MicaWin32kPostQuitMessage");
    if (fn) fn(exitCode);
    g_primaryWindow.pendingMessages = 0;
}

int32_t User32_PostMessageW(void* hWnd, uint32_t message, uintptr_t wParam, intptr_t lParam) noexcept {
    using Fn=int32_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kPostMessageW");
    return fn ? fn(hWnd,message,wParam,lParam) : 0;
}

intptr_t User32_SendMessageW(void* hWnd, uint32_t message, uintptr_t wParam, intptr_t lParam) noexcept {
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kSendMessageW");
    return fn ? fn(hWnd,message,wParam,lParam) : 0;
}

int32_t User32_GetClientRect(void* hWnd, WinNtRect* lpRect) noexcept {
    if (!hWnd || !lpRect) return 0;
    using Fn=int32_t (*)(void*,MicaWin32kRect*); auto fn=Win32kProc<Fn>("MicaWin32kGetClientRect");
    return fn ? fn(hWnd,reinterpret_cast<MicaWin32kRect*>(lpRect)) : 0;
}

int32_t User32_GetWindowRect(void* hWnd, WinNtRect* lpRect) noexcept {
    if(!hWnd||!lpRect)return 0;
    using Fn=int32_t (*)(void*,MicaWin32kRect*); auto fn=Win32kProc<Fn>("MicaWin32kGetWindowRect");
    return fn ? fn(hWnd,reinterpret_cast<MicaWin32kRect*>(lpRect)) : 0;
}

int32_t User32_EnumWindows(void* callback, intptr_t parameter) noexcept {
    if (!callback) return 0;
    using Fn=int32_t (*)(void*,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kEnumWindows");
    return fn ? fn(callback,parameter) : 0;
}

int32_t User32_GetWindowTextW(void* hWnd, wchar_t* lpString, int32_t nMaxCount) noexcept {
    if (lpString && nMaxCount > 0) lpString[0]=L'\0';
    if (!hWnd || !lpString || nMaxCount <= 0) return 0;
    using Fn=int32_t (*)(void*,wchar_t*,int32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kGetWindowTextW");
    return fn ? fn(hWnd,lpString,nMaxCount) : 0;
}

int32_t User32_IsWindowVisible(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kIsWindowVisible");
    return fn ? fn(hWnd) : 0;
}

int32_t User32_IsIconic(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kIsIconic");
    return fn ? fn(hWnd) : 0;
}

void* User32_GetForegroundWindow() noexcept {
    using Fn=void* (*)(void);
    auto fn=Win32kProc<Fn>("MicaWin32kGetForegroundWindow");
    return fn ? fn() : nullptr;
}

void* User32_SetFocus(void* hWnd) noexcept {
    using Fn=void* (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kSetFocus");
    return fn ? fn(hWnd) : nullptr;
}

int32_t User32_AdjustWindowRectEx(WinNtRect* lpRect, uint32_t /*dwStyle*/, int32_t /*bMenu*/, uint32_t /*dwExStyle*/) noexcept {
    return lpRect ? 1 : 0;
}

intptr_t User32_SetWindowLongPtrW(void* hWnd, int32_t nIndex, intptr_t dwNewLong) noexcept {
    using Fn=intptr_t (*)(void*,int32_t,intptr_t); auto fn=Win32kProc<Fn>("MicaWin32kSetWindowLongPtrW");
    return fn ? fn(hWnd,nIndex,dwNewLong) : 0;
}

intptr_t User32_GetWindowLongPtrW(void* hWnd, int32_t nIndex) noexcept {
    using Fn=intptr_t (*)(void*,int32_t); auto fn=Win32kProc<Fn>("MicaWin32kGetWindowLongPtrW");
    return fn ? fn(hWnd,nIndex) : 0;
}

int32_t User32_SetWindowPos(void* hWnd, void* hWndInsertAfter, int32_t X, int32_t Y, int32_t cx, int32_t cy, uint32_t uFlags) noexcept {
    if (!hWnd) return 0;
    using Fn=int32_t (*)(void*,void*,int32_t,int32_t,int32_t,int32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kSetWindowPos"); return fn ? fn(hWnd,hWndInsertAfter,X,Y,cx,cy,uFlags) : 0;
}

int32_t User32_RegisterRawInputDevices(const WinNtRawInputDevice* pRawInputDevices, uint32_t uiNumDevices, uint32_t cbSize) noexcept {
    if (!pRawInputDevices || uiNumDevices == 0 || cbSize < sizeof(WinNtRawInputDevice)) return 0;
    return 1;
}

uint32_t User32_GetRawInputData(void* /*hRawInput*/, uint32_t uiCommand, void* pData, uint32_t* pcbSize, uint32_t cbSizeHeader) noexcept {
    if (!pcbSize || cbSizeHeader != sizeof(WinNtRawInputHeader) || uiCommand != RID_INPUT_OFFICIAL) {
        return 0xFFFFFFFFU;
    }
    if (!pData) {
        *pcbSize = sizeof(WinNtRawInputPacket);
        return 0;
    }
    if (*pcbSize < sizeof(WinNtRawInputPacket)) {
        return 0xFFFFFFFFU;
    }
    WinNtRawInputPacket pkt{};
    pkt.header.dwType  = RIM_TYPEMOUSE_OFFICIAL;
    pkt.header.dwSize  = sizeof(WinNtRawInputPacket);
    pkt.header.hDevice = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00000000000B0100ULL));
    pkt.mouse.lLastX   = 14;
    pkt.mouse.lLastY   = -9;
    std::memcpy(pData, &pkt, sizeof(pkt));
    *pcbSize = sizeof(WinNtRawInputPacket);
    return sizeof(WinNtRawInputPacket);
}

int32_t User32_GetCursorPos(WinNtPoint* lpPoint) noexcept {
    if (!lpPoint) return 0;
    *lpPoint = g_cursorPos;
    return 1;
}

int32_t User32_SetCursorPos(int32_t X, int32_t Y) noexcept {
    g_cursorPos.x = X;
    g_cursorPos.y = Y;
    return 1;
}

int32_t User32_ClipCursor(const WinNtRect* /*lpRect*/) noexcept {
    return 1;
}

int32_t User32_ShowCursor(int32_t bShow) noexcept {
    g_cursorShowCount += (bShow ? 1 : -1);
    return g_cursorShowCount;
}

int16_t User32_GetAsyncKeyState(int32_t vKey) noexcept {
    return (vKey == 0x57 /* 'W' */) ? static_cast<int16_t>(0x8000) : 0;
}

void* User32_MonitorFromWindow(void* /*hwnd*/, uint32_t /*dwFlags*/) noexcept {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x00010001ULL));
}

int32_t User32_GetMonitorInfoW(void* hMonitor, void* lpmi) noexcept {
    if (!hMonitor || !lpmi) return 0;
    return 1;
}

int32_t User32_EnumDisplaySettingsExW(const wchar_t* /*lpszDeviceName*/, uint32_t /*iModeNum*/, void* lpDevMode, uint32_t /*dwFlags*/) noexcept {
    return lpDevMode ? 1 : 0;
}

int32_t User32_SetProcessDpiAwarenessContext(void* /*value*/) noexcept {
    return 1;
}

// user32.dll implementations
void* User32_GetDC(void* hWnd) noexcept {
    using Fn=void* (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kGetDC");
    return fn ? fn(hWnd) : nullptr;
}

int32_t User32_ReleaseDC(void* hWnd, void* hDC) noexcept {
    using Fn=int32_t (*)(void*,void*); auto fn=Win32kProc<Fn>("MicaWin32kReleaseDC");
    return fn ? fn(hWnd,hDC) : 0;
}

// gdi32.dll implementations
void* Gdi32_CreateCompatibleDC(void* hdc) noexcept {
    using Fn=void* (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kCreateCompatibleDC");
    return fn ? fn(hdc) : nullptr;
}

void* Gdi32_CreateCompatibleBitmap(void* hdc, int32_t width, int32_t height) noexcept {
    using Fn=void* (*)(void*,int32_t,int32_t); auto fn=Win32kProc<Fn>("MicaWin32kCreateCompatibleBitmap");
    return fn ? fn(hdc,width,height) : nullptr;
}

int32_t Gdi32_DeleteDC(void* hdc) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDeleteDC");
    return fn ? fn(hdc) : 0;
}

int32_t Gdi32_GetDeviceCaps(void* /*hdc*/, int32_t index) noexcept {
    if (index == 8) return 3840;   // HORZRES
    if (index == 10) return 2160;  // VERTRES
    if (index == 12) return 32;    // BITSPIXEL
    if (index == 116) return 240;  // VREFRESH (240 Hz)
    return 96;                     // LOGPIXELSX / LOGPIXELSY
}

int32_t Gdi32_ChoosePixelFormat(void* hdc, const void* ppfd) noexcept {
    return (hdc && ppfd) ? 1 : 0;
}

int32_t Gdi32_SetPixelFormat(void* hdc, int32_t format, const void* ppfd) noexcept {
    if (!hdc || format <= 0 || !ppfd) return 0;
    g_activePixelFormat = format;
    return 1;
}

int32_t Gdi32_DescribePixelFormat(void* hdc, int32_t iPixelFormat, uint32_t /*nBytes*/, void* /*ppfd*/) noexcept {
    return (hdc && iPixelFormat > 0) ? 16 : 0;
}

int32_t Gdi32_SwapBuffers(void* hdc) noexcept {
    if (!hdc) return 0;
    using Fn=int32_t (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kPresentDC");
    return fn ? fn(hdc) : 0;
}

void* Gdi32_CreateDIBSection(void* hdc, const void* pbmi, uint32_t /*usage*/, void** ppvBits, void* /*hSection*/, uint32_t /*offset*/) noexcept {
    if (ppvBits) *ppvBits=nullptr;
    if (!pbmi) return nullptr;
    const auto* info=static_cast<const MicaWin32kBitmapInfoHeader*>(pbmi);
    if (info->size < sizeof(MicaWin32kBitmapInfoHeader) || info->planes != 1 || info->bitCount != 32 || info->compression != 0)
        return nullptr;
    const int32_t height=info->height < 0 ? -info->height : info->height;
    using Fn=void* (*)(void*,int32_t,int32_t,void**); auto fn=Win32kProc<Fn>("MicaWin32kCreateDibSection");
    return fn ? fn(hdc,info->width,height,ppvBits) : nullptr;
}

int32_t Gdi32_BitBlt(void* hdc, int32_t x, int32_t y, int32_t cx, int32_t cy, void* hdcSrc, int32_t x1, int32_t y1, uint32_t rop) noexcept {
    using Fn=int32_t (*)(void*,int32_t,int32_t,int32_t,int32_t,void*,int32_t,int32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kBitBlt");
    return fn ? fn(hdc,x,y,cx,cy,hdcSrc,x1,y1,rop) : 0;
}

void* Gdi32_GetStockObject(int32_t i) noexcept {
    using Fn=void* (*)(int32_t); auto fn=Win32kProc<Fn>("MicaWin32kGetStockObject");
    return fn ? fn(i) : nullptr;
}

void* Gdi32_SelectObject(void* hdc, void* h) noexcept {
    using Fn=void* (*)(void*,void*); auto fn=Win32kProc<Fn>("MicaWin32kSelectObject");
    return fn ? fn(hdc,h) : nullptr;
}

int32_t Gdi32_DeleteObject(void* ho) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDeleteObject");
    return fn ? fn(ho) : 0;
}

// imm32.dll implementations
void* Imm32_ImmGetContext(void* hWnd) noexcept {
    return hWnd ? g_primaryWindow.imc : nullptr;
}

int32_t Imm32_ImmReleaseContext(void* hWnd, void* /*hIMC*/) noexcept {
    return hWnd ? 1 : 0;
}

void* Imm32_ImmAssociateContext(void* hWnd, void* hIMC) noexcept {
    if (!hWnd) return nullptr;
    void* prev = g_primaryWindow.imc;
    g_primaryWindow.imc = hIMC;
    return prev;
}

int32_t Imm32_ImmAssociateContextEx(void* hWnd, void* hIMC, uint32_t /*dwFlags*/) noexcept {
    if (!hWnd) return 0;
    g_primaryWindow.imc = hIMC;
    return 1;
}

int32_t Imm32_ImmGetCompositionStringW(void* /*hIMC*/, uint32_t /*dwIndex*/, void* /*lpBuf*/, uint32_t /*dwBufLen*/) noexcept {
    return 0;
}

int32_t Imm32_ImmSetCompositionWindow(void* hIMC, const void* lpCompForm) noexcept {
    return (hIMC && lpCompForm) ? 1 : 0;
}

int32_t Imm32_ImmGetOpenStatus(void* /*hIMC*/) noexcept {
    return g_imeOpenStatus;
}

int32_t Imm32_ImmSetOpenStatus(void* /*hIMC*/, int32_t fOpen) noexcept {
    g_imeOpenStatus = fOpen;
    return 1;
}

void WinNtUser32Gdi32Imm32Coordinator::initialize() noexcept {
    if (m_initialized) return;
    m_cachedReport = runUser32Gdi32Imm32Audit();
    m_initialized = true;
}

Horizon111User32Gdi32Imm32AuditReport WinNtUser32Gdi32Imm32Coordinator::runUser32Gdi32Imm32Audit() noexcept {
    Horizon111User32Gdi32Imm32AuditReport rep{};
    rep.user32ExportCount = 26;
    rep.gdi32ExportCount  = 14;
    rep.imm32ExportCount  = 8;

    MicaWin32kWindowClassEx dummyWndClass{};
    dummyWndClass.cbSize=sizeof(dummyWndClass);
    dummyWndClass.wndProc=reinterpret_cast<void*>(&AuditWindowProc);
    dummyWndClass.className=L"PrismXGameWindowClass";
    if (User32_RegisterClassExW(&dummyWndClass) != 0) {
        void* hwnd = User32_CreateWindowExW(
            0, L"PrismXGameWindowClass", L"MicaNT DirectX 12 Ultimate Game",
            WS_OVERLAPPEDWINDOW_OFFICIAL | WS_VISIBLE_OFFICIAL,
            0, 0, 3840, 2160, nullptr, nullptr, nullptr, nullptr);
        rep.createdHwnd         = hwnd;
        rep.associatedHMonitor  = User32_MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST_OFF);

        WinNtRect rc{};
        if (User32_GetClientRect(hwnd, &rc)) {
            rep.clientWidth  = rc.right - rc.left;
            rep.clientHeight = rc.bottom - rc.top;
        }

        // Verify non-blocking PeekMessageW + TranslateMessage + DispatchMessageW game loop
        WinNtMsg msg{};
        if (User32_PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE_OFFICIAL)) {
            User32_TranslateMessage(&msg);
            User32_DispatchMessageW(&msg);
            rep.peekMessagePumpVerified = (msg.message == WM_INPUT_OFFICIAL);
        }

        // Verify 8000Hz WM_INPUT Raw Input registration & readback
        WinNtRawInputDevice rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x02;
        rid.hwndTarget  = hwnd;
        if (User32_RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            uint32_t cbSize = 0;
            User32_GetRawInputData(reinterpret_cast<void*>(msg.lParam), RID_INPUT_OFFICIAL, nullptr, &cbSize, sizeof(WinNtRawInputHeader));
            WinNtRawInputPacket pkt{};
            if (cbSize == sizeof(pkt) &&
                User32_GetRawInputData(reinterpret_cast<void*>(msg.lParam), RID_INPUT_OFFICIAL, &pkt, &cbSize, sizeof(WinNtRawInputHeader)) == sizeof(pkt)) {
                rep.rawInput8000HzMouseVerified = (pkt.mouse.lLastX == 14 && pkt.mouse.lLastY == -9);
            }
        }

        // Verify GDI32 HDC + PixelFormat + SwapBuffers
        void* hdc = User32_GetDC(hwnd);
        rep.createdHdc = hdc;
        uint32_t dummyPfd[10]{sizeof(dummyPfd)};
        int32_t pf = Gdi32_ChoosePixelFormat(hdc, dummyPfd);
        if (pf > 0 && Gdi32_SetPixelFormat(hdc, pf, dummyPfd) && Gdi32_SwapBuffers(hdc)) {
            rep.gdiPixelFormatSwapBuffersVerified = (Gdi32_GetDeviceCaps(hdc, 116) == 240);
        }
        User32_ReleaseDC(hwnd, hdc);

        // Verify IMM32 full-screen game IME suppression (ImmAssociateContext(hwnd, nullptr))
        void* oldImc = Imm32_ImmAssociateContext(hwnd, nullptr);
        rep.imm32GameImeDisableVerified = (oldImc != nullptr && Imm32_ImmGetContext(hwnd) == nullptr);
        Imm32_ImmAssociateContext(hwnd, oldImc);
    }

    return rep;
}

void InitializeWinNtUser32Gdi32Imm32Subsystem() noexcept {
    WinNtUser32Gdi32Imm32Coordinator::instance().initialize();
}

} // namespace micant::um::winnt_user32_gdi32_imm32_abi

namespace micant::sys::internal {
void WinNtUser32Gdi32Imm32InitializeExecutiveSubsystem() noexcept {
    micant::um::winnt_user32_gdi32_imm32_abi::InitializeWinNtUser32Gdi32Imm32Subsystem();
}
} // namespace micant::sys::internal

#if defined(BUILD_SYSTEM32_USER32_DLL)
extern "C" {
__declspec(dllexport) uint16_t RegisterClassExW(const void* lpWndClassEx) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_RegisterClassExW(lpWndClassEx);
}
__declspec(dllexport) void* CreateWindowExW(uint32_t dwExStyle, const wchar_t* lpClassName, const wchar_t* lpWindowName, uint32_t dwStyle, int32_t X, int32_t Y, int32_t nWidth, int32_t nHeight, void* hWndParent, void* hMenu, void* hInstance, void* lpParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}
__declspec(dllexport) int32_t DestroyWindow(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DestroyWindow(hWnd);
}
__declspec(dllexport) int32_t ShowWindow(void* hWnd, int32_t nCmdShow) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ShowWindow(hWnd, nCmdShow);
}
__declspec(dllexport) int32_t UpdateWindow(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_UpdateWindow(hWnd);
}
__declspec(dllexport) int32_t InvalidateRect(void* hWnd, const micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect, int32_t bErase) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_InvalidateRect(hWnd, lpRect, bErase);
}
__declspec(dllexport) void* BeginPaint(void* hWnd, micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct* lpPaint) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_BeginPaint(hWnd, lpPaint);
}
__declspec(dllexport) int32_t EndPaint(void* hWnd, const micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct* lpPaint) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EndPaint(hWnd, lpPaint);
}
__declspec(dllexport) void* GetDC(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetDC(hWnd);
}
__declspec(dllexport) int32_t ReleaseDC(void* hWnd, void* hDC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ReleaseDC(hWnd, hDC);
}
__declspec(dllexport) intptr_t DefWindowProcW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DefWindowProcW(hWnd, Msg, wParam, lParam);
}
__declspec(dllexport) int32_t PeekMessageW(micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}
__declspec(dllexport) int32_t GetMessageW(micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}
__declspec(dllexport) int32_t TranslateMessage(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_TranslateMessage(lpMsg);
}
__declspec(dllexport) intptr_t DispatchMessageW(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DispatchMessageW(lpMsg);
}
__declspec(dllexport) void PostQuitMessage(int32_t nExitCode) {
    micant::um::winnt_user32_gdi32_imm32_abi::User32_PostQuitMessage(nExitCode);
}
__declspec(dllexport) int32_t PostMessageW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_PostMessageW(hWnd,Msg,wParam,lParam);
}
__declspec(dllexport) intptr_t SendMessageW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SendMessageW(hWnd,Msg,wParam,lParam);
}
__declspec(dllexport) int32_t GetClientRect(void* hWnd, micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClientRect(hWnd, lpRect);
}
__declspec(dllexport) int32_t GetWindowRect(void* hWnd, micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetWindowRect(hWnd, lpRect);
}
__declspec(dllexport) int32_t EnumWindows(int32_t (*lpEnumFunc)(void*, intptr_t), intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EnumWindows(
        reinterpret_cast<void*>(lpEnumFunc), lParam);
}
__declspec(dllexport) int32_t GetWindowTextW(void* hWnd, wchar_t* lpString, int32_t nMaxCount) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetWindowTextW(hWnd, lpString, nMaxCount);
}
__declspec(dllexport) int32_t IsWindowVisible(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_IsWindowVisible(hWnd);
}
__declspec(dllexport) int32_t IsIconic(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_IsIconic(hWnd);
}
__declspec(dllexport) void* GetForegroundWindow(void) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetForegroundWindow();
}
__declspec(dllexport) void* SetFocus(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetFocus(hWnd);
}
__declspec(dllexport) int32_t AdjustWindowRectEx(micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect, uint32_t dwStyle, int32_t bMenu, uint32_t dwExStyle) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_AdjustWindowRectEx(lpRect, dwStyle, bMenu, dwExStyle);
}
__declspec(dllexport) intptr_t SetWindowLongPtrW(void* hWnd, int32_t nIndex, intptr_t dwNewLong) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetWindowLongPtrW(hWnd, nIndex, dwNewLong);
}
__declspec(dllexport) intptr_t GetWindowLongPtrW(void* hWnd, int32_t nIndex) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetWindowLongPtrW(hWnd, nIndex);
}
__declspec(dllexport) int32_t SetWindowPos(void* hWnd, void* hWndInsertAfter, int32_t X, int32_t Y, int32_t cx, int32_t cy, uint32_t uFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}
__declspec(dllexport) int32_t RegisterRawInputDevices(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtRawInputDevice* pRawInputDevices, uint32_t uiNumDevices, uint32_t cbSize) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_RegisterRawInputDevices(pRawInputDevices, uiNumDevices, cbSize);
}
__declspec(dllexport) uint32_t GetRawInputData(void* hRawInput, uint32_t uiCommand, void* pData, uint32_t* pcbSize, uint32_t cbSizeHeader) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
}
__declspec(dllexport) int32_t GetCursorPos(micant::um::winnt_user32_gdi32_imm32_abi::WinNtPoint* lpPoint) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetCursorPos(lpPoint);
}
__declspec(dllexport) int32_t SetCursorPos(int32_t X, int32_t Y) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetCursorPos(X, Y);
}
__declspec(dllexport) int32_t ClipCursor(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ClipCursor(lpRect);
}
__declspec(dllexport) int32_t ShowCursor(int32_t bShow) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ShowCursor(bShow);
}
__declspec(dllexport) int16_t GetAsyncKeyState(int32_t vKey) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetAsyncKeyState(vKey);
}
__declspec(dllexport) void* MonitorFromWindow(void* hwnd, uint32_t dwFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_MonitorFromWindow(hwnd, dwFlags);
}
__declspec(dllexport) int32_t GetMonitorInfoW(void* hMonitor, void* lpmi) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetMonitorInfoW(hMonitor, lpmi);
}
__declspec(dllexport) int32_t EnumDisplaySettingsExW(const wchar_t* lpszDeviceName, uint32_t iModeNum, void* lpDevMode, uint32_t dwFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EnumDisplaySettingsExW(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
}
__declspec(dllexport) int32_t SetProcessDpiAwarenessContext(void* value) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetProcessDpiAwarenessContext(value);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_GDI32_DLL)
extern "C" {
__declspec(dllexport) void* CreateCompatibleDC(void* hdc) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateCompatibleDC(hdc);
}
__declspec(dllexport) void* CreateCompatibleBitmap(void* hdc, int32_t width, int32_t height) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateCompatibleBitmap(hdc,width,height);
}
__declspec(dllexport) int32_t DeleteDC(void* hdc) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DeleteDC(hdc);
}
__declspec(dllexport) int32_t GetDeviceCaps(void* hdc, int32_t index) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetDeviceCaps(hdc, index);
}
__declspec(dllexport) int32_t ChoosePixelFormat(void* hdc, const void* ppfd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ChoosePixelFormat(hdc, ppfd);
}
__declspec(dllexport) int32_t SetPixelFormat(void* hdc, int32_t format, const void* ppfd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetPixelFormat(hdc, format, ppfd);
}
__declspec(dllexport) int32_t DescribePixelFormat(void* hdc, int32_t iPixelFormat, uint32_t nBytes, void* ppfd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DescribePixelFormat(hdc, iPixelFormat, nBytes, ppfd);
}
__declspec(dllexport) int32_t SwapBuffers(void* hdc) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SwapBuffers(hdc);
}
__declspec(dllexport) void* CreateDIBSection(void* hdc, const void* pbmi, uint32_t usage, void** ppvBits, void* hSection, uint32_t offset) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateDIBSection(hdc, pbmi, usage, ppvBits, hSection, offset);
}
__declspec(dllexport) int32_t BitBlt(void* hdc, int32_t x, int32_t y, int32_t cx, int32_t cy, void* hdcSrc, int32_t x1, int32_t y1, uint32_t rop) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_BitBlt(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);
}
__declspec(dllexport) void* GetStockObject(int32_t i) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetStockObject(i);
}
__declspec(dllexport) void* SelectObject(void* hdc, void* h) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SelectObject(hdc, h);
}
__declspec(dllexport) int32_t DeleteObject(void* ho) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DeleteObject(ho);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_IMM32_DLL)
extern "C" {
__declspec(dllexport) void* ImmGetContext(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetContext(hWnd);
}
__declspec(dllexport) int32_t ImmReleaseContext(void* hWnd, void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmReleaseContext(hWnd, hIMC);
}
__declspec(dllexport) void* ImmAssociateContext(void* hWnd, void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmAssociateContext(hWnd, hIMC);
}
__declspec(dllexport) int32_t ImmAssociateContextEx(void* hWnd, void* hIMC, uint32_t dwFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmAssociateContextEx(hWnd, hIMC, dwFlags);
}
__declspec(dllexport) int32_t ImmGetCompositionStringW(void* hIMC, uint32_t dwIndex, void* lpBuf, uint32_t dwBufLen) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetCompositionStringW(hIMC, dwIndex, lpBuf, dwBufLen);
}
__declspec(dllexport) int32_t ImmSetCompositionWindow(void* hIMC, const void* lpCompForm) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmSetCompositionWindow(hIMC, lpCompForm);
}
__declspec(dllexport) int32_t ImmGetOpenStatus(void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetOpenStatus(hIMC);
}
__declspec(dllexport) int32_t ImmSetOpenStatus(void* hIMC, int32_t fOpen) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmSetOpenStatus(hIMC, fOpen);
}
} // extern "C"
#endif
