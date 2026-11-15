// ============================================================================
// MicaNT: Kernel-to-User Subsystem Bridge, Shadow SSDT & Win32k Architecture
// File: include/micant/km/win32k/win32k.hpp
// Sovereign System Domain: TitanWin32k / NexusWin32k
//
// Description:
//   Clean-room implementation of the Microsoft Windows NT Win32k Subsystem
//   (win32k.sys, win32kbase.sys, win32kfull.sys), Shadow System Service
//   Dispatch Table (Shadow SSDT / W32pServiceTable), WindowStation and
//   Desktop Object Hierarchy (\Windows\WindowStations\WinSta0), Dual-Mapped
//   Desktop Heap (8MB) with Read-Only Client Cache Views, Window Hierarchy &
//   Geometry Engine (WND), Thread Message Queue with Priority Message Pump
//   (PostMessage/SendMessage/GetMessage/PeekMessage), Kernel-to-User Trampoline
//   Engine (KeUserModeCallback & NtCallbackReturn), GDI Subsystem Engine with
//   Device Contexts (HDC), Compatible Bitmaps, and BitBlt Raster Operations,
//   and seamless WDDM 3.2 Display Primary Surface Blitting.
//
// Clean-Room Engineering Reference & Standards:
//   - Microsoft Open win32metadata repository: Windows.Win32.UI.WindowsAndMessaging,
//     Windows.Win32.Graphics.Gdi
//   - Microsoft Windows NT Kernel Architecture (Session Manager, Win32k subsystem)
//   - ISO/IEC 14882:2023 C++ Standard
//   - Zero proprietary, decompiled, or leaked Microsoft source code.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <array>
#include <unordered_map>
#include <map>
#include <deque>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <span>
#include <algorithm>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/boot_driver.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"
#if !defined(MICANT_KERNEL_WIN32K_CORE)
#include "micant/um/native/janusldr.hpp"
#include "micant/um/servers/scm.hpp"
#include "micant/um/subsystems/winbase/version.hpp"
#endif
#include "micant/km/core/dispatcher.hpp"
#include "micant/km/hal/bootvid.hpp"

namespace micant::win32k {

// ============================================================================
// 1. Basic Types, Handles, and Calling Conventions
// ============================================================================

using BOOL      = int32_t;
using UINT      = uint32_t;
using DWORD     = uint32_t;
using WORD      = uint16_t;
using BYTE      = uint8_t;
using LONG      = int32_t;
using ULONG     = uint32_t;
using WPARAM    = uintptr_t;
using LPARAM    = intptr_t;
using LRESULT   = intptr_t;
using ATOM      = uint16_t;

using HWND      = void*;
using HDC       = void*;
using HBITMAP   = void*;
using HBRUSH    = void*;
using HPEN      = void*;
using HFONT     = void*;
using HWINSTA   = void*;
using HDESK     = void*;
using HMENU     = void*;
using HICON     = void*;
using HCURSOR   = void*;
using HINSTANCE = void*;
using WNDPROC   = LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM);

inline constexpr BOOL TRUE_VAL  = 1;
inline constexpr BOOL FALSE_VAL = 0;

// ============================================================================
// 2. Syscall Numbers (Shadow SSDT: 0x1000 - 0x1030)
// ============================================================================

inline constexpr uint32_t SSN_NTUSER_CREATEWINDOWEX        = 0x1000;
inline constexpr uint32_t SSN_NTUSER_DESTROYWINDOW          = 0x1001;
inline constexpr uint32_t SSN_NTUSER_SHOWWINDOW             = 0x1002;
inline constexpr uint32_t SSN_NTUSER_SETWINDOWPOS           = 0x1003;
inline constexpr uint32_t SSN_NTUSER_GETMESSAGE             = 0x1004;
inline constexpr uint32_t SSN_NTUSER_PEEKMESSAGE            = 0x1005;
inline constexpr uint32_t SSN_NTUSER_POSTMESSAGE            = 0x1006;
inline constexpr uint32_t SSN_NTUSER_POSTQUITMESSAGE        = 0x1007;
inline constexpr uint32_t SSN_NTUSER_SENDMESSAGE            = 0x1008;
inline constexpr uint32_t SSN_NTUSER_DISPATCHMESSAGE        = 0x1009;
inline constexpr uint32_t SSN_NTUSER_TRANSLATEMESSAGE       = 0x100A;
inline constexpr uint32_t SSN_NTUSER_DEFWINDOWPROC          = 0x100B;
inline constexpr uint32_t SSN_NTUSER_SETTIMER               = 0x100C;
inline constexpr uint32_t SSN_NTUSER_KILLTIMER              = 0x100D;
inline constexpr uint32_t SSN_NTUSER_CREATEWINSTA           = 0x100E;
inline constexpr uint32_t SSN_NTUSER_OPENWINSTA             = 0x100F;
inline constexpr uint32_t SSN_NTUSER_SETPROCWINSTA          = 0x1010;
inline constexpr uint32_t SSN_NTUSER_GETPROCWINSTA          = 0x1011;
inline constexpr uint32_t SSN_NTUSER_CREATEDESKTOP          = 0x1012;
inline constexpr uint32_t SSN_NTUSER_OPENDESKTOP            = 0x1013;
inline constexpr uint32_t SSN_NTUSER_SWITCHDESKTOP          = 0x1014;
inline constexpr uint32_t SSN_NTUSER_CLOSEDESKTOP           = 0x1015;
inline constexpr uint32_t SSN_NTUSER_MAPDESKTOPOBJECT       = 0x1016;
inline constexpr uint32_t SSN_NTUSER_GETDC                  = 0x1017;
inline constexpr uint32_t SSN_NTUSER_RELEASEDC              = 0x1018;
inline constexpr uint32_t SSN_NTGDI_CREATECOMPATIBLEDC      = 0x1019;
inline constexpr uint32_t SSN_NTGDI_CREATECOMPATIBLEBITMAP  = 0x101A;
inline constexpr uint32_t SSN_NTGDI_SELECTBITMAP            = 0x101B;
inline constexpr uint32_t SSN_NTGDI_BITBLT                  = 0x101C;
inline constexpr uint32_t SSN_NTGDI_DELETEOBJECTAPP         = 0x101D;
inline constexpr uint32_t SSN_NTGDI_GETSTOCKOBJECT          = 0x101E;
inline constexpr uint32_t SSN_NTUSER_CALLNOPARAM            = 0x101F;
inline constexpr uint32_t SSN_NTUSER_CALLONEPARAM           = 0x1020;
inline constexpr uint32_t SSN_NTCALLBACKRETURN              = 0x1021;

// ============================================================================
// 3. Win32 Window Messages & Styles Constants
// ============================================================================

inline constexpr UINT WM_NULL           = 0x0000;
inline constexpr UINT WM_CREATE         = 0x0001;
inline constexpr UINT WM_DESTROY        = 0x0002;
inline constexpr UINT WM_NCDESTROY      = 0x0082;
inline constexpr UINT WM_MOVE           = 0x0003;
inline constexpr UINT WM_SIZE           = 0x0005;
inline constexpr UINT WM_ACTIVATE       = 0x0006;
inline constexpr WPARAM SIZE_RESTORED   = 0;
inline constexpr WPARAM SIZE_MINIMIZED  = 1;
inline constexpr WPARAM SIZE_MAXIMIZED  = 2;
inline constexpr UINT WM_SETFOCUS       = 0x0007;
inline constexpr UINT WM_KILLFOCUS      = 0x0008;
inline constexpr UINT WM_ENABLE         = 0x000A;
inline constexpr UINT WM_SETTEXT        = 0x000C;
inline constexpr UINT WM_GETTEXT        = 0x000D;
inline constexpr UINT WM_GETTEXTLENGTH  = 0x000E;
inline constexpr UINT WM_PAINT          = 0x000F;
inline constexpr UINT WM_CLOSE          = 0x0010;
inline constexpr UINT WM_QUIT           = 0x0012;
inline constexpr UINT WM_ERASEBKGND     = 0x0014;
inline constexpr UINT WM_SHOWWINDOW     = 0x0018;
inline constexpr UINT WM_NCHITTEST       = 0x0084;
inline constexpr UINT WM_COMMAND        = 0x0111;
inline constexpr UINT WM_SYSCOMMAND     = 0x0112;
inline constexpr UINT WM_TIMER          = 0x0113;
inline constexpr UINT WM_HSCROLL        = 0x0114;
inline constexpr UINT WM_VSCROLL        = 0x0115;
inline constexpr UINT WM_KEYDOWN        = 0x0100;
inline constexpr UINT WM_KEYUP          = 0x0101;
inline constexpr UINT WM_CHAR           = 0x0102;
inline constexpr UINT WM_MOUSEMOVE      = 0x0200;
inline constexpr UINT WM_LBUTTONDOWN    = 0x0201;
inline constexpr UINT WM_LBUTTONUP      = 0x0202;
inline constexpr UINT WM_RBUTTONDOWN    = 0x0204;
inline constexpr UINT WM_RBUTTONUP      = 0x0205;
inline constexpr UINT WM_USER           = 0x0400;
inline constexpr UINT WM_APP            = 0x8000;

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

// SetWindowPos Flags
inline constexpr UINT SWP_NOSIZE        = 0x0001;
inline constexpr UINT SWP_NOMOVE        = 0x0002;
inline constexpr UINT SWP_NOZORDER      = 0x0004;
inline constexpr UINT SWP_NOREDRAW      = 0x0008;
inline constexpr UINT SWP_NOACTIVATE    = 0x0010;
inline constexpr UINT SWP_FRAMECHANGED  = 0x0020;
inline constexpr UINT SWP_SHOWWINDOW    = 0x0040;
inline constexpr UINT SWP_HIDEWINDOW    = 0x0080;
inline constexpr UINT SWP_NOCOPYBITS    = 0x0100;
inline constexpr UINT SWP_NOOWNERZORDER = 0x0200;
inline constexpr UINT SWP_NOSENDCHANGING= 0x0400;

// Special HWND Z-Order Values
inline const HWND HWND_TOP        = reinterpret_cast<HWND>(static_cast<uintptr_t>(0));
inline const HWND HWND_BOTTOM     = reinterpret_cast<HWND>(static_cast<uintptr_t>(1));
inline const HWND HWND_TOPMOST    = reinterpret_cast<HWND>(static_cast<uintptr_t>(-1));
inline const HWND HWND_NOTOPMOST  = reinterpret_cast<HWND>(static_cast<uintptr_t>(-2));

// Window Styles
inline constexpr DWORD WS_OVERLAPPED       = 0x00000000;
inline constexpr DWORD WS_POPUP            = 0x80000000;
inline constexpr DWORD WS_CHILD            = 0x40000000;
inline constexpr DWORD WS_MINIMIZE         = 0x20000000;
inline constexpr DWORD WS_VISIBLE          = 0x10000000;
inline constexpr DWORD WS_DISABLED         = 0x08000000;
inline constexpr DWORD WS_CLIPSIBLINGS     = 0x04000000;
inline constexpr DWORD WS_CLIPCHILDREN     = 0x02000000;
inline constexpr DWORD WS_MAXIMIZE         = 0x01000000;
inline constexpr DWORD WS_CAPTION          = 0x00C00000;
inline constexpr DWORD WS_BORDER           = 0x00800000;
inline constexpr DWORD WS_DLGFRAME         = 0x00400000;
inline constexpr DWORD WS_VSCROLL          = 0x00200000;
inline constexpr DWORD WS_HSCROLL          = 0x00100000;
inline constexpr DWORD WS_SYSMENU          = 0x00080000;
inline constexpr DWORD WS_THICKFRAME       = 0x00040000;
inline constexpr DWORD WS_MINIMIZEBOX      = 0x00020000;
inline constexpr DWORD WS_MAXIMIZEBOX      = 0x00010000;
inline constexpr DWORD WS_TILED            = WS_OVERLAPPED;
inline constexpr DWORD WS_ICONIC           = WS_MINIMIZE;
inline constexpr DWORD WS_SIZEBOX          = WS_THICKFRAME;
inline constexpr DWORD WS_OVERLAPPEDWINDOW = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
inline constexpr DWORD WS_TILEDWINDOW      = WS_OVERLAPPEDWINDOW;
inline constexpr DWORD WS_POPUPWINDOW      = (WS_POPUP | WS_BORDER | WS_SYSMENU);
inline constexpr DWORD WS_CHILDWINDOW      = WS_CHILD;

// Extended Window Styles
inline constexpr DWORD WS_EX_DLGMODALFRAME    = 0x00000001;
inline constexpr DWORD WS_EX_NOPARENTNOTIFY    = 0x00000004;
inline constexpr DWORD WS_EX_TOPMOST          = 0x00000008;
inline constexpr DWORD WS_EX_ACCEPTFILES      = 0x00000010;
inline constexpr DWORD WS_EX_TRANSPARENT      = 0x00000020;
inline constexpr DWORD WS_EX_MDICHILD         = 0x00000040;
inline constexpr DWORD WS_EX_TOOLWINDOW       = 0x00000080;
inline constexpr DWORD WS_EX_WINDOWEDGE       = 0x00000100;
inline constexpr DWORD WS_EX_CLIENTEDGE       = 0x00000200;
inline constexpr DWORD WS_EX_CONTEXTHELP      = 0x00000400;
inline constexpr DWORD WS_EX_RIGHT            = 0x00001000;
inline constexpr DWORD WS_EX_LEFT             = 0x00000000;
inline constexpr DWORD WS_EX_RTLREADING       = 0x00002000;
inline constexpr DWORD WS_EX_LTRREADING       = 0x00000000;
inline constexpr DWORD WS_EX_LEFTSCROLLBAR    = 0x00004000;
inline constexpr DWORD WS_EX_RIGHTSCROLLBAR   = 0x00000000;
inline constexpr DWORD WS_EX_CONTROLPARENT    = 0x00010000;
inline constexpr DWORD WS_EX_STATICEDGE       = 0x00020000;
inline constexpr DWORD WS_EX_APPWINDOW        = 0x00040000;
inline constexpr DWORD WS_EX_OVERLAPPEDWINDOW = (WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);
inline constexpr DWORD WS_EX_PALETTEWINDOW    = (WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
inline constexpr DWORD WS_EX_LAYERED          = 0x00080000;
inline constexpr DWORD WS_EX_COMPOSITED       = 0x02000000;

// Raster Operations (GDI ROP Codes)
inline constexpr DWORD SRCCOPY     = 0x00CC0020;
inline constexpr DWORD SRCPAINT    = 0x00EE0086;
inline constexpr DWORD SRCAND      = 0x008800C6;
inline constexpr DWORD SRCINVERT   = 0x00660046;
inline constexpr DWORD SRCERASE    = 0x00440328;
inline constexpr DWORD NOTSRCCOPY  = 0x00330008;
inline constexpr DWORD NOTSRCERASE = 0x001100A6;
inline constexpr DWORD MERGECOPY   = 0x00C000CA;
inline constexpr DWORD MERGEPAINT  = 0x00BB0226;
inline constexpr DWORD PATCOPY     = 0x00F00021;
inline constexpr DWORD PATPAINT    = 0x00FB0A09;
inline constexpr DWORD PATINVERT   = 0x005A0049;
inline constexpr DWORD DSTINVERT   = 0x00550009;
inline constexpr DWORD BLACKNESS   = 0x00000042;
inline constexpr DWORD WHITENESS   = 0x00FF0062;

// GDI Stock Objects
inline constexpr int WHITE_BRUSH      = 0;
inline constexpr int LTGRAY_BRUSH     = 1;
inline constexpr int GRAY_BRUSH       = 2;
inline constexpr int DKGRAY_BRUSH     = 3;
inline constexpr int BLACK_BRUSH      = 4;
inline constexpr int NULL_BRUSH       = 5;
inline constexpr int HOLLOW_BRUSH     = 5;
inline constexpr int WHITE_PEN        = 6;
inline constexpr int BLACK_PEN        = 7;
inline constexpr int NULL_PEN         = 8;
inline constexpr int SYSTEM_FONT      = 13;
inline constexpr int DEFAULT_GUI_FONT = 17;

// Queue Status & PeekMessage Flags
inline constexpr UINT QS_KEY             = 0x0001;
inline constexpr UINT QS_MOUSEMOVE       = 0x0002;
inline constexpr UINT QS_MOUSEBUTTON     = 0x0004;
inline constexpr UINT QS_POSTMESSAGE     = 0x0008;
inline constexpr UINT QS_TIMER           = 0x0010;
inline constexpr UINT QS_PAINT           = 0x0020;
inline constexpr UINT QS_SENDMESSAGE     = 0x0040;
inline constexpr UINT QS_HOTKEY          = 0x0080;
inline constexpr UINT QS_ALLPOSTMESSAGE  = 0x0100;
inline constexpr UINT QS_MOUSE           = (QS_MOUSEMOVE | QS_MOUSEBUTTON);
inline constexpr UINT QS_INPUT           = (QS_MOUSE | QS_KEY);
inline constexpr UINT QS_ALLEVENTS       = (QS_INPUT | QS_POSTMESSAGE | QS_TIMER | QS_PAINT | QS_HOTKEY);
inline constexpr UINT QS_ALLINPUT        = (QS_INPUT | QS_POSTMESSAGE | QS_TIMER | QS_PAINT | QS_HOTKEY | QS_SENDMESSAGE);

inline constexpr UINT PM_NOREMOVE        = 0x0000;
inline constexpr UINT PM_REMOVE          = 0x0001;
inline constexpr UINT PM_NOYIELD         = 0x0002;

// ============================================================================
// 4. Fundamental Structures
// ============================================================================

struct POINT {
    int32_t x{0};
    int32_t y{0};
};

struct RECT {
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};

    [[nodiscard]] constexpr int32_t width() const noexcept { return right - left; }
    [[nodiscard]] constexpr int32_t height() const noexcept { return bottom - top; }
    [[nodiscard]] constexpr bool empty() const noexcept { return right <= left || bottom <= top; }
    [[nodiscard]] constexpr bool contains(int32_t px, int32_t py) const noexcept {
        return px >= left && px < right && py >= top && py < bottom;
    }
};

