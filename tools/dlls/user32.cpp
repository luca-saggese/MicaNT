/**
 * @file user32.cpp
 * @brief USER32 DLL implementation, split from the former combined
 *        User32/GDI32/IMM32 translation unit.
 */

#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"
#include "micant/um/subsystems/imaging/ico_decoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/subsystems/winuser/icon_handle_table.hpp"
#include "micant/um/subsystems/winuser/cursor_image.hpp"
#include "micant/um/subsystems/winuser/ani_image.hpp"
#include "micant/um/native/pe.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/native_gdi_api.h"
#include "micant/shared/native_uxtheme_api.h"
#include "micant/shared/win32k_client.h"

#include <algorithm>
#include <array>

extern "C" __declspec(dllimport) int32_t __stdcall Beep(
    uint32_t frequency, uint32_t duration) noexcept;

#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

namespace micant::um::winnt_user32_gdi32_imm32_abi {

extern "C" __declspec(dllimport) int32_t GetDeviceCaps(
    void*, int32_t) noexcept;

int32_t User32_IsWindowEnabled(void*) noexcept;
extern "C" __declspec(dllimport) void MicaGdi32ForgetDcState(void*) noexcept;
extern "C" void MicaUser32MenuWindowDestroyed(void*) noexcept;
extern "C" void* CreateFontW(int32_t, int32_t, int32_t, int32_t,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, const wchar_t*) noexcept;
intptr_t User32_ButtonWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
intptr_t User32_StaticWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
intptr_t User32_EditWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
intptr_t User32_ScrollBarWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
void* User32_BeginPaint(void*, WinNtPaintStruct*) noexcept;
int32_t User32_EndPaint(void*, const WinNtPaintStruct*) noexcept;
void drawAttachedWindowScrollBars(void*, void*) noexcept;
intptr_t User32_SendMessageW(void*, uint32_t, uintptr_t, intptr_t) noexcept;
void* User32_SetFocus(void*) noexcept;
void* User32_GetFocus() noexcept;
intptr_t User32_GetWindowLongPtrW(void*, int32_t) noexcept;
void* User32_SetCapture(void*) noexcept;
int32_t User32_ReleaseCapture() noexcept;
intptr_t User32_DefWindowProcW(void*, uint32_t, uintptr_t, intptr_t) noexcept;

namespace {
template <typename T> T Win32kProc(const char* name) noexcept;

struct AnimatedCursor {
    void* handle{};
    std::array<micant::um::imaging::ImageFrame,
        micant::um::winuser::AniFileView::MaxFrames> frames{};
    std::array<micant::um::winuser::AniStep,
        micant::um::winuser::AniFileView::MaxSteps> steps{};
    uint16_t frameCount{}, stepCount{}, step{};
    uint64_t deadlineMs{};
    uint16_t hotX{}, hotY{};
    std::array<uint16_t, micant::um::winuser::AniFileView::MaxFrames> frameHotX{};
    std::array<uint16_t, micant::um::winuser::AniFileView::MaxFrames> frameHotY{};
};
class CursorMutex {
public:
    void lock() noexcept {
        while (__atomic_exchange_n(&locked_, 1u, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause" ::: "memory");
    }
    void unlock() noexcept { __atomic_store_n(&locked_, 0u, __ATOMIC_RELEASE); }
private:
    uint32_t locked_{};
};
struct CursorLockGuard {
    explicit CursorLockGuard(CursorMutex& lock) noexcept : lock_(lock) { lock_.lock(); }
    ~CursorLockGuard() { lock_.unlock(); }
    CursorLockGuard(const CursorLockGuard&) = delete;
    CursorLockGuard& operator=(const CursorLockGuard&) = delete;
private:
    CursorMutex& lock_;
};
std::array<AnimatedCursor, 16> g_animatedCursors{};
uint32_t g_animatedCursorSelected{};
struct ButtonState {
    void* hwnd{};
    void* parent{};
    uintptr_t id{};
    uint32_t state{};
    uint32_t style{};
    bool armed{};
    bool spaceArmed{};
    uint32_t checkState{};
    uint32_t radioGroup{};
    uint64_t creationOrder{};
    wchar_t text[128]{};
};
struct EditState {
    void* hwnd{};
    void* parent{};
    uintptr_t id{};
    uint32_t state{};
    uint32_t style{};
    uint16_t length{};
    wchar_t text[512]{};
};
struct StaticState {
    void* hwnd{};
    void* parent{};
    uintptr_t id{};
    uint32_t style{}, extendedStyle{};
    uint16_t length{};
    wchar_t text[256]{};
};
struct ScrollInfoW {
    uint32_t cbSize, fMask;
    int32_t nMin, nMax;
    uint32_t nPage;
    int32_t nPos, nTrackPos;
};
struct ScrollBarState {
    void* hwnd{};
    void* parent{};
    uintptr_t id{};
    int32_t minimum{}, maximum{100}, position{}, trackPosition{};
    uint32_t page{10}, style{}, state{}, hotPart{}, pressedPart{}, disabledArrows{};
    bool captured{};
};
struct WindowScrollInfo {
    int32_t minimum{}, maximum{100}, position{}, trackPosition{};
    uint32_t page{}, disabledArrows{};
};
struct WindowScrollState {
    void* hwnd{};
    WindowScrollInfo bars[2]{}; // SB_HORZ, SB_VERT
    uint32_t pressedBar{0xFFFFFFFFu};
    uint32_t pressedPart{};
    uint32_t hotBar{0xFFFFFFFFu};
    uint32_t hotPart{};
};
// Native dialogs can contain far more than a small demo's handful of
// controls; leave sufficient room for complete, complex control trees.
std::array<ButtonState, 256> g_buttons{};
std::array<EditState, 256> g_edits{};
std::array<StaticState, 256> g_statics{};
std::array<ScrollBarState, 128> g_scrollBars{};
std::array<WindowScrollState, 128> g_windowScrollBars{};
void* g_hotButton{};
uint32_t g_nextRadioGroup{1};
uint64_t g_nextButtonOrder{1};
constexpr uint32_t kButtonHot = 0x01;
constexpr uint32_t kButtonPressed = 0x02;
constexpr uint32_t kButtonFocused = 0x04;
constexpr uint32_t kButtonDisabled = 0x08;
constexpr uint32_t kButtonDefault = 0x10;
constexpr uint32_t kWsDisabled = 0x08000000u;
constexpr uint32_t kWsVisible = 0x10000000u;
constexpr uint32_t kBsTypeMask = 0x0Fu;
constexpr uint32_t kBsPushButton = 0u;
constexpr uint32_t kBsDefPushButton = 1u;
constexpr uint32_t kBsCheckBox = 2u;
constexpr uint32_t kBsAutoCheckBox = 3u;
constexpr uint32_t kBsRadioButton = 4u;
constexpr uint32_t kBs3State = 5u;
constexpr uint32_t kBsAuto3State = 6u;
constexpr uint32_t kBsGroupBox = 7u;
constexpr uint32_t kBsAutoRadioButton = 9u;
constexpr uint32_t kBsOwnerDraw = 11u;
constexpr uint32_t kWsGroup = 0x00020000u;
constexpr uint32_t kEsPassword = 0x00000020u;
constexpr uint32_t kEsReadOnly = 0x00000800u;
constexpr uint32_t kEsUnsupportedMask = 0x000037DFu;
constexpr uint32_t kEsControlMask = 0x00003FFFu;
constexpr uint32_t kVkBack = 0x08u;
constexpr uint32_t kWmChar = 0x0102u;
constexpr uint32_t kWmNcCreate = 0x0081u;
constexpr uint32_t kWmDestroy = 0x0002u;
constexpr uint32_t kWmNcDestroy = 0x0082u;
constexpr uint32_t kWmSettingChange = 0x001Au;
constexpr uint32_t kWmThemeChanged = 0x031Au;
constexpr uint32_t kWmPaint = 0x000Fu;
constexpr uint32_t kWmSetText = 0x000Cu;
constexpr uint32_t kWmGetText = 0x000Du;
constexpr uint32_t kWmGetTextLength = 0x000Eu;
constexpr uint32_t kWmEnable = 0x000Au;
constexpr uint32_t kWmSetFocus = 0x0007u;
constexpr uint32_t kWmKillFocus = 0x0008u;
constexpr uint32_t kWmMouseMove = 0x0200u;
constexpr uint32_t kWmLButtonDown = 0x0201u;
constexpr uint32_t kWmLButtonUp = 0x0202u;
constexpr uint32_t kWmKeyDown = 0x0100u;
constexpr uint32_t kWmKeyUp = 0x0101u;
constexpr uint32_t kWmMouseLeave = 0x02A3u;
constexpr uint32_t kWmCaptureChanged = 0x0215u;
constexpr uint32_t kWmCommand = 0x0111u;
constexpr uint32_t kBmGetState = 0x00F2u;
constexpr uint32_t kBmSetState = 0x00F3u;
constexpr uint32_t kBmSetStyle = 0x00F4u;
constexpr uint32_t kBmClick = 0x00F5u;
constexpr uint32_t kBmGetCheck = 0x00F0u;
constexpr uint32_t kBmSetCheck = 0x00F1u;
constexpr uint32_t kBnClicked = 0u;
constexpr uint32_t kEnChange = 0x0300u;
constexpr uint32_t kSbSetPos = 0x00E0u;
constexpr uint32_t kSbGetPos = 0x00E1u;
constexpr uint32_t kSbSetRange = 0x00E2u;
constexpr uint32_t kSbGetRange = 0x00E3u;
constexpr uint32_t kSbEnableArrows = 0x00E4u;
constexpr uint32_t kSbSetRangeRedraw = 0x00E6u;
constexpr uint32_t kSbSetScrollInfo = 0x00E9u;
constexpr uint32_t kSbGetScrollInfo = 0x00EAu;
constexpr uint32_t kSbsVert = 0x0001u;
constexpr uint32_t kSbsUnsupported = 0x0018u;
constexpr uint32_t kSifRange = 0x0001u;
constexpr uint32_t kSifPage = 0x0002u;
constexpr uint32_t kSifPos = 0x0004u;
constexpr uint32_t kSifDisableNoScroll = 0x0008u;
constexpr uint32_t kSifTrackPos = 0x0010u;
constexpr uint32_t kSifAll = kSifRange | kSifPage | kSifPos | kSifTrackPos;
constexpr uint32_t kSifSet = kSifRange | kSifPage | kSifPos | kSifDisableNoScroll;
constexpr uint32_t kSbLineUp = 0u;
constexpr uint32_t kSbLineDown = 1u;
constexpr uint32_t kSbPageUp = 2u;
constexpr uint32_t kSbPageDown = 3u;
constexpr uint32_t kSbThumbPosition = 4u;
constexpr uint32_t kSbThumbTrack = 5u;
constexpr uint32_t kSbEndScroll = 8u;
constexpr uint32_t kMkLButton = 0x0001u;
constexpr uint32_t kVkSpace = 0x20u;
constexpr uint32_t kDtCenter = 0x00000001u;
constexpr uint32_t kDtLeft = 0x00000000u;
constexpr uint32_t kDtVCenter = 0x00000004u;
constexpr uint32_t kDtSingleLine = 0x00000020u;
constexpr uint32_t kDtNoPrefix = 0x00000800u;
constexpr uint32_t kDibRgb = 0u;
constexpr int32_t kStockDefaultGuiFont = 17;
constexpr uint32_t kColorWindowText = 0xFF202124u;
constexpr uint32_t kColorButton = 0xFFF0F0F0u;
constexpr uint32_t kColorButtonHot = 0xFFE5EEF9u;
constexpr uint32_t kColorButtonPressed = 0xFFD7E4F2u;
constexpr uint32_t kColorButtonDisabled = 0xFFE8E8E8u;
constexpr uint32_t kColorBorder = 0xFF8A8A8Au;
bool isSupportedButtonType(uint32_t type) noexcept {
    return type <= kBsGroupBox || type == kBsAutoRadioButton || type == kBsOwnerDraw;
}
bool isCheckableButtonType(uint32_t type) noexcept {
    return type == kBsCheckBox || type == kBsAutoCheckBox ||
        type == kBsRadioButton || type == kBsAutoRadioButton ||
        type == kBs3State || type == kBsAuto3State;
}
bool isRadioButtonType(uint32_t type) noexcept {
    return type == kBsRadioButton || type == kBsAutoRadioButton;
}
bool isAutoCheckButtonType(uint32_t type) noexcept {
    return type == kBsAutoCheckBox || type == kBsAuto3State;
}
struct ButtonLock {
    void lock() noexcept {
        while (__atomic_exchange_n(&locked, 1u, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause" ::: "memory");
    }
    void unlock() noexcept { __atomic_store_n(&locked, 0u, __ATOMIC_RELEASE); }
    uint32_t locked{};
};
ButtonLock g_buttonLock{};
struct ButtonLockGuard {
    ButtonLock& lock;
    explicit ButtonLockGuard(ButtonLock& value) noexcept : lock(value) { lock.lock(); }
    ~ButtonLockGuard() { lock.unlock(); }
};
ButtonState* findButton(void* hwnd) noexcept {
    for (auto& button : g_buttons) if (button.hwnd == hwnd && hwnd) return &button;
    return nullptr;
}
struct SharedPeCursorCacheEntry {
    void* module{};
    void* cursor{};
    uint32_t width{}, height{};
    uint16_t id{}, nameLength{};
    bool named{}, reserved{};
    std::array<char16_t, 256> name{};
};
std::array<SharedPeCursorCacheEntry, 255> g_sharedPeCursorCache{};
CursorMutex g_animatedCursorLock{};
CursorMutex g_sharedPeCursorCacheLock{};
struct SystemCursor { uintptr_t id; const wchar_t* file; };
constexpr SystemCursor g_systemCursors[]{
    {32512, L"mini_mouse_arrow.cur"}, {32513, L"mini_mouse_beam.cur"},
    {32515, L"mini_mouse_cross.cur"}, {32516, L"mini_mouse_up.cur"},
    {32631, L"mini_mouse_pen.cur"},
    {32642, L"mini_mouse_nwse.cur"}, {32643, L"mini_mouse_nesw.cur"},
    {32644, L"mini_mouse_ew.cur"}, {32645, L"mini_mouse_ns.cur"},
    {32646, L"mini_mouse_move.cur"}, {32648, L"mini_mouse_unavail.cur"},
    {32649, L"mini_mouse_link.cur"}, {32651, L"mini_mouse_helpsel.cur"},
    {32514, L"mini_mouse_busy.ani"}, {32650, L"mini_mouse_working.ani"},
};
struct SystemCursorCacheEntry { void* handle{}; bool reserved{}; };
std::array<SystemCursorCacheEntry, std::size(g_systemCursors)> g_systemCursorCache{};
CursorMutex g_systemCursorCacheLock{};
struct SharedCursorReservation {
    size_t slot{g_sharedPeCursorCache.size()};
    bool active{};
    ~SharedCursorReservation() {
        if (!active) return;
        CursorLockGuard lock(g_sharedPeCursorCacheLock);
        g_sharedPeCursorCache[slot] = {};
    }
    void publish(void* handle) noexcept {
        if (!active) return;
        CursorLockGuard lock(g_sharedPeCursorCacheLock);
        g_sharedPeCursorCache[slot].cursor = handle;
        g_sharedPeCursorCache[slot].reserved = false;
        active = false;
    }
};
struct SystemCursorReservation {
    size_t slot{};
    bool active{true};
    ~SystemCursorReservation() {
        if (!active) return;
        CursorLockGuard lock(g_systemCursorCacheLock);
        g_systemCursorCache[slot] = {};
    }
    void publish(void* handle) noexcept {
        if (!active) return;
        CursorLockGuard lock(g_systemCursorCacheLock);
        g_systemCursorCache[slot].handle = handle;
        g_systemCursorCache[slot].reserved = false;
        active = false;
    }
};
struct CursorThemeOverride {
    uintptr_t id{};
    std::array<char, 64> file{};
};
std::array<CursorThemeOverride, 32> g_cursorThemeOverrides{};
size_t g_cursorThemeOverrideCount{};
bool g_cursorThemeMapLoaded{};
CursorMutex g_cursorThemeMapLock{};

void setThreadLastError(uint32_t error) noexcept {
    __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(error) : "memory");
}

bool validSystemCursorId(uintptr_t id) noexcept {
    for (const auto& cursor : g_systemCursors)
        if (cursor.id == id) return true;
    return false;
}

bool validCursorFileName(const char* value, size_t length) noexcept {
    if (!length || length >= 64) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        if (c == '.' && i + 1 < length && value[i + 1] == '.') return false;
    }
    constexpr char cur[] = ".cur", ani[] = ".ani";
    const auto endsWith = [length, value](const char* extension, size_t count) noexcept {
        if (length < count) return false;
        for (size_t i = 0; i < count; ++i) {
            char a = value[length - count + i];
            char b = extension[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
            if (a != b) return false;
        }
        return true;
    };
    return endsWith(cur, sizeof(cur) - 1) || endsWith(ani, sizeof(ani) - 1);
}

bool cursorSystemPath(const wchar_t* suffix, wchar_t (&path)[512]) noexcept {
    if (!suffix) return false;
    auto* shared = reinterpret_cast<volatile const micant::KUserSharedData*>(
        micant::UserSharedDataAddress);
    size_t rootLength = 0;
    while (rootLength < std::size(shared->ntSystemRoot) &&
           shared->ntSystemRoot[rootLength]) ++rootLength;
    if (rootLength < 3 || rootLength >= std::size(shared->ntSystemRoot) ||
        shared->ntSystemRoot[1] != u':' || shared->ntSystemRoot[2] != u'\\') return false;
    size_t cursor = 0;
    for (; cursor < rootLength && cursor + 1 < std::size(path); ++cursor)
        path[cursor] = static_cast<wchar_t>(shared->ntSystemRoot[cursor]);
    for (size_t i = 0; suffix[i]; ++i) {
        if (cursor + 1 >= std::size(path)) return false;
        path[cursor++] = suffix[i];
    }
    path[cursor] = L'\0';
    return true;
}

void loadCursorThemeMap() noexcept {
    CursorLockGuard lock(g_cursorThemeMapLock);
    if (g_cursorThemeMapLoaded) return;
    g_cursorThemeMapLoaded = true;
    wchar_t path[512]{};
    if (!cursorSystemPath(L"\\System\\Cursors\\MiniMouse\\cursor.map", path)) return;
    imaging::ImageLimits limits{};
    limits.maxEncodedBytes = 4096;
    limits.maxPixels = 1;
    limits.maxDecodedBytes = 4;
    limits.maxIntermediateBytes = 4096;
    imaging::ImageBuffer encoded{};
    if (imaging::ReadIconSourceFile(path, encoded, imaging::NativeImageAllocator(), limits) !=
        imaging::ImageStatus::Success) return;
    std::array<CursorThemeOverride, 32> parsed{};
    size_t count = 0, position = 0;
    const auto bytes = encoded.span();
    while (position < bytes.size()) {
        size_t end = position;
        while (end < bytes.size() && bytes[end] != '\n') ++end;
        const size_t nextLine = end < bytes.size() ? end + 1 : bytes.size();
        size_t begin = position;
        while (begin < end && (bytes[begin] == ' ' || bytes[begin] == '\t' || bytes[begin] == '\r')) ++begin;
        while (end > begin && (bytes[end - 1] == ' ' || bytes[end - 1] == '\t' || bytes[end - 1] == '\r')) --end;
        if (begin < end && bytes[begin] != '#') {
            uintptr_t id = 0;
            size_t cursor = begin;
            while (cursor < end && bytes[cursor] >= '0' && bytes[cursor] <= '9') {
                const unsigned digit = bytes[cursor++] - '0';
                if (id > (UINTPTR_MAX - digit) / 10) return;
                id = id * 10 + digit;
            }
            if (cursor == begin || cursor == end ||
                (bytes[cursor] != ' ' && bytes[cursor] != '\t')) return;
            while (cursor < end && (bytes[cursor] == ' ' || bytes[cursor] == '\t')) ++cursor;
            const size_t fileLength = end - cursor;
            if (!validSystemCursorId(id) ||
                !validCursorFileName(reinterpret_cast<const char*>(bytes.data() + cursor), fileLength) ||
                count == parsed.size()) return;
            for (size_t i = 0; i < count; ++i) if (parsed[i].id == id) return;
            parsed[count].id = id;
            for (size_t i = 0; i < fileLength; ++i)
                parsed[count].file[i] = static_cast<char>(bytes[cursor + i]);
            ++count;
        }
        position = nextLine;
    }
    g_cursorThemeOverrides = parsed;
    g_cursorThemeOverrideCount = count;
}

void cursorThemeFile(uintptr_t id, const wchar_t* fallback, wchar_t (&output)[64]) noexcept {
    loadCursorThemeMap();
    const CursorThemeOverride* selected = nullptr;
    {
        CursorLockGuard lock(g_cursorThemeMapLock);
        for (size_t i = 0; i < g_cursorThemeOverrideCount; ++i)
            if (g_cursorThemeOverrides[i].id == id) { selected = &g_cursorThemeOverrides[i]; break; }
        if (selected) {
            size_t i = 0;
            while (i + 1 < std::size(output) && selected->file[i]) {
                output[i] = static_cast<wchar_t>(static_cast<unsigned char>(selected->file[i]));
                ++i;
            }
            output[i] = L'\0';
            return;
        }
    }
    size_t i = 0;
    while (i + 1 < std::size(output) && fallback[i]) { output[i] = fallback[i]; ++i; }
    output[i] = L'\0';
}

uint64_t cursorClockMs() noexcept {
    return *reinterpret_cast<volatile const uint64_t*>(
        micant::UserSharedDataAddress + offsetof(micant::KUserSharedData, tickCountQuad));
}

void updateAnimatedCursor() noexcept {
    using namespace micant::um;
    // Message loops call this before every blocking GetMessage probe. Avoid a
    // Win32k syscall on every idle poll when the selected cursor is static.
    if (!__atomic_load_n(&g_animatedCursorSelected, __ATOMIC_ACQUIRE)) return;
    using InfoFn = int32_t (*)(MicaWin32kCursorInfo*);
    using SetFn = void* (*)(void*, const uint32_t*, uint32_t, uint32_t,
                            uint32_t, uint32_t, uint32_t);
    auto getInfo = Win32kProc<InfoFn>("MicaWin32kGetCursorInfo");
    auto setCursor = Win32kProc<SetFn>("MicaWin32kSetCursor");
    if (!getInfo || !setCursor) return;
    MicaWin32kCursorInfo info{};
    if (!getInfo(&info) || !info.cursorHandle) return;
    const uint64_t now = cursorClockMs();
    CursorLockGuard lock(g_animatedCursorLock);
    for (auto& animation : g_animatedCursors) {
        if (animation.handle != reinterpret_cast<void*>(info.cursorHandle) ||
            !animation.frameCount || !animation.stepCount) continue;
        if (now < animation.deadlineMs) return;
        // Bound catch-up work by the parsed step count; a stalled message loop
        // skips elapsed frames without accumulating timing drift.
        for (uint16_t advanced = 0; advanced < animation.stepCount &&
             now >= animation.deadlineMs; ++advanced) {
            animation.step = static_cast<uint16_t>((animation.step + 1) % animation.stepCount);
            const auto& next = animation.steps[animation.step];
            const uint64_t duration = std::max<uint64_t>(1,
                (uint64_t(next.durationJiffies) * 1000 + 59) / 60);
            animation.deadlineMs += duration;
        }
        if (now >= animation.deadlineMs) {
            const auto& current = animation.steps[animation.step];
            const uint64_t duration = std::max<uint64_t>(1,
                (uint64_t(current.durationJiffies) * 1000 + 59) / 60);
            animation.deadlineMs = now + duration;
        }
        const auto& step = animation.steps[animation.step];
        const auto& frame = animation.frames[step.frameIndex];
        (void)setCursor(animation.handle,
            reinterpret_cast<const uint32_t*>(frame.pixels.data()), frame.width,
            frame.height, frame.stride / 4, animation.frameHotX[step.frameIndex],
            animation.frameHotY[step.frameIndex]);
        return;
    }
}

void* loadAniCursor(std::span<const uint8_t> encoded,
                    const imaging::ImageAllocator& allocator,
                    const imaging::ImageLimits& limits, bool shared = false,
                    uint32_t requestedWidth = 0,
                    uint32_t requestedHeight = 0) noexcept {
    winuser::AniFileView view{};
    if (winuser::ParseAniFile(encoded, view, limits.maxEncodedBytes) != winuser::AniStatus::Success)
        return nullptr;
    auto frameLimits = limits;
    AnimatedCursor candidate{};
    candidate.frameCount = view.frameCount;
    for (uint16_t i = 0; i < view.frameCount; ++i) {
        winuser::CursorImage cursor{};
        if (winuser::DecodeCursorImage(view.frames[i], requestedWidth, requestedHeight,
                cursor, allocator, frameLimits) !=
            imaging::ImageStatus::Success) return nullptr;
        const uint32_t targetWidth = requestedWidth ? requestedWidth : cursor.image.width;
        const uint32_t targetHeight = requestedHeight ? requestedHeight : cursor.image.height;
        if (!targetWidth || !targetHeight) return nullptr;
        uint16_t hotX = cursor.hotX, hotY = cursor.hotY;
        if (cursor.image.width != targetWidth || cursor.image.height != targetHeight) {
            const uint32_t sourceWidth = cursor.image.width, sourceHeight = cursor.image.height;
            imaging::ImageFrame scaled{};
            if (imaging::ScaleIconFrame(cursor.image, targetWidth, targetHeight, scaled,
                    allocator, frameLimits) != imaging::ImageStatus::Success) return nullptr;
            hotX = static_cast<uint16_t>(std::min<uint32_t>(targetWidth - 1,
                (uint32_t(hotX) * targetWidth + sourceWidth / 2) / sourceWidth));
            hotY = static_cast<uint16_t>(std::min<uint32_t>(targetHeight - 1,
                (uint32_t(hotY) * targetHeight + sourceHeight / 2) / sourceHeight));
            cursor.image = std::move(scaled);
        }
        candidate.frameHotX[i] = hotX;
        candidate.frameHotY[i] = hotY;
        candidate.frames[i] = std::move(cursor.image);
    }
    candidate.stepCount = view.stepCount;
    std::copy_n(view.steps.begin(), view.stepCount, candidate.steps.begin());
    imaging::ImageFrame first{};
    const uint16_t firstFrameIndex = view.steps[0].frameIndex;
    const auto& frame = candidate.frames[firstFrameIndex];
    candidate.hotX = candidate.frameHotX[firstFrameIndex];
    candidate.hotY = candidate.frameHotY[firstFrameIndex];
    first.width = frame.width; first.height = frame.height; first.stride = frame.stride;
    first.format = imaging::PixelFormat::Bgra8Premultiplied;
    if (!first.pixels.allocate(frame.pixels.size(), allocator)) return nullptr;
    std::memcpy(first.pixels.data(), frame.pixels.data(), frame.pixels.size());
    candidate.handle = winuser::ProcessIconHandles().createCursor(std::move(first), allocator,
        candidate.hotX, candidate.hotY, shared);
    if (!candidate.handle) return nullptr;
    const uint64_t firstDuration = std::max<uint64_t>(1,
        (uint64_t(candidate.steps[0].durationJiffies) * 1000 + 59) / 60);
    candidate.deadlineMs = cursorClockMs() + firstDuration;
    CursorLockGuard lock(g_animatedCursorLock);
    for (auto& slot : g_animatedCursors) {
        if (slot.handle) continue;
        slot = std::move(candidate);
        return slot.handle;
    }
    (void)winuser::ProcessIconHandles().destroyCursor(candidate.handle);
    return nullptr;
}

bool loadedModulePath(void* moduleHandle, wchar_t (&path)[512], uint32_t* loadedImageSize = nullptr) noexcept {
    constexpr uintptr_t UserLimit = 0x00007FFFFFFF0000ULL;
    const uintptr_t wantedBase = reinterpret_cast<uintptr_t>(moduleHandle);
    if (wantedBase < 0x10000 || wantedBase >= UserLimit) return false;
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (teb < 0x10000 || teb >= UserLimit - 0x68) return false;
    const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    if (peb < 0x10000 || peb >= UserLimit - 0x20) return false;
    const uintptr_t loader = *reinterpret_cast<const uintptr_t*>(peb + 0x18);
    if (loader < 0x10000 || loader >= UserLimit - 0x28) return false;
    const uintptr_t headAddress = loader + 0x20; // InMemoryOrderModuleList
    const auto* head = reinterpret_cast<const uintptr_t*>(headAddress);
    uintptr_t link = head[0];
    for (size_t count = 0; count < 64 && link != headAddress; ++count) {
        if (link < 0x10010 || link >= UserLimit) return false;
        const uintptr_t entry = link - 0x10;
        if (entry >= UserLimit - 0x60) return false;
        if (*reinterpret_cast<const uintptr_t*>(entry + 0x30) == wantedBase) {
            const uint32_t imageSize = *reinterpret_cast<const uint32_t*>(entry + 0x40);
            const auto* unicode = reinterpret_cast<const uint8_t*>(entry + 0x48);
            const uint16_t bytes = *reinterpret_cast<const uint16_t*>(unicode);
            const wchar_t* buffer = *reinterpret_cast<const wchar_t* const*>(unicode + 8);
            if (imageSize < 0x1000 || imageSize > 256u * 1024u * 1024u ||
                !bytes || (bytes & 1u) || bytes > (sizeof(path) - sizeof(wchar_t)) ||
                reinterpret_cast<uintptr_t>(buffer) < 0x10000 ||
                reinterpret_cast<uintptr_t>(buffer) >= UserLimit - bytes) return false;
            const size_t length = bytes / sizeof(wchar_t);
            for (size_t i = 0; i < length; ++i) path[i] = buffer[i];
            path[length] = L'\0';
            if (loadedImageSize) *loadedImageSize = imageSize;
            return true;
        }
        link = *reinterpret_cast<const uintptr_t*>(link);
    }
    return false;
}

int32_t User32_LoadStringW(void* moduleHandle, uint32_t stringId,
                           wchar_t* buffer, int32_t capacity) noexcept {
    using namespace micant::um;
    if (!buffer || capacity < 0 || (stringId >> 4) + 1 > UINT16_MAX) {
        setThreadLastError(87); // ERROR_INVALID_PARAMETER
        return 0;
    }
    if (!moduleHandle) {
        // The loader's image list supplies the current image base for a null
        // HINSTANCE. Do not guess an image or search unrelated modules.
        uintptr_t teb{};
        __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
        if (teb < 0x10000 || teb >= 0x00007FFFFFFF0000ULL - 0x68) {
            setThreadLastError(1814);
            return 0;
        }
        const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
        if (peb < 0x10000 || peb >= 0x00007FFFFFFF0000ULL - 0x10) {
            setThreadLastError(1814);
            return 0;
        }
        moduleHandle = *reinterpret_cast<void* const*>(peb + 0x10);
    }
    wchar_t ignoredPath[512]{};
    uint32_t imageSize{};
    if (!loadedModulePath(moduleHandle, ignoredPath, &imageSize)) {
        setThreadLastError(1814); // ERROR_RESOURCE_NAME_NOT_FOUND
        return 0;
    }
    pe::PeImageView64 image{};
    const auto mapped = std::span<const uint8_t>(
        static_cast<const uint8_t*>(moduleHandle), imageSize);
    if (image.inspectMapped(mapped) != NtStatus::Success) {
        setThreadLastError(13); // ERROR_INVALID_DATA
        return 0;
    }
    const pe::PeResourceIdentifier type{6, {}, false}; // RT_STRING
    const pe::PeResourceIdentifier block{
        static_cast<uint16_t>((stringId >> 4) + 1), {}, false};
    pe::PeResourceData resource{};
    const NtStatus status = pe::PeResourceView(image).find(type, block, 0, resource);
    if (status != NtStatus::Success) {
        setThreadLastError(status == NtStatus::NotFound ? 1814 : 13);
        return 0;
    }
    size_t offset{};
    const uint32_t slot = stringId & 15u;
    for (uint32_t index = 0; index <= slot; ++index) {
        if (offset > resource.bytes.size() || resource.bytes.size() - offset < 2) {
            setThreadLastError(13);
            return 0;
        }
        const uint16_t chars = pe::PeResourceView::read16(resource.bytes.data() + offset);
        offset += 2;
        const size_t byteCount = static_cast<size_t>(chars) * 2;
        if (byteCount > resource.bytes.size() - offset) {
            setThreadLastError(13);
            return 0;
        }
        if (index != slot) { offset += byteCount; continue; }
        const auto* text = reinterpret_cast<const wchar_t*>(resource.bytes.data() + offset);
        if (capacity == 0) {
            *reinterpret_cast<const wchar_t**>(buffer) = text;
            return chars;
        }
        const size_t copied = std::min<size_t>(chars, static_cast<size_t>(capacity - 1));
        for (size_t i = 0; i < copied; ++i) buffer[i] = text[i];
        buffer[copied] = L'\0';
        return static_cast<int32_t>(copied);
    }
    setThreadLastError(13);
    return 0;
}

void* loadPeCursor(void* moduleHandle, const wchar_t* resourceName,
                   uint32_t desiredWidth, uint32_t desiredHeight,
                   bool shared) noexcept {
    using namespace micant::um;
    if (!resourceName) {
        setThreadLastError(87); // ERROR_INVALID_PARAMETER
        return nullptr;
    }
    pe::PeResourceIdentifier identifier{};
    std::array<char16_t, 256> nameBuffer{};
    const uintptr_t rawName = reinterpret_cast<uintptr_t>(resourceName);
    if (rawName <= UINT16_MAX) {
        if (!rawName) { setThreadLastError(1814); return nullptr; }
        identifier = {static_cast<uint16_t>(rawName), {}, false};
    } else {
        size_t length{};
        while (length < nameBuffer.size() && resourceName[length]) {
            const uint32_t value = static_cast<uint32_t>(resourceName[length]);
            if (value > UINT16_MAX) { setThreadLastError(87); return nullptr; }
            nameBuffer[length] = static_cast<char16_t>(value);
            ++length;
        }
        if (!length || length == nameBuffer.size()) { setThreadLastError(1814); return nullptr; }
        identifier = {0, std::u16string_view(nameBuffer.data(), length), true};
    }
    size_t cacheSlot = g_sharedPeCursorCache.size();
    if (shared) {
        for (;;) {
            bool waitForReservedMatch = false;
            cacheSlot = g_sharedPeCursorCache.size();
            {
                CursorLockGuard lock(g_sharedPeCursorCacheLock);
                for (size_t i = 0; i < g_sharedPeCursorCache.size(); ++i) {
                    auto& entry = g_sharedPeCursorCache[i];
                    if (!entry.reserved && !entry.cursor) {
                        if (cacheSlot == g_sharedPeCursorCache.size()) cacheSlot = i;
                        continue;
                    }
                    if (entry.module != moduleHandle || entry.width != desiredWidth ||
                        entry.height != desiredHeight || entry.named != identifier.isName) continue;
                    const bool matches = identifier.isName
                        ? entry.nameLength == identifier.name.size() &&
                            std::equal(identifier.name.begin(), identifier.name.end(), entry.name.begin())
                        : entry.id == identifier.id;
                    if (matches) {
                        if (entry.cursor) return entry.cursor;
                        waitForReservedMatch = true;
                        break;
                    }
                }
                if (!waitForReservedMatch) {
                    if (cacheSlot == g_sharedPeCursorCache.size()) return nullptr;
                    auto& entry = g_sharedPeCursorCache[cacheSlot];
                    entry.module = moduleHandle;
                    entry.width = desiredWidth;
                    entry.height = desiredHeight;
                    entry.named = identifier.isName;
                    entry.id = identifier.id;
                    entry.nameLength = static_cast<uint16_t>(identifier.name.size());
                    entry.reserved = true;
                    if (identifier.isName)
                        std::copy(identifier.name.begin(), identifier.name.end(), entry.name.begin());
                }
            }
            if (!waitForReservedMatch) break;
            __asm__ __volatile__("pause" ::: "memory");
        }
    }
    SharedCursorReservation reservation{cacheSlot, shared};
    wchar_t modulePath[512]{};
    if (!loadedModulePath(moduleHandle, modulePath)) { setThreadLastError(1814); return nullptr; }
    imaging::ImageLimits limits{};
    limits.maxEncodedBytes = 64ull * 1024 * 1024;
    limits.maxPixels = 16ull * 1024 * 1024;
    limits.maxDecodedBytes = 64ull * 1024 * 1024;
    limits.maxIntermediateBytes = 80ull * 1024 * 1024;
    imaging::ImageBuffer encoded{};
    const auto allocator = imaging::NativeImageAllocator();
    if (imaging::ReadIconSourceFile(modulePath, encoded, allocator, limits) !=
        imaging::ImageStatus::Success) { setThreadLastError(2); return nullptr; }
    pe::PeImageView64 image{};
    if (image.inspect(encoded.span()) != NtStatus::Success) { setThreadLastError(193); return nullptr; }
    pe::PeResourceView resources(image);
    pe::PeGroupCursorView cursors(resources);
    pe::PeCursorResource selected{};
    const NtStatus resourceStatus =
        cursors.findCursor(identifier, 0, desiredWidth, desiredHeight, selected);
    if (resourceStatus != NtStatus::Success) {
        setThreadLastError(resourceStatus == NtStatus::NotFound ? 1814 :
            resourceStatus == NtStatus::InvalidParameter ? 87 : 13);
        return nullptr;
    }
    const uint32_t targetWidth = desiredWidth ? desiredWidth : selected.entry.width;
    const uint32_t targetHeight = desiredHeight ? desiredHeight : selected.entry.height;
    imaging::ImageFrame frame{};
    if (imaging::DecodeIconResourcePayload(selected.imageBytes, frame, allocator, limits) !=
        imaging::ImageStatus::Success || frame.width != selected.entry.width ||
        frame.height != selected.entry.height) {
        setThreadLastError(13); // ERROR_INVALID_DATA
        return nullptr;
    }
    uint16_t hotX = selected.entry.hotX, hotY = selected.entry.hotY;
    if (frame.width != targetWidth || frame.height != targetHeight) {
        const uint32_t oldWidth = frame.width, oldHeight = frame.height;
        imaging::ImageFrame scaled{};
        if (imaging::ScaleIconFrame(frame, targetWidth, targetHeight, scaled, allocator, limits) !=
            imaging::ImageStatus::Success) { setThreadLastError(13); return nullptr; }
        hotX = static_cast<uint16_t>(std::min<uint32_t>(targetWidth - 1,
            (uint32_t(hotX) * targetWidth + oldWidth / 2) / oldWidth));
        hotY = static_cast<uint16_t>(std::min<uint32_t>(targetHeight - 1,
            (uint32_t(hotY) * targetHeight + oldHeight / 2) / oldHeight));
        frame = std::move(scaled);
    }
    void* handle = winuser::ProcessIconHandles().createCursor(std::move(frame), allocator,
        hotX, hotY, shared);
    if (handle) reservation.publish(handle);
    return handle;
}

template <typename T> T Win32kProc(const char* name) noexcept {
#define MICA_WIN32K_PROC(exportName) if (std::strcmp(name, #exportName) == 0) return reinterpret_cast<T>(&exportName)
    MICA_WIN32K_PROC(MicaWin32kGetSystemMetrics);
    MICA_WIN32K_PROC(MicaWin32kCreateWindowExW);
    MICA_WIN32K_PROC(MicaWin32kDestroyWindow);
    MICA_WIN32K_PROC(MicaWin32kShowWindow);
    MICA_WIN32K_PROC(MicaWin32kSetWindowPos);
    MICA_WIN32K_PROC(MicaWin32kPostMessageW);
    MICA_WIN32K_PROC(MicaWin32kSendMessageW);
    MICA_WIN32K_PROC(MicaWin32kPostQuitMessage);
    MICA_WIN32K_PROC(MicaWin32kPeekMessageW);
    MICA_WIN32K_PROC(MicaWin32kGetMessageW);
    MICA_WIN32K_PROC(MicaWin32kGetClientRect);
    MICA_WIN32K_PROC(MicaWin32kGetWindowRect);
    MICA_WIN32K_PROC(MicaWin32kAdjustWindowRectEx);
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_CALC)
    MICA_WIN32K_PROC(MicaWin32kDebugInputTrace);
#endif
    MICA_WIN32K_PROC(MicaWin32kEnumWindows);
    MICA_WIN32K_PROC(MicaWin32kGetDlgItem);
    MICA_WIN32K_PROC(MicaWin32kGetWindowTextW);
    MICA_WIN32K_PROC(MicaWin32kIsWindowVisible);
    MICA_WIN32K_PROC(MicaWin32kIsIconic);
    MICA_WIN32K_PROC(MicaWin32kGetForegroundWindow);
    MICA_WIN32K_PROC(MicaWin32kSetFocus);
    MICA_WIN32K_PROC(MicaWin32kGetFocus);
    MICA_WIN32K_PROC(MicaWin32kGetCapture);
    MICA_WIN32K_PROC(MicaWin32kSetCapture);
    MICA_WIN32K_PROC(MicaWin32kReleaseCapture);
    MICA_WIN32K_PROC(MicaWin32kGetCursorPos);
    MICA_WIN32K_PROC(MicaWin32kGetClipCursor);
    MICA_WIN32K_PROC(MicaWin32kSetCursorPos);
    MICA_WIN32K_PROC(MicaWin32kClipCursor);
    MICA_WIN32K_PROC(MicaWin32kShowCursor);
    MICA_WIN32K_PROC(MicaWin32kSetCursor);
    MICA_WIN32K_PROC(MicaWin32kGetCursor);
    MICA_WIN32K_PROC(MicaWin32kGetCursorInfo);
    MICA_WIN32K_PROC(MicaWin32kSetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kSetWindowIconData);
    MICA_WIN32K_PROC(MicaWin32kGetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kSetWindowTitleSurface);
    MICA_WIN32K_PROC(MicaWin32kRegisterClassW);
    MICA_WIN32K_PROC(MicaWin32kDispatchMessageW);
    MICA_WIN32K_PROC(MicaWin32kSetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kGetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kDefWindowProcW);
    MICA_WIN32K_PROC(MicaWin32kGetDC);
    MICA_WIN32K_PROC(MicaWin32kGetDCSurface);
    MICA_WIN32K_PROC(MicaWin32kReleaseDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleDC);
    MICA_WIN32K_PROC(MicaWin32kDeleteDC);
    MICA_WIN32K_PROC(MicaWin32kCreateCompatibleBitmap);
    MICA_WIN32K_PROC(MicaWin32kSelectObject);
    MICA_WIN32K_PROC(MicaWin32kBitBlt);
    MICA_WIN32K_PROC(MicaWin32kPresentDC);
    MICA_WIN32K_PROC(MicaWin32kPresentDCRect);
    MICA_WIN32K_PROC(MicaWin32kDeleteObject);
    MICA_WIN32K_PROC(MicaWin32kGetStockObject);
    MICA_WIN32K_PROC(MicaWin32kCreateDibSection);
    MICA_WIN32K_PROC(MicaWin32kFillRect);
#undef MICA_WIN32K_PROC
    return nullptr;
}

struct CaptionDibInfo {
    uint32_t size{40};
    int32_t width{}, height{};
    uint16_t planes{1}, bitCount{32};
    uint32_t compression{}, imageSize{};
    int32_t xPixelsPerMeter{}, yPixelsPerMeter{};
    uint32_t colorsUsed{}, colorsImportant{};
};
struct CaptionTextExtent { int32_t cx{}, cy{}; };

bool publishCaptionTitleSurface(void* hwnd, const wchar_t* title) noexcept {
    using SetTitleFn = int32_t (*)(void*, const uint32_t*, uint32_t, uint32_t, uint32_t);
    const auto setTitle = Win32kProc<SetTitleFn>("MicaWin32kSetWindowTitleSurface");
    if (!hwnd || !setTitle) return false;
    const auto clearTitle = [&]() noexcept { return setTitle(hwnd, nullptr, 0, 0, 0) != 0; };

    size_t length = 0;
    if (title) {
        while (length + 1 < MICANT_WIN32K_TEXT_CAPACITY && title[length]) ++length;
        if (title[length]) {
            (void)clearTitle();
            return false;
        }
    }
    if (!length) return clearTitle();

    void* dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        (void)clearTitle();
        return false;
    }
    // WhiteSur's headerbar title uses the installed Segoe UI Semibold TTF
    // (resolved by GDI's font catalog to Selawik-Semibold.ttf). Keep the
    // stock GUI font as a failure fallback. Scale the point size from the
    // device's current vertical DPI.
    const int32_t dpi = std::max<int32_t>(1, GetDeviceCaps(dc, 90));
    const int32_t fontHeight = -std::max<int32_t>(1, (13 * dpi + 48) / 96);
    void* font = CreateFontW(fontHeight, 0, 0, 0, 600, 0, 0, 0, 1, 0, 0, 0, 0,
                             L"Segoe UI Semibold");
    const bool ownsFont = font != nullptr;
    if (!font) font = GetStockObject(kStockDefaultGuiFont);
    void* oldFont = font ? SelectObject(dc, font) : nullptr;
    using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
    using GetIconFn = uint64_t (*)(void*, uint32_t);
    const auto getWindowRect = Win32kProc<GetRectFn>("MicaWin32kGetWindowRect");
    const auto getClientRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
    const auto getWindowIcon = Win32kProc<GetIconFn>("MicaWin32kGetWindowIcon");
    MicaWin32kRect windowRect{}, clientRect{};
    const bool haveGeometry = oldFont && getWindowRect && getClientRect &&
        getWindowRect(hwnd, &windowRect) && getClientRect(hwnd, &clientRect) &&
        windowRect.right > windowRect.left &&
        clientRect.right > clientRect.left;
    if (!haveGeometry) {
        if (oldFont) (void)SelectObject(dc, oldFont);
        if (ownsFont) (void)DeleteObject(font);
        (void)DeleteDC(dc);
        (void)clearTitle();
        return false;
    }
    const int64_t outerWidth = static_cast<int64_t>(windowRect.right) - windowRect.left;
    const int64_t clientWidth = static_cast<int64_t>(clientRect.right) - clientRect.left;
    const int64_t frame = std::max<int64_t>(0, (outerWidth - clientWidth) / 2);
    const int64_t titleStart = frame + 9;
    const int64_t closeLeft = outerWidth - frame - 30;
    const int64_t titleLimit = closeLeft - 66;
    const int64_t iconReserve = getWindowIcon && getWindowIcon(hwnd, 0) ? 32 : 0;
    const int64_t availableWidth = std::max<int64_t>(
        0, titleLimit - titleStart - iconReserve);
    if (!availableWidth) {
        if (oldFont) (void)SelectObject(dc, oldFont);
        if (ownsFont) (void)DeleteObject(font);
        (void)DeleteDC(dc);
        return clearTitle();
    }

    wchar_t displayTitle[MICANT_WIN32K_TEXT_CAPACITY]{};
    size_t displayLength = length;
    for (size_t index = 0; index < length; ++index) displayTitle[index] = title[index];
    CaptionTextExtent extent{};
    bool measured = GetTextExtentPoint32W(dc, displayTitle,
        static_cast<int32_t>(displayLength), &extent) && extent.cx > 0 && extent.cy > 0;
    if (measured && extent.cx > availableWidth) {
        constexpr wchar_t kEllipsis[]{L'.', L'.', L'.', 0};
        CaptionTextExtent ellipsisExtent{};
        if (!GetTextExtentPoint32W(dc, kEllipsis, 3, &ellipsisExtent) ||
            ellipsisExtent.cx <= 0 || ellipsisExtent.cx > availableWidth) {
            if (oldFont) (void)SelectObject(dc, oldFont);
            if (ownsFont) (void)DeleteObject(font);
            (void)DeleteDC(dc);
            return clearTitle();
        }
        size_t prefix = length;
        measured = false;
        while (true) {
            for (size_t index = 0; index < prefix; ++index)
                displayTitle[index] = title[index];
            displayTitle[prefix] = L'.';
            displayTitle[prefix + 1] = L'.';
            displayTitle[prefix + 2] = L'.';
            displayTitle[prefix + 3] = L'\0';
            displayLength = prefix + 3;
            measured = GetTextExtentPoint32W(dc, displayTitle,
                static_cast<int32_t>(displayLength), &extent) &&
                extent.cx > 0 && extent.cy > 0;
            if (measured && extent.cx <= availableWidth) break;
            if (!prefix) { measured = false; break; }
            --prefix;
        }
    }
    const uint32_t width = measured ? static_cast<uint32_t>(extent.cx) : 0;
    const uint32_t height = measured ? static_cast<uint32_t>(extent.cy) : 0;
    if (!width || !height || width > INT32_MAX || height > INT32_MAX ||
        static_cast<size_t>(width) > SIZE_MAX / height ||
        static_cast<size_t>(width) * height > UINT32_MAX / sizeof(uint32_t)) {
        if (oldFont) (void)SelectObject(dc, oldFont);
        if (ownsFont) (void)DeleteObject(font);
        (void)DeleteDC(dc);
        (void)clearTitle();
        return false;
    }
    CaptionDibInfo info{};
    info.width = static_cast<int32_t>(width);
    info.height = -static_cast<int32_t>(height);
    info.imageSize = width * height * sizeof(uint32_t);
    void* bits = nullptr;
    void* bitmap = CreateDIBSection(dc, &info, 0, &bits, nullptr, 0);
    void* oldBitmap = bitmap ? SelectObject(dc, bitmap) : nullptr;
    const size_t pixelCount = static_cast<size_t>(width) * height;
    auto* pixels = static_cast<uint32_t*>(bits);
    if (pixels) std::fill_n(pixels, pixelCount, 0u);
    const bool rendered = pixels && oldBitmap && measured &&
        SetBkMode(dc, 1) > 0 && SetTextColor(dc, 0x00FFFFFFu) != 0xFFFFFFFFu &&
        TextOutW(dc, 0, 0, displayTitle, static_cast<int32_t>(displayLength));
    bool published = false;
    if (rendered) {
        // GDI's FontManager blends a white glyph into the transparent DIB.
        // Keep that grayscale coverage as alpha; the compositor applies the
        // current active/inactive HeaderBar foreground color at composition.
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t& pixel = pixels[static_cast<size_t>(y) * width + x];
                const uint32_t source = pixel;
                const uint32_t coverage = std::max({source & 0xFFu,
                    (source >> 8) & 0xFFu, (source >> 16) & 0xFFu});
                pixel = coverage << 24;
            }
        }
        published = setTitle(hwnd, pixels, width, height, width) != 0;
    }
    if (oldFont) (void)SelectObject(dc, oldFont);
    if (oldBitmap) (void)SelectObject(dc, oldBitmap);
    if (bitmap) (void)DeleteObject(bitmap);
    if (ownsFont) (void)DeleteObject(font);
    (void)DeleteObject(bitmap);
    (void)DeleteDC(dc);
    if (!published) (void)clearTitle();
    return published;
}

bool hasWindowCaption(void* hwnd) noexcept {
    using GetLongFn = intptr_t (*)(void*, int32_t);
    const auto getLong = Win32kProc<GetLongFn>("MicaWin32kGetWindowLongPtrW");
    constexpr int32_t kGwlStyle = -16;
    constexpr uint32_t kWsCaption = 0x00C00000u;
    return hwnd && getLong &&
        (static_cast<uint32_t>(getLong(hwnd, kGwlStyle)) & kWsCaption) == kWsCaption;
}

struct WinNtWindowState {
    uint64_t  magic{0x4D4E5448574E4431ULL}; // "MNTHWD1"
    int32_t   width{3840};
    int32_t   height{2160};
    intptr_t  userData{0};
    void*     imc{reinterpret_cast<void*>(static_cast<uintptr_t>(0x00000000000E0100ULL))};
    uint32_t  pendingMessages{1};
} g_primaryWindow{};
struct RegisteredClassIcons {
    wchar_t name[MICANT_WIN32K_TEXT_CAPACITY]{};
    void* large{};
    void* small{};
    void* cursor{};
    winuser::IconHandleTable::Lease largeLease{};
    winuser::IconHandleTable::Lease smallLease{};
    winuser::IconHandleTable::Lease cursorLease{};
};
RegisteredClassIcons g_registeredClassIcons[64]{};
struct RegisteredWindowClass { void* window{}; RegisteredClassIcons* icons{}; };
RegisteredWindowClass g_registeredWindowClasses[128]{};
RegisteredWindowClass* findRegisteredWindowClass(void* hwnd) noexcept {
    for (auto& entry : g_registeredWindowClasses)
        if (entry.window == hwnd) return &entry;
    return nullptr;
}
RegisteredClassIcons* findRegisteredClassIcons(const wchar_t* name) noexcept {
    if (!name) return nullptr;
    for (auto& entry : g_registeredClassIcons) {
        if (!entry.name[0]) continue;
        size_t i = 0;
        while (i < MICANT_WIN32K_TEXT_CAPACITY && entry.name[i] == name[i] && name[i]) ++i;
        if (i < MICANT_WIN32K_TEXT_CAPACITY && !entry.name[i] && !name[i]) return &entry;
    }
    return nullptr;
}

int32_t setWindowIconPixels(void* hwnd, uint32_t type, void* icon,
                            uint64_t* previous) noexcept {
    if (previous) *previous = 0;
    if (!previous || type >= 3u) return 0;
    winuser::IconHandleTable::Lease lease{};
    uint32_t width{}, height{}, stride{};
    const uint8_t* pixels = nullptr;
    if (icon) {
        if (!winuser::ProcessIconHandles().acquire(icon, lease)) return 0;
        const auto& view = lease.bitmap();
        if (view.cursor || !view.width || !view.height ||
            view.width > UINT32_MAX / sizeof(uint32_t) ||
            view.stride != view.width * sizeof(uint32_t) ||
            static_cast<uint64_t>(view.stride) * view.height > view.pixels.size())
            return 0;
        width = view.width;
        height = view.height;
        stride = view.stride;
        pixels = view.pixels.data();
    }
    using SetFn = int32_t (*)(void*, uint32_t, uint64_t, const uint8_t*,
        uint32_t, uint32_t, uint32_t, uint64_t*);
    const auto set = Win32kProc<SetFn>("MicaWin32kSetWindowIconData");
    return set ? set(hwnd, type, reinterpret_cast<uint64_t>(icon), pixels,
        width, height, stride, previous) : 0;
}

int32_t g_activePixelFormat = 1;
int32_t g_imeOpenStatus = 0;
intptr_t AuditWindowProc(void*,uint32_t message,uintptr_t,intptr_t) noexcept {
    return message == 0x0081 ? 1 : 0;
}
} // namespace

