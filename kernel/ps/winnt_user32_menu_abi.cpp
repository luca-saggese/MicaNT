#include <cstddef>
#include <cstdint>
#include "micant/shared/native_gdi_api.h"
#include "micant/shared/native_uxtheme_api.h"
#include "micant/shared/win32k_client.h"
#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"

#if defined(BUILD_SYSTEM32_USER32_DLL)
namespace {
constexpr size_t kMenuCapacity = 32;
constexpr size_t kItemCapacity = 32;
constexpr size_t kTextCapacity = 128;
constexpr size_t kAttachmentCapacity = 64;
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
struct Attachment { void* hwnd{}; void* menu{}; };
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
Menu g_menus[kMenuCapacity]{};
Attachment g_attachments[kAttachmentCapacity]{};
PopupSession g_popup{};
uintptr_t g_nextHandle{1};
uint32_t g_menuLock{};
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
void PostQuitMessage(int32_t) noexcept;
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
    for (auto& attachment : g_attachments)
        if (attachment.menu == handle) attachment.menu = nullptr;
    *menu = {};
    return true;
}
constexpr uint32_t kWmPaint = 0x000Fu, kWmClose = 0x0010u;
constexpr uint32_t kWmDestroy = 0x0002u, kWmNcDestroy = 0x0082u;
constexpr uint32_t kWmMouseMove = 0x0200u, kWmLButtonDown = 0x0201u;
constexpr uint32_t kWmLButtonUp = 0x0202u, kWmKeyDown = 0x0100u;
constexpr uint32_t kWmKillFocus = 0x0008u, kWmCommand = 0x0111u;
constexpr uint32_t kWmQuit = 0x0012u, kVkUp = 0x26u, kVkDown = 0x28u;
constexpr uint32_t kVkEnter = 0x0Du, kVkEscape = 0x1Bu;
constexpr uint32_t kWsPopup = 0x80000000u, kWsVisible = 0x10000000u;
constexpr uint32_t kTpmSupportedFlags = 0u;
constexpr int32_t kSwShow = 5;
constexpr uint32_t kDtSingleLine = 0x20u, kDtVCenter = 0x4u;
constexpr uint32_t kColorMenu = 4, kColorMenuText = 7;
constexpr uint32_t kColorHighlight = 13, kColorHighlightText = 14, kColorGrayText = 17;
constexpr int32_t kRowHeight = 24, kBorder = 1;
constexpr wchar_t kPopupClassName[] = L"MicaNT.MenuPopup";
uint32_t g_popupClassRegistered{};
intptr_t MenuPopupWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;

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
    if (item.submenu || (item.type & kSeparator) || (item.state & kDisabled)) return -1;
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
            const bool disabled = (item.state & kDisabled) != 0 || item.submenu;
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
            bool textThemed = g_popup.theme &&
                DrawThemeText(g_popup.theme, dc, 1, textState, item.text, -1,
                    kDtSingleLine | kDtVCenter, 0, &textRect) == 0;
            if (!textThemed) {
                const uint32_t color = disabled ? rgb(128, 128, 128) : textColor;
                (void)SetBkMode(dc, 1);
                (void)SetTextColor(dc, color);
                size_t length = 0;
                while (length < kTextCapacity && item.text[length]) ++length;
                void* clipToken{};
                if (MicaGdiPushClipRect(dc, &textRect, &clipToken) && clipToken) {
                    (void)TextOutW(dc, textRect.left,
                        top + (kRowHeight - 14) / 2, item.text,
                        static_cast<int32_t>(length));
                    (void)MicaGdiPopClipRect(dc, clipToken);
                }
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
            if (menu->items[index].submenu || (menu->items[index].type & (kBitmap | kOwnerDraw)))
                return false;
            g_popup.items[index] = menu->items[index];
        }
        g_popup.active = true;
        g_popup.selected = nextSelectable(-1, 1);
    }
    if (g_popup.x + g_popup.width > MICANT_COMPOSITOR_SCREEN_WIDTH)
        g_popup.x = MICANT_COMPOSITOR_SCREEN_WIDTH - g_popup.width;
    if (g_popup.y + g_popup.height > MICANT_COMPOSITOR_SCREEN_HEIGHT)
        g_popup.y = MICANT_COMPOSITOR_SCREEN_HEIGHT - g_popup.height;
    g_popup.theme = OpenThemeData(owner, L"MENU");
    auto* window = CreateWindowExW(0, kPopupClassName, L"", kWsPopup,
        g_popup.x, g_popup.y, g_popup.width, g_popup.height, owner, nullptr, nullptr, nullptr);
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
    const uint32_t command = chose && selected >= 0
        ? g_popup.items[selected].id : 0u;
    if (g_popup.hwnd) (void)DestroyWindow(g_popup.hwnd);
    if (g_popup.theme) (void)CloseThemeData(g_popup.theme);
    g_popup = {};
    if (!chose || !command) return false;
    (void)SendMessageW(owner, kWmCommand, command, 0);
    return modalCompleted;
}
}

extern "C" void MicaUser32MenuWindowDestroyed(void* hwnd) noexcept {
    if (!hwnd) return;
    LockGuard guard;
    for (auto& attachment : g_attachments)
        if (attachment.hwnd == hwnd) attachment = {};
    if (!g_popup.active) return;
    if (g_popup.owner == hwnd) finishPopup(false, -1, true);
    if (g_popup.hwnd == hwnd) {
        g_popup.hwnd = nullptr;
        if (!g_popup.done) { g_popup.chose = false; g_popup.done = true; }
    }
}

extern "C" {
__declspec(dllexport) void* CreateMenu() noexcept {
    LockGuard guard;
    auto* menu = createMenu();
    return menu ? reinterpret_cast<void*>(menu->handle) : nullptr;
}
__declspec(dllexport) void* CreatePopupMenu() noexcept { return CreateMenu(); }
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
    LockGuard guard;
    for (const auto& item : g_attachments) if (item.hwnd == hwnd) return item.menu;
    return nullptr;
}
__declspec(dllexport) int32_t SetMenu(void* hwnd, void* handle) noexcept {
    if (!hwnd) return false;
    LockGuard guard;
    if (handle && !findMenu(handle)) return false;
    Attachment* freeSlot = nullptr;
    for (auto& item : g_attachments) {
        if (item.hwnd == hwnd) {
            item.menu = handle;
            if (!handle) item = {};
            return true;
        }
        if (!item.hwnd && !freeSlot) freeSlot = &item;
    }
    if (!handle) return true;
    if (!freeSlot) return false;
    *freeSlot = {hwnd, handle};
    return true;
}
__declspec(dllexport) int32_t DrawMenuBar(void*) noexcept { return 0; }
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
