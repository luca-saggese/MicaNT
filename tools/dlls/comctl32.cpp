/**
 * @file comctl32.cpp
 * @brief Freestanding common controls for the native Ring 3 system image.
 */

#include <algorithm>
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
struct TabItemW {
    uint32_t mask, state, stateMask;
    wchar_t* text;
    int32_t textCapacity, image;
    intptr_t parameter;
};
struct TabPoint { int32_t x, y; };
struct TabHitTestInfo { TabPoint point; uint32_t flags; };
struct NotifyHeader { void* hwndFrom; uintptr_t idFrom; uint32_t code; };

constexpr uint32_t kIccProgress = 0x00000020u;
constexpr uint32_t kIccTab = 0x00000008u;
constexpr uint32_t kIccTreeView = 0x00000002u;
constexpr uint32_t kWmCreate = 0x0001u;
constexpr uint32_t kWmDestroy = 0x0002u;
constexpr uint32_t kWmNCCreate = 0x0081u;
constexpr uint32_t kWmPaint = 0x000Fu;
constexpr uint32_t kWmThemeChanged = 0x031Au;
constexpr uint32_t kWmMouseMove = 0x0200u;
constexpr uint32_t kWmLButtonUp = 0x0202u;
constexpr uint32_t kWmSetFocus = 0x0007u;
constexpr uint32_t kWmKillFocus = 0x0008u;
constexpr uint32_t kWmKeyDown = 0x0100u;
constexpr uint32_t kWmGetDlgCode = 0x0087u;
constexpr uint32_t kWmNotify = 0x004Eu;
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
constexpr size_t kTabCapacity = 32;
constexpr size_t kTabItemCapacity = 32;
constexpr size_t kTabTextCapacity = 128;
constexpr size_t kTreeCapacity = 4;
constexpr size_t kTreeNodeCapacity = 32;
constexpr size_t kTreeTextCapacity = 96;
constexpr uint32_t kTcmFirst = 0x1300u;
constexpr uint32_t kTcmGetItemCount = kTcmFirst + 4u;
constexpr uint32_t kTcmDeleteItem = kTcmFirst + 8u;
constexpr uint32_t kTcmGetCurSel = kTcmFirst + 11u;
constexpr uint32_t kTcmSetCurSel = kTcmFirst + 12u;
constexpr uint32_t kTcmHitTest = kTcmFirst + 13u;
constexpr uint32_t kTcmInsertItemW = kTcmFirst + 62u;
constexpr uint32_t kTcmGetItemW = kTcmFirst + 60u;
constexpr uint32_t kTcmSetItemW = kTcmFirst + 61u;
constexpr uint32_t kTcnSelChange = static_cast<uint32_t>(-551);
constexpr uint32_t kTcifText = 0x0001u;
constexpr uint32_t kTcifImage = 0x0002u;
constexpr uint32_t kTcifParam = 0x0008u;
constexpr uint32_t kTcsStyleMask = 0x0000FFFEu;
constexpr uint32_t kTvmFirst = 0x1100u;
constexpr uint32_t kTvmInsertItemW = kTvmFirst + 50u;
constexpr uint32_t kTvmDeleteItem = kTvmFirst + 1u;
constexpr uint32_t kTvmExpand = kTvmFirst + 2u;
constexpr uint32_t kTvmGetCount = kTvmFirst + 5u;
constexpr uint32_t kTvmGetNextItem = kTvmFirst + 10u;
constexpr uint32_t kTvmSelectItem = kTvmFirst + 11u;
constexpr intptr_t kTvRoot = -0x10000;
constexpr intptr_t kTvFirst = -0x0FFFF;
constexpr intptr_t kTvLast = -0x0FFFE;
constexpr uint32_t kTvExpanded = 0x0020u;
constexpr uint32_t kTvExpand = 0x0002u;
constexpr uint32_t kTvCollapse = 0x0001u;
constexpr uint32_t kTvSelect = 0x0009u;
constexpr uint32_t kTvNext = 0x0001u;
constexpr uint32_t kTvPrevious = 0x0002u;
constexpr uint32_t kTvFirstVisible = 0x0005u;
constexpr uint32_t kTvNextVisible = 0x0006u;
constexpr uint32_t kTvLastVisible = 0x000Au;
constexpr uint32_t kTvGetSelection = 0x0009u;
constexpr uint32_t kTvifText = 0x0001u;
constexpr uint32_t kTvifParam = 0x0004u;
constexpr uint32_t kTvsHasButtons = 0x0001u;
constexpr uint32_t kTvsHasLines = 0x0002u;
constexpr uint32_t kTvsLinesAtRoot = 0x0004u;
constexpr int32_t kTreeIndent = 18;
constexpr int32_t kTreeRowHeight = 22;

