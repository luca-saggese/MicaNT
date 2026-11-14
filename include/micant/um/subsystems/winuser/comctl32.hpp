// ============================================================================
// MicaNT: Win32 Common Controls Subsystem (comctl32.dll)
//
// Modern Clean-Room Implementation in pure ISO C++23.
// Provides Windows Standard Common Controls, Image Lists (HIMAGELIST),
// Progress Bars, Status Bars, UpDown / Spin Controls, TrackBars,
// ListViews, TreeViews, and InitCommonControlsEx Class Registration.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include "micant/shared/ntdef.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/winuser/user32.hpp"
#include "micant/um/subsystems/winuser/gdi32.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::comctl32 {

// ============================================================================
// 1. Data Types & Handle Definitions
// ============================================================================

using HIMAGELIST = void*;
using HTREEITEM  = void*;

using HWND      = win32::HWND;
using HDC       = user32::HDC;
using HBITMAP   = gdi32::HBITMAP;
using HICON     = win32::HICON;
using HINSTANCE = win32::HINSTANCE;
using UINT      = win32::UINT;
using DWORD     = win32::DWORD;
using BOOL      = win32::BOOL;
using LONG      = int32_t;
using WPARAM    = user32::WPARAM;
using LPARAM    = user32::LPARAM;
using LRESULT   = user32::LRESULT;
using LPCWSTR   = const wchar_t*;
using LPCSTR    = const char*;
using LPWSTR    = wchar_t*;
using LPSTR     = char*;

// Common Control Window Class Names
inline constexpr const wchar_t* PROGRESS_CLASSW  = L"msctls_progress32";
inline constexpr const wchar_t* STATUSCLASSNAMEW = L"msctls_statusbar32";
inline constexpr const wchar_t* UPDOWN_CLASSW    = L"msctls_updown32";
inline constexpr const wchar_t* TRACKBAR_CLASSW  = L"msctls_trackbar32";
inline constexpr const wchar_t* WC_LISTVIEWW     = L"SysListView32";
inline constexpr const wchar_t* WC_TREEVIEWW     = L"SysTreeView32";
inline constexpr const wchar_t* WC_TABCONTROLW   = L"SysTabControl32";

inline constexpr const char* PROGRESS_CLASSA  = "msctls_progress32";
inline constexpr const char* STATUSCLASSNAMEA = "msctls_statusbar32";
inline constexpr const char* UPDOWN_CLASSA    = "msctls_updown32";
inline constexpr const char* TRACKBAR_CLASSA  = "msctls_trackbar32";
inline constexpr const char* WC_LISTVIEWA     = "SysListView32";
inline constexpr const char* WC_TREEVIEWA     = "SysTreeView32";

// InitCommonControlsEx Flags
inline constexpr DWORD ICC_LISTVIEW_CLASSES   = 0x00000001;
inline constexpr DWORD ICC_TREEVIEW_CLASSES   = 0x00000002;
inline constexpr DWORD ICC_BAR_CLASSES        = 0x00000004;
inline constexpr DWORD ICC_TAB_CLASSES        = 0x00000008;
inline constexpr DWORD ICC_UPDOWN_CLASS       = 0x00000010;
inline constexpr DWORD ICC_PROGRESS_CLASS     = 0x00000020;
inline constexpr DWORD ICC_HOTKEY_CLASS       = 0x00000040;
inline constexpr DWORD ICC_ANIMATE_CLASS      = 0x00000080;
inline constexpr DWORD ICC_WIN95_CLASSES      = 0x000000FF;
inline constexpr DWORD ICC_DATE_CLASSES       = 0x00000100;
inline constexpr DWORD ICC_USEREX_CLASSES     = 0x00000200;
inline constexpr DWORD ICC_COOL_CLASSES       = 0x00000400;
inline constexpr DWORD ICC_INTERNET_CLASSES   = 0x00000800;
inline constexpr DWORD ICC_PAGESCROLLER_CLASS = 0x00001000;
inline constexpr DWORD ICC_NATIVEFNTCTL_CLASS = 0x00002000;
inline constexpr DWORD ICC_STANDARD_CLASSES   = 0x00004000;
inline constexpr DWORD ICC_LINK_CLASS         = 0x00008000;

struct INITCOMMONCONTROLSEX {
    DWORD dwSize{sizeof(INITCOMMONCONTROLSEX)};
    DWORD dwICC{ICC_WIN95_CLASSES};
};

// ImageList Creation & Drawing Flags
inline constexpr UINT ILC_MASK    = 0x00000001;
inline constexpr UINT ILC_COLOR   = 0x00000000;
inline constexpr UINT ILC_COLOR4  = 0x00000004;
inline constexpr UINT ILC_COLOR8  = 0x00000008;
inline constexpr UINT ILC_COLOR16 = 0x00000010;
inline constexpr UINT ILC_COLOR24 = 0x00000018;
inline constexpr UINT ILC_COLOR32 = 0x00000020;

inline constexpr UINT ILD_NORMAL      = 0x00000000;
inline constexpr UINT ILD_TRANSPARENT = 0x00000001;
inline constexpr UINT ILD_BLEND25     = 0x00000002;
inline constexpr UINT ILD_FOCUS       = 0x00000004;

// Progress Bar Messages
inline constexpr UINT PBM_SETRANGE    = 0x0401;
inline constexpr UINT PBM_SETPOS      = 0x0402;
inline constexpr UINT PBM_DELTAPOS    = 0x0403;
inline constexpr UINT PBM_SETSTEP     = 0x0404;
inline constexpr UINT PBM_STEPIT      = 0x0405;
inline constexpr UINT PBM_SETRANGE32  = 0x0406;
inline constexpr UINT PBM_GETRANGE    = 0x0407;
inline constexpr UINT PBM_GETPOS      = 0x0408;
inline constexpr UINT PBM_SETBARCOLOR = 0x0409;
inline constexpr UINT PBM_SETBKCOLOR  = 0x2001;

// Status Bar Messages
inline constexpr UINT SB_SETTEXTW       = 0x0401;
inline constexpr UINT SB_SETTEXTA       = 0x0401;
inline constexpr UINT SB_GETTEXTW       = 0x0402;
inline constexpr UINT SB_GETTEXTA       = 0x0402;
inline constexpr UINT SB_GETTEXTLENGTHW = 0x0403;
inline constexpr UINT SB_SETPARTS       = 0x0404;
inline constexpr UINT SB_GETPARTS       = 0x0406;
inline constexpr UINT SB_SIMPLE         = 0x0409;

// UpDown / Spin Messages
inline constexpr UINT UDM_SETRANGE   = 0x0465;
inline constexpr UINT UDM_GETRANGE   = 0x0466;
inline constexpr UINT UDM_SETPOS     = 0x0467;
inline constexpr UINT UDM_GETPOS     = 0x0468;
inline constexpr UINT UDM_SETBUDDY   = 0x0469;
inline constexpr UINT UDM_GETBUDDY   = 0x046A;
inline constexpr UINT UDM_SETRANGE32 = 0x046F;
inline constexpr UINT UDM_GETRANGE32 = 0x0470;

