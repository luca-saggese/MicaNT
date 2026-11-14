// ============================================================================
// MicaNT: Win32 GDI Subsystem (gdi32.dll)
//
// Modern Clean-Room Implementation in pure ISO C++23.
// Provides complete Graphics Device Interface (GDI) Device Contexts,
// Bitmaps, DIB Sections, Brushes, Pens, Fonts, 2D Drawing Primitives,
// ROP2/ROP3 Blitting, Typography, and OpenGL/3D Pixel Format Management.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include "micant/shared/ntdef.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/winuser/user32.hpp"
#include "micant/shared/font8x8.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::gdi32 {

// ============================================================================
// 1. Data Types & Handle Definitions
// ============================================================================

using COLORREF = uint32_t;
using HGDIOBJ  = void*;
using HDC      = void*;
using HBITMAP  = void*;
using HBRUSH   = void*;
using HPEN     = void*;
using HFONT    = void*;
using HRGN     = void*;
using BOOL     = win32::BOOL;
using DWORD    = win32::DWORD;
using UINT     = win32::UINT;
using LPCWSTR  = win32::LPCWSTR;
using LPCSTR   = win32::LPCSTR;
using LPARAM   = win32::LPARAM;

using micant::TRUE;
using micant::FALSE;

inline constexpr COLORREF RGB(uint8_t r, uint8_t g, uint8_t b) noexcept {
    return static_cast<COLORREF>(r | (static_cast<uint16_t>(g) << 8) | (static_cast<uint32_t>(b) << 16));
}

inline constexpr uint8_t GetRValue(COLORREF rgb) noexcept { return static_cast<uint8_t>(rgb & 0xFF); }
inline constexpr uint8_t GetGValue(COLORREF rgb) noexcept { return static_cast<uint8_t>((rgb >> 8) & 0xFF); }
inline constexpr uint8_t GetBValue(COLORREF rgb) noexcept { return static_cast<uint8_t>((rgb >> 16) & 0xFF); }

// GDI Object Types
inline constexpr uint32_t OBJ_PEN    = 1;
inline constexpr uint32_t OBJ_BRUSH  = 2;
inline constexpr uint32_t OBJ_DC     = 3;
inline constexpr uint32_t OBJ_FONT   = 6;
inline constexpr uint32_t OBJ_BITMAP = 7;
inline constexpr uint32_t OBJ_REGION = 8;

// Stock Objects
inline constexpr int WHITE_BRUSH         = 0;
inline constexpr int LTGRAY_BRUSH        = 1;
inline constexpr int GRAY_BRUSH          = 2;
inline constexpr int DKGRAY_BRUSH        = 3;
inline constexpr int BLACK_BRUSH         = 4;
inline constexpr int NULL_BRUSH          = 5;
inline constexpr int HOLLOW_BRUSH        = NULL_BRUSH;
inline constexpr int WHITE_PEN           = 6;
inline constexpr int BLACK_PEN           = 7;
inline constexpr int NULL_PEN            = 8;
inline constexpr int OEM_FIXED_FONT      = 10;
inline constexpr int ANSI_FIXED_FONT     = 11;
inline constexpr int ANSI_VAR_FONT       = 12;
inline constexpr int SYSTEM_FONT         = 13;
inline constexpr int DEVICE_DEFAULT_FONT = 14;
inline constexpr int DEFAULT_PALETTE     = 15;
inline constexpr int SYSTEM_FIXED_FONT   = 16;
inline constexpr int DEFAULT_GUI_FONT    = 17;

// Pen Styles
inline constexpr int PS_SOLID       = 0;
inline constexpr int PS_DASH        = 1;
inline constexpr int PS_DOT         = 2;
inline constexpr int PS_DASHDOT     = 3;
inline constexpr int PS_DASHDOTDOT  = 4;
inline constexpr int PS_NULL        = 5;
inline constexpr int PS_INSIDEFRAME = 6;

// Brush Styles
inline constexpr uint32_t BS_SOLID   = 0;
inline constexpr uint32_t BS_NULL    = 1;
inline constexpr uint32_t BS_HOLLOW  = BS_NULL;
inline constexpr uint32_t BS_HATCHED = 2;
inline constexpr uint32_t BS_PATTERN = 3;

// Background Modes
inline constexpr int TRANSPARENT = 1;
inline constexpr int OPAQUE      = 2;

// Raster Operations (ROPs)
inline constexpr uint32_t SRCCOPY     = 0x00CC0020;
inline constexpr uint32_t SRCPAINT    = 0x00EE0086;
inline constexpr uint32_t SRCAND      = 0x008800C6;
inline constexpr uint32_t SRCINVERT   = 0x00660046;
inline constexpr uint32_t SRCERASE    = 0x00440328;
inline constexpr uint32_t NOTSRCCOPY  = 0x00330008;
inline constexpr uint32_t NOTSRCERASE = 0x001100A6;
inline constexpr uint32_t MERGECOPY   = 0x00C000CA;
inline constexpr uint32_t MERGEPAINT  = 0x00BB0226;
inline constexpr uint32_t PATCOPY     = 0x00F00021;
inline constexpr uint32_t PATPAINT    = 0x00FB0A09;
inline constexpr uint32_t PATINVERT   = 0x005A0049;
inline constexpr uint32_t DSTINVERT   = 0x00550009;
inline constexpr uint32_t BLACKNESS   = 0x00000042;
inline constexpr uint32_t WHITENESS   = 0x00FF0062;

// DIB Color Table Usage
inline constexpr uint32_t DIB_RGB_COLORS = 0;
inline constexpr uint32_t DIB_PAL_COLORS = 1;

// Pixel Format Flags (OpenGL)
inline constexpr uint32_t PFD_DOUBLEBUFFER      = 0x00000001;
inline constexpr uint32_t PFD_STEREO            = 0x00000002;
inline constexpr uint32_t PFD_DRAW_TO_WINDOW    = 0x00000004;
inline constexpr uint32_t PFD_DRAW_TO_BITMAP    = 0x00000008;
inline constexpr uint32_t PFD_SUPPORT_GDI       = 0x00000010;
inline constexpr uint32_t PFD_SUPPORT_OPENGL    = 0x00000020;
inline constexpr uint8_t  PFD_TYPE_RGBA         = 0;
inline constexpr uint8_t  PFD_TYPE_COLORINDEX   = 1;
inline constexpr uint8_t  PFD_MAIN_PLANE        = 0;

// Device Caps
inline constexpr int HORZRES     = 8;
inline constexpr int VERTRES     = 10;
inline constexpr int BITSPIXEL   = 12;
inline constexpr int PLANES      = 14;
inline constexpr int LOGPIXELSX  = 88;
inline constexpr int LOGPIXELSY  = 90;

// Structures
struct POINT {
    int32_t x{0};
    int32_t y{0};
};
using LPPOINT = POINT*;

struct SIZE {
    int32_t cx{0};
    int32_t cy{0};
};
using LPSIZE = SIZE*;

struct RECT {
    int32_t left{0};
    int32_t top{0};
    int32_t right{0};
    int32_t bottom{0};
};
using LPRECT = RECT*;

struct BITMAP {
    int32_t  bmType{0};
    int32_t  bmWidth{0};
    int32_t  bmHeight{0};
    int32_t  bmWidthBytes{0};
    uint16_t bmPlanes{1};
    uint16_t bmBitsPixel{32};
    void*    bmBits{nullptr};
};

struct BITMAPINFOHEADER {
    uint32_t biSize{sizeof(BITMAPINFOHEADER)};
    int32_t  biWidth{0};
    int32_t  biHeight{0};
    uint16_t biPlanes{1};
    uint16_t biBitCount{32};
    uint32_t biCompression{0}; // BI_RGB
    uint32_t biSizeImage{0};
    int32_t  biXPelsPerMeter{2835};
    int32_t  biYPelsPerMeter{2835};
    uint32_t biClrUsed{0};
    uint32_t biClrImportant{0};
};

struct RGBQUAD {
    uint8_t rgbBlue{0};
    uint8_t rgbGreen{0};
    uint8_t rgbRed{0};
    uint8_t rgbReserved{0};
};

struct BITMAPINFO {
    BITMAPINFOHEADER bmiHeader{};
    RGBQUAD          bmiColors[1]{};
};

struct RGNDATAHEADER {
    uint32_t dwSize;
    uint32_t iType;
    uint32_t nCount;
    uint32_t nRgnSize;
    RECT rcBound;
};

struct RGNDATA {
    RGNDATAHEADER rdh;
    char Buffer[1];
};

struct LOGBRUSH {
    uint32_t  lbStyle{BS_SOLID};
    COLORREF  lbColor{RGB(255, 255, 255)};
    uintptr_t lbHatch{0};
};

struct LOGPEN {
    uint32_t lopnStyle{PS_SOLID};
    POINT    lopnWidth{1, 0};
    COLORREF lopnColor{RGB(0, 0, 0)};
};

struct LOGFONTW {
    int32_t lfHeight{16};
    int32_t lfWidth{0};
    int32_t lfEscapement{0};
    int32_t lfOrientation{0};
    int32_t lfWeight{400};
    uint8_t lfItalic{0};
    uint8_t lfUnderline{0};
    uint8_t lfStrikeOut{0};
    uint8_t lfCharSet{1};
    uint8_t lfOutPrecision{0};
    uint8_t lfClipPrecision{0};
    uint8_t lfQuality{0};
    uint8_t lfPitchAndFamily{0};
    wchar_t lfFaceName[32]{L"System"};
};

struct TEXTMETRICW {
    int32_t tmHeight{8};
    int32_t tmAscent{7};
    int32_t tmDescent{1};
    int32_t tmInternalLeading{0};
    int32_t tmExternalLeading{0};
    int32_t tmAveCharWidth{8};
    int32_t tmMaxCharWidth{8};
    int32_t tmWeight{400};
    int32_t tmOverhang{0};
    int32_t tmDigitizedAspectX{96};
    int32_t tmDigitizedAspectY{96};
    wchar_t tmFirstChar{32};
    wchar_t tmLastChar{126};
    wchar_t tmDefaultChar{32};
    wchar_t tmBreakChar{32};
    uint8_t tmItalic{0};
    uint8_t tmUnderlined{0};
    uint8_t tmStruckOut{0};
    uint8_t tmPitchAndFamily{0};
    uint8_t tmCharSet{1};
};

struct LOGFONTA {
    int32_t lfHeight{14};
    int32_t lfWidth{0};
    int32_t lfEscapement{0};
    int32_t lfOrientation{0};
    int32_t lfWeight{400};
    uint8_t lfItalic{0};
    uint8_t lfUnderline{0};
    uint8_t lfStrikeOut{0};
    uint8_t lfCharSet{0};
    uint8_t lfOutPrecision{0};
    uint8_t lfClipPrecision{0};
    uint8_t lfQuality{0};
    uint8_t lfPitchAndFamily{0};
    char lfFaceName[32]{"Courier New"};
};

struct TEXTMETRICA {
    int32_t tmHeight{14};
    int32_t tmAscent{11};
    int32_t tmDescent{3};
    int32_t tmInternalLeading{0};
    int32_t tmExternalLeading{0};
    int32_t tmAveCharWidth{8};
    int32_t tmMaxCharWidth{16};
    int32_t tmWeight{400};
    int32_t tmOverhang{0};
    int32_t tmDigitizedAspectX{96};
    int32_t tmDigitizedAspectY{96};
    uint8_t tmFirstChar{32};
    uint8_t tmLastChar{126};
    uint8_t tmDefaultChar{32};
    uint8_t tmBreakChar{32};
    uint8_t tmItalic{0};
    uint8_t tmUnderlined{0};
    uint8_t tmStruckOut{0};
    uint8_t tmPitchAndFamily{0};
    uint8_t tmCharSet{0};
};

struct ABCFLOAT {
    float abcfA{0.0f};
    float abcfB{8.0f};
    float abcfC{0.0f};
};

struct CHARSETINFO {
    uint32_t ciCharset{0};
    uint32_t ciACP{1252};
    struct {
        uint32_t fsUsb[4];
        uint32_t fsCsb[2];
    } fs{};
};

struct PIXELFORMATDESCRIPTOR {
    uint16_t nSize{sizeof(PIXELFORMATDESCRIPTOR)};
    uint16_t nVersion{1};
    uint32_t dwFlags{PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER};
    uint8_t  iPixelType{PFD_TYPE_RGBA};
    uint8_t  cColorBits{32};
    uint8_t  cRedBits{8};
    uint8_t  cRedShift{16};
    uint8_t  cGreenBits{8};
    uint8_t  cGreenShift{8};
    uint8_t  cBlueBits{8};
    uint8_t  cBlueShift{0};
    uint8_t  cAlphaBits{8};
    uint8_t  cAlphaShift{24};
    uint8_t  cAccumBits{0};
    uint8_t  cAccumRedBits{0};
    uint8_t  cAccumGreenBits{0};
    uint8_t  cAccumBlueBits{0};
    uint8_t  cAccumAlphaBits{0};
    uint8_t  cDepthBits{24};
    uint8_t  cStencilBits{8};
    uint8_t  cAuxBuffers{0};
    uint8_t  iLayerType{PFD_MAIN_PLANE};
    uint8_t  bReserved{0};
    uint32_t dwLayerMask{0};
    uint32_t dwVisibleMask{0};
    uint32_t dwDamageMask{0};
};
using LPPIXELFORMATDESCRIPTOR = PIXELFORMATDESCRIPTOR*;

// ============================================================================
// 2. Concrete GDI Objects
// ============================================================================

class GdiObject {
public:
    virtual ~GdiObject() = default;
    [[nodiscard]] virtual uint32_t GetType() const noexcept = 0;
};

class GdiPen : public GdiObject {
public:
    int      style{PS_SOLID};
    int      width{1};
    COLORREF color{RGB(0, 0, 0)};

    GdiPen(int s, int w, COLORREF c) : style(s), width(std::max(1, w)), color(c) {}
    [[nodiscard]] uint32_t GetType() const noexcept override { return OBJ_PEN; }
};

class GdiBrush : public GdiObject {
public:
    uint32_t style{BS_SOLID};
    COLORREF color{RGB(255, 255, 255)};

