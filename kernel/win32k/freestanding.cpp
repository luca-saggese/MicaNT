#include "micant/km/win32k/freestanding.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/shared/compositor_surface.h"
#include "micant/km/core/sync_baremetal.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <new>

#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;
#endif

namespace micant::win32k::freestanding {
namespace {

// Boot-time theme and control probes register process-owned classes before
// the desktop shell starts. Keep the table bounded while leaving room for
// the independently started Ring 3 terminal and later applications.
constexpr size_t kClassCapacity = 1024;
constexpr size_t kMessageCapacity = 256;
constexpr uint64_t kHandleTag = 0x4D4E570000000000ULL;
constexpr uint32_t kWmQuit = 0x0012;
constexpr uint32_t kWmMove = 0x0003;
constexpr uint32_t kWmSize = 0x0005;
constexpr uint32_t kWmNcCreate = 0x0081;
constexpr uint32_t kWmActivate = 0x0006;
constexpr uint32_t kWmSetFocus = 0x0007;
constexpr uint32_t kWmPaint = 0x000F;
constexpr int32_t kMenuBarHeight = 26;
constexpr uint32_t kWmMouseMove = 0x0200;
constexpr uint32_t kWmMouseHover = 0x02A1;
constexpr uint32_t kWmMouseLeave = 0x02A3;
constexpr uint32_t kWmNcMouseHover = 0x02A0;
constexpr uint32_t kWmNcMouseLeave = 0x02A2;
constexpr uint32_t kTmeHover = 0x00000001u;
constexpr uint32_t kTmeLeave = 0x00000002u;
constexpr uint32_t kTmeNonClient = 0x00000010u;
constexpr uint32_t kTmeCancel = 0x80000000u;
constexpr uint32_t kTmeQuery = 0x40000000u;
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
constexpr uint32_t kWindowAnimationMinimize = MICANT_WIN32K_ANIMATION_MINIMIZE;
constexpr uint32_t kWindowAnimationRestore = MICANT_WIN32K_ANIMATION_RESTORE;
constexpr int32_t kGwlpWndProc = -4;
constexpr int32_t kGwlStyle = -16;
constexpr int32_t kGwlpUserData = -21;
constexpr uint32_t kWsChild = 0x40000000u;
constexpr uint32_t kWsDisabled = 0x08000000u;
constexpr uint32_t kWsBorder = 0x00800000u;
constexpr uint32_t kWsDlgFrame = 0x00400000u;
constexpr uint32_t kWsCaption = kWsBorder | kWsDlgFrame;
constexpr uint32_t kWsThickFrame = 0x00040000u;
int32_t g_screenWidth{};
int32_t g_screenHeight{};
std::atomic_flag g_stateLock = ATOMIC_FLAG_INIT;
sync::detail::BootstrapEvent g_compositorWakeEvent{true, false};
MicaCompositorDirtyRect* g_pendingDirtyRegions{};
uint32_t g_pendingDirtyRegionCount{};
size_t g_pendingDirtyRegionCapacity{};
bool g_pendingFullRedraw{true};
bool g_damageChangedSinceLastPublish{true};
CreateCompositorSurface g_createCompositorSurface{};
ReleaseCompositorSurface g_releaseCompositorSurface{};
ps::EProcess* g_compositorProcess{};
CompositorSurfaceStorage g_surfaceMetadataStorage{};
CompositorSurfaceStorage g_dirtyMetadataStorage{};
size_t g_surfaceMetadataCapacity{};
size_t g_dirtyMetadataCapacity{};
size_t g_lastPublishedSurfaceCount{};
MicaCompositorMetadataStore* g_compositorStore{};
struct RetiredMetadataStorage {
    CompositorSurfaceStorage storage{};
    uint64_t releaseAfterSequence{};
    RetiredMetadataStorage* next{};
};
RetiredMetadataStorage* g_retiredMetadataStorage{};

bool retireMetadataStorage(CompositorSurfaceStorage& storage) noexcept {
    auto* store = g_compositorStore;
    if (!store || !storage.sectionReference || !g_releaseCompositorSurface)
        return false;
    const uint64_t sequence = __atomic_load_n(&store->frameSequence, __ATOMIC_ACQUIRE);
    if ((sequence & 1u) || sequence > UINT64_MAX - 2u) return false;
    auto* retired = new (std::nothrow_t{}) RetiredMetadataStorage{};
    if (!retired) return false;
    retired->storage = storage;
    retired->releaseAfterSequence = sequence + 2u;
    retired->next = g_retiredMetadataStorage;
    g_retiredMetadataStorage = retired;
    return true;
}

void reapRetiredMetadataStorage(uint64_t acknowledgedSequence) noexcept {
    auto** link = &g_retiredMetadataStorage;
    while (*link) {
        auto* retired = *link;
        if (acknowledgedSequence < retired->releaseAfterSequence ||
            !g_releaseCompositorSurface ||
            g_releaseCompositorSurface(retired->storage, true) != NtStatus::Success) {
            link = &retired->next;
            continue;
        }
        *link = retired->next;
        delete retired;
    }
}

template <typename T>
bool ensureMetadataArray(CompositorSurfaceStorage& storage, size_t& capacity,
                         size_t required) noexcept {
    if (required <= capacity) return true;
    if (!g_createCompositorSurface || required > SIZE_MAX / sizeof(T)) return false;
    size_t next = capacity ? capacity : 4;
    while (next < required) {
        if (next > SIZE_MAX / 2) { next = required; break; }
        next *= 2;
    }
    const size_t bytes = next * sizeof(T);
    if (bytes > SIZE_MAX - 3 || (bytes + 3) / 4 > UINT32_MAX) return false;
    CompositorSurfaceStorage replacement{};
    if (g_createCompositorSurface(static_cast<uint32_t>((bytes + 3) / 4),
                                  1, replacement) != NtStatus::Success)
        return false;
    if (storage.sectionReference) {
        if (!retireMetadataStorage(storage)) {
            if (g_releaseCompositorSurface)
                (void)g_releaseCompositorSurface(replacement, true);
            return false;
        }
    }
    storage = replacement;
    capacity = next;
    return true;
}

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
    uint32_t windowExtraBytes{};
    bool active{};
};

struct WindowIconImage {
    uint32_t width{}, height{}, stride{};
    uint64_t generation{};
    CompositorSurfaceStorage storage{};
};
struct WindowTitleSurface {
    uint32_t width{}, height{}, stride{};
    uint64_t generation{};
    CompositorSurfaceStorage storage{};
};

struct Window {
    uint16_t title[MICANT_WIN32K_TEXT_CAPACITY]{};
    uint64_t handle{};
    uint64_t parent{};
    uint64_t menu{};
    uint64_t windowProcedure{};
    uint64_t icons[3]{};
    intptr_t userData{};
    uint32_t windowExtraBytes{};
    alignas(uintptr_t) uintptr_t windowExtra[8]{};
    int32_t x{}, y{}, width{}, height{};
    int32_t restoreX{}, restoreY{}, restoreWidth{}, restoreHeight{};
    uint32_t style{}, extendedStyle{};
    uint32_t mouseTrackingFlags{}, mouseHoverTime{};
    uint64_t mouseHoverStartCounter{};
    int32_t mouseHoverX{}, mouseHoverY{};
    bool mouseHoverStarted{}, mouseHoverFired{};
    MicaLiquidGlassDescriptor liquidGlass{};
    uint32_t animationFlags{};
    uint32_t compositorSlot{UINT32_MAX};
    CompositorSurfaceStorage surface{};
    uint64_t animationGeneration{};
    MicaWin32kRect taskbarButtonBounds{};
    MicaWin32kRect animationTarget{};
    uint64_t contentGeneration{};
    uint32_t activeWriters{};
    struct WriterOwner { uint64_t processId{}; uint32_t count{}; WriterOwner* next{}; };
    WriterOwner* writerOwners{};
    int32_t dirtyLeft{}, dirtyTop{}, dirtyRight{}, dirtyBottom{};
    uint16_t classAtom{};
    uint64_t ownerProcessId{};
    uint64_t zOrder{};
    bool active{}, visible{}, iconic{}, maximized{};
    bool hasTaskbarButtonBounds{};
    WindowIconImage iconImages[3]{};
    WindowTitleSurface titleSurface{};
};

class WindowTable {
public:
    Window* begin() noexcept { return entries_; }
    Window* end() noexcept { return entries_ ? entries_ + capacity_ : nullptr; }
    const Window* begin() const noexcept { return entries_; }
    const Window* end() const noexcept { return entries_ ? entries_ + capacity_ : nullptr; }
    Window& operator[](size_t index) noexcept { return entries_[index]; }
    const Window& operator[](size_t index) const noexcept { return entries_[index]; }
    size_t capacity() const noexcept { return capacity_; }
    Window* data() noexcept { return entries_; }

    bool grow() noexcept {
        const size_t nextCapacity = capacity_ ? capacity_ * 2 : 16;
        if (nextCapacity < capacity_ || nextCapacity > SIZE_MAX / sizeof(Window)) return false;
        auto* replacement = new (std::nothrow_t{}) Window[nextCapacity]{};
        if (!replacement) return false;
        for (size_t index = 0; index < capacity_; ++index)
            replacement[index] = entries_[index];
        delete[] entries_;
        entries_ = replacement;
        capacity_ = nextCapacity;
        return true;
    }

