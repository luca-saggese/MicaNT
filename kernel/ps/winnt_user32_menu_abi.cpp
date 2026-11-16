#include <cstddef>
#include <cstdint>

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
Menu g_menus[kMenuCapacity]{};
Attachment g_attachments[kAttachmentCapacity]{};
uintptr_t g_nextHandle{1};
uint32_t g_menuLock{};

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
__declspec(dllexport) int32_t TrackPopupMenu(void*, uint32_t, int32_t, int32_t,
                                          int32_t, void*, const void*) noexcept { return false; }
__declspec(dllexport) int32_t TrackPopupMenuEx(void*, uint32_t, int32_t, int32_t,
                                            void*, void*) noexcept { return false; }
}
#endif