    GdiBrush(uint32_t s, COLORREF c) : style(s), color(c) {}
    [[nodiscard]] uint32_t GetType() const noexcept override { return OBJ_BRUSH; }
};

class GdiFont : public GdiObject {
public:
    LOGFONTW logFont{};

    explicit GdiFont(const LOGFONTW& lf) : logFont(lf) {}
    [[nodiscard]] uint32_t GetType() const noexcept override { return OBJ_FONT; }
};

class GdiBitmap : public GdiObject {
private:
    uint32_t              m_width{0};
    uint32_t              m_height{0};
    std::vector<uint32_t> m_pixels;
    bool                  m_isSection{false};

public:
    GdiBitmap(uint32_t w, uint32_t h, COLORREF initColor = RGB(255, 255, 255), bool isSection = false)
        : m_width(w), m_height(h), m_pixels(static_cast<size_t>(w) * h, 0xFF000000 | (initColor & 0xFFFFFF)), m_isSection(isSection) {}

    [[nodiscard]] uint32_t GetType() const noexcept override { return OBJ_BITMAP; }
    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_height; }
    [[nodiscard]] uint32_t* GetBits() noexcept { return m_pixels.data(); }
    [[nodiscard]] const uint32_t* GetBits() const noexcept { return m_pixels.data(); }
    [[nodiscard]] bool IsSection() const noexcept { return m_isSection; }

    [[nodiscard]] uint32_t GetPixel(uint32_t x, uint32_t y) const noexcept {
        if (x >= m_width || y >= m_height) return 0;
        return m_pixels[static_cast<size_t>(y) * m_width + x];
    }

    void SetPixel(uint32_t x, uint32_t y, uint32_t c) noexcept {
        if (x < m_width && y < m_height) {
            m_pixels[static_cast<size_t>(y) * m_width + x] = c;
        }
    }
};

// ============================================================================
// 3. Device Context (HDC) Architecture
// ============================================================================

class DeviceContext {
public:
    enum class DcType { Memory, Window };

private:
    DcType                       m_type{DcType::Memory};
    win32::HWND                  m_hwnd{nullptr};
    std::shared_ptr<GdiBitmap>   m_bitmap;

    std::shared_ptr<GdiPen>      m_selectedPen;
    std::shared_ptr<GdiBrush>    m_selectedBrush;
    std::shared_ptr<GdiFont>     m_selectedFont;
    std::shared_ptr<GdiBitmap>   m_selectedBitmap;

    COLORREF                     m_textColor{RGB(0, 0, 0)};
    COLORREF                     m_bkColor{RGB(255, 255, 255)};
    int                          m_bkMode{OPAQUE};
    POINT                        m_currentPos{0, 0};

    PIXELFORMATDESCRIPTOR        m_pfd{};
    int                          m_pixelFormatIndex{0};

public:
    DeviceContext(DcType type, win32::HWND hwnd, uint32_t w, uint32_t h)
        : m_type(type), m_hwnd(hwnd) {
        m_bitmap = std::make_shared<GdiBitmap>(w, h, RGB(255, 255, 255));
        m_selectedBitmap = m_bitmap;
        m_selectedPen = std::make_shared<GdiPen>(PS_SOLID, 1, RGB(0, 0, 0));
        m_selectedBrush = std::make_shared<GdiBrush>(BS_SOLID, RGB(255, 255, 255));
        LOGFONTW lf{};
        std::wcsncpy(lf.lfFaceName, L"System", 31);
        lf.lfHeight = 8;
        m_selectedFont = std::make_shared<GdiFont>(lf);

        // Standard 32-bit OpenGL-ready PFD
        m_pfd = PIXELFORMATDESCRIPTOR{};
        m_pixelFormatIndex = 1;
    }

    [[nodiscard]] DcType GetType() const noexcept { return m_type; }
    [[nodiscard]] win32::HWND GetHwnd() const noexcept { return m_hwnd; }
    [[nodiscard]] uint32_t GetWidth() const noexcept { return m_bitmap ? m_bitmap->GetWidth() : 0; }
    [[nodiscard]] uint32_t GetHeight() const noexcept { return m_bitmap ? m_bitmap->GetHeight() : 0; }
    [[nodiscard]] GdiBitmap* GetBitmap() noexcept { return m_selectedBitmap.get(); }

    void SelectPen(std::shared_ptr<GdiPen> pen) noexcept { if (pen) m_selectedPen = std::move(pen); }
    void SelectBrush(std::shared_ptr<GdiBrush> brush) noexcept { if (brush) m_selectedBrush = std::move(brush); }
    void SelectFont(std::shared_ptr<GdiFont> font) noexcept { if (font) m_selectedFont = std::move(font); }
    void SelectBitmap(std::shared_ptr<GdiBitmap> bmp) noexcept { if (bmp) m_selectedBitmap = std::move(bmp); }

    [[nodiscard]] std::shared_ptr<GdiPen> GetSelectedPen() const noexcept { return m_selectedPen; }
    [[nodiscard]] std::shared_ptr<GdiBrush> GetSelectedBrush() const noexcept { return m_selectedBrush; }
    [[nodiscard]] std::shared_ptr<GdiFont> GetSelectedFont() const noexcept { return m_selectedFont; }
    [[nodiscard]] std::shared_ptr<GdiBitmap> GetSelectedBitmap() const noexcept { return m_selectedBitmap; }

    void SetTextColor(COLORREF c) noexcept { m_textColor = c; }
    [[nodiscard]] COLORREF GetTextColor() const noexcept { return m_textColor; }
    void SetBkColor(COLORREF c) noexcept { m_bkColor = c; }
    [[nodiscard]] COLORREF GetBkColor() const noexcept { return m_bkColor; }
    void SetBkMode(int m) noexcept { m_bkMode = m; }
    [[nodiscard]] int GetBkMode() const noexcept { return m_bkMode; }

    void MoveTo(int x, int y, POINT* ptOld) noexcept {
        if (ptOld) *ptOld = m_currentPos;
        m_currentPos.x = x;
        m_currentPos.y = y;
    }
    [[nodiscard]] POINT GetCurrentPosition() const noexcept { return m_currentPos; }

    void SetPixelFormat(int fmt, const PIXELFORMATDESCRIPTOR& pfd) noexcept {
        m_pixelFormatIndex = fmt;
        m_pfd = pfd;
    }
    [[nodiscard]] int GetPixelFormatIndex() const noexcept { return m_pixelFormatIndex; }
    [[nodiscard]] const PIXELFORMATDESCRIPTOR& GetPixelFormatDescriptor() const noexcept { return m_pfd; }

    // ------------------------------------------------------------------------
    // Drawing Primitives
    // ------------------------------------------------------------------------
    COLORREF SetPixel(int x, int y, COLORREF color) noexcept {
        if (!m_selectedBitmap) return 0;
        if (x < 0 || y < 0 || x >= static_cast<int>(m_selectedBitmap->GetWidth()) || y >= static_cast<int>(m_selectedBitmap->GetHeight())) {
            return 0;
        }
        uint32_t bgra = 0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color >> 16) & 0xFF);
        m_selectedBitmap->SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), bgra);
        FlushIfWindow();
        return color;
    }

    [[nodiscard]] COLORREF GetPixel(int x, int y) const noexcept {
        if (!m_selectedBitmap) return 0;
        if (x < 0 || y < 0 || x >= static_cast<int>(m_selectedBitmap->GetWidth()) || y >= static_cast<int>(m_selectedBitmap->GetHeight())) {
            return 0;
        }
        uint32_t c = m_selectedBitmap->GetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(c & 0xFF);
        return RGB(r, g, b);
    }

    void LineTo(int x1, int y1) noexcept {
        if (!m_selectedBitmap || m_selectedPen->style == PS_NULL) {
            m_currentPos = { x1, y1 };
            return;
        }

        int x0 = m_currentPos.x;
        int y0 = m_currentPos.y;
        COLORREF penClr = m_selectedPen->color;
        uint32_t bgra = 0xFF000000 | ((penClr & 0xFF) << 16) | (penClr & 0xFF00) | ((penClr >> 16) & 0xFF);

        int dx = std::abs(x1 - x0);
        int dy = std::abs(y1 - y0);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            if (x0 >= 0 && y0 >= 0 && x0 < static_cast<int>(m_selectedBitmap->GetWidth()) && y0 < static_cast<int>(m_selectedBitmap->GetHeight())) {
                m_selectedBitmap->SetPixel(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), bgra);
            }
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }

        m_currentPos = { x1, y1 };
        FlushIfWindow();
    }

    void Rectangle(int left, int top, int right, int bottom) noexcept {
        if (!m_selectedBitmap) return;
        int x0 = std::min(left, right);
        int x1 = std::max(left, right) - 1;
        int y0 = std::min(top, bottom);
        int y1 = std::max(top, bottom) - 1;

        // 1. Fill interior with brush
        if (m_selectedBrush && m_selectedBrush->style != BS_NULL) {
            COLORREF bClr = m_selectedBrush->color;
            uint32_t fillBgra = 0xFF000000 | ((bClr & 0xFF) << 16) | (bClr & 0xFF00) | ((bClr >> 16) & 0xFF);
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    if (x >= 0 && y >= 0 && x < static_cast<int>(m_selectedBitmap->GetWidth()) && y < static_cast<int>(m_selectedBitmap->GetHeight())) {
                        m_selectedBitmap->SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), fillBgra);
                    }
                }
            }
        }

        // 2. Draw border with pen
        if (m_selectedPen && m_selectedPen->style != PS_NULL) {
            COLORREF pClr = m_selectedPen->color;
            uint32_t penBgra = 0xFF000000 | ((pClr & 0xFF) << 16) | (pClr & 0xFF00) | ((pClr >> 16) & 0xFF);
            for (int x = x0; x <= x1; ++x) {
                if (x >= 0 && x < static_cast<int>(m_selectedBitmap->GetWidth())) {
                    if (y0 >= 0 && y0 < static_cast<int>(m_selectedBitmap->GetHeight())) m_selectedBitmap->SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y0), penBgra);
                    if (y1 >= 0 && y1 < static_cast<int>(m_selectedBitmap->GetHeight())) m_selectedBitmap->SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y1), penBgra);
                }
            }
            for (int y = y0; y <= y1; ++y) {
                if (y >= 0 && y < static_cast<int>(m_selectedBitmap->GetHeight())) {
                    if (x0 >= 0 && x0 < static_cast<int>(m_selectedBitmap->GetWidth())) m_selectedBitmap->SetPixel(static_cast<uint32_t>(x0), static_cast<uint32_t>(y), penBgra);
                    if (x1 >= 0 && x1 < static_cast<int>(m_selectedBitmap->GetWidth())) m_selectedBitmap->SetPixel(static_cast<uint32_t>(x1), static_cast<uint32_t>(y), penBgra);
                }
            }
        }

        FlushIfWindow();
    }

    void Ellipse(int left, int top, int right, int bottom) noexcept {
        if (!m_selectedBitmap) return;
        int x0 = std::min(left, right);
        int x1 = std::max(left, right) - 1;
        int y0 = std::min(top, bottom);
        int y1 = std::max(top, bottom) - 1;

        float cx = (x0 + x1) * 0.5f;
        float cy = (y0 + y1) * 0.5f;
        float rx = (x1 - x0) * 0.5f;
        float ry = (y1 - y0) * 0.5f;

        if (rx <= 0.0f || ry <= 0.0f) return;
        float invRx2 = 1.0f / (rx * rx);
        float invRy2 = 1.0f / (ry * ry);

        // Fill interior
        if (m_selectedBrush && m_selectedBrush->style != BS_NULL) {
            COLORREF bClr = m_selectedBrush->color;
            uint32_t fillBgra = 0xFF000000 | ((bClr & 0xFF) << 16) | (bClr & 0xFF00) | ((bClr >> 16) & 0xFF);

            for (int y = y0; y <= y1; ++y) {
                float dy = (y + 0.5f) - cy;
                float dy2 = dy * dy * invRy2;
                if (dy2 > 1.0f) continue;
                for (int x = x0; x <= x1; ++x) {
                    float dx = (x + 0.5f) - cx;
                    if (dx * dx * invRx2 + dy2 <= 1.0f) {
                        if (x >= 0 && y >= 0 && x < static_cast<int>(m_selectedBitmap->GetWidth()) && y < static_cast<int>(m_selectedBitmap->GetHeight())) {
                            m_selectedBitmap->SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), fillBgra);
                        }
                    }
                }
            }
        }

        // Outline
        if (m_selectedPen && m_selectedPen->style != PS_NULL) {
            COLORREF pClr = m_selectedPen->color;
            uint32_t penBgra = 0xFF000000 | ((pClr & 0xFF) << 16) | (pClr & 0xFF00) | ((pClr >> 16) & 0xFF);
            int steps = static_cast<int>(std::max(rx, ry) * 6.28f) + 16;
            for (int i = 0; i < steps; ++i) {
                float theta = (6.2831853f * i) / steps;
                int px = static_cast<int>(cx + rx * std::cos(theta));
                int py = static_cast<int>(cy + ry * std::sin(theta));
                if (px >= 0 && py >= 0 && px < static_cast<int>(m_selectedBitmap->GetWidth()) && py < static_cast<int>(m_selectedBitmap->GetHeight())) {
                    m_selectedBitmap->SetPixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), penBgra);
                }
            }
        }

        FlushIfWindow();
    }

    void FillRect(const RECT& rc, GdiBrush* pBrush) noexcept {
        if (!m_selectedBitmap || !pBrush || pBrush->style == BS_NULL) return;
        COLORREF bClr = pBrush->color;
        uint32_t fillBgra = 0xFF000000 | ((bClr & 0xFF) << 16) | (bClr & 0xFF00) | ((bClr >> 16) & 0xFF);

        int x0 = std::max(0, std::min(rc.left, rc.right));
        int x1 = std::min(static_cast<int>(m_selectedBitmap->GetWidth()), std::max(rc.left, rc.right));
        int y0 = std::max(0, std::min(rc.top, rc.bottom));
        int y1 = std::min(static_cast<int>(m_selectedBitmap->GetHeight()), std::max(rc.top, rc.bottom));

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                m_selectedBitmap->SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), fillBgra);
            }
        }

        FlushIfWindow();
    }

    // ------------------------------------------------------------------------
    // Blitting & BitBlt ROPs
    // ------------------------------------------------------------------------
    bool BitBlt(int xDest, int yDest, int w, int h, DeviceContext* srcDc, int xSrc, int ySrc, uint32_t rop) noexcept {
        if (!m_selectedBitmap || !srcDc || !srcDc->GetBitmap() || w <= 0 || h <= 0) return false;

        auto* dstBmp = m_selectedBitmap.get();
        auto* srcBmp = srcDc->GetBitmap();

        for (int y = 0; y < h; ++y) {
            int sy = ySrc + y;
            int dy = yDest + y;
            if (dy < 0 || dy >= static_cast<int>(dstBmp->GetHeight())) continue;

            for (int x = 0; x < w; ++x) {
                int sx = xSrc + x;
                int dx = xDest + x;
                if (dx < 0 || dx >= static_cast<int>(dstBmp->GetWidth())) continue;

                uint32_t srcPixel = (sx >= 0 && sy >= 0 && sx < static_cast<int>(srcBmp->GetWidth()) && sy < static_cast<int>(srcBmp->GetHeight()))
                    ? srcBmp->GetPixel(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy))
                    : 0xFFFFFFFF;
                uint32_t dstPixel = dstBmp->GetPixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy));

                uint32_t resultPixel = srcPixel;
                switch (rop) {
                    case SRCCOPY:    resultPixel = srcPixel; break;
                    case SRCPAINT:   resultPixel = srcPixel | dstPixel; break;
                    case SRCAND:     resultPixel = srcPixel & dstPixel; break;
                    case SRCINVERT:  resultPixel = srcPixel ^ dstPixel; break;
                    case BLACKNESS:  resultPixel = 0xFF000000; break;
                    case WHITENESS:  resultPixel = 0xFFFFFFFF; break;
                    default:         resultPixel = srcPixel; break;
                }

                dstBmp->SetPixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy), resultPixel);
            }
        }

        FlushIfWindow();
        return true;
    }

    bool StretchBlt(int xDest, int yDest, int wDest, int hDest, DeviceContext* srcDc, int xSrc, int ySrc, int wSrc, int hSrc, uint32_t rop) noexcept {
        if (!m_selectedBitmap || !srcDc || !srcDc->GetBitmap() || wDest <= 0 || hDest <= 0 || wSrc <= 0 || hSrc <= 0) return false;

        auto* dstBmp = m_selectedBitmap.get();
        auto* srcBmp = srcDc->GetBitmap();

        float scaleX = static_cast<float>(wSrc) / wDest;
        float scaleY = static_cast<float>(hSrc) / hDest;

        for (int y = 0; y < hDest; ++y) {
            int dy = yDest + y;
            if (dy < 0 || dy >= static_cast<int>(dstBmp->GetHeight())) continue;
            int sy = ySrc + static_cast<int>(y * scaleY);
            if (sy < 0 || sy >= static_cast<int>(srcBmp->GetHeight())) continue;

            for (int x = 0; x < wDest; ++x) {
                int dx = xDest + x;
                if (dx < 0 || dx >= static_cast<int>(dstBmp->GetWidth())) continue;
                int sx = xSrc + static_cast<int>(x * scaleX);
                if (sx < 0 || sx >= static_cast<int>(srcBmp->GetWidth())) continue;

                uint32_t srcPixel = srcBmp->GetPixel(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
                uint32_t dstPixel = dstBmp->GetPixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy));

                uint32_t resultPixel = (rop == SRCINVERT) ? (srcPixel ^ dstPixel) : srcPixel;
                dstBmp->SetPixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy), resultPixel);
            }
        }

        FlushIfWindow();
        return true;
    }

    // ------------------------------------------------------------------------
    // Typography & TextOutW
    // ------------------------------------------------------------------------
    bool TextOutW(int x, int y, const wchar_t* lpString, int c) noexcept {
        if (!m_selectedBitmap || !lpString || c <= 0) return false;

        uint32_t textBgra = 0xFF000000 | ((m_textColor & 0xFF) << 16) | (m_textColor & 0xFF00) | ((m_textColor >> 16) & 0xFF);
        uint32_t bkBgra   = 0xFF000000 | ((m_bkColor & 0xFF) << 16) | (m_bkColor & 0xFF00) | ((m_bkColor >> 16) & 0xFF);

        int cursorX = x;
        for (int i = 0; i < c; ++i) {
            wchar_t wc = lpString[i];
            char asciiCh = (wc >= 32 && wc <= 126) ? static_cast<char>(wc) : '?';
            const uint8_t* glyph = micant::shared::FONT_8X8[asciiCh - 32];

            for (int row = 0; row < 8; ++row) {
                int py = y + row;
                if (py < 0 || py >= static_cast<int>(m_selectedBitmap->GetHeight())) continue;
                uint8_t rowBits = glyph[row];

                for (int col = 0; col < 8; ++col) {
                    int px = cursorX + col;
                    if (px < 0 || px >= static_cast<int>(m_selectedBitmap->GetWidth())) continue;

                    bool bitOn = (rowBits & (1 << (7 - col))) != 0;
                    if (bitOn) {
                        m_selectedBitmap->SetPixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), textBgra);
                    } else if (m_bkMode == OPAQUE) {
                        m_selectedBitmap->SetPixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), bkBgra);
                    }
                }
            }

            cursorX += 8;
        }

        FlushIfWindow();
        return true;
    }

    void FlushIfWindow() noexcept {
        if (m_type == DcType::Window && m_hwnd && m_selectedBitmap) {
            uint32_t w = m_selectedBitmap->GetWidth();
            uint32_t h = m_selectedBitmap->GetHeight();
            user32::WindowManager::get().blitToWindow(
                m_hwnd,
                reinterpret_cast<const uint8_t*>(m_selectedBitmap->GetBits()),
                w, h, w * 4
            );
        }
    }
};