struct TreeItemW {
    uint32_t mask;
    void* item;
    uint32_t state, stateMask;
    wchar_t* text;
    int32_t textCapacity, image, selectedImage, children;
    intptr_t parameter;
};
struct TreeInsertW { void* parent; void* insertAfter; TreeItemW item; };
struct TreeNode {
    bool used{}, expanded{};
    int16_t parent{-1};
    wchar_t text[kTreeTextCapacity]{};
    intptr_t parameter{};
};
struct TreeState {
    void* hwnd{};
    uint32_t style{};
    int16_t selected{-1}, hot{-1};
    bool enabled{true}, focused{};
    TreeNode nodes[kTreeNodeCapacity]{};
};

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
struct TabItem { wchar_t text[kTabTextCapacity]{}; intptr_t parameter{}; };
struct TabState {
    void* hwnd{};
    void* parent{};
    uintptr_t id{};
    uint32_t style{};
    int32_t count{};
    int32_t selected{-1};
    int32_t hot{-1};
    bool focused{};
    bool enabled{true};
    TabItem items[kTabItemCapacity]{};
};
TabState g_tabs[kTabCapacity]{};
std::atomic<uint32_t> g_tabRegistrationState{0};
TreeState g_trees[kTreeCapacity]{};
std::atomic<uint32_t> g_treeRegistrationState{0};

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
__declspec(dllimport) int32_t GetThemeColor(void*, int32_t, int32_t, int32_t,
                                             uint32_t*);
__declspec(dllimport) int32_t DrawThemeText(void*, void*, int32_t, int32_t,
        const wchar_t*, int32_t, uint32_t, uint32_t, const Rect*);
__declspec(dllimport) intptr_t SendMessageW(void*, uint32_t, uintptr_t, intptr_t);
__declspec(dllimport) void* SetFocus(void*);
__declspec(dllimport) uint32_t SetTextColor(void*, uint32_t);
__declspec(dllimport) int32_t SetBkMode(void*, int32_t);
__declspec(dllimport) int32_t TextOutW(void*, int32_t, int32_t,
                                        const wchar_t*, int32_t);
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

    // Progress trough colors can carry alpha in the theme snapshot. Seed the
    // control with the themed window surface before UxTheme blends that color.
    uint32_t baseSurface = kDefaultBackColor;
    void* windowTheme = OpenThemeData(hwnd, L"WINDOW");
    uint32_t themedSurface{};
    if (windowTheme && GetThemeColor(windowTheme, 1, 1, 3802,
                                     &themedSurface) == 0)
        baseSurface = themedSurface;
    (void)fillWithColor(hdc, bounds, baseSurface);

    void* theme = OpenThemeData(hwnd, L"PROGRESS");
    bool backgroundDrawn = theme &&
        DrawThemeBackground(theme, hdc, kProgressBarPart, 1, &bounds, nullptr) == 0;
    if (!backgroundDrawn) {
        uint32_t back = snapshot.hasBackgroundColor
            ? snapshot.backgroundColor : baseSurface;
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
                uint32_t bar = snapshot.hasBarColor
                    ? snapshot.barColor : kDefaultBarColor;
                uint32_t themedBar{};
                if (!snapshot.hasBarColor && theme &&
                    (GetThemeColor(theme, kProgressBarPart, kProgressFillState,
                                   3802, &themedBar) == 0 ||
                     GetThemeColor(theme, kProgressBarPart, kProgressFillState,
                                   3803, &themedBar) == 0))
                    bar = themedBar;
                (void)fillWithColor(hdc, fill, bar);
            }
        }
    }
    if (theme) (void)CloseThemeData(theme);
    if (windowTheme) (void)CloseThemeData(windowTheme);
    (void)EndPaint(hwnd, &paint);
    return 0;
}

TabState* findTab(void* hwnd) noexcept {
    for (auto& state : g_tabs) if (state.hwnd == hwnd) return &state;
    return nullptr;
}

TreeState* findTree(void* hwnd) noexcept {
    for (auto& state : g_trees) if (state.hwnd == hwnd) return &state;
    return nullptr;
}

int16_t visibleTreeItem(const TreeState& state, int32_t visibleIndex) noexcept {
    if (visibleIndex < 0) return -1;
    int32_t current = 0;
    for (int16_t i = 0; i < static_cast<int16_t>(kTreeNodeCapacity); ++i) {
        if (!state.nodes[i].used) continue;
        int16_t parent = state.nodes[i].parent;
        bool visible = true;
        while (parent >= 0) {
            if (!state.nodes[parent].expanded) { visible = false; break; }
            parent = state.nodes[parent].parent;
        }
        if (!visible) continue;
        if (current++ == visibleIndex) return i;
    }
    return -1;
}

int32_t treeVisibleCount(const TreeState& state) noexcept {
    int32_t count = 0;
    for (int16_t i = 0; i < static_cast<int16_t>(kTreeNodeCapacity); ++i) {
        if (!state.nodes[i].used) continue;
        int16_t parent = state.nodes[i].parent;
        bool visible = true;
        while (parent >= 0) {
            if (!state.nodes[parent].expanded) { visible = false; break; }
            parent = state.nodes[parent].parent;
        }
        if (visible) ++count;
    }
    return count;
}