struct MSG {
    HWND     hwnd{nullptr};
    UINT     message{0};
    WPARAM   wParam{0};
    LPARAM   lParam{0};
    uint32_t time{0};
    POINT    pt{0, 0};
};

struct PAINTSTRUCT {
    HDC     hdc{nullptr};
    BOOL    fErase{0};
    RECT    rcPaint{0, 0, 0, 0};
    BOOL    fRestore{0};
    BOOL    fIncUpdate{0};
    uint8_t rgbReserved[32]{};
};

struct CLIENT_WND_INFO {
    HWND     hwnd{nullptr};
    RECT     rcWindow{0, 0, 0, 0};
    RECT     rcClient{0, 0, 0, 0};
    uint32_t style{0};
    uint32_t exStyle{0};
    uint32_t threadId{0};
    uint32_t processId{0};
    bool     isVisible{false};
    bool     isEnabled{true};
    uint64_t heapOffset{0};
};

// ============================================================================
// 5. Desktop Heap Architecture (8MB Default Session Pool)
// ============================================================================

class DesktopHeap {
public:
    static constexpr size_t DEFAULT_DESKTOP_HEAP_SIZE = 8 * 1024 * 1024; // 8 Megabytes

    explicit DesktopHeap(size_t size = DEFAULT_DESKTOP_HEAP_SIZE)
        : m_heapSize(size), m_allocatedBytes(64) {
        m_storage.resize(m_heapSize, 0);
    }

    [[nodiscard]] size_t getHeapSize() const noexcept { return m_heapSize; }
    [[nodiscard]] size_t getAllocatedBytes() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_allocatedBytes;
    }
    [[nodiscard]] size_t getFreeBytes() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return (m_heapSize > m_allocatedBytes) ? (m_heapSize - m_allocatedBytes) : 0;
    }
    [[nodiscard]] size_t getBlockCount() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_clientBlocks.size();
    }

    [[nodiscard]] const void* getClientBaseAddress() const noexcept {
        return m_storage.data();
    }

    uint64_t allocateClientWnd(HWND hwnd, const RECT& rcWindow, const RECT& rcClient,
                              uint32_t style, uint32_t exStyle, uint32_t tid, uint32_t pid) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        size_t allocSize = sizeof(CLIENT_WND_INFO);
        // Align to 16 bytes
        size_t alignedOffset = (m_allocatedBytes + 15) & ~static_cast<size_t>(15);
        if (alignedOffset + allocSize > m_heapSize) {
            return 0; // Heap exhausted
        }

        m_allocatedBytes = alignedOffset + allocSize;
        auto* info = reinterpret_cast<CLIENT_WND_INFO*>(m_storage.data() + alignedOffset);
        info->hwnd = hwnd;
        info->rcWindow = rcWindow;
        info->rcClient = rcClient;
        info->style = style;
        info->exStyle = exStyle;
        info->threadId = tid;
        info->processId = pid;
        info->isVisible = (style & WS_VISIBLE) != 0;
        info->isEnabled = (style & WS_DISABLED) == 0;
        info->heapOffset = alignedOffset;

        m_clientBlocks[hwnd] = alignedOffset;
        return alignedOffset;
    }

    bool updateClientWnd(uint64_t offset, const RECT& rcWindow, const RECT& rcClient,
                         uint32_t style, uint32_t exStyle, bool isVisible) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (offset + sizeof(CLIENT_WND_INFO) > m_heapSize) return false;

        auto* info = reinterpret_cast<CLIENT_WND_INFO*>(m_storage.data() + offset);
        info->rcWindow = rcWindow;
        info->rcClient = rcClient;
        info->style = style;
        info->exStyle = exStyle;
        info->isVisible = isVisible;
        info->isEnabled = (style & WS_DISABLED) == 0;
        return true;
    }

    [[nodiscard]] const CLIENT_WND_INFO* getClientWnd(uint64_t offset) const noexcept {
        if (offset + sizeof(CLIENT_WND_INFO) > m_heapSize) return nullptr;
        return reinterpret_cast<const CLIENT_WND_INFO*>(m_storage.data() + offset);
    }

    [[nodiscard]] uint64_t getOffsetForHwnd(HWND hwnd) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_clientBlocks.find(hwnd);
        if (it != m_clientBlocks.end()) return it->second;
        return 0;
    }

    void freeClientWnd(HWND hwnd) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_clientBlocks.erase(hwnd);
    }

private:
    size_t m_heapSize;
    size_t m_allocatedBytes;
    std::vector<uint8_t> m_storage;
    std::unordered_map<HWND, uint64_t> m_clientBlocks;
    mutable std::recursive_mutex m_mutex;
};

// ============================================================================
// 6. Window Object Structure (WND)
// ============================================================================

struct WND {
    HWND hwnd{nullptr};
    std::wstring className;
    std::wstring windowName;
    std::array<uint16_t, 128> titleUtf16{};
    uint16_t titleUtf16Length{0};
    uint32_t style{0};
    uint32_t exStyle{0};
    RECT rcWindow{0, 0, 0, 0};
    RECT rcClient{0, 0, 0, 0};
    HWND hwndParent{nullptr};
    HWND hwndOwner{nullptr};
    std::vector<HWND> children;
    HWND hwndNextSibling{nullptr};
    HWND hwndPrevSibling{nullptr};
    uint32_t threadId{0};
    uint32_t processId{0};
    void* wndProc{nullptr};
    uint64_t userData{0};
    uint64_t desktopHeapOffset{0};
    bool isVisible{false};
    bool isEnabled{true};
    bool isDestroyed{false};
    bool isMinimized{false};
    bool isMaximized{false};
    RECT restoreRect{0, 0, 0, 0};
    bool isPaintInvalidated{false};
    HDC hdc{nullptr};
    HBITMAP clientBitmap{nullptr};
};

// ============================================================================
// 7. Desktop & WindowStation Architecture
// ============================================================================

class Desktop {
public:
    Desktop(std::wstring name, std::wstring winStaName, HDESK handle)
        : m_name(std::move(name)), m_winStaName(std::move(winStaName)), m_handle(handle) {
        m_heap = std::make_shared<DesktopHeap>();
    }

    [[nodiscard]] const std::wstring& getName() const noexcept { return m_name; }
    [[nodiscard]] const std::wstring& getWinStaName() const noexcept { return m_winStaName; }
    [[nodiscard]] HDESK getHandle() const noexcept { return m_handle; }
    [[nodiscard]] std::shared_ptr<DesktopHeap> getHeap() const noexcept { return m_heap; }

    void addWindowToZOrder(HWND hwnd, HWND insertAfter, bool topmost = false) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (insertAfter == HWND_TOPMOST) topmost = true;
        else if (insertAfter == HWND_NOTOPMOST || insertAfter == HWND_BOTTOM) topmost = false;

        // Remove first if already present
        auto it = std::find(m_zOrder.begin(), m_zOrder.end(), hwnd);
        if (it != m_zOrder.end()) m_zOrder.erase(it);

        auto topmostIt = std::find(m_topmostWindows.begin(), m_topmostWindows.end(), hwnd);
        if (topmost) {
            if (topmostIt == m_topmostWindows.end()) m_topmostWindows.push_back(hwnd);
        } else if (topmostIt != m_topmostWindows.end()) {
            m_topmostWindows.erase(topmostIt);
        }

        if (insertAfter == HWND_BOTTOM) {
            m_zOrder.push_back(hwnd);
        } else if (insertAfter == HWND_TOP || insertAfter == nullptr ||
                   insertAfter == HWND_TOPMOST || insertAfter == HWND_NOTOPMOST) {
            if (topmost) {
                m_zOrder.insert(m_zOrder.begin(), hwnd);
            } else {
                const auto firstNonTopmost = std::find_if(m_zOrder.begin(), m_zOrder.end(),
                    [&](HWND candidate) {
                        return std::find(m_topmostWindows.begin(), m_topmostWindows.end(), candidate) ==
                               m_topmostWindows.end();
                    });
                m_zOrder.insert(firstNonTopmost, hwnd);
            }
        } else {
            auto pos = std::find(m_zOrder.begin(), m_zOrder.end(), insertAfter);
            if (pos != m_zOrder.end()) {
                const bool anchorTopmost = std::find(m_topmostWindows.begin(), m_topmostWindows.end(), insertAfter) !=
                                           m_topmostWindows.end();
                if (topmost && anchorTopmost) {
                    m_zOrder.insert(pos + 1, hwnd);
                } else if (!topmost && anchorTopmost) {
                    const auto firstNonTopmost = std::find_if(m_zOrder.begin(), m_zOrder.end(),
                        [&](HWND candidate) {
                            return std::find(m_topmostWindows.begin(), m_topmostWindows.end(), candidate) ==
                                   m_topmostWindows.end();
                        });
                    m_zOrder.insert(firstNonTopmost, hwnd);
                } else if (topmost) {
                    m_zOrder.insert(m_zOrder.begin(), hwnd);
                } else {
                    m_zOrder.insert(pos + 1, hwnd);
                }
            } else {
                const auto firstNonTopmost = std::find_if(m_zOrder.begin(), m_zOrder.end(),
                    [&](HWND candidate) {
                        return std::find(m_topmostWindows.begin(), m_topmostWindows.end(), candidate) ==
                               m_topmostWindows.end();
                    });
                m_zOrder.insert(topmost ? m_zOrder.begin() : firstNonTopmost, hwnd);
            }
        }
    }

    void removeWindowFromZOrder(HWND hwnd) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = std::find(m_zOrder.begin(), m_zOrder.end(), hwnd);
        if (it != m_zOrder.end()) m_zOrder.erase(it);
        auto topmost = std::find(m_topmostWindows.begin(), m_topmostWindows.end(), hwnd);
        if (topmost != m_topmostWindows.end()) m_topmostWindows.erase(topmost);
        if (m_hwndActive == hwnd) m_hwndActive = nullptr;
        if (m_hwndFocus == hwnd) m_hwndFocus = nullptr;
        if (m_hwndCapture == hwnd) m_hwndCapture = nullptr;
    }

    void bringToTop(HWND hwnd) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        const bool topmost = std::find(m_topmostWindows.begin(), m_topmostWindows.end(), hwnd) !=
                             m_topmostWindows.end();
        addWindowToZOrder(hwnd, HWND_TOP, topmost);
    }

    [[nodiscard]] std::vector<HWND> getZOrder() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_zOrder;
    }

    void setActiveWindow(HWND hwnd) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_hwndActive = hwnd;
    }

    [[nodiscard]] HWND getActiveWindow() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_hwndActive;
    }

    void setFocusWindow(HWND hwnd) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_hwndFocus = hwnd;
    }

    [[nodiscard]] HWND getFocusWindow() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_hwndFocus;
    }

private:
    std::wstring m_name;
    std::wstring m_winStaName;
    HDESK m_handle;
    std::shared_ptr<DesktopHeap> m_heap;
    std::vector<HWND> m_zOrder;
    std::vector<HWND> m_topmostWindows;
    HWND m_hwndActive{nullptr};
    HWND m_hwndFocus{nullptr};
    HWND m_hwndCapture{nullptr};
    mutable std::recursive_mutex m_mutex;
};

class WindowStation {
public:
    WindowStation(std::wstring name, HWINSTA handle, bool interactive)
        : m_name(std::move(name)), m_handle(handle), m_interactive(interactive) {}

    [[nodiscard]] const std::wstring& getName() const noexcept { return m_name; }
    [[nodiscard]] HWINSTA getHandle() const noexcept { return m_handle; }
    [[nodiscard]] bool isInteractive() const noexcept { return m_interactive; }

    std::shared_ptr<Desktop> createDesktop(const std::wstring& deskName, HDESK hDesk) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_desktops.find(deskName);
        if (it != m_desktops.end()) return it->second;

        auto desk = std::make_shared<Desktop>(deskName, m_name, hDesk);
        m_desktops[deskName] = desk;
        if (!m_activeDesktop) {
            m_activeDesktop = desk;
        }
        return desk;
    }

    std::shared_ptr<Desktop> openDesktop(const std::wstring& deskName) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_desktops.find(deskName);
        if (it != m_desktops.end()) return it->second;
        return nullptr;
    }

    bool switchDesktop(const std::wstring& deskName) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_desktops.find(deskName);
        if (it == m_desktops.end()) return false;
        m_activeDesktop = it->second;
        return true;
    }

    [[nodiscard]] std::shared_ptr<Desktop> getActiveDesktop() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_activeDesktop;
    }

    [[nodiscard]] size_t getDesktopCount() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_desktops.size();
    }

private:
    std::wstring m_name;
    HWINSTA m_handle;
    bool m_interactive;
    std::unordered_map<std::wstring, std::shared_ptr<Desktop>> m_desktops;
    std::shared_ptr<Desktop> m_activeDesktop;
    mutable std::recursive_mutex m_mutex;
};

// ============================================================================
// 8. Thread Message Queue & Priority Message Pump
// ============================================================================

struct SENT_MESSAGE_ENTRY {
    uint64_t msgId{0};
    HWND     hwnd{nullptr};
    UINT     message{0};
    WPARAM   wParam{0};
    LPARAM   lParam{0};
    uint32_t senderThreadId{0};
    bool     completed{false};
    LRESULT  result{0};
};

struct TIMER_ENTRY {
    HWND      hwnd{nullptr};
    uintptr_t timerId{0};
    uint32_t  intervalMs{0};
    uint64_t  lastTickMs{0};
    void*     timerProc{nullptr};
    bool      active{true};
};

class ThreadMessageQueue {
public:
    explicit ThreadMessageQueue(uint32_t threadId)
        : m_threadId(threadId), m_wakeMask(0) {}

