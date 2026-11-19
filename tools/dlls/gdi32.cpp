/**
 * @file gdi32.cpp
 * @brief GDI32 DLL implementation, split from the former combined
 *        User32/GDI32/IMM32 translation unit.
 */

#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/um/subsystems/graphics/font_manager.hpp"

#include <algorithm>
#include <array>


#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

namespace micant::um::winnt_user32_gdi32_imm32_abi {

int32_t Gdi32_GetDeviceCaps(void*, int32_t) noexcept;


namespace {
void updateAnimatedCursor() noexcept {}

template <typename T> T Win32kProc(const char* name) noexcept {
#define MICA_WIN32K_PROC(exportName) if (std::strcmp(name, #exportName) == 0) return reinterpret_cast<T>(&exportName)
    MICA_WIN32K_PROC(MicaWin32kGetSystemMetrics);
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
    MICA_WIN32K_PROC(MicaWin32kAdjustWindowRectEx);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
    MICA_WIN32K_PROC(MicaWin32kDebugInputTrace);
#endif
    MICA_WIN32K_PROC(MicaWin32kEnumWindows);
    MICA_WIN32K_PROC(MicaWin32kGetDlgItem);
    MICA_WIN32K_PROC(MicaWin32kGetWindowTextW);
    MICA_WIN32K_PROC(MicaWin32kIsWindowVisible);
    MICA_WIN32K_PROC(MicaWin32kIsIconic);
    MICA_WIN32K_PROC(MicaWin32kGetForegroundWindow);
    MICA_WIN32K_PROC(MicaWin32kSetFocus);
    MICA_WIN32K_PROC(MicaWin32kGetFocus);
    MICA_WIN32K_PROC(MicaWin32kGetCapture);
    MICA_WIN32K_PROC(MicaWin32kSetCapture);
    MICA_WIN32K_PROC(MicaWin32kReleaseCapture);
    MICA_WIN32K_PROC(MicaWin32kGetCursorPos);
    MICA_WIN32K_PROC(MicaWin32kGetClipCursor);
    MICA_WIN32K_PROC(MicaWin32kSetCursorPos);
    MICA_WIN32K_PROC(MicaWin32kClipCursor);
    MICA_WIN32K_PROC(MicaWin32kShowCursor);
    MICA_WIN32K_PROC(MicaWin32kSetCursor);
    MICA_WIN32K_PROC(MicaWin32kGetCursor);
    MICA_WIN32K_PROC(MicaWin32kGetCursorInfo);
    MICA_WIN32K_PROC(MicaWin32kSetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kSetWindowIconData);
    MICA_WIN32K_PROC(MicaWin32kGetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kSetWindowTitleSurface);
    MICA_WIN32K_PROC(MicaWin32kRegisterClassW);
    MICA_WIN32K_PROC(MicaWin32kDispatchMessageW);
    MICA_WIN32K_PROC(MicaWin32kSetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kGetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kDefWindowProcW);
    MICA_WIN32K_PROC(MicaWin32kGetDC);
    MICA_WIN32K_PROC(MicaWin32kGetDCSurface);
    MICA_WIN32K_PROC(MicaWin32kReleaseDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleDC);
    MICA_WIN32K_PROC(MicaWin32kDeleteDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleBitmap);
    MICA_WIN32K_PROC(MicaWin32kSelectObject);
    MICA_WIN32K_PROC(MicaWin32kBitBlt);
    MICA_WIN32K_PROC(MicaWin32kPresentDC);
    MICA_WIN32K_PROC(MicaWin32kPresentDCRect);
    MICA_WIN32K_PROC(MicaWin32kDeleteObject);
    MICA_WIN32K_PROC(MicaWin32kGetStockObject);
    MICA_WIN32K_PROC(MicaWin32kCreateDibSection);
    MICA_WIN32K_PROC(MicaWin32kFillRect);
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
constexpr size_t GdiFontCapacity = 24;
constexpr size_t GdiBitmapCapacity = 32;
constexpr size_t GdiDcCapacity = 64;
constexpr size_t GdiBrushCapacity = 64;
constexpr int32_t GdiFontSize = 9;
struct GdiLogFontW {
    int32_t height, width, escapement, orientation, weight;
    uint8_t italic, underline, strikeOut, charSet, outPrecision, clipPrecision, quality, pitchAndFamily;
    wchar_t faceName[32];
};
struct GdiSize { int32_t cx{}, cy{}; };
struct GdiTextMetricW {
    int32_t height{}, ascent{}, descent{}, internalLeading{}, externalLeading{};
    int32_t averageCharWidth{}, maximumCharWidth{}, weight{}, overhang{};
    int32_t digitizedAspectX{}, digitizedAspectY{};
    wchar_t firstChar{}, lastChar{}, defaultChar{}, breakChar{};
    uint8_t italic{}, underlined{}, struckOut{}, pitchAndFamily{}, charSet{};
};
struct GdiEnumLogFontExW {
    GdiLogFontW logFont{};
    wchar_t fullName[64]{};
    wchar_t style[32]{};
    wchar_t script[32]{};
};
struct GdiNewTextMetricExW {
    GdiTextMetricW textMetric{};
    uint32_t fontSignature[4]{};
};
using GdiFontEnumProcW = int32_t (*)(const GdiLogFontW*, const GdiTextMetricW*,
                                     uint32_t, intptr_t);
static_assert(sizeof(GdiSize) == 8);
static_assert(sizeof(GdiTextMetricW) == 60);
static_assert(offsetof(GdiTextMetricW, firstChar) == 44);
static_assert(offsetof(GdiEnumLogFontExW, fullName) == sizeof(GdiLogFontW));
struct GdiFontObject {
    uint64_t magic{0x4D494341464F4E54ULL};
    bool active{};
    bool stock{};
    char16_t family[32]{};
    micant::um::graphics::LogicalFontRequest request{};
    micant::um::graphics::FontManager manager{};
};
static_assert(sizeof(GdiLogFontW) == 92);
static_assert(offsetof(GdiLogFontW, faceName) == 28);
struct GdiBitmapSurface {
    void* bitmap{};
    uint32_t* pixels{};
    uint32_t width{}, height{};
    bool topDown{};
};
struct GdiDcSelection {
    void* dc{};
    void* bitmap{};
    GdiFontObject* font{};
    uint32_t textColor{0x00000000u}; // COLORREF black
    int32_t backgroundMode{1};       // TRANSPARENT
    MicaWin32kRect clip{};
    struct ClipState { uint32_t generation{}; MicaWin32kRect previous{}; bool occupied{}; } clips[16]{};
    uint32_t nextClipGeneration{1};
};
struct GdiBrushObject {
    uint32_t colorRef{};
    uint32_t generation{};
    bool active{};
};
GdiFontObject g_gdiFonts[GdiFontCapacity]{};
GdiFontObject* g_systemFont{};
GdiBitmapSurface g_gdiBitmaps[GdiBitmapCapacity]{};
GdiDcSelection g_gdiDcs[GdiDcCapacity]{};
GdiBrushObject g_gdiBrushes[GdiBrushCapacity]{};
uint32_t g_gdiBrushGeneration{1};
uint32_t g_gdiBrushLock{};
GdiFontObject* GetSystemGdiFont() noexcept;
struct GdiBrushGuard {
    GdiBrushGuard() noexcept {
        while (__atomic_exchange_n(&g_gdiBrushLock, 1u, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause" ::: "memory");
    }
    ~GdiBrushGuard() { __atomic_store_n(&g_gdiBrushLock, 0u, __ATOMIC_RELEASE); }
};

void* GdiBrushHandle(size_t slot, uint32_t generation) noexcept {
    return reinterpret_cast<void*>((static_cast<uintptr_t>(generation) << 7) | (slot + 1));
}
GdiBrushObject* FindGdiBrush(void* handle) noexcept {
    const uintptr_t value = reinterpret_cast<uintptr_t>(handle);
    const uintptr_t slot = value & 0x7Fu;
    const uint32_t generation = static_cast<uint32_t>(value >> 7);
    if (!slot || slot > GdiBrushCapacity || !generation) return nullptr;
    auto& brush = g_gdiBrushes[slot - 1];
    return brush.active && brush.generation == generation ? &brush : nullptr;
}

GdiFontObject* FindGdiFont(void* handle) noexcept {
    for (auto& font : g_gdiFonts) if (font.active && handle == &font) return &font;
    return nullptr;
}
GdiBitmapSurface* FindGdiBitmap(void* handle) noexcept {
    for (auto& bitmap : g_gdiBitmaps) if (bitmap.bitmap == handle && handle) return &bitmap;
    return nullptr;
}
GdiDcSelection* FindGdiDc(void* handle, bool create) noexcept {
    if (!handle) return nullptr;
    GdiDcSelection* freeSlot = nullptr;
    for (auto& dc : g_gdiDcs) {
        if (dc.dc == handle) return &dc;
        if (!dc.dc && !freeSlot) freeSlot = &dc;
    }
    if (!create || !freeSlot) return nullptr;
    *freeSlot = GdiDcSelection{};
    freeSlot->dc = handle;
    // A newly acquired Windows DC starts with its stock system font selected.
    // Keep that default in the GDI32-owned per-DC state, including HDCs that
    // User32 created for BeginPaint and passed across the DLL boundary.
    freeSlot->font = GetSystemGdiFont();
    if (!freeSlot->font) {
        *freeSlot = {};
        return nullptr;
    }
    return freeSlot;
}

GdiFontObject* CreateGdiFont(const GdiLogFontW& logical) noexcept {
    if (logical.width || logical.escapement || logical.orientation || logical.underline ||
        logical.strikeOut || logical.weight < 0 || logical.weight > 1000)
        return nullptr;
    size_t familyLength = 0;
    while (familyLength < 32 && logical.faceName[familyLength]) ++familyLength;
    if (familyLength == 32) return nullptr;
    for (auto& font : g_gdiFonts) {
        if (font.active) continue;
        font.magic = 0x4D494341464F4E54ULL;
        font.stock = false;
        font.family[0] = 0;
        font.active = true;
        const wchar_t* family = familyLength ? logical.faceName : L"Segoe UI";
        size_t copyLength = familyLength ? familyLength : 8;
        if (copyLength > 31) copyLength = 31;
        for (size_t i = 0; i < copyLength; ++i) font.family[i] = static_cast<char16_t>(family[i]);
        font.family[copyLength] = 0;
        uint32_t points = GdiFontSize;
        if (logical.height) {
            const int64_t magnitude = logical.height < 0
                ? -static_cast<int64_t>(logical.height) : logical.height;
            const uint64_t scaled = static_cast<uint64_t>(magnitude) * 72u;
            points = static_cast<uint32_t>((scaled + 48u) / 96u);
            if (!points) points = 1;
            if (points > 512) { font.active = false; return nullptr; }
        }
        font.request = {font.family,
            static_cast<uint16_t>(logical.weight ? logical.weight : 400),
            logical.italic != 0, static_cast<uint16_t>(points), 96};
        if (!font.manager.initialize(font.request)) {
            font.manager.shutdown();
            font.active = false;
            return nullptr;
        }
        return &font;
    }
    return nullptr;
}

GdiFontObject* GetSystemGdiFont() noexcept {
    if (g_systemFont && g_systemFont->active) return g_systemFont;
    GdiLogFontW logical{};
    GdiFontObject* font = CreateGdiFont(logical);
    if (font) {
        font->stock = true;
        g_systemFont = font;
    }
    return font;
}

bool AppendUtf8(uint32_t scalar, char* output, size_t capacity, size_t& used) noexcept {
    uint8_t bytes[4]{}; size_t count{};
    if (scalar <= 0x7F) { bytes[0] = static_cast<uint8_t>(scalar); count = 1; }
    else if (scalar <= 0x7FF) {
        bytes[0] = static_cast<uint8_t>(0xC0 | (scalar >> 6));
        bytes[1] = static_cast<uint8_t>(0x80 | (scalar & 0x3F)); count = 2;
    } else if (scalar <= 0xFFFF && !(scalar >= 0xD800 && scalar <= 0xDFFF)) {
        bytes[0] = static_cast<uint8_t>(0xE0 | (scalar >> 12));
        bytes[1] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3F));
        bytes[2] = static_cast<uint8_t>(0x80 | (scalar & 0x3F)); count = 3;
    } else if (scalar <= 0x10FFFF) {
        bytes[0] = static_cast<uint8_t>(0xF0 | (scalar >> 18));
        bytes[1] = static_cast<uint8_t>(0x80 | ((scalar >> 12) & 0x3F));
        bytes[2] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3F));
        bytes[3] = static_cast<uint8_t>(0x80 | (scalar & 0x3F)); count = 4;
    } else return false;
    if (used + count >= capacity) return false;
    for (size_t i = 0; i < count; ++i) output[used++] = static_cast<char>(bytes[i]);
    return true;
}

bool Utf16ToUtf8(const wchar_t* input, int32_t length, char* output,
                 size_t capacity) noexcept {
    if (!input || !output || length <= 0 || length > 4096 || capacity < 2) return false;
    size_t used = 0;
    for (int32_t i = 0; i < length; ++i) {
        uint32_t scalar = static_cast<uint16_t>(input[i]);
        if (scalar >= 0xD800 && scalar <= 0xDBFF) {
            if (++i >= length) return false;
            const uint32_t low = static_cast<uint16_t>(input[i]);
            if (low < 0xDC00 || low > 0xDFFF) return false;
            scalar = 0x10000 + ((scalar - 0xD800) << 10) + (low - 0xDC00);
        } else if (scalar >= 0xDC00 && scalar <= 0xDFFF) return false;
        if (!scalar || !AppendUtf8(scalar, output, capacity, used)) return false;
    }
    output[used] = 0;
    return true;
}

int32_t g_activePixelFormat = 1;
int32_t g_imeOpenStatus = 0;
intptr_t AuditWindowProc(void*,uint32_t message,uintptr_t,intptr_t) noexcept {
    return message == 0x0081 ? 1 : 0;
}
} // namespace