TabState* acquireTab(void* hwnd) noexcept {
    for (auto& state : g_tabs) {
        if (state.hwnd == hwnd) return &state;
        if (!state.hwnd) {
            state = {};
            state.hwnd = hwnd;
            state.selected = -1;
            state.hot = -1;
            return &state;
        }
    }
    return nullptr;
}

bool copyTabText(wchar_t (&destination)[kTabTextCapacity], const wchar_t* source) noexcept {
    if (!source) { destination[0] = L'\0'; return true; }
    if (reinterpret_cast<uintptr_t>(source) == UINTPTR_MAX) return false;
    size_t index = 0;
    for (; index + 1 < kTabTextCapacity && source[index]; ++index)
        destination[index] = source[index];
    if (source[index]) return false;
    destination[index] = L'\0';
    return true;
}

bool validTabIndex(const TabState& state, intptr_t index) noexcept {
    return index >= 0 && index < state.count;
}

int32_t tabAt(const TabState& state, const Rect& bounds, int32_t x,
              int32_t y) noexcept {
    if (x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom ||
        state.count <= 0) return -1;
    const int32_t width = bounds.right - bounds.left;
    const int32_t tabWidth = std::max<int32_t>(1, width / state.count);
    const int32_t index = std::min<int32_t>(state.count - 1,
        (x - bounds.left) / tabWidth);
    return index;
}

void notifyTabSelection(const TabState& state) noexcept {
    if (!state.parent) return;
    const NotifyHeader header{state.hwnd, state.id, kTcnSelChange};
    (void)SendMessageW(state.parent, kWmNotify, state.id,
                       reinterpret_cast<intptr_t>(&header));
}

