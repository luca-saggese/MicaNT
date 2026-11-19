#ifndef MICANT_WIN32K_EXPORTS
#define MICANT_WIN32K_EXPORTS
#endif

#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/shared/compositor_surface.h"
#include "micant/shared/pixel_composite.h"
#include "micant/shared/native_nt_api.hpp"

#include <cstdint>
#include <algorithm>


#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

namespace {


constexpr size_t kDcCapacity = 32;
constexpr size_t kBitmapCapacity = 32;
constexpr uint64_t kHandleTag = 0x4D574B0000000000ULL;

struct DeviceContext {
    uint64_t handle{};
    uint64_t defaultBitmapHandle{};
    void* window{};
    uint32_t bitmap{};
    uint32_t defaultPixel{};
    uint32_t surfacePitch{};
    bool memory{};
    bool active{};
    bool windowUpdateActive{};
    bool windowUpdatePresented{};
};

struct Bitmap {
    uint64_t handle{};
    uint32_t width{}, height{};
    uint32_t* pixels{};
    bool active{};
};

uint32_t* g_screenPixels{};
size_t g_screenPixelCount{};
uint32_t g_screenWidth{}, g_screenHeight{}, g_screenPitch{};
DeviceContext g_contexts[kDcCapacity]{};
Bitmap g_bitmaps[kBitmapCapacity]{};
uint32_t g_nextHandle{1};

uint64_t makeHandle(uint8_t kind, size_t index) noexcept {
    return kHandleTag | (static_cast<uint64_t>(kind) << 32) |
           (static_cast<uint64_t>(index + 1) << 16) | g_nextHandle++;
}

DeviceContext* findContext(void* handle) noexcept {
    for (auto& context : g_contexts) {
        if (context.active && context.handle == reinterpret_cast<uint64_t>(handle)) {
            return &context;
        }
    }
    return nullptr;
}

Bitmap* findBitmap(void* handle) noexcept {
    for (auto& bitmap : g_bitmaps) {
        if (bitmap.active && bitmap.handle == reinterpret_cast<uint64_t>(handle)) {
            return &bitmap;
        }
    }
    return nullptr;
}

DeviceContext* allocateContext(void* window, bool memory) noexcept {
    for (auto& context : g_contexts) {
        if (context.active) continue;
        context = {};
        context.handle = makeHandle(1, &context - g_contexts);
        context.window = window;
        context.memory = memory;
        if (memory) {
            context.defaultBitmapHandle = makeHandle(3, &context - g_contexts);
        }
        context.active = true;
        return &context;
    }
    return nullptr;
}

bool request(uint32_t operation, MicaWin32kRequest& value) noexcept;

uint32_t* allocateUserPixels(size_t count) noexcept {
    if (!count || count > SIZE_MAX / sizeof(uint32_t)) return nullptr;
    void* base = nullptr;
    size_t bytes = count * sizeof(uint32_t);
    if (micant::native_nt::NtAllocateVirtualMemory(micant::InvalidHandleValue,
            &base, 0, &bytes, 0x3000u, 0x04u) != micant::NtStatus::Success || !base)
        return nullptr;
    return static_cast<uint32_t*>(base);
}

void releaseUserPixels(uint32_t*& pixels) noexcept {
    if (!pixels) return;
    void* base = pixels;
    size_t bytes = 0;
    (void)micant::native_nt::NtFreeVirtualMemory(micant::InvalidHandleValue,
        &base, &bytes, 0x8000u);
    pixels = nullptr;
}

Bitmap* allocateBitmap(uint32_t width, uint32_t height) noexcept {
    if (!width || !height || static_cast<size_t>(width) > SIZE_MAX / height ||
        static_cast<size_t>(width) * height > SIZE_MAX / sizeof(uint32_t)) {
        return nullptr;
    }
    const size_t count = static_cast<size_t>(width) * height;
    for (auto& bitmap : g_bitmaps) {
        if (bitmap.active) continue;
        auto* pixels = allocateUserPixels(count);
        if (!pixels) return nullptr;
        std::fill_n(pixels, count, 0u);
        bitmap = {};
        bitmap.handle = makeHandle(2, &bitmap - g_bitmaps);
        bitmap.width = width;
        bitmap.height = height;
        bitmap.pixels = pixels;
        bitmap.active = true;
        return &bitmap;
    }
    return nullptr;
}

bool ensureDisplaySurface() noexcept {
    micant::um::UserVideoInfo64 mode{};
    if (micant::sys::invokeUserService(0x80, reinterpret_cast<uint64_t>(&mode)) != 0 ||
        !mode.width || !mode.height || mode.bpp != 32 || mode.pixelFormat != 1 ||
        mode.pitch < mode.width ||
        static_cast<size_t>(mode.pitch) > SIZE_MAX / mode.height ||
        static_cast<size_t>(mode.pitch) * mode.height > SIZE_MAX / sizeof(uint32_t))
        return false;
    const size_t requiredPixels = static_cast<size_t>(mode.pitch) * mode.height;
    if (g_screenPixels && mode.width == g_screenWidth && mode.height == g_screenHeight &&
        mode.pitch == g_screenPitch && requiredPixels == g_screenPixelCount) return true;
    auto* replacement = allocateUserPixels(requiredPixels);
    if (!replacement) return false;
    std::fill_n(replacement, requiredPixels, 0u);
    releaseUserPixels(g_screenPixels);
    g_screenPixels = replacement;
    g_screenPixelCount = requiredPixels;
    g_screenWidth = mode.width;
    g_screenHeight = mode.height;
    g_screenPitch = mode.pitch;
    return true;
}

uint32_t* pixelsFor(DeviceContext& context, uint32_t& width,
                    uint32_t& height) noexcept {
    if (!context.memory && context.window) {
        MicaWin32kRequest value{};
        value.args[0] = reinterpret_cast<uint64_t>(context.window);
        if (!request(MICANT_WIN32K_OP_GET_WINDOW_SURFACE, value) ||
            !value.result || !value.args[1] || !value.args[2] ||
            !value.args[3] || !value.args[4] ||
            value.rect.left < 0 || value.rect.top < 0 ||
            value.rect.right <= value.rect.left || value.rect.bottom <= value.rect.top ||
            static_cast<uint32_t>(value.rect.right) > value.args[4] ||
            static_cast<uint32_t>(value.rect.bottom) > value.args[2])
            return nullptr;
        width = static_cast<uint32_t>(value.rect.right - value.rect.left);
        height = static_cast<uint32_t>(value.rect.bottom - value.rect.top);
        context.surfacePitch = static_cast<uint32_t>(value.args[1]);
        return reinterpret_cast<uint32_t*>(value.args[3]) +
            static_cast<size_t>(value.rect.top) * context.surfacePitch +
            static_cast<uint32_t>(value.rect.left);
    }
    if (context.bitmap) {
        auto& bitmap = g_bitmaps[context.bitmap - 1];
        if (!bitmap.active) return nullptr;
        width = bitmap.width;
        height = bitmap.height;
        context.surfacePitch = bitmap.width;
        return bitmap.pixels;
    }
    if (context.memory) {
        width = 1;
        height = 1;
        context.surfacePitch = 1;
        return &context.defaultPixel;
    }
    if (!ensureDisplaySurface()) return nullptr;
    width = g_screenWidth;
    height = g_screenHeight;
    context.surfacePitch = g_screenPitch;
    return g_screenPixels;
}

bool request(uint32_t operation, MicaWin32kRequest& value) noexcept {
    value.size = sizeof(value);
    value.version = MICANT_WIN32K_REQUEST_VERSION;
    value.operation = operation;
    return micant::sys::invokeUserService(
        0x86, reinterpret_cast<uint64_t>(&value)) == 0;
}

bool copyText(uint16_t* destination, const wchar_t* source) noexcept {
    if (!source) {
        destination[0] = 0;
        return true;
    }
    for (size_t index = 0; index < MICANT_WIN32K_TEXT_CAPACITY; ++index) {
        const auto character = static_cast<uint16_t>(source[index]);
        destination[index] = character;
        if (!character) return true;
    }
    destination[MICANT_WIN32K_TEXT_CAPACITY - 1] = 0;
    return false;
}

intptr_t callWindowProcedure(void* hwnd, uint32_t message,
                             uintptr_t wParam, intptr_t lParam) noexcept {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = static_cast<uint64_t>(-4);
    if (!request(MICANT_WIN32K_OP_GET_WINDOW_LONG, value)) return 0;
    using WindowProcedure = intptr_t (*)(void*, uint32_t, uintptr_t, intptr_t);
    auto procedure = reinterpret_cast<WindowProcedure>(value.result);
    return procedure ? procedure(hwnd, message, wParam, lParam)
                     : MicaWin32kDefWindowProcW(hwnd, message, wParam, lParam);
}

bool presentSurface(const uint32_t* pixels, uint32_t width,
                    uint32_t height, uint32_t dstX, uint32_t dstY,
                    uint32_t sourcePitch) noexcept {
    if (!ensureDisplaySurface() || !pixels || !width || !height ||
        sourcePitch < width ||
        dstX >= g_screenWidth || dstY >= g_screenHeight ||
        width > g_screenWidth - dstX || height > g_screenHeight - dstY) {
        return false;
    }
    micant::um::UserPresentSurfaceParams64 params{};
    params.surfaceBufferVa = reinterpret_cast<uint64_t>(pixels);
    params.dstX = dstX;
    params.dstY = dstY;
    params.width = width;
    params.height = height;
    params.pitch = sourcePitch;
    return micant::sys::invokeUserService(
        0x81, reinterpret_cast<uint64_t>(&params)) == 0;
}

void copyMessage(const MicaWin32kRequest& source,
                 MicaWin32kMessage* destination) noexcept {
    destination->hwnd = reinterpret_cast<void*>(source.messageHwnd);
    destination->message = source.messageId;
    destination->wParam = static_cast<uintptr_t>(source.messageWParam);
    destination->lParam = static_cast<intptr_t>(source.messageLParam);
    destination->time = source.messageTime;
    destination->pt = {source.messageX, source.messageY};
}

}

