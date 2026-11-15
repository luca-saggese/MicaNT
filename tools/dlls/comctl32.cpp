/**
 * @file comctl32.cpp
 * @brief Freestanding common controls for the native Ring 3 system image.
 */

#include <atomic>
#include <cstdint>
#include <limits>

#include "micant/shared/win32k_client.h"

namespace {

using WindowProc = intptr_t (*)(void*, uint32_t, uintptr_t, intptr_t);

struct Rect { int32_t left, top, right, bottom; };
struct PaintStruct {
    void* hdc;
    int32_t erase;
    Rect paint;
    int32_t restore, incUpdate;
    uint8_t reserved[32];
};
struct InitCommonControlsExArgs { uint32_t size, classes; };
struct ProgressRange { int32_t minimum, maximum; };

constexpr uint32_t kIccProgress = 0x00000020u;
constexpr uint32_t kWmCreate = 0x0001u;
constexpr uint32_t kWmDestroy = 0x0002u;
constexpr uint32_t kWmNCCreate = 0x0081u;
constexpr uint32_t kWmPaint = 0x000Fu;
constexpr uint32_t kWmThemeChanged = 0x031Au;
constexpr uint32_t kPbmSetRange = 0x0401u;
constexpr uint32_t kPbmSetPos = 0x0402u;
constexpr uint32_t kPbmDeltaPos = 0x0403u;
constexpr uint32_t kPbmSetStep = 0x0404u;
constexpr uint32_t kPbmStepIt = 0x0405u;
constexpr uint32_t kPbmSetRange32 = 0x0406u;
constexpr uint32_t kPbmGetRange = 0x0407u;
constexpr uint32_t kPbmGetPos = 0x0408u;
constexpr uint32_t kPbmSetBarColor = 0x0409u;
constexpr uint32_t kPbmSetBkColor = 0x2001u;
constexpr uint32_t kGwlExStyle = -20;
constexpr uint32_t kProgressBarPart = 1;
constexpr uint32_t kProgressFillState = 7;
constexpr uint32_t kDefaultBackColor = 0x00F0F0F0u;
constexpr uint32_t kDefaultBarColor = 0x00D77800u;
constexpr size_t kProgressCapacity = 64;

struct ProgressState {
    void* hwnd{};
    int32_t minimum{};
    int32_t maximum{100};
    int32_t position{};
    int32_t step{10};
    uint32_t barColor{};
    uint32_t backgroundColor{};
    bool hasBarColor{};
    bool hasBackgroundColor{};
};

std::atomic_flag g_stateLock = ATOMIC_FLAG_INIT;
ProgressState g_progress[kProgressCapacity]{};
std::atomic<uint32_t> g_registrationState{0};

struct StateGuard {
    StateGuard() noexcept {
        while (g_stateLock.test_and_set(std::memory_order_acquire)) { }
    }
    ~StateGuard() { g_stateLock.clear(std::memory_order_release); }
};

ProgressState* findProgress(void* hwnd) noexcept {
    for (auto& state : g_progress) if (state.hwnd == hwnd) return &state;
    return nullptr;
}

ProgressState* acquireProgress(void* hwnd) noexcept {
    for (auto& state : g_progress) {
        if (state.hwnd == hwnd) return &state;
        if (!state.hwnd) {
            state = {};
            state.hwnd = hwnd;
            state.maximum = 100;
            state.step = 10;
            return &state;
        }
    }
    return nullptr;
}

int32_t clampPosition(const ProgressState& state, int64_t position) noexcept {
    if (position < state.minimum) return state.minimum;
    if (position > state.maximum) return state.maximum;
    return static_cast<int32_t>(position);
}

extern "C" {
__declspec(dllimport) uint16_t RegisterClassExW(const MicaWin32kWindowClassEx*);
__declspec(dllimport) void* BeginPaint(void*, PaintStruct*);
__declspec(dllimport) int32_t EndPaint(void*, const PaintStruct*);
__declspec(dllimport) int32_t GetClientRect(void*, Rect*);
__declspec(dllimport) int32_t InvalidateRect(void*, const Rect*, int32_t);
__declspec(dllimport) void* CreateSolidBrush(uint32_t);
__declspec(dllimport) int32_t FillRect(void*, const Rect*, void*);
__declspec(dllimport) int32_t DeleteObject(void*);
__declspec(dllimport) void* OpenThemeData(void*, const wchar_t*);
__declspec(dllimport) int32_t CloseThemeData(void*);
__declspec(dllimport) int32_t DrawThemeBackground(void*, void*, int32_t, int32_t,
                                                  const Rect*, const Rect*);
}

bool fillWithColor(void* hdc, const Rect& rect, uint32_t colorRef) noexcept {
    void* brush = CreateSolidBrush(colorRef);
    if (!brush) return false;
    const bool filled = FillRect(hdc, &rect, brush) != 0;
    const bool released = DeleteObject(brush) != 0;
    return filled && released;
}

intptr_t ProgressBarWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                            intptr_t lParam) noexcept {
    if (!hwnd) return 0;
    if (message == kWmNCCreate) return 1;
    if (message == kWmCreate) {
        StateGuard guard;
        return acquireProgress(hwnd) ? 0 : -1;
    }
    if (message == kWmDestroy) {
        StateGuard guard;
        if (auto* state = findProgress(hwnd)) *state = {};
        return 0;
    }

    ProgressState snapshot{};
    {
        StateGuard guard;
        auto* state = findProgress(hwnd);
        if (!state && message != kWmDestroy) state = acquireProgress(hwnd);
        if (!state) return 0;
        switch (message) {
            case kPbmSetRange: {
                const int32_t oldMin = state->minimum;
                const int32_t minimum = static_cast<int16_t>(static_cast<uintptr_t>(lParam));
                const int32_t maximum = static_cast<int16_t>(static_cast<uintptr_t>(lParam) >> 16);
                if (maximum < minimum) return oldMin;
                state->minimum = minimum;
                state->maximum = maximum;
                state->position = clampPosition(*state, state->position);
                snapshot = *state;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return oldMin;
            }
            case kPbmSetRange32: {
                const int32_t oldMin = state->minimum;
                const int32_t minimum = static_cast<int32_t>(wParam);
                const int32_t maximum = static_cast<int32_t>(lParam);
                if (maximum < minimum) return oldMin;
                state->minimum = minimum;
                state->maximum = maximum;
                state->position = clampPosition(*state, state->position);
                (void)InvalidateRect(hwnd, nullptr, 1);
                return oldMin;
            }
            case kPbmSetPos: {
                const int32_t old = state->position;
                state->position = clampPosition(*state, static_cast<int32_t>(wParam));
                (void)InvalidateRect(hwnd, nullptr, 1);
                return old;
            }
            case kPbmGetPos: return state->position;
            case kPbmDeltaPos: {
                const int32_t old = state->position;
                state->position = clampPosition(*state,
                    static_cast<int64_t>(state->position) + static_cast<int32_t>(wParam));
                (void)InvalidateRect(hwnd, nullptr, 1);
                return old;
            }
            case kPbmSetStep: {
                const int32_t old = state->step;
                state->step = static_cast<int32_t>(wParam);
                return old;
            }
            case kPbmStepIt: {
                const int32_t old = state->position;
                state->position = clampPosition(*state,
                    static_cast<int64_t>(state->position) + state->step);
                (void)InvalidateRect(hwnd, nullptr, 1);
                return old;
            }
            case kPbmSetBarColor: {
                const uint32_t old = state->hasBarColor ? state->barColor : 0xFFFFFFFFu;
                state->barColor = static_cast<uint32_t>(lParam);
                state->hasBarColor = true;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return static_cast<intptr_t>(old);
            }
            case kPbmSetBkColor: {
                const uint32_t old = state->hasBackgroundColor
                    ? state->backgroundColor : 0xFFFFFFFFu;
                state->backgroundColor = static_cast<uint32_t>(lParam);
                state->hasBackgroundColor = true;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return static_cast<intptr_t>(old);
            }
            case kPbmGetRange: {
                if (lParam) {
                    auto* range = reinterpret_cast<ProgressRange*>(lParam);
                    range->minimum = state->minimum;
                    range->maximum = state->maximum;
                }
                return wParam ? state->maximum : state->minimum;
            }
            case kWmPaint: snapshot = *state; break;
            case kWmThemeChanged:
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 0;
            default: return 0;
        }
    }

    if (message != kWmPaint) return 0;
    PaintStruct paint{};
    void* hdc = BeginPaint(hwnd, &paint);
    if (!hdc) return 0;
    Rect bounds{};
    if (!GetClientRect(hwnd, &bounds) || bounds.right < bounds.left ||
        bounds.bottom < bounds.top) {
        (void)EndPaint(hwnd, &paint);
        return 0;
    }

    void* theme = OpenThemeData(hwnd, L"PROGRESS");
    bool backgroundDrawn = theme &&
        DrawThemeBackground(theme, hdc, kProgressBarPart, 1, &bounds, nullptr) == 0;
    if (!backgroundDrawn) {
        const uint32_t back = snapshot.hasBackgroundColor
            ? snapshot.backgroundColor : kDefaultBackColor;
        (void)fillWithColor(hdc, bounds, back);
    }

    const int64_t range = static_cast<int64_t>(snapshot.maximum) - snapshot.minimum;
    const int64_t extent = static_cast<int64_t>(bounds.right) - bounds.left;
    if (range > 0 && extent > 0 && snapshot.position > snapshot.minimum) {
        const int64_t completed = static_cast<int64_t>(snapshot.position) - snapshot.minimum;
        const int64_t fillExtent = extent * completed / range;
        if (fillExtent > 0 && fillExtent <= std::numeric_limits<int32_t>::max()) {
            Rect fill{bounds.left, bounds.top,
                      static_cast<int32_t>(int64_t(bounds.left) + fillExtent), bounds.bottom};
            bool fillDrawn = false;
            if (!snapshot.hasBarColor && theme)
                fillDrawn = DrawThemeBackground(theme, hdc, kProgressBarPart,
                    kProgressFillState, &fill, &bounds) == 0;
            if (!fillDrawn) {
                const uint32_t bar = snapshot.hasBarColor
                    ? snapshot.barColor : kDefaultBarColor;
                (void)fillWithColor(hdc, fill, bar);
            }
        }
    }
    if (theme) (void)CloseThemeData(theme);
    (void)EndPaint(hwnd, &paint);
    return 0;
}

