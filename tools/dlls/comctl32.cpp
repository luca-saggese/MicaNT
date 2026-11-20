/**
 * @file comctl32.cpp
 * @brief Freestanding common controls for the native Ring 3 system image.
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
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
struct TabKeyDownNotification {
    NotifyHeader header;
    uint16_t virtualKey;
    uint16_t padding{};
    uint32_t flags{};
};
struct StatusBarState {
    void* hwnd{};
    int32_t parts[3]{-1, -1, -1};
    wchar_t text[3][128]{};
    int32_t minimumHeight{20};
};
constexpr size_t kListViewCapacity = 8;
constexpr size_t kListViewItemCapacity = 128;
constexpr size_t kListViewColumnCapacity = 16;
constexpr size_t kListViewTextCapacity = 128;
struct ListViewItemState {
    uint32_t state{};
    intptr_t parameter{};
    uint32_t callbackMask{};
    wchar_t text[kListViewColumnCapacity][kListViewTextCapacity]{};
};
struct ListViewColumnState { int32_t width{120}; wchar_t text[kListViewTextCapacity]{}; };
struct ListViewState {
    void* hwnd{};
    void* parent{};
    uint32_t style{}, extendedStyle{}, view{1};
    uintptr_t id{};
    uint32_t itemCount{}, columnCount{};
    int32_t selectionMark{-1};
    ListViewColumnState columns[kListViewColumnCapacity]{};
    ListViewItemState items[kListViewItemCapacity]{};
};

constexpr uint32_t kIccProgress = 0x00000020u;
constexpr uint32_t kIccTab = 0x00000008u;
constexpr uint32_t kIccTreeView = 0x00000002u;
constexpr uint32_t kIccListView = 0x00000001u;
constexpr uint32_t kWmCreate = 0x0001u;
constexpr uint32_t kWmDestroy = 0x0002u;
constexpr uint32_t kWmNCCreate = 0x0081u;
constexpr uint32_t kWmPaint = 0x000Fu;
constexpr uint32_t kWmThemeChanged = 0x031Au;
constexpr uint32_t kWmMouseMove = 0x0200u;
constexpr uint32_t kWmLButtonDown = 0x0201u;
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
constexpr uint32_t kTcmDeleteAllItems = kTcmFirst + 9u;
constexpr uint32_t kTcmGetItemRect = kTcmFirst + 10u;
constexpr uint32_t kTcmGetCurSel = kTcmFirst + 11u;
constexpr uint32_t kTcmSetCurSel = kTcmFirst + 12u;
constexpr uint32_t kTcmHitTest = kTcmFirst + 13u;
constexpr uint32_t kTcmInsertItemW = kTcmFirst + 62u;
constexpr uint32_t kTcmGetItemW = kTcmFirst + 60u;
constexpr uint32_t kTcmSetItemW = kTcmFirst + 61u;
constexpr uint32_t kTcmGetCurFocus = kTcmFirst + 47u;
constexpr uint32_t kTcmSetCurFocus = kTcmFirst + 48u;
constexpr uint32_t kTcmAdjustRect = kTcmFirst + 40u;
constexpr uint32_t kTcmSetItemSize = kTcmFirst + 41u;
constexpr uint32_t kTcmSetPadding = kTcmFirst + 43u;
constexpr uint32_t kTcmGetRowCount = kTcmFirst + 44u;
constexpr uint32_t kTcmSetMinTabWidth = kTcmFirst + 49u;
constexpr uint32_t kTcmDeselectAll = kTcmFirst + 50u;
constexpr uint32_t kTcmHighlightItem = kTcmFirst + 51u;
constexpr uint32_t kTcmSetExtendedStyle = kTcmFirst + 52u;
constexpr uint32_t kTcmGetExtendedStyle = kTcmFirst + 53u;
constexpr uint32_t kCcmSetUnicodeFormat = 0x2005u;
constexpr uint32_t kCcmGetUnicodeFormat = 0x2006u;
constexpr uint32_t kTcnSelChange = static_cast<uint32_t>(-551);
constexpr uint32_t kTcnSelChanging = static_cast<uint32_t>(-552);
constexpr uint32_t kTcnFocusChange = static_cast<uint32_t>(-553);
constexpr uint32_t kTcnKeyDown = static_cast<uint32_t>(-550);
constexpr uint32_t kTcifText = 0x0001u;
constexpr uint32_t kTcifImage = 0x0002u;
constexpr uint32_t kTcifParam = 0x0008u;
constexpr uint32_t kTcifState = 0x0010u;
constexpr uint32_t kTcisButtonPressed = 0x0001u;
constexpr uint32_t kTcisHighlighted = 0x0002u;
constexpr uint32_t kTcsButtons = 0x0100u;
constexpr uint32_t kTcsFocusNever = 0x8000u;
constexpr uint32_t kTcsFocusOnButtonDown = 0x1000u;
constexpr uint32_t kTcsStyleMask = 0x0000FFFEu;
constexpr uint32_t kLvmFirst = 0x1000u;
constexpr uint32_t kLvmGetItemCount = kLvmFirst + 4u;
constexpr uint32_t kLvmGetItemW = kLvmFirst + 75u;
constexpr uint32_t kLvmSetItemW = kLvmFirst + 76u;
constexpr uint32_t kLvmInsertItemW = kLvmFirst + 77u;
constexpr uint32_t kLvmDeleteItem = kLvmFirst + 8u;
constexpr uint32_t kLvmDeleteAllItems = kLvmFirst + 9u;
constexpr uint32_t kLvmGetNextItem = kLvmFirst + 12u;
constexpr uint32_t kLvmEnsureVisible = kLvmFirst + 19u;
constexpr uint32_t kLvmRedrawItems = kLvmFirst + 21u;
constexpr uint32_t kLvmInsertColumnW = kLvmFirst + 97u;
constexpr uint32_t kLvmGetColumnWidth = kLvmFirst + 29u;
constexpr uint32_t kLvmSetExtendedStyle = kLvmFirst + 54u;
constexpr uint32_t kLvmGetExtendedStyle = kLvmFirst + 55u;
constexpr uint32_t kLvmGetSelectedCount = kLvmFirst + 50u;
constexpr uint32_t kLvmSetItemState = kLvmFirst + 43u;
constexpr uint32_t kLvmGetItemState = kLvmFirst + 44u;
constexpr uint32_t kLvmSetSelectionMark = kLvmFirst + 67u;
constexpr uint32_t kLvmGetSelectionMark = kLvmFirst + 66u;
constexpr uint32_t kLvmGetHeader = kLvmFirst + 31u;
constexpr uint32_t kLvifText = 0x0001u;
constexpr uint32_t kLvifParam = 0x0004u;
constexpr uint32_t kLvifState = 0x0008u;
constexpr uint32_t kLvnGetDispInfoW = static_cast<uint32_t>(-177);
constexpr uint32_t kLvcfText = 0x0004u;
constexpr uint32_t kLvcfWidth = 0x0002u;
constexpr uint32_t kLvisFocused = 0x0001u;
constexpr uint32_t kLvisSelected = 0x0002u;
constexpr uint32_t kLvsReport = 0x0001u;
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
struct TabItem {
    wchar_t text[kTabTextCapacity]{};
    uint32_t state{};
    int32_t image{-1};
    intptr_t parameter{};
};
struct TabState {
    void* hwnd{};
    void* parent{};
    uintptr_t id{};
    uint32_t style{};
    int32_t count{};
    int32_t selected{-1};
    int32_t focusIndex{-1};
    int32_t hot{-1};
    int32_t itemWidth{};
    int32_t itemHeight{24};
    int32_t minimumItemWidth{54};
    int32_t horizontalPadding{6};
    int32_t verticalPadding{3};
    uint32_t extendedStyle{};
    bool unicodeFormat{true};
    bool focused{};
    bool enabled{true};
    TabItem items[kTabItemCapacity]{};
};
TabState g_tabs[kTabCapacity]{};
std::atomic<uint32_t> g_tabRegistrationState{0};
TreeState g_trees[kTreeCapacity]{};
std::atomic<uint32_t> g_treeRegistrationState{0};
StatusBarState g_statusBars[16]{};
std::atomic<uint32_t> g_statusRegistrationState{0};
ListViewState g_listViews[kListViewCapacity]{};
std::atomic<uint32_t> g_listViewRegistrationState{0};

struct StateGuard {
    bool held{true};
    StateGuard() noexcept {
        while (g_stateLock.test_and_set(std::memory_order_acquire)) { }
    }
    void release() noexcept {
        if (held) { g_stateLock.clear(std::memory_order_release); held = false; }
    }
    ~StateGuard() { release(); }
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
__declspec(dllimport) void* CreateWindowExW(uint32_t, const wchar_t*, const wchar_t*,
    uint32_t, int32_t, int32_t, int32_t, int32_t, void*, void*, void*, void*);
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
            state.focusIndex = -1;
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
    if (state.count <= 0) return -1;
    int32_t left = bounds.left + 2;
    const int32_t top = bounds.top + 2;
    const int32_t bottom = std::min(bounds.bottom, top + state.itemHeight);
    for (int32_t index = 0; index < state.count; ++index) {
        const auto& item = state.items[index];
        size_t chars{};
        while (chars + 1 < kTabTextCapacity && item.text[chars]) ++chars;
        const int32_t width = state.itemWidth > 0 ? state.itemWidth :
            std::max(state.minimumItemWidth,
                static_cast<int32_t>(chars * 7) + state.horizontalPadding * 2);
        const int32_t right = std::min(bounds.right - 2, left + width);
        if (x >= left && x < right && y >= top && y < bottom) return index;
        left = right;
    }
    return -1;
}

Rect tabItemRect(const TabState& state, const Rect& bounds, int32_t item) noexcept {
    int32_t left = bounds.left + 2;
    for (int32_t index = 0; index < item; ++index) {
        const auto& previous = state.items[index];
        size_t chars{};
        while (chars + 1 < kTabTextCapacity && previous.text[chars]) ++chars;
        left += state.itemWidth > 0 ? state.itemWidth : std::max(state.minimumItemWidth,
            static_cast<int32_t>(chars * 7) + state.horizontalPadding * 2);
    }
    const auto& tab = state.items[item];
    size_t chars{};
    while (chars + 1 < kTabTextCapacity && tab.text[chars]) ++chars;
    const int32_t width = state.itemWidth > 0 ? state.itemWidth : std::max(state.minimumItemWidth,
        static_cast<int32_t>(chars * 7) + state.horizontalPadding * 2);
    const int32_t top = bounds.top + 2;
    return {left, top, std::min(bounds.right - 2, left + width),
            std::min(bounds.bottom, top + state.itemHeight)};
}

void notifyTabSelection(const TabState& state) noexcept {
    if (!state.parent) return;
    const NotifyHeader header{state.hwnd, state.id, kTcnSelChange};
    (void)SendMessageW(state.parent, kWmNotify, state.id,
                       reinterpret_cast<intptr_t>(&header));
}

intptr_t notifyTab(const TabState& state, uint32_t code) noexcept {
    if (!state.parent) return 0;
    const NotifyHeader header{state.hwnd, state.id, code};
    return SendMessageW(state.parent, kWmNotify, state.id,
                       reinterpret_cast<intptr_t>(&header));
}

intptr_t TabControlWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                           intptr_t lParam) noexcept {
    if (!hwnd) return 0;
    if (message == kTcmInsertItemW || message == kWmPaint)
        MicaWin32kDebugInputTrace(143, message, static_cast<uint32_t>(wParam));
    if (message == kWmGetDlgCode) return 0x0005u; // DLGC_WANTARROWS | DLGC_WANTCHARS
    if (message == kWmNCCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create) return 0;
        const uint32_t styles = static_cast<uint32_t>(create->style) & kTcsStyleMask;
        constexpr uint32_t supportedTabStyles = kTcsButtons | kTcsFocusNever |
            kTcsFocusOnButtonDown | 0x0040u; // HOTTRACK
        if (styles & ~supportedTabStyles) return 0;
        TabState initial{};
        initial.hwnd = hwnd;
        initial.parent = create->parent;
        initial.id = reinterpret_cast<uintptr_t>(create->menu);
        initial.style = styles;
        initial.selected = -1;
        initial.focusIndex = -1;
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

    if (message == kWmLButtonDown) {
        TabState before{};
        int32_t index{-1};
        {
            StateGuard guard;
            auto* state = findTab(hwnd);
            if (!state || !state->enabled) return 0;
            Rect bounds{};
            if (!GetClientRect(hwnd, &bounds)) return 0;
            index = tabAt(*state, bounds,
                static_cast<int16_t>(static_cast<uintptr_t>(lParam)),
                static_cast<int16_t>(static_cast<uintptr_t>(lParam) >> 16));
            if (index < 0 || index == state->selected) return 0;
            before = *state;
        }
        if (!(before.style & kTcsFocusNever)) (void)SetFocus(hwnd);
        if (notifyTab(before, kTcnSelChanging) != 0) return 0;
        TabState after{};
        {
            StateGuard guard;
            auto* state = findTab(hwnd);
            if (!state || !validTabIndex(*state, index)) return 0;
            state->selected = index;
            state->focusIndex = index;
            state->items[index].state |= kTcisButtonPressed;
            for (int32_t i = 0; i < state->count; ++i)
                if (i != index) state->items[i].state &= ~kTcisButtonPressed;
            after = *state;
        }
        (void)InvalidateRect(hwnd, nullptr, 1);
        notifyTabSelection(after);
        return 0;
    }
    if (message == kWmLButtonUp) {
        TabState snapshot{};
        {
            StateGuard guard;
            auto* state = findTab(hwnd);
            if (!state) return 0;
            snapshot = *state;
        }
        (void)notifyTab(snapshot, 0xFFFFFFFEu); // NM_CLICK
        return 0;
    }
    if (message == kWmKeyDown) {
        TabState snapshot{};
        {
            StateGuard guard;
            auto* state = findTab(hwnd);
            if (!state) return 0;
            snapshot = *state;
        }
        if (snapshot.parent) {
            TabKeyDownNotification notification{};
            notification.header = {hwnd, snapshot.id, kTcnKeyDown};
            notification.virtualKey = static_cast<uint16_t>(wParam);
            notification.flags = static_cast<uint32_t>(lParam);
            (void)SendMessageW(snapshot.parent, kWmNotify, snapshot.id,
                reinterpret_cast<intptr_t>(&notification));
        }
        if ((wParam == 0x25u || wParam == 0x27u) && snapshot.enabled &&
            snapshot.count > 0) {
            const int32_t next = snapshot.focusIndex + (wParam == 0x25u ? -1 : 1);
            if (next >= 0 && next < snapshot.count)
                (void)SendMessageW(hwnd, kTcmSetCurFocus,
                    static_cast<uintptr_t>(next), 0);
        }
        return 0;
    }

    TabState snapshot{};
    bool selectionChanged = false;
    bool selectionChanging = false;
    bool focusChanged = false;
    bool repaint = false;
    {
        StateGuard guard;
        auto* state = findTab(hwnd);
        if (!state) return 0;
        switch (message) {
            case kTcmGetItemCount: return state->count;
            case kTcmGetCurSel: return state->selected;
            case kTcmGetCurFocus: return state->focusIndex;
            case kTcmSetCurSel: {
                const int32_t previous = state->selected;
                const int32_t index = static_cast<int32_t>(wParam);
                if (index < -1 || index >= state->count) return -1;
                if (previous != index) {
                    if (previous >= 0) state->items[previous].state &= ~kTcisButtonPressed;
                    state->selected = index;
                    state->focusIndex = index;
                    if (index >= 0) state->items[index].state |= kTcisButtonPressed;
                    repaint = true;
                }
                snapshot = *state;
                if (repaint) (void)InvalidateRect(hwnd, nullptr, 1);
                return previous;
            }
            case kTcmSetCurFocus: {
                const int32_t index = static_cast<int32_t>(wParam);
                if (index < 0) {
                    state->focusIndex = -1;
                    if (state->selected != -1) {
                        state->selected = -1;
                        selectionChanged = true;
                        repaint = true;
                    }
                } else if (index < state->count) {
                    const int32_t previousFocus = state->focusIndex;
                    if (previousFocus != index) {
                        state->focusIndex = index;
                        if (state->style & kTcsButtons) {
                            focusChanged = true;
                        } else if (previousFocus != -1 && state->selected != index) {
                            state->selected = index;
                            selectionChanged = true;
                            selectionChanging = true;
                        }
                        repaint = true;
                    }
                } else {
                    return 0;
                }
                snapshot = *state;
                break;
            }
            case kTcmInsertItemW: {
                if (!lParam || state->count >= static_cast<int32_t>(kTabItemCapacity)) return -1;
                const auto* item = reinterpret_cast<const TabItemW*>(lParam);
                if (item->mask & ~(kTcifText | kTcifParam | kTcifImage | kTcifState)) return -1;
                const intptr_t requested = static_cast<intptr_t>(wParam);
                const int32_t index = requested == -1 ? state->count
                    : static_cast<int32_t>(requested);
                if (index < 0 || index > state->count) return -1;
                TabItem inserted{};
                if ((item->mask & kTcifText) && !copyTabText(inserted.text, item->text))
                    return -1;
                if (item->mask & kTcifParam) inserted.parameter = item->parameter;
                if (item->mask & kTcifImage) inserted.image = item->image;
                if (item->mask & kTcifState) inserted.state = item->state & item->stateMask;
                for (int32_t current = state->count; current > index; --current)
                    state->items[current] = state->items[current - 1];
                state->items[index] = inserted;
                ++state->count;
                if (state->selected >= index) ++state->selected;
                if (state->selected < 0) state->selected = 0;
                if (state->focusIndex >= index) ++state->focusIndex;
                if (state->focusIndex < 0) state->focusIndex = index;
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
                if (!state->count) state->focusIndex = -1;
                else if (state->focusIndex > index) --state->focusIndex;
                else if (state->focusIndex == index)
                    state->focusIndex = state->selected;
                snapshot = *state;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 1;
            }
            case kTcmDeleteAllItems:
                if (!state->count) return 1;
                for (int32_t i = 0; i < state->count; ++i) state->items[i] = {};
                state->count = 0;
                state->selected = state->focusIndex = state->hot = -1;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 1;
            case kTcmGetItemRect: {
                if (!lParam || !validTabIndex(*state, static_cast<intptr_t>(wParam))) return 0;
                Rect bounds{};
                if (!GetClientRect(hwnd, &bounds)) return 0;
                *reinterpret_cast<Rect*>(lParam) = tabItemRect(*state, bounds,
                    static_cast<int32_t>(wParam));
                return 1;
            }
            case kTcmAdjustRect: {
                if (!lParam) return -1;
                auto& rect = *reinterpret_cast<Rect*>(lParam);
                if (wParam) {
                    rect.left -= 4;
                    rect.right += 4;
                    rect.top -= state->itemHeight + 4;
                    rect.bottom += 4;
                } else {
                    rect.left += 4;
                    rect.right -= 4;
                    rect.top += state->itemHeight + 4;
                    rect.bottom -= 4;
                }
                return 0;
            }
            case kTcmSetItemSize: {
                const int32_t oldWidth = state->itemWidth;
                const int32_t oldHeight = state->itemHeight;
                const int32_t width = static_cast<int16_t>(static_cast<uintptr_t>(lParam));
                const int32_t height = static_cast<int16_t>(static_cast<uintptr_t>(lParam) >> 16);
                if (width <= 0 || height <= 0) return 0;
                state->itemWidth = width;
                state->itemHeight = height;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return static_cast<uint16_t>(oldWidth) |
                    (static_cast<uint32_t>(static_cast<uint16_t>(oldHeight)) << 16);
            }
            case kTcmSetPadding:
                state->horizontalPadding = static_cast<uint16_t>(static_cast<uintptr_t>(lParam));
                state->verticalPadding = static_cast<uint16_t>(static_cast<uintptr_t>(lParam) >> 16);
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 0;
            case kTcmGetRowCount: return state->count ? 1 : 0;
            case kTcmSetMinTabWidth: {
                const int32_t previous = state->minimumItemWidth;
                state->minimumItemWidth = std::max<int32_t>(0, static_cast<int32_t>(lParam));
                (void)InvalidateRect(hwnd, nullptr, 1);
                return previous;
            }
            case kTcmDeselectAll:
                for (int32_t i = 0; i < state->count; ++i)
                    if (!wParam || i != state->focusIndex)
                        state->items[i].state &= ~kTcisButtonPressed;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 0;
            case kTcmHighlightItem: {
                if (!validTabIndex(*state, static_cast<intptr_t>(wParam))) return 0;
                auto& item = state->items[wParam];
                if (static_cast<uint16_t>(lParam)) item.state |= kTcisHighlighted;
                else item.state &= ~kTcisHighlighted;
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 1;
            }
            case kTcmSetExtendedStyle: {
                const uint32_t previous = state->extendedStyle;
                const uint32_t mask = static_cast<uint32_t>(wParam);
                state->extendedStyle = mask ? (previous & ~mask) |
                    (static_cast<uint32_t>(lParam) & mask) : static_cast<uint32_t>(lParam);
                (void)InvalidateRect(hwnd, nullptr, 1);
                return previous;
            }
            case kTcmGetExtendedStyle: return state->extendedStyle;
            case kCcmSetUnicodeFormat: {
                const bool previous = state->unicodeFormat;
                state->unicodeFormat = wParam != 0;
                return previous;
            }
            case kCcmGetUnicodeFormat: return state->unicodeFormat;
            case kTcmGetItemW: {
                if (!lParam || !validTabIndex(*state, static_cast<intptr_t>(wParam))) return 0;
                auto* item = reinterpret_cast<TabItemW*>(lParam);
                if (item->mask & ~(kTcifText | kTcifParam | kTcifImage | kTcifState)) return 0;
                if (item->mask & kTcifImage) item->image = state->items[wParam].image;
                if (item->mask & kTcifParam)
                    item->parameter = state->items[wParam].parameter;
                if (item->mask & kTcifState)
                    item->state = state->items[wParam].state & item->stateMask;
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
                if (item->mask & ~(kTcifText | kTcifParam | kTcifImage | kTcifState)) return 0;
                auto& destination = state->items[wParam];
                if ((item->mask & kTcifText) && !copyTabText(destination.text, item->text))
                    return 0;
                if (item->mask & kTcifParam) destination.parameter = item->parameter;
                if (item->mask & kTcifImage) destination.image = item->image;
                if (item->mask & kTcifState) destination.state =
                    (destination.state & ~item->stateMask) | (item->state & item->stateMask);
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
            case kWmSetFocus: state->focused = true; repaint = true; snapshot = *state; break;
            case kWmKillFocus: state->focused = false; repaint = true; snapshot = *state; break;
            case 0x000Au: state->enabled = wParam != 0; repaint = true; snapshot = *state; break;
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
    const bool changingCancelled = selectionChanging &&
        notifyTab(snapshot, kTcnSelChanging) != 0;
    if (selectionChanged && !changingCancelled) notifyTabSelection(snapshot);
    if (focusChanged) (void)notifyTab(snapshot, kTcnFocusChange);
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
    for (int32_t index = 0; index < snapshot.count; ++index) {
        Rect tab = tabItemRect(snapshot, bounds, index);
        if (snapshot.style & kTcsButtons) {
            tab.bottom = std::min(bounds.bottom, tab.bottom + snapshot.verticalPadding);
        } else if (snapshot.selected == index) {
            tab.left = std::max(bounds.left, tab.left - 2);
            tab.right = std::min(bounds.right, tab.right + 2);
            tab.top = std::max(bounds.top, tab.top - 2);
        }
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
        Rect text{tab.left + snapshot.horizontalPadding, tab.top + snapshot.verticalPadding,
                  tab.right - snapshot.horizontalPadding,
                  tab.bottom - snapshot.verticalPadding};
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

StatusBarState* findStatusBar(void* hwnd) noexcept {
    for (auto& state : g_statusBars) if (state.hwnd == hwnd) return &state;
    return nullptr;
}

intptr_t StatusBarWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                          intptr_t lParam) noexcept {
    if (message == kWmNCCreate) return 1;
    if (message == kWmCreate) {
        StateGuard guard;
        for (auto& state : g_statusBars) if (!state.hwnd) {
            state = {};
            state.hwnd = hwnd;
            state.minimumHeight = 20;
            return 0;
        }
        return -1;
    }
    if (message == kWmDestroy) {
        StateGuard guard;
        if (auto* state = findStatusBar(hwnd)) *state = {};
        return 0;
    }
    if (message == 0x0408u /* SB_SETMINHEIGHT */) {
        StateGuard guard;
        if (auto* state = findStatusBar(hwnd)) state->minimumHeight = static_cast<int32_t>(wParam);
        return 0;
    }
    if (message == 0x0404u /* SB_SETPARTS */) {
        StateGuard guard;
        auto* state = findStatusBar(hwnd);
        if (!state || !lParam || !wParam || wParam > 3) return 0;
        const auto* parts = reinterpret_cast<const int32_t*>(lParam);
        for (size_t i = 0; i < wParam; ++i) state->parts[i] = parts[i];
        for (size_t i = static_cast<size_t>(wParam); i < 3; ++i) state->parts[i] = -1;
        return 1;
    }
    if (message == 0x0401u /* SB_SETTEXTA */ || message == 0x040Bu /* SB_SETTEXTW */) {
        const size_t index = static_cast<size_t>(wParam & 0xFFu);
        if (index >= 3) return 0;
        StateGuard guard;
        auto* state = findStatusBar(hwnd);
        if (!state) return 0;
        const auto* text = reinterpret_cast<const wchar_t*>(lParam);
        size_t length = 0;
        if (text) while (length + 1 < std::size(state->text[index]) && text[length]) ++length;
        for (size_t i = 0; i <= length; ++i) state->text[index][i] = text ? text[i] : 0;
        (void)InvalidateRect(hwnd, nullptr, 1);
        return 1;
    }
    if (message == kWmPaint) {
        PaintStruct paint{};
        void* dc = BeginPaint(hwnd, &paint);
        if (!dc) return 0;
        Rect client{};
        (void)GetClientRect(hwnd, &client);
        StatusBarState snapshot{};
        {
            StateGuard guard;
            if (auto* state = findStatusBar(hwnd)) snapshot = *state;
        }
        (void)fillWithColor(dc, client, 0x00F0F0F0u);
        const uint32_t oldColor = SetTextColor(dc, 0x00000000u);
        const int32_t oldMode = SetBkMode(dc, 1);
        int32_t left = 2;
        for (size_t i = 0; i < 3; ++i) {
            const int32_t right = snapshot.parts[i] < 0 ? client.right : snapshot.parts[i];
            if (snapshot.text[i][0]) {
                size_t length = 0;
                while (length < std::size(snapshot.text[i]) && snapshot.text[i][length]) ++length;
                (void)TextOutW(dc, left + 4, 2, snapshot.text[i], static_cast<int32_t>(length));
            }
            if (right < client.right) {
                Rect divider{right - 2, 2, right - 1, client.bottom - 2};
                (void)fillWithColor(dc, divider, 0x00B0B0B0u);
            }
            left = right;
        }
        if (oldMode > 0) (void)SetBkMode(dc, oldMode);
        if (oldColor != 0xFFFFFFFFu) (void)SetTextColor(dc, oldColor);
        return EndPaint(hwnd, &paint) != 0 ? 1 : 0;
    }
    if (message == kWmThemeChanged || message == 0x0005u /* WM_SIZE */) {
        (void)InvalidateRect(hwnd, nullptr, 1);
        return 0;
    }
    return 0;
}

