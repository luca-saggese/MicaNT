#include "micant/km/win32k/freestanding.hpp"
#include "micant/shared/compositor_surface.h"
#include "micant/km/core/sync_baremetal.hpp"
#include <algorithm>
#include <atomic>
#include <climits>

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
constexpr uint32_t kWmSetCursor = 0x0020;
constexpr uint32_t kWmKeyDown = 0x0100;
constexpr uint32_t kWmKeyUp = 0x0101;
constexpr uint32_t kWmChar = 0x0102;
constexpr uint32_t kWmClose = 0x0010;
constexpr int32_t kSwShowMinimized = 2;
constexpr int32_t kSwMinimize = 6;
constexpr int32_t kSwRestore = 9;
constexpr int32_t kGwlpWndProc = -4;
constexpr int32_t kGwlpUserData = -21;
constexpr uint32_t kWsChild = 0x40000000u;
constexpr uint32_t kWsBorder = 0x00800000u;
constexpr uint32_t kWsDlgFrame = 0x00400000u;
constexpr uint32_t kWsCaption = kWsBorder | kWsDlgFrame;
constexpr uint32_t kWsThickFrame = 0x00040000u;
constexpr int32_t kScreenWidth = MICANT_COMPOSITOR_SCREEN_WIDTH;
constexpr int32_t kScreenHeight = MICANT_COMPOSITOR_SCREEN_HEIGHT;
std::atomic_flag g_stateLock = ATOMIC_FLAG_INIT;
sync::detail::BootstrapEvent g_compositorWakeEvent{true, false};
MicaCompositorDirtyRect g_pendingDirtyRegions[MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY]{};
uint32_t g_pendingDirtyRegionCount{};
bool g_pendingFullRedraw{true};
bool g_damageChangedSinceLastPublish{true};

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
    uint64_t icons[3]{};
    intptr_t userData{};
    int32_t x{}, y{}, width{}, height{};
    int32_t restoreX{}, restoreY{}, restoreWidth{}, restoreHeight{};
    uint32_t style{}, extendedStyle{};
    uint64_t contentGeneration{};
    uint32_t activeWriters{};
    struct WriterOwner { uint64_t processId{}; uint32_t count{}; };
    WriterOwner writerOwners[MICANT_COMPOSITOR_SURFACE_CAPACITY]{};
    int32_t dirtyLeft{}, dirtyTop{}, dirtyRight{}, dirtyBottom{};
    uint16_t classAtom{};
    uint64_t ownerProcessId{};
    uint64_t zOrder{};
    bool active{}, visible{}, iconic{}, maximized{};
};

struct QueuedMessage {
    uint64_t hwnd{}, wParam{};
    int64_t lParam{};
    uint32_t message{}, time{};
    int32_t x{}, y{};
    uint64_t ownerProcessId{};
    bool active{};
};

enum class PointerGestureMode : uint8_t { None, Move, Resize };
struct PointerGesture {
    uint64_t window{};
    int32_t hitTest{};
    int32_t startX{}, startY{};
    int32_t windowX{}, windowY{}, windowWidth{}, windowHeight{};
    PointerGestureMode mode{PointerGestureMode::None};
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
bool g_mousePositionValid{};
MicaWin32kRect g_cursorClip{0, 0, kScreenWidth, kScreenHeight};
uint64_t g_cursorClipOwner{};
int32_t g_cursorDisplayCount{1};
uint64_t g_activeCursorHandle{};
uint64_t g_activeCursorOwner{};
uint32_t g_activeCursorWidth{16}, g_activeCursorHeight{16}, g_activeCursorPitch{16};
uint32_t g_activeCursorHotX{}, g_activeCursorHotY{};
uint32_t g_activeCursorPixels[MICANT_COMPOSITOR_CURSOR_MAX_PIXELS]{};
bool g_defaultCursorInitialized{};
constexpr uint16_t kDefaultArrowRows[16]{
    0x8000,0xC000,0xE000,0xF000,0xF800,0xFC00,0xFE00,0xFF00,
    0xFF80,0xFCC0,0xCCC0,0x86C0,0x06C0,0x0360,0x0360,0x0000};

void resetCursorImage() noexcept {
    g_activeCursorHandle = 0;
    g_activeCursorOwner = 0;
    g_activeCursorWidth = g_activeCursorHeight = g_activeCursorPitch = 16;
    g_activeCursorHotX = g_activeCursorHotY = 0;
    for (size_t i = 0; i < 16 * 16; ++i) g_activeCursorPixels[i] = 0;
    for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x) {
        if (!(kDefaultArrowRows[y] & (0x8000u >> x))) continue;
        const bool edge = x == 0 || y == 0 ||
            !(kDefaultArrowRows[y] & (0x8000u >> (x - 1))) ||
            (y && !(kDefaultArrowRows[y - 1] & (0x8000u >> x)));
        g_activeCursorPixels[y * 16 + x] = edge ? 0xFF000000u : 0xFFFFFFFFu;
    }
}
uint64_t g_captionCapture{};
uint8_t g_pressedCaptionButton{};
PointerGesture g_pointerGesture{};
uint64_t g_cursorHitWindow{};
int32_t g_cursorHitTest{};
MicaCompositorMetadataStore* g_compositorStore{};
MicaThemeSnapshot g_themeSnapshot{};
uint64_t g_themeGeneration{};

int32_t frameInset(const Window& window) noexcept {
    if (window.parent || (window.style & kWsChild)) return 0;
    if (window.style & kWsThickFrame) return 5;
    return (window.style & (kWsBorder | kWsDlgFrame)) ? 1 : 0;
}