extern "C" {

void MicaWin32kDebugInputTrace(uint32_t stage, uint32_t controlId,
                              uint32_t flags) {
    MicaWin32kRequest value{};
    value.operation = MICANT_WIN32K_OP_DEBUG_INPUT_TRACE;
    value.args[0] = stage;
    value.args[1] = controlId;
    value.args[2] = flags;
    (void)request(value.operation, value);
}

int32_t MicaWin32kGetSystemMetrics(int32_t index) {
    if (index != 0 && index != 1) return 0;
    MicaWin32kRequest value{};
    value.operation = MICANT_WIN32K_OP_GET_SYSTEM_METRICS;
    value.args[0] = static_cast<uint32_t>(index);
    return request(value.operation, value) ? static_cast<int32_t>(value.result) : 0;
}

void* MicaWin32kGetDlgItem(void* dialog, int32_t controlId) {
    MicaWin32kRequest value{};
    value.operation = MICANT_WIN32K_OP_GET_DLG_ITEM;
    value.args[0] = reinterpret_cast<uint64_t>(dialog);
    value.args[1] = static_cast<uint32_t>(controlId);
    return request(MICANT_WIN32K_OP_GET_DLG_ITEM, value) && value.result
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

int32_t MicaWin32kTrackMouseEvent(void* window, uint32_t flags,
    uint32_t hoverTime, uint32_t* currentFlags, uint32_t* currentHoverTime) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(window);
    value.args[1] = flags;
    value.args[2] = hoverTime;
    if (!request(MICANT_WIN32K_OP_TRACK_MOUSE_EVENT, value)) return 0;
    if (currentFlags) *currentFlags = static_cast<uint32_t>(value.result);
    if (currentHoverTime) *currentHoverTime = static_cast<uint32_t>(value.args[3]);
    return 1;
}

int32_t MicaWin32kGetClassNameW(void* window, wchar_t* output, int32_t capacity) {
    if (!output || capacity <= 0) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(window);
    value.args[1] = static_cast<uint32_t>(capacity);
    if (!request(MICANT_WIN32K_OP_GET_CLASS_NAME, value)) {
        output[0] = L'\0';
        return 0;
    }
    const size_t count = static_cast<size_t>(value.result);
    if (count >= static_cast<size_t>(capacity)) {
        output[0] = L'\0';
        return 0;
    }
    for (size_t index = 0; index < count; ++index)
        output[index] = static_cast<wchar_t>(value.text[index]);
    output[count] = L'\0';
    return static_cast<int32_t>(count);
}

int32_t MicaWin32kEnumChildWindows(void* parent, void* callback,
    intptr_t parameter) {
    if (!parent || !callback) return 0;
    using Callback = int32_t (*)(void*, intptr_t);
    const auto enumerate = reinterpret_cast<Callback>(callback);
    uint64_t first = 0;
    for (;;) {
        MicaWin32kRequest value{};
        value.args[0] = reinterpret_cast<uint64_t>(parent);
        value.args[1] = first;
        value.args[2] = MICANT_WIN32K_ENUM_BATCH_CAPACITY;
        if (!request(MICANT_WIN32K_OP_ENUM_CHILD_WINDOWS, value)) {
            MicaWin32kDebugInputTrace(41,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(parent)),
                static_cast<uint32_t>(first));
            return 0;
        }
        for (uint64_t index = 0; index < value.result; ++index) {
            if (!enumerate(reinterpret_cast<void*>(value.windowHandles[index]), parameter)) {
                MicaWin32kDebugInputTrace(42,
                    static_cast<uint32_t>(value.windowHandles[index]),
                    static_cast<uint32_t>(index));
                return 0;
            }
        }
        if (value.result < MICANT_WIN32K_ENUM_BATCH_CAPACITY) return 1;
        if (first > UINT64_MAX - value.result) return 0;
        first += value.result;
    }
}

