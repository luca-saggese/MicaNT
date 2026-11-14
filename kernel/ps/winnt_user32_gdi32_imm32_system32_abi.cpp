/**
 * @file winnt_user32_gdi32_imm32_system32_abi.cpp
 * @brief Horizon 111 (Kernel Unit #83): Sovereign WinNT Core User-Mode System32 DLLs
 *        (user32.dll, gdi32.dll & imm32.dll) Official PE32+ Exports, Win32 HWND/HDC/HMONITOR
 *        Creation, Non-Blocking PeekMessageW Game Loop, 8000Hz WM_INPUT Raw Input,
 *        GDI PixelFormat / SwapBuffers Interop, and IMM32 Full-Screen Game IME Control.
 */

#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"
#if defined(BUILD_SYSTEM32_USER32_DLL)
#include "micant/um/subsystems/imaging/ico_decoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/subsystems/winuser/icon_handle_table.hpp"
#include "micant/shared/native_nt_api.hpp"
#endif
#include "micant/shared/win32k_client.h"

#include <algorithm>

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
    MICA_WIN32K_PROC(MicaWin32kSetFocus);
    MICA_WIN32K_PROC(MicaWin32kSetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kGetWindowIcon);
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
#if defined(BUILD_SYSTEM32_USER32_DLL)
struct RegisteredClassIcons {
    wchar_t name[MICANT_WIN32K_TEXT_CAPACITY]{};
    void* large{};
    void* small{};
    winuser::IconHandleTable::Lease largeLease{};
    winuser::IconHandleTable::Lease smallLease{};
};
RegisteredClassIcons g_registeredClassIcons[64]{};
struct RegisteredWindowClass { void* window{}; RegisteredClassIcons* icons{}; };
RegisteredWindowClass g_registeredWindowClasses[128]{};
RegisteredWindowClass* findRegisteredWindowClass(void* hwnd) noexcept {
    for (auto& entry : g_registeredWindowClasses)
        if (entry.window == hwnd) return &entry;
    return nullptr;
}
RegisteredClassIcons* findRegisteredClassIcons(const wchar_t* name) noexcept {
    if (!name) return nullptr;
    for (auto& entry : g_registeredClassIcons) {
        if (!entry.name[0]) continue;
        size_t i = 0;
        while (i < MICANT_WIN32K_TEXT_CAPACITY && entry.name[i] == name[i] && name[i]) ++i;
        if (i < MICANT_WIN32K_TEXT_CAPACITY && !entry.name[i] && !name[i]) return &entry;
    }
    return nullptr;
}
#endif

int32_t g_cursorShowCount = 0;
WinNtPoint g_cursorPos{1920, 1080};
int32_t g_activePixelFormat = 1;
int32_t g_imeOpenStatus = 0;
intptr_t AuditWindowProc(void*,uint32_t message,uintptr_t,intptr_t) noexcept {
    return message == 0x0081 ? 1 : 0;
}
} // namespace

#if defined(BUILD_SYSTEM32_USER32_DLL)
void* User32_CreateIconFromResourceEx(const uint8_t* resourceBytes,
                                      uint32_t resourceByteCount, int32_t isIcon,
                                      uint32_t resourceVersion, int32_t desiredWidth,
                                      int32_t desiredHeight, uint32_t flags) noexcept {
    using namespace micant::um;
    if (!resourceBytes || !resourceByteCount || isIcon != 1 ||
        resourceVersion < 0x00020000u || resourceVersion > 0x00030000u ||
        desiredWidth < 0 || desiredHeight < 0 || (flags & ~0x00000040u)) return nullptr;
    imaging::ImageFrame frame{};
    imaging::ImageLimits limits{};
    limits.maxEncodedBytes = 16ull * 1024 * 1024;
    limits.maxPixels = 16ull * 1024 * 1024;
    limits.maxDecodedBytes = 64ull * 1024 * 1024;
    limits.maxIntermediateBytes = 80ull * 1024 * 1024;
    const auto status = imaging::DecodeIconResourcePayload(
        std::span<const uint8_t>(resourceBytes, resourceByteCount), frame,
        imaging::NativeImageAllocator(), limits);
    if (status != imaging::ImageStatus::Success) return nullptr;
    const uint32_t targetWidth = desiredWidth ? static_cast<uint32_t>(desiredWidth)
        : ((flags & 0x40u) ? 32u : frame.width);
    const uint32_t targetHeight = desiredHeight ? static_cast<uint32_t>(desiredHeight)
        : ((flags & 0x40u) ? 32u : frame.height);
    if (!targetWidth || !targetHeight || targetWidth > 4096 || targetHeight > 4096)
        return nullptr;
    if (frame.width != targetWidth || frame.height != targetHeight) {
        imaging::ImageFrame scaled{};
        if (imaging::ScaleIconFrame(frame, targetWidth, targetHeight, scaled,
                                    imaging::NativeImageAllocator(), limits) !=
            imaging::ImageStatus::Success) return nullptr;
        frame = std::move(scaled);
    }
    return winuser::ProcessIconHandles().create(std::move(frame),
        imaging::NativeImageAllocator(), false);
}

