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
#include <cstdlib>


#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

namespace micant::um::winnt_user32_gdi32_imm32_abi {

int32_t Gdi32_GetDeviceCaps(void*, int32_t) noexcept;
void* Gdi32_GetStockObject(int32_t) noexcept;
int32_t Gdi32_DeleteObject(void*) noexcept;


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
    MICA_WIN32K_PROC(MicaWin32kGetBitmapSurface);
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
constexpr size_t GdiPenCapacity = 64;
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
    struct GdiBrushObject* brush{};
    struct GdiPenObject* pen{};
    uint32_t textColor{0x00000000u}; // COLORREF black
    uint32_t backgroundColor{0x00FFFFFFu};
    uint32_t dcBrushColor{0x00FFFFFFu};
    int32_t backgroundMode{1};       // TRANSPARENT
    int32_t currentX{}, currentY{};
    int32_t rop2{13};
    int32_t polyFillMode{1};
    int32_t stretchBltMode{1};
    MicaWin32kRect clip{};
    MicaWin32kRect excluded[16]{};
    uint8_t excludedCount{};
    bool clipEmpty{};
    struct ClipState { uint32_t generation{}; MicaWin32kRect previous{}; bool occupied{}; } clips[16]{};
    uint32_t nextClipGeneration{1};
};
struct GdiBrushObject {
    uint32_t colorRef{};
    uint32_t generation{};
    void* patternBitmap{};
    uint32_t hatch{};
    uint8_t style{};
    bool stock{};
    int32_t stockId{-1};
    bool active{};
};
struct GdiPenObject {
    uint32_t colorRef{};
    int32_t width{1};
    uint32_t style{};
    bool active{};
    bool stock{};
};
GdiFontObject g_gdiFonts[GdiFontCapacity]{};
GdiFontObject* g_systemFont{};
GdiBitmapSurface g_gdiBitmaps[GdiBitmapCapacity]{};
GdiDcSelection g_gdiDcs[GdiDcCapacity]{};
GdiBrushObject g_gdiBrushes[GdiBrushCapacity]{};
GdiPenObject g_gdiPens[GdiPenCapacity]{};
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
GdiPenObject* FindGdiPen(void* handle) noexcept {
    for (auto& pen : g_gdiPens) if (pen.active && handle == &pen) return &pen;
    return nullptr;
}
bool TrackGdiBitmap(void* handle, uint32_t* pixels, uint32_t width,
                    uint32_t height, bool topDown) noexcept {
    if (!handle || !pixels || !width || !height || width > INT32_MAX / 4u ||
        height > INT32_MAX) return false;
    for (auto& current : g_gdiBitmaps) {
        if (current.bitmap == handle) {
            current = {handle, pixels, width, height, topDown};
            return true;
        }
    }
    for (auto& current : g_gdiBitmaps) {
        if (current.bitmap) continue;
        current = {handle, pixels, width, height, topDown};
        return true;
    }
    return false;
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

struct GdiSurfaceView {
    uint32_t* pixels{};
    uint32_t width{}, height{}, stride{};
    bool window{};
};

bool GetGdiSurface(void* hdc, GdiSurfaceView& surface) noexcept {
    surface = {};
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return false;
    if (auto* bitmap = FindGdiBitmap(dc->bitmap)) {
        if (!bitmap->pixels || !bitmap->topDown) return false;
        surface = {bitmap->pixels, bitmap->width, bitmap->height, bitmap->width, false};
    } else {
        using SurfaceFn = int32_t (*)(void*, uint32_t**, uint32_t*, uint32_t*, uint32_t*);
        const auto getSurface = Win32kProc<SurfaceFn>("MicaWin32kGetDCSurface");
        if (!getSurface || !getSurface(hdc, &surface.pixels, &surface.width,
                &surface.height, &surface.stride) || !surface.pixels ||
            !surface.width || !surface.height || surface.stride < surface.width)
            return false;
        surface.window = true;
    }
    if (!dc->clipEmpty && (dc->clip.right <= dc->clip.left || dc->clip.bottom <= dc->clip.top))
        dc->clip = {0, 0, static_cast<int32_t>(surface.width),
                    static_cast<int32_t>(surface.height)};
    return true;
}

uint32_t GdiColorRefToPixel(uint32_t color) noexcept {
    return 0xFF000000u | ((color & 0x000000FFu) << 16) |
        (color & 0x0000FF00u) | ((color & 0x00FF0000u) >> 16);
}
uint32_t GdiPixelToColorRef(uint32_t pixel) noexcept {
    return ((pixel & 0x00FF0000u) >> 16) | (pixel & 0x0000FF00u) |
        ((pixel & 0x000000FFu) << 16);
}
bool WriteGdiPixel(void* hdc, int32_t x, int32_t y, uint32_t colorRef) noexcept {
    auto* dc = FindGdiDc(hdc, false);
    GdiSurfaceView surface{};
    if (!dc || !GetGdiSurface(hdc, surface) || x < 0 || y < 0 ||
        static_cast<uint32_t>(x) >= surface.width ||
        static_cast<uint32_t>(y) >= surface.height || dc->clipEmpty || x < dc->clip.left ||
        y < dc->clip.top || x >= dc->clip.right || y >= dc->clip.bottom)
        return false;
    for (uint8_t i = 0; i < dc->excludedCount; ++i) {
        const auto& excluded = dc->excluded[i];
        if (x >= excluded.left && x < excluded.right && y >= excluded.top && y < excluded.bottom)
            return false;
    }
    uint32_t& destination = surface.pixels[static_cast<size_t>(y) * surface.stride +
                                            static_cast<uint32_t>(x)];
    const uint32_t source = GdiColorRefToPixel(colorRef);
    switch (dc->rop2) {
    case 13: destination = source; break; // R2_COPYPEN
    case 7: destination = (destination & 0xFF000000u) |
        ((destination ^ source) & 0x00FFFFFFu); break;
    case 10: destination = (destination & 0xFF000000u) |
        (~(destination ^ source) & 0x00FFFFFFu); break;
    case 11: break; // R2_NOP
    case 1: destination = (destination & 0xFF000000u); break;
    case 16: destination = (destination & 0xFF000000u) | 0x00FFFFFFu; break;
    default: return false;
    }
    return true;
}
int32_t PresentGdiRect(void* hdc, const MicaWin32kRect& rect) noexcept {
    auto* dc = FindGdiDc(hdc, false);
    if (!dc) return 0;
    if (FindGdiBitmap(dc->bitmap)) return 1;
    using PresentFn = int32_t (*)(void*, const MicaWin32kRect*);
    const auto present = Win32kProc<PresentFn>("MicaWin32kPresentDCRect");
    return present ? present(hdc, &rect) : 0;
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
    void* bitmap = fn ? fn(hdc,width,height) : nullptr;
    if (!bitmap) return nullptr;
    using SurfaceFn=int32_t (*)(void*,uint32_t**,uint32_t*,uint32_t*);
    auto surfaceFn=Win32kProc<SurfaceFn>("MicaWin32kGetBitmapSurface");
    uint32_t* pixels{}; uint32_t surfaceWidth{},surfaceHeight{};
    if (!surfaceFn || !surfaceFn(bitmap,&pixels,&surfaceWidth,&surfaceHeight) ||
        surfaceWidth != static_cast<uint32_t>(width) ||
        surfaceHeight != static_cast<uint32_t>(height) ||
        !TrackGdiBitmap(bitmap,pixels,surfaceWidth,surfaceHeight,true)) {
        using DeleteFn=int32_t (*)(void*); auto deleteFn=Win32kProc<DeleteFn>("MicaWin32kDeleteObject");
        if (deleteFn) (void)deleteFn(bitmap);
        return nullptr;
    }
    return bitmap;
}

void* Gdi32_CreateBitmap(int32_t width, int32_t height, uint32_t planes,
                         uint32_t bitsPerPixel, const void* initialBits) noexcept {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || planes != 1 ||
        (bitsPerPixel != 1 && bitsPerPixel != 24 && bitsPerPixel != 32)) return nullptr;
    const uint64_t rowBits = uint64_t(static_cast<uint32_t>(width)) * bitsPerPixel;
    const uint64_t rowBytes = ((rowBits + 31u) / 32u) * 4u;
    if (rowBytes > SIZE_MAX || rowBytes * static_cast<uint32_t>(height) > SIZE_MAX) return nullptr;
    void* bitmapHandle = Gdi32_CreateCompatibleBitmap(nullptr, width, height);
    auto* bitmap = FindGdiBitmap(bitmapHandle);
    if (!bitmap) return nullptr;
    if (!initialBits) return bitmapHandle;
    const auto* input = static_cast<const uint8_t*>(initialBits);
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* row = input + static_cast<size_t>(height - 1 - y) *
            static_cast<size_t>(rowBytes);
        for (int32_t x = 0; x < width; ++x) {
            uint32_t pixel{};
            if (bitsPerPixel == 1) {
                const bool white = (row[static_cast<uint32_t>(x) >> 3] &
                    (0x80u >> (static_cast<uint32_t>(x) & 7u))) != 0;
                pixel = white ? 0xFFFFFFFFu : 0xFF000000u;
            } else if (bitsPerPixel == 24) {
                const uint8_t* source = row + static_cast<size_t>(x) * 3u;
                pixel = 0xFF000000u | (uint32_t(source[2]) << 16) |
                    (uint32_t(source[1]) << 8) | source[0];
            } else {
                pixel = reinterpret_cast<const uint32_t*>(row)[x] | 0xFF000000u;
            }
            bitmap->pixels[static_cast<size_t>(y) * bitmap->width +
                           static_cast<uint32_t>(x)] = pixel;
        }
    }
    return bitmapHandle;
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
        brush.patternBitmap = nullptr;
        brush.style = 0;
        brush.stock = false;
        brush.stockId = -1;
        brush.generation = generation;
        brush.active = true;
        return GdiBrushHandle(i, generation);
    }
    return nullptr;
}

