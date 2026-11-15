#ifndef MICANT_WIN32K_EXPORTS
#define MICANT_WIN32K_EXPORTS
#endif

#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/shared/compositor_surface.h"
#include "micant/shared/pixel_composite.h"

#include <cstdint>
#include <algorithm>

#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

namespace {

constexpr uint32_t kScreenWidth = 1024;
constexpr uint32_t kScreenHeight = 768;
constexpr size_t kScreenPixels =
    static_cast<size_t>(kScreenWidth) * kScreenHeight;
constexpr size_t kPixelCapacity = 4u * 1024u * 1024u;
constexpr size_t kDcCapacity = 32;
constexpr size_t kBitmapCapacity = 16;
constexpr uint64_t kHandleTag = 0x4D574B0000000000ULL;
constexpr size_t kSurfacePixels = MICANT_COMPOSITOR_SURFACE_PIXELS;

struct DeviceContext {
    uint64_t handle{};
    uint64_t defaultBitmapHandle{};
    void* window{};
    uint32_t bitmap{};
    uint32_t defaultPixel{};
    bool memory{};
    bool active{};
};

struct Bitmap {
    uint64_t handle{};
    uint32_t pixelOffset{};
    uint32_t width{}, height{};
    bool active{};
};

uint32_t g_screenPixels[kScreenPixels]{};
uint32_t g_pixelArena[kPixelCapacity]{};
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

Bitmap* allocateBitmap(uint32_t width, uint32_t height) noexcept {
    if (!width || !height || width > kScreenWidth || height > kScreenHeight ||
        static_cast<size_t>(width) > kPixelCapacity / height) {
        return nullptr;
    }
    const size_t count = static_cast<size_t>(width) * height;
    for (auto& bitmap : g_bitmaps) {
        if (bitmap.active) continue;
        uint32_t offset = 0;
        for (;;) {
            bool collided = false;
            size_t next = offset;
            for (const auto& other : g_bitmaps) {
                if (!other.active) continue;
                const size_t otherEnd = static_cast<size_t>(other.pixelOffset) +
                                        static_cast<size_t>(other.width) * other.height;
                if (offset < otherEnd && other.pixelOffset < offset + count) {
                    offset = static_cast<uint32_t>(otherEnd);
                    next = offset;
                    collided = true;
                    break;
                }
            }
            if (!collided) break;
            if (next > kPixelCapacity || count > kPixelCapacity - next) return nullptr;
        }
        if (offset > kPixelCapacity || count > kPixelCapacity - offset) return nullptr;
        bitmap = {};
        bitmap.handle = makeHandle(2, &bitmap - g_bitmaps);
        bitmap.pixelOffset = offset;
        bitmap.width = width;
        bitmap.height = height;
        bitmap.active = true;
        auto* pixels = g_pixelArena + offset;
        for (size_t index = 0; index < count; ++index) pixels[index] = 0;
        return &bitmap;
    }
    return nullptr;
}

uint32_t* pixelsFor(DeviceContext& context, uint32_t& width,
                    uint32_t& height) noexcept {
    if (!context.memory && context.window) {
        MicaWin32kRequest value{};
        value.args[0] = reinterpret_cast<uint64_t>(context.window);
        if (!request(MICANT_WIN32K_OP_GET_WINDOW_SURFACE, value) ||
            !value.result || value.result > MICANT_COMPOSITOR_SURFACE_CAPACITY ||
            value.rect.right <= value.rect.left || value.rect.bottom <= value.rect.top ||
            value.rect.right > static_cast<int32_t>(MICANT_COMPOSITOR_SCREEN_WIDTH) ||
            value.rect.bottom > static_cast<int32_t>(MICANT_COMPOSITOR_SCREEN_HEIGHT))
            return nullptr;
        width = static_cast<uint32_t>(value.rect.right - value.rect.left);
        height = static_cast<uint32_t>(value.rect.bottom - value.rect.top);
        return reinterpret_cast<uint32_t*>(MICANT_COMPOSITOR_PIXELS_BASE) +
            (value.result - 1) * kSurfacePixels;
    }
    if (context.bitmap) {
        auto& bitmap = g_bitmaps[context.bitmap - 1];
        if (!bitmap.active) return nullptr;
        width = bitmap.width;
        height = bitmap.height;
        return g_pixelArena + bitmap.pixelOffset;
    }
    if (context.memory) {
        width = 1;
        height = 1;
        return &context.defaultPixel;
    }
    width = kScreenWidth;
    height = kScreenHeight;
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
                    uint32_t height, uint32_t dstX, uint32_t dstY) noexcept {
    if (!pixels || !width || !height || dstX >= kScreenWidth ||
        dstY >= kScreenHeight || width > kScreenWidth - dstX ||
        height > kScreenHeight - dstY) {
        return false;
    }
    micant::um::UserPresentSurfaceParams64 params{};
    params.surfaceBufferVa = reinterpret_cast<uint64_t>(pixels);
    params.dstX = dstX;
    params.dstY = dstY;
    params.width = width;
    params.height = height;
    params.pitch = width;
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

uint16_t MicaWin32kRegisterClassW(const wchar_t* className, void* procedure) {
    MicaWin32kRequest value{};
    if (!copyText(value.className, className)) return 0;
    value.args[0] = reinterpret_cast<uint64_t>(procedure);
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

int32_t MicaWin32kEnumWindows(void* callback, intptr_t parameter) {
    if (!callback) return 0;
    MicaWin32kRequest value{};
    if (!request(MICANT_WIN32K_OP_ENUM_WINDOWS, value)) return 0;
    using Callback = int32_t (*)(void*, intptr_t);
    auto enumerate = reinterpret_cast<Callback>(callback);
    for (uint64_t index = 0; index < value.result; ++index) {
        if (!enumerate(reinterpret_cast<void*>(value.windowHandles[index]),
                       parameter)) return 0;
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

uint64_t MicaWin32kGetWindowIcon(void* hwnd, uint32_t type) {
    if (type > 2) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(hwnd);
    value.args[1] = type;
    return request(MICANT_WIN32K_OP_GET_WINDOW_ICON, value) ? value.result : 0;
}

void* MicaWin32kGetDC(void* window) {
    auto* context = allocateContext(window, false);
    return context ? reinterpret_cast<void*>(context->handle) : nullptr;
}

int32_t MicaWin32kReleaseDC(void* window, void* dc) {
    auto* context = findContext(dc);
    if (!context || context->memory || context->window != window) return 0;
    context->active = false;
    return 1;
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
    const uint32_t dstPitch = dst->window
        ? MICANT_COMPOSITOR_SCREEN_WIDTH : dstWidth;
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
            const uint32_t sourcePixel = srcPixels[static_cast<size_t>(sy) * srcWidth + sx];
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

int32_t MicaWin32kPresentDC(void* dc) {
    auto* context = findContext(dc);
    if (!context) return 0;
    uint32_t width{}, height{};
    auto* pixels = pixelsFor(*context, width, height);
    if (!pixels) return 0;
    if (context->window) {
        MicaWin32kRequest value{};
        value.operation = MICANT_WIN32K_OP_PRESENT_WINDOW;
        value.args[0] = reinterpret_cast<uint64_t>(context->window);
        return request(value.operation, value) && value.result ? 1 : 0;
    }
    if (!context->memory) return 0;
    return presentSurface(pixels, width, height, 0, 0);
}

int32_t MicaWin32kDeleteObject(void* object) {
    auto* bitmap = findBitmap(object);
    if (!bitmap) return 0;
    const uint32_t index = static_cast<uint32_t>(bitmap - g_bitmaps) + 1;
    for (auto& context : g_contexts) {
        if (context.active && context.bitmap == index) return 0;
    }
    bitmap->active = false;
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
    if (bits) *bits = g_pixelArena + bitmap->pixelOffset;
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
        width > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
        height > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION || pitch < width ||
        hotX >= width || hotY >= height)) return nullptr;
    MicaWin32kCursorImage image{};
    image.size = sizeof(image);
    image.version = MICANT_WIN32K_CURSOR_IMAGE_VERSION;
    image.cursorHandle = reinterpret_cast<uint64_t>(cursor);
    image.width = width; image.height = height;
    image.pitch = cursor ? width : 0;
    image.hotX = cursor ? hotX : 0; image.hotY = cursor ? hotY : 0;
    if (cursor) {
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                image.pixels[static_cast<size_t>(y) * width + x] = pixels[static_cast<size_t>(y) * pitch + x];
    }
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

int32_t MicaWin32kSetThemeSnapshot(const MicaThemeSnapshot* snapshot) {
    if (!snapshot || !MicaThemeSnapshotIsValid(snapshot, sizeof(*snapshot))) return 0;
    MicaWin32kRequest value{};
    value.args[0] = reinterpret_cast<uint64_t>(snapshot);
    return request(MICANT_WIN32K_OP_SET_THEME_SNAPSHOT, value) && value.result == 1;
}

}