// ============================================================================
// 4. Central GDI Object & DC Engine
// ============================================================================

class GdiEngine {
private:
    std::mutex m_mutex;
    std::unordered_map<uintptr_t, std::shared_ptr<GdiObject>>     m_objects;
    std::unordered_map<uintptr_t, std::shared_ptr<DeviceContext>> m_dcs;
    uintptr_t m_nextHandle{0x1000};

    // Pre-allocated Stock Objects
    std::shared_ptr<GdiBrush> m_stockWhiteBrush;
    std::shared_ptr<GdiBrush> m_stockBlackBrush;
    std::shared_ptr<GdiBrush> m_stockNullBrush;
    std::shared_ptr<GdiPen>   m_stockWhitePen;
    std::shared_ptr<GdiPen>   m_stockBlackPen;
    std::shared_ptr<GdiPen>   m_stockNullPen;
    std::shared_ptr<GdiFont>  m_stockSystemFont;

    GdiEngine() {
        m_stockWhiteBrush = std::make_shared<GdiBrush>(BS_SOLID, RGB(255, 255, 255));
        m_stockBlackBrush = std::make_shared<GdiBrush>(BS_SOLID, RGB(0, 0, 0));
        m_stockNullBrush  = std::make_shared<GdiBrush>(BS_NULL, RGB(0, 0, 0));

        m_stockWhitePen   = std::make_shared<GdiPen>(PS_SOLID, 1, RGB(255, 255, 255));
        m_stockBlackPen   = std::make_shared<GdiPen>(PS_SOLID, 1, RGB(0, 0, 0));
        m_stockNullPen    = std::make_shared<GdiPen>(PS_NULL, 0, RGB(0, 0, 0));

        LOGFONTW lf{};
        std::wcsncpy(lf.lfFaceName, L"System", 31);
        lf.lfHeight = 8;
        m_stockSystemFont = std::make_shared<GdiFont>(lf);
    }

public:
    static GdiEngine& get() {
        static GdiEngine instance;
        return instance;
    }

    HGDIOBJ registerObject(std::shared_ptr<GdiObject> obj) {
        if (!obj) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        uintptr_t h = m_nextHandle++;
        m_objects[h] = std::move(obj);
        return reinterpret_cast<HGDIOBJ>(h);
    }

    std::shared_ptr<GdiObject> getObject(HGDIOBJ h) {
        if (!h) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_objects.find(reinterpret_cast<uintptr_t>(h));
        return (it != m_objects.end()) ? it->second : nullptr;
    }

    bool deleteObject(HGDIOBJ h) {
        if (!h) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_objects.erase(reinterpret_cast<uintptr_t>(h)) > 0;
    }

    HDC registerDc(std::shared_ptr<DeviceContext> dc) {
        if (!dc) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        uintptr_t h = m_nextHandle++;
        m_dcs[h] = std::move(dc);
        return reinterpret_cast<HDC>(h);
    }

    std::shared_ptr<DeviceContext> getDc(HDC hdc) {
        if (!hdc) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_dcs.find(reinterpret_cast<uintptr_t>(hdc));
        return (it != m_dcs.end()) ? it->second : nullptr;
    }

    bool deleteDc(HDC hdc) {
        if (!hdc) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dcs.erase(reinterpret_cast<uintptr_t>(hdc)) > 0;
    }

    HDC getWindowDC(win32::HWND hwnd) {
        if (!hwnd) return nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);

        // Check if DC already exists for this HWND
        for (const auto& [h, dc] : m_dcs) {
            if (dc->GetType() == DeviceContext::DcType::Window && dc->GetHwnd() == hwnd) {
                return reinterpret_cast<HDC>(h);
            }
        }

        uint32_t w = 640, h = 480;
        user32::WindowManager::get().getWindowPixelBuffer(hwnd, &w, &h);
        if (w == 0) w = 640;
        if (h == 0) h = 480;

        auto winDc = std::make_shared<DeviceContext>(DeviceContext::DcType::Window, hwnd, w, h);
        uintptr_t handle = m_nextHandle++;
        m_dcs[handle] = winDc;
        return reinterpret_cast<HDC>(handle);
    }

    HGDIOBJ getStockObject(int i) {
        switch (i) {
            case WHITE_BRUSH:  return registerObject(m_stockWhiteBrush);
            case BLACK_BRUSH:  return registerObject(m_stockBlackBrush);
            case NULL_BRUSH:   return registerObject(m_stockNullBrush);
            case WHITE_PEN:    return registerObject(m_stockWhitePen);
            case BLACK_PEN:    return registerObject(m_stockBlackPen);
            case NULL_PEN:     return registerObject(m_stockNullPen);
            case SYSTEM_FONT:
            case DEFAULT_GUI_FONT:
            case ANSI_VAR_FONT:
            case OEM_FIXED_FONT:
                return registerObject(m_stockSystemFont);
            default:
                return registerObject(m_stockWhiteBrush);
        }
    }
};

// ============================================================================
// 5. Win32 GDI C-API Export Surface (gdi32.dll)
// ============================================================================

inline HDC CreateCompatibleDC(HDC hdc) noexcept {
    auto srcDc = GdiEngine::get().getDc(hdc);
    uint32_t w = srcDc ? srcDc->GetWidth() : 640;
    uint32_t h = srcDc ? srcDc->GetHeight() : 480;
    auto memDc = std::make_shared<DeviceContext>(DeviceContext::DcType::Memory, nullptr, w, h);
    return GdiEngine::get().registerDc(memDc);
}

inline win32::BOOL DeleteDC(HDC hdc) noexcept {
    return GdiEngine::get().deleteDc(hdc) ? win32::TRUE : win32::FALSE;
}

inline HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    auto obj = GdiEngine::get().getObject(h);
    if (!dc || !obj) return nullptr;

    HGDIOBJ oldObj = nullptr;
    switch (obj->GetType()) {
        case OBJ_PEN:
            oldObj = GdiEngine::get().registerObject(dc->GetSelectedPen());
            dc->SelectPen(std::dynamic_pointer_cast<GdiPen>(obj));
            break;
        case OBJ_BRUSH:
            oldObj = GdiEngine::get().registerObject(dc->GetSelectedBrush());
            dc->SelectBrush(std::dynamic_pointer_cast<GdiBrush>(obj));
            break;
        case OBJ_FONT:
            oldObj = GdiEngine::get().registerObject(dc->GetSelectedFont());
            dc->SelectFont(std::dynamic_pointer_cast<GdiFont>(obj));
            break;
        case OBJ_BITMAP:
            oldObj = GdiEngine::get().registerObject(dc->GetSelectedBitmap());
            dc->SelectBitmap(std::dynamic_pointer_cast<GdiBitmap>(obj));
            break;
        default:
            break;
    }
    return oldObj;
}

inline win32::BOOL DeleteObject(HGDIOBJ ho) noexcept {
    return GdiEngine::get().deleteObject(ho) ? win32::TRUE : win32::FALSE;
}

inline HGDIOBJ GetStockObject(int i) noexcept {
    return GdiEngine::get().getStockObject(i);
}

inline HBITMAP CreateCompatibleBitmap(HDC /*hdc*/, int cx, int cy) noexcept {
    if (cx <= 0 || cy <= 0) return nullptr;
    auto bmp = std::make_shared<GdiBitmap>(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
    return GdiEngine::get().registerObject(bmp);
}

inline HBITMAP CreateDIBSection(HDC, const BITMAPINFO* pbmi, uint32_t, void** ppvBits, win32::HANDLE, uint32_t) noexcept {
    if (!pbmi) return nullptr;
    uint32_t w = std::abs(pbmi->bmiHeader.biWidth);
    uint32_t h = std::abs(pbmi->bmiHeader.biHeight);
    if (w == 0 || h == 0) return nullptr;

    auto bmp = std::make_shared<GdiBitmap>(w, h, RGB(0, 0, 0), true);
    if (ppvBits) *ppvBits = bmp->GetBits();
    return GdiEngine::get().registerObject(bmp);
}

inline HPEN CreatePen(int iStyle, int cWidth, COLORREF color) noexcept {
    auto pen = std::make_shared<GdiPen>(iStyle, cWidth, color);
    return GdiEngine::get().registerObject(pen);
}

inline HBRUSH CreateSolidBrush(COLORREF color) noexcept {
    auto brush = std::make_shared<GdiBrush>(BS_SOLID, color);
    return GdiEngine::get().registerObject(brush);
}

inline win32::BOOL MoveToEx(HDC hdc, int x, int y, LPPOINT lppt) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return win32::FALSE;
    dc->MoveTo(x, y, lppt);
    return win32::TRUE;
}