void* Gdi32_CreateBrushIndirect(const void* logicalBrush) noexcept {
    if (!logicalBrush) return nullptr;
    struct LogicalBrush { uint32_t style, color; uintptr_t hatch; };
    const auto& source = *static_cast<const LogicalBrush*>(logicalBrush);
    if (source.style > 2 || (source.style == 2 && source.hatch > 5)) return nullptr;
    void* handle = Gdi32_CreateSolidBrush(source.color);
    if (!handle) return nullptr;
    GdiBrushGuard guard;
    auto* brush = FindGdiBrush(handle);
    if (!brush) return nullptr;
    brush->style = static_cast<uint8_t>(source.style);
    brush->hatch = static_cast<uint32_t>(source.hatch);
    return handle;
}

void* Gdi32_CreatePen(int32_t style, int32_t width, uint32_t color) noexcept {
    if (style < 0 || style > 5 || width < 0 || width > 128) return nullptr;
    for (auto& pen : g_gdiPens) {
        if (pen.active) continue;
        pen = {color & 0x00FFFFFFu, width ? width : 1,
               static_cast<uint32_t>(style), true, false};
        return &pen;
    }
    return nullptr;
}

void* Gdi32_CreatePatternBrush(void* bitmapHandle) noexcept {
    auto* bitmap = FindGdiBitmap(bitmapHandle);
    if (!bitmap || !bitmap->pixels || !bitmap->width || !bitmap->height) return nullptr;
    void* brushHandle = Gdi32_CreateSolidBrush(0);
    if (!brushHandle) return nullptr;
    GdiBrushGuard guard;
    auto* brush = FindGdiBrush(brushHandle);
    if (!brush) return nullptr;
    brush->style = 3;
    brush->patternBitmap = bitmapHandle;
    return brushHandle;
}

namespace {
bool GdiPenDraws(const GdiPenObject& pen, uint32_t step) noexcept {
    switch (pen.style) {
    case 0: return true; // PS_SOLID
    case 1: return step % 8u < 5u;
    case 2: return step % 4u == 0u;
    case 3: return step % 12u < 6u || step % 12u == 9u;
    case 4: return step % 16u < 6u || step % 16u == 9u || step % 16u == 12u;
    case 5: return false; // PS_NULL
    default: return false;
    }
}
bool DrawGdiLine(void* hdc, int32_t x1, int32_t y1, int32_t x2,
                 int32_t y2) noexcept {
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return false;
    GdiPenObject* pen = dc->pen;
    if (!pen) pen = static_cast<GdiPenObject*>(Gdi32_GetStockObject(7));
    if (!pen) return false;
    const int64_t dx = std::llabs(static_cast<int64_t>(x2) - x1);
    const int64_t dy = std::llabs(static_cast<int64_t>(y2) - y1);
    const int64_t steps = std::max(dx, dy);
    if (steps > 1000000) return false;
    if (dc->rop2 != 1 && dc->rop2 != 7 && dc->rop2 != 10 && dc->rop2 != 11 &&
        dc->rop2 != 13 && dc->rop2 != 16) return false;
    const int64_t half = std::max(1, pen->width) / 2;
    const int32_t left = std::max<int64_t>(INT32_MIN,
        int64_t(std::min(x1, x2)) - half);
    const int32_t top = std::max<int64_t>(INT32_MIN,
        int64_t(std::min(y1, y2)) - half);
    const int32_t right = std::min<int64_t>(INT32_MAX,
        int64_t(std::max(x1, x2)) + half + 1);
    const int32_t bottom = std::min<int64_t>(INT32_MAX,
        int64_t(std::max(y1, y2)) + half + 1);
    for (int64_t index = 0; index < std::max<int64_t>(1, steps); ++index) {
        if (!GdiPenDraws(*pen, static_cast<uint32_t>(index))) continue;
        const int32_t x = steps ? static_cast<int32_t>(int64_t(x1) +
            (int64_t(x2) - x1) * index / steps) : x1;
        const int32_t y = steps ? static_cast<int32_t>(int64_t(y1) +
            (int64_t(y2) - y1) * index / steps) : y1;
        for (int32_t oy = -static_cast<int32_t>(half); oy <= static_cast<int32_t>(half); ++oy)
            for (int32_t ox = -static_cast<int32_t>(half); ox <= static_cast<int32_t>(half); ++ox)
                (void)WriteGdiPixel(hdc, x + ox, y + oy, pen->colorRef);
    }
    GdiSurfaceView surface{};
    if (!GetGdiSurface(hdc, surface)) return false;
    const int32_t clippedLeft = std::max({left, dc->clip.left, 0});
    const int32_t clippedTop = std::max({top, dc->clip.top, 0});
    const int32_t clippedRight = std::min({right, dc->clip.right,
        static_cast<int32_t>(surface.width)});
    const int32_t clippedBottom = std::min({bottom, dc->clip.bottom,
        static_cast<int32_t>(surface.height)});
    return clippedRight <= clippedLeft || clippedBottom <= clippedTop ||
        PresentGdiRect(hdc, {clippedLeft, clippedTop, clippedRight, clippedBottom}) != 0;
}
void* SelectedGdiBrush(GdiDcSelection& dc) noexcept {
    if (dc.brush) return GdiBrushHandle(
        static_cast<size_t>(dc.brush - g_gdiBrushes), dc.brush->generation);
    return Gdi32_GetStockObject(0);
}
}