int32_t MicaWin32kEnableWindow(void* window, int32_t enable,
    int32_t* wasDisabled, int32_t* changed) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(window);
    value.args[1] = enable != 0;
    if (!request(MICANT_WIN32K_OP_ENABLE_WINDOW, value)) return 0;
    if (wasDisabled) *wasDisabled = static_cast<int32_t>(value.result);
    if (changed) *changed = static_cast<int32_t>(value.args[2]);
    return 1;
}

void* MicaWin32kGetParent(void* window) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(window);
    if (!request(MICANT_WIN32K_OP_GET_PARENT, value)) return nullptr;
    return reinterpret_cast<void*>(value.result);
}

int32_t MicaWin32kSetWindowMenu(void* hwnd, void* menu) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = reinterpret_cast<uint64_t>(menu);
    return request(MICANT_WIN32K_OP_SET_WINDOW_MENU, value) ? 1 : 0;
}

void* MicaWin32kGetWindowMenu(void* hwnd) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    return request(MICANT_WIN32K_OP_GET_WINDOW_MENU, value)
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

void* MicaWin32kCreateWindowExW(uint32_t exStyle, const wchar_t* className,
    const wchar_t* title, uint32_t style, int32_t x, int32_t y, int32_t width,
    int32_t height, void* parent, void* menu, void* instance, void* parameter) {
    MicaWin32kRequest value{};
    if (!copyText(value.className, className) ||
        !copyText(value.title, title)) return nullptr;
    value.args[0] = exStyle;
    value.args[1] = style;
    value.args[2] = static_cast<uint64_t>(static_cast<int64_t>(x));
    value.args[3] = static_cast<uint64_t>(static_cast<int64_t>(y));
    value.args[4] = static_cast<uint64_t>(static_cast<int64_t>(width));
    value.args[5] = static_cast<uint64_t>(static_cast<int64_t>(height));
    value.args[6] = reinterpret_cast<uint64_t>(parent);
    value.args[7] = reinterpret_cast<uint64_t>(menu);
    if (!request(MICANT_WIN32K_OP_CREATE_WINDOW, value) || !value.result) {
        return nullptr;
    }
    void* hwnd = reinterpret_cast<void*>(value.result);
    MicaWin32kCreateStruct create{parameter, instance, menu, parent, height,
        width, y, x, static_cast<int32_t>(style), title, className, exStyle};
    if (callWindowProcedure(hwnd, 0x0081, 1,
                            reinterpret_cast<intptr_t>(&create)) == 0 ||
        callWindowProcedure(hwnd, 0x0001, 0,
                            reinterpret_cast<intptr_t>(&create)) == -1) {
        MicaWin32kDestroyWindow(hwnd);
        return nullptr;
    }
    return hwnd;
}

int32_t MicaWin32kDestroyWindow(void* hwnd) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    return request(MICANT_WIN32K_OP_DESTROY_WINDOW, value) && value.result;
}

int32_t MicaWin32kShowWindow(void* hwnd, int32_t command) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = static_cast<uint64_t>(static_cast<int64_t>(command));
    return request(MICANT_WIN32K_OP_SHOW_WINDOW, value)
        ? static_cast<int32_t>(value.result) : 0;
}