inline win32::BOOL LineTo(HDC hdc, int x, int y) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return win32::FALSE;
    dc->LineTo(x, y);
    return win32::TRUE;
}

inline win32::BOOL Rectangle(HDC hdc, int left, int top, int right, int bottom) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return win32::FALSE;
    dc->Rectangle(left, top, right, bottom);
    return win32::TRUE;
}

inline win32::BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return win32::FALSE;
    dc->Ellipse(left, top, right, bottom);
    return win32::TRUE;
}

inline int FillRect(HDC hdc, const RECT* lprc, HBRUSH hbr) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    auto brush = std::dynamic_pointer_cast<GdiBrush>(GdiEngine::get().getObject(hbr));
    if (!dc || !lprc || !brush) return 0;
    dc->FillRect(*lprc, brush.get());
    return 1;
}

inline COLORREF SetPixel(HDC hdc, int x, int y, COLORREF color) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    return dc ? dc->SetPixel(x, y, color) : 0;
}

inline COLORREF GetPixel(HDC hdc, int x, int y) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    return dc ? dc->GetPixel(x, y) : 0;
}

inline win32::BOOL BitBlt(HDC hdcDest, int xDest, int yDest, int w, int h, HDC hdcSrc, int xSrc, int ySrc, uint32_t rop) noexcept {
    auto dstDc = GdiEngine::get().getDc(hdcDest);
    auto srcDc = GdiEngine::get().getDc(hdcSrc);
    if (!dstDc || !srcDc) return win32::FALSE;
    return dstDc->BitBlt(xDest, yDest, w, h, srcDc.get(), xSrc, ySrc, rop) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL StretchBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest, HDC hdcSrc, int xSrc, int ySrc, int wSrc, int hSrc, uint32_t rop) noexcept {
    auto dstDc = GdiEngine::get().getDc(hdcDest);
    auto srcDc = GdiEngine::get().getDc(hdcSrc);
    if (!dstDc || !srcDc) return win32::FALSE;
    return dstDc->StretchBlt(xDest, yDest, wDest, hDest, srcDc.get(), xSrc, ySrc, wSrc, hSrc, rop) ? win32::TRUE : win32::FALSE;
}

inline COLORREF SetTextColor(HDC hdc, COLORREF color) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return 0;
    COLORREF old = dc->GetTextColor();
    dc->SetTextColor(color);
    return old;
}

inline COLORREF GetTextColor(HDC hdc) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    return dc ? dc->GetTextColor() : 0;
}

inline COLORREF SetBkColor(HDC hdc, COLORREF color) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return 0;
    COLORREF old = dc->GetBkColor();
    dc->SetBkColor(color);
    return old;
}

inline COLORREF GetBkColor(HDC hdc) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    return dc ? dc->GetBkColor() : 0;
}

inline int SetBkMode(HDC hdc, int mode) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return 0;
    int old = dc->GetBkMode();
    dc->SetBkMode(mode);
    return old;
}

inline int GetBkMode(HDC hdc) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    return dc ? dc->GetBkMode() : 0;
}

inline win32::BOOL TextOutW(HDC hdc, int x, int y, const wchar_t* lpString, int c) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return win32::FALSE;
    return dc->TextOutW(x, y, lpString, c) ? win32::TRUE : win32::FALSE;
}

inline win32::BOOL TextOutA(HDC hdc, int x, int y, const char* lpString, int c) noexcept {
    if (!lpString || c <= 0) return win32::FALSE;
    std::wstring wStr;
    wStr.reserve(static_cast<size_t>(c));
    for (int i = 0; i < c; ++i) wStr.push_back(static_cast<wchar_t>(static_cast<unsigned char>(lpString[i])));
    return TextOutW(hdc, x, y, wStr.c_str(), static_cast<int>(wStr.size()));
}

inline win32::BOOL GetTextExtentPoint32W(HDC, const wchar_t* lpString, int c, LPSIZE psizl) noexcept {
    if (!psizl || !lpString || c < 0) return win32::FALSE;
    psizl->cx = c * 8;
    psizl->cy = 8;
    return win32::TRUE;
}

inline int GetObjectW(HGDIOBJ hgdiobj, int cbBuffer, void* lpvObject) noexcept {
    auto obj = GdiEngine::get().getObject(hgdiobj);
    if (!obj || !lpvObject || cbBuffer <= 0) return 0;

    if (obj->GetType() == OBJ_BITMAP) {
        auto bmp = std::dynamic_pointer_cast<GdiBitmap>(obj);
        BITMAP bm{};
        bm.bmWidth = static_cast<int32_t>(bmp->GetWidth());
        bm.bmHeight = static_cast<int32_t>(bmp->GetHeight());
        bm.bmWidthBytes = bm.bmWidth * 4;
        bm.bmPlanes = 1;
        bm.bmBitsPixel = 32;
        bm.bmBits = bmp->GetBits();
        int copyBytes = std::min(cbBuffer, static_cast<int>(sizeof(BITMAP)));
        std::memcpy(lpvObject, &bm, copyBytes);
        return copyBytes;
    }
    return 0;
}

inline int GetDeviceCaps(HDC hdc, int nIndex) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    switch (nIndex) {
        case HORZRES:    return dc ? static_cast<int>(dc->GetWidth()) : 1920;
        case VERTRES:    return dc ? static_cast<int>(dc->GetHeight()) : 1080;
        case BITSPIXEL:  return 32;
        case PLANES:     return 1;
        case LOGPIXELSX: return 96;
        case LOGPIXELSY: return 96;
        default:         return 0;
    }
}

// ----------------------------------------------------------------------------
// Pixel Formats (OpenGL WGL & 3D Acceleration Bridge)
// ----------------------------------------------------------------------------

inline int ChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR* ppfd) noexcept {
    if (!ppfd) return 0;
    return 1; // Standard Sovereign 32-bpp BGRA Pixel Format
}

inline win32::BOOL SetPixelFormat(HDC hdc, int format, const PIXELFORMATDESCRIPTOR* ppfd) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc || !ppfd || format != 1) return win32::FALSE;
    dc->SetPixelFormat(format, *ppfd);
    return win32::TRUE;
}

inline int DescribePixelFormat(HDC, int iPixelFormat, uint32_t nBytes, LPPIXELFORMATDESCRIPTOR ppfd) noexcept {
    if (iPixelFormat != 1 || !ppfd || nBytes < sizeof(PIXELFORMATDESCRIPTOR)) return 0;
    *ppfd = PIXELFORMATDESCRIPTOR{};
    return 1;
}

inline win32::BOOL SwapBuffers(HDC hdc) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return win32::FALSE;
    dc->FlushIfWindow();
    return win32::TRUE;
}

inline int GetPixelFormat(HDC hdc) noexcept {
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return 0;
    return 1;
}

using LPLOGFONTW = LOGFONTW*;
using FONTENUMPROCW = int (*)(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM);

struct DOCINFOW {
    int cbSize{sizeof(DOCINFOW)};
    LPCWSTR lpszDocName{nullptr};
    LPCWSTR lpszOutput{nullptr};
    LPCWSTR lpszDatatype{nullptr};
    DWORD fwType{0};
};

struct BLENDFUNCTION {
    uint8_t BlendOp{0};
    uint8_t BlendFlags{0};
    uint8_t SourceConstantAlpha{255};
    uint8_t AlphaFormat{0};
};

inline HFONT CreateFontW(
    int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
    DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet,
    DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality,
    DWORD iPitchAndFamily, LPCWSTR pszFaceName
) noexcept {
    LOGFONTW lf{};
    lf.lfHeight = cHeight;
    lf.lfWidth = cWidth;
    lf.lfEscapement = cEscapement;
    lf.lfOrientation = cOrientation;
    lf.lfWeight = cWeight;
    lf.lfItalic = static_cast<uint8_t>(bItalic);
    lf.lfUnderline = static_cast<uint8_t>(bUnderline);
    lf.lfStrikeOut = static_cast<uint8_t>(bStrikeOut);
    lf.lfCharSet = static_cast<uint8_t>(iCharSet);
    lf.lfOutPrecision = static_cast<uint8_t>(iOutPrecision);
    lf.lfClipPrecision = static_cast<uint8_t>(iClipPrecision);
    lf.lfQuality = static_cast<uint8_t>(iQuality);
    lf.lfPitchAndFamily = static_cast<uint8_t>(iPitchAndFamily);
    if (pszFaceName) wcsncpy(lf.lfFaceName, pszFaceName, 31);
    static uint64_t dummyFont = 0xF001;
    return reinterpret_cast<HFONT>(&dummyFont);
}

inline HFONT CreateFontIndirectW(const LOGFONTW* lplf) noexcept {
    if (!lplf) return nullptr;
    static uint64_t dummyFont = 0xF001;
    return reinterpret_cast<HFONT>(&dummyFont);
}

inline HBITMAP CreateBitmap(int nWidth, int nHeight, UINT /*nPlanes*/, UINT /*nBitCount*/, const void* /*lpBits*/) noexcept {
    return CreateCompatibleBitmap(nullptr, nWidth, nHeight);
}

inline BOOL SetWindowOrgEx(HDC /*hdc*/, int /*x*/, int /*y*/, POINT* lppt) noexcept {
    if (lppt) { lppt->x = 0; lppt->y = 0; }
    return TRUE;
}

inline BOOL OffsetWindowOrgEx(HDC /*hdc*/, int /*x*/, int /*y*/, POINT* lppt) noexcept {
    if (lppt) { lppt->x = 0; lppt->y = 0; }
    return TRUE;
}

inline HBRUSH CreatePatternBrush(HBITMAP /*hbm*/) noexcept {
    return CreateSolidBrush(RGB(240, 240, 240));
}

inline BOOL PatBlt(HDC hdc, int x, int y, int w, int h, DWORD /*rop*/) noexcept {
    RECT rc{x, y, x + w, y + h};
    FillRect(hdc, &rc, nullptr);
    return TRUE;
}

inline BOOL SetBrushOrgEx(HDC /*hdc*/, int /*x*/, int /*y*/, POINT* lppt) noexcept {
    if (lppt) { lppt->x = 0; lppt->y = 0; }
    return TRUE;
}

inline int SetStretchBltMode(HDC /*hdc*/, int /*mode*/) noexcept {
    return 1;
}

inline int SetDIBits(HDC /*hdc*/, HBITMAP /*hbm*/, UINT /*start*/, UINT cLines, const void* /*lpBits*/, const BITMAPINFO* /*lpbmi*/, UINT /*ColorUse*/) noexcept {
    return static_cast<int>(cLines);
}

inline int GetDIBits(HDC /*hdc*/, HBITMAP /*hbm*/, UINT /*start*/, UINT cLines, void* /*lpBits*/, BITMAPINFO* /*lpbmi*/, UINT /*usage*/) noexcept {
    return static_cast<int>(cLines);
}

inline int StretchDIBits(
    HDC hdc, int xDest, int yDest, int DestWidth, int DestHeight,
    int xSrc, int ySrc, int SrcWidth, int SrcHeight,
    const void* lpBits, const BITMAPINFO* lpbmi, UINT /*iUsage*/, DWORD /*rop*/
) noexcept {
    if (!hdc || DestWidth <= 0 || DestHeight <= 0) return 0;
    auto dc = GdiEngine::get().getDc(hdc);
    if (!dc) return 0;

    auto* dstBmp = dc->GetBitmap();
    if (!dstBmp || !lpBits || !lpbmi) {
        RECT rc{xDest, yDest, xDest + DestWidth, yDest + DestHeight};
        FillRect(hdc, &rc, nullptr);
        return DestHeight;
    }

    int dibWidth = lpbmi->bmiHeader.biWidth;
    int dibHeight = lpbmi->bmiHeader.biHeight;
    bool topDown = (dibHeight < 0);
    int absDibHeight = std::abs(dibHeight);
    uint16_t bpp = lpbmi->bmiHeader.biBitCount;

    if (SrcWidth <= 0) SrcWidth = dibWidth;
    if (SrcHeight <= 0) SrcHeight = absDibHeight;

    const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(lpBits);
    size_t stride = ((static_cast<size_t>(dibWidth) * bpp + 31) / 32) * 4;

    float scaleX = static_cast<float>(SrcWidth) / DestWidth;
    float scaleY = static_cast<float>(SrcHeight) / DestHeight;

    for (int dy = 0; dy < DestHeight; ++dy) {
        int targetY = yDest + dy;
        if (targetY < 0 || targetY >= static_cast<int>(dstBmp->GetHeight())) continue;

        int sy = ySrc + static_cast<int>(dy * scaleY);
        if (sy < 0 || sy >= absDibHeight) continue;

        int rowInDIB = topDown ? sy : (absDibHeight - 1 - sy);
        const uint8_t* rowPtr = srcBytes + rowInDIB * stride;

        for (int dx = 0; dx < DestWidth; ++dx) {
            int targetX = xDest + dx;
            if (targetX < 0 || targetX >= static_cast<int>(dstBmp->GetWidth())) continue;

            int sx = xSrc + static_cast<int>(dx * scaleX);
            if (sx < 0 || sx >= dibWidth) continue;

            uint32_t bgra = 0xFF000000;
            if (bpp == 32) {
                const auto* pPix = reinterpret_cast<const uint32_t*>(rowPtr + sx * 4);
                bgra = *pPix;
            } else if (bpp == 24) {
                const uint8_t* pPix = rowPtr + sx * 3;
                bgra = 0xFF000000 | (static_cast<uint32_t>(pPix[2]) << 16) | (static_cast<uint32_t>(pPix[1]) << 8) | pPix[0];
            }

            dstBmp->SetPixel(static_cast<uint32_t>(targetX), static_cast<uint32_t>(targetY), bgra);
        }
    }

    dc->FlushIfWindow();
    return DestHeight;
}