int32_t Gdi32_MoveToEx(void* hdc, int32_t x, int32_t y, void* previousPoint) noexcept {
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0;
    if (previousPoint) {
        struct Point { int32_t x, y; } previous{dc->currentX, dc->currentY};
        std::memcpy(previousPoint, &previous, sizeof(previous));
    }
    dc->currentX = x;
    dc->currentY = y;
    return 1;
}
int32_t Gdi32_LineTo(void* hdc, int32_t x, int32_t y) noexcept {
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0;
    if (!DrawGdiLine(hdc, dc->currentX, dc->currentY, x, y)) return 0;
    dc->currentX = x;
    dc->currentY = y;
    return 1;
}
uint32_t Gdi32_GetPixel(void* hdc, int32_t x, int32_t y) noexcept {
    GdiSurfaceView surface{};
    if (!GetGdiSurface(hdc, surface) || x < 0 || y < 0 ||
        static_cast<uint32_t>(x) >= surface.width || static_cast<uint32_t>(y) >= surface.height)
        return 0xFFFFFFFFu;
    return GdiPixelToColorRef(surface.pixels[static_cast<size_t>(y) * surface.stride + x]);
}
int32_t Gdi32_SetPixelV(void* hdc, int32_t x, int32_t y, uint32_t color) noexcept {
    if (!WriteGdiPixel(hdc, x, y, color)) return 0;
    return PresentGdiRect(hdc, {x, y, x + 1, y + 1});
}
uint32_t Gdi32_SetBkColor(void* hdc, uint32_t color) noexcept {
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0xFFFFFFFFu;
    const uint32_t previous = dc->backgroundColor;
    dc->backgroundColor = color & 0x00FFFFFFu;
    return previous;
}
uint32_t Gdi32_SetDCBrushColor(void* hdc, uint32_t color) noexcept {
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0xFFFFFFFFu;
    const uint32_t previous = dc->dcBrushColor;
    dc->dcBrushColor = color & 0x00FFFFFFu;
    return previous;
}
int32_t Gdi32_GetROP2(void* hdc) noexcept {
    auto* dc = FindGdiDc(hdc, false);
    return dc ? dc->rop2 : 0;
}
int32_t Gdi32_SetROP2(void* hdc, int32_t mode) noexcept {
    if (mode < 1 || mode > 16) return 0;
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0;
    const int32_t previous = dc->rop2;
    dc->rop2 = mode;
    return previous;
}
int32_t Gdi32_SetPolyFillMode(void* hdc, int32_t mode) noexcept {
    if (mode != 1 && mode != 2) return 0;
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0;
    const int32_t previous = dc->polyFillMode;
    dc->polyFillMode = mode;
    return previous;
}
int32_t Gdi32_SetStretchBltMode(void* hdc, int32_t mode) noexcept {
    if (mode < 1 || mode > 4) return 0;
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0;
    const int32_t previous = dc->stretchBltMode;
    dc->stretchBltMode = mode;
    return previous;
}

int32_t Gdi32_FillRect(void* hdc, const void* rectangle, void* hbrush) noexcept {
    if (!hdc || !rectangle) return 0;
    GdiBrushGuard guard;
    auto* brush = FindGdiBrush(hbrush);
    if (!brush) return 0;
    const auto& requested = *static_cast<const MicaWin32kRect*>(rectangle);
    if (requested.right <= requested.left || requested.bottom <= requested.top) return 1;
    if (brush->style == 1) return 1; // BS_NULL / HOLLOW_BRUSH
    GdiSurfaceView surface{};
    auto* dc = FindGdiDc(hdc, true);
    if (!dc || !GetGdiSurface(hdc, surface)) return 0;
    const int32_t left = std::max({requested.left, dc->clip.left, 0});
    const int32_t top = std::max({requested.top, dc->clip.top, 0});
    const int32_t right = std::min({requested.right, dc->clip.right,
        static_cast<int32_t>(surface.width)});
    const int32_t bottom = std::min({requested.bottom, dc->clip.bottom,
        static_cast<int32_t>(surface.height)});
    if (dc->clipEmpty || right <= left || bottom <= top) return 1;
    auto* pattern = brush->style == 3 ? FindGdiBitmap(brush->patternBitmap) : nullptr;
    if (brush->style == 3 && (!pattern || !pattern->pixels || !pattern->width || !pattern->height))
        return 0;
    for (int32_t y = top; y < bottom; ++y) {
        for (int32_t x = left; x < right; ++x) {
            bool excludedPixel = false;
            for (uint8_t i = 0; i < dc->excludedCount; ++i) {
                const auto& excluded = dc->excluded[i];
                if (x >= excluded.left && x < excluded.right &&
                    y >= excluded.top && y < excluded.bottom) { excludedPixel = true; break; }
            }
            if (excludedPixel) continue;
            bool foreground = true;
            uint32_t pixel = GdiColorRefToPixel(brush->colorRef);
            if (brush->style == 2) {
                const uint32_t hx = static_cast<uint32_t>(x) & 7u;
                const uint32_t hy = static_cast<uint32_t>(y) & 7u;
                switch (brush->hatch) {
                case 0: foreground = hy == 0; break;
                case 1: foreground = hx == 0; break;
                case 2: foreground = ((hx + hy) & 7u) == 0; break;
                case 3: foreground = ((hx + 8u - hy) & 7u) == 0; break;
                case 4: foreground = hx == 0 || hy == 0; break;
                case 5: foreground = ((hx + hy) & 7u) == 0 ||
                                     ((hx + 8u - hy) & 7u) == 0; break;
                default: return 0;
                }
                if (!foreground && dc->backgroundMode == 1) continue;
                if (!foreground) pixel = GdiColorRefToPixel(dc->backgroundColor);
            } else if (pattern) {
                const uint32_t px = static_cast<uint32_t>(x) % pattern->width;
                const uint32_t py = static_cast<uint32_t>(y) % pattern->height;
                pixel = pattern->pixels[static_cast<size_t>(py) * pattern->width + px];
            }
            surface.pixels[static_cast<size_t>(y) * surface.stride +
                           static_cast<uint32_t>(x)] = pixel;
        }
    }
    return surface.window ? PresentGdiRect(hdc, {left, top, right, bottom}) : 1;
}

