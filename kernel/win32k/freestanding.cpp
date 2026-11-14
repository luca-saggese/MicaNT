#include "micant/km/win32k/freestanding.hpp"
#include "micant/shared/compositor_surface.h"
#include <atomic>

namespace micant::win32k::freestanding {
namespace {

constexpr size_t kWindowCapacity = MICANT_WIN32K_WINDOW_CAPACITY;
constexpr size_t kClassCapacity = 32;
constexpr size_t kMessageCapacity = 256;
constexpr uint64_t kHandleTag = 0x4D4E570000000000ULL;
constexpr uint32_t kWmQuit = 0x0012;
constexpr uint32_t kWmMove = 0x0003;
constexpr uint32_t kWmSize = 0x0005;
constexpr uint32_t kWmNcCreate = 0x0081;
constexpr uint32_t kWmActivate = 0x0006;
constexpr uint32_t kWmSetFocus = 0x0007;
constexpr uint32_t kWmMouseMove = 0x0200;
constexpr uint32_t kWmLButtonDown = 0x0201;
constexpr uint32_t kWmLButtonUp = 0x0202;
constexpr uint32_t kWmRButtonDown = 0x0204;
constexpr uint32_t kWmRButtonUp = 0x0205;
constexpr uint32_t kWmMButtonDown = 0x0207;
constexpr uint32_t kWmMButtonUp = 0x0208;
constexpr uint32_t kWmKeyDown = 0x0100;
constexpr uint32_t kWmKeyUp = 0x0101;
constexpr uint32_t kWmChar = 0x0102;
constexpr int32_t kSwShowMinimized = 2;
constexpr int32_t kSwMinimize = 6;
constexpr int32_t kGwlpWndProc = -4;
constexpr int32_t kGwlpUserData = -21;
std::atomic_flag g_stateLock = ATOMIC_FLAG_INIT;

class StateLockGuard {
public:
    StateLockGuard() noexcept {
        while (g_stateLock.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#elif defined(__aarch64__)
            __asm__ __volatile__("yield");
#endif
        }
    }

    ~StateLockGuard() noexcept {
        g_stateLock.clear(std::memory_order_release);
    }