void* User32_CreateIconFromResourceEx(const uint8_t* resourceBytes,
                                      uint32_t resourceByteCount, int32_t isIcon,
                                      uint32_t resourceVersion, int32_t desiredWidth,
                                      int32_t desiredHeight, uint32_t flags) noexcept {
    using namespace micant::um;
    if (!resourceBytes || !resourceByteCount || isIcon != 1 ||
        resourceVersion < 0x00020000u || resourceVersion > 0x00030000u ||
        desiredWidth < 0 || desiredHeight < 0 || (flags & ~0x00000040u)) return nullptr;
    imaging::ImageFrame frame{};
    imaging::ImageLimits limits{};
    limits.maxEncodedBytes = 16ull * 1024 * 1024;
    limits.maxPixels = 16ull * 1024 * 1024;
    limits.maxDecodedBytes = 64ull * 1024 * 1024;
    limits.maxIntermediateBytes = 80ull * 1024 * 1024;
    const auto status = imaging::DecodeIconResourcePayload(
        std::span<const uint8_t>(resourceBytes, resourceByteCount), frame,
        imaging::NativeImageAllocator(), limits);
    if (status != imaging::ImageStatus::Success) return nullptr;
    const uint32_t targetWidth = desiredWidth ? static_cast<uint32_t>(desiredWidth)
        : ((flags & 0x40u) ? 32u : frame.width);
    const uint32_t targetHeight = desiredHeight ? static_cast<uint32_t>(desiredHeight)
        : ((flags & 0x40u) ? 32u : frame.height);
    if (!targetWidth || !targetHeight || targetWidth > 4096 || targetHeight > 4096)
        return nullptr;
    if (frame.width != targetWidth || frame.height != targetHeight) {
        imaging::ImageFrame scaled{};
        if (imaging::ScaleIconFrame(frame, targetWidth, targetHeight, scaled,
                                    imaging::NativeImageAllocator(), limits) !=
            imaging::ImageStatus::Success) return nullptr;
        frame = std::move(scaled);
    }
    return winuser::ProcessIconHandles().create(std::move(frame),
        imaging::NativeImageAllocator(), false);
}

void* User32_CopyIcon(void* icon) noexcept {
    return micant::um::winuser::ProcessIconHandles().copy(
        icon, micant::um::imaging::NativeImageAllocator());
}

void* User32_CopyCursor(void* cursor) noexcept {
    return micant::um::winuser::ProcessIconHandles().copyCursor(
        cursor, micant::um::imaging::NativeImageAllocator());
}

int32_t User32_DestroyCursor(void* cursor) noexcept {
    micant::um::winuser::IconHandleTable::Lease lease{};
    if (micant::um::winuser::ProcessIconHandles().acquireCursor(cursor, lease) &&
        lease.shared()) return 1;
    const bool destroyed = micant::um::winuser::ProcessIconHandles().destroyCursor(cursor);
    if (destroyed) {
        CursorLockGuard lock(g_animatedCursorLock);
        for (auto& animation : g_animatedCursors) {
            if (animation.handle == cursor) {
                animation = {};
                if (__atomic_load_n(&g_animatedCursorSelected, __ATOMIC_RELAXED))
                    __atomic_store_n(&g_animatedCursorSelected, 0u, __ATOMIC_RELEASE);
                break;
            }
        }
    }
    return destroyed ? 1 : 0;
}

void* User32_LoadCursorW(void* instance, const wchar_t* name) noexcept {
    if (instance) return loadPeCursor(instance, name, 32, 32, true);
    if (!name || reinterpret_cast<uintptr_t>(name) > UINT16_MAX) return nullptr;
    const uintptr_t id = reinterpret_cast<uintptr_t>(name);
    for (size_t i = 0; i < std::size(g_systemCursors); ++i) {
        if (g_systemCursors[i].id != id) continue;
        for (;;) {
            bool waitForLoader = false;
            {
                CursorLockGuard lock(g_systemCursorCacheLock);
                auto& entry = g_systemCursorCache[i];
                if (entry.handle) return entry.handle;
                if (entry.reserved) waitForLoader = true;
                else entry.reserved = true;
            }
            if (!waitForLoader) break;
            __asm__ __volatile__("pause" ::: "memory");
        }
        SystemCursorReservation reservation{i};
        wchar_t fileName[64]{};
        cursorThemeFile(id, g_systemCursors[i].file, fileName);
        const auto makePath = [](const wchar_t* file, wchar_t (&path)[512]) noexcept {
            wchar_t suffix[128] = L"\\System\\Cursors\\MiniMouse\\";
            size_t length = 0;
            while (length < std::size(suffix) && suffix[length]) ++length;
            size_t fileLength = 0;
            while (file[fileLength] && length + fileLength + 1 < std::size(suffix)) {
                suffix[length + fileLength] = file[fileLength];
                ++fileLength;
            }
            if (!fileLength || file[fileLength]) return false;
            suffix[length + fileLength] = L'\0';
            return cursorSystemPath(suffix, path);
        };
        using namespace micant::um;
        const auto allocator = imaging::NativeImageAllocator();
        imaging::ImageLimits limits{};
        limits.maxEncodedBytes = 16ull * 1024 * 1024;
        limits.maxPixels = 16ull * 1024 * 1024;
        limits.maxDecodedBytes = 64ull * 1024 * 1024;
        limits.maxIntermediateBytes = 80ull * 1024 * 1024;
        imaging::ImageBuffer encoded{};
        wchar_t path[512]{};
        bool loaded = makePath(fileName, path) &&
            imaging::ReadIconSourceFile(path, encoded, allocator, limits) == imaging::ImageStatus::Success;
        if (!loaded) {
            imaging::ImageBuffer fallback{};
            if (!makePath(g_systemCursors[i].file, path) ||
                imaging::ReadIconSourceFile(path, fallback, allocator, limits) !=
                    imaging::ImageStatus::Success) return nullptr;
            encoded = std::move(fallback);
        }
        const auto bytes = encoded.span();
        if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' &&
            bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'A' &&
            bytes[9] == 'C' && bytes[10] == 'O' && bytes[11] == 'N') {
            void* handle = loadAniCursor(bytes, allocator, limits, true);
            reservation.publish(handle);
            return handle;
        }
        winuser::CursorImage cursor{};
        if (winuser::DecodeCursorImage(bytes, 0, 0, cursor, allocator, limits) !=
            imaging::ImageStatus::Success) return nullptr;
        void* handle = winuser::ProcessIconHandles().createCursor(
            std::move(cursor.image), allocator, cursor.hotX, cursor.hotY, true);
        reservation.publish(handle);
        return handle;
    }
    // ANI system cursors need timer driven frame changes; do not silently
    // return a static image for IDC_WAIT or IDC_APPSTARTING.
    return nullptr;
}

void* User32_LoadCursorA(void* instance, const char* name) noexcept {
    constexpr uintptr_t IDC_ARROW = 32512;
    const uintptr_t value = reinterpret_cast<uintptr_t>(name);
    if (value <= UINT16_MAX) return User32_LoadCursorW(instance, reinterpret_cast<const wchar_t*>(value));
    if (!name) return nullptr;
    constexpr char arrow[] = "IDC_ARROW";
    for (size_t i = 0; i < sizeof(arrow); ++i) {
        char ch = name[i]; if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch != arrow[i]) return nullptr;
    }
    return User32_LoadCursorW(instance, reinterpret_cast<const wchar_t*>(uintptr_t(IDC_ARROW)));
}

