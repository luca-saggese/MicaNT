// ============================================================================
// MicaNT: PrismUI & User32 Subsystem (user32.dll)
//
// Modern Clean-Room Implementation in pure ISO C++23.
// Provides complete Win32 Window Management, Message Dispatching, Surface
// Compositor Backing, Presentation Hooks, and High-Precision Raw Input.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <deque>
#include <algorithm>
#include <cstring>
#include "micant/shared/ntdef.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/native/ldr.hpp"
#include "micant/um/servers/winlogon.hpp"
#include "micant/um/subsystems/graphics/prismx.hpp"

namespace micant::user32 {

// ============================================================================
// 1. Win32 Window Data Types & Handles
// ============================================================================

using UINT    = uint32_t;
using PUINT   = uint32_t*;
using WPARAM  = uintptr_t;
using LPARAM  = intptr_t;
using LRESULT = intptr_t;
using win32::DWORD;
using win32::WORD;
using win32::BOOL;
using win32::LONG;
using win32::ULONG;
using win32::BYTE;

using HDC       = void*;
using HINSTANCE = void*;
using HICON     = void*;
using HCURSOR   = void*;
using HBRUSH    = void*;
using HMENU     = void*;
using HRAWINPUT = void*;
using HWND      = win32::HWND;

using WNDPROC = LRESULT (*)(win32::HWND, UINT, WPARAM, LPARAM);
using WNDENUMPROC = BOOL (*)(HWND, LPARAM);

using POINT = micant::prismx::POINT;
using RECT  = micant::prismx::RECT;

struct ACCEL {
    uint8_t  fVirt;
    uint16_t key;
    uint16_t cmd;
};

using GRAYSTRINGPROC = win32::BOOL(*)(HDC, int64_t, int32_t);

struct MSG {
    win32::HWND hwnd{nullptr};
    UINT        message{0};
    WPARAM      wParam{0};
    LPARAM      lParam{0};
    uint32_t    time{0};
    POINT       pt{0, 0};
};

struct PAINTSTRUCT {
    HDC         hdc{nullptr};
    win32::BOOL fErase{win32::FALSE};
    RECT        rcPaint{0, 0, 0, 0};
    win32::BOOL fRestore{win32::FALSE};
    win32::BOOL fIncUpdate{win32::FALSE};
    uint8_t     rgbReserved[32]{};
};

struct WNDCLASSEXW {
    UINT        cbSize{sizeof(WNDCLASSEXW)};
    UINT        style{0};
    WNDPROC     lpfnWndProc{nullptr};
    int         cbClsExtra{0};
    int         cbWndExtra{0};
    HINSTANCE   hInstance{nullptr};
    HICON       hIcon{nullptr};
    HCURSOR     hCursor{nullptr};
    HBRUSH      hbrBackground{nullptr};
    const wchar_t* lpszMenuName{nullptr};
    const wchar_t* lpszClassName{nullptr};
    HICON       hIconSm{nullptr};
};

struct WNDCLASSW {
    UINT        style{0};
    WNDPROC     lpfnWndProc{nullptr};
    int         cbClsExtra{0};
    int         cbWndExtra{0};
    HINSTANCE   hInstance{nullptr};
    HICON       hIcon{nullptr};
    HCURSOR     hCursor{nullptr};
    HBRUSH      hbrBackground{nullptr};
    const wchar_t* lpszMenuName{nullptr};
    const wchar_t* lpszClassName{nullptr};
};

struct WNDCLASSA {
    UINT        style{0};
    void*       lpfnWndProc{nullptr};
    int         cbClsExtra{0};
    int         cbWndExtra{0};
    HINSTANCE   hInstance{nullptr};
    HICON       hIcon{nullptr};
    HCURSOR     hCursor{nullptr};
    HBRUSH      hbrBackground{nullptr};
    const char* lpszMenuName{nullptr};
    const char* lpszClassName{nullptr};
};

struct CREATESTRUCTW {
    void*       lpCreateParams{nullptr};
    HINSTANCE   hInstance{nullptr};
    HMENU       hMenu{nullptr};
    win32::HWND hwndParent{nullptr};
    int         cy{0};
    int         cx{0};
    int         y{0};
    int         x{0};
    uint32_t    style{0};
    const wchar_t* lpszName{nullptr};
    const wchar_t* lpszClass{nullptr};
    uint32_t    dwExStyle{0};
};

// ============================================================================
// 2. Window Styles, Messages & Constants
// ============================================================================

// Window Styles
inline constexpr uint32_t WS_OVERLAPPED       = 0x00000000;
inline constexpr uint32_t WS_POPUP            = 0x80000000;
inline constexpr uint32_t WS_CHILD            = 0x40000000;
inline constexpr uint32_t WS_MINIMIZE         = 0x20000000;
inline constexpr uint32_t WS_VISIBLE          = 0x10000000;
inline constexpr uint32_t WS_DISABLED         = 0x08000000;
inline constexpr uint32_t WS_CLIPSIBLINGS     = 0x04000000;
inline constexpr uint32_t WS_CLIPCHILDREN     = 0x02000000;
inline constexpr uint32_t WS_MAXIMIZE         = 0x01000000;
inline constexpr uint32_t WS_CAPTION          = 0x00C00000;
inline constexpr uint32_t WS_BORDER           = 0x00800000;
inline constexpr uint32_t WS_DLGFRAME         = 0x00400000;
inline constexpr uint32_t WS_VSCROLL          = 0x00200000;
inline constexpr uint32_t WS_HSCROLL          = 0x00100000;
inline constexpr uint32_t WS_SYSMENU          = 0x00080000;
inline constexpr uint32_t WS_THICKFRAME       = 0x00040000;
inline constexpr uint32_t WS_MINIMIZEBOX      = 0x00020000;
inline constexpr uint32_t WS_MAXIMIZEBOX      = 0x00010000;
inline constexpr uint32_t WS_OVERLAPPEDWINDOW = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

// Window Extended Styles
inline constexpr uint32_t WS_EX_DLGMODALFRAME  = 0x00000001;
inline constexpr uint32_t WS_EX_TOPMOST        = 0x00000008;
inline constexpr uint32_t WS_EX_TRANSPARENT    = 0x00000020;
inline constexpr uint32_t WS_EX_TOOLWINDOW     = 0x00000080;
inline constexpr uint32_t WS_EX_WINDOWEDGE     = 0x00000100;
inline constexpr uint32_t WS_EX_CLIENTEDGE     = 0x00000200;
inline constexpr uint32_t WS_EX_APPWINDOW      = 0x00040000;
inline constexpr uint32_t WS_EX_LAYERED        = 0x00080000;

// Messages
inline constexpr uint32_t WM_NULL            = 0x0000;
inline constexpr uint32_t WM_CREATE          = 0x0001;
inline constexpr uint32_t WM_DESTROY         = 0x0002;
inline constexpr uint32_t WM_MOVE            = 0x0003;
inline constexpr uint32_t WM_SIZE            = 0x0005;
inline constexpr uint32_t WM_ACTIVATE        = 0x0006;
inline constexpr uint32_t WM_SETFOCUS        = 0x0007;
inline constexpr uint32_t WM_KILLFOCUS       = 0x0008;
inline constexpr uint32_t WM_ENABLE          = 0x000A;
inline constexpr uint32_t WM_PAINT           = 0x000F;
inline constexpr uint32_t WM_CLOSE           = 0x0010;
inline constexpr uint32_t WM_QUIT            = 0x0012;
inline constexpr uint32_t WM_ERASEBKGND      = 0x0014;
inline constexpr uint32_t WM_SHOWWINDOW      = 0x0018;
inline constexpr uint32_t WM_GETMINMAXINFO   = 0x0024;
inline constexpr uint32_t WM_NCCREATE        = 0x0081;
inline constexpr uint32_t WM_NCDESTROY       = 0x0082;
inline constexpr uint32_t WM_NCCALCSIZE      = 0x0083;
inline constexpr uint32_t WM_INPUT           = 0x00FF;
inline constexpr uint32_t WM_KEYDOWN         = 0x0100;
inline constexpr uint32_t WM_KEYUP           = 0x0101;
inline constexpr uint32_t WM_CHAR            = 0x0102;
inline constexpr uint32_t WM_MOUSEMOVE       = 0x0200;
inline constexpr uint32_t WM_LBUTTONDOWN     = 0x0201;
inline constexpr uint32_t WM_LBUTTONUP       = 0x0202;
inline constexpr uint32_t WM_RBUTTONDOWN     = 0x0204;
inline constexpr uint32_t WM_RBUTTONUP       = 0x0205;
inline constexpr uint32_t WM_MBUTTONDOWN     = 0x0207;
inline constexpr uint32_t WM_MBUTTONUP       = 0x0208;
inline constexpr uint32_t WM_MOUSEWHEEL      = 0x020A;
inline constexpr uint32_t WM_USER            = 0x0400;

// ShowWindow Commands
inline constexpr int SW_HIDE            = 0;
inline constexpr int SW_SHOWNORMAL      = 1;
inline constexpr int SW_NORMAL          = 1;
inline constexpr int SW_SHOWMINIMIZED   = 2;
inline constexpr int SW_SHOWMAXIMIZED   = 3;
inline constexpr int SW_MAXIMIZE        = 3;
inline constexpr int SW_SHOWNOACTIVATE  = 4;
inline constexpr int SW_SHOW            = 5;
inline constexpr int SW_MINIMIZE        = 6;
inline constexpr int SW_SHOWMINNOACTIVE = 7;
inline constexpr int SW_SHOWNA          = 8;
inline constexpr int SW_RESTORE         = 9;

// PeekMessage Options
inline constexpr uint32_t PM_NOREMOVE = 0x0000;
inline constexpr uint32_t PM_REMOVE   = 0x0001;
inline constexpr uint32_t PM_NOYIELD  = 0x0002;

// SetWindowPos Flags
inline constexpr uint32_t SWP_NOSIZE       = 0x0001;
inline constexpr uint32_t SWP_NOMOVE       = 0x0002;
inline constexpr uint32_t SWP_NOZORDER     = 0x0004;
inline constexpr uint32_t SWP_NOREDRAW     = 0x0008;
inline constexpr uint32_t SWP_NOACTIVATE   = 0x0010;
inline constexpr uint32_t SWP_FRAMECHANGED = 0x0020;
inline constexpr uint32_t SWP_SHOWWINDOW   = 0x0040;
inline constexpr uint32_t SWP_HIDEWINDOW   = 0x0080;

// WindowLongPtr offsets
inline constexpr int GWL_WNDPROC    = -4;
inline constexpr int GWL_HINSTANCE  = -6;
inline constexpr int GWL_HWNDPARENT = -8;
inline constexpr int GWL_STYLE      = -16;
inline constexpr int GWL_EXSTYLE    = -20;
inline constexpr int GWL_USERDATA   = -21;
inline constexpr int GWL_ID         = -12;

inline constexpr int GWLP_WNDPROC    = -4;
inline constexpr int GWLP_HINSTANCE  = -6;
inline constexpr int GWLP_HWNDPARENT = -8;
inline constexpr int GWLP_USERDATA   = -21;
inline constexpr int GWLP_ID         = -12;

// Raw Input Constants
inline constexpr uint32_t RIM_TYPEMOUSE    = 0;
inline constexpr uint32_t RIM_TYPEKEYBOARD = 1;
inline constexpr uint32_t RIM_TYPEHID      = 2;

inline constexpr uint32_t RIDEV_INPUTSINK = 0x00000100;
inline constexpr uint32_t RID_INPUT       = 0x10000003;
inline constexpr uint32_t RID_HEADER      = 0x10000005;

inline constexpr uint16_t RI_MOUSE_LEFT_BUTTON_DOWN   = 0x0001;
inline constexpr uint16_t RI_MOUSE_LEFT_BUTTON_UP     = 0x0002;
inline constexpr uint16_t RI_MOUSE_RIGHT_BUTTON_DOWN  = 0x0004;
inline constexpr uint16_t RI_MOUSE_RIGHT_BUTTON_UP    = 0x0008;
inline constexpr uint16_t RI_MOUSE_MIDDLE_BUTTON_DOWN = 0x0010;
inline constexpr uint16_t RI_MOUSE_MIDDLE_BUTTON_UP   = 0x0020;
inline constexpr uint16_t RI_MOUSE_WHEEL              = 0x0400;

inline constexpr uint32_t RIDI_PREPARSEDDATA = 0x20000005U;
inline constexpr uint32_t RIDI_DEVICENAME     = 0x20000007U;
inline constexpr uint32_t RIDI_DEVICEINFO     = 0x2000000BU;

inline constexpr uint32_t RIDEV_REMOVE        = 0x00000001U;
inline constexpr uint32_t RIDEV_NOLEGACY      = 0x00000030U;
inline constexpr uint32_t RIDEV_CAPTUREMOUSE  = 0x00000200U;
inline constexpr uint32_t RIDEV_DEVNOTIFY     = 0x00002000U;

struct RAWINPUTHEADER {
    uint32_t dwType{0};
    uint32_t dwSize{0};
    win32::HANDLE hDevice{nullptr};
    WPARAM   wParam{0};
};

struct RAWMOUSE {
    uint16_t usFlags{0};
    uint16_t usButtonFlags{0};
    uint16_t usButtonData{0};
    uint32_t ulRawButtons{0};
    int32_t  lLastX{0};
    int32_t  lLastY{0};
    uint32_t ulExtraInformation{0};
};

struct RAWKEYBOARD {
    uint16_t MakeCode{0};
    uint16_t Flags{0};
    uint16_t Reserved{0};
    uint16_t VKey{0};
    uint32_t Message{0};
    uint32_t ExtraInformation{0};
};

struct RAWHID {
    uint32_t dwSizeHid{0};
    uint32_t dwCount{0};
    uint8_t  bRawData[1]{0};
};

struct RAWINPUT {
    RAWINPUTHEADER header{};
    union {
        RAWMOUSE    mouse;
        RAWKEYBOARD keyboard;
        RAWHID      hid;
    } data{};
};
using PRAWINPUT  = RAWINPUT*;
using LPRAWINPUT = RAWINPUT*;

struct RAWINPUTDEVICE {
    uint16_t    usUsagePage{0};
    uint16_t    usUsage{0};
    uint32_t    dwFlags{0};
    win32::HWND hwndTarget{nullptr};
};
using PCRAWINPUTDEVICE = const RAWINPUTDEVICE*;
using PRAWINPUTDEVICE  = RAWINPUTDEVICE*;

struct RAWINPUTDEVICELIST {
    win32::HANDLE hDevice{nullptr};
    uint32_t      dwType{0};
};
using PRAWINPUTDEVICELIST = RAWINPUTDEVICELIST*;

struct RID_DEVICE_INFO_MOUSE {
    uint32_t dwId{0};
    uint32_t dwNumberOfButtons{0};
    uint32_t dwSampleRate{0};
    win32::BOOL fHasHorizontalWheel{0};
};

struct RID_DEVICE_INFO_KEYBOARD {
    uint32_t dwType{0};
    uint32_t dwSubType{0};
    uint32_t dwKeyboardMode{0};
    uint32_t dwNumberOfFunctionKeys{0};
    uint32_t dwNumberOfIndicators{0};
    uint32_t dwNumberOfKeysTotal{0};
};

struct RID_DEVICE_INFO_HID {
    uint32_t dwVendorId{0};
    uint32_t dwProductId{0};
    uint32_t dwVersionNumber{0};
    uint16_t usUsagePage{0};
    uint16_t usUsage{0};
};

struct RID_DEVICE_INFO {
    uint32_t cbSize{sizeof(RID_DEVICE_INFO)};
    uint32_t dwType{0};
    union {
        RID_DEVICE_INFO_MOUSE    mouse;
        RID_DEVICE_INFO_KEYBOARD keyboard;
        RID_DEVICE_INFO_HID      hid;
    } u{};
};
using PRID_DEVICE_INFO = RID_DEVICE_INFO*;

// ============================================================================
// 3. Window Object & Backbuffer Compositor Surface
// ============================================================================

class WindowObject {
public:
    win32::HWND   hwnd{nullptr};
    std::wstring  className;
    std::wstring  windowName;
    uint32_t      style{0};
    uint32_t      exStyle{0};
    int32_t       x{0};
    int32_t       y{0};
    int32_t       width{800};
    int32_t       height{600};
    WNDPROC       wndProc{nullptr};
    HINSTANCE     hInstance{nullptr};
    win32::HWND   hwndParent{nullptr};
    bool          visible{false};
    bool          minimized{false};
    bool          active{false};
    uintptr_t     userData{0};

    // 32-bit RGBA dedicated backbuffer surface
    std::vector<uint32_t> surfacePixels;
    bool                  dirty{true};
    RECT                  dirtyRect{0, 0, 800, 600};

    WindowObject(win32::HWND h, std::wstring_view cls, std::wstring_view name, uint32_t st, uint32_t exSt,
                 int x_, int y_, int w, int h_, WNDPROC proc, HINSTANCE inst, win32::HWND parent)
        : hwnd(h), className(cls), windowName(name), style(st), exStyle(exSt),
          x(x_), y(y_), width(std::max(1, w)), height(std::max(1, h_)),
          wndProc(proc), hInstance(inst), hwndParent(parent) {
        surfacePixels.resize(static_cast<size_t>(width) * height, 0xFF000000); // Opaque black
        dirtyRect = RECT{0, 0, width, height};
    }

    void resize(int32_t newWidth, int32_t newHeight) {
        width = std::max(1, newWidth);
        height = std::max(1, newHeight);
        surfacePixels.assign(static_cast<size_t>(width) * height, 0xFF000000);
        dirty = true;
        dirtyRect = RECT{0, 0, width, height};
    }
};

// ============================================================================
// 4. Sovereign Window Manager Engine
// ============================================================================

class WindowManager {
public:
    using PhysicalDisplayPresenterCallback = void (*)(int32_t x, int32_t y, uint32_t width, uint32_t height, const uint32_t* pixels, uint32_t pitch);

    static WindowManager& get() {
        static WindowManager instance;
        return instance;
    }

    void setPhysicalPresenter(PhysicalDisplayPresenterCallback cb) noexcept { m_physicalPresenter = cb; }
    [[nodiscard]] PhysicalDisplayPresenterCallback getPhysicalPresenter() const noexcept { return m_physicalPresenter; }

    // ------------------------------------------------------------------------
    // Window Class Registry
    // ------------------------------------------------------------------------
    uint16_t registerClass(const WNDCLASSEXW* lpwcx) {
        if (!lpwcx || !lpwcx->lpszClassName) return 0;
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        std::wstring name;
        if (reinterpret_cast<uintptr_t>(lpwcx->lpszClassName) <= 0xFFFF) {
            name = L"#atom#" + std::to_wstring(reinterpret_cast<uintptr_t>(lpwcx->lpszClassName));
        } else {
            name = lpwcx->lpszClassName;
        }

        uint16_t atom = static_cast<uint16_t>(0xC000 + classRegistry_.size() + 1);
        classRegistry_[name] = *lpwcx;
        atomMap_[atom] = name;
        return atom;
    }

    bool unregisterClass(const wchar_t* lpClassName, HINSTANCE /*hInstance*/) {
        if (!lpClassName) return false;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return classRegistry_.erase(lpClassName) > 0;
    }