bool registerStatusBarClass() noexcept {
    uint32_t state = g_statusRegistrationState.load(std::memory_order_acquire);
    for (;;) {
        if (state == 2) return true;
        if (state == 3) return false;
        if (state == 0 && g_statusRegistrationState.compare_exchange_weak(
                state, 1, std::memory_order_acq_rel, std::memory_order_acquire)) break;
        state = g_statusRegistrationState.load(std::memory_order_acquire);
    }
    MicaWin32kWindowClassEx wc{};
    wc.cbSize = sizeof(wc);
    wc.wndProc = reinterpret_cast<void*>(StatusBarWndProc);
    wc.className = L"msctls_statusbar32";
    const bool registered = RegisterClassExW(&wc) != 0;
    g_statusRegistrationState.store(registered ? 2u : 3u, std::memory_order_release);
    return registered;
}

struct ListViewItemW {
    uint32_t mask;
    int32_t item, subItem;
    uint32_t state, stateMask;
    wchar_t* text;
    int32_t textCapacity, image;
    intptr_t parameter;
    int32_t indent, groupId;
};
struct ListViewColumnW {
    uint32_t mask;
    int32_t format, width;
    wchar_t* text;
    int32_t textCapacity, subItem, image, order;
};

ListViewState* findListView(void* hwnd) noexcept {
    for (auto& state : g_listViews) if (state.hwnd == hwnd) return &state;
    return nullptr;
}