namespace {
bool FillBrushPixel(GdiDcSelection& dc, GdiBrushObject* brush,
                    GdiSurfaceView& surface, int32_t x, int32_t y) noexcept {
    if (!brush || brush->style == 1) return true;
    uint32_t pixel = GdiColorRefToPixel(brush->colorRef);
    if (brush->style == 2) {
        const uint32_t hx = static_cast<uint32_t>(x) & 7u;
        const uint32_t hy = static_cast<uint32_t>(y) & 7u;
        bool foreground{};
        switch (brush->hatch) {
        case 0: foreground = hy == 0; break;
        case 1: foreground = hx == 0; break;
        case 2: foreground = ((hx + hy) & 7u) == 0; break;
        case 3: foreground = ((hx + 8u - hy) & 7u) == 0; break;
        case 4: foreground = hx == 0 || hy == 0; break;
        case 5: foreground = ((hx + hy) & 7u) == 0 || ((hx + 8u - hy) & 7u) == 0; break;
        default: return false;
        }
        if (!foreground && dc.backgroundMode == 1) return true;
        if (!foreground) pixel = GdiColorRefToPixel(dc.backgroundColor);
    } else if (brush->style == 3) {
        auto* pattern = FindGdiBitmap(brush->patternBitmap);
        if (!pattern || !pattern->pixels || !pattern->width || !pattern->height) return false;
        const uint32_t px = static_cast<uint32_t>(x) % pattern->width;
        const uint32_t py = static_cast<uint32_t>(y) % pattern->height;
        pixel = pattern->pixels[static_cast<size_t>(py) * pattern->width + px];
    }
    surface.pixels[static_cast<size_t>(y) * surface.stride + static_cast<uint32_t>(x)] = pixel;
    return true;
}
bool InsideEllipse(int64_t px, int64_t py, int64_t left, int64_t top,
                   int64_t right, int64_t bottom) noexcept {
    if (right <= left || bottom <= top) return false;
    const int64_t rx = right - left, ry = bottom - top;
    const int64_t dx = 2 * px + 1 - left - right;
    const int64_t dy = 2 * py + 1 - top - bottom;
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}
bool InsideRoundedRect(int64_t px, int64_t py, int64_t left, int64_t top,
                       int64_t right, int64_t bottom, int64_t radiusX,
                       int64_t radiusY) noexcept {
    if (px < left || px >= right || py < top || py >= bottom) return false;
    if (radiusX <= 0 || radiusY <= 0) return true;
    const int64_t centerLeft = left + radiusX;
    const int64_t centerRight = right - radiusX;
    const int64_t centerTop = top + radiusY;
    const int64_t centerBottom = bottom - radiusY;
    if (px >= centerLeft && px < centerRight) return true;
    if (py >= centerTop && py < centerBottom) return true;
    const int64_t cx = px < centerLeft ? centerLeft : centerRight;
    const int64_t cy = py < centerTop ? centerTop : centerBottom;
    const int64_t dx = (2 * px + 1) - 2 * cx;
    const int64_t dy = (2 * py + 1) - 2 * cy;
    const int64_t rx = 2 * radiusX, ry = 2 * radiusY;
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}
int32_t DrawGdiShape(void* hdc, int32_t left, int32_t top, int32_t right,
    int32_t bottom, int32_t radiusX, int32_t radiusY) noexcept {
    if (!hdc) return 0;
    if (right <= left || bottom <= top) return 1;
    const int64_t width = int64_t(right) - left;
    const int64_t height = int64_t(bottom) - top;
    if (width > 4096 || height > 4096) return 0;
    auto* dc = FindGdiDc(hdc, true);
    GdiSurfaceView surface{};
    if (!dc || !GetGdiSurface(hdc, surface)) return 0;
    GdiPenObject* pen = dc->pen ? dc->pen : static_cast<GdiPenObject*>(Gdi32_GetStockObject(7));
    GdiBrushObject* brush = dc->brush ? dc->brush : FindGdiBrush(Gdi32_GetStockObject(0));
    if (!pen || (dc->rop2 != 1 && dc->rop2 != 7 && dc->rop2 != 10 &&
        dc->rop2 != 11 && dc->rop2 != 13 && dc->rop2 != 16)) return 0;
    const int32_t thickness = pen->width > 0 ? pen->width : 1;
    const int64_t innerLeft = int64_t(left) + thickness;
    const int64_t innerTop = int64_t(top) + thickness;
    const int64_t innerRight = int64_t(right) - thickness;
    const int64_t innerBottom = int64_t(bottom) - thickness;
    const int64_t innerRadiusX = std::max<int64_t>(0, int64_t(radiusX) - thickness);
    const int64_t innerRadiusY = std::max<int64_t>(0, int64_t(radiusY) - thickness);
    const int32_t drawLeft = std::max({left, dc->clip.left, 0});
    const int32_t drawTop = std::max({top, dc->clip.top, 0});
    const int32_t drawRight = std::min({right, dc->clip.right, static_cast<int32_t>(surface.width)});
    const int32_t drawBottom = std::min({bottom, dc->clip.bottom, static_cast<int32_t>(surface.height)});
    for (int32_t y = drawTop; y < drawBottom; ++y) {
        for (int32_t x = drawLeft; x < drawRight; ++x) {
            const bool outer = InsideRoundedRect(x, y, left, top, right, bottom,
                                                  radiusX, radiusY);
            if (!outer) continue;
            const bool interior = InsideRoundedRect(x, y, innerLeft, innerTop,
                innerRight, innerBottom, innerRadiusX, innerRadiusY);
            if (interior && brush && brush->style != 1) {
                if (!FillBrushPixel(*dc, brush, surface, x, y)) return 0;
            } else if (!interior && GdiPenDraws(*pen, static_cast<uint32_t>(x + y))) {
                (void)WriteGdiPixel(hdc, x, y, pen->colorRef);
            }
        }
    }
    if (drawRight <= drawLeft || drawBottom <= drawTop) return 1;
    return PresentGdiRect(hdc, {drawLeft, drawTop, drawRight, drawBottom});
}
}