int32_t MicaWin32kSetWindowPos(void* hwnd, void* after, int32_t x,
    int32_t y, int32_t width, int32_t height, uint32_t flags) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = static_cast<uint64_t>(static_cast<int64_t>(x));
    value.args[2] = static_cast<uint64_t>(static_cast<int64_t>(y));
    value.args[3] = static_cast<uint64_t>(static_cast<int64_t>(width));
    value.args[4] = static_cast<uint64_t>(static_cast<int64_t>(height));
    value.args[5] = flags;
    value.args[6] = reinterpret_cast<uint64_t>(after);
    return request(MICANT_WIN32K_OP_SET_WINDOW_POS, value) && value.result;
}

int32_t MicaWin32kSetTaskbarButtonBounds(void* hwnd,
                                         const MicaWin32kRect* bounds) {
    if (!hwnd || !bounds) return 0;
    MicaWin32kRequest value{};
    value.operation = MICANT_WIN32K_OP_SET_TASKBAR_BUTTON_BOUNDS;
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.rect = *bounds;
    return request(value.operation, value) && value.result;
}

int32_t MicaWin32kPostMessageW(void* hwnd, uint32_t message,
                               uintptr_t wParam, intptr_t lParam) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = message;
    value.args[2] = wParam;
    value.args[3] = static_cast<uint64_t>(lParam);
    return request(MICANT_WIN32K_OP_POST_MESSAGE, value) && value.result;
}

intptr_t MicaWin32kSendMessageW(void* hwnd, uint32_t message,
                                uintptr_t wParam, intptr_t lParam) {
    if (message == 0x0010) {
        MicaWin32kPostMessageW(hwnd, 0x0010, wParam, lParam);
        return 1;
    }
    return callWindowProcedure(hwnd, message, wParam, lParam);
}

void MicaWin32kPostQuitMessage(int32_t exitCode) {
    MicaWin32kRequest value{};
    value.args[0] = static_cast<uint64_t>(static_cast<int64_t>(exitCode));
    request(MICANT_WIN32K_OP_POST_QUIT, value);
}

uint16_t MicaWin32kRegisterClassW(const wchar_t* className, void* procedure,
                                  int32_t windowExtra) {
    MicaWin32kRequest value{};
    if (!copyText(value.className, className) || windowExtra < 0 || windowExtra > 64) return 0;
    value.args[0] = reinterpret_cast<uint64_t>(procedure);
    value.args[1] = static_cast<uint32_t>(windowExtra);
    return request(MICANT_WIN32K_OP_REGISTER_CLASS, value)
        ? static_cast<uint16_t>(value.result) : 0;
}

intptr_t MicaWin32kDispatchMessageW(const MicaWin32kMessage* message) {
    if (!message) return 0;
    return callWindowProcedure(message->hwnd, message->message,
                               message->wParam, message->lParam);
}

intptr_t MicaWin32kSetWindowLongPtrW(void* hwnd, int32_t index,
                                     intptr_t newValue) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = static_cast<uint64_t>(static_cast<int64_t>(index));
    value.args[2] = static_cast<uint64_t>(newValue);
    return request(MICANT_WIN32K_OP_SET_WINDOW_LONG, value)
        ? static_cast<intptr_t>(value.result) : 0;
}

intptr_t MicaWin32kGetWindowLongPtrW(void* hwnd, int32_t index) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = static_cast<uint64_t>(static_cast<int64_t>(index));
    return request(MICANT_WIN32K_OP_GET_WINDOW_LONG, value)
        ? static_cast<intptr_t>(value.result) : 0;
}

intptr_t MicaWin32kDefWindowProcW(void* hwnd, uint32_t message,
                                  uintptr_t wParam, intptr_t lParam) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = message;
    value.args[2] = wParam;
    value.args[3] = static_cast<uint64_t>(lParam);
    return request(MICANT_WIN32K_OP_DEF_WINDOW_PROC, value)
        ? static_cast<intptr_t>(value.result) : 0;
}

int32_t MicaWin32kPeekMessageW(MicaWin32kMessage* output, void* hwnd,
    uint32_t minimum, uint32_t maximum, uint32_t remove) {
    if (!output) return 0;
    MicaWin32kRequest input{};
    (void)request(MICANT_WIN32K_OP_PUMP_INPUT, input);
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = minimum;
    value.args[2] = maximum;
    value.args[3] = remove;
    if (!request(MICANT_WIN32K_OP_PEEK_MESSAGE, value) || !value.result) return 0;
    copyMessage(value, output);
    return 1;
}

int32_t MicaWin32kGetMessageW(MicaWin32kMessage* output, void* hwnd,
    uint32_t minimum, uint32_t maximum) {
    if (!output) return -1;
    MicaWin32kRequest input{};
    (void)request(MICANT_WIN32K_OP_PUMP_INPUT, input);
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = minimum;
    value.args[2] = maximum;
    value.args[3] = 1; // GetMessageW removes the retrieved message from the queue.
    if (!request(MICANT_WIN32K_OP_GET_MESSAGE, value)) return -1;
    if (!value.result) return -2; // Internal no-message result; User32 performs the wait.
    copyMessage(value, output);
    return value.messageId == 0x0012 ? 0 : 1;
}

int32_t MicaWin32kGetClientRect(void* hwnd, MicaWin32kRect* output) {
    if (!output) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    if (!request(MICANT_WIN32K_OP_GET_CLIENT_RECT, value) || !value.result) return 0;
    *output = value.rect;
    return 1;
}

int32_t MicaWin32kGetWindowRect(void* hwnd, MicaWin32kRect* output) {
    if (!output) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    if (!request(MICANT_WIN32K_OP_GET_WINDOW_RECT, value) || !value.result) return 0;
    *output = value.rect;
    return 1;
}