int32_t captionHeight(const Window& window) noexcept {
    if (window.parent || (window.style & kWsChild) || !(window.style & kWsCaption))
        return 0;
    constexpr size_t headerbarIndex = 13u * MICANT_THEME_STATE_COUNT;
    const auto& headerbar = g_themeSnapshot.styles[headerbarIndex];
    if (MicaThemeSnapshotIsValid(&g_themeSnapshot, sizeof(g_themeSnapshot)) &&
        (headerbar.flags & MICANT_THEME_HAS_MIN_HEIGHT) &&
        headerbar.minHeight >= 24 && headerbar.minHeight <= 96)
        return headerbar.minHeight;
    return 28;
}

void clearPendingDamage() noexcept {
    g_pendingDirtyRegionCount = 0;
    g_pendingFullRedraw = false;
    for (auto& window : g_windows) {
        window.dirtyLeft = window.dirtyTop = 0;
        window.dirtyRight = window.dirtyBottom = 0;
    }
}

void markDamageRect(int64_t left, int64_t top,
                    int64_t right, int64_t bottom) noexcept {
    left = std::max<int64_t>(left, 0);
    top = std::max<int64_t>(top, 0);
    right = std::min<int64_t>(right, kScreenWidth);
    bottom = std::min<int64_t>(bottom, kScreenHeight);
    if (left >= right || top >= bottom) return;
    g_damageChangedSinceLastPublish = true;
    if (g_pendingFullRedraw) return;

    MicaCompositorDirtyRect incoming{static_cast<int32_t>(left),
        static_cast<int32_t>(top), static_cast<int32_t>(right),
        static_cast<int32_t>(bottom)};
    for (uint32_t index = 0; index < g_pendingDirtyRegionCount;) {
        auto& existing = g_pendingDirtyRegions[index];
        const bool separated = incoming.right < existing.left ||
            existing.right < incoming.left || incoming.bottom < existing.top ||
            existing.bottom < incoming.top;
        if (separated) {
            ++index;
            continue;
        }
        incoming.left = std::min(incoming.left, existing.left);
        incoming.top = std::min(incoming.top, existing.top);
        incoming.right = std::max(incoming.right, existing.right);
        incoming.bottom = std::max(incoming.bottom, existing.bottom);
        g_pendingDirtyRegions[index] =
            g_pendingDirtyRegions[--g_pendingDirtyRegionCount];
        index = 0;
    }
    if (g_pendingDirtyRegionCount == MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY) {
        g_pendingDirtyRegionCount = 0;
        g_pendingFullRedraw = true;
        return;
    }
    g_pendingDirtyRegions[g_pendingDirtyRegionCount++] = incoming;
}

void markDamageFull() noexcept {
    g_damageChangedSinceLastPublish = true;
    g_pendingDirtyRegionCount = 0;
    g_pendingFullRedraw = true;
}

void markWindowBounds(const Window& window) noexcept {
    if (!window.active || !window.visible || window.iconic) return;
    const int64_t left = window.x;
    const int64_t top = window.y;
    markDamageRect(left, top, left + window.width, top + window.height);
}

void markCursorBounds(int32_t x, int32_t y) noexcept {
    if (!g_mousePositionValid || g_cursorDisplayCount < 0 ||
        !g_activeCursorWidth || !g_activeCursorHeight) return;
    const int64_t left = static_cast<int64_t>(x) - g_activeCursorHotX;
    const int64_t top = static_cast<int64_t>(y) - g_activeCursorHotY;
    markDamageRect(left, top, left + g_activeCursorWidth,
                   top + g_activeCursorHeight);
}

void markWindowClientDamage(Window& window, int64_t left, int64_t top,
                            int64_t right, int64_t bottom) noexcept {
    const int64_t frame = frameInset(window);
    const int64_t caption = captionHeight(window);
    const int64_t clientWidth = std::max<int64_t>(1, window.width - frame * 2);
    const int64_t clientHeight = std::max<int64_t>(1,
        window.height - frame * 2 - caption);
    left = std::clamp<int64_t>(left, 0, clientWidth);
    top = std::clamp<int64_t>(top, 0, clientHeight);
    right = std::clamp<int64_t>(right, 0, clientWidth);
    bottom = std::clamp<int64_t>(bottom, 0, clientHeight);
    if (left >= right || top >= bottom) return;
    g_damageChangedSinceLastPublish = true;
    if (window.dirtyRight <= window.dirtyLeft ||
        window.dirtyBottom <= window.dirtyTop) {
        window.dirtyLeft = static_cast<int32_t>(left);
        window.dirtyTop = static_cast<int32_t>(top);
        window.dirtyRight = static_cast<int32_t>(right);
        window.dirtyBottom = static_cast<int32_t>(bottom);
    } else {
        window.dirtyLeft = std::min(window.dirtyLeft, static_cast<int32_t>(left));
        window.dirtyTop = std::min(window.dirtyTop, static_cast<int32_t>(top));
        window.dirtyRight = std::max(window.dirtyRight, static_cast<int32_t>(right));
        window.dirtyBottom = std::max(window.dirtyBottom, static_cast<int32_t>(bottom));
    }
    ++window.contentGeneration;
    if (!window.contentGeneration) ++window.contentGeneration;
    markDamageRect(static_cast<int64_t>(window.x) + frame + left,
        static_cast<int64_t>(window.y) + frame + caption + top,
        static_cast<int64_t>(window.x) + frame + right,
        static_cast<int64_t>(window.y) + frame + caption + bottom);
}