void* User32_SetCursor(void* cursor) noexcept {
    using namespace micant::um::winuser;
    IconHandleTable::Lease lease{};
    if (cursor && !ProcessIconHandles().acquireCursor(cursor, lease)) return nullptr;
    using Fn = void* (*)(void*, const uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    auto fn = Win32kProc<Fn>("MicaWin32kSetCursor");
    if (!fn) return nullptr;
    const auto* pixels = cursor ? reinterpret_cast<const uint32_t*>(lease.bitmap().pixels.data()) : nullptr;
    const auto& bitmap = lease.bitmap();
    void* previous = fn(cursor, pixels, cursor ? bitmap.width : 0,
        cursor ? bitmap.height : 0, cursor ? bitmap.stride / 4 : 0,
        cursor ? bitmap.hotX : 0, cursor ? bitmap.hotY : 0);
    if (cursor || previous) {
        IconHandleTable::Lease previousLease{};
        if (previous && !ProcessIconHandles().acquireCursor(previous, previousLease)) return nullptr;
    }
    bool animated = false;
    if (cursor) {
        CursorLockGuard lock(g_animatedCursorLock);
        for (const auto& animation : g_animatedCursors) {
            if (animation.handle == cursor && animation.frameCount && animation.stepCount) {
                animated = true;
                break;
            }
        }
    }
    __atomic_store_n(&g_animatedCursorSelected, animated ? 1u : 0u, __ATOMIC_RELEASE);
    return previous;
}

void* User32_GetCursor() noexcept {
    using Fn = void* (*)();
    auto fn = Win32kProc<Fn>("MicaWin32kGetCursor");
    void* cursor = fn ? fn() : nullptr;
    micant::um::winuser::IconHandleTable::Lease lease{};
    return cursor && micant::um::winuser::ProcessIconHandles().acquireCursor(cursor, lease)
        ? cursor : nullptr;
}

int32_t User32_GetCursorInfo(void* information) noexcept {
    if (!information) return 0;
    struct CursorInfoLayout { uint32_t size, flags; void* cursor; WinNtPoint position; };
    auto* output = static_cast<CursorInfoLayout*>(information);
    if (output->size != sizeof(CursorInfoLayout)) return 0;
    using Fn = int32_t (*)(MicaWin32kCursorInfo*);
    auto fn = Win32kProc<Fn>("MicaWin32kGetCursorInfo");
    MicaWin32kCursorInfo state{};
    if (!fn || !fn(&state)) return 0;
    output->flags = state.flags;
    output->position = {state.position.x, state.position.y};
    output->cursor = reinterpret_cast<void*>(state.cursorHandle);
    if (output->cursor) {
        micant::um::winuser::IconHandleTable::Lease lease{};
        if (!micant::um::winuser::ProcessIconHandles().acquireCursor(output->cursor, lease))
            output->cursor = nullptr;
    }
    return 1;
}

void* User32_CreateCursor(void*, int32_t hotX, int32_t hotY, int32_t width,
                          int32_t height, const uint8_t* andPlane,
                          const uint8_t* xorPlane) noexcept {
    using namespace micant::um;
    if (!andPlane || !xorPlane || width <= 0 || height <= 0 || width > 64 || height > 64 ||
        hotX < 0 || hotY < 0 || hotX >= width || hotY >= height) return nullptr;
    const uint64_t planeStride = ((uint64_t(width) + 31) / 32) * 4;
    const uint64_t planeBytes = planeStride * static_cast<uint32_t>(height);
    const uint64_t imageBytes = uint64_t(width) * static_cast<uint32_t>(height) * 4;
    if (planeBytes > SIZE_MAX || imageBytes > 64u * 64u * 4u) return nullptr;
    imaging::ImageFrame frame{};
    frame.width = static_cast<uint32_t>(width);
    frame.height = static_cast<uint32_t>(height);
    frame.stride = static_cast<uint32_t>(width) * 4;
    frame.format = imaging::PixelFormat::Bgra8Premultiplied;
    const auto allocator = imaging::NativeImageAllocator();
    if (!frame.pixels.allocate(static_cast<size_t>(imageBytes), allocator)) return nullptr;
    for (int32_t y = 0; y < height; ++y) {
        const int32_t sourceY = height - 1 - y;
        for (int32_t x = 0; x < width; ++x) {
            const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
            const bool andBit = (andPlane[static_cast<size_t>(sourceY) * planeStride + x / 8] & mask) != 0;
            const bool xorBit = (xorPlane[static_cast<size_t>(sourceY) * planeStride + x / 8] & mask) != 0;
            if (andBit && xorBit) return nullptr; // destination inversion needs a different overlay op.
            const uint8_t alpha = andBit ? 0 : 255;
            const uint8_t color = xorBit ? 255 : 0;
            const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
            frame.pixels[pixel] = frame.pixels[pixel+1] = frame.pixels[pixel+2] = color;
            frame.pixels[pixel+3] = alpha;
        }
    }
    return winuser::ProcessIconHandles().createCursor(std::move(frame), allocator,
        static_cast<uint16_t>(hotX), static_cast<uint16_t>(hotY), false);
}

void* User32_CopyImage(void* image, uint32_t type, int32_t width, int32_t height,
                       uint32_t flags) noexcept {
    constexpr uint32_t ImageIcon = 1;
    if (!image || type != ImageIcon || width < 0 || height < 0 || flags != 0) return nullptr;
    micant::um::winuser::IconHandleTable::Lease lease{};
    if (!micant::um::winuser::ProcessIconHandles().acquire(image, lease) &&
        !micant::um::winuser::ProcessWindowIcons().acquire(image, lease)) return nullptr;
    const auto& view = lease.bitmap();
    const uint32_t targetWidth = width ? static_cast<uint32_t>(width) : view.width;
    const uint32_t targetHeight = height ? static_cast<uint32_t>(height) : view.height;
    return micant::um::winuser::ProcessIconHandles().copyScaled(
        lease, targetWidth, targetHeight, micant::um::imaging::NativeImageAllocator());
}

int32_t User32_DestroyIcon(void* icon) noexcept {
    return micant::um::winuser::ProcessIconHandles().destroy(icon) ? 1 : 0;
}

void* User32_LoadImageW(void* instance, const wchar_t* name, uint32_t type,
                        int32_t desiredWidth, int32_t desiredHeight,
                        uint32_t flags) noexcept {
    using namespace micant::um;
    constexpr uint32_t LoadFromFile = 0x0010u;
    constexpr uint32_t DefaultSize = 0x0040u;
    constexpr uint32_t Shared = 0x8000u;
    if (!name || (type != 1u && type != 2u) || desiredWidth < 0 || desiredHeight < 0)
        return nullptr;
    if (!(flags & LoadFromFile)) {
        if (type != 2u || (flags & ~(DefaultSize | Shared))) return nullptr;
        if (!instance) {
            if (!(flags & Shared) || reinterpret_cast<uintptr_t>(name) > UINT16_MAX)
                return nullptr;
            return User32_LoadCursorW(nullptr, name);
        }
        uint32_t width = static_cast<uint32_t>(desiredWidth);
        uint32_t height = static_cast<uint32_t>(desiredHeight);
        if (flags & DefaultSize) {
            if (!width) width = 32;
            if (!height) height = 32;
        }
        return loadPeCursor(instance, name, width, height, (flags & Shared) != 0);
    }
    if ((flags & ~(LoadFromFile | DefaultSize)) ||
        reinterpret_cast<uintptr_t>(name) <= UINT16_MAX || !*name) return nullptr;
    uint32_t width = static_cast<uint32_t>(desiredWidth);
    uint32_t height = static_cast<uint32_t>(desiredHeight);
    if (flags & DefaultSize) {
        if (!width) width = 32;
        if (!height) height = 32;
    }
    imaging::ImageLimits limits{};
    limits.maxEncodedBytes = 16ull * 1024 * 1024;
    limits.maxPixels = 16ull * 1024 * 1024;
    limits.maxDecodedBytes = 64ull * 1024 * 1024;
    limits.maxIntermediateBytes = 80ull * 1024 * 1024;
    if (type == 2u) {
        imaging::ImageBuffer encoded{};
        if (imaging::ReadIconSourceFile(name, encoded, imaging::NativeImageAllocator(), limits) !=
            imaging::ImageStatus::Success) return nullptr;
        const auto bytes = encoded.span();
        if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' &&
            bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'A' &&
            bytes[9] == 'C' && bytes[10] == 'O' && bytes[11] == 'N') {
            return loadAniCursor(bytes, imaging::NativeImageAllocator(), limits,
                false, width, height);
        }
        winuser::CursorImage cursor{};
        if (winuser::DecodeCursorImage(bytes, width, height, cursor,
                imaging::NativeImageAllocator(), limits) != imaging::ImageStatus::Success)
            return nullptr;
        uint32_t targetWidth = width ? width : cursor.image.width;
        uint32_t targetHeight = height ? height : cursor.image.height;
        if (!targetWidth || !targetHeight) return nullptr;
        uint16_t hotX = cursor.hotX, hotY = cursor.hotY;
        if (cursor.image.width != targetWidth || cursor.image.height != targetHeight) {
            const uint32_t oldWidth = cursor.image.width, oldHeight = cursor.image.height;
            imaging::ImageFrame scaled{};
            if (imaging::ScaleIconFrame(cursor.image, targetWidth, targetHeight, scaled,
                    imaging::NativeImageAllocator(), limits) != imaging::ImageStatus::Success)
                return nullptr;
            hotX = static_cast<uint16_t>(std::min<uint32_t>(targetWidth - 1,
                (uint32_t(hotX) * targetWidth + oldWidth / 2) / oldWidth));
            hotY = static_cast<uint16_t>(std::min<uint32_t>(targetHeight - 1,
                (uint32_t(hotY) * targetHeight + oldHeight / 2) / oldHeight));
            cursor.image = std::move(scaled);
        }
        return winuser::ProcessIconHandles().createCursor(std::move(cursor.image),
            imaging::NativeImageAllocator(), hotX, hotY, false);
    }
    imaging::ImageFrame frame{};
    const auto status = imaging::DecodeIconFile(name, frame,
        imaging::NativeImageAllocator(), width, height, limits);
    if (status != imaging::ImageStatus::Success) {
        const uint32_t error = status == imaging::ImageStatus::AccessDenied ? 5u :
            status == imaging::ImageStatus::IoFailure ? 2u :
            status == imaging::ImageStatus::OutOfMemory ? 8u :
            status == imaging::ImageStatus::UnsupportedFeature ? 50u :
            status == imaging::ImageStatus::UnknownFormat ? 1813u : 13u;
        setThreadLastError(error);
        return nullptr;
    }
    void* icon = winuser::ProcessIconHandles().create(std::move(frame),
        imaging::NativeImageAllocator(), false);
    if (!icon) setThreadLastError(8); // ERROR_NOT_ENOUGH_MEMORY.
    return icon;
}

void* User32_LoadCursorFromFileW(const wchar_t* path) noexcept {
    return User32_LoadImageW(nullptr, path, 2u, 0, 0, 0x0010u);
}

void* User32_LoadCursorFromFileA(const char* path) noexcept {
    if (!path || reinterpret_cast<uintptr_t>(path) <= UINT16_MAX) return nullptr;
    wchar_t wide[260]{};
    size_t length = 0;
    for (; path[length] && length + 1 < std::size(wide); ++length) {
        const uint8_t ch = static_cast<uint8_t>(path[length]);
        if (ch >= 0x80) return nullptr;
        wide[length] = static_cast<wchar_t>(ch);
    }
    if (!length || path[length]) return nullptr;
    return User32_LoadCursorFromFileW(wide);
}

void* User32_LoadImageA(void* instance, const char* name, uint32_t type,
                        int32_t desiredWidth, int32_t desiredHeight,
                        uint32_t flags) noexcept {
    if (!name || reinterpret_cast<uintptr_t>(name) <= UINT16_MAX) return nullptr;
    wchar_t path[260]{};
    size_t length = 0;
    for (; name[length] && length + 1 < 260; ++length) {
        const unsigned char byte = static_cast<unsigned char>(name[length]);
        // The freestanding User32 target has no active ANSI codepage service.
        // Accept ASCII paths losslessly; reject bytes requiring codepage mapping.
        if (byte >= 0x80) return nullptr;
        path[length] = static_cast<wchar_t>(byte);
    }
    if (name[length] || !length) return nullptr;
    return User32_LoadImageW(instance, path, type, desiredWidth, desiredHeight, flags);
}

void* User32_LoadIconW(void* /*instance*/, const wchar_t* /*name*/) noexcept {
    // Module resource lookup and stock artwork are not connected to the Ring 3
    // resource loader yet. Keep the API explicit and never manufacture HICON.
    return nullptr;
}

void* User32_LoadIconA(void* /*instance*/, const char* /*name*/) noexcept {
    return nullptr;
}

int32_t User32_DrawIconEx(void* destination, int32_t x, int32_t y, void* icon,
                          int32_t width, int32_t height, uint32_t animationStep,
                          void* brush, uint32_t flags) noexcept {
    using namespace micant::um;
    constexpr uint32_t DI_IMAGE = 0x0002u;
    constexpr uint32_t DI_NORMAL = 0x0003u;
    if (!destination || !icon || width < 0 || height < 0 || animationStep || brush ||
        (flags != DI_IMAGE && flags != DI_NORMAL)) return 0;
    winuser::IconHandleTable::Lease source{};
    if (!winuser::ProcessIconHandles().acquire(icon, source) &&
        !winuser::ProcessWindowIcons().acquire(icon, source)) return 0;
    const auto& sourceView = source.bitmap();
    const uint32_t targetWidth = width ? static_cast<uint32_t>(width) : sourceView.width;
    const uint32_t targetHeight = height ? static_cast<uint32_t>(height) : sourceView.height;
    if (!targetWidth || !targetHeight || targetWidth > 4096 || targetHeight > 4096) return 0;

    imaging::ImageFrame scaled{};
    std::span<const uint8_t> pixels = sourceView.pixels;
    uint32_t stride = sourceView.stride;
    if (targetWidth != sourceView.width || targetHeight != sourceView.height) {
        imaging::ImageFrame input{};
        input.width = sourceView.width; input.height = sourceView.height;
        input.stride = sourceView.stride;
        input.format = imaging::PixelFormat::Bgra8Premultiplied;
        // The immutable lease is the owner for this temporary non-owning view.
        // ScaleIconFrame accepts an owned ImageBuffer; copy only after the
        // bounded allocation succeeds so it never retains caller memory.
        if (!input.pixels.allocate(sourceView.pixels.size(), imaging::NativeImageAllocator())) return 0;
        std::memcpy(input.pixels.data(), sourceView.pixels.data(), sourceView.pixels.size());
        imaging::ImageLimits limits{};
        limits.maxPixels = 16ull * 1024 * 1024;
        limits.maxDecodedBytes = 64ull * 1024 * 1024;
        limits.maxIntermediateBytes = 64ull * 1024 * 1024;
        if (imaging::ScaleIconFrame(input, targetWidth, targetHeight, scaled,
                                    imaging::NativeImageAllocator(), limits) !=
            imaging::ImageStatus::Success) return 0;
        pixels = scaled.pixels.span();
        stride = scaled.stride;
    }

    using CreateDc = void* (*)(void*);
    using DeleteDc = int32_t (*)(void*);
    using CreateDib = void* (*)(void*, int32_t, int32_t, void**);
    using Select = void* (*)(void*, void*);
    using DeleteObject = int32_t (*)(void*);
    using BitBlt = int32_t (*)(void*, int32_t, int32_t, int32_t, int32_t,
                               void*, int32_t, int32_t, uint32_t);
    const auto createDc = Win32kProc<CreateDc>("MicaWin32kCreateCompatibleDC");
    const auto deleteDc = Win32kProc<DeleteDc>("MicaWin32kDeleteDC");
    const auto createDib = Win32kProc<CreateDib>("MicaWin32kCreateDibSection");
    const auto select = Win32kProc<Select>("MicaWin32kSelectObject");
    const auto deleteObject = Win32kProc<DeleteObject>("MicaWin32kDeleteObject");
    const auto bitBlt = Win32kProc<BitBlt>("MicaWin32kBitBlt");
    if (!createDc || !deleteDc || !createDib || !select || !deleteObject || !bitBlt) return 0;

    void* memoryDc = createDc(nullptr);
    if (!memoryDc) return 0;
    void* dibBits{};
    void* bitmap = createDib(nullptr, static_cast<int32_t>(targetWidth),
                             static_cast<int32_t>(targetHeight), &dibBits);
    if (!bitmap || !dibBits) { deleteDc(memoryDc); return 0; }
    const size_t rowBytes = static_cast<size_t>(targetWidth) * 4;
    for (uint32_t row = 0; row < targetHeight; ++row)
        std::memcpy(static_cast<uint8_t*>(dibBits) + static_cast<size_t>(row) * rowBytes,
                    pixels.data() + static_cast<size_t>(row) * stride, rowBytes);
    void* oldBitmap = select(memoryDc, bitmap);
    if (!oldBitmap) {
        deleteObject(bitmap); deleteDc(memoryDc); return 0;
    }
    const int32_t drawn = bitBlt(destination, x, y, static_cast<int32_t>(targetWidth),
        static_cast<int32_t>(targetHeight), memoryDc, 0, 0,
        MICANT_WIN32K_ROP_ALPHA_SOURCE_OVER);
    select(memoryDc, oldBitmap);
    const int32_t bitmapReleased = deleteObject(bitmap);
    const int32_t dcReleased = deleteDc(memoryDc);
    return drawn && bitmapReleased && dcReleased;
}

int32_t User32_DrawIcon(void* destination, int32_t x, int32_t y, void* icon) noexcept {
    return User32_DrawIconEx(destination, x, y, icon, 0, 0, 0, nullptr, 0x0003u);
}