int32_t MicaWin32kAdjustWindowRectEx(MicaWin32kRect* rectangle,
    uint32_t style, int32_t hasMenu, uint32_t exStyle) {
    (void)exStyle;
    if (!rectangle || (hasMenu != 0 && hasMenu != 1)) return 0;
    MicaWin32kRequest value{};
    value.args[0] = style;
    value.args[1] = static_cast<uint32_t>(hasMenu);
    value.rect = *rectangle;
    if (!request(MICANT_WIN32K_OP_ADJUST_WINDOW_RECT, value) || !value.result)
        return 0;
    *rectangle = value.rect;
    return 1;
}

int32_t MicaWin32kEnumWindows(void* callback, intptr_t parameter) {
    if (!callback) return 0;
    using Callback = int32_t (*)(void*, intptr_t);
    auto enumerate = reinterpret_cast<Callback>(callback);
    uint64_t first = 0;
    for (;;) {
        MicaWin32kRequest value{};
        value.args[0] = first;
        value.args[1] = MICANT_WIN32K_ENUM_BATCH_CAPACITY;
        if (!request(MICANT_WIN32K_OP_ENUM_WINDOWS, value)) return 0;
        for (uint64_t index = 0; index < value.result; ++index) {
            if (!enumerate(reinterpret_cast<void*>(value.windowHandles[index]),
                           parameter)) return 0;
        }
        if (!value.result || value.result < MICANT_WIN32K_ENUM_BATCH_CAPACITY)
            break;
        if (first > UINT64_MAX - value.result) return 0;
        first += value.result;
    }
    return 1;
}

int32_t MicaWin32kGetWindowTextW(void* hwnd, wchar_t* output,
                                 int32_t capacity) {
    if (!output || capacity <= 0) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    if (!request(MICANT_WIN32K_OP_GET_WINDOW_TEXT, value)) return 0;
    const uint64_t count = value.result < static_cast<uint64_t>(capacity - 1)
        ? value.result : static_cast<uint64_t>(capacity - 1);
    for (uint64_t index = 0; index < count; ++index) output[index] = value.text[index];
    output[count] = 0;
    return static_cast<int32_t>(count);
}

int32_t MicaWin32kIsWindowVisible(void* hwnd) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    return request(MICANT_WIN32K_OP_IS_WINDOW_VISIBLE, value)
        ? static_cast<int32_t>(value.result) : 0;
}

int32_t MicaWin32kIsIconic(void* hwnd) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    return request(MICANT_WIN32K_OP_IS_ICONIC, value)
        ? static_cast<int32_t>(value.result) : 0;
}