int32_t Gdi32_Rectangle(void* hdc, int32_t left, int32_t top,
                        int32_t right, int32_t bottom) noexcept {
    return DrawGdiShape(hdc, left, top, right, bottom, 0, 0);
}
int32_t Gdi32_RoundRect(void* hdc, int32_t left, int32_t top,
                        int32_t right, int32_t bottom,
                        int32_t ellipseWidth, int32_t ellipseHeight) noexcept {
    if (ellipseWidth < 0 || ellipseHeight < 0) return 0;
    const int32_t radiusX = std::min(ellipseWidth / 2, std::max(0, (right - left) / 2));
    const int32_t radiusY = std::min(ellipseHeight / 2, std::max(0, (bottom - top) / 2));
    return DrawGdiShape(hdc, left, top, right, bottom, radiusX, radiusY);
}
int32_t Gdi32_Ellipse(void* hdc, int32_t left, int32_t top,
                      int32_t right, int32_t bottom) noexcept {
    if (right <= left || bottom <= top) return 1;
    const int64_t width = int64_t(right) - left, height = int64_t(bottom) - top;
    if (width > 4096 || height > 4096) return 0;
    auto* dc = FindGdiDc(hdc, true);
    GdiSurfaceView surface{};
    if (!dc || !GetGdiSurface(hdc, surface)) return 0;
    GdiPenObject* pen = dc->pen ? dc->pen : static_cast<GdiPenObject*>(Gdi32_GetStockObject(7));
    GdiBrushObject* brush = dc->brush ? dc->brush : FindGdiBrush(Gdi32_GetStockObject(0));
    if (!pen) return 0;
    const int32_t thickness = std::max(1, pen->width);
    const int32_t drawLeft = std::max({left, dc->clip.left, 0});
    const int32_t drawTop = std::max({top, dc->clip.top, 0});
    const int32_t drawRight = std::min({right, dc->clip.right, static_cast<int32_t>(surface.width)});
    const int32_t drawBottom = std::min({bottom, dc->clip.bottom, static_cast<int32_t>(surface.height)});
    for (int32_t y = drawTop; y < drawBottom; ++y) {
        for (int32_t x = drawLeft; x < drawRight; ++x) {
            if (!InsideEllipse(x, y, left, top, right, bottom)) continue;
            const bool interior = InsideEllipse(x, y, int64_t(left) + thickness,
                int64_t(top) + thickness, int64_t(right) - thickness,
                int64_t(bottom) - thickness);
            if (interior && brush && brush->style != 1) {
                if (!FillBrushPixel(*dc, brush, surface, x, y)) return 0;
            } else if (!interior && GdiPenDraws(*pen, static_cast<uint32_t>(x + y))) {
                (void)WriteGdiPixel(hdc, x, y, pen->colorRef);
            }
        }
    }
    if (drawRight <= drawLeft || drawBottom <= drawTop) return 1;
    return PresentGdiRect(hdc, {drawLeft, drawTop, drawRight, drawBottom});
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
    if (!fn || cx <= 0 || cy <= 0 ||
        !fn(hdc,x,y,cx,cy,hdcSrc,x1,y1,rop)) return 0;

    // The window-manager blit copies into the selected surface but does not
    // publish damage. Submit the bounded destination area here so an ordinary
    // GetDC/ReleaseDC caller does not turn a small blit into a full-window
    // invalidation when ReleaseDC closes the update scope.
    GdiSurfaceView surface{};
    if (!GetGdiSurface(hdc, surface)) return 0;
    const int64_t requestedRight = static_cast<int64_t>(x) + cx;
    const int64_t requestedBottom = static_cast<int64_t>(y) + cy;
    const int32_t surfaceRight = static_cast<int32_t>(
        std::min<uint32_t>(surface.width, static_cast<uint32_t>(INT32_MAX)));
    const int32_t surfaceBottom = static_cast<int32_t>(
        std::min<uint32_t>(surface.height, static_cast<uint32_t>(INT32_MAX)));
    const MicaWin32kRect dirty{
        static_cast<int32_t>(std::clamp<int64_t>(x, 0, surfaceRight)),
        static_cast<int32_t>(std::clamp<int64_t>(y, 0, surfaceBottom)),
        static_cast<int32_t>(std::clamp<int64_t>(requestedRight, 0, surfaceRight)),
        static_cast<int32_t>(std::clamp<int64_t>(requestedBottom, 0, surfaceBottom))};
    return PresentGdiRect(hdc, dirty);
}

void* Gdi32_GetStockObject(int32_t i) noexcept {
    if (i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 16 || i == 17)
        return GetSystemGdiFont();
    constexpr uint32_t stockColors[] = {
        0x00FFFFFFu, 0x00C0C0C0u, 0x00808080u,
        0x00404040u, 0x00000000u, 0x00FFFFFFu};
    if (i >= 0 && i < static_cast<int32_t>(std::size(stockColors))) {
        GdiBrushGuard guard;
        for (size_t index = 0; index < GdiBrushCapacity; ++index) {
            auto& brush = g_gdiBrushes[index];
            if (brush.active && brush.stock && brush.stockId == i)
                return GdiBrushHandle(index, brush.generation);
        }
        size_t slot = GdiBrushCapacity;
        for (size_t index = 0; index < GdiBrushCapacity; ++index)
            if (!g_gdiBrushes[index].active) { slot = index; break; }
        if (slot == GdiBrushCapacity) return nullptr;
        auto& brush = g_gdiBrushes[slot];
        const uint32_t generation = static_cast<uint32_t>(slot + 1);
        brush = {};
        brush.colorRef = stockColors[i];
        brush.generation = generation;
        brush.style = i == 5 ? 1 : 0; // HOLLOW_BRUSH has no fill.
        brush.stock = true;
        brush.stockId = i;
        brush.active = true;
        return GdiBrushHandle(slot, generation);
    }
    if (i == 7 || i == 6) {
        for (auto& pen : g_gdiPens) {
            if (pen.active && pen.stock && pen.style == static_cast<uint32_t>(i))
                return &pen;
        }
        for (auto& pen : g_gdiPens) {
            if (pen.active) continue;
            pen = {i == 7 ? 0x00000000u : 0x00FFFFFFu, 1, static_cast<uint32_t>(i), true, true};
            return &pen;
        }
    }
    using Fn=void* (*)(int32_t); auto fn=Win32kProc<Fn>("MicaWin32kGetStockObject");
    return fn ? fn(i) : nullptr;
}