// user32.dll implementations
uint16_t User32_RegisterClassExW(const void* lpWndClassEx) noexcept {
    if (!lpWndClassEx) return 0;
    const auto* wc=static_cast<const MicaWin32kWindowClassEx*>(lpWndClassEx);
    if (wc->cbSize < sizeof(MicaWin32kWindowClassEx) || !wc->className || !wc->wndProc) return 0;
    RegisteredClassIcons* iconSlot = nullptr;
    for (auto& entry : g_registeredClassIcons) if (!entry.name[0]) { iconSlot = &entry; break; }
    if (!iconSlot) return 0;
    size_t classNameLength = 0;
    while (classNameLength < MICANT_WIN32K_TEXT_CAPACITY && wc->className[classNameLength])
        ++classNameLength;
    if (classNameLength == MICANT_WIN32K_TEXT_CAPACITY) return 0;
    winuser::IconHandleTable::Lease classLargeLease{};
    winuser::IconHandleTable::Lease classSmallLease{};
    winuser::IconHandleTable::Lease classCursorLease{};
    if ((wc->icon && !winuser::ProcessIconHandles().acquire(wc->icon, classLargeLease)) ||
        (wc->smallIcon && !winuser::ProcessIconHandles().acquire(wc->smallIcon, classSmallLease)) ||
        (wc->cursor && !winuser::ProcessIconHandles().acquireCursor(wc->cursor, classCursorLease)))
        return 0;
    using Fn=uint16_t (*)(const wchar_t*,void*,int32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kRegisterClassW");
    const uint16_t atom = fn ? fn(wc->className,wc->wndProc,wc->windowExtra) : 0;
    if (atom && wc->className) {
        for (size_t i = 0; i < classNameLength; ++i) iconSlot->name[i] = wc->className[i];
        iconSlot->name[classNameLength] = 0;
        iconSlot->large = wc->icon; iconSlot->small = wc->smallIcon;
        iconSlot->cursor = wc->cursor;
        iconSlot->largeLease = std::move(classLargeLease);
        iconSlot->smallLease = std::move(classSmallLease);
        iconSlot->cursorLease = std::move(classCursorLease);
    }
    return atom;
}

bool isClassName(const wchar_t* candidate, const wchar_t* expected) noexcept {
    if (!candidate || !expected || reinterpret_cast<uintptr_t>(candidate) <= UINT16_MAX)
        return false;
    size_t index{};
    for (; expected[index]; ++index) {
        wchar_t actual = candidate[index];
        wchar_t wanted = expected[index];
        if (actual >= L'a' && actual <= L'z') actual -= L'a' - L'A';
        if (wanted >= L'a' && wanted <= L'z') wanted -= L'a' - L'A';
        if (actual != wanted) return false;
    }
    return candidate[index] == L'\0';
}

void registerBuiltinClass(const wchar_t* name, void* wndProc) noexcept {
    using RegisterFn = uint16_t (*)(const wchar_t*, void*, int32_t);
    const auto registerClass = Win32kProc<RegisterFn>("MicaWin32kRegisterClassW");
    // Win32k owns class lifetime per process. Attempt registration in that
    // namespace on every CreateWindowExW path instead of caching a DLL-global
    // success bit that can belong to a different process. A duplicate is
    // harmless here: CreateWindowExW below validates the process-owned class.
    if (registerClass) (void)registerClass(name, wndProc, 0);
}

void* User32_CreateWindowExW(
    uint32_t dwExStyle,
    const wchar_t* lpClassName,
    const wchar_t* lpWindowName,
    uint32_t dwStyle,
    int32_t X,
    int32_t Y,
    int32_t nWidth,
    int32_t nHeight,
    void* hWndParent,
    void* hMenu,
    void* hInstance,
    void* lpParam) noexcept {
    if (isClassName(lpClassName, L"BUTTON")) {
        const uint32_t type = dwStyle & kBsTypeMask;
        if (!isSupportedButtonType(type)) {
            setThreadLastError(87); // ERROR_INVALID_PARAMETER: unsupported BS_* type.
            return nullptr;
        }
        registerBuiltinClass(L"BUTTON", reinterpret_cast<void*>(&User32_ButtonWndProc));
    } else if (isClassName(lpClassName, L"STATIC")) {
        registerBuiltinClass(L"STATIC", reinterpret_cast<void*>(&User32_StaticWndProc));
    } else if (isClassName(lpClassName, L"EDIT")) {
        const uint32_t editStyles = dwStyle & kEsControlMask;
        if (editStyles & kEsUnsupportedMask) {
            setThreadLastError(87); // Unsupported ES_* styles fail explicitly.
            return nullptr;
        }
        registerBuiltinClass(L"EDIT", reinterpret_cast<void*>(&User32_EditWndProc));
    } else if (isClassName(lpClassName, L"SCROLLBAR")) {
        if (dwStyle & kSbsUnsupported) {
            setThreadLastError(87); // SBS_SIZEBOX/SBS_SIZEGRIP are not implemented.
            return nullptr;
        }
        registerBuiltinClass(L"SCROLLBAR", reinterpret_cast<void*>(&User32_ScrollBarWndProc));
    }
    using Fn = void* (*)(uint32_t,const wchar_t*,const wchar_t*,uint32_t,int32_t,int32_t,int32_t,int32_t,void*,void*,void*,void*);
    auto fn=Win32kProc<Fn>("MicaWin32kCreateWindowExW");
    void* hwnd = fn ? fn(dwExStyle,lpClassName,lpWindowName,dwStyle,X,Y,nWidth,nHeight,hWndParent,hMenu,hInstance,lpParam) : nullptr;
    if (hwnd) {
        if (isClassName(lpClassName, L"STATIC")) {
            bool initialized{};
            {
                ButtonLockGuard guard(g_buttonLock);
                for (const auto& entry : g_statics)
                    if (entry.hwnd == hwnd) { initialized = true; break; }
            }
            if (!initialized) {
                MicaWin32kCreateStruct create{lpParam, hInstance, hMenu,
                    hWndParent, nHeight, nWidth, Y, X, static_cast<int32_t>(dwStyle),
                    lpWindowName, lpClassName, dwExStyle};
                (void)User32_StaticWndProc(hwnd, 0x0081u /* WM_NCCREATE */, 1,
                    reinterpret_cast<intptr_t>(&create));
            }
        }
        constexpr uint32_t kWsCaption = 0x00C00000u;
        if (!hWndParent && (dwStyle & kWsCaption) == kWsCaption)
            (void)publishCaptionTitleSurface(hwnd, lpWindowName);
        if (auto* icons = findRegisteredClassIcons(lpClassName)) {
            for (auto& entry : g_registeredWindowClasses) {
                if (entry.window) continue;
                entry = {hwnd, icons};
                break;
            }
            const auto applyDefault = [&](uint32_t type, void* icon) noexcept {
                if (!icon) return;
                uint64_t previous{};
                if (!setWindowIconPixels(hwnd, type, icon, &previous)) return;
                (void)winuser::ProcessWindowIcons().set(hwnd, type, icon);
                if (winuser::ProcessWindowIcons().get(hwnd, type) != icon) {
                    uint64_t ignored{};
                    (void)setWindowIconPixels(hwnd, type,
                        reinterpret_cast<void*>(previous), &ignored);
                }
            };
            applyDefault(1, icons->large);
            applyDefault(0, icons->small);
        }
    }
    return hwnd;
}

intptr_t User32_SendMessageW(void* hWnd, uint32_t message, uintptr_t wParam,
                              intptr_t lParam) noexcept;
int32_t User32_EnumChildWindows(void* parent, void* callback,
                                intptr_t parameter) noexcept;

struct DestroyChildList {
    void* windows[512]{};
    size_t count{};
    bool overflow{};
};

int32_t collectDestroyChild(void* hwnd, intptr_t parameter) noexcept {
    auto* list = reinterpret_cast<DestroyChildList*>(parameter);
    if (!list || list->count == std::size(list->windows)) {
        if (list) list->overflow = true;
        return 0;
    }
    list->windows[list->count++] = hwnd;
    return 1;
}

int32_t User32_DestroyWindow(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDestroyWindow");
    if (!fn) return 0;
    // Win32k owns the HWND tree, but its children have User32 window
    // procedures and per-control state. Deliver their teardown callbacks in
    // descendant-first order while every HWND is still valid, so controls
    // release their Ring 3 state before the tree is removed.
    DestroyChildList children{};
    if (!User32_EnumChildWindows(hWnd,
            reinterpret_cast<void*>(&collectDestroyChild),
            reinterpret_cast<intptr_t>(&children)) || children.overflow)
        return 0;
    while (children.count) {
        void* child = children.windows[--children.count];
        (void)User32_SendMessageW(child, 0x0002u, 0, 0); // WM_DESTROY
        (void)User32_SendMessageW(child, 0x0082u, 0, 0); // WM_NCDESTROY
    }
    (void)User32_SendMessageW(hWnd, 0x0002u, 0, 0); // WM_DESTROY
    (void)User32_SendMessageW(hWnd, 0x0082u, 0, 0); // WM_NCDESTROY
    const int32_t destroyed = fn(hWnd);
    if (destroyed) {
        MicaUser32MenuWindowDestroyed(hWnd);
        {
            ButtonLockGuard guard(g_buttonLock);
            for (auto& state : g_windowScrollBars) {
                if (state.hwnd == hWnd) {
                    state = {};
                    break;
                }
            }
        }
        winuser::ProcessWindowIcons().remove(hWnd);
        if (auto* entry = findRegisteredWindowClass(hWnd)) *entry = {};
    }
    return destroyed;
}

uintptr_t User32_GetClassLongPtrW(void* hwnd, int32_t index) noexcept {
    auto* windowClass = findRegisteredWindowClass(hwnd);
    if (!windowClass || !windowClass->icons) return 0;
    if (index == -14) return reinterpret_cast<uintptr_t>(windowClass->icons->large);
    if (index == -34) return reinterpret_cast<uintptr_t>(windowClass->icons->small);
    return 0;
}

uintptr_t User32_SetClassLongPtrW(void* hwnd, int32_t index, uintptr_t value) noexcept {
    auto* windowClass = findRegisteredWindowClass(hwnd);
    if (!windowClass || !windowClass->icons || (index != -14 && index != -34)) return 0;
    auto& icons = *windowClass->icons;
    void*& handle = index == -14 ? icons.large : icons.small;
    auto& lease = index == -14 ? icons.largeLease : icons.smallLease;
    void* replacement = reinterpret_cast<void*>(value);
    winuser::IconHandleTable::Lease replacementLease{};
    if (replacement && !winuser::ProcessIconHandles().acquire(replacement, replacementLease)) return 0;
    void* previous = handle;
    const uint32_t type = index == -14 ? 1u : 0u;
    void* changedWindows[128]{};
    void* previousIcons[128]{};
    winuser::IconHandleTable::Lease previousLeases[128]{};
    size_t changedCount = 0;
    for (const auto& window : g_registeredWindowClasses) {
        if (window.icons != windowClass->icons || !window.window) continue;
        void* oldIcon = winuser::ProcessWindowIcons().get(window.window, type);
        if (oldIcon && !winuser::ProcessIconHandles().acquire(oldIcon,
                                                               previousLeases[changedCount]))
            return 0;
        uint64_t ignored{};
        const bool applied = setWindowIconPixels(window.window, type, replacement, &ignored);
        const bool recorded = applied &&
            winuser::ProcessWindowIcons().set(window.window, type, replacement);
        if (!recorded) {
            if (applied) {
                uint64_t rollback{};
                (void)setWindowIconPixels(window.window, type, oldIcon, &rollback);
            }
            while (changedCount) {
                --changedCount;
                uint64_t rollback{};
                (void)setWindowIconPixels(changedWindows[changedCount], type,
                    previousIcons[changedCount], &rollback);
                (void)winuser::ProcessWindowIcons().set(changedWindows[changedCount],
                    type, previousIcons[changedCount]);
            }
            return 0;
        }
        changedWindows[changedCount] = window.window;
        previousIcons[changedCount] = oldIcon;
        ++changedCount;
    }
    handle = replacement;
    lease = std::move(replacementLease);
    return reinterpret_cast<uintptr_t>(previous);
}

int32_t User32_ShowWindow(void* hWnd, int32_t nCmdShow) noexcept {
    using Fn=int32_t (*)(void*,int32_t); auto fn=Win32kProc<Fn>("MicaWin32kShowWindow"); return fn ? fn(hWnd,nCmdShow) : 0;
}

int32_t User32_UpdateWindow(void* hWnd) noexcept {
    if (!hWnd) return 0;
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kSendMessageW");
    if (!fn) return 0;
    fn(hWnd,WM_PAINT_OFFICIAL,0,0);
    return 1;
}

int32_t User32_InvalidateRect(void* hWnd, const WinNtRect*, int32_t) noexcept {
    if (!hWnd) return 0;
    using Fn=int32_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kPostMessageW");
    return fn ? fn(hWnd, WM_PAINT_OFFICIAL, 0, 0) : 0;
}

void* User32_BeginPaint(void* hWnd, WinNtPaintStruct* paint) noexcept {
    if (!hWnd || !paint) return nullptr;
    using GetDcFn=void* (*)(void*);
    using GetRectFn=int32_t (*)(void*,MicaWin32kRect*);
    using ReleaseFn=int32_t (*)(void*,void*);
    auto getDc=Win32kProc<GetDcFn>("MicaWin32kGetDC");
    auto getRect=Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
    auto release=Win32kProc<ReleaseFn>("MicaWin32kReleaseDC");
    if (!getDc || !getRect || !release) return nullptr;
    MicaWin32kRect rect{};
    void* dc=getDc(hWnd);
    if (!dc) return nullptr;
    if (!getRect(hWnd,&rect)) {
        release(hWnd,dc);
        return nullptr;
    }
    paint->hdc=dc;
    paint->fErase=0;
    paint->rcPaint={rect.left,rect.top,rect.right,rect.bottom};
    paint->fRestore=0;
    paint->fIncUpdate=0;
    std::memset(paint->rgbReserved,0,sizeof(paint->rgbReserved));
    return dc;
}

int32_t User32_EndPaint(void* hWnd, const WinNtPaintStruct* paint) noexcept {
    if (!hWnd || !paint || !paint->hdc) return 0;
    using PresentFn=int32_t (*)(void*, const MicaWin32kRect*);
    using ReleaseFn=int32_t (*)(void*,void*);
    auto present=Win32kProc<PresentFn>("MicaWin32kPresentDCRect");
    auto release=Win32kProc<ReleaseFn>("MicaWin32kReleaseDC");
    const MicaWin32kRect dirtyRect{paint->rcPaint.left, paint->rcPaint.top,
        paint->rcPaint.right, paint->rcPaint.bottom};
    drawAttachedWindowScrollBars(hWnd, paint->hdc);
    const int32_t presented = present ? present(paint->hdc, &dirtyRect) : 0;
    const int32_t released = release ? release(hWnd,paint->hdc) : 0;
    if (released) MicaGdi32ForgetDcState(paint->hdc);
    return presented && released;
}

namespace {
uint32_t buttonStateFor(void* hwnd) noexcept {
    ButtonLockGuard guard(g_buttonLock);
    const auto* button = findButton(hwnd);
    return button ? button->state : kButtonDisabled;
}

void invalidateButton(void* hwnd) noexcept {
    using PostFn = int32_t (*)(void*, uint32_t, uintptr_t, intptr_t);
    const auto post = Win32kProc<PostFn>("MicaWin32kPostMessageW");
    if (post) (void)post(hwnd, kWmPaint, 0, 0);
}

size_t selectAutoRadioGroupLocked(ButtonState& selected,
                                  void** changedWindows) noexcept {
    if ((selected.style & kBsTypeMask) != kBsAutoRadioButton) return 0;
    size_t changedCount{};
    for (auto& candidate : g_buttons) {
        if (!candidate.hwnd || &candidate == &selected ||
            (candidate.style & kBsTypeMask) != kBsAutoRadioButton ||
            candidate.parent != selected.parent ||
            candidate.radioGroup != selected.radioGroup || !candidate.checkState)
            continue;
        candidate.checkState = 0;
        changedWindows[changedCount++] = candidate.hwnd;
    }
    selected.checkState = 1;
    return changedCount;
}

void leaveHotButton(void* except) noexcept {
    void* previous{};
    {
        ButtonLockGuard guard(g_buttonLock);
        if (!g_hotButton || g_hotButton == except) return;
        previous = g_hotButton;
        if (auto* button = findButton(previous)) {
            button->state &= ~kButtonHot;
            if (!button->armed) button->state &= ~kButtonPressed;
        }
        g_hotButton = nullptr;
    }
    invalidateButton(previous);
}

bool buttonPointInside(void* hwnd, intptr_t lParam) noexcept {
    using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
    const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
    MicaWin32kRect client{};
    if (!getRect || !getRect(hwnd, &client)) return false;
    const auto packed = static_cast<uintptr_t>(lParam);
    const int32_t x = static_cast<int16_t>(packed & 0xFFFFu);
    const int32_t y = static_cast<int16_t>((packed >> 16) & 0xFFFFu);
    return x >= client.left && y >= client.top &&
        x < client.right && y < client.bottom;
}

void notifyButtonClicked(void* hwnd) noexcept {
    ButtonState copy{};
    {
        ButtonLockGuard guard(g_buttonLock);
        const auto* button = findButton(hwnd);
        if (!button || (button->state & kButtonDisabled)) return;
        copy = *button;
    }
    if (copy.parent) {
        const uintptr_t command = (copy.id & 0xFFFFu) | (kBnClicked << 16);
        const bool traceCalcButton = copy.id == 1036u || copy.id == 1052u ||
            copy.id == 1057u || copy.id == 1071u || copy.id == 1073u;
        using TraceFn = void (*)(uint32_t, uint32_t, uint32_t);
        const auto trace = traceCalcButton
            ? Win32kProc<TraceFn>("MicaWin32kDebugInputTrace") : nullptr;
        if (trace) trace(3, static_cast<uint32_t>(copy.id), 0);
        const intptr_t result = User32_SendMessageW(copy.parent, kWmCommand,
            command, reinterpret_cast<intptr_t>(hwnd));
        if (trace) trace(4, static_cast<uint32_t>(copy.id),
                         static_cast<uint32_t>(result));
    }
}

EditState* findEdit(void* hwnd) noexcept;

void notifyEditChanged(void* hwnd) noexcept {
    EditState copy{};
    {
        ButtonLockGuard guard(g_buttonLock);
        const auto* edit = findEdit(hwnd);
        if (!edit) return;
        copy = *edit;
    }
    if (copy.parent) {
        const uintptr_t command = (copy.id & 0xFFFFu) | (kEnChange << 16);
        (void)User32_SendMessageW(copy.parent, kWmCommand, command,
                                  reinterpret_cast<intptr_t>(hwnd));
    }
}

uint32_t colorRef(uint32_t argb) noexcept {
    return ((argb >> 16) & 0xFFu) | (argb & 0x0000FF00u) |
           ((argb & 0xFFu) << 16);
}

struct ButtonDibInfo {
    uint32_t size{40};
    int32_t width{}, height{};
    uint16_t planes{1}, bitCount{32};
    uint32_t compression{kDibRgb}, imageSize{};
    int32_t xPixelsPerMeter{}, yPixelsPerMeter{};
    uint32_t colorsUsed{}, colorsImportant{}, colorTable{};
};
struct ButtonTextExtent { int32_t cx{}, cy{}; };
struct Win32DrawItemStruct64 {
    uint32_t controlType{}, controlId{}, itemId{}, action{}, state{}, padding{};
    void* window{};
    void* dc{};
    WinNtRect rect{};
    uintptr_t itemData{};
};
static_assert(sizeof(Win32DrawItemStruct64) == 64);

bool fillButtonRect(void* dc, const WinNtRect& rect, uint32_t color) noexcept {
    if (auto* brush = CreateSolidBrush(colorRef(color))) {
        const int32_t result = FillRect(dc, &rect, brush);
        (void)DeleteObject(brush);
        if (result) return true;
    }
    using FillFn = int32_t (*)(void*, const MicaWin32kRect*, uint32_t);
    const auto fill = Win32kProc<FillFn>("MicaWin32kFillRect");
    return fill && fill(dc, reinterpret_cast<const MicaWin32kRect*>(&rect),
                        colorRef(color));
}

EditState* findEdit(void* hwnd) noexcept {
    for (auto& edit : g_edits) if (edit.hwnd == hwnd && hwnd) return &edit;
    return nullptr;
}

ScrollBarState* findScrollBar(void* hwnd) noexcept {
    for (auto& bar : g_scrollBars) if (bar.hwnd == hwnd && hwnd) return &bar;
    return nullptr;
}

int32_t scrollBarMaximumPosition(const ScrollBarState& bar) noexcept {
    const int64_t range = int64_t(bar.maximum) - bar.minimum + 1;
    const int64_t visible = std::min<int64_t>(bar.page, std::max<int64_t>(0, range));
    const int64_t maximum = int64_t(bar.maximum) - std::max<int64_t>(0, visible - 1);
    return static_cast<int32_t>(std::clamp<int64_t>(maximum, bar.minimum, INT32_MAX));
}

void clampScrollBar(ScrollBarState& bar) noexcept {
    if (bar.maximum < bar.minimum) bar.maximum = bar.minimum;
    bar.position = std::clamp(bar.position, bar.minimum, scrollBarMaximumPosition(bar));
    bar.trackPosition = std::clamp(bar.trackPosition, bar.minimum,
                                   scrollBarMaximumPosition(bar));
}

struct ScrollBarGeometry {
    MicaWin32kRect firstArrow{}, secondArrow{}, track{}, thumb{};
    int32_t trackStart{}, trackLength{}, thumbLength{};
    bool vertical{};
};

ScrollBarGeometry scrollBarGeometry(const ScrollBarState& bar,
                                    int32_t width, int32_t height) noexcept {
    ScrollBarGeometry geometry{};
    geometry.vertical = (bar.style & kSbsVert) != 0;
    const int32_t axis = geometry.vertical ? height : width;
    const int32_t cross = geometry.vertical ? width : height;
    if (axis <= 0 || cross <= 0) return geometry;
    const int32_t arrow = std::min(std::max(1, cross), std::max(1, axis / 2));
    if (geometry.vertical) {
        geometry.firstArrow = {0, 0, width, arrow};
        geometry.secondArrow = {0, height - arrow, width, height};
        geometry.track = {0, arrow, width, height - arrow};
    } else {
        geometry.firstArrow = {0, 0, arrow, height};
        geometry.secondArrow = {width - arrow, 0, width, height};
        geometry.track = {arrow, 0, width - arrow, height};
    }
    geometry.trackStart = arrow;
    geometry.trackLength = std::max(0, axis - 2 * arrow);
    const int64_t range = std::max<int64_t>(1, int64_t(bar.maximum) - bar.minimum + 1);
    const int64_t proportional = geometry.trackLength *
        std::min<int64_t>(bar.page, range) / range;
    geometry.thumbLength = std::clamp<int32_t>(static_cast<int32_t>(proportional),
        std::min(10, geometry.trackLength), geometry.trackLength);
    const int32_t travel = std::max(0, geometry.trackLength - geometry.thumbLength);
    const int64_t scrollRange = int64_t(scrollBarMaximumPosition(bar)) - bar.minimum;
    const int64_t offset = scrollRange > 0
        ? (int64_t(bar.position) - bar.minimum) * travel / scrollRange : 0;
    const int32_t thumbStart = geometry.trackStart + static_cast<int32_t>(offset);
    if (geometry.vertical)
        geometry.thumb = {0, thumbStart, width, thumbStart + geometry.thumbLength};
    else
        geometry.thumb = {thumbStart, 0, thumbStart + geometry.thumbLength, height};
    return geometry;
}

uint32_t scrollBarPartAt(const ScrollBarState& bar, int32_t x, int32_t y,
                         int32_t width, int32_t height) noexcept {
    const auto g = scrollBarGeometry(bar, width, height);
    const auto contains = [x, y](const MicaWin32kRect& r) noexcept {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    };
    if (contains(g.firstArrow)) return g.vertical ? 1u : 3u;
    if (contains(g.secondArrow)) return g.vertical ? 2u : 4u;
    if (contains(g.thumb)) return g.vertical ? 6u : 5u;
    if (contains(g.track)) {
        const int32_t axis = g.vertical ? y : x;
        const int32_t thumbAxis = g.vertical ? g.thumb.top : g.thumb.left;
        return axis < thumbAxis ? 7u : 8u;
    }
    return 0;
}

uint32_t scrollArrowState(const ScrollBarState& bar, uint32_t part) noexcept {
    uint32_t base = 1;
    if (bar.style & kSbsVert) base = part == 2 ? 5 : 1;
    else base = part == 3 ? 9 : 13;
    const bool disabled = (bar.state & kButtonDisabled) ||
        (bar.disabledArrows & ((part == 1 || part == 3) ? 1u : 2u));
    if (disabled) return base + 3;
    if (bar.pressedPart == part) return base + 2;
    if (bar.hotPart == part) return base + 1;
    return base;
}

void invalidateScrollBar(void* hwnd) noexcept {
    using PostFn = int32_t (*)(void*, uint32_t, uintptr_t, intptr_t);
    const auto post = Win32kProc<PostFn>("MicaWin32kPostMessageW");
    if (post) (void)post(hwnd, kWmPaint, 0, 0);
}

void notifyScrollBar(const ScrollBarState& bar, uint32_t code) noexcept {
    if (!bar.parent) return;
    const uint32_t packed = (static_cast<uint32_t>(bar.trackPosition) & 0xFFFFu) << 16 |
                            (code & 0xFFFFu);
    (void)User32_SendMessageW(bar.parent, (bar.style & kSbsVert) ? 0x0115u : 0x0114u,
                              packed, reinterpret_cast<intptr_t>(bar.hwnd));
}

bool drawScrollBarPart(void* theme, void* dc, int32_t part, int32_t state,
                       const MicaWin32kRect& rect, uint32_t fallback) noexcept {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return true;
    if (part == 1) {
        const uint32_t groupState = (state - 1) % 4;
        fallback = groupState == 1 ? kColorButtonHot :
            groupState == 2 ? kColorButtonPressed :
            groupState == 3 ? kColorButtonDisabled : kColorButton;
    } else if (part == 2 || part == 3) {
        fallback = state == 2 ? 0xFFB6C8E0u :
            state == 3 ? 0xFF9FB8D5u :
            state == 4 ? 0xFFE4E4E4u : 0xFFD1D1D1u;
    }
    const WinNtRect fill{rect.left, rect.top, rect.right, rect.bottom};
    const bool baseDrawn = fillButtonRect(dc, fill, fallback);
    const bool themedDrawn = theme &&
        DrawThemeBackground(theme, dc, part, state, &rect, nullptr) == 0;
    return baseDrawn || themedDrawn;
}

bool drawScrollBar(void* hwnd, const ScrollBarState& bar, void* dc,
                   int32_t width, int32_t height, int32_t offsetX = 0,
                   int32_t offsetY = 0) noexcept {
    if (!dc || width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    auto g = scrollBarGeometry(bar, width, height);
    auto offset = [=](MicaWin32kRect& r) {
        r.left += offsetX; r.right += offsetX;
        r.top += offsetY; r.bottom += offsetY;
    };
    offset(g.firstArrow); offset(g.secondArrow); offset(g.track); offset(g.thumb);
    void* theme = OpenThemeData(hwnd, L"SCROLLBAR");
    const uint32_t thumbPart = g.vertical ? 3u : 2u;
    MicaWin32kRect before = g.track;
    MicaWin32kRect after = g.track;
    if (g.vertical) {
        before.bottom = std::clamp(g.thumb.top, before.top, before.bottom);
        after.top = std::clamp(g.thumb.bottom, after.top, after.bottom);
    } else {
        before.right = std::clamp(g.thumb.left, before.left, before.right);
        after.left = std::clamp(g.thumb.right, after.left, after.right);
    }
    bool drawn = drawScrollBarPart(theme, dc, g.vertical ? 7 : 6, 1,
                                    before, 0xFFF5F5F5u);
    drawn &= drawScrollBarPart(theme, dc, g.vertical ? 5 : 4, 1,
                               after, 0xFFF5F5F5u);
    drawn &= drawScrollBarPart(theme, dc, 1, scrollArrowState(bar,
        g.vertical ? 1u : 3u), g.firstArrow, kColorButton);
    drawn &= drawScrollBarPart(theme, dc, 1, scrollArrowState(bar,
        g.vertical ? 2u : 4u), g.secondArrow, kColorButton);
    const int32_t thumbState = (bar.state & kButtonDisabled) ? 4 :
        bar.pressedPart == (g.vertical ? 6u : 5u) ? 3 :
        bar.hotPart == (g.vertical ? 6u : 5u) ? 2 : 1;
    drawn &= drawScrollBarPart(theme, dc, thumbPart, thumbState, g.thumb, 0xFFD1D1D1u);
    if (theme) (void)CloseThemeData(theme);
    return drawn;
}

void copyScrollInfo(const ScrollBarState& bar, ScrollInfoW& info) noexcept {
    const uint32_t mask = info.fMask;
    if (mask & kSifRange) { info.nMin = bar.minimum; info.nMax = bar.maximum; }
    if (mask & kSifPage) info.nPage = bar.page;
    if (mask & kSifPos) info.nPos = bar.position;
    if (mask & kSifTrackPos) info.nTrackPos = bar.trackPosition;
}

WindowScrollState* findWindowScrollState(void* hwnd) noexcept {
    for (auto& state : g_windowScrollBars)
        if (hwnd && state.hwnd == hwnd) return &state;
    return nullptr;
}

WindowScrollState* acquireWindowScrollState(void* hwnd) noexcept {
    if (auto* state = findWindowScrollState(hwnd)) return state;
    for (auto& state : g_windowScrollBars) {
        if (state.hwnd) continue;
        state = {};
        state.hwnd = hwnd;
        return &state;
    }
    return nullptr;
}

bool isValidScrollWindow(void* hwnd) noexcept {
    if (!hwnd) return false;
    MicaWin32kRect client{};
    using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
    const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
    return getRect && getRect(hwnd, &client) != 0;
}

int32_t windowScrollMaximumPosition(const WindowScrollInfo& bar) noexcept {
    const int64_t range = int64_t(bar.maximum) - bar.minimum + 1;
    const int64_t visible = std::min<int64_t>(bar.page, std::max<int64_t>(0, range));
    const int64_t maximum = int64_t(bar.maximum) - std::max<int64_t>(0, visible - 1);
    return static_cast<int32_t>(std::clamp<int64_t>(maximum, bar.minimum, INT32_MAX));
}

void clampWindowScrollInfo(WindowScrollInfo& bar) noexcept {
    if (bar.maximum < bar.minimum) bar.maximum = bar.minimum;
    bar.position = std::clamp(bar.position, bar.minimum,
                              windowScrollMaximumPosition(bar));
    bar.trackPosition = std::clamp(bar.trackPosition, bar.minimum,
                                   windowScrollMaximumPosition(bar));
}

void setWindowScrollInfo(WindowScrollInfo& bar, const ScrollInfoW& info) noexcept {
    if (info.fMask & kSifRange) {
        bar.minimum = info.nMin;
        bar.maximum = info.nMax;
    }
    if (info.fMask & kSifPage) bar.page = info.nPage;
    if (info.fMask & kSifPos) bar.position = info.nPos;
    if (info.fMask & kSifDisableNoScroll) {
        bar.disabledArrows = windowScrollMaximumPosition(bar) <= bar.minimum ? 3u : 0u;
    }
    clampWindowScrollInfo(bar);
}

void copyWindowScrollInfo(const WindowScrollInfo& bar, ScrollInfoW& info) noexcept {
    const uint32_t mask = info.fMask;
    if (mask & kSifRange) { info.nMin = bar.minimum; info.nMax = bar.maximum; }
    if (mask & kSifPage) info.nPage = bar.page;
    if (mask & kSifPos) info.nPos = bar.position;
    if (mask & kSifTrackPos) info.nTrackPos = bar.trackPosition;
}

constexpr uint32_t kWsHScroll = 0x00100000u;
constexpr uint32_t kWsVScroll = 0x00200000u;
constexpr uint32_t kSbHorz = 0u;
constexpr uint32_t kSbVert = 1u;

uint32_t windowStyle(void* hwnd) noexcept {
    using Fn = intptr_t (*)(void*, int32_t);
    const auto get = Win32kProc<Fn>("MicaWin32kGetWindowLongPtrW");
    return get ? static_cast<uint32_t>(get(hwnd, -16)) : 0;
}

ScrollBarState attachedBarSnapshot(void* hwnd, uint32_t index,
                                   const WindowScrollInfo& info,
                                   const WindowScrollState* owner = nullptr) noexcept {
    ScrollBarState bar{};
    bar.hwnd = hwnd;
    bar.style = index == kSbVert ? kSbsVert : 0;
    bar.minimum = info.minimum;
    bar.maximum = info.maximum;
    bar.position = info.position;
    bar.trackPosition = info.trackPosition;
    bar.page = info.page;
    bar.disabledArrows = info.disabledArrows;
    if (owner) {
        if (owner->hotBar == index) bar.hotPart = owner->hotPart;
        if (owner->pressedBar == index) bar.pressedPart = owner->pressedPart;
    }
    return bar;
}

void drawAttachedWindowScrollBarsImpl(void* hwnd, void* dc) noexcept {
    if (!hwnd || !dc) return;
    MicaWin32kRect client{};
    using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
    const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
    if (!getRect || !getRect(hwnd, &client)) return;
    const int32_t width = client.right - client.left;
    const int32_t height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return;
    const uint32_t style = windowStyle(hwnd);
    auto* state = findWindowScrollState(hwnd);
    const WindowScrollInfo defaults{};
    if (style & kWsVScroll) {
        const auto bar = attachedBarSnapshot(hwnd, kSbVert,
            state ? state->bars[kSbVert] : defaults, state);
        (void)drawScrollBar(hwnd, bar, dc, 16, height, width - 16, 0);
    }
    if (style & kWsHScroll) {
        const auto bar = attachedBarSnapshot(hwnd, kSbHorz,
            state ? state->bars[kSbHorz] : defaults, state);
        const int32_t barWidth = (style & kWsVScroll) ? std::max(0, width - 16) : width;
        (void)drawScrollBar(hwnd, bar, dc, barWidth, 16, 0, height - 16);
    }
}

bool dispatchAttachedScrollInput(const WinNtMsg& message) noexcept {
    constexpr uint32_t kWmMouseMove = 0x0200u;
    constexpr uint32_t kWmLButtonDown = 0x0201u;
    constexpr uint32_t kWmLButtonUp = 0x0202u;
    constexpr uint32_t kWmCaptureChanged = 0x0215u;
    if (message.message == kWmCaptureChanged) {
        auto* state = findWindowScrollState(message.hwnd);
        if (state && state->pressedBar != 0xFFFFFFFFu) {
            state->pressedBar = 0xFFFFFFFFu;
            state->pressedPart = 0;
            invalidateScrollBar(message.hwnd);
        }
        return false;
    }
    if (message.message != kWmMouseMove && message.message != kWmLButtonDown &&
        message.message != kWmLButtonUp) return false;
    const uint32_t style = windowStyle(message.hwnd);
    if (!(style & (kWsHScroll | kWsVScroll))) return false;
    MicaWin32kRect client{};
    using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
    const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
    if (!getRect || !getRect(message.hwnd, &client)) return false;
    const int32_t width = client.right - client.left, height = client.bottom - client.top;
    const int32_t x = static_cast<int16_t>(message.lParam & 0xFFFF);
    const int32_t y = static_cast<int16_t>((static_cast<uintptr_t>(message.lParam) >> 16) & 0xFFFF);
    auto* state = acquireWindowScrollState(message.hwnd);
    if (!state) return false;
    if (message.message == kWmMouseMove && state->pressedBar == 0xFFFFFFFFu) {
        uint32_t hotBar = 0xFFFFFFFFu;
        if ((style & kWsVScroll) && x >= width - 16 && y >= 0 && y < height) hotBar = kSbVert;
        else if ((style & kWsHScroll) && y >= height - 16 && x >= 0 &&
                 x < width - ((style & kWsVScroll) ? 16 : 0)) hotBar = kSbHorz;
        uint32_t hotPart{};
        if (hotBar != 0xFFFFFFFFu) {
            const auto bar = attachedBarSnapshot(message.hwnd, hotBar,
                state->bars[hotBar], state);
            const int32_t barWidth = hotBar == kSbVert ? 16 :
                std::max(0, width - ((style & kWsVScroll) ? 16 : 0));
            hotPart = scrollBarPartAt(bar,
                hotBar == kSbVert ? x - (width - 16) : x,
                hotBar == kSbVert ? y : y - (height - 16),
                barWidth, hotBar == kSbVert ? height : 16);
        }
        if (state->hotBar != hotBar || state->hotPart != hotPart) {
            state->hotBar = hotBar;
            state->hotPart = hotPart;
            invalidateScrollBar(message.hwnd);
        }
        return false;
    }
    uint32_t index = state->pressedBar;
    if (message.message == kWmLButtonDown) {
        index = 0xFFFFFFFFu;
        if ((style & kWsVScroll) && x >= width - 16 && y >= 0 && y < height) index = kSbVert;
        else if ((style & kWsHScroll) && y >= height - 16 && x >= 0 &&
                 x < width - ((style & kWsVScroll) ? 16 : 0)) index = kSbHorz;
        if (index == 0xFFFFFFFFu) return false;
        const auto& info = state->bars[index];
        auto bar = attachedBarSnapshot(message.hwnd, index, info, state);
        const int32_t barWidth = index == kSbVert ? 16 :
            std::max(0, width - ((style & kWsVScroll) ? 16 : 0));
        const int32_t barHeight = index == kSbVert ? height : 16;
        const uint32_t part = scrollBarPartAt(bar, index == kSbVert ? x - (width - 16) : x,
            index == kSbVert ? y : y - (height - 16), barWidth,
            barHeight);
        state->pressedBar = index;
        state->pressedPart = part;
        (void)User32_SetCapture(message.hwnd);
        uint32_t code = kSbEndScroll;
        auto& target = state->bars[index];
        const uint32_t decrement = index == kSbVert ? 1u : 3u;
        const uint32_t increment = index == kSbVert ? 2u : 4u;
        if (part == decrement) {
            target.position = static_cast<int32_t>(std::max<int64_t>(target.minimum,
                int64_t(target.position) - 1));
            code = 0u;
        } else if (part == increment) {
            target.position = static_cast<int32_t>(std::min<int64_t>(
                windowScrollMaximumPosition(target), int64_t(target.position) + 1));
            code = 1u;
        } else if (part == 7u || part == 8u) {
            const int64_t delta = std::max(1u, target.page);
            target.position = static_cast<int32_t>(std::clamp<int64_t>(
                int64_t(target.position) + (part == 7u ? -delta : delta),
                target.minimum, windowScrollMaximumPosition(target)));
            code = part == 7u ? 2u : 3u;
        }
        if (code != kSbEndScroll) {
            target.trackPosition = target.position;
            const uint32_t packed = (static_cast<uint32_t>(target.position) & 0xFFFFu) << 16 | code;
            (void)User32_SendMessageW(message.hwnd, index == kSbVert ? 0x0115u : 0x0114u,
                                      packed, 0);
        }
        invalidateScrollBar(message.hwnd);
        return true;
    }
    if (index == 0xFFFFFFFFu) return false;
    if (message.message == kWmMouseMove && state->pressedPart ==
        (index == kSbVert ? 6u : 5u)) {
        auto& target = state->bars[index];
        const int32_t barWidth = index == kSbVert ? 16 :
            std::max(0, width - ((style & kWsVScroll) ? 16 : 0));
        const auto g = scrollBarGeometry(attachedBarSnapshot(message.hwnd, index, target),
                                         barWidth, index == kSbVert ? height : 16);
        const int32_t axis = index == kSbVert ? y : x;
        const int32_t travel = std::max(0, g.trackLength - g.thumbLength);
        const int32_t offset = std::clamp(axis - g.thumbLength / 2 - g.trackStart,
                                         0, travel);
        const int64_t range = int64_t(windowScrollMaximumPosition(target)) - target.minimum;
        const int32_t position = travel && range
            ? static_cast<int32_t>(int64_t(target.minimum) + range * offset / travel)
            : target.minimum;
        if (position != target.position) {
            target.position = target.trackPosition = position;
            const uint32_t packed = (static_cast<uint32_t>(position) & 0xFFFFu) << 16 | 5u;
            (void)User32_SendMessageW(message.hwnd, index == kSbVert ? 0x0115u : 0x0114u,
                                      packed, 0);
            invalidateScrollBar(message.hwnd);
        }
        return true;
    }
    if (message.message == kWmLButtonUp) {
        const uint32_t pressedPart = state->pressedPart;
        if (pressedPart) {
            const uint32_t msg = index == kSbVert ? 0x0115u : 0x0114u;
            if (pressedPart == (index == kSbVert ? 6u : 5u)) {
                const uint32_t thumb = (static_cast<uint32_t>(state->bars[index].position) & 0xFFFFu) << 16 | 4u;
                (void)User32_SendMessageW(message.hwnd, msg, thumb, 0);
            }
            const uint32_t end = (static_cast<uint32_t>(state->bars[index].position) & 0xFFFFu) << 16 | kSbEndScroll;
            (void)User32_SendMessageW(message.hwnd, msg, end, 0);
        }
        state->pressedBar = 0xFFFFFFFFu;
        state->pressedPart = 0;
        (void)User32_ReleaseCapture();
        invalidateScrollBar(message.hwnd);
        return true;
    }
    return true;
}

void invalidateEdit(void* hwnd) noexcept {
    using PostFn = int32_t (*)(void*, uint32_t, uintptr_t, intptr_t);
    const auto post = Win32kProc<PostFn>("MicaWin32kPostMessageW");
    if (post) (void)post(hwnd, kWmPaint, 0, 0);
}

bool drawRoundedEditFocusRing(void* dc, int32_t width, int32_t height,
                              uint32_t radius, uint32_t thickness,
                              uint32_t color) noexcept {
    if (!dc || width <= 0 || height <= 0 || !thickness) return false;
    const uint32_t maxRadius = static_cast<uint32_t>(
        std::min(width, height) / 2);
    radius = std::min(radius, maxRadius);
    thickness = std::min(thickness, maxRadius);
    const auto integerSqrt = [](uint32_t value) noexcept {
        uint32_t low = 0, high = 129;
        while (low + 1 < high) {
            const uint32_t middle = low + (high - low) / 2;
            if (middle * middle <= value) low = middle;
            else high = middle;
        }
        return low;
    };
    const auto roundedInset = [&](uint32_t cornerRadius,
                                  uint32_t distance) noexcept {
        if (!cornerRadius || distance >= cornerRadius) return uint32_t{0};
        const uint32_t cornerY = cornerRadius - 1u - distance;
        return cornerRadius - integerSqrt(cornerRadius * cornerRadius -
                                          cornerY * cornerY);
    };
    bool drawn = true;
    for (int32_t y = 0; y < height; ++y) {
        const uint32_t outerY = static_cast<uint32_t>(
            std::min(y, height - 1 - y));
        const uint32_t outerInset = roundedInset(radius, outerY);
        const int32_t outerLeft = static_cast<int32_t>(outerInset);
        const int32_t outerRight = width - static_cast<int32_t>(outerInset);
        const bool hasInner = radius > thickness && y >= static_cast<int32_t>(thickness) &&
            y < height - static_cast<int32_t>(thickness);
        if (!hasInner) {
            drawn &= fillButtonRect(dc, {outerLeft, y, outerRight, y + 1}, color);
            continue;
        }
        const uint32_t innerRadius = radius - thickness;
        const uint32_t innerY = static_cast<uint32_t>(std::min(
            y - static_cast<int32_t>(thickness),
            height - static_cast<int32_t>(thickness) - 1 - y));
        const uint32_t innerInset = roundedInset(innerRadius, innerY);
        const int32_t innerLeft = static_cast<int32_t>(thickness + innerInset);
        const int32_t innerRight = width - static_cast<int32_t>(thickness + innerInset);
        drawn &= fillButtonRect(dc, {outerLeft, y, innerLeft, y + 1}, color);
        drawn &= fillButtonRect(dc, {innerRight, y, outerRight, y + 1}, color);
    }
    return drawn;
}

void* createControlFont(void* dc, bool& ownsFont) noexcept {
    ownsFont = false;
    const int32_t dpi = std::max<int32_t>(1, GetDeviceCaps(dc, 90));
    // WhiteSur's control labels use the desktop UI face at roughly 11pt. The
    // stock GUI font is 9pt here and makes entry and button text look tiny.
    const int32_t fontHeight = -std::max<int32_t>(1, (11 * dpi + 36) / 72);
    void* font = CreateFontW(fontHeight, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0,
                             L"Segoe UI");
    ownsFont = font != nullptr;
    return font ? font : GetStockObject(kStockDefaultGuiFont);
}

bool drawEdit(void* hwnd, const EditState& edit, void* dc,
              int32_t width, int32_t height) noexcept {
    if (!dc || width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    void* targetDc = dc;
    void* memoryDc = CreateCompatibleDC(nullptr);
    if (!memoryDc) return false;
    ButtonDibInfo info{};
    info.width = width;
    info.height = -height;
    void* pixels{};
    void* bitmap = CreateDIBSection(nullptr, &info, 0, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        if (bitmap) (void)DeleteObject(bitmap);
        (void)DeleteDC(memoryDc);
        return false;
    }
    std::fill_n(static_cast<uint32_t*>(pixels),
                static_cast<size_t>(width) * static_cast<size_t>(height), 0u);
    void* oldBitmap = SelectObject(memoryDc, bitmap);
    bool ownsFont = false;
    void* font = createControlFont(memoryDc, ownsFont);
    void* oldFont = font ? SelectObject(memoryDc, font) : nullptr;
    if (!oldBitmap || !oldFont) {
        if (oldFont) (void)SelectObject(memoryDc, oldFont);
        if (oldBitmap) (void)SelectObject(memoryDc, oldBitmap);
        if (ownsFont) (void)DeleteObject(font);
        (void)DeleteObject(bitmap);
        (void)DeleteDC(memoryDc);
        return false;
    }
    dc = memoryDc;
    const WinNtRect bounds{0, 0, width, height};
    const int32_t themeState = (edit.state & kButtonDisabled) ? 4 :
        (edit.state & kButtonFocused) ? 5 : 1;
    wchar_t displayText[std::size(edit.text)]{};
    const wchar_t* text = edit.text;
    if (edit.style & kEsPassword) {
        for (size_t index = 0; index < edit.length; ++index)
            displayText[index] = L'*';
        text = displayText;
    }
    void* theme = OpenThemeData(hwnd, L"EDIT");
    bool themed = theme && DrawThemeBackground(theme, dc, 1, themeState,
                                                 &bounds, nullptr) == 0;
    // A profile may define only the normal EDIT surface. Keep its dark fill
    // when focused/disabled states omit a background instead of painting a
    // classic white rectangle over it.
    if (!themed && theme)
        themed = DrawThemeBackground(theme, dc, 1, 1, &bounds, nullptr) == 0;
    if (!themed) {
        const uint32_t background = (edit.state & kButtonDisabled)
            ? kColorButtonDisabled : 0xFFFFFFFFu;
        (void)fillButtonRect(dc, bounds, background);
        (void)fillButtonRect(dc, {0, 0, width, 1}, kColorBorder);
        (void)fillButtonRect(dc, {0, height - 1, width, height}, kColorBorder);
        (void)fillButtonRect(dc, {0, 0, 1, height}, kColorBorder);
        (void)fillButtonRect(dc, {width - 1, 0, width, height}, kColorBorder);
    }
    // A themed entry can intentionally omit border-color (WhiteSur uses
    // rounded backgrounds and box-shadow). Do not replace that style with the
    // square classic border just because TMT_BORDERCOLOR is absent.
    // WhiteSur expresses the focused entry ring with box-shadow, which the
    // bounded native snapshot does not model. Keep the focus state visible
    // with a two-pixel GDI ring while retaining UxTheme for the surface.
    if (edit.state & kButtonFocused) {
        uint32_t kFocusRing = 0xFF4A90D9u;
        uint32_t themedFocus{};
        if (theme &&
            (GetThemeColor(theme, 1, themeState, 3801, &themedFocus) == 0 ||
             GetThemeColor(theme, 1, 1, 3801, &themedFocus) == 0))
            kFocusRing = 0xFF000000u | colorRef(themedFocus);
        int32_t cornerRadius = 0;
        if (theme) (void)GetThemeInt(theme, 1, themeState, 2404, &cornerRadius);
        if (cornerRadius < 0 || cornerRadius > 128) cornerRadius = 0;
        (void)drawRoundedEditFocusRing(dc, width, height,
            static_cast<uint32_t>(cornerRadius), 2, kFocusRing);
    }

    uint32_t textColor = (edit.state & kButtonDisabled) ? 0xFF777777u : kColorWindowText;
    if (theme) {
        uint32_t queried{};
        if (GetThemeColor(theme, 1, themeState, 3803, &queried) != 0)
            (void)GetThemeColor(theme, 1, 1, 3803, &queried);
        if (queried) {
            textColor = 0xFF000000u | ((queried & 0xFFu) << 16) |
                (queried & 0x0000FF00u) | ((queried >> 16) & 0xFFu);
        }
    }
    const int32_t textLeft = std::min(8, width);
    const int32_t textTop = std::min(1, height);
    const WinNtRect textBounds{textLeft, textTop,
        std::max(textLeft, width - 4), std::max(textTop, height - 1)};
    const bool hasTextBounds = textBounds.right > textBounds.left &&
                               textBounds.bottom > textBounds.top;
    bool textDrawn = edit.length == 0 || !hasTextBounds;
    if (edit.length && hasTextBounds) {
        ButtonTextExtent textExtent{};
        const bool measured = GetTextExtentPoint32W(dc, text,
            static_cast<int32_t>(edit.length), &textExtent) != 0 &&
            textExtent.cx >= 0 && textExtent.cy > 0;
        void* clipToken{};
        const bool clipped = measured &&
            MicaGdiPushClipRect(dc, &textBounds, &clipToken) != 0 && clipToken;
        if (clipped) {
            const uint32_t previousColor = SetTextColor(dc, colorRef(textColor));
            const int32_t previousMode = SetBkMode(dc, 1);
            const int32_t textY = std::max(0, (height - textExtent.cy) / 2);
            textDrawn = previousColor != 0xFFFFFFFFu && previousMode > 0 &&
                TextOutW(dc, textBounds.left, textY,
                         text, edit.length) != 0;
            if (previousMode > 0) (void)SetBkMode(dc, previousMode);
            if (previousColor != 0xFFFFFFFFu) (void)SetTextColor(dc, previousColor);
            textDrawn = (MicaGdiPopClipRect(dc, clipToken) != 0) && textDrawn;
        }
    }
    if (theme) (void)CloseThemeData(theme);
    const bool copied = BitBlt(targetDc, 0, 0, width, height,
        memoryDc, 0, 0, 0x00CC0020u) != 0;
    const bool fontRestored = SelectObject(memoryDc, oldFont) != nullptr;
    const bool bitmapRestored = SelectObject(memoryDc, oldBitmap) != nullptr;
    const bool bitmapDeleted = DeleteObject(bitmap) != 0;
    const bool dcDeleted = DeleteDC(memoryDc) != 0;
    const bool fontDeleted = !ownsFont || DeleteObject(font) != 0;
    return textDrawn && copied && fontRestored && bitmapRestored &&
        bitmapDeleted && dcDeleted && fontDeleted;
}

bool drawEditFallback(void* dc, const EditState& edit,
                      int32_t width, int32_t height) noexcept {
    if (!dc || width <= 0 || height <= 0) return false;
    const WinNtRect bounds{0, 0, width, height};
    const uint32_t background = (edit.state & kButtonDisabled)
        ? kColorButtonDisabled : 0xFFFFFFFFu;
    const uint32_t border = (edit.state & kButtonFocused)
        ? 0xFF4A90D9u : kColorBorder;
    bool surfaceDrawn = fillButtonRect(dc, bounds, background);
    surfaceDrawn &= fillButtonRect(dc, {0, 0, width, 1}, border);
    surfaceDrawn &= fillButtonRect(dc, {0, height - 1, width, height}, border);
    surfaceDrawn &= fillButtonRect(dc, {0, 0, 1, height}, border);
    surfaceDrawn &= fillButtonRect(dc, {width - 1, 0, width, height}, border);
    if (edit.length) {
        void* stockFont = GetStockObject(kStockDefaultGuiFont);
        void* oldFont = stockFont ? SelectObject(dc, stockFont) : nullptr;
        const uint32_t color = (edit.state & kButtonDisabled)
            ? 0xFF777777u : kColorWindowText;
        const uint32_t oldColor = SetTextColor(dc, colorRef(color));
        const int32_t oldMode = SetBkMode(dc, 1);
        const bool textDrawn = oldColor != 0xFFFFFFFFu && oldMode > 0 &&
            TextOutW(dc, std::min(5, width), std::max(1, (height - 14) / 2),
                     edit.text, edit.length) != 0;
        if (oldMode > 0) (void)SetBkMode(dc, oldMode);
        if (oldColor != 0xFFFFFFFFu) (void)SetTextColor(dc, oldColor);
        if (oldFont) (void)SelectObject(dc, oldFont);
        (void)textDrawn;
    }
    return surfaceDrawn;
}

void drawButtonFallback(void* dc, int32_t width, int32_t height,
                        const ButtonState& button) noexcept {
    // Disabling a button changes its text treatment, not its layout or
    // visibility. Preserve the normal face so all controls remain discoverable.
    const uint32_t background = (button.state & kButtonDisabled) ? kColorButton :
        (button.state & kButtonPressed) ? kColorButtonPressed :
        (button.state & kButtonHot) ? kColorButtonHot : kColorButton;
    (void)fillButtonRect(dc, {0, 0, width, height}, background);
    (void)fillButtonRect(dc, {0, 0, width, 1}, kColorBorder);
    (void)fillButtonRect(dc, {0, height - 1, width, height}, kColorBorder);
    (void)fillButtonRect(dc, {0, 0, 1, height}, kColorBorder);
    (void)fillButtonRect(dc, {width - 1, 0, width, height}, kColorBorder);
}

bool drawButtonFocus(void* dc, int32_t width, int32_t height) noexcept {
    constexpr int32_t inset = 3;
    constexpr int32_t thickness = 2;
    if (!dc || width <= inset * 2 + thickness || height <= inset * 2 + thickness)
        return false;
    constexpr uint32_t focusColor = 0xFF0860F2u;
    const WinNtRect top{inset, inset, width - inset, inset + thickness};
    const WinNtRect bottom{inset, height - inset - thickness,
                           width - inset, height - inset};
    const WinNtRect left{inset, inset, inset + thickness, height - inset};
    const WinNtRect right{width - inset - thickness, inset,
                          width - inset, height - inset};
    const auto fillFocusRect = [dc, focusColor](const WinNtRect& rect) noexcept {
        using FillFn = int32_t (*)(void*, const MicaWin32kRect*, uint32_t);
        const auto fill = Win32kProc<FillFn>("MicaWin32kFillRect");
        if (fill && fill(dc, reinterpret_cast<const MicaWin32kRect*>(&rect),
                         colorRef(focusColor))) return true;
        return fillButtonRect(dc, rect, focusColor);
    };
    const bool topDrawn = fillFocusRect(top);
    const bool bottomDrawn = fillFocusRect(bottom);
    const bool leftDrawn = fillFocusRect(left);
    const bool rightDrawn = fillFocusRect(right);
    return topDrawn && bottomDrawn && leftDrawn && rightDrawn;
}

bool renderButton(void* hwnd, const ButtonState& button, void* targetDc,
                  int32_t width, int32_t height) noexcept {

    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    void* memoryDc = CreateCompatibleDC(nullptr);

    if (!memoryDc) return false;
    ButtonDibInfo info{};
    info.width = width;
    info.height = -height;
    void* pixels{};
    void* bitmap = CreateDIBSection(nullptr, &info, 0, &pixels, nullptr, 0);

    if (!bitmap || !pixels) {
        if (bitmap) (void)DeleteObject(bitmap);
        (void)DeleteDC(memoryDc);
        return false;
    }
    // DIB storage is not guaranteed to start transparent. Initialize the
    // rounded-corner pixels so they reveal the parent instead of stale white.
    std::fill_n(static_cast<uint32_t*>(pixels),
                static_cast<size_t>(width) * static_cast<size_t>(height), 0u);
    void* oldBitmap = SelectObject(memoryDc, bitmap);
    void* stockFont = GetStockObject(kStockDefaultGuiFont);
    void* oldFont = stockFont ? SelectObject(memoryDc, stockFont) : nullptr;
    if (!oldBitmap || !oldFont) {
        if (oldFont) (void)SelectObject(memoryDc, oldFont);
        if (oldBitmap) (void)SelectObject(memoryDc, oldBitmap);
        (void)DeleteObject(bitmap);
        (void)DeleteDC(memoryDc);
        return false;
    }

    const uint32_t type = button.style & kBsTypeMask;
    const bool groupBox = type == kBsGroupBox;
    const bool radio = isRadioButtonType(type);
    const bool checkable = isCheckableButtonType(type);
    const int32_t part = groupBox ? 4 : checkable ? (radio ? 2 : 3) : 1;
    const int32_t baseState = (button.state & kButtonDisabled) ? 4 :
        (button.state & kButtonPressed) ? 3 :
        (button.state & kButtonHot) ? 2 :
        (button.state & kButtonDefault) ? 5 : 1;
    const int32_t themeState = checkable
        ? static_cast<int32_t>((button.checkState == 2 ? 8 :
            button.checkState == 1 ? 4 : 0) + baseState) : baseState;
    const int32_t glyphSize = std::min<int32_t>(13, std::max<int32_t>(9, height - 8));
    const int32_t glyphY = std::max<int32_t>(0, (height - glyphSize) / 2);
    const WinNtRect glyph{4, glyphY, 4 + glyphSize, glyphY + glyphSize};
    const WinNtRect bounds = checkable ? glyph : WinNtRect{0, 0, width, height};

    void* theme = OpenThemeData(hwnd, L"BUTTON");

    bool backgroundDrawn = theme &&
        DrawThemeBackground(theme, memoryDc, part, themeState, &bounds, nullptr) == 0;
    // Normal state supplies the profile's surface when a hot, pressed, or
    // disabled variant has text/border overrides but no background.
    if (!backgroundDrawn && theme)
        backgroundDrawn = DrawThemeBackground(theme, memoryDc, part, 1,
                                                &bounds, nullptr) == 0;

    if (!backgroundDrawn && groupBox) {
        const int32_t textStart = 8;
        ButtonTextExtent extent{};
        size_t textLength{};
        while (textLength < std::size(button.text) && button.text[textLength]) ++textLength;
        if (textLength) (void)GetTextExtentPoint32W(memoryDc, button.text,
            static_cast<int32_t>(textLength), &extent);
        (void)fillButtonRect(memoryDc, {0, 0, width, height}, 0xFFF0F0F0u);
        (void)fillButtonRect(memoryDc, {0, 6, textLength ? textStart : width, 7}, kColorBorder);
        if (textLength) (void)fillButtonRect(memoryDc,
            {textStart + extent.cx + 4, 6, width, 7}, kColorBorder);
        (void)fillButtonRect(memoryDc, {0, 6, 1, height}, kColorBorder);
        (void)fillButtonRect(memoryDc, {width - 1, 6, width, height}, kColorBorder);
        (void)fillButtonRect(memoryDc, {0, height - 1, width, height}, kColorBorder);
        backgroundDrawn = true;
    } else if (!backgroundDrawn && !checkable) drawButtonFallback(memoryDc, width, height, button);
    if (checkable && !backgroundDrawn) {
        const uint32_t face = 0xFFFFFFFFu;
        (void)fillButtonRect(memoryDc, glyph, face);
        (void)fillButtonRect(memoryDc, {glyph.left, glyph.top, glyph.right, glyph.top + 1}, kColorBorder);
        (void)fillButtonRect(memoryDc, {glyph.left, glyph.bottom - 1, glyph.right, glyph.bottom}, kColorBorder);
        (void)fillButtonRect(memoryDc, {glyph.left, glyph.top, glyph.left + 1, glyph.bottom}, kColorBorder);
        (void)fillButtonRect(memoryDc, {glyph.right - 1, glyph.top, glyph.right, glyph.bottom}, kColorBorder);
    }

    uint32_t textColor = (button.state & kButtonDisabled)
        ? 0xFF777777u : kColorWindowText;
    if (theme) {

        uint32_t themedColor{};
        if (GetThemeColor(theme, part, themeState, 3803, &themedColor) != 0)
            (void)GetThemeColor(theme, part, 1, 3803, &themedColor);
        if (themedColor)
            textColor = 0xFF000000u | ((themedColor & 0xFFu) << 16) |
                (themedColor & 0x0000FF00u) | ((themedColor >> 16) & 0xFFu);

    }
    if (checkable && button.checkState != 0) {
        const uint32_t mark = (button.state & kButtonDisabled) ? 0xFF777777u : textColor;
        const int32_t cx = (glyph.left + glyph.right) / 2;
        const int32_t cy = (glyph.top + glyph.bottom) / 2;
        if (radio) {
            for (int32_t row = -2; row <= 2; ++row) {
                const int32_t half = row == 0 ? 2 : 1;
                (void)fillButtonRect(memoryDc, {cx - half, cy + row, cx + half + 1, cy + row + 1}, mark);
            }
        } else if (button.checkState == 2) {
            (void)fillButtonRect(memoryDc, {glyph.left + 3, cy - 1, glyph.right - 3, cy + 2}, mark);
        } else {
            (void)fillButtonRect(memoryDc, {glyph.left + 2, cy, glyph.left + 5, cy + 2}, mark);
            (void)fillButtonRect(memoryDc, {glyph.left + 4, cy + 1, glyph.left + 7, cy + 3}, mark);
            (void)fillButtonRect(memoryDc, {glyph.left + 6, cy - 2, glyph.left + 8, cy + 2}, mark);
        }
    }
    const WinNtRect textBounds = checkable
        ? WinNtRect{glyph.right + 5, 0, width, height} :
          groupBox ? WinNtRect{8, 0, width, height} : bounds;

    bool textDrawn = theme &&
        DrawThemeText(theme, memoryDc, part, themeState, button.text, -1,
            (checkable || groupBox ? kDtLeft : kDtCenter) |
                kDtVCenter | kDtSingleLine | kDtNoPrefix,
            0, &textBounds) == 0;

    if (!textDrawn && button.text[0]) {

        ButtonTextExtent extent{};
        size_t textLength{};
        while (textLength < std::size(button.text) && button.text[textLength]) ++textLength;
        if (textLength < std::size(button.text) &&
            GetTextExtentPoint32W(memoryDc, button.text,
                static_cast<int32_t>(textLength), &extent) &&
            extent.cx >= 0 && extent.cy > 0) {
            (void)SetBkMode(memoryDc, 1);
            (void)SetTextColor(memoryDc, colorRef(textColor));
            const int32_t textWidth = textBounds.right - textBounds.left;
            const int32_t x = textBounds.left +
                ((checkable || groupBox) ? 0 : std::max(0, (textWidth - extent.cx) / 2));
            const int32_t y = std::max(0, (height - extent.cy) / 2);
            (void)TextOutW(memoryDc, x, y, button.text,
                           static_cast<int32_t>(textLength));
        }

    }

    const int32_t copied = BitBlt(targetDc, 0, 0, width, height,
                                   memoryDc, 0, 0, 0x00CC0020u);

    // Draw on the window DC after copying the themed DIB. In this path the
    // window DC is the presented surface; fillButtonRect also falls back to
    // Win32k's direct software fill when GDI FillRect rejects that DC.
    const bool focusDrawn = !(button.state & kButtonFocused) ||
        drawButtonFocus(targetDc, width, height);

    if (theme) (void)CloseThemeData(theme);
    (void)SelectObject(memoryDc, oldFont);
    (void)SelectObject(memoryDc, oldBitmap);
    const int32_t bitmapDeleted = DeleteObject(bitmap);
    const int32_t dcDeleted = DeleteDC(memoryDc);

    return copied && focusDrawn && bitmapDeleted && dcDeleted;
}
} // namespace

void drawAttachedWindowScrollBars(void* hwnd, void* dc) noexcept {
    drawAttachedWindowScrollBarsImpl(hwnd, dc);
}

intptr_t User32_ButtonWndProc(void* hwnd, uint32_t message,
                              uintptr_t wParam, intptr_t lParam) noexcept {
    if (message == kWmNcCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create || !isSupportedButtonType(
                static_cast<uint32_t>(create->style) & kBsTypeMask))
            return 0;
        ButtonState initial{};
        initial.hwnd = hwnd;
        initial.parent = create->parent;
        initial.style = static_cast<uint32_t>(create->style);
        if (initial.style & kWsDisabled) initial.state |= kButtonDisabled;
        if ((initial.style & kBsTypeMask) == kBsDefPushButton)
            initial.state |= kButtonDefault;
        if (create->title) {
            size_t length{};
            while (length < std::size(initial.text) - 1 && create->title[length]) {
                initial.text[length] = create->title[length];
                ++length;
            }
            if (create->title[length]) return 0;
        }
        ButtonLockGuard guard(g_buttonLock);
        initial.creationOrder = g_nextButtonOrder++;
        if (!g_nextButtonOrder) g_nextButtonOrder = 1;
        if (isRadioButtonType(initial.style & kBsTypeMask)) {
            ButtonState* previousRadio{};
            if (!(initial.style & kWsGroup)) {
                for (auto& candidate : g_buttons) {
                    if (candidate.hwnd && candidate.parent == initial.parent &&
                        isRadioButtonType(candidate.style & kBsTypeMask) &&
                        (!previousRadio || candidate.creationOrder >
                            previousRadio->creationOrder))
                        previousRadio = &candidate;
                }
            }
            if (previousRadio) {
                initial.radioGroup = previousRadio->radioGroup;
            } else {
                initial.radioGroup = g_nextRadioGroup++;
                if (!g_nextRadioGroup) g_nextRadioGroup = 1;
            }
        }
        for (auto& entry : g_buttons) {
            if (entry.hwnd) continue;
            entry = initial;
            if ((initial.style & (kWsDisabled | kWsVisible)) ==
                (kWsDisabled | kWsVisible))
                (void)User32_InvalidateRect(hwnd, nullptr, 1);
            return 1;
        }
        return 0;
    }

    if (message == kWmSetText) {
        wchar_t replacement[128]{};
        const auto* source = reinterpret_cast<const wchar_t*>(lParam);
        size_t length{};
        if (source) {
            while (length < std::size(replacement) - 1 && source[length]) {
                replacement[length] = source[length];
                ++length;
            }
            if (source[length]) return 0;
        }
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* button = findButton(hwnd);
            if (!button) return 0;
            std::memcpy(button->text, replacement, sizeof(replacement));
        }
        invalidateButton(hwnd);
        return 1;
    }
    if (message == kWmGetTextLength) {
        ButtonLockGuard guard(g_buttonLock);
        const auto* button = findButton(hwnd);
        if (!button) return 0;
        size_t length{};
        while (length < std::size(button->text) && button->text[length]) ++length;
        return static_cast<intptr_t>(length);
    }
    if (message == kWmGetText) {
        if (!lParam || !wParam) return 0;
        ButtonLockGuard guard(g_buttonLock);
        const auto* button = findButton(hwnd);
        if (!button) return 0;
        size_t length{};
        while (length < std::size(button->text) && button->text[length]) ++length;
        const size_t copied = std::min(length, static_cast<size_t>(wParam - 1));
        auto* destination = reinterpret_cast<wchar_t*>(lParam);
        if (copied) std::memcpy(destination, button->text, copied * sizeof(wchar_t));
        destination[copied] = L'\0';
        return static_cast<intptr_t>(copied);
    }

    if (message == kBmGetState) {
        ButtonLockGuard guard(g_buttonLock);
        const auto* button = findButton(hwnd);
        if (!button) return 0;
        return (button->checkState == 2 ? 0x0003 : button->checkState ? 0x0001 : 0) |
               ((button->state & kButtonPressed) ? 0x0004 : 0) |
               ((button->state & kButtonFocused) ? 0x0008 : 0);
    }
    if (message == kBmGetCheck) {
        ButtonLockGuard guard(g_buttonLock);
        const auto* button = findButton(hwnd);
        return button && isCheckableButtonType(button->style & kBsTypeMask)
            ? static_cast<intptr_t>(button->checkState) : 0;
    }
    if (message == kBmSetCheck) {
        bool changed = false;
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* button = findButton(hwnd);
            if (!button || !isCheckableButtonType(button->style & kBsTypeMask)) return 0;
            const uint32_t type = button->style & kBsTypeMask;
            uint32_t value = wParam == 1 ? 1u : 0u;
            if ((type == kBs3State || type == kBsAuto3State) && wParam == 2) value = 2;
            changed = button->checkState != value;
            button->checkState = value;
        }
        if (changed) invalidateButton(hwnd);
        return 0;
    }
    if (message == kBmSetState) {
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* button = findButton(hwnd);
            if (!button) return 0;
            if (wParam) button->state |= kButtonPressed;
            else button->state &= ~kButtonPressed;
        }
        invalidateButton(hwnd);
        return 0;
    }
    if (message == kBmSetStyle) {
        const uint32_t type = static_cast<uint32_t>(wParam) & kBsTypeMask;
        if (!isSupportedButtonType(type)) return 0;
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* button = findButton(hwnd);
            if (!button) return 0;
            button->style = (button->style & ~kBsTypeMask) | type;
            if (type == kBsDefPushButton) button->state |= kButtonDefault;
            else button->state &= ~kButtonDefault;
            if (!isCheckableButtonType(type)) button->checkState = 0;
            else if (type != kBs3State && type != kBsAuto3State && button->checkState == 2)
                button->checkState = 0;
        }
        if (lParam) invalidateButton(hwnd);
        return 0;
    }
    if (message == kBmClick) {
        void* changedRadios[64]{};
        size_t changedRadioCount{};
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* button = findButton(hwnd);
            if (!button || (button->state & kButtonDisabled)) return 0;
            button->state |= kButtonPressed;
            const uint32_t type = button->style & kBsTypeMask;
            if (type == kBsAutoRadioButton) {
                changedRadioCount = selectAutoRadioGroupLocked(*button, changedRadios);
            } else if (isAutoCheckButtonType(type)) {
                button->checkState = type == kBsAuto3State
                    ? (button->checkState + 1u) % 3u : (button->checkState == 0 ? 1u : 0u);
            }
        }
        for (size_t index = 0; index < changedRadioCount; ++index)
            invalidateButton(changedRadios[index]);
        invalidateButton(hwnd);
        notifyButtonClicked(hwnd);
        {
            ButtonLockGuard guard(g_buttonLock);
            if (auto* button = findButton(hwnd)) button->state &= ~kButtonPressed;
        }
        invalidateButton(hwnd);
        return 0;
    }

    if (message == kWmNcDestroy || message == kWmDestroy) {
        bool wasHot = false;
        bool wasArmed = false;
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* button = findButton(hwnd);
            if (button) {
                wasHot = g_hotButton == hwnd;
                wasArmed = button->armed;
                *button = {};
            }
            if (wasHot) g_hotButton = nullptr;
        }
        if (wasArmed) (void)User32_ReleaseCapture();
        if (message == kWmNcDestroy) return 0;
        return 0;
    }

    bool repaint = false;
    bool click = false;
    bool acquireMouse = false;
    bool releaseMouse = false;
    uint32_t inputTraceFlags{};
    uint32_t inputTraceId{};
    void* changedRadios[64]{};
    size_t changedRadioCount{};
    const bool pointInside = (message == kWmMouseMove || message == kWmLButtonUp)
        ? buttonPointInside(hwnd, lParam) : true;
    if (message == kWmMouseMove) leaveHotButton(hwnd);
    if (message == kWmSetFocus || message == kWmKillFocus ||
        message == 0x001Fu /* WM_CANCELMODE */ ||
        message == kWmEnable || message == kWmMouseMove ||
        message == kWmMouseLeave || message == kWmCaptureChanged || message == kWmLButtonDown ||
        message == kWmLButtonUp || message == kWmKeyDown || message == kWmKeyUp ||
        message == 0x031Au || message == 0x001Au) {
        ButtonLockGuard guard(g_buttonLock);
        auto* button = findButton(hwnd);
        if (!button) return 0;
        const uint32_t oldState = button->state;
        const uint32_t oldCheckState = button->checkState;
        switch (message) {
        case kWmSetFocus:
            button->state |= kButtonFocused;
            break;
        case kWmKillFocus:
            button->state &= ~(kButtonFocused | kButtonPressed);
            button->spaceArmed = false;
            if (button->armed) {
                button->armed = false;
                releaseMouse = true;
            }
            break;
        case 0x001Fu: // WM_CANCELMODE
            button->state &= ~kButtonPressed;
            button->spaceArmed = false;
            if (button->armed) {
                button->armed = false;
                releaseMouse = true;
            }
            break;
        case kWmEnable:
            inputTraceId = static_cast<uint32_t>(button->id);
            inputTraceFlags = (wParam ? 1u : 0u) |
                ((button->state & kButtonDisabled) ? 2u : 0u);
            if (wParam) button->state &= ~kButtonDisabled;
            else {
                button->state = (button->state | kButtonDisabled) & ~kButtonPressed;
                button->spaceArmed = false;
                if (button->armed) {
                    button->armed = false;
                    releaseMouse = true;
                }
            }
            break;
        case kWmMouseMove:
            if (pointInside) {
                g_hotButton = hwnd;
                button->state |= kButtonHot;
            } else {
                button->state &= ~kButtonHot;
                if (g_hotButton == hwnd) g_hotButton = nullptr;
            }
            if (button->armed && (wParam & kMkLButton)) {
                if (pointInside) button->state |= kButtonPressed;
                else button->state &= ~kButtonPressed;
            }
            break;
        case kWmMouseLeave:
            button->state &= ~kButtonHot;
            if (!button->armed) button->state &= ~kButtonPressed;
            if (g_hotButton == hwnd) g_hotButton = nullptr;
            break;
        case kWmCaptureChanged:
            button->armed = false;
            // ReactOS cancels a pending press when capture moves to another
            // window. Space activation has its own arm bit, so clear it here
            // too; otherwise a later WM_KEYUP can click a button after its
            // press was canceled by another control taking capture.
            button->spaceArmed = false;
            button->state &= ~kButtonPressed;
            break;
        case kWmLButtonDown:
            inputTraceId = static_cast<uint32_t>(button->id);
            inputTraceFlags = (button->state & kButtonDisabled) ? 1u : 0u;
            if (!(button->state & kButtonDisabled)) {
                button->state |= kButtonHot | kButtonPressed | kButtonFocused;
                button->armed = true;
                acquireMouse = true;
                g_hotButton = hwnd;
            }
            break;
        case kWmLButtonUp:
            inputTraceId = static_cast<uint32_t>(button->id);
            inputTraceFlags = (button->armed ? 1u : 0u) |
                (pointInside ? 2u : 0u) |
                ((button->state & kButtonDisabled) ? 4u : 0u);
            click = button->armed && pointInside &&
                    !(button->state & kButtonDisabled);
            releaseMouse = button->armed;
            button->armed = false;
            button->state &= ~kButtonPressed;
            break;
        case kWmKeyDown:
            if (wParam == kVkSpace && (button->state & kButtonFocused) &&
                !(button->state & kButtonDisabled) && !button->spaceArmed) {
                button->spaceArmed = true;
                button->state |= kButtonPressed;
                // The built-in ReactOS BUTTON procedure captures input while
                // Space is held, allowing WM_CAPTURECHANGED/WM_CANCELMODE to
                // cancel the activation consistently with a mouse press.
                acquireMouse = true;
            }
            break;
        case kWmKeyUp:
            click = wParam == kVkSpace && button->spaceArmed &&
                    (button->state & kButtonFocused) &&
                    !(button->state & kButtonDisabled);
            if (wParam == kVkSpace) {
                releaseMouse = button->spaceArmed;
                button->spaceArmed = false;
                if (!button->armed) button->state &= ~kButtonPressed;
            }
            break;
        case 0x031Au: // WM_THEMECHANGED
        case 0x001Au: // WM_SETTINGCHANGE
            repaint = true;
            break;
        }
        if (click) {
            const uint32_t type = button->style & kBsTypeMask;
            if (type == kBsAutoRadioButton) {
                changedRadioCount = selectAutoRadioGroupLocked(*button, changedRadios);
            } else if (isAutoCheckButtonType(type)) {
                button->checkState = type == kBsAuto3State
                    ? (button->checkState + 1u) % 3u
                    : (button->checkState == 0 ? 1u : 0u);
            }
        }
        repaint |= oldState != button->state || oldCheckState != button->checkState;
    }
    if ((message == kWmLButtonDown || message == kWmLButtonUp) &&
        (inputTraceId == 1036u || inputTraceId == 1052u ||
         inputTraceId == 1057u || inputTraceId == 1071u ||
         inputTraceId == 1073u)) {
        using TraceFn = void (*)(uint32_t, uint32_t, uint32_t);
        const auto trace = Win32kProc<TraceFn>("MicaWin32kDebugInputTrace");
        if (trace) trace(message == kWmLButtonDown ? 1u :
                         message == kWmLButtonUp ? 2u : 5u,
                         inputTraceId, inputTraceFlags | (click ? 8u : 0u));
    }
    if (acquireMouse) (void)User32_SetCapture(hwnd);
    if (releaseMouse) (void)User32_ReleaseCapture();
    for (size_t index = 0; index < changedRadioCount; ++index)
        invalidateButton(changedRadios[index]);
    if (message == kWmLButtonDown) (void)User32_SetFocus(hwnd);
    if (repaint) {
        if (message == kWmSetFocus || message == kWmKillFocus)
            (void)User32_SendMessageW(hwnd, kWmPaint, 0, 0);
        else
            invalidateButton(hwnd);
    }
    if (message == kWmPaint) {

        ButtonState button{};
        {
            ButtonLockGuard guard(g_buttonLock);
            const auto* current = findButton(hwnd);
            if (!current) return 0;
            button = *current;
        }
        if (User32_GetFocus() == hwnd) button.state |= kButtonFocused;
        else button.state &= ~kButtonFocused;
        WinNtPaintStruct paint{};
        void* targetDc = User32_BeginPaint(hwnd, &paint);

        if (!targetDc) return 0;
        MicaWin32kRect client{};
        using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
        const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
        const bool hasRect = getRect && getRect(hwnd, &client);

        if ((button.style & kBsTypeMask) == kBsOwnerDraw) {
            bool handled{};
            if (hasRect && button.parent) {
                uint32_t itemState{};
                if (button.state & kButtonPressed) itemState |= 0x0001u; // ODS_SELECTED
                if (button.state & kButtonDisabled) itemState |= 0x0004u; // ODS_DISABLED
                if (button.state & kButtonFocused) itemState |= 0x0010u; // ODS_FOCUS
                if (button.state & kButtonDefault) itemState |= 0x0020u; // ODS_DEFAULT
                if (button.state & kButtonHot) itemState |= 0x0040u; // ODS_HOTLIGHT
                Win32DrawItemStruct64 item{};
                item.controlType = 4; // ODT_BUTTON
                item.controlId = static_cast<uint32_t>(button.id);
                item.itemId = static_cast<uint32_t>(button.id);
                item.action = 1; // ODA_DRAWENTIRE
                item.state = itemState;
                item.window = hwnd;
                item.dc = targetDc;
                item.rect = {0, 0, client.right, client.bottom};
                handled = User32_SendMessageW(button.parent, 0x002Bu /* WM_DRAWITEM */,
                    button.id, reinterpret_cast<intptr_t>(&item)) != 0;
            }
            (void)User32_EndPaint(hwnd, &paint);
            return handled ? 1 : 0;
        }

        bool drawn = false;
        if (hasRect && client.right > 0 && client.bottom > 0) {

            drawn = renderButton(hwnd, button, targetDc, client.right, client.bottom);

        }
        if (!drawn && hasRect && client.right > 0 && client.bottom > 0) {
            drawButtonFallback(targetDc, client.right, client.bottom, button);
            if (button.state & kButtonFocused)
                (void)drawButtonFocus(targetDc, client.right, client.bottom);
        }
        (void)User32_EndPaint(hwnd, &paint);

        return hasRect && client.right > 0 && client.bottom > 0;
    }
    if (click) notifyButtonClicked(hwnd);
    return User32_DefWindowProcW(hwnd, message, wParam, lParam);
}