void* MicaWin32kGetForegroundWindow() {
    MicaWin32kRequest value{};
    return request(MICANT_WIN32K_OP_GET_FOREGROUND_WINDOW, value)
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

void* MicaWin32kSetFocus(void* hwnd) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    return request(MICANT_WIN32K_OP_SET_FOCUS, value)
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

void* MicaWin32kGetFocus(void) {
    MicaWin32kRequest value{};
    return request(MICANT_WIN32K_OP_GET_FOCUS, value)
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

void* MicaWin32kGetCapture(void) {
    MicaWin32kRequest value{};
    return request(MICANT_WIN32K_OP_GET_CAPTURE, value)
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

void* MicaWin32kSetCapture(void* hwnd) {
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    return request(MICANT_WIN32K_OP_SET_CAPTURE, value)
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

int32_t MicaWin32kReleaseCapture(void) {
    MicaWin32kRequest value{};
    return request(MICANT_WIN32K_OP_RELEASE_CAPTURE, value)
        ? static_cast<int32_t>(value.result) : 0;
}

int32_t MicaWin32kSetWindowIcon(void* hwnd, uint32_t type, uint64_t icon,
                                uint64_t* previous) {
    if (previous) *previous = 0;
    if (!previous || type > 2) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = type;
    value.args[2] = icon;
    if (!request(MICANT_WIN32K_OP_SET_WINDOW_ICON, value)) return 0;
    *previous = value.result;
    return 1;
}

int32_t MicaWin32kSetWindowIconData(void* hwnd, uint32_t type, uint64_t icon,
    const uint8_t* pixels, uint32_t width, uint32_t height,
    uint32_t strideBytes, uint64_t* previous) {
    if (previous) *previous = 0;
    if (!previous || type >= 3u ||
        (icon && (!pixels || !width || !height ||
            height > SIZE_MAX / (static_cast<size_t>(width) * sizeof(uint32_t)) ||
            strideBytes != width * sizeof(uint32_t))) ||
        (!icon && (pixels || width || height || strideBytes))) return 0;
    MicaWin32kRequest value{};
    value.operation = MICANT_WIN32K_OP_SET_WINDOW_ICON_DATA;
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = type;
    value.args[2] = icon;
    value.args[3] = reinterpret_cast<uint64_t>(pixels);
    value.args[4] = width;
    value.args[5] = height;
    value.args[6] = strideBytes;
    if (!request(value.operation, value)) return 0;
    *previous = value.result;
    return 1;
}

int32_t MicaWin32kSetWindowTitleSurface(void* hwnd, const uint32_t* pixels,
    uint32_t width, uint32_t height, uint32_t stride) {
    if ((!pixels && (width || height || stride)) ||
        (pixels && (!width || !height ||
            stride < width ||
            height > SIZE_MAX / (static_cast<size_t>(stride) * sizeof(uint32_t))))) return 0;
    MicaWin32kRequest value{};
    value.operation = MICANT_WIN32K_OP_SET_WINDOW_TITLE_SURFACE;
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = reinterpret_cast<uint64_t>(pixels);
    value.args[2] = width;
    value.args[3] = height;
    value.args[4] = stride;
    return request(value.operation, value) ? 1 : 0;
}

uint64_t MicaWin32kGetWindowIcon(void* hwnd, uint32_t type) {
    if (type > 2) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = type;
    return request(MICANT_WIN32K_OP_GET_WINDOW_ICON, value) ? value.result : 0;
}

void* MicaWin32kGetDC(void* window) {
    auto* context = allocateContext(window, false);
    if (context && window) {
        MicaWin32kRequest value{};
        value.operation = MICANT_WIN32K_OP_BEGIN_WINDOW_UPDATE;
        value.args[0] = reinterpret_cast<uint64_t>(window);
        if (!request(value.operation, value) || !value.result) {
            context->active = false;
            return nullptr;
        }
        context->windowUpdateActive = true;
    }
    return context ? reinterpret_cast<void*>(context->handle) : nullptr;
}

int32_t MicaWin32kGetDCSurface(void* dc, uint32_t** pixels,
                               uint32_t* clientWidth, uint32_t* clientHeight,
                               uint32_t* stride) {
    if (pixels) *pixels = nullptr;
    if (clientWidth) *clientWidth = 0;
    if (clientHeight) *clientHeight = 0;
    if (stride) *stride = 0;
    auto* context = findContext(dc);
    if (!context || context->memory || !context->window || !pixels ||
        !clientWidth || !clientHeight || !stride) return 0;
    uint32_t surfaceWidth{}, surfaceHeight{};
    auto* surface = pixelsFor(*context, surfaceWidth, surfaceHeight);
    if (!surface || !surfaceWidth || !surfaceHeight) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(context->window);
    if (!request(MICANT_WIN32K_OP_GET_CLIENT_RECT, value) || !value.result ||
        value.rect.right <= value.rect.left || value.rect.bottom <= value.rect.top ||
        static_cast<uint32_t>(value.rect.right) > surfaceWidth ||
        static_cast<uint32_t>(value.rect.bottom) > surfaceHeight) return 0;
    *pixels = surface;
    *clientWidth = static_cast<uint32_t>(value.rect.right - value.rect.left);
    *clientHeight = static_cast<uint32_t>(value.rect.bottom - value.rect.top);
    *stride = context->surfacePitch;
    return 1;
}

int32_t MicaWin32kReleaseDC(void* window, void* dc) {
    auto* context = findContext(dc);
    if (!context || context->memory || context->window != window) return 0;
    bool released = true;
    if (context->windowUpdateActive) {
        MicaWin32kRequest value{};
        value.operation = MICANT_WIN32K_OP_END_WINDOW_UPDATE;
        value.args[0] = reinterpret_cast<uint64_t>(window);
        value.args[1] = context->windowUpdatePresented ? 1u : 0u;
        released = request(value.operation, value) && value.result;
    }
    context->active = false;
    context->windowUpdateActive = false;
    return released;
}

void* MicaWin32kCreateCompatibleDC(void*) {
    auto* context = allocateContext(nullptr, true);
    return context ? reinterpret_cast<void*>(context->handle) : nullptr;
}

int32_t MicaWin32kDeleteDC(void* dc) {
    auto* context = findContext(dc);
    if (!context || !context->memory) return 0;
    context->active = false;
    return 1;
}

void* MicaWin32kCreateCompatibleBitmap(void*, int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return nullptr;
    auto* bitmap = allocateBitmap(static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height));
    return bitmap ? reinterpret_cast<void*>(bitmap->handle) : nullptr;
}

void* MicaWin32kSelectObject(void* dc, void* object) {
    auto* context = findContext(dc);
    if (!context || !context->memory) return nullptr;
    auto* bitmap = findBitmap(object);
    const bool selectDefault =
        reinterpret_cast<uint64_t>(object) == context->defaultBitmapHandle;
    if (!bitmap && !selectDefault) return nullptr;
    if (bitmap) {
        const uint32_t index = static_cast<uint32_t>(bitmap - g_bitmaps) + 1;
        for (const auto& other : g_contexts) {
            if (other.active && &other != context && other.bitmap == index) {
                return nullptr;
            }
        }
    }
    void* previous = context->bitmap
        ? reinterpret_cast<void*>(g_bitmaps[context->bitmap - 1].handle)
        : reinterpret_cast<void*>(context->defaultBitmapHandle);
    context->bitmap = bitmap ? static_cast<uint32_t>(bitmap - g_bitmaps) + 1 : 0;
    return previous;
}

int32_t MicaWin32kBitBlt(void* destination, int32_t x, int32_t y,
    int32_t width, int32_t height, void* source, int32_t sourceX,
    int32_t sourceY, uint32_t rasterOperation) {
    constexpr uint32_t kSrcCopy = 0x00CC0020;
    constexpr uint32_t kAlphaSourceOver = MICANT_WIN32K_ROP_ALPHA_SOURCE_OVER;
    auto* dst = findContext(destination);
    auto* src = findContext(source);
    if (!dst || !src || (rasterOperation != kSrcCopy && rasterOperation != kAlphaSourceOver) ||
        width <= 0 || height <= 0) return 0;

    uint32_t dstWidth{}, dstHeight{}, srcWidth{}, srcHeight{};
    auto* dstPixels = pixelsFor(*dst, dstWidth, dstHeight);
    auto* srcPixels = pixelsFor(*src, srcWidth, srcHeight);
    if (!dstPixels || !srcPixels) return 0;
    const uint32_t dstPitch = dst->surfacePitch ? dst->surfacePitch : dstWidth;
    const uint32_t srcPitch = src->surfacePitch ? src->surfacePitch : srcWidth;
    const int64_t left = x;
    const int64_t top = y;
    const int64_t right = left + width;
    const int64_t bottom = top + height;
    if (right < left || bottom < top) return 0;
    for (int64_t row = 0; row < height; ++row) {
        const int64_t sy = static_cast<int64_t>(sourceY) + row;
        const int64_t dy = top + row;
        if (sy < 0 || sy >= srcHeight || dy < 0 || dy >= dstHeight) continue;
        for (int64_t column = 0; column < width; ++column) {
            const int64_t sx = static_cast<int64_t>(sourceX) + column;
            const int64_t dx = left + column;
            if (sx < 0 || sx >= srcWidth || dx < 0 || dx >= dstWidth) continue;
            uint32_t& target = dstPixels[static_cast<size_t>(dy) * dstPitch + dx];
            const uint32_t sourcePixel = srcPixels[static_cast<size_t>(sy) * srcPitch + sx];
            if (rasterOperation == kSrcCopy) {
                target = sourcePixel;
            } else {
                target = micant::shared::graphics::PremultipliedSourceOver(
                    sourcePixel, target);
            }
        }
    }
    return 1;
}

int32_t MicaWin32kFillRect(void* dc, const MicaWin32kRect* rectangle,
                           uint32_t colorRef) {
    auto* context = findContext(dc);
    if (!context || !rectangle ||
        rectangle->right <= rectangle->left || rectangle->bottom <= rectangle->top)
        return 0;
    uint32_t width{}, height{};
    uint32_t* pixels = pixelsFor(*context, width, height);
    if (!pixels || !width || !height) return 0;
    const uint32_t pitch = context->window ? context->surfacePitch : width;
    const int64_t left = std::max<int64_t>(0, rectangle->left);
    const int64_t top = std::max<int64_t>(0, rectangle->top);
    const int64_t right = std::min<int64_t>(width, rectangle->right);
    const int64_t bottom = std::min<int64_t>(height, rectangle->bottom);
    if (right <= left || bottom <= top) return 1;
    const uint32_t red = colorRef & 0xFFu;
    const uint32_t green = (colorRef >> 8) & 0xFFu;
    const uint32_t blue = (colorRef >> 16) & 0xFFu;
    const uint32_t pixel = 0xFF000000u | (red << 16) | (green << 8) | blue;
    for (int64_t y = top; y < bottom; ++y)
        std::fill(pixels + static_cast<size_t>(y) * pitch + left,
                  pixels + static_cast<size_t>(y) * pitch + right, pixel);
    return 1;
}

int32_t MicaWin32kBlendRect(void* dc, const MicaWin32kRect* rectangle,
                            uint32_t argb) {
    auto* context = findContext(dc);
    if (!context || !rectangle ||
        rectangle->right <= rectangle->left || rectangle->bottom <= rectangle->top)
        return 0;
    uint32_t width{}, height{};
    uint32_t* pixels = pixelsFor(*context, width, height);
    if (!pixels || !width || !height) return 0;
    const uint32_t pitch = context->window ? context->surfacePitch : width;
    const int64_t left = std::max<int64_t>(0, rectangle->left);
    const int64_t top = std::max<int64_t>(0, rectangle->top);
    const int64_t right = std::min<int64_t>(width, rectangle->right);
    const int64_t bottom = std::min<int64_t>(height, rectangle->bottom);
    if (right <= left || bottom <= top) return 1;
    const uint32_t alpha = argb >> 24;
    if (!alpha) return 1;
    const uint32_t inverse = 255u - alpha;
    const uint32_t red = (argb >> 16) & 0xFFu;
    const uint32_t green = (argb >> 8) & 0xFFu;
    const uint32_t blue = argb & 0xFFu;
    for (int64_t y = top; y < bottom; ++y) {
        for (int64_t x = left; x < right; ++x) {
            uint32_t& destination = pixels[static_cast<size_t>(y) * pitch + x];
            const uint32_t dstRed = (destination >> 16) & 0xFFu;
            const uint32_t dstGreen = (destination >> 8) & 0xFFu;
            const uint32_t dstBlue = destination & 0xFFu;
            const uint32_t outRed = (red * alpha + dstRed * inverse + 127u) / 255u;
            const uint32_t outGreen = (green * alpha + dstGreen * inverse + 127u) / 255u;
            const uint32_t outBlue = (blue * alpha + dstBlue * inverse + 127u) / 255u;
            const uint32_t dstAlpha = destination >> 24;
            const uint32_t outAlpha = alpha +
                (dstAlpha * inverse + 127u) / 255u;
            destination = (outAlpha << 24) | (outRed << 16) |
                (outGreen << 8) | outBlue;
        }
    }
    return 1;
}

int32_t MicaWin32kPresentDCRect(void* dc, const MicaWin32kRect* dirtyRect) {
    auto* context = findContext(dc);
    if (!context) return 0;
    uint32_t width{}, height{};
    auto* pixels = pixelsFor(*context, width, height);
    if (!pixels) return 0;
    if (context->window) {
        MicaWin32kRequest value{};
        value.operation = MICANT_WIN32K_OP_PRESENT_WINDOW;
        value.args[0] = reinterpret_cast<uint64_t>(context->window);
        if (dirtyRect) {
            value.rect = *dirtyRect;
            value.args[1] = 1;
        }
        const bool presented = request(value.operation, value) && value.result;
        if (presented && context->windowUpdateActive)
            context->windowUpdatePresented = true;
        return presented ? 1 : 0;
    }
    if (!context->memory) return 0;
    return presentSurface(pixels, width, height, 0, 0,
                          context->surfacePitch ? context->surfacePitch : width);
}

int32_t MicaWin32kPresentDC(void* dc) {
    return MicaWin32kPresentDCRect(dc, nullptr);
}

int32_t MicaWin32kDeleteObject(void* object) {
    auto* bitmap = findBitmap(object);
    if (!bitmap) return 0;
    const uint32_t index = static_cast<uint32_t>(bitmap - g_bitmaps) + 1;
    for (auto& context : g_contexts) {
        if (context.active && context.bitmap == index) return 0;
    }
    bitmap->active = false;
    releaseUserPixels(bitmap->pixels);
    return 1;
}

void* MicaWin32kGetStockObject(int32_t) { return nullptr; }

void* MicaWin32kCreateDibSection(void*, int32_t width, int32_t height,
                                 void** bits) {
    if (bits) *bits = nullptr;
    if (width <= 0 || height <= 0) return nullptr;
    auto* bitmap = allocateBitmap(static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height));
    if (!bitmap) return nullptr;
    if (bits) *bits = bitmap->pixels;
    return reinterpret_cast<void*>(bitmap->handle);
}

int32_t MicaWin32kGetCursorPos(MicaWin32kPoint* output) {
    if (!output) return 0;
    MicaWin32kRequest value{};
    if (!request(MICANT_WIN32K_OP_GET_CURSOR_POS, value) || !value.result) return 0;
    output->x = value.rect.left;
    output->y = value.rect.top;
    return 1;
}

int32_t MicaWin32kGetClipCursor(MicaWin32kRect* output) {
    if (!output) return 0;
    MicaWin32kRequest value{};
    if (!request(MICANT_WIN32K_OP_GET_CURSOR_CLIP, value) || !value.result) return 0;
    *output = value.rect;
    return 1;
}

int32_t MicaWin32kSetCursorPos(int32_t x, int32_t y) {
    MicaWin32kRequest value{};
    value.args[0] = static_cast<uint64_t>(static_cast<int64_t>(x));
    value.args[1] = static_cast<uint64_t>(static_cast<int64_t>(y));
    return request(MICANT_WIN32K_OP_SET_CURSOR_POS, value) && value.result ? 1 : 0;
}

int32_t MicaWin32kClipCursor(const MicaWin32kRect* rect) {
    MicaWin32kRequest value{};
    value.args[0] = rect ? 1 : 0;
    if (rect) value.rect = *rect;
    return request(MICANT_WIN32K_OP_SET_CURSOR_CLIP, value) && value.result ? 1 : 0;
}

int32_t MicaWin32kShowCursor(int32_t show) {
    if (show != 0 && show != 1) return INT32_MIN;
    MicaWin32kRequest value{};
    value.args[0] = show ? 1 : static_cast<uint64_t>(static_cast<int64_t>(-1));
    if (!request(MICANT_WIN32K_OP_SHOW_CURSOR, value)) return INT32_MIN;
    return static_cast<int32_t>(static_cast<int64_t>(value.result));
}

void* MicaWin32kSetCursor(void* cursor, const uint32_t* pixels, uint32_t width,
                          uint32_t height, uint32_t pitch,
                          uint32_t hotX, uint32_t hotY) {
    if (cursor && (!pixels || !width || !height ||
        pitch < width ||
        height > SIZE_MAX / (static_cast<size_t>(pitch) * sizeof(uint32_t)) ||
        hotX >= width || hotY >= height)) return nullptr;
    MicaWin32kCursorImage image{};
    image.size = sizeof(image);
    image.version = MICANT_WIN32K_CURSOR_IMAGE_VERSION;
    image.cursorHandle = reinterpret_cast<uint64_t>(cursor);
    image.width = cursor ? width : 0; image.height = cursor ? height : 0;
    image.pitch = cursor ? pitch : 0;
    image.hotX = cursor ? hotX : 0; image.hotY = cursor ? hotY : 0;
    image.pixelsAddress = cursor ? reinterpret_cast<uint64_t>(pixels) : 0;
    image.pixelSizeBytes = cursor
        ? static_cast<uint64_t>(pitch) * height * sizeof(uint32_t) : 0;
    if (MicaWin32kApplyCursorImage(&image) != 0)
        return nullptr;
    return reinterpret_cast<void*>(image.previousCursorHandle);
}

void* MicaWin32kGetCursor(void) {
    MicaWin32kRequest value{};
    return request(MICANT_WIN32K_OP_GET_CURSOR_HANDLE, value)
        ? reinterpret_cast<void*>(value.result) : nullptr;
}

int32_t MicaWin32kApplyCursorImage(MicaWin32kCursorImage* image) {
    if (!image) return -1;
    return static_cast<int32_t>(micant::sys::invokeUserService(
        0x87, reinterpret_cast<uint64_t>(image)));
}

int32_t MicaWin32kGetCursorInfo(MicaWin32kCursorInfo* output) {
    if (!output) return 0;
    MicaWin32kRequest value{};
    if (!request(MICANT_WIN32K_OP_GET_CURSOR_INFO, value)) return 0;
    output->size = sizeof(*output);
    output->flags = value.flags;
    output->cursorHandle = value.result;
    output->position = {value.rect.left, value.rect.top};
    return 1;
}

int32_t MicaWin32kSetThemeSnapshot(const MicaThemeSnapshot* snapshot,
                                   const MicaThemeCaptionImageSet* captionImages) {
    if (!snapshot || !MicaThemeSnapshotIsValid(snapshot, sizeof(*snapshot))) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(snapshot);
    value.args[1] = reinterpret_cast<uint64_t>(captionImages);
    return request(MICANT_WIN32K_OP_SET_THEME_SNAPSHOT, value) && value.result == 1;
}

int32_t MicaWin32kGetThemeSnapshot(MicaThemeSnapshot* snapshot) {
    if (!snapshot) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(snapshot);
    return request(MICANT_WIN32K_OP_GET_THEME_SNAPSHOT, value) && value.result == 1 &&
           MicaThemeSnapshotIsValid(snapshot, sizeof(*snapshot));
}

int32_t MicaWin32kSetLiquidGlass(void* hwnd,
                                const MicaLiquidGlassDescriptor* descriptor) {
    if (!hwnd || !descriptor) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.liquidGlass = *descriptor;
    return request(MICANT_WIN32K_OP_SET_LIQUID_GLASS, value) && value.result == 1;
}

}