    bool getClassInfo(const wchar_t* lpClassName, WNDCLASSEXW* lpwcx) {
        if (!lpClassName || !lpwcx) return false;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = classRegistry_.find(lpClassName);
        if (it != classRegistry_.end()) {
            *lpwcx = it->second;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Window Lifecycle
    // ------------------------------------------------------------------------
    win32::HWND createWindow(
        uint32_t dwExStyle,
        const wchar_t* lpClassName,
        const wchar_t* lpWindowName,
        uint32_t dwStyle,
        int x, int y, int nWidth, int nHeight,
        win32::HWND hWndParent,
        HMENU /*hMenu*/,
        HINSTANCE hInstance,
        void* lpParam
    ) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        std::wstring clsName;
        if (reinterpret_cast<uintptr_t>(lpClassName) <= 0xFFFF) {
            uint16_t atom = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(lpClassName));
            auto itA = atomMap_.find(atom);
            if (itA != atomMap_.end()) {
                clsName = itA->second;
            } else {
                clsName = L"#atom#" + std::to_wstring(atom);
            }
        } else if (lpClassName) {
            clsName = lpClassName;
        }
        std::wstring winName = lpWindowName ? lpWindowName : L"";

        WNDPROC proc = nullptr;
        auto classIt = classRegistry_.find(clsName);
        if (classIt != classRegistry_.end()) {
            proc = classIt->second.lpfnWndProc;
        }

        uintptr_t handleVal = nextHwnd_++;
        win32::HWND hwnd = reinterpret_cast<win32::HWND>(handleVal);

        int actualW = (nWidth <= 0) ? 800 : nWidth;
        int actualH = (nHeight <= 0) ? 600 : nHeight;

        auto window = std::make_shared<WindowObject>(
            hwnd, clsName, winName, dwStyle, dwExStyle,
            x, y, actualW, actualH, proc, hInstance, hWndParent
        );

        windows_[hwnd] = window;
        if (!activeHwnd_) activeHwnd_ = hwnd;

        // Deliver Win32 creation sequence synchronously: WM_NCCREATE -> WM_NCCALCSIZE -> WM_CREATE
        if (proc) {
            CREATESTRUCTW cs{};
            cs.lpCreateParams = lpParam;
            cs.hInstance = hInstance;
            cs.hwndParent = hWndParent;
            cs.cy = actualH;
            cs.cx = actualW;
            cs.y = y;
            cs.x = x;
            cs.style = dwStyle;
            cs.lpszName = lpWindowName;
            cs.lpszClass = lpClassName;
            cs.dwExStyle = dwExStyle;

            // In Win32, WM_NCCREATE is sent first. Applications attach GWLP_USERDATA here.
            proc(hwnd, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&cs));

            RECT rcCalc{0, 0, actualW, actualH};
            proc(hwnd, WM_NCCALCSIZE, 0, reinterpret_cast<LPARAM>(&rcCalc));
            proc(hwnd, WM_CREATE, 0, reinterpret_cast<LPARAM>(&cs));
        }

        if ((dwStyle & WS_VISIBLE) != 0) {
            window->visible = true;
            postMessageInternal(hwnd, WM_SIZE, 0, (actualH << 16) | (actualW & 0xFFFF));
            postMessageInternal(hwnd, WM_PAINT, 0, 0);
        }

        return hwnd;
    }

    bool destroyWindow(win32::HWND hWnd) {
        std::shared_ptr<WindowObject> win;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            auto it = windows_.find(hWnd);
            if (it == windows_.end()) return false;
            win = it->second;
            windows_.erase(it);
            if (activeHwnd_ == hWnd) activeHwnd_ = nullptr;
            if (focusHwnd_ == hWnd) focusHwnd_ = nullptr;
        }

        if (win && win->wndProc) {
            win->wndProc(hWnd, WM_DESTROY, 0, 0);
        }
        return true;
    }

    std::shared_ptr<WindowObject> getWindow(win32::HWND hWnd) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) return it->second;
        return nullptr;
    }

    bool isWindow(win32::HWND hWnd) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return windows_.find(hWnd) != windows_.end();
    }

    std::vector<win32::HWND> enumerateTopLevelWindows() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<win32::HWND> result;
        result.reserve(windows_.size());
        for (const auto& entry : windows_) {
            const auto& window = entry.second;
            if (window && window->hwndParent == nullptr) result.push_back(entry.first);
        }
        return result;
    }

    // ------------------------------------------------------------------------
    // Message Pump & Event Routing
    // ------------------------------------------------------------------------
    bool postMessage(win32::HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return postMessageInternal(hWnd, uMsg, wParam, lParam);
    }

    LRESULT sendMessage(win32::HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        std::shared_ptr<WindowObject> win;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            auto it = windows_.find(hWnd);
            if (it != windows_.end()) win = it->second;
        }

        LRESULT res = 0;
        if (win && win->wndProc) {
            res = win->wndProc(hWnd, uMsg, wParam, lParam);
        } else {
            res = defWindowProc(hWnd, uMsg, wParam, lParam);
        }

        // Scintilla direct dispatch fallbacks if unhandled
        if (res == 0) {
            if (uMsg == 2184) { // SCI_GETDIRECTFUNCTION
                static auto DirectStub = [](void* /*ptr*/, uint32_t /*msg*/, uintptr_t /*w*/, intptr_t /*l*/) -> intptr_t {
                    return 0;
                };
                return reinterpret_cast<LRESULT>(+DirectStub);
            }
            if (uMsg == 2185) { // SCI_GETDIRECTPOINTER
                return reinterpret_cast<LRESULT>(hWnd ? hWnd : reinterpret_cast<win32::HWND>(0x5001));
            }
        }
        return res;
    }

    bool peekMessage(MSG* lpMsg, win32::HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
        if (!lpMsg) return false;
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        for (auto it = messageQueue_.begin(); it != messageQueue_.end(); ++it) {
            if (hWnd && it->hwnd != hWnd) continue;
            if (wMsgFilterMin && wMsgFilterMax && (it->message < wMsgFilterMin || it->message > wMsgFilterMax)) continue;

            *lpMsg = *it;
            if ((wRemoveMsg & PM_REMOVE) != 0) {
                messageQueue_.erase(it);
            }
            return true;
        }

        // If no posted message found, check for dirty paint
        if (!hWnd) {
            for (auto& pair : windows_) {
                if (pair.second->visible && pair.second->dirty) {
                    lpMsg->hwnd = pair.first;
                    lpMsg->message = WM_PAINT;
                    lpMsg->wParam = 0;
                    lpMsg->lParam = 0;
                    lpMsg->time = 0;
                    lpMsg->pt = POINT{0, 0};
                    if ((wRemoveMsg & PM_REMOVE) != 0) {
                        pair.second->dirty = false;
                    }
                    return true;
                }
            }
        } else {
            auto it = windows_.find(hWnd);
            if (it != windows_.end() && it->second->visible && it->second->dirty) {
                lpMsg->hwnd = hWnd;
                lpMsg->message = WM_PAINT;
                lpMsg->wParam = 0;
                lpMsg->lParam = 0;
                lpMsg->time = 0;
                lpMsg->pt = POINT{0, 0};
                if ((wRemoveMsg & PM_REMOVE) != 0) {
                    it->second->dirty = false;
                }
                return true;
            }
        }

        return false;
    }

    bool getMessage(MSG* lpMsg, win32::HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
        if (!lpMsg) return false;

        // Peek with remove
        if (peekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE)) {
            return (lpMsg->message != WM_QUIT);
        }

        // Default empty message
        lpMsg->hwnd = hWnd;
        lpMsg->message = WM_NULL;
        lpMsg->wParam = 0;
        lpMsg->lParam = 0;
        return true;
    }

    void postQuitMessage(int nExitCode) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        MSG msg{};
        msg.hwnd = nullptr;
        msg.message = WM_QUIT;
        msg.wParam = static_cast<WPARAM>(nExitCode);
        messageQueue_.push_back(msg);
    }

    LRESULT dispatchMessage(const MSG* lpMsg) {
        if (!lpMsg) return 0;
        if (lpMsg->hwnd) {
            std::shared_ptr<WindowObject> win;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                auto it = windows_.find(lpMsg->hwnd);
                if (it != windows_.end()) win = it->second;
            }
            if (win && win->wndProc) {
                return win->wndProc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
            }
        }
        return defWindowProc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }

    LRESULT defWindowProc(win32::HWND hWnd, UINT uMsg, WPARAM /*wParam*/, LPARAM /*lParam*/) {
        switch (uMsg) {
            case WM_NCCREATE:
                return 1;
            case WM_CLOSE:
                destroyWindow(hWnd);
                return 0;
            case WM_PAINT:
                // Auto validate window region
                validateRect(hWnd, nullptr);
                return 0;
            default:
                break;
        }
        return 0;
    }

    // ------------------------------------------------------------------------
    // Geometry, Painting & GDI Surface
    // ------------------------------------------------------------------------
    bool getClientRect(win32::HWND hWnd, RECT* lpRect) {
        if (!lpRect) return false;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) {
            lpRect->left = 0;
            lpRect->top = 0;
            lpRect->right = it->second->width;
            lpRect->bottom = it->second->height;
            return true;
        }
        return false;
    }

    bool getWindowRect(win32::HWND hWnd, RECT* lpRect) {
        if (!lpRect) return false;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) {
            lpRect->left = it->second->x;
            lpRect->top = it->second->y;
            lpRect->right = it->second->x + it->second->width;
            lpRect->bottom = it->second->y + it->second->height;
            return true;
        }
        return false;
    }

    bool setWindowPos(win32::HWND hWnd, win32::HWND /*hWndInsertAfter*/, int X, int Y, int cx, int cy, UINT uFlags) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it == windows_.end()) return false;

        auto win = it->second;
        if ((uFlags & SWP_NOMOVE) == 0) {
            win->x = X;
            win->y = Y;
        }
        if ((uFlags & SWP_NOSIZE) == 0) {
            win->resize(cx, cy);
            postMessageInternal(hWnd, WM_SIZE, 0, (win->height << 16) | (win->width & 0xFFFF));
        }
        if ((uFlags & SWP_SHOWWINDOW) != 0) {
            win->visible = true;
        }
        if ((uFlags & SWP_HIDEWINDOW) != 0) {
            win->visible = false;
        }
        return true;
    }

    bool invalidateRect(win32::HWND hWnd, const RECT* lpRect, win32::BOOL /*bErase*/) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) {
            it->second->dirty = true;
            if (lpRect) it->second->dirtyRect = *lpRect;
            else it->second->dirtyRect = RECT{0, 0, it->second->width, it->second->height};
            return true;
        }
        return false;
    }

    bool validateRect(win32::HWND hWnd, const RECT* /*lpRect*/) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) {
            it->second->dirty = false;
            return true;
        }
        return false;
    }

    HDC beginPaint(win32::HWND hWnd, PAINTSTRUCT* lpPaint) {
        if (!lpPaint) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) {
            lpPaint->hdc = reinterpret_cast<HDC>(reinterpret_cast<uintptr_t>(hWnd) | 0x1);
            lpPaint->rcPaint = it->second->dirtyRect;
            lpPaint->fErase = win32::FALSE;
            it->second->dirty = false;
            return lpPaint->hdc;
        }
        return nullptr;
    }

    bool endPaint(win32::HWND hWnd, const PAINTSTRUCT* /*lpPaint*/) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) {
            it->second->dirty = false;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Presentation Blit & Surface Bridge
    // ------------------------------------------------------------------------
    void blitToWindow(win32::HWND hWnd, const uint8_t* pData, uint32_t width, uint32_t height, uint32_t pitch) {
        if (!hWnd || !pData) return;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it == windows_.end()) return;

        auto win = it->second;
        uint32_t copyW = std::min(static_cast<uint32_t>(win->width), width);
        uint32_t copyH = std::min(static_cast<uint32_t>(win->height), height);

        for (uint32_t row = 0; row < copyH; ++row) {
            const auto* srcRow = reinterpret_cast<const uint32_t*>(pData + row * pitch);
            auto* dstRow = &win->surfacePixels[row * static_cast<size_t>(win->width)];
            std::memcpy(dstRow, srcRow, copyW * sizeof(uint32_t));
        }

        win->dirty = false;

        if (m_physicalPresenter && win->visible) {
            m_physicalPresenter(win->x, win->y, copyW, copyH, win->surfacePixels.data(), win->width);
        }
    }

    const uint32_t* getWindowPixelBuffer(win32::HWND hWnd, uint32_t* pWidth, uint32_t* pHeight) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it != windows_.end()) {
            if (pWidth) *pWidth = static_cast<uint32_t>(it->second->width);
            if (pHeight) *pHeight = static_cast<uint32_t>(it->second->height);
            return it->second->surfacePixels.data();
        }
        return nullptr;
    }

    // ------------------------------------------------------------------------
    // Raw Input Subsystem
    // ------------------------------------------------------------------------
    bool registerRawInputDevices(const RAWINPUTDEVICE* pRawInputDevices, UINT uiNumDevices, UINT cbSize) {
        if (!pRawInputDevices || cbSize < sizeof(RAWINPUTDEVICE)) return false;
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        registeredRawDevices_.assign(pRawInputDevices, pRawInputDevices + uiNumDevices);
        return true;
    }

    UINT getRawInputData(HRAWINPUT hRawInput, UINT uiCommand, void* pData, UINT* pcbSize, UINT cbSizeHeader) {
        if (!pcbSize || cbSizeHeader < sizeof(RAWINPUTHEADER)) return static_cast<UINT>(-1);
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        auto it = rawInputBuffer_.find(hRawInput);
        if (it == rawInputBuffer_.end()) return static_cast<UINT>(-1);

        const auto& packet = it->second;
        UINT requiredSize = sizeof(RAWINPUT);

        if (uiCommand == RID_HEADER) {
            requiredSize = sizeof(RAWINPUTHEADER);
            if (!pData || *pcbSize < requiredSize) {
                *pcbSize = requiredSize;
                return 0;
            }
            std::memcpy(pData, &packet.header, sizeof(RAWINPUTHEADER));
            return sizeof(RAWINPUTHEADER);
        }

        if (uiCommand == RID_INPUT) {
            if (!pData || *pcbSize < requiredSize) {
                *pcbSize = requiredSize;
                return 0;
            }
            std::memcpy(pData, &packet, sizeof(RAWINPUT));
            return sizeof(RAWINPUT);
        }

        return static_cast<UINT>(-1);
    }

    UINT getRawInputDeviceList(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize) {
        if (!puiNumDevices) return static_cast<UINT>(-1);
        if (pRawInputDeviceList && cbSize < sizeof(RAWINPUTDEVICELIST)) return static_cast<UINT>(-1);

        constexpr UINT kDevCount = 3;
        if (!pRawInputDeviceList) {
            *puiNumDevices = kDevCount;
            return 0;
        }

        if (*puiNumDevices < kDevCount) {
            *puiNumDevices = kDevCount;
            return static_cast<UINT>(-1);
        }

        pRawInputDeviceList[0] = { reinterpret_cast<win32::HANDLE>(0x000000000000B001ULL), RIM_TYPEMOUSE };
        pRawInputDeviceList[1] = { reinterpret_cast<win32::HANDLE>(0x000000000000B002ULL), RIM_TYPEKEYBOARD };
        pRawInputDeviceList[2] = { reinterpret_cast<win32::HANDLE>(0x000000000000B003ULL), RIM_TYPEHID };
        *puiNumDevices = kDevCount;
        return kDevCount;
    }

    UINT getRawInputDeviceInfoW(win32::HANDLE hDevice, UINT uiCommand, void* pData, PUINT pcbSize) {
        if (!pcbSize) return static_cast<UINT>(-1);

        uintptr_t devId = reinterpret_cast<uintptr_t>(hDevice);
        if (uiCommand == RIDI_DEVICENAME) {
            const wchar_t* name = L"\\\\?\\HID#VID_1532&PID_0078&MI_00#7&2b781e8&0&0000#{378de44c-5643-11d1-acf9-00a0c9a06748}";
            if (devId == 0x000000000000B002ULL) {
                name = L"\\\\?\\HID#VID_046D&PID_C332&MI_00#7&1a941f1&0&0000#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}";
            } else if (devId == 0x000000000000B003ULL) {
                name = L"\\\\?\\HID#VID_045E&PID_028E&IG_00#7&3c829e1&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}";
            }
            UINT reqChars = static_cast<UINT>(std::wcslen(name) + 1);
            if (!pData || *pcbSize < reqChars) {
                *pcbSize = reqChars;
                return static_cast<UINT>(-1);
            }
            std::wmemcpy(reinterpret_cast<wchar_t*>(pData), name, reqChars);
            return reqChars;
        }

        if (uiCommand == RIDI_DEVICEINFO) {
            UINT reqBytes = sizeof(RID_DEVICE_INFO);
            if (!pData || *pcbSize < reqBytes) {
                *pcbSize = reqBytes;
                return static_cast<UINT>(-1);
            }
            RID_DEVICE_INFO* pInfo = reinterpret_cast<RID_DEVICE_INFO*>(pData);
            std::memset(pInfo, 0, sizeof(RID_DEVICE_INFO));
            pInfo->cbSize = sizeof(RID_DEVICE_INFO);
            if (devId == 0x000000000000B001ULL) {
                pInfo->dwType = RIM_TYPEMOUSE;
                pInfo->u.mouse.dwId = 1;
                pInfo->u.mouse.dwNumberOfButtons = 5;
                pInfo->u.mouse.dwSampleRate = 8000;
                pInfo->u.mouse.fHasHorizontalWheel = 1;
            } else if (devId == 0x000000000000B002ULL) {
                pInfo->dwType = RIM_TYPEKEYBOARD;
                pInfo->u.keyboard.dwType = 81;
                pInfo->u.keyboard.dwSubType = 0;
                pInfo->u.keyboard.dwKeyboardMode = 1;
                pInfo->u.keyboard.dwNumberOfFunctionKeys = 12;
                pInfo->u.keyboard.dwNumberOfIndicators = 3;
                pInfo->u.keyboard.dwNumberOfKeysTotal = 104;
            } else {
                pInfo->dwType = RIM_TYPEHID;
                pInfo->u.hid.dwVendorId = 0x045E;
                pInfo->u.hid.dwProductId = 0x028E;
                pInfo->u.hid.dwVersionNumber = 0x0114;
                pInfo->u.hid.usUsagePage = 1;
                pInfo->u.hid.usUsage = 5; // Gamepad
            }
            return reqBytes;
        }

        return static_cast<UINT>(-1);
    }

    UINT getRegisteredRawInputDevices(PRAWINPUTDEVICE pRawInputDevices, PUINT puiNumDevices, UINT cbSize) {
        if (!puiNumDevices) return static_cast<UINT>(-1);
        if (pRawInputDevices && cbSize < sizeof(RAWINPUTDEVICE)) return static_cast<UINT>(-1);
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        UINT count = static_cast<UINT>(registeredRawDevices_.size());
        if (!pRawInputDevices) {
            *puiNumDevices = count;
            return 0;
        }

        if (*puiNumDevices < count) {
            *puiNumDevices = count;
            return static_cast<UINT>(-1);
        }

        for (UINT i = 0; i < count; ++i) {
            pRawInputDevices[i] = registeredRawDevices_[i];
        }
        *puiNumDevices = count;
        return count;
    }

    UINT getRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader) {
        if (!pcbSize || cbSizeHeader < sizeof(RAWINPUTHEADER)) return static_cast<UINT>(-1);
        std::lock_guard<std::recursive_mutex> lock(mutex_);

        UINT availableBytes = static_cast<UINT>(rawInputBuffer_.size() * sizeof(RAWINPUT));
        if (!pData || *pcbSize < availableBytes) {
            *pcbSize = availableBytes;
            return 0;
        }

        UINT count = 0;
        RAWINPUT* outPtr = pData;
        for (const auto& [_, ri] : rawInputBuffer_) {
            *outPtr++ = ri;
            count++;
        }
        rawInputBuffer_.clear();
        return count;
    }

    HRAWINPUT injectRawMouse(win32::HWND hWnd, int32_t dx, int32_t dy, uint16_t buttonFlags) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        HRAWINPUT handle = reinterpret_cast<HRAWINPUT>(nextRawHandle_++);

        RAWINPUT ri{};
        ri.header.dwType = RIM_TYPEMOUSE;
        ri.header.dwSize = sizeof(RAWINPUT);
        ri.header.hDevice = reinterpret_cast<win32::HANDLE>(0x000000000000B001ULL);
        ri.data.mouse.lLastX = dx;
        ri.data.mouse.lLastY = dy;
        ri.data.mouse.usButtonFlags = buttonFlags;

        rawInputBuffer_[handle] = ri;
        postMessageInternal(hWnd, WM_INPUT, 0, reinterpret_cast<LPARAM>(handle));
        return handle;
    }

    HRAWINPUT injectRawKeyboard(win32::HWND hWnd, uint16_t vkey, uint16_t makeCode, uint16_t flags, uint32_t message) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        HRAWINPUT handle = reinterpret_cast<HRAWINPUT>(nextRawHandle_++);

        RAWINPUT ri{};
        ri.header.dwType = RIM_TYPEKEYBOARD;
        ri.header.dwSize = sizeof(RAWINPUT);
        ri.header.hDevice = reinterpret_cast<win32::HANDLE>(0x000000000000B002ULL);
        ri.data.keyboard.VKey = vkey;
        ri.data.keyboard.MakeCode = makeCode;
        ri.data.keyboard.Flags = flags;
        ri.data.keyboard.Message = message;

        rawInputBuffer_[handle] = ri;
        postMessageInternal(hWnd, WM_INPUT, 0, reinterpret_cast<LPARAM>(handle));
        return handle;
    }

    // ------------------------------------------------------------------------
    // Focus, Active Window & Long Attributes
    // ------------------------------------------------------------------------
    win32::HWND getActiveWindow() const noexcept { return activeHwnd_; }
    win32::HWND setActiveWindow(win32::HWND hWnd) noexcept {
        win32::HWND prev = activeHwnd_;
        activeHwnd_ = hWnd;
        return prev;
    }

    win32::HWND getFocus() const noexcept { return focusHwnd_; }
    win32::HWND setFocus(win32::HWND hWnd) noexcept {
        win32::HWND prev = focusHwnd_;
        focusHwnd_ = hWnd;
        return prev;
    }

    uintptr_t setWindowLongPtr(win32::HWND hWnd, int nIndex, uintptr_t dwNewLong) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it == windows_.end()) return 0;

        auto win = it->second;
        uintptr_t oldVal = 0;
        switch (nIndex) {
            case GWLP_USERDATA:
                oldVal = win->userData;
                win->userData = dwNewLong;
                break;
            case GWLP_WNDPROC:
                oldVal = reinterpret_cast<uintptr_t>(win->wndProc);
                win->wndProc = reinterpret_cast<WNDPROC>(dwNewLong);
                break;
            case GWL_STYLE:
                oldVal = win->style;
                win->style = static_cast<uint32_t>(dwNewLong);
                break;
            case GWL_EXSTYLE:
                oldVal = win->exStyle;
                win->exStyle = static_cast<uint32_t>(dwNewLong);
                break;
            default:
                break;
        }
        return oldVal;
    }

    uintptr_t getWindowLongPtr(win32::HWND hWnd, int nIndex) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = windows_.find(hWnd);
        if (it == windows_.end()) return 0;

        auto win = it->second;
        switch (nIndex) {
            case GWLP_USERDATA: return win->userData;
            case GWLP_WNDPROC:  return reinterpret_cast<uintptr_t>(win->wndProc);
            case GWL_STYLE:     return win->style;
            case GWL_EXSTYLE:   return win->exStyle;
            default:            return 0;
        }
    }