intptr_t User32_StaticWndProc(void* hwnd, uint32_t message,
                              uintptr_t wParam, intptr_t lParam) noexcept {
    constexpr uint32_t wmNcCreate = 0x0081, wmNcDestroy = 0x0082;
    constexpr uint32_t wmSetText = 0x000c, wmGetText = 0x000d, wmGetTextLength = 0x000e;
    constexpr uint32_t wmPaint = 0x000f;
    if (message == wmNcCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create) return 0;
        StaticState initial{};
        initial.hwnd = hwnd;
        initial.parent = create->parent;
        initial.id = reinterpret_cast<uintptr_t>(create->menu);
        initial.style = static_cast<uint32_t>(create->style);
        initial.extendedStyle = static_cast<uint32_t>(create->exStyle);
        if (create->title) {
            size_t length{};
            while (length < std::size(initial.text) - 1 && create->title[length]) {
                initial.text[length] = create->title[length]; ++length;
            }
            if (create->title[length]) return 0;
            initial.length = static_cast<uint16_t>(length);
        }
        ButtonLockGuard guard(g_buttonLock);
        for (auto& entry : g_statics) if (!entry.hwnd) { entry = initial; return 1; }
        return 0;
    }
    if (message == wmNcDestroy) {
        ButtonLockGuard guard(g_buttonLock);
        for (auto& entry : g_statics) if (entry.hwnd == hwnd) { entry = {}; break; }
        return 0;
    }
    if (message == wmSetText) {
        const auto* source = reinterpret_cast<const wchar_t*>(lParam);
        wchar_t replacement[256]{}; size_t length{};
        while (source && length < std::size(replacement) - 1 && source[length]) {
            replacement[length] = source[length]; ++length;
        }
        if (source && source[length]) return 0;
        bool updated{};
        uintptr_t controlId{};
        {
            ButtonLockGuard guard(g_buttonLock);
            for (auto& entry : g_statics) if (entry.hwnd == hwnd) {
                std::memcpy(entry.text, replacement, sizeof(replacement));
                entry.length = static_cast<uint16_t>(length);
                controlId = entry.id;
                updated = true;
                break;
            }
        }
        if (updated && controlId == 1074u) {
            using TraceFn = void (*)(uint32_t, uint32_t, uint32_t);
            const auto trace = Win32kProc<TraceFn>("MicaWin32kDebugInputTrace");
            if (trace) {
                const uint32_t first = length ? static_cast<uint32_t>(replacement[0]) : 0u;
                trace(9, static_cast<uint32_t>(controlId),
                      (static_cast<uint32_t>(length) << 16) | (first & 0xFFFFu));
            }
        }
        if (updated) {
            (void)User32_InvalidateRect(hwnd, nullptr, 1);
        }
        return updated ? 1 : 0;
    }
    if (message == wmGetTextLength || message == wmGetText) {
        ButtonLockGuard guard(g_buttonLock);
        for (const auto& entry : g_statics) if (entry.hwnd == hwnd) {
            if (message == wmGetTextLength) return entry.length;
            if (!lParam || !wParam) return 0;
            const size_t copied = std::min(static_cast<size_t>(entry.length),
                static_cast<size_t>(wParam - 1));
            auto* destination = reinterpret_cast<wchar_t*>(lParam);
            if (copied) std::memcpy(destination, entry.text, copied * sizeof(wchar_t));
            destination[copied] = 0;
            return static_cast<intptr_t>(copied);
        }
        return 0;
    }
    if (message == wmPaint) {
        StaticState state{};
        {
            ButtonLockGuard guard(g_buttonLock);
            bool found{};
            for (const auto& entry : g_statics) if (entry.hwnd == hwnd) { state = entry; found = true; break; }
            if (!found) return 0;
        }
        if (state.id == 1074u) {
            using TraceFn = void (*)(uint32_t, uint32_t, uint32_t);
            const auto trace = Win32kProc<TraceFn>("MicaWin32kDebugInputTrace");
            if (trace) {
                const uint32_t first = state.length
                    ? static_cast<uint32_t>(state.text[0]) : 0u;
                trace(10, static_cast<uint32_t>(state.id),
                      (static_cast<uint32_t>(state.length) << 16) | (first & 0xFFFFu));
            }
        }
        WinNtPaintStruct paint{};
        void* dc = User32_BeginPaint(hwnd, &paint);
        if (!dc) return 0;
        MicaWin32kRect client{};
        using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
        const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
        bool drawn = getRect && getRect(hwnd, &client) && client.right > 0 && client.bottom > 0;
        if (drawn) {
            const uint32_t type = state.style & 0x1fu;
            WinNtRect bounds{0, 0, client.right, client.bottom};
            uint32_t background = 0x00FFFFFFu; // COLOR_WINDOW default.
            if (type == 4) background = 0xFF000000u;
            else if (type == 5 || type == 8) background = 0xFF808080u;
            else if (type == 6 || type == 9) background = 0x00FFFFFFu;

            // Static controls erase using WM_CTLCOLORSTATIC. This lets the
            // dialog select its own brush (Calc uses WHITE_BRUSH for the LCD).
            void* brush = reinterpret_cast<void*>(User32_SendMessageW(
                state.parent, 0x0138u /* WM_CTLCOLORSTATIC */,
                reinterpret_cast<uintptr_t>(dc), reinterpret_cast<intptr_t>(hwnd)));
            const bool filledByParent = brush && FillRect(dc, &bounds, brush) != 0;
            if (!filledByParent && (type == 4 || type == 5 || type == 6 ||
                                    type == 7 || type == 8 || type == 9 ||
                                    type == 0 || type == 1 || type == 2 || type == 3))
                drawn = fillButtonRect(dc, bounds, background);

            constexpr uint32_t kWsExClientEdge = 0x00000200u;
            if ((state.extendedStyle & kWsExClientEdge) &&
                client.right > 3 && client.bottom > 3) {
                // WS_EX_CLIENTEDGE is a recessed two-tone border. Inset the
                // static text area so its value does not touch the frame.
                drawn &= fillButtonRect(dc, {0, 0, client.right, 1}, 0x00404040u);
                drawn &= fillButtonRect(dc, {0, 0, 1, client.bottom}, 0x00404040u);
                drawn &= fillButtonRect(dc, {1, 1, client.right - 1, 2}, 0x00808080u);
                drawn &= fillButtonRect(dc, {1, 1, 2, client.bottom - 1}, 0x00808080u);
                drawn &= fillButtonRect(dc, {0, client.bottom - 1, client.right, client.bottom}, 0x00FFFFFFu);
                drawn &= fillButtonRect(dc, {client.right - 1, 0, client.right, client.bottom}, 0x00FFFFFFu);
                drawn &= fillButtonRect(dc, {client.right - 2, 1, client.right - 1, client.bottom - 1}, 0x00E0E0E0u);
                drawn &= fillButtonRect(dc, {1, client.bottom - 2, client.right - 1, client.bottom - 1}, 0x00E0E0E0u);
                bounds = {2, 2, client.right - 2, client.bottom - 2};
            }
            if (state.length && type <= 2) {
                void* font = GetStockObject(kStockDefaultGuiFont);
                void* oldFont = font ? SelectObject(dc, font) : nullptr;
                const uint32_t oldColor = SetTextColor(dc, 0x00000000u);
                const int32_t oldMode = SetBkMode(dc, 1);
                ButtonTextExtent extent{};
                const bool measured = GetTextExtentPoint32W(dc, state.text, state.length, &extent) != 0;
                int32_t x = bounds.left + 1;
                if (measured && type == 1)
                    x = bounds.left + std::max(0, (bounds.right - bounds.left - extent.cx) / 2);
                else if (measured && type == 2)
                    x = std::max(bounds.left, bounds.right - extent.cx - 2);
                const int32_t y = (state.style & 0x00000200u) && measured
                    ? std::max(bounds.top, bounds.top + (bounds.bottom - bounds.top - extent.cy) / 2)
                    : bounds.top;
                drawn &= oldColor != 0xffffffffu && oldMode > 0 &&
                    TextOutW(dc, x, y, state.text, state.length) != 0;
                if (oldMode > 0) (void)SetBkMode(dc, oldMode);
                if (oldColor != 0xffffffffu) (void)SetTextColor(dc, oldColor);
                if (oldFont) (void)SelectObject(dc, oldFont);
            }
        }
        (void)User32_EndPaint(hwnd, &paint);
        return drawn ? 1 : 0;
    }
    return User32_DefWindowProcW(hwnd, message, wParam, lParam);
}