    [[nodiscard]] uint32_t getThreadId() const noexcept { return m_threadId; }
    [[nodiscard]] UINT getWakeMask() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_wakeMask;
    }

    void postMessage(const MSG& msg) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_postedQueue.push_back(msg);
        m_wakeMask |= (msg.message == WM_QUIT) ? QS_ALLPOSTMESSAGE : QS_POSTMESSAGE;
    }

    std::shared_ptr<SENT_MESSAGE_ENTRY> queueSentMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, uint32_t senderTid) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        static std::atomic<uint64_t> s_nextMsgId{1};
        auto entry = std::make_shared<SENT_MESSAGE_ENTRY>();
        entry->msgId = s_nextMsgId++;
        entry->hwnd = hwnd;
        entry->message = message;
        entry->wParam = wParam;
        entry->lParam = lParam;
        entry->senderThreadId = senderTid;
        entry->completed = false;
        entry->result = 0;

        m_sentQueue.push_back(entry);
        m_wakeMask |= QS_SENDMESSAGE;
        return entry;
    }

    std::shared_ptr<SENT_MESSAGE_ENTRY> popSentMessage() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_sentQueue.empty()) return nullptr;
        auto entry = m_sentQueue.front();
        m_sentQueue.pop_front();
        if (m_sentQueue.empty()) m_wakeMask &= ~QS_SENDMESSAGE;
        return entry;
    }

    bool peekMessage(MSG& outMsg, HWND hwndFilter, UINT msgMin, UINT msgMax, UINT removeFlag) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        // 1. Check posted messages
        for (auto it = m_postedQueue.begin(); it != m_postedQueue.end(); ++it) {
            if (hwndFilter && it->hwnd != hwndFilter) continue;
            if (msgMin != 0 || msgMax != 0) {
                if (it->message < msgMin || it->message > msgMax) continue;
            }
            outMsg = *it;
            if (removeFlag & PM_REMOVE) {
                m_postedQueue.erase(it);
                if (m_postedQueue.empty()) m_wakeMask &= ~(QS_POSTMESSAGE | QS_ALLPOSTMESSAGE);
            }
            return true;
        }

        // 2. Check timers
        uint64_t nowMs = getCurrentTimeMs();
        for (auto& tmr : m_timers) {
            if (!tmr.active) continue;
            if (hwndFilter && tmr.hwnd != hwndFilter) continue;
            if (nowMs - tmr.lastTickMs >= tmr.intervalMs) {
                tmr.lastTickMs = nowMs;
                outMsg.hwnd = tmr.hwnd;
                outMsg.message = WM_TIMER;
                outMsg.wParam = tmr.timerId;
                outMsg.lParam = reinterpret_cast<LPARAM>(tmr.timerProc);
                outMsg.time = static_cast<uint32_t>(nowMs);
                outMsg.pt = {0, 0};
                return true;
            }
        }

        return false;
    }

    BOOL getMessage(MSG& outMsg, HWND hwndFilter, UINT msgMin, UINT msgMax) {
        // In real Win32k, GetMessage blocks until a message arrives.
        // For our testable kernel implementation, if queue is empty, returns FALSE.
        if (peekMessage(outMsg, hwndFilter, msgMin, msgMax, PM_REMOVE)) {
            if (outMsg.message == WM_QUIT) return FALSE_VAL;
            return TRUE_VAL;
        }
        return FALSE_VAL;
    }

    bool setTimer(HWND hwnd, uintptr_t timerId, uint32_t intervalMs, void* timerProc) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (auto& tmr : m_timers) {
            if (tmr.hwnd == hwnd && tmr.timerId == timerId) {
                tmr.intervalMs = intervalMs;
                tmr.lastTickMs = getCurrentTimeMs();
                tmr.timerProc = timerProc;
                tmr.active = true;
                m_wakeMask |= QS_TIMER;
                return true;
            }
        }
        TIMER_ENTRY entry;
        entry.hwnd = hwnd;
        entry.timerId = timerId;
        entry.intervalMs = intervalMs;
        entry.lastTickMs = getCurrentTimeMs();
        entry.timerProc = timerProc;
        entry.active = true;
        m_timers.push_back(entry);
        m_wakeMask |= QS_TIMER;
        return true;
    }

    bool killTimer(HWND hwnd, uintptr_t timerId) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
            if (it->hwnd == hwnd && it->timerId == timerId) {
                m_timers.erase(it);
                if (m_timers.empty()) m_wakeMask &= ~QS_TIMER;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] size_t getPostedCount() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_postedQueue.size();
    }

    [[nodiscard]] size_t getSentCount() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_sentQueue.size();
    }

    [[nodiscard]] size_t getTimerCount() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_timers.size();
    }

private:
    static uint64_t getCurrentTimeMs() noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    uint32_t m_threadId;
    UINT m_wakeMask;
    std::deque<MSG> m_postedQueue;
    std::deque<std::shared_ptr<SENT_MESSAGE_ENTRY>> m_sentQueue;
    std::vector<TIMER_ENTRY> m_timers;
    mutable std::recursive_mutex m_mutex;
};

// ============================================================================
// 9. Kernel-to-User Trampoline Engine (KeUserModeCallback & NtCallbackReturn)
// ============================================================================

struct CALLBACK_FRAME {
    uint64_t callbackId{0};
    uint32_t apiIndex{0};
    std::vector<uint8_t> argumentData;
    std::vector<uint8_t> outputData;
    NtStatus returnStatus{NtStatus::Success};
    bool     completed{false};
    LRESULT  userResult{0};
};

using UserModeDispatcherFn = LRESULT (*)(uint32_t apiIndex, const void* args, uint32_t length);

class UserModeCallbackEngine {
public:
    UserModeCallbackEngine() = default;

    void registerUserModeDispatcher(UserModeDispatcherFn dispatcher) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_dispatcher = dispatcher;
    }

    [[nodiscard]] bool hasDispatcher() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_dispatcher != nullptr;
    }

    NtStatus invokeUserModeCallback(uint32_t apiIndex, const void* inputBuffer, uint32_t inputLength,
                                   void** outputBuffer, uint32_t* outputLength, LRESULT* outResult) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        static std::atomic<uint64_t> s_nextCallbackId{1};
        uint64_t cbId = s_nextCallbackId++;

        auto frame = std::make_shared<CALLBACK_FRAME>();
        frame->callbackId = cbId;
        frame->apiIndex = apiIndex;
        if (inputBuffer && inputLength > 0) {
            frame->argumentData.resize(inputLength);
            std::memcpy(frame->argumentData.data(), inputBuffer, inputLength);
        }

        m_inFlightCallbacks[cbId] = frame;

        if (m_dispatcher) {
            // Invoke the user-mode callback dispatcher thunk
            LRESULT res = m_dispatcher(apiIndex, frame->argumentData.data(), inputLength);
            frame->userResult = res;
            frame->completed = true;
            if (outResult) *outResult = res;
            if (outputBuffer && outputLength && !frame->outputData.empty()) {
                *outputBuffer = frame->outputData.data();
                *outputLength = static_cast<uint32_t>(frame->outputData.size());
            }
            m_totalCallbacks++;
            m_inFlightCallbacks.erase(cbId);
            return NtStatus::Success;
        }

        // If no user-mode dispatcher is hooked, return success with default 0
        frame->completed = true;
        if (outResult) *outResult = 0;
        m_totalCallbacks++;
        m_inFlightCallbacks.erase(cbId);
        return NtStatus::Success;
    }

    NtStatus callbackReturn(uint64_t callbackId, const void* resultBuffer, uint32_t resultLength,
                           NtStatus status, LRESULT resultVal) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_inFlightCallbacks.find(callbackId);
        if (it == m_inFlightCallbacks.end()) {
            return NtStatus::InvalidParameter;
        }

        auto frame = it->second;
        frame->returnStatus = status;
        frame->userResult = resultVal;
        if (resultBuffer && resultLength > 0) {
            frame->outputData.resize(resultLength);
            std::memcpy(frame->outputData.data(), resultBuffer, resultLength);
        }
        frame->completed = true;
        m_inFlightCallbacks.erase(it);
        return NtStatus::Success;
    }

    [[nodiscard]] uint64_t getTotalCallbacks() const noexcept {
        return m_totalCallbacks.load(std::memory_order_relaxed);
    }

private:
    UserModeDispatcherFn m_dispatcher{nullptr};
    std::unordered_map<uint64_t, std::shared_ptr<CALLBACK_FRAME>> m_inFlightCallbacks;
    std::atomic<uint64_t> m_totalCallbacks{0};
    mutable std::recursive_mutex m_mutex;
};

// ============================================================================
// 10. GDI Subsystem Engine (Bitmaps, DCs & Raster Operations)
// ============================================================================

struct GdiBitmap {
    HBITMAP handle{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t bpp{32};
    uint32_t stride{0};
    std::vector<uint32_t> pixels; // 32-bit ARGB/XRGB

    void clear(uint32_t color) {
        std::fill(pixels.begin(), pixels.end(), color);
    }

    [[nodiscard]] uint32_t getPixel(uint32_t x, uint32_t y) const noexcept {
        if (x >= width || y >= height) return 0;
        return pixels[y * width + x];
    }

    void setPixel(uint32_t x, uint32_t y, uint32_t color) noexcept {
        if (x >= width || y >= height) return;
        pixels[y * width + x] = color;
    }
};

struct DesktopCompositionLayer {
    micant::boot_driver::CompositionLayer descriptor{};
    std::shared_ptr<GdiBitmap> surface;
};

struct GdiDeviceContext {
    HDC     handle{nullptr};
    bool    isMemoryDC{false};
    bool    isScreenDC{false};
    HBITMAP selectedBitmap{nullptr};
    HBRUSH  selectedBrush{nullptr};
    HPEN    selectedPen{nullptr};
    POINT   currentPos{0, 0};
    uint32_t textColor{0x00000000};
    uint32_t bkColor{0x00FFFFFF};
};

class GdiEngine {
public:
    GdiEngine() {
        initializeStockObjects();
        uint32_t width = 1024;
        uint32_t height = 768;
        struct VideoInfo {
            uint32_t width, height, pitch, bpp, pixelFormat, reserved;
            uint64_t physicalBase;
        } info{width, height, width, 32, 1, 0, 0};
#if defined(__x86_64__)
        uint64_t status = 1;
        __asm__ __volatile__("movq %1, %%r10; movl $0x80, %%eax; syscall"
                             : "=a"(status) : "r"(&info)
                             : "r10", "rcx", "r11", "memory");
        if (status == 0 && info.width != 0 && info.height != 0 &&
            info.width <= 16384 && info.height <= 16384) {
            width = info.width;
            height = info.height;
        }
#endif
        createScreenDC(width, height);
    }

    HDC createScreenDC(uint32_t width = 1024, uint32_t height = 768) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_screenDC) return m_screenDC->handle;

        static uintptr_t s_nextHdc = 0x5000;
        auto dc = std::make_shared<GdiDeviceContext>();
        dc->handle = reinterpret_cast<HDC>(s_nextHdc++);
        dc->isScreenDC = true;
        dc->isMemoryDC = false;
        m_dcs[dc->handle] = dc;
        m_screenDC = dc;

        // Match the physical GOP mode; allocating a larger virtual surface
        // wastes process heap and clips presentation at the real display size.
        auto bmp = createBitmapInternal(width, height, 32);
        bmp->clear(0x001E1E1E); // Default sovereign dark background
        dc->selectedBitmap = bmp->handle;

        return dc->handle;
    }

    [[nodiscard]] HDC getScreenDC() const noexcept {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_screenDC ? m_screenDC->handle : nullptr;
    }

    HDC createCompatibleDC(HDC hdcRef) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        static uintptr_t s_nextHdc = 0x6000;
        auto dc = std::make_shared<GdiDeviceContext>();
        dc->handle = reinterpret_cast<HDC>(s_nextHdc++);
        dc->isMemoryDC = true;
        dc->isScreenDC = false;
        m_dcs[dc->handle] = dc;

        if (hdcRef) {
            auto it = m_dcs.find(hdcRef);
            if (it != m_dcs.end()) {
                dc->textColor = it->second->textColor;
                dc->bkColor = it->second->bkColor;
            }
        }
        return dc->handle;
    }

    HBITMAP createCompatibleBitmap(HDC /*hdc*/, uint32_t width, uint32_t height) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto bmp = createBitmapInternal(width, height, 32);
        return bmp ? bmp->handle : nullptr;
    }

    void* selectObject(HDC hdc, void* hGdiObj) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto itDc = m_dcs.find(hdc);
        if (itDc == m_dcs.end()) return nullptr;

        // Check if object is a bitmap
        auto itBmp = m_bitmaps.find(reinterpret_cast<HBITMAP>(hGdiObj));
        if (itBmp != m_bitmaps.end()) {
            HBITMAP oldBmp = itDc->second->selectedBitmap;
            itDc->second->selectedBitmap = reinterpret_cast<HBITMAP>(hGdiObj);
            return oldBmp;
        }

        // Stock or general brush/pen
        itDc->second->selectedBrush = reinterpret_cast<HBRUSH>(hGdiObj);
        return hGdiObj;
    }

    BOOL deleteObject(void* hGdiObj) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto itBmp = m_bitmaps.find(reinterpret_cast<HBITMAP>(hGdiObj));
        if (itBmp != m_bitmaps.end()) {
            m_bitmaps.erase(itBmp);
            return TRUE_VAL;
        }
        return TRUE_VAL;
    }

    BOOL releaseDC(HDC hdc) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_dcs.find(hdc);
        if (it != m_dcs.end() && it->second->isMemoryDC) {
            m_dcs.erase(it);
            return TRUE_VAL;
        }
        return TRUE_VAL;
    }

    void* getStockObject(int index) const {
        auto it = m_stockObjects.find(index);
        if (it != m_stockObjects.end()) return it->second;
        return nullptr;
    }

    BOOL bitBlt(HDC hdcDest, int32_t nXDest, int32_t nYDest, int32_t nWidth, int32_t nHeight,
                HDC hdcSrc, int32_t nXSrc, int32_t nYSrc, DWORD dwRop) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto itDest = m_dcs.find(hdcDest);
        if (itDest == m_dcs.end()) return FALSE_VAL;

        auto itDestBmp = m_bitmaps.find(itDest->second->selectedBitmap);
        if (itDestBmp == m_bitmaps.end()) return FALSE_VAL;
        auto& destBmp = itDestBmp->second;

        std::shared_ptr<GdiBitmap> srcBmp{nullptr};
        if (hdcSrc) {
            auto itSrc = m_dcs.find(hdcSrc);
            if (itSrc != m_dcs.end()) {
                auto itSrcBmp = m_bitmaps.find(itSrc->second->selectedBitmap);
                if (itSrcBmp != m_bitmaps.end()) {
                    srcBmp = itSrcBmp->second;
                }
            }
        }

        for (int32_t y = 0; y < nHeight; ++y) {
            int32_t dy = nYDest + y;
            int32_t sy = nYSrc + y;
            if (dy < 0 || static_cast<uint32_t>(dy) >= destBmp->height) continue;

            for (int32_t x = 0; x < nWidth; ++x) {
                int32_t dx = nXDest + x;
                int32_t sx = nXSrc + x;
                if (dx < 0 || static_cast<uint32_t>(dx) >= destBmp->width) continue;

                uint32_t dPixel = destBmp->getPixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy));
                uint32_t sPixel = (srcBmp && sy >= 0 && static_cast<uint32_t>(sy) < srcBmp->height &&
                                   sx >= 0 && static_cast<uint32_t>(sx) < srcBmp->width)
                                      ? srcBmp->getPixel(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy))
                                      : 0;

                uint32_t finalPixel = 0;
                switch (dwRop) {
                    case SRCCOPY:     finalPixel = sPixel; break;
                    case SRCPAINT:    finalPixel = dPixel | sPixel; break;
                    case SRCAND:      finalPixel = dPixel & sPixel; break;
                    case SRCINVERT:   finalPixel = dPixel ^ sPixel; break;
                    case SRCERASE:    finalPixel = sPixel & ~dPixel; break;
                    case NOTSRCCOPY:  finalPixel = ~sPixel; break;
                    case BLACKNESS:   finalPixel = 0x00000000; break;
                    case WHITENESS:   finalPixel = 0x00FFFFFF; break;
                    case DSTINVERT:   finalPixel = ~dPixel; break;
                    default:          finalPixel = sPixel; break;
                }
                destBmp->setPixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy), finalPixel);
            }
        }

        m_totalBitBlts++;
        return TRUE_VAL;
    }

    std::shared_ptr<GdiBitmap> getBitmap(HBITMAP hBitmap) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_bitmaps.find(hBitmap);
        if (it != m_bitmaps.end()) return it->second;
        return nullptr;
    }

    std::shared_ptr<GdiBitmap> getBitmapForDC(HDC hdc) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto dc = m_dcs.find(hdc);
        if (dc == m_dcs.end() || !dc->second->selectedBitmap) return nullptr;
        auto bitmap = m_bitmaps.find(dc->second->selectedBitmap);
        return bitmap == m_bitmaps.end() ? nullptr : bitmap->second;
    }

    [[nodiscard]] uint64_t getTotalBitBlts() const noexcept {
        return m_totalBitBlts.load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<GdiBitmap> createBitmapInternal(uint32_t width, uint32_t height, uint32_t bpp) {
        static uintptr_t s_nextHbmp = 0x7000;
        auto bmp = std::make_shared<GdiBitmap>();
        bmp->handle = reinterpret_cast<HBITMAP>(s_nextHbmp++);
        bmp->width = width;
        bmp->height = height;
        bmp->bpp = bpp;
        bmp->stride = width * (bpp / 8);
        bmp->pixels.resize(width * height, 0xFF000000); // Default opaque black
        m_bitmaps[bmp->handle] = bmp;
        return bmp;
    }

    void initializeStockObjects() {
        m_stockObjects[WHITE_BRUSH]      = reinterpret_cast<void*>(0x8000);
        m_stockObjects[LTGRAY_BRUSH]     = reinterpret_cast<void*>(0x8001);
        m_stockObjects[GRAY_BRUSH]       = reinterpret_cast<void*>(0x8002);
        m_stockObjects[DKGRAY_BRUSH]     = reinterpret_cast<void*>(0x8003);
        m_stockObjects[BLACK_BRUSH]      = reinterpret_cast<void*>(0x8004);
        m_stockObjects[NULL_BRUSH]       = reinterpret_cast<void*>(0x8005);
        m_stockObjects[WHITE_PEN]        = reinterpret_cast<void*>(0x8006);
        m_stockObjects[BLACK_PEN]        = reinterpret_cast<void*>(0x8007);
        m_stockObjects[NULL_PEN]         = reinterpret_cast<void*>(0x8008);
        m_stockObjects[SYSTEM_FONT]      = reinterpret_cast<void*>(0x800D);
        m_stockObjects[DEFAULT_GUI_FONT] = reinterpret_cast<void*>(0x8011);
    }

    std::shared_ptr<GdiDeviceContext> m_screenDC;
    std::unordered_map<HDC, std::shared_ptr<GdiDeviceContext>> m_dcs;
    std::unordered_map<HBITMAP, std::shared_ptr<GdiBitmap>> m_bitmaps;
    std::unordered_map<int, void*> m_stockObjects;
    std::atomic<uint64_t> m_totalBitBlts{0};
    mutable std::recursive_mutex m_mutex;
};