bool copyListText(wchar_t* destination, const wchar_t* source, size_t capacity) noexcept {
    if (!destination || !capacity) return false;
    if (reinterpret_cast<uintptr_t>(source) == UINTPTR_MAX) return false;
    size_t length{};
    if (source && reinterpret_cast<uintptr_t>(source) > UINT16_MAX) {
        while (length + 1 < capacity && source[length]) ++length;
        if (length + 1 == capacity && source[length]) return false;
    }
    for (size_t i = 0; i < length; ++i) destination[i] = source[i];
    destination[length] = 0;
    return true;
}

bool getCallbackText(const ListViewState& state, uint32_t row, uint32_t column,
                     wchar_t* text, size_t capacity) noexcept {
    if (!state.parent || !text || capacity == 0 || row >= state.itemCount ||
        column >= kListViewColumnCapacity) return false;
    struct DisplayInfo { NotifyHeader header; ListViewItemW item; } info{};
    info.header = {state.hwnd, state.id, kLvnGetDispInfoW};
    info.item.mask = kLvifText | kLvifParam;
    info.item.item = static_cast<int32_t>(row);
    info.item.subItem = static_cast<int32_t>(column);
    info.item.text = text;
    info.item.textCapacity = static_cast<int32_t>(std::min<size_t>(capacity, INT32_MAX));
    info.item.parameter = state.items[row].parameter;
    text[0] = L'\0';
    (void)SendMessageW(state.parent, kWmNotify, state.id,
                       reinterpret_cast<intptr_t>(&info));
    static uint32_t tracedCells = 0;
    if (tracedCells < 64) {
        ++tracedCells;
        size_t length = 0;
        while (length + 1 < capacity && text[length]) ++length;
        MicaWin32kDebugInputTrace(155, row, column);
        MicaWin32kDebugInputTrace(156, static_cast<uint32_t>(length),
                                 text[0] ? 1u : 0u);
    }
    return true;
}