// gdi32.dll implementations
void* Gdi32_CreateCompatibleDC(void* hdc) noexcept {
    using Fn=void* (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kCreateCompatibleDC");
    void* dc = fn ? fn(hdc) : nullptr;
    if (dc && !FindGdiDc(dc, true)) {
        using DeleteFn=int32_t (*)(void*); auto deleteFn=Win32kProc<DeleteFn>("MicaWin32kDeleteDC");
        if (deleteFn) (void)deleteFn(dc);
        return nullptr;
    }
    return dc;
}

void* Gdi32_CreateCompatibleBitmap(void* hdc, int32_t width, int32_t height) noexcept {
    using Fn=void* (*)(void*,int32_t,int32_t); auto fn=Win32kProc<Fn>("MicaWin32kCreateCompatibleBitmap");
    return fn ? fn(hdc,width,height) : nullptr;
}

int32_t Gdi32_DeleteDC(void* hdc) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDeleteDC");
    const int32_t result = fn ? fn(hdc) : 0;
    if (result) for (auto& dc : g_gdiDcs) if (dc.dc == hdc) dc = GdiDcSelection{};
    return result;
}

void Gdi32_ForgetDcState(void* hdc) noexcept {
    if (auto* dc = FindGdiDc(hdc, false)) *dc = GdiDcSelection{};
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

void* Gdi32_CreateSolidBrush(uint32_t colorRef) noexcept {
    GdiBrushGuard guard;
    for (size_t i = 0; i < GdiBrushCapacity; ++i) {
        auto& brush = g_gdiBrushes[i];
        if (brush.active) continue;
        uint32_t generation = ++g_gdiBrushGeneration;
        if (!generation) generation = ++g_gdiBrushGeneration;
        brush.colorRef = colorRef & 0x00FFFFFFu;
        brush.generation = generation;
        brush.active = true;
        return GdiBrushHandle(i, generation);
    }
    return nullptr;
}

int32_t Gdi32_FillRect(void* hdc, const void* rectangle, void* hbrush) noexcept {
    if (!hdc || !rectangle) return 0;
    GdiBrushGuard guard;
    auto* brush = FindGdiBrush(hbrush);
    if (!brush) return 0;
    using Fn = int32_t (*)(void*, const MicaWin32kRect*, uint32_t);
    auto fill = Win32kProc<Fn>("MicaWin32kFillRect");
    return fill ? fill(hdc, static_cast<const MicaWin32kRect*>(rectangle),
                       brush->colorRef) : 0;
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

void* Gdi32_CreateDIBSection(void* hdc, const void* pbmi, uint32_t usage, void** ppvBits, void* hSection, uint32_t offset) noexcept {
    if (ppvBits) *ppvBits=nullptr;
    if (!pbmi || !ppvBits || usage != 0 || hSection || offset != 0) return nullptr;
    const auto* info=static_cast<const MicaWin32kBitmapInfoHeader*>(pbmi);
    if (info->size < sizeof(MicaWin32kBitmapInfoHeader) || info->planes != 1 || info->bitCount != 32 || info->compression != 0)
        return nullptr;
    if (info->width <= 0 || info->height == 0 || info->height == INT32_MIN) return nullptr;
    const int32_t height=info->height < 0 ? -info->height : info->height;
    using Fn=void* (*)(void*,int32_t,int32_t,void**); auto fn=Win32kProc<Fn>("MicaWin32kCreateDibSection");
    void* bitmap = fn ? fn(hdc,info->width,height,ppvBits) : nullptr;
    if (!bitmap) return nullptr;
    if (!*ppvBits) {
        using DeleteFn=int32_t (*)(void*); auto deleteFn=Win32kProc<DeleteFn>("MicaWin32kDeleteObject");
        if (deleteFn) (void)deleteFn(bitmap);
        return nullptr;
    }
    GdiBitmapSurface* slot = nullptr;
    for (auto& current : g_gdiBitmaps) if (!current.bitmap) { slot = &current; break; }
    if (!slot) {
        using DeleteFn=int32_t (*)(void*); auto deleteFn=Win32kProc<DeleteFn>("MicaWin32kDeleteObject");
        if (deleteFn) (void)deleteFn(bitmap);
        *ppvBits = nullptr;
        return nullptr;
    }
    *slot = {bitmap, static_cast<uint32_t*>(*ppvBits), static_cast<uint32_t>(info->width),
             static_cast<uint32_t>(height), info->height < 0};
    return bitmap;
}

int32_t Gdi32_BitBlt(void* hdc, int32_t x, int32_t y, int32_t cx, int32_t cy, void* hdcSrc, int32_t x1, int32_t y1, uint32_t rop) noexcept {
    using Fn=int32_t (*)(void*,int32_t,int32_t,int32_t,int32_t,void*,int32_t,int32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kBitBlt");
    return fn ? fn(hdc,x,y,cx,cy,hdcSrc,x1,y1,rop) : 0;
}

void* Gdi32_GetStockObject(int32_t i) noexcept {
    if (i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 16 || i == 17)
        return GetSystemGdiFont();
    using Fn=void* (*)(int32_t); auto fn=Win32kProc<Fn>("MicaWin32kGetStockObject");
    return fn ? fn(i) : nullptr;
}

int32_t Gdi32_GetObjectW(void* handle, int32_t bufferBytes, void* output) noexcept {
    GdiFontObject* font = FindGdiFont(handle);
    if (!font || bufferBytes < 0) return 0;

    GdiLogFontW logical{};
    const auto& request = font->request;
    const uint32_t emPixels = micant::um::graphics::truetype::pixelsFromPoints(
        request.pointSize, request.dpi);
    if (emPixels > static_cast<uint32_t>(INT32_MAX)) return 0;
    logical.height = -static_cast<int32_t>(emPixels);
    logical.weight = request.weight;
    logical.italic = request.italic ? 1 : 0;
    logical.charSet = 1; // DEFAULT_CHARSET
    for (size_t i = 0; i < 31 && font->family[i]; ++i)
        logical.faceName[i] = static_cast<wchar_t>(font->family[i]);

    constexpr int32_t requiredBytes = static_cast<int32_t>(sizeof(logical));
    if (!output) return requiredBytes;
    if (!bufferBytes) return 0;
    const int32_t copiedBytes = std::min(bufferBytes, requiredBytes);
    std::memcpy(output, &logical, static_cast<size_t>(copiedBytes));
    return copiedBytes;
}

void* Gdi32_SelectObject(void* hdc, void* h) noexcept {
    if (!hdc || !h) return nullptr;
    if (GdiFontObject* font = FindGdiFont(h)) {
        GdiDcSelection* dc = FindGdiDc(hdc, true);
        if (!dc) return nullptr;
        void* previous = dc->font ? static_cast<void*>(dc->font) : Gdi32_GetStockObject(13);
        if (!previous) return nullptr;
        dc->font = font;
        return previous;
    }
    using Fn=void* (*)(void*,void*); auto fn=Win32kProc<Fn>("MicaWin32kSelectObject");
    void* previous = fn ? fn(hdc,h) : nullptr;
    if (previous) {
        GdiDcSelection* dc = FindGdiDc(hdc, false);
        GdiBitmapSurface* bitmap = FindGdiBitmap(h);
        if (!dc && bitmap) {
            (void)fn(hdc, previous); // Roll back when local tracking cannot be reserved.
            return nullptr;
        }
        if (dc) {
            // Selecting the DC's stock/default bitmap must clear the old
            // DIB reference too; otherwise DeleteObject sees a stale local
            // selection and leaks every temporary DIB used by themed paint.
            dc->bitmap = bitmap ? h : nullptr;
            if (!bitmap) dc->clip = {};
        }
        if (bitmap && dc) {
            GdiBitmapSurface* surface = FindGdiBitmap(h);
            dc->clip = {0, 0, static_cast<int32_t>(surface->width),
                        static_cast<int32_t>(surface->height)};
        }
    }
    return previous;
}

int32_t Gdi32_DeleteObject(void* ho) noexcept {
    {
        GdiBrushGuard guard;
    if (GdiBrushObject* brush = FindGdiBrush(ho)) {
        brush->active = false;
        brush->colorRef = 0;
        return 1;
    }
    }
    if (GdiFontObject* font = FindGdiFont(ho)) {
        if (font->stock) return 0;
        for (const auto& dc : g_gdiDcs) if (dc.dc && dc.font == font) return 0;
        font->manager.shutdown();
        font->active = false;
        font->family[0] = 0;
        return 1;
    }
    GdiBitmapSurface* bitmap = FindGdiBitmap(ho);
    if (bitmap) for (const auto& dc : g_gdiDcs) if (dc.dc && dc.bitmap == ho) return 0;
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDeleteObject");
    const int32_t result = fn ? fn(ho) : 0;
    if (result && bitmap) *bitmap = GdiBitmapSurface{};
    return result;
}

void* Gdi32_CreateFontIndirectW(const GdiLogFontW* logical) noexcept {
    return logical ? static_cast<void*>(CreateGdiFont(*logical)) : nullptr;
}

void* Gdi32_CreateFontW(int32_t height, int32_t width, int32_t escapement,
        int32_t orientation, int32_t weight, uint32_t italic, uint32_t underline,
        uint32_t strikeOut, uint32_t charSet, uint32_t outPrecision,
        uint32_t clipPrecision, uint32_t quality, uint32_t pitchAndFamily,
        const wchar_t* faceName) noexcept {
    GdiLogFontW logical{};
    logical.height = height; logical.width = width;
    logical.escapement = escapement; logical.orientation = orientation;
    logical.weight = weight; logical.italic = italic ? 1 : 0;
    logical.underline = underline ? 1 : 0; logical.strikeOut = strikeOut ? 1 : 0;
    logical.charSet = static_cast<uint8_t>(charSet);
    logical.outPrecision = static_cast<uint8_t>(outPrecision);
    logical.clipPrecision = static_cast<uint8_t>(clipPrecision);
    logical.quality = static_cast<uint8_t>(quality);
    logical.pitchAndFamily = static_cast<uint8_t>(pitchAndFamily);
    if (faceName) {
        size_t i = 0;
        while (i < 31 && faceName[i]) { logical.faceName[i] = faceName[i]; ++i; }
        if (faceName[i] && i == 31) return nullptr;
    }
    return CreateGdiFont(logical);
}

int32_t Gdi32_SetTextColor(void* hdc, uint32_t color) noexcept {
    GdiDcSelection* dc = FindGdiDc(hdc, true);
    if (!dc) return static_cast<int32_t>(0xFFFFFFFFu);
    const uint32_t previous = dc->textColor;
    dc->textColor = color & 0x00FFFFFFu;
    return static_cast<int32_t>(previous);
}

int32_t Gdi32_SetBkMode(void* hdc, int32_t mode) noexcept {
    if (mode != 1) return 0; // Opaque backgrounds are not part of this renderer subset.
    GdiDcSelection* dc = FindGdiDc(hdc, true);
    if (!dc) return 0;
    const int32_t previous = dc->backgroundMode;
    dc->backgroundMode = mode;
    return previous;
}

int32_t Gdi32_TextOutW(void* hdc, int32_t x, int32_t y,
                       const wchar_t* text, int32_t count) noexcept {
    if (!hdc || !text || count <= 0 || count > 4096) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || dc->backgroundMode != 1) return 0;
    GdiBitmapSurface windowSurface{};
    GdiBitmapSurface* bitmap = FindGdiBitmap(dc->bitmap);
    if (!bitmap) {
        using SurfaceFn = int32_t (*)(void*, uint32_t**, uint32_t*, uint32_t*, uint32_t*);
        const auto getSurface = Win32kProc<SurfaceFn>("MicaWin32kGetDCSurface");
        uint32_t* pixels{};
        uint32_t clientWidth{}, clientHeight{}, stride{};
        if (!getSurface || !getSurface(hdc, &pixels, &clientWidth, &clientHeight, &stride) ||
            !pixels || !clientWidth || !clientHeight || stride < clientWidth) return 0;
        windowSurface = {nullptr, pixels, stride, clientHeight, true};
        bitmap = &windowSurface;
        if (dc->clip.right <= dc->clip.left || dc->clip.bottom <= dc->clip.top)
            dc->clip = {0, 0, static_cast<int32_t>(clientWidth),
                        static_cast<int32_t>(clientHeight)};
    }
    if (!bitmap->pixels || !bitmap->topDown) return 0;
    char utf8[16385]{};
    if (!Utf16ToUtf8(text, count, utf8, sizeof(utf8))) return 0;
    const uint32_t r = dc->textColor & 0xFF;
    const uint32_t g = (dc->textColor >> 8) & 0xFF;
    const uint32_t b = (dc->textColor >> 16) & 0xFF;
    const uint32_t argb = 0xFF000000u | (r << 16) | (g << 8) | b;
    const micant::um::graphics::TextClipRect clip{
        dc->clip.left, dc->clip.top, dc->clip.right, dc->clip.bottom};
    return dc->font->manager.drawUtf8(bitmap->pixels, bitmap->width, bitmap->height,
        x, y, utf8, argb, &clip) ? 1 : 0;
}

int32_t Gdi32_MicaPushClipRect(void* hdc, const MicaWin32kRect* rectangle,
                               void** token) noexcept {
    if (token) *token = nullptr;
    if (!hdc || !rectangle || !token || rectangle->right < rectangle->left ||
        rectangle->bottom < rectangle->top) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || (dc->bitmap && !FindGdiBitmap(dc->bitmap))) return 0;
    size_t slot = 0;
    while (slot < std::size(dc->clips) && dc->clips[slot].occupied) ++slot;
    if (slot == std::size(dc->clips)) return 0;
    uint32_t generation = dc->nextClipGeneration++;
    if (!generation) generation = dc->nextClipGeneration++;
    auto& state = dc->clips[slot];
    state = {generation, dc->clip, true};
    dc->clip.left = std::max(dc->clip.left, rectangle->left);
    dc->clip.top = std::max(dc->clip.top, rectangle->top);
    dc->clip.right = std::min(dc->clip.right, rectangle->right);
    dc->clip.bottom = std::min(dc->clip.bottom, rectangle->bottom);
    *token = reinterpret_cast<void*>((static_cast<uintptr_t>(generation) << 5) |
                                     static_cast<uintptr_t>(slot + 1));
    return 1;
}

int32_t Gdi32_MicaPopClipRect(void* hdc, void* token) noexcept {
    if (!hdc || !token) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc) return 0;
    const uintptr_t encoded = reinterpret_cast<uintptr_t>(token);
    const uintptr_t slotValue = encoded & 0x1Fu;
    const uint32_t generation = static_cast<uint32_t>(encoded >> 5);
    if (!slotValue || slotValue > std::size(dc->clips) || !generation) return 0;
    auto& state = dc->clips[slotValue - 1];
    if (!state.occupied || state.generation != generation) return 0;
    dc->clip = state.previous;
    state = {};
    return 1;
}