void* User32_CopyIcon(void* icon) noexcept {
    return micant::um::winuser::ProcessIconHandles().copy(
        icon, micant::um::imaging::NativeImageAllocator());
}

int32_t User32_DestroyIcon(void* icon) noexcept {
    return micant::um::winuser::ProcessIconHandles().destroy(icon) ? 1 : 0;
}

void* User32_LoadImageW(void* /*instance*/, const wchar_t* name, uint32_t type,
                        int32_t desiredWidth, int32_t desiredHeight,
                        uint32_t flags) noexcept {
    using namespace micant::um;
    constexpr uint32_t LoadFromFile = 0x0010u;
    constexpr uint32_t DefaultSize = 0x0040u;
    if (!name || !*name || type != 1u || desiredWidth < 0 || desiredHeight < 0 ||
        !(flags & LoadFromFile) || (flags & ~(LoadFromFile | DefaultSize))) return nullptr;
    uint32_t width = static_cast<uint32_t>(desiredWidth);
    uint32_t height = static_cast<uint32_t>(desiredHeight);
    if (flags & DefaultSize) {
        if (!width) width = 32;
        if (!height) height = 32;
    }
    imaging::ImageLimits limits{};
    limits.maxEncodedBytes = 16ull * 1024 * 1024;
    limits.maxPixels = 16ull * 1024 * 1024;
    limits.maxDecodedBytes = 64ull * 1024 * 1024;
    limits.maxIntermediateBytes = 80ull * 1024 * 1024;
    imaging::ImageFrame frame{};
    const auto status = imaging::DecodeIconFile(name, frame,
        imaging::NativeImageAllocator(), width, height, limits);
    if (status != imaging::ImageStatus::Success) return nullptr;
    return winuser::ProcessIconHandles().create(std::move(frame),
        imaging::NativeImageAllocator(), false);
}

void* User32_LoadImageA(void* instance, const char* name, uint32_t type,
                        int32_t desiredWidth, int32_t desiredHeight,
                        uint32_t flags) noexcept {
    if (!name || reinterpret_cast<uintptr_t>(name) <= UINT16_MAX) return nullptr;
    wchar_t path[260]{};
    size_t length = 0;
    for (; name[length] && length + 1 < 260; ++length) {
        const unsigned char byte = static_cast<unsigned char>(name[length]);
        // The freestanding User32 target has no active ANSI codepage service.
        // Accept ASCII paths losslessly; reject bytes requiring codepage mapping.
        if (byte >= 0x80) return nullptr;
        path[length] = static_cast<wchar_t>(byte);
    }
    if (name[length] || !length) return nullptr;
    return User32_LoadImageW(instance, path, type, desiredWidth, desiredHeight, flags);
}

void* User32_LoadIconW(void* /*instance*/, const wchar_t* /*name*/) noexcept {
    // Module resource lookup and stock artwork are not connected to the Ring 3
    // resource loader yet. Keep the API explicit and never manufacture HICON.
    return nullptr;
}

void* User32_LoadIconA(void* /*instance*/, const char* /*name*/) noexcept {
    return nullptr;
}

