#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "micant/shared/native_gdi_api.h"
#include "micant/shared/native_uxtheme_api.h"
#include "micant/shared/win32k_client.h"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"
#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"

#if defined(BUILD_SYSTEM32_USER32_DLL)
namespace {
constexpr size_t kMenuCapacity = 32;
constexpr size_t kItemCapacity = 32;
constexpr size_t kTextCapacity = 128;
constexpr size_t kMenuBarCapacity = 64;
constexpr uintptr_t kMenuTag = sizeof(uintptr_t) == 8
    ? uintptr_t{0x4D4E550000000000ull} : uintptr_t{0x4D4E0000u};
constexpr uintptr_t kMenuIndexMask = ~kMenuTag;
constexpr uint32_t kByPosition = 0x0400u;
constexpr uint32_t kPopup = 0x0010u;
constexpr uint32_t kSeparator = 0x0800u;
constexpr uint32_t kChecked = 0x0008u;
constexpr uint32_t kDisabled = 0x0003u;
constexpr uint32_t kBreak = 0x0040u;
constexpr uint32_t kBarBreak = 0x0020u;
constexpr uint32_t kRadioCheck = 0x0200u;
constexpr uint32_t kDefault = 0x1000u;
constexpr uint32_t kHilite = 0x0080u;
constexpr uint32_t kOwnerDraw = 0x0100u;
constexpr uint32_t kBitmap = 0x0004u;
constexpr uint32_t kMiimState = 0x00000001u;
constexpr uint32_t kMiimId = 0x00000002u;
constexpr uint32_t kMiimSubMenu = 0x00000004u;
constexpr uint32_t kMiimCheckmarks = 0x00000008u;
constexpr uint32_t kMiimType = 0x00000010u;
constexpr uint32_t kMiimData = 0x00000020u;
constexpr uint32_t kMiimString = 0x00000040u;
constexpr uint32_t kMiimBitmap = 0x00000080u;
constexpr uint32_t kSupportedMiim = kMiimState | kMiimId | kMiimSubMenu |
    kMiimCheckmarks | kMiimType | kMiimData | kMiimString | kMiimBitmap;

struct MenuItemInfoW {
    uint32_t cbSize, fMask, fType, fState, wID;
    void* hSubMenu;
    void* hbmpChecked;
    void* hbmpUnchecked;
    uintptr_t dwItemData;
    wchar_t* dwTypeData;
    uint32_t cch;
    void* hbmpItem;
};
struct MenuItem {
    uint32_t type{}, state{}, id{};
    void* submenu{};
    void* checkedBitmap{};
    void* uncheckedBitmap{};
    void* itemBitmap{};
    uintptr_t data{};
    wchar_t text[kTextCapacity]{};
};
struct Menu {
    uintptr_t handle{};
    uint16_t count{};
    bool active{};
    MenuItem items[kItemCapacity]{};
};
struct PopupSession {
    void* hwnd{};
    void* owner{};
    void* sourceMenu{};
    MicaThemeHandle theme{};
    uint32_t count{};
    int32_t selected{-1};
    int32_t pressed{-1};
    int32_t x{}, y{}, width{240}, height{};
    bool active{}, done{}, chose{};
    MenuItem items[kItemCapacity]{};
};
struct MenuBarState {
    void* hwnd{};
    void* owner{};
    int32_t left{}, top{};
    uint32_t width{};
    int32_t selected{-1};
    int32_t pressed{-1};
    bool active{};
};
Menu g_menus[kMenuCapacity]{};
PopupSession g_popup{};
MenuBarState g_menuBars[kMenuBarCapacity]{};
uintptr_t g_nextHandle{1};
uint32_t g_menuLock{};
struct ClipboardState {
    uint32_t ownerThread{};
    uint32_t ownerWindow{};
    void* ansi{};
    void* unicode{};
    bool open{};
} g_clipboard{};
using DialogProcedure = intptr_t (*)(void*, uint32_t, uintptr_t, intptr_t);
constexpr int32_t kDwlpDlgProc = static_cast<int32_t>(sizeof(uintptr_t));
constexpr int32_t kDialogWindowExtra = 30;
void finishPopup(bool chose, int32_t index, bool postClose) noexcept;

extern "C" {
uint16_t RegisterClassExW(const void*) noexcept;
void* CreateWindowExW(uint32_t, const wchar_t*, const wchar_t*, uint32_t,
    int32_t, int32_t, int32_t, int32_t, void*, void*, void*, void*) noexcept;
int32_t ShowWindow(void*, int32_t) noexcept;
int32_t UpdateWindow(void*) noexcept;
int32_t DestroyWindow(void*) noexcept;
int32_t GetMessageW(micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg*,
    void*, uint32_t, uint32_t) noexcept;
intptr_t DispatchMessageW(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg*) noexcept;
void* SetFocus(void*) noexcept;
int32_t InvalidateRect(void*, const MicaWin32kRect*, int32_t) noexcept;
void* BeginPaint(void*, micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct*) noexcept;
int32_t EndPaint(void*, const micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct*) noexcept;
intptr_t SendMessageW(void*, uint32_t, uintptr_t, intptr_t) noexcept;
intptr_t DefWindowProcW(void*, uint32_t, uintptr_t, intptr_t) noexcept;
int32_t PostMessageW(void*, uint32_t, uintptr_t, intptr_t) noexcept;
int32_t IsWindowVisible(void*) noexcept;
int32_t EnumChildWindows(void*, void*, intptr_t) noexcept;
intptr_t SetWindowLongPtrW(void*, int32_t, intptr_t) noexcept;
intptr_t GetWindowLongPtrW(void*, int32_t) noexcept;
int32_t GetClientRect(void*, MicaWin32kRect*) noexcept;
int32_t GetWindowRect(void*, MicaWin32kRect*) noexcept;
int32_t SetWindowPos(void*, void*, int32_t, int32_t, int32_t, int32_t, uint32_t) noexcept;
int32_t AdjustWindowRectEx(
    micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect*, uint32_t, int32_t,
    uint32_t) noexcept;
void PostQuitMessage(int32_t) noexcept;
uint16_t RegisterClassExW(const void*) noexcept;
__declspec(dllexport) int32_t SetMenu(void*, void*) noexcept;
__declspec(dllexport) int32_t DrawMenuBar(void*) noexcept;
__declspec(dllexport) void* LoadMenuW(void*, const wchar_t*) noexcept;
__declspec(dllexport) int32_t DestroyMenu(void*) noexcept;
__declspec(dllexport) int32_t TrackPopupMenu(void*, uint32_t, int32_t, int32_t,
    int32_t, void*, const void*) noexcept;
__declspec(dllimport) void* __stdcall FindResourceExW(void*, const wchar_t*,
    const wchar_t*, uint16_t) noexcept;
__declspec(dllimport) void* __stdcall LoadResource(void*, void*) noexcept;
__declspec(dllimport) void* __stdcall LockResource(void*) noexcept;
__declspec(dllimport) uint32_t __stdcall SizeofResource(void*, void*) noexcept;
__declspec(dllimport) uint16_t __stdcall GetUserDefaultUILanguage() noexcept;
}

class LockGuard {
public:
    LockGuard() noexcept {
        while (__atomic_exchange_n(&g_menuLock, 1u, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause" ::: "memory");
    }
    ~LockGuard() { __atomic_store_n(&g_menuLock, 0u, __ATOMIC_RELEASE); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};
Menu* findMenu(void* handle) noexcept {
    if (!handle) return nullptr;
    const auto value = reinterpret_cast<uintptr_t>(handle);
    for (auto& menu : g_menus)
        if (menu.active && menu.handle == value) return &menu;
    return nullptr;
}
Menu* createMenu() noexcept {
    if (!g_nextHandle || (g_nextHandle & ~kMenuIndexMask)) return nullptr;
    for (auto& menu : g_menus) {
        if (menu.active) continue;
        menu = {};
        menu.handle = kMenuTag | g_nextHandle++;
        menu.active = true;
        return &menu;
    }
    return nullptr;
}
bool copyText(wchar_t (&destination)[kTextCapacity], const wchar_t* source) noexcept {
    destination[0] = L'\0';
    if (!source) return true;
    size_t count = 0;
    while (count < kTextCapacity && source[count]) ++count;
    if (count == kTextCapacity) return false;
    for (size_t index = 0; index <= count; ++index) destination[index] = source[index];
    return true;
}
bool containsMenu(void* root, void* wanted, size_t depth) noexcept {
    auto* menu = findMenu(root);
    if (!menu || depth >= kMenuCapacity) return false;
    if (root == wanted) return true;
    for (size_t index = 0; index < menu->count; ++index)
        if (menu->items[index].submenu &&
            containsMenu(menu->items[index].submenu, wanted, depth + 1)) return true;
    return false;
}
int findItem(Menu& menu, uint32_t item, bool byPosition) noexcept {
    if (byPosition) return item < menu.count ? static_cast<int>(item) : -1;
    for (size_t index = 0; index < menu.count; ++index)
        if (menu.items[index].id == item) return static_cast<int>(index);
    return -1;
}
bool setItem(Menu&, size_t, uint32_t, uintptr_t, const wchar_t*) noexcept;
bool destroyTree(void*, size_t) noexcept;
struct MenuResourceCursor { const uint8_t* current{}; const uint8_t* end{}; };
bool readMenuWord(MenuResourceCursor& cursor, uint16_t& value) noexcept {
    if (!cursor.current || cursor.current > cursor.end ||
        static_cast<size_t>(cursor.end - cursor.current) < sizeof(value)) return false;
    value = static_cast<uint16_t>(cursor.current[0] |
        (static_cast<uint16_t>(cursor.current[1]) << 8));
    cursor.current += sizeof(value);
    return true;
}
bool readDialogByte(MenuResourceCursor& cursor, uint8_t& value) noexcept {
    if (!cursor.current || cursor.current >= cursor.end) return false;
    value = *cursor.current++;
    return true;
}
bool readDialogDword(MenuResourceCursor& cursor, uint32_t& value) noexcept {
    uint16_t lo{}, hi{};
    if (!readMenuWord(cursor, lo) || !readMenuWord(cursor, hi)) return false;
    value = static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
    return true;
}
bool readDialogField(MenuResourceCursor& cursor, wchar_t (&text)[kTextCapacity],
                     uintptr_t& ordinal, bool& isOrdinal) noexcept {
    uint16_t first{};
    if (!readMenuWord(cursor, first)) return false;
    ordinal = 0; isOrdinal = false; text[0] = L'\0';
    if (!first) return true;
    if (first == 0xFFFFu) {
        uint16_t value{};
        if (!readMenuWord(cursor, value)) return false;
        ordinal = value; isOrdinal = true; return true;
    }
    size_t count = 0;
    for (;;) {
        if (count + 1 >= kTextCapacity) return false;
        text[count++] = static_cast<wchar_t>(first);
        uint16_t value{};
        if (!readMenuWord(cursor, value)) return false;
        if (!value) { text[count] = L'\0'; return true; }
        first = value;
    }
}
void alignDialogDword(MenuResourceCursor& cursor) noexcept {
    const uintptr_t address = reinterpret_cast<uintptr_t>(cursor.current);
    cursor.current = reinterpret_cast<const uint8_t*>((address + 3u) & ~uintptr_t{3u});
}
intptr_t dialogWindowProcedure(void* window, uint32_t message,
    uintptr_t wParam, intptr_t lParam) noexcept {
    const auto procedure = reinterpret_cast<DialogProcedure>(
        GetWindowLongPtrW(window, kDwlpDlgProc));
    if (message == 0x0081u /* WM_NCCREATE */) return 1;
    if (procedure) {
        const intptr_t result = procedure(window, message, wParam, lParam);
        if (result) return result;
    }
    const intptr_t result = DefWindowProcW(window, message, wParam, lParam);
    if (message == 0x0082u /* WM_NCDESTROY */)
        (void)SetWindowLongPtrW(window, kDwlpDlgProc, 0);
    return result;
}
int32_t repaintDialogChild(void* child, intptr_t) noexcept {
    if (child && IsWindowVisible(child)) (void)UpdateWindow(child);
    return 1;
}
bool readMenuText(MenuResourceCursor& cursor, wchar_t (&text)[kTextCapacity]) noexcept {
    size_t count = 0;
    for (;;) {
        uint16_t value{};
        if (!readMenuWord(cursor, value)) return false;
        if (!value) { text[count] = L'\0'; return true; }
        if (count + 1 >= kTextCapacity) return false;
        text[count++] = static_cast<wchar_t>(value);
    }
}
bool parseMenuLevel(Menu& menu, MenuResourceCursor& cursor, size_t depth) noexcept {
    if (depth >= 16) return false;
    for (;;) {
        uint16_t flags{}, id{};
        if (menu.count >= kItemCapacity || !readMenuWord(cursor, flags)) return false;
        const bool popup = (flags & 0x0010u) != 0;
        if (!popup && !readMenuWord(cursor, id)) return false;
        wchar_t text[kTextCapacity]{};
        if (!readMenuText(cursor, text)) return false;
        void* childHandle = nullptr;
        if (popup) {
            auto* child = createMenu();
            if (!child) return false;
            childHandle = reinterpret_cast<void*>(child->handle);
        }
        if (!setItem(menu, menu.count, flags, popup
                ? reinterpret_cast<uintptr_t>(childHandle) : id, text)) {
            if (childHandle) (void)destroyTree(childHandle, 0);
            return false;
        }
        if (popup) {
            auto* child = findMenu(childHandle);
            if (!child || !parseMenuLevel(*child, cursor, depth + 1)) return false;
        }
        if (flags & 0x0080u) return true; // MF_END
    }
}
bool setItem(Menu& menu, size_t index, uint32_t flags, uintptr_t idOrMenu,
             const wchar_t* text) noexcept {
    if (index >= kItemCapacity || (flags & kBitmap) || (flags & kOwnerDraw)) return false;
    void* submenu = nullptr;
    if (flags & kPopup) {
        submenu = reinterpret_cast<void*>(idOrMenu);
        if (!findMenu(submenu) || containsMenu(submenu, reinterpret_cast<void*>(menu.handle), 0))
            return false;
    }
    MenuItem item{};
    item.type = flags & (kSeparator | kBreak | kBarBreak | kRadioCheck);
    item.state = flags & (kChecked | kDisabled | kDefault | kHilite);
    item.id = (flags & kPopup) ? 0 : static_cast<uint32_t>(idOrMenu);
    item.submenu = submenu;
    if (!(flags & kSeparator) && !copyText(item.text, text)) return false;
    menu.items[index] = item;
    if (index == menu.count) ++menu.count;
    return true;
}
bool setInfo(Menu& menu, MenuItem& item, const MenuItemInfoW& info) noexcept {
    if (info.fMask & ~kSupportedMiim) return false;
    if ((info.fMask & kMiimSubMenu) && info.hSubMenu &&
        (!findMenu(info.hSubMenu) || containsMenu(info.hSubMenu,
            reinterpret_cast<void*>(menu.handle), 0))) return false;
    if ((info.fMask & kMiimType) && (info.fType & kBitmap)) return false;
    MenuItem next = item;
    if (info.fMask & kMiimType) next.type = info.fType;
    if (info.fMask & kMiimState) next.state = info.fState;
    if (info.fMask & kMiimId) next.id = info.wID;
    if (info.fMask & kMiimSubMenu) next.submenu = info.hSubMenu;
    if (info.fMask & kMiimCheckmarks) {
        next.checkedBitmap = info.hbmpChecked;
        next.uncheckedBitmap = info.hbmpUnchecked;
    }
    if (info.fMask & kMiimBitmap) {
        if (info.hbmpItem) return false;
        next.itemBitmap = nullptr;
    }
    if (info.fMask & kMiimData) next.data = info.dwItemData;
    if (info.fMask & kMiimString && !copyText(next.text, info.dwTypeData)) return false;
    item = next;
    return true;
}
bool destroyTree(void* handle, size_t depth) noexcept {
    auto* menu = findMenu(handle);
    if (!menu || depth >= kMenuCapacity) return false;
    if (g_popup.active && containsMenu(handle, g_popup.sourceMenu, 0) && g_popup.hwnd)
        finishPopup(false, -1, true);
    for (size_t index = 0; index < menu->count; ++index)
        if (menu->items[index].submenu &&
            !destroyTree(menu->items[index].submenu, depth + 1)) return false;
    for (auto& parent : g_menus) {
        if (!parent.active || &parent == menu) continue;
        for (size_t index = 0; index < parent.count; ++index)
            if (parent.items[index].submenu == handle) parent.items[index].submenu = nullptr;
    }
    *menu = {};
    return true;
}
constexpr uint32_t kWmPaint = 0x000Fu, kWmClose = 0x0010u;
constexpr uint32_t kWmDestroy = 0x0002u, kWmNcDestroy = 0x0082u;
constexpr uint32_t kWmMouseMove = 0x0200u, kWmLButtonDown = 0x0201u;
constexpr uint32_t kWmLButtonUp = 0x0202u, kWmKeyDown = 0x0100u;
constexpr uint32_t kWmKillFocus = 0x0008u, kWmCommand = 0x0111u;
constexpr uint32_t kWmCaptureChanged = 0x0215u;
constexpr uint32_t kWmQuit = 0x0012u, kVkUp = 0x26u, kVkDown = 0x28u;
constexpr uint32_t kVkEnter = 0x0Du, kVkEscape = 0x1Bu;
constexpr uint32_t kWsPopup = 0x80000000u, kWsVisible = 0x10000000u;
constexpr uint32_t kWsChild = 0x40000000u;
constexpr uint32_t kTpmSupportedFlags = 0u;
constexpr int32_t kSwShow = 5;
constexpr uint32_t kDtSingleLine = 0x20u, kDtVCenter = 0x4u;
constexpr uint32_t kColorMenu = 4, kColorMenuText = 7;
constexpr uint32_t kColorHighlight = 13, kColorHighlightText = 14, kColorGrayText = 17;
constexpr int32_t kRowHeight = 24, kBorder = 1;
constexpr wchar_t kPopupClassName[] = L"MicaNT.MenuPopup";
constexpr wchar_t kMenuBarClassName[] = L"MicaNT.MenuBar";
constexpr int32_t kMenuBarHeight = 26;
uint32_t g_popupClassRegistered{};
uint32_t g_menuBarClassRegistered{};
intptr_t MenuPopupWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
intptr_t MenuBarWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
size_t menuDisplayText(const wchar_t*, wchar_t*, size_t, size_t&) noexcept;

uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) noexcept {
    return uint32_t(red) | (uint32_t(green) << 8) | (uint32_t(blue) << 16);
}
int32_t fill(void* dc, const MicaWin32kRect& rect, uint32_t color) noexcept {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return 1;
    auto* brush = CreateSolidBrush(color);
    if (!brush) return 0;
    const int32_t result = FillRect(dc, &rect, brush);
    (void)DeleteObject(brush);
    return result;
}
void invalidatePopup() noexcept {
    if (g_popup.hwnd) (void)InvalidateRect(g_popup.hwnd, nullptr, 1);
}
int32_t selectable(int32_t index) noexcept {
    if (!g_popup.active || index < 0 || index >= static_cast<int32_t>(g_popup.count)) return -1;
    const auto& item = g_popup.items[index];
    if ((item.type & kSeparator) || (item.state & kDisabled)) return -1;
    return index;
}
int32_t nextSelectable(int32_t start, int32_t direction) noexcept {
    if (!g_popup.count) return -1;
    int32_t index = start;
    for (uint32_t step = 0; step < g_popup.count; ++step) {
        index += direction;
        if (index < 0) index = static_cast<int32_t>(g_popup.count) - 1;
        if (index >= static_cast<int32_t>(g_popup.count)) index = 0;
        if (selectable(index) >= 0) return index;
    }
    return -1;
}
void finishPopup(bool chose, int32_t index, bool postClose = false) noexcept {
    if (!g_popup.active) return;
    g_popup.chose = chose;
    g_popup.selected = chose ? index : -1;
    g_popup.done = true;
    if (postClose && g_popup.hwnd) (void)PostMessageW(g_popup.hwnd, kWmClose, 0, 0);
}
bool registerPopupClass() noexcept {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&g_popupClassRegistered, &expected, 1u, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        MicaWin32kWindowClassEx cls{};
        cls.cbSize = sizeof(cls);
        cls.wndProc = reinterpret_cast<void*>(&MenuPopupWndProc);
        cls.className = kPopupClassName;
        const bool registered = RegisterClassExW(&cls) != 0;
        __atomic_store_n(&g_popupClassRegistered, registered ? 2u : 0u, __ATOMIC_RELEASE);
        return registered;
    }
    for (uint32_t spin = 0; spin < 100000; ++spin) {
        const auto state = __atomic_load_n(&g_popupClassRegistered, __ATOMIC_ACQUIRE);
        if (state == 2) return true;
        if (state == 0) return false;
        __asm__ __volatile__("pause" ::: "memory");
    }
    return false;
}
intptr_t MenuPopupWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                          intptr_t lParam) noexcept {
    if (message == kWmPaint) {
        micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct paint{};
        void* dc = BeginPaint(hwnd, &paint);
        if (!dc) return 0;
        const MicaWin32kRect client{0, 0, g_popup.width, g_popup.height};
        uint32_t menuColor = GetThemeSysColor(g_popup.theme, kColorMenu);
        uint32_t textColor = GetThemeSysColor(g_popup.theme, kColorMenuText);
        if (menuColor == 0xFFFFFFFFu) menuColor = rgb(255, 255, 255);
        if (textColor == 0xFFFFFFFFu) textColor = rgb(32, 32, 32);
        bool themed = g_popup.theme &&
            DrawThemeBackground(g_popup.theme, dc, 1, 1, &client, nullptr) == 0;
        if (!themed) {
            menuColor = rgb(255, 255, 255);
            textColor = rgb(32, 32, 32);
            (void)fill(dc, client, menuColor);
        }
        (void)fill(dc, {0, 0, g_popup.width, kBorder}, rgb(176, 176, 176));
        (void)fill(dc, {0, g_popup.height - kBorder, g_popup.width,
                        g_popup.height}, rgb(176, 176, 176));
        (void)fill(dc, {0, 0, kBorder, g_popup.height}, rgb(176, 176, 176));
        (void)fill(dc, {g_popup.width - kBorder, 0, g_popup.width,
                        g_popup.height}, rgb(176, 176, 176));
        for (uint32_t index = 0; index < g_popup.count; ++index) {
            const auto& item = g_popup.items[index];
            const int32_t top = kBorder + static_cast<int32_t>(index) * kRowHeight;
            const MicaWin32kRect row{kBorder, top, g_popup.width - kBorder,
                                      top + kRowHeight};
            if (item.type & kSeparator) {
                (void)fill(dc, {12, top + kRowHeight / 2, g_popup.width - 12,
                                top + kRowHeight / 2 + 1}, rgb(210, 210, 210));
                continue;
            }
            const bool disabled = (item.state & kDisabled) != 0;
            const bool hot = static_cast<int32_t>(index) == g_popup.selected;
            if (hot || disabled) {
                const int32_t state = disabled ? 4 : 2;
                const bool itemThemed = g_popup.theme &&
                    DrawThemeBackground(g_popup.theme, dc, 1, state, &row, nullptr) == 0;
                if (!itemThemed) {
                    const uint32_t background = hot ? rgb(220, 232, 248) : menuColor;
                    (void)fill(dc, row, background);
                }
            }
            const int32_t textState = disabled ? 4 : (hot ? 2 : 1);
            MicaWin32kRect textRect{34, top, g_popup.width - 8, top + kRowHeight};
            wchar_t display[kTextCapacity]{};
            size_t hotkey{};
            const size_t length = menuDisplayText(item.text, display, kTextCapacity, hotkey);
            bool textThemed = g_popup.theme &&
                DrawThemeText(g_popup.theme, dc, 1, textState, display,
                    static_cast<int32_t>(length),
                    kDtSingleLine | kDtVCenter, 0, &textRect) == 0;
            if (!textThemed) {
                const uint32_t color = disabled ? rgb(128, 128, 128) : textColor;
                (void)SetBkMode(dc, 1);
                (void)SetTextColor(dc, color);
                void* clipToken{};
                if (MicaGdiPushClipRect(dc, &textRect, &clipToken) && clipToken) {
                    (void)TextOutW(dc, textRect.left,
                        top + (kRowHeight - 14) / 2, display,
                        static_cast<int32_t>(length));
                    (void)MicaGdiPopClipRect(dc, clipToken);
                }
            }
            if (hotkey < length)
                (void)fill(dc, {textRect.left + 8 + static_cast<int32_t>(hotkey * 8),
                    top + (kRowHeight + 2) / 2, textRect.left + 14 + static_cast<int32_t>(hotkey * 8),
                    top + (kRowHeight + 3) / 2}, disabled ? rgb(128,128,128) : textColor);
            if (item.submenu) {
                (void)SetBkMode(dc, 1);
                (void)SetTextColor(dc, disabled ? rgb(128,128,128) : textColor);
                (void)TextOutW(dc, g_popup.width - 18, top + (kRowHeight - 14) / 2, L"›", 1);
            }
            if (item.state & kChecked) {
                (void)SetBkMode(dc, 1);
                (void)SetTextColor(dc, hot ? rgb(20, 50, 90) : textColor);
                (void)TextOutW(dc, 10, top + (kRowHeight - 14) / 2, L"✓", 1);
            }
        }
        (void)EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == kWmMouseMove) {
        const int32_t x = static_cast<int16_t>(static_cast<uintptr_t>(lParam) & 0xFFFFu);
        const int32_t y = static_cast<int16_t>((static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
        const int32_t row = (y - kBorder) / kRowHeight;
        const int32_t next = x >= 0 && x < g_popup.width && y >= kBorder &&
            y < g_popup.height - kBorder && selectable(row) >= 0 ? row : -1;
        if (next != g_popup.selected) { g_popup.selected = next; invalidatePopup(); }
        return 0;
    }
    if (message == kWmLButtonDown) {
        const int32_t y = static_cast<int16_t>((static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
        const int32_t row = (y - kBorder) / kRowHeight;
        g_popup.pressed = selectable(row);
        return 0;
    }
    if (message == kWmLButtonUp) {
        const int32_t y = static_cast<int16_t>((static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFu);
        const int32_t row = (y - kBorder) / kRowHeight;
        if (g_popup.pressed >= 0 && row == g_popup.pressed)
            finishPopup(true, row);
        else g_popup.pressed = -1;
        return 0;
    }
    if (message == kWmKeyDown) {
        if (wParam == kVkEscape) { finishPopup(false, -1); return 0; }
        if (wParam == 0x27u && selectable(g_popup.selected) >= 0 &&
            g_popup.items[g_popup.selected].submenu) {
            finishPopup(true, g_popup.selected);
            return 0;
        }
        if (wParam == kVkDown || wParam == kVkUp) {
            const int32_t next = nextSelectable(g_popup.selected,
                wParam == kVkDown ? 1 : -1);
            if (next != g_popup.selected) { g_popup.selected = next; invalidatePopup(); }
            return 0;
        }
        if (wParam == kVkEnter) {
            if (selectable(g_popup.selected) >= 0) finishPopup(true, g_popup.selected);
            return 0;
        }
    }
    if (message == kWmKillFocus) { finishPopup(false, -1); return 0; }
    if (message == kWmClose) { (void)DestroyWindow(hwnd); return 0; }
    if (message == kWmDestroy || message == kWmNcDestroy) {
        if (g_popup.hwnd == hwnd) {
            g_popup.hwnd = nullptr;
            if (!g_popup.done) { g_popup.chose = false; g_popup.done = true; }
        }
        return 0;
    }
    if (message == 0x0081u) return 1; // WM_NCCREATE
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
bool registerMenuBarClass() noexcept {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&g_menuBarClassRegistered, &expected, 1u, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        MicaWin32kWindowClassEx cls{};
        cls.cbSize = sizeof(cls);
        cls.wndProc = reinterpret_cast<void*>(&MenuBarWndProc);
        cls.className = kMenuBarClassName;
        const bool registered = RegisterClassExW(&cls) != 0;
        __atomic_store_n(&g_menuBarClassRegistered, registered ? 2u : 0u,
                         __ATOMIC_RELEASE);
        return registered;
    }
    for (uint32_t spin = 0; spin < 100000; ++spin) {
        const auto state = __atomic_load_n(&g_menuBarClassRegistered, __ATOMIC_ACQUIRE);
        if (state == 2) return true;
        if (state == 0) return false;
        __asm__ __volatile__("pause" ::: "memory");
    }
    return false;
}
int32_t menuBarItemWidth(const MenuItem& item) noexcept {
    if (item.type & kSeparator) return 12;
    size_t length = 0;
    while (length < kTextCapacity && item.text[length]) ++length;
    size_t shown = 0;
    for (size_t i = 0; i < length; ++i) {
        if (item.text[i] == L'&') {
            if (i + 1 < length && item.text[i + 1] == L'&') { ++shown; ++i; }
        } else ++shown;
    }
    return static_cast<int32_t>(std::min<size_t>(shown, 120) * 8u + 24u);
}
size_t menuDisplayText(const wchar_t* source, wchar_t* destination,
                       size_t capacity, size_t& accelerator) noexcept {
    accelerator = SIZE_MAX;
    if (!destination || !capacity) return 0;
    size_t out = 0;
    for (size_t in = 0; source && in < kTextCapacity - 1 && source[in] && out + 1 < capacity; ++in) {
        if (source[in] == L'&') {
            if (source[in + 1] == L'&') { destination[out++] = L'&'; ++in; }
            else if (source[in + 1]) accelerator = out;
        } else destination[out++] = source[in];
    }
    destination[out] = L'\0';
    return out;
}
bool menuBarSnapshot(void* hwnd, void*& owner, MenuItem* items,
                     uint32_t& count, int32_t& selected, uint32_t& width) noexcept {
    LockGuard guard;
    for (const auto& bar : g_menuBars) {
        if (!bar.active || bar.hwnd != hwnd) continue;
        owner = bar.owner;
        selected = bar.selected;
        width = bar.width;
        const auto* menu = findMenu(MicaWin32kGetWindowMenu(owner));
        if (!menu) return false;
        count = menu->count;
        for (uint32_t index = 0; index < count; ++index) items[index] = menu->items[index];
        return true;
    }
    return false;
}
int32_t menuBarHitTest(const MenuItem* items, uint32_t count, int32_t x) noexcept {
    int32_t left = 2;
    for (uint32_t index = 0; index < count; ++index) {
        const int32_t right = left + menuBarItemWidth(items[index]);
        if (x >= left && x < right && !(items[index].type & kSeparator)) return index;
        left = right;
    }
    return -1;
}
intptr_t MenuBarWndProc(void* hwnd, uint32_t message, uintptr_t wParam,
                        intptr_t lParam) noexcept {
    if (message == 0x0081u) return 1; // WM_NCCREATE
    void* owner{};
    MenuItem items[kItemCapacity]{};
    uint32_t count{};
    int32_t selected{-1};
    uint32_t width{};
    const bool hasMenu = menuBarSnapshot(hwnd, owner, items, count, selected, width);
    if (message == kWmPaint) {
        micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct paint{};
        void* dc = BeginPaint(hwnd, &paint);
        if (!dc) return 0;
        const MicaWin32kRect bounds{0, 0, static_cast<int32_t>(width), kMenuBarHeight};
        MicaThemeHandle theme = OpenThemeData(hwnd, L"MENU");
        uint32_t base = theme ? GetThemeSysColor(theme, kColorMenu) : rgb(255, 255, 255);
        uint32_t text = theme ? GetThemeSysColor(theme, kColorMenuText) : rgb(32, 32, 32);
        if (base == 0xFFFFFFFFu) base = rgb(255, 255, 255);
        if (text == 0xFFFFFFFFu) text = rgb(32, 32, 32);
        const bool themed = hasMenu && theme &&
            DrawThemeBackground(theme, dc, 1, 1, &bounds, nullptr) == 0;
        if (!themed) (void)fill(dc, bounds, rgb(255, 255, 255));
        (void)fill(dc, {0, kMenuBarHeight - 1, static_cast<int32_t>(width),
                        kMenuBarHeight}, rgb(190, 190, 190));
        int32_t left = 2;
        for (uint32_t index = 0; hasMenu && index < count; ++index) {
            const auto& item = items[index];
            const int32_t itemWidth = menuBarItemWidth(item);
            const MicaWin32kRect itemRect{left, 1, left + itemWidth, kMenuBarHeight - 1};
            if (item.type & kSeparator) {
                const int32_t x = left + itemWidth / 2;
                (void)fill(dc, {x, 5, x + 1, kMenuBarHeight - 5}, rgb(210, 210, 210));
                left += itemWidth;
                continue;
            }
            const bool disabled = (item.state & kDisabled) != 0;
            const bool hot = static_cast<int32_t>(index) == selected;
            if (hot || disabled) {
                const int32_t state = disabled ? 4 : 2;
                const bool itemThemed = theme &&
                    DrawThemeBackground(theme, dc, 1, state, &itemRect, nullptr) == 0;
                if (!itemThemed) (void)fill(dc, itemRect,
                    hot ? rgb(220, 232, 248) : base);
            }
            MicaWin32kRect textRect{left + 10, 1, left + itemWidth - 6,
                                     kMenuBarHeight - 1};
            wchar_t display[kTextCapacity]{};
            size_t accelerator{};
            const size_t length = menuDisplayText(item.text, display, kTextCapacity, accelerator);
            const int32_t textState = disabled ? 4 : (hot ? 2 : 1);
            const bool textThemed = theme && DrawThemeText(theme, dc, 1, textState,
                display, static_cast<int32_t>(length),
                kDtSingleLine | kDtVCenter, 0, &textRect) == 0;
            if (!textThemed) {
                (void)SetBkMode(dc, 1);
                (void)SetTextColor(dc, disabled ? rgb(128, 128, 128) : text);
                void* token{};
                if (MicaGdiPushClipRect(dc, &textRect, &token) && token) {
                    (void)TextOutW(dc, textRect.left, 6, display,
                                   static_cast<int32_t>(length));
                    (void)MicaGdiPopClipRect(dc, token);
                }
            }
            if (accelerator < length)
                (void)fill(dc, {textRect.left + static_cast<int32_t>(accelerator * 8), 20,
                    textRect.left + static_cast<int32_t>(accelerator * 8) + 6, 21},
                    disabled ? rgb(128,128,128) : text);
            left += itemWidth;
        }
        (void)EndPaint(hwnd, &paint);
        if (theme) (void)CloseThemeData(theme);
        return 0;
    }
    if (message == kWmMouseMove || message == kWmLButtonDown || message == kWmLButtonUp) {
        if (!hasMenu) return 0;
        const int32_t x = static_cast<int16_t>(static_cast<uintptr_t>(lParam) & 0xFFFFu);
        const int32_t index = menuBarHitTest(items, count, x);
        if (message == kWmMouseMove) {
            bool changed = false;
            {
                LockGuard guard;
                for (auto& bar : g_menuBars) if (bar.active && bar.hwnd == hwnd) {
                    changed = bar.selected != index;
                    bar.selected = index;
                    break;
                }
            }
            if (changed) (void)InvalidateRect(hwnd, nullptr, 1);
            return 0;
        }
        if (message == kWmLButtonDown) {
            {
                LockGuard guard;
                for (auto& bar : g_menuBars) if (bar.active && bar.hwnd == hwnd) {
                    bar.pressed = index;
                    break;
                }
            }
            (void)SetFocus(hwnd);
            return 0;
        }
        int32_t pressed = -1;
        {
            LockGuard guard;
            for (auto& bar : g_menuBars) if (bar.active && bar.hwnd == hwnd) {
                pressed = bar.pressed;
                bar.pressed = -1;
                break;
            }
        }
        if (pressed < 0 || pressed != index) return 0;
        const auto& item = items[pressed];
        if ((item.state & kDisabled) != 0) return 0;
        if (item.submenu) {
            MicaWin32kRect rect{};
            if (GetWindowRect(hwnd, &rect))
                (void)TrackPopupMenu(item.submenu, 0, rect.left + 2, rect.bottom, 0,
                                     owner, nullptr);
        } else if (item.id) {
            (void)SendMessageW(owner, kWmCommand, item.id, 0);
        }
        return 0;
    }
    if (message == kWmKeyDown && hasMenu) {
        const bool forward = wParam == 0x25u || wParam == 0x27u;
        if (forward && count) {
            int32_t current = selected;
            int32_t next = current;
            for (uint32_t step = 0; step < count; ++step) {
                next += wParam == 0x27u ? 1 : -1;
                if (next < 0) next = static_cast<int32_t>(count) - 1;
                if (next >= static_cast<int32_t>(count)) next = 0;
                if (!(items[next].type & kSeparator) &&
                    !(items[next].state & kDisabled)) break;
            }
            {
                LockGuard guard;
                for (auto& bar : g_menuBars) if (bar.active && bar.hwnd == hwnd) {
                    bar.selected = next;
                    break;
                }
            }
            (void)InvalidateRect(hwnd, nullptr, 1);
            return 0;
        }
        if (wParam == kVkEnter && selected >= 0 && selected < static_cast<int32_t>(count)) {
            const auto& item = items[selected];
            if (item.submenu) {
                MicaWin32kRect rect{};
                if (GetWindowRect(hwnd, &rect))
                    (void)TrackPopupMenu(item.submenu, 0, rect.left + 2, rect.bottom,
                                         0, owner, nullptr);
            } else if (!(item.state & kDisabled) && item.id)
                (void)SendMessageW(owner, kWmCommand, item.id, 0);
            return 0;
        }
    }
    if (message == kWmCaptureChanged) {
        LockGuard guard;
        for (auto& bar : g_menuBars) if (bar.active && bar.hwnd == hwnd) {
            bar.pressed = -1;
            break;
        }
        return 0;
    }
    if (message == kWmDestroy || message == kWmNcDestroy) {
        LockGuard guard;
        for (auto& bar : g_menuBars) if (bar.active && bar.hwnd == hwnd) {
            bar = {};
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
bool drawMenuBar(void* owner) noexcept {
    if (!owner || !registerMenuBarClass()) return false;
    MicaWin32kRect client{};
    if (!GetClientRect(owner, &client) ||
        client.right <= client.left ||
        client.bottom <= client.top) return false;
    // Menu-band windows use the parent's menu allocation at the top of the
    // client origin. Their negative local Y places them above the client area;
    // normal child controls are shifted below the menu by Win32k.
    constexpr int32_t left = 0;
    constexpr int32_t top = -kMenuBarHeight;
    void* menu{};
    void* barWindow{};
    MenuBarState* freeSlot = nullptr;
    bool attached = false;
    {
        LockGuard guard;
        menu = MicaWin32kGetWindowMenu(owner);
        attached = menu != nullptr;
        if (menu && !findMenu(menu)) return false;
        for (auto& bar : g_menuBars) {
            if (bar.active && bar.owner == owner) { barWindow = bar.hwnd; break; }
            if (!bar.active && !freeSlot) freeSlot = &bar;
        }
        if (!attached || !menu) {
            for (auto& bar : g_menuBars)
                if (bar.active && bar.owner == owner) { bar = {}; break; }
        }
        if (!attached || !menu) {
            // The child is removed after releasing the menu lock.
        } else {
            if (!barWindow && !freeSlot) return false;
            if (!barWindow) *freeSlot = {nullptr, owner, left, top,
                static_cast<uint32_t>(client.right - client.left), -1, -1, true};
        }
    }
    if (!attached || !menu) {
        if (barWindow) (void)DestroyWindow(barWindow);
        return true;
    }
    if (!barWindow) {
        barWindow = CreateWindowExW(0, kMenuBarClassName, L"",
            kWsChild | kWsVisible, left, top, client.right - client.left,
            kMenuBarHeight, owner, nullptr, nullptr, nullptr);
        if (!barWindow) {
            LockGuard guard;
            for (auto& bar : g_menuBars)
                if (bar.active && bar.owner == owner && !bar.hwnd) { bar = {}; break; }
            return false;
        }
        LockGuard guard;
        for (auto& bar : g_menuBars) if (bar.active && bar.owner == owner && !bar.hwnd) {
            bar.hwnd = barWindow;
            bar.left = left;
            bar.top = top;
            bar.width = static_cast<uint32_t>(client.right - client.left);
            break;
        }
    } else {
        (void)SetWindowPos(barWindow, nullptr, left, top,
            client.right - client.left, kMenuBarHeight, 0x14u);
        LockGuard guard;
        for (auto& bar : g_menuBars) if (bar.active && bar.owner == owner) {
            bar.width = static_cast<uint32_t>(client.right - client.left);
            bar.left = left;
            bar.top = top;
            break;
        }
    }
    (void)InvalidateRect(barWindow, nullptr, 1);
    return UpdateWindow(barWindow) != 0;
}
bool showPopup(void* handle, uint32_t flags, int32_t x, int32_t y,
               int32_t reserved, void* owner) noexcept {
    if (flags != kTpmSupportedFlags || reserved != 0 || !owner || x < 0 || y < 0 ||
        !registerPopupClass()) return false;
    {
        LockGuard guard;
        auto* menu = findMenu(handle);
        if (!menu || !menu->count || g_popup.active || menu->count > kItemCapacity) return false;
        g_popup = {};
        g_popup.owner = owner;
        g_popup.sourceMenu = handle;
        g_popup.count = menu->count;
        g_popup.x = x;
        g_popup.y = y;
        g_popup.height = int32_t(menu->count) * kRowHeight + 2 * kBorder;
        for (uint32_t index = 0; index < menu->count; ++index) {
        if (menu->items[index].type & (kBitmap | kOwnerDraw))
                return false;
            g_popup.items[index] = menu->items[index];
        }
        g_popup.active = true;
        g_popup.selected = nextSelectable(-1, 1);
    }
    micant::um::UserVideoInfo64 display{};
    if (micant::sys::invokeUserService(0x80,
            reinterpret_cast<uint64_t>(&display)) != 0) {
        LockGuard guard; g_popup = {};
        return false;
    }
    const int32_t screenWidth = static_cast<int32_t>(display.width);
    const int32_t screenHeight = static_cast<int32_t>(display.height);
    if (screenWidth <= 0 || screenHeight <= 0) {
        LockGuard guard; g_popup = {};
        return false;
    }
    if (g_popup.x + g_popup.width > screenWidth)
        g_popup.x = std::max(0, screenWidth - g_popup.width);
    if (g_popup.y + g_popup.height > screenHeight)
        g_popup.y = std::max(0, screenHeight - g_popup.height);
    g_popup.theme = OpenThemeData(owner, L"MENU");
    auto* window = CreateWindowExW(0, kPopupClassName, L"", kWsPopup,
        g_popup.x, g_popup.y, g_popup.width, g_popup.height, nullptr, nullptr,
        nullptr, nullptr);
    if (!window) {
        if (g_popup.theme) (void)CloseThemeData(g_popup.theme);
        LockGuard guard; g_popup = {};
        return false;
    }
    g_popup.hwnd = window;
    (void)ShowWindow(window, kSwShow);
    (void)SetFocus(window);
    (void)UpdateWindow(window);
    bool modalCompleted = true;
    while (!g_popup.done) {
        if (!IsWindowVisible(owner)) { finishPopup(false, -1); break; }
        micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg message{};
        const int32_t result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (message.message == kWmQuit)
                PostQuitMessage(static_cast<int32_t>(message.wParam));
            finishPopup(false, -1);
            modalCompleted = false;
            break;
        }
        const bool outsideClick = message.message == kWmLButtonDown && message.hwnd != window;
        (void)DispatchMessageW(&message);
        if (outsideClick) finishPopup(false, -1);
    }
    const bool chose = g_popup.chose;
    const int32_t selected = g_popup.selected;
    void* selectedSubmenu = chose && selected >= 0 &&
        static_cast<uint32_t>(selected) < g_popup.count
        ? g_popup.items[selected].submenu : nullptr;
    const uint32_t command = chose && selected >= 0
        ? g_popup.items[selected].id : 0u;
    const int32_t submenuX = g_popup.x + g_popup.width - 2;
    const int32_t submenuY = g_popup.y + kBorder + std::max(0, selected) * kRowHeight;
    if (g_popup.hwnd) (void)DestroyWindow(g_popup.hwnd);
    if (g_popup.theme) (void)CloseThemeData(g_popup.theme);
    g_popup = {};
    if (selectedSubmenu)
        return showPopup(selectedSubmenu, flags, submenuX, submenuY, 0, owner);
    if (!chose || !command) return false;
    (void)SendMessageW(owner, kWmCommand, command, 0);
    return modalCompleted;
}
}

extern "C" void MicaUser32MenuWindowDestroyed(void* hwnd) noexcept {
    if (!hwnd) return;
    void* barWindow = nullptr;
    {
        LockGuard guard;
        for (auto& bar : g_menuBars) {
            if (bar.active && bar.owner == hwnd) {
                barWindow = bar.hwnd;
                bar = {};
                break;
            }
        }
        if (g_popup.active && g_popup.owner == hwnd) finishPopup(false, -1, true);
        if (g_popup.active && g_popup.hwnd == hwnd) {
            g_popup.hwnd = nullptr;
            if (!g_popup.done) { g_popup.chose = false; g_popup.done = true; }
        }
    }
    if (barWindow) (void)DestroyWindow(barWindow);
}

extern "C" {
__declspec(dllexport) void* CreateMenu() noexcept {
    LockGuard guard;
    auto* menu = createMenu();
    return menu ? reinterpret_cast<void*>(menu->handle) : nullptr;
}
__declspec(dllexport) void* CreatePopupMenu() noexcept { return CreateMenu(); }
__declspec(dllexport) void* __stdcall CreateDialogParamW(void* instance,
    const wchar_t* templateName, void* parent, void* dialogProc,
    intptr_t initParam) noexcept {
    if (!instance || !templateName || !dialogProc) {
        uint32_t error = 87;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
        return nullptr;
    }
    const auto resourceType = reinterpret_cast<const wchar_t*>(static_cast<uintptr_t>(5));
    void* resource = FindResourceExW(instance, resourceType, templateName,
                                     GetUserDefaultUILanguage());
    void* loaded = resource ? LoadResource(instance, resource) : nullptr;
    const auto* data = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    const uint32_t size = loaded ? SizeofResource(instance, resource) : 0;
    if (!data || size < 24) return nullptr;
    MenuResourceCursor cursor{data, data + size};
    uint16_t version{}, signature{}, itemCount{}, xRaw{}, yRaw{}, widthRaw{}, heightRaw{};
    uint32_t helpId{}, exStyle{}, style{};
    if (!readMenuWord(cursor, version) || !readMenuWord(cursor, signature) ||
        version != 1 || signature != 0xFFFFu || !readDialogDword(cursor, helpId) ||
        !readDialogDword(cursor, exStyle) || !readDialogDword(cursor, style) ||
        !readMenuWord(cursor, itemCount) || !readMenuWord(cursor, xRaw) ||
        !readMenuWord(cursor, yRaw) || !readMenuWord(cursor, widthRaw) ||
        !readMenuWord(cursor, heightRaw) || itemCount > 256) {
        uint32_t error = 13;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
        return nullptr;
    }
    wchar_t menuName[kTextCapacity]{}, className[kTextCapacity]{}, caption[kTextCapacity]{};
    uintptr_t menuOrdinal{}, classOrdinal{};
    bool hasMenuOrdinal{}, hasClassOrdinal{};
    if (!readDialogField(cursor, menuName, menuOrdinal, hasMenuOrdinal) ||
        !readDialogField(cursor, className, classOrdinal, hasClassOrdinal) ||
        !readMenuText(cursor, caption)) return nullptr;
    uint16_t pointSize{};
    if (style & 0x00000040u /* DS_SETFONT */) {
        uint16_t weight{}; uint8_t italic{}, charset{};
        wchar_t face[kTextCapacity]{}; uintptr_t ignoredOrdinal{}; bool ignoredIsOrdinal{};
        if (!readMenuWord(cursor, pointSize) || !readMenuWord(cursor, weight) ||
            !readDialogByte(cursor, italic) || !readDialogByte(cursor, charset) ||
            !readDialogField(cursor, face, ignoredOrdinal, ignoredIsOrdinal)) return nullptr;
        (void)weight; (void)italic; (void)charset;
    }
    alignDialogDword(cursor);
    static constexpr wchar_t kDialogClass[] = L"#32770";
    MicaWin32kWindowClassEx dialogClass{};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.wndProc = reinterpret_cast<void*>(dialogWindowProcedure);
    dialogClass.windowExtra = kDialogWindowExtra;
    dialogClass.className = kDialogClass;
    (void)RegisterClassExW(&dialogClass);
    DialogProcedure callback = reinterpret_cast<DialogProcedure>(dialogProc);
    const int32_t x = static_cast<int16_t>(xRaw) == static_cast<int16_t>(0x8000u)
        ? 48 : (static_cast<int32_t>(static_cast<int16_t>(xRaw)) * 7) / 4;
    const int32_t y = static_cast<int16_t>(yRaw) == static_cast<int16_t>(0x8000u)
        ? 48 : (static_cast<int32_t>(static_cast<int16_t>(yRaw)) * 15) / 8;
    int32_t width = (static_cast<int32_t>(widthRaw) * 7) / 4;
    int32_t height = (static_cast<int32_t>(heightRaw) * 15) / 8;
    micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect outerRect{
        0, 0, width, height};
    if (!AdjustWindowRectEx(&outerRect, style,
            (hasMenuOrdinal || menuName[0]) ? 1 : 0, exStyle) ||
        outerRect.right <= outerRect.left || outerRect.bottom <= outerRect.top) {
        uint32_t error = 87;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
        return nullptr;
    }
    width = outerRect.right - outerRect.left;
    height = outerRect.bottom - outerRect.top;
    void* window = CreateWindowExW(exStyle, kDialogClass, caption, style,
        x, y, width, height, parent, nullptr, instance, nullptr);
    if (!window) return nullptr;
    (void)SetWindowLongPtrW(window, kDwlpDlgProc,
        reinterpret_cast<intptr_t>(callback));
    void* firstFocus = nullptr;
    for (uint32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
        alignDialogDword(cursor);
        uint32_t itemHelp{}, itemExStyle{}, itemStyle{}, controlId{};
        uint16_t itemX{}, itemY{}, itemWidth{}, itemHeight{};
        if (!readDialogDword(cursor, itemHelp) || !readDialogDword(cursor, itemExStyle) ||
            !readDialogDword(cursor, itemStyle) || !readMenuWord(cursor, itemX) ||
            !readMenuWord(cursor, itemY) || !readMenuWord(cursor, itemWidth) ||
            !readMenuWord(cursor, itemHeight) || !readDialogDword(cursor, controlId)) {
            (void)DestroyWindow(window); return nullptr;
        }
        wchar_t controlClass[kTextCapacity]{}, text[kTextCapacity]{};
        uintptr_t classId{}, textId{}; bool classIsOrdinal{}, textIsOrdinal{};
        if (!readDialogField(cursor, controlClass, classId, classIsOrdinal) ||
            !readDialogField(cursor, text, textId, textIsOrdinal)) {
            (void)DestroyWindow(window); return nullptr;
        }
        uint16_t extraBytes{};
        if (!readMenuWord(cursor, extraBytes) || cursor.current > cursor.end ||
            static_cast<size_t>(cursor.end - cursor.current) < extraBytes) {
            (void)DestroyWindow(window); return nullptr;
        }
        cursor.current += extraBytes;
        const wchar_t* classPointer = controlClass;
        if (classIsOrdinal) {
            switch (classId) {
                case 0x80: classPointer = L"Button"; break;
                case 0x81: classPointer = L"Edit"; break;
                case 0x82: classPointer = L"Static"; break;
                case 0x83: classPointer = L"ListBox"; break;
                case 0x84: classPointer = L"ScrollBar"; break;
                case 0x85: classPointer = L"ComboBox"; break;
                default: (void)DestroyWindow(window); return nullptr;
            }
        }
        const wchar_t* controlText = textIsOrdinal
            ? reinterpret_cast<const wchar_t*>(textId) : text;
        void* control = CreateWindowExW(itemExStyle, classPointer, controlText,
            itemStyle, (static_cast<int32_t>(static_cast<int16_t>(itemX)) * 7) / 4,
            (static_cast<int32_t>(static_cast<int16_t>(itemY)) * 15) / 8,
            (static_cast<int32_t>(itemWidth) * 7) / 4,
            (static_cast<int32_t>(itemHeight) * 15) / 8,
            window, reinterpret_cast<void*>(static_cast<uintptr_t>(controlId)),
            instance, nullptr);
        if (!control) { (void)DestroyWindow(window); return nullptr; }
        if (!firstFocus && (itemStyle & 0x00010000u) /* WS_TABSTOP */ &&
            !(itemStyle & 0x08000000u /* WS_DISABLED */)) firstFocus = control;
        (void)itemHelp;
    }
    void* menu = nullptr;
    if (hasMenuOrdinal) {
        menu = LoadMenuW(instance, reinterpret_cast<const wchar_t*>(menuOrdinal));
    } else if (menuName[0]) {
        menu = LoadMenuW(instance, menuName);
    }
    if ((hasMenuOrdinal || menuName[0]) && (!menu || !SetMenu(window, menu))) {
        (void)DestroyWindow(window);
        if (menu) (void)DestroyMenu(menu);
        return nullptr;
    }
    if (menu) (void)DrawMenuBar(window);
    (void)callback(window, 0x0110u /* WM_INITDIALOG */,
        reinterpret_cast<uintptr_t>(firstFocus), initParam);
    if (style & 0x10000000u /* WS_VISIBLE */) {
        (void)ShowWindow(window, 5);
        (void)UpdateWindow(window);
        // WM_INITDIALOG may set static/button text after each child has already
        // received its creation paint. Redraw the visible children once the
        // dialog callback has completed so its final control state is shown.
        (void)EnumChildWindows(window,
            reinterpret_cast<void*>(&repaintDialogChild), 0);
    }
    (void)helpId; (void)classOrdinal; (void)hasClassOrdinal; (void)pointSize;
    return window;
}
__declspec(dllexport) void* LoadMenuW(void* instance,
    const wchar_t* name) noexcept {
    if (!instance || !name) {
        uint32_t error = 87;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
        return nullptr;
    }
    const auto type = reinterpret_cast<const wchar_t*>(static_cast<uintptr_t>(4));
    void* resource = FindResourceExW(instance, type, name, GetUserDefaultUILanguage());
    void* loaded = resource ? LoadResource(instance, resource) : nullptr;
    const auto* data = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
    const uint32_t size = loaded ? SizeofResource(instance, resource) : 0;
    if (!data || size < 4) return nullptr;
    MenuResourceCursor cursor{data, data + size};
    uint16_t version{}, offset{};
    if (!readMenuWord(cursor, version) || !readMenuWord(cursor, offset) ||
        version != 0 || offset > static_cast<size_t>(cursor.end - cursor.current)) {
        uint32_t error = version == 1 ? 13u : 1813u;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
        return nullptr;
    }
    cursor.current += offset;
    LockGuard guard;
    auto* menu = createMenu();
    if (!menu || !parseMenuLevel(*menu, cursor, 0)) {
        if (menu) (void)destroyTree(reinterpret_cast<void*>(menu->handle), 0);
        uint32_t error = 13;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
        return nullptr;
    }
    return reinterpret_cast<void*>(menu->handle);
}
__declspec(dllexport) int32_t DestroyMenu(void* handle) noexcept {
    LockGuard guard;
    return destroyTree(handle, 0);
}
__declspec(dllexport) int32_t AppendMenuW(void* handle, uint32_t flags, uintptr_t item,
                                      const wchar_t* text) noexcept {
    if (flags & (kByPosition | kOwnerDraw)) return false;
    LockGuard guard;
    auto* menu = findMenu(handle);
    return menu && menu->count < kItemCapacity &&
        setItem(*menu, menu->count, flags, item, text);
}
__declspec(dllexport) int32_t AppendMenuA(void* handle, uint32_t flags, uintptr_t item,
                                      const char* text) noexcept {
    if (flags & (kByPosition | kOwnerDraw | kBitmap)) return false;
    if (!text) return AppendMenuW(handle, flags, item, nullptr);
    wchar_t wide[kTextCapacity]{};
    size_t index = 0;
    for (; index < kTextCapacity && text[index]; ++index) {
        const auto ch = static_cast<unsigned char>(text[index]);
        if (ch >= 0x80) return false;
        wide[index] = static_cast<wchar_t>(ch);
    }
    return index < kTextCapacity && AppendMenuW(handle, flags, item, wide);
}
__declspec(dllexport) int32_t InsertMenuW(void* handle, uint32_t position, uint32_t flags,
                                      uintptr_t item, const wchar_t* text) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu || menu->count == kItemCapacity || (flags & (kBitmap | kOwnerDraw))) return false;
    size_t insertion = position;
    if (flags & kByPosition) {
        if (insertion > menu->count) return false;
    } else {
        const int existing = findItem(*menu, position, false);
        if (existing < 0) return false;
        insertion = static_cast<size_t>(existing);
    }
    for (size_t index = menu->count; index > insertion; --index)
        menu->items[index] = menu->items[index - 1];
    if (!setItem(*menu, insertion, flags & ~kByPosition, item, text)) {
        for (size_t index = insertion; index < menu->count; ++index)
            menu->items[index] = menu->items[index + 1];
        menu->items[menu->count] = {};
        return false;
    }
    ++menu->count;
    return true;
}
__declspec(dllexport) int32_t ModifyMenuW(void* handle, uint32_t position, uint32_t flags,
                                      uintptr_t item, const wchar_t* text) noexcept {
    if (flags & kOwnerDraw) return false;
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu) return false;
    const int index = findItem(*menu, position, (flags & kByPosition) != 0);
    return index >= 0 && setItem(*menu, static_cast<size_t>(index),
        flags & ~kByPosition, item, text);
}
__declspec(dllexport) int32_t RemoveMenu(void* handle, uint32_t item, uint32_t flags) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu) return false;
    const int index = findItem(*menu, item, (flags & kByPosition) != 0);
    if (index < 0) return false;
    for (size_t move = static_cast<size_t>(index); move + 1 < menu->count; ++move)
        menu->items[move] = menu->items[move + 1];
    menu->items[--menu->count] = {};
    return true;
}
__declspec(dllexport) int32_t DeleteMenu(void* handle, uint32_t item, uint32_t flags) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu) return false;
    const int index = findItem(*menu, item, (flags & kByPosition) != 0);
    if (index < 0) return false;
    void* submenu = menu->items[index].submenu;
    for (size_t move = static_cast<size_t>(index); move + 1 < menu->count; ++move)
        menu->items[move] = menu->items[move + 1];
    menu->items[--menu->count] = {};
    return !submenu || destroyTree(submenu, 0);
}
__declspec(dllexport) int GetMenuItemCount(void* handle) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    return menu ? menu->count : -1;
}
__declspec(dllexport) uint32_t GetMenuItemID(void* handle, int position) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu || position < 0 || static_cast<size_t>(position) >= menu->count ||
        menu->items[position].submenu) return UINT32_MAX;
    return menu->items[position].id;
}
__declspec(dllexport) uint32_t EnableMenuItem(void* handle, uint32_t item,
                                             uint32_t enable) noexcept {
    constexpr uint32_t byPosition = 0x0400u;
    constexpr uint32_t disabled = 0x0003u;
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu) return UINT32_MAX;
    const int index = findItem(*menu, item, (enable & byPosition) != 0);
    if (index < 0) return UINT32_MAX;
    auto& state = menu->items[index].state;
    const uint32_t previous = state & disabled;
    state = (state & ~disabled) | (enable & disabled);
    for (const auto& bar : g_menuBars)
        if (bar.active && MicaWin32kGetWindowMenu(bar.owner) == handle)
            (void)InvalidateRect(bar.hwnd, nullptr, 1);
    return previous;
}
__declspec(dllexport) uint32_t CheckMenuItem(void* handle, uint32_t item,
                                            uint32_t check) noexcept {
    constexpr uint32_t byPosition = 0x0400u;
    constexpr uint32_t checked = 0x0008u;
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu) return UINT32_MAX;
    const int index = findItem(*menu, item, (check & byPosition) != 0);
    if (index < 0) return UINT32_MAX;
    auto& state = menu->items[index].state;
    const uint32_t previous = state & checked;
    state = (state & ~checked) | (check & checked);
    return previous;
}
__declspec(dllexport) void* GetSubMenu(void* handle, int position) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu || position < 0 || static_cast<size_t>(position) >= menu->count) return nullptr;
    return menu->items[position].submenu;
}
__declspec(dllexport) int32_t OpenClipboard(void* owner) noexcept {
    (void)owner;
    uint32_t threadId{};
    __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(threadId));
    LockGuard guard;
    if (!threadId || g_clipboard.open) return 0;
    g_clipboard.open = true;
    g_clipboard.ownerThread = threadId;
    return 1;
}
__declspec(dllexport) int32_t CloseClipboard() noexcept {
    uint32_t threadId{};
    __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(threadId));
    LockGuard guard;
    if (!g_clipboard.open || g_clipboard.ownerThread != threadId) return 0;
    g_clipboard.open = false;
    g_clipboard.ownerThread = 0;
    return 1;
}
__declspec(dllexport) int32_t EmptyClipboard() noexcept {
    uint32_t threadId{};
    __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(threadId));
    LockGuard guard;
    if (!g_clipboard.open || g_clipboard.ownerThread != threadId) return 0;
    g_clipboard.ansi = nullptr;
    g_clipboard.unicode = nullptr;
    g_clipboard.ownerWindow = 0;
    return 1;
}
__declspec(dllexport) void* SetClipboardData(uint32_t format, void* memory) noexcept {
    uint32_t threadId{};
    __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(threadId));
    LockGuard guard;
    if (!g_clipboard.open || g_clipboard.ownerThread != threadId || !memory) return nullptr;
    if (format == 1u) g_clipboard.ansi = memory;
    else if (format == 13u) g_clipboard.unicode = memory;
    else return nullptr;
    return memory;
}
__declspec(dllexport) void* GetClipboardData(uint32_t format) noexcept {
    uint32_t threadId{};
    __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(threadId));
    LockGuard guard;
    if (!g_clipboard.open || g_clipboard.ownerThread != threadId) return nullptr;
    if (format == 1u) return g_clipboard.ansi;
    if (format == 13u) return g_clipboard.unicode;
    return nullptr;
}
__declspec(dllexport) int32_t IsClipboardFormatAvailable(uint32_t format) noexcept {
    LockGuard guard;
    if (format == 1u) return g_clipboard.ansi != nullptr;
    if (format == 13u) return g_clipboard.unicode != nullptr;
    return 0;
}
__declspec(dllexport) int GetMenuStringW(void* handle, uint32_t item, wchar_t* output,
                                        int capacity, uint32_t flags) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu || capacity < 0 || (capacity && !output)) return 0;
    const int index = findItem(*menu, item, (flags & kByPosition) != 0);
    if (index < 0) return 0;
    const auto& text = menu->items[index].text;
    size_t length = 0;
    while (length < kTextCapacity && text[length]) ++length;
    if (capacity) {
        const size_t copied = length < static_cast<size_t>(capacity - 1)
            ? length : static_cast<size_t>(capacity - 1);
        for (size_t at = 0; at < copied; ++at) output[at] = text[at];
        output[copied] = L'\0';
        return static_cast<int>(copied);
    }
    return static_cast<int>(length);
}
__declspec(dllexport) int32_t SetMenuItemInfoW(void* handle, uint32_t item, int32_t byPosition,
                                           const MenuItemInfoW* info) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu || !info || info->cbSize < sizeof(MenuItemInfoW)) return false;
    const int index = findItem(*menu, item, byPosition);
    return index >= 0 && setInfo(*menu, menu->items[index], *info);
}
__declspec(dllexport) int32_t GetMenuItemInfoW(void* handle, uint32_t item, int32_t byPosition,
                                           MenuItemInfoW* info) noexcept {
    LockGuard guard;
    auto* menu = findMenu(handle);
    if (!menu || !info || info->cbSize < sizeof(MenuItemInfoW) ||
        (info->fMask & ~kSupportedMiim)) return false;
    const int index = findItem(*menu, item, byPosition != 0);
    if (index < 0) return false;
    const auto& saved = menu->items[index];
    if (info->fMask & kMiimType) info->fType = saved.type;
    if (info->fMask & kMiimState) info->fState = saved.state;
    if (info->fMask & kMiimId) info->wID = saved.id;
    if (info->fMask & kMiimSubMenu) info->hSubMenu = saved.submenu;
    if (info->fMask & kMiimCheckmarks) {
        info->hbmpChecked = saved.checkedBitmap;
        info->hbmpUnchecked = saved.uncheckedBitmap;
    }
    if (info->fMask & kMiimBitmap) info->hbmpItem = saved.itemBitmap;
    if (info->fMask & kMiimData) info->dwItemData = saved.data;
    if (info->fMask & kMiimString) {
        size_t length = 0;
        while (length < kTextCapacity && saved.text[length]) ++length;
        if (info->dwTypeData && info->cch) {
            const size_t copied = length < info->cch - 1 ? length : info->cch - 1;
            for (size_t at = 0; at < copied; ++at) info->dwTypeData[at] = saved.text[at];
            info->dwTypeData[copied] = L'\0';
        }
        info->cch = static_cast<uint32_t>(length);
    }
    return true;
}
__declspec(dllexport) void* GetMenu(void* hwnd) noexcept {
    if (!hwnd) return nullptr;
    return MicaWin32kGetWindowMenu(hwnd);
}
__declspec(dllexport) int32_t SetMenu(void* hwnd, void* handle) noexcept {
    if (!hwnd) return false;
    {
        LockGuard guard;
        if (handle && !findMenu(handle)) return false;
    }
    if (!MicaWin32kSetWindowMenu(hwnd, handle)) return false;
    void* barWindow = nullptr;
    {
        LockGuard guard;
        for (const auto& bar : g_menuBars)
            if (bar.active && bar.owner == hwnd) { barWindow = bar.hwnd; break; }
    }
    if (barWindow) (void)InvalidateRect(barWindow, nullptr, 1);
    return true;
}
__declspec(dllexport) int32_t DrawMenuBar(void* hwnd) noexcept {
    return drawMenuBar(hwnd) ? 1 : 0;
}
__declspec(dllexport) int32_t TrackPopupMenu(void* menu, uint32_t flags,
    int32_t x, int32_t y, int32_t reserved, void* owner, const void*) noexcept {
    return showPopup(menu, flags, x, y, reserved, owner);
}
__declspec(dllexport) int32_t TrackPopupMenuEx(void* menu, uint32_t flags,
    int32_t x, int32_t y, void* owner, void* info) noexcept {
    if (info) return 0;
    return showPopup(menu, flags, x, y, 0, owner);
}
}
#endif