int32_t Gdi32_ExtTextOutW(void* hdc, int32_t x, int32_t y, uint32_t options,
        const void* rect, const wchar_t* text, uint32_t count, const int32_t* advances) noexcept {
    if (options || rect || advances || count > 4096 || count > static_cast<uint32_t>(INT32_MAX))
        return 0;
    return Gdi32_TextOutW(hdc, x, y, text, static_cast<int32_t>(count));
}

int32_t Gdi32_GetTextExtentPoint32W(void* hdc, const wchar_t* text, int32_t count,
                                    GdiSize* output) noexcept {
    if (!hdc || !text || !output || count < 0 || count > 4096) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || !dc->font->active) return 0;
    if (count == 0) {
        micant::um::graphics::FontMetricsPixels metrics{};
        if (!dc->font->manager.pixelMetrics(metrics)) return 0;
        *output = {0, metrics.lineHeight};
        return 1;
    }
    char utf8[16385]{};
    if (!Utf16ToUtf8(text, count, utf8, sizeof(utf8))) return 0;
    micant::um::graphics::TextExtentPixels extent{};
    if (!dc->font->manager.measureUtf8(utf8, extent)) return 0;
    *output = {extent.width, extent.height};
    return 1;
}

int32_t Gdi32_GetTextMetricsW(void* hdc, GdiTextMetricW* output) noexcept {
    if (!hdc || !output) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || !dc->font->active) return 0;
    micant::um::graphics::FontMetricsPixels metrics{};
    if (!dc->font->manager.pixelMetrics(metrics)) return 0;
    *output = {};
    output->height = metrics.lineHeight;
    output->ascent = metrics.ascent;
    output->descent = metrics.descent;
    output->externalLeading = metrics.lineGap;
    output->averageCharWidth = metrics.averageAdvance;
    output->maximumCharWidth = metrics.maximumAdvance;
    output->weight = dc->font->request.weight;
    output->digitizedAspectX = static_cast<int32_t>(dc->font->request.dpi);
    output->digitizedAspectY = static_cast<int32_t>(dc->font->request.dpi);
    output->firstChar = L' ';
    output->lastChar = L'~';
    output->defaultChar = L'?';
    output->breakChar = L' ';
    output->italic = dc->font->request.italic ? 1 : 0;
    output->charSet = 1;
    return 1;
}