bool beginWindowWriter(Window& window, uint64_t processId) noexcept {
    if (!processId || window.activeWriters == UINT32_MAX) return false;
    Window::WriterOwner* freeOwner = nullptr;
    for (auto& owner : window.writerOwners) {
        if (owner.processId == processId) {
            if (owner.count == UINT32_MAX) return false;
            ++owner.count;
            ++window.activeWriters;
            return true;
        }
        if (!owner.processId && !freeOwner) freeOwner = &owner;
    }
    if (!freeOwner) return false;
    freeOwner->processId = processId;
    freeOwner->count = 1;
    ++window.activeWriters;
    return true;
}

bool endWindowWriter(Window& window, uint64_t processId) noexcept {
    for (auto& owner : window.writerOwners) {
        if (owner.processId != processId || !owner.count) continue;
        if (!window.activeWriters) return false;
        if (!--owner.count) owner.processId = 0;
        --window.activeWriters;
        return true;
    }
    return false;
}

uint8_t captionButtonAt(const Window& window, int32_t localX,
                       int32_t localY) noexcept {
    const int32_t frame = frameInset(window);
    const int32_t caption = captionHeight(window);
    if (!caption || localY < frame || localY >= frame + caption) return 0;
    const int32_t closeLeft = window.width - frame - 30;
    if (localX >= closeLeft && localX < window.width - frame - 2) return 3;
    if (localX >= closeLeft - 29 && localX < closeLeft - 1) return 2;
    if (localX >= closeLeft - 58 && localX < closeLeft - 30) return 1;
    return 0;
}
uint32_t g_mouseButtons{};
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