intptr_t ListViewWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                         intptr_t lParam) noexcept {
    if (!hwnd) return 0;
    if (message == kWmGetDlgCode) return 0x0004u; // DLGC_WANTCHARS
    if (message == kWmNCCreate) return 1;
    if (message == kWmCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create) return -1;
        StateGuard guard;
        for (auto& state : g_listViews) {
            if (state.hwnd) continue;
            state = {};
            state.hwnd = hwnd;
            state.parent = create->parent;
            state.style = static_cast<uint32_t>(create->style);
            state.view = state.style & 0x0003u;
            state.id = reinterpret_cast<uintptr_t>(create->menu);
            state.selectionMark = -1;
            return 0;
        }
        return -1;
    }
    if (message == kWmDestroy) {
        StateGuard guard;
        if (auto* state = findListView(hwnd)) *state = {};
        return 0;
    }
    if (message == kWmPaint) {
        ListViewState snapshot{};
        {
            StateGuard guard;
            if (auto* state = findListView(hwnd)) snapshot = *state;
            else return 0;
        }
        PaintStruct paint{};
        void* dc = BeginPaint(hwnd, &paint);
        if (!dc) return 0;
        Rect client{};
        if (!GetClientRect(hwnd, &client)) { (void)EndPaint(hwnd, &paint); return 0; }
        (void)fillWithColor(dc, client, 0x00FFFFFFu);
        uint32_t headerColor = 0x00E8E8E8u;
        int32_t y = 0;
        if (snapshot.view == kLvsReport && snapshot.columnCount) {
            Rect header{0, 0, client.right, 24};
            (void)fillWithColor(dc, header, headerColor);
            int32_t x = 0;
            for (uint32_t col = 0; col < snapshot.columnCount && x < client.right; ++col) {
                const auto& column = snapshot.columns[col];
                if (column.text[0]) (void)TextOutW(dc, x + 6, 5, column.text,
                    static_cast<int32_t>(std::char_traits<wchar_t>::length(column.text)));
                x += std::max(24, column.width);
                Rect divider{x - 1, 0, x, 24};
                (void)fillWithColor(dc, divider, 0x00C0C0C0u);
            }
            Rect headerLine{0, 23, client.right, 24};
            (void)fillWithColor(dc, headerLine, 0x00808080u);
            y = 24;
        }
        for (uint32_t row = 0; row < snapshot.itemCount && y < client.bottom; ++row) {
            const auto& item = snapshot.items[row];
            const bool selected = (item.state & kLvisSelected) != 0;
            if (selected) {
                Rect rowRect{0, y, client.right, y + 22};
                (void)fillWithColor(dc, rowRect, 0x00D77800u);
            }
            int32_t x = 4;
            const uint32_t cells = snapshot.view == kLvsReport
                ? std::max(1u, snapshot.columnCount) : 1u;
            for (uint32_t col = 0; col < cells; ++col) {
                const int32_t width = col < snapshot.columnCount
                    ? std::max(24, snapshot.columns[col].width) : client.right - x;
                wchar_t callbackText[kListViewTextCapacity]{};
                const wchar_t* cellText = item.text[col];
                if (item.callbackMask & (1u << col)) {
                    if (getCallbackText(snapshot, row, col, callbackText,
                                        kListViewTextCapacity)) cellText = callbackText;
                }
                if (cellText[0]) (void)TextOutW(dc, x + 2, y + 3, cellText,
                    static_cast<int32_t>(std::char_traits<wchar_t>::length(cellText)));
                x += width;
            }
            y += 22;
        }
        return EndPaint(hwnd, &paint) != 0 ? 1 : 0;
    }
    if (message == kWmLButtonUp) {
        Rect client{};
        if (!GetClientRect(hwnd, &client)) return 0;
        const int32_t y = static_cast<int16_t>((static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
        const int32_t row = (y - (client.top + 24)) / 22;
        StateGuard guard;
        auto* state = findListView(hwnd);
        if (state && row >= 0 && static_cast<uint32_t>(row) < state->itemCount) {
            for (uint32_t i = 0; i < state->itemCount; ++i)
                state->items[i].state &= ~(kLvisSelected | kLvisFocused);
            state->items[row].state |= kLvisSelected | kLvisFocused;
            state->selectionMark = row;
            (void)InvalidateRect(hwnd, nullptr, 1);
        }
        return 0;
    }

    StateGuard guard;
    auto* state = findListView(hwnd);
    if (!state) return 0;
    switch (message) {
        case kLvmGetItemCount: return state->itemCount;
        case kLvmInsertColumnW: {
            if (!lParam || state->columnCount >= kListViewColumnCapacity) return -1;
            const auto* column = reinterpret_cast<const ListViewColumnW*>(lParam);
            const size_t index = std::min<size_t>(static_cast<size_t>(wParam), state->columnCount);
            for (uint32_t i = state->columnCount; i > index; --i)
                state->columns[i] = state->columns[i - 1];
            auto& destination = state->columns[index];
            destination = {};
            destination.width = (column->mask & kLvcfWidth) ? std::max(24, column->width) : 120;
            if ((column->mask & kLvcfText) && !copyListText(destination.text, column->text,
                    kListViewTextCapacity)) return -1;
            ++state->columnCount;
            (void)InvalidateRect(hwnd, nullptr, 1);
            return static_cast<intptr_t>(index);
        }
        case kLvmGetColumnWidth:
            return wParam < state->columnCount ? state->columns[wParam].width : 0;
        case kLvmInsertItemW: {
            if (!lParam || state->itemCount >= kListViewItemCapacity) return -1;
            const auto* source = reinterpret_cast<const ListViewItemW*>(lParam);
            if (source->item < 0 || static_cast<uint32_t>(source->item) > state->itemCount)
                return -1;
            const uint32_t index = static_cast<uint32_t>(source->item);
            for (uint32_t i = state->itemCount; i > index; --i)
                state->items[i] = state->items[i - 1];
            state->items[index] = {};
            if (source->mask & kLvifText) {
                if (reinterpret_cast<uintptr_t>(source->text) == UINTPTR_MAX)
                    state->items[index].callbackMask |= 1u;
                else if (!copyListText(state->items[index].text[0], source->text,
                                       kListViewTextCapacity)) return -1;
            }
            if (source->mask & kLvifParam) state->items[index].parameter = source->parameter;
            if (source->mask & kLvifState) state->items[index].state = source->state & source->stateMask;
            ++state->itemCount;
            (void)InvalidateRect(hwnd, nullptr, 1);
            return index;
        }
        case kLvmSetItemW:
        case kLvmGetItemW: {
            if (!lParam) return 0;
            auto* item = reinterpret_cast<ListViewItemW*>(lParam);
            if (item->item < 0 || static_cast<uint32_t>(item->item) >= state->itemCount ||
                item->subItem < 0 || static_cast<uint32_t>(item->subItem) >= kListViewColumnCapacity)
                return 0;
            auto& destination = state->items[item->item];
            if (message == kLvmSetItemW) {
                if (item->mask & kLvifText) {
                    if (reinterpret_cast<uintptr_t>(item->text) == UINTPTR_MAX) {
                        destination.callbackMask |= 1u << item->subItem;
                        destination.text[item->subItem][0] = L'\0';
                    } else {
                        destination.callbackMask &= ~(1u << item->subItem);
                        if (!copyListText(destination.text[item->subItem], item->text,
                                          kListViewTextCapacity)) return 0;
                    }
                }
                if (item->mask & kLvifParam) destination.parameter = item->parameter;
                if (item->mask & kLvifState) destination.state =
                    (destination.state & ~item->stateMask) | (item->state & item->stateMask);
                (void)InvalidateRect(hwnd, nullptr, 1);
                return 1;
            }
            if (item->mask & kLvifText) {
                if (destination.callbackMask & (1u << item->subItem)) {
                    const auto snapshot = *state;
                    // Parent notifications must run without the global control-state lock.
                    guard.release();
                    const bool ok = getCallbackText(snapshot, static_cast<uint32_t>(item->item),
                        static_cast<uint32_t>(item->subItem), item->text,
                        item->textCapacity > 0 ? static_cast<size_t>(item->textCapacity) : 1u);
                    return ok ? 1 : 0;
                }
                (void)copyListText(item->text, destination.text[item->subItem],
                    item->textCapacity > 0 ? static_cast<size_t>(item->textCapacity) : 1u);
            }
            if (item->mask & kLvifParam) item->parameter = destination.parameter;
            if (item->mask & kLvifState) item->state = destination.state & item->stateMask;
            return 1;
        }
        case kLvmDeleteItem: {
            if (wParam >= state->itemCount) return 0;
            for (uint32_t i = static_cast<uint32_t>(wParam); i + 1 < state->itemCount; ++i)
                state->items[i] = state->items[i + 1];
            state->items[--state->itemCount] = {};
            if (state->selectionMark == static_cast<int32_t>(wParam)) state->selectionMark = -1;
            (void)InvalidateRect(hwnd, nullptr, 1);
            return 1;
        }
        case kLvmDeleteAllItems:
            for (uint32_t i = 0; i < state->itemCount; ++i) state->items[i] = {};
            state->itemCount = 0; state->selectionMark = -1;
            (void)InvalidateRect(hwnd, nullptr, 1);
            return 1;
        case kLvmGetItemState:
            return wParam < state->itemCount
                ? state->items[wParam].state & static_cast<uint32_t>(lParam) : 0;
        case kLvmSetItemState: {
            if (!lParam) return 0;
            const auto* item = reinterpret_cast<const ListViewItemW*>(lParam);
            const uint32_t first = wParam == UINT32_MAX ? 0u : static_cast<uint32_t>(wParam);
            const uint32_t last = wParam == UINT32_MAX ? state->itemCount : first + 1;
            if (first > state->itemCount || last > state->itemCount) return 0;
            for (uint32_t i = first; i < last; ++i)
                state->items[i].state = (state->items[i].state & ~item->stateMask) |
                    (item->state & item->stateMask);
            (void)InvalidateRect(hwnd, nullptr, 1);
            return 1;
        }
        case kLvmGetNextItem: {
            const uint32_t wanted = static_cast<uint32_t>(lParam);
            uint32_t first = wParam == UINT32_MAX ? 0u : static_cast<uint32_t>(wParam + 1);
            for (uint32_t i = first; i < state->itemCount; ++i)
                if ((state->items[i].state & wanted) == wanted) return i;
            return -1;
        }
        case kLvmGetSelectedCount: {
            uint32_t count{};
            for (uint32_t i = 0; i < state->itemCount; ++i)
                if (state->items[i].state & kLvisSelected) ++count;
            return count;
        }
        case kLvmSetSelectionMark: {
            const int32_t previous = state->selectionMark;
            state->selectionMark = static_cast<int32_t>(wParam);
            return previous;
        }
        case kLvmGetSelectionMark: return state->selectionMark;
        case kLvmSetExtendedStyle: {
            const uint32_t previous = state->extendedStyle;
            const uint32_t mask = static_cast<uint32_t>(wParam);
            state->extendedStyle = mask ? (previous & ~mask) | (static_cast<uint32_t>(lParam) & mask)
                                        : static_cast<uint32_t>(lParam);
            (void)InvalidateRect(hwnd, nullptr, 1);
            return previous;
        }
        case kLvmGetExtendedStyle: return state->extendedStyle;
        case kLvmEnsureVisible:
        case kLvmRedrawItems: (void)InvalidateRect(hwnd, nullptr, 1); return 1;
        case kLvmGetHeader: return reinterpret_cast<intptr_t>(hwnd);
        default:
            if (message >= kLvmFirst && message < kLvmFirst + 160u) return 0;
            return 0;
    }
}

bool registerListViewClass() noexcept {
    uint32_t state = g_listViewRegistrationState.load(std::memory_order_acquire);
    for (;;) {
        if (state == 2) return true;
        if (state == 3) return false;
        if (state == 0 && g_listViewRegistrationState.compare_exchange_weak(
                state, 1, std::memory_order_acq_rel, std::memory_order_acquire)) break;
        state = g_listViewRegistrationState.load(std::memory_order_acquire);
    }
    MicaWin32kWindowClassEx wc{};
    wc.cbSize = sizeof(wc);
    wc.wndProc = reinterpret_cast<void*>(ListViewWndProc);
    wc.className = L"SysListView32";
    const bool registered = RegisterClassExW(&wc) != 0;
    g_listViewRegistrationState.store(registered ? 2u : 3u, std::memory_order_release);
    return registered;
}

} // namespace