uint32_t Gdi32_GetGlyphIndicesW(void* hdc, const wchar_t* text, int32_t count,
        uint16_t* glyphIndices, uint32_t flags) noexcept {
    constexpr uint32_t GdiError = 0xFFFFFFFFu;
    constexpr uint32_t MarkMissing = 0x0001u;
    if (count < 0 || count > 4096 || (flags & ~MarkMissing) ||
        (count && (!text || !glyphIndices))) return GdiError;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || !dc->font->active) return GdiError;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t scalar = static_cast<uint16_t>(text[i]);
        if (scalar >= 0xD800u && scalar <= 0xDFFFu) return GdiError;
        uint16_t glyph = dc->font->manager.glyphIndex(scalar);
        if (!glyph && (flags & MarkMissing)) glyph = 0xFFFFu;
        glyphIndices[i] = glyph;
    }
    return static_cast<uint32_t>(count);
}

int32_t Gdi32_EnumFontFamiliesExW(void* hdc, const GdiLogFontW* filter,
        GdiFontEnumProcW callback, intptr_t parameter, uint32_t flags) noexcept {
    constexpr uint32_t TrueTypeFontType = 0x0004;
    if (!FindGdiDc(hdc, false) || !callback || (flags != 0 && flags != TrueTypeFontType))
        return 0;
    if (filter && filter->charSet != 0 && filter->charSet != 1) return 0;
    const auto familyMatches = [](const wchar_t* requested, const wchar_t* candidate) noexcept {
        if (!requested || !requested[0]) return true;
        size_t i = 0;
        for (; i < 31 && requested[i] && candidate[i]; ++i) {
            wchar_t left = requested[i], right = candidate[i];
            if (left >= L'A' && left <= L'Z') left += L'a' - L'A';
            if (right >= L'A' && right <= L'Z') right += L'a' - L'A';
            if (left != right) return false;
        }
        return i < 31 && requested[i] == 0 && candidate[i] == 0;
    };
    struct Style { uint16_t weight; bool italic; const wchar_t* label; };
    constexpr Style standardStyles[] = {
        {400, false, L"Regular"}, {700, false, L"Bold"},
        {400, true, L"Italic"}, {700, true, L"Bold Italic"}};
    constexpr Style selawikStyles[] = {
        {400, false, L"Regular"}, {600, false, L"Semibold"}, {700, false, L"Bold"}};
    constexpr const wchar_t* families[] = {
        L"Segoe UI", L"Segoe UI Semibold", L"Arial", L"Times New Roman",
        L"Courier New", L"Calibri", L"DejaVu Sans"};
    int32_t lastResult = 0;
    for (const wchar_t* family : families) {
        if (!familyMatches(filter ? filter->faceName : nullptr, family)) continue;
        const Style* styles = family == families[0] || family == families[1]
            ? selawikStyles : standardStyles;
        const size_t styleCount = styles == selawikStyles
            ? std::size(selawikStyles) : std::size(standardStyles);
        for (size_t styleIndex = 0; styleIndex < styleCount; ++styleIndex) {
            if (family == families[1] && styleIndex != 1) continue;
            const Style& style = styles[styleIndex];
            GdiLogFontW logical{};
            logical.height = -12;
            logical.weight = style.weight;
            logical.italic = style.italic ? 1 : 0;
            logical.charSet = 1;
            char16_t requestedFamily[32]{};
            for (size_t i = 0; family[i] && i + 1 < std::size(logical.faceName); ++i)
                requestedFamily[i] = static_cast<char16_t>(family[i]);
            for (size_t i = 0; i < std::size(requestedFamily); ++i)
                logical.faceName[i] = family[i];
            micant::um::graphics::FontManager metricManager{};
            const micant::um::graphics::LogicalFontRequest request{
                requestedFamily, style.weight, style.italic, 9, 96};
            if (!metricManager.initializeForMetrics(request)) continue;
            GdiTextMetricW metrics{};
            micant::um::graphics::FontMetricsPixels projected{};
            const bool projectedOk = metricManager.pixelMetrics(projected);
            if (!projectedOk) { metricManager.shutdown(); return 0; }
            metrics.height = projected.lineHeight;
            metrics.ascent = projected.ascent;
            metrics.descent = projected.descent;
            metrics.externalLeading = projected.lineGap;
            metrics.averageCharWidth = projected.averageAdvance;
            metrics.maximumCharWidth = projected.maximumAdvance;
            metrics.weight = style.weight;
            metrics.digitizedAspectX = request.dpi;
            metrics.digitizedAspectY = request.dpi;
            metrics.firstChar = L' ';
            metrics.lastChar = L'~';
            metrics.defaultChar = L'?';
            metrics.breakChar = L' ';
            metrics.italic = style.italic ? 1 : 0;
            metrics.charSet = 1;
            GdiEnumLogFontExW enumeration{};
            enumeration.logFont = logical;
            for (size_t i = 0; family[i] && i + 1 < std::size(enumeration.fullName); ++i)
                enumeration.fullName[i] = family[i];
            for (size_t i = 0; style.label[i] && i + 1 < std::size(enumeration.style); ++i)
                enumeration.style[i] = style.label[i];
            constexpr wchar_t script[] = L"Western";
            for (size_t i = 0; i < std::size(script); ++i) enumeration.script[i] = script[i];
            GdiNewTextMetricExW newMetrics{};
            newMetrics.textMetric = metrics;
            const int32_t callbackResult = callback(&enumeration.logFont,
                &newMetrics.textMetric, TrueTypeFontType, parameter);
            metricManager.shutdown();
            lastResult = callbackResult;
            if (!callbackResult) return 0;
        }
    }
    return lastResult;
}

} // namespace micant::um::winnt_user32_gdi32_imm32_abi

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
__declspec(dllexport) void* CreateSolidBrush(uint32_t colorRef) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateSolidBrush(colorRef);
}
__declspec(dllexport) int32_t FillRect(void* hdc, const void* rectangle, void* hbrush) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_FillRect(hdc, rectangle, hbrush);
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
__declspec(dllexport) int32_t GetObjectW(void* handle, int32_t bufferBytes, void* output) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetObjectW(
        handle, bufferBytes, output);
}
__declspec(dllexport) void* SelectObject(void* hdc, void* h) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SelectObject(hdc, h);
}
__declspec(dllexport) int32_t DeleteObject(void* ho) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DeleteObject(ho);
}
__declspec(dllexport) void MicaGdi32ForgetDcState(void* hdc) {
    micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ForgetDcState(hdc);
}
__declspec(dllexport) void* CreateFontIndirectW(const micant::um::winnt_user32_gdi32_imm32_abi::GdiLogFontW* logical) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateFontIndirectW(logical);
}
__declspec(dllexport) void* CreateFontW(int32_t height, int32_t width, int32_t escapement,
        int32_t orientation, int32_t weight, uint32_t italic, uint32_t underline,
        uint32_t strikeOut, uint32_t charSet, uint32_t outPrecision,
        uint32_t clipPrecision, uint32_t quality, uint32_t pitchAndFamily,
        const wchar_t* faceName) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateFontW(height, width,
        escapement, orientation, weight, italic, underline, strikeOut, charSet,
        outPrecision, clipPrecision, quality, pitchAndFamily, faceName);
}
__declspec(dllexport) int32_t TextOutW(void* hdc, int32_t x, int32_t y,
        const wchar_t* text, int32_t count) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_TextOutW(hdc, x, y, text, count);
}
__declspec(dllexport) int32_t ExtTextOutW(void* hdc, int32_t x, int32_t y,
        uint32_t options, const void* rect, const wchar_t* text, uint32_t count,
        const int32_t* advances) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ExtTextOutW(hdc, x, y,
        options, rect, text, count, advances);
}
__declspec(dllexport) uint32_t SetTextColor(void* hdc, uint32_t color) {
    return static_cast<uint32_t>(micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetTextColor(hdc, color));
}
__declspec(dllexport) int32_t SetBkMode(void* hdc, int32_t mode) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetBkMode(hdc, mode);
}
__declspec(dllexport) int32_t GetTextExtentPoint32W(void* hdc, const wchar_t* text,
        int32_t count, micant::um::winnt_user32_gdi32_imm32_abi::GdiSize* size) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetTextExtentPoint32W(
        hdc, text, count, size);
}
__declspec(dllexport) int32_t MicaGdiPushClipRect(void* hdc, const void* rectangle,
                                                  void** token) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_MicaPushClipRect(
        hdc, static_cast<const MicaWin32kRect*>(rectangle), token);
}
__declspec(dllexport) int32_t MicaGdiPopClipRect(void* hdc, void* token) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_MicaPopClipRect(hdc, token);
}
__declspec(dllexport) int32_t GetTextMetricsW(void* hdc,
        micant::um::winnt_user32_gdi32_imm32_abi::GdiTextMetricW* metrics) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetTextMetricsW(hdc, metrics);
}
__declspec(dllexport) uint32_t GetGlyphIndicesW(void* hdc, const wchar_t* text,
        int32_t count, uint16_t* glyphIndices, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetGlyphIndicesW(
        hdc, text, count, glyphIndices, flags);
}
__declspec(dllexport) int32_t EnumFontFamiliesExW(void* hdc,
        const micant::um::winnt_user32_gdi32_imm32_abi::GdiLogFontW* filter,
        micant::um::winnt_user32_gdi32_imm32_abi::GdiFontEnumProcW callback,
        intptr_t parameter, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_EnumFontFamiliesExW(
        hdc, filter, callback, parameter, flags);
}
} // extern "C"