    StateLockGuard(const StateLockGuard&) = delete;
    StateLockGuard& operator=(const StateLockGuard&) = delete;
};

struct WindowClass {
    uint16_t name[MICANT_WIN32K_TEXT_CAPACITY]{};
    uint16_t atom{};
    uint64_t windowProcedure{};
    uint64_t ownerProcessId{};
    bool active{};
};

struct Window {
    uint16_t title[MICANT_WIN32K_TEXT_CAPACITY]{};
    uint64_t handle{};
    uint64_t parent{};
    uint64_t menu{};
    uint64_t windowProcedure{};
    intptr_t userData{};
    int32_t x{}, y{}, width{}, height{};
    uint32_t style{}, extendedStyle{};
    uint16_t classAtom{};
    uint64_t ownerProcessId{};
    uint64_t zOrder{};
    bool active{}, visible{}, iconic{};
};

struct QueuedMessage {
    uint64_t hwnd{}, wParam{};
    int64_t lParam{};
    uint32_t message{}, time{};
    int32_t x{}, y{};
    uint64_t ownerProcessId{};
    bool active{};
};

WindowClass g_classes[kClassCapacity]{};
Window g_windows[kWindowCapacity]{};
QueuedMessage g_messages[kMessageCapacity]{};
uint16_t g_nextAtom{1};
uint32_t g_nextGeneration{1};
uint32_t g_messageTime{};
uint64_t g_foregroundWindow{};
uint64_t g_nextZOrder{};
int32_t g_mouseX{-1}, g_mouseY{-1};
uint32_t g_mouseButtons{};
MicaCompositorMetadataStore* g_compositorStore{};
void (*g_compositorPublishCallback)() noexcept{};

bool equalText(const uint16_t* first, const uint16_t* second) noexcept {
    for (size_t index = 0; index < MICANT_WIN32K_TEXT_CAPACITY; ++index) {
        if (first[index] != second[index]) return false;
        if (first[index] == 0) return true;
    }
    return false;
}

bool terminatedText(const uint16_t* text) noexcept {
    for (size_t index = 0; index < MICANT_WIN32K_TEXT_CAPACITY; ++index) {
        if (text[index] == 0) return true;
    }
    return false;
}

void copyText(uint16_t* destination, const uint16_t* source) noexcept {
    for (size_t index = 0; index < MICANT_WIN32K_TEXT_CAPACITY; ++index) {
        destination[index] = source[index];
        if (source[index] == 0) break;
    }
}

WindowClass* findClass(const uint16_t* name, uint64_t ownerProcessId) noexcept {
    for (auto& windowClass : g_classes) {
        if (windowClass.active && windowClass.ownerProcessId == ownerProcessId &&
            equalText(windowClass.name, name)) {
            return &windowClass;
        }
    }
    return nullptr;
}

Window* findWindow(uint64_t handle, uint64_t ownerProcessId) noexcept {
    if (!handle) return nullptr;
    for (auto& window : g_windows) {
        if (window.active && window.handle == handle &&
            window.ownerProcessId == ownerProcessId) return &window;
    }
    return nullptr;
}

Window* findWindowAny(uint64_t handle) noexcept {
    if (!handle) return nullptr;
    for (auto& window : g_windows)
        if (window.active && window.handle == handle) return &window;
    return nullptr;
}

uint64_t makeHandle(size_t index) noexcept {
    uint64_t generation = g_nextGeneration++;
    if (generation == 0 || generation > 0x00FFFFFFu) {
        g_nextGeneration = 2;
        generation = 1;
    }
    return kHandleTag | (generation << 8) | static_cast<uint64_t>(index + 1);
}

bool enqueue(const QueuedMessage& message) noexcept {
    for (auto& slot : g_messages) {
        if (!slot.active) {
            slot = message;
            slot.active = true;
            return true;
        }
    }
    return false;
}

uint64_t nextZOrder() noexcept {
    if (g_nextZOrder == UINT64_MAX) {
        uint64_t order = 0;
        for (auto& window : g_windows)
            if (window.active) window.zOrder = ++order;
        g_nextZOrder = order;
    }
    return ++g_nextZOrder;
}

Window* topmostVisibleWindow(int32_t x, int32_t y) noexcept {
    Window* topmost{};
    for (auto& window : g_windows) {
        const int64_t localX = static_cast<int64_t>(x) - window.x;
        const int64_t localY = static_cast<int64_t>(y) - window.y;
        if (!window.active || !window.visible || window.iconic || localX < 0 ||
            localY < 0 || localX >= window.width || localY >= window.height) continue;
        if (!topmost || window.zOrder > topmost->zOrder) topmost = &window;
    }
    return topmost;
}

Window* topmostVisibleWindow() noexcept {
    Window* topmost{};
    for (auto& window : g_windows) {
        if (!window.active || !window.visible || window.iconic) continue;
        if (!topmost || window.zOrder > topmost->zOrder) topmost = &window;
    }
    return topmost;
}

bool enqueueWindowMessage(const Window& window, uint32_t message,
                          uint64_t wParam, int64_t lParam,
                          int32_t x = 0, int32_t y = 0) noexcept {
    return enqueue(QueuedMessage{
        .hwnd = window.handle, .wParam = wParam, .lParam = lParam,
        .message = message, .time = ++g_messageTime,
        .x = x, .y = y,
        .ownerProcessId = window.ownerProcessId, .active = true});
}

bool messageMatches(const QueuedMessage& message,
                    const MicaWin32kRequest& request,
                    uint64_t ownerProcessId) noexcept {
    const uint64_t requestedWindow = request.args[0];
    const uint32_t minimum = static_cast<uint32_t>(request.args[1]);
    const uint32_t maximum = static_cast<uint32_t>(request.args[2]);
    return message.ownerProcessId == ownerProcessId &&
           (!requestedWindow || requestedWindow == message.hwnd) &&
           (!minimum || message.message >= minimum) &&
           (!maximum || message.message <= maximum);
}

bool dequeue(MicaWin32kRequest& request, uint64_t ownerProcessId) noexcept {
    for (auto& message : g_messages) {
        if (!message.active || !messageMatches(message, request, ownerProcessId)) continue;
        request.messageHwnd = message.hwnd;
        request.messageWParam = message.wParam;
        request.messageLParam = message.lParam;
        request.messageId = message.message;
        request.messageTime = message.time;
        request.messageX = message.x;
        request.messageY = message.y;
        if (request.args[3]) message.active = false;
        return true;
    }
    return false;
}

NtStatus registerClass(MicaWin32kRequest& request, uint64_t ownerProcessId) noexcept {
    if (!terminatedText(request.className) || request.className[0] == 0) {
        return NtStatus::InvalidParameter;
    }
    if (findClass(request.className, ownerProcessId)) return NtStatus::ObjectNameCollision;
    for (auto& windowClass : g_classes) {
        if (windowClass.active) continue;
        windowClass.active = true;
        windowClass.atom = g_nextAtom++;
        if (g_nextAtom == 0) g_nextAtom = 1;
        windowClass.windowProcedure = request.args[0];
        windowClass.ownerProcessId = ownerProcessId;
        copyText(windowClass.name, request.className);
        request.result = windowClass.atom;
        return NtStatus::Success;
    }
    return NtStatus::InsufficientResources;
}

NtStatus createWindow(MicaWin32kRequest& request, uint64_t ownerProcessId) noexcept {
    if (!terminatedText(request.className) || !terminatedText(request.title)) {
        return NtStatus::InvalidParameter;
    }
    const auto* windowClass = findClass(request.className, ownerProcessId);
    if (!windowClass) return NtStatus::ObjectNameNotFound;
    const int64_t width = static_cast<int64_t>(request.args[4]);
    const int64_t height = static_cast<int64_t>(request.args[5]);
    const int64_t x = static_cast<int64_t>(request.args[2]);
    const int64_t y = static_cast<int64_t>(request.args[3]);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
        x + width > INT32_MAX || y + height > INT32_MAX) {
        return NtStatus::InvalidParameter;
    }
    for (size_t index = 0; index < kWindowCapacity; ++index) {
        auto& window = g_windows[index];
        if (window.active) continue;
        window = {};
        window.active = true;
        window.handle = makeHandle(index);
        window.parent = request.args[6];
        window.menu = request.args[7];
        window.extendedStyle = static_cast<uint32_t>(request.args[0]);
        window.style = static_cast<uint32_t>(request.args[1]);
        window.x = static_cast<int32_t>(x);
        window.y = static_cast<int32_t>(y);
        window.width = static_cast<int32_t>(width);
        window.height = static_cast<int32_t>(height);
        window.classAtom = windowClass->atom;
        window.ownerProcessId = ownerProcessId;
        window.zOrder = nextZOrder();
        window.windowProcedure = windowClass->windowProcedure;
        window.visible = (window.style & 0x10000000u) != 0;
        copyText(window.title, request.title);
        request.result = window.handle;
        if (window.visible) g_foregroundWindow = window.handle;
        return NtStatus::Success;
    }
    return NtStatus::InsufficientResources;
}

} // namespace