// TrackBar / Slider Messages
inline constexpr UINT TBM_GETPOS        = 0x0400;
inline constexpr UINT TBM_GETRANGEMIN   = 0x0401;
inline constexpr UINT TBM_GETRANGEMAX   = 0x0402;
inline constexpr UINT TBM_SETPOS        = 0x0405;
inline constexpr UINT TBM_SETRANGE      = 0x0406;
inline constexpr UINT TBM_SETRANGEMIN   = 0x0407;
inline constexpr UINT TBM_SETRANGEMAX   = 0x0408;

// ListView Messages & Structures
inline constexpr UINT LVM_GETITEMCOUNT    = 0x1004;
inline constexpr UINT LVM_INSERTITEMW     = 0x104D;
inline constexpr UINT LVM_SETITEMW        = 0x104C;
inline constexpr UINT LVM_GETITEMW        = 0x104B;
inline constexpr UINT LVM_DELETEITEM      = 0x1008;
inline constexpr UINT LVM_DELETEALLITEMS  = 0x1009;
inline constexpr UINT LVM_GETITEMTEXTW    = 0x1073;
inline constexpr UINT LVM_SETITEMTEXTW    = 0x1074;
inline constexpr UINT LVM_SETIMAGELIST    = 0x1003;
inline constexpr UINT LVM_GETIMAGELIST    = 0x1002;

inline constexpr UINT LVIF_TEXT       = 0x0001;
inline constexpr UINT LVIF_IMAGE      = 0x0002;
inline constexpr UINT LVIF_PARAM      = 0x0004;
inline constexpr UINT LVIF_STATE      = 0x0008;

struct LVITEMW {
    UINT    mask{0};
    int     iItem{0};
    int     iSubItem{0};
    UINT    state{0};
    UINT    stateMask{0};
    LPWSTR  pszText{nullptr};
    int     cchTextMax{0};
    int     iImage{0};
    LPARAM  lParam{0};
    int     iIndent{0};
    int     iGroupId{0};
    UINT    cColumns{0};
    UINT*   puColumns{nullptr};
    int*    piColFmt{nullptr};
    int     iGroup{0};
};

// TreeView Messages & Structures
inline constexpr UINT TVM_INSERTITEMW = 0x1132;
inline constexpr UINT TVM_DELETEITEM  = 0x1101;
inline constexpr UINT TVM_GETCOUNT    = 0x1105;
inline constexpr UINT TVM_GETITEMW    = 0x113E;
inline constexpr UINT TVM_SETITEMW    = 0x113F;
inline constexpr UINT TVM_GETNEXTITEM = 0x110A;

inline constexpr UINT TVIF_TEXT       = 0x0001;
inline constexpr UINT TVIF_IMAGE      = 0x0002;
inline constexpr UINT TVIF_PARAM      = 0x0004;
inline constexpr UINT TVIF_STATE      = 0x0008;
inline constexpr UINT TVIF_HANDLE     = 0x0010;

struct TVITEMW {
    UINT      mask{0};
    HTREEITEM hItem{nullptr};
    UINT      state{0};
    UINT      stateMask{0};
    LPWSTR    pszText{nullptr};
    int       cchTextMax{0};
    int       iImage{0};
    int       iSelectedImage{0};
    int       cChildren{0};
    LPARAM    lParam{0};
};

struct TVINSERTSTRUCTW {
    HTREEITEM hParent{nullptr};
    HTREEITEM hInsertAfter{nullptr};
    TVITEMW   item{};
};

inline const HTREEITEM TVI_ROOT  = reinterpret_cast<HTREEITEM>(static_cast<uintptr_t>(0xFFFF0000));
inline const HTREEITEM TVI_FIRST = reinterpret_cast<HTREEITEM>(static_cast<uintptr_t>(0xFFFF0001));
inline const HTREEITEM TVI_LAST  = reinterpret_cast<HTREEITEM>(static_cast<uintptr_t>(0xFFFF0002));
inline const HTREEITEM TVI_SORT  = reinterpret_cast<HTREEITEM>(static_cast<uintptr_t>(0xFFFF0003));


// ============================================================================
// 2. Image List Subsystem (HIMAGELIST)
// ============================================================================

class ImageListObject {
public:
    static constexpr size_t MaxImages = 128;
private:
    static constexpr size_t MaxBytes = 8 * 1024 * 1024;
    mutable std::mutex m_mutex;
    int m_cx{16};
    int m_cy{16};
    UINT m_flags{ILC_COLOR32};
    std::vector<std::shared_ptr<gdi32::GdiBitmap>> m_images;

public:
    ImageListObject(int cx, int cy, UINT flags)
        : m_cx(cx), m_cy(cy), m_flags(flags) {}

    [[nodiscard]] int getWidth() const noexcept { std::lock_guard lock(m_mutex); return m_cx; }
    [[nodiscard]] int getHeight() const noexcept { std::lock_guard lock(m_mutex); return m_cy; }
    [[nodiscard]] UINT getFlags() const noexcept { return m_flags; }
    [[nodiscard]] int getImageCount() const noexcept { std::lock_guard lock(m_mutex); return static_cast<int>(m_images.size()); }

    int add(HBITMAP hbmImage, HBITMAP hbmMask) {
        std::lock_guard lock(m_mutex);
        if (hbmMask) return -1;
        auto obj = gdi32::GdiEngine::get().getObject(hbmImage);
        auto bmp = std::dynamic_pointer_cast<gdi32::GdiBitmap>(obj);
        const size_t bytes = static_cast<size_t>(m_cx) * static_cast<size_t>(m_cy) * 4;
        if (!bmp || m_images.size() >= MaxImages || bmp->GetWidth() != static_cast<uint32_t>(m_cx) ||
            bmp->GetHeight() != static_cast<uint32_t>(m_cy) || bytes > MaxBytes - m_bytes) return -1;
        std::shared_ptr<gdi32::GdiBitmap> snapshot;
        try {
            snapshot = std::make_shared<gdi32::GdiBitmap>(bmp->GetWidth(), bmp->GetHeight(), 0);
            for (uint32_t y = 0; y < bmp->GetHeight(); ++y)
                for (uint32_t x = 0; x < bmp->GetWidth(); ++x)
                    snapshot->SetPixel(x, y, bmp->GetPixel(x, y));
            m_images.push_back(std::move(snapshot));
        } catch (...) { return -1; }
        int index = static_cast<int>(m_images.size()) - 1;
        m_bytes += bytes;
        return index;
    }

    int addIcon(HICON /*hicon*/) { return -1; }

    bool replaceIcon(int /*i*/, HICON /*hicon*/) { return false; }