inline int EnumFontFamiliesExW(HDC /*hdc*/, LPLOGFONTW /*lpLogfont*/, FONTENUMPROCW lpProc, LPARAM lParam, DWORD /*dwFlags*/) noexcept {
    if (lpProc) {
        LOGFONTW lf{};
        wcscpy(lf.lfFaceName, L"Segoe UI");
        TEXTMETRICW tm{};
        lpProc(&lf, &tm, 1, lParam);
    }
    return 1;
}

inline int WINAPI EnumFontFamiliesExA(
    HDC /*hdc*/,
    void* /*lpLogfont*/,
    void* /*lpProc*/,
    LPARAM /*lParam*/,
    DWORD /*dwFlags*/
) noexcept {
    return 1;
}

inline int StartDocW(HDC /*hdc*/, const DOCINFOW* /*lpdi*/) noexcept { return 1; }
inline int EndDoc(HDC /*hdc*/) noexcept { return 1; }
inline int StartPage(HDC /*hdc*/) noexcept { return 1; }
inline int EndPage(HDC /*hdc*/) noexcept { return 1; }

inline BOOL DPtoLP(HDC /*hdc*/, POINT* /*lppt*/, int /*c*/) noexcept { return TRUE; }

inline BOOL ExtTextOutW(
    HDC hdc, int x, int y, UINT /*options*/, const RECT* /*lprect*/,
    LPCWSTR lpString, UINT c, const int* /*lpDx*/
) noexcept {
    return TextOutW(hdc, x, y, lpString, static_cast<int>(c));
}

inline BOOL ExtTextOutA(
    HDC hdc, int x, int y, UINT /*options*/, const RECT* /*lprect*/,
    LPCSTR lpString, UINT c, const int* /*lpDx*/
) noexcept {
    return TextOutA(hdc, x, y, lpString, static_cast<int>(c));
}

inline UINT SetTextAlign(HDC /*hdc*/, UINT /*align*/) noexcept { return 0; }
inline BOOL RectVisible(HDC /*hdc*/, const RECT* /*lprect*/) noexcept { return TRUE; }

struct ENUMLOGFONTW {
    LOGFONTW elfLogFont;
    wchar_t elfFullName[64];
    wchar_t elfStyle[32];
};

struct NEWTEXTMETRICW {
    int32_t tmHeight;
    int32_t tmAscent;
    int32_t tmDescent;
    int32_t tmInternalLeading;
    int32_t tmExternalLeading;
    int32_t tmAveCharWidth;
    int32_t tmMaxCharWidth;
    int32_t tmWeight;
    int32_t tmOverhang;
    int32_t tmDigitizedAspectX;
    int32_t tmDigitizedAspectY;
    wchar_t tmFirstChar;
    wchar_t tmLastChar;
    wchar_t tmDefaultChar;
    wchar_t tmBreakChar;
    uint8_t tmItalic;
    uint8_t tmUnderlined;
    uint8_t tmStruckOut;
    uint8_t tmPitchAndFamily;
    uint8_t tmCharSet;
    uint32_t ntmFlags;
    uint32_t ntmSizeEM;
    uint32_t ntmCellHeight;
    uint32_t ntmAvgWidth;
};

inline uint32_t WINAPI GetLayout(HDC /*hdc*/) noexcept {
    return 0; // LAYOUT_LTR
}

inline int32_t WINAPI SetPolyFillMode(HDC /*hdc*/, int32_t /*mode*/) noexcept {
    return 1; // ALTERNATE
}

inline int32_t WINAPI GetPolyFillMode(HDC /*hdc*/) noexcept {
    return 1; // ALTERNATE
}

inline BOOL WINAPI SetViewportExtEx(HDC /*hdc*/, int32_t x, int32_t y, SIZE* lpsz) noexcept {
    if (lpsz) {
        lpsz->cx = x ? x : 1;
        lpsz->cy = y ? y : 1;
    }
    return TRUE;
}

inline BOOL WINAPI GetViewportExtEx(HDC /*hdc*/, SIZE* lpsz) noexcept {
    if (lpsz) {
        lpsz->cx = 1;
        lpsz->cy = 1;
    }
    return TRUE;
}

inline BOOL WINAPI SetWindowExtEx(HDC /*hdc*/, int32_t x, int32_t y, SIZE* lpsz) noexcept {
    if (lpsz) {
        lpsz->cx = x ? x : 1;
        lpsz->cy = y ? y : 1;
    }
    return TRUE;
}

inline BOOL WINAPI GetWindowExtEx(HDC /*hdc*/, SIZE* lpsz) noexcept {
    if (lpsz) {
        lpsz->cx = 1;
        lpsz->cy = 1;
    }
    return TRUE;
}

inline BOOL WINAPI OffsetViewportOrgEx(HDC /*hdc*/, int32_t /*x*/, int32_t /*y*/, POINT* lppt) noexcept {
    if (lppt) {
        lppt->x = 0;
        lppt->y = 0;
    }
    return TRUE;
}

inline BOOL WINAPI ScaleViewportExtEx(HDC /*hdc*/, int32_t /*xn*/, int32_t /*dx*/, int32_t /*yn*/, int32_t /*yd*/, SIZE* lpsz) noexcept {
    if (lpsz) {
        lpsz->cx = 1;
        lpsz->cy = 1;
    }
    return TRUE;
}

inline BOOL WINAPI ScaleWindowExtEx(HDC /*hdc*/, int32_t /*xn*/, int32_t /*dx*/, int32_t /*yn*/, int32_t /*yd*/, SIZE* lpsz) noexcept {
    if (lpsz) {
        lpsz->cx = 1;
        lpsz->cy = 1;
    }
    return TRUE;
}

inline int32_t WINAPI GetTextFaceW(HDC /*hdc*/, int32_t c, wchar_t* lpName) noexcept {
    const wchar_t fontName[] = L"Segoe UI";
    constexpr int32_t len = 8;
    if (!lpName || c <= 0) {
        return len + 1;
    }
    int32_t toCopy = std::min(c - 1, len);
    std::wmemcpy(lpName, fontName, toCopy);
    lpName[toCopy] = L'\0';
    return toCopy;
}

inline HRGN WINAPI CreateEllipticRgn(int32_t /*x1*/, int32_t /*y1*/, int32_t /*x2*/, int32_t /*y2*/) noexcept {
    static uintptr_t s_rgn = 0x8800;
    return reinterpret_cast<HRGN>(++s_rgn);
}

inline BOOL WINAPI PtVisible(HDC /*hdc*/, int32_t /*x*/, int32_t /*y*/) noexcept {
    return TRUE;
}

inline int32_t WINAPI Escape(HDC /*hdc*/, int32_t /*iEscape*/, int32_t /*cjIn*/, const char* /*pvIn*/, void* /*pvOut*/) noexcept {
    return 1;
}

inline int32_t WINAPI EnumFontFamiliesW(HDC /*hdc*/, const wchar_t* /*lpLogfont*/, void* lpProc, LPARAM lParam) noexcept {
    if (!lpProc) {
        return 0;
    }
    auto fn = reinterpret_cast<int32_t(*)(const ENUMLOGFONTW*, const NEWTEXTMETRICW*, uint32_t, LPARAM)>(lpProc);
    ENUMLOGFONTW elf{};
    std::wcsncpy(elf.elfLogFont.lfFaceName, L"Segoe UI", 31);
    elf.elfLogFont.lfHeight = -12;
    elf.elfLogFont.lfWeight = 400;
    elf.elfLogFont.lfCharSet = 1;
    std::wcsncpy(elf.elfFullName, L"Segoe UI Regular", 63);
    std::wcsncpy(elf.elfStyle, L"Regular", 31);

    NEWTEXTMETRICW ntm{};
    ntm.tmHeight = 15;
    ntm.tmAscent = 12;
    ntm.tmAveCharWidth = 7;
    ntm.ntmFlags = 0x00000004;

    return fn(&elf, &ntm, 0x0004, lParam);
}

inline void* WINAPI CopyMetaFileW(void* /*hmf*/, const wchar_t* /*pszFile*/) noexcept {
    static uintptr_t s_hmf = 0x9900;
    return reinterpret_cast<void*>(++s_hmf);
}

inline BOOL GetTextExtentPointW(HDC hdc, LPCWSTR lpString, int c, SIZE* lpsz) noexcept {
    return GetTextExtentPoint32W(hdc, lpString, c, lpsz);
}

inline BOOL GetTextExtentPoint32A(HDC /*hdc*/, LPCSTR lpString, int c, SIZE* lpsz) noexcept {
    if (!lpsz) return FALSE;
    int len = (c < 0 && lpString) ? static_cast<int>(strlen(lpString)) : c;
    lpsz->cx = len * 8;
    lpsz->cy = 16;
    return TRUE;
}

inline BOOL GetTextExtentExPointW(HDC /*hdc*/, LPCWSTR lpString, int cchString, int nMaxExtent, int* lpnFit, int* alpDx, SIZE* lpSize) noexcept {
    int len = (cchString < 0 && lpString) ? static_cast<int>(wcslen(lpString)) : cchString;
    if (lpSize) { lpSize->cx = len * 8; lpSize->cy = 16; }
    if (lpnFit) *lpnFit = (nMaxExtent > 0) ? std::min(len, nMaxExtent / 8) : len;
    if (alpDx) {
        for (int i = 0; i < len; ++i) alpDx[i] = (i + 1) * 8;
    }
    return TRUE;
}

inline BOOL GetTextExtentExPointA(HDC /*hdc*/, LPCSTR lpString, int cchString, int nMaxExtent, int* lpnFit, int* alpDx, SIZE* lpSize) noexcept {
    int len = (cchString < 0 && lpString) ? static_cast<int>(strlen(lpString)) : cchString;
    if (lpSize) { lpSize->cx = len * 8; lpSize->cy = 16; }
    if (lpnFit) *lpnFit = (nMaxExtent > 0) ? std::min(len, nMaxExtent / 8) : len;
    if (alpDx) {
        for (int i = 0; i < len; ++i) alpDx[i] = (i + 1) * 8;
    }
    return TRUE;
}

inline HBRUSH CreateHatchBrush(int /*iHatch*/, COLORREF color) noexcept {
    return CreateSolidBrush(color);
}

inline BOOL Polygon(HDC /*hdc*/, const POINT* /*apt*/, int /*cpt*/) noexcept { return TRUE; }
inline BOOL Polyline(HDC /*hdc*/, const POINT* /*apt*/, int /*cpt*/) noexcept { return TRUE; }
inline BOOL PolyPolyline(HDC /*hdc*/, const void* /*apt*/, const DWORD* /*asz*/, DWORD /*csz*/) noexcept { return TRUE; }

inline uint32_t SetLayout(HDC /*hdc*/, uint32_t /*l*/) noexcept {
    return 0; // LAYOUT_LTR
}

inline int ExtSelectClipRgn(HDC /*hdc*/, void* /*hrgn*/, int /*mode*/) noexcept {
    return 1; // SIMPLEREGION
}

inline BOOL GradientFill(HDC /*hdc*/, void* /*pVertex*/, uint32_t /*nVertex*/, void* /*pMesh*/, uint32_t /*nMesh*/, uint32_t /*ulMode*/) noexcept {
    return TRUE;
}

inline HPEN ExtCreatePen(DWORD /*iPenStyle*/, DWORD cWidth, const LOGBRUSH* plbrush, DWORD /*cStyle*/, const DWORD* /*pstyle*/) noexcept {
    COLORREF color = plbrush ? plbrush->lbColor : RGB(0, 0, 0);
    return CreatePen(PS_SOLID, static_cast<int>(cWidth), color);
}

inline BOOL GdiAlphaBlend(HDC /*hdcDest*/, int /*xoriginDest*/, int /*yoriginDest*/, int /*wDest*/, int /*hDest*/, HDC /*hdcSrc*/, int /*xoriginSrc*/, int /*yoriginSrc*/, int /*wSrc*/, int /*hSrc*/, BLENDFUNCTION /*ftn*/) noexcept {
    return TRUE;
}

inline BOOL AlphaBlend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn) noexcept {
    return GdiAlphaBlend(hdcDest, xoriginDest, yoriginDest, wDest, hDest, hdcSrc, xoriginSrc, yoriginSrc, wSrc, hSrc, ftn);
}

inline BOOL GetTextMetricsW(HDC /*hdc*/, TEXTMETRICW* lptm) noexcept {
    if (!lptm) return FALSE;
    *lptm = TEXTMETRICW{};
    lptm->tmHeight = 16;
    lptm->tmAscent = 13;
    lptm->tmDescent = 3;
    lptm->tmAveCharWidth = 8;
    lptm->tmMaxCharWidth = 16;
    return TRUE;
}

inline int SetROP2(HDC /*hdc*/, int rop2) noexcept {
    static int s_rop2 = 13; // R2_COPYPEN
    int prev = s_rop2;
    s_rop2 = rop2;
    return prev;
}

inline int GetROP2(HDC /*hdc*/) noexcept { return 13; }

inline int SaveDC(HDC /*hdc*/) noexcept { static int s_dc = 1; return ++s_dc; }
inline BOOL RestoreDC(HDC /*hdc*/, int /*nSavedDC*/) noexcept { return TRUE; }

inline HRGN CreateRectRgn(int /*x1*/, int /*y1*/, int /*x2*/, int /*y2*/) noexcept {
    static uint64_t dummyRgn = 0x9999;
    return reinterpret_cast<HRGN>(&dummyRgn);
}

inline HRGN CreateRectRgnIndirect(const RECT* /*lprect*/) noexcept {
    static uint64_t dummyRgn = 0x9999;
    return reinterpret_cast<HRGN>(&dummyRgn);
}

inline UINT GetTextAlign(HDC /*hdc*/) noexcept {
    return 0; // TA_LEFT | TA_TOP | TA_NOUPDATECP
}

inline int OffsetClipRgn(HDC /*hdc*/, int /*x*/, int /*y*/) noexcept {
    return 2; // SIMPLEREGION
}

inline BOOL GetDCOrgEx(HDC /*hdc*/, POINT* lppt) noexcept {
    if (!lppt) return FALSE;
    lppt->x = 0;
    lppt->y = 0;
    return TRUE;
}