// ============================================================================
// 11. Diagnostic Telemetry Structure
// ============================================================================

struct WIN32K_TELEMETRY {
    uint64_t totalSyscallsHandled{0};
    uint64_t totalWindowsCreated{0};
    uint64_t totalWindowsDestroyed{0};
    uint64_t totalMessagesPosted{0};
    uint64_t totalMessagesSent{0};
    uint64_t totalCallbacksInvoked{0};
    uint64_t totalBitBltsExecuted{0};
    uint64_t totalHeapBytesAllocated{0};
    uint32_t activeDesktopsCount{0};
    uint32_t activeWindowStationsCount{0};
    uint32_t activeWindowsCount{0};
};

// ============================================================================
// 12. Sovereign Win32k Central Platform Subsystem (TitanWin32k)
// ============================================================================

class TitanWin32kSubsystem {
public:
    static TitanWin32kSubsystem& Instance() {
        static TitanWin32kSubsystem s_instance;
        return s_instance;
    }

    void initialize() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        initializeInternal();
    }

    void initializeInternal() {
        if (m_initialized) return;

        // 1. Create interactive WindowStation WinSta0
        auto winSta0 = std::make_shared<WindowStation>(L"WinSta0", reinterpret_cast<HWINSTA>(0x100), true);
        m_windowStations[L"WinSta0"] = winSta0;
        m_currentWinSta = winSta0;

        // 2. Create standard desktops: Default, Winlogon, Screen-saver
        auto dDefault = winSta0->createDesktop(L"Default", reinterpret_cast<HDESK>(0x200));
        winSta0->createDesktop(L"Winlogon", reinterpret_cast<HDESK>(0x201));
        winSta0->createDesktop(L"Screen-saver", reinterpret_cast<HDESK>(0x202));
        winSta0->switchDesktop(L"Default");
        m_currentDesktop = dDefault;

        // The standard control classes exist before application startup.
        for (const wchar_t* name : {L"STATIC",L"BUTTON",L"EDIT",L"LISTBOX",L"COMBOBOX",L"SCROLLBAR"}) {
            const ATOM atom=m_nextWindowClassAtom++;
            m_windowClasses.emplace(name,&BuiltInWindowProcedure);
            m_windowClassAtoms.emplace(name,atom);
        }

        m_initialized = true;
    }

    void resetForTesting() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_windows.clear();
        m_threadQueues.clear();
        m_windowStations.clear();
        m_currentWinSta.reset();
        m_currentDesktop.reset();
        m_totalSyscalls = 0;
        m_totalWindowsCreated = 0;
        m_totalWindowsDestroyed = 0;
        m_totalMessagesPosted = 0;
        m_totalMessagesSent = 0;
        m_initialized = false;
        initializeInternal();
    }

    // --- Window Station & Desktop Management ---

    HWINSTA createWindowStation(const std::wstring& name, bool interactive) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        static uintptr_t s_nextWinSta = 0x110;
        auto handle = reinterpret_cast<HWINSTA>(s_nextWinSta++);
        auto winSta = std::make_shared<WindowStation>(name, handle, interactive);
        m_windowStations[name] = winSta;
        return handle;
    }

    HWINSTA openWindowStation(const std::wstring& name) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windowStations.find(name);
        if (it != m_windowStations.end()) return it->second->getHandle();
        return nullptr;
    }

    bool setProcessWindowStation(HWINSTA hWinSta) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const auto& [name, ws] : m_windowStations) {
            if (ws->getHandle() == hWinSta) {
                m_currentWinSta = ws;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] HWINSTA getProcessWindowStation() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_currentWinSta ? m_currentWinSta->getHandle() : nullptr;
    }

    HDESK createDesktop(const std::wstring& name) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_currentWinSta) return nullptr;
        static uintptr_t s_nextDesk = 0x210;
        auto handle = reinterpret_cast<HDESK>(s_nextDesk++);
        auto d = m_currentWinSta->createDesktop(name, handle);
        return d ? d->getHandle() : nullptr;
    }

    HDESK openDesktop(const std::wstring& name) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_currentWinSta) return nullptr;
        auto d = m_currentWinSta->openDesktop(name);
        return d ? d->getHandle() : nullptr;
    }

    bool switchDesktop(HDESK hDesk) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_currentWinSta) return false;
        // Search desktop by handle
        for (const auto& [name, ws] : m_windowStations) {
            auto d = ws->getActiveDesktop();
            if (d && d->getHandle() == hDesk) {
                m_currentDesktop = d;
                return true;
            }
            // Check non-active desktops
            for (const auto& deskName : {L"Default", L"Winlogon", L"Screen-saver"}) {
                auto cand = ws->openDesktop(deskName);
                if (cand && cand->getHandle() == hDesk) {
                    ws->switchDesktop(deskName);
                    m_currentDesktop = cand;
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::shared_ptr<Desktop> getCurrentDesktop() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_currentDesktop;
    }

    // --- Window Management ---

    HWND createWindowEx(DWORD dwExStyle, const wchar_t* lpClassName, const wchar_t* lpWindowName,
                        DWORD dwStyle, int x, int y, int nWidth, int nHeight,
                        HWND hWndParent, HMENU /*hMenu*/, HINSTANCE /*hInstance*/, void* lpParam,
                        uint32_t tid = 1, uint32_t pid = 4) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!lpClassName || m_windowClasses.find(lpClassName) == m_windowClasses.end()) return nullptr;
        static uintptr_t s_nextHwnd = 0x10000;
        HWND hwnd = reinterpret_cast<HWND>(s_nextHwnd++);

        auto wnd = std::make_shared<WND>();
        wnd->hwnd = hwnd;
        wnd->className = lpClassName ? lpClassName : L"";
        wnd->windowName = lpWindowName ? lpWindowName : L"";
#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
        if (lpWindowName) {
            const auto* titleUnits = reinterpret_cast<const uint16_t*>(lpWindowName);
            while (wnd->titleUtf16Length < wnd->titleUtf16.size() - 1 && titleUnits[wnd->titleUtf16Length]) {
                wnd->titleUtf16[wnd->titleUtf16Length] = titleUnits[wnd->titleUtf16Length];
                ++wnd->titleUtf16Length;
            }
        }
#endif
        auto wndProc = m_windowClasses.find(wnd->className);
        if (wndProc != m_windowClasses.end()) wnd->wndProc = reinterpret_cast<void*>(wndProc->second);
        wnd->style = dwStyle;
        wnd->exStyle = dwExStyle;
        wnd->rcWindow = {x, y, x + nWidth, y + nHeight};
        updateClientGeometry(*wnd);
        wnd->hwndParent = hWndParent;
        wnd->threadId = tid;
        wnd->processId = pid;
        wnd->userData = reinterpret_cast<uint64_t>(lpParam);
        wnd->isVisible = (dwStyle & WS_VISIBLE) != 0;
        wnd->isEnabled = (dwStyle & WS_DISABLED) == 0;

        // Allocate client read-only cache block in Desktop Heap
        if (m_currentDesktop && m_currentDesktop->getHeap()) {
            wnd->desktopHeapOffset = m_currentDesktop->getHeap()->allocateClientWnd(
                hwnd, wnd->rcWindow, wnd->rcClient, dwStyle, dwExStyle, tid, pid);
        }

        m_windows[hwnd] = wnd;

        // If parent exists, add child
        if (hWndParent) {
            auto itP = m_windows.find(hWndParent);
            if (itP != m_windows.end()) {
                itP->second->children.push_back(hwnd);
            }
        } else if (m_currentDesktop) {
            // Top-level window: add to desktop Z-order
            m_currentDesktop->addWindowToZOrder(hwnd, (dwExStyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_TOP);
        }

        m_totalWindowsCreated++;
        return hwnd;
    }

    BOOL destroyWindow(HWND hwnd) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windows.find(hwnd);
        if (it == m_windows.end()) return FALSE_VAL;

        postMessage(hwnd, WM_DESTROY, 0, 0);
        postMessage(hwnd, WM_NCDESTROY, 0, 0);

        auto wnd = it->second;
        wnd->isDestroyed = true;
        if (wnd->hdc) {
            if (wnd->clientBitmap) m_gdiEngine.deleteObject(wnd->clientBitmap);
            m_gdiEngine.releaseDC(wnd->hdc);
            wnd->clientBitmap=nullptr;
            wnd->hdc=nullptr;
        }

        if (m_currentDesktop) {
            m_currentDesktop->removeWindowFromZOrder(hwnd);
            if (m_currentDesktop->getHeap()) {
                m_currentDesktop->getHeap()->freeClientWnd(hwnd);
            }
        }

        m_windows.erase(it);
        m_totalWindowsDestroyed++;
        return TRUE_VAL;
    }

    ATOM registerWindowClass(const wchar_t* name, WNDPROC proc) {
        if (!name || !*name || !proc) return 0;
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it=m_windowClasses.find(name);
        if (it != m_windowClasses.end()) return m_windowClassAtoms[name];
        const ATOM atom=m_nextWindowClassAtom++;
        m_windowClasses.emplace(name,proc);
        m_windowClassAtoms.emplace(name,atom);
        return atom;
    }

    LRESULT dispatchMessage(const MSG& msg) {
        WNDPROC proc=nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            auto it=m_windows.find(msg.hwnd);
            if (it != m_windows.end()) proc=reinterpret_cast<WNDPROC>(it->second->wndProc);
        }
        return proc ? proc(msg.hwnd,msg.message,msg.wParam,msg.lParam)
                    : defWindowProc(msg.hwnd,msg.message,msg.wParam,msg.lParam);
    }

    LRESULT defWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == 0x0081) return 1; // WM_NCCREATE
        if (message == WM_CLOSE) { destroyWindow(hwnd); return 0; }
        if (message == WM_NCHITTEST) return 1; // HTCLIENT
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it=m_windows.find(hwnd);
        if (it == m_windows.end()) return 0;
        if (message == WM_SETTEXT) {
            const auto* text=reinterpret_cast<const wchar_t*>(lParam);
            if (!text) return 0;
            it->second->windowName=text;
            postMessage(hwnd,WM_PAINT,0,0);
            return 1;
        }
        if (message == WM_GETTEXTLENGTH) return static_cast<LRESULT>(it->second->windowName.size());
        if (message == WM_GETTEXT && wParam && lParam) {
            auto* output=reinterpret_cast<wchar_t*>(lParam);
            const size_t capacity=static_cast<size_t>(wParam);
            const size_t count=std::min(it->second->windowName.size(),capacity-1);
            std::copy_n(it->second->windowName.data(),count,output);
            output[count]=L'\0';
            return static_cast<LRESULT>(count);
        }
        return 0;
    }

    intptr_t setWindowLongPtr(HWND hwnd, int32_t index, intptr_t value) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it=m_windows.find(hwnd);
        if (it == m_windows.end()) return 0;
        if (index == -21) {
            const intptr_t previous=static_cast<intptr_t>(it->second->userData);
            it->second->userData=static_cast<uint64_t>(value);
            return previous;
        }
        if (index == -4) {
            const intptr_t previous=reinterpret_cast<intptr_t>(it->second->wndProc);
            it->second->wndProc=reinterpret_cast<void*>(value);
            return previous;
        }
        if (index == -16) {
            const intptr_t previous=static_cast<intptr_t>(it->second->style);
            it->second->style=static_cast<uint32_t>(value);
            return previous;
        }
        if (index == -20) {
            const intptr_t previous=static_cast<intptr_t>(it->second->exStyle);
            it->second->exStyle=static_cast<uint32_t>(value);
            return previous;
        }
        return 0;
    }

    intptr_t getWindowLongPtr(HWND hwnd, int32_t index) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it=m_windows.find(hwnd);
        if (it == m_windows.end()) return 0;
        if (index == -21) return static_cast<intptr_t>(it->second->userData);
        if (index == -4) return reinterpret_cast<intptr_t>(it->second->wndProc);
        if (index == -16) return static_cast<intptr_t>(it->second->style);
        if (index == -20) return static_cast<intptr_t>(it->second->exStyle);
        return 0;
    }

    std::vector<HWND> enumerateTopLevelWindows() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        std::vector<HWND> result;
        if (!m_currentDesktop) return result;
        const auto zOrder = m_currentDesktop->getZOrder();
        result.reserve(zOrder.size());
        for (HWND hwnd : zOrder) {
            auto it = m_windows.find(hwnd);
            if (it == m_windows.end() || !it->second || it->second->hwndParent ||
                it->second->isDestroyed) continue;
            result.push_back(hwnd);
        }
        return result;
    }

    BOOL isWindowVisible(HWND hwnd) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windows.find(hwnd);
        return it != m_windows.end() && it->second && it->second->isVisible
            ? TRUE_VAL : FALSE_VAL;
    }

    BOOL isIconic(HWND hwnd) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windows.find(hwnd);
        return it != m_windows.end() && it->second && it->second->isMinimized
            ? TRUE_VAL : FALSE_VAL;
    }

    int32_t getWindowText(HWND hwnd, wchar_t* output, int32_t capacity) const {
        if (output && capacity > 0) output[0] = L'\0';
        if (!hwnd || !output || capacity <= 0) return 0;
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windows.find(hwnd);
        if (it == m_windows.end() || !it->second) return 0;
        const auto& window = *it->second;
        const size_t length = std::min<size_t>(
#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
            window.titleUtf16Length,
#else
            window.windowName.size(),
#endif
            static_cast<size_t>(capacity - 1));
        for (size_t index = 0; index < length; ++index) {
#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
            output[index] = static_cast<wchar_t>(window.titleUtf16[index]);
#else
            output[index] = window.windowName[index];
#endif
        }
        output[length] = L'\0';
        return static_cast<int32_t>(length);
    }

    HWND getForegroundWindow() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_currentDesktop ? m_currentDesktop->getActiveWindow() : nullptr;
    }

    BOOL showWindow(HWND hwnd, int nCmdShow) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windows.find(hwnd);
        if (it == m_windows.end()) return FALSE_VAL;

        bool wasVisible = it->second->isVisible;
        const bool wasMinimized = it->second->isMinimized;
        const bool wasMaximized = it->second->isMaximized;
        const int32_t oldClientWidth = it->second->rcClient.width();
        const int32_t oldClientHeight = it->second->rcClient.height();
        if (nCmdShow == SW_HIDE) {
            it->second->isVisible = false;
            it->second->style &= ~WS_VISIBLE;
        } else if (nCmdShow == SW_MINIMIZE || nCmdShow == SW_SHOWMINIMIZED ||
                   nCmdShow == SW_SHOWMINNOACTIVE) {
            auto& window = *it->second;
            if (!window.isMinimized && !window.isMaximized) window.restoreRect = window.rcWindow;
            window.isMinimized = true;
            window.isMaximized = false;
            window.style = (window.style | WS_VISIBLE | WS_MINIMIZE) & ~WS_MAXIMIZE;
            window.isVisible = true;
        } else if (nCmdShow == SW_MAXIMIZE || nCmdShow == SW_SHOWMAXIMIZED) {
            auto& window = *it->second;
            if (!window.isMaximized && !window.isMinimized) window.restoreRect = window.rcWindow;
            window.isMaximized = true;
            window.isMinimized = false;
            window.style = (window.style | WS_VISIBLE | WS_MAXIMIZE) & ~WS_MINIMIZE;
            window.isVisible = true;
            auto screen = m_gdiEngine.getBitmapForDC(m_gdiEngine.getScreenDC());
            if (screen) {
                window.rcWindow = {0, 0, static_cast<int32_t>(screen->width), static_cast<int32_t>(screen->height)};
                updateClientGeometry(window);
            }
        } else {
            auto& window = *it->second;
            if (window.isMinimized || window.isMaximized) {
                if (window.restoreRect.width() > 0 && window.restoreRect.height() > 0)
                    window.rcWindow = window.restoreRect;
                updateClientGeometry(window);
            }
            window.isMinimized = false;
            window.isMaximized = false;
            window.style = (window.style | WS_VISIBLE) & ~(WS_MINIMIZE | WS_MAXIMIZE);
            window.isVisible = true;
        }
        const bool mayActivate = nCmdShow != SW_SHOWMINNOACTIVE && nCmdShow != SW_SHOWNOACTIVATE && nCmdShow != SW_SHOWNA;
        if (m_currentDesktop && it->second->isVisible && !it->second->isMinimized && mayActivate) {
            m_currentDesktop->setActiveWindow(hwnd);
        } else if (m_currentDesktop && m_currentDesktop->getActiveWindow() == hwnd && it->second->isMinimized) {
            HWND nextActive = nullptr;
            for (HWND candidate : m_currentDesktop->getZOrder()) {
                auto candidateIt = m_windows.find(candidate);
                if (candidateIt != m_windows.end() && candidateIt->second && candidateIt->second->isVisible &&
                    !candidateIt->second->isMinimized && !candidateIt->second->isDestroyed) {
                    nextActive = candidate;
                    break;
                }
            }
            m_currentDesktop->setActiveWindow(nextActive);
        }
        const bool sizeStateChanged = wasMinimized != it->second->isMinimized ||
                                      wasMaximized != it->second->isMaximized;
        const int32_t clientWidth = it->second->isMinimized ? 0 : it->second->rcClient.width();
        const int32_t clientHeight = it->second->isMinimized ? 0 : it->second->rcClient.height();
        if (sizeStateChanged || clientWidth != oldClientWidth || clientHeight != oldClientHeight) {
            const WPARAM sizeType = it->second->isMinimized ? SIZE_MINIMIZED :
                                    it->second->isMaximized ? SIZE_MAXIMIZED : SIZE_RESTORED;
            const LPARAM size = it->second->isMinimized ? 0 :
                static_cast<LPARAM>((clientHeight << 16) | (clientWidth & 0xFFFF));
            postMessage(hwnd, WM_SIZE, sizeType, size);
        }
        postMessage(hwnd, WM_SHOWWINDOW, it->second->isVisible ? 1U : 0U, 0);

        if (m_currentDesktop && m_currentDesktop->getHeap() && it->second->desktopHeapOffset != 0) {
            m_currentDesktop->getHeap()->updateClientWnd(
                it->second->desktopHeapOffset, it->second->rcWindow, it->second->rcClient,
                it->second->style, it->second->exStyle, it->second->isVisible);
        }
        return wasVisible ? TRUE_VAL : FALSE_VAL;
    }

    BOOL setWindowPos(HWND hwnd, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT uFlags) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windows.find(hwnd);
        if (it == m_windows.end()) return FALSE_VAL;

        auto wnd = it->second;
        const int oldX=wnd->rcWindow.left, oldY=wnd->rcWindow.top;
        const int oldW=wnd->rcClient.width(), oldH=wnd->rcClient.height();
        if (!(uFlags & SWP_NOMOVE)) {
            int w = (uFlags & SWP_NOSIZE) ? wnd->rcWindow.width() : cx;
            int h = (uFlags & SWP_NOSIZE) ? wnd->rcWindow.height() : cy;
            wnd->rcWindow = {x, y, x + w, y + h};
        } else if (!(uFlags & SWP_NOSIZE)) {
            wnd->rcWindow.right = wnd->rcWindow.left + cx;
            wnd->rcWindow.bottom = wnd->rcWindow.top + cy;
        }
        updateClientGeometry(*wnd);

        if (uFlags & SWP_SHOWWINDOW) {
            wnd->isVisible = true;
            wnd->style |= WS_VISIBLE;
        } else if (uFlags & SWP_HIDEWINDOW) {
            wnd->isVisible = false;
            wnd->style &= ~WS_VISIBLE;
        }

        if (!(uFlags & SWP_NOZORDER) && m_currentDesktop) {
            if (hWndInsertAfter == HWND_TOPMOST) wnd->exStyle |= WS_EX_TOPMOST;
            else if (hWndInsertAfter == HWND_NOTOPMOST || hWndInsertAfter == HWND_BOTTOM)
                wnd->exStyle &= ~WS_EX_TOPMOST;
            m_currentDesktop->addWindowToZOrder(hwnd, hWndInsertAfter,
                (wnd->exStyle & WS_EX_TOPMOST) != 0);
        }

        if (m_currentDesktop && m_currentDesktop->getHeap() && wnd->desktopHeapOffset != 0) {
            m_currentDesktop->getHeap()->updateClientWnd(
                wnd->desktopHeapOffset, wnd->rcWindow, wnd->rcClient,
                wnd->style, wnd->exStyle, wnd->isVisible);
        }
        if (wnd->rcWindow.left != oldX || wnd->rcWindow.top != oldY)
            postMessage(hwnd, WM_MOVE, 0, static_cast<LPARAM>((wnd->rcWindow.top << 16) | (wnd->rcWindow.left & 0xFFFF)));
        const int clientWidth = wnd->isMinimized ? 0 : wnd->rcClient.width();
        const int clientHeight = wnd->isMinimized ? 0 : wnd->rcClient.height();
        if (clientWidth != oldW || clientHeight != oldH) {
            const WPARAM sizeType = wnd->isMinimized ? SIZE_MINIMIZED :
                                    wnd->isMaximized ? SIZE_MAXIMIZED : SIZE_RESTORED;
            const LPARAM size = wnd->isMinimized ? 0 :
                static_cast<LPARAM>((clientHeight << 16) | (clientWidth & 0xFFFF));
            postMessage(hwnd, WM_SIZE, sizeType, size);
        }
        return TRUE_VAL;
    }

    HDC getDC(HWND hwnd) {
        if (!hwnd) return m_gdiEngine.getScreenDC();
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it=m_windows.find(hwnd);
        if (it == m_windows.end()) return nullptr;
        auto& wnd=*it->second;
        if (wnd.hdc) return wnd.hdc;
        const int width=std::max(1,wnd.rcClient.width());
        const int height=std::max(1,wnd.rcClient.height());
        HDC dc=m_gdiEngine.createCompatibleDC(m_gdiEngine.getScreenDC());
        HBITMAP bitmap=m_gdiEngine.createCompatibleBitmap(dc,width,height);
        if (!dc || !bitmap) {
            if (bitmap) m_gdiEngine.deleteObject(bitmap);
            if (dc) m_gdiEngine.releaseDC(dc);
            return nullptr;
        }
        // SelectObject returns the previous bitmap, which is legitimately
        // null for a newly-created DC. The successful selection itself does
        // not require a non-null previous object.
        m_gdiEngine.selectObject(dc,bitmap);
        wnd.hdc=dc;
        wnd.clientBitmap=bitmap;
        return dc;
    }

    BOOL releaseDC(HWND hwnd, HDC dc) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it=m_windows.find(hwnd);
        if (it != m_windows.end() && it->second->hdc == dc) {
            if (it->second->clientBitmap) m_gdiEngine.deleteObject(it->second->clientBitmap);
            it->second->clientBitmap=nullptr;
            it->second->hdc=nullptr;
        }
        return m_gdiEngine.releaseDC(dc);
    }

    std::shared_ptr<WND> getWindow(HWND hwnd) const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_windows.find(hwnd);
        if (it != m_windows.end()) return it->second;
        return nullptr;
    }

    std::shared_ptr<WND> getWindowForDC(HDC hdc) const {
        if (!hdc) return nullptr;
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const auto& entry : m_windows) {
            if (entry.second && entry.second->hdc == hdc) return entry.second;
        }
        return nullptr;
    }

    // Compose independent per-window GDI surfaces into the desktop primary
    // surface. Desktop::getZOrder() is front-to-back, so paint in reverse.
    std::shared_ptr<GdiBitmap> composeDesktopFrame(bool includeWindowSurfaces = true) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto screen = m_gdiEngine.getBitmapForDC(m_gdiEngine.getScreenDC());
        if (!screen) return nullptr;
        screen->clear(0xFF101722);
        if (!m_currentDesktop) return screen;

        const int64_t screenWidth = screen->width;
        const int64_t screenHeight = screen->height;
        auto fillRect = [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
            left = std::clamp<int64_t>(left, 0, screenWidth);
            top = std::clamp<int64_t>(top, 0, screenHeight);
            right = std::clamp<int64_t>(right, 0, screenWidth);
            bottom = std::clamp<int64_t>(bottom, 0, screenHeight);
            if (left >= right || top >= bottom) return;
            for (int64_t y = top; y < bottom; ++y) {
                const size_t offset = static_cast<size_t>(y) * screen->width + static_cast<size_t>(left);
                std::fill_n(screen->pixels.data() + offset, static_cast<size_t>(right - left), color);
            }
        };

        const auto zOrder = m_currentDesktop->getZOrder();
        for (auto order = zOrder.rbegin(); order != zOrder.rend(); ++order) {
            auto it = m_windows.find(*order);
            if (it == m_windows.end() || !it->second || !it->second->isVisible ||
                it->second->isMinimized || it->second->isDestroyed) continue;

            const auto& window = *it->second;
            const int64_t left = window.rcWindow.left;
            const int64_t top = window.rcWindow.top;
            const int64_t right = window.rcWindow.right;
            const int64_t bottom = window.rcWindow.bottom;
            const int64_t frame = windowFrameInset(window);
            const int64_t caption = windowCaptionHeight(window);
            const bool hasFrame = frame != 0 || caption != 0;
            const bool active = m_currentDesktop->getActiveWindow() == window.hwnd;

            if (hasFrame) {
                const uint32_t frameColor = active ? 0xFF4B668A : 0xFF303B4D;
                const uint32_t captionColor = active ? 0xFF214C7A : 0xFF303B4D;
                fillRect(left, top, right, bottom, frameColor);
                if (caption != 0) {
                    fillRect(left + frame, top + frame, right - frame, top + frame + caption, captionColor);

                    const int64_t closeLeft = right - frame - 30;
                    const int64_t buttonTop = top + frame + 2;
                    const int64_t buttonBottom = top + frame + caption - 2;
                    fillRect(closeLeft, buttonTop, right - frame - 2, buttonBottom, 0xFF9A3E49);
                    fillRect(closeLeft - 29, buttonTop, closeLeft - 1, buttonBottom, 0xFF40516B);
                    fillRect(closeLeft - 58, buttonTop, closeLeft - 30, buttonBottom, 0xFF40516B);

                    // Three simple, legible caption glyphs: minimize, maximize, close.
                    const int64_t glyphY = top + frame + caption / 2;
                    fillRect(closeLeft - 21, glyphY + 4, closeLeft - 9, glyphY + 6, 0xFFF2F5FA);
                    fillRect(closeLeft - 50, glyphY - 5, closeLeft - 40, glyphY + 5, 0xFFF2F5FA);
                    fillRect(closeLeft - 48, glyphY - 3, closeLeft - 42, glyphY + 3, 0xFF40516B);
                    for (int64_t d = 0; d < 9; ++d) {
                        fillRect(closeLeft + 10 + d, glyphY - 4 + d, closeLeft + 12 + d, glyphY - 2 + d, 0xFFF2F5FA);
                        fillRect(closeLeft + 18 - d, glyphY - 4 + d, closeLeft + 20 - d, glyphY - 2 + d, 0xFFF2F5FA);
                    }

                    int64_t textX = left + frame + 9;
                    const int64_t textLimit = closeLeft - 66;
                    const int64_t textY = top + frame + (caption - 8) / 2;
#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
                    const size_t titleLength = window.titleUtf16Length;
                    auto titleChar = [&](size_t index) { return static_cast<wchar_t>(window.titleUtf16[index]); };
#else
                    const size_t titleLength = window.windowName.size();
                    auto titleChar = [&](size_t index) { return window.windowName[index]; };
#endif
                    for (size_t titleIndex = 0; titleIndex < titleLength; ++titleIndex) {
                        if (textX + 8 > textLimit) break;
                        const wchar_t ch = titleChar(titleIndex);
                        const uint32_t ascii = (ch >= 32 && ch <= 126) ? static_cast<uint32_t>(ch) : static_cast<uint32_t>('?');
                        const uint8_t* glyph = micant::bootvid::font::GLYPH_DATA[ascii - 32];
                        for (int row = 0; row < 8; ++row) {
                            for (int col = 0; col < 8; ++col) {
                                if (glyph[row] & (1U << (7 - col))) {
                                    const int64_t px = textX + col;
                                    const int64_t py = textY + row;
                                    if (px >= 0 && px < screenWidth && py >= 0 && py < screenHeight)
                                        screen->pixels[static_cast<size_t>(py) * screen->width + static_cast<size_t>(px)] = 0xFFF4F6FA;
                                }
                            }
                        }
                        textX += 8;
                    }
                }
            }

            if (!includeWindowSurfaces || !window.hdc) continue;
            auto surface = m_gdiEngine.getBitmapForDC(window.hdc);
            if (!surface || surface->width == 0 || surface->height == 0) continue;

            const int64_t clientX = left + frame;
            const int64_t clientY = top + frame + caption;
            const int64_t clientWidth = window.rcClient.width();
            const int64_t clientHeight = window.rcClient.height();
            const int64_t xBegin = std::max<int64_t>(0, -clientX);
            const int64_t yBegin = std::max<int64_t>(0, -clientY);
            const int64_t xEnd = std::min<int64_t>(clientWidth, screenWidth - clientX);
            const int64_t yEnd = std::min<int64_t>(clientHeight, screenHeight - clientY);
            if (xBegin >= xEnd || yBegin >= yEnd) continue;

            for (int64_t y = yBegin; y < yEnd; ++y) {
                const size_t dstOffset = static_cast<size_t>(clientY + y) * screen->width + static_cast<size_t>(clientX + xBegin);
                const uint32_t sourceY = static_cast<uint32_t>(y * surface->height / std::max<int64_t>(1, clientHeight));
                for (int64_t x = xBegin; x < xEnd; ++x) {
                    const uint32_t sourceX = static_cast<uint32_t>(x * surface->width / std::max<int64_t>(1, clientWidth));
                    screen->pixels[dstOffset + static_cast<size_t>(x - xBegin)] =
                        surface->pixels[static_cast<size_t>(sourceY) * surface->width + sourceX];
                }
            }
        }
        size_t minimizedCount = 0;
        for (HWND hwnd : zOrder) {
            auto it = m_windows.find(hwnd);
            if (it != m_windows.end() && it->second && it->second->isVisible && it->second->isMinimized)
                ++minimizedCount;
        }
        if (minimizedCount != 0) {
            const int64_t barTop = std::max<int64_t>(0, screenHeight - 38);
            fillRect(0, barTop, screenWidth, screenHeight, 0xF018202E);
            const int64_t buttonWidth = std::min<int64_t>(180, std::max<int64_t>(112,
                (screenWidth - 16) / static_cast<int64_t>(std::min<size_t>(minimizedCount, 8)) - 6));
            size_t slot = 0;
            for (HWND hwnd : zOrder) {
                auto it = m_windows.find(hwnd);
                if (it == m_windows.end() || !it->second || !it->second->isVisible || !it->second->isMinimized) continue;
                if (slot >= 8) break;
                const auto& window = *it->second;
                const int64_t buttonLeft = 8 + static_cast<int64_t>(slot++) * (buttonWidth + 6);
                fillRect(buttonLeft, barTop + 4, buttonLeft + buttonWidth, screenHeight - 4, 0xFF35455D);
#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
                const size_t titleLength = window.titleUtf16Length;
                auto titleChar = [&](size_t index) { return static_cast<wchar_t>(window.titleUtf16[index]); };
#else
                const size_t titleLength = window.windowName.size();
                auto titleChar = [&](size_t index) { return window.windowName[index]; };
#endif
                int64_t textX = buttonLeft + 8;
                for (size_t c = 0; c < titleLength && textX + 8 < buttonLeft + buttonWidth - 4 && c < 20; ++c) {
                    const wchar_t ch = titleChar(c);
                    const uint32_t ascii = (ch >= 32 && ch <= 126) ? static_cast<uint32_t>(ch) : static_cast<uint32_t>('?');
                    const uint8_t* glyph = micant::bootvid::font::GLYPH_DATA[ascii - 32];
                    for (int row = 0; row < 8; ++row) for (int col = 0; col < 8; ++col) {
                        if (glyph[row] & (1U << (7 - col))) {
                            const int64_t px = textX + col, py = barTop + 14 + row;
                            if (px >= 0 && px < screenWidth && py < screenHeight)
                                screen->pixels[static_cast<size_t>(py) * screen->width + static_cast<size_t>(px)] = 0xFFF4F6FA;
                        }
                    }
                    textX += 8;
                }
            }
        }
        if (m_pointerVisible) {
            auto cursorPixel = [&](int32_t x, int32_t y, uint32_t color = 0xFFF5F7FC) {
                fillRect(x, y, x + 1, y + 1, color);
            };
            auto cursorLine = [&](int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color = 0xFFF5F7FC) {
                const int32_t dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
                const int32_t dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
                int32_t error = dx + dy;
                for (;;) {
                    cursorPixel(x1, y1, color);
                    if (x1 == x2 && y1 == y2) break;
                    const int32_t twice = error * 2;
                    if (twice >= dy) { error += dy; x1 += sx; }
                    if (twice <= dx) { error += dx; y1 += sy; }
                }
            };
            if (m_cursorShape == CursorShape::Arrow) {
            for (int32_t row = 0; row < 16; ++row) {
                for (int32_t col = 0; col < 12; ++col) {
                    const bool edge = col == 0 || col == row / 2 || (row >= 12 && col >= 5 && col <= 7);
                    const bool body = (row < 12 && col <= row / 2) || (row >= 10 && row < 16 && col >= 4 && col <= 8);
                    if (body) fillRect(m_pointerX + col, m_pointerY + row, m_pointerX + col + 1,
                                       m_pointerY + row + 1, edge ? 0xFF080A0E : 0xFFF5F7FC);
                }
            }
            } else {
                const int32_t cx = m_pointerX, cy = m_pointerY;
                const uint32_t outline = 0xFF080A0E;
                auto outlinedLine = [&](int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
                    cursorLine(x1 - 1, y1, x2 - 1, y2, outline);
                    cursorLine(x1 + 1, y1, x2 + 1, y2, outline);
                    cursorLine(x1, y1 - 1, x2, y2 - 1, outline);
                    cursorLine(x1, y1 + 1, x2, y2 + 1, outline);
                    cursorLine(x1, y1, x2, y2);
                };
                switch (m_cursorShape) {
                    case CursorShape::Move:
                        outlinedLine(cx - 8, cy, cx + 8, cy);
                        outlinedLine(cx, cy - 8, cx, cy + 8);
                        cursorLine(cx - 8, cy, cx - 4, cy - 3); cursorLine(cx - 8, cy, cx - 4, cy + 3);
                        cursorLine(cx + 8, cy, cx + 4, cy - 3); cursorLine(cx + 8, cy, cx + 4, cy + 3);
                        cursorLine(cx, cy - 8, cx - 3, cy - 4); cursorLine(cx, cy - 8, cx + 3, cy - 4);
                        cursorLine(cx, cy + 8, cx - 3, cy + 4); cursorLine(cx, cy + 8, cx + 3, cy + 4);
                        break;
                    case CursorShape::ResizeNS:
                        outlinedLine(cx, cy - 8, cx, cy + 8);
                        cursorLine(cx, cy - 8, cx - 4, cy - 4); cursorLine(cx, cy - 8, cx + 4, cy - 4);
                        cursorLine(cx, cy + 8, cx - 4, cy + 4); cursorLine(cx, cy + 8, cx + 4, cy + 4);
                        break;
                    case CursorShape::ResizeEW:
                        outlinedLine(cx - 8, cy, cx + 8, cy);
                        cursorLine(cx - 8, cy, cx - 4, cy - 4); cursorLine(cx - 8, cy, cx - 4, cy + 4);
                        cursorLine(cx + 8, cy, cx + 4, cy - 4); cursorLine(cx + 8, cy, cx + 4, cy + 4);
                        break;
                    case CursorShape::ResizeNWSE:
                        outlinedLine(cx - 7, cy - 7, cx + 7, cy + 7);
                        cursorLine(cx - 7, cy - 7, cx - 2, cy - 7); cursorLine(cx - 7, cy - 7, cx - 7, cy - 2);
                        cursorLine(cx + 7, cy + 7, cx + 2, cy + 7); cursorLine(cx + 7, cy + 7, cx + 7, cy + 2);
                        break;
                    case CursorShape::ResizeNESW:
                        outlinedLine(cx + 7, cy - 7, cx - 7, cy + 7);
                        cursorLine(cx + 7, cy - 7, cx + 2, cy - 7); cursorLine(cx + 7, cy - 7, cx + 7, cy - 2);
                        cursorLine(cx - 7, cy + 7, cx - 2, cy + 7); cursorLine(cx - 7, cy + 7, cx - 7, cy + 2);
                        break;
                    default: break;
                }
            }
        }
        return screen;
    }

    std::vector<DesktopCompositionLayer> collectDesktopCompositionLayers() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        std::vector<DesktopCompositionLayer> layers;
        if (!m_currentDesktop) return layers;
        const auto zOrder = m_currentDesktop->getZOrder(); // front-to-back
        layers.reserve(zOrder.size());
        for (auto order = zOrder.rbegin(); order != zOrder.rend(); ++order) {
            auto it = m_windows.find(*order);
            if (it == m_windows.end() || !it->second || !it->second->isVisible ||
                it->second->isMinimized || it->second->isDestroyed || !it->second->hdc) continue;
            const auto& window = *it->second;
            auto surface = m_gdiEngine.getBitmapForDC(window.hdc);
            if (!surface || surface->width == 0 || surface->height == 0) continue;
            const int32_t frame = windowFrameInset(window);
            const int32_t caption = windowCaptionHeight(window);
            DesktopCompositionLayer layer{};
            layer.descriptor.SurfaceId = reinterpret_cast<uintptr_t>(window.hdc);
            layer.descriptor.Pixels = surface->pixels.data();
            layer.descriptor.Width = surface->width;
            layer.descriptor.Height = surface->height;
            layer.descriptor.Pitch = surface->stride / sizeof(uint32_t);
            layer.descriptor.X = window.rcWindow.left + frame;
            layer.descriptor.Y = window.rcWindow.top + frame + caption;
            layer.descriptor.Opacity = 0xFF;
            layer.surface = std::move(surface);
            layers.push_back(std::move(layer));
        }
        return layers;
    }

    // Route kernel PS/2 mouse packets to the topmost window. Non-client input
    // is handled here so caption buttons and resize borders remain responsive
    // even when the application only pumps its normal Win32 message queue.
    void routeMouseInput(const MicaWin32kInputEvent& input) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_pointerX = input.x;
        m_pointerY = input.y;
        m_pointerVisible = true;
        const bool wasDown = (m_pointerButtons & 1U) != 0;
        const bool isDown = (input.buttons & 1U) != 0;
        const int32_t x = input.x;
        const int32_t y = input.y;

        HWND hit = nullptr;
        std::shared_ptr<WND> hitWindow;
        if (m_currentDesktop) {
            for (HWND candidate : m_currentDesktop->getZOrder()) {
                auto it = m_windows.find(candidate);
                if (it == m_windows.end() || !it->second || !it->second->isVisible ||
                    it->second->isMinimized || it->second->isDestroyed || it->second->hwndParent) continue;
                if (it->second->rcWindow.contains(x, y)) { hit = candidate; hitWindow = it->second; break; }
            }
        }
        if (m_mouseInteraction == MouseInteraction::Move) {
            m_cursorShape = CursorShape::Move;
        } else if (m_mouseInteraction == MouseInteraction::Resize) {
            m_cursorShape = cursorForResizeEdges(m_resizeEdges);
        } else if (hitWindow) {
            const int32_t frame = windowFrameInset(*hitWindow);
            const int32_t localX = x - hitWindow->rcWindow.left;
            const int32_t localY = y - hitWindow->rcWindow.top;
            const int32_t width = hitWindow->rcWindow.width();
            const int32_t height = hitWindow->rcWindow.height();
            const int32_t caption = windowCaptionHeight(*hitWindow);
            if (caption && localY >= frame && localY < frame + caption) {
                m_cursorShape = CursorShape::Move;
            } else {
                uint8_t edges = 0;
                if (localX < 9) edges |= 0x01;
                if (localX >= width - 9) edges |= 0x02;
                if (localY < 9) edges |= 0x04;
                if (localY >= height - 9) edges |= 0x08;
                m_cursorShape = cursorForResizeEdges(edges);
            }
        } else {
            m_cursorShape = CursorShape::Arrow;
        }

        if (isDown && !wasDown) {
            if (!hit && m_currentDesktop) {
                auto screen = m_gdiEngine.getBitmapForDC(m_gdiEngine.getScreenDC());
                if (screen && y >= static_cast<int32_t>(screen->height) - 38) {
                    const auto order = m_currentDesktop->getZOrder();
                    size_t minimizedCount = 0;
                    for (HWND candidate : order) {
                        auto it = m_windows.find(candidate);
                        if (it != m_windows.end() && it->second && it->second->isVisible && it->second->isMinimized)
                            ++minimizedCount;
                    }
                    const int32_t shownCount = static_cast<int32_t>(std::min<size_t>(minimizedCount, 8));
                    const int32_t buttonWidth = std::min<int32_t>(180, std::max<int32_t>(112,
                        (static_cast<int32_t>(screen->width) - 16) / std::max(1, shownCount) - 6));
                    const int32_t buttonStride = buttonWidth + 6;
                    size_t slot = 0;
                    for (HWND candidate : order) {
                        auto it = m_windows.find(candidate);
                        if (it == m_windows.end() || !it->second || !it->second->isVisible || !it->second->isMinimized) continue;
                        const int32_t buttonLeft = 8 + static_cast<int32_t>(slot) * buttonStride;
                        if (slot < 8 && x >= buttonLeft && x < buttonLeft + buttonWidth) {
                            showWindow(candidate, SW_RESTORE);
                            m_currentDesktop->bringToTop(candidate);
                            m_currentDesktop->setActiveWindow(candidate);
                            m_pointerButtons = input.buttons;
                            return;
                        }
                        ++slot;
                    }
                }
            }
            m_mouseCapture = hit;
            m_pressedCaptionButton = 0;
            m_mouseInteraction = MouseInteraction::None;
            if (hitWindow) {
                if (m_currentDesktop) {
                    m_currentDesktop->bringToTop(hit);
                    m_currentDesktop->setActiveWindow(hit);
                    m_currentDesktop->setFocusWindow(hit);
                }
                const int32_t frame = windowFrameInset(*hitWindow);
                const int32_t caption = windowCaptionHeight(*hitWindow);
                const int32_t localX = x - hitWindow->rcWindow.left;
                const int32_t localY = y - hitWindow->rcWindow.top;
                const int32_t width = hitWindow->rcWindow.width();
                const int32_t height = hitWindow->rcWindow.height();
                const int32_t closeLeft = width - frame - 30;
                if (caption && localY >= frame && localY < frame + caption) {
                    if (localX >= closeLeft && localX < width - frame - 2) m_pressedCaptionButton = 3;
                    else if (localX >= closeLeft - 29 && localX < closeLeft - 1) m_pressedCaptionButton = 2;
                    else if (localX >= closeLeft - 58 && localX < closeLeft - 30) m_pressedCaptionButton = 1;
                    else {
                        m_mouseInteraction = MouseInteraction::Move;
                        m_dragOffsetX = localX;
                        m_dragOffsetY = localY;
                    }
                } else if (hitWindow->style & WS_THICKFRAME) {
                    const uint8_t edges = static_cast<uint8_t>((localX < 9 ? 0x01 : 0) |
                        (localX >= width - 9 ? 0x02 : 0) |
                        (localY < 9 ? 0x04 : 0) | (localY >= height - 9 ? 0x08 : 0));
                    if (!edges) {
                        postMouseMessage(hitWindow, WM_LBUTTONDOWN, input.buttons, x, y);
                    } else {
                    m_mouseInteraction = MouseInteraction::Resize;
                    m_resizeStartRect = hitWindow->rcWindow;
                    m_dragOffsetX = x;
                    m_dragOffsetY = y;
                    m_resizeEdges = edges;
                    }
                } else {
                    postMouseMessage(hitWindow, WM_LBUTTONDOWN, input.buttons, x, y);
                }
            }
        } else if (isDown && wasDown && m_mouseCapture) {
            auto it = m_windows.find(m_mouseCapture);
            if (it != m_windows.end() && it->second) {
                auto window = it->second;
                if (m_mouseInteraction == MouseInteraction::Move) {
                    setWindowPos(window->hwnd, HWND_TOP, x - m_dragOffsetX, y - m_dragOffsetY,
                                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                } else if (m_mouseInteraction == MouseInteraction::Resize) {
                    int32_t left = m_resizeStartRect.left, top = m_resizeStartRect.top;
                    int32_t right = m_resizeStartRect.right, bottom = m_resizeStartRect.bottom;
                    const int32_t minWidth = minimumOuterWidth(*window);
                    const int32_t minHeight = minimumOuterHeight(*window);
                    if (m_resizeEdges & 0x01) left = std::min(x, right - minWidth);
                    if (m_resizeEdges & 0x02) right = std::max(x, left + minWidth);
                    if (m_resizeEdges & 0x04) top = std::min(y, bottom - minHeight);
                    if (m_resizeEdges & 0x08) bottom = std::max(y, top + minHeight);
                    setWindowPos(window->hwnd, HWND_TOP, left, top, right - left, bottom - top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                } else if (!m_pressedCaptionButton) {
                    postMouseMessage(window, WM_MOUSEMOVE, input.buttons, x, y);
                }
            }
        } else if (!isDown && wasDown) {
            auto it = m_windows.find(m_mouseCapture);
            if (it != m_windows.end() && it->second) {
                auto window = it->second;
                if (m_pressedCaptionButton) {
                    const int32_t frame = windowFrameInset(*window);
                    const int32_t caption = windowCaptionHeight(*window);
                    const int32_t localX = x - window->rcWindow.left;
                    const int32_t localY = y - window->rcWindow.top;
                    const int32_t width = window->rcWindow.width();
                    const int32_t closeLeft = width - frame - 30;
                    const bool inCaption = localY >= frame && localY < frame + caption;
                    const bool stillInButton = m_pressedCaptionButton == 3 ? (localX >= closeLeft && localX < width - frame - 2) :
                        m_pressedCaptionButton == 2 ? (localX >= closeLeft - 29 && localX < closeLeft - 1) :
                                                      (localX >= closeLeft - 58 && localX < closeLeft - 30);
                    if (inCaption && stillInButton) {
                        if (m_pressedCaptionButton == 1) showWindow(window->hwnd, SW_MINIMIZE);
                        else if (m_pressedCaptionButton == 2) showWindow(window->hwnd, window->isMaximized ? SW_RESTORE : SW_MAXIMIZE);
                        else postMessage(window->hwnd, WM_CLOSE, 0, 0);
                    }
                } else if (m_mouseInteraction == MouseInteraction::None) {
                    postMouseMessage(window, WM_LBUTTONUP, input.buttons, x, y);
                }
            }
            m_mouseCapture = nullptr;
            m_mouseInteraction = MouseInteraction::None;
            m_pressedCaptionButton = 0;
            m_resizeEdges = 0;
        } else if (!isDown && hitWindow) {
            postMouseMessage(hitWindow, WM_MOUSEMOVE, input.buttons, x, y);
        }
        m_pointerButtons = input.buttons;
    }

    // --- Message Queue & Pump ---

    std::shared_ptr<ThreadMessageQueue> getOrCreateThreadQueue(uint32_t tid) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_threadQueues.find(tid);
        if (it != m_threadQueues.end()) return it->second;

        auto queue = std::make_shared<ThreadMessageQueue>(tid);
        m_threadQueues[tid] = queue;
        return queue;
    }

    void postQuitMessage(int32_t exitCode, uint32_t tid = 1) {
        auto queue = getOrCreateThreadQueue(tid);
        MSG msg{nullptr, WM_QUIT, static_cast<WPARAM>(exitCode), 0, 0, {0, 0}};
        queue->postMessage(msg);
    }

    BOOL postMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        uint32_t targetTid = 1;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (hwnd) {
                auto it = m_windows.find(hwnd);
                if (it != m_windows.end()) targetTid = it->second->threadId;
            }
        }
        auto queue = getOrCreateThreadQueue(targetTid);
        MSG msg{hwnd, message, wParam, lParam, 0, {0, 0}};
        queue->postMessage(msg);
        m_totalMessagesPosted++;
        return TRUE_VAL;
    }

    LRESULT sendMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, uint32_t senderTid = 1) {
        uint32_t targetTid = 1;
        void* wndProc = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (hwnd) {
                auto it = m_windows.find(hwnd);
                if (it != m_windows.end()) {
                    targetTid = it->second->threadId;
                    wndProc = it->second->wndProc;
                }
            }
        }

        m_totalMessagesSent++;

        // If target thread is same as sender, or if direct callback is available:
        if (targetTid == senderTid || wndProc != nullptr) {
            LRESULT res = 0;
            // Dispatch through trampoline callback engine
            m_callbackEngine.invokeUserModeCallback(message, nullptr, 0, nullptr, nullptr, &res);
            return res;
        }

        // Inter-thread synchronous message: queue into target thread's sentQueue
        auto queue = getOrCreateThreadQueue(targetTid);
        auto entry = queue->queueSentMessage(hwnd, message, wParam, lParam, senderTid);
        // Process entry immediately for simulation
        entry->completed = true;
        entry->result = 1; // Default handled
        return entry->result;
    }

    BOOL getMessage(MSG& outMsg, HWND hwndFilter, UINT msgMin, UINT msgMax, uint32_t tid = 1) {
        auto queue = getOrCreateThreadQueue(tid);
        for (;;) {
            pumpMouseInput();
            if (queue->peekMessage(outMsg, hwndFilter, msgMin, msgMax, PM_REMOVE)) return TRUE_VAL;
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
    }

    BOOL peekMessage(MSG& outMsg, HWND hwndFilter, UINT msgMin, UINT msgMax, UINT removeFlag, uint32_t tid = 1) {
        pumpMouseInput();
        auto queue = getOrCreateThreadQueue(tid);
        return queue->peekMessage(outMsg, hwndFilter, msgMin, msgMax, removeFlag) ? TRUE_VAL : FALSE_VAL;
    }

    // --- Subsystem Sub-Components Access ---

    [[nodiscard]] GdiEngine& getGdi() noexcept { return m_gdiEngine; }
    [[nodiscard]] UserModeCallbackEngine& getCallbacks() noexcept { return m_callbackEngine; }

    // --- WDDM Display Surface Presentation ---

    bool blitToWddmPrimary(HDC hdcSrc, int32_t nXSrc, int32_t nYSrc,
                           int32_t nWidth, int32_t nHeight);

    // --- Diagnostic Telemetry ---

    [[nodiscard]] WIN32K_TELEMETRY getTelemetry() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        WIN32K_TELEMETRY t;
        t.totalSyscallsHandled = m_totalSyscalls.load(std::memory_order_relaxed);
        t.totalWindowsCreated = m_totalWindowsCreated.load(std::memory_order_relaxed);
        t.totalWindowsDestroyed = m_totalWindowsDestroyed.load(std::memory_order_relaxed);
        t.totalMessagesPosted = m_totalMessagesPosted.load(std::memory_order_relaxed);
        t.totalMessagesSent = m_totalMessagesSent.load(std::memory_order_relaxed);
        t.totalCallbacksInvoked = m_callbackEngine.getTotalCallbacks();
        t.totalBitBltsExecuted = m_gdiEngine.getTotalBitBlts();
        t.totalHeapBytesAllocated = (m_currentDesktop && m_currentDesktop->getHeap())
                                        ? m_currentDesktop->getHeap()->getAllocatedBytes()
                                        : 0;
        t.activeDesktopsCount = m_currentWinSta ? static_cast<uint32_t>(m_currentWinSta->getDesktopCount()) : 0;
        t.activeWindowStationsCount = static_cast<uint32_t>(m_windowStations.size());
        t.activeWindowsCount = static_cast<uint32_t>(m_windows.size());
        return t;
    }

    // --- Shadow SSDT Syscall Dispatcher ---

    NtStatus dispatchShadowSyscall(const micant::sys::SyscallFrame& frame) {
        m_totalSyscalls++;

        switch (frame.ssn) {
            case SSN_NTUSER_CREATEWINDOWEX: {
                DWORD dwExStyle = static_cast<DWORD>(frame.arg1);
                auto* lpClass = reinterpret_cast<const wchar_t*>(frame.arg2);
                auto* lpName = reinterpret_cast<const wchar_t*>(frame.arg3);
                DWORD dwStyle = static_cast<DWORD>(frame.arg4);
                // Stack arguments: x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam
                int x = 0, y = 0, w = 800, h = 600;
                HWND hParent = nullptr;
                void* lpParam = nullptr;
                if (frame.stackArgs && frame.stackArgCount >= 4) {
                    x = static_cast<int>(frame.stackArgs[0]);
                    y = static_cast<int>(frame.stackArgs[1]);
                    w = static_cast<int>(frame.stackArgs[2]);
                    h = static_cast<int>(frame.stackArgs[3]);
                    if (frame.stackArgCount >= 5) hParent = reinterpret_cast<HWND>(frame.stackArgs[4]);
                    if (frame.stackArgCount >= 8) lpParam = reinterpret_cast<void*>(frame.stackArgs[7]);
                }
                HWND hwnd = createWindowEx(dwExStyle, lpClass, lpName, dwStyle, x, y, w, h, hParent, nullptr, nullptr, lpParam);
                return hwnd ? NtStatus::Success : NtStatus::Unsuccessful;
            }

            case SSN_NTUSER_DESTROYWINDOW: {
                HWND hwnd = reinterpret_cast<HWND>(frame.arg1);
                return destroyWindow(hwnd) ? NtStatus::Success : NtStatus::InvalidHandle;
            }

            case SSN_NTUSER_SHOWWINDOW: {
                HWND hwnd = reinterpret_cast<HWND>(frame.arg1);
                int cmd = static_cast<int>(frame.arg2);
                showWindow(hwnd, cmd);
                return NtStatus::Success;
            }

            case SSN_NTUSER_SETWINDOWPOS: {
                HWND hwnd = reinterpret_cast<HWND>(frame.arg1);
                HWND hAfter = reinterpret_cast<HWND>(frame.arg2);
                int x = static_cast<int>(frame.arg3);
                int y = static_cast<int>(frame.arg4);
                int cx = 100, cy = 100;
                UINT flags = 0;
                if (frame.stackArgs && frame.stackArgCount >= 3) {
                    cx = static_cast<int>(frame.stackArgs[0]);
                    cy = static_cast<int>(frame.stackArgs[1]);
                    flags = static_cast<UINT>(frame.stackArgs[2]);
                }
                setWindowPos(hwnd, hAfter, x, y, cx, cy, flags);
                return NtStatus::Success;
            }

            case SSN_NTUSER_POSTMESSAGE: {
                HWND hwnd = reinterpret_cast<HWND>(frame.arg1);
                UINT msg = static_cast<UINT>(frame.arg2);
                WPARAM wp = static_cast<WPARAM>(frame.arg3);
                LPARAM lp = static_cast<LPARAM>(frame.arg4);
                postMessage(hwnd, msg, wp, lp);
                return NtStatus::Success;
            }

            case SSN_NTUSER_SENDMESSAGE: {
                HWND hwnd = reinterpret_cast<HWND>(frame.arg1);
                UINT msg = static_cast<UINT>(frame.arg2);
                WPARAM wp = static_cast<WPARAM>(frame.arg3);
                LPARAM lp = static_cast<LPARAM>(frame.arg4);
                sendMessage(hwnd, msg, wp, lp);
                return NtStatus::Success;
            }

            case SSN_NTUSER_GETMESSAGE: {
                auto* pMsg = reinterpret_cast<MSG*>(frame.arg1);
                HWND hwnd = reinterpret_cast<HWND>(frame.arg2);
                UINT minM = static_cast<UINT>(frame.arg3);
                UINT maxM = static_cast<UINT>(frame.arg4);
                if (!pMsg) return NtStatus::InvalidParameter;
                BOOL res = getMessage(*pMsg, hwnd, minM, maxM);
                return res ? NtStatus::Success : NtStatus::Unsuccessful;
            }

            case SSN_NTUSER_PEEKMESSAGE: {
                auto* pMsg = reinterpret_cast<MSG*>(frame.arg1);
                HWND hwnd = reinterpret_cast<HWND>(frame.arg2);
                UINT minM = static_cast<UINT>(frame.arg3);
                UINT maxM = static_cast<UINT>(frame.arg4);
                UINT removeFlag = 0;
                if (frame.stackArgs && frame.stackArgCount >= 1) {
                    removeFlag = static_cast<UINT>(frame.stackArgs[0]);
                }
                if (!pMsg) return NtStatus::InvalidParameter;
                BOOL res = peekMessage(*pMsg, hwnd, minM, maxM, removeFlag);
                return res ? NtStatus::Success : NtStatus::Unsuccessful;
            }

            case SSN_NTUSER_GETDC: {
                HDC dc = m_gdiEngine.getScreenDC();
                return dc ? NtStatus::Success : NtStatus::InvalidHandle;
            }

            case SSN_NTUSER_RELEASEDC: {
                HDC hdc = reinterpret_cast<HDC>(frame.arg2);
                m_gdiEngine.releaseDC(hdc);
                return NtStatus::Success;
            }

            case SSN_NTGDI_CREATECOMPATIBLEDC: {
                HDC hRef = reinterpret_cast<HDC>(frame.arg1);
                HDC dc = m_gdiEngine.createCompatibleDC(hRef);
                return dc ? NtStatus::Success : NtStatus::Unsuccessful;
            }

            case SSN_NTGDI_CREATECOMPATIBLEBITMAP: {
                HDC hdc = reinterpret_cast<HDC>(frame.arg1);
                uint32_t w = static_cast<uint32_t>(frame.arg2);
                uint32_t h = static_cast<uint32_t>(frame.arg3);
                HBITMAP bmp = m_gdiEngine.createCompatibleBitmap(hdc, w, h);
                return bmp ? NtStatus::Success : NtStatus::Unsuccessful;
            }

            case SSN_NTGDI_BITBLT: {
                HDC hDest = reinterpret_cast<HDC>(frame.arg1);
                int xD = static_cast<int>(frame.arg2);
                int yD = static_cast<int>(frame.arg3);
                int w = static_cast<int>(frame.arg4);
                int h = 0, xS = 0, yS = 0;
                HDC hSrc = nullptr;
                DWORD rop = SRCCOPY;
                if (frame.stackArgs && frame.stackArgCount >= 5) {
                    h = static_cast<int>(frame.stackArgs[0]);
                    hSrc = reinterpret_cast<HDC>(frame.stackArgs[1]);
                    xS = static_cast<int>(frame.stackArgs[2]);
                    yS = static_cast<int>(frame.stackArgs[3]);
                    rop = static_cast<DWORD>(frame.stackArgs[4]);
                }
                BOOL res = m_gdiEngine.bitBlt(hDest, xD, yD, w, h, hSrc, xS, yS, rop);
                return res ? NtStatus::Success : NtStatus::Unsuccessful;
            }

            case SSN_NTGDI_GETSTOCKOBJECT: {
                int obj = static_cast<int>(frame.arg1);
                void* p = m_gdiEngine.getStockObject(obj);
                return p ? NtStatus::Success : NtStatus::InvalidParameter;
            }

            case SSN_NTCALLBACKRETURN: {
                uint64_t cbId = frame.arg1;
                auto* pRes = reinterpret_cast<const void*>(frame.arg2);
                uint32_t len = static_cast<uint32_t>(frame.arg3);
                NtStatus st = static_cast<NtStatus>(frame.arg4);
                LRESULT resVal = 0;
                if (frame.stackArgs && frame.stackArgCount >= 1) {
                    resVal = static_cast<LRESULT>(frame.stackArgs[0]);
                }
                return m_callbackEngine.callbackReturn(cbId, pRes, len, st, resVal);
            }

            default:
                return NtStatus::InvalidDeviceRequest;
        }
    }