NtStatus dispatchRequest(MicaWin32kRequest& request, uint64_t ownerProcessId) noexcept {
    StateLockGuard lock;
    request.result = 0;
    switch (request.operation) {
        case MICANT_WIN32K_OP_REGISTER_CLASS:
            return registerClass(request, ownerProcessId);
        case MICANT_WIN32K_OP_CREATE_WINDOW:
            return createWindow(request, ownerProcessId);
        case MICANT_WIN32K_OP_GET_WINDOW_SURFACE: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const size_t slot = static_cast<size_t>(window - g_windows);
            if (slot >= MICANT_COMPOSITOR_SURFACE_CAPACITY)
                return NtStatus::InsufficientResources;
            request.result = static_cast<uint64_t>(slot) + 1;
            request.rect = {0, 0, window->width, window->height};
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_PRESENT_WINDOW: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_DESTROY_WINDOW: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const bool wasForeground = g_foregroundWindow == window->handle;
            window->active = false;
            if (wasForeground) {
                g_foregroundWindow = 0;
                if (auto* next = topmostVisibleWindow()) {
                    g_foregroundWindow = next->handle;
                    (void)enqueueWindowMessage(*next, kWmActivate, 1, 0);
                    (void)enqueueWindowMessage(*next, kWmSetFocus, 0, 0);
                }
            }
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SHOW_WINDOW: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const int32_t command = static_cast<int32_t>(request.args[1]);
            const bool wasVisible = window->visible;
            window->visible = command != 0;
            window->iconic = command == kSwShowMinimized ||
                             command == kSwMinimize;
            if (window->visible) {
                window->zOrder = nextZOrder();
                g_foregroundWindow = window->handle;
            }
            request.result = wasVisible;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_WINDOW_POS: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const int32_t oldX = window->x, oldY = window->y;
            const int32_t oldWidth = window->width, oldHeight = window->height;
            const uint32_t flags = static_cast<uint32_t>(request.args[5]);
            int64_t x = window->x;
            int64_t y = window->y;
            int64_t width = window->width;
            int64_t height = window->height;
            if (!(flags & 0x0002u)) {
                x = static_cast<int64_t>(request.args[1]);
                y = static_cast<int64_t>(request.args[2]);
            }
            if (!(flags & 0x0001u)) {
                width = static_cast<int64_t>(request.args[3]);
                height = static_cast<int64_t>(request.args[4]);
            }
            if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
                x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
                x + width > INT32_MAX || y + height > INT32_MAX)
                return NtStatus::InvalidParameter;
            window->x = static_cast<int32_t>(x);
            window->y = static_cast<int32_t>(y);
            window->width = static_cast<int32_t>(width);
            window->height = static_cast<int32_t>(height);
            if (request.args[6] == 0) {
                window->zOrder = nextZOrder();
                g_foregroundWindow = window->handle;
            }
            if (window->x != oldX || window->y != oldY) {
                const uint32_t packedPosition =
                    (static_cast<uint32_t>(window->y) & 0xFFFFu) << 16 |
                    (static_cast<uint32_t>(window->x) & 0xFFFFu);
                (void)enqueueWindowMessage(*window, kWmMove, 0,
                    static_cast<int64_t>(packedPosition));
            }
            if (window->width != oldWidth || window->height != oldHeight) {
                const uint32_t packedSize =
                    (static_cast<uint32_t>(window->height) & 0xFFFFu) << 16 |
                    (static_cast<uint32_t>(window->width) & 0xFFFFu);
                (void)enqueueWindowMessage(*window, kWmSize, 0,
                    static_cast<int64_t>(packedSize));
            }
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_POST_MESSAGE: {
            if (request.args[0] && !findWindow(request.args[0], ownerProcessId)) {
                return NtStatus::InvalidHandle;
            }
            const QueuedMessage message{
                .hwnd = request.args[0],
                .wParam = request.args[2],
                .lParam = static_cast<int64_t>(request.args[3]),
                .message = static_cast<uint32_t>(request.args[1]),
                .time = ++g_messageTime,
                .x = 0,
                .y = 0,
                .ownerProcessId = ownerProcessId,
                .active = true,
            };
            if (!enqueue(message)) return NtStatus::InsufficientResources;
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SEND_MESSAGE:
            if (request.args[0] && !findWindow(request.args[0], ownerProcessId)) {
                return NtStatus::InvalidHandle;
            }
            return NtStatus::Success;
        case MICANT_WIN32K_OP_POST_QUIT:
            return enqueue(QueuedMessage{
                .wParam = request.args[0], .message = kWmQuit,
                .time = ++g_messageTime, .ownerProcessId = ownerProcessId,
                .active = true}) ? NtStatus::Success : NtStatus::InsufficientResources;
        case MICANT_WIN32K_OP_PEEK_MESSAGE:
        case MICANT_WIN32K_OP_GET_MESSAGE:
            if (dequeue(request, ownerProcessId)) {
                request.result = 1;
                return NtStatus::Success;
            }
            return request.operation == MICANT_WIN32K_OP_GET_MESSAGE
                ? NtStatus::Timeout : NtStatus::Success;
        case MICANT_WIN32K_OP_GET_CLIENT_RECT:
        case MICANT_WIN32K_OP_GET_WINDOW_RECT: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (request.operation == MICANT_WIN32K_OP_GET_WINDOW_RECT &&
                (window->x > INT32_MAX - window->width ||
                 window->y > INT32_MAX - window->height))
                return NtStatus::InvalidParameter;
            request.rect = request.operation == MICANT_WIN32K_OP_GET_CLIENT_RECT
                ? MicaWin32kRect{0, 0, window->width, window->height}
                : MicaWin32kRect{window->x, window->y,
                    window->x + window->width, window->y + window->height};
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_ENUM_WINDOWS: {
            for (const auto& window : g_windows) {
                if (!window.active || window.ownerProcessId != ownerProcessId || window.parent) continue;
                request.windowHandles[request.result++] = window.handle;
            }
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_WINDOW_TEXT: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            copyText(request.text, window->title);
            while (request.result < MICANT_WIN32K_TEXT_CAPACITY &&
                   request.text[request.result] != 0) {
                ++request.result;
            }
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_IS_WINDOW_VISIBLE: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            request.result = window->visible;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_IS_ICONIC: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            request.result = window->iconic;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_FOREGROUND_WINDOW:
            request.result = g_foregroundWindow;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_SET_FOCUS: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (!window->visible || window->iconic) return NtStatus::InvalidParameter;
            const uint64_t previousHandle = g_foregroundWindow;
            if (previousHandle != window->handle) {
                auto* previous = findWindowAny(previousHandle);
                if (previous && !enqueueWindowMessage(*previous, kWmActivate, 0, 0))
                    return NtStatus::InsufficientResources;
                g_foregroundWindow = window->handle;
                if (!enqueueWindowMessage(*window, kWmActivate, 1, 0) ||
                    !enqueueWindowMessage(*window, kWmSetFocus, 0, 0))
                    return NtStatus::InsufficientResources;
            }
            window->zOrder = nextZOrder();
            request.result = previousHandle;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_WINDOW_LONG: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const int32_t index = static_cast<int32_t>(request.args[1]);
            if (index == kGwlpUserData) {
                request.result = static_cast<uint64_t>(window->userData);
                window->userData = static_cast<intptr_t>(request.args[2]);
            } else if (index == kGwlpWndProc) {
                request.result = window->windowProcedure;
                window->windowProcedure = request.args[2];
            } else {
                return NtStatus::InvalidParameter;
            }
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_WINDOW_LONG: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const int32_t index = static_cast<int32_t>(request.args[1]);
            if (index == kGwlpUserData) {
                request.result = static_cast<uint64_t>(window->userData);
            } else if (index == kGwlpWndProc) {
                request.result = window->windowProcedure;
            } else {
                return NtStatus::InvalidParameter;
            }
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_DEF_WINDOW_PROC:
            if (request.args[0] && !findWindow(request.args[0], ownerProcessId)) {
                return NtStatus::InvalidHandle;
            }
            request.result = static_cast<uint32_t>(request.args[1]) == kWmNcCreate;
            return NtStatus::Success;
        default:
            return NtStatus::NotSupported;
    }
}

void releaseProcess(uint64_t ownerProcessId) noexcept {
    if (!ownerProcessId) return;
    auto publish = g_compositorPublishCallback;
    {
        StateLockGuard lock;
        for (auto& message : g_messages)
            if (message.active && message.ownerProcessId == ownerProcessId) message.active = false;
        for (auto& window : g_windows) {
            if (!window.active || window.ownerProcessId != ownerProcessId) continue;
            if (g_foregroundWindow == window.handle) g_foregroundWindow = 0;
            window = {};
        }
        for (auto& windowClass : g_classes)
            if (windowClass.active && windowClass.ownerProcessId == ownerProcessId) windowClass = {};
    }
    if (publish) publish();
}

bool dispatchMouseInput(const MicaWin32kInputEvent& event) noexcept {
    StateLockGuard lock;
    const int32_t x = event.x;
    const int32_t y = event.y;
    const uint32_t previousButtons = g_mouseButtons;
    const uint32_t currentButtons = event.buttons & 0x07u;
    const bool moved = x != g_mouseX || y != g_mouseY;
    g_mouseButtons = currentButtons;
    g_mouseX = x;
    g_mouseY = y;
    auto* target = topmostVisibleWindow(x, y);
    if (!target) return false;

    uint32_t keyState = 0;
    if (currentButtons & 0x01u) keyState |= 0x0001u;
    if (currentButtons & 0x02u) keyState |= 0x0002u;
    if (currentButtons & 0x04u) keyState |= 0x0010u;
    const auto localX64 = static_cast<int64_t>(x) - target->x;
    const auto localY64 = static_cast<int64_t>(y) - target->y;
    const uint32_t localX = static_cast<uint32_t>(localX64) & 0xFFFFu;
    const uint32_t localY = static_cast<uint32_t>(localY64) & 0xFFFFu;
    const int64_t packedPoint = static_cast<int64_t>((localY << 16) | localX);
    if (moved && !enqueueWindowMessage(*target, kWmMouseMove, keyState,
                                      packedPoint, x, y))
        return false;

    constexpr uint32_t buttonMasks[] = {0x01u, 0x02u, 0x04u};
    constexpr uint32_t downMessages[] = {
        kWmLButtonDown, kWmRButtonDown, kWmMButtonDown};
    constexpr uint32_t upMessages[] = {
        kWmLButtonUp, kWmRButtonUp, kWmMButtonUp};
    bool buttonDelivered = false;
    for (size_t index = 0; index < 3; ++index) {
        const uint32_t mask = buttonMasks[index];
        if ((previousButtons & mask) == (currentButtons & mask)) continue;
        if (currentButtons & mask) {
            target->zOrder = nextZOrder();
            if (g_foregroundWindow != target->handle) {
                auto* previous = findWindowAny(g_foregroundWindow);
                if (previous && !enqueueWindowMessage(*previous, kWmActivate, 0, 0))
                    return false;
                g_foregroundWindow = target->handle;
                if (!enqueueWindowMessage(*target, kWmActivate, 1, 0) ||
                    !enqueueWindowMessage(*target, kWmSetFocus, 0, 0)) return false;
            }
            if (!enqueueWindowMessage(*target, downMessages[index], keyState,
                                      packedPoint, x, y)) return false;
            buttonDelivered = true;
        } else if (!enqueueWindowMessage(*target, upMessages[index], keyState,
                                         packedPoint, x, y)) {
            return false;
        } else {
            buttonDelivered = true;
        }
    }
    return buttonDelivered;
}

bool dispatchKeyboardInput(const MicaWin32kKeyboardEvent& event) noexcept {
    StateLockGuard lock;
    auto* target = findWindowAny(g_foregroundWindow);
    if (!target || !target->visible || target->iconic || !event.virtualKey) return false;
    const bool down = (event.flags & 0x01u) != 0;
    const uint64_t lParam = (static_cast<uint64_t>(event.sequence & 0xFFu) << 16) |
                            (down ? 1u : ((1u << 30) | (1u << 31)));
    if (!enqueueWindowMessage(*target, down ? kWmKeyDown : kWmKeyUp,
                              event.virtualKey, static_cast<int64_t>(lParam))) return false;
    if (down && event.character) {
        if (!enqueueWindowMessage(*target, kWmChar, event.character,
                                  static_cast<int64_t>(lParam))) return false;
    }
    return true;
}

void bindCompositorStore(MicaCompositorMetadataStore* store,
                         void (*publishCallback)() noexcept) noexcept {
    StateLockGuard lock;
    g_compositorStore = store;
    g_compositorPublishCallback = publishCallback;
}

void publishCompositorSurfaces() noexcept {
    StateLockGuard lock;
    auto* store = g_compositorStore;
    if (!store) return;
    __atomic_add_fetch(&store->frameSequence, 1, __ATOMIC_RELEASE);
    for (size_t index = 0; index < MICANT_COMPOSITOR_SURFACE_CAPACITY; ++index) {
        auto& info = store->surfaces[index];
        info = {};
        if (index >= kWindowCapacity) continue;
        const auto& window = g_windows[index];
        if (!window.active) continue;
        info.window = window.handle;
        info.ownerProcessId = window.ownerProcessId;
        info.x = window.x;
        info.y = window.y;
        info.width = static_cast<uint32_t>(window.width);
        info.height = static_cast<uint32_t>(window.height);
        info.pitch = MICANT_COMPOSITOR_SCREEN_WIDTH;
        info.zOrder = static_cast<uint32_t>(window.zOrder);
        info.flags = MICANT_COMPOSITOR_SURFACE_ACTIVE |
            (window.visible ? MICANT_COMPOSITOR_SURFACE_VISIBLE : 0u) |
            (window.iconic ? MICANT_COMPOSITOR_SURFACE_ICONIC : 0u);
        info.pixelOffset = static_cast<uint32_t>(index * MICANT_COMPOSITOR_SURFACE_PIXELS);
        info.dirtyRight = window.width;
        info.dirtyBottom = window.height;
    }
    __atomic_add_fetch(&store->frameSequence, 1, __ATOMIC_RELEASE);
}

} // namespace micant::win32k::freestanding