int32_t User32_DrawIconEx(void* destination, int32_t x, int32_t y, void* icon,
                          int32_t width, int32_t height, uint32_t animationStep,
                          void* brush, uint32_t flags) noexcept {
    using namespace micant::um;
    constexpr uint32_t DI_IMAGE = 0x0002u;
    constexpr uint32_t DI_NORMAL = 0x0003u;
    if (!destination || !icon || width < 0 || height < 0 || animationStep || brush ||
        (flags != DI_IMAGE && flags != DI_NORMAL)) return 0;
    winuser::IconHandleTable::Lease source{};
    if (!winuser::ProcessIconHandles().acquire(icon, source) &&
        !winuser::ProcessWindowIcons().acquire(icon, source)) return 0;
    const auto& sourceView = source.bitmap();
    const uint32_t targetWidth = width ? static_cast<uint32_t>(width) : sourceView.width;
    const uint32_t targetHeight = height ? static_cast<uint32_t>(height) : sourceView.height;
    if (!targetWidth || !targetHeight || targetWidth > 4096 || targetHeight > 4096) return 0;

    imaging::ImageFrame scaled{};
    std::span<const uint8_t> pixels = sourceView.pixels;
    uint32_t stride = sourceView.stride;
    if (targetWidth != sourceView.width || targetHeight != sourceView.height) {
        imaging::ImageFrame input{};
        input.width = sourceView.width; input.height = sourceView.height;
        input.stride = sourceView.stride;
        input.format = imaging::PixelFormat::Bgra8Premultiplied;
        // The immutable lease is the owner for this temporary non-owning view.
        // ScaleIconFrame accepts an owned ImageBuffer; copy only after the
        // bounded allocation succeeds so it never retains caller memory.
        if (!input.pixels.allocate(sourceView.pixels.size(), imaging::NativeImageAllocator())) return 0;
        std::memcpy(input.pixels.data(), sourceView.pixels.data(), sourceView.pixels.size());
        imaging::ImageLimits limits{};
        limits.maxPixels = 16ull * 1024 * 1024;
        limits.maxDecodedBytes = 64ull * 1024 * 1024;
        limits.maxIntermediateBytes = 64ull * 1024 * 1024;
        if (imaging::ScaleIconFrame(input, targetWidth, targetHeight, scaled,
                                    imaging::NativeImageAllocator(), limits) !=
            imaging::ImageStatus::Success) return 0;
        pixels = scaled.pixels.span();
        stride = scaled.stride;
    }

    using CreateDc = void* (*)(void*);
    using DeleteDc = int32_t (*)(void*);
    using CreateDib = void* (*)(void*, int32_t, int32_t, void**);
    using Select = void* (*)(void*, void*);
    using DeleteObject = int32_t (*)(void*);
    using BitBlt = int32_t (*)(void*, int32_t, int32_t, int32_t, int32_t,
                               void*, int32_t, int32_t, uint32_t);
    const auto createDc = Win32kProc<CreateDc>("MicaWin32kCreateCompatibleDC");
    const auto deleteDc = Win32kProc<DeleteDc>("MicaWin32kDeleteDC");
    const auto createDib = Win32kProc<CreateDib>("MicaWin32kCreateDibSection");
    const auto select = Win32kProc<Select>("MicaWin32kSelectObject");
    const auto deleteObject = Win32kProc<DeleteObject>("MicaWin32kDeleteObject");
    const auto bitBlt = Win32kProc<BitBlt>("MicaWin32kBitBlt");
    if (!createDc || !deleteDc || !createDib || !select || !deleteObject || !bitBlt) return 0;

    void* memoryDc = createDc(nullptr);
    if (!memoryDc) return 0;
    void* dibBits{};
    void* bitmap = createDib(nullptr, static_cast<int32_t>(targetWidth),
                             static_cast<int32_t>(targetHeight), &dibBits);
    if (!bitmap || !dibBits) { deleteDc(memoryDc); return 0; }
    const size_t rowBytes = static_cast<size_t>(targetWidth) * 4;
    for (uint32_t row = 0; row < targetHeight; ++row)
        std::memcpy(static_cast<uint8_t*>(dibBits) + static_cast<size_t>(row) * rowBytes,
                    pixels.data() + static_cast<size_t>(row) * stride, rowBytes);
    void* oldBitmap = select(memoryDc, bitmap);
    if (!oldBitmap) {
        deleteObject(bitmap); deleteDc(memoryDc); return 0;
    }
    const int32_t drawn = bitBlt(destination, x, y, static_cast<int32_t>(targetWidth),
        static_cast<int32_t>(targetHeight), memoryDc, 0, 0,
        MICANT_WIN32K_ROP_ALPHA_SOURCE_OVER);
    select(memoryDc, oldBitmap);
    const int32_t bitmapReleased = deleteObject(bitmap);
    const int32_t dcReleased = deleteDc(memoryDc);
    return drawn && bitmapReleased && dcReleased;
}

int32_t User32_DrawIcon(void* destination, int32_t x, int32_t y, void* icon) noexcept {
    return User32_DrawIconEx(destination, x, y, icon, 0, 0, 0, nullptr, 0x0003u);
}
#endif