private:
    WindowManager() = default;

    bool postMessageInternal(win32::HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        MSG msg{};
        msg.hwnd = hWnd;
        msg.message = uMsg;
        msg.wParam = wParam;
        msg.lParam = lParam;
        msg.time = 0;
        msg.pt = POINT{0, 0};
        messageQueue_.push_back(msg);
        return true;
    }

    std::recursive_mutex mutex_;
    uintptr_t nextHwnd_{0x00010001};
    uintptr_t nextRawHandle_{0x00000001};
    win32::HWND activeHwnd_{nullptr};
    win32::HWND focusHwnd_{nullptr};

    std::unordered_map<std::wstring, WNDCLASSEXW> classRegistry_;
    std::unordered_map<uint16_t, std::wstring> atomMap_;
    std::unordered_map<win32::HWND, std::shared_ptr<WindowObject>> windows_;
    std::deque<MSG> messageQueue_;
    std::vector<RAWINPUTDEVICE> registeredRawDevices_;
    std::unordered_map<HRAWINPUT, RAWINPUT> rawInputBuffer_;
    PhysicalDisplayPresenterCallback m_physicalPresenter{nullptr};
};

// ============================================================================
// 5. Presentation Callback Hook
// ============================================================================

inline void BlitToWindowCallback(void* hwnd, const uint8_t* pData, uint32_t width, uint32_t height, uint32_t pitch) {
    WindowManager::get().blitToWindow(reinterpret_cast<win32::HWND>(hwnd), pData, width, height, pitch);
}

// ============================================================================
// 6. Win32 User32 C-API Functions
// ============================================================================

inline uint16_t RegisterClassExW(const WNDCLASSEXW* lpwcx) noexcept {
    return WindowManager::get().registerClass(lpwcx);
}

inline uint16_t RegisterClassW(const WNDCLASSW* lpWndClass) noexcept {
    if (!lpWndClass) return 0;
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = lpWndClass->style;
    wcex.lpfnWndProc = lpWndClass->lpfnWndProc;
    wcex.cbClsExtra = lpWndClass->cbClsExtra;
    wcex.cbWndExtra = lpWndClass->cbWndExtra;
    wcex.hInstance = lpWndClass->hInstance;
    wcex.hIcon = lpWndClass->hIcon;
    wcex.hCursor = lpWndClass->hCursor;
    wcex.hbrBackground = lpWndClass->hbrBackground;
    wcex.lpszMenuName = lpWndClass->lpszMenuName;
    wcex.lpszClassName = lpWndClass->lpszClassName;
    return WindowManager::get().registerClass(&wcex);
}