int32_t Gdi32_GetObjectW(void* handle, int32_t bufferBytes, void* output) noexcept {
    GdiFontObject* font = FindGdiFont(handle);
    if (bufferBytes < 0) return 0;
    if (!font) {
        auto* bitmap = FindGdiBitmap(handle);
        if (!bitmap) return 0;
        struct BitmapObject {
            int32_t type, width, height, widthBytes;
            uint16_t planes, bitsPixel;
            void* bits;
        } object{0, static_cast<int32_t>(bitmap->width),
            static_cast<int32_t>(bitmap->height),
            static_cast<int32_t>(bitmap->width * sizeof(uint32_t)), 1, 32,
            bitmap->pixels};
        if (!output) return sizeof(object);
        if (bufferBytes < static_cast<int32_t>(sizeof(object))) return 0;
        std::memcpy(output, &object, sizeof(object));
        return sizeof(object);
    }

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

int32_t Gdi32_GetDIBits(void* hdc, void* handle, uint32_t startScan,
                       uint32_t scanLines, void* outputBits, void* bitmapInfo,
                       uint32_t usage) noexcept {
    struct BitmapInfoHeader {
        uint32_t size;
        int32_t width, height;
        uint16_t planes, bitCount;
        uint32_t compression, sizeImage;
        int32_t xPelsPerMeter, yPelsPerMeter;
        uint32_t colorsUsed, colorsImportant;
    };
    static_assert(sizeof(BitmapInfoHeader) == 40);
    if (!handle || !bitmapInfo || usage != 0) return 0;
    auto* bitmap = FindGdiBitmap(handle);
    if (!bitmap) return 0;
    for (const auto& dc : g_gdiDcs)
        if (dc.dc && dc.bitmap == handle) return 0;
    auto* info = static_cast<BitmapInfoHeader*>(bitmapInfo);
    if (info->size < sizeof(BitmapInfoHeader)) return 0;
    if (!outputBits) {
        info->width = static_cast<int32_t>(bitmap->width);
        info->height = static_cast<int32_t>(bitmap->height);
        info->planes = 1;
        if (!info->bitCount) info->bitCount = 32;
        info->compression = 0;
        const uint64_t stride = ((uint64_t(bitmap->width) * info->bitCount + 31u) / 32u) * 4u;
        const uint64_t imageBytes = stride * bitmap->height;
        if (imageBytes > UINT32_MAX) return 0;
        info->sizeImage = static_cast<uint32_t>(imageBytes);
        return 0;
    }
    if (!hdc || !scanLines || startScan >= bitmap->height ||
        scanLines > bitmap->height - startScan || info->width != static_cast<int32_t>(bitmap->width) ||
        (info->height != static_cast<int32_t>(bitmap->height) &&
         info->height != -static_cast<int32_t>(bitmap->height)) ||
        info->planes != 1 || (info->bitCount != 1 && info->bitCount != 24 &&
         info->bitCount != 32) || info->compression != 0) return 0;
    const bool topDown = info->height < 0;
    const uint64_t rowBytes64 = ((uint64_t(bitmap->width) * info->bitCount + 31u) / 32u) * 4u;
    if (rowBytes64 > SIZE_MAX || rowBytes64 > UINT32_MAX) return 0;
    const size_t rowBytes = static_cast<size_t>(rowBytes64);
    auto* destination = static_cast<uint8_t*>(outputBits);
    for (uint32_t row = 0; row < scanLines; ++row) {
        const uint32_t logicalY = startScan + row;
        const uint32_t sourceY = topDown ? logicalY : bitmap->height - 1u - logicalY;
        uint8_t* target = destination + static_cast<size_t>(row) * rowBytes;
        const uint32_t* source = bitmap->pixels + static_cast<size_t>(sourceY) * bitmap->width;
        if (info->bitCount == 32) {
            std::memcpy(target, source, static_cast<size_t>(bitmap->width) * 4u);
        } else if (info->bitCount == 24) {
            for (uint32_t x = 0; x < bitmap->width; ++x) {
                target[x * 3u] = static_cast<uint8_t>(source[x]);
                target[x * 3u + 1u] = static_cast<uint8_t>(source[x] >> 8);
                target[x * 3u + 2u] = static_cast<uint8_t>(source[x] >> 16);
            }
        } else {
            std::memset(target, 0, rowBytes);
            for (uint32_t x = 0; x < bitmap->width; ++x) {
                const uint32_t pixel = source[x];
                const uint32_t luminance = ((pixel & 0xffu) * 29u +
                    ((pixel >> 8) & 0xffu) * 150u +
                    ((pixel >> 16) & 0xffu) * 77u) >> 8;
                if (luminance >= 128u) target[x >> 3] |= static_cast<uint8_t>(0x80u >> (x & 7u));
            }
        }
    }
    return static_cast<int32_t>(scanLines);
}

int32_t Gdi32_SetDIBits(void* hdc, void* handle, uint32_t startScan,
                       uint32_t scanLines, const void* inputBits,
                       const void* bitmapInfo, uint32_t usage) noexcept {
    struct BitmapInfoHeader {
        uint32_t size;
        int32_t width, height;
        uint16_t planes, bitCount;
        uint32_t compression, sizeImage;
        int32_t xPelsPerMeter, yPelsPerMeter;
        uint32_t colorsUsed, colorsImportant;
    };
    static_assert(sizeof(BitmapInfoHeader) == 40);
    if (!hdc || !handle || !inputBits || !bitmapInfo || usage != 0) return 0;
    auto* bitmap = FindGdiBitmap(handle);
    if (!bitmap) return 0;
    for (const auto& dc : g_gdiDcs) if (dc.dc && dc.bitmap == handle) return 0;
    const auto* info = static_cast<const BitmapInfoHeader*>(bitmapInfo);
    if (info->size < sizeof(BitmapInfoHeader) || info->width != static_cast<int32_t>(bitmap->width) ||
        (info->height != static_cast<int32_t>(bitmap->height) &&
         info->height != -static_cast<int32_t>(bitmap->height)) || info->planes != 1 ||
        info->compression != 0 || (info->bitCount != 1 && info->bitCount != 24 &&
         info->bitCount != 32) || !scanLines || startScan >= bitmap->height ||
        scanLines > bitmap->height - startScan) return 0;
    const uint64_t rowBytes64 = ((uint64_t(bitmap->width) * info->bitCount + 31u) / 32u) * 4u;
    if (rowBytes64 > SIZE_MAX) return 0;
    const size_t rowBytes = static_cast<size_t>(rowBytes64);
    const auto* colors = reinterpret_cast<const uint8_t*>(bitmapInfo) + sizeof(BitmapInfoHeader);
    const auto* input = static_cast<const uint8_t*>(inputBits);
    const bool topDown = info->height < 0;
    for (uint32_t row = 0; row < scanLines; ++row) {
        const uint32_t logicalY = startScan + row;
        const uint32_t targetY = topDown ? logicalY : bitmap->height - 1u - logicalY;
        const auto* source = input + static_cast<size_t>(row) * rowBytes;
        uint32_t* target = bitmap->pixels + static_cast<size_t>(targetY) * bitmap->width;
        for (uint32_t x = 0; x < bitmap->width; ++x) {
            if (info->bitCount == 32) {
                target[x] = reinterpret_cast<const uint32_t*>(source)[x] | 0xFF000000u;
            } else if (info->bitCount == 24) {
                const uint8_t* pixel = source + static_cast<size_t>(x) * 3u;
                target[x] = 0xFF000000u | (uint32_t(pixel[2]) << 16) |
                    (uint32_t(pixel[1]) << 8) | pixel[0];
            } else {
                const bool set = (source[x >> 3] & (0x80u >> (x & 7u))) != 0;
                const uint8_t* rgb = colors + (set ? 4u : 0u);
                if (!info->colorsUsed) {
                    target[x] = set ? 0xFFFFFFFFu : 0xFF000000u;
                } else {
                    target[x] = 0xFF000000u | (uint32_t(rgb[2]) << 16) |
                        (uint32_t(rgb[1]) << 8) | rgb[0];
                }
            }
        }
    }
    return static_cast<int32_t>(scanLines);
}

void* Gdi32_CreateDIBitmap(void* hdc, const void* headerPointer, uint32_t init,
    const void* bits, const void* bitmapInfo, uint32_t usage) noexcept {
    struct Header { uint32_t size; int32_t width, height; uint16_t planes, bitCount;
        uint32_t compression, sizeImage; int32_t xPelsPerMeter, yPelsPerMeter;
        uint32_t colorsUsed, colorsImportant; };
    if (!hdc || !headerPointer || usage != 0 || (init & ~0x00000004u)) return nullptr;
    const auto* header = static_cast<const Header*>(headerPointer);
    if (header->size < sizeof(Header) || header->width <= 0 || header->height == 0 ||
        header->height == INT32_MIN || header->width > 4096 ||
        (header->bitCount != 1 && header->bitCount != 24 && header->bitCount != 32) ||
        header->compression != 0 || (header->planes != 1)) return nullptr;
    const uint32_t height = static_cast<uint32_t>(header->height < 0
        ? -header->height : header->height);
    void* bitmap = Gdi32_CreateCompatibleBitmap(hdc, header->width,
                                                 static_cast<int32_t>(height));
    if (!bitmap) return nullptr;
    if (init & 0x00000004u) {
        if (!bits || !bitmapInfo || Gdi32_SetDIBits(hdc, bitmap, 0, height,
                bits, bitmapInfo, usage) != static_cast<int32_t>(height)) {
            (void)Gdi32_DeleteObject(bitmap);
            return nullptr;
        }
    }
    return bitmap;
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
    if (GdiBrushObject* brush = FindGdiBrush(h)) {
        GdiDcSelection* dc = FindGdiDc(hdc, true);
        if (!dc) return nullptr;
        void* previous = dc->brush ? GdiBrushHandle(
            static_cast<size_t>(dc->brush - g_gdiBrushes), dc->brush->generation)
            : Gdi32_GetStockObject(0);
        if (!previous) return nullptr;
        dc->brush = brush;
        return previous;
    }
    if (GdiPenObject* pen = FindGdiPen(h)) {
        GdiDcSelection* dc = FindGdiDc(hdc, true);
        if (!dc) return nullptr;
        void* previous = dc->pen ? static_cast<void*>(dc->pen) : Gdi32_GetStockObject(7);
        if (!previous) return nullptr;
        dc->pen = pen;
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
        if (brush->stock) return 0;
        for (const auto& dc : g_gdiDcs) if (dc.dc && dc.brush == brush) return 0;
        brush->active = false;
        brush->colorRef = 0;
        return 1;
    }
    }
    if (GdiPenObject* pen = FindGdiPen(ho)) {
        if (pen->stock) return 0;
        for (const auto& dc : g_gdiDcs) if (dc.dc && dc.pen == pen) return 0;
        *pen = {};
        return 1;
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
    if (dc->clipEmpty) return 1;
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

int32_t Gdi32_ExcludeClipRect(void* hdc, int32_t left, int32_t top,
                              int32_t right, int32_t bottom) noexcept {
    if (!hdc || right < left || bottom < top) return 0;
    auto* dc = FindGdiDc(hdc, true);
    if (!dc) return 0;
    if (dc->clipEmpty) return 1;
    const MicaWin32kRect clipped{
        std::max(left, dc->clip.left), std::max(top, dc->clip.top),
        std::min(right, dc->clip.right), std::min(bottom, dc->clip.bottom)};
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top)
        return dc->excludedCount ? 3 : 2;
    if (clipped.left <= dc->clip.left && clipped.top <= dc->clip.top &&
        clipped.right >= dc->clip.right && clipped.bottom >= dc->clip.bottom) {
        dc->clipEmpty = true;
        return 1;
    }
    for (uint8_t i = 0; i < dc->excludedCount; ++i) {
        const auto& existing = dc->excluded[i];
        if (existing.left == clipped.left && existing.top == clipped.top &&
            existing.right == clipped.right && existing.bottom == clipped.bottom)
            return 3;
    }
    if (dc->excludedCount == std::size(dc->excluded)) return 0;
    dc->excluded[dc->excludedCount++] = clipped;
    return 3;
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
// Printer DCs and spooler-backed documents are not provided by the current
// LuceOS system image. Keep the GDI print ABI present and report failure.
__declspec(dllexport) int32_t AbortDoc(void*) { return -1; }
__declspec(dllexport) int32_t EndDoc(void*) { return -1; }
__declspec(dllexport) int32_t EndPage(void*) { return -1; }
__declspec(dllexport) int32_t StartDocW(void*, const void*) { return -1; }
__declspec(dllexport) int32_t StartPage(void*) { return -1; }
__declspec(dllexport) int32_t SetMapMode(void*, int32_t) { return 0; }
__declspec(dllexport) int32_t ChoosePixelFormat(void* hdc, const void* ppfd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ChoosePixelFormat(hdc, ppfd);
}
__declspec(dllexport) void* CreateSolidBrush(uint32_t colorRef) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateSolidBrush(colorRef);
}
__declspec(dllexport) void* CreateBrushIndirect(const void* brush) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateBrushIndirect(brush);
}
__declspec(dllexport) void* CreatePen(int32_t style, int32_t width, uint32_t color) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreatePen(style, width, color);
}
__declspec(dllexport) void* CreatePatternBrush(void* bitmap) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreatePatternBrush(bitmap);
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
__declspec(dllexport) int32_t GetDIBits(void* hdc, void* bitmap,
    uint32_t startScan, uint32_t scanLines, void* bits, void* bitmapInfo,
    uint32_t usage) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetDIBits(
        hdc, bitmap, startScan, scanLines, bits, bitmapInfo, usage);
}
__declspec(dllexport) int32_t SetDIBits(void* hdc, void* bitmap,
    uint32_t startScan, uint32_t scanLines, const void* bits,
    const void* bitmapInfo, uint32_t usage) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetDIBits(
        hdc, bitmap, startScan, scanLines, bits, bitmapInfo, usage);
}
__declspec(dllexport) void* CreateBitmap(int32_t width, int32_t height,
    uint32_t planes, uint32_t bitsPerPixel, const void* bits) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateBitmap(
        width, height, planes, bitsPerPixel, bits);
}
__declspec(dllexport) void* CreateDIBitmap(void* hdc, const void* header,
    uint32_t init, const void* bits, const void* info, uint32_t usage) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateDIBitmap(
        hdc, header, init, bits, info, usage);
}
__declspec(dllexport) int32_t MoveToEx(void* hdc, int32_t x, int32_t y, void* previous) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_MoveToEx(hdc, x, y, previous);
}
__declspec(dllexport) int32_t LineTo(void* hdc, int32_t x, int32_t y) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_LineTo(hdc, x, y);
}
__declspec(dllexport) uint32_t GetPixel(void* hdc, int32_t x, int32_t y) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetPixel(hdc, x, y);
}
__declspec(dllexport) int32_t SetPixelV(void* hdc, int32_t x, int32_t y, uint32_t color) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetPixelV(hdc, x, y, color);
}
__declspec(dllexport) uint32_t SetBkColor(void* hdc, uint32_t color) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetBkColor(hdc, color);
}
__declspec(dllexport) uint32_t SetDCBrushColor(void* hdc, uint32_t color) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetDCBrushColor(hdc, color);
}
__declspec(dllexport) int32_t GetROP2(void* hdc) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetROP2(hdc);
}
__declspec(dllexport) int32_t SetROP2(void* hdc, int32_t mode) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetROP2(hdc, mode);
}
__declspec(dllexport) int32_t SetPolyFillMode(void* hdc, int32_t mode) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetPolyFillMode(hdc, mode);
}
__declspec(dllexport) int32_t SetStretchBltMode(void* hdc, int32_t mode) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetStretchBltMode(hdc, mode);
}
__declspec(dllexport) int32_t Rectangle(void* hdc, int32_t l, int32_t t, int32_t r, int32_t b) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_Rectangle(hdc, l, t, r, b);
}
__declspec(dllexport) int32_t RoundRect(void* hdc, int32_t l, int32_t t, int32_t r, int32_t b,
                                        int32_t ew, int32_t eh) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_RoundRect(hdc, l, t, r, b, ew, eh);
}
__declspec(dllexport) int32_t Ellipse(void* hdc, int32_t l, int32_t t, int32_t r, int32_t b) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_Ellipse(hdc, l, t, r, b);
}
// These contracts are exported explicitly so the PE loader can distinguish
// unsupported drawing features from missing DLL exports. Implementations are
// added at the GDI surface owner as Paint's real drawing paths are connected.
__declspec(dllexport) int32_t BeginPath(void*) { return 0; }
__declspec(dllexport) int32_t EndPath(void*) { return 0; }
__declspec(dllexport) int32_t WidenPath(void*) { return 0; }
__declspec(dllexport) void* PathToRegion(void*) { return nullptr; }
__declspec(dllexport) int32_t Polygon(void*, const void*, int32_t) { return 0; }
__declspec(dllexport) int32_t Polyline(void*, const void*, int32_t) { return 0; }
__declspec(dllexport) int32_t PolyBezier(void*, const void*, uint32_t) { return 0; }
__declspec(dllexport) void* ExtCreatePen(uint32_t, uint32_t, const void*, uint32_t, const void*) { return nullptr; }
__declspec(dllexport) int32_t ExtFloodFill(void*, int32_t, int32_t, uint32_t, uint32_t) { return 0; }
__declspec(dllexport) int32_t FillRgn(void*, void*, void*) { return 0; }
__declspec(dllexport) int32_t GdiTransparentBlt(void*, int32_t, int32_t, int32_t, int32_t,
    void*, int32_t, int32_t, int32_t, int32_t, uint32_t) { return 0; }