intptr_t User32_ScrollBarWndProc(void* hwnd, uint32_t message,
                                uintptr_t wParam, intptr_t lParam) noexcept {
    if (message == kWmNcCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create || (static_cast<uint32_t>(create->style) & kSbsUnsupported))
            return 0;
        ScrollBarState initial{};
        initial.hwnd = hwnd;
        initial.parent = create->parent;
        initial.id = reinterpret_cast<uintptr_t>(create->menu);
        initial.style = static_cast<uint32_t>(create->style);
        if (initial.style & kWsDisabled) initial.state |= kButtonDisabled;
        ButtonLockGuard guard(g_buttonLock);
        for (auto& entry : g_scrollBars) {
            if (entry.hwnd) continue;
            entry = initial;
            return 1;
        }
        return 0;
    }
    if (message == kWmDestroy || message == kWmNcDestroy) {
        ButtonLockGuard guard(g_buttonLock);
        if (auto* bar = findScrollBar(hwnd)) *bar = {};
        return 0;
    }
    if (message == kSbGetPos) {
        ButtonLockGuard guard(g_buttonLock);
        const auto* bar = findScrollBar(hwnd);
        return bar ? bar->position : 0;
    }
    if (message == kSbSetPos || message == kSbSetRange ||
        message == kSbSetRangeRedraw || message == kSbSetScrollInfo ||
        message == kSbEnableArrows) {
        int32_t result{};
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* bar = findScrollBar(hwnd);
            if (!bar) return 0;
            result = bar->position;
            if (message == kSbSetPos) bar->position = static_cast<int32_t>(lParam);
            else if (message == kSbSetRange || message == kSbSetRangeRedraw) {
                bar->minimum = static_cast<int32_t>(wParam);
                bar->maximum = static_cast<int32_t>(lParam);
            } else if (message == kSbEnableArrows) {
                bar->disabledArrows = static_cast<uint32_t>(wParam) & 3u;
            } else {
                const auto* info = reinterpret_cast<const ScrollInfoW*>(lParam);
                if (!info || info->cbSize < sizeof(ScrollInfoW) ||
                    (info->fMask & ~kSifSet)) return 0;
                if (info->fMask & kSifRange) {
                    bar->minimum = info->nMin;
                    bar->maximum = info->nMax;
                }
                if (info->fMask & kSifPage) bar->page = info->nPage;
                if (info->fMask & kSifPos) bar->position = info->nPos;
                if (info->fMask & kSifDisableNoScroll)
                    bar->disabledArrows = bar->maximum <= bar->minimum ? 3u : 0u;
            }
            clampScrollBar(*bar);
        }
        if (message != kSbSetPos || wParam || message == kSbSetRangeRedraw ||
            message == kSbSetScrollInfo)
            invalidateScrollBar(hwnd);
        return result;
    }
    if (message == kSbGetRange || message == kSbGetScrollInfo) {
        ButtonLockGuard guard(g_buttonLock);
        const auto* bar = findScrollBar(hwnd);
        if (!bar) return 0;
        if (message == kSbGetRange) {
            if (wParam) *reinterpret_cast<int32_t*>(wParam) = bar->minimum;
            if (lParam) *reinterpret_cast<int32_t*>(lParam) = bar->maximum;
        } else {
            auto* info = reinterpret_cast<ScrollInfoW*>(lParam);
            if (!info || info->cbSize < sizeof(ScrollInfoW) ||
                (info->fMask & ~kSifAll)) return 0;
            copyScrollInfo(*bar, *info);
        }
        return 1;
    }
    if (message == kWmThemeChanged || message == kWmSettingChange) {
        invalidateScrollBar(hwnd);
        return 0;
    }
    if (message == kWmEnable) {
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* bar = findScrollBar(hwnd);
            if (!bar) return 0;
            if (wParam) bar->state &= ~kButtonDisabled;
            else bar->state |= kButtonDisabled;
        }
        invalidateScrollBar(hwnd);
        return 0;
    }
    if (message == kWmCaptureChanged) {
        ButtonLockGuard guard(g_buttonLock);
        if (auto* bar = findScrollBar(hwnd)) {
            bar->captured = false;
            bar->pressedPart = 0;
            clampScrollBar(*bar);
        }
        invalidateScrollBar(hwnd);
        return 0;
    }
    const auto getDimensions = [hwnd](int32_t& width, int32_t& height) noexcept {
        MicaWin32kRect rect{};
        using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
        const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
        if (!getRect || !getRect(hwnd, &rect) || rect.right <= rect.left ||
            rect.bottom <= rect.top) return false;
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        return true;
    };
    if (message == kWmMouseMove || message == kWmLButtonDown ||
        message == kWmLButtonUp) {
        int32_t width{}, height{};
        if (!getDimensions(width, height)) return 0;
        const int32_t x = static_cast<int16_t>(static_cast<uint16_t>(lParam));
        const int32_t y = static_cast<int16_t>(static_cast<uint16_t>(
            static_cast<uintptr_t>(lParam) >> 16));
        uint32_t code{};
        bool redraw = false;
        bool release = false;
        ScrollBarState copy{};
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* bar = findScrollBar(hwnd);
            if (!bar) return 0;
            const uint32_t hit = scrollBarPartAt(*bar, x, y, width, height);
            if (message == kWmMouseMove) {
                redraw = bar->hotPart != hit;
                bar->hotPart = hit;
                if (bar->captured && bar->pressedPart == ((bar->style & kSbsVert) ? 6u : 5u)) {
                    const auto g = scrollBarGeometry(*bar, width, height);
                    const int32_t axis = g.vertical ? y : x;
                    const int32_t travel = std::max(0, g.trackLength - g.thumbLength);
                    const int32_t offset = std::clamp(axis - g.thumbLength / 2 - g.trackStart,
                                                     0, travel);
                    const int64_t range = int64_t(scrollBarMaximumPosition(*bar)) - bar->minimum;
                    bar->trackPosition = travel && range
                        ? static_cast<int32_t>(int64_t(bar->minimum) + range * offset / travel)
                        : bar->minimum;
                    if (bar->position != bar->trackPosition) {
                        bar->position = bar->trackPosition;
                        code = kSbThumbTrack;
                        redraw = true;
                    }
                }
            } else if (message == kWmLButtonDown) {
                if (bar->state & kButtonDisabled) return 0;
                if ((hit == 1 || hit == 3) && (bar->disabledArrows & 1u)) return 0;
                if ((hit == 2 || hit == 4) && (bar->disabledArrows & 2u)) return 0;
                bar->hotPart = hit;
                bar->pressedPart = hit;
                bar->captured = hit != 0;
                if (hit == ((bar->style & kSbsVert) ? 6u : 5u)) {
                    bar->trackPosition = bar->position;
                } else if (hit == 1 || hit == 3) {
                    bar->position = static_cast<int32_t>(std::max<int64_t>(
                        bar->minimum, int64_t(bar->position) - 1));
                    code = kSbLineUp;
                } else if (hit == 2 || hit == 4) {
                    bar->position = static_cast<int32_t>(std::min<int64_t>(
                        scrollBarMaximumPosition(*bar), int64_t(bar->position) + 1));
                    code = kSbLineDown;
                } else if (hit == 7) {
                    const int32_t page = static_cast<int32_t>(std::min<uint32_t>(bar->page, INT32_MAX));
                    bar->position = static_cast<int32_t>(std::max<int64_t>(
                        bar->minimum, int64_t(bar->position) - page));
                    code = kSbPageUp;
                } else if (hit == 8) {
                    const int32_t page = static_cast<int32_t>(std::min<uint32_t>(bar->page, INT32_MAX));
                    bar->position = static_cast<int32_t>(std::min<int64_t>(
                        scrollBarMaximumPosition(*bar), int64_t(bar->position) + page));
                    code = kSbPageDown;
                }
                redraw = true;
                copy = *bar;
            } else {
                if (bar->captured && bar->pressedPart == ((bar->style & kSbsVert) ? 6u : 5u)) {
                    bar->position = bar->trackPosition;
                    code = kSbThumbPosition;
                }
                release = bar->captured;
                bar->captured = false;
                bar->pressedPart = 0;
                copy = *bar;
                redraw = true;
            }
            if (!copy.hwnd) copy = *bar;
        }
        if (message == kWmLButtonDown && copy.captured) (void)User32_SetCapture(hwnd);
        if (release) (void)User32_ReleaseCapture();
        if (redraw) invalidateScrollBar(hwnd);
        if (code) notifyScrollBar(copy, code);
        if (message == kWmLButtonUp && release) notifyScrollBar(copy, kSbEndScroll);
        return 0;
    }
    if (message == kWmPaint) {
        ScrollBarState bar{};
        {
            ButtonLockGuard guard(g_buttonLock);
            const auto* current = findScrollBar(hwnd);
            if (!current) return 0;
            bar = *current;
        }
        WinNtPaintStruct paint{};
        void* dc = User32_BeginPaint(hwnd, &paint);
        if (!dc) return 0;
        int32_t width{}, height{};
        const bool hasDimensions = getDimensions(width, height);
        const bool drawn = hasDimensions && drawScrollBar(hwnd, bar, dc, width, height);
        const bool presented = User32_EndPaint(hwnd, &paint) != 0;
        return drawn && presented;
    }
    return User32_DefWindowProcW(hwnd, message, wParam, lParam);
}

intptr_t User32_EditWndProc(void* hwnd, uint32_t message,
                            uintptr_t wParam, intptr_t lParam) noexcept {
    if (message == kWmNcCreate) {
        const auto* create = reinterpret_cast<const MicaWin32kCreateStruct*>(lParam);
        if (!create || (static_cast<uint32_t>(create->style) & kEsUnsupportedMask))
            return 0;
        EditState initial{};
        initial.hwnd = hwnd;
        initial.parent = create->parent;
        initial.id = reinterpret_cast<uintptr_t>(create->menu);
        initial.style = static_cast<uint32_t>(create->style);
        if (initial.style & kWsDisabled) initial.state |= kButtonDisabled;
        if (create->title) {
            while (initial.length < std::size(initial.text) - 1 &&
                   create->title[initial.length]) {
                initial.text[initial.length] = create->title[initial.length];
                ++initial.length;
            }
            if (create->title[initial.length]) return 0;
        }
        ButtonLockGuard guard(g_buttonLock);
        for (auto& entry : g_edits) {
            if (entry.hwnd) continue;
            entry = initial;
            return 1;
        }
        return 0;
    }
    if (message == kWmSetText) {
        EditState replacement{};
        replacement.hwnd = hwnd;
        const auto* source = reinterpret_cast<const wchar_t*>(lParam);
        if (source) {
            while (replacement.length < std::size(replacement.text) - 1 &&
                   source[replacement.length]) {
                replacement.text[replacement.length] = source[replacement.length];
                ++replacement.length;
            }
            if (source[replacement.length]) return 0;
        }
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* edit = findEdit(hwnd);
            if (!edit) return 0;
            void* parent = edit->parent;
            const uintptr_t id = edit->id;
            const uint32_t state = edit->state;
            const uint32_t style = edit->style;
            *edit = replacement;
            edit->parent = parent;
            edit->id = id;
            edit->state = state;
            edit->style = style;
        }
        invalidateEdit(hwnd);
        notifyEditChanged(hwnd);
        return 1;
    }
    if (message == kWmGetTextLength) {
        ButtonLockGuard guard(g_buttonLock);
        const auto* edit = findEdit(hwnd);
        return edit ? edit->length : 0;
    }
    if (message == kWmGetText) {
        if (!lParam || !wParam) return 0;
        ButtonLockGuard guard(g_buttonLock);
        const auto* edit = findEdit(hwnd);
        if (!edit) return 0;
        const size_t copied = std::min<size_t>(edit->length, wParam - 1);
        auto* destination = reinterpret_cast<wchar_t*>(lParam);
        if (copied) std::memcpy(destination, edit->text, copied * sizeof(wchar_t));
        destination[copied] = L'\0';
        return static_cast<intptr_t>(copied);
    }
    if (message == kWmDestroy || message == kWmNcDestroy) {
        ButtonLockGuard guard(g_buttonLock);
        if (auto* edit = findEdit(hwnd)) *edit = {};
        return 0;
    }
    bool repaint = false;
    if (message == kWmSetFocus || message == kWmKillFocus || message == kWmEnable ||
        message == kWmThemeChanged || message == kWmSettingChange) {
        ButtonLockGuard guard(g_buttonLock);
        auto* edit = findEdit(hwnd);
        if (!edit) return 0;
        const uint32_t oldState = edit->state;
        if (message == kWmSetFocus) edit->state |= kButtonFocused;
        else if (message == kWmKillFocus) edit->state &= ~kButtonFocused;
        else if (message == kWmEnable) {
            if (wParam) edit->state &= ~kButtonDisabled;
            else edit->state |= kButtonDisabled;
        }
        repaint = oldState != edit->state || message == kWmThemeChanged ||
                  message == kWmSettingChange;
    }
    if (message == kWmChar) {
        bool changed = false;
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* edit = findEdit(hwnd);
            if (!edit || (edit->state & kButtonDisabled) ||
                (edit->style & kEsReadOnly)) return 0;
            if (wParam == 8u) {
                if (edit->length) {
                    --edit->length;
                    edit->text[edit->length] = L'\0';
                    repaint = changed = true;
                }
            } else if (wParam >= 0x20u && wParam <= 0xFFFFu &&
                       edit->length < std::size(edit->text) - 1) {
                edit->text[edit->length++] = static_cast<wchar_t>(wParam);
                edit->text[edit->length] = L'\0';
                repaint = changed = true;
            }
        }
        if (changed) notifyEditChanged(hwnd);
    }
    if (repaint) invalidateEdit(hwnd);
    if (message == kWmLButtonDown) (void)User32_SetFocus(hwnd);
    if (message == kWmPaint) {
        EditState edit{};
        {
            ButtonLockGuard guard(g_buttonLock);
            const auto* current = findEdit(hwnd);
            if (!current) return 0;
            edit = *current;
        }
        WinNtPaintStruct paint{};
        void* targetDc = User32_BeginPaint(hwnd, &paint);
        if (!targetDc) {
            return 0;
        }
        MicaWin32kRect client{};
        using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
        const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
        const bool hasRect = getRect && getRect(hwnd, &client);
        bool drawn = false;
        if (hasRect) drawn = drawEdit(hwnd, edit, targetDc, client.right, client.bottom);
        if (!drawn && hasRect && client.right > client.left && client.bottom > client.top)
            drawn = drawEditFallback(targetDc, edit, client.right, client.bottom);
        const bool presented = User32_EndPaint(hwnd, &paint) != 0;
        const uint8_t paintStatus = !hasRect ? 3 :
            (client.right <= client.left || client.bottom <= client.top) ? 4 :
            drawn && presented ? 6 : 5;
        return paintStatus == 6;
    }
    return User32_DefWindowProcW(hwnd, message, wParam, lParam);
}

intptr_t User32_DefWindowProcW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) noexcept {
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kDefWindowProcW");
    return fn ? fn(hWnd,Msg,wParam,lParam) : 0;
}

intptr_t User32_CallWindowProcW(void* procedure, void* window, uint32_t message,
    uintptr_t wParam, intptr_t lParam) noexcept {
    if (!procedure) {
        setThreadLastError(87);
        return 0;
    }
    using WindowProcedure = intptr_t (__attribute__((stdcall)) *)(
        void*, uint32_t, uintptr_t, intptr_t);
    return reinterpret_cast<WindowProcedure>(procedure)(window, message, wParam, lParam);
}

thread_local uint8_t g_threadKeyboardState[256]{};
thread_local uint8_t g_recentKeyPress[256]{};

void updateThreadKeyboardState(const WinNtMsg& message) noexcept {
    constexpr uint32_t kWmKeyDown = 0x0100u, kWmKeyUp = 0x0101u;
    constexpr uint32_t kWmSysKeyDown = 0x0104u, kWmSysKeyUp = 0x0105u;
    if (message.wParam >= 256) return;
    const auto key = static_cast<uint8_t>(message.wParam);
    if (message.message == kWmKeyDown || message.message == kWmSysKeyDown) {
        if (!(g_threadKeyboardState[key] & 0x80u)) {
            g_recentKeyPress[key] = 1;
            if (key == 0x14u || key == 0x90u || key == 0x91u)
                g_threadKeyboardState[key] ^= 0x01u;
        }
        g_threadKeyboardState[key] |= 0x80u;
    } else if (message.message == kWmKeyUp || message.message == kWmSysKeyUp) {
        g_threadKeyboardState[key] &= 0x7Fu;
    }
}

int32_t User32_GetKeyboardState(uint8_t* state) noexcept {
    if (!state) {
        constexpr uint32_t errorInvalidParameter = 87;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidParameter) : "memory");
        return 0;
    }
    std::memcpy(state, g_threadKeyboardState, sizeof(g_threadKeyboardState));
    return 1;
}

uintptr_t User32_GetKeyboardLayout(uint32_t threadId) noexcept {
    uint32_t currentThreadId{};
    __asm__ __volatile__("movl %%gs:0x48, %0" : "=r"(currentThreadId));
    if (threadId != 0 && threadId != currentThreadId) {
        // The current Win32k input owner has one active US layout and does not
        // yet publish layouts for other GUI threads. Match the native failure
        // shape instead of returning a fabricated layout for an unknown TID.
        constexpr uint32_t errorInvalidThreadId = 1444;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidThreadId) : "memory");
        return 0;
    }
    // The input stack currently emits US virtual-key/character pairs. This is
    // the sole installed layout until Win32k owns configurable per-thread KLs.
    return 0x04090409u; // US English (United States)
}

namespace {
bool validUsKeyboardLayout(uintptr_t layout) noexcept {
    return layout == 0 || layout == 0x04090409u;
}

uint8_t usVirtualKeyToScan(uint32_t key) noexcept {
    if (key >= 'A' && key <= 'Z') {
        constexpr uint8_t scans[26]{
            0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
            0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C};
        return scans[key - 'A'];
    }
    if (key >= '1' && key <= '9') {
        constexpr uint8_t scans[]{0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A};
        return scans[key - '1'];
    }
    if (key == '0') return 0x0B;
    switch (key) {
        case 0x08: return 0x0E; // Backspace
        case 0x09: return 0x0F; // Tab
        case 0x0D: return 0x1C; // Enter
        case 0x10: case 0xA0: return 0x2A;
        case 0xA1: return 0x36;
        case 0x11: case 0xA2: case 0xA3: return 0x1D;
        case 0x12: case 0xA4: case 0xA5: return 0x38;
        case 0x14: return 0x3A;
        case 0x1B: return 0x01;
        case 0x20: return 0x39;
        case 0x21: return 0x49; // Page Up
        case 0x22: return 0x51; // Page Down
        case 0x23: return 0x4F; // End
        case 0x24: return 0x47; // Home
        case 0x25: return 0x4B; // Left
        case 0x26: return 0x48; // Up
        case 0x27: return 0x4D; // Right
        case 0x28: return 0x50; // Down
        case 0x2D: return 0x52; // Insert
        case 0x2E: return 0x53; // Delete
        case 0x6A: return 0x37; // Numpad multiply
        case 0x6B: return 0x4E; // Numpad add
        case 0x6D: return 0x4A; // Numpad subtract
        case 0x6E: return 0x53; // Numpad decimal
        case 0x6F: return 0x35; // Numpad divide
        case 0xBA: return 0x27; // ;:
        case 0xBB: return 0x0D; // =+
        case 0xBC: return 0x33; // ,<
        case 0xBD: return 0x0C; // -_
        case 0xBE: return 0x34; // .>
        case 0xBF: return 0x35; // /?
        case 0xC0: return 0x29; // `~
        case 0xDB: return 0x1A; // [{
        case 0xDC: return 0x2B; // \\
        case 0xDD: return 0x1B; // ]}
        case 0xDE: return 0x28; // '
        default: break;
    }
    if (key >= 0x70 && key <= 0x7B) return static_cast<uint8_t>(0x3B + key - 0x70);
    if (key >= 0x60 && key <= 0x69) {
        constexpr uint8_t scans[]{0x52,0x4F,0x50,0x51,0x4B,0x4C,0x4D,0x47,0x48,0x49};
        return scans[key - 0x60];
    }
    return 0;
}

wchar_t usVirtualKeyToCharacter(uint32_t key) noexcept {
    if (key >= 'A' && key <= 'Z') return static_cast<wchar_t>(key - 'A' + 'a');
    if (key >= '0' && key <= '9') return static_cast<wchar_t>(key);
    switch (key) {
        case 0x20: return L' ';
        case 0xBA: return L';'; case 0xBB: return L'='; case 0xBC: return L',';
        case 0xBD: return L'-'; case 0xBE: return L'.'; case 0xBF: return L'/';
        case 0xC0: return L'`'; case 0xDB: return L'['; case 0xDC: return L'\\';
        case 0xDD: return L']'; case 0xDE: return L'\'';
        default: return 0;
    }
}
}