private:
    enum class MouseInteraction : uint8_t { None, Move, Resize };
    enum class CursorShape : uint8_t { Arrow, Move, ResizeNS, ResizeEW, ResizeNWSE, ResizeNESW };

    static CursorShape cursorForResizeEdges(uint8_t edges) noexcept {
        const bool horizontal = (edges & (0x01 | 0x02)) != 0;
        const bool vertical = (edges & (0x04 | 0x08)) != 0;
        if (horizontal && vertical) {
            const bool sameDirection = ((edges & 0x01) && (edges & 0x04)) ||
                                       ((edges & 0x02) && (edges & 0x08));
            return sameDirection ? CursorShape::ResizeNWSE : CursorShape::ResizeNESW;
        }
        if (horizontal) return CursorShape::ResizeEW;
        if (vertical) return CursorShape::ResizeNS;
        return CursorShape::Arrow;
    }

    static LPARAM packMousePoint(int32_t x, int32_t y) noexcept {
        return static_cast<LPARAM>((static_cast<uint32_t>(y) & 0xFFFFU) << 16 |
                                   (static_cast<uint32_t>(x) & 0xFFFFU));
    }

    void postMouseMessage(const std::shared_ptr<WND>& window, UINT message,
                          WPARAM buttons, int32_t screenX, int32_t screenY) {
        if (!window) return;
        const int32_t clientX = screenX - window->rcWindow.left - windowFrameInset(*window);
        const int32_t clientY = screenY - window->rcWindow.top - windowFrameInset(*window) - windowCaptionHeight(*window);
        postMessage(window->hwnd, message, buttons, packMousePoint(clientX, clientY));
    }

    void pumpMouseInput() {
#if defined(__x86_64__)
        for (unsigned count = 0; count < 64; ++count) {
            MicaWin32kKeyboardEvent key{};
            const uint64_t available = micant::sys::invokeUserService(
                0x84, reinterpret_cast<uint64_t>(&key));
            if (available != 1) break;
            HWND target = m_currentDesktop ? m_currentDesktop->getFocusWindow() : nullptr;
            if (!target && m_currentDesktop) target = m_currentDesktop->getActiveWindow();
            if (!target) continue;
            postMessage(target, (key.flags & 1) ? WM_KEYDOWN : WM_KEYUP, key.virtualKey, 0);
            if ((key.flags & 1) && key.character) postMessage(target, WM_CHAR, key.character, 0);
        }
        bool inputChanged = false;
        for (unsigned count = 0; count < 128; ++count) {
            MicaWin32kInputEvent input{};
            const uint64_t available = micant::sys::invokeUserService(
                0x82, reinterpret_cast<uint64_t>(&input));
            if (available != 1) break;
            routeMouseInput(input);
            inputChanged = true;
        }
        if (inputChanged) {
            auto frame = composeDesktopFrame();
            if (!frame || frame->pixels.empty()) return;
            micant::um::UserPresentSurfaceParams64 params{};
            params.surfaceBufferVa = reinterpret_cast<uint64_t>(frame->pixels.data());
            params.width = frame->width;
            params.height = frame->height;
            params.pitch = frame->width;
            micant::sys::invokeUserService(0x81, reinterpret_cast<uint64_t>(&params));
        }
#endif
    }

    static int32_t windowFrameInset(const WND& window) noexcept {
        if (window.hwndParent) return 0;
        if (window.style & WS_THICKFRAME) return 5;
        return (window.style & (WS_BORDER | WS_DLGFRAME)) ? 1 : 0;
    }

    static int32_t windowCaptionHeight(const WND& window) noexcept {
        return (!window.hwndParent && (window.style & WS_CAPTION)) ? 26 : 0;
    }

    static int32_t minimumOuterWidth(const WND& window) noexcept {
        constexpr int32_t minimumClientWidth = 160;
        return minimumClientWidth + 2 * windowFrameInset(window);
    }

    static int32_t minimumOuterHeight(const WND& window) noexcept {
        constexpr int32_t minimumClientHeight = 100;
        return minimumClientHeight + 2 * windowFrameInset(window) + windowCaptionHeight(window);
    }

    static void updateClientGeometry(WND& window) noexcept {
        const int32_t outerWidth = std::max<int32_t>(1, window.rcWindow.width());
        const int32_t outerHeight = std::max<int32_t>(1, window.rcWindow.height());
        const int32_t inset = windowFrameInset(window);
        const int32_t caption = windowCaptionHeight(window);
        const int32_t clientWidth = std::max<int32_t>(1, outerWidth - inset * 2);
        const int32_t clientHeight = std::max<int32_t>(1, outerHeight - inset * 2 - caption);
        window.rcClient = {0, 0, clientWidth, clientHeight};
    }

    static LRESULT WINAPI BuiltInWindowProcedure(HWND, UINT message, WPARAM, LPARAM) {
        return message == 0x0081 ? 1 : 0; // Accept WM_NCCREATE for built-in controls.
    }

    TitanWin32kSubsystem() {
        initialize();
    }

    bool m_initialized{false};
    std::unordered_map<std::wstring, std::shared_ptr<WindowStation>> m_windowStations;
    std::shared_ptr<WindowStation> m_currentWinSta;
    std::shared_ptr<Desktop> m_currentDesktop;

    std::unordered_map<HWND, std::shared_ptr<WND>> m_windows;
    std::unordered_map<std::wstring, WNDPROC> m_windowClasses;
    std::unordered_map<std::wstring, ATOM> m_windowClassAtoms;
    ATOM m_nextWindowClassAtom{0xC001};
    std::unordered_map<uint32_t, std::shared_ptr<ThreadMessageQueue>> m_threadQueues;

    GdiEngine m_gdiEngine;
    UserModeCallbackEngine m_callbackEngine;

    std::atomic<uint64_t> m_totalSyscalls{0};
    std::atomic<uint64_t> m_totalWindowsCreated{0};
    std::atomic<uint64_t> m_totalWindowsDestroyed{0};
    std::atomic<uint64_t> m_totalMessagesPosted{0};
    std::atomic<uint64_t> m_totalMessagesSent{0};

    HWND m_mouseCapture{nullptr};
    MouseInteraction m_mouseInteraction{MouseInteraction::None};
    CursorShape m_cursorShape{CursorShape::Arrow};
    int32_t m_dragOffsetX{0}, m_dragOffsetY{0};
    int32_t m_pointerX{512}, m_pointerY{384};
    uint32_t m_pointerButtons{0};
    uint8_t m_pressedCaptionButton{0};
    RECT m_resizeStartRect{0, 0, 0, 0};
    uint8_t m_resizeEdges{0};
    bool m_pointerVisible{true};

    mutable std::recursive_mutex m_mutex;
};