extern "C" {

int32_t __stdcall DllMain(void*, uint32_t reason, void*) {
    // Common-control classes must be available while the resource dialog
    // manager creates child controls, before WM_INITDIALOG can call the
    // application's later InitCommonControls entry point.
    if (reason == 1u /* DLL_PROCESS_ATTACH */)
        return registerProgressClass() && registerTabClass() && registerTreeClass() &&
            registerListViewClass();
    return 1;
}

__declspec(dllexport) int32_t __stdcall InitCommonControlsEx(
    const InitCommonControlsExArgs* args) {
    if (!args || args->size != sizeof(InitCommonControlsExArgs) ||
        (args->classes & ~(kIccProgress | kIccTab | kIccTreeView | kIccListView)) != 0) return 0;
    if ((args->classes & kIccListView) && !registerListViewClass()) return 0;
    if ((args->classes & kIccProgress) && !registerProgressClass()) return 0;
    if ((args->classes & kIccTab) && !registerTabClass()) return 0;
    if ((args->classes & kIccTreeView) && !registerTreeClass()) return 0;
    return 1;
}

// The legacy entry point requests the standard controls set and has no return
// value in the Windows ABI. Keep registration in the same canonical class
// initializers used by the Ex variant.
__declspec(dllexport) void __stdcall InitCommonControls() {
    const InitCommonControlsExArgs args{sizeof(InitCommonControlsExArgs),
        kIccProgress | kIccTab | kIccTreeView | kIccListView};
    (void)InitCommonControlsEx(&args);
}

__declspec(dllexport) void* __stdcall CreateStatusWindowW(
    int32_t style, const wchar_t* text, void* parent, uint32_t identifier) {
    const bool registered = registerStatusBarClass();
    MicaWin32kDebugInputTrace(141, registered ? 1u : 0u,
        parent ? 1u : 0u);
    if (!parent || !registered) return nullptr;
    void* status = CreateWindowExW(0, L"msctls_statusbar32", text ? text : L"",
        static_cast<uint32_t>(style) | 0x40000000u /* WS_CHILD */ | 0x10000000u /* WS_VISIBLE */,
        0, 0, 0, 0, parent, reinterpret_cast<void*>(static_cast<uintptr_t>(identifier)),
        nullptr, nullptr);
    MicaWin32kDebugInputTrace(142, status ? 1u : 0u, style);
    return status;
}

struct _PROPSHEETPAGEA_V3;
struct _PROPSHEETPAGEW_V3;
struct _PROPSHEETHEADERA;
struct _PROPSHEETHEADERW;
struct _PSP;
using HPROPSHEETPAGE = _PSP*;
using LPCPROPSHEETPAGEA = const _PROPSHEETPAGEA_V3*;
using LPCPROPSHEETPAGEW = const _PROPSHEETPAGEW_V3*;
using LPCPROPSHEETHEADERA = const _PROPSHEETHEADERA*;
using LPCPROPSHEETHEADERW = const _PROPSHEETHEADERW*;

__declspec(dllexport) HPROPSHEETPAGE __stdcall CreatePropertySheetPageA(LPCPROPSHEETPAGEA) {
    return nullptr;
}

__declspec(dllexport) HPROPSHEETPAGE __stdcall CreatePropertySheetPageW(LPCPROPSHEETPAGEW) {
    return nullptr;
}

__declspec(dllexport) intptr_t __stdcall PropertySheetA(LPCPROPSHEETHEADERA) {
    return static_cast<intptr_t>(-1);
}

__declspec(dllexport) intptr_t __stdcall PropertySheetW(LPCPROPSHEETHEADERW) {
    return static_cast<intptr_t>(-1);
}

} // extern "C"