int32_t hitTestWindow(const Window& window, int32_t x, int32_t y) noexcept {
    constexpr int32_t HTCLIENT = 1, HTCAPTION = 2, HTLEFT = 10, HTRIGHT = 11;
    constexpr int32_t HTTOP = 12, HTTOPLEFT = 13, HTTOPRIGHT = 14;
    constexpr int32_t HTBOTTOM = 15, HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;
    const int32_t localX = x - window.x, localY = y - window.y;
    const int32_t frame = frameInset(window);
    if (frame >= 5 && (localX < frame || localX >= window.width - frame ||
                       localY < frame || localY >= window.height - frame)) {
        const bool left = localX < frame, right = localX >= window.width - frame;
        const bool top = localY < frame, bottom = localY >= window.height - frame;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    const int32_t caption = captionHeight(window);
    if (caption && localY < frame + caption) return HTCAPTION;
    return HTCLIENT;
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

size_t freeMessageSlots() noexcept {
    size_t count = 0;
    for (const auto& message : g_messages) if (!message.active) ++count;
    return count;
}

void beginPointerGesture(const Window& window, int32_t hitTest,
                         int32_t x, int32_t y) noexcept {
    constexpr int32_t HTCAPTION = 2;
    constexpr int32_t HTLEFT = 10, HTRIGHT = 11, HTTOP = 12;
    constexpr int32_t HTTOPLEFT = 13, HTTOPRIGHT = 14, HTBOTTOM = 15;
    constexpr int32_t HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;
    const bool resize = hitTest >= HTLEFT && hitTest <= HTBOTTOMRIGHT;
    if (hitTest != HTCAPTION && !resize) return;
    g_pointerGesture = PointerGesture{
        .window = window.handle,
        .hitTest = hitTest,
        .startX = x,
        .startY = y,
        .windowX = window.x,
        .windowY = window.y,
        .windowWidth = window.width,
        .windowHeight = window.height,
        .mode = resize ? PointerGestureMode::Resize : PointerGestureMode::Move,
    };
}

bool updatePointerGesture(int32_t x, int32_t y) noexcept {
    auto* window = findWindowAny(g_pointerGesture.window);
    if (!window || !window->active || !window->visible || window->iconic ||
        g_pointerGesture.mode == PointerGestureMode::None) {
        g_pointerGesture = {};
        return false;
    }
    const auto& gesture = g_pointerGesture;
    const int64_t dx = static_cast<int64_t>(x) - gesture.startX;
    const int64_t dy = static_cast<int64_t>(y) - gesture.startY;
    int64_t left = gesture.windowX;
    int64_t top = gesture.windowY;
    int64_t width = gesture.windowWidth;
    int64_t height = gesture.windowHeight;
    if (gesture.mode == PointerGestureMode::Move) {
        left += dx;
        top += dy;
        constexpr int64_t visibleCaption = 48;
        left = std::clamp<int64_t>(left, -width + visibleCaption,
                                  static_cast<int64_t>(kScreenWidth) - visibleCaption);
        top = std::clamp<int64_t>(top, 0,
                                 static_cast<int64_t>(kScreenHeight) - 28);
    } else {
        constexpr int32_t HTLEFT = 10, HTRIGHT = 11, HTTOP = 12;
        constexpr int32_t HTTOPLEFT = 13, HTTOPRIGHT = 14, HTBOTTOM = 15;
        constexpr int32_t HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;
        const int64_t minWidth = 160 + frameInset(*window) * 2;
        const int64_t minHeight = 100 + frameInset(*window) * 2 +
                                  captionHeight(*window);
        constexpr int64_t maxExtent = 16384;
        const bool moveLeft = gesture.hitTest == HTLEFT ||
            gesture.hitTest == HTTOPLEFT || gesture.hitTest == HTBOTTOMLEFT;
        const bool moveRight = gesture.hitTest == HTRIGHT ||
            gesture.hitTest == HTTOPRIGHT || gesture.hitTest == HTBOTTOMRIGHT;
        const bool moveTop = gesture.hitTest == HTTOP ||
            gesture.hitTest == HTTOPLEFT || gesture.hitTest == HTTOPRIGHT;
        const bool moveBottom = gesture.hitTest == HTBOTTOM ||
            gesture.hitTest == HTBOTTOMLEFT || gesture.hitTest == HTBOTTOMRIGHT;
        if (moveLeft) {
            width = std::clamp<int64_t>(gesture.windowWidth - dx, minWidth, maxExtent);
            left = static_cast<int64_t>(gesture.windowX) + gesture.windowWidth - width;
        } else if (moveRight) {
            width = std::clamp<int64_t>(gesture.windowWidth + dx, minWidth, maxExtent);
        }
        if (moveTop) {
            height = std::clamp<int64_t>(gesture.windowHeight - dy, minHeight, maxExtent);
            top = static_cast<int64_t>(gesture.windowY) + gesture.windowHeight - height;
        } else if (moveBottom) {
            height = std::clamp<int64_t>(gesture.windowHeight + dy, minHeight, maxExtent);
        }
        if (left < INT32_MIN || left > INT32_MAX || top < INT32_MIN ||
            top > INT32_MAX || left + width > INT32_MAX || top + height > INT32_MAX)
            return false;
    }
    const int32_t newX = static_cast<int32_t>(left);
    const int32_t newY = static_cast<int32_t>(top);
    const int32_t newWidth = static_cast<int32_t>(width);
    const int32_t newHeight = static_cast<int32_t>(height);
    const bool moved = newX != window->x || newY != window->y;
    const bool sized = newWidth != window->width || newHeight != window->height;
    if (!moved && !sized) return true;
    const size_t requiredMessages = static_cast<size_t>(moved) +
                                    static_cast<size_t>(sized);
    if (freeMessageSlots() < requiredMessages) return false;

    markWindowBounds(*window);
    window->x = newX;
    window->y = newY;
    window->width = newWidth;
    window->height = newHeight;
    if (moved) {
        const uint32_t packedPosition =
            (static_cast<uint32_t>(newY) & 0xFFFFu) << 16 |
            (static_cast<uint32_t>(newX) & 0xFFFFu);
        (void)enqueueWindowMessage(*window, kWmMove, 0,
                                   static_cast<int64_t>(packedPosition));
    }
    if (sized) {
        const int32_t frame = frameInset(*window);
        const int32_t caption = captionHeight(*window);
        const uint32_t clientWidth = static_cast<uint32_t>(
            std::max(1, newWidth - frame * 2));
        const uint32_t clientHeight = static_cast<uint32_t>(
            std::max(1, newHeight - frame * 2 - caption));
        const uint32_t packedSize = (clientHeight & 0xFFFFu) << 16 |
                                    (clientWidth & 0xFFFFu);
        (void)enqueueWindowMessage(*window, kWmSize, 0,
                                   static_cast<int64_t>(packedSize));
    }
    markWindowBounds(*window);
    return true;
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
        markWindowBounds(window);
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
            if (request.args[1] > 1) return NtStatus::InvalidParameter;
            const auto rect = request.rect;
            if (request.args[1] &&
                (rect.right < rect.left || rect.bottom < rect.top))
                return NtStatus::InvalidParameter;
            const int64_t clientWidth = std::max<int64_t>(1,
                window->width - frameInset(*window) * 2);
            const int64_t clientHeight = std::max<int64_t>(1,
                window->height - frameInset(*window) * 2 - captionHeight(*window));
            if (request.args[1]) {
                markWindowClientDamage(*window, rect.left, rect.top,
                    rect.right, rect.bottom);
            } else {
                markWindowClientDamage(*window, 0, 0, clientWidth, clientHeight);
            }
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_BEGIN_WINDOW_UPDATE: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (!beginWindowWriter(*window, ownerProcessId))
                return NtStatus::InsufficientResources;
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_END_WINDOW_UPDATE: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (!endWindowWriter(*window, ownerProcessId))
                return NtStatus::InvalidDeviceState;
            if (!request.args[1]) {
                const int64_t clientWidth = std::max<int64_t>(1,
                    window->width - frameInset(*window) * 2);
                const int64_t clientHeight = std::max<int64_t>(1,
                    window->height - frameInset(*window) * 2 - captionHeight(*window));
                markWindowClientDamage(*window, 0, 0, clientWidth, clientHeight);
            }
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_DESTROY_WINDOW: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const bool wasForeground = g_foregroundWindow == window->handle;
            markWindowBounds(*window);
            if (g_pointerGesture.window == window->handle) g_pointerGesture = {};
            if (g_captionCapture == window->handle) {
                g_captionCapture = 0;
                g_pressedCaptionButton = 0;
            }
            window->active = false;
            if (wasForeground) {
                g_foregroundWindow = 0;
                if (auto* next = topmostVisibleWindow()) {
                    g_foregroundWindow = next->handle;
                    markWindowBounds(*next);
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
            const bool wasIconic = window->iconic;
            if (wasVisible || wasIconic) markWindowBounds(*window);
            window->visible = command != 0;
            window->iconic = command == kSwShowMinimized ||
                             command == kSwMinimize;
            if (window->visible) {
                window->zOrder = nextZOrder();
                g_foregroundWindow = window->handle;
            }
            if (window->visible) markWindowBounds(*window);
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
            const bool geometryChanged = x != window->x || y != window->y ||
                width != window->width || height != window->height;
            const bool raiseWindow = request.args[6] == 0;
            const uint64_t oldForeground = g_foregroundWindow;
            if (geometryChanged || raiseWindow) {
                if (auto* previous = findWindowAny(oldForeground))
                    markWindowBounds(*previous);
                markWindowBounds(*window);
            }
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
            if (geometryChanged || raiseWindow) markWindowBounds(*window);
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
            return NtStatus::Success;
        case MICANT_WIN32K_OP_GET_CLIENT_RECT:
        case MICANT_WIN32K_OP_GET_WINDOW_RECT: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (request.operation == MICANT_WIN32K_OP_GET_WINDOW_RECT &&
                (window->x > INT32_MAX - window->width ||
                 window->y > INT32_MAX - window->height))
                return NtStatus::InvalidParameter;
            if (request.operation == MICANT_WIN32K_OP_GET_CLIENT_RECT) {
                const int32_t frame = frameInset(*window);
                const int32_t caption = captionHeight(*window);
                request.rect = {0, 0,
                    window->width > frame * 2 ? window->width - frame * 2 : 1,
                    window->height > frame * 2 + caption
                        ? window->height - frame * 2 - caption : 1};
            } else request.rect = MicaWin32kRect{window->x, window->y,
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
                if (previous) markWindowBounds(*previous);
                markWindowBounds(*window);
                if (previous && !enqueueWindowMessage(*previous, kWmActivate, 0, 0))
                    return NtStatus::InsufficientResources;
                g_foregroundWindow = window->handle;
                if (!enqueueWindowMessage(*window, kWmActivate, 1, 0) ||
                    !enqueueWindowMessage(*window, kWmSetFocus, 0, 0))
                    return NtStatus::InsufficientResources;
            }
            if (previousHandle == window->handle) markWindowBounds(*window);
            window->zOrder = nextZOrder();
            request.result = previousHandle;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_WINDOW_ICON: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            const uint32_t type = static_cast<uint32_t>(request.args[1]);
            if (!window || type > 2) return NtStatus::InvalidParameter;
            markWindowBounds(*window);
            request.result = window->icons[type];
            window->icons[type] = request.args[2];
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_WINDOW_ICON: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            const uint32_t type = static_cast<uint32_t>(request.args[1]);
            if (!window || type > 2) return NtStatus::InvalidParameter;
            request.result = window->icons[type];
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_CURSOR_POS:
            request.rect.left = g_mouseX < 0 ? 0 : g_mouseX;
            request.rect.top = g_mouseY < 0 ? 0 : g_mouseY;
            request.result = 1;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_GET_CURSOR_CLIP:
            request.rect = g_cursorClip;
            request.result = 1;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_GET_CURSOR_HANDLE:
            request.result = g_activeCursorOwner == ownerProcessId ? g_activeCursorHandle : 0;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_GET_CURSOR_INFO:
            request.rect.left = g_mousePositionValid ? g_mouseX : 0;
            request.rect.top = g_mousePositionValid ? g_mouseY : 0;
            request.flags = g_cursorDisplayCount >= 0 && g_mousePositionValid ? 1u : 0u;
            request.result = g_activeCursorOwner == ownerProcessId ? g_activeCursorHandle : 0;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_SET_CURSOR_POS: {
            const int32_t requestedX = static_cast<int32_t>(request.args[0]);
            const int32_t requestedY = static_cast<int32_t>(request.args[1]);
            const int32_t left = std::max(0, g_cursorClip.left);
            const int32_t top = std::max(0, g_cursorClip.top);
            const int32_t right = std::min(kScreenWidth, g_cursorClip.right);
            const int32_t bottom = std::min(kScreenHeight, g_cursorClip.bottom);
            if (right <= left || bottom <= top) return NtStatus::InvalidParameter;
            const int32_t newX = std::clamp(requestedX, left, right - 1);
            const int32_t newY = std::clamp(requestedY, top, bottom - 1);
            const bool moved = !g_mousePositionValid || newX != g_mouseX || newY != g_mouseY;
            if (moved && g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
            g_mouseX = newX;
            g_mouseY = newY;
            g_mousePositionValid = true;
            if (moved) markCursorBounds(newX, newY);
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_CURSOR_CLIP: {
            if (request.args[0] == 0) {
                if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
                g_cursorClip = {0, 0, kScreenWidth, kScreenHeight};
                g_cursorClipOwner = 0;
                if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
                request.result = 1;
                return NtStatus::Success;
            }
            const auto rect = request.rect;
            if (rect.left < 0 || rect.top < 0 || rect.right > kScreenWidth ||
                rect.bottom > kScreenHeight || rect.right <= rect.left || rect.bottom <= rect.top)
                return NtStatus::InvalidParameter;
            if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
            g_cursorClip = rect;
            g_cursorClipOwner = ownerProcessId;
            g_mouseX = std::clamp(g_mouseX, rect.left, rect.right - 1);
            g_mouseY = std::clamp(g_mouseY, rect.top, rect.bottom - 1);
            if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SHOW_CURSOR: {
            const int32_t delta = static_cast<int32_t>(request.args[0]);
            if (delta != -1 && delta != 1) return NtStatus::InvalidParameter;
            if ((delta > 0 && g_cursorDisplayCount == INT32_MAX) ||
                (delta < 0 && g_cursorDisplayCount == INT32_MIN))
                return NtStatus::InvalidParameter;
            const bool wasVisible = g_cursorDisplayCount >= 0;
            g_cursorDisplayCount += delta;
            const bool isVisible = g_cursorDisplayCount >= 0;
            if (wasVisible && !isVisible)
                markCursorBounds(g_mouseX, g_mouseY);
            if (!wasVisible && isVisible)
                markCursorBounds(g_mouseX, g_mouseY);
            request.result = static_cast<uint64_t>(static_cast<int64_t>(g_cursorDisplayCount));
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
            markWindowBounds(window);
            window.activeWriters = 0;
            for (auto& writerOwner : window.writerOwners) writerOwner = {};
            if (g_foregroundWindow == window.handle) g_foregroundWindow = 0;
            window = {};
        }
        for (auto& window : g_windows) {
            if (!window.active) continue;
            for (auto& writerOwner : window.writerOwners) {
                if (writerOwner.processId != ownerProcessId) continue;
                window.activeWriters -= std::min(window.activeWriters,
                                                  writerOwner.count);
                writerOwner = {};
            }
        }
        if (g_pointerGesture.window) {
            const auto* gestureWindow = findWindowAny(g_pointerGesture.window);
            if (!gestureWindow || gestureWindow->ownerProcessId == ownerProcessId)
                g_pointerGesture = {};
        }
        if (g_captionCapture) {
            const auto* captureWindow = findWindowAny(g_captionCapture);
            if (!captureWindow || captureWindow->ownerProcessId == ownerProcessId) {
                g_captionCapture = 0;
                g_pressedCaptionButton = 0;
            }
        }
        for (auto& windowClass : g_classes)
            if (windowClass.active && windowClass.ownerProcessId == ownerProcessId) windowClass = {};
        if (g_cursorClipOwner == ownerProcessId) {
            g_cursorClip = {0, 0, kScreenWidth, kScreenHeight};
            g_cursorClipOwner = 0;
        }
        if (g_activeCursorOwner == ownerProcessId) {
            if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
            resetCursorImage();
            if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
        }
    }
    if (publish) publish();
}

bool dispatchMouseInput(const MicaWin32kInputEvent& event) noexcept {
    StateLockGuard lock;
    const int32_t left = std::max(0, g_cursorClip.left);
    const int32_t top = std::max(0, g_cursorClip.top);
    const int32_t right = std::min(kScreenWidth, g_cursorClip.right);
    const int32_t bottom = std::min(kScreenHeight, g_cursorClip.bottom);
    const int32_t x = std::clamp(event.x, left, std::max(left, right - 1));
    const int32_t y = std::clamp(event.y, top, std::max(top, bottom - 1));
    const int32_t oldMouseX = g_mouseX;
    const int32_t oldMouseY = g_mouseY;
    const bool oldMouseValid = g_mousePositionValid;
    g_mousePositionValid = true;
    const uint32_t previousButtons = g_mouseButtons;
    const uint32_t currentButtons = event.buttons & 0x07u;
    const bool moved = x != g_mouseX || y != g_mouseY;
    if (moved && oldMouseValid) markCursorBounds(oldMouseX, oldMouseY);
    g_mouseButtons = currentButtons;
    g_mouseX = x;
    g_mouseY = y;
    if (moved || !oldMouseValid) markCursorBounds(x, y);
    auto* target = topmostVisibleWindow(x, y);
    const int32_t hitTest = target ? hitTestWindow(*target, x, y) : 0;
    const uint64_t oldCursorHitWindow = g_cursorHitWindow;
    const int32_t oldCursorHitTest = g_cursorHitTest;
    if (oldCursorHitWindow != (target ? target->handle : 0) ||
        oldCursorHitTest != hitTest) {
        if (auto* oldHover = findWindowAny(oldCursorHitWindow)) markWindowBounds(*oldHover);
        if (target) markWindowBounds(*target);
    }
    if (!target) {
        g_cursorHitWindow = 0;
        g_cursorHitTest = 0;
    } else if (target->handle != g_cursorHitWindow || hitTest != g_cursorHitTest) {
        const int64_t setCursorLParam = static_cast<int64_t>(
            (static_cast<uint32_t>(kWmMouseMove) << 16) |
            (static_cast<uint32_t>(hitTest) & 0xFFFFu));
        if (!enqueueWindowMessage(*target, kWmSetCursor, target->handle,
                                  setCursorLParam, x, y)) return false;
        g_cursorHitWindow = target->handle;
        g_cursorHitTest = hitTest;
    }
    const bool leftPressed = !(previousButtons & 0x01u) && (currentButtons & 0x01u);
    const bool leftReleased = (previousButtons & 0x01u) && !(currentButtons & 0x01u);
    if (g_pointerGesture.mode != PointerGestureMode::None) {
        if (currentButtons & 0x01u) {
            if (moved) (void)updatePointerGesture(x, y);
            return true;
        }
        if (leftReleased) {
            (void)updatePointerGesture(x, y);
            g_pointerGesture = {};
            return true;
        }
        g_pointerGesture = {};
    }
    if (leftPressed && target) {
        if (auto* previous = findWindowAny(g_foregroundWindow))
            markWindowBounds(*previous);
        markWindowBounds(*target);
        target->zOrder = nextZOrder();
        if (g_foregroundWindow != target->handle) {
            auto* previous = findWindowAny(g_foregroundWindow);
            if (previous) (void)enqueueWindowMessage(*previous, kWmActivate, 0, 0);
            g_foregroundWindow = target->handle;
            (void)enqueueWindowMessage(*target, kWmActivate, 1, 0);
            (void)enqueueWindowMessage(*target, kWmSetFocus, 0, 0);
        }
        const uint8_t button = captionButtonAt(*target,
            static_cast<int32_t>(static_cast<int64_t>(x) - target->x),
            static_cast<int32_t>(static_cast<int64_t>(y) - target->y));
        if (button) {
            g_captionCapture = target->handle;
            g_pressedCaptionButton = button;
            return true;
        }
        g_captionCapture = 0;
        g_pressedCaptionButton = 0;
        beginPointerGesture(*target, hitTest, x, y);
        if (g_pointerGesture.mode != PointerGestureMode::None) return true;
    }

    if (leftReleased && g_captionCapture) {
        auto* captured = findWindowAny(g_captionCapture);
        if (captured) markWindowBounds(*captured);
        const uint8_t releasedButton = captured ? captionButtonAt(*captured,
            static_cast<int32_t>(static_cast<int64_t>(x) - captured->x),
            static_cast<int32_t>(static_cast<int64_t>(y) - captured->y)) : 0;
        const uint8_t pressedButton = g_pressedCaptionButton;
        g_captionCapture = 0;
        g_pressedCaptionButton = 0;
        if (!captured || !pressedButton || releasedButton != pressedButton) return true;
        if (pressedButton == 1) {
            captured->iconic = true;
            (void)enqueueWindowMessage(*captured, kWmSize, 1, 0);
            if (g_foregroundWindow == captured->handle) {
                auto* next = topmostVisibleWindow();
                g_foregroundWindow = next ? next->handle : 0;
                if (next) {
                    markWindowBounds(*next);
                    (void)enqueueWindowMessage(*next, kWmActivate, 1, 0);
                    (void)enqueueWindowMessage(*next, kWmSetFocus, 0, 0);
                }
            }
        } else if (pressedButton == 2) {
            if (!captured->maximized) {
                captured->restoreX = captured->x;
                captured->restoreY = captured->y;
                captured->restoreWidth = captured->width;
                captured->restoreHeight = captured->height;
                captured->x = 0;
                captured->y = 0;
                captured->width = MICANT_COMPOSITOR_SCREEN_WIDTH;
                captured->height = MICANT_COMPOSITOR_SCREEN_HEIGHT - 40;
                captured->maximized = true;
            } else {
                captured->x = captured->restoreX;
                captured->y = captured->restoreY;
                captured->width = captured->restoreWidth;
                captured->height = captured->restoreHeight;
                captured->maximized = false;
            }
            markWindowBounds(*captured);
            const int32_t frame = frameInset(*captured);
            const int32_t caption = captionHeight(*captured);
            const uint32_t clientWidth = static_cast<uint32_t>(
                captured->width > frame * 2 ? captured->width - frame * 2 : 1);
            const uint32_t clientHeight = static_cast<uint32_t>(
                captured->height > frame * 2 + caption
                    ? captured->height - frame * 2 - caption : 1);
            const uint32_t packedSize =
                (clientHeight & 0xFFFFu) << 16 | (clientWidth & 0xFFFFu);
            (void)enqueueWindowMessage(*captured, kWmSize,
                captured->maximized ? 2u : 0u, static_cast<int64_t>(packedSize));
        } else {
            (void)enqueueWindowMessage(*captured, kWmClose, 0, 0);
        }
        return true;
    }
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
    if (!g_defaultCursorInitialized) {
        resetCursorImage();
        g_defaultCursorInitialized = true;
    }
    g_compositorStore = store;
    g_compositorPublishCallback = publishCallback;
    g_pendingDirtyRegionCount = 0;
    g_pendingFullRedraw = true;
    g_damageChangedSinceLastPublish = true;
}

void publishCompositorSurfaces() noexcept {
    auto* store = g_compositorStore;
    if (!store) return;
    {
    StateLockGuard lock;
    store = g_compositorStore;
    if (!store) return;
    const uint64_t previousSequence = __atomic_load_n(
        &store->frameSequence, __ATOMIC_ACQUIRE);
    const uint64_t acknowledged = __atomic_load_n(
        &store->compositorAckSequence, __ATOMIC_ACQUIRE);
    if (!g_damageChangedSinceLastPublish && previousSequence &&
        !(previousSequence & 1u) && acknowledged == previousSequence)
        clearPendingDamage();
    __atomic_add_fetch(&store->frameSequence, 1, __ATOMIC_ACQ_REL);
    if (store->themeGeneration != g_themeGeneration) {
        store->theme = g_themeSnapshot;
        store->themeGeneration = g_themeGeneration;
    }
    auto& cursor = store->cursor;
    cursor = {};
    cursor.x = g_mousePositionValid ? g_mouseX : 0;
    cursor.y = g_mousePositionValid ? g_mouseY : 0;
    cursor.width = g_activeCursorWidth;
    cursor.height = g_activeCursorHeight;
    cursor.pitch = g_activeCursorPitch;
    cursor.hotX = g_activeCursorHotX;
    cursor.hotY = g_activeCursorHotY;
    cursor.flags = (g_activeCursorWidth && g_activeCursorHeight
        ? MICANT_COMPOSITOR_CURSOR_ACTIVE : 0u) |
        (g_cursorDisplayCount >= 0 && g_mousePositionValid
            ? MICANT_COMPOSITOR_CURSOR_VISIBLE : 0u);
    if (g_activeCursorWidth && g_activeCursorHeight)
        for (size_t i = 0; i < size_t(g_activeCursorPitch) * g_activeCursorHeight; ++i)
            cursor.pixels[i] = g_activeCursorPixels[i];
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
            (window.iconic ? MICANT_COMPOSITOR_SURFACE_ICONIC : 0u) |
            (window.handle == g_foregroundWindow
                ? MICANT_COMPOSITOR_SURFACE_FOREGROUND : 0u) |
            (window.maximized ? MICANT_COMPOSITOR_SURFACE_MAXIMIZED : 0u) |
            ((g_captionCapture == window.handle && g_pressedCaptionButton)
                ? MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_PRESSED |
                    (static_cast<uint32_t>(g_pressedCaptionButton) <<
                     MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_SHIFT)
                : 0u) |
            ((g_cursorHitWindow == window.handle &&
              captionButtonAt(window, g_mouseX - window.x,
                              g_mouseY - window.y) != 0)
                ? MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_HOVER |
                    (static_cast<uint32_t>(captionButtonAt(
                        window, g_mouseX - window.x, g_mouseY - window.y)) <<
                     MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_SHIFT)
                : 0u);
        info.pixelOffset = static_cast<uint32_t>(index * MICANT_COMPOSITOR_SURFACE_PIXELS);
        info.dirtyLeft = window.dirtyLeft;
        info.dirtyTop = window.dirtyTop;
        info.dirtyRight = window.dirtyRight;
        info.dirtyBottom = window.dirtyBottom;
        info.contentGeneration = window.contentGeneration;
        info.activeWriters = window.activeWriters;
        info.style = window.style;
        for (size_t character = 0; character + 1 < sizeof(info.title) / sizeof(info.title[0]) &&
             window.title[character]; ++character)
            info.title[character] = window.title[character];
    }
    store->dirtyRegionCount = g_pendingFullRedraw
        ? 0u : g_pendingDirtyRegionCount;
    store->dirtyFlags = g_pendingFullRedraw
        ? MICANT_COMPOSITOR_DIRTY_FULL : 0u;
    for (size_t index = 0; index < MICANT_COMPOSITOR_DIRTY_REGION_CAPACITY; ++index)
        store->dirtyRegions[index] = index < store->dirtyRegionCount
            ? g_pendingDirtyRegions[index] : MicaCompositorDirtyRect{};
    __atomic_add_fetch(&store->frameSequence, 1, __ATOMIC_RELEASE);
    g_damageChangedSinceLastPublish = false;
    }
    (void)g_compositorWakeEvent.set();
}

micant::NtStatus waitForCompositorUpdate(uint64_t observedSequence) noexcept {
    auto* current = ke::KeGetCurrentThread();
    auto* store = g_compositorStore;
    if (!current || !store || current->eventWaitInProgress ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return micant::NtStatus::InvalidDeviceState;

    // Reset before checking the generation. A concurrent publication either
    // changes the observed generation or leaves the event set for this wait.
    (void)g_compositorWakeEvent.reset();
    const uint64_t sequence = __atomic_load_n(&store->frameSequence, __ATOMIC_ACQUIRE);
    if (!(sequence & 1u) && sequence != observedSequence)
        return micant::NtStatus::Success;

    current->eventWaitInProgress = true;
    current->eventWaitUserMode = current->previousUserMode;
    current->eventWaitCanTerminate = current->eventWaitUserMode &&
        ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL;
    const auto result = g_compositorWakeEvent.wait(false, false);
    const bool terminate = current->terminationRequested && current->eventWaitCanTerminate &&
        current->serviceTermination;
    auto terminationRoutine = current->serviceTermination;
    void* terminationContext = current->terminationContext;
    current->eventWaitInProgress = false;
    current->eventWaitUserMode = false;
    current->eventWaitCanTerminate = false;
    if (terminate) terminationRoutine(terminationContext);
    return result;
}

micant::NtStatus setCursorImage(const MicaWin32kCursorImage& image,
                                uint64_t ownerProcessId) noexcept {
    if (!ownerProcessId || image.size != sizeof(image) ||
        image.version != MICANT_WIN32K_CURSOR_IMAGE_VERSION) return NtStatus::InvalidParameter;
    if (!image.cursorHandle) {
        if (image.width || image.height || image.pitch || image.hotX || image.hotY)
            return NtStatus::InvalidParameter;
    } else if (!image.width || !image.height ||
        image.width > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
        image.height > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
        image.pitch != image.width || image.hotX >= image.width || image.hotY >= image.height) {
        return NtStatus::InvalidParameter;
    }
    for (size_t i = 0; i < size_t(image.pitch) * image.height; ++i) {
        const uint32_t pixel = image.pixels[i];
        const uint32_t alpha = pixel >> 24;
        if (((pixel >> 16) & 0xFFu) > alpha || ((pixel >> 8) & 0xFFu) > alpha ||
            (pixel & 0xFFu) > alpha) return NtStatus::InvalidParameter;
    }
    StateLockGuard lock;
    if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
    g_activeCursorHandle = image.cursorHandle;
    g_activeCursorOwner = image.cursorHandle ? ownerProcessId : 0;
    if (image.cursorHandle) {
        g_activeCursorWidth = image.width;
        g_activeCursorHeight = image.height;
        g_activeCursorPitch = image.pitch;
        g_activeCursorHotX = image.hotX;
        g_activeCursorHotY = image.hotY;
        for (size_t i = 0; i < size_t(image.pitch) * image.height; ++i)
            g_activeCursorPixels[i] = image.pixels[i];
    } else {
        resetCursorImage();
    }
    if (g_mousePositionValid) markCursorBounds(g_mouseX, g_mouseY);
    return NtStatus::Success;
}

micant::NtStatus setThemeSnapshot(const MicaThemeSnapshot& snapshot) noexcept {
    if (!MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot)))
        return NtStatus::InvalidParameter;
    StateLockGuard lock;
    constexpr uint32_t kWmThemeChanged = 0x031A;
    size_t activeWindows = 0;
    size_t freeMessageSlots = 0;
    for (const auto& window : g_windows) activeWindows += window.active ? 1u : 0u;
    for (const auto& message : g_messages) freeMessageSlots += message.active ? 0u : 1u;
    if (freeMessageSlots < activeWindows) return NtStatus::InsufficientResources;

    markDamageFull();
    g_themeSnapshot = snapshot;
    if (++g_themeGeneration == 0) ++g_themeGeneration;
    for (const auto& window : g_windows) {
        if (window.active && !enqueueWindowMessage(window, kWmThemeChanged, 0, 0))
            return NtStatus::InsufficientResources;
    }
    return NtStatus::Success;
}

micant::NtStatus getThemeSnapshot(MicaThemeSnapshot& snapshot) noexcept {
    StateLockGuard lock;
    if (!MicaThemeSnapshotIsValid(&g_themeSnapshot, sizeof(g_themeSnapshot)))
        return NtStatus::NotFound;
    snapshot = g_themeSnapshot;
    return NtStatus::Success;
}

} // namespace micant::win32k::freestanding