intptr_t TabControlWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                           intptr_t lParam) noexcept {
    if (!hwnd) return 0;
    if (message == kWmGetDlgCode) return 0x0001u; // DLGC_WANTARROWS
    if (message == kWmNCCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create) return 0;
        const uint32_t styles = static_cast<uint32_t>(create->style) & kTcsStyleMask;
        // This bounded control implements the standard top, single-row tab
        // strip. Unsupported layout/style modes fail window creation.
        if (styles) return 0;
        TabState initial{};
        initial.hwnd = hwnd;
        initial.parent = create->parent;
        initial.id = reinterpret_cast<uintptr_t>(create->menu);
        initial.style = styles;
        initial.selected = -1;
        initial.hot = -1;
        initial.enabled = (static_cast<uint32_t>(create->style) & 0x08000000u) == 0;
        StateGuard guard;
        for (auto& entry : g_tabs) {
            if (entry.hwnd) continue;
            entry = initial;
            return 1;
        }
        return 0;
    }
    if (message == kWmCreate) {
        StateGuard guard;
        return findTab(hwnd) ? 0 : -1;
    }
    if (message == kWmDestroy) {
        StateGuard guard;
        if (auto* state = findTab(hwnd)) *state = {};
        return 0;
    }

    TabState snapshot{};
    bool selectionChanged = false;
    bool repaint = false;
    {
        StateGuard guard;
        auto* state = findTab(hwnd);
        if (!state) return 0;
        switch (message) {
            case kTcmGetItemCount: return state->count;
            case kTcmGetCurSel: return state->selected;
            case kTcmSetCurSel: {
                const int32_t previous = state->selected;
                const int32_t index = static_cast<int32_t>(wParam);
                if (index < -1 || index >= state->count) return -1;
                state->selected = index;
                repaint = previous != index;
                snapshot = *state;
                if (repaint) (void)InvalidateRect(hwnd, nullptr, 1);
                return previous;
            }
            case kTcmInsertItemW: {
                if (!lParam || state->count >= static_cast<int32_t>(kTabItemCapacity)) return -1;
                const auto* item = reinterpret_cast<const TabItemW*>(lParam);
                if ((item->mask & ~(kTcifText | kTcifParam)) ||
                    (item->mask & kTcifImage)) return -1;
                const intptr_t requested = static_cast<intptr_t>(wParam);
                const int32_t index = requested == -1 ? state->count
                    : static_cast<int32_t>(requested);
                if (index < 0 || index > state->count) return -1;
                TabItem inserted{};
                if ((item->mask & kTcifText) && !copyTabText(inserted.text, item->text))
                    return -1;
                if (item->mask & kTcifParam) inserted.parameter = item->parameter;
                for (int32_t current = state->count; current > index; --current)
                    state->items[current] = state->items[current - 1];
                state->items[index] = inserted;
                ++state->count;
                if (state->selected >= index) ++state->selected;
                if (state->selected < 0) state->selected = 0;
                snapshot = *state;
                repaint = true;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return index;
            }
            case kTcmDeleteItem: {
                if (!validTabIndex(*state, static_cast<intptr_t>(wParam))) return 0;
                const int32_t index = static_cast<int32_t>(wParam);
                for (int32_t current = index; current + 1 < state->count; ++current)
                    state->items[current] = state->items[current + 1];
                state->items[--state->count] = {};
                if (!state->count) state->selected = -1;
                else if (state->selected > index) --state->selected;
                else if (state->selected == index)
                    state->selected = std::min(index, state->count - 1);
                snapshot = *state;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 1;
            }
            case kTcmGetItemW: {
                if (!lParam || !validTabIndex(*state, static_cast<intptr_t>(wParam))) return 0;
                auto* item = reinterpret_cast<TabItemW*>(lParam);
                if (item->mask & ~(kTcifText | kTcifParam | kTcifImage)) return 0;
                if (item->mask & kTcifImage) { item->image = -1; }
                if (item->mask & kTcifParam)
                    item->parameter = state->items[wParam].parameter;
                if ((item->mask & kTcifText) && item->text && item->textCapacity > 0) {
                    int32_t index = 0;
                    for (; index + 1 < item->textCapacity &&
                         state->items[wParam].text[index]; ++index)
                        item->text[index] = state->items[wParam].text[index];
                    item->text[index] = L'\0';
                }
                return 1;
            }
            case kTcmSetItemW: {
                if (!lParam || !validTabIndex(*state, static_cast<intptr_t>(wParam))) return 0;
                const auto* item = reinterpret_cast<const TabItemW*>(lParam);
                if (item->mask & ~(kTcifText | kTcifParam) ||
                    (item->mask & kTcifImage)) return 0;
                auto& destination = state->items[wParam];
                if ((item->mask & kTcifText) && !copyTabText(destination.text, item->text))
                    return 0;
                if (item->mask & kTcifParam) destination.parameter = item->parameter;
                snapshot = *state;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 1;
            }
            case kTcmHitTest: {
                if (!lParam) return -1;
                auto* hit = reinterpret_cast<TabHitTestInfo*>(lParam);
                Rect bounds{};
                if (!GetClientRect(hwnd, &bounds)) return -1;
                const int32_t index = tabAt(*state, bounds, hit->point.x, hit->point.y);
                hit->flags = index < 0 ? 1u : 4u;
                return index;
            }
            case kWmMouseMove: {
                Rect bounds{};
                if (!GetClientRect(hwnd, &bounds)) return 0;
                const int32_t x = static_cast<int16_t>(
                    static_cast<uintptr_t>(lParam) & 0xFFFFu);
                const int32_t y = static_cast<int16_t>(
                    (static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
                const int32_t hot = state->enabled ? tabAt(*state, bounds, x, y) : -1;
                repaint = hot != state->hot;
                state->hot = hot;
                snapshot = *state;
                break;
            }
            case kWmLButtonUp: {
                Rect bounds{};
                if (!GetClientRect(hwnd, &bounds)) return 0;
                const int32_t x = static_cast<int16_t>(
                    static_cast<uintptr_t>(lParam) & 0xFFFFu);
                const int32_t y = static_cast<int16_t>(
                    (static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
                const int32_t index = tabAt(*state, bounds, x, y);
                if (index >= 0 && state->enabled) {
                    selectionChanged = state->selected != index;
                    state->selected = index;
                    state->focused = true;
                    snapshot = *state;
                    repaint = true;
                }
                break;
            }
            case kWmSetFocus: state->focused = true; repaint = true; snapshot = *state; break;
            case kWmKillFocus: state->focused = false; repaint = true; snapshot = *state; break;
            case 0x000Au: state->enabled = wParam != 0; repaint = true; snapshot = *state; break;
            case kWmKeyDown: {
                if (!state->focused || !state->enabled || state->count == 0 ||
                    (wParam != 0x25u && wParam != 0x27u)) return 0;
                const int32_t old = state->selected;
                if (wParam == 0x25u && state->selected > 0) --state->selected;
                if (wParam == 0x27u && state->selected + 1 < state->count)
                    ++state->selected;
                selectionChanged = old != state->selected;
                repaint = selectionChanged;
                snapshot = *state;
                break;
            }
            case kWmThemeChanged:
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 0;
            case kWmPaint:
                snapshot = *state;
                break;
            default:
                // Unsupported TCM_ messages fail rather than claiming success.
                if (message >= kTcmFirst && message < kTcmFirst + 128u) return -1;
                return 0;
        }
    }
    if (repaint) (void)InvalidateRect(hwnd, nullptr, 1);
    if (selectionChanged) notifyTabSelection(snapshot);
    if (message != kWmPaint) {
        if (message == kWmLButtonUp) (void)SetFocus(hwnd);
        return 0;
    }

    PaintStruct paint{};
    void* hdc = BeginPaint(hwnd, &paint);
    if (!hdc) return 0;
    Rect bounds{};
    if (!GetClientRect(hwnd, &bounds) || bounds.right <= bounds.left ||
        bounds.bottom <= bounds.top) {
        (void)EndPaint(hwnd, &paint);
        return 0;
    }
    void* theme = OpenThemeData(hwnd, L"TAB");
    uint32_t clientColor = 0x00F0F0F0u;
    if (theme) {
        uint32_t themedClientColor{};
        if (GetThemeColor(theme, 1, 1, 3802, &themedClientColor) == 0)
            clientColor = themedClientColor;
    }
    bool painted = fillWithColor(hdc, bounds, clientColor);
    const int32_t tabWidth = std::max<int32_t>(1,
        (bounds.right - bounds.left) / std::max<int32_t>(1, snapshot.count));
    const int32_t tabHeight = std::min<int32_t>(28, bounds.bottom - bounds.top);
    for (int32_t index = 0; index < snapshot.count; ++index) {
        Rect tab{bounds.left + index * tabWidth, bounds.top,
            index + 1 == snapshot.count ? bounds.right :
                std::min(bounds.right, bounds.left + (index + 1) * tabWidth),
            bounds.top + tabHeight};
        const int32_t state = !snapshot.enabled ? 4 :
            snapshot.selected == index ? 7 : snapshot.hot == index ? 2 : 1;
        const bool themed = theme &&
            DrawThemeBackground(theme, hdc, 1, state, &tab, nullptr) == 0;
        if (!themed) {
            uint32_t color = snapshot.selected == index ? 0x00FFFFFFu : clientColor;
            uint32_t themedColor{};
            if (theme && GetThemeColor(theme, 1, state, 3802, &themedColor) == 0)
                color = themedColor;
            painted &= fillWithColor(hdc, tab, color);
        }
        Rect text{tab.left + 6, tab.top + 2, tab.right - 6, tab.bottom - 2};
        const auto& label = snapshot.items[index].text;
        int32_t length = 0;
        while (length < static_cast<int32_t>(kTabTextCapacity) && label[length]) ++length;
        if (length && text.right > text.left && text.bottom > text.top) {
            const bool themedText = theme && DrawThemeText(theme, hdc, 1, state,
                label, length, 0x00000001u | 0x00000020u | 0x00000800u,
                0, &text) == 0;
            if (!themedText) {
                uint32_t textColor = snapshot.enabled ? 0x00000000u : 0x00808080u;
                uint32_t themedTextColor{};
                if (theme && GetThemeColor(theme, 1, state, 3803,
                                           &themedTextColor) == 0)
                    textColor = themedTextColor;
                const uint32_t oldColor = SetTextColor(hdc, textColor);
                const int32_t oldMode = SetBkMode(hdc, 1);
                const int32_t textX = text.left + std::max(0,
                    ((text.right - text.left) - length * 8) / 2);
                const int32_t textY = text.top + std::max(0,
                    ((text.bottom - text.top) - 16) / 2);
                const bool textOk = oldColor != 0xFFFFFFFFu && oldMode > 0 &&
                    TextOutW(hdc, textX, textY, label, length) != 0;
                if (oldMode > 0) (void)SetBkMode(hdc, oldMode);
                if (oldColor != 0xFFFFFFFFu) (void)SetTextColor(hdc, oldColor);
                painted &= textOk;
            }
        }
    }
    if (theme) (void)CloseThemeData(theme);
    const bool presented = EndPaint(hwnd, &paint) != 0;
    return painted && presented ? 1 : 0;
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

bool registerTabClass() noexcept {
    uint32_t state = g_tabRegistrationState.load(std::memory_order_acquire);
    for (;;) {
        if (state == 2) return true;
        if (state == 3) return false;
        if (state == 0 && g_tabRegistrationState.compare_exchange_weak(
                state, 1, std::memory_order_acq_rel, std::memory_order_acquire)) break;
        state = g_tabRegistrationState.load(std::memory_order_acquire);
    }
    MicaWin32kWindowClassEx wc{};
    wc.cbSize = sizeof(wc);
    wc.wndProc = reinterpret_cast<void*>(TabControlWndProc);
    wc.className = L"SysTabControl32";
    const bool registered = RegisterClassExW(&wc) != 0;
    g_tabRegistrationState.store(registered ? 2u : 3u, std::memory_order_release);
    return registered;
}

intptr_t TreeViewWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                         intptr_t lParam) noexcept {
    if (!hwnd) return 0;
    if (message == kWmNCCreate) return 1;
    if (message == kWmCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create) return -1;
        TreeState initial{};
        initial.hwnd = hwnd;
        initial.style = static_cast<uint32_t>(create->style);
        initial.enabled = (initial.style & 0x08000000u) == 0;
        StateGuard guard;
        for (auto& entry : g_trees) {
            if (entry.hwnd) continue;
            entry = initial;
            return 0;
        }
        return -1;
    }
    if (message == kWmDestroy) {
        StateGuard guard;
        if (auto* state = findTree(hwnd)) *state = {};
        return 0;
    }

    TreeState snapshot{};
    bool repaint = false;
    {
        StateGuard guard;
        auto* state = findTree(hwnd);
        if (!state) return 0;
        switch (message) {
        case kTvmGetCount: {
            int32_t count = 0;
            for (const auto& node : state->nodes) if (node.used) ++count;
            return count;
        }
        case kTvmInsertItemW: {
            if (!lParam) return 0;
            const auto* insert = reinterpret_cast<const TreeInsertW*>(lParam);
            const auto& item = insert->item;
            if ((item.mask & ~(kTvifText | kTvifParam)) != 0 ||
                (item.mask & kTvifText && !item.text)) return 0;
            int16_t parent = -1;
            if (insert->parent && reinterpret_cast<intptr_t>(insert->parent) != kTvRoot) {
                const uintptr_t handle = reinterpret_cast<uintptr_t>(insert->parent);
                if (!handle || handle > kTreeNodeCapacity || !state->nodes[handle - 1].used)
                    return 0;
                parent = static_cast<int16_t>(handle - 1);
            }
            int16_t slot = -1;
            for (int16_t i = 0; i < static_cast<int16_t>(kTreeNodeCapacity); ++i)
                if (!state->nodes[i].used) { slot = i; break; }
            if (slot < 0) return 0;
            TreeNode node{};
            node.used = true;
            node.parent = parent;
            node.expanded = (item.state & kTvExpanded) != 0;
            if (item.mask & kTvifParam) node.parameter = item.parameter;
            if (item.mask & kTvifText) {
                size_t length = 0;
                while (length + 1 < kTreeTextCapacity && item.text[length]) {
                    node.text[length] = item.text[length];
                    ++length;
                }
                node.text[length] = L'\0';
            }
            state->nodes[slot] = node;
            snapshot = *state;
            repaint = true;
            (void)InvalidateRect(hwnd, nullptr, 1);
            return static_cast<intptr_t>(slot + 1);
        }
        case kTvmExpand: {
            const uintptr_t handle = wParam;
            if (!handle || handle > kTreeNodeCapacity || !state->nodes[handle - 1].used)
                return 0;
            bool hasChildren = false;
            for (const auto& node : state->nodes)
                if (node.used && node.parent == static_cast<int16_t>(handle - 1))
                    hasChildren = true;
            const bool expanded = (static_cast<uint32_t>(lParam) & kTvExpand) != 0;
            state->nodes[handle - 1].expanded = hasChildren && expanded;
            snapshot = *state;
            repaint = true;
            break;
        }
        case kTvmSelectItem: {
            if (wParam != kTvSelect) return 0;
            const uintptr_t handle = static_cast<uintptr_t>(lParam);
            if (handle > kTreeNodeCapacity || (handle && !state->nodes[handle - 1].used))
                return 0;
            state->selected = handle ? static_cast<int16_t>(handle - 1) : -1;
            snapshot = *state;
            repaint = true;
            break;
        }
        case kTvmGetNextItem: {
            const uint32_t relation = static_cast<uint32_t>(wParam);
            if (relation == kTvGetSelection)
                return state->selected >= 0 ? state->selected + 1 : 0;
            const uintptr_t handle = static_cast<uintptr_t>(lParam);
            if (relation == kTvFirstVisible) {
                const int16_t first = visibleTreeItem(*state, 0);
                return first >= 0 ? first + 1 : 0;
            }
            if (!handle || handle > kTreeNodeCapacity || !state->nodes[handle - 1].used)
                return 0;
            const int32_t visibleCount = treeVisibleCount(*state);
            int32_t index = 0;
            for (; index < visibleCount; ++index)
                if (visibleTreeItem(*state, index) == static_cast<int16_t>(handle - 1)) break;
            int16_t result = -1;
            if (relation == kTvNextVisible) result = visibleTreeItem(*state, index + 1);
            else if (relation == kTvLastVisible)
                result = visibleTreeItem(*state, visibleCount - 1);
            else if (relation == kTvNext || relation == kTvPrevious) {
                const int16_t parent = state->nodes[handle - 1].parent;
                for (int16_t i = static_cast<int16_t>(handle - 1) +
                        (relation == kTvNext ? 1 : -1);
                     i >= 0 && i < static_cast<int16_t>(kTreeNodeCapacity);
                     i += relation == kTvNext ? 1 : -1) {
                    if (state->nodes[i].used && state->nodes[i].parent == parent) {
                        result = i;
                        break;
                    }
                }
            }
            return result >= 0 ? result + 1 : 0;
        }
        case kTvmDeleteItem: {
            const uintptr_t handle = static_cast<uintptr_t>(lParam);
            if (static_cast<intptr_t>(handle) == kTvRoot) {
                for (auto& node : state->nodes) node = {};
                state->selected = -1;
            } else {
                if (!handle || handle > kTreeNodeCapacity || !state->nodes[handle - 1].used)
                    return 0;
                bool removed[kTreeNodeCapacity]{};
                removed[handle - 1] = true;
                bool changed = true;
                while (changed) {
                    changed = false;
                    for (size_t i = 0; i < kTreeNodeCapacity; ++i)
                        if (state->nodes[i].used && state->nodes[i].parent >= 0 &&
                            removed[state->nodes[i].parent] && !removed[i]) {
                            removed[i] = true;
                            changed = true;
                        }
                }
                for (size_t i = 0; i < kTreeNodeCapacity; ++i) {
                    if (removed[i]) state->nodes[i] = {};
                    if (state->selected == static_cast<int16_t>(i) && removed[i])
                        state->selected = -1;
                }
            }
            snapshot = *state;
            repaint = true;
            break;
        }
        case kWmMouseMove: {
            const int32_t y = static_cast<int16_t>(
                (static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
            const int16_t hot = state->enabled ? visibleTreeItem(*state,
                std::max(0, (y - 4) / kTreeRowHeight)) : -1;
            repaint = hot != state->hot;
            state->hot = hot;
            snapshot = *state;
            break;
        }
        case kWmLButtonUp: {
            const int32_t y = static_cast<int16_t>((static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
            const int32_t x = static_cast<int16_t>(static_cast<uintptr_t>(lParam) & 0xFFFFu);
            const int16_t index = visibleTreeItem(*state,
                std::max(0, (y - 4) / kTreeRowHeight));
            if (index >= 0 && state->enabled) {
                int32_t depth = 0;
                int16_t parent = state->nodes[index].parent;
                while (parent >= 0 && depth < static_cast<int32_t>(kTreeNodeCapacity)) {
                    ++depth;
                    parent = state->nodes[parent].parent;
                }
                if (x < 12 + depth * kTreeIndent + 17) {
                    bool hasChildren = false;
                    for (const auto& node : state->nodes)
                        if (node.used && node.parent == index) hasChildren = true;
                    if (hasChildren) state->nodes[index].expanded = !state->nodes[index].expanded;
                }
                state->selected = index;
                state->focused = true;
                snapshot = *state;
                repaint = true;
            }
            break;
        }
        case kWmSetFocus: state->focused = true; repaint = true; snapshot = *state; break;
        case kWmKillFocus: state->focused = false; repaint = true; snapshot = *state; break;
        case kWmThemeChanged: (void)InvalidateRect(hwnd, nullptr, 1); return 0;
        case kWmPaint: snapshot = *state; break;
        default:
            if (message >= kTvmFirst && message < kTvmFirst + 128u) return -1;
            return 0;
        }
    }
    if (repaint) (void)InvalidateRect(hwnd, nullptr, 1);
    if (message != kWmPaint) {
        if (message == kWmLButtonUp) (void)SetFocus(hwnd);
        return 0;
    }

    PaintStruct paint{};
    void* hdc = BeginPaint(hwnd, &paint);
    if (!hdc) return 0;
    Rect bounds{};
    if (!GetClientRect(hwnd, &bounds) || bounds.right <= bounds.left ||
        bounds.bottom <= bounds.top) {
        (void)EndPaint(hwnd, &paint);
        return 0;
    }
    void* theme = OpenThemeData(hwnd, L"WINDOW");
    uint32_t background = 0x00F0F0F0u;
    if (theme) {
        uint32_t color{};
        if (GetThemeColor(theme, 1, 1, 3802, &color) == 0) background = color;
    }
    bool painted = fillWithColor(hdc, bounds, background);
    void* buttonTheme = OpenThemeData(hwnd, L"BUTTON");
    const int32_t visibleCount = treeVisibleCount(snapshot);
    const int32_t rowCount = std::min<int32_t>(visibleCount,
        (bounds.bottom - bounds.top) / kTreeRowHeight + 1);
    for (int32_t row = 0; row < rowCount; ++row) {
        const int16_t index = visibleTreeItem(snapshot, row);
        if (index < 0) continue;
        const auto& node = snapshot.nodes[index];
        int32_t depth = 0;
        int16_t parent = node.parent;
        while (parent >= 0 && depth < static_cast<int32_t>(kTreeNodeCapacity)) {
            ++depth;
            parent = snapshot.nodes[parent].parent;
        }
        Rect rowRect{bounds.left + 2, bounds.top + 3 + row * kTreeRowHeight,
            bounds.right - 2, bounds.top + 3 + (row + 1) * kTreeRowHeight};
        const bool selected = snapshot.selected == index;
        const int32_t state = !snapshot.enabled ? 4 : selected ? 3 :
            snapshot.hot == index ? 2 : 1;
        if (selected || snapshot.hot == index) {
            const bool themed = buttonTheme &&
                DrawThemeBackground(buttonTheme, hdc, 1, state, &rowRect, nullptr) == 0;
            if (!themed) painted &= fillWithColor(hdc, rowRect,
                selected ? 0x00DCEBFAu : 0x00E8F3FCu);
        }
        bool hasChildren = false;
        for (const auto& child : snapshot.nodes)
            if (child.used && child.parent == index) hasChildren = true;
        const int32_t glyphX = bounds.left + 7 + depth * kTreeIndent;
        if (hasChildren && (snapshot.style & kTvsHasButtons)) {
            Rect box{glyphX, rowRect.top + 6, glyphX + 10, rowRect.top + 16};
            painted &= fillWithColor(hdc, box, background);
            Rect top{box.left, box.top, box.right, box.top + 1};
            Rect bottom{box.left, box.bottom - 1, box.right, box.bottom};
            Rect left{box.left, box.top, box.left + 1, box.bottom};
            Rect right{box.right - 1, box.top, box.right, box.bottom};
            painted &= fillWithColor(hdc, top, 0x00808080u) &&
                fillWithColor(hdc, bottom, 0x00808080u) &&
                fillWithColor(hdc, left, 0x00808080u) &&
                fillWithColor(hdc, right, 0x00808080u);
            Rect minus{box.left + 3, box.top + 4, box.right - 3, box.top + 5};
            painted &= fillWithColor(hdc, minus, 0x00000000u);
            if (!node.expanded) {
                Rect plus{box.left + 4, box.top + 2, box.left + 5, box.bottom - 2};
                painted &= fillWithColor(hdc, plus, 0x00000000u);
            }
        }
        Rect textRect{glyphX + 15, rowRect.top, bounds.right - 4, rowRect.bottom};
        int32_t length = 0;
        while (length + 1 < static_cast<int32_t>(kTreeTextCapacity) && node.text[length]) ++length;
        if (length) {
            const bool themed = theme && DrawThemeText(theme, hdc, 1, 1, node.text,
                length, 0x00000020u | 0x00000800u, 0, &textRect) == 0;
            if (!themed) {
                uint32_t color = snapshot.enabled ? 0x00000000u : 0x00808080u;
                uint32_t themedColor{};
                if (theme && GetThemeColor(theme, 1, 1, 3803, &themedColor) == 0)
                    color = themedColor;
                const uint32_t oldColor = SetTextColor(hdc, color);
                const int32_t oldMode = SetBkMode(hdc, 1);
                const bool drawn = oldColor != 0xFFFFFFFFu && oldMode > 0 &&
                    TextOutW(hdc, textRect.left, textRect.top + 3, node.text, length) != 0;
                if (oldMode > 0) (void)SetBkMode(hdc, oldMode);
                if (oldColor != 0xFFFFFFFFu) (void)SetTextColor(hdc, oldColor);
                painted &= drawn;
            }
        }
    }
    if (buttonTheme) (void)CloseThemeData(buttonTheme);
    if (theme) (void)CloseThemeData(theme);
    return painted && EndPaint(hwnd, &paint) != 0 ? 1 : 0;
}

bool registerTreeClass() noexcept {
    uint32_t state = g_treeRegistrationState.load(std::memory_order_acquire);
    for (;;) {
        if (state == 2) return true;
        if (state == 3) return false;
        if (state == 0 && g_treeRegistrationState.compare_exchange_weak(
                state, 1, std::memory_order_acq_rel, std::memory_order_acquire)) break;
        state = g_treeRegistrationState.load(std::memory_order_acquire);
    }
    MicaWin32kWindowClassEx wc{};
    wc.cbSize = sizeof(wc);
    wc.wndProc = reinterpret_cast<void*>(TreeViewWndProc);
    wc.className = L"SysTreeView32";
    const bool registered = RegisterClassExW(&wc) != 0;
    g_treeRegistrationState.store(registered ? 2u : 3u, std::memory_order_release);
    return registered;
}

} // namespace

extern "C" {

bool DllMain(void*, unsigned long, void*) noexcept { return true; }

__declspec(dllexport) int32_t InitCommonControlsEx(
    const InitCommonControlsExArgs* args) noexcept {
    if (!args || args->size != sizeof(InitCommonControlsExArgs) ||
        (args->classes & ~(kIccProgress | kIccTab | kIccTreeView)) != 0) return 0;
    if ((args->classes & kIccProgress) && !registerProgressClass()) return 0;
    if ((args->classes & kIccTab) && !registerTabClass()) return 0;
    if ((args->classes & kIccTreeView) && !registerTreeClass()) return 0;
    return 1;
}

// The legacy entry point requests the standard controls set and has no return
// value in the Windows ABI. Keep registration in the same canonical class
// initializers used by the Ex variant.
__declspec(dllexport) void InitCommonControls() noexcept {
    const InitCommonControlsExArgs args{sizeof(InitCommonControlsExArgs),
        kIccProgress | kIccTab | kIccTreeView};
    (void)InitCommonControlsEx(&args);
}

__declspec(dllexport) void* CreatePropertySheetPageW(const void*) noexcept { return nullptr; }
__declspec(dllexport) int64_t PropertySheetW(const void*) noexcept { return -1; }

} // extern "C"