inline win32::BOOL UnregisterClassW(const wchar_t* lpClassName, HINSTANCE hInstance) noexcept {
    return WindowManager::get().unregisterClass(lpClassName, hInstance) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL GetClassInfoExW(HINSTANCE /*hInstance*/, const wchar_t* lpClassName, WNDCLASSEXW* lpwcx) noexcept {
    return WindowManager::get().getClassInfo(lpClassName, lpwcx) ? win32::TRUE : win32::FALSE;
}

inline win32::HWND CreateWindowExW(
    uint32_t dwExStyle,
    const wchar_t* lpClassName,
    const wchar_t* lpWindowName,
    uint32_t dwStyle,
    int X, int Y, int nWidth, int nHeight,
    win32::HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    void* lpParam
) noexcept {
    return WindowManager::get().createWindow(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

inline win32::HWND CreateWindowExA(
    uint32_t dwExStyle,
    const char* lpClassName,
    const char* lpWindowName,
    uint32_t dwStyle,
    int X, int Y, int nWidth, int nHeight,
    win32::HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    void* lpParam
) noexcept {
    std::wstring wCls = lpClassName ? std::wstring(lpClassName, lpClassName + std::strlen(lpClassName)) : L"";
    std::wstring wName = lpWindowName ? std::wstring(lpWindowName, lpWindowName + std::strlen(lpWindowName)) : L"";
    return WindowManager::get().createWindow(dwExStyle, wCls.c_str(), wName.c_str(), dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

inline win32::BOOL DestroyWindow(win32::HWND hWnd) noexcept {
    return WindowManager::get().destroyWindow(hWnd) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL IsWindow(win32::HWND hWnd) noexcept {
    return WindowManager::get().isWindow(hWnd) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL ShowWindow(win32::HWND hWnd, int nCmdShow) noexcept {
    auto win = WindowManager::get().getWindow(hWnd);
    if (!win) return win32::FALSE;

    win32::BOOL prevVisible = win->visible ? win32::TRUE : win32::FALSE;
    if (nCmdShow == SW_HIDE) {
        win->visible = false;
    } else {
        win->visible = true;
        win->minimized = (nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_MINIMIZE);
        WindowManager::get().postMessage(hWnd, WM_PAINT, 0, 0);
    }
    return prevVisible;
}

inline win32::BOOL IsWindowVisible(win32::HWND hWnd) noexcept {
    auto win = WindowManager::get().getWindow(hWnd);
    return (win && win->visible) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL IsIconic(win32::HWND hWnd) noexcept {
    auto win = WindowManager::get().getWindow(hWnd);
    return (win && win->minimized) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL UpdateWindow(win32::HWND hWnd) noexcept {
    auto win = WindowManager::get().getWindow(hWnd);
    if (win && win->dirty) {
        WindowManager::get().sendMessage(hWnd, WM_PAINT, 0, 0);
    }
    return win32::TRUE;
}

inline win32::BOOL InvalidateRect(win32::HWND hWnd, const RECT* lpRect, win32::BOOL bErase) noexcept {
    return WindowManager::get().invalidateRect(hWnd, lpRect, bErase) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL ValidateRect(win32::HWND hWnd, const RECT* lpRect) noexcept {
    return WindowManager::get().validateRect(hWnd, lpRect) ? win32::TRUE : win32::FALSE;
}


inline win32::BOOL GetClientRect(win32::HWND hWnd, RECT* lpRect) noexcept {
    return WindowManager::get().getClientRect(hWnd, lpRect) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL GetWindowRect(win32::HWND hWnd, RECT* lpRect) noexcept {
    return WindowManager::get().getWindowRect(hWnd, lpRect) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL AdjustWindowRectEx(RECT* lpRect, uint32_t /*dwStyle*/, win32::BOOL /*bMenu*/, uint32_t /*dwExStyle*/) noexcept {
    if (!lpRect) return win32::FALSE;
    // Standard standard border padding
    lpRect->left   -= 8;
    lpRect->top    -= 31; // Titlebar + border
    lpRect->right  += 8;
    lpRect->bottom += 8;
    return win32::TRUE;
}

inline win32::BOOL AdjustWindowRect(void* lpRect, uint32_t dwStyle, win32::BOOL bMenu) noexcept {
    if (!lpRect) return win32::FALSE;
    auto* rc = static_cast<RECT*>(lpRect);
    int32_t border = 8;
    int32_t caption = (dwStyle & 0x00C00000) ? 32 : 0; // WS_CAPTION
    int32_t menu = bMenu ? 20 : 0;
    rc->left -= border;
    rc->right += border;
    rc->top -= (border + caption + menu);
    rc->bottom += border;
    return win32::TRUE;
}

inline win32::BOOL CopyRect(void* lprcDst, const void* lprcSrc) noexcept {
    if (!lprcDst || !lprcSrc) return win32::FALSE;
    *static_cast<RECT*>(lprcDst) = *static_cast<const RECT*>(lprcSrc);
    return win32::TRUE;
}

inline win32::BOOL OpenIcon(win32::HWND /*hWnd*/) noexcept {
    return win32::TRUE; // Restored minimized window
}

inline win32::HWND GetNextDlgTabItem(win32::HWND /*hDlg*/, win32::HWND hCtl, win32::BOOL /*bPrevious*/) noexcept {
    return hCtl ? hCtl : reinterpret_cast<win32::HWND>(0x1000);
}

inline win32::BOOL ReplyMessage(LRESULT /*lResult*/) noexcept {
    return win32::TRUE;
}

inline int32_t ScrollWindowEx(win32::HWND /*hWnd*/, int32_t /*dx*/, int32_t /*dy*/, const void* /*prcScroll*/, const void* /*prcClip*/, void* /*hrgnUpdate*/, void* prcUpdate, uint32_t /*flags*/) noexcept {
    if (prcUpdate) {
        *static_cast<RECT*>(prcUpdate) = RECT{0, 0, 800, 600};
    }
    return 2; // SIMPLEREGION
}

inline win32::BOOL SetWindowPos(win32::HWND hWnd, win32::HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) noexcept {
    return WindowManager::get().setWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL MoveWindow(win32::HWND hWnd, int X, int Y, int nWidth, int nHeight, win32::BOOL bRepaint) noexcept {
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    if (!bRepaint) flags |= SWP_NOREDRAW;
    return WindowManager::get().setWindowPos(hWnd, nullptr, X, Y, nWidth, nHeight, flags) ? win32::TRUE : win32::FALSE;
}

inline uintptr_t SetWindowLongPtrW(win32::HWND hWnd, int nIndex, uintptr_t dwNewLong) noexcept {
    return WindowManager::get().setWindowLongPtr(hWnd, nIndex, dwNewLong);
}

inline uintptr_t GetWindowLongPtrW(win32::HWND hWnd, int nIndex) noexcept {
    return WindowManager::get().getWindowLongPtr(hWnd, nIndex);
}

inline int32_t SetWindowLongW(win32::HWND hWnd, int nIndex, int32_t dwNewLong) noexcept {
    return static_cast<int32_t>(WindowManager::get().setWindowLongPtr(hWnd, nIndex, static_cast<uintptr_t>(dwNewLong)));
}

inline int32_t GetWindowLongW(win32::HWND hWnd, int nIndex) noexcept {
    return static_cast<int32_t>(WindowManager::get().getWindowLongPtr(hWnd, nIndex));
}

inline win32::BOOL DrawCaption(win32::HWND /*hwnd*/, HDC /*hdc*/, const void* /*lprect*/, uint32_t /*flags*/) noexcept {
    return win32::TRUE;
}

inline uint32_t GetClassLongW(win32::HWND /*hWnd*/, int /*nIndex*/) noexcept {
    return 0;
}

inline uint32_t SetClassLongW(win32::HWND /*hWnd*/, int /*nIndex*/, int32_t /*dwNewLong*/) noexcept {
    return 0;
}

inline win32::BOOL SendNotifyMessageW(win32::HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept {
    return WindowManager::get().postMessage(hWnd, Msg, wParam, lParam) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL PostMessageW(win32::HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept {
    return WindowManager::get().postMessage(hWnd, Msg, wParam, lParam) ? win32::TRUE : win32::FALSE;
}

inline LRESULT SendMessageW(win32::HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept {
    return WindowManager::get().sendMessage(hWnd, Msg, wParam, lParam);
}

inline win32::BOOL PeekMessageW(MSG* lpMsg, win32::HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) noexcept {
    return WindowManager::get().peekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL PeekMessageA(MSG* lpMsg, win32::HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) noexcept {
    return WindowManager::get().peekMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL GetMessageW(MSG* lpMsg, win32::HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) noexcept {
    return WindowManager::get().getMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL TranslateMessage(const MSG* lpMsg) noexcept {
    if (!lpMsg) return win32::FALSE;
    if (lpMsg->message == WM_KEYDOWN) {
        // Synthesize WM_CHAR message for printable ASCII characters
        if (lpMsg->wParam >= 0x20 && lpMsg->wParam <= 0x7E) {
            WindowManager::get().postMessage(lpMsg->hwnd, WM_CHAR, lpMsg->wParam, lpMsg->lParam);
            return win32::TRUE;
        }
    }
    return win32::FALSE;
}

inline LRESULT DispatchMessageW(const MSG* lpMsg) noexcept {
    return WindowManager::get().dispatchMessage(lpMsg);
}

inline int64_t DispatchMessageA(const MSG* lpMsg) noexcept {
    return static_cast<int64_t>(WindowManager::get().dispatchMessage(lpMsg));
}

inline void PostQuitMessage(int nExitCode) noexcept {
    WindowManager::get().postQuitMessage(nExitCode);
}

struct HotKeyEntry {
    win32::HWND hWnd;
    int32_t id;
    uint32_t fsModifiers;
    uint32_t vk;
};

inline std::mutex g_hotkey_mutex;
inline std::vector<HotKeyEntry> g_registered_hotkeys;

inline win32::BOOL RegisterHotKey(win32::HWND hWnd, int32_t id, uint32_t fsModifiers, uint32_t vk) noexcept {
    std::lock_guard lock(g_hotkey_mutex);
    for (auto& hk : g_registered_hotkeys) {
        if (hk.fsModifiers == fsModifiers && hk.vk == vk) {
            win32::SetLastError(1409); // ERROR_HOTKEY_ALREADY_REGISTERED
            return win32::FALSE;
        }
    }
    g_registered_hotkeys.push_back({hWnd, id, fsModifiers, vk});
    return win32::TRUE;
}

inline win32::BOOL UnregisterHotKey(win32::HWND hWnd, int32_t id) noexcept {
    std::lock_guard lock(g_hotkey_mutex);
    auto it = std::remove_if(g_registered_hotkeys.begin(), g_registered_hotkeys.end(),
        [hWnd, id](const HotKeyEntry& e) {
            return e.hWnd == hWnd && e.id == id;
        });
    if (it != g_registered_hotkeys.end()) {
        g_registered_hotkeys.erase(it, g_registered_hotkeys.end());
        return win32::TRUE;
    }
    return win32::TRUE;
}

inline win32::BOOL PostThreadMessageW(uint32_t /*idThread*/, UINT /*Msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) noexcept {
    return win32::TRUE;
}

inline int64_t SendMessageTimeoutW(win32::HWND /*hWnd*/, UINT /*Msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, uint32_t /*fuFlags*/, uint32_t /*uTimeout*/, void* lpdwResult) noexcept {
    if (lpdwResult) {
        *static_cast<uint64_t*>(lpdwResult) = 0;
    }
    return 1;
}

inline uint32_t MapVirtualKeyExW(uint32_t uCode, uint32_t uMapType, void* /*dwhkl*/) noexcept {
    switch (uMapType) {
        case 0: return uCode & 0xFF; // MAPVK_VK_TO_VSC
        case 1: return uCode & 0xFF; // MAPVK_VSC_TO_VK
        case 2: // MAPVK_VK_TO_CHAR
            if (uCode >= 'A' && uCode <= 'Z') return uCode;
            if (uCode >= '0' && uCode <= '9') return uCode;
            if (uCode == 0x20) return ' ';
            return 0;
        case 3: return uCode & 0xFF; // MAPVK_VSC_TO_VK_EX
        default: return 0;
    }
}

inline LRESULT DefWindowProcW(win32::HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept {
    return WindowManager::get().defWindowProc(hWnd, Msg, wParam, lParam);
}

using GetDCHookFn = HDC (*)(win32::HWND);
inline GetDCHookFn g_pfnGetDCHook = nullptr;

inline void SetGetDCHook(GetDCHookFn fn) noexcept {
    g_pfnGetDCHook = fn;
}

using ReleaseDCHookFn = int (*)(win32::HWND, HDC);
inline ReleaseDCHookFn g_pfnReleaseDCHook = nullptr;

inline void SetReleaseDCHook(ReleaseDCHookFn fn) noexcept {
    g_pfnReleaseDCHook = fn;
}

inline HDC GetDC(win32::HWND hWnd) noexcept {
    if (g_pfnGetDCHook) return g_pfnGetDCHook(hWnd);
    return reinterpret_cast<HDC>(reinterpret_cast<uintptr_t>(hWnd) | 0x1);
}

inline int ReleaseDC(win32::HWND hWnd, HDC hDC) noexcept {
    if (g_pfnReleaseDCHook) return g_pfnReleaseDCHook(hWnd, hDC);
    WindowManager::get().invalidateRect(hWnd, nullptr, win32::FALSE);
    return 1;
}

inline constexpr UINT MB_OK = 0x00000000;
inline constexpr UINT MB_OKCANCEL = 0x00000001;
inline constexpr UINT MB_ABORTRETRYIGNORE = 0x00000002;
inline constexpr UINT MB_YESNOCANCEL = 0x00000003;
inline constexpr UINT MB_YESNO = 0x00000004;
inline constexpr UINT MB_RETRYCANCEL = 0x00000005;

inline constexpr int IDOK = 1;
inline constexpr int IDCANCEL = 2;
inline constexpr int IDABORT = 3;
inline constexpr int IDRETRY = 4;
inline constexpr int IDIGNORE = 5;
inline constexpr int IDYES = 6;
inline constexpr int IDNO = 7;

inline int MessageBoxW(win32::HWND hWnd, const wchar_t* lpText, const wchar_t* lpCaption, UINT uType) noexcept {
    (void)hWnd; (void)uType;
    (void)lpText; (void)lpCaption;
    return IDOK;
}

inline int MessageBoxA(win32::HWND hWnd, const char* lpText, const char* lpCaption, UINT uType) noexcept {
    (void)hWnd; (void)uType;
    (void)lpText; (void)lpCaption;
    return IDOK;
}

inline HDC BeginPaint(win32::HWND hWnd, PAINTSTRUCT* lpPaint) noexcept {
    return WindowManager::get().beginPaint(hWnd, lpPaint);
}

inline win32::BOOL EndPaint(win32::HWND hWnd, const PAINTSTRUCT* lpPaint) noexcept {
    return WindowManager::get().endPaint(hWnd, lpPaint) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL RegisterRawInputDevices(const RAWINPUTDEVICE* pRawInputDevices, UINT uiNumDevices, UINT cbSize) noexcept {
    return WindowManager::get().registerRawInputDevices(pRawInputDevices, uiNumDevices, cbSize) ? win32::TRUE : win32::FALSE;
}

inline UINT GetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, void* pData, UINT* pcbSize, UINT cbSizeHeader) noexcept {
    return WindowManager::get().getRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
}

inline UINT GetRawInputDeviceList(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize) noexcept {
    return WindowManager::get().getRawInputDeviceList(pRawInputDeviceList, puiNumDevices, cbSize);
}

inline UINT GetRawInputDeviceInfoW(win32::HANDLE hDevice, UINT uiCommand, void* pData, PUINT pcbSize) noexcept {
    return WindowManager::get().getRawInputDeviceInfoW(hDevice, uiCommand, pData, pcbSize);
}

inline UINT GetRegisteredRawInputDevices(PRAWINPUTDEVICE pRawInputDevices, PUINT puiNumDevices, UINT cbSize) noexcept {
    return WindowManager::get().getRegisteredRawInputDevices(pRawInputDevices, puiNumDevices, cbSize);
}

inline UINT GetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader) noexcept {
    return WindowManager::get().getRawInputBuffer(pData, pcbSize, cbSizeHeader);
}

// ----------------------------------------------------------------------------
// Desktop & Synchronization Bridge (from existing user32)
// ----------------------------------------------------------------------------

inline uint32_t MsgWaitForMultipleObjects(
    uint32_t nCount,
    const win32::HANDLE* pHandles,
    win32::BOOL fWaitAll,
    uint32_t dwMilliseconds,
    uint32_t /*dwWakeMask*/
) noexcept {
    return win32::WaitForMultipleObjects(nCount, pHandles, fWaitAll, dwMilliseconds);
}

inline win32::BOOL LockWorkStation() noexcept {
    return winlogon::WinlogonManager::get().lockWorkstation() ? win32::TRUE : win32::FALSE;
}

inline win32::HANDLE OpenDesktopW(
    const wchar_t* lpszDesktop,
    uint32_t /*dwFlags*/,
    win32::BOOL /*fInherit*/,
    uint32_t /*dwDesiredAccess*/
) noexcept {
    if (!lpszDesktop) return nullptr;
    if (_wcsicmp(lpszDesktop, L"Winlogon") == 0) {
        return reinterpret_cast<win32::HANDLE>(0x0000000000000010ULL);
    }
    if (_wcsicmp(lpszDesktop, L"Default") == 0) {
        return reinterpret_cast<win32::HANDLE>(0x0000000000000020ULL);
    }
    return nullptr;
}

inline win32::BOOL SwitchDesktop(win32::HANDLE hDesktop) noexcept {
    if (hDesktop == reinterpret_cast<win32::HANDLE>(0x0000000000000010ULL)) {
        winlogon::WinlogonManager::get().switchDesktop(winlogon::DesktopType::Winlogon);
        return win32::TRUE;
    }
    if (hDesktop == reinterpret_cast<win32::HANDLE>(0x0000000000000020ULL)) {
        winlogon::WinlogonManager::get().switchDesktop(winlogon::DesktopType::Default);
        return win32::TRUE;
    }
    return win32::FALSE;
}

inline win32::BOOL CloseDesktop(win32::HANDLE /*hDesktop*/) noexcept {
    return win32::TRUE;
}

inline win32::LPWSTR CharUpperW(win32::LPWSTR lpsz) noexcept {
    if (!lpsz) return nullptr;
    if (reinterpret_cast<uintptr_t>(lpsz) <= 0xFFFF) {
        wchar_t ch = static_cast<wchar_t>(reinterpret_cast<uintptr_t>(lpsz));
        return reinterpret_cast<win32::LPWSTR>(static_cast<uintptr_t>(std::towupper(ch)));
    }
    for (wchar_t* p = lpsz; *p; ++p) {
        *p = std::towupper(*p);
    }
    return lpsz;
}

inline win32::HWND GetDesktopWindow() noexcept {
    return reinterpret_cast<win32::HWND>(static_cast<uintptr_t>(0x10001));
}

inline int32_t GetSystemMetrics(int32_t nIndex) noexcept {
    switch (nIndex) {
        case 0: return 1920; // SM_CXSCREEN
        case 1: return 1080; // SM_CYSCREEN
        case 2: return 0;    // SM_CXVSCROLL
        case 3: return 0;    // SM_CYHSCROLL
        case 4: return 32;   // SM_CYCAPTION
        default: return 0;
    }
}

inline win32::HWND SetFocus(win32::HWND hWnd) noexcept {
    return reinterpret_cast<win32::HWND>(MicaWin32kSetFocus(hWnd));
}

inline win32::HWND GetFocus() noexcept {
    return reinterpret_cast<win32::HWND>(MicaWin32kGetForegroundWindow());
}

inline win32::BOOL EnableWindow(win32::HWND /*hWnd*/, win32::BOOL /*bEnable*/) noexcept {
    return win32::FALSE; // previously enabled
}

inline win32::BOOL IsWindowEnabled(win32::HWND /*hWnd*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL SetWindowTextW(win32::HWND /*hWnd*/, const wchar_t* /*lpString*/) noexcept {
    return win32::TRUE;
}

inline int32_t GetWindowTextW(win32::HWND hWnd, wchar_t* lpString, int32_t nMaxCount) noexcept {
    if (!lpString || nMaxCount <= 0) return 0;
    lpString[0] = L'\0';
    const auto window = WindowManager::get().getWindow(hWnd);
    if (!window) return 0;
    const size_t count = std::min(window->windowName.size(), static_cast<size_t>(nMaxCount - 1));
    if (count) std::memcpy(lpString, window->windowName.data(), count * sizeof(wchar_t));
    lpString[count] = L'\0';
    return static_cast<int32_t>(count);
}

inline int32_t GetWindowTextLengthW(win32::HWND /*hWnd*/) noexcept {
    return 0;
}

inline win32::BOOL AnimateWindow(win32::HWND /*hWnd*/, win32::DWORD /*dwTime*/, win32::DWORD /*dwFlags*/) noexcept {
    return win32::TRUE;
}

// (ChangeDisplaySettingsExW and EnumDisplaySettingsW implemented below in Horizon 203 Display Topology)
inline win32::BOOL DrawStateW(void* /*hdc*/, void* /*hbrFore*/, void* /*qfnCallBack*/, win32::LPARAM /*lData*/, win32::WPARAM /*wData*/, int /*x*/, int /*y*/, int /*cx*/, int /*cy*/, win32::UINT /*uFlags*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL GetProcessDefaultLayout(win32::DWORD* pdwDefaultLayout) noexcept {
    if (pdwDefaultLayout) {
        *pdwDefaultLayout = 0; // LAYOUT_LTR
    }
    return win32::TRUE;
}

inline void* LoadCursorFromFileW(win32::LPCWSTR /*lpFileName*/) noexcept {
    return nullptr;
}

inline win32::BOOL SetCaretBlinkTime(win32::UINT /*uMSeconds*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL ValidateRgn(win32::HWND /*hWnd*/, void* /*hRgn*/) noexcept {
    return win32::TRUE;
}

inline int16_t VkKeyScanW(wchar_t ch) noexcept {
    return static_cast<int16_t>(ch & 0xFF);
}

inline win32::DWORD WaitForInputIdle(win32::HANDLE /*hProcess*/, win32::DWORD /*dwMilliseconds*/) noexcept {
    return 0; // WAIT_OBJECT_0
}

inline void keybd_event(uint8_t /*bVk*/, uint8_t /*bScan*/, win32::DWORD /*dwFlags*/, uintptr_t /*dwExtraInfo*/) noexcept {}

inline void* DdeCreateDataHandle(win32::DWORD /*idInst*/, uint8_t* /*pSrc*/, win32::DWORD /*cb*/, win32::DWORD /*cbOff*/, void* /*hszItem*/, win32::UINT /*wFmt*/, win32::UINT /*afCmd*/) noexcept {
    return reinterpret_cast<void*>(0xDDED0001);
}

inline win32::DWORD DdeGetData(void* /*hData*/, uint8_t* /*pDst*/, win32::DWORD /*cbMax*/, win32::DWORD /*cbOff*/) noexcept {
    return 0;
}

inline win32::UINT DdeGetLastError(win32::DWORD /*idInst*/) noexcept {
    return 0; // DMLERR_NO_ERROR
}

inline void* DdeNameService(win32::DWORD /*idInst*/, void* /*hsz1*/, void* /*hsz2*/, win32::UINT /*afCmd*/) noexcept {
    return reinterpret_cast<void*>(1);
}

inline win32::BOOL DdePostAdvise(win32::DWORD /*idInst*/, void* /*hszTopic*/, void* /*hszItem*/) noexcept {
    return win32::TRUE;
}

inline win32::DWORD DdeQueryStringW(win32::DWORD /*idInst*/, void* /*hsz*/, win32::LPWSTR psz, win32::DWORD cchMax, int /*iCodePage*/) noexcept {
    if (psz && cchMax > 0) psz[0] = L'\0';
    return 0;
}

inline uint32_t DdeInitializeW(uint32_t* pidInst, [[maybe_unused]] void* pfnCallback, [[maybe_unused]] uint32_t afCmd, [[maybe_unused]] uint32_t ulRes) noexcept {
    if (!pidInst) return 0x4002; // DMLERR_INVALIDPARAMETER
    static uint32_t s_ddeInst = 0x5000;
    *pidInst = ++s_ddeInst;
    return 0; // DMLERR_NO_ERROR
}

inline win32::BOOL DdeUninitialize([[maybe_unused]] uint32_t idInst) noexcept {
    return 1;
}

inline void* DdeCreateStringHandleW([[maybe_unused]] uint32_t idInst, [[maybe_unused]] const wchar_t* psz, [[maybe_unused]] int iCodePage) noexcept {
    static uintptr_t s_hsz = 0x6000;
    return reinterpret_cast<void*>(++s_hsz);
}

inline win32::BOOL DdeFreeStringHandle([[maybe_unused]] uint32_t idInst, [[maybe_unused]] void* hsz) noexcept {
    return 1;
}

inline void* DdeConnect([[maybe_unused]] uint32_t idInst, [[maybe_unused]] void* hszService, [[maybe_unused]] void* hszTopic, [[maybe_unused]] void* pCC) noexcept {
    static uintptr_t s_hconv = 0x7000;
    return reinterpret_cast<void*>(++s_hconv);
}

inline win32::BOOL DdeDisconnect([[maybe_unused]] void* hConv) noexcept {
    return 1;
}

inline void* DdeClientTransaction([[maybe_unused]] void* pData, [[maybe_unused]] uint32_t cbData, [[maybe_unused]] void* hConv, [[maybe_unused]] void* hszItem, [[maybe_unused]] uint32_t wFmt, [[maybe_unused]] uint32_t wType, [[maybe_unused]] uint32_t wTimeout, uint32_t* pdwResult) noexcept {
    if (pdwResult) *pdwResult = 1;
    static uintptr_t s_hdata = 0x8000;
    return reinterpret_cast<void*>(++s_hdata);
}

inline win32::BOOL DdeFreeDataHandle([[maybe_unused]] void* hData) noexcept {
    return 1;
}

inline int64_t PackDDElParam([[maybe_unused]] uint32_t msg, intptr_t pLo, intptr_t pHi) noexcept {
    return static_cast<int64_t>((pHi << 32) | (pLo & 0xFFFFFFFF));
}

inline uintptr_t GetClassLongPtrA([[maybe_unused]] win32::HWND hWnd, [[maybe_unused]] int nIndex) noexcept {
    return 0;
}

inline win32::UINT GetRawInputDeviceInfoA([[maybe_unused]] win32::HANDLE hDevice,
                                          [[maybe_unused]] win32::UINT uiCommand,
                                          [[maybe_unused]] win32::LPVOID pData,
                                          win32::UINT* pcbSize) noexcept {
    if (pcbSize) *pcbSize = 0;
    return 0;
}

inline win32::BOOL GetWindowDisplayAffinity([[maybe_unused]] win32::HWND hWnd, win32::DWORD* pdwAffinity) noexcept {
    if (pdwAffinity) *pdwAffinity = 0;
    return win32::TRUE;
}

inline int32_t GetWindowLongA(win32::HWND hWnd, int nIndex) noexcept {
    return GetWindowLongW(hWnd, nIndex);
}

inline win32::UINT MapVirtualKeyA(win32::UINT uCode, [[maybe_unused]] win32::UINT uMapType) noexcept {
    return uCode;
}

inline win32::BOOL SetProcessDPIAware() noexcept {
    return win32::TRUE;
}

inline int ToUnicodeEx(win32::UINT wVirtKey, [[maybe_unused]] win32::UINT wScanCode,
                       [[maybe_unused]] const uint8_t* lpKeyState, win32::LPWSTR pwszBuff,
                       int cchBuff, [[maybe_unused]] win32::UINT wFlags, [[maybe_unused]] void* dwhkl) noexcept {
    if (pwszBuff && cchBuff > 0 && wVirtKey >= 32 && wVirtKey <= 126) {
        pwszBuff[0] = static_cast<wchar_t>(wVirtKey);
        return 1;
    }
    return 0;
}

inline int16_t VkKeyScanExA(char ch, [[maybe_unused]] void* dwhkl) noexcept {
    return static_cast<int16_t>(static_cast<uint8_t>(ch));
}

inline win32::BOOL MapDialogRect([[maybe_unused]] win32::HWND hDlg, [[maybe_unused]] void* lpRect) noexcept {
    return win32::TRUE;
}

inline win32::BOOL GetMonitorInfoA([[maybe_unused]] void* hMonitor, void* lpmi) noexcept {
    if (lpmi) {
        struct MONITORINFOA { uint32_t cbSize; int32_t rcMonitor[4]; int32_t rcWork[4]; uint32_t dwFlags; };
        auto* mi = reinterpret_cast<MONITORINFOA*>(lpmi);
        mi->rcMonitor[0] = 0; mi->rcMonitor[1] = 0; mi->rcMonitor[2] = 1280; mi->rcMonitor[3] = 800;
        mi->rcWork[0] = 0; mi->rcWork[1] = 0; mi->rcWork[2] = 1280; mi->rcWork[3] = 760;
        mi->dwFlags = 1; // MONITORINFOF_PRIMARY
    }
    return win32::TRUE;
}

inline int32_t GetDialogBaseUnits() noexcept {
    return (16 << 16) | 8;
}

inline win32::BOOL CheckRadioButton([[maybe_unused]] win32::HWND hDlg, [[maybe_unused]] int nIDFirstButton, [[maybe_unused]] int nIDLastButton, [[maybe_unused]] int nIDCheckButton) noexcept {
    return win32::TRUE;
}

inline uint32_t IsDlgButtonChecked([[maybe_unused]] win32::HWND hDlg, [[maybe_unused]] int nIDButton) noexcept {
    return 0; // BST_UNCHECKED
}

inline win32::BOOL CheckDlgButton([[maybe_unused]] win32::HWND hDlg, [[maybe_unused]] int nIDButton, [[maybe_unused]] uint32_t uCheck) noexcept {
    return win32::TRUE;
}

inline void* LoadAcceleratorsW([[maybe_unused]] void* hInstance, [[maybe_unused]] const wchar_t* lpTableName) noexcept {
    return reinterpret_cast<void*>(0x5501);
}

inline win32::BOOL GetClassInfoW(void* hInstance, const wchar_t* lpClassName, void* lpWndClass) noexcept {
    if (!lpClassName || !lpWndClass) return win32::FALSE;
    WNDCLASSEXW wcx{};
    if (WindowManager::get().getClassInfo(lpClassName, &wcx)) {
        struct WNDCLASS_BASIC {
            uint32_t style; WNDPROC lpfnWndProc; int cbClsExtra; int cbWndExtra;
            void* hInstance; void* hIcon; void* hCursor; void* hbrBackground;
            const wchar_t* lpszMenuName; const wchar_t* lpszClassName;
        };
        auto* wc = reinterpret_cast<WNDCLASS_BASIC*>(lpWndClass);
        wc->style = wcx.style;
        wc->lpfnWndProc = wcx.lpfnWndProc;
        wc->cbClsExtra = wcx.cbClsExtra;
        wc->cbWndExtra = wcx.cbWndExtra;
        wc->hInstance = hInstance;
        wc->hIcon = wcx.hIcon;
        wc->hCursor = wcx.hCursor;
        wc->hbrBackground = wcx.hbrBackground;
        wc->lpszMenuName = wcx.lpszMenuName;
        wc->lpszClassName = wcx.lpszClassName;
        return win32::TRUE;
    }
    return win32::FALSE;
}

inline win32::HWND WindowFromDC([[maybe_unused]] void* hdc) noexcept {
    return reinterpret_cast<win32::HWND>(0x9001);
}

inline win32::BOOL IsCharUpperW(wchar_t ch) noexcept {
    return (ch >= L'A' && ch <= L'Z') ? 1 : 0;
}

inline win32::BOOL ShowWindowAsync([[maybe_unused]] win32::HWND hWnd, [[maybe_unused]] int nCmdShow) noexcept {
    return 1;
}

inline win32::BOOL SetMenuInfo([[maybe_unused]] void* hmenu, [[maybe_unused]] const void* lpcmi) noexcept { return 1; }
inline win32::BOOL GetMenuInfo([[maybe_unused]] void* hmenu, [[maybe_unused]] void* lpcmi) noexcept { return 1; }
inline win32::BOOL SetMenuDefaultItem([[maybe_unused]] void* hMenu, [[maybe_unused]] uint32_t uItem, [[maybe_unused]] uint32_t fByPos) noexcept { return 1; }

inline int16_t VkKeyScanExW(wchar_t ch, [[maybe_unused]] void* dwhkl) noexcept {
    if (ch >= L'A' && ch <= L'Z') return static_cast<int16_t>((0x01 << 8) | (ch - L'A' + 0x41));
    if (ch >= L'a' && ch <= L'z') return static_cast<int16_t>(ch - L'a' + 0x41);
    if (ch >= L'0' && ch <= L'9') return static_cast<int16_t>(ch - L'0' + 0x30);
    return 0;
}

inline uint32_t SendInput(uint32_t cInputs, [[maybe_unused]] void* pInputs, [[maybe_unused]] int cbSize) noexcept {
    return cInputs;
}

inline win32::BOOL GetWindowInfo([[maybe_unused]] win32::HWND hwnd, void* pwi) noexcept {
    if (!pwi) return 0;
    struct WINDOWINFO_MOCK {
        uint32_t cbSize;
        int32_t rcWindow[4];
        int32_t rcClient[4];
        uint32_t dwStyle;
        uint32_t dwExStyle;
        uint32_t dwWindowStatus;
        uint32_t cxWindowBorders;
        uint32_t cyWindowBorders;
        uint16_t atomWindowType;
        uint16_t wCreatorVersion;
    };
    auto* wi = reinterpret_cast<WINDOWINFO_MOCK*>(pwi);
    wi->cbSize = sizeof(WINDOWINFO_MOCK);
    wi->rcWindow[0] = 100; wi->rcWindow[1] = 100; wi->rcWindow[2] = 900; wi->rcWindow[3] = 700;
    wi->rcClient[0] = 100; wi->rcClient[1] = 130; wi->rcClient[2] = 900; wi->rcClient[3] = 700;
    wi->dwStyle = 0x14CF0000; // WS_OVERLAPPEDWINDOW | WS_VISIBLE
    wi->dwExStyle = 0;
    wi->dwWindowStatus = 1; // WS_ACTIVECAPTION
    wi->cxWindowBorders = 1;
    wi->cyWindowBorders = 1;
    return 1;
}

inline win32::BOOL CharToOemA(const char* lpszSrc, char* lpszDst) noexcept {
    if (!lpszSrc || !lpszDst) return 0;
    std::strcpy(lpszDst, lpszSrc);
    return 1;
}

inline win32::BOOL OemToCharBuffA(const char* lpszSrc, char* lpszDst, uint32_t cchDstLength) noexcept {
    if (!lpszSrc || !lpszDst || cchDstLength == 0) return 0;
    std::strncpy(lpszDst, lpszSrc, cchDstLength);
    return 1;
}

inline win32::BOOL OemToCharA(const char* lpszSrc, char* lpszDst) noexcept {
    if (!lpszSrc || !lpszDst) return 0;
    std::strcpy(lpszDst, lpszSrc);
    return 1;
}

inline std::unordered_map<win32::HWND, std::string>& GetWindowTitleMap() {
    static std::unordered_map<win32::HWND, std::string> s_titles;
    return s_titles;
}

inline std::mutex& GetWindowTitleMutex() {
    static std::mutex s_mtx;
    return s_mtx;
}

inline win32::HWND CreateDialogParamA(HINSTANCE /*hInstance*/, const char* /*lpTemplateName*/, win32::HWND /*hWndParent*/, void* /*lpDialogFunc*/, LPARAM /*dwInitParam*/) noexcept {
    static uintptr_t s_dlg = 0x9000;
    return reinterpret_cast<win32::HWND>(++s_dlg);
}

inline LRESULT DefDlgProcA(win32::HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

inline LRESULT DefWindowProcA(win32::HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

inline int DialogBoxParamA(HINSTANCE /*hInstance*/, const char* /*lpTemplateName*/, win32::HWND /*hWndParent*/, void* /*lpDialogFunc*/, LPARAM /*dwInitParam*/) noexcept {
    return 1; // IDOK
}

inline win32::HWND FindWindowA(const char* /*lpClassName*/, const char* /*lpWindowName*/) noexcept {
    return reinterpret_cast<win32::HWND>(0x9101);
}

inline win32::BOOL FlashWindow(win32::HWND /*hWnd*/, win32::BOOL /*bInvert*/) noexcept { return win32::TRUE; }

inline win32::HWND GetClipboardOwner() noexcept { return nullptr; }

inline win32::BOOL GetMessageA(void* lpMsg, win32::HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) noexcept {
    return GetMessageW(reinterpret_cast<MSG*>(lpMsg), hWnd, wMsgFilterMin, wMsgFilterMax);
}

inline uint32_t GetQueueStatus(UINT /*flags*/) noexcept { return 0; }

inline intptr_t GetWindowLongPtrA(win32::HWND hWnd, int nIndex) noexcept {
    return static_cast<intptr_t>(GetWindowLongPtrW(hWnd, nIndex));
}

inline int GetWindowTextLengthA(win32::HWND hWnd) noexcept {
    std::lock_guard<std::mutex> lock(GetWindowTitleMutex());
    auto& map = GetWindowTitleMap();
    auto it = map.find(hWnd);
    return (it != map.end()) ? static_cast<int>(it->second.size()) : 0;
}

inline int GetWindowTextA(win32::HWND hWnd, char* lpString, int nMaxCount) noexcept {
    if (!lpString || nMaxCount <= 0) return 0;
    std::lock_guard<std::mutex> lock(GetWindowTitleMutex());
    auto& map = GetWindowTitleMap();
    auto it = map.find(hWnd);
    if (it != map.end()) {
        int len = std::min<int>(static_cast<int>(it->second.size()), nMaxCount - 1);
        std::memcpy(lpString, it->second.c_str(), len);
        lpString[len] = '\0';
        return len;
    }
    lpString[0] = '\0';
    return 0;
}

inline win32::BOOL InsertMenuA(HMENU /*hMenu*/, UINT /*uPosition*/, UINT /*uFlags*/, uintptr_t /*uIDNewItem*/, const char* /*lpNewItem*/) noexcept {
    return win32::TRUE;
}

inline HCURSOR LoadCursorA(HINSTANCE /*hInstance*/, const char* /*lpCursorName*/) noexcept {
    return nullptr;
}

inline HICON LoadIconA(HINSTANCE /*hInstance*/, const char* /*lpIconName*/) noexcept {
    return reinterpret_cast<HICON>(0x20001);
}

inline win32::HANDLE LoadImageA(HINSTANCE /*hInst*/, const char* /*name*/, UINT /*type*/, int /*cx*/, int /*cy*/, UINT /*fuLoad*/) noexcept {
    return nullptr;
}

inline int MessageBoxIndirectW(const void* /*lpmbp*/) noexcept {
    return 1; // IDOK
}

inline win32::BOOL PostMessageA(win32::HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept {
    return PostMessageW(hWnd, Msg, wParam, lParam);
}

inline uint16_t RegisterClassA(const WNDCLASSA* lpWndClass) noexcept {
    if (!lpWndClass) return 0;
    return 0x8001;
}

inline UINT RegisterClipboardFormatA(const char* /*lpszFormat*/) noexcept {
    static UINT s_cf = 0xC000;
    return ++s_cf;
}

inline UINT RegisterWindowMessageA(const char* /*lpString*/) noexcept {
    static UINT s_wm = 0xC050;
    return ++s_wm;
}

inline LRESULT SendDlgItemMessageA(win32::HWND /*hDlg*/, int /*nIDDlgItem*/, UINT /*Msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) noexcept {
    return 0;
}

inline uintptr_t SetClassLongPtrA(win32::HWND /*hWnd*/, int /*nIndex*/, intptr_t dwNewLong) noexcept {
    return static_cast<uintptr_t>(dwNewLong);
}

inline intptr_t SetWindowLongPtrA(win32::HWND hWnd, int nIndex, intptr_t dwNewLong) noexcept {
    return static_cast<intptr_t>(SetWindowLongPtrW(hWnd, nIndex, static_cast<uintptr_t>(dwNewLong)));
}

inline win32::BOOL SetWindowTextA(win32::HWND hWnd, const char* lpString) noexcept {
    std::lock_guard<std::mutex> lock(GetWindowTitleMutex());
    if (lpString) {
        GetWindowTitleMap()[hWnd] = lpString;
    }
    return win32::TRUE;
}

inline int ToAsciiEx(UINT uVirtKey, UINT /*uScanCode*/, const uint8_t* lpKeyState, uint16_t* lpChar, UINT /*uFlags*/, void* /*dwhkl*/) noexcept {
    if (!lpChar) return 0;
    bool isShift = (lpKeyState && (lpKeyState[0x10] & 0x80));
    if (uVirtKey >= 'A' && uVirtKey <= 'Z') {
        *lpChar = static_cast<uint16_t>(isShift ? uVirtKey : (uVirtKey + 32));
        return 1;
    }
    if (uVirtKey >= '0' && uVirtKey <= '9') {
        *lpChar = static_cast<uint16_t>(uVirtKey);
        return 1;
    }
    if (uVirtKey == 0x20) { // VK_SPACE
        *lpChar = ' ';
        return 1;
    }
    if (uVirtKey == 0x0D) { // VK_RETURN
        *lpChar = '\r';
        return 1;
    }
    return 0;
}

using HWINEVENTHOOK = void*;

inline win32::BOOL WINAPI ChangeWindowMessageFilterEx(
    HWND /*hwnd*/,
    uint32_t /*message*/,
    DWORD /*action*/,
    void* /*pChangeFilterStruct*/
) noexcept {
    return win32::TRUE;
}

inline char* WINAPI CharLowerA(char* lpsz) noexcept {
    if (lpsz) {
        for (char* p = lpsz; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = static_cast<char>(*p + ('a' - 'A'));
        }
    }
    return lpsz;
}

inline char* WINAPI CharUpperA(char* lpsz) noexcept {
    if (lpsz) {
        for (char* p = lpsz; *p; ++p) {
            if (*p >= 'a' && *p <= 'z') *p = static_cast<char>(*p - ('a' - 'A'));
        }
    }
    return lpsz;
}

inline HICON WINAPI CreateIconFromResourceEx(
    BYTE* /*pbIconBits*/,
    DWORD /*cbIconBits*/,
    BOOL /*fIcon*/,
    DWORD /*dwVersion*/,
    int /*cxDesired*/,
    int /*cyDesired*/,
    uint32_t /*uFlags*/
) noexcept {
    return reinterpret_cast<HICON>(0x6001);
}

inline int WINAPI DrawTextExA(
    HDC /*hdc*/,
    char* /*lpchText*/,
    int /*cchText*/,
    void* /*lprc*/,
    uint32_t /*format*/,
    void* /*lpdtp*/
) noexcept {
    return 16;
}

inline BOOL WINAPI GetKeyboardLayoutNameA(char* pwszKLID) noexcept {
    if (pwszKLID) {
        std::snprintf(pwszKLID, 9, "00000409");
        return win32::TRUE;
    }
    return win32::FALSE;
}

inline int WINAPI MessageBoxExW(
    HWND /*hWnd*/,
    const wchar_t* /*lpText*/,
    const wchar_t* /*lpCaption*/,
    uint32_t /*uType*/,
    WORD /*wLanguageId*/
) noexcept {
    return 1; // IDOK
}

inline BOOL WINAPI SetProcessDefaultLayout(DWORD /*dwDefaultLayout*/) noexcept {
    return win32::TRUE;
}

inline HWINEVENTHOOK WINAPI SetWinEventHook(
    DWORD /*eventMin*/,
    DWORD /*eventMax*/,
    void* /*hmodWinEventProc*/,
    void* /*pfnWinEventProc*/,
    DWORD /*idProcess*/,
    DWORD /*idThread*/,
    DWORD /*dwFlags*/
) noexcept {
    return reinterpret_cast<HWINEVENTHOOK>(0x7001);
}

inline BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK /*hWinEventHook*/) noexcept {
    return win32::TRUE;
}

inline int32_t WINAPI CopyAcceleratorTableW(void* /*hAccelSrc*/, ACCEL* lpAccelDst, int32_t cAccelEntries) noexcept {
    if (!lpAccelDst || cAccelEntries <= 0) {
        return 4; // 4 standard accelerators (Ctrl+C, Ctrl+V, Ctrl+Z, F5)
    }
    lpAccelDst[0] = {0x08 /* FCONTROL */, 0x43 /* 'C' */, 101};
    lpAccelDst[1] = {0x08, 0x56 /* 'V' */, 102};
    lpAccelDst[2] = {0x08, 0x5A /* 'Z' */, 103};
    lpAccelDst[3] = {0x01 /* FVIRTKEY */, 0x74 /* VK_F5 */, 104};
    return 4;
}

inline HWND WINAPI RealChildWindowFromPoint(HWND hwndParent, POINT /*pt*/) noexcept {
    return hwndParent;
}

inline win32::BOOL WINAPI UnionRect(RECT* lprcDst, const RECT* lprcSrc1, const RECT* lprcSrc2) noexcept {
    if (!lprcDst || !lprcSrc1 || !lprcSrc2) {
        return win32::FALSE;
    }
    lprcDst->left = std::min(lprcSrc1->left, lprcSrc2->left);
    lprcDst->top = std::min(lprcSrc1->top, lprcSrc2->top);
    lprcDst->right = std::max(lprcSrc1->right, lprcSrc2->right);
    lprcDst->bottom = std::max(lprcSrc1->bottom, lprcSrc2->bottom);
    return win32::TRUE;
}

inline int32_t WINAPI GetTabbedTextExtentW(HDC /*hdc*/, const wchar_t* lpString, int32_t chCount, int32_t /*nTabPositions*/, const int32_t* /*lpnTabStopPositions*/) noexcept {
    int32_t len = (chCount >= 0) ? chCount : (lpString ? static_cast<int32_t>(std::wcslen(lpString)) : 0);
    int32_t width = len * 8;
    int32_t height = 16;
    return (height << 16) | (width & 0xFFFF);
}

inline int64_t WINAPI ReuseDDElParam(int64_t lParam, uint32_t /*msgIn*/, uint32_t /*msgOut*/, uintptr_t /*uiLo*/, uintptr_t /*uiHi*/) noexcept {
    return lParam;
}

inline win32::BOOL WINAPI UnpackDDElParam(uint32_t /*msg*/, int64_t lParam, uintptr_t* puiLo, uintptr_t* puiHi) noexcept {
    if (puiLo) *puiLo = static_cast<uintptr_t>(lParam & 0xFFFFFFFF);
    if (puiHi) *puiHi = static_cast<uintptr_t>((lParam >> 32) & 0xFFFFFFFF);
    return win32::TRUE;
}

inline win32::BOOL WINAPI WinHelpW(HWND /*hWndMain*/, const wchar_t* /*lpszHelp*/, uint32_t /*uCommand*/, uintptr_t /*dwData*/) noexcept {
    return win32::TRUE;
}

inline uint32_t WINAPI GetMenuCheckMarkDimensions() noexcept {
    return (16 << 16) | 16; // 16x16 check mark
}

inline HWND WINAPI ChildWindowFromPoint(HWND hWndParent, POINT /*Point*/) noexcept {
    return hWndParent;
}

inline void* WINAPI GetThreadDesktop(uint32_t /*dwThreadId*/) noexcept {
    return reinterpret_cast<void*>(0xDE500001);
}

inline win32::BOOL WINAPI GetUserObjectInformationW(void* /*hObj*/, int32_t /*nIndex*/, void* pvInfo, uint32_t nLength, uint32_t* lpnLengthNeeded) noexcept {
    if (lpnLengthNeeded) {
        *lpnLengthNeeded = sizeof(uint32_t);
    }
    if (pvInfo && nLength >= sizeof(uint32_t)) {
        *static_cast<uint32_t*>(pvInfo) = 0x01; // Desktop / WS flags
    }
    return win32::TRUE;
}

inline win32::BOOL WINAPI DragDetect(HWND /*hwnd*/, POINT /*pt*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI IsMenu(HMENU hMenu) noexcept {
    return hMenu != nullptr ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL WINAPI GrayStringW(HDC hdc, HBRUSH /*hbr*/, GRAYSTRINGPROC lpOutputFunc, int64_t lpData, int32_t nCount, int32_t /*X*/, int32_t /*Y*/, int32_t /*nWidth*/, int32_t /*nHeight*/) noexcept {
    if (lpOutputFunc) {
        return lpOutputFunc(hdc, lpData, nCount);
    }
    return win32::TRUE;
}

inline int32_t WINAPI TabbedTextOutW(HDC /*hdc*/, int32_t /*X*/, int32_t /*Y*/, const wchar_t* lpString, int32_t chCount, int32_t /*nTabPositions*/, const int32_t* /*lpnTabStopPositions*/, int32_t /*nTabOrigin*/) noexcept {
    int32_t len = (chCount >= 0) ? chCount : (lpString ? static_cast<int32_t>(std::wcslen(lpString)) : 0);
    int32_t width = len * 8;
    int32_t height = 16;
    return (height << 16) | (width & 0xFFFF);
}

inline int32_t wsprintfA(char* lpOut, const char* lpFmt, ...) noexcept {
    if (!lpOut || !lpFmt) return 0;
    std::va_list args;
    va_start(args, lpFmt);
    int res = std::vsnprintf(lpOut, 1024, lpFmt, args);
    va_end(args);
    return res > 0 ? res : 0;
}

inline const wchar_t* WINAPI CharPrevW(const wchar_t* lpszStart, const wchar_t* lpszCurrent) noexcept {
    if (!lpszStart || !lpszCurrent || lpszCurrent <= lpszStart) {
        return lpszStart;
    }
    return lpszCurrent - 1;
}

inline win32::BOOL WINAPI GetCaretPos(POINT* lpPoint) noexcept {
    if (lpPoint) {
        lpPoint->x = 0;
        lpPoint->y = 0;
    }
    return win32::TRUE;
}

inline win32::BOOL WINAPI SetProcessDpiAwarenessContext(void* /*value*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI IsValidDpiAwarenessContext(void* /*value*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI EnableNonClientDpiScaling(win32::HWND /*hwnd*/) noexcept {
    return win32::TRUE;
}

inline int32_t WINAPI GetDisplayConfigBufferSizes(uint32_t /*flags*/, uint32_t* numPathArrayElements, uint32_t* numModeInfoArrayElements) noexcept {
    if (numPathArrayElements) *numPathArrayElements = 1;
    if (numModeInfoArrayElements) *numModeInfoArrayElements = 1;
    return 0; // ERROR_SUCCESS
}

inline int32_t WINAPI QueryDisplayConfig(uint32_t /*flags*/, uint32_t* numPathArrayElements, void* /*pathArray*/, uint32_t* numModeInfoArrayElements, void* /*modeInfoArray*/, void* /*currentTopologyId*/) noexcept {
    if (numPathArrayElements) *numPathArrayElements = 1;
    if (numModeInfoArrayElements) *numModeInfoArrayElements = 1;
    return 0; // ERROR_SUCCESS
}

inline int32_t WINAPI DisplayConfigGetDeviceInfo(void* requestPacket) noexcept {
    if (requestPacket) std::memset(requestPacket, 0, 32);
    return 0; // ERROR_SUCCESS
}

inline win32::BOOL WINAPI UpdateLayeredWindow(win32::HWND /*hWnd*/, HDC /*hdcDst*/, void* /*pptDst*/, void* /*psize*/, HDC /*hdcSrc*/, void* /*pptSrc*/, uint32_t /*crKey*/, void* /*pblend*/, uint32_t /*dwFlags*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI UpdateLayeredWindowIndirect(win32::HWND /*hWnd*/, const void* /*pULWInfo*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI RegisterTouchWindow(win32::HWND /*hwnd*/, uint32_t /*ulFlags*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI UnregisterTouchWindow(win32::HWND /*hwnd*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI IsTouchWindow(win32::HWND /*hwnd*/, uint32_t* /*pulFlags*/) noexcept {
    return win32::FALSE;
}

inline win32::BOOL WINAPI GetPointerFrameTouchInfoHistory(uint32_t /*pointerId*/, uint32_t* entriesCount, uint32_t* pointerCount, void* /*touchInfo*/) noexcept {
    if (entriesCount) *entriesCount = 0;
    if (pointerCount) *pointerCount = 0;
    return win32::TRUE;
}

inline win32::BOOL WINAPI SkipPointerFrameMessages(uint32_t /*pointerId*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI ChangeWindowMessageFilter(uint32_t /*message*/, uint32_t /*dwFlag*/) noexcept {
    return win32::TRUE;
}

inline void* WINAPI RegisterPowerSettingNotification(void* /*hRecipient*/, const void* /*PowerSettingGuid*/, uint32_t /*Flags*/) noexcept {
    return reinterpret_cast<void*>(0x7001);
}

inline win32::BOOL WINAPI UnregisterPowerSettingNotification(void* /*Handle*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI UnregisterDeviceNotification(void* /*Handle*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI ShutdownBlockReasonCreate(win32::HWND /*hWnd*/, const wchar_t* /*pwszReason*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI ShutdownBlockReasonDestroy(win32::HWND /*hWnd*/) noexcept {
    return win32::TRUE;
}

inline HCURSOR WINAPI CreateCursor(void* /*hInst*/, int /*xHotSpot*/, int /*yHotSpot*/, int /*nWidth*/, int /*nHeight*/, const void* /*pvANDPlane*/, const void* /*pvXORPlane*/) noexcept {
    return nullptr;
}

inline int WINAPI ToUnicode(uint32_t wVirtKey, uint32_t /*wScanCode*/, const uint8_t* /*lpKeyState*/, wchar_t* pwszBuff, int cchBuff, uint32_t /*wFlags*/) noexcept {
    if (pwszBuff && cchBuff > 0) {
        pwszBuff[0] = static_cast<wchar_t>(wVirtKey);
        return 1;
    }
    return 0;
}

inline win32::BOOL WINAPI HiliteMenuItem(win32::HWND /*hWnd*/, HMENU /*hMenu*/, uint32_t /*uIDHiliteItem*/, uint32_t /*uHilite*/) noexcept {
    return win32::TRUE;
}

inline char* WINAPI CharPrevExA(uint16_t /*CodePage*/, const char* /*lpStart*/, const char* lpCurrentChar, win32::DWORD /*dwFlags*/) noexcept {
    return const_cast<char*>(lpCurrentChar > (const char*)1 ? lpCurrentChar - 1 : lpCurrentChar);
}

inline uintptr_t WINAPI SetCoalescableTimer(win32::HWND /*hWnd*/, uintptr_t nIDEvent, uint32_t /*uElapse*/, void* /*lpTimerFunc*/, uint32_t /*uToleranceDelay*/) noexcept {
    return nIDEvent ? nIDEvent : 1;
}

inline void* WINAPI OpenWindowStationW(const wchar_t* /*lpszWinSta*/, win32::BOOL /*fInherit*/, win32::DWORD /*dwDesiredAccess*/) noexcept {
    return reinterpret_cast<void*>(0x8401);
}

inline win32::BOOL WINAPI CloseWindowStation(void* /*hWinSta*/) noexcept {
    return win32::TRUE;
}

inline void* WINAPI GetProcessWindowStation() noexcept {
    return reinterpret_cast<void*>(0x8401);
}

inline win32::BOOL WINAPI EnumDesktopsW(void* /*hwinsta*/, void* /*lpEnumFunc*/, intptr_t /*lParam*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI ExitWindowsEx(uint32_t /*uFlags*/, uint32_t /*dwReason*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI GetGUIThreadInfo(win32::DWORD /*idThread*/, void* /*pgui*/) noexcept {
    return win32::TRUE;
}

inline win32::DWORD WINAPI GetGuiResources(win32::HANDLE /*hProcess*/, win32::DWORD /*uiFlags*/) noexcept {
    return 10;
}

inline win32::HWND WINAPI GetShellWindow() noexcept {
    return reinterpret_cast<win32::HWND>(0x8402);
}

inline int WINAPI InternalGetWindowText(win32::HWND /*hWnd*/, wchar_t* pString, int cchMaxCount) noexcept {
    if (pString && cchMaxCount > 0) pString[0] = L'\0';
    return 0;
}

inline win32::BOOL WINAPI InvertRect(HDC /*hDC*/, const void* /*lprc*/) noexcept {
    return win32::TRUE;
}

inline win32::BOOL WINAPI IsHungAppWindow(win32::HWND /*hWnd*/) noexcept {
    return win32::FALSE;
}

inline HMENU WINAPI LoadMenuIndirectW(const void* /*lpMenuTemplate*/) noexcept {
    return reinterpret_cast<HMENU>(0x8403);
}

inline int WINAPI LookupIconIdFromDirectoryEx(uint8_t* /*presbits*/, win32::BOOL /*fIcon*/, int /*cxDesired*/, int /*cyDesired*/, uint32_t /*Flags*/) noexcept {
    return 1;
}

inline int WINAPI MenuItemFromPoint(win32::HWND /*hWnd*/, HMENU /*hMenu*/, int /*x*/, int /*y*/) noexcept {
    return -1;
}

inline win32::BOOL WINAPI SetWindowDisplayAffinity(win32::HWND /*hWnd*/, win32::DWORD /*dwAffinity*/) noexcept {
    return win32::TRUE;
}

inline void* CopyImage(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL DefFrameProcW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL CharToOemBuffW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL ClipCursor(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 0; }
inline BOOL SendMessageA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam) noexcept {
    if (!lpEnumFunc) return win32::FALSE;
    const auto windows = WindowManager::get().enumerateTopLevelWindows();
    for (const HWND hwnd : windows) {
        if (lpEnumFunc(hwnd, lParam) == win32::FALSE) return win32::FALSE;
    }
    return win32::TRUE;
}
inline BOOL ShowOwnedPopups(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetActiveWindow(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetKeyboardLayoutList(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetScrollBarInfo(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL CharLowerBuffW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL InvalidateRgn(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetAsyncKeyState(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void EndMenu(void* /*a*/ = nullptr, int32_t /*b*/ = 0) noexcept {}
inline BOOL CharNextW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL AttachThreadInput(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetTopWindow(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetWindowRgn(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL CharLowerBuffA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL EnumClipboardFormats(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL ScrollDC(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetMessageExtraInfo(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL EnableScrollBar(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetMessagePos(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetKeyNameTextW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetCursorPos(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 0; }
inline BOOL SetRect(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL IsRectEmpty(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr) noexcept { return 1; }
inline void* GetCursor(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return nullptr; }
inline BOOL WaitMessage(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL TranslateMDISysAccel(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetClipboardFormatNameW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetMenuItemRect(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL InsertMenuItemA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline DWORD RegisterDeviceNotificationW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 0; /* ERROR_SUCCESS */ }
inline void* GetUpdateRect(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL AllowSetForegroundWindow(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL IsWindowUnicode(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr) noexcept { return 1; }
inline BOOL DefMDIChildProcW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetSystemMenu(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetCursorInfo(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return nullptr; }
inline BOOL CharUpperBuffW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetClassLongPtrW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetClassLongPtrW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline HWND GetForegroundWindow() noexcept { return WindowManager::get().getActiveWindow(); }
inline BOOL MsgWaitForMultipleObjectsEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* LoadKeyboardLayoutW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetMenuItemInfoA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL ActivateKeyboardLayout(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL DrawIcon(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetKeyboardState(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* CreateIcon(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SubtractRect(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetKeyboardLayoutNameW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL CountClipboardFormats(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL CharUpperBuffA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* CopyIcon(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL IsDialogMessageA(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr) noexcept { return 1; }
inline void* GetMenuDefaultItem(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetGestureConfig(void* /*hwnd*/, uint32_t /*dwReserved*/, uint32_t /*cIDs*/, void* /*pGestureConfig*/, uint32_t /*cbSize*/) noexcept { return 1; }
inline BOOL CloseGestureInfoHandle(void* /*hGestureInfo*/) noexcept { return 1; }
inline BOOL GetGestureInfo(void* /*hGestureInfo*/, void* /*pGestureInfo*/) noexcept { return 0; }
inline BOOL SystemParametersInfoForDpi(uint32_t /*uiAction*/, uint32_t /*uiParam*/, void* /*pvParam*/, uint32_t /*fWinIni*/, uint32_t /*dpi*/) noexcept { return 1; }
inline BOOL PhysicalToLogicalPoint(void* /*hWnd*/, void* /*lpPoint*/) noexcept { return 1; }
inline int32_t GetSystemMetricsForDpi(int32_t nIndex, uint32_t dpi) noexcept {
    int32_t base = 0;
    switch (nIndex) {
        case 0: base = 1920; break;
        case 1: base = 1080; break;
        case 2: base = 32; break;
        case 3: base = 32; break;
        case 4: base = 32; break;
        default: base = 16; break;
    }
    return static_cast<int32_t>(static_cast<uint64_t>(base) * dpi / 96);
}
inline uint32_t GetDpiForWindow(void* /*hWnd*/) noexcept { return 96; }
inline BOOL AreDpiAwarenessContextsEqual(void* a, void* b) noexcept { return a == b ? 1 : 0; }
inline int32_t GetAwarenessFromDpiAwarenessContext(void* /*value*/) noexcept { return 2; }
inline void* GetWindowDpiAwarenessContext(void* /*hWnd*/) noexcept { return reinterpret_cast<void*>(static_cast<uintptr_t>(-4)); }
inline void* GetThreadDpiAwarenessContext() noexcept { return reinterpret_cast<void*>(static_cast<uintptr_t>(-4)); }
inline void* SetThreadDpiAwarenessContext(void* /*dpiContext*/) noexcept { return reinterpret_cast<void*>(static_cast<uintptr_t>(-4)); }
inline BOOL AdjustWindowRectExForDpi(void* /*lpRect*/, uint32_t /*dwStyle*/, BOOL /*bMenu*/, uint32_t /*dwExStyle*/, uint32_t /*dpi*/) noexcept { return 1; }

// ============================================================================
// Multi-Monitor Display Topology & Mode Enumeration (user32.dll)
// ============================================================================

using HMONITOR = void*;
using LPCRECT  = const RECT*;
using LPRECT   = RECT*;

inline constexpr DWORD DISPLAY_DEVICE_ATTACHED_TO_DESKTOP = 0x00000001;
inline constexpr DWORD DISPLAY_DEVICE_MULTI_DRIVER         = 0x00000002;
inline constexpr DWORD DISPLAY_DEVICE_PRIMARY_DEVICE       = 0x00000004;
inline constexpr DWORD DISPLAY_DEVICE_MIRRORING_DRIVER     = 0x00000008;
inline constexpr DWORD DISPLAY_DEVICE_VGA_COMPATIBLE       = 0x00000010;
inline constexpr DWORD DISPLAY_DEVICE_REMOVABLE            = 0x00000020;
inline constexpr DWORD DISPLAY_DEVICE_ACTIVE               = 0x00000001;
inline constexpr DWORD DISPLAY_DEVICE_MODESPRUNED          = 0x08000000;

inline constexpr DWORD ENUM_CURRENT_SETTINGS  = static_cast<DWORD>(-1);
inline constexpr DWORD ENUM_REGISTRY_SETTINGS = static_cast<DWORD>(-2);

inline constexpr DWORD MONITOR_DEFAULTTONULL    = 0x00000000;
inline constexpr DWORD MONITOR_DEFAULTTOPRIMARY = 0x00000001;
inline constexpr DWORD MONITOR_DEFAULTTONEAREST = 0x00000002;

inline constexpr DWORD MONITORINFOF_PRIMARY = 0x00000001;

inline constexpr DWORD DM_BITSPERPEL       = 0x00040000;
inline constexpr DWORD DM_PELSWIDTH        = 0x00080000;
inline constexpr DWORD DM_PELSHEIGHT       = 0x00100000;
inline constexpr DWORD DM_DISPLAYFLAGS     = 0x00200000;
inline constexpr DWORD DM_DISPLAYFREQUENCY = 0x00400000;

inline constexpr LONG DISP_CHANGE_SUCCESSFUL = 0;
inline constexpr LONG DISP_CHANGE_RESTART    = 1;
inline constexpr LONG DISP_CHANGE_FAILED     = -1;
inline constexpr LONG DISP_CHANGE_BADMODE    = -2;

using win32::WCHAR;
using win32::LPCWSTR;
using win32::LPVOID;

struct DISPLAY_DEVICEW {
    DWORD  cb;
    WCHAR  DeviceName[32];
    WCHAR  DeviceString[128];
    DWORD  StateFlags;
    WCHAR  DeviceID[128];
    WCHAR  DeviceKey[128];
};
using PDISPLAY_DEVICEW = DISPLAY_DEVICEW*;

struct DEVMODEW {
    WCHAR   dmDeviceName[32];
    WORD    dmSpecVersion;
    WORD    dmDriverVersion;
    WORD    dmSize;
    WORD    dmDriverExtra;
    DWORD   dmFields;
    short   dmOrientation;
    short   dmPaperSize;
    short   dmPaperLength;
    short   dmPaperWidth;
    short   dmScale;
    short   dmCopies;
    short   dmDefaultSource;
    short   dmPrintQuality;
    short   dmColor;
    short   dmDuplex;
    short   dmYResolution;
    short   dmTTOption;
    short   dmCollate;
    WCHAR   dmFormName[32];
    WORD    dmLogPixels;
    DWORD   dmBitsPerPel;
    DWORD   dmPelsWidth;
    DWORD   dmPelsHeight;
    DWORD   dmDisplayFlags;
    DWORD   dmDisplayFrequency;
    DWORD   dmICMMethod;
    DWORD   dmICMIntent;
    DWORD   dmMediaType;
    DWORD   dmDitherType;
    DWORD   dmReserved1;
    DWORD   dmReserved2;
    DWORD   dmPanningWidth;
    DWORD   dmPanningHeight;
};

struct MONITORINFO {
    DWORD   cbSize;
    RECT    rcMonitor;
    RECT    rcWork;
    DWORD   dwFlags;
};
using LPMONITORINFO = MONITORINFO*;

struct MONITORINFOEXW : public MONITORINFO {
    WCHAR   szDevice[32];
};
using LPMONITORINFOEXW = MONITORINFOEXW*;

class SovereignDisplayTopologyManager {
private:
    std::mutex m_mutex;
    struct DisplayDeviceState {
        std::wstring name;
        std::wstring string;
        std::wstring id;
        std::wstring key;
        DWORD stateFlags;
        std::vector<DEVMODEW> modes;
        DEVMODEW currentMode;
        RECT monitorRect;
        RECT workRect;
        bool isPrimary;
    };
    std::vector<DisplayDeviceState> m_devices;

public:
    static SovereignDisplayTopologyManager& get() noexcept {
        static SovereignDisplayTopologyManager instance;
        return instance;
    }

    SovereignDisplayTopologyManager() {
        // Primary Display (\\.\DISPLAY1) - 4K 144Hz / 240Hz / 540Hz
        DisplayDeviceState disp1{};
        disp1.name = L"\\\\.\\DISPLAY1";
        disp1.string = L"PrismX DEC PRISM-64 Display Adapter";
        disp1.id = L"PCI\\VEN_10DE&DEV_2684&SUBSYS_00000000";
        disp1.key = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\{GUID1}";
        disp1.stateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_PRIMARY_DEVICE | DISPLAY_DEVICE_ACTIVE;
        disp1.monitorRect = RECT{0, 0, 3840, 2160};
        disp1.workRect = RECT{0, 0, 3840, 2120};
        disp1.isPrimary = true;

        auto makeMode = [](const wchar_t* devName, DWORD w, DWORD h, DWORD bpp, DWORD hz) {
            DEVMODEW m{};
            m.dmSize = sizeof(DEVMODEW);
            std::wcsncpy(m.dmDeviceName, devName, 31);
            m.dmPelsWidth = w;
            m.dmPelsHeight = h;
            m.dmBitsPerPel = bpp;
            m.dmDisplayFrequency = hz;
            m.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
            return m;
        };

        disp1.modes.push_back(makeMode(L"\\\\.\\DISPLAY1", 1920, 1080, 32, 60));  // Mode 0: 1080p legacy
        disp1.modes.push_back(makeMode(L"\\\\.\\DISPLAY1", 3840, 2160, 32, 144)); // Mode 1: 4K@144
        disp1.modes.push_back(makeMode(L"\\\\.\\DISPLAY1", 3840, 2160, 32, 540)); // Mode 2: 4K@540
        disp1.modes.push_back(makeMode(L"\\\\.\\DISPLAY1", 2560, 1440, 32, 240)); // Mode 3: 1440p@240
        disp1.modes.push_back(makeMode(L"\\\\.\\DISPLAY1", 3840, 2160, 32, 240)); // Mode 4: 4K@240
        disp1.currentMode = makeMode(L"\\\\.\\DISPLAY1", 3840, 2160, 32, 240);    // 4K@240Hz default

        // Secondary Display (\\.\DISPLAY2) - 1440p 144Hz
        DisplayDeviceState disp2{};
        disp2.name = L"\\\\.\\DISPLAY2";
        disp2.string = L"PrismX Secondary High-Speed Digital Display";
        disp2.id = L"PCI\\VEN_10DE&DEV_2684&SUBSYS_00000001";
        disp2.key = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\{GUID2}";
        disp2.stateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_ACTIVE;
        disp2.monitorRect = RECT{3840, 0, 3840 + 2560, 1440};
        disp2.workRect = RECT{3840, 0, 3840 + 2560, 1440};
        disp2.isPrimary = false;
        disp2.modes.push_back(makeMode(L"\\\\.\\DISPLAY2", 2560, 1440, 32, 144));
        disp2.modes.push_back(makeMode(L"\\\\.\\DISPLAY2", 1920, 1080, 32, 144));
        disp2.currentMode = disp2.modes[0];

        m_devices.push_back(disp1);
        m_devices.push_back(disp2);
    }

    win32::BOOL enumDisplayDevices(LPCWSTR lpDevice, DWORD iDevNum, PDISPLAY_DEVICEW lpDisplayDevice, DWORD /*dwFlags*/) noexcept {
        if (!lpDisplayDevice || lpDisplayDevice->cb < sizeof(DISPLAY_DEVICEW)) {
            return win32::FALSE;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (lpDevice == nullptr) {
            if (iDevNum >= m_devices.size()) return win32::FALSE;
            const auto& dev = m_devices[iDevNum];
            std::memset(lpDisplayDevice->DeviceName, 0, sizeof(lpDisplayDevice->DeviceName));
            std::memset(lpDisplayDevice->DeviceString, 0, sizeof(lpDisplayDevice->DeviceString));
            std::memset(lpDisplayDevice->DeviceID, 0, sizeof(lpDisplayDevice->DeviceID));
            std::memset(lpDisplayDevice->DeviceKey, 0, sizeof(lpDisplayDevice->DeviceKey));
            std::wcsncpy(lpDisplayDevice->DeviceName, dev.name.c_str(), 31);
            std::wcsncpy(lpDisplayDevice->DeviceString, dev.string.c_str(), 127);
            std::wcsncpy(lpDisplayDevice->DeviceID, dev.id.c_str(), 127);
            std::wcsncpy(lpDisplayDevice->DeviceKey, dev.key.c_str(), 127);
            lpDisplayDevice->StateFlags = dev.stateFlags;
            return win32::TRUE;
        } else {
            if (iDevNum != 0) return win32::FALSE;
            for (const auto& dev : m_devices) {
                if (dev.name == lpDevice) {
                    std::memset(lpDisplayDevice->DeviceName, 0, sizeof(lpDisplayDevice->DeviceName));
                    std::memset(lpDisplayDevice->DeviceString, 0, sizeof(lpDisplayDevice->DeviceString));
                    std::memset(lpDisplayDevice->DeviceID, 0, sizeof(lpDisplayDevice->DeviceID));
                    std::memset(lpDisplayDevice->DeviceKey, 0, sizeof(lpDisplayDevice->DeviceKey));
                    std::wstring monName = dev.name + L"\\Monitor0";
                    std::wcsncpy(lpDisplayDevice->DeviceName, monName.c_str(), 31);
                    std::wcsncpy(lpDisplayDevice->DeviceString, L"Generic PnP High-Refresh Gaming Monitor", 127);
                    std::wcsncpy(lpDisplayDevice->DeviceID, L"MONITOR\\GSM5B09\\{GUID}", 127);
                    lpDisplayDevice->StateFlags = dev.stateFlags;
                    return win32::TRUE;
                }
            }
            return win32::FALSE;
        }
    }

    win32::BOOL enumDisplaySettings(LPCWSTR lpszDeviceName, DWORD iModeNum, void* lpDevMode) noexcept {
        if (!lpDevMode) return win32::FALSE;
        std::lock_guard<std::mutex> lock(m_mutex);
        const DisplayDeviceState* target = nullptr;
        if (lpszDeviceName == nullptr) {
            target = &m_devices[0];
        } else {
            for (const auto& dev : m_devices) {
                if (dev.name == lpszDeviceName) {
                    target = &dev;
                    break;
                }
            }
        }
        if (!target) return win32::FALSE;

        DEVMODEW* dm = reinterpret_cast<DEVMODEW*>(lpDevMode);
        if (iModeNum == ENUM_CURRENT_SETTINGS) {
            *dm = target->currentMode;
            return win32::TRUE;
        } else if (iModeNum == ENUM_REGISTRY_SETTINGS) {
            *dm = target->modes[0];
            return win32::TRUE;
        } else {
            if (iModeNum >= target->modes.size()) return win32::FALSE;
            *dm = target->modes[iModeNum];
            if (iModeNum == 0) {
                *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(lpDevMode) + 108) = 1920;
                *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(lpDevMode) + 112) = 1080;
            }
            return win32::TRUE;
        }
    }

    LONG changeDisplaySettingsEx(LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, win32::HWND /*hwnd*/, DWORD /*dwflags*/, LPVOID /*lParam*/) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        DisplayDeviceState* target = nullptr;
        if (lpszDeviceName == nullptr) {
            target = &m_devices[0];
        } else {
            for (auto& dev : m_devices) {
                if (dev.name == lpszDeviceName) {
                    target = &dev;
                    break;
                }
            }
        }
        if (!target) return DISP_CHANGE_FAILED;
        if (lpDevMode == nullptr) {
            target->currentMode = target->modes[0];
            return DISP_CHANGE_SUCCESSFUL;
        }
        target->currentMode = *lpDevMode;
        return DISP_CHANGE_SUCCESSFUL;
    }

    win32::BOOL getMonitorInfo(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept {
        if (!lpmi || lpmi->cbSize < sizeof(MONITORINFO)) {
            return win32::FALSE;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        uintptr_t idx = reinterpret_cast<uintptr_t>(hMonitor);
        size_t devIndex = (idx == 0x8801) ? 0 : ((idx == 0x8802) ? 1 : 0);
        if (devIndex >= m_devices.size()) devIndex = 0;
        const auto& dev = m_devices[devIndex];

        lpmi->rcMonitor = dev.monitorRect;
        lpmi->rcWork = dev.workRect;
        lpmi->dwFlags = dev.isPrimary ? MONITORINFOF_PRIMARY : 0;

        if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) {
            auto* ex = reinterpret_cast<LPMONITORINFOEXW>(lpmi);
            std::memset(ex->szDevice, 0, sizeof(ex->szDevice));
            std::wcsncpy(ex->szDevice, dev.name.c_str(), 31);
        }
        return win32::TRUE;
    }

    HMONITOR monitorFromPoint(POINT pt, DWORD dwFlags) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_devices.size(); ++i) {
            const auto& r = m_devices[i].monitorRect;
            if (pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom) {
                return reinterpret_cast<HMONITOR>(0x8801 + i);
            }
        }
        if (dwFlags == MONITOR_DEFAULTTOPRIMARY) return reinterpret_cast<HMONITOR>(0x8801);
        if (dwFlags == MONITOR_DEFAULTTONEAREST) return reinterpret_cast<HMONITOR>(0x8801);
        return nullptr;
    }

    HMONITOR monitorFromRect(LPCRECT lprc, DWORD dwFlags) noexcept {
        if (!lprc) {
            if (dwFlags == MONITOR_DEFAULTTOPRIMARY) return reinterpret_cast<HMONITOR>(0x8801);
            return nullptr;
        }
        POINT pt{ (lprc->left + lprc->right) / 2, (lprc->top + lprc->bottom) / 2 };
        return monitorFromPoint(pt, dwFlags);
    }

    HMONITOR monitorFromWindow(win32::HWND /*hwnd*/, DWORD dwFlags) noexcept {
        if (dwFlags == MONITOR_DEFAULTTOPRIMARY || dwFlags == MONITOR_DEFAULTTONEAREST) {
            return reinterpret_cast<HMONITOR>(0x8801);
        }
        return reinterpret_cast<HMONITOR>(0x8801);
    }
};

inline win32::BOOL WINAPI EnumDisplayDevicesW(LPCWSTR lpDevice, DWORD iDevNum, PDISPLAY_DEVICEW lpDisplayDevice, DWORD dwFlags) noexcept {
    return SovereignDisplayTopologyManager::get().enumDisplayDevices(lpDevice, iDevNum, lpDisplayDevice, dwFlags);
}

inline win32::BOOL WINAPI EnumDisplaySettingsW(LPCWSTR lpszDeviceName, DWORD iModeNum, void* lpDevMode) noexcept {
    return SovereignDisplayTopologyManager::get().enumDisplaySettings(lpszDeviceName, iModeNum, lpDevMode);
}

inline LONG WINAPI ChangeDisplaySettingsExW(LPCWSTR lpszDeviceName, DEVMODEW* lpDevMode, win32::HWND hwnd, DWORD dwflags, LPVOID lParam) noexcept {
    return SovereignDisplayTopologyManager::get().changeDisplaySettingsEx(lpszDeviceName, lpDevMode, hwnd, dwflags, lParam);
}

inline win32::BOOL WINAPI GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept {
    return SovereignDisplayTopologyManager::get().getMonitorInfo(hMonitor, lpmi);
}

inline HMONITOR WINAPI MonitorFromWindow(win32::HWND hwnd, DWORD dwFlags) noexcept {
    return SovereignDisplayTopologyManager::get().monitorFromWindow(hwnd, dwFlags);
}

inline HMONITOR WINAPI MonitorFromRect(LPCRECT lprc, DWORD dwFlags) noexcept {
    return SovereignDisplayTopologyManager::get().monitorFromRect(lprc, dwFlags);
}

inline HMONITOR WINAPI MonitorFromPoint(POINT pt, DWORD dwFlags) noexcept {
    return SovereignDisplayTopologyManager::get().monitorFromPoint(pt, dwFlags);
}

// ============================================================================
// 7. Subsystem Export Registration
// ============================================================================

inline void InitializeUser32SubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();

    // Hook SwapChain backbuffer presenter
    prismx::PrismXSwapChainImpl::SetGlobalWindowPresenter(BlitToWindowCallback);

    ldr.registerExport("user32.dll", "RegisterClassExW", reinterpret_cast<void*>(RegisterClassExW));
    ldr.registerExport("user32.dll", "RegisterClassW", reinterpret_cast<void*>(RegisterClassW));
    ldr.registerExport("user32.dll", "UnregisterClassW", reinterpret_cast<void*>(UnregisterClassW));
    ldr.registerExport("user32.dll", "GetClassInfoExW", reinterpret_cast<void*>(GetClassInfoExW));
    ldr.registerExport("user32.dll", "CreateWindowExW", reinterpret_cast<void*>(CreateWindowExW));
    ldr.registerExport("user32.dll", "CreateWindowExA", reinterpret_cast<void*>(CreateWindowExA));
    ldr.registerExport("user32.dll", "DestroyWindow", reinterpret_cast<void*>(DestroyWindow));
    ldr.registerExport("user32.dll", "IsWindow", reinterpret_cast<void*>(IsWindow));
    ldr.registerExport("user32.dll", "ShowWindow", reinterpret_cast<void*>(ShowWindow));
    ldr.registerExport("user32.dll", "IsWindowVisible", reinterpret_cast<void*>(IsWindowVisible));
    ldr.registerExport("user32.dll", "IsIconic", reinterpret_cast<void*>(IsIconic));
    ldr.registerExport("user32.dll", "UpdateWindow", reinterpret_cast<void*>(UpdateWindow));
    ldr.registerExport("user32.dll", "InvalidateRect", reinterpret_cast<void*>(InvalidateRect));
    ldr.registerExport("user32.dll", "ValidateRect", reinterpret_cast<void*>(ValidateRect));
    ldr.registerExport("user32.dll", "GetClientRect", reinterpret_cast<void*>(GetClientRect));
    ldr.registerExport("user32.dll", "GetWindowRect", reinterpret_cast<void*>(GetWindowRect));
    ldr.registerExport("user32.dll", "AdjustWindowRectEx", reinterpret_cast<void*>(AdjustWindowRectEx));
    ldr.registerExport("user32.dll", "SetWindowPos", reinterpret_cast<void*>(SetWindowPos));
    ldr.registerExport("user32.dll", "MoveWindow", reinterpret_cast<void*>(MoveWindow));
    ldr.registerExport("user32.dll", "SetWindowLongPtrW", reinterpret_cast<void*>(SetWindowLongPtrW));
    ldr.registerExport("user32.dll", "GetWindowLongPtrW", reinterpret_cast<void*>(GetWindowLongPtrW));
    ldr.registerExport("user32.dll", "SetWindowLongW", reinterpret_cast<void*>(SetWindowLongW));
    ldr.registerExport("user32.dll", "GetWindowLongW", reinterpret_cast<void*>(GetWindowLongW));
    ldr.registerExport("user32.dll", "PostMessageW", reinterpret_cast<void*>(PostMessageW));
    ldr.registerExport("user32.dll", "SendMessageW", reinterpret_cast<void*>(SendMessageW));
    ldr.registerExport("user32.dll", "PeekMessageW", reinterpret_cast<void*>(PeekMessageW));
    ldr.registerExport("user32.dll", "PeekMessageA", reinterpret_cast<void*>(PeekMessageA));
    ldr.registerExport("user32.dll", "GetMessageW", reinterpret_cast<void*>(GetMessageW));
    ldr.registerExport("user32.dll", "TranslateMessage", reinterpret_cast<void*>(TranslateMessage));
    ldr.registerExport("user32.dll", "DispatchMessageW", reinterpret_cast<void*>(DispatchMessageW));
    ldr.registerExport("user32.dll", "DispatchMessageA", reinterpret_cast<void*>(DispatchMessageA));
    ldr.registerExport("user32.dll", "PostQuitMessage", reinterpret_cast<void*>(PostQuitMessage));
    ldr.registerExport("user32.dll", "DefWindowProcW", reinterpret_cast<void*>(DefWindowProcW));
    ldr.registerExport("user32.dll", "GetDC", reinterpret_cast<void*>(GetDC));
    ldr.registerExport("user32.dll", "ReleaseDC", reinterpret_cast<void*>(ReleaseDC));
    ldr.registerExport("user32.dll", "BeginPaint", reinterpret_cast<void*>(BeginPaint));
    ldr.registerExport("user32.dll", "EndPaint", reinterpret_cast<void*>(EndPaint));
    ldr.registerExport("user32.dll", "RegisterRawInputDevices", reinterpret_cast<void*>(RegisterRawInputDevices));
    ldr.registerExport("user32.dll", "GetRawInputData", reinterpret_cast<void*>(GetRawInputData));
    ldr.registerExport("user32.dll", "GetRawInputDeviceList", reinterpret_cast<void*>(GetRawInputDeviceList));
    ldr.registerExport("user32.dll", "GetRawInputDeviceInfoW", reinterpret_cast<void*>(GetRawInputDeviceInfoW));
    ldr.registerExport("user32.dll", "GetRegisteredRawInputDevices", reinterpret_cast<void*>(GetRegisteredRawInputDevices));
    ldr.registerExport("user32.dll", "GetRawInputBuffer", reinterpret_cast<void*>(GetRawInputBuffer));
    ldr.registerExport("user32.dll", "MsgWaitForMultipleObjects", reinterpret_cast<void*>(MsgWaitForMultipleObjects));
    ldr.registerExport("user32.dll", "LockWorkStation", reinterpret_cast<void*>(LockWorkStation));
    ldr.registerExport("user32.dll", "OpenDesktopW", reinterpret_cast<void*>(OpenDesktopW));
    ldr.registerExport("user32.dll", "SwitchDesktop", reinterpret_cast<void*>(SwitchDesktop));
    ldr.registerExport("user32.dll", "CloseDesktop", reinterpret_cast<void*>(CloseDesktop));
    ldr.registerExport("user32.dll", "MessageBoxW", reinterpret_cast<void*>(MessageBoxW));
    ldr.registerExport("user32.dll", "MessageBoxA", reinterpret_cast<void*>(MessageBoxA));
    ldr.registerExport("user32.dll", "CharUpperW", reinterpret_cast<void*>(CharUpperW));
    ldr.registerExport("user32.dll", "GetDesktopWindow", reinterpret_cast<void*>(GetDesktopWindow));
    ldr.registerExport("user32.dll", "GetSystemMetrics", reinterpret_cast<void*>(GetSystemMetrics));
    ldr.registerExport("user32.dll", "SetFocus", reinterpret_cast<void*>(SetFocus));
    ldr.registerExport("user32.dll", "GetFocus", reinterpret_cast<void*>(GetFocus));
    ldr.registerExport("user32.dll", "EnableWindow", reinterpret_cast<void*>(EnableWindow));
    ldr.registerExport("user32.dll", "IsWindowEnabled", reinterpret_cast<void*>(IsWindowEnabled));
    ldr.registerExport("user32.dll", "SetWindowTextW", reinterpret_cast<void*>(SetWindowTextW));
    ldr.registerExport("user32.dll", "GetWindowTextW", reinterpret_cast<void*>(GetWindowTextW));
    ldr.registerExport("user32.dll", "GetWindowTextLengthW", reinterpret_cast<void*>(GetWindowTextLengthW));
    ldr.registerExport("user32.dll", "AnimateWindow", reinterpret_cast<void*>(AnimateWindow));
    ldr.registerExport("user32.dll", "DrawStateW", reinterpret_cast<void*>(DrawStateW));
    ldr.registerExport("user32.dll", "GetProcessDefaultLayout", reinterpret_cast<void*>(GetProcessDefaultLayout));
    ldr.registerExport("user32.dll", "LoadCursorFromFileW", reinterpret_cast<void*>(LoadCursorFromFileW));
    ldr.registerExport("user32.dll", "SetCaretBlinkTime", reinterpret_cast<void*>(SetCaretBlinkTime));
    ldr.registerExport("user32.dll", "ValidateRgn", reinterpret_cast<void*>(ValidateRgn));
    ldr.registerExport("user32.dll", "VkKeyScanW", reinterpret_cast<void*>(VkKeyScanW));
    ldr.registerExport("user32.dll", "WaitForInputIdle", reinterpret_cast<void*>(WaitForInputIdle));
    ldr.registerExport("user32.dll", "keybd_event", reinterpret_cast<void*>(keybd_event));
    ldr.registerExport("user32.dll", "DdeCreateDataHandle", reinterpret_cast<void*>(DdeCreateDataHandle));
    ldr.registerExport("user32.dll", "DdeGetData", reinterpret_cast<void*>(DdeGetData));
    ldr.registerExport("user32.dll", "DdeGetLastError", reinterpret_cast<void*>(DdeGetLastError));
    ldr.registerExport("user32.dll", "DdeNameService", reinterpret_cast<void*>(DdeNameService));
    ldr.registerExport("user32.dll", "DdePostAdvise", reinterpret_cast<void*>(DdePostAdvise));
    ldr.registerExport("user32.dll", "DdeQueryStringW", reinterpret_cast<void*>(DdeQueryStringW));
    ldr.registerExport("user32.dll", "GetClassLongPtrA", reinterpret_cast<void*>(GetClassLongPtrA));
    ldr.registerExport("user32.dll", "GetRawInputDeviceInfoA", reinterpret_cast<void*>(GetRawInputDeviceInfoA));
    ldr.registerExport("user32.dll", "GetWindowDisplayAffinity", reinterpret_cast<void*>(GetWindowDisplayAffinity));
    ldr.registerExport("user32.dll", "GetWindowLongA", reinterpret_cast<void*>(GetWindowLongA));
    ldr.registerExport("user32.dll", "MapVirtualKeyA", reinterpret_cast<void*>(MapVirtualKeyA));
    ldr.registerExport("user32.dll", "SetProcessDPIAware", reinterpret_cast<void*>(SetProcessDPIAware));
    ldr.registerExport("user32.dll", "ToUnicodeEx", reinterpret_cast<void*>(ToUnicodeEx));
    ldr.registerExport("user32.dll", "VkKeyScanExA", reinterpret_cast<void*>(VkKeyScanExA));
    ldr.registerExport("user32.dll", "MapDialogRect", reinterpret_cast<void*>(MapDialogRect));
    ldr.registerExport("user32.dll", "GetMonitorInfoA", reinterpret_cast<void*>(GetMonitorInfoA));
    ldr.registerExport("user32.dll", "GetDialogBaseUnits", reinterpret_cast<void*>(GetDialogBaseUnits));
    ldr.registerExport("user32.dll", "CheckRadioButton", reinterpret_cast<void*>(CheckRadioButton));
    ldr.registerExport("user32.dll", "IsDlgButtonChecked", reinterpret_cast<void*>(IsDlgButtonChecked));
    ldr.registerExport("user32.dll", "CheckDlgButton", reinterpret_cast<void*>(CheckDlgButton));
    ldr.registerExport("user32.dll", "LoadAcceleratorsW", reinterpret_cast<void*>(LoadAcceleratorsW));
    ldr.registerExport("user32.dll", "GetClassInfoW", reinterpret_cast<void*>(GetClassInfoW));
    ldr.registerExport("user32.dll", "ScrollWindowEx", reinterpret_cast<void*>(ScrollWindowEx));
    ldr.registerExport("user32.dll", "AdjustWindowRect", reinterpret_cast<void*>(AdjustWindowRect));
    ldr.registerExport("user32.dll", "CopyRect", reinterpret_cast<void*>(CopyRect));
    ldr.registerExport("user32.dll", "OpenIcon", reinterpret_cast<void*>(OpenIcon));
    ldr.registerExport("user32.dll", "GetNextDlgTabItem", reinterpret_cast<void*>(GetNextDlgTabItem));
    ldr.registerExport("user32.dll", "ReplyMessage", reinterpret_cast<void*>(ReplyMessage));
    ldr.registerExport("user32.dll", "RegisterHotKey", reinterpret_cast<void*>(RegisterHotKey));
    ldr.registerExport("user32.dll", "UnregisterHotKey", reinterpret_cast<void*>(UnregisterHotKey));
    ldr.registerExport("user32.dll", "PostThreadMessageW", reinterpret_cast<void*>(PostThreadMessageW));
    ldr.registerExport("user32.dll", "SendMessageTimeoutW", reinterpret_cast<void*>(SendMessageTimeoutW));
    ldr.registerExport("user32.dll", "MapVirtualKeyExW", reinterpret_cast<void*>(MapVirtualKeyExW));
    ldr.registerExport("user32.dll", "DrawCaption", reinterpret_cast<void*>(DrawCaption));
    ldr.registerExport("user32.dll", "GetClassLongW", reinterpret_cast<void*>(GetClassLongW));
    ldr.registerExport("user32.dll", "SetClassLongW", reinterpret_cast<void*>(SetClassLongW));
    ldr.registerExport("user32.dll", "SendNotifyMessageW", reinterpret_cast<void*>(SendNotifyMessageW));
    ldr.registerExport("user32.dll", "WindowFromDC", reinterpret_cast<void*>(WindowFromDC));
    ldr.registerExport("user32.dll", "IsCharUpperW", reinterpret_cast<void*>(IsCharUpperW));
    ldr.registerExport("user32.dll", "ShowWindowAsync", reinterpret_cast<void*>(ShowWindowAsync));
    ldr.registerExport("user32.dll", "SetMenuInfo", reinterpret_cast<void*>(SetMenuInfo));
    ldr.registerExport("user32.dll", "GetMenuInfo", reinterpret_cast<void*>(GetMenuInfo));
    ldr.registerExport("user32.dll", "SetMenuDefaultItem", reinterpret_cast<void*>(SetMenuDefaultItem));
    ldr.registerExport("user32.dll", "VkKeyScanExW", reinterpret_cast<void*>(VkKeyScanExW));
    ldr.registerExport("user32.dll", "SendInput", reinterpret_cast<void*>(SendInput));
    ldr.registerExport("user32.dll", "GetWindowInfo", reinterpret_cast<void*>(GetWindowInfo));
    ldr.registerExport("user32.dll", "CharToOemA", reinterpret_cast<void*>(CharToOemA));
    ldr.registerExport("user32.dll", "OemToCharBuffA", reinterpret_cast<void*>(OemToCharBuffA));
    ldr.registerExport("user32.dll", "OemToCharA", reinterpret_cast<void*>(OemToCharA));
    ldr.registerExport("user32.dll", "DdeInitializeW", reinterpret_cast<void*>(DdeInitializeW));
    ldr.registerExport("user32.dll", "DdeUninitialize", reinterpret_cast<void*>(DdeUninitialize));
    ldr.registerExport("user32.dll", "DdeCreateStringHandleW", reinterpret_cast<void*>(DdeCreateStringHandleW));
    ldr.registerExport("user32.dll", "DdeFreeStringHandle", reinterpret_cast<void*>(DdeFreeStringHandle));
    ldr.registerExport("user32.dll", "DdeConnect", reinterpret_cast<void*>(DdeConnect));
    ldr.registerExport("user32.dll", "DdeDisconnect", reinterpret_cast<void*>(DdeDisconnect));
    ldr.registerExport("user32.dll", "DdeClientTransaction", reinterpret_cast<void*>(DdeClientTransaction));
    ldr.registerExport("user32.dll", "DdeFreeDataHandle", reinterpret_cast<void*>(DdeFreeDataHandle));
    ldr.registerExport("user32.dll", "PackDDElParam", reinterpret_cast<void*>(PackDDElParam));
    ldr.registerExport("user32.dll", "CreateDialogParamA", reinterpret_cast<void*>(CreateDialogParamA));
    ldr.registerExport("user32.dll", "DefDlgProcA", reinterpret_cast<void*>(DefDlgProcA));
    ldr.registerExport("user32.dll", "DefWindowProcA", reinterpret_cast<void*>(DefWindowProcA));
    ldr.registerExport("user32.dll", "DialogBoxParamA", reinterpret_cast<void*>(DialogBoxParamA));
    ldr.registerExport("user32.dll", "FindWindowA", reinterpret_cast<void*>(FindWindowA));
    ldr.registerExport("user32.dll", "FlashWindow", reinterpret_cast<void*>(FlashWindow));
    ldr.registerExport("user32.dll", "GetClipboardOwner", reinterpret_cast<void*>(GetClipboardOwner));
    ldr.registerExport("user32.dll", "GetMessageA", reinterpret_cast<void*>(GetMessageA));
    ldr.registerExport("user32.dll", "GetQueueStatus", reinterpret_cast<void*>(GetQueueStatus));
    ldr.registerExport("user32.dll", "GetWindowLongPtrA", reinterpret_cast<void*>(GetWindowLongPtrA));
    ldr.registerExport("user32.dll", "GetWindowTextLengthA", reinterpret_cast<void*>(GetWindowTextLengthA));
    ldr.registerExport("user32.dll", "GetWindowTextA", reinterpret_cast<void*>(GetWindowTextA));
    ldr.registerExport("user32.dll", "InsertMenuA", reinterpret_cast<void*>(InsertMenuA));
    ldr.registerExport("user32.dll", "LoadCursorA", reinterpret_cast<void*>(LoadCursorA));
    ldr.registerExport("user32.dll", "LoadIconA", reinterpret_cast<void*>(LoadIconA));
    ldr.registerExport("user32.dll", "LoadImageA", reinterpret_cast<void*>(LoadImageA));
    ldr.registerExport("user32.dll", "MessageBoxIndirectW", reinterpret_cast<void*>(MessageBoxIndirectW));
    ldr.registerExport("user32.dll", "PostMessageA", reinterpret_cast<void*>(PostMessageA));
    ldr.registerExport("user32.dll", "RegisterClassA", reinterpret_cast<void*>(RegisterClassA));
    ldr.registerExport("user32.dll", "RegisterClipboardFormatA", reinterpret_cast<void*>(RegisterClipboardFormatA));
    ldr.registerExport("user32.dll", "RegisterWindowMessageA", reinterpret_cast<void*>(RegisterWindowMessageA));
    ldr.registerExport("user32.dll", "SendDlgItemMessageA", reinterpret_cast<void*>(SendDlgItemMessageA));
    ldr.registerExport("user32.dll", "SetClassLongPtrA", reinterpret_cast<void*>(SetClassLongPtrA));
    ldr.registerExport("user32.dll", "SetWindowLongPtrA", reinterpret_cast<void*>(SetWindowLongPtrA));
    ldr.registerExport("user32.dll", "SetWindowTextA", reinterpret_cast<void*>(SetWindowTextA));
    ldr.registerExport("user32.dll", "ToAsciiEx", reinterpret_cast<void*>(ToAsciiEx));
    ldr.registerExport("user32.dll", "ChangeWindowMessageFilterEx", reinterpret_cast<void*>(ChangeWindowMessageFilterEx));
    ldr.registerExport("user32.dll", "CharLowerA", reinterpret_cast<void*>(CharLowerA));
    ldr.registerExport("user32.dll", "CharUpperA", reinterpret_cast<void*>(CharUpperA));
    ldr.registerExport("user32.dll", "CreateIconFromResourceEx", reinterpret_cast<void*>(CreateIconFromResourceEx));
    ldr.registerExport("user32.dll", "DrawTextExA", reinterpret_cast<void*>(DrawTextExA));
    ldr.registerExport("user32.dll", "GetKeyboardLayoutNameA", reinterpret_cast<void*>(GetKeyboardLayoutNameA));
    ldr.registerExport("user32.dll", "MessageBoxExW", reinterpret_cast<void*>(MessageBoxExW));
    ldr.registerExport("user32.dll", "SetProcessDefaultLayout", reinterpret_cast<void*>(SetProcessDefaultLayout));
    ldr.registerExport("user32.dll", "SetWinEventHook", reinterpret_cast<void*>(SetWinEventHook));
    ldr.registerExport("user32.dll", "UnhookWinEvent", reinterpret_cast<void*>(UnhookWinEvent));
    ldr.registerExport("user32.dll", "CopyAcceleratorTableW", reinterpret_cast<void*>(CopyAcceleratorTableW));
    ldr.registerExport("user32.dll", "RealChildWindowFromPoint", reinterpret_cast<void*>(RealChildWindowFromPoint));
    ldr.registerExport("user32.dll", "UnionRect", reinterpret_cast<void*>(UnionRect));
    ldr.registerExport("user32.dll", "GetTabbedTextExtentW", reinterpret_cast<void*>(GetTabbedTextExtentW));
    ldr.registerExport("user32.dll", "ReuseDDElParam", reinterpret_cast<void*>(ReuseDDElParam));
    ldr.registerExport("user32.dll", "UnpackDDElParam", reinterpret_cast<void*>(UnpackDDElParam));
    ldr.registerExport("user32.dll", "WinHelpW", reinterpret_cast<void*>(WinHelpW));
    ldr.registerExport("user32.dll", "GetMenuCheckMarkDimensions", reinterpret_cast<void*>(GetMenuCheckMarkDimensions));
    ldr.registerExport("user32.dll", "ChildWindowFromPoint", reinterpret_cast<void*>(ChildWindowFromPoint));
    ldr.registerExport("user32.dll", "GetThreadDesktop", reinterpret_cast<void*>(GetThreadDesktop));
    ldr.registerExport("user32.dll", "GetUserObjectInformationW", reinterpret_cast<void*>(GetUserObjectInformationW));
    ldr.registerExport("user32.dll", "DragDetect", reinterpret_cast<void*>(DragDetect));
    ldr.registerExport("user32.dll", "IsMenu", reinterpret_cast<void*>(IsMenu));
    ldr.registerExport("user32.dll", "GrayStringW", reinterpret_cast<void*>(GrayStringW));
    ldr.registerExport("user32.dll", "TabbedTextOutW", reinterpret_cast<void*>(TabbedTextOutW));
    ldr.registerExport("user32.dll", "wsprintfA", reinterpret_cast<void*>(wsprintfA));
    ldr.registerExport("user32.dll", "CharPrevW", reinterpret_cast<void*>(CharPrevW));
    ldr.registerExport("user32.dll", "GetCaretPos", reinterpret_cast<void*>(GetCaretPos));
    ldr.registerExport("user32.dll", "SetProcessDpiAwarenessContext", reinterpret_cast<void*>(SetProcessDpiAwarenessContext));
    ldr.registerExport("user32.dll", "IsValidDpiAwarenessContext", reinterpret_cast<void*>(IsValidDpiAwarenessContext));
    ldr.registerExport("user32.dll", "EnableNonClientDpiScaling", reinterpret_cast<void*>(EnableNonClientDpiScaling));
    ldr.registerExport("user32.dll", "GetDisplayConfigBufferSizes", reinterpret_cast<void*>(GetDisplayConfigBufferSizes));
    ldr.registerExport("user32.dll", "QueryDisplayConfig", reinterpret_cast<void*>(QueryDisplayConfig));
    ldr.registerExport("user32.dll", "DisplayConfigGetDeviceInfo", reinterpret_cast<void*>(DisplayConfigGetDeviceInfo));
    ldr.registerExport("user32.dll", "UpdateLayeredWindow", reinterpret_cast<void*>(UpdateLayeredWindow));
    ldr.registerExport("user32.dll", "UpdateLayeredWindowIndirect", reinterpret_cast<void*>(UpdateLayeredWindowIndirect));
    ldr.registerExport("user32.dll", "RegisterTouchWindow", reinterpret_cast<void*>(RegisterTouchWindow));
    ldr.registerExport("user32.dll", "UnregisterTouchWindow", reinterpret_cast<void*>(UnregisterTouchWindow));
    ldr.registerExport("user32.dll", "IsTouchWindow", reinterpret_cast<void*>(IsTouchWindow));
    ldr.registerExport("user32.dll", "GetPointerFrameTouchInfoHistory", reinterpret_cast<void*>(GetPointerFrameTouchInfoHistory));
    ldr.registerExport("user32.dll", "SkipPointerFrameMessages", reinterpret_cast<void*>(SkipPointerFrameMessages));
    ldr.registerExport("user32.dll", "ChangeWindowMessageFilter", reinterpret_cast<void*>(ChangeWindowMessageFilter));
    ldr.registerExport("user32.dll", "RegisterPowerSettingNotification", reinterpret_cast<void*>(RegisterPowerSettingNotification));
    ldr.registerExport("user32.dll", "UnregisterPowerSettingNotification", reinterpret_cast<void*>(UnregisterPowerSettingNotification));
    ldr.registerExport("user32.dll", "UnregisterDeviceNotification", reinterpret_cast<void*>(UnregisterDeviceNotification));
    ldr.registerExport("user32.dll", "ShutdownBlockReasonCreate", reinterpret_cast<void*>(ShutdownBlockReasonCreate));
    ldr.registerExport("user32.dll", "ShutdownBlockReasonDestroy", reinterpret_cast<void*>(ShutdownBlockReasonDestroy));
    ldr.registerExport("user32.dll", "CreateCursor", reinterpret_cast<void*>(CreateCursor));
    ldr.registerExport("user32.dll", "ToUnicode", reinterpret_cast<void*>(ToUnicode));
    ldr.registerExport("user32.dll", "HiliteMenuItem", reinterpret_cast<void*>(HiliteMenuItem));
    ldr.registerExport("user32.dll", "CharPrevExA", reinterpret_cast<void*>(CharPrevExA));
    ldr.registerExport("user32.dll", "SetCoalescableTimer", reinterpret_cast<void*>(SetCoalescableTimer));
    ldr.registerExport("user32.dll", "OpenWindowStationW", reinterpret_cast<void*>(OpenWindowStationW));
    ldr.registerExport("user32.dll", "CloseWindowStation", reinterpret_cast<void*>(CloseWindowStation));
    ldr.registerExport("user32.dll", "GetProcessWindowStation", reinterpret_cast<void*>(GetProcessWindowStation));
    ldr.registerExport("user32.dll", "EnumDesktopsW", reinterpret_cast<void*>(EnumDesktopsW));
    ldr.registerExport("user32.dll", "ExitWindowsEx", reinterpret_cast<void*>(ExitWindowsEx));
    ldr.registerExport("user32.dll", "GetGUIThreadInfo", reinterpret_cast<void*>(GetGUIThreadInfo));
    ldr.registerExport("user32.dll", "GetGuiResources", reinterpret_cast<void*>(GetGuiResources));
    ldr.registerExport("user32.dll", "GetShellWindow", reinterpret_cast<void*>(GetShellWindow));
    ldr.registerExport("user32.dll", "InternalGetWindowText", reinterpret_cast<void*>(InternalGetWindowText));
    ldr.registerExport("user32.dll", "InvertRect", reinterpret_cast<void*>(InvertRect));
    ldr.registerExport("user32.dll", "IsHungAppWindow", reinterpret_cast<void*>(IsHungAppWindow));
    ldr.registerExport("user32.dll", "LoadMenuIndirectW", reinterpret_cast<void*>(LoadMenuIndirectW));
    ldr.registerExport("user32.dll", "LookupIconIdFromDirectoryEx", reinterpret_cast<void*>(LookupIconIdFromDirectoryEx));
    ldr.registerExport("user32.dll", "MenuItemFromPoint", reinterpret_cast<void*>(MenuItemFromPoint));
    ldr.registerExport("user32.dll", "SetWindowDisplayAffinity", reinterpret_cast<void*>(SetWindowDisplayAffinity));

    ldr.registerExport("user32.dll", "CopyImage", reinterpret_cast<void*>(CopyImage));
    ldr.registerExport("user32.dll", "DefFrameProcW", reinterpret_cast<void*>(DefFrameProcW));
    ldr.registerExport("user32.dll", "CharToOemBuffW", reinterpret_cast<void*>(CharToOemBuffW));
    ldr.registerExport("user32.dll", "ClipCursor", reinterpret_cast<void*>(ClipCursor));
    ldr.registerExport("user32.dll", "SendMessageA", reinterpret_cast<void*>(SendMessageA));
    ldr.registerExport("user32.dll", "EnumWindows", reinterpret_cast<void*>(EnumWindows));
    ldr.registerExport("user32.dll", "ShowOwnedPopups", reinterpret_cast<void*>(ShowOwnedPopups));
    ldr.registerExport("user32.dll", "SetActiveWindow", reinterpret_cast<void*>(SetActiveWindow));
    ldr.registerExport("user32.dll", "GetKeyboardLayoutList", reinterpret_cast<void*>(GetKeyboardLayoutList));
    ldr.registerExport("user32.dll", "GetScrollBarInfo", reinterpret_cast<void*>(GetScrollBarInfo));
    ldr.registerExport("user32.dll", "CharLowerBuffW", reinterpret_cast<void*>(CharLowerBuffW));
    ldr.registerExport("user32.dll", "InvalidateRgn", reinterpret_cast<void*>(InvalidateRgn));
    ldr.registerExport("user32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(GetAsyncKeyState));
    ldr.registerExport("user32.dll", "EndMenu", reinterpret_cast<void*>(EndMenu));
    ldr.registerExport("user32.dll", "CharNextW", reinterpret_cast<void*>(CharNextW));
    ldr.registerExport("user32.dll", "AttachThreadInput", reinterpret_cast<void*>(AttachThreadInput));
    ldr.registerExport("user32.dll", "GetTopWindow", reinterpret_cast<void*>(GetTopWindow));
    ldr.registerExport("user32.dll", "SetWindowRgn", reinterpret_cast<void*>(SetWindowRgn));
    ldr.registerExport("user32.dll", "CharLowerBuffA", reinterpret_cast<void*>(CharLowerBuffA));
    ldr.registerExport("user32.dll", "EnumClipboardFormats", reinterpret_cast<void*>(EnumClipboardFormats));
    ldr.registerExport("user32.dll", "ScrollDC", reinterpret_cast<void*>(ScrollDC));
    ldr.registerExport("user32.dll", "GetMessageExtraInfo", reinterpret_cast<void*>(GetMessageExtraInfo));
    ldr.registerExport("user32.dll", "EnableScrollBar", reinterpret_cast<void*>(EnableScrollBar));
    ldr.registerExport("user32.dll", "GetMessagePos", reinterpret_cast<void*>(GetMessagePos));
    ldr.registerExport("user32.dll", "GetKeyNameTextW", reinterpret_cast<void*>(GetKeyNameTextW));
    ldr.registerExport("user32.dll", "SetCursorPos", reinterpret_cast<void*>(SetCursorPos));
    ldr.registerExport("user32.dll", "SetRect", reinterpret_cast<void*>(SetRect));
    ldr.registerExport("user32.dll", "IsRectEmpty", reinterpret_cast<void*>(IsRectEmpty));
    ldr.registerExport("user32.dll", "GetCursor", reinterpret_cast<void*>(GetCursor));
    ldr.registerExport("user32.dll", "WaitMessage", reinterpret_cast<void*>(WaitMessage));
    ldr.registerExport("user32.dll", "TranslateMDISysAccel", reinterpret_cast<void*>(TranslateMDISysAccel));
    ldr.registerExport("user32.dll", "GetClipboardFormatNameW", reinterpret_cast<void*>(GetClipboardFormatNameW));
    ldr.registerExport("user32.dll", "GetMenuItemRect", reinterpret_cast<void*>(GetMenuItemRect));
    ldr.registerExport("user32.dll", "InsertMenuItemA", reinterpret_cast<void*>(InsertMenuItemA));
    ldr.registerExport("user32.dll", "RegisterDeviceNotificationW", reinterpret_cast<void*>(RegisterDeviceNotificationW));
    ldr.registerExport("user32.dll", "GetUpdateRect", reinterpret_cast<void*>(GetUpdateRect));
    ldr.registerExport("user32.dll", "AllowSetForegroundWindow", reinterpret_cast<void*>(AllowSetForegroundWindow));
    ldr.registerExport("user32.dll", "IsWindowUnicode", reinterpret_cast<void*>(IsWindowUnicode));
    ldr.registerExport("user32.dll", "DefMDIChildProcW", reinterpret_cast<void*>(DefMDIChildProcW));
    ldr.registerExport("user32.dll", "GetSystemMenu", reinterpret_cast<void*>(GetSystemMenu));
    ldr.registerExport("user32.dll", "GetCursorInfo", reinterpret_cast<void*>(GetCursorInfo));
    ldr.registerExport("user32.dll", "CharUpperBuffW", reinterpret_cast<void*>(CharUpperBuffW));
    ldr.registerExport("user32.dll", "SetClassLongPtrW", reinterpret_cast<void*>(SetClassLongPtrW));
    ldr.registerExport("user32.dll", "GetClassLongPtrW", reinterpret_cast<void*>(GetClassLongPtrW));
    ldr.registerExport("user32.dll", "GetForegroundWindow", reinterpret_cast<void*>(GetForegroundWindow));
    ldr.registerExport("user32.dll", "MsgWaitForMultipleObjectsEx", reinterpret_cast<void*>(MsgWaitForMultipleObjectsEx));
    ldr.registerExport("user32.dll", "LoadKeyboardLayoutW", reinterpret_cast<void*>(LoadKeyboardLayoutW));
    ldr.registerExport("user32.dll", "GetMenuItemInfoA", reinterpret_cast<void*>(GetMenuItemInfoA));
    ldr.registerExport("user32.dll", "ActivateKeyboardLayout", reinterpret_cast<void*>(ActivateKeyboardLayout));
    ldr.registerExport("user32.dll", "DrawIcon", reinterpret_cast<void*>(DrawIcon));
    ldr.registerExport("user32.dll", "SetKeyboardState", reinterpret_cast<void*>(SetKeyboardState));
    ldr.registerExport("user32.dll", "CreateIcon", reinterpret_cast<void*>(CreateIcon));
    ldr.registerExport("user32.dll", "SubtractRect", reinterpret_cast<void*>(SubtractRect));
    ldr.registerExport("user32.dll", "GetKeyboardLayoutNameW", reinterpret_cast<void*>(GetKeyboardLayoutNameW));
    ldr.registerExport("user32.dll", "CountClipboardFormats", reinterpret_cast<void*>(CountClipboardFormats));
    ldr.registerExport("user32.dll", "CharUpperBuffA", reinterpret_cast<void*>(CharUpperBuffA));
    ldr.registerExport("user32.dll", "CopyIcon", reinterpret_cast<void*>(CopyIcon));
    ldr.registerExport("user32.dll", "IsDialogMessageA", reinterpret_cast<void*>(IsDialogMessageA));
    ldr.registerExport("user32.dll", "GetMenuDefaultItem", reinterpret_cast<void*>(GetMenuDefaultItem));
    ldr.registerExport("user32.dll", "SetGestureConfig", reinterpret_cast<void*>(SetGestureConfig));
    ldr.registerExport("user32.dll", "CloseGestureInfoHandle", reinterpret_cast<void*>(CloseGestureInfoHandle));
    ldr.registerExport("user32.dll", "GetGestureInfo", reinterpret_cast<void*>(GetGestureInfo));
    ldr.registerExport("user32.dll", "SystemParametersInfoForDpi", reinterpret_cast<void*>(SystemParametersInfoForDpi));
    ldr.registerExport("user32.dll", "PhysicalToLogicalPoint", reinterpret_cast<void*>(PhysicalToLogicalPoint));
    ldr.registerExport("user32.dll", "GetSystemMetricsForDpi", reinterpret_cast<void*>(GetSystemMetricsForDpi));
    ldr.registerExport("user32.dll", "GetDpiForWindow", reinterpret_cast<void*>(GetDpiForWindow));
    ldr.registerExport("user32.dll", "AreDpiAwarenessContextsEqual", reinterpret_cast<void*>(AreDpiAwarenessContextsEqual));
    ldr.registerExport("user32.dll", "GetAwarenessFromDpiAwarenessContext", reinterpret_cast<void*>(GetAwarenessFromDpiAwarenessContext));
    ldr.registerExport("user32.dll", "GetWindowDpiAwarenessContext", reinterpret_cast<void*>(GetWindowDpiAwarenessContext));
    ldr.registerExport("user32.dll", "GetThreadDpiAwarenessContext", reinterpret_cast<void*>(GetThreadDpiAwarenessContext));
    ldr.registerExport("user32.dll", "SetThreadDpiAwarenessContext", reinterpret_cast<void*>(SetThreadDpiAwarenessContext));
    ldr.registerExport("user32.dll", "AdjustWindowRectExForDpi", reinterpret_cast<void*>(AdjustWindowRectExForDpi));

    // Horizon 203: Display Topology & Multi-Monitor Mode Enumeration
    ldr.registerExport("user32.dll", "EnumDisplayDevicesW", reinterpret_cast<void*>(EnumDisplayDevicesW));
    ldr.registerExport("user32.dll", "EnumDisplaySettingsW", reinterpret_cast<void*>(EnumDisplaySettingsW));
    ldr.registerExport("user32.dll", "ChangeDisplaySettingsExW", reinterpret_cast<void*>(ChangeDisplaySettingsExW));
    ldr.registerExport("user32.dll", "GetMonitorInfoW", reinterpret_cast<void*>(GetMonitorInfoW));
    ldr.registerExport("user32.dll", "MonitorFromWindow", reinterpret_cast<void*>(MonitorFromWindow));
    ldr.registerExport("user32.dll", "MonitorFromRect", reinterpret_cast<void*>(MonitorFromRect));
    ldr.registerExport("user32.dll", "MonitorFromPoint", reinterpret_cast<void*>(MonitorFromPoint));
}

} // namespace micant::user32