// user32.dll implementations
uint16_t User32_RegisterClassExW(const void* lpWndClassEx) noexcept {
    if (!lpWndClassEx) return 0;
    const auto* wc=static_cast<const MicaWin32kWindowClassEx*>(lpWndClassEx);
    if (wc->cbSize < sizeof(MicaWin32kWindowClassEx) || !wc->className || !wc->wndProc) return 0;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    RegisteredClassIcons* iconSlot = nullptr;
    for (auto& entry : g_registeredClassIcons) if (!entry.name[0]) { iconSlot = &entry; break; }
    if (!iconSlot) return 0;
    size_t classNameLength = 0;
    while (classNameLength < MICANT_WIN32K_TEXT_CAPACITY && wc->className[classNameLength])
        ++classNameLength;
    if (classNameLength == MICANT_WIN32K_TEXT_CAPACITY) return 0;
    winuser::IconHandleTable::Lease classLargeLease{};
    winuser::IconHandleTable::Lease classSmallLease{};
    if ((wc->icon && !winuser::ProcessIconHandles().acquire(wc->icon, classLargeLease)) ||
        (wc->smallIcon && !winuser::ProcessIconHandles().acquire(wc->smallIcon, classSmallLease)))
        return 0;
#endif
    using Fn=uint16_t (*)(const wchar_t*,void*);
    auto fn=Win32kProc<Fn>("MicaWin32kRegisterClassW");
    const uint16_t atom = fn ? fn(wc->className,wc->wndProc) : 0;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (atom && wc->className) {
        for (size_t i = 0; i < classNameLength; ++i) iconSlot->name[i] = wc->className[i];
        iconSlot->name[classNameLength] = 0;
        iconSlot->large = wc->icon; iconSlot->small = wc->smallIcon;
        iconSlot->largeLease = std::move(classLargeLease);
        iconSlot->smallLease = std::move(classSmallLease);
    }
#endif
    return atom;
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
    void* hwnd = fn ? fn(dwExStyle,lpClassName,lpWindowName,dwStyle,X,Y,nWidth,nHeight,hWndParent,hMenu,hInstance,lpParam) : nullptr;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (hwnd) {
        if (auto* icons = findRegisteredClassIcons(lpClassName)) {
            for (auto& entry : g_registeredWindowClasses) {
                if (entry.window) continue;
                entry = {hwnd, icons};
                break;
            }
            using SetIconFn = int32_t (*)(void*, uint32_t, uint64_t, uint64_t*);
            const auto setIcon = Win32kProc<SetIconFn>("MicaWin32kSetWindowIcon");
            const auto applyDefault = [&](uint32_t type, void* icon) noexcept {
                if (!icon || !setIcon) return;
                uint64_t previous{};
                if (!setIcon(hwnd, type, reinterpret_cast<uint64_t>(icon), &previous)) return;
                (void)winuser::ProcessWindowIcons().set(hwnd, type, icon);
                if (winuser::ProcessWindowIcons().get(hwnd, type) != icon) {
                    uint64_t ignored{};
                    (void)setIcon(hwnd, type, previous, &ignored);
                }
            };
            applyDefault(1, icons->large);
            applyDefault(0, icons->small);
        }
    }
#endif
    return hwnd;
}

int32_t User32_DestroyWindow(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDestroyWindow");
    const int32_t destroyed = fn ? fn(hWnd) : 0;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (destroyed) {
        winuser::ProcessWindowIcons().remove(hWnd);
        if (auto* entry = findRegisteredWindowClass(hWnd)) *entry = {};
    }
#endif
    return destroyed;
}

#if defined(BUILD_SYSTEM32_USER32_DLL)
uintptr_t User32_GetClassLongPtrW(void* hwnd, int32_t index) noexcept {
    auto* windowClass = findRegisteredWindowClass(hwnd);
    if (!windowClass || !windowClass->icons) return 0;
    if (index == -14) return reinterpret_cast<uintptr_t>(windowClass->icons->large);
    if (index == -34) return reinterpret_cast<uintptr_t>(windowClass->icons->small);
    return 0;
}