    bool draw(int i, HDC hdcDst, int x, int y, UINT fStyle) {
        if (fStyle != ILD_NORMAL && fStyle != ILD_TRANSPARENT) return false;
        std::shared_ptr<gdi32::GdiBitmap> bmp;
        int width{}, height{};
        {
            std::lock_guard lock(m_mutex);
            if (i < 0 || static_cast<size_t>(i) >= m_images.size()) return false;
            bmp = m_images[static_cast<size_t>(i)];
            width = m_cx; height = m_cy;
        }
        auto dc = gdi32::GdiEngine::get().getDc(reinterpret_cast<gdi32::HDC>(hdcDst));
        if (!dc) return false;
        if (!bmp) return false;

        // Blit icon frame into destination DC
        for (uint32_t row = 0; row < static_cast<uint32_t>(height); ++row) {
            for (uint32_t col = 0; col < static_cast<uint32_t>(width); ++col) {
                uint32_t color = bmp->GetPixel(col, row);
                dc->SetPixel(x + static_cast<int>(col), y + static_cast<int>(row), color);
            }
        }
        dc->FlushIfWindow();
        return true;
    }

    bool remove(int i) {
        std::lock_guard lock(m_mutex);
        if (i == -1) { m_images.clear(); m_bytes = 0; return true; }
        if (i < 0 || static_cast<size_t>(i) >= m_images.size()) return false;
        m_images.erase(m_images.begin() + i);
        m_bytes -= static_cast<size_t>(m_cx) * static_cast<size_t>(m_cy) * 4;
        return true;
    }

    bool setIconSize(int cx, int cy) {
        std::lock_guard lock(m_mutex);
        if (!m_images.empty() || cx <= 0 || cy <= 0 || cx > 256 || cy > 256) return false;
        m_cx = cx; m_cy = cy; return true;
    }

private:
    size_t m_bytes{};
};

class ImageListRegistry {
private:
    std::mutex m_mutex;
    std::unordered_map<uintptr_t, std::shared_ptr<ImageListObject>> m_lists;
    uintptr_t m_nextHandle{0x5000};

    ImageListRegistry() = default;

public:
    static ImageListRegistry& get() {
        static ImageListRegistry instance;
        return instance;
    }

    HIMAGELIST create(int cx, int cy, UINT flags, int initial, int grow) {
        constexpr UINT supportedFlags = ILC_MASK | ILC_COLOR4 | ILC_COLOR8 | ILC_COLOR16 |
            ILC_COLOR24 | ILC_COLOR32;
        if (cx <= 0 || cy <= 0 || cx > 256 || cy > 256 || initial < 0 || grow < 0 ||
            initial > 128 || grow > 128 || (flags & ~supportedFlags) || (flags & ILC_MASK) ||
            (static_cast<size_t>(cx) * cy * 4 * ImageListObject::MaxImages) > 8 * 1024 * 1024)
            return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lists.size() >= 64 || m_nextHandle == UINTPTR_MAX) return nullptr;
        uintptr_t handle = m_nextHandle++;
        try { m_lists[handle] = std::make_shared<ImageListObject>(cx, cy, flags); }
        catch (...) { return nullptr; }
        return reinterpret_cast<HIMAGELIST>(handle);
    }

    bool destroy(HIMAGELIST himl) {
        if (!himl) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lists.erase(reinterpret_cast<uintptr_t>(himl)) > 0;
    }

    std::shared_ptr<ImageListObject> getList(HIMAGELIST himl) {
        if (!himl) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_lists.find(reinterpret_cast<uintptr_t>(himl));
        return (it != m_lists.end()) ? it->second : nullptr;
    }
};

inline HIMAGELIST ImageList_Create(int cx, int cy, UINT flags, int cInitial, int cGrow) noexcept {
    return ImageListRegistry::get().create(cx, cy, flags, cInitial, cGrow);
}

inline BOOL ImageList_Destroy(HIMAGELIST himl) noexcept {
    return ImageListRegistry::get().destroy(himl) ? win32::TRUE : win32::FALSE;
}

inline int ImageList_GetImageCount(HIMAGELIST himl) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    return list ? list->getImageCount() : 0;
}

inline int ImageList_Add(HIMAGELIST himl, HBITMAP hbmImage, HBITMAP hbmMask) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    return list ? list->add(hbmImage, hbmMask) : -1;
}

inline int ImageList_AddIcon(HIMAGELIST himl, HICON hicon) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    return list ? list->addIcon(hicon) : -1;
}

inline int ImageList_ReplaceIcon(HIMAGELIST himl, int i, HICON hicon) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    return (list && list->replaceIcon(i, hicon)) ? i : -1;
}

inline BOOL ImageList_GetIconSize(HIMAGELIST himl, int* cx, int* cy) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    if (!list) return win32::FALSE;
    if (cx) *cx = list->getWidth();
    if (cy) *cy = list->getHeight();
    return win32::TRUE;
}

inline BOOL ImageList_Draw(HIMAGELIST himl, int i, HDC hdcDst, int x, int y, UINT fStyle) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    return (list && list->draw(i, hdcDst, x, y, fStyle)) ? win32::TRUE : win32::FALSE;
}

// ============================================================================
// 3. Control Window State Stores & Window Procedures
// ============================================================================

// --- Progress Bar State ---
struct ProgressBarState {
    int32_t rangeMin{0};
    int32_t rangeMax{100};
    int32_t pos{0};
    int32_t step{10};
    gdi32::COLORREF barColor{gdi32::RGB(0, 120, 215)}; // Windows Blue
    gdi32::COLORREF bkColor{gdi32::RGB(230, 230, 230)};  // Light Gray
};

inline std::mutex g_ProgressBarMutex;
inline std::unordered_map<HWND, ProgressBarState> g_ProgressBars;