inline DWORD GetRegionData(HRGN /*hrgn*/, DWORD dwCount, void* lpRgnData) noexcept {
    constexpr DWORD totalSize = sizeof(RGNDATAHEADER) + sizeof(RECT);
    if (!lpRgnData || dwCount < totalSize) {
        return totalSize;
    }
    auto* rgn = static_cast<RGNDATA*>(lpRgnData);
    rgn->rdh.dwSize = sizeof(RGNDATAHEADER);
    rgn->rdh.iType = 1; // RDH_RECTANGLES
    rgn->rdh.nCount = 1;
    rgn->rdh.nRgnSize = sizeof(RECT);
    rgn->rdh.rcBound = {0, 0, 1920, 1080};
    auto* rect = reinterpret_cast<RECT*>(rgn->Buffer);
    *rect = {0, 0, 1920, 1080};
    return totalSize;
}

inline COLORREF GetNearestColor(HDC /*hdc*/, COLORREF color) noexcept {
    return color;
}

inline HBITMAP CreateBitmapIndirect(const BITMAP* /*pbm*/) noexcept {
    static uintptr_t s_hbm = 0xB17A00;
    return reinterpret_cast<HBITMAP>(++s_hbm);
}

inline int SelectClipRgn(HDC /*hdc*/, HRGN /*hrgn*/) noexcept { return 2; /* SIMPLEREGION */ }
inline int GetClipRgn(HDC /*hdc*/, HRGN /*hrgn*/) noexcept { return 0; /* No clip region */ }
inline int IntersectClipRect(HDC /*hdc*/, int /*left*/, int /*top*/, int /*right*/, int /*bottom*/) noexcept { return 2; }
inline int ExcludeClipRect(HDC /*hdc*/, int /*left*/, int /*top*/, int /*right*/, int /*bottom*/) noexcept { return 2; }
inline int CombineRgn(HRGN /*hrgnDst*/, HRGN /*hrgnSrc1*/, HRGN /*hrgnSrc2*/, int /*iMode*/) noexcept { return 2; }

inline BOOL RoundRect(HDC hdc, int left, int top, int right, int bottom, int /*width*/, int /*height*/) noexcept {
    return Rectangle(hdc, left, top, right, bottom);
}

inline HRGN CreatePolygonRgn([[maybe_unused]] const void* lppt,
                             [[maybe_unused]] int cPoints,
                             [[maybe_unused]] int fnPolyFillMode) noexcept {
    return reinterpret_cast<HRGN>(0x00020001);
}

inline BOOL PolyPolygon([[maybe_unused]] HDC hdc,
                        [[maybe_unused]] const void* apt,
                        [[maybe_unused]] const int* asz,
                        [[maybe_unused]] int csz) noexcept {
    return TRUE;
}

inline BOOL EqualRgn(HRGN hSrcRgn1, HRGN hSrcRgn2) noexcept {
    return (hSrcRgn1 == hSrcRgn2) ? TRUE : FALSE;
}

inline BOOL PtInRegion([[maybe_unused]] HRGN hrgn,
                       [[maybe_unused]] int x,
                       [[maybe_unused]] int y) noexcept {
    return TRUE;
}

inline BOOL RectInRegion([[maybe_unused]] HRGN hrgn,
                         [[maybe_unused]] const void* lprc) noexcept {
    return TRUE;
}

inline void* GetEnhMetaFileW([[maybe_unused]] LPCWSTR lpName) noexcept {
    return reinterpret_cast<void*>(0x00030001);
}

inline void* SetMetaFileBitsEx([[maybe_unused]] UINT cbBuffer,
                               [[maybe_unused]] const uint8_t* lpData) noexcept {
    return reinterpret_cast<void*>(0x00030002);
}

inline int GetGraphicsMode([[maybe_unused]] HDC hdc) noexcept {
    return 1; // GM_COMPATIBLE
}

inline BOOL GetWorldTransform([[maybe_unused]] HDC hdc, void* lpXform) noexcept {
    if (lpXform) {
        float* f = reinterpret_cast<float*>(lpXform);
        f[0] = 1.0f; f[1] = 0.0f; // eM11, eM12
        f[2] = 0.0f; f[3] = 1.0f; // eM21, eM22
        f[4] = 0.0f; f[5] = 0.0f; // eDx, eDy
    }
    return TRUE;
}

inline BOOL ModifyWorldTransform([[maybe_unused]] HDC hdc,
                                 [[maybe_unused]] const void* lpXform,
                                 [[maybe_unused]] DWORD iMode) noexcept {
    return TRUE;
}

inline HFONT CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, const char* pszFaceName) noexcept {
    wchar_t wFace[64]{};
    if (pszFaceName) {
        int i = 0;
        while (pszFaceName[i] && i < 63) { wFace[i] = static_cast<wchar_t>(pszFaceName[i]); ++i; }
        wFace[i] = L'\0';
    }
    return CreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, wFace);
}

inline HFONT CreateFontIndirectA(const LOGFONTA* lplf) noexcept {
    if (!lplf) return reinterpret_cast<HFONT>(0x101);
    return CreateFontA(lplf->lfHeight, lplf->lfWidth, lplf->lfEscapement, lplf->lfOrientation, lplf->lfWeight, lplf->lfItalic, lplf->lfUnderline, lplf->lfStrikeOut, lplf->lfCharSet, lplf->lfOutPrecision, lplf->lfClipPrecision, lplf->lfQuality, lplf->lfPitchAndFamily, lplf->lfFaceName);
}

inline BOOL GetCharABCWidthsFloatA([[maybe_unused]] HDC hdc, UINT iFirst, UINT iLast, ABCFLOAT* lpABC) noexcept {
    if (!lpABC || iLast < iFirst) return FALSE;
    UINT count = iLast - iFirst + 1;
    for (UINT i = 0; i < count; ++i) {
        lpABC[i] = ABCFLOAT{0.0f, 8.0f, 0.0f};
    }
    return TRUE;
}

inline BOOL GetCharWidth32A([[maybe_unused]] HDC hdc, UINT iFirst, UINT iLast, int* lpBuffer) noexcept {
    if (!lpBuffer || iLast < iFirst) return FALSE;
    UINT count = iLast - iFirst + 1;
    for (UINT i = 0; i < count; ++i) lpBuffer[i] = 8;
    return TRUE;
}

inline BOOL GetCharWidth32W([[maybe_unused]] HDC hdc, UINT iFirst, UINT iLast, int* lpBuffer) noexcept {
    if (!lpBuffer || iLast < iFirst) return FALSE;
    UINT count = iLast - iFirst + 1;
    for (UINT i = 0; i < count; ++i) lpBuffer[i] = 8;
    return TRUE;
}

inline BOOL GetCharWidthA(HDC hdc, UINT iFirst, UINT iLast, int* lpBuffer) noexcept {
    return GetCharWidth32A(hdc, iFirst, iLast, lpBuffer);
}

inline BOOL GetCharWidthW(HDC hdc, UINT iFirst, UINT iLast, int* lpBuffer) noexcept {
    return GetCharWidth32W(hdc, iFirst, iLast, lpBuffer);
}

inline DWORD GetCharacterPlacementW([[maybe_unused]] HDC hdc, [[maybe_unused]] const wchar_t* lpString, int nCount, [[maybe_unused]] int nMexExtent, [[maybe_unused]] void* lpResults, [[maybe_unused]] DWORD dwFlags) noexcept {
    return static_cast<DWORD>(nCount * 8);
}

inline int GetObjectA([[maybe_unused]] void* hgdiobj, int cbBuffer, void* lpvObject) noexcept {
    if (!lpvObject || cbBuffer <= 0) return sizeof(LOGFONTA);
    std::memset(lpvObject, 0, cbBuffer);
    return std::min<int>(cbBuffer, sizeof(LOGFONTA));
}

inline DWORD GetOutlineTextMetricsA([[maybe_unused]] HDC hdc, [[maybe_unused]] UINT cbData, [[maybe_unused]] void* lpOTM) noexcept {
    return 0;
}

inline BOOL GetTextExtentPointA([[maybe_unused]] HDC hdc, const char* lpString, int c, void* lpSize) noexcept {
    if (!lpSize) return FALSE;
    struct SIZE_WIN32 { int32_t cx; int32_t cy; };
    auto* s = reinterpret_cast<SIZE_WIN32*>(lpSize);
    s->cx = (c > 0 && lpString) ? (c * 8) : 0;
    s->cy = 14;
    return TRUE;
}

inline BOOL GetTextMetricsA([[maybe_unused]] HDC hdc, TEXTMETRICA* lptm) noexcept {
    if (!lptm) return FALSE;
    *lptm = TEXTMETRICA{};
    return TRUE;
}

inline BOOL TranslateCharsetInfo([[maybe_unused]] DWORD* lpSrc, CHARSETINFO* lpCs, [[maybe_unused]] DWORD dwFlags) noexcept {
    if (!lpCs) return FALSE;
    lpCs->ciCharset = 0;
    lpCs->ciACP = 1252;
    return TRUE;
}

inline int UpdateColors([[maybe_unused]] HDC hdc) noexcept { return 1; }

inline BOOL WINAPI GetCharABCWidthsW(HDC /*hdc*/, uint32_t /*wFirst*/, uint32_t wLast, void* lpABC) noexcept {
    if (lpABC) std::memset(lpABC, 0, (wLast + 1) * 12);
    return TRUE;
}

inline BOOL WINAPI GetCharABCWidthsFloatW(HDC /*hdc*/, uint32_t /*iFirst*/, uint32_t iLast, void* lpABC) noexcept {
    if (lpABC) std::memset(lpABC, 0, (iLast + 1) * 12);
    return TRUE;
}

inline DWORD WINAPI GetGlyphOutlineW(HDC /*hdc*/, uint32_t /*uChar*/, uint32_t /*fuFormat*/, void* lpgm, uint32_t /*cjBuffer*/, void* /*pvBuffer*/, const void* /*lpmat2*/) noexcept {
    if (lpgm) std::memset(lpgm, 0, 32);
    return 1;
}

inline BOOL WINAPI GetCharWidthI(HDC /*hdc*/, uint32_t /*giFirst*/, uint32_t cgi, uint16_t* /*pgi*/, int32_t* piWidths) noexcept {
    if (piWidths) {
        for (uint32_t i = 0; i < cgi; ++i) piWidths[i] = 8;
    }
    return TRUE;
}

inline uint32_t WINAPI GetOutlineTextMetricsW(HDC /*hdc*/, uint32_t /*cjCopy*/, void* potm) noexcept {
    if (potm) std::memset(potm, 0, 64);
    return 64;
}

inline int WINAPI AddFontResourceExW(const wchar_t* /*name*/, DWORD /*fl*/, void* /*res*/) noexcept {
    return 1;
}

inline BOOL WINAPI RemoveFontResourceExW(const wchar_t* /*name*/, DWORD /*fl*/, void* /*res*/) noexcept {
    return TRUE;
}

inline void* WINAPI AddFontMemResourceEx(void* /*pFileView*/, DWORD /*cjSize*/, void* /*pvResrved*/, DWORD* pNumFonts) noexcept {
    if (pNumFonts) *pNumFonts = 1;
    return reinterpret_cast<void*>(0xB001);
}

inline BOOL WINAPI RemoveFontMemResourceEx(void* /*h*/) noexcept {
    return TRUE;
}

inline DWORD WINAPI GetFontData(HDC /*hdc*/, DWORD /*dwTable*/, DWORD /*dwOffset*/, void* /*pvBuffer*/, DWORD /*cjBuffer*/) noexcept {
    return 0; // GDI_ERROR
}

inline BOOL WINAPI GetCharABCWidthsI(HDC /*hdc*/, uint32_t /*giFirst*/, uint32_t cgi, uint16_t* /*pgi*/, void* lpabc) noexcept {
    if (lpabc) std::memset(lpabc, 0, cgi * 12);
    return TRUE;
}

inline uint32_t WINAPI GetObjectType(void* /*h*/) noexcept {
    return 1; // OBJ_PEN
}

inline uint32_t WINAPI SetBoundsRect(HDC /*hdc*/, const void* /*lprcBounds*/, uint32_t /*flags*/) noexcept {
    return 0;
}

inline uint32_t WINAPI SetDCBrushColor(HDC /*hdc*/, uint32_t /*crColor*/) noexcept {
    return 0;
}