bool registerProgressClass() noexcept {
    uint32_t state = g_registrationState.load(std::memory_order_acquire);
    for (;;) {
        if (state == 2) return true;
        if (state == 3) return false;
        if (state == 0 && g_registrationState.compare_exchange_weak(
                state, 1, std::memory_order_acq_rel, std::memory_order_acquire)) break;
        state = g_registrationState.load(std::memory_order_acquire);
    }

    MicaWin32kWindowClassEx wc{};
    wc.cbSize = sizeof(wc);
    wc.wndProc = reinterpret_cast<void*>(ProgressBarWndProc);
    wc.className = L"msctls_progress32";
    const bool registered = RegisterClassExW(&wc) != 0;
    g_registrationState.store(registered ? 2u : 3u, std::memory_order_release);
    return registered;
}

} // namespace

extern "C" {

bool DllMain(void*, unsigned long, void*) noexcept { return true; }

__declspec(dllexport) int32_t InitCommonControlsEx(
    const InitCommonControlsExArgs* args) noexcept {
    if (!args || args->size != sizeof(InitCommonControlsExArgs) ||
        (args->classes & ~kIccProgress) != 0) return 0;
    if ((args->classes & kIccProgress) == 0) return 1;
    return registerProgressClass() ? 1 : 0;
}

__declspec(dllexport) void* CreatePropertySheetPageW(const void*) noexcept { return nullptr; }
__declspec(dllexport) int64_t PropertySheetW(const void*) noexcept { return -1; }

} // extern "C"