    void releaseIfEmpty() noexcept {
        for (size_t index = 0; index < capacity_; ++index)
            if (entries_[index].active) return;
        delete[] entries_;
        entries_ = nullptr;
        capacity_ = 0;
    }

private:
    Window* entries_{};
    size_t capacity_{};
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
WindowTable g_windows{};
QueuedMessage g_messages[kMessageCapacity]{};
uint16_t g_nextAtom{1};
uint32_t g_nextGeneration{1};
uint32_t g_messageTime{};
uint64_t g_foregroundWindow{};
uint64_t g_focusedWindow{};
uint64_t g_focusedWindowOwner{};
uint64_t g_nextZOrder{};
int32_t g_mouseX{-1}, g_mouseY{-1};
bool g_mousePositionValid{};
MicaWin32kRect g_cursorClip{};
uint64_t g_cursorClipOwner{};
int32_t g_cursorDisplayCount{1};
uint64_t g_activeCursorHandle{};
uint64_t g_activeCursorOwner{};
uint32_t g_activeCursorWidth{16}, g_activeCursorHeight{16}, g_activeCursorPitch{16};
uint32_t g_activeCursorHotX{}, g_activeCursorHotY{};
CompositorSurfaceStorage g_activeCursorStorage{};
bool g_defaultCursorInitialized{};
constexpr uint16_t kDefaultArrowRows[16]{
    0x8000,0xC000,0xE000,0xF000,0xF800,0xFC00,0xFE00,0xFF00,
    0xFF80,0xFCC0,0xCCC0,0x86C0,0x06C0,0x0360,0x0360,0x0000};

void resetCursorImage(bool ownerAddressValid = false,
                      bool createReplacement = true) noexcept {
    if (g_activeCursorStorage.sectionReference && g_releaseCompositorSurface)
        (void)g_releaseCompositorSurface(g_activeCursorStorage, ownerAddressValid);
    g_activeCursorStorage = {};
    g_activeCursorHandle = 0;
    g_activeCursorOwner = 0;
    g_activeCursorWidth = g_activeCursorHeight = g_activeCursorPitch = 16;
    g_activeCursorHotX = g_activeCursorHotY = 0;
    if (!createReplacement || !g_createCompositorSurface ||
        g_createCompositorSurface(16, 16, g_activeCursorStorage) != NtStatus::Success) {
        g_activeCursorWidth = g_activeCursorHeight = g_activeCursorPitch = 0;
        return;
    }
    auto* pixels = reinterpret_cast<uint32_t*>(g_activeCursorStorage.ownerAddress);
    std::fill_n(pixels, 16 * 16, 0u);
    for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x) {
        if (!(kDefaultArrowRows[y] & (0x8000u >> x))) continue;
        const bool edge = x == 0 || y == 0 ||
            !(kDefaultArrowRows[y] & (0x8000u >> (x - 1))) ||
            (y && !(kDefaultArrowRows[y - 1] & (0x8000u >> x)));
        pixels[y * 16 + x] = edge ? 0xFF000000u : 0xFFFFFFFFu;
    }
}
uint64_t g_captionCapture{};
uint8_t g_pressedCaptionButton{};
uint64_t g_mouseCaptureWindow{};
uint64_t g_mouseCaptureOwner{};
uint64_t g_leftButtonReleaseWindow{};
uint64_t g_leftButtonReleaseOwner{};
PointerGesture g_pointerGesture{};
uint64_t g_cursorHitWindow{};
int32_t g_cursorHitTest{};
MicaThemeSnapshot g_themeSnapshot{};
MicaThemeCaptionImageSet g_themeCaptionImageSet{};
uint64_t g_themeGeneration{};

Window* findWindowAny(uint64_t handle) noexcept;

int32_t frameInset(const Window& window) noexcept {
    if (window.style & kWsChild) return 0;
    constexpr size_t windowStyleIndex = 14u * MICANT_THEME_STATE_COUNT;
    const auto& style = g_themeSnapshot.styles[windowStyleIndex];
    const int32_t themedWidth =
        MicaThemeSnapshotIsValid(&g_themeSnapshot, sizeof(g_themeSnapshot)) &&
        (style.flags & MICANT_THEME_HAS_BORDER_WIDTH) &&
        style.borderWidth >= 1 && style.borderWidth <= 16
            ? style.borderWidth : 0;
    // Keep a usable native resize target even when the theme uses a one-pixel
    // visual stroke. A wider explicit theme border can widen the target.
    if (window.style & kWsThickFrame)
        return themedWidth > 5 ? themedWidth : 5;
    return (window.style & (kWsBorder | kWsDlgFrame))
        ? (themedWidth ? themedWidth : 1) : 0;
}

int32_t captionHeight(const Window& window) noexcept {
    if ((window.style & kWsChild) || !(window.style & kWsCaption))
        return 0;
    constexpr size_t headerbarIndex = 13u * MICANT_THEME_STATE_COUNT;
    const auto& headerbar = g_themeSnapshot.styles[headerbarIndex];
    if (MicaThemeSnapshotIsValid(&g_themeSnapshot, sizeof(g_themeSnapshot)) &&
        (headerbar.flags & MICANT_THEME_HAS_MIN_HEIGHT) &&
        headerbar.minHeight >= 24 && headerbar.minHeight <= 96)
        return headerbar.minHeight;
    return 28;
}

int32_t menuHeight(const Window& window) noexcept {
    return !(window.style & kWsChild) && window.menu ? kMenuBarHeight : 0;
}

bool adjustWindowRect(MicaWin32kRect& rect, uint32_t style, bool hasMenu) noexcept {
    Window metricsWindow{};
    metricsWindow.style = style;
    const int64_t frame = frameInset(metricsWindow);
    const int64_t caption = captionHeight(metricsWindow);
    // Child windows cannot own a menu bar. Match the client-origin and
    // client-size calculations, which only reserve menu space for top-level
    // windows that actually have an attached menu.
    const int64_t menu = hasMenu && !(style & kWsChild) ? kMenuBarHeight : 0;
    const int64_t left = static_cast<int64_t>(rect.left) - frame;
    const int64_t top = static_cast<int64_t>(rect.top) - frame - caption - menu;
    const int64_t right = static_cast<int64_t>(rect.right) + frame;
    const int64_t bottom = static_cast<int64_t>(rect.bottom) + frame;
    if (left < INT32_MIN || top < INT32_MIN || right > INT32_MAX ||
        bottom > INT32_MAX || right < left || bottom < top)
        return false;
    rect = {static_cast<int32_t>(left), static_cast<int32_t>(top),
            static_cast<int32_t>(right), static_cast<int32_t>(bottom)};
    return true;
}

bool windowScreenOrigin(const Window& window, int64_t& screenX,
                        int64_t& screenY) noexcept {
    screenX = 0;
    screenY = 0;
    const Window* current = &window;
    for (size_t depth = 0; depth < g_windows.capacity(); ++depth) {
        screenX += current->x;
        screenY += current->y;
        // A parent on an overlapped/popup window is its owner. Owned windows
        // keep screen coordinates; only WS_CHILD coordinates are relative to
        // their parent's client origin.
        if (!current->parent || !(current->style & kWsChild)) {
            return screenX >= INT32_MIN && screenX <= INT32_MAX &&
                screenY >= INT32_MIN && screenY <= INT32_MAX;
        }
        const auto* parent = findWindowAny(current->parent);
        if (!parent || parent->ownerProcessId != current->ownerProcessId)
            return false;
        screenX += frameInset(*parent);
        screenY += frameInset(*parent) + captionHeight(*parent) +
            menuHeight(*parent);
        current = parent;
    }
    return false;
}

bool hasVisibleAncestors(const Window& window) noexcept {
    const Window* current = &window;
    for (size_t depth = 0; current->parent && depth < g_windows.capacity(); ++depth) {
        const auto* parent = findWindowAny(current->parent);
        if (!parent || !parent->active || !parent->visible || parent->iconic ||
            parent->ownerProcessId != current->ownerProcessId) return false;
        current = parent;
    }
    return current->parent == 0;
}

void clearPendingDamage() noexcept {
    g_pendingDirtyRegionCount = 0;
    g_pendingFullRedraw = false;
    for (auto& window : g_windows) {
        window.dirtyLeft = window.dirtyTop = 0;
        window.dirtyRight = window.dirtyBottom = 0;
    }
}

bool ensurePendingDamageCapacity(size_t required) noexcept {
    if (required <= g_pendingDirtyRegionCapacity) return true;
    if (required > UINT32_MAX || required > SIZE_MAX / sizeof(MicaCompositorDirtyRect))
        return false;
    size_t next = g_pendingDirtyRegionCapacity ? g_pendingDirtyRegionCapacity : 16;
    while (next < required) {
        if (next > UINT32_MAX / 2u) { next = required; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(MicaCompositorDirtyRect)) return false;
    auto* replacement = new (std::nothrow_t{}) MicaCompositorDirtyRect[next]{};
    if (!replacement) return false;
    if (g_pendingDirtyRegionCount)
        std::copy_n(g_pendingDirtyRegions, g_pendingDirtyRegionCount, replacement);
    delete[] g_pendingDirtyRegions;
    g_pendingDirtyRegions = replacement;
    g_pendingDirtyRegionCapacity = next;
    return true;
}

void markDamageRect(int64_t left, int64_t top,
                    int64_t right, int64_t bottom) noexcept {
    left = std::max<int64_t>(left, 0);
    top = std::max<int64_t>(top, 0);
    right = std::min<int64_t>(right, g_screenWidth);
    bottom = std::min<int64_t>(bottom, g_screenHeight);
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
    if (!ensurePendingDamageCapacity(
            static_cast<size_t>(g_pendingDirtyRegionCount) + 1)) {
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
    int64_t left{}, top{};
    if (!windowScreenOrigin(window, left, top)) return;
    // The compositor's themed shadow extends beyond the HWND bounds. Include
    // that apron when a window moves, hides, minimizes, or is destroyed so its
    // old shadow pixels are repainted from the desktop and lower windows.
    int64_t shadow{};
    // The compositor metadata view exists only in the compositor process.
    // Window mutations can arrive from any client process, so damage
    // calculation must use Win32k's kernel-owned theme snapshot.
    if (MicaThemeSnapshotIsValid(&g_themeSnapshot, sizeof(g_themeSnapshot))) {
        const auto& windowStyle =
            g_themeSnapshot.styles[14u * MICANT_THEME_STATE_COUNT];
        const auto& headerbarStyle =
            g_themeSnapshot.styles[13u * MICANT_THEME_STATE_COUNT];
        if ((windowStyle.flags & MICANT_THEME_HAS_RADIUS) &&
            windowStyle.borderRadius)
            shadow = windowStyle.borderRadius;
        else if ((headerbarStyle.flags & MICANT_THEME_HAS_BACKGROUND) &&
                 !window.maximized)
            shadow = headerbarStyle.borderRadius;
    }
    markDamageRect(left - shadow, top - shadow,
                   left + window.width + shadow,
                   top + window.height + shadow);
}

void markCursorChanged() noexcept {
    if (!g_mousePositionValid) return;
    // Cursor motion is presented as a scanout overlay. Wake the compositor
    // with a new metadata generation without invalidating or recomposing scene
    // pixels beneath the cursor.
    g_damageChangedSinceLastPublish = true;
}

void markCaptionButtonBounds(const Window& window, uint8_t button) noexcept {
    if (button < 1 || button > 3 || !window.active || !window.visible ||
        window.iconic) return;
    const int64_t frame = frameInset(window);
    const int64_t caption = captionHeight(window);
    if (!caption) return;
    const int64_t closeLeft = static_cast<int64_t>(window.width) - frame - 30;
    int64_t left{}, right{};
    if (button == 1) {
        left = closeLeft - 58;
        right = closeLeft - 30;
    } else if (button == 2) {
        left = closeLeft - 29;
        right = closeLeft - 1;
    } else {
        left = closeLeft;
        right = static_cast<int64_t>(window.width) - frame - 2;
    }
    int64_t screenX{}, screenY{};
    if (!windowScreenOrigin(window, screenX, screenY)) return;
    const int64_t top = screenY + frame + 3;
    const int64_t bottom = screenY + frame + caption - 3;
    if (left >= right || top >= bottom) return;
    markDamageRect(screenX + left, top, screenX + right, bottom);
}

void markWindowClientDamage(Window& window, int64_t left, int64_t top,
                            int64_t right, int64_t bottom) noexcept {
    const int64_t frame = frameInset(window);
    const int64_t caption = captionHeight(window);
    const int64_t menu = menuHeight(window);
    const int64_t clientWidth = std::max<int64_t>(1, window.width - frame * 2);
    const int64_t clientHeight = std::max<int64_t>(1,
        window.height - frame * 2 - caption - menu);
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
    int64_t screenX{}, screenY{};
    if (!windowScreenOrigin(window, screenX, screenY)) return;
    markDamageRect(screenX + frame + left,
        screenY + frame + caption + menu + top,
        screenX + frame + right,
        screenY + frame + caption + menu + bottom);
}

bool beginWindowWriter(Window& window, uint64_t processId) noexcept {
    if (!processId || window.activeWriters == UINT32_MAX) return false;
    for (auto* owner = window.writerOwners; owner; owner = owner->next) {
        if (owner->processId == processId) {
            if (owner->count == UINT32_MAX) return false;
            ++owner->count;
            ++window.activeWriters;
            return true;
        }
    }
    auto* owner = new (std::nothrow_t{}) Window::WriterOwner{};
    if (!owner) return false;
    owner->processId = processId;
    owner->count = 1;
    owner->next = window.writerOwners;
    window.writerOwners = owner;
    ++window.activeWriters;
    return true;
}

bool endWindowWriter(Window& window, uint64_t processId) noexcept {
    auto** link = &window.writerOwners;
    while (*link) {
        auto* owner = *link;
        if (owner->processId != processId || !owner->count) {
            link = &owner->next;
            continue;
        }
        if (!window.activeWriters) return false;
        if (!--owner->count) {
            *link = owner->next;
            delete owner;
        }
        --window.activeWriters;
        return true;
    }
    return false;
}

void clearWindowWriters(Window& window) noexcept {
    while (window.writerOwners) {
        auto* owner = window.writerOwners;
        window.writerOwners = owner->next;
        delete owner;
    }
    window.activeWriters = 0;
}

void removeWindowWriterOwner(Window& window, uint64_t processId) noexcept {
    auto** link = &window.writerOwners;
    while (*link) {
        auto* owner = *link;
        if (owner->processId != processId) {
            link = &owner->next;
            continue;
        }
        window.activeWriters -= std::min(window.activeWriters, owner->count);
        *link = owner->next;
        delete owner;
    }
}

uint8_t captionButtonAt(const Window& window, int32_t localX,
                       int32_t localY) noexcept {
    const int32_t frame = frameInset(window);
    const int32_t caption = captionHeight(window);
    if (!caption || localY < frame + 3 || localY >= frame + caption - 3) return 0;
    const int32_t closeLeft = window.width - frame - 30;
    if (localX >= closeLeft && localX < window.width - frame - 2) return 3;
    if (localX >= closeLeft - 29 && localX < closeLeft - 1) return 2;
    if (localX >= closeLeft - 58 && localX < closeLeft - 30) return 1;
    return 0;
}

void beginWindowAnimation(Window& window, uint32_t mode) noexcept {
    if (!window.hasTaskbarButtonBounds ||
        (mode != kWindowAnimationMinimize && mode != kWindowAnimationRestore)) {
        window.animationFlags = 0;
        return;
    }
    window.animationFlags = MICANT_COMPOSITOR_ANIMATION_ACTIVE |
        (mode == kWindowAnimationMinimize
            ? MICANT_COMPOSITOR_ANIMATION_MINIMIZE : 0u);
    window.animationTarget = window.taskbarButtonBounds;
    if (++window.animationGeneration == 0) ++window.animationGeneration;
}
uint32_t g_mouseButtons{};
void (*g_compositorPublishCallback)() noexcept{};

bool equalText(const uint16_t* first, const uint16_t* second) noexcept {
    for (size_t index = 0; index < MICANT_WIN32K_TEXT_CAPACITY; ++index) {
        const auto foldAscii = [](uint16_t character) noexcept {
            return character >= static_cast<uint16_t>(L'a') &&
                    character <= static_cast<uint16_t>(L'z')
                ? static_cast<uint16_t>(character - (L'a' - L'A'))
                : character;
        };
        if (foldAscii(first[index]) != foldAscii(second[index])) return false;
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

Window* rootWindow(Window* window) noexcept {
    for (size_t depth = 0; window && window->parent &&
         (window->style & kWsChild) &&
         depth < g_windows.capacity(); ++depth) {
        auto* parent = findWindowAny(window->parent);
        if (!parent || parent->ownerProcessId != window->ownerProcessId) break;
        window = parent;
    }
    return window;
}

bool ensureCompositorSlot(Window& window) noexcept {
    auto* root = rootWindow(&window);
    if (!root) return false;
    if (root->compositorSlot == UINT32_MAX) {
        size_t slot = 0;
        for (;;) {
            bool occupied = false;
            for (const auto& candidate : g_windows)
                if (candidate.active && candidate.compositorSlot == slot) {
                    occupied = true;
                    break;
                }
            if (!occupied) break;
            if (++slot == UINT32_MAX) return false;
        }
        root->compositorSlot = static_cast<uint32_t>(slot);
    }
    window.compositorSlot = root->compositorSlot;
    return true;
}

uint64_t visibleOrder(const Window& window) noexcept {
    auto* root = rootWindow(const_cast<Window*>(&window));
    const uint64_t rootOrder = root ? root->zOrder : window.zOrder;
    uint32_t depth = 0;
    for (auto* current = &window; current && current != root &&
         (current->style & kWsChild) && depth < 255u;
         current = findWindowAny(current->parent)) ++depth;
    return rootOrder <= (UINT32_MAX - 255u) / 256u
        ? rootOrder * 256u + depth : UINT32_MAX;
}

bool belongsToWindow(const Window* window, uint64_t ancestorHandle) noexcept {
    for (size_t depth = 0; window && depth < g_windows.capacity(); ++depth) {
        if (window->handle == ancestorHandle) return true;
        window = findWindowAny(window->parent);
    }
    return false;
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
    // WM_PAINT is an invalidation notification, not a work item that needs one
    // queue entry per caller. BeginPaint computes the current invalid region,
    // so a single pending message covers every invalidation up to dispatch.
    if (message.message == kWmPaint) {
        for (auto& slot : g_messages) {
            if (!slot.active || slot.message != kWmPaint ||
                slot.hwnd != message.hwnd ||
                slot.ownerProcessId != message.ownerProcessId) continue;
            slot.time = message.time;
            return true;
        }
    }
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
        int64_t originX{}, originY{};
        if (!windowScreenOrigin(window, originX, originY)) continue;
        const int64_t localX = static_cast<int64_t>(x) - originX;
        const int64_t localY = static_cast<int64_t>(y) - originY;
        if (!window.active || !window.visible || window.iconic ||
            !hasVisibleAncestors(window) || localX < 0 ||
            localY < 0 || localX >= window.width || localY >= window.height) continue;
        // ReactOS co_WinPosSearchChildren does not return a disabled WS_CHILD
        // from mouse hit testing. Let the parent/sibling underneath receive
        // the hit instead; a disabled top-level window remains a hit target.
        if ((window.style & kWsDisabled) &&
            (window.parent || (window.style & kWsChild))) continue;
        const uint64_t order = visibleOrder(window);
        const uint64_t topmostOrder = topmost ? visibleOrder(*topmost) : 0;
        if (!topmost || order > topmostOrder ||
            (order == topmostOrder && window.zOrder > topmost->zOrder))
            topmost = &window;
    }
    return topmost;
}

Window* topmostVisibleWindow() noexcept {
    Window* topmost{};
    for (auto& window : g_windows) {
        if (!window.active || !window.visible || window.iconic ||
            !hasVisibleAncestors(window)) continue;
        if (!topmost || window.zOrder > topmost->zOrder) topmost = &window;
    }
    return topmost;
}

Window* topmostVisibleTopLevelWindow() noexcept {
    Window* topmost{};
    for (auto& window : g_windows) {
        if (!window.active || !window.visible || window.iconic ||
            (window.style & kWsChild)) continue;
        if (!topmost || window.zOrder > topmost->zOrder) topmost = &window;
    }
    return topmost;
}

int32_t hitTestWindow(const Window& window, int32_t x, int32_t y) noexcept {
    constexpr int32_t HTCLIENT = 1, HTCAPTION = 2, HTLEFT = 10, HTRIGHT = 11;
    constexpr int32_t HTTOP = 12, HTTOPLEFT = 13, HTTOPRIGHT = 14;
    constexpr int32_t HTBOTTOM = 15, HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;
    int64_t originX{}, originY{};
    if (!windowScreenOrigin(window, originX, originY)) return 0;
    const int32_t localX = static_cast<int32_t>(static_cast<int64_t>(x) - originX);
    const int32_t localY = static_cast<int32_t>(static_cast<int64_t>(y) - originY);
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

bool setFocusedWindow(Window* window) noexcept {
    const uint64_t previousHandle = g_focusedWindow;
    if (!window) {
        if (!previousHandle) return true;
        auto* previous = findWindowAny(previousHandle);
        if (previous && !enqueueWindowMessage(*previous, 0x0008u, 0, 0))
            return false;
        if (previous) markWindowBounds(*previous);
        g_focusedWindow = 0;
        g_focusedWindowOwner = 0;
        return true;
    }
    if (!window->active || !window->visible || window->iconic) return false;
    if (previousHandle == window->handle) return true;
    auto* previous = findWindowAny(previousHandle);
    if (previous && !enqueueWindowMessage(*previous, 0x0008u, window->handle, 0))
        return false;
    if (!enqueueWindowMessage(*window, kWmSetFocus, previousHandle, 0)) return false;
    if (previous) markWindowBounds(*previous);
    markWindowBounds(*window);
    g_focusedWindow = window->handle;
    g_focusedWindowOwner = window->ownerProcessId;
    return true;
}

bool activateWindow(Window* target) noexcept {
    auto* root = rootWindow(target);
    if (!root || !root->active || !root->visible || root->iconic) return false;
    if (g_foregroundWindow != root->handle) {
        auto* previous = findWindowAny(g_foregroundWindow);
        if (previous && !enqueueWindowMessage(*previous, kWmActivate, 0, 0))
            return false;
        if (!enqueueWindowMessage(*root, kWmActivate, 1, 0)) return false;
        if (previous) markWindowBounds(*previous);
        markWindowBounds(*root);
        g_foregroundWindow = root->handle;
    }
    root->zOrder = nextZOrder();
    return setFocusedWindow(target);
}

bool hasQueuedWindowMessage(const Window& window, uint32_t message) noexcept {
    for (const auto& queued : g_messages) {
        if (queued.active && queued.hwnd == window.handle &&
            queued.ownerProcessId == window.ownerProcessId &&
            queued.message == message) return true;
    }
    return false;
}

bool enqueueLatestWindowMessage(const Window& window, uint32_t message,
                                uint64_t wParam, int64_t lParam,
                                int32_t x = 0, int32_t y = 0) noexcept {
    for (auto& queued : g_messages) {
        if (!queued.active || queued.hwnd != window.handle ||
            queued.ownerProcessId != window.ownerProcessId ||
            queued.message != message) continue;
        queued.wParam = wParam;
        queued.lParam = lParam;
        queued.time = ++g_messageTime;
        queued.x = x;
        queued.y = y;
        return true;
    }
    return enqueueWindowMessage(window, message, wParam, lParam, x, y);
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
                                  static_cast<int64_t>(g_screenWidth) - visibleCaption);
        top = std::clamp<int64_t>(top, 0,
                                 static_cast<int64_t>(g_screenHeight) - 28);
    } else {
        constexpr int32_t HTLEFT = 10, HTRIGHT = 11, HTTOP = 12;
        constexpr int32_t HTTOPLEFT = 13, HTTOPRIGHT = 14, HTBOTTOM = 15;
        constexpr int32_t HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;
        const int64_t minWidth = 160 + frameInset(*window) * 2;
        const int64_t minHeight = 100 + frameInset(*window) * 2 +
                                  captionHeight(*window) + menuHeight(*window);
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
    const size_t requiredMessages =
        static_cast<size_t>(moved && !hasQueuedWindowMessage(*window, kWmMove)) +
        static_cast<size_t>(sized && !hasQueuedWindowMessage(*window, kWmSize));
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
        (void)enqueueLatestWindowMessage(*window, kWmMove, 0,
                                         static_cast<int64_t>(packedPosition));
    }
    if (sized) {
        const int32_t frame = frameInset(*window);
        const int32_t caption = captionHeight(*window);
        const uint32_t clientWidth = static_cast<uint32_t>(
            std::max(1, newWidth - frame * 2));
        const uint32_t clientHeight = static_cast<uint32_t>(
            std::max(1, newHeight - frame * 2 - caption - menuHeight(*window)));
        const uint32_t packedSize = (clientHeight & 0xFFFFu) << 16 |
                                    (clientWidth & 0xFFFFu);
        (void)enqueueLatestWindowMessage(*window, kWmSize, 0,
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
    if (message.ownerProcessId != ownerProcessId ||
        (minimum && message.message < minimum) ||
        (maximum && message.message > maximum)) return false;
    if (!requestedWindow || requestedWindow == message.hwnd) return true;

    // GetMessage/PeekMessage with a non-null HWND include messages addressed
    // to that window and any descendant. Controls otherwise never receive
    // mouse input when an application filters its loop to the top-level HWND.
    uint64_t ancestor = message.hwnd;
    for (size_t depth = 0; depth < 64 && ancestor; ++depth) {
        const auto* window = findWindow(ancestor, ownerProcessId);
        if (!window) return false;
        ancestor = reinterpret_cast<uint64_t>(window->parent);
        if (ancestor == requestedWindow) return true;
    }
    return false;
}

bool dequeue(MicaWin32kRequest& request, uint64_t ownerProcessId) noexcept {
    QueuedMessage* oldest{};
    for (auto& message : g_messages) {
        if (!message.active) continue;
        // DestroyWindow invalidates the HWND immediately. Input already
        // queued for that window must not be dispatched later to a recycled
        // user-mode WndProc/session (notably a modal menu popup).
        if (message.hwnd && !findWindow(message.hwnd, message.ownerProcessId)) {
            message.active = false;
            continue;
        }
        if (!messageMatches(message, request, ownerProcessId)) continue;
        // Queue slots are reused from the start of the fixed array. Slot order
        // therefore ceases to represent enqueue order after messages are
        // removed. Select by the monotonically incremented Win32 message time
        // instead, using signed modular comparison so the uint32_t clock may
        // wrap while the bounded queue still has an unambiguous ordering.
        if (!oldest || static_cast<int32_t>(message.time - oldest->time) < 0)
            oldest = &message;
    }
    if (!oldest) return false;
    request.messageHwnd = oldest->hwnd;
    request.messageWParam = oldest->wParam;
    request.messageLParam = oldest->lParam;
    request.messageId = oldest->message;
    request.messageTime = oldest->time;
    request.messageX = oldest->x;
    request.messageY = oldest->y;
    if (request.args[3]) oldest->active = false;
    return true;
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
        windowClass.windowExtraBytes = static_cast<uint32_t>(request.args[1]);
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
    if (width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX ||
        x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
        x + width > INT32_MAX || y + height > INT32_MAX) {
        return NtStatus::InvalidParameter;
    }
    size_t index = 0;
    while (index < g_windows.capacity() && g_windows[index].active) ++index;
    if (index == g_windows.capacity()) {
        const size_t oldCapacity = g_windows.capacity();
        if (!g_windows.grow()) return NtStatus::InsufficientResources;
        index = oldCapacity;
    }
    {
        auto& window = g_windows[index];
        window = {};
        window.active = true;
        window.handle = makeHandle(index);
        window.parent = request.args[6];
        window.menu = request.args[7];
        window.extendedStyle = static_cast<uint32_t>(request.args[0]);
        window.style = static_cast<uint32_t>(request.args[1]);
        window.windowExtraBytes = windowClass->windowExtraBytes;
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
        // A top-level window created with WS_VISIBLE is immediately eligible
        // for composition and painting. ShowWindow cannot be relied on to
        // assign the slot or invalidate it: callers may create an already-
        // visible window and enter their message loop directly.
        if (window.visible && !(window.style & kWsChild)) {
            if (freeMessageSlots() < 5 || !ensureCompositorSlot(window) ||
                !activateWindow(&window) ||
                !enqueueWindowMessage(window, kWmPaint, 0, 0)) {
                window = {};
                return NtStatus::InsufficientResources;
            }
        } else if (window.visible &&
                   !enqueueWindowMessage(window, kWmPaint, 0, 0)) {
            window = {};
            return NtStatus::InsufficientResources;
        }
        request.result = window.handle;
        markWindowBounds(window);
        return NtStatus::Success;
    }
    return NtStatus::InsufficientResources;
}

} // namespace

NtStatus setWindowIconData(MicaWin32kRequest& request,
                           uint64_t ownerProcessId, const uint32_t* pixels,
                           uint32_t width, uint32_t height) noexcept {
    const uint32_t type = static_cast<uint32_t>(request.args[1]);
    const uint64_t icon = request.args[2];
    if (type >= 3u ||
        (icon && (!pixels || !width || !height ||
                  height > SIZE_MAX / (static_cast<size_t>(width) * sizeof(uint32_t)))) ||
        (!icon && (pixels || width || height)))
        return NtStatus::InvalidParameter;
    StateLockGuard lock;
    auto* window = findWindow(request.args[0], ownerProcessId);
    if (!window) return NtStatus::InvalidHandle;
    auto& image = window->iconImages[type];
    request.result = window->icons[type];
    window->icons[type] = icon;
    CompositorSurfaceStorage replacement{};
    if (icon) {
        if (!g_createCompositorSurface) return NtStatus::DeviceNotReady;
        const auto status = g_createCompositorSurface(width, height, replacement);
        if (status != NtStatus::Success) return status;
        std::copy_n(pixels, static_cast<size_t>(width) * height,
                    reinterpret_cast<uint32_t*>(replacement.ownerAddress));
    }
    if (image.storage.sectionReference) {
        if (!g_releaseCompositorSurface) {
            if (replacement.sectionReference) (void)g_releaseCompositorSurface(replacement, true);
            return NtStatus::DeviceNotReady;
        }
        const auto status = g_releaseCompositorSurface(image.storage, true);
        if (status != NtStatus::Success) {
            if (replacement.sectionReference) (void)g_releaseCompositorSurface(replacement, true);
            return status;
        }
    }
    image.storage = replacement;
    image.width = width;
    image.height = height;
    image.stride = width;
    if (++image.generation == 0) ++image.generation;
    markWindowBounds(*window);
    return NtStatus::Success;
}

NtStatus setWindowTitleSurface(MicaWin32kRequest& request,
                               uint64_t ownerProcessId, const uint32_t* pixels,
                               uint32_t width, uint32_t height,
                               uint32_t stride) noexcept {
    if ((!pixels && (width || height || stride)) ||
        (pixels && (!width || !height ||
                    height > SIZE_MAX / (static_cast<size_t>(width) * sizeof(uint32_t)) ||
                    stride < width ||
                    height > SIZE_MAX / (static_cast<size_t>(stride) * sizeof(uint32_t)))) )
        return NtStatus::InvalidParameter;
    StateLockGuard lock;
    auto* window = findWindow(request.args[0], ownerProcessId);
    if (!window) return NtStatus::InvalidHandle;
    auto& surface = window->titleSurface;
    CompositorSurfaceStorage replacement{};
    if (pixels) {
        if (!g_createCompositorSurface) return NtStatus::DeviceNotReady;
        const auto status = g_createCompositorSurface(width, height, replacement);
        if (status != NtStatus::Success) return status;
        auto* destination = reinterpret_cast<uint32_t*>(replacement.ownerAddress);
        for (uint32_t row = 0; row < height; ++row)
            std::copy_n(pixels + static_cast<size_t>(row) * stride, width,
                        destination + static_cast<size_t>(row) * width);
    }
    if (surface.storage.sectionReference) {
        if (!g_releaseCompositorSurface) return NtStatus::DeviceNotReady;
        const auto status = g_releaseCompositorSurface(surface.storage, true);
        if (status != NtStatus::Success) {
            if (replacement.sectionReference) (void)g_releaseCompositorSurface(replacement, true);
            return status;
        }
    }
    surface.storage = replacement;
    surface.width = width;
    surface.height = height;
    surface.stride = width;
    if (++surface.generation == 0) ++surface.generation;
    markWindowBounds(*window);
    request.result = surface.generation;
    return NtStatus::Success;
}

NtStatus dispatchRequest(MicaWin32kRequest& request, uint64_t ownerProcessId) noexcept {
    StateLockGuard lock;
    request.result = 0;
    switch (request.operation) {
        case MICANT_WIN32K_OP_REGISTER_CLASS:
            return registerClass(request, ownerProcessId);
        case MICANT_WIN32K_OP_GET_SYSTEM_METRICS:
            if (request.args[0] == 0) request.result = static_cast<uint32_t>(g_screenWidth);
            else if (request.args[0] == 1) request.result = static_cast<uint32_t>(g_screenHeight);
            else return NtStatus::NotSupported;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_GET_DLG_ITEM: {
            const auto* parent = findWindow(request.args[0], ownerProcessId);
            if (!parent) return NtStatus::InvalidHandle;
            const uint32_t controlId = static_cast<uint32_t>(request.args[1]);
            for (const auto& window : g_windows) {
                if (window.active && window.ownerProcessId == ownerProcessId &&
                    window.parent == parent->handle &&
                    static_cast<uint32_t>(window.menu) == controlId) {
                    request.result = window.handle;
                    return NtStatus::Success;
                }
            }
            return NtStatus::ObjectNameNotFound;
        }
        case MICANT_WIN32K_OP_GET_CLASS_NAME: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const uint32_t capacity = static_cast<uint32_t>(request.args[1]);
            if (!capacity) return NtStatus::InvalidParameter;
            const WindowClass* windowClass{};
            for (const auto& candidate : g_classes) {
                if (candidate.active && candidate.ownerProcessId == ownerProcessId &&
                    candidate.atom == window->classAtom) {
                    windowClass = &candidate;
                    break;
                }
            }
            if (!windowClass) return NtStatus::ObjectNameNotFound;
            size_t length = 0;
            while (length < MICANT_WIN32K_TEXT_CAPACITY && windowClass->name[length]) ++length;
            const size_t copied = std::min<size_t>(length, capacity - 1);
            for (size_t index = 0; index < copied; ++index)
                request.text[index] = windowClass->name[index];
            request.text[copied] = 0;
            request.result = copied;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_ENUM_CHILD_WINDOWS: {
            const auto* parent = findWindow(request.args[0], ownerProcessId);
            if (!parent) return NtStatus::InvalidHandle;
            const uint64_t first = request.args[1];
            const size_t capacity = std::min<uint64_t>(request.args[2],
                MICANT_WIN32K_ENUM_BATCH_CAPACITY);
            if (!capacity) return NtStatus::InvalidParameter;
            uint64_t matchingIndex = 0;
            size_t returned = 0;
            for (const auto& candidate : g_windows) {
                if (!candidate.active || candidate.ownerProcessId != ownerProcessId ||
                    candidate.handle == parent->handle) continue;
                uint64_t ancestor = candidate.parent;
                bool descendant = false;
                for (size_t depth = 0; ancestor && depth < g_windows.capacity(); ++depth) {
                    if (ancestor == parent->handle) { descendant = true; break; }
                    const auto* ancestorWindow = findWindow(ancestor, ownerProcessId);
                    if (!ancestorWindow) break;
                    ancestor = ancestorWindow->parent;
                }
                if (!descendant) continue;
                if (matchingIndex++ < first) continue;
                request.windowHandles[returned++] = candidate.handle;
                if (returned == capacity) break;
            }
            request.result = returned;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_ENABLE_WINDOW: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const bool wasDisabled = (window->style & kWsDisabled) != 0;
            const bool requestedDisabled = request.args[1] == 0;
            if (requestedDisabled) window->style |= kWsDisabled;
            else window->style &= ~kWsDisabled;
            request.result = wasDisabled ? 1u : 0u;
            request.args[2] = wasDisabled != requestedDisabled ? 1u : 0u;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_PARENT: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            request.result = window->parent;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_WINDOW_MENU: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            window->menu = request.args[1];
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_WINDOW_MENU: {
            const auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            request.result = window->menu;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_TRACK_MOUSE_EVENT: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const uint32_t flags = static_cast<uint32_t>(request.args[1]);
            constexpr uint32_t supported = kTmeHover | kTmeLeave | kTmeNonClient |
                                           kTmeCancel | kTmeQuery;
            if (flags & ~supported) return NtStatus::InvalidParameter;
            if (flags & kTmeQuery) {
                if (flags != kTmeQuery) return NtStatus::InvalidParameter;
                request.result = window->mouseTrackingFlags;
                request.args[3] = window->mouseHoverTime;
                return NtStatus::Success;
            }
            if (flags & kTmeCancel) {
                if (flags & ~(kTmeCancel | kTmeHover | kTmeLeave | kTmeNonClient))
                    return NtStatus::InvalidParameter;
                const uint32_t cancel = flags & (kTmeHover | kTmeLeave);
                window->mouseTrackingFlags &= ~(cancel ? cancel : (kTmeHover | kTmeLeave));
                if (!(window->mouseTrackingFlags & kTmeHover)) {
                    window->mouseHoverStarted = false;
                    window->mouseHoverFired = false;
                }
                if (!(window->mouseTrackingFlags & (kTmeHover | kTmeLeave)))
                    window->mouseTrackingFlags = 0;
                request.result = 1;
                return NtStatus::Success;
            }
            const uint32_t tracking = flags & (kTmeHover | kTmeLeave | kTmeNonClient);
            if (!(tracking & (kTmeHover | kTmeLeave)) ||
                ((tracking & kTmeNonClient) && !(tracking & (kTmeHover | kTmeLeave))))
                return NtStatus::InvalidParameter;
            window->mouseTrackingFlags |= tracking;
            if (tracking & kTmeHover) {
                uint32_t hoverTime = static_cast<uint32_t>(request.args[2]);
                if (!hoverTime || hoverTime == UINT32_MAX) hoverTime = 400;
                window->mouseHoverTime = hoverTime;
                window->mouseHoverStarted = false;
                window->mouseHoverFired = false;
                if (g_mousePositionValid && g_cursorHitWindow == window->handle) {
                    uint64_t counter{}, frequency{};
                    if (!ke::KeQueryPerformanceCounterValue(counter, frequency) || !frequency)
                        return NtStatus::DeviceNotReady;
                    window->mouseHoverStartCounter = counter;
                    window->mouseHoverX = g_mouseX;
                    window->mouseHoverY = g_mouseY;
                    window->mouseHoverStarted = true;
                }
            }
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_PUMP_INPUT: {
            uint64_t counter{}, frequency{};
            if (!ke::KeQueryPerformanceCounterValue(counter, frequency) || !frequency)
                return NtStatus::DeviceNotReady;
            for (auto& window : g_windows) {
                if (!window.active || !window.mouseHoverStarted || window.mouseHoverFired ||
                    !(window.mouseTrackingFlags & kTmeHover) ||
                    g_cursorHitWindow != window.handle || !g_mousePositionValid)
                    continue;
                if (window.mouseHoverX != g_mouseX || window.mouseHoverY != g_mouseY) {
                    window.mouseHoverX = g_mouseX;
                    window.mouseHoverY = g_mouseY;
                    window.mouseHoverStartCounter = counter;
                    continue;
                }
                if (counter < window.mouseHoverStartCounter) {
                    window.mouseHoverStartCounter = counter;
                    continue;
                }
                const uint64_t elapsed = counter - window.mouseHoverStartCounter;
                const uint64_t delayMs = window.mouseHoverTime;
                const uint64_t whole = frequency / 1000;
                const uint64_t remainder = frequency % 1000;
                if (delayMs && whole > UINT64_MAX / delayMs) continue;
                const uint64_t delay = whole * delayMs + (remainder * delayMs) / 1000;
                if (elapsed < delay) continue;
                int64_t originX{}, originY{};
                if (!windowScreenOrigin(window, originX, originY)) continue;
                const int32_t localX = g_mouseX - static_cast<int32_t>(originX) - frameInset(window);
                const int32_t localY = g_mouseY - static_cast<int32_t>(originY) -
                    frameInset(window) - captionHeight(window) - menuHeight(window);
                const bool nonClient = (window.mouseTrackingFlags & kTmeNonClient) != 0;
                const uint32_t message = nonClient ? kWmNcMouseHover : kWmMouseHover;
                const int64_t position = nonClient
                    ? static_cast<int64_t>((static_cast<uint32_t>(g_mouseY) << 16) |
                                           (static_cast<uint32_t>(g_mouseX) & 0xFFFFu))
                    : static_cast<int64_t>((static_cast<uint32_t>(localY) << 16) |
                                           (static_cast<uint32_t>(localX) & 0xFFFFu));
                const uintptr_t hitTest = nonClient
                    ? static_cast<uintptr_t>(hitTestWindow(window, g_mouseX, g_mouseY)) : 0;
                if (!enqueueWindowMessage(window, message, hitTest, position,
                                          g_mouseX, g_mouseY))
                    return NtStatus::InsufficientResources;
                window.mouseHoverFired = true;
                window.mouseHoverStarted = false;
                window.mouseTrackingFlags &= ~kTmeHover;
                if (!(window.mouseTrackingFlags & kTmeLeave)) window.mouseTrackingFlags = 0;
            }
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_CREATE_WINDOW:
            return createWindow(request, ownerProcessId);
        case MICANT_WIN32K_OP_GET_WINDOW_SURFACE: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (!ensureCompositorSlot(*window)) return NtStatus::InsufficientResources;
            auto* root = rootWindow(window);
            int64_t windowX{}, windowY{}, rootX{}, rootY{};
            if (!root || !windowScreenOrigin(*window, windowX, windowY) ||
                !windowScreenOrigin(*root, rootX, rootY))
                return NtStatus::InvalidParameter;
            const int32_t frame = frameInset(*window);
            const int32_t caption = captionHeight(*window);
            const int32_t menu = menuHeight(*window);
            const int32_t clientWidth = std::max(1, window->width - frame * 2);
            const int32_t clientHeight = std::max(1,
                window->height - frame * 2 - caption - menu);
            const int32_t rootFrame = frameInset(*root);
            const int32_t rootCaption = captionHeight(*root);
            const int32_t rootClientWidth = std::max(1,
                root->width - rootFrame * 2);
            const int32_t rootClientHeight = std::max(1,
                root->height - rootFrame * 2 - rootCaption);
            if (root->surface.sectionReference &&
                (root->surface.width != static_cast<uint32_t>(rootClientWidth) ||
                 root->surface.height != static_cast<uint32_t>(rootClientHeight))) {
                if (!g_releaseCompositorSurface) return NtStatus::DeviceNotReady;
                const auto releaseStatus = g_releaseCompositorSurface(root->surface, true);
                if (releaseStatus != NtStatus::Success) return releaseStatus;
                root->surface = {};
            }
            if (!root->surface.sectionReference) {
                if (!g_createCompositorSurface) return NtStatus::DeviceNotReady;
                const auto surfaceStatus = g_createCompositorSurface(
                    static_cast<uint32_t>(rootClientWidth),
                    static_cast<uint32_t>(rootClientHeight), root->surface);
                if (surfaceStatus != NtStatus::Success) return surfaceStatus;
                if (!root->surface.sectionReference || !root->surface.ownerAddress ||
                    !root->surface.compositorAddress ||
                    root->surface.width != static_cast<uint32_t>(rootClientWidth) ||
                    root->surface.height != static_cast<uint32_t>(rootClientHeight) ||
                    root->surface.pitch < root->surface.width ||
                    !root->surface.sizeBytes) {
                    if (g_releaseCompositorSurface)
                        (void)g_releaseCompositorSurface(root->surface, true);
                    root->surface = {};
                    return NtStatus::Unsuccessful;
                }
            }
            const int64_t clientX = window == root ? 0
                : windowX - rootX - rootFrame + frame;
            const int64_t clientY = window == root ? 0
                : windowY - rootY - rootFrame - rootCaption + frame + caption;
            if (clientX < 0 || clientY < 0 ||
                clientX + clientWidth > rootClientWidth ||
                clientY + clientHeight > rootClientHeight)
                return NtStatus::InvalidParameter;
            request.result = 1;
            request.args[1] = root->surface.pitch;
            request.args[2] = root->surface.height;
            request.args[3] = root->surface.ownerAddress;
            request.args[4] = root->surface.width;
            request.rect = {static_cast<int32_t>(clientX), static_cast<int32_t>(clientY),
                static_cast<int32_t>(clientX + clientWidth),
                static_cast<int32_t>(clientY + clientHeight)};
            if (window == root) request.rect.top += menu;
            if (window == root) request.rect.bottom += menu;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_LIQUID_GLASS: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            const auto& glass = request.liquidGlass;
            if (glass.magic != MICANT_LIQUID_GLASS_MAGIC ||
                glass.version != MICANT_LIQUID_GLASS_VERSION ||
                glass.size != sizeof(glass) ||
                (glass.flags & ~MICANT_LIQUID_GLASS_ACTIVE) != 0)
                return NtStatus::InvalidParameter;
            if (glass.flags & MICANT_LIQUID_GLASS_ACTIVE) {
                const auto finiteRange = [](float value, float low,
                                            float high) noexcept {
                    return value >= low && value <= high;
                };
                if (!glass.width || !glass.height ||
                    glass.width > 640 || glass.height > 640 ||
                    glass.left < 0 || glass.top < 0 ||
                    static_cast<uint64_t>(glass.left) + glass.width >
                        static_cast<uint32_t>(window->width) ||
                    static_cast<uint64_t>(glass.top) + glass.height >
                        static_cast<uint32_t>(window->height) ||
                    !glass.generation ||
                    !finiteRange(glass.radius, 0.0f, 128.0f) ||
                    !finiteRange(glass.bezel, 0.0f, 128.0f) ||
                    !finiteRange(glass.thickness, 0.0f, 128.0f) ||
                    !finiteRange(glass.dispersion, 0.0f, 1.0f) ||
                    !finiteRange(glass.blurPx, 0.0f, 64.0f) ||
                    !finiteRange(glass.tintMix, 0.0f, 1.0f) ||
                    !finiteRange(glass.saturation, 0.0f, 2.0f) ||
                    !finiteRange(glass.dark, 0.0f, 1.0f) ||
                    !finiteRange(glass.sourceBlur, 0.0f, 32.0f) ||
                    !finiteRange(glass.downsample, 0.5f, 1.0f) ||
                    !finiteRange(glass.pad, 0.0f, 32.0f) ||
                    !finiteRange(glass.rim, 0.0f, 2.0f) ||
                    !finiteRange(glass.grain, 0.0f, 0.25f) ||
                    !finiteRange(glass.magnify, 0.0f, 1.0f) ||
                    !finiteRange(glass.shadowAmt, 0.0f, 1.0f) ||
                    !finiteRange(glass.rimTint, 0.0f, 1.0f) ||
                    !finiteRange(glass.ringW, -64.0f, 64.0f) ||
                    !finiteRange(glass.flatInner, 0.0f, 1.0f))
                    return NtStatus::InvalidParameter;
            }
            window->liquidGlass = glass;
            markWindowClientDamage(*window, 0, 0,
                std::max<int32_t>(1, window->width - frameInset(*window) * 2),
                std::max<int32_t>(1, window->height - frameInset(*window) * 2 -
                    captionHeight(*window)));
            request.result = 1;
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
            if (rootWindow(window) == window && window->surface.sectionReference) {
                if (!g_releaseCompositorSurface) return NtStatus::DeviceNotReady;
                const auto releaseStatus = g_releaseCompositorSurface(window->surface, true);
                if (releaseStatus != NtStatus::Success) return releaseStatus;
                window->surface = {};
            }
            for (auto& icon : window->iconImages) {
                if (icon.storage.sectionReference && g_releaseCompositorSurface) {
                    const auto releaseStatus = g_releaseCompositorSurface(icon.storage, true);
                    if (releaseStatus != NtStatus::Success) return releaseStatus;
                }
                icon = {};
            }
            auto& titleSurface = window->titleSurface;
            if (titleSurface.storage.sectionReference && g_releaseCompositorSurface) {
                const auto releaseStatus = g_releaseCompositorSurface(titleSurface.storage, true);
                if (releaseStatus != NtStatus::Success) return releaseStatus;
            }
            titleSurface = {};
            auto* oldFocus = findWindowAny(g_focusedWindow);
            const bool wasFocus = belongsToWindow(oldFocus, window->handle);
            const bool wasForeground = g_foregroundWindow == window->handle;
            if (wasFocus) {
                (void)enqueueWindowMessage(*oldFocus, 0x0008u, 0, 0);
                g_focusedWindow = 0;
                g_focusedWindowOwner = 0;
            }
            markWindowBounds(*window);
            if (g_pointerGesture.window == window->handle) g_pointerGesture = {};
            if (g_captionCapture == window->handle) {
                g_captionCapture = 0;
                g_pressedCaptionButton = 0;
            }
            if (g_mouseCaptureWindow == window->handle) {
                g_mouseCaptureWindow = 0;
                g_mouseCaptureOwner = 0;
            }
            clearWindowWriters(*window);
            window->active = false;
            if (wasFocus && !wasForeground) {
                if (auto* parent = rootWindow(findWindowAny(window->parent));
                    parent && parent->visible && !parent->iconic)
                    (void)setFocusedWindow(parent);
            }
            if (wasForeground) {
                g_foregroundWindow = 0;
                if (auto* next = topmostVisibleTopLevelWindow()) {
                    (void)activateWindow(next);
                }
            }
            g_windows.releaseIfEmpty();
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SHOW_WINDOW: {
            // ShowWindow is a public cross-process operation: the shell taskbar
            // must be able to restore or minimize windows owned by applications.
            // Keep destruction and other ownership-sensitive operations scoped
            // to the caller, but resolve this HWND from the global window table.
            auto* window = findWindowAny(request.args[0]);
            if (!window) return NtStatus::InvalidHandle;
            const int32_t command = static_cast<int32_t>(request.args[1]);
            if (command && !(window->style & kWsChild) && !ensureCompositorSlot(*window))
                return NtStatus::InsufficientResources;
            const bool wasVisible = window->visible;
            const bool wasIconic = window->iconic;
            const bool minimize = command == kSwShowMinimized || command == kSwMinimize;
            if (minimize && !wasIconic)
                beginWindowAnimation(*window, kWindowAnimationMinimize);
            else if (wasIconic && command == kSwRestore)
                beginWindowAnimation(*window, kWindowAnimationRestore);
            else
                window->animationFlags = 0;
            if (wasVisible || wasIconic) markWindowBounds(*window);
            window->visible = command != 0;
            window->iconic = minimize;
            if (window->visible && !window->iconic) {
                window->zOrder = nextZOrder();
                if (!(window->style & kWsChild)) (void)activateWindow(window);
            } else if (belongsToWindow(findWindowAny(g_focusedWindow), window->handle)) {
                if (auto* focus = findWindowAny(g_focusedWindow))
                    (void)enqueueWindowMessage(*focus, 0x0008u, 0, 0);
                g_focusedWindow = 0;
                g_focusedWindowOwner = 0;
                if (window->parent) {
                    if (auto* parent = rootWindow(window->parent
                            ? findWindowAny(window->parent) : nullptr);
                        parent && parent->visible && !parent->iconic)
                        (void)setFocusedWindow(parent);
                }
                if (g_foregroundWindow == window->handle) {
                    g_foregroundWindow = 0;
                    if (auto* next = topmostVisibleTopLevelWindow())
                        (void)activateWindow(next);
                }
            }
            if (window->visible) markWindowBounds(*window);
            request.result = wasVisible;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_TASKBAR_BUTTON_BOUNDS: {
            auto* window = findWindowAny(request.args[0]);
            if (!window || (window->style & kWsChild)) return NtStatus::InvalidHandle;
            const auto& bounds = request.rect;
            if (bounds.left < 0 || bounds.top < 0 ||
                bounds.right > g_screenWidth || bounds.bottom > g_screenHeight ||
                bounds.left >= bounds.right || bounds.top >= bounds.bottom)
                return NtStatus::InvalidParameter;
            window->taskbarButtonBounds = bounds;
            window->hasTaskbarButtonBounds = true;
            request.result = 1;
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
                if (!(window->style & kWsChild)) g_foregroundWindow = window->handle;
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
            const auto* window = request.operation == MICANT_WIN32K_OP_GET_WINDOW_RECT
                ? findWindowAny(request.args[0])
                : findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (request.operation == MICANT_WIN32K_OP_GET_CLIENT_RECT) {
                const int32_t frame = frameInset(*window);
                const int32_t caption = captionHeight(*window);
                const int32_t menu = menuHeight(*window);
                request.rect = {0, 0,
                    window->width > frame * 2 ? window->width - frame * 2 : 1,
                    window->height > frame * 2 + caption + menu
                        ? window->height - frame * 2 - caption - menu : 1};
            } else {
                int64_t screenX{}, screenY{};
                if (!windowScreenOrigin(*window, screenX, screenY) ||
                    screenX > INT32_MAX - window->width ||
                    screenY > INT32_MAX - window->height)
                    return NtStatus::InvalidParameter;
                request.rect = MicaWin32kRect{
                    static_cast<int32_t>(screenX), static_cast<int32_t>(screenY),
                    static_cast<int32_t>(screenX + window->width),
                    static_cast<int32_t>(screenY + window->height)};
            }
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_ADJUST_WINDOW_RECT: {
            if (request.args[1] > 1) return NtStatus::InvalidParameter;
            if (!adjustWindowRect(request.rect,
                    static_cast<uint32_t>(request.args[0]), request.args[1] != 0))
                return NtStatus::InvalidParameter;
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_DEBUG_INPUT_TRACE:
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
            MicantNetbootSerialWrite("[MicaNT CalcButton] stage=");
            MicantNetbootSerialWriteNumber(request.args[0]);
            MicantNetbootSerialWrite(" id=");
            MicantNetbootSerialWriteNumber(request.args[1]);
            MicantNetbootSerialWrite(" flags=");
            MicantNetbootSerialWriteNumber(request.args[2]);
            MicantNetbootSerialWrite("\r\n");
            request.result = 1;
            return NtStatus::Success;
#elif defined(MICANT_BOOTSTRAP_REACTOS_APP_WINMINE)
            MicantNetbootSerialWrite("[MicaNT WinMine] stage=");
            MicantNetbootSerialWriteNumber(request.args[0]);
            MicantNetbootSerialWrite(" value=");
            MicantNetbootSerialWriteNumber(request.args[1]);
            MicantNetbootSerialWrite(" flags=");
            MicantNetbootSerialWriteNumber(request.args[2]);
            MicantNetbootSerialWrite("\r\n");
            request.result = 1;
            return NtStatus::Success;
#elif defined(MICANT_BOOTSTRAP_REACTOS_APP_MSPAINT)
            MicantNetbootSerialWrite("[MicaNT Paint] stage=");
            MicantNetbootSerialWriteNumber(request.args[0]);
            MicantNetbootSerialWrite(" value=");
            MicantNetbootSerialWriteNumber(request.args[1]);
            MicantNetbootSerialWrite(" flags=");
            MicantNetbootSerialWriteNumber(request.args[2]);
            MicantNetbootSerialWrite("\r\n");
            request.result = 1;
            return NtStatus::Success;
#elif defined(MICANT_BOOTSTRAP_REACTOS_APP_NOTEPAD)
            MicantNetbootSerialWrite("[MicaNT Notepad] debug hash=");
            MicantNetbootSerialWriteNumber(request.args[1]);
            MicantNetbootSerialWrite(" chars=");
            MicantNetbootSerialWriteNumber(request.args[2]);
            MicantNetbootSerialWrite("\r\n");
            request.result = 1;
            return NtStatus::Success;
#else
            return NtStatus::NotImplemented;
#endif
        case MICANT_WIN32K_OP_ENUM_WINDOWS: {
            const size_t first = static_cast<size_t>(request.args[0]);
            const size_t requested = request.args[1]
                ? static_cast<size_t>(request.args[1])
                : MICANT_WIN32K_ENUM_BATCH_CAPACITY;
            const size_t capacity = std::min<size_t>(requested,
                MICANT_WIN32K_ENUM_BATCH_CAPACITY);
            size_t ordinal = 0;
            for (const auto& window : g_windows) {
                if (!window.active || (window.style & kWsChild)) continue;
                if (ordinal++ < first) continue;
                if (request.result == capacity) break;
                request.windowHandles[request.result++] = window.handle;
            }
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_WINDOW_TEXT: {
            const auto* window = findWindowAny(request.args[0]);
            if (!window) return NtStatus::InvalidHandle;
            copyText(request.text, window->title);
            while (request.result < MICANT_WIN32K_TEXT_CAPACITY &&
                   request.text[request.result] != 0) {
                ++request.result;
            }
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_IS_WINDOW_VISIBLE: {
            const auto* window = findWindowAny(request.args[0]);
            if (!window) return NtStatus::InvalidHandle;
            request.result = window->visible;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_IS_ICONIC: {
            const auto* window = findWindowAny(request.args[0]);
            if (!window) return NtStatus::InvalidHandle;
            request.result = window->iconic;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_FOREGROUND_WINDOW:
            request.result = g_foregroundWindow;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_SET_FOREGROUND_WINDOW: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (!activateWindow(window)) return NtStatus::InsufficientResources;
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_FOCUS: {
            const uint64_t previousHandle = g_focusedWindow;
            if (!request.args[0]) {
                if (!setFocusedWindow(nullptr)) return NtStatus::InsufficientResources;
                request.result = previousHandle;
                return NtStatus::Success;
            }
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window) return NtStatus::InvalidHandle;
            if (!window->visible || window->iconic) return NtStatus::InvalidParameter;
            auto* root = rootWindow(window);
            if (!root || root->handle != g_foregroundWindow)
                return NtStatus::InvalidParameter;
            if (!setFocusedWindow(window)) return NtStatus::InsufficientResources;
            request.result = previousHandle;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_GET_FOCUS:
            request.result = g_focusedWindowOwner == ownerProcessId
                ? g_focusedWindow : 0;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_GET_CAPTURE:
            request.result = g_mouseCaptureOwner == ownerProcessId
                ? g_mouseCaptureWindow : 0;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_SET_CAPTURE: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            if (!window || !window->visible || window->iconic)
                return NtStatus::InvalidHandle;
            request.result = g_mouseCaptureOwner == ownerProcessId
                ? g_mouseCaptureWindow : 0;
            if (g_mouseCaptureWindow != window->handle) {
                if (auto* previous = findWindowAny(g_mouseCaptureWindow))
                    (void)enqueueWindowMessage(*previous, 0x0215u, window->handle, 0);
                g_mouseCaptureWindow = window->handle;
                g_mouseCaptureOwner = ownerProcessId;
            }
            if (g_leftButtonReleaseWindow &&
                g_leftButtonReleaseOwner == ownerProcessId)
                g_leftButtonReleaseWindow = window->handle;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_RELEASE_CAPTURE:
            if (g_mouseCaptureOwner != ownerProcessId || !g_mouseCaptureWindow)
                return NtStatus::InvalidDeviceState;
            if (auto* previous = findWindowAny(g_mouseCaptureWindow))
                (void)enqueueWindowMessage(*previous, 0x0215u, 0, 0);
            g_mouseCaptureWindow = 0;
            g_mouseCaptureOwner = 0;
            request.result = 1;
            return NtStatus::Success;
        case MICANT_WIN32K_OP_SET_WINDOW_ICON: {
            auto* window = findWindow(request.args[0], ownerProcessId);
            const uint32_t type = static_cast<uint32_t>(request.args[1]);
            if (!window || type > 2) return NtStatus::InvalidParameter;
            markWindowBounds(*window);
            request.result = window->icons[type];
            window->icons[type] = request.args[2];
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_WINDOW_ICON_DATA:
        case MICANT_WIN32K_OP_SET_WINDOW_TITLE_SURFACE:
            return NtStatus::InvalidParameter;
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
            const int32_t right = std::min(g_screenWidth, g_cursorClip.right);
            const int32_t bottom = std::min(g_screenHeight, g_cursorClip.bottom);
            if (right <= left || bottom <= top) return NtStatus::InvalidParameter;
            const int32_t newX = std::clamp(requestedX, left, right - 1);
            const int32_t newY = std::clamp(requestedY, top, bottom - 1);
            const bool moved = !g_mousePositionValid || newX != g_mouseX || newY != g_mouseY;
            if (moved && g_mousePositionValid) markCursorChanged();
            g_mouseX = newX;
            g_mouseY = newY;
            g_mousePositionValid = true;
            if (moved) markCursorChanged();
            request.result = 1;
            return NtStatus::Success;
        }
        case MICANT_WIN32K_OP_SET_CURSOR_CLIP: {
            if (request.args[0] == 0) {
                if (g_mousePositionValid) markCursorChanged();
                g_cursorClip = {0, 0, g_screenWidth, g_screenHeight};
                g_cursorClipOwner = 0;
                if (g_mousePositionValid) markCursorChanged();
                request.result = 1;
                return NtStatus::Success;
            }
            const auto rect = request.rect;
            if (rect.left < 0 || rect.top < 0 || rect.right > g_screenWidth ||
                rect.bottom > g_screenHeight || rect.right <= rect.left || rect.bottom <= rect.top)
                return NtStatus::InvalidParameter;
            if (g_mousePositionValid) markCursorChanged();
            g_cursorClip = rect;
            g_cursorClipOwner = ownerProcessId;
            g_mouseX = std::clamp(g_mouseX, rect.left, rect.right - 1);
            g_mouseY = std::clamp(g_mouseY, rect.top, rect.bottom - 1);
            if (g_mousePositionValid) markCursorChanged();
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
                markCursorChanged();
            if (!wasVisible && isVisible)
                markCursorChanged();
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
            } else if (index >= 0 && index % static_cast<int32_t>(sizeof(uintptr_t)) == 0 &&
                       static_cast<uint32_t>(index) <= window->windowExtraBytes &&
                       sizeof(uintptr_t) <= window->windowExtraBytes - static_cast<uint32_t>(index)) {
                const size_t slot = static_cast<uint32_t>(index) / sizeof(uintptr_t);
                request.result = window->windowExtra[slot];
                window->windowExtra[slot] = static_cast<uintptr_t>(request.args[2]);
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
            } else if (index >= 0 && index % static_cast<int32_t>(sizeof(uintptr_t)) == 0 &&
                       static_cast<uint32_t>(index) <= window->windowExtraBytes &&
                       sizeof(uintptr_t) <= window->windowExtraBytes - static_cast<uint32_t>(index)) {
                const size_t slot = static_cast<uint32_t>(index) / sizeof(uintptr_t);
                request.result = window->windowExtra[slot];
            } else if (index == kGwlStyle) {
                request.result = window->style;
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
    auto* compositor = g_compositorProcess;
    const bool compositorIsBeingReleased = compositor &&
        compositor->processId() == ownerProcessId;
    ke::KAPC_STATE attachState{};
    bool attachAttempted = false;
    bool compositorContext = !compositor || ke::KeGetCurrentProcess() == compositor;
    if (compositor && !compositorIsBeingReleased && !compositorContext) {
        attachAttempted = true;
        ke::KeStackAttachProcess(compositor, &attachState);
        compositorContext = ke::KeGetCurrentProcess() == compositor;
    }
    auto publish = g_compositorPublishCallback;
    {
        StateLockGuard lock;
        for (auto& message : g_messages)
            if (message.active && message.ownerProcessId == ownerProcessId) message.active = false;
        for (auto& window : g_windows) {
            if (!window.active || window.ownerProcessId != ownerProcessId) continue;
            markWindowBounds(window);
            if (rootWindow(&window) == &window && window.surface.sectionReference &&
                g_releaseCompositorSurface) {
                // Ps has already removed the terminating process's views.
                if (g_releaseCompositorSurface(window.surface, false) == NtStatus::Success)
                    window.surface = {};
            }
            clearWindowWriters(window);
            if (g_foregroundWindow == window.handle) g_foregroundWindow = 0;
            for (auto& icon : window.iconImages) {
                if (icon.storage.sectionReference && g_releaseCompositorSurface)
                    (void)g_releaseCompositorSurface(icon.storage, false);
                icon = {};
            }
            auto& title = window.titleSurface;
            if (title.storage.sectionReference && g_releaseCompositorSurface)
                (void)g_releaseCompositorSurface(title.storage, false);
            title = {};
            window = {};
        }
        if (g_focusedWindowOwner == ownerProcessId) {
            g_focusedWindow = 0;
            g_focusedWindowOwner = 0;
        }
        if (!g_foregroundWindow) {
            if (auto* next = topmostVisibleTopLevelWindow())
                (void)activateWindow(next);
        }
        for (auto& window : g_windows) {
            if (!window.active) continue;
            removeWindowWriterOwner(window, ownerProcessId);
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
        if (g_mouseCaptureOwner == ownerProcessId) {
            g_mouseCaptureWindow = 0;
            g_mouseCaptureOwner = 0;
        }
        if (g_leftButtonReleaseOwner == ownerProcessId) {
            g_leftButtonReleaseWindow = 0;
            g_leftButtonReleaseOwner = 0;
        }
        for (auto& windowClass : g_classes)
            if (windowClass.active && windowClass.ownerProcessId == ownerProcessId) windowClass = {};
        if (g_cursorClipOwner == ownerProcessId) {
            g_cursorClip = {0, 0, g_screenWidth, g_screenHeight};
            g_cursorClipOwner = 0;
        }
        if (g_activeCursorOwner == ownerProcessId) {
            if (g_mousePositionValid) markCursorChanged();
            // The old owner's view was removed with its address space. The
            // replacement view belongs to whichever process is current, so
            // only build it after attaching to the compositor address space.
            resetCursorImage(false, compositorContext && !compositorIsBeingReleased);
            if (g_mousePositionValid) markCursorChanged();
        }
    }
    if (publish && compositorContext && !compositorIsBeingReleased) publish();
    if (attachAttempted && compositorContext)
        ke::KeUnstackDetachProcess(&attachState);
}

bool dispatchMouseInput(const MicaWin32kInputEvent& event) noexcept {
    StateLockGuard lock;
    const int32_t left = std::max(0, g_cursorClip.left);
    const int32_t top = std::max(0, g_cursorClip.top);
    const int32_t right = std::min(g_screenWidth, g_cursorClip.right);
    const int32_t bottom = std::min(g_screenHeight, g_cursorClip.bottom);
    const int32_t x = std::clamp(event.x, left, std::max(left, right - 1));
    const int32_t y = std::clamp(event.y, top, std::max(top, bottom - 1));
    const int32_t oldMouseX = g_mouseX;
    const int32_t oldMouseY = g_mouseY;
    const bool oldMouseValid = g_mousePositionValid;
    g_mousePositionValid = true;
    const uint32_t previousButtons = g_mouseButtons;
    const uint32_t currentButtons = event.buttons & 0x07u;
    const bool moved = x != g_mouseX || y != g_mouseY;
    if (moved && oldMouseValid) markCursorChanged();
    g_mouseButtons = currentButtons;
    g_mouseX = x;
    g_mouseY = y;
    if (moved || !oldMouseValid) markCursorChanged();
    auto* target = topmostVisibleWindow(x, y);
    const int32_t hitTest = target ? hitTestWindow(*target, x, y) : 0;
    const uint64_t oldCursorHitWindow = g_cursorHitWindow;
    const int32_t oldCursorHitTest = g_cursorHitTest;
    auto* oldHover = findWindowAny(oldCursorHitWindow);
    if (oldHover && oldCursorHitWindow != (target ? target->handle : 0) &&
        (oldHover->mouseTrackingFlags & kTmeLeave)) {
        const bool nonClient = (oldHover->mouseTrackingFlags & kTmeNonClient) != 0;
        const uintptr_t hitTest = nonClient
            ? static_cast<uintptr_t>(hitTestWindow(*oldHover, oldMouseX, oldMouseY)) : 0;
        const int64_t screenPoint = static_cast<int64_t>(
            (static_cast<uint32_t>(oldMouseY) << 16) |
            (static_cast<uint32_t>(oldMouseX) & 0xFFFFu));
        if (!enqueueWindowMessage(*oldHover,
                nonClient ? kWmNcMouseLeave : kWmMouseLeave,
                hitTest, nonClient ? screenPoint : 0, oldMouseX, oldMouseY))
            return false;
        oldHover->mouseTrackingFlags &= ~kTmeLeave;
        if (!(oldHover->mouseTrackingFlags & kTmeHover))
            oldHover->mouseTrackingFlags = 0;
    }
    int64_t oldHoverX{}, oldHoverY{}, targetX{}, targetY{};
    const bool oldHoverOrigin = oldHover &&
        windowScreenOrigin(*oldHover, oldHoverX, oldHoverY);
    const bool targetOrigin = target &&
        windowScreenOrigin(*target, targetX, targetY);
    const uint8_t oldHoveredButton = oldHover && oldMouseValid && oldHoverOrigin
        ? captionButtonAt(*oldHover, oldMouseX - static_cast<int32_t>(oldHoverX),
                          oldMouseY - static_cast<int32_t>(oldHoverY)) : 0;
    const uint8_t newHoveredButton = target && targetOrigin
        ? captionButtonAt(*target, x - static_cast<int32_t>(targetX),
                          y - static_cast<int32_t>(targetY)) : 0;
    if (oldCursorHitWindow != (target ? target->handle : 0) ||
        oldCursorHitTest != hitTest || oldHoveredButton != newHoveredButton) {
        if (oldHover) markCaptionButtonBounds(*oldHover, oldHoveredButton);
        if (target) markCaptionButtonBounds(*target, newHoveredButton);
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
    if (target && (target->mouseTrackingFlags & kTmeHover) &&
        !target->mouseHoverStarted && !target->mouseHoverFired) {
        uint64_t counter{}, frequency{};
        if (ke::KeQueryPerformanceCounterValue(counter, frequency) && frequency) {
            target->mouseHoverStartCounter = counter;
            target->mouseHoverX = x;
            target->mouseHoverY = y;
            target->mouseHoverStarted = true;
        }
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
        (void)activateWindow(target);
        markWindowBounds(*target);
        target->zOrder = nextZOrder();
        // A click on a top-level client area must not steal focus from one
        // of its child controls. This matters for apps such as Notepad whose
        // edit control covers the client area and owns keyboard input.
        if (!target->parent && g_focusedWindow != target->handle) {
            auto* focused = findWindowAny(g_focusedWindow);
            if (!focused || !belongsToWindow(focused, target->handle))
                (void)setFocusedWindow(target);
        }
        int64_t targetX{}, targetY{};
        if (!windowScreenOrigin(*target, targetX, targetY)) return false;
        const uint8_t button = captionButtonAt(*target,
            static_cast<int32_t>(static_cast<int64_t>(x) - targetX),
            static_cast<int32_t>(static_cast<int64_t>(y) - targetY));
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
            beginWindowAnimation(*captured, kWindowAnimationMinimize);
            captured->iconic = true;
            (void)enqueueWindowMessage(*captured, kWmSize, 1, 0);
            if (g_foregroundWindow == captured->handle) {
                g_foregroundWindow = 0;
                if (auto* next = topmostVisibleTopLevelWindow())
                    (void)activateWindow(next);
            }
        } else if (pressedButton == 2) {
            if (!captured->maximized) {
                captured->restoreX = captured->x;
                captured->restoreY = captured->y;
                captured->restoreWidth = captured->width;
                captured->restoreHeight = captured->height;
                captured->x = 0;
                captured->y = 0;
                captured->width = g_screenWidth;
                captured->height = std::max(1, g_screenHeight - 40);
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
                captured->height > frame * 2 + caption + menuHeight(*captured)
                    ? captured->height - frame * 2 - caption - menuHeight(*captured) : 1);
            const uint32_t packedSize =
                (clientHeight & 0xFFFFu) << 16 | (clientWidth & 0xFFFFu);
            (void)enqueueWindowMessage(*captured, kWmSize,
                captured->maximized ? 2u : 0u, static_cast<int64_t>(packedSize));
        } else {
            (void)enqueueWindowMessage(*captured, kWmClose, 0, 0);
        }
        return true;
    }
    auto* routeTarget = target;
    if (g_mouseCaptureWindow && g_mouseCaptureOwner) {
        auto* capture = findWindow(g_mouseCaptureWindow, g_mouseCaptureOwner);
        if (capture && capture->visible && !capture->iconic) routeTarget = capture;
        else {
            g_mouseCaptureWindow = 0;
            g_mouseCaptureOwner = 0;
        }
    }
    if (!routeTarget) return false;

    // Keep each physical left-button release paired with the HWND that
    // received its press. The app processes the queued WM_LBUTTONDOWN in
    // Ring 3 and may call SetCapture after the next PS/2 packet was already
    // sampled; relying only on the asynchronous capture state can otherwise
    // deliver WM_LBUTTONUP to a different child control.
    if (leftPressed) {
        g_leftButtonReleaseWindow = routeTarget->handle;
        g_leftButtonReleaseOwner = routeTarget->ownerProcessId;
    } else if (leftReleased && g_leftButtonReleaseWindow) {
        auto* pressedTarget = findWindow(g_leftButtonReleaseWindow,
                                         g_leftButtonReleaseOwner);
        if (pressedTarget && pressedTarget->visible && !pressedTarget->iconic)
            routeTarget = pressedTarget;
    }

#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
    if (leftPressed || leftReleased) {
        MicantNetbootSerialWrite("[MicaNT CalcInput] event=");
        MicantNetbootSerialWrite(leftPressed ? "down" : "up");
        MicantNetbootSerialWrite(" controlId=");
        MicantNetbootSerialWriteNumber(routeTarget->parent ? routeTarget->menu : 0);
    }
#endif

    uint32_t keyState = 0;
    if (currentButtons & 0x01u) keyState |= 0x0001u;
    if (currentButtons & 0x02u) keyState |= 0x0002u;
    if (currentButtons & 0x04u) keyState |= 0x0010u;
    // Mouse message lParam coordinates are client-relative. Hit testing and
    // capture use screen coordinates above, so remove the nonclient frame and
    // caption before publishing WM_MOUSEMOVE / button messages to the app.
    const int32_t clientFrame = frameInset(*routeTarget);
    const int32_t clientCaption = captionHeight(*routeTarget);
        const int32_t clientMenu = menuHeight(*routeTarget);
    int64_t routeX{}, routeY{};
    if (!windowScreenOrigin(*routeTarget, routeX, routeY)) return false;
    const auto localX64 = static_cast<int64_t>(x) - routeX - clientFrame;
    const auto localY64 = static_cast<int64_t>(y) - routeY - clientFrame -
        clientCaption - clientMenu;
    const uint32_t localX = static_cast<uint32_t>(localX64) & 0xFFFFu;
    const uint32_t localY = static_cast<uint32_t>(localY64) & 0xFFFFu;
    const int64_t packedPoint = static_cast<int64_t>((localY << 16) | localX);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
    if (leftPressed || leftReleased) {
        MicantNetbootSerialWrite(" localX=");
        MicantNetbootSerialWriteNumber(localX);
        MicantNetbootSerialWrite(" localY=");
        MicantNetbootSerialWriteNumber(localY);
        MicantNetbootSerialWrite(" width=");
        MicantNetbootSerialWriteNumber(static_cast<uint64_t>(routeTarget->width));
        MicantNetbootSerialWrite(" height=");
        MicantNetbootSerialWriteNumber(static_cast<uint64_t>(routeTarget->height));
        MicantNetbootSerialWrite("\r\n");
    }
#endif
    if (moved && !enqueueLatestWindowMessage(*routeTarget, kWmMouseMove,
                                             keyState, packedPoint, x, y))
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
            const bool activated = activateWindow(routeTarget);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
            if (index == 0) {
                MicantNetbootSerialWrite("[MicaNT CalcButtonRoute] id=");
                MicantNetbootSerialWriteNumber(routeTarget->parent ? routeTarget->menu : 0);
                MicantNetbootSerialWrite(" enabled=");
                MicantNetbootSerialWriteNumber(
                    (routeTarget->style & kWsDisabled) ? 0u : 1u);
                MicantNetbootSerialWrite(" activate=");
                MicantNetbootSerialWriteNumber(activated ? 1u : 0u);
                MicantNetbootSerialWrite("\r\n");
            }
#endif
            if (!activated) return false;
            routeTarget->zOrder = nextZOrder();
            const bool queued = enqueueWindowMessage(*routeTarget, downMessages[index],
                keyState, packedPoint, x, y);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
            if (index == 0) {
                MicantNetbootSerialWrite("[MicaNT CalcButtonRoute] id=");
                MicantNetbootSerialWriteNumber(routeTarget->parent ? routeTarget->menu : 0);
                MicantNetbootSerialWrite(" down-queued=");
                MicantNetbootSerialWriteNumber(queued ? 1u : 0u);
                MicantNetbootSerialWrite("\r\n");
            }
#endif
            if (!queued) return false;
            buttonDelivered = true;
        } else if (!enqueueWindowMessage(*routeTarget, upMessages[index], keyState,
                                         packedPoint, x, y)) {
            return false;
        } else {
            buttonDelivered = true;
        }
    }
    if (leftReleased) {
        g_leftButtonReleaseWindow = 0;
        g_leftButtonReleaseOwner = 0;
    }
    return buttonDelivered;
}

void initializeCursorPosition(int32_t x, int32_t y) noexcept {
    StateLockGuard lock;
    const int32_t left = std::max(0, g_cursorClip.left);
    const int32_t top = std::max(0, g_cursorClip.top);
    const int32_t right = std::min(g_screenWidth, g_cursorClip.right);
    const int32_t bottom = std::min(g_screenHeight, g_cursorClip.bottom);
    x = std::clamp(x, left, std::max(left, right - 1));
    y = std::clamp(y, top, std::max(top, bottom - 1));
    if (g_mousePositionValid) markCursorChanged();
    g_mouseX = x;
    g_mouseY = y;
    g_mouseButtons = 0;
    g_mousePositionValid = true;
    markCursorChanged();
}

bool dispatchKeyboardInput(const MicaWin32kKeyboardEvent& event) noexcept {
    StateLockGuard lock;
    auto* target = findWindow(g_focusedWindow, g_focusedWindowOwner);
    if (!target || !target->visible || target->iconic)
        target = findWindowAny(g_foregroundWindow);
    if (!target || !target->visible || target->iconic || !event.virtualKey) return false;
    const bool down = (event.flags & 0x01u) != 0;
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
    if (event.virtualKey == 0x75u || event.virtualKey == 0x76u) {
        MicantNetbootSerialWrite("[MicaNT CalcKeyRoute] vk=");
        MicantNetbootSerialWriteNumber(event.virtualKey);
        MicantNetbootSerialWrite(" down=");
        MicantNetbootSerialWriteNumber(down ? 1u : 0u);
        MicantNetbootSerialWrite(" target=");
        MicantNetbootSerialWriteNumber(target->handle);
        MicantNetbootSerialWrite("\r\n");
    }
#endif
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
                         void (*publishCallback)() noexcept,
                         uint32_t* compositorDecorPixels,
                         CreateCompositorSurface createSurface,
                         ReleaseCompositorSurface releaseSurface) noexcept {
    StateLockGuard lock;
    g_compositorProcess = ke::KeGetCurrentProcess();
    g_createCompositorSurface = createSurface;
    g_releaseCompositorSurface = releaseSurface;
    if (!g_defaultCursorInitialized) {
        resetCursorImage(false);
        g_defaultCursorInitialized = true;
    }
    g_compositorStore = store;
    (void)compositorDecorPixels;
    g_compositorPublishCallback = publishCallback;
    if (store && store->screenWidth && store->screenHeight &&
        store->screenWidth <= static_cast<uint32_t>(INT32_MAX) &&
        store->screenHeight <= static_cast<uint32_t>(INT32_MAX)) {
        g_screenWidth = static_cast<int32_t>(store->screenWidth);
        g_screenHeight = static_cast<int32_t>(store->screenHeight);
        g_cursorClip = {0, 0, g_screenWidth, g_screenHeight};
    }
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
    reapRetiredMetadataStorage(acknowledged);
    if (!g_damageChangedSinceLastPublish && previousSequence &&
        !(previousSequence & 1u) && acknowledged == previousSequence)
        clearPendingDamage();
    size_t rootEntryCount = 0;
    size_t childEntryCount = 0;
    for (const auto& candidate : g_windows) {
        if (!candidate.active) continue;
        if (candidate.style & kWsChild) {
            ++childEntryCount;
        } else if (candidate.compositorSlot != UINT32_MAX) {
            rootEntryCount = std::max(rootEntryCount,
                static_cast<size_t>(candidate.compositorSlot) + 1);
        }
    }
    const size_t requiredSurfaceEntries = rootEntryCount + childEntryCount;
    const size_t requiredDirtyEntries = g_pendingFullRedraw
        ? 0 : g_pendingDirtyRegionCount;
    if ((requiredSurfaceEntries && !ensureMetadataArray<MicaCompositorSurfaceInfo>(
            g_surfaceMetadataStorage, g_surfaceMetadataCapacity,
            requiredSurfaceEntries)) ||
        (requiredDirtyEntries && !ensureMetadataArray<MicaCompositorDirtyRect>(
            g_dirtyMetadataStorage, g_dirtyMetadataCapacity, requiredDirtyEntries))) {
        g_pendingFullRedraw = true;
        g_damageChangedSinceLastPublish = true;
        return;
    }
    auto* surfaceInfos = g_surfaceMetadataStorage.ownerAddress
        ? reinterpret_cast<MicaCompositorSurfaceInfo*>(
            g_surfaceMetadataStorage.ownerAddress) : nullptr;
    auto* dirtyInfos = g_dirtyMetadataStorage.ownerAddress
        ? reinterpret_cast<MicaCompositorDirtyRect*>(
            g_dirtyMetadataStorage.ownerAddress) : nullptr;
    __atomic_add_fetch(&store->frameSequence, 1, __ATOMIC_ACQ_REL);
    store->surfaces = g_surfaceMetadataStorage.compositorAddress
        ? reinterpret_cast<MicaCompositorSurfaceInfo*>(
            g_surfaceMetadataStorage.compositorAddress) : nullptr;
    store->surfaceArraySizeBytes = g_surfaceMetadataStorage.sizeBytes;
    store->dirtyRegions = g_dirtyMetadataStorage.compositorAddress
        ? reinterpret_cast<MicaCompositorDirtyRect*>(
            g_dirtyMetadataStorage.compositorAddress) : nullptr;
    store->dirtyRegionArraySizeBytes = g_dirtyMetadataStorage.sizeBytes;
    if (store->themeGeneration != g_themeGeneration) {
        store->theme = g_themeSnapshot;
        store->captionImageSet = g_themeCaptionImageSet;
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
    cursor.pixelsAddress = g_activeCursorStorage.compositorAddress;
    cursor.pixelSizeBytes = g_activeCursorStorage.sizeBytes;
    cursor.flags = (g_activeCursorWidth && g_activeCursorHeight
        ? MICANT_COMPOSITOR_CURSOR_ACTIVE : 0u) |
        (g_cursorDisplayCount >= 0 && g_mousePositionValid
            ? MICANT_COMPOSITOR_CURSOR_VISIBLE : 0u);
    const size_t clearSurfaceCount = std::max(g_lastPublishedSurfaceCount,
        requiredSurfaceEntries);
    for (size_t index = 0; index < clearSurfaceCount; ++index) {
        auto& info = surfaceInfos[index];
        info = {};
        const Window* windowPointer = nullptr;
        for (const auto& candidate : g_windows)
            if (candidate.active && !(candidate.style & kWsChild) &&
                candidate.compositorSlot == index) {
                windowPointer = &candidate;
                break;
            }
        if (!windowPointer) continue;
        const auto& window = *windowPointer;
        if (!window.active) continue;
        int64_t screenX{}, screenY{};
        if (!windowScreenOrigin(window, screenX, screenY)) continue;
        info.window = window.handle;
        info.parentWindow = window.parent;
        info.ownerProcessId = window.ownerProcessId;
        info.x = static_cast<int32_t>(screenX);
        info.y = static_cast<int32_t>(screenY);
        info.width = static_cast<uint32_t>(window.width);
        info.height = static_cast<uint32_t>(window.height);
        info.pitch = window.surface.pitch;
        info.surfaceAddress = window.surface.compositorAddress;
        info.surfaceSizeBytes = window.surface.sizeBytes;
        info.zOrder = static_cast<uint32_t>(visibleOrder(window));
        const uint8_t hoveredCaptionButton = g_cursorHitWindow == window.handle
            ? captionButtonAt(window,
                g_mouseX - static_cast<int32_t>(screenX),
                g_mouseY - static_cast<int32_t>(screenY)) : 0;
        const bool visibleThroughParents = window.visible &&
            (!window.parent || hasVisibleAncestors(window));
        info.flags = MICANT_COMPOSITOR_SURFACE_ACTIVE |
            (visibleThroughParents ? MICANT_COMPOSITOR_SURFACE_VISIBLE : 0u) |
            (window.iconic ? MICANT_COMPOSITOR_SURFACE_ICONIC : 0u) |
            (window.handle == g_foregroundWindow
                ? MICANT_COMPOSITOR_SURFACE_FOREGROUND : 0u) |
            (window.maximized ? MICANT_COMPOSITOR_SURFACE_MAXIMIZED : 0u) |
            ((g_captionCapture == window.handle && g_pressedCaptionButton)
                ? MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_PRESSED |
                    (static_cast<uint32_t>(g_pressedCaptionButton) <<
                     MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_SHIFT)
                : 0u) |
            (hoveredCaptionButton
                ? MICANT_COMPOSITOR_SURFACE_CAPTION_BUTTON_HOVER |
                    (static_cast<uint32_t>(hoveredCaptionButton) <<
                     MICANT_COMPOSITOR_SURFACE_CAPTION_HOVER_SHIFT)
                : 0u);
        info.surfaceSourceX = 0;
        info.surfaceSourceY = 0;
        const WindowIconImage* icon = nullptr;
        uint32_t iconType = 0;
        for (; iconType < 3u; ++iconType) {
            const auto& candidate = window.iconImages[iconType];
            if (candidate.width && candidate.height) {
                icon = &candidate;
                break;
            }
        }
        if (icon && icon->storage.compositorAddress) {
            info.iconWidth = icon->width;
            info.iconHeight = icon->height;
            info.iconStride = icon->stride;
            info.iconGeneration = icon->generation;
            info.iconAddress = icon->storage.compositorAddress;
            info.iconSizeBytes = icon->storage.sizeBytes;
        }
        const auto& title = window.titleSurface;
        if (title.width && title.height && title.storage.compositorAddress) {
            info.titleWidth = title.width;
            info.titleHeight = title.height;
            info.titleStride = title.stride;
            info.titleGeneration = title.generation;
            info.titleAddress = title.storage.compositorAddress;
            info.titleSizeBytes = title.storage.sizeBytes;
        }
        info.dirtyLeft = window.dirtyLeft;
        info.dirtyTop = window.dirtyTop;
        info.dirtyRight = window.dirtyRight;
        info.dirtyBottom = window.dirtyBottom;
        info.contentGeneration = window.contentGeneration;
        for (const auto& candidate : g_windows)
            if (candidate.active && candidate.compositorSlot == index)
                info.activeWriters += candidate.activeWriters;
        info.style = window.style;
        info.liquidGlass = window.liquidGlass;
        info.animationFlags = window.animationFlags;
        info.animationTarget = {
            window.animationTarget.left, window.animationTarget.top,
            window.animationTarget.right, window.animationTarget.bottom};
        info.animationGeneration = window.animationGeneration;
    }
    // Child HWNDs share their root window's pixel block. Publish their client
    // rectangles as metadata layers so the compositor's existing child overlay
    // pass can restore their pixels after the root client is drawn.
    for (const auto& window : g_windows) {
        if (!window.active || !window.parent || !(window.style & kWsChild) ||
            !window.visible ||
            !hasVisibleAncestors(window)) continue;
        auto* root = rootWindow(const_cast<Window*>(&window));
        if (!root || root->compositorSlot >= rootEntryCount ||
            !root->surface.compositorAddress || !root->surface.pitch ||
            !root->surface.sizeBytes)
            continue;
        size_t metadataIndex = 0;
        while (metadataIndex < requiredSurfaceEntries &&
               surfaceInfos[metadataIndex].window) ++metadataIndex;
        if (metadataIndex == requiredSurfaceEntries) break;

        int64_t windowX{}, windowY{}, rootX{}, rootY{};
        if (!windowScreenOrigin(window, windowX, windowY) ||
            !windowScreenOrigin(*root, rootX, rootY)) continue;
        const int32_t frame = frameInset(window);
        const int32_t caption = captionHeight(window);
        const int32_t clientWidth = std::max(1, window.width - frame * 2);
        const int32_t clientHeight = std::max(1,
            window.height - frame * 2 - caption);
        const int32_t rootFrame = frameInset(*root);
        const int32_t rootCaption = captionHeight(*root);
        const int64_t clientX = windowX - rootX - rootFrame + frame;
        const int64_t clientY = windowY - rootY - rootFrame - rootCaption + frame + caption;
        const int32_t rootClientWidth = std::max(1, root->width - rootFrame * 2);
        const int32_t rootClientHeight = std::max(1,
            root->height - rootFrame * 2 - rootCaption);
        if (clientX < 0 || clientY < 0 ||
            clientX + clientWidth > rootClientWidth ||
            clientY + clientHeight > rootClientHeight) continue;

        auto& info = surfaceInfos[metadataIndex];
        info.window = window.handle;
        info.parentWindow = window.parent;
        info.ownerProcessId = window.ownerProcessId;
        info.x = static_cast<int32_t>(windowX + frame);
        info.y = static_cast<int32_t>(windowY + frame + caption);
        info.width = static_cast<uint32_t>(clientWidth);
        info.height = static_cast<uint32_t>(clientHeight);
        info.pitch = root->surface.pitch;
        info.surfaceAddress = root->surface.compositorAddress;
        info.surfaceSizeBytes = root->surface.sizeBytes;
        info.zOrder = static_cast<uint32_t>(visibleOrder(window));
        info.flags = MICANT_COMPOSITOR_SURFACE_ACTIVE |
            MICANT_COMPOSITOR_SURFACE_VISIBLE;
        if (clientX > UINT32_MAX || clientY > UINT32_MAX) continue;
        info.surfaceSourceX = static_cast<uint32_t>(clientX);
        info.surfaceSourceY = static_cast<uint32_t>(clientY);
        info.activeWriters = window.activeWriters;
        info.style = window.style;
        info.contentGeneration = window.contentGeneration;
    }
    size_t publishedSurfaceCount = 0;
    for (size_t index = 0; index < clearSurfaceCount; ++index)
        if (surfaceInfos[index].window) publishedSurfaceCount = index + 1;
    store->surfaceEntryCount = static_cast<uint32_t>(publishedSurfaceCount);
    g_lastPublishedSurfaceCount = publishedSurfaceCount;
    store->dirtyRegionCount = g_pendingFullRedraw
        ? 0u : g_pendingDirtyRegionCount;
    store->dirtyFlags = g_pendingFullRedraw
        ? MICANT_COMPOSITOR_DIRTY_FULL : 0u;
    for (size_t index = 0; index < store->dirtyRegionCount; ++index)
        dirtyInfos[index] = g_pendingDirtyRegions[index];
    __atomic_add_fetch(&store->frameSequence, 1, __ATOMIC_RELEASE);
    g_damageChangedSinceLastPublish = false;
    }
    (void)g_compositorWakeEvent.set();
}

micant::NtStatus waitForCompositorUpdate(uint64_t observedSequence,
                                         uint64_t timeout100ns) noexcept {
    constexpr uint64_t maximumTimeout100ns = 2'500'000; // The compositor's 250 ms HUD cadence.
    auto* current = ke::KeGetCurrentThread();
    auto* store = g_compositorStore;
    if (!current || !store || current->eventWaitInProgress ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL ||
        timeout100ns > maximumTimeout100ns)
        return micant::NtStatus::InvalidDeviceState;

    // Reset before checking the generation. A concurrent publication either
    // changes the observed generation or leaves the event set for this wait.
    (void)g_compositorWakeEvent.reset();
    const uint64_t sequence = __atomic_load_n(&store->frameSequence, __ATOMIC_ACQUIRE);
    if (!(sequence & 1u) && sequence != observedSequence) {
#if defined(MICANT_KERNEL_PERF_DIAGNOSTICS)
        ke::KeRecordCompositorWaitResult(false);
#endif
        return micant::NtStatus::Success;
    }

    current->eventWaitInProgress = true;
    current->eventWaitUserMode = current->previousUserMode;
    current->eventWaitCanTerminate = current->eventWaitUserMode &&
        ke::KeGetCurrentIrql() == ke::PASSIVE_LEVEL;
    const auto result = g_compositorWakeEvent.wait(false, false, timeout100ns);
#if defined(MICANT_KERNEL_PERF_DIAGNOSTICS)
    ke::KeRecordCompositorWaitResult(result == micant::NtStatus::Timeout);
#endif
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
                                const uint32_t* pixels,
                                uint64_t ownerProcessId) noexcept {
    if (!ownerProcessId || image.size != sizeof(image) ||
        image.version != MICANT_WIN32K_CURSOR_IMAGE_VERSION) return NtStatus::InvalidParameter;
    if (!image.cursorHandle) {
        if (image.width || image.height || image.pitch || image.hotX || image.hotY ||
            image.pixelsAddress || image.pixelSizeBytes || pixels)
            return NtStatus::InvalidParameter;
    } else if (!image.width || !image.height ||
        !pixels || image.pitch < image.width ||
        image.height > SIZE_MAX / (static_cast<size_t>(image.pitch) * sizeof(uint32_t)) ||
        image.hotX >= image.width || image.hotY >= image.height) {
        return NtStatus::InvalidParameter;
    }
    if (!image.cursorHandle) {
        StateLockGuard lock;
        if (g_mousePositionValid) markCursorChanged();
        if (g_activeCursorStorage.sectionReference) {
            const auto releaseStatus = g_releaseCompositorSurface
                ? g_releaseCompositorSurface(g_activeCursorStorage,
                    g_activeCursorOwner == ownerProcessId)
                : NtStatus::DeviceNotReady;
            if (releaseStatus != NtStatus::Success) return releaseStatus;
        }
        g_activeCursorStorage = {};
        g_activeCursorHandle = 0;
        g_activeCursorOwner = 0;
        resetCursorImage(false);
        if (g_mousePositionValid) markCursorChanged();
        return g_activeCursorStorage.sectionReference
            ? NtStatus::Success : NtStatus::InsufficientResources;
    }
    const size_t requiredBytes = static_cast<size_t>(image.pitch) * image.height *
        sizeof(uint32_t);
    if (image.pixelSizeBytes < requiredBytes ||
        requiredBytes > static_cast<size_t>(INT64_MAX))
        return NtStatus::InvalidParameter;
    CompositorSurfaceStorage replacement{};
    if (!g_createCompositorSurface) return NtStatus::DeviceNotReady;
    const auto allocationStatus = g_createCompositorSurface(
        image.width, image.height, replacement);
    if (allocationStatus != NtStatus::Success) return allocationStatus;
    auto* destination = reinterpret_cast<uint32_t*>(replacement.ownerAddress);
    for (uint32_t y = 0; y < image.height; ++y) {
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint32_t pixel = pixels[static_cast<size_t>(y) * image.pitch + x];
            const uint32_t alpha = pixel >> 24;
            if (((pixel >> 16) & 0xFFu) > alpha || ((pixel >> 8) & 0xFFu) > alpha ||
                (pixel & 0xFFu) > alpha) {
                if (g_releaseCompositorSurface)
                    (void)g_releaseCompositorSurface(replacement, true);
                return NtStatus::InvalidParameter;
            }
            destination[static_cast<size_t>(y) * image.width + x] = pixel;
        }
    }
    StateLockGuard lock;
    if (g_activeCursorStorage.sectionReference) {
        const auto releaseStatus = g_releaseCompositorSurface
            ? g_releaseCompositorSurface(g_activeCursorStorage,
                g_activeCursorOwner == ownerProcessId)
            : NtStatus::DeviceNotReady;
        if (releaseStatus != NtStatus::Success) {
            if (g_releaseCompositorSurface)
                (void)g_releaseCompositorSurface(replacement, true);
            return releaseStatus;
        }
    }
    if (g_mousePositionValid) markCursorChanged();
    g_activeCursorHandle = image.cursorHandle;
    g_activeCursorOwner = image.cursorHandle ? ownerProcessId : 0;
    if (image.cursorHandle) {
        g_activeCursorWidth = image.width;
        g_activeCursorHeight = image.height;
        g_activeCursorPitch = image.width;
        g_activeCursorHotX = image.hotX;
        g_activeCursorHotY = image.hotY;
        g_activeCursorStorage = replacement;
    }
    if (g_mousePositionValid) markCursorChanged();
    return NtStatus::Success;
}

micant::NtStatus setThemeSnapshot(const MicaThemeSnapshot& snapshot,
    const MicaThemeCaptionImageSet* captionImages) noexcept {
    if (!MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot)))
        return NtStatus::InvalidParameter;
    if (captionImages && !MicaThemeCaptionImageSetIsValid(
            captionImages, sizeof(*captionImages))) return NtStatus::InvalidParameter;
    StateLockGuard lock;
    constexpr uint32_t kWmThemeChanged = 0x031A;
    size_t activeWindows = 0;
    size_t freeMessageSlots = 0;
    for (const auto& window : g_windows) activeWindows += window.active ? 1u : 0u;
    for (const auto& message : g_messages) freeMessageSlots += message.active ? 0u : 1u;
    if (freeMessageSlots < activeWindows) return NtStatus::InsufficientResources;

    markDamageFull();
    if (++g_themeGeneration == 0) ++g_themeGeneration;
    g_themeSnapshot = snapshot;
    g_themeCaptionImageSet = captionImages ? *captionImages : MicaThemeCaptionImageSet{};
    g_themeCaptionImageSet.generation = g_themeGeneration;
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