uint32_t User32_MapVirtualKeyExW(uint32_t code, uint32_t mapType,
                                uintptr_t layout) noexcept {
    if (!validUsKeyboardLayout(layout)) return 0;
    if (mapType == 0 || mapType == 4) return usVirtualKeyToScan(code);
    if (mapType == 2) return static_cast<uint32_t>(usVirtualKeyToCharacter(code));
    if (mapType == 1 || mapType == 3) {
        const uint32_t scan = code & 0xFFu;
        if (scan == 0x1E) return 'A'; if (scan == 0x30) return 'B';
        if (scan == 0x2E) return 'C'; if (scan == 0x20) return 'D';
        if (scan == 0x12) return 'E'; if (scan == 0x21) return 'F';
        if (scan == 0x22) return 'G'; if (scan == 0x23) return 'H';
        if (scan == 0x17) return 'I'; if (scan == 0x24) return 'J';
        if (scan == 0x25) return 'K'; if (scan == 0x26) return 'L';
        if (scan == 0x32) return 'M'; if (scan == 0x31) return 'N';
        if (scan == 0x18) return 'O'; if (scan == 0x19) return 'P';
        if (scan == 0x10) return 'Q'; if (scan == 0x13) return 'R';
        if (scan == 0x1F) return 'S'; if (scan == 0x14) return 'T';
        if (scan == 0x16) return 'U'; if (scan == 0x2F) return 'V';
        if (scan == 0x11) return 'W'; if (scan == 0x2D) return 'X';
        if (scan == 0x15) return 'Y'; if (scan == 0x2C) return 'Z';
        if (scan >= 0x02 && scan <= 0x0A) return '1' + scan - 0x02;
        if (scan == 0x0B) return '0';
        if (scan == 0x1C) return 0x0D;
        if (scan == 0x39) return 0x20;
        if (scan == 0x0F) return 0x09;
        if (mapType == 3 && (code & 0xFF00u) == 0xE000u) {
            switch (scan) {
                case 0x47: return 0x24; case 0x48: return 0x26;
                case 0x49: return 0x21; case 0x4B: return 0x25;
                case 0x4D: return 0x27; case 0x4F: return 0x23;
                case 0x50: return 0x28; case 0x51: return 0x22;
                case 0x52: return 0x2D; case 0x53: return 0x2E;
                default: return 0;
            }
        }
        return 0;
    }
    return 0;
}

int32_t User32_ToAsciiEx(uint32_t virtualKey, uint32_t /*scanCode*/,
    const uint8_t* keyState, uint16_t* characters, uint32_t /*flags*/,
    uintptr_t layout) noexcept {
    if (!keyState || !characters || !validUsKeyboardLayout(layout)) return 0;
    bool shift = (keyState[0x10] & 0x80u) ||
                 (keyState[0xA0] & 0x80u) || (keyState[0xA1] & 0x80u);
    const bool control = (keyState[0x11] & 0x80u) ||
                         (keyState[0xA2] & 0x80u) || (keyState[0xA3] & 0x80u);
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        wchar_t ch = static_cast<wchar_t>(virtualKey - 'A' + 'a');
        if (shift ^ ((keyState[0x14] & 1u) != 0)) ch = static_cast<wchar_t>(ch - 'a' + 'A');
        if (control) ch = static_cast<wchar_t>(virtualKey - 'A' + 1);
        *characters = static_cast<uint16_t>(ch);
        return 1;
    }
    wchar_t ch = usVirtualKeyToCharacter(virtualKey);
    if (!ch) return 0;
    if (virtualKey >= '0' && virtualKey <= '9' && shift) {
        constexpr wchar_t shiftedDigits[]{L')',L'!',L'@',L'#',L'$',L'%',L'^',L'&',L'*',L'('};
        ch = shiftedDigits[virtualKey - '0'];
    } else if (shift) {
        switch (virtualKey) {
            case 0xBA: ch = L':'; break; case 0xBB: ch = L'+'; break;
            case 0xBC: ch = L'<'; break; case 0xBD: ch = L'_'; break;
            case 0xBE: ch = L'>'; break; case 0xBF: ch = L'?'; break;
            case 0xC0: ch = L'~'; break; case 0xDB: ch = L'{'; break;
            case 0xDC: ch = L'|'; break; case 0xDD: ch = L'}'; break;
            case 0xDE: ch = L'"'; break; default: break;
        }
    }
    *characters = static_cast<uint16_t>(ch);
    return 1;
}

int16_t User32_GetAsyncKeyState(int32_t vKey) noexcept {
    if (vKey < 0 || vKey >= 256) return 0;
    const auto key = static_cast<size_t>(vKey);
    const int16_t result = static_cast<int16_t>(
        (g_threadKeyboardState[key] & 0x80u ? 0x8000u : 0u) |
        (g_recentKeyPress[key] ? 0x0001u : 0u));
    g_recentKeyPress[key] = 0;
    return result;
}

int32_t User32_PeekMessageW(WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg) noexcept {
    updateAnimatedCursor();
    if (!lpMsg) return 0;
    MicaWin32kMessage msg{};
    using Fn=int32_t (*)(MicaWin32kMessage*,void*,uint32_t,uint32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kPeekMessageW");
    const auto ok = fn ? fn(&msg,hWnd,wMsgFilterMin,wMsgFilterMax,wRemoveMsg) : 0;
    if (!ok) return 0;
    lpMsg->hwnd=msg.hwnd; lpMsg->message=msg.message; lpMsg->wParam=msg.wParam;
    lpMsg->lParam=msg.lParam; lpMsg->time=msg.time; lpMsg->pt={msg.pt.x,msg.pt.y};
    if (wRemoveMsg & 0x0001u) updateThreadKeyboardState(*lpMsg);
    return 1;
}

int32_t User32_GetMessageW(WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax) noexcept {
    if (!lpMsg) return -1;
    using Fn=int32_t (*)(MicaWin32kMessage*,void*,uint32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kGetMessageW");
    if (!fn) return -1;
    for (;;) {
        updateAnimatedCursor();
        MicaWin32kMessage msg{};
        const int32_t result = fn(&msg,hWnd,wMsgFilterMin,wMsgFilterMax);
        if (result != -2) {
            if (result > 0) {
                lpMsg->hwnd=msg.hwnd;lpMsg->message=msg.message;lpMsg->wParam=msg.wParam;
                lpMsg->lParam=msg.lParam;lpMsg->time=msg.time;lpMsg->pt={msg.pt.x,msg.pt.y};
                updateThreadKeyboardState(*lpMsg);
            }
            return result;
        }
        micant::LargeInteger interval{};
        interval.QuadPart = -300000; // 30ms: cap idle input/ANI polling at about 33 Hz.
        if (micant::native_nt::NtDelayExecution(0, &interval) != micant::NtStatus::Success)
            return -1;
    }
}

int32_t User32_PostMessageW(void* hWnd, uint32_t message,
                            uintptr_t wParam, intptr_t lParam) noexcept;

int32_t User32_TranslateMessage(const WinNtMsg* lpMsg) noexcept {
    constexpr uint32_t WM_KEYDOWN = 0x0100u;
    constexpr uint32_t WM_CHAR = 0x0102u;
    if (!lpMsg || lpMsg->message != WM_KEYDOWN) return 0;

    // The kernel keyboard path queues both WM_KEYDOWN and its WM_CHAR for
    // printable and control keys. Reposting here would duplicate Backspace,
    // Enter, and Tab in native message loops.
    (void)WM_CHAR;
    return 0;
}

intptr_t User32_DispatchMessageW(const WinNtMsg* lpMsg) noexcept {
    if (!lpMsg) return 0;
    if (lpMsg->message == 0x0200u) {
        bool buttonTarget = false;
        {
            ButtonLockGuard guard(g_buttonLock);
            buttonTarget = findButton(lpMsg->hwnd) != nullptr;
        }
        if (!buttonTarget) leaveHotButton(nullptr);
    }
    if (dispatchAttachedScrollInput(*lpMsg)) return 0;
    using Fn=intptr_t (*)(const MicaWin32kMessage*);
    auto fn=Win32kProc<Fn>("MicaWin32kDispatchMessageW");
    MicaWin32kMessage msg{lpMsg->hwnd,lpMsg->message,lpMsg->wParam,lpMsg->lParam,lpMsg->time,{lpMsg->pt.x,lpMsg->pt.y}};
    const intptr_t result = fn ? fn(&msg)
        : User32_DefWindowProcW(lpMsg->hwnd,lpMsg->message,lpMsg->wParam,lpMsg->lParam);
    if (lpMsg->message == 0x0020u && result == 0) {
        constexpr uint32_t HTCAPTION = 2, HTLEFT = 10, HTRIGHT = 11, HTTOP = 12;
        constexpr uint32_t HTTOPLEFT = 13, HTTOPRIGHT = 14, HTBOTTOM = 15;
        constexpr uint32_t HTBOTTOMLEFT = 16, HTBOTTOMRIGHT = 17;
        constexpr uintptr_t IDC_ARROW = 32512, IDC_SIZEWE = 32644, IDC_SIZENS = 32645;
        constexpr uintptr_t IDC_SIZEALL = 32646, IDC_SIZENWSE = 32642, IDC_SIZENESW = 32643;
        void* cursor{};
        const uint32_t hitTest = static_cast<uint16_t>(lpMsg->lParam);
        uintptr_t cursorId = IDC_ARROW;
        switch (hitTest) {
        case HTLEFT: case HTRIGHT: cursorId = IDC_SIZEWE; break;
        case HTTOP: case HTBOTTOM: cursorId = IDC_SIZENS; break;
        case HTTOPLEFT: case HTBOTTOMRIGHT: cursorId = IDC_SIZENWSE; break;
        case HTTOPRIGHT: case HTBOTTOMLEFT: cursorId = IDC_SIZENESW; break;
        case HTCAPTION: cursorId = IDC_SIZEALL; break;
        default: break;
        }
        if (hitTest != 1u) cursor = User32_LoadCursorW(nullptr,
            reinterpret_cast<const wchar_t*>(cursorId));
        if (auto* window = findRegisteredWindowClass(lpMsg->hwnd);
            !cursor && window && window->icons) cursor = window->icons->cursor;
        if (!cursor) cursor = User32_LoadCursorW(nullptr,
            reinterpret_cast<const wchar_t*>(IDC_ARROW));
        if (cursor) { (void)User32_SetCursor(cursor); return 1; }
    }
    return result;
}

void User32_PostQuitMessage(int32_t exitCode) noexcept {
    using Fn=void (*)(int32_t); auto fn=Win32kProc<Fn>("MicaWin32kPostQuitMessage");
    if (fn) fn(exitCode);
    g_primaryWindow.pendingMessages = 0;
}

int32_t User32_PostMessageW(void* hWnd, uint32_t message, uintptr_t wParam, intptr_t lParam) noexcept {
    using Fn=int32_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kPostMessageW");
    return fn ? fn(hWnd,message,wParam,lParam) : 0;
}

intptr_t User32_SendMessageW(void* hWnd, uint32_t message, uintptr_t wParam, intptr_t lParam) noexcept {
    // Built-in STATIC controls are implemented in this DLL. Keep their text
    // messages in the same user-mode window-procedure path as their painting;
    // this also preserves WM_SETTEXT's result instead of relying on the
    // kernel-side default procedure for a system class.
    if (message == 0x000Cu || message == 0x000Du || message == 0x000Eu ||
        message == 0x000Fu) {
        bool isStatic{};
        {
            ButtonLockGuard guard(g_buttonLock);
            for (const auto& entry : g_statics) {
                if (entry.hwnd == hWnd) { isStatic = true; break; }
            }
        }
        if (isStatic)
            return User32_StaticWndProc(hWnd, message, wParam, lParam);
    }
    using SetIconFn=int32_t (*)(void*,uint32_t,uint64_t,uint64_t*);
    using GetIconFn=uint64_t (*)(void*,uint32_t);
    if (message == WM_SETICON_OFFICIAL) {
        if (wParam > 2) return 0;
        void* icon = reinterpret_cast<void*>(lParam);
        winuser::IconHandleTable::Lease validation{};
        if (icon && !winuser::ProcessIconHandles().acquire(icon, validation)) return 0;
        uint64_t previous{};
        if (!setWindowIconPixels(hWnd, static_cast<uint32_t>(wParam), icon,
                                 &previous)) return 0;
        (void)winuser::ProcessWindowIcons().set(hWnd, static_cast<uint32_t>(wParam), icon);
        if (icon && winuser::ProcessWindowIcons().get(hWnd, static_cast<uint32_t>(wParam)) != icon) {
            uint64_t ignored{};
            (void)setWindowIconPixels(hWnd, static_cast<uint32_t>(wParam),
                reinterpret_cast<void*>(previous), &ignored);
            return 0;
        }
        return static_cast<intptr_t>(previous);
    }
    if (message == WM_GETICON_OFFICIAL) {
        if (wParam > 2) return 0;
        auto get=Win32kProc<GetIconFn>("MicaWin32kGetWindowIcon");
        return get ? static_cast<intptr_t>(get(hWnd, static_cast<uint32_t>(wParam))) : 0;
    }
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kSendMessageW");
    const intptr_t result = fn ? fn(hWnd,message,wParam,lParam) : 0;
    if (message == 0x000Cu /* WM_SETTEXT */ && result && hasWindowCaption(hWnd))
        (void)publishCaptionTitleSurface(hWnd,
            reinterpret_cast<const wchar_t*>(lParam));
    return result;
}

int32_t User32_GetClientRect(void* hWnd, WinNtRect* lpRect) noexcept {
    if (!hWnd || !lpRect) return 0;
    using Fn=int32_t (*)(void*,MicaWin32kRect*); auto fn=Win32kProc<Fn>("MicaWin32kGetClientRect");
    return fn ? fn(hWnd,reinterpret_cast<MicaWin32kRect*>(lpRect)) : 0;
}

int32_t User32_GetWindowRect(void* hWnd, WinNtRect* lpRect) noexcept {
    if(!hWnd||!lpRect)return 0;
    using Fn=int32_t (*)(void*,MicaWin32kRect*); auto fn=Win32kProc<Fn>("MicaWin32kGetWindowRect");
    return fn ? fn(hWnd,reinterpret_cast<MicaWin32kRect*>(lpRect)) : 0;
}

int32_t User32_EnumWindows(void* callback, intptr_t parameter) noexcept {
    if (!callback) return 0;
    using Fn=int32_t (*)(void*,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kEnumWindows");
    return fn ? fn(callback,parameter) : 0;
}

void* User32_GetDlgItem(void* dialog, int32_t controlId) noexcept {
    if (!dialog) return nullptr;
    using Fn = void* (*)(void*, int32_t);
    const auto fn = Win32kProc<Fn>("MicaWin32kGetDlgItem");
    return fn ? fn(dialog, controlId) : nullptr;
}

uint32_t User32_GetDlgItemTextW(void* dialog, int32_t controlId,
    wchar_t* text, int32_t capacity) noexcept {
    if (capacity <= 0) return 0;
    if (!text) {
        constexpr uint32_t errorInvalidParameter = 87;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidParameter) : "memory");
        return 0;
    }
    text[0] = L'\0';
    void* control = User32_GetDlgItem(dialog, controlId);
    if (!control) return 0;
    const intptr_t copied = User32_SendMessageW(control, 0x000Du /* WM_GETTEXT */,
        static_cast<uintptr_t>(capacity), reinterpret_cast<intptr_t>(text));
    return copied > 0 ? static_cast<uint32_t>(copied) : 0;
}

int32_t User32_SetDlgItemTextW(void* dialog, int32_t controlId,
    const wchar_t* text) noexcept {
    if (!dialog || !text) {
        constexpr uint32_t errorInvalidParameter = 87;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidParameter) : "memory");
        return 0;
    }
    void* control = User32_GetDlgItem(dialog, controlId);
    if (!control) {
        constexpr uint32_t errorInvalidWindowHandle = 1400;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidWindowHandle) : "memory");
        return 0;
    }
    if (controlId == 1074) {
        size_t length{};
        while (length < 255 && text[length]) ++length;
        using TraceFn = void (*)(uint32_t, uint32_t, uint32_t);
        const auto trace = Win32kProc<TraceFn>("MicaWin32kDebugInputTrace");
        if (trace) {
            const uint32_t first = length ? static_cast<uint32_t>(text[0]) : 0u;
            trace(8, static_cast<uint32_t>(controlId),
                  (static_cast<uint32_t>(length) << 16) | (first & 0xFFFFu));
        }
    }
    return User32_SendMessageW(control, 0x000Cu /* WM_SETTEXT */, 0,
        reinterpret_cast<intptr_t>(text)) != 0;
}

uint32_t User32_IsDlgButtonChecked(void* dialog, int32_t controlId) noexcept {
    void* control = User32_GetDlgItem(dialog, controlId);
    if (!control) return 0;
    const intptr_t state = User32_SendMessageW(control, 0x00F0u /* BM_GETCHECK */, 0, 0);
    return state >= 0 && state <= 2 ? static_cast<uint32_t>(state) : 0;
}
intptr_t User32_SendDlgItemMessageW(void* dialog, int32_t controlId,
    uint32_t message, uintptr_t wParam, intptr_t lParam) noexcept {
    void* control = User32_GetDlgItem(dialog, controlId);
    return control ? User32_SendMessageW(control, message, wParam, lParam) : 0;
}

int32_t User32_CheckRadioButton(void* dialog, uint32_t firstId,
    uint32_t lastId, uint32_t checkId) noexcept {
    if (!dialog || firstId > lastId ||
        static_cast<uint64_t>(lastId) - firstId >= 1024u) {
        setThreadLastError(87);
        return 0;
    }
    for (uint32_t id = firstId;; ++id) {
        if (void* control = User32_GetDlgItem(dialog, static_cast<int32_t>(id))) {
            (void)User32_SendMessageW(control, 0x00F1u /* BM_SETCHECK */,
                id == checkId ? 1u : 0u, 0);
        }
        if (id == lastId) break;
    }
    return 1;
}
int32_t User32_CheckDlgButton(void* dialog, int32_t controlId,
    uint32_t check) noexcept {
    if (!dialog || check > 2) {
        setThreadLastError(87);
        return 0;
    }
    void* control = User32_GetDlgItem(dialog, controlId);
    if (!control) return 0;
    return User32_SendMessageW(control, 0x00F1u /* BM_SETCHECK */, check, 0) == 0;
}

int32_t User32_GetClassNameW(void* window, wchar_t* output,
                             int32_t capacity) noexcept {
    if (output && capacity > 0) output[0] = L'\0';
    if (!window || !output || capacity <= 0) return 0;
    using Fn = int32_t (*)(void*, wchar_t*, int32_t);
    const auto fn = Win32kProc<Fn>("MicaWin32kGetClassNameW");
    return fn ? fn(window, output, capacity) : 0;
}

int32_t User32_EnumChildWindows(void* parent, void* callback,
                                intptr_t parameter) noexcept {
    if (!callback) return 0;
    if (!parent) return User32_EnumWindows(callback, parameter);
    using Fn = int32_t (*)(void*, void*, intptr_t);
    const auto fn = Win32kProc<Fn>("MicaWin32kEnumChildWindows");
    return fn ? fn(parent, callback, parameter) : 0;
}

int32_t User32_EnableWindow(void* window, int32_t enable) noexcept {
    if (!window) {
        constexpr uint32_t errorInvalidWindowHandle = 1400;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidWindowHandle) : "memory");
        return 0;
    }
    const bool wasEnabled = User32_IsWindowEnabled(window) != 0;
    if (!enable && wasEnabled)
        (void)User32_SendMessageW(window, 0x001Fu /* WM_CANCELMODE */, 0, 0);
    using Fn = int32_t (*)(void*, int32_t, int32_t*, int32_t*);
    const auto fn = Win32kProc<Fn>("MicaWin32kEnableWindow");
    int32_t wasDisabled{}, changed{};
    if (!fn || !fn(window, enable, &wasDisabled, &changed)) {
        constexpr uint32_t errorInvalidWindowHandle = 1400;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidWindowHandle) : "memory");
        return 0;
    }
    if (!enable && changed && User32_GetFocus() == window)
        (void)User32_SetFocus(nullptr);
    if (changed) (void)User32_SendMessageW(window, 0x000Au /* WM_ENABLE */,
                                            enable != 0, 0);
    return wasDisabled;
}

int32_t User32_IsWindowEnabled(void* window) noexcept {
    if (!window) {
        setThreadLastError(1400);
        return 0;
    }
    constexpr int32_t kGwlStyle = -16;
    constexpr uintptr_t kWsDisabled = 0x08000000u;
    return (static_cast<uintptr_t>(User32_GetWindowLongPtrW(window, kGwlStyle)) &
        kWsDisabled) == 0;
}
int32_t User32_IsWindow(void* window) noexcept {
    WinNtRect bounds{};
    return window && User32_GetWindowRect(window, &bounds);
}
void* User32_GetParent(void* window) noexcept {
    if (!window) return nullptr;
    using Fn = void* (*)(void*);
    const auto fn = Win32kProc<Fn>("MicaWin32kGetParent");
    return fn ? fn(window) : nullptr;
}

int32_t User32_DrawStateW(void* dc, void*, void* callback, intptr_t data,
    uintptr_t count, int32_t x, int32_t y, int32_t width, int32_t height,
    uint32_t flags) noexcept {
    constexpr uint32_t kDstText = 0x0001u;
    constexpr uint32_t kDssDisabled = 0x0020u;
    constexpr uint32_t kDssMask = 0x00FFu;
    if (!dc || callback || (flags & 0x000Fu) != kDstText ||
        (flags & ~(0x000Fu | kDssMask)) != 0 || count > 4096u) {
        setThreadLastError(87); // ERROR_INVALID_PARAMETER
        return 0;
    }
    const auto* text = reinterpret_cast<const wchar_t*>(data);
    if (!text) { setThreadLastError(87); return 0; }
    size_t length = static_cast<size_t>(count);
    if (!length) {
        while (length < 4096u && text[length]) ++length;
        if (length == 4096u) { setThreadLastError(87); return 0; }
    }
    if (width <= 0 || height <= 0) return 1;
    if (flags & kDssDisabled) {
        const uint32_t prior = SetTextColor(dc, 0x00FFFFFFu);
        const bool highlight = TextOutW(dc, x + 1, y + 1, text,
            static_cast<int32_t>(length)) != 0;
        if (prior != UINT32_MAX) (void)SetTextColor(dc, prior);
        if (!highlight) return 0;
        const uint32_t priorDisabled = SetTextColor(dc, 0x00808080u);
        const bool drawn = TextOutW(dc, x, y, text,
            static_cast<int32_t>(length)) != 0;
        if (priorDisabled != UINT32_MAX) (void)SetTextColor(dc, priorDisabled);
        return drawn ? 1 : 0;
    }
    return TextOutW(dc, x, y, text, static_cast<int32_t>(length));
}

int32_t User32_DrawFrameControl(void* dc, WinNtRect* rect, uint32_t type,
    uint32_t state) noexcept {
    constexpr uint32_t kDfcButton = 4u, kDfcsButtonPush = 0x0010u;
    constexpr uint32_t kPushed = 0x0200u, kInactive = 0x0100u;
    constexpr uint32_t kFlat = 0x4000u, kMono = 0x8000u;
    constexpr uint32_t kAdjustRect = 0x2000u;
    if (!dc || !rect || type != kDfcButton ||
        (state & 0x00F0u) != kDfcsButtonPush ||
        (state & ~(kDfcsButtonPush | kPushed | kInactive | kFlat | kMono | kAdjustRect))) {
        setThreadLastError(87);
        return 0;
    }
    if (rect->right <= rect->left || rect->bottom <= rect->top) {
        setThreadLastError(87);
        return 0;
    }
    constexpr uint32_t kFace = 0x00F0F0F0u;
    const bool sunken = (state & kPushed) != 0;
    const uint32_t topLeft = (state & kInactive) ? 0x00FFFFFFu
        : sunken ? 0x00808080u : 0x00FFFFFFu;
    const uint32_t bottomRight = (state & kInactive) ? 0x00C8C8C8u
        : sunken ? 0x00FFFFFFu : 0x00808080u;
    auto fill = [dc](const WinNtRect& area, uint32_t color) noexcept {
        void* brush = CreateSolidBrush(color);
        if (!brush) return false;
        const bool ok = FillRect(dc, &area, brush) != 0;
        (void)DeleteObject(brush);
        return ok;
    };
    WinNtRect inner = *rect;
    if (!fill(inner, kFace)) return 0;
    if (state & kFlat) {
        if (!fill({rect->left, rect->top, rect->right, rect->top + 1}, bottomRight) ||
            !fill({rect->left, rect->bottom - 1, rect->right, rect->bottom}, bottomRight) ||
            !fill({rect->left, rect->top, rect->left + 1, rect->bottom}, bottomRight) ||
            !fill({rect->right - 1, rect->top, rect->right, rect->bottom}, bottomRight)) return 0;
    } else {
        if (!fill({rect->left, rect->top, rect->right, rect->top + 1}, topLeft) ||
            !fill({rect->left, rect->top, rect->left + 1, rect->bottom}, topLeft) ||
            !fill({rect->left, rect->bottom - 1, rect->right, rect->bottom}, bottomRight) ||
            !fill({rect->right - 1, rect->top, rect->right, rect->bottom}, bottomRight)) return 0;
    }
    if (state & kAdjustRect) {
        ++rect->left; ++rect->top; --rect->right; --rect->bottom;
    }
    return 1;
}

struct WinNtTrackMouseEvent {
    uint32_t size{};
    uint32_t flags{};
    void* window{};
    uint32_t hoverTime{};
};
static_assert(sizeof(WinNtTrackMouseEvent) == 24);

int32_t User32_TrackMouseEvent(WinNtTrackMouseEvent* event) noexcept {
    constexpr uint32_t tmeHover = 0x00000001u;
    constexpr uint32_t tmeLeave = 0x00000002u;
    constexpr uint32_t tmeNonClient = 0x00000010u;
    constexpr uint32_t tmeCancel = 0x80000000u;
    constexpr uint32_t tmeQuery = 0x40000000u;
    constexpr uint32_t supported = tmeHover | tmeLeave | tmeNonClient |
                                   tmeCancel | tmeQuery;
    if (!event || event->size != sizeof(*event) || !event->window ||
        (event->flags & ~supported)) {
        constexpr uint32_t errorInvalidParameter = 87;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidParameter) : "memory");
        return 0;
    }
    using Fn = int32_t (*)(void*, uint32_t, uint32_t, uint32_t*, uint32_t*);
    const auto fn = Win32kProc<Fn>("MicaWin32kTrackMouseEvent");
    if (!fn) {
        constexpr uint32_t errorNotSupported = 50;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorNotSupported) : "memory");
        return 0;
    }
    uint32_t currentFlags{}, currentHoverTime{};
    if (!fn(event->window, event->flags, event->hoverTime,
            &currentFlags, &currentHoverTime)) {
        constexpr uint32_t errorInvalidWindowHandle = 1400;
        __asm__ __volatile__("movl %0, %%gs:0x68" : : "r"(errorInvalidWindowHandle) : "memory");
        return 0;
    }
    if (event->flags & tmeQuery) {
        event->flags = currentFlags;
        event->hoverTime = currentHoverTime;
    }
    return 1;
}

int32_t User32_GetWindowTextW(void* hWnd, wchar_t* lpString, int32_t nMaxCount) noexcept {
    if (lpString && nMaxCount > 0) lpString[0]=L'\0';
    if (!hWnd || !lpString || nMaxCount <= 0) return 0;
    using Fn=int32_t (*)(void*,wchar_t*,int32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kGetWindowTextW");
    return fn ? fn(hWnd,lpString,nMaxCount) : 0;
}

int32_t User32_SetWindowTextW(void* hWnd, const wchar_t* lpString) noexcept {
    if (!hWnd || !lpString) return 0;
    return User32_SendMessageW(hWnd, 0x000Cu /* WM_SETTEXT */, 0,
        reinterpret_cast<intptr_t>(lpString)) != 0;
}

int32_t User32_IsWindowVisible(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kIsWindowVisible");
    return fn ? fn(hWnd) : 0;
}

int32_t User32_IsIconic(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kIsIconic");
    return fn ? fn(hWnd) : 0;
}

void* User32_GetForegroundWindow() noexcept {
    using Fn=void* (*)(void);
    auto fn=Win32kProc<Fn>("MicaWin32kGetForegroundWindow");
    return fn ? fn() : nullptr;
}

void* User32_SetFocus(void* hWnd) noexcept {
    using Fn=void* (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kSetFocus");
    return fn ? fn(hWnd) : nullptr;
}

void* User32_GetFocus() noexcept {
    using Fn=void* (*)(void);
    auto fn=Win32kProc<Fn>("MicaWin32kGetFocus");
    return fn ? fn() : nullptr;
}

void* User32_GetCapture() noexcept {
    using Fn=void* (*)(void);
    auto fn=Win32kProc<Fn>("MicaWin32kGetCapture");
    return fn ? fn() : nullptr;
}

void* User32_SetCapture(void* hWnd) noexcept {
    using Fn=void* (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kSetCapture");
    return fn ? fn(hWnd) : nullptr;
}

int32_t User32_ReleaseCapture() noexcept {
    using Fn=int32_t (*)(void);
    auto fn=Win32kProc<Fn>("MicaWin32kReleaseCapture");
    return fn ? fn() : 0;
}

int32_t User32_AdjustWindowRectEx(WinNtRect* lpRect, uint32_t dwStyle,
                                  int32_t bMenu, uint32_t dwExStyle) noexcept {
    if (!lpRect || (bMenu != 0 && bMenu != 1)) return 0;
    using Fn = int32_t (*)(MicaWin32kRect*, uint32_t, int32_t, uint32_t);
    const auto fn = Win32kProc<Fn>("MicaWin32kAdjustWindowRectEx");
    return fn ? fn(reinterpret_cast<MicaWin32kRect*>(lpRect), dwStyle,
                   bMenu, dwExStyle) : 0;
}

intptr_t User32_SetWindowLongPtrW(void* hWnd, int32_t nIndex, intptr_t dwNewLong) noexcept {
    using Fn=intptr_t (*)(void*,int32_t,intptr_t); auto fn=Win32kProc<Fn>("MicaWin32kSetWindowLongPtrW");
    return fn ? fn(hWnd,nIndex,dwNewLong) : 0;
}

intptr_t User32_GetWindowLongPtrW(void* hWnd, int32_t nIndex) noexcept {
    using Fn=intptr_t (*)(void*,int32_t); auto fn=Win32kProc<Fn>("MicaWin32kGetWindowLongPtrW");
    return fn ? fn(hWnd,nIndex) : 0;
}

int32_t User32_SetWindowPos(void* hWnd, void* hWndInsertAfter, int32_t X, int32_t Y, int32_t cx, int32_t cy, uint32_t uFlags) noexcept {
    if (!hWnd) return 0;
    using Fn=int32_t (*)(void*,void*,int32_t,int32_t,int32_t,int32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kSetWindowPos"); return fn ? fn(hWnd,hWndInsertAfter,X,Y,cx,cy,uFlags) : 0;
}

int32_t User32_MoveWindow(void* window, int32_t x, int32_t y, int32_t width,
    int32_t height, int32_t repaint) noexcept {
    if (!window || width < 0 || height < 0) {
        setThreadLastError(window ? 87u : 1400u);
        return 0;
    }
    constexpr uint32_t kSwpNoZOrder = 0x0004u;
    constexpr uint32_t kSwpNoActivate = 0x0010u;
    constexpr uint32_t kSwpNoRedraw = 0x0008u;
    if (!User32_SetWindowPos(window, nullptr, x, y, width, height,
            kSwpNoZOrder | kSwpNoActivate | (repaint ? 0u : kSwpNoRedraw))) {
        setThreadLastError(1400);
        return 0;
    }
    if (repaint) {
        (void)User32_InvalidateRect(window, nullptr, 1);
        (void)User32_UpdateWindow(window);
    }
    return 1;
}

int32_t User32_RegisterRawInputDevices(const WinNtRawInputDevice* pRawInputDevices, uint32_t uiNumDevices, uint32_t cbSize) noexcept {
    if (!pRawInputDevices || uiNumDevices == 0 || cbSize < sizeof(WinNtRawInputDevice)) return 0;
    return 1;
}

uint32_t User32_GetRawInputData(void* /*hRawInput*/, uint32_t uiCommand, void* pData, uint32_t* pcbSize, uint32_t cbSizeHeader) noexcept {
    if (!pcbSize || cbSizeHeader != sizeof(WinNtRawInputHeader) || uiCommand != RID_INPUT_OFFICIAL) {
        return 0xFFFFFFFFU;
    }
    if (!pData) {
        *pcbSize = sizeof(WinNtRawInputPacket);
        return 0;
    }
    if (*pcbSize < sizeof(WinNtRawInputPacket)) {
        return 0xFFFFFFFFU;
    }
    WinNtRawInputPacket pkt{};
    pkt.header.dwType  = RIM_TYPEMOUSE_OFFICIAL;
    pkt.header.dwSize  = sizeof(WinNtRawInputPacket);
    pkt.header.hDevice = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00000000000B0100ULL));
    pkt.mouse.lLastX   = 14;
    pkt.mouse.lLastY   = -9;
    std::memcpy(pData, &pkt, sizeof(pkt));
    *pcbSize = sizeof(WinNtRawInputPacket);
    return sizeof(WinNtRawInputPacket);
}

int32_t User32_GetCursorPos(WinNtPoint* lpPoint) noexcept {
    if (!lpPoint) return 0;
    using Fn = int32_t (*)(MicaWin32kPoint*);
    auto fn = Win32kProc<Fn>("MicaWin32kGetCursorPos");
    return fn ? fn(reinterpret_cast<MicaWin32kPoint*>(lpPoint)) : 0;
}

int32_t User32_SetCursorPos(int32_t X, int32_t Y) noexcept {
    using Fn = int32_t (*)(int32_t, int32_t);
    auto fn = Win32kProc<Fn>("MicaWin32kSetCursorPos");
    return fn ? fn(X, Y) : 0;
}

int32_t User32_ClipCursor(const WinNtRect* lpRect) noexcept {
    using Fn = int32_t (*)(const MicaWin32kRect*);
    auto fn = Win32kProc<Fn>("MicaWin32kClipCursor");
    return fn ? fn(reinterpret_cast<const MicaWin32kRect*>(lpRect)) : 0;
}

int32_t User32_GetClipCursor(WinNtRect* output) noexcept {
    if (!output) return 0;
    using Fn = int32_t (*)(MicaWin32kRect*);
    auto fn = Win32kProc<Fn>("MicaWin32kGetClipCursor");
    return fn ? fn(reinterpret_cast<MicaWin32kRect*>(output)) : 0;
}

int32_t User32_ShowCursor(int32_t bShow) noexcept {
    using Fn = int32_t (*)(int32_t);
    auto fn = Win32kProc<Fn>("MicaWin32kShowCursor");
    return fn ? fn(bShow != 0) : INT32_MIN;
}

void* User32_MonitorFromWindow(void* /*hwnd*/, uint32_t /*dwFlags*/) noexcept {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x00010001ULL));
}