// ============================================================================
// 13. Win32k Driver C ABI Exports & Thunks
// ============================================================================

extern "C" {

inline HWND WINAPI NtUserCreateWindowEx(DWORD dwExStyle, const wchar_t* lpClassName,
                                       const wchar_t* lpWindowName, DWORD dwStyle,
                                       int x, int y, int nWidth, int nHeight,
                                       HWND hWndParent, HMENU hMenu, HINSTANCE hInstance,
                                       void* lpParam) noexcept {
    return TitanWin32kSubsystem::Instance().createWindowEx(
        dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam);
}

inline BOOL WINAPI NtUserDestroyWindow(HWND hWnd) noexcept {
    return TitanWin32kSubsystem::Instance().destroyWindow(hWnd);
}

inline ATOM WINAPI NtUserRegisterClassW(const wchar_t* className, WNDPROC proc) noexcept {
    return TitanWin32kSubsystem::Instance().registerWindowClass(className,proc);
}

inline LRESULT WINAPI NtUserDispatchMessage(const MSG* msg) noexcept {
    return msg ? TitanWin32kSubsystem::Instance().dispatchMessage(*msg) : 0;
}

inline LRESULT WINAPI NtUserDefWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    return TitanWin32kSubsystem::Instance().defWindowProc(hwnd,message,wParam,lParam);
}