__declspec(dllexport) int32_t MaskBlt(void*, int32_t, int32_t, int32_t, int32_t,
    void*, int32_t, int32_t, void*, int32_t, int32_t, uint32_t) { return 0; }
__declspec(dllexport) int32_t StretchBlt(void*, int32_t, int32_t, int32_t, int32_t,
    void*, int32_t, int32_t, int32_t, int32_t, uint32_t) { return 0; }
__declspec(dllexport) uint32_t GetDIBColorTable(void*, uint32_t, uint32_t, void*) { return 0; }
__declspec(dllexport) uint32_t GetEnhMetaFileHeader(void*, uint32_t, void*) { return 0; }
__declspec(dllexport) int32_t PlayEnhMetaFile(void*, void*, const void*) { return 0; }
__declspec(dllexport) int32_t DeleteEnhMetaFile(void*) { return 0; }
__declspec(dllexport) int32_t SaveDC(void*) { return 0; }
__declspec(dllexport) int32_t RestoreDC(void*, int32_t) { return 0; }
__declspec(dllexport) int32_t IntersectClipRect(void*, int32_t, int32_t, int32_t, int32_t) { return 0; }
__declspec(dllexport) int32_t ExcludeClipRect(void* hdc, int32_t left, int32_t top,
        int32_t right, int32_t bottom) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ExcludeClipRect(
        hdc, left, top, right, bottom);
}
__declspec(dllexport) int32_t EnumFontFamiliesW(void*, const wchar_t*, void*, intptr_t) { return 0; }
__declspec(dllexport) void* SelectObject(void* hdc, void* h) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SelectObject(hdc, h);
}
__declspec(dllexport) int32_t DeleteObject(void* ho) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DeleteObject(ho);
}
__declspec(dllexport) void MicaGdi32ForgetDcState(void* hdc) {
    micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ForgetDcState(hdc);
}
__declspec(dllexport) uint32_t MicaGdi32GetResourceCount(uint32_t flags) noexcept {
    if (flags != 0) return 0;
    uint32_t count = 0;
    for (const auto& font : micant::um::winnt_user32_gdi32_imm32_abi::g_gdiFonts)
        if (font.active && !font.stock) ++count;
    for (const auto& bitmap : micant::um::winnt_user32_gdi32_imm32_abi::g_gdiBitmaps)
        if (bitmap.bitmap) ++count;
    for (const auto& dc : micant::um::winnt_user32_gdi32_imm32_abi::g_gdiDcs)
        if (dc.dc) ++count;
    {
        micant::um::winnt_user32_gdi32_imm32_abi::GdiBrushGuard guard;
        for (const auto& brush : micant::um::winnt_user32_gdi32_imm32_abi::g_gdiBrushes)
            if (brush.active && !brush.stock) ++count;
    }
    for (const auto& pen : micant::um::winnt_user32_gdi32_imm32_abi::g_gdiPens)
        if (pen.active && !pen.stock) ++count;
    return count;
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