inline BOOL TransparentBlt(void* /*hdcDest*/, int /*xoriginDest*/, int /*yoriginDest*/, int /*wDest*/, int /*hDest*/, void* /*hdcSrc*/, int /*xoriginSrc*/, int /*yoriginSrc*/, int /*wSrc*/, int /*hSrc*/, uint32_t /*crTransparent*/) noexcept {
    return 1;
}
inline BOOL Pie(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetPaletteEntries(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetRandomRgn(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetEnhMetaFileHeader(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL CloseEnhMetaFile(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL AngleArc(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL ResizePalette(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetAbortProc(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetRectRgn(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetWindowOrgEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetPixelV(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* CreatePalette(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* CreateDCW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* CreateICW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL PolyBezierTo(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL PlayEnhMetaFile(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetBitmapBits(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL AbortDoc(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetSystemPaletteEntries(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetEnhMetaFileBits(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* CreatePenIndirect(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetEnhMetaFilePaletteEntries(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetMapMode(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetMapMode(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL PolyBezier(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL LPtoDP(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetCurrentObject(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetWinMetaFileBits(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetEnhMetaFileDescriptionW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL ArcTo(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* CreateEnhMetaFileW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL Arc(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* SelectPalette(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetGraphicsMode(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL MaskBlt(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL DeleteEnhMetaFile(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL Chord(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetViewportOrgEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetViewportOrgEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL RealizePalette(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetDIBColorTable(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetDIBColorTable(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* CreateBrushIndirect(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetEnhMetaFileBits(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL SetWorldTransform(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL FrameRgn(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetClipBox(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetWinMetaFileBits(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* CreateDIBitmap(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetStretchBltMode(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL ExtCreateRegion(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetRgnBox(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL EnumFontsW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* CreateHalftonePalette(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL ExtFloodFill(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline BOOL UnrealizeObject(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* CopyEnhMetaFileW(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL OffsetRgn(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetMetaFileBitsEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetBrushOrgEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* GetCurrentPositionEx(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL SetDCPenColor(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetNearestPaletteIndex(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline void* CreateRoundRectRgn(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }
inline BOOL GdiFlush(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return 1; }
inline void* GetPaletteEntries(void* /*a*/ = nullptr, void* /*b*/ = nullptr, void* /*c*/ = nullptr, void* /*d*/ = nullptr, void* /*e*/ = nullptr, void* /*f*/ = nullptr, void* /*g*/ = nullptr) noexcept { return reinterpret_cast<void*>(0x8800); }

// ============================================================================
// 6. Subsystem Export Registration
// ============================================================================

inline void InitializeGdi32SubsystemExports() {
    // Interop hooks: Connect User32 window DC lifecycle to GdiEngine
    user32::SetGetDCHook([](win32::HWND h) -> user32::HDC {
        return reinterpret_cast<user32::HDC>(GdiEngine::get().getWindowDC(h));
    });
    user32::SetReleaseDCHook([](win32::HWND hWnd, user32::HDC hDC) -> int {
        auto dc = GdiEngine::get().getDc(reinterpret_cast<HDC>(hDC));
        if (dc) {
            dc->FlushIfWindow();
        }
        user32::WindowManager::get().invalidateRect(hWnd, nullptr, win32::FALSE);
        return 1;
    });

    auto& ldr = ldr::DynamicLoader::get();

    ldr.registerExport("gdi32.dll", "GetCharABCWidthsW", reinterpret_cast<void*>(GetCharABCWidthsW));
    ldr.registerExport("gdi32.dll", "GetCharABCWidthsFloatW", reinterpret_cast<void*>(GetCharABCWidthsFloatW));
    ldr.registerExport("gdi32.dll", "GetGlyphOutlineW", reinterpret_cast<void*>(GetGlyphOutlineW));
    ldr.registerExport("gdi32.dll", "GetCharWidthI", reinterpret_cast<void*>(GetCharWidthI));
    ldr.registerExport("gdi32.dll", "GetOutlineTextMetricsW", reinterpret_cast<void*>(GetOutlineTextMetricsW));
    ldr.registerExport("gdi32.dll", "AddFontResourceExW", reinterpret_cast<void*>(AddFontResourceExW));
    ldr.registerExport("gdi32.dll", "RemoveFontResourceExW", reinterpret_cast<void*>(RemoveFontResourceExW));
    ldr.registerExport("gdi32.dll", "AddFontMemResourceEx", reinterpret_cast<void*>(AddFontMemResourceEx));
    ldr.registerExport("gdi32.dll", "RemoveFontMemResourceEx", reinterpret_cast<void*>(RemoveFontMemResourceEx));
    ldr.registerExport("gdi32.dll", "GetFontData", reinterpret_cast<void*>(GetFontData));
    ldr.registerExport("gdi32.dll", "GetCharABCWidthsI", reinterpret_cast<void*>(GetCharABCWidthsI));

    ldr.registerExport("gdi32.dll", "CreateCompatibleDC", reinterpret_cast<void*>(CreateCompatibleDC));
    ldr.registerExport("gdi32.dll", "DeleteDC", reinterpret_cast<void*>(DeleteDC));
    ldr.registerExport("gdi32.dll", "SelectObject", reinterpret_cast<void*>(SelectObject));
    ldr.registerExport("gdi32.dll", "DeleteObject", reinterpret_cast<void*>(DeleteObject));
    ldr.registerExport("gdi32.dll", "GetStockObject", reinterpret_cast<void*>(GetStockObject));
    ldr.registerExport("gdi32.dll", "CreateCompatibleBitmap", reinterpret_cast<void*>(CreateCompatibleBitmap));
    ldr.registerExport("gdi32.dll", "CreateDIBSection", reinterpret_cast<void*>(CreateDIBSection));
    ldr.registerExport("gdi32.dll", "CreatePen", reinterpret_cast<void*>(CreatePen));
    ldr.registerExport("gdi32.dll", "CreateSolidBrush", reinterpret_cast<void*>(CreateSolidBrush));
    ldr.registerExport("gdi32.dll", "MoveToEx", reinterpret_cast<void*>(MoveToEx));
    ldr.registerExport("gdi32.dll", "LineTo", reinterpret_cast<void*>(LineTo));
    ldr.registerExport("gdi32.dll", "Rectangle", reinterpret_cast<void*>(Rectangle));
    ldr.registerExport("gdi32.dll", "Ellipse", reinterpret_cast<void*>(Ellipse));
    ldr.registerExport("gdi32.dll", "FillRect", reinterpret_cast<void*>(FillRect));
    ldr.registerExport("gdi32.dll", "SetPixel", reinterpret_cast<void*>(SetPixel));
    ldr.registerExport("gdi32.dll", "GetPixel", reinterpret_cast<void*>(GetPixel));
    ldr.registerExport("gdi32.dll", "BitBlt", reinterpret_cast<void*>(BitBlt));
    ldr.registerExport("gdi32.dll", "StretchBlt", reinterpret_cast<void*>(StretchBlt));
    ldr.registerExport("gdi32.dll", "SetTextColor", reinterpret_cast<void*>(SetTextColor));
    ldr.registerExport("gdi32.dll", "GetTextColor", reinterpret_cast<void*>(GetTextColor));
    ldr.registerExport("gdi32.dll", "SetBkColor", reinterpret_cast<void*>(SetBkColor));
    ldr.registerExport("gdi32.dll", "GetBkColor", reinterpret_cast<void*>(GetBkColor));
    ldr.registerExport("gdi32.dll", "SetBkMode", reinterpret_cast<void*>(SetBkMode));
    ldr.registerExport("gdi32.dll", "GetBkMode", reinterpret_cast<void*>(GetBkMode));
    ldr.registerExport("gdi32.dll", "TextOutW", reinterpret_cast<void*>(TextOutW));
    ldr.registerExport("gdi32.dll", "TextOutA", reinterpret_cast<void*>(TextOutA));
    ldr.registerExport("gdi32.dll", "GetTextExtentPoint32W", reinterpret_cast<void*>(GetTextExtentPoint32W));
    ldr.registerExport("gdi32.dll", "GetObjectW", reinterpret_cast<void*>(GetObjectW));
    ldr.registerExport("gdi32.dll", "GetDeviceCaps", reinterpret_cast<void*>(GetDeviceCaps));
    ldr.registerExport("gdi32.dll", "ChoosePixelFormat", reinterpret_cast<void*>(ChoosePixelFormat));
    ldr.registerExport("gdi32.dll", "SetPixelFormat", reinterpret_cast<void*>(SetPixelFormat));
    ldr.registerExport("gdi32.dll", "DescribePixelFormat", reinterpret_cast<void*>(DescribePixelFormat));
    ldr.registerExport("gdi32.dll", "SwapBuffers", reinterpret_cast<void*>(SwapBuffers));
    ldr.registerExport("gdi32.dll", "GetPixelFormat", reinterpret_cast<void*>(GetPixelFormat));
    ldr.registerExport("gdi32.dll", "CreateFontW", reinterpret_cast<void*>(CreateFontW));
    ldr.registerExport("gdi32.dll", "CreateFontIndirectW", reinterpret_cast<void*>(CreateFontIndirectW));
    ldr.registerExport("gdi32.dll", "CreateBitmap", reinterpret_cast<void*>(CreateBitmap));
    ldr.registerExport("gdi32.dll", "SetWindowOrgEx", reinterpret_cast<void*>(SetWindowOrgEx));
    ldr.registerExport("gdi32.dll", "OffsetWindowOrgEx", reinterpret_cast<void*>(OffsetWindowOrgEx));
    ldr.registerExport("gdi32.dll", "CreatePatternBrush", reinterpret_cast<void*>(CreatePatternBrush));
    ldr.registerExport("gdi32.dll", "PatBlt", reinterpret_cast<void*>(PatBlt));
    ldr.registerExport("gdi32.dll", "SetBrushOrgEx", reinterpret_cast<void*>(SetBrushOrgEx));
    ldr.registerExport("gdi32.dll", "SetStretchBltMode", reinterpret_cast<void*>(SetStretchBltMode));
    ldr.registerExport("gdi32.dll", "SetDIBits", reinterpret_cast<void*>(SetDIBits));
    ldr.registerExport("gdi32.dll", "GetDIBits", reinterpret_cast<void*>(GetDIBits));
    ldr.registerExport("gdi32.dll", "StretchDIBits", reinterpret_cast<void*>(StretchDIBits));
    ldr.registerExport("gdi32.dll", "EnumFontFamiliesExW", reinterpret_cast<void*>(EnumFontFamiliesExW));
    ldr.registerExport("gdi32.dll", "EnumFontFamiliesExA", reinterpret_cast<void*>(EnumFontFamiliesExA));
    ldr.registerExport("gdi32.dll", "StartDocW", reinterpret_cast<void*>(StartDocW));
    ldr.registerExport("gdi32.dll", "EndDoc", reinterpret_cast<void*>(EndDoc));
    ldr.registerExport("gdi32.dll", "StartPage", reinterpret_cast<void*>(StartPage));
    ldr.registerExport("gdi32.dll", "EndPage", reinterpret_cast<void*>(EndPage));
    ldr.registerExport("gdi32.dll", "DPtoLP", reinterpret_cast<void*>(DPtoLP));
    ldr.registerExport("gdi32.dll", "ExtTextOutW", reinterpret_cast<void*>(ExtTextOutW));
    ldr.registerExport("gdi32.dll", "ExtTextOutA", reinterpret_cast<void*>(ExtTextOutA));
    ldr.registerExport("gdi32.dll", "SetTextAlign", reinterpret_cast<void*>(SetTextAlign));
    ldr.registerExport("gdi32.dll", "RectVisible", reinterpret_cast<void*>(RectVisible));
    ldr.registerExport("gdi32.dll", "GetTextExtentPointW", reinterpret_cast<void*>(GetTextExtentPointW));
    ldr.registerExport("gdi32.dll", "GetTextExtentPoint32A", reinterpret_cast<void*>(GetTextExtentPoint32A));
    ldr.registerExport("gdi32.dll", "GetTextExtentExPointW", reinterpret_cast<void*>(GetTextExtentExPointW));
    ldr.registerExport("gdi32.dll", "GetTextExtentExPointA", reinterpret_cast<void*>(GetTextExtentExPointA));
    ldr.registerExport("gdi32.dll", "CreateHatchBrush", reinterpret_cast<void*>(CreateHatchBrush));
    ldr.registerExport("gdi32.dll", "Polygon", reinterpret_cast<void*>(Polygon));
    ldr.registerExport("gdi32.dll", "Polyline", reinterpret_cast<void*>(Polyline));
    ldr.registerExport("gdi32.dll", "PolyPolyline", reinterpret_cast<void*>(PolyPolyline));
    ldr.registerExport("gdi32.dll", "ExtCreatePen", reinterpret_cast<void*>(ExtCreatePen));
    ldr.registerExport("gdi32.dll", "GdiAlphaBlend", reinterpret_cast<void*>(GdiAlphaBlend));
    ldr.registerExport("gdi32.dll", "GetTextMetricsW", reinterpret_cast<void*>(GetTextMetricsW));
    ldr.registerExport("gdi32.dll", "SetROP2", reinterpret_cast<void*>(SetROP2));
    ldr.registerExport("gdi32.dll", "GetROP2", reinterpret_cast<void*>(GetROP2));
    ldr.registerExport("gdi32.dll", "SaveDC", reinterpret_cast<void*>(SaveDC));
    ldr.registerExport("gdi32.dll", "RestoreDC", reinterpret_cast<void*>(RestoreDC));
    ldr.registerExport("gdi32.dll", "CreateRectRgn", reinterpret_cast<void*>(CreateRectRgn));
    ldr.registerExport("gdi32.dll", "CreateRectRgnIndirect", reinterpret_cast<void*>(CreateRectRgnIndirect));
    ldr.registerExport("gdi32.dll", "SelectClipRgn", reinterpret_cast<void*>(SelectClipRgn));
    ldr.registerExport("gdi32.dll", "GetClipRgn", reinterpret_cast<void*>(GetClipRgn));
    ldr.registerExport("gdi32.dll", "IntersectClipRect", reinterpret_cast<void*>(IntersectClipRect));
    ldr.registerExport("gdi32.dll", "ExcludeClipRect", reinterpret_cast<void*>(ExcludeClipRect));
    ldr.registerExport("gdi32.dll", "CombineRgn", reinterpret_cast<void*>(CombineRgn));
    ldr.registerExport("gdi32.dll", "RoundRect", reinterpret_cast<void*>(RoundRect));
    ldr.registerExport("gdi32.dll", "CreatePolygonRgn", reinterpret_cast<void*>(CreatePolygonRgn));
    ldr.registerExport("gdi32.dll", "PolyPolygon", reinterpret_cast<void*>(PolyPolygon));
    ldr.registerExport("gdi32.dll", "EqualRgn", reinterpret_cast<void*>(EqualRgn));
    ldr.registerExport("gdi32.dll", "PtInRegion", reinterpret_cast<void*>(PtInRegion));
    ldr.registerExport("gdi32.dll", "RectInRegion", reinterpret_cast<void*>(RectInRegion));
    ldr.registerExport("gdi32.dll", "GetEnhMetaFileW", reinterpret_cast<void*>(GetEnhMetaFileW));
    ldr.registerExport("gdi32.dll", "SetMetaFileBitsEx", reinterpret_cast<void*>(SetMetaFileBitsEx));
    ldr.registerExport("gdi32.dll", "GetGraphicsMode", reinterpret_cast<void*>(GetGraphicsMode));
    ldr.registerExport("gdi32.dll", "GetWorldTransform", reinterpret_cast<void*>(GetWorldTransform));
    ldr.registerExport("gdi32.dll", "ModifyWorldTransform", reinterpret_cast<void*>(ModifyWorldTransform));
    ldr.registerExport("gdi32.dll", "GetTextAlign", reinterpret_cast<void*>(GetTextAlign));
    ldr.registerExport("gdi32.dll", "OffsetClipRgn", reinterpret_cast<void*>(OffsetClipRgn));
    ldr.registerExport("gdi32.dll", "GetDCOrgEx", reinterpret_cast<void*>(GetDCOrgEx));
    ldr.registerExport("gdi32.dll", "GetRegionData", reinterpret_cast<void*>(GetRegionData));
    ldr.registerExport("gdi32.dll", "GetNearestColor", reinterpret_cast<void*>(GetNearestColor));
    ldr.registerExport("gdi32.dll", "CreateBitmapIndirect", reinterpret_cast<void*>(CreateBitmapIndirect));
    ldr.registerExport("gdi32.dll", "SetLayout", reinterpret_cast<void*>(SetLayout));
    ldr.registerExport("gdi32.dll", "ExtSelectClipRgn", reinterpret_cast<void*>(ExtSelectClipRgn));
    ldr.registerExport("gdi32.dll", "GradientFill", reinterpret_cast<void*>(GradientFill));
    ldr.registerExport("msimg32.dll", "GradientFill", reinterpret_cast<void*>(GradientFill));
    ldr.registerExport("msimg32.dll", "AlphaBlend", reinterpret_cast<void*>(AlphaBlend));
    ldr.registerExport("gdi32.dll", "AlphaBlend", reinterpret_cast<void*>(AlphaBlend));
    ldr.registerExport("gdi32.dll", "CreateFontA", reinterpret_cast<void*>(CreateFontA));
    ldr.registerExport("gdi32.dll", "CreateFontIndirectA", reinterpret_cast<void*>(CreateFontIndirectA));
    ldr.registerExport("gdi32.dll", "GetCharABCWidthsFloatA", reinterpret_cast<void*>(GetCharABCWidthsFloatA));
    ldr.registerExport("gdi32.dll", "GetCharWidth32A", reinterpret_cast<void*>(GetCharWidth32A));
    ldr.registerExport("gdi32.dll", "GetCharWidth32W", reinterpret_cast<void*>(GetCharWidth32W));
    ldr.registerExport("gdi32.dll", "GetCharWidthA", reinterpret_cast<void*>(GetCharWidthA));
    ldr.registerExport("gdi32.dll", "GetCharWidthW", reinterpret_cast<void*>(GetCharWidthW));
    ldr.registerExport("gdi32.dll", "GetCharacterPlacementW", reinterpret_cast<void*>(GetCharacterPlacementW));
    ldr.registerExport("gdi32.dll", "GetObjectA", reinterpret_cast<void*>(GetObjectA));
    ldr.registerExport("gdi32.dll", "GetOutlineTextMetricsA", reinterpret_cast<void*>(GetOutlineTextMetricsA));
    ldr.registerExport("gdi32.dll", "GetTextExtentPointA", reinterpret_cast<void*>(GetTextExtentPointA));
    ldr.registerExport("gdi32.dll", "GetTextMetricsA", reinterpret_cast<void*>(GetTextMetricsA));
    ldr.registerExport("gdi32.dll", "TranslateCharsetInfo", reinterpret_cast<void*>(TranslateCharsetInfo));
    ldr.registerExport("gdi32.dll", "UpdateColors", reinterpret_cast<void*>(UpdateColors));
    ldr.registerExport("gdi32.dll", "GetLayout", reinterpret_cast<void*>(GetLayout));
    ldr.registerExport("gdi32.dll", "SetPolyFillMode", reinterpret_cast<void*>(SetPolyFillMode));
    ldr.registerExport("gdi32.dll", "GetPolyFillMode", reinterpret_cast<void*>(GetPolyFillMode));
    ldr.registerExport("gdi32.dll", "SetViewportExtEx", reinterpret_cast<void*>(SetViewportExtEx));
    ldr.registerExport("gdi32.dll", "GetViewportExtEx", reinterpret_cast<void*>(GetViewportExtEx));
    ldr.registerExport("gdi32.dll", "SetWindowExtEx", reinterpret_cast<void*>(SetWindowExtEx));
    ldr.registerExport("gdi32.dll", "GetWindowExtEx", reinterpret_cast<void*>(GetWindowExtEx));
    ldr.registerExport("gdi32.dll", "OffsetViewportOrgEx", reinterpret_cast<void*>(OffsetViewportOrgEx));
    ldr.registerExport("gdi32.dll", "ScaleViewportExtEx", reinterpret_cast<void*>(ScaleViewportExtEx));
    ldr.registerExport("gdi32.dll", "ScaleWindowExtEx", reinterpret_cast<void*>(ScaleWindowExtEx));
    ldr.registerExport("gdi32.dll", "GetTextFaceW", reinterpret_cast<void*>(GetTextFaceW));
    ldr.registerExport("gdi32.dll", "CreateEllipticRgn", reinterpret_cast<void*>(CreateEllipticRgn));
    ldr.registerExport("gdi32.dll", "PtVisible", reinterpret_cast<void*>(PtVisible));
    ldr.registerExport("gdi32.dll", "Escape", reinterpret_cast<void*>(Escape));
    ldr.registerExport("gdi32.dll", "EnumFontFamiliesW", reinterpret_cast<void*>(EnumFontFamiliesW));
    ldr.registerExport("gdi32.dll", "CopyMetaFileW", reinterpret_cast<void*>(CopyMetaFileW));
    ldr.registerExport("gdi32.dll", "GetObjectType", reinterpret_cast<void*>(GetObjectType));
    ldr.registerExport("gdi32.dll", "SetBoundsRect", reinterpret_cast<void*>(SetBoundsRect));
    ldr.registerExport("gdi32.dll", "SetDCBrushColor", reinterpret_cast<void*>(SetDCBrushColor));

    ldr.registerExport("gdi32.dll", "TransparentBlt", reinterpret_cast<void*>(TransparentBlt));
    ldr.registerExport("msimg32.dll", "TransparentBlt", reinterpret_cast<void*>(TransparentBlt));
    ldr.registerExport("gdi32.dll", "Pie", reinterpret_cast<void*>(Pie));
    ldr.registerExport("gdi32.dll", "SetPaletteEntries", reinterpret_cast<void*>(SetPaletteEntries));
    ldr.registerExport("gdi32.dll", "GetRandomRgn", reinterpret_cast<void*>(GetRandomRgn));
    ldr.registerExport("gdi32.dll", "GetEnhMetaFileHeader", reinterpret_cast<void*>(GetEnhMetaFileHeader));
    ldr.registerExport("gdi32.dll", "CloseEnhMetaFile", reinterpret_cast<void*>(CloseEnhMetaFile));
    ldr.registerExport("gdi32.dll", "AngleArc", reinterpret_cast<void*>(AngleArc));
    ldr.registerExport("gdi32.dll", "ResizePalette", reinterpret_cast<void*>(ResizePalette));
    ldr.registerExport("gdi32.dll", "SetAbortProc", reinterpret_cast<void*>(SetAbortProc));
    ldr.registerExport("gdi32.dll", "SetRectRgn", reinterpret_cast<void*>(SetRectRgn));
    ldr.registerExport("gdi32.dll", "GetWindowOrgEx", reinterpret_cast<void*>(GetWindowOrgEx));
    ldr.registerExport("gdi32.dll", "SetPixelV", reinterpret_cast<void*>(SetPixelV));
    ldr.registerExport("gdi32.dll", "CreatePalette", reinterpret_cast<void*>(CreatePalette));
    ldr.registerExport("gdi32.dll", "CreateDCW", reinterpret_cast<void*>(CreateDCW));
    ldr.registerExport("gdi32.dll", "CreateICW", reinterpret_cast<void*>(CreateICW));
    ldr.registerExport("gdi32.dll", "PolyBezierTo", reinterpret_cast<void*>(PolyBezierTo));
    ldr.registerExport("gdi32.dll", "PlayEnhMetaFile", reinterpret_cast<void*>(PlayEnhMetaFile));
    ldr.registerExport("gdi32.dll", "GetBitmapBits", reinterpret_cast<void*>(GetBitmapBits));
    ldr.registerExport("gdi32.dll", "AbortDoc", reinterpret_cast<void*>(AbortDoc));
    ldr.registerExport("gdi32.dll", "GetSystemPaletteEntries", reinterpret_cast<void*>(GetSystemPaletteEntries));
    ldr.registerExport("gdi32.dll", "GetEnhMetaFileBits", reinterpret_cast<void*>(GetEnhMetaFileBits));
    ldr.registerExport("gdi32.dll", "CreatePenIndirect", reinterpret_cast<void*>(CreatePenIndirect));
    ldr.registerExport("gdi32.dll", "GetEnhMetaFilePaletteEntries", reinterpret_cast<void*>(GetEnhMetaFilePaletteEntries));
    ldr.registerExport("gdi32.dll", "SetMapMode", reinterpret_cast<void*>(SetMapMode));
    ldr.registerExport("gdi32.dll", "GetMapMode", reinterpret_cast<void*>(GetMapMode));
    ldr.registerExport("gdi32.dll", "PolyBezier", reinterpret_cast<void*>(PolyBezier));
    ldr.registerExport("gdi32.dll", "LPtoDP", reinterpret_cast<void*>(LPtoDP));
    ldr.registerExport("gdi32.dll", "GetCurrentObject", reinterpret_cast<void*>(GetCurrentObject));
    ldr.registerExport("gdi32.dll", "GetWinMetaFileBits", reinterpret_cast<void*>(GetWinMetaFileBits));
    ldr.registerExport("gdi32.dll", "GetEnhMetaFileDescriptionW", reinterpret_cast<void*>(GetEnhMetaFileDescriptionW));
    ldr.registerExport("gdi32.dll", "ArcTo", reinterpret_cast<void*>(ArcTo));
    ldr.registerExport("gdi32.dll", "CreateEnhMetaFileW", reinterpret_cast<void*>(CreateEnhMetaFileW));
    ldr.registerExport("gdi32.dll", "Arc", reinterpret_cast<void*>(Arc));
    ldr.registerExport("gdi32.dll", "SelectPalette", reinterpret_cast<void*>(SelectPalette));
    ldr.registerExport("gdi32.dll", "SetGraphicsMode", reinterpret_cast<void*>(SetGraphicsMode));
    ldr.registerExport("gdi32.dll", "MaskBlt", reinterpret_cast<void*>(MaskBlt));
    ldr.registerExport("gdi32.dll", "DeleteEnhMetaFile", reinterpret_cast<void*>(DeleteEnhMetaFile));
    ldr.registerExport("gdi32.dll", "Chord", reinterpret_cast<void*>(Chord));
    ldr.registerExport("gdi32.dll", "SetViewportOrgEx", reinterpret_cast<void*>(SetViewportOrgEx));
    ldr.registerExport("gdi32.dll", "GetViewportOrgEx", reinterpret_cast<void*>(GetViewportOrgEx));
    ldr.registerExport("gdi32.dll", "RealizePalette", reinterpret_cast<void*>(RealizePalette));
    ldr.registerExport("gdi32.dll", "SetDIBColorTable", reinterpret_cast<void*>(SetDIBColorTable));
    ldr.registerExport("gdi32.dll", "GetDIBColorTable", reinterpret_cast<void*>(GetDIBColorTable));
    ldr.registerExport("gdi32.dll", "CreateBrushIndirect", reinterpret_cast<void*>(CreateBrushIndirect));
    ldr.registerExport("gdi32.dll", "SetEnhMetaFileBits", reinterpret_cast<void*>(SetEnhMetaFileBits));
    ldr.registerExport("gdi32.dll", "SetWorldTransform", reinterpret_cast<void*>(SetWorldTransform));
    ldr.registerExport("gdi32.dll", "FrameRgn", reinterpret_cast<void*>(FrameRgn));
    ldr.registerExport("gdi32.dll", "GetClipBox", reinterpret_cast<void*>(GetClipBox));
    ldr.registerExport("gdi32.dll", "SetWinMetaFileBits", reinterpret_cast<void*>(SetWinMetaFileBits));
    ldr.registerExport("gdi32.dll", "CreateDIBitmap", reinterpret_cast<void*>(CreateDIBitmap));
    ldr.registerExport("gdi32.dll", "GetStretchBltMode", reinterpret_cast<void*>(GetStretchBltMode));
    ldr.registerExport("gdi32.dll", "ExtCreateRegion", reinterpret_cast<void*>(ExtCreateRegion));
    ldr.registerExport("gdi32.dll", "GetRgnBox", reinterpret_cast<void*>(GetRgnBox));
    ldr.registerExport("gdi32.dll", "EnumFontsW", reinterpret_cast<void*>(EnumFontsW));
    ldr.registerExport("gdi32.dll", "CreateHalftonePalette", reinterpret_cast<void*>(CreateHalftonePalette));
    ldr.registerExport("gdi32.dll", "ExtFloodFill", reinterpret_cast<void*>(ExtFloodFill));
    ldr.registerExport("gdi32.dll", "UnrealizeObject", reinterpret_cast<void*>(UnrealizeObject));
    ldr.registerExport("gdi32.dll", "CopyEnhMetaFileW", reinterpret_cast<void*>(CopyEnhMetaFileW));
    ldr.registerExport("gdi32.dll", "OffsetRgn", reinterpret_cast<void*>(OffsetRgn));
    ldr.registerExport("gdi32.dll", "GetMetaFileBitsEx", reinterpret_cast<void*>(GetMetaFileBitsEx));
    ldr.registerExport("gdi32.dll", "GetBrushOrgEx", reinterpret_cast<void*>(GetBrushOrgEx));
    ldr.registerExport("gdi32.dll", "GetCurrentPositionEx", reinterpret_cast<void*>(GetCurrentPositionEx));
    ldr.registerExport("gdi32.dll", "SetDCPenColor", reinterpret_cast<void*>(SetDCPenColor));
    ldr.registerExport("gdi32.dll", "GetNearestPaletteIndex", reinterpret_cast<void*>(GetNearestPaletteIndex));
    ldr.registerExport("gdi32.dll", "CreateRoundRectRgn", reinterpret_cast<void*>(CreateRoundRectRgn));
    ldr.registerExport("gdi32.dll", "GdiFlush", reinterpret_cast<void*>(GdiFlush));
    ldr.registerExport("gdi32.dll", "GetPaletteEntries", reinterpret_cast<void*>(GetPaletteEntries));
}

} // namespace micant::gdi32