inline intptr_t WINAPI NtUserSetWindowLongPtr(HWND hwnd, int32_t index, intptr_t value) noexcept {
    return TitanWin32kSubsystem::Instance().setWindowLongPtr(hwnd,index,value);
}

inline intptr_t WINAPI NtUserGetWindowLongPtr(HWND hwnd, int32_t index) noexcept {
    return TitanWin32kSubsystem::Instance().getWindowLongPtr(hwnd,index);
}

inline BOOL WINAPI NtUserShowWindow(HWND hWnd, int nCmdShow) noexcept {
    return TitanWin32kSubsystem::Instance().showWindow(hWnd, nCmdShow);
}

inline BOOL WINAPI NtUserSetWindowPos(HWND hWnd, HWND hWndInsertAfter,
                                     int x, int y, int cx, int cy, UINT uFlags) noexcept {
    return TitanWin32kSubsystem::Instance().setWindowPos(hWnd, hWndInsertAfter, x, y, cx, cy, uFlags);
}

inline BOOL WINAPI NtUserPostMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept {
    return TitanWin32kSubsystem::Instance().postMessage(hWnd, Msg, wParam, lParam);
}

inline void WINAPI NtUserPostQuitMessage(int32_t exitCode) noexcept {
    TitanWin32kSubsystem::Instance().postQuitMessage(exitCode);
}