int32_t User32_GetSystemMetrics(int32_t index) noexcept {
    using Fn = int32_t (*)(int32_t);
    const auto getMetrics = Win32kProc<Fn>("MicaWin32kGetSystemMetrics");
    return getMetrics ? getMetrics(index) : 0;
}

int32_t User32_GetMonitorInfoW(void* hMonitor, void* lpmi) noexcept {
    if (!hMonitor || !lpmi) return 0;
    return 1;
}

int32_t User32_EnumDisplaySettingsExW(const wchar_t* /*lpszDeviceName*/, uint32_t /*iModeNum*/, void* lpDevMode, uint32_t /*dwFlags*/) noexcept {
    return lpDevMode ? 1 : 0;
}

int32_t User32_SetProcessDpiAwarenessContext(void* /*value*/) noexcept {
    return 1;
}

int32_t User32_MessageBeep(uint32_t type) noexcept {
    if (type == 0xffffffffu) return 1; // MB_OK: explicitly request silence.
    uint32_t frequency = 800;
    if (type & 0x10u) frequency = 440;       // MB_ICONHAND / MB_ICONERROR
    else if (type & 0x20u) frequency = 660;  // MB_ICONQUESTION
    else if (type & 0x30u) frequency = 880;  // MB_ICONEXCLAMATION / MB_ICONWARNING
    if (Beep(frequency, 120)) return 1;
    return 0;
}

// user32.dll implementations
void* User32_GetDC(void* hWnd) noexcept {
    using Fn=void* (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kGetDC");
    void* dc = fn ? fn(hWnd) : nullptr;
    return dc;
}

int32_t User32_ReleaseDC(void* hWnd, void* hDC) noexcept {
    using Fn=int32_t (*)(void*,void*); auto fn=Win32kProc<Fn>("MicaWin32kReleaseDC");
    return fn ? fn(hWnd,hDC) : 0;
}

int32_t User32_SetScrollInfo(void* hwnd, int32_t nBar,
                             const ScrollInfoW* info, bool redraw) noexcept {
    if (!info || info->cbSize < sizeof(ScrollInfoW) || (info->fMask & ~kSifSet))
        return 0;
    if ((info->fMask & kSifRange) && info->nMax < info->nMin) return 0;
    if (nBar == 2) {
        (void)User32_SendMessageW(hwnd, kSbSetScrollInfo, redraw ? 1u : 0u,
                                  reinterpret_cast<intptr_t>(info));
        return static_cast<int32_t>(User32_SendMessageW(hwnd, kSbGetPos, 0, 0));
    }
    if ((nBar != 0 && nBar != 1) || !isValidScrollWindow(hwnd)) return 0;
    ButtonLockGuard guard(g_buttonLock);
    auto* owner = acquireWindowScrollState(hwnd);
    if (!owner) return 0;
    auto& bar = owner->bars[nBar];
    setWindowScrollInfo(bar, *info);
    if (redraw) invalidateScrollBar(hwnd);
    return bar.position;
}

int32_t User32_GetScrollInfo(void* hwnd, int32_t nBar,
                             ScrollInfoW* info) noexcept {
    if (!info || info->cbSize < sizeof(ScrollInfoW) || (info->fMask & ~kSifAll))
        return 0;
    if (nBar == 2)
        return static_cast<int32_t>(User32_SendMessageW(hwnd, kSbGetScrollInfo,
            0, reinterpret_cast<intptr_t>(info)));
    if ((nBar != 0 && nBar != 1) || !isValidScrollWindow(hwnd)) return 0;
    ButtonLockGuard guard(g_buttonLock);
    auto* owner = acquireWindowScrollState(hwnd);
    if (!owner) return 0;
    copyWindowScrollInfo(owner->bars[nBar], *info);
    return 1;
}

int32_t User32_SetScrollPos(void* hwnd, int32_t nBar, int32_t position,
                            bool redraw) noexcept {
    if (nBar == 2)
        return static_cast<int32_t>(User32_SendMessageW(hwnd, kSbSetPos,
            redraw ? 1u : 0u, position));
    if ((nBar != 0 && nBar != 1) || !isValidScrollWindow(hwnd)) return 0;
    ButtonLockGuard guard(g_buttonLock);
    auto* owner = acquireWindowScrollState(hwnd);
    if (!owner) return 0;
    auto& bar = owner->bars[nBar];
    const int32_t previous = bar.position;
    bar.position = position;
    clampWindowScrollInfo(bar);
    if (redraw) invalidateScrollBar(hwnd);
    return previous;
}

int32_t User32_GetScrollPos(void* hwnd, int32_t nBar) noexcept {
    if (nBar == 2)
        return static_cast<int32_t>(User32_SendMessageW(hwnd, kSbGetPos, 0, 0));
    if ((nBar != 0 && nBar != 1) || !isValidScrollWindow(hwnd)) return 0;
    ButtonLockGuard guard(g_buttonLock);
    auto* owner = acquireWindowScrollState(hwnd);
    return owner ? owner->bars[nBar].position : 0;
}

int32_t User32_SetScrollRange(void* hwnd, int32_t nBar, int32_t minimum,
                              int32_t maximum, bool redraw) noexcept {
    if (maximum < minimum) return 0;
    if (nBar == 2) {
        (void)User32_SendMessageW(hwnd,
            redraw ? kSbSetRangeRedraw : kSbSetRange,
            static_cast<uintptr_t>(static_cast<intptr_t>(minimum)), maximum);
        return static_cast<int32_t>(User32_SendMessageW(hwnd, kSbGetRange, 0, 0));
    }
    if ((nBar != 0 && nBar != 1) || !isValidScrollWindow(hwnd)) return 0;
    ButtonLockGuard guard(g_buttonLock);
    auto* owner = acquireWindowScrollState(hwnd);
    if (!owner) return 0;
    auto& bar = owner->bars[nBar];
    bar.minimum = minimum;
    bar.maximum = maximum;
    clampWindowScrollInfo(bar);
    if (redraw) invalidateScrollBar(hwnd);
    return 1;
}

int32_t User32_GetScrollRange(void* hwnd, int32_t nBar,
                              int32_t* minimum, int32_t* maximum) noexcept {
    if (nBar == 2)
        return static_cast<int32_t>(User32_SendMessageW(hwnd, kSbGetRange,
            reinterpret_cast<uintptr_t>(minimum),
            reinterpret_cast<intptr_t>(maximum)));
    if ((nBar != 0 && nBar != 1) || (!minimum && !maximum) ||
        !isValidScrollWindow(hwnd)) return 0;
    ButtonLockGuard guard(g_buttonLock);
    auto* owner = acquireWindowScrollState(hwnd);
    if (!owner) return 0;
    if (minimum) *minimum = owner->bars[nBar].minimum;
    if (maximum) *maximum = owner->bars[nBar].maximum;
    return 1;
}

int32_t User32_EnableScrollBar(void* hwnd, uint32_t nBar,
                              uint32_t arrows) noexcept {
    if (arrows > 3 || nBar > 3) return 0;
    if (nBar == 2) {
        (void)User32_SendMessageW(hwnd, kSbEnableArrows, arrows, 0);
        ScrollInfoW probe{};
        probe.cbSize = sizeof(probe);
        return static_cast<int32_t>(User32_SendMessageW(hwnd, kSbGetScrollInfo,
            0, reinterpret_cast<intptr_t>(&probe)));
    }
    if (!isValidScrollWindow(hwnd)) return 0;
    ButtonLockGuard guard(g_buttonLock);
    auto* owner = acquireWindowScrollState(hwnd);
    if (!owner) return 0;
    if (nBar == 0 || nBar == 3) owner->bars[0].disabledArrows = arrows;
    if (nBar == 1 || nBar == 3) owner->bars[1].disabledArrows = arrows;
    invalidateScrollBar(hwnd);
    return 1;
}

} // namespace micant::um::winnt_user32_gdi32_imm32_abi

extern "C" {
__declspec(dllexport) int32_t LoadStringW(void* instance, uint32_t stringId,
                                           wchar_t* buffer, int32_t capacity) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadStringW(
        instance, stringId, buffer, capacity);
}
__declspec(dllexport) void* CreateIconFromResourceEx(
    const uint8_t* bits, uint32_t size, int32_t isIcon, uint32_t version,
    int32_t cx, int32_t cy, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CreateIconFromResourceEx(
        bits, size, isIcon, version, cx, cy, flags);
}
__declspec(dllexport) void* CreateIconFromResource(const uint8_t* bits,
    uint32_t size, int32_t isIcon, uint32_t version) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CreateIconFromResourceEx(
        bits, size, isIcon, version, 0, 0, 0);
}
__declspec(dllexport) void* CopyIcon(void* icon) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CopyIcon(icon);
}
__declspec(dllexport) void* CopyImage(void* image, uint32_t type, int32_t cx,
                                      int32_t cy, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CopyImage(
        image, type, cx, cy, flags);
}
__declspec(dllexport) int32_t DestroyIcon(void* icon) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DestroyIcon(icon);
}
__declspec(dllexport) void* CopyCursor(void* cursor) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CopyCursor(cursor);
}
__declspec(dllexport) int32_t DestroyCursor(void* cursor) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DestroyCursor(cursor);
}
__declspec(dllexport) void* SetCursor(void* cursor) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetCursor(cursor);
}
__declspec(dllexport) void* GetCursor(void) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetCursor();
}
__declspec(dllexport) int32_t GetCursorInfo(void* information) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetCursorInfo(information);
}
__declspec(dllexport) void* CreateCursor(void* instance, int32_t hotX, int32_t hotY,
    int32_t width, int32_t height, const uint8_t* andPlane, const uint8_t* xorPlane) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CreateCursor(
        instance, hotX, hotY, width, height, andPlane, xorPlane);
}
__declspec(dllexport) void* LoadImageW(void* instance, const wchar_t* name,
    uint32_t type, int32_t cx, int32_t cy, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadImageW(
        instance, name, type, cx, cy, flags);
}
__declspec(dllexport) void* LoadCursorFromFileW(const wchar_t* path) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadCursorFromFileW(path);
}
__declspec(dllexport) void* LoadCursorFromFileA(const char* path) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadCursorFromFileA(path);
}
__declspec(dllexport) void* LoadCursorW(void* instance, const wchar_t* name) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadCursorW(instance, name);
}
__declspec(dllexport) void* LoadCursorA(void* instance, const char* name) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadCursorA(instance, name);
}
__declspec(dllexport) void* LoadImageA(void* instance, const char* name,
    uint32_t type, int32_t cx, int32_t cy, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadImageA(
        instance, name, type, cx, cy, flags);
}
__declspec(dllexport) void* LoadIconW(void* instance, const wchar_t* name) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadIconW(instance, name);
}
__declspec(dllexport) void* LoadIconA(void* instance, const char* name) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_LoadIconA(instance, name);
}
__declspec(dllexport) int32_t DrawIconEx(void* hdc, int32_t x, int32_t y,
    void* icon, int32_t cx, int32_t cy, uint32_t step, void* brush, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DrawIconEx(
        hdc, x, y, icon, cx, cy, step, brush, flags);
}
__declspec(dllexport) int32_t DrawIcon(void* hdc, int32_t x, int32_t y, void* icon) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DrawIcon(
        hdc, x, y, icon);
}
__declspec(dllexport) uintptr_t GetClassLongPtrW(void* hwnd, int32_t index) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClassLongPtrW(hwnd, index);
}
__declspec(dllexport) uintptr_t SetClassLongPtrW(void* hwnd, int32_t index, uintptr_t value) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetClassLongPtrW(hwnd, index, value);
}
__declspec(dllexport) uintptr_t GetClassLongPtrA(void* hwnd, int32_t index) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClassLongPtrW(hwnd, index);
}
__declspec(dllexport) uintptr_t SetClassLongPtrA(void* hwnd, int32_t index, uintptr_t value) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetClassLongPtrW(hwnd, index, value);
}
__declspec(dllexport) uint16_t RegisterClassExW(const void* lpWndClassEx) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_RegisterClassExW(lpWndClassEx);
}
__declspec(dllexport) void* CreateWindowExW(uint32_t dwExStyle, const wchar_t* lpClassName, const wchar_t* lpWindowName, uint32_t dwStyle, int32_t X, int32_t Y, int32_t nWidth, int32_t nHeight, void* hWndParent, void* hMenu, void* hInstance, void* lpParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}
__declspec(dllexport) int32_t DestroyWindow(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DestroyWindow(hWnd);
}
__declspec(dllexport) int32_t ShowWindow(void* hWnd, int32_t nCmdShow) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ShowWindow(hWnd, nCmdShow);
}
__declspec(dllexport) int32_t UpdateWindow(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_UpdateWindow(hWnd);
}
__declspec(dllexport) int32_t InvalidateRect(void* hWnd, const micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect, int32_t bErase) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_InvalidateRect(hWnd, lpRect, bErase);
}
__declspec(dllexport) void* BeginPaint(void* hWnd, micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct* lpPaint) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_BeginPaint(hWnd, lpPaint);
}
__declspec(dllexport) int32_t EndPaint(void* hWnd, const micant::um::winnt_user32_gdi32_imm32_abi::WinNtPaintStruct* lpPaint) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EndPaint(hWnd, lpPaint);
}
__declspec(dllexport) void* GetDC(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetDC(hWnd);
}
__declspec(dllexport) int32_t ReleaseDC(void* hWnd, void* hDC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ReleaseDC(hWnd, hDC);
}
__declspec(dllexport) intptr_t DefWindowProcW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DefWindowProcW(hWnd, Msg, wParam, lParam);
}
__declspec(dllexport) intptr_t __stdcall CallWindowProcW(void* procedure,
    void* window, uint32_t message, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CallWindowProcW(
        procedure, window, message, wParam, lParam);
}
__declspec(dllexport) int32_t PeekMessageW(micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}
__declspec(dllexport) int32_t GetMessageW(micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}
__declspec(dllexport) int32_t GetKeyboardState(uint8_t* state) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetKeyboardState(state);
}
__declspec(dllexport) void* __stdcall GetDlgItem(void* dialog, int32_t controlId) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetDlgItem(dialog, controlId);
}
__declspec(dllexport) uint32_t __stdcall GetDlgItemTextW(
    void* dialog, int32_t controlId, wchar_t* text, int32_t capacity) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetDlgItemTextW(
        dialog, controlId, text, capacity);
}
__declspec(dllexport) int32_t __stdcall SetDlgItemTextW(
    void* dialog, int32_t controlId, const wchar_t* text) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetDlgItemTextW(
        dialog, controlId, text);
}
__declspec(dllexport) uint32_t __stdcall IsDlgButtonChecked(
    void* dialog, int32_t controlId) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_IsDlgButtonChecked(
        dialog, controlId);
}
__declspec(dllexport) int32_t __stdcall CheckRadioButton(
    void* dialog, uint32_t firstId, uint32_t lastId, uint32_t checkId) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CheckRadioButton(
        dialog, firstId, lastId, checkId);
}
__declspec(dllexport) int32_t __stdcall CheckDlgButton(
    void* dialog, int32_t controlId, uint32_t check) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_CheckDlgButton(
        dialog, controlId, check);
}
__declspec(dllexport) intptr_t __stdcall SendDlgItemMessageW(void* dialog,
    int32_t controlId, uint32_t message, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SendDlgItemMessageW(
        dialog, controlId, message, wParam, lParam);
}
__declspec(dllexport) int32_t __stdcall GetClassNameW(
    void* window, wchar_t* output, int32_t capacity) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClassNameW(
        window, output, capacity);
}
__declspec(dllexport) int32_t __stdcall EnumChildWindows(
    void* parent, void* callback, intptr_t parameter) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EnumChildWindows(
        parent, callback, parameter);
}
__declspec(dllexport) int32_t __stdcall EnableWindow(void* window, int32_t enable) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EnableWindow(window, enable);
}
__declspec(dllexport) int32_t __stdcall IsWindowEnabled(void* window) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_IsWindowEnabled(window);
}
__declspec(dllexport) int32_t __stdcall IsWindow(void* window) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_IsWindow(window);
}
__declspec(dllexport) void* __stdcall GetParent(void* window) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetParent(window);
}
__declspec(dllexport) int32_t __stdcall DrawStateW(void* dc, void* brush,
    void* callback, intptr_t data, uintptr_t count, int32_t x, int32_t y,
    int32_t width, int32_t height, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DrawStateW(
        dc, brush, callback, data, count, x, y, width, height, flags);
}
__declspec(dllexport) int32_t __stdcall DrawFrameControl(
    void* dc, micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* rect,
    uint32_t type, uint32_t state) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DrawFrameControl(
        dc, rect, type, state);
}
__declspec(dllexport) int32_t __stdcall TrackMouseEvent(
    micant::um::winnt_user32_gdi32_imm32_abi::WinNtTrackMouseEvent* event) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_TrackMouseEvent(event);
}
__declspec(dllexport) void* __stdcall GetKeyboardLayout(uint32_t threadId) {
    return reinterpret_cast<void*>(
        micant::um::winnt_user32_gdi32_imm32_abi::User32_GetKeyboardLayout(threadId));
}
__declspec(dllexport) uint32_t __stdcall MapVirtualKeyExW(
    uint32_t code, uint32_t mapType, void* layout) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_MapVirtualKeyExW(
        code, mapType, reinterpret_cast<uintptr_t>(layout));
}
__declspec(dllexport) int32_t __stdcall ToAsciiEx(uint32_t virtualKey,
    uint32_t scanCode, const uint8_t* keyState, uint16_t* characters,
    uint32_t flags, void* layout) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ToAsciiEx(
        virtualKey, scanCode, keyState, characters, flags,
        reinterpret_cast<uintptr_t>(layout));
}
__declspec(dllexport) int32_t TranslateMessage(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_TranslateMessage(lpMsg);
}
__declspec(dllexport) intptr_t DispatchMessageW(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_DispatchMessageW(lpMsg);
}
__declspec(dllexport) void PostQuitMessage(int32_t nExitCode) {
    micant::um::winnt_user32_gdi32_imm32_abi::User32_PostQuitMessage(nExitCode);
}
__declspec(dllexport) int32_t PostMessageW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_PostMessageW(hWnd,Msg,wParam,lParam);
}
__declspec(dllexport) intptr_t SendMessageW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SendMessageW(hWnd,Msg,wParam,lParam);
}
__declspec(dllexport) int32_t SetScrollInfo(void* hWnd, int32_t nBar,
        const void* info, int32_t redraw) {
    const auto* scrollInfo = static_cast<const micant::um::winnt_user32_gdi32_imm32_abi::ScrollInfoW*>(info);
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetScrollInfo(
        hWnd, nBar, scrollInfo, redraw != 0);
}
__declspec(dllexport) int32_t GetScrollInfo(void* hWnd, int32_t nBar, void* info) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetScrollInfo(
        hWnd, nBar, static_cast<micant::um::winnt_user32_gdi32_imm32_abi::ScrollInfoW*>(info));
}
__declspec(dllexport) int32_t SetScrollPos(void* hWnd, int32_t nBar,
        int32_t position, int32_t redraw) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetScrollPos(
        hWnd, nBar, position, redraw != 0);
}
__declspec(dllexport) int32_t GetScrollPos(void* hWnd, int32_t nBar) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetScrollPos(hWnd, nBar);
}
__declspec(dllexport) int32_t SetScrollRange(void* hWnd, int32_t nBar,
        int32_t nMinPos, int32_t nMaxPos, int32_t redraw) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetScrollRange(
        hWnd, nBar, nMinPos, nMaxPos, redraw != 0);
}
__declspec(dllexport) int32_t GetScrollRange(void* hWnd, int32_t nBar,
        int32_t* lpMinPos, int32_t* lpMaxPos) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetScrollRange(
        hWnd, nBar, lpMinPos, lpMaxPos);
}
__declspec(dllexport) int32_t EnableScrollBar(void* hWnd, uint32_t wSBflags,
        uint32_t wArrows) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EnableScrollBar(
        hWnd, wSBflags, wArrows);
}
__declspec(dllexport) int32_t GetClientRect(void* hWnd, micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClientRect(hWnd, lpRect);
}
__declspec(dllexport) int32_t GetWindowRect(void* hWnd, micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetWindowRect(hWnd, lpRect);
}
__declspec(dllexport) int32_t EnumWindows(int32_t (*lpEnumFunc)(void*, intptr_t), intptr_t lParam) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EnumWindows(
        reinterpret_cast<void*>(lpEnumFunc), lParam);
}
__declspec(dllexport) int32_t GetWindowTextW(void* hWnd, wchar_t* lpString, int32_t nMaxCount) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetWindowTextW(hWnd, lpString, nMaxCount);
}
__declspec(dllexport) int32_t SetWindowTextW(void* hWnd, const wchar_t* lpString) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetWindowTextW(hWnd, lpString);
}
__declspec(dllexport) int32_t IsWindowVisible(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_IsWindowVisible(hWnd);
}
__declspec(dllexport) int32_t IsIconic(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_IsIconic(hWnd);
}
__declspec(dllexport) void* GetForegroundWindow(void) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetForegroundWindow();
}
__declspec(dllexport) void* SetFocus(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetFocus(hWnd);
}
__declspec(dllexport) void* GetFocus(void) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetFocus();
}
__declspec(dllexport) void* GetCapture(void) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetCapture();
}
__declspec(dllexport) void* SetCapture(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetCapture(hWnd);
}
__declspec(dllexport) int32_t ReleaseCapture(void) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ReleaseCapture();
}
__declspec(dllexport) int32_t AdjustWindowRectEx(micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect, uint32_t dwStyle, int32_t bMenu, uint32_t dwExStyle) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_AdjustWindowRectEx(lpRect, dwStyle, bMenu, dwExStyle);
}
__declspec(dllexport) intptr_t SetWindowLongPtrW(void* hWnd, int32_t nIndex, intptr_t dwNewLong) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetWindowLongPtrW(hWnd, nIndex, dwNewLong);
}
__declspec(dllexport) intptr_t GetWindowLongPtrW(void* hWnd, int32_t nIndex) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetWindowLongPtrW(hWnd, nIndex);
}
__declspec(dllexport) int32_t SetWindowPos(void* hWnd, void* hWndInsertAfter, int32_t X, int32_t Y, int32_t cx, int32_t cy, uint32_t uFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}
__declspec(dllexport) int32_t MoveWindow(void* window, int32_t x, int32_t y,
    int32_t width, int32_t height, int32_t repaint) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_MoveWindow(
        window, x, y, width, height, repaint);
}
__declspec(dllexport) int32_t RegisterRawInputDevices(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtRawInputDevice* pRawInputDevices, uint32_t uiNumDevices, uint32_t cbSize) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_RegisterRawInputDevices(pRawInputDevices, uiNumDevices, cbSize);
}
__declspec(dllexport) uint32_t GetRawInputData(void* hRawInput, uint32_t uiCommand, void* pData, uint32_t* pcbSize, uint32_t cbSizeHeader) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
}
__declspec(dllexport) int32_t GetCursorPos(micant::um::winnt_user32_gdi32_imm32_abi::WinNtPoint* lpPoint) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetCursorPos(lpPoint);
}
__declspec(dllexport) int32_t SetCursorPos(int32_t X, int32_t Y) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetCursorPos(X, Y);
}
__declspec(dllexport) int32_t ClipCursor(const micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ClipCursor(lpRect);
}
__declspec(dllexport) int32_t GetClipCursor(micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect* lpRect) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetClipCursor(lpRect);
}
__declspec(dllexport) int32_t ShowCursor(int32_t bShow) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_ShowCursor(bShow);
}
__declspec(dllexport) int16_t GetAsyncKeyState(int32_t vKey) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetAsyncKeyState(vKey);
}
__declspec(dllexport) int32_t GetSystemMetrics(int32_t index) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetSystemMetrics(index);
}
__declspec(dllexport) void* MonitorFromWindow(void* hwnd, uint32_t dwFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_MonitorFromWindow(hwnd, dwFlags);
}
__declspec(dllexport) int32_t GetMonitorInfoW(void* hMonitor, void* lpmi) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetMonitorInfoW(hMonitor, lpmi);
}
__declspec(dllexport) int32_t EnumDisplaySettingsExW(const wchar_t* lpszDeviceName, uint32_t iModeNum, void* lpDevMode, uint32_t dwFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_EnumDisplaySettingsExW(lpszDeviceName, iModeNum, lpDevMode, dwFlags);
}
__declspec(dllexport) int32_t SetProcessDpiAwarenessContext(void* value) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_SetProcessDpiAwarenessContext(value);
}
__declspec(dllexport) int32_t MessageBeep(uint32_t type) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_MessageBeep(type);
}

// APIs below are not implemented as exports by this canonical User32 PE
// target. Keep the intended ABI visible here so future work can be compared
// against ReactOS without accidentally registering placeholder success paths.
// ReactOS dialog.c parses dialog templates, builds child HWNDs, sends
// WM_INITDIALOG, owns a nested modal queue loop and restores the owner on exit.
// Failure while parsing/creating children unwinds partial state and returns -1.
// INT_PTR __stdcall DialogBoxParamW(HINSTANCE instance, LPCWSTR templateName,
//     HWND owner, DLGPROC dialogProc, LPARAM initParam);
// The indirect-template variant follows the same modal lifecycle but parses
// the caller's in-memory DLGTEMPLATE/DLGTEMPLATEEX instead of loading a
// resource. ReactOS validates the template bounds and unwinds partial controls.
// INT_PTR __stdcall DialogBoxIndirectParamW(HINSTANCE instance,
//     LPCDLGTEMPLATEW dialogTemplate, HWND owner, DLGPROC dialogProc,
//     LPARAM initParam);

// ReactOS messagebox.c creates a real dialog with the requested buttons,
// default button, icon and modality; it returns the selected button ID. The
// separate tools/dlls/user32.cpp implementation returning IDOK without UI is
// not part of this DLL and must not be used to satisfy this export.
// int __stdcall MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption,
//                           UINT type);
// ReactOS's A entry point converts caption/text with CP_ACP and then shares
// the same real modal dialog implementation; allocation/conversion failures
// must propagate rather than silently presenting an empty successful box.
// int __stdcall MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption,
//                           UINT type);

// ReactOS dialog.c routes Tab/arrows/Enter/Escape and mnemonics through the
// dialog's control order and dialog-code rules, consuming messages when it
// performs navigation. Returning FALSE unconditionally breaks real dialogs.
// BOOL __stdcall IsDialogMessageW(HWND dialog, LPMSG message);
// ANSI form shares the same navigation rules after translating any text-bearing
// messages using the active code page. ReactOS returns TRUE only when the
// dialog manager consumed the message.
// BOOL __stdcall IsDialogMessageA(HWND dialog, LPMSG message);

// ANSI entry points must convert strings using the process code page and then
// preserve the W implementation's validation, return values and last-error
// behavior; ASCII-only path/name conversion is not Win32 A/W compatibility.
// ATOM __stdcall RegisterClassExA(const WNDCLASSEXA* windowClass);
// HWND __stdcall CreateWindowExA(DWORD exStyle, LPCSTR className,
//     LPCSTR windowName, DWORD style, int x, int y, int width, int height,
//     HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter);
// int __stdcall GetMessageA(MSG* message, HWND window, UINT minFilter,
//                           UINT maxFilter);
// BOOL __stdcall PostMessageA(HWND window, UINT message,
//                             WPARAM wParam, LPARAM lParam);
// LRESULT __stdcall SendMessageA(HWND window, UINT message,
//                                WPARAM wParam, LPARAM lParam);

// ReactOS loads MENU/MENUEX resources recursively, preserving item flags,
// command IDs, state, text and submenus. The W loader in the menu ABI handles
// only the standard template subset; the inline A/W loader placeholders return
// null today and must not be confused with a complete resource implementation.
// HMENU __stdcall LoadMenuA(HINSTANCE instance, LPCSTR menuName);

// ReactOS's A variant applies MIIM_* selection and command/position lookup,
// copies ANSI item text with the caller's cch limit, and reports the full
// required length. The inline catalog currently uses an incorrect generic
// signature and sentinel result; replace that declaration at its owner before
// exposing an ABI-compatible export.
// BOOL __stdcall GetMenuItemInfoA(HMENU menu, UINT item, BOOL byPosition,
//                                 LPMENUITEMINFOA information);

// ReactOS messagebox/dialog helpers use accelerator tables to match a real
// key message to a command, send WM_COMMAND/WM_SYSCOMMAND, and return 1 only
// when consumed. The current inline TranslateAccelerator placeholder always
// returns zero and has no accelerator resource/table lifetime.
// int __stdcall TranslateAcceleratorW(HWND window, HACCEL table, LPMSG message);
// HACCEL __stdcall CreateAcceleratorTableW(LPACCEL entries, int count);
// BOOL __stdcall DestroyAcceleratorTable(HACCEL table);
} // extern "C"
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
            micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect rect{};
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
                micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect rect{};
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
    micant::um::winnt_user32_gdi32_imm32_abi::WinNtRect client{};
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