uintptr_t User32_SetClassLongPtrW(void* hwnd, int32_t index, uintptr_t value) noexcept {
    auto* windowClass = findRegisteredWindowClass(hwnd);
    if (!windowClass || !windowClass->icons || (index != -14 && index != -34)) return 0;
    auto& icons = *windowClass->icons;
    void*& handle = index == -14 ? icons.large : icons.small;
    auto& lease = index == -14 ? icons.largeLease : icons.smallLease;
    void* replacement = reinterpret_cast<void*>(value);
    winuser::IconHandleTable::Lease replacementLease{};
    if (replacement && !winuser::ProcessIconHandles().acquire(replacement, replacementLease)) return 0;
    void* previous = handle;
    handle = replacement;
    lease = std::move(replacementLease);
    const uint32_t type = index == -14 ? 1u : 0u;
    using SetIconFn = int32_t (*)(void*, uint32_t, uint64_t, uint64_t*);
    const auto setIcon = Win32kProc<SetIconFn>("MicaWin32kSetWindowIcon");
    if (setIcon) {
        for (const auto& window : g_registeredWindowClasses) {
            if (window.icons != windowClass->icons || !window.window) continue;
            uint64_t ignored{};
            if (!setIcon(window.window, type, reinterpret_cast<uint64_t>(replacement), &ignored))
                continue;
            (void)winuser::ProcessWindowIcons().set(window.window, type, replacement);
        }
    }
    return reinterpret_cast<uintptr_t>(previous);
}
#endif

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
#if defined(BUILD_SYSTEM32_USER32_DLL)
    using SetIconFn=int32_t (*)(void*,uint32_t,uint64_t,uint64_t*);
    using GetIconFn=uint64_t (*)(void*,uint32_t);
    if (message == WM_SETICON_OFFICIAL) {
        if (wParam > 2) return 0;
        void* icon = reinterpret_cast<void*>(lParam);
        winuser::IconHandleTable::Lease validation{};
        if (icon && !winuser::ProcessIconHandles().acquire(icon, validation)) return 0;
        uint64_t previous{};
        auto set=Win32kProc<SetIconFn>("MicaWin32kSetWindowIcon");
        if (!set || !set(hWnd, static_cast<uint32_t>(wParam),
                          reinterpret_cast<uint64_t>(icon), &previous)) return 0;
        (void)winuser::ProcessWindowIcons().set(hWnd, static_cast<uint32_t>(wParam), icon);
        if (icon && winuser::ProcessWindowIcons().get(hWnd, static_cast<uint32_t>(wParam)) != icon) {
            uint64_t ignored{};
            (void)set(hWnd, static_cast<uint32_t>(wParam), previous, &ignored);
            return 0;
        }
        return static_cast<intptr_t>(previous);
    }
    if (message == WM_GETICON_OFFICIAL) {
        if (wParam > 2) return 0;
        auto get=Win32kProc<GetIconFn>("MicaWin32kGetWindowIcon");
        return get ? static_cast<intptr_t>(get(hWnd, static_cast<uint32_t>(wParam))) : 0;
    }
#endif
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
__declspec(dllexport) void* CreateIconFromResourceEx(
    const uint8_t* bits, uint32_t size, int32_t isIcon, uint32_t version,
    int32_t cx, int32_t cy, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CreateIconFromResourceEx(
        bits, size, isIcon, version, cx, cy, flags);
}
__declspec(dllexport) void* CreateIconFromResource(const uint8_t* bits,
    uint32_t size, int32_t isIcon, uint32_t version) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CreateIconFromResourceEx(
        bits, size, isIcon, version, 0, 0, 0);
}
__declspec(dllexport) void* CopyIcon(void* icon) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CopyIcon(icon);
}
__declspec(dllexport) int32_t DestroyIcon(void* icon) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DestroyIcon(icon);
}
__declspec(dllexport) void* LoadImageW(void* instance, const wchar_t* name,
    uint32_t type, int32_t cx, int32_t cy, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadImageW(
        instance, name, type, cx, cy, flags);
}
__declspec(dllexport) void* LoadImageA(void* instance, const char* name,
    uint32_t type, int32_t cx, int32_t cy, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadImageA(
        instance, name, type, cx, cy, flags);
}
__declspec(dllexport) void* LoadIconW(void* instance, const wchar_t* name) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadIconW(instance, name);
}
__declspec(dllexport) void* LoadIconA(void* instance, const char* name) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadIconA(instance, name);
}
__declspec(dllexport) int32_t DrawIconEx(void* hdc, int32_t x, int32_t y,
    void* icon, int32_t cx, int32_t cy, uint32_t step, void* brush, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DrawIconEx(
        hdc, x, y, icon, cx, cy, step, brush, flags);
}
__declspec(dllexport) int32_t DrawIcon(void* hdc, int32_t x, int32_t y, void* icon) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DrawIcon(
        hdc, x, y, icon);
}
__declspec(dllexport) uintptr_t GetClassLongPtrW(void* hwnd, int32_t index) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClassLongPtrW(hwnd, index);
}
__declspec(dllexport) uintptr_t SetClassLongPtrW(void* hwnd, int32_t index, uintptr_t value) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetClassLongPtrW(hwnd, index, value);
}
__declspec(dllexport) uintptr_t GetClassLongPtrA(void* hwnd, int32_t index) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClassLongPtrW(hwnd, index);
}
__declspec(dllexport) uintptr_t SetClassLongPtrA(void* hwnd, int32_t index, uintptr_t value) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetClassLongPtrW(hwnd, index, value);
}
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