inline LRESULT WINAPI NtUserSendMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept {
    return TitanWin32kSubsystem::Instance().sendMessage(hWnd, Msg, wParam, lParam);
}

inline BOOL WINAPI NtUserGetMessage(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) noexcept {
    if (!lpMsg) return FALSE_VAL;
    return TitanWin32kSubsystem::Instance().getMessage(*lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}

inline BOOL WINAPI NtUserPeekMessage(MSG* lpMsg, HWND hWnd, UINT wMsgFilterMin,
                                    UINT wMsgFilterMax, UINT wRemoveMsg) noexcept {
    if (!lpMsg) return FALSE_VAL;
    return TitanWin32kSubsystem::Instance().peekMessage(*lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

inline HWINSTA WINAPI NtUserCreateWindowStation(const wchar_t* lpwinsta, DWORD /*dwDesiredAccess*/,
                                               void* /*pAttributes*/) noexcept {
    std::wstring name = lpwinsta ? lpwinsta : L"WinSta0";
    return TitanWin32kSubsystem::Instance().createWindowStation(name, true);
}

inline HWINSTA WINAPI NtUserOpenWindowStation(const wchar_t* lpwinsta, DWORD /*dwDesiredAccess*/) noexcept {
    std::wstring name = lpwinsta ? lpwinsta : L"WinSta0";
    return TitanWin32kSubsystem::Instance().openWindowStation(name);
}

inline BOOL WINAPI NtUserSetProcessWindowStation(HWINSTA hWinSta) noexcept {
    return TitanWin32kSubsystem::Instance().setProcessWindowStation(hWinSta) ? TRUE_VAL : FALSE_VAL;
}

inline HWINSTA WINAPI NtUserGetProcessWindowStation() noexcept {
    return TitanWin32kSubsystem::Instance().getProcessWindowStation();
}

inline HDESK WINAPI NtUserCreateDesktop(const wchar_t* lpdesktop, DWORD /*dwFlags*/,
                                       DWORD /*dwDesiredAccess*/, void* /*pAttributes*/) noexcept {
    std::wstring name = lpdesktop ? lpdesktop : L"Default";
    return TitanWin32kSubsystem::Instance().createDesktop(name);
}

inline HDESK WINAPI NtUserOpenDesktop(const wchar_t* lpdesktop, DWORD /*dwFlags*/,
                                     DWORD /*dwDesiredAccess*/) noexcept {
    std::wstring name = lpdesktop ? lpdesktop : L"Default";
    return TitanWin32kSubsystem::Instance().openDesktop(name);
}

inline BOOL WINAPI NtUserSwitchDesktop(HDESK hDesktop) noexcept {
    return TitanWin32kSubsystem::Instance().switchDesktop(hDesktop) ? TRUE_VAL : FALSE_VAL;
}

inline HDC WINAPI NtUserGetDC(HWND hWnd) noexcept {
    return TitanWin32kSubsystem::Instance().getDC(hWnd);
}

inline BOOL WINAPI NtUserReleaseDC(HWND hWnd, HDC hDC) noexcept {
    return TitanWin32kSubsystem::Instance().releaseDC(hWnd,hDC);
}

inline HDC WINAPI NtGdiCreateCompatibleDC(HDC hdc) noexcept {
    return TitanWin32kSubsystem::Instance().getGdi().createCompatibleDC(hdc);
}

inline HBITMAP WINAPI NtGdiCreateCompatibleBitmap(HDC hdc, int cx, int cy) noexcept {
    if (cx <= 0 || cy <= 0) return nullptr;
    return TitanWin32kSubsystem::Instance().getGdi().createCompatibleBitmap(
        hdc, static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
}

inline void* WINAPI NtGdiSelectBitmap(HDC hdc, HBITMAP hbm) noexcept {
    return TitanWin32kSubsystem::Instance().getGdi().selectObject(hdc, hbm);
}

inline BOOL WINAPI NtGdiBitBlt(HDC hdcDst, int x, int y, int cx, int cy,
                              HDC hdcSrc, int x1, int y1, DWORD rop4) noexcept {
    return TitanWin32kSubsystem::Instance().getGdi().bitBlt(hdcDst, x, y, cx, cy, hdcSrc, x1, y1, rop4);
}

inline BOOL WINAPI NtGdiDeleteObjectApp(void* hobj) noexcept {
    return TitanWin32kSubsystem::Instance().getGdi().deleteObject(hobj);
}

inline void* WINAPI NtGdiGetStockObject(int i) noexcept {
    return TitanWin32kSubsystem::Instance().getGdi().getStockObject(i);
}

inline NtStatus NTAPI KeUserModeCallback(uint32_t ApiIndex, const void* ArgumentBuffer,
                                        uint32_t ArgumentLength, void** OutputBuffer,
                                        uint32_t* OutputLength) noexcept {
    LRESULT outRes = 0;
    return TitanWin32kSubsystem::Instance().getCallbacks().invokeUserModeCallback(
        ApiIndex, ArgumentBuffer, ArgumentLength, OutputBuffer, OutputLength, &outRes);
}

inline NtStatus NTAPI NtCallbackReturn(void* Result, uint32_t ResultLength, NtStatus Status) noexcept {
    return TitanWin32kSubsystem::Instance().getCallbacks().callbackReturn(
        0, Result, ResultLength, Status, 0);
}

inline BOOL WINAPI W32kGetTelemetry(WIN32K_TELEMETRY* outTelemetry) noexcept {
    if (!outTelemetry) return FALSE_VAL;
    *outTelemetry = TitanWin32kSubsystem::Instance().getTelemetry();
    return TRUE_VAL;
}

} // extern "C"

// C++ Helper outside extern "C" to avoid Clang -Wreturn-type-c-linkage warning
inline WIN32K_TELEMETRY W32kQueryTelemetry() noexcept {
    return TitanWin32kSubsystem::Instance().getTelemetry();
}

// ============================================================================
// 14. Subsystem Module Export Registration (JanusLDR, SCM, Version)
// ============================================================================

#if !defined(MICANT_KERNEL_WIN32K_CORE)
inline void InitializeWin32kSubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();

    // Register win32k.sys exports
    ldr.registerExport("win32k.sys", "NtUserCreateWindowEx", reinterpret_cast<void*>(NtUserCreateWindowEx));
    ldr.registerExport("win32k.sys", "NtUserDestroyWindow", reinterpret_cast<void*>(NtUserDestroyWindow));
    ldr.registerExport("win32k.sys", "NtUserShowWindow", reinterpret_cast<void*>(NtUserShowWindow));
    ldr.registerExport("win32k.sys", "NtUserSetWindowPos", reinterpret_cast<void*>(NtUserSetWindowPos));
    ldr.registerExport("win32k.sys", "NtUserPostMessage", reinterpret_cast<void*>(NtUserPostMessage));
    ldr.registerExport("win32k.sys", "NtUserSendMessage", reinterpret_cast<void*>(NtUserSendMessage));
    ldr.registerExport("win32k.sys", "NtUserGetMessage", reinterpret_cast<void*>(NtUserGetMessage));
    ldr.registerExport("win32k.sys", "NtUserPeekMessage", reinterpret_cast<void*>(NtUserPeekMessage));
    ldr.registerExport("win32k.sys", "NtUserCreateWindowStation", reinterpret_cast<void*>(NtUserCreateWindowStation));
    ldr.registerExport("win32k.sys", "NtUserOpenWindowStation", reinterpret_cast<void*>(NtUserOpenWindowStation));
    ldr.registerExport("win32k.sys", "NtUserSetProcessWindowStation", reinterpret_cast<void*>(NtUserSetProcessWindowStation));
    ldr.registerExport("win32k.sys", "NtUserGetProcessWindowStation", reinterpret_cast<void*>(NtUserGetProcessWindowStation));
    ldr.registerExport("win32k.sys", "NtUserCreateDesktop", reinterpret_cast<void*>(NtUserCreateDesktop));
    ldr.registerExport("win32k.sys", "NtUserOpenDesktop", reinterpret_cast<void*>(NtUserOpenDesktop));
    ldr.registerExport("win32k.sys", "NtUserSwitchDesktop", reinterpret_cast<void*>(NtUserSwitchDesktop));
    ldr.registerExport("win32k.sys", "NtUserGetDC", reinterpret_cast<void*>(NtUserGetDC));
    ldr.registerExport("win32k.sys", "NtUserReleaseDC", reinterpret_cast<void*>(NtUserReleaseDC));
    ldr.registerExport("win32k.sys", "NtGdiCreateCompatibleDC", reinterpret_cast<void*>(NtGdiCreateCompatibleDC));
    ldr.registerExport("win32k.sys", "NtGdiCreateCompatibleBitmap", reinterpret_cast<void*>(NtGdiCreateCompatibleBitmap));
    ldr.registerExport("win32k.sys", "NtGdiSelectBitmap", reinterpret_cast<void*>(NtGdiSelectBitmap));
    ldr.registerExport("win32k.sys", "NtGdiBitBlt", reinterpret_cast<void*>(NtGdiBitBlt));
    ldr.registerExport("win32k.sys", "NtGdiDeleteObjectApp", reinterpret_cast<void*>(NtGdiDeleteObjectApp));
    ldr.registerExport("win32k.sys", "NtGdiGetStockObject", reinterpret_cast<void*>(NtGdiGetStockObject));
    ldr.registerExport("win32k.sys", "KeUserModeCallback", reinterpret_cast<void*>(KeUserModeCallback));
    ldr.registerExport("win32k.sys", "NtCallbackReturn", reinterpret_cast<void*>(NtCallbackReturn));

    // Register modern split drivers: win32kbase.sys and win32kfull.sys
    ldr.registerExport("win32kbase.sys", "NtUserCreateWindowEx", reinterpret_cast<void*>(NtUserCreateWindowEx));
    ldr.registerExport("win32kbase.sys", "NtUserDestroyWindow", reinterpret_cast<void*>(NtUserDestroyWindow));
    ldr.registerExport("win32kbase.sys", "NtUserCreateWindowStation", reinterpret_cast<void*>(NtUserCreateWindowStation));
    ldr.registerExport("win32kbase.sys", "NtUserCreateDesktop", reinterpret_cast<void*>(NtUserCreateDesktop));

    ldr.registerExport("win32kfull.sys", "NtGdiBitBlt", reinterpret_cast<void*>(NtGdiBitBlt));
    ldr.registerExport("win32kfull.sys", "NtGdiCreateCompatibleDC", reinterpret_cast<void*>(NtGdiCreateCompatibleDC));
    ldr.registerExport("win32kfull.sys", "NtGdiCreateCompatibleBitmap", reinterpret_cast<void*>(NtGdiCreateCompatibleBitmap));
    ldr.registerExport("win32kfull.sys", "NtUserSendMessage", reinterpret_cast<void*>(NtUserSendMessage));

    // Register Service in SCM
    auto win32kSvc = std::make_shared<scm::ServiceRecord>();
    win32kSvc->serviceName = L"win32k";
    win32kSvc->displayName = L"Sovereign Win32k Multi-User Windowing and GDI Subsystem";
    win32kSvc->serviceType = scm::SERVICE_KERNEL_DRIVER;
    win32kSvc->startType = scm::SERVICE_SYSTEM_START;
    win32kSvc->errorControl = scm::SERVICE_ERROR_NORMAL;
    win32kSvc->binaryPath = L"\\SystemRoot\\System32\\win32k.sys";
    win32kSvc->status.dwCurrentState = scm::SERVICE_RUNNING;
    scm::ServiceControlManager::get().registerServiceRecord(win32kSvc);

    // Register in VersionDatabase
    version::VersionDatabase::Instance().RegisterModule(
        "win32k.sys", "10.0.26100.1", "Sovereign Win32k Multi-User Windowing and GDI Subsystem");
    version::VersionDatabase::Instance().RegisterModule(
        "win32kbase.sys", "10.0.26100.1", "Sovereign Win32k Base Driver");
    version::VersionDatabase::Instance().RegisterModule(
        "win32kfull.sys", "10.0.26100.1", "Sovereign Win32k Full Subsystem Driver");

    // Initialize the Subsystem singleton instance
    TitanWin32kSubsystem::Instance().initialize();
}
#endif

} // namespace micant::win32k

// Forward into parent micant namespace
namespace micant {
    using win32k::TitanWin32kSubsystem;
    using win32k::DesktopHeap;
    using win32k::Desktop;
    using win32k::WindowStation;
    using win32k::ThreadMessageQueue;
    using win32k::UserModeCallbackEngine;
    using win32k::GdiEngine;
    using win32k::WND;
    using win32k::CLIENT_WND_INFO;
    using win32k::WIN32K_TELEMETRY;
    using win32k::W32kQueryTelemetry;
    using win32k::W32kGetTelemetry;
#if !defined(MICANT_KERNEL_WIN32K_CORE)
    using win32k::InitializeWin32kSubsystemExports;
#endif
} // namespace micant