inline LRESULT ProgressBarWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    std::lock_guard<std::mutex> lock(g_ProgressBarMutex);
    auto& state = g_ProgressBars[hWnd];

    switch (uMsg) {
        case user32::WM_CREATE:
            state = ProgressBarState{};
            return 0;

        case user32::WM_DESTROY:
            g_ProgressBars.erase(hWnd);
            return 0;

        case PBM_SETRANGE: {
            state.rangeMin = static_cast<int16_t>(lParam & 0xFFFF);
            state.rangeMax = static_cast<int16_t>((lParam >> 16) & 0xFFFF);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return 1;
        }

        case PBM_SETRANGE32: {
            int32_t oldMin = state.rangeMin;
            state.rangeMin = static_cast<int32_t>(wParam);
            state.rangeMax = static_cast<int32_t>(lParam);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return oldMin;
        }

        case PBM_SETPOS: {
            int32_t oldPos = state.pos;
            state.pos = std::clamp(static_cast<int32_t>(wParam), state.rangeMin, state.rangeMax);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return oldPos;
        }

        case PBM_GETPOS:
            return state.pos;

        case PBM_DELTAPOS: {
            int32_t oldPos = state.pos;
            state.pos = std::clamp(state.pos + static_cast<int32_t>(wParam), state.rangeMin, state.rangeMax);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return oldPos;
        }

        case PBM_SETSTEP: {
            int32_t oldStep = state.step;
            state.step = static_cast<int32_t>(wParam);
            return oldStep;
        }

        case PBM_STEPIT: {
            int32_t oldPos = state.pos;
            state.pos = std::clamp(state.pos + state.step, state.rangeMin, state.rangeMax);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return oldPos;
        }

        case PBM_SETBARCOLOR: {
            gdi32::COLORREF old = state.barColor;
            state.barColor = static_cast<gdi32::COLORREF>(lParam);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return old;
        }

        case PBM_SETBKCOLOR: {
            gdi32::COLORREF old = state.bkColor;
            state.bkColor = static_cast<gdi32::COLORREF>(lParam);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return old;
        }

        case user32::WM_PAINT: {
            user32::PAINTSTRUCT ps{};
            HDC hdc = user32::BeginPaint(hWnd, &ps);
            if (hdc) {
                user32::RECT rc{};
                user32::GetClientRect(hWnd, &rc);

                // Draw background
                gdi32::HBRUSH hbrBk = gdi32::CreateSolidBrush(state.bkColor);
                gdi32::FillRect(reinterpret_cast<gdi32::HDC>(hdc), reinterpret_cast<const gdi32::RECT*>(&rc), hbrBk);
                gdi32::DeleteObject(hbrBk);

                // Draw filled portion
                int32_t totalRange = state.rangeMax - state.rangeMin;
                if (totalRange > 0 && state.pos > state.rangeMin) {
                    int32_t fillWidth = (rc.right * (state.pos - state.rangeMin)) / totalRange;
                    gdi32::RECT fillRc{0, 0, fillWidth, rc.bottom};
                    gdi32::HBRUSH hbrBar = gdi32::CreateSolidBrush(state.barColor);
                    gdi32::FillRect(reinterpret_cast<gdi32::HDC>(hdc), &fillRc, hbrBar);
                    gdi32::DeleteObject(hbrBar);
                }

                user32::EndPaint(hWnd, &ps);
            }
            return 0;
        }

        default:
            return user32::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

// --- Status Bar State ---
struct StatusBarState {
    std::vector<int> partWidths;
    std::vector<std::wstring> partTexts;
    bool simpleMode{false};
};

inline std::mutex g_StatusBarMutex;
inline std::unordered_map<HWND, StatusBarState> g_StatusBars;

inline LRESULT StatusBarWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    std::lock_guard<std::mutex> lock(g_StatusBarMutex);
    auto& state = g_StatusBars[hWnd];

    switch (uMsg) {
        case user32::WM_CREATE:
            state.partWidths = { -1 };
            state.partTexts = { L"" };
            return 0;

        case user32::WM_DESTROY:
            g_StatusBars.erase(hWnd);
            return 0;

        case SB_SETPARTS: {
            int numParts = static_cast<int>(wParam);
            const int* pWidths = reinterpret_cast<const int*>(lParam);
            if (numParts <= 0 || !pWidths) return win32::FALSE;

            state.partWidths.assign(pWidths, pWidths + numParts);
            state.partTexts.resize(numParts, L"");
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return win32::TRUE;
        }

        case SB_GETPARTS: {
            int nParts = static_cast<int>(wParam);
            int* pWidths = reinterpret_cast<int*>(lParam);
            int total = static_cast<int>(state.partWidths.size());
            if (pWidths && nParts > 0) {
                int count = std::min(nParts, total);
                for (int i = 0; i < count; ++i) {
                    pWidths[i] = state.partWidths[i];
                }
            }
            return total;
        }

        case SB_SETTEXTW: {
            int partIdx = static_cast<int>(wParam & 0x00FF);
            const wchar_t* text = reinterpret_cast<const wchar_t*>(lParam);
            if (partIdx >= static_cast<int>(state.partTexts.size())) {
                state.partTexts.resize(partIdx + 1, L"");
                state.partWidths.resize(partIdx + 1, -1);
            }
            state.partTexts[partIdx] = text ? text : L"";
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return win32::TRUE;
        }

        case SB_GETTEXTW: {
            int partIdx = static_cast<int>(wParam);
            wchar_t* outBuf = reinterpret_cast<wchar_t*>(lParam);
            if (partIdx < 0 || partIdx >= static_cast<int>(state.partTexts.size())) {
                return 0;
            }
            const auto& str = state.partTexts[partIdx];
            if (outBuf) {
                std::wcscpy(outBuf, str.c_str());
            }
            return str.size();
        }

        case SB_GETTEXTLENGTHW: {
            int partIdx = static_cast<int>(wParam);
            if (partIdx < 0 || partIdx >= static_cast<int>(state.partTexts.size())) {
                return 0;
            }
            return state.partTexts[partIdx].size();
        }

        case SB_SIMPLE: {
            state.simpleMode = (wParam != 0);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return 1;
        }

        case user32::WM_PAINT: {
            user32::PAINTSTRUCT ps{};
            HDC hdc = user32::BeginPaint(hWnd, &ps);
            if (hdc) {
                user32::RECT rc{};
                user32::GetClientRect(hWnd, &rc);

                // Background
                gdi32::HBRUSH hbr = gdi32::CreateSolidBrush(gdi32::RGB(240, 240, 240));
                gdi32::FillRect(reinterpret_cast<gdi32::HDC>(hdc), reinterpret_cast<const gdi32::RECT*>(&rc), hbr);
                gdi32::DeleteObject(hbr);

                // Text
                int curX = 4;
                for (size_t i = 0; i < state.partTexts.size(); ++i) {
                    const auto& txt = state.partTexts[i];
                    if (!txt.empty()) {
                        gdi32::TextOutW(reinterpret_cast<gdi32::HDC>(hdc), curX, 4, txt.c_str(), static_cast<int>(txt.size()));
                    }
                    int w = (i < state.partWidths.size()) ? state.partWidths[i] : -1;
                    curX = (w > 0) ? w + 4 : (curX + 100);
                }

                user32::EndPaint(hWnd, &ps);
            }
            return 0;
        }

        default:
            return user32::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

// --- UpDown / Spin Control State ---
struct UpDownState {
    int32_t rangeMin{0};
    int32_t rangeMax{100};
    int32_t pos{0};
    HWND buddyHwnd{nullptr};
};

inline std::mutex g_UpDownMutex;
inline std::unordered_map<HWND, UpDownState> g_UpDowns;

inline LRESULT UpDownWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    std::lock_guard<std::mutex> lock(g_UpDownMutex);
    auto& state = g_UpDowns[hWnd];

    switch (uMsg) {
        case user32::WM_CREATE:
            state = UpDownState{};
            return 0;

        case user32::WM_DESTROY:
            g_UpDowns.erase(hWnd);
            return 0;

        case UDM_SETRANGE:
            state.rangeMin = static_cast<int16_t>((lParam >> 16) & 0xFFFF);
            state.rangeMax = static_cast<int16_t>(lParam & 0xFFFF);
            return 0;

        case UDM_GETRANGE:
            return static_cast<LRESULT>((static_cast<uint16_t>(state.rangeMin) << 16) | (static_cast<uint16_t>(state.rangeMax)));

        case UDM_SETRANGE32:
            state.rangeMin = static_cast<int32_t>(wParam);
            state.rangeMax = static_cast<int32_t>(lParam);
            return 0;

        case UDM_GETRANGE32:
            if (wParam) *reinterpret_cast<int32_t*>(wParam) = state.rangeMin;
            if (lParam) *reinterpret_cast<int32_t*>(lParam) = state.rangeMax;
            return 0;

        case UDM_SETPOS: {
            int32_t oldPos = state.pos;
            state.pos = std::clamp(static_cast<int32_t>(lParam), state.rangeMin, state.rangeMax);
            return oldPos;
        }

        case UDM_GETPOS:
            return state.pos;

        case UDM_SETBUDDY: {
            HWND oldBuddy = state.buddyHwnd;
            state.buddyHwnd = reinterpret_cast<HWND>(wParam);
            return reinterpret_cast<LRESULT>(oldBuddy);
        }

        case UDM_GETBUDDY:
            return reinterpret_cast<LRESULT>(state.buddyHwnd);

        default:
            return user32::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

// --- TrackBar / Slider State ---
struct TrackBarState {
    int32_t rangeMin{0};
    int32_t rangeMax{100};
    int32_t pos{0};
};

inline std::mutex g_TrackBarMutex;
inline std::unordered_map<HWND, TrackBarState> g_TrackBars;

inline LRESULT TrackBarWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    std::lock_guard<std::mutex> lock(g_TrackBarMutex);
    auto& state = g_TrackBars[hWnd];

    switch (uMsg) {
        case user32::WM_CREATE:
            state = TrackBarState{};
            return 0;

        case user32::WM_DESTROY:
            g_TrackBars.erase(hWnd);
            return 0;

        case TBM_GETPOS:
            return state.pos;

        case TBM_SETPOS:
            state.pos = std::clamp(static_cast<int32_t>(lParam), state.rangeMin, state.rangeMax);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return 0;

        case TBM_GETRANGEMIN:
            return state.rangeMin;

        case TBM_GETRANGEMAX:
            return state.rangeMax;

        case TBM_SETRANGE:
            state.rangeMin = static_cast<int16_t>(lParam & 0xFFFF);
            state.rangeMax = static_cast<int16_t>((lParam >> 16) & 0xFFFF);
            state.pos = std::clamp(state.pos, state.rangeMin, state.rangeMax);
            user32::InvalidateRect(hWnd, nullptr, win32::TRUE);
            return 0;

        case TBM_SETRANGEMIN:
            state.rangeMin = static_cast<int32_t>(lParam);
            state.pos = std::clamp(state.pos, state.rangeMin, state.rangeMax);
            return 0;

        case TBM_SETRANGEMAX:
            state.rangeMax = static_cast<int32_t>(lParam);
            state.pos = std::clamp(state.pos, state.rangeMin, state.rangeMax);
            return 0;

        default:
            return user32::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

// --- ListView State ---
struct ListViewItemInternal {
    std::wstring text;
    int imageIndex{-1};
    LPARAM lParam{0};
};

struct ListViewState {
    std::vector<ListViewItemInternal> items;
    HIMAGELIST himl{nullptr};
};

inline std::mutex g_ListViewMutex;
inline std::unordered_map<HWND, ListViewState> g_ListViews;

inline LRESULT ListViewWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    std::lock_guard<std::mutex> lock(g_ListViewMutex);
    auto& state = g_ListViews[hWnd];

    switch (uMsg) {
        case user32::WM_CREATE:
            state = ListViewState{};
            return 0;

        case user32::WM_DESTROY:
            g_ListViews.erase(hWnd);
            return 0;

        case LVM_GETITEMCOUNT:
            return static_cast<LRESULT>(state.items.size());

        case LVM_INSERTITEMW: {
            const auto* pItem = reinterpret_cast<const LVITEMW*>(lParam);
            if (!pItem) return -1;
            ListViewItemInternal item{};
            if ((pItem->mask & LVIF_TEXT) && pItem->pszText) {
                item.text = pItem->pszText;
            }
            if (pItem->mask & LVIF_IMAGE) item.imageIndex = pItem->iImage;
            if (pItem->mask & LVIF_PARAM) item.lParam = pItem->lParam;

            int insertIdx = std::clamp(pItem->iItem, 0, static_cast<int>(state.items.size()));
            state.items.insert(state.items.begin() + insertIdx, std::move(item));
            return insertIdx;
        }

        case LVM_SETITEMW: {
            const auto* pItem = reinterpret_cast<const LVITEMW*>(lParam);
            if (!pItem || pItem->iItem < 0 || static_cast<size_t>(pItem->iItem) >= state.items.size()) {
                return win32::FALSE;
            }
            auto& item = state.items[pItem->iItem];
            if ((pItem->mask & LVIF_TEXT) && pItem->pszText) item.text = pItem->pszText;
            if (pItem->mask & LVIF_IMAGE) item.imageIndex = pItem->iImage;
            if (pItem->mask & LVIF_PARAM) item.lParam = pItem->lParam;
            return win32::TRUE;
        }

        case LVM_GETITEMW: {
            auto* pItem = reinterpret_cast<LVITEMW*>(lParam);
            if (!pItem || pItem->iItem < 0 || static_cast<size_t>(pItem->iItem) >= state.items.size()) {
                return win32::FALSE;
            }
            const auto& item = state.items[pItem->iItem];
            if ((pItem->mask & LVIF_TEXT) && pItem->pszText && pItem->cchTextMax > 0) {
                std::wcsncpy(pItem->pszText, item.text.c_str(), static_cast<size_t>(pItem->cchTextMax));
                pItem->pszText[pItem->cchTextMax - 1] = L'\0';
            }
            if (pItem->mask & LVIF_IMAGE) pItem->iImage = item.imageIndex;
            if (pItem->mask & LVIF_PARAM) pItem->lParam = item.lParam;
            return win32::TRUE;
        }

        case LVM_DELETEITEM: {
            int idx = static_cast<int>(wParam);
            if (idx < 0 || static_cast<size_t>(idx) >= state.items.size()) return win32::FALSE;
            state.items.erase(state.items.begin() + idx);
            return win32::TRUE;
        }

        case LVM_DELETEALLITEMS:
            state.items.clear();
            return win32::TRUE;

        case LVM_SETIMAGELIST: {
            HIMAGELIST old = state.himl;
            state.himl = reinterpret_cast<HIMAGELIST>(lParam);
            return reinterpret_cast<LRESULT>(old);
        }

        case LVM_GETIMAGELIST:
            return reinterpret_cast<LRESULT>(state.himl);

        default:
            return user32::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

// --- TreeView State ---
struct TreeViewNode {
    uintptr_t handle{0};
    uintptr_t parentHandle{0};
    std::wstring text;
    int imageIndex{-1};
    LPARAM lParam{0};
};

struct TreeViewState {
    std::unordered_map<uintptr_t, TreeViewNode> nodes;
    uintptr_t nextNodeId{1};
};

inline std::mutex g_TreeViewMutex;
inline std::unordered_map<HWND, TreeViewState> g_TreeViews;

inline LRESULT TreeViewWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept {
    std::lock_guard<std::mutex> lock(g_TreeViewMutex);
    auto& state = g_TreeViews[hWnd];

    switch (uMsg) {
        case user32::WM_CREATE:
            state = TreeViewState{};
            return 0;

        case user32::WM_DESTROY:
            g_TreeViews.erase(hWnd);
            return 0;

        case TVM_INSERTITEMW: {
            const auto* pInsert = reinterpret_cast<const TVINSERTSTRUCTW*>(lParam);
            if (!pInsert) return 0;
            uintptr_t id = state.nextNodeId++;
            TreeViewNode node{};
            node.handle = id;
            node.parentHandle = reinterpret_cast<uintptr_t>(pInsert->hParent);
            if ((pInsert->item.mask & TVIF_TEXT) && pInsert->item.pszText) {
                node.text = pInsert->item.pszText;
            }
            if (pInsert->item.mask & TVIF_IMAGE) node.imageIndex = pInsert->item.iImage;
            if (pInsert->item.mask & TVIF_PARAM) node.lParam = pInsert->item.lParam;

            state.nodes[id] = std::move(node);
            return static_cast<LRESULT>(id);
        }

        case TVM_GETCOUNT:
            return static_cast<LRESULT>(state.nodes.size());

        case TVM_DELETEITEM: {
            uintptr_t hNode = static_cast<uintptr_t>(lParam);
            if (hNode == 0xFFFF0000 /* TVI_ROOT */ || hNode == 0) {
                state.nodes.clear();
                return win32::TRUE;
            }
            return state.nodes.erase(hNode) > 0 ? win32::TRUE : win32::FALSE;
        }

        case TVM_GETITEMW: {
            auto* pItem = reinterpret_cast<TVITEMW*>(lParam);
            if (!pItem) return win32::FALSE;
            uintptr_t hNode = reinterpret_cast<uintptr_t>(pItem->hItem);
            auto it = state.nodes.find(hNode);
            if (it == state.nodes.end()) return win32::FALSE;

            if ((pItem->mask & TVIF_TEXT) && pItem->pszText && pItem->cchTextMax > 0) {
                std::wcsncpy(pItem->pszText, it->second.text.c_str(), static_cast<size_t>(pItem->cchTextMax));
                pItem->pszText[pItem->cchTextMax - 1] = L'\0';
            }
            if (pItem->mask & TVIF_IMAGE) pItem->iImage = it->second.imageIndex;
            if (pItem->mask & TVIF_PARAM) pItem->lParam = it->second.lParam;
            return win32::TRUE;
        }

        default:
            return user32::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

// ============================================================================
// 4. Common Control Factory & Helper Functions
// ============================================================================

inline HWND CreateStatusWindowW(LONG style, LPCWSTR lpszText, HWND hwndParent, UINT wID) noexcept {
    HWND hWnd = user32::CreateWindowExW(
        0, STATUSCLASSNAMEW, lpszText, static_cast<uint32_t>(style),
        0, 0, 0, 0, hwndParent, reinterpret_cast<user32::HMENU>(static_cast<uintptr_t>(wID)),
        nullptr, nullptr
    );
    if (hWnd && lpszText && *lpszText) {
        user32::SendMessageW(hWnd, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(lpszText));
    }
    return hWnd;
}

inline HWND CreateStatusWindowA(LONG style, LPCSTR lpszText, HWND hwndParent, UINT wID) noexcept {
    if (!lpszText) return CreateStatusWindowW(style, nullptr, hwndParent, wID);
    std::wstring wText(lpszText, lpszText + std::strlen(lpszText));
    return CreateStatusWindowW(style, wText.c_str(), hwndParent, wID);
}

inline HWND CreateUpDownControl(
    DWORD dwStyle,
    int x, int y, int cx, int cy,
    HWND hParent,
    int nID,
    HINSTANCE hInst,
    HWND hBuddy,
    int nUpper, int nLower, int nPos
) noexcept {
    HWND hWnd = user32::CreateWindowExW(
        0, UPDOWN_CLASSW, L"", dwStyle,
        x, y, cx, cy, hParent, reinterpret_cast<user32::HMENU>(static_cast<uintptr_t>(nID)),
        hInst, nullptr
    );
    if (hWnd) {
        if (hBuddy) user32::SendMessageW(hWnd, UDM_SETBUDDY, reinterpret_cast<WPARAM>(hBuddy), 0);
        user32::SendMessageW(hWnd, UDM_SETRANGE32, static_cast<WPARAM>(nLower), static_cast<LPARAM>(nUpper));
        user32::SendMessageW(hWnd, UDM_SETPOS, 0, static_cast<LPARAM>(nPos));
    }
    return hWnd;
}

// ============================================================================
// 5. Common Controls Initialization & Subsystem Export Registration
// ============================================================================

inline BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX* lpInitCtrls) noexcept {
    (void)lpInitCtrls;

    // Register all standard common control classes in WindowManager
    user32::WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);

    wc.lpszClassName = PROGRESS_CLASSW;
    wc.lpfnWndProc = ProgressBarWndProc;
    user32::RegisterClassExW(&wc);

    wc.lpszClassName = STATUSCLASSNAMEW;
    wc.lpfnWndProc = StatusBarWndProc;
    user32::RegisterClassExW(&wc);

    wc.lpszClassName = UPDOWN_CLASSW;
    wc.lpfnWndProc = UpDownWndProc;
    user32::RegisterClassExW(&wc);

    wc.lpszClassName = TRACKBAR_CLASSW;
    wc.lpfnWndProc = TrackBarWndProc;
    user32::RegisterClassExW(&wc);

    wc.lpszClassName = WC_LISTVIEWW;
    wc.lpfnWndProc = ListViewWndProc;
    user32::RegisterClassExW(&wc);

    wc.lpszClassName = WC_TREEVIEWW;
    wc.lpfnWndProc = TreeViewWndProc;
    user32::RegisterClassExW(&wc);

    return win32::TRUE;
}

inline void InitCommonControls() noexcept {
    INITCOMMONCONTROLSEX icce{};
    InitCommonControlsEx(&icce);
}

inline void ImageList_EndDrag() noexcept {}
inline BOOL ImageList_DragShowNolock(BOOL /*fShow*/) noexcept { return win32::FALSE; }
inline BOOL ImageList_DragEnter(HWND /*hwndLockWindow*/, int /*x*/, int /*y*/) noexcept { return win32::FALSE; }
inline BOOL ImageList_DragMove(int /*x*/, int /*y*/) noexcept { return win32::FALSE; }
inline BOOL ImageList_BeginDrag(HIMAGELIST /*himlTrack*/, int /*iTrack*/, int /*dxHotspot*/, int /*dyHotspot*/) noexcept { return win32::FALSE; }
inline BOOL ImageList_Remove(HIMAGELIST himl, int i) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    return (list && list->remove(i)) ? win32::TRUE : win32::FALSE;
}
inline BOOL ImageList_SetIconSize(HIMAGELIST himl, int cx, int cy) noexcept {
    auto list = ImageListRegistry::get().getList(himl);
    return (list && list->setIconSize(cx, cy)) ? win32::TRUE : win32::FALSE;
}
inline int ImageList_AddMasked(HIMAGELIST /*himl*/, HBITMAP /*hbmImage*/, DWORD /*crMask*/) noexcept { return -1; }
inline HICON ImageList_GetIcon(HIMAGELIST /*himl*/, int /*i*/, UINT /*flags*/) noexcept { return nullptr; }

struct IMAGEINFO {
    HBITMAP hbmImage{nullptr};
    HBITMAP hbmMask{nullptr};
    int     Unused1{0};
    int     Unused2{0};
    user32::RECT rcImage{0, 0, 16, 16};
};

inline BOOL ImageList_GetImageInfo(HIMAGELIST /*himl*/, int /*i*/, IMAGEINFO* /*pImageInfo*/) noexcept {
    return win32::FALSE;
}

struct TRACKMOUSEEVENT {
    DWORD cbSize{sizeof(TRACKMOUSEEVENT)};
    DWORD dwFlags{0};
    HWND  hwndTrack{nullptr};
    DWORD dwHoverTime{0};
};

inline BOOL _TrackMouseEvent(TRACKMOUSEEVENT* /*lpEventTrack*/) noexcept {
    return win32::TRUE;
}

inline intptr_t ComCtlOrdinalStub(void* /*p1*/, void* /*p2*/, void* /*p3*/, void* /*p4*/) noexcept {
    return 0;
}

inline win32::HWND CreateToolbarEx(
    win32::HWND /*hwnd*/, uint32_t /*ws*/, uint32_t /*wID*/, int /*nBitmaps*/,
    void* /*hBMInst*/, uintptr_t /*wBMID*/, void* /*lpButtons*/, int /*iNumButtons*/,
    int /*dxButton*/, int /*dyButton*/, int /*dxBitmap*/, int /*dyBitmap*/, uint32_t /*uStructSize*/
) noexcept {
    return reinterpret_cast<win32::HWND>(0x8001);
}

inline intptr_t PropertySheetW(void* /*lppsph*/) noexcept {
    return 0; // IDOK
}

inline void* CreatePropertySheetPageW(const void* /*constPropSheetPage*/) noexcept {
    static uintptr_t s_psp = 0x8890;
    return reinterpret_cast<void*>(++s_psp);
}

inline win32::BOOL WINAPI DestroyPropertySheetPage(void* /*hPSPage*/) noexcept {
    return win32::TRUE;
}

inline win32::HRESULT WINAPI ImageList_CoCreateInstance(const GUID* /*rclsid*/, void* /*punkOuter*/, const GUID* /*riid*/, void** ppv) noexcept {
    if (ppv) *ppv = reinterpret_cast<void*>(0x8301);
    return 0; // S_OK
}

inline void* WINAPI ImageList_Read_Ordinal13(void* /*pstm*/) noexcept {
    return reinterpret_cast<void*>(0x8301);
}

inline win32::BOOL WINAPI ImageList_Write_Ordinal14(void* /*himl*/, void* /*pstm*/) noexcept {
    return win32::TRUE;
}

inline win32::HRESULT WINAPI ImageList_ReadEx_Ordinal15(uint32_t /*dwFlags*/, void* /*pstm*/, const GUID* /*riid*/, void** ppv) noexcept {
    if (ppv) *ppv = reinterpret_cast<void*>(0x8301);
    return 0; // S_OK
}

inline win32::BOOL FlatSB_SetScrollInfo(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::TRUE; }
inline win32::BOOL FlatSB_SetScrollProp(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::TRUE; }
inline win32::BOOL ImageList_GetDragImage(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_DrawEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_SetImageCount(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL FlatSB_GetScrollPos(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::TRUE; }
inline win32::BOOL FlatSB_SetScrollPos(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::TRUE; }
inline win32::BOOL InitializeFlatSB(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::TRUE; }
inline win32::BOOL ImageList_Copy(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL FlatSB_GetScrollInfo(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::TRUE; }
inline win32::BOOL ImageList_Write(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_DrawIndirect(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_SetBkColor(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_GetBkColor(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_Replace(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_SetDragCursorImage(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_Read(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_DragLeave(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::TRUE; }
inline win32::BOOL ImageList_LoadImageW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }
inline win32::BOOL ImageList_SetOverlayImage(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return win32::FALSE; }

inline void InitializeComCtl32SubsystemExports() {
    InitCommonControls();

    auto& ldr = ldr::DynamicLoader::get();

    ldr.registerExport("comctl32.dll", "CreateToolbarEx", reinterpret_cast<void*>(CreateToolbarEx));
    ldr.registerExport("comctl32.dll", "PropertySheetW", reinterpret_cast<void*>(PropertySheetW));
    ldr.registerExport("comctl32.dll", "CreatePropertySheetPageW", reinterpret_cast<void*>(CreatePropertySheetPageW));

    ldr.registerExport("comctl32.dll", "InitCommonControls", reinterpret_cast<void*>(InitCommonControls));
    ldr.registerExportOrdinal("comctl32.dll", 17, reinterpret_cast<void*>(InitCommonControls));
    ldr.registerExport("comctl32.dll", "InitCommonControlsEx", reinterpret_cast<void*>(InitCommonControlsEx));
    ldr.registerExport("comctl32.dll", "ImageList_Create", reinterpret_cast<void*>(ImageList_Create));
    ldr.registerExport("comctl32.dll", "ImageList_Destroy", reinterpret_cast<void*>(ImageList_Destroy));
    ldr.registerExport("comctl32.dll", "ImageList_GetImageCount", reinterpret_cast<void*>(ImageList_GetImageCount));
    ldr.registerExport("comctl32.dll", "ImageList_Add", reinterpret_cast<void*>(ImageList_Add));
    ldr.registerExport("comctl32.dll", "ImageList_AddIcon", reinterpret_cast<void*>(ImageList_AddIcon));
    ldr.registerExport("comctl32.dll", "ImageList_ReplaceIcon", reinterpret_cast<void*>(ImageList_ReplaceIcon));
    ldr.registerExport("comctl32.dll", "ImageList_GetIconSize", reinterpret_cast<void*>(ImageList_GetIconSize));
    ldr.registerExport("comctl32.dll", "ImageList_Draw", reinterpret_cast<void*>(ImageList_Draw));
    ldr.registerExport("comctl32.dll", "CreateStatusWindowW", reinterpret_cast<void*>(CreateStatusWindowW));
    ldr.registerExport("comctl32.dll", "CreateStatusWindowA", reinterpret_cast<void*>(CreateStatusWindowA));
    ldr.registerExport("comctl32.dll", "CreateUpDownControl", reinterpret_cast<void*>(CreateUpDownControl));
    ldr.registerExport("comctl32.dll", "ImageList_EndDrag", reinterpret_cast<void*>(ImageList_EndDrag));
    ldr.registerExport("comctl32.dll", "ImageList_DragShowNolock", reinterpret_cast<void*>(ImageList_DragShowNolock));
    ldr.registerExport("comctl32.dll", "ImageList_DragEnter", reinterpret_cast<void*>(ImageList_DragEnter));
    ldr.registerExport("comctl32.dll", "ImageList_DragMove", reinterpret_cast<void*>(ImageList_DragMove));
    ldr.registerExport("comctl32.dll", "ImageList_BeginDrag", reinterpret_cast<void*>(ImageList_BeginDrag));
    ldr.registerExport("comctl32.dll", "ImageList_Remove", reinterpret_cast<void*>(ImageList_Remove));
    ldr.registerExport("comctl32.dll", "ImageList_SetIconSize", reinterpret_cast<void*>(ImageList_SetIconSize));
    ldr.registerExport("comctl32.dll", "ImageList_AddMasked", reinterpret_cast<void*>(ImageList_AddMasked));
    ldr.registerExport("comctl32.dll", "ImageList_GetIcon", reinterpret_cast<void*>(ImageList_GetIcon));
    ldr.registerExport("comctl32.dll", "ImageList_GetImageInfo", reinterpret_cast<void*>(ImageList_GetImageInfo));
    ldr.registerExport("comctl32.dll", "_TrackMouseEvent", reinterpret_cast<void*>(_TrackMouseEvent));

    ldr.registerExport("comctl32.dll", "FlatSB_SetScrollInfo", reinterpret_cast<void*>(FlatSB_SetScrollInfo));
    ldr.registerExport("comctl32.dll", "FlatSB_SetScrollProp", reinterpret_cast<void*>(FlatSB_SetScrollProp));
    ldr.registerExport("comctl32.dll", "ImageList_GetDragImage", reinterpret_cast<void*>(ImageList_GetDragImage));
    ldr.registerExport("comctl32.dll", "ImageList_DrawEx", reinterpret_cast<void*>(ImageList_DrawEx));
    ldr.registerExport("comctl32.dll", "ImageList_SetImageCount", reinterpret_cast<void*>(ImageList_SetImageCount));
    ldr.registerExport("comctl32.dll", "FlatSB_GetScrollPos", reinterpret_cast<void*>(FlatSB_GetScrollPos));
    ldr.registerExport("comctl32.dll", "FlatSB_SetScrollPos", reinterpret_cast<void*>(FlatSB_SetScrollPos));
    ldr.registerExport("comctl32.dll", "InitializeFlatSB", reinterpret_cast<void*>(InitializeFlatSB));
    ldr.registerExport("comctl32.dll", "ImageList_Copy", reinterpret_cast<void*>(ImageList_Copy));
    ldr.registerExport("comctl32.dll", "FlatSB_GetScrollInfo", reinterpret_cast<void*>(FlatSB_GetScrollInfo));
    ldr.registerExport("comctl32.dll", "ImageList_Write", reinterpret_cast<void*>(ImageList_Write));
    ldr.registerExport("comctl32.dll", "ImageList_DrawIndirect", reinterpret_cast<void*>(ImageList_DrawIndirect));
    ldr.registerExport("comctl32.dll", "ImageList_SetBkColor", reinterpret_cast<void*>(ImageList_SetBkColor));
    ldr.registerExport("comctl32.dll", "ImageList_GetBkColor", reinterpret_cast<void*>(ImageList_GetBkColor));
    ldr.registerExport("comctl32.dll", "ImageList_Replace", reinterpret_cast<void*>(ImageList_Replace));
    ldr.registerExport("comctl32.dll", "ImageList_SetDragCursorImage", reinterpret_cast<void*>(ImageList_SetDragCursorImage));
    ldr.registerExport("comctl32.dll", "ImageList_Read", reinterpret_cast<void*>(ImageList_Read));
    ldr.registerExport("comctl32.dll", "ImageList_DragLeave", reinterpret_cast<void*>(ImageList_DragLeave));
    ldr.registerExport("comctl32.dll", "ImageList_LoadImageW", reinterpret_cast<void*>(ImageList_LoadImageW));
    ldr.registerExport("comctl32.dll", "ImageList_SetOverlayImage", reinterpret_cast<void*>(ImageList_SetOverlayImage));

    // Ordinal exports
    ldr.registerExportOrdinal("comctl32.dll", 17, reinterpret_cast<void*>(ComCtlOrdinalStub));
    ldr.registerExportOrdinal("comctl32.dll", 345, reinterpret_cast<void*>(ComCtlOrdinalStub));
    ldr.registerExportOrdinal("comctl32.dll", 381, reinterpret_cast<void*>(ComCtlOrdinalStub));
    ldr.registerExportOrdinal("comctl32.dll", 410, reinterpret_cast<void*>(ComCtlOrdinalStub));
    ldr.registerExportOrdinal("comctl32.dll", 411, reinterpret_cast<void*>(ComCtlOrdinalStub));
    ldr.registerExportOrdinal("comctl32.dll", 412, reinterpret_cast<void*>(ComCtlOrdinalStub));
    ldr.registerExportOrdinal("comctl32.dll", 413, reinterpret_cast<void*>(ComCtlOrdinalStub));
    ldr.registerExport("comctl32.dll", "DestroyPropertySheetPage", reinterpret_cast<void*>(DestroyPropertySheetPage));
    ldr.registerExport("comctl32.dll", "ImageList_CoCreateInstance", reinterpret_cast<void*>(ImageList_CoCreateInstance));
    ldr.registerExportOrdinal("comctl32.dll", 13, reinterpret_cast<void*>(ImageList_Read_Ordinal13));
    ldr.registerExportOrdinal("comctl32.dll", 14, reinterpret_cast<void*>(ImageList_Write_Ordinal14));
    ldr.registerExportOrdinal("comctl32.dll", 15, reinterpret_cast<void*>(ImageList_ReadEx_Ordinal15));
}

} // namespace micant::comctl32
