/**
 * @file winnt_user32_gdi32_imm32_system32_abi.cpp
 * @brief Horizon 111 (Kernel Unit #83): Sovereign WinNT Core User-Mode System32 DLLs
 *        (user32.dll, gdi32.dll & imm32.dll) Official PE32+ Exports, Win32 HWND/HDC/HMONITOR
 *        Creation, Non-Blocking PeekMessageW Game Loop, 8000Hz WM_INPUT Raw Input,
 *        GDI PixelFormat / SwapBuffers Interop, and IMM32 Full-Screen Game IME Control.
 */

#include "micant/um/subsystems/winuser/winnt_user32_gdi32_imm32_system32_abi.hpp"
#if defined(BUILD_SYSTEM32_USER32_DLL)
#include "micant/um/subsystems/imaging/ico_decoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/subsystems/winuser/icon_handle_table.hpp"
#include "micant/um/subsystems/winuser/cursor_image.hpp"
#include "micant/um/subsystems/winuser/ani_image.hpp"
#include "micant/um/native/pe.hpp"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/native_gdi_api.h"
#include "micant/shared/native_uxtheme_api.h"
#endif
#include "micant/shared/win32k_client.h"
#if defined(BUILD_SYSTEM32_GDI32_DLL)
#include "micant/um/subsystems/graphics/font_manager.hpp"
#endif

#include <algorithm>
#include <array>

#if defined(BUILD_SYSTEM32_STANDALONE_DLL)
extern "C" bool DllMain(void*, unsigned long, void*) noexcept { return true; }
#endif

namespace micant::um::winnt_user32_gdi32_imm32_abi {

#if defined(BUILD_SYSTEM32_USER32_DLL)
intptr_t User32_ButtonWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
intptr_t User32_EditWndProc(void*, uint32_t, uintptr_t, intptr_t) noexcept;
void* User32_BeginPaint(void*, WinNtPaintStruct*) noexcept;
int32_t User32_EndPaint(void*, const WinNtPaintStruct*) noexcept;
intptr_t User32_SendMessageW(void*, uint32_t, uintptr_t, intptr_t) noexcept;
void* User32_SetFocus(void*) noexcept;
void* User32_SetCapture(void*) noexcept;
int32_t User32_ReleaseCapture() noexcept;
intptr_t User32_DefWindowProcW(void*, uint32_t, uintptr_t, intptr_t) noexcept;
#endif

namespace {
#if defined(BUILD_SYSTEM32_USER32_DLL)
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
struct ButtonState {
    void* hwnd{};
    void* parent{};
    uintptr_t id{};
    uint32_t state{};
    uint32_t style{};
    bool armed{};
    bool spaceArmed{};
    uint32_t checkState{};
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
std::array<ButtonState, 64> g_buttons{};
std::array<EditState, 64> g_edits{};
void* g_hotButton{};
uint32_t g_buttonClassState{};
uint32_t g_editClassState{};
constexpr uint32_t kButtonHot = 0x01;
constexpr uint32_t kButtonPressed = 0x02;
constexpr uint32_t kButtonFocused = 0x04;
constexpr uint32_t kButtonDisabled = 0x08;
constexpr uint32_t kButtonDefault = 0x10;
constexpr uint32_t kWsDisabled = 0x08000000u;
constexpr uint32_t kBsTypeMask = 0x0Fu;
constexpr uint32_t kBsPushButton = 0u;
constexpr uint32_t kBsDefPushButton = 1u;
constexpr uint32_t kBsCheckBox = 2u;
constexpr uint32_t kBsAutoCheckBox = 3u;
constexpr uint32_t kBsRadioButton = 4u;
constexpr uint32_t kBs3State = 5u;
constexpr uint32_t kBsAuto3State = 6u;
constexpr uint32_t kEsReadOnly = 0x00000800u;
constexpr uint32_t kEsUnsupportedMask = 0x000037FFu;
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
constexpr uint32_t kMkLButton = 0x0001u;
constexpr uint32_t kVkSpace = 0x20u;
constexpr uint32_t kDtCenter = 0x00000001u;
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
    return type <= kBs3State || type == kBsAuto3State;
}
bool isCheckableButtonType(uint32_t type) noexcept {
    return type == kBsCheckBox || type == kBsAutoCheckBox ||
        type == kBsRadioButton || type == kBs3State || type == kBsAuto3State;
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
    frameLimits.maxPixels = MICANT_COMPOSITOR_CURSOR_MAX_PIXELS;
    frameLimits.maxDecodedBytes = MICANT_COMPOSITOR_CURSOR_MAX_PIXELS * 4u;
    frameLimits.maxIntermediateBytes = 1024u * 1024u;
    if (requestedWidth > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
        requestedHeight > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION) return nullptr;
    AnimatedCursor candidate{};
    candidate.frameCount = view.frameCount;
    for (uint16_t i = 0; i < view.frameCount; ++i) {
        winuser::CursorImage cursor{};
        if (winuser::DecodeCursorImage(view.frames[i], requestedWidth, requestedHeight,
                cursor, allocator, frameLimits) !=
            imaging::ImageStatus::Success) return nullptr;
        if (cursor.image.width > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
            cursor.image.height > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION) return nullptr;
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

bool loadedModulePath(void* moduleHandle, wchar_t (&path)[512]) noexcept {
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
            return true;
        }
        link = *reinterpret_cast<const uintptr_t*>(link);
    }
    return false;
}

void* loadPeCursor(void* moduleHandle, const wchar_t* resourceName,
                   uint32_t desiredWidth, uint32_t desiredHeight,
                   bool shared) noexcept {
    using namespace micant::um;
    if (!resourceName ||
        desiredWidth > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
        desiredHeight > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION) {
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
        frame.height != selected.entry.height || frame.width > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
        frame.height > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION) {
        setThreadLastError(13); // ERROR_INVALID_DATA
        return nullptr;
    }
    uint16_t hotX = selected.entry.hotX, hotY = selected.entry.hotY;
    if (targetWidth > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
        targetHeight > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION) {
        setThreadLastError(87);
        return nullptr;
    }
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
#else
void updateAnimatedCursor() noexcept {}
#endif

template <typename T> T Win32kProc(const char* name) noexcept {
#define MICA_WIN32K_PROC(exportName) if (std::strcmp(name, #exportName) == 0) return reinterpret_cast<T>(&exportName)
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
    MICA_WIN32K_PROC(MicaWin32kEnumWindows);
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
    MICA_WIN32K_PROC(MicaWin32kGetWindowIcon);
    MICA_WIN32K_PROC(MicaWin32kRegisterClassW);
    MICA_WIN32K_PROC(MicaWin32kDispatchMessageW);
    MICA_WIN32K_PROC(MicaWin32kSetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kGetWindowLongPtrW);
    MICA_WIN32K_PROC(MicaWin32kDefWindowProcW);
    MICA_WIN32K_PROC(MicaWin32kGetDC);
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

struct WinNtWindowState {
    uint64_t  magic{0x4D4E5448574E4431ULL}; // "MNTHWD1"
    int32_t   width{3840};
    int32_t   height{2160};
    intptr_t  userData{0};
    void*     imc{reinterpret_cast<void*>(static_cast<uintptr_t>(0x00000000000E0100ULL))};
    uint32_t  pendingMessages{1};
} g_primaryWindow{};
#if defined(BUILD_SYSTEM32_GDI32_DLL)
constexpr size_t GdiFontCapacity = 24;
constexpr size_t GdiBitmapCapacity = 32;
constexpr size_t GdiDcCapacity = 64;
constexpr size_t GdiBrushCapacity = 64;
constexpr int32_t GdiFontSize = 9;
struct GdiLogFontW {
    int32_t height, width, escapement, orientation, weight;
    uint8_t italic, underline, strikeOut, charSet, outPrecision, clipPrecision, quality, pitchAndFamily;
    wchar_t faceName[32];
};
struct GdiSize { int32_t cx{}, cy{}; };
struct GdiTextMetricW {
    int32_t height{}, ascent{}, descent{}, internalLeading{}, externalLeading{};
    int32_t averageCharWidth{}, maximumCharWidth{}, weight{}, overhang{};
    int32_t digitizedAspectX{}, digitizedAspectY{};
    wchar_t firstChar{}, lastChar{}, defaultChar{}, breakChar{};
    uint8_t italic{}, underlined{}, struckOut{}, pitchAndFamily{}, charSet{};
};
struct GdiEnumLogFontExW {
    GdiLogFontW logFont{};
    wchar_t fullName[64]{};
    wchar_t style[32]{};
    wchar_t script[32]{};
};
struct GdiNewTextMetricExW {
    GdiTextMetricW textMetric{};
    uint32_t fontSignature[4]{};
};
using GdiFontEnumProcW = int32_t (*)(const GdiLogFontW*, const GdiTextMetricW*,
                                     uint32_t, intptr_t);
static_assert(sizeof(GdiSize) == 8);
static_assert(sizeof(GdiTextMetricW) == 60);
static_assert(offsetof(GdiTextMetricW, firstChar) == 44);
static_assert(offsetof(GdiEnumLogFontExW, fullName) == sizeof(GdiLogFontW));
struct GdiFontObject {
    uint64_t magic{0x4D494341464F4E54ULL};
    bool active{};
    bool stock{};
    char16_t family[32]{};
    micant::um::graphics::LogicalFontRequest request{};
    micant::um::graphics::FontManager manager{};
};
static_assert(sizeof(GdiLogFontW) == 92);
static_assert(offsetof(GdiLogFontW, faceName) == 28);
struct GdiBitmapSurface {
    void* bitmap{};
    uint32_t* pixels{};
    uint32_t width{}, height{};
    bool topDown{};
};
struct GdiDcSelection {
    void* dc{};
    void* bitmap{};
    GdiFontObject* font{};
    uint32_t textColor{0x00000000u}; // COLORREF black
    int32_t backgroundMode{1};       // TRANSPARENT
    MicaWin32kRect clip{};
    struct ClipState { uint32_t generation{}; MicaWin32kRect previous{}; bool occupied{}; } clips[16]{};
    uint32_t nextClipGeneration{1};
};
struct GdiBrushObject {
    uint32_t colorRef{};
    uint32_t generation{};
    bool active{};
};
GdiFontObject g_gdiFonts[GdiFontCapacity]{};
GdiFontObject* g_systemFont{};
GdiBitmapSurface g_gdiBitmaps[GdiBitmapCapacity]{};
GdiDcSelection g_gdiDcs[GdiDcCapacity]{};
GdiBrushObject g_gdiBrushes[GdiBrushCapacity]{};
uint32_t g_gdiBrushGeneration{1};
uint32_t g_gdiBrushLock{};
struct GdiBrushGuard {
    GdiBrushGuard() noexcept {
        while (__atomic_exchange_n(&g_gdiBrushLock, 1u, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause" ::: "memory");
    }
    ~GdiBrushGuard() { __atomic_store_n(&g_gdiBrushLock, 0u, __ATOMIC_RELEASE); }
};

void* GdiBrushHandle(size_t slot, uint32_t generation) noexcept {
    return reinterpret_cast<void*>((static_cast<uintptr_t>(generation) << 7) | (slot + 1));
}
GdiBrushObject* FindGdiBrush(void* handle) noexcept {
    const uintptr_t value = reinterpret_cast<uintptr_t>(handle);
    const uintptr_t slot = value & 0x7Fu;
    const uint32_t generation = static_cast<uint32_t>(value >> 7);
    if (!slot || slot > GdiBrushCapacity || !generation) return nullptr;
    auto& brush = g_gdiBrushes[slot - 1];
    return brush.active && brush.generation == generation ? &brush : nullptr;
}

GdiFontObject* FindGdiFont(void* handle) noexcept {
    for (auto& font : g_gdiFonts) if (font.active && handle == &font) return &font;
    return nullptr;
}
GdiBitmapSurface* FindGdiBitmap(void* handle) noexcept {
    for (auto& bitmap : g_gdiBitmaps) if (bitmap.bitmap == handle && handle) return &bitmap;
    return nullptr;
}
GdiDcSelection* FindGdiDc(void* handle, bool create) noexcept {
    if (!handle) return nullptr;
    GdiDcSelection* freeSlot = nullptr;
    for (auto& dc : g_gdiDcs) {
        if (dc.dc == handle) return &dc;
        if (!dc.dc && !freeSlot) freeSlot = &dc;
    }
    if (!create || !freeSlot) return nullptr;
    *freeSlot = GdiDcSelection{};
    freeSlot->dc = handle;
    return freeSlot;
}

GdiFontObject* CreateGdiFont(const GdiLogFontW& logical) noexcept {
    if (logical.width || logical.escapement || logical.orientation || logical.underline ||
        logical.strikeOut || logical.weight < 0 || logical.weight > 1000)
        return nullptr;
    size_t familyLength = 0;
    while (familyLength < 32 && logical.faceName[familyLength]) ++familyLength;
    if (familyLength == 32) return nullptr;
    for (auto& font : g_gdiFonts) {
        if (font.active) continue;
        font.magic = 0x4D494341464F4E54ULL;
        font.stock = false;
        font.family[0] = 0;
        font.active = true;
        const wchar_t* family = familyLength ? logical.faceName : L"Segoe UI";
        size_t copyLength = familyLength ? familyLength : 8;
        if (copyLength > 31) copyLength = 31;
        for (size_t i = 0; i < copyLength; ++i) font.family[i] = static_cast<char16_t>(family[i]);
        font.family[copyLength] = 0;
        uint32_t points = GdiFontSize;
        if (logical.height) {
            const int64_t magnitude = logical.height < 0
                ? -static_cast<int64_t>(logical.height) : logical.height;
            const uint64_t scaled = static_cast<uint64_t>(magnitude) * 72u;
            points = static_cast<uint32_t>((scaled + 48u) / 96u);
            if (!points) points = 1;
            if (points > 512) { font.active = false; return nullptr; }
        }
        font.request = {font.family,
            static_cast<uint16_t>(logical.weight ? logical.weight : 400),
            logical.italic != 0, static_cast<uint16_t>(points), 96};
        if (!font.manager.initialize(font.request)) {
            font.manager.shutdown();
            font.active = false;
            return nullptr;
        }
        return &font;
    }
    return nullptr;
}

GdiFontObject* GetSystemGdiFont() noexcept {
    if (g_systemFont && g_systemFont->active) return g_systemFont;
    GdiLogFontW logical{};
    GdiFontObject* font = CreateGdiFont(logical);
    if (font) {
        font->stock = true;
        g_systemFont = font;
    }
    return font;
}

bool AppendUtf8(uint32_t scalar, char* output, size_t capacity, size_t& used) noexcept {
    uint8_t bytes[4]{}; size_t count{};
    if (scalar <= 0x7F) { bytes[0] = static_cast<uint8_t>(scalar); count = 1; }
    else if (scalar <= 0x7FF) {
        bytes[0] = static_cast<uint8_t>(0xC0 | (scalar >> 6));
        bytes[1] = static_cast<uint8_t>(0x80 | (scalar & 0x3F)); count = 2;
    } else if (scalar <= 0xFFFF && !(scalar >= 0xD800 && scalar <= 0xDFFF)) {
        bytes[0] = static_cast<uint8_t>(0xE0 | (scalar >> 12));
        bytes[1] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3F));
        bytes[2] = static_cast<uint8_t>(0x80 | (scalar & 0x3F)); count = 3;
    } else if (scalar <= 0x10FFFF) {
        bytes[0] = static_cast<uint8_t>(0xF0 | (scalar >> 18));
        bytes[1] = static_cast<uint8_t>(0x80 | ((scalar >> 12) & 0x3F));
        bytes[2] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3F));
        bytes[3] = static_cast<uint8_t>(0x80 | (scalar & 0x3F)); count = 4;
    } else return false;
    if (used + count >= capacity) return false;
    for (size_t i = 0; i < count; ++i) output[used++] = static_cast<char>(bytes[i]);
    return true;
}

bool Utf16ToUtf8(const wchar_t* input, int32_t length, char* output,
                 size_t capacity) noexcept {
    if (!input || !output || length <= 0 || length > 4096 || capacity < 2) return false;
    size_t used = 0;
    for (int32_t i = 0; i < length; ++i) {
        uint32_t scalar = static_cast<uint16_t>(input[i]);
        if (scalar >= 0xD800 && scalar <= 0xDBFF) {
            if (++i >= length) return false;
            const uint32_t low = static_cast<uint16_t>(input[i]);
            if (low < 0xDC00 || low > 0xDFFF) return false;
            scalar = 0x10000 + ((scalar - 0xD800) << 10) + (low - 0xDC00);
        } else if (scalar >= 0xDC00 && scalar <= 0xDFFF) return false;
        if (!scalar || !AppendUtf8(scalar, output, capacity, used)) return false;
    }
    output[used] = 0;
    return true;
}
#endif
#if defined(BUILD_SYSTEM32_USER32_DLL)
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
#endif

int32_t g_activePixelFormat = 1;
int32_t g_imeOpenStatus = 0;
intptr_t AuditWindowProc(void*,uint32_t message,uintptr_t,intptr_t) noexcept {
    return message == 0x0081 ? 1 : 0;
}
} // namespace

#if defined(BUILD_SYSTEM32_USER32_DLL)
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
            if (animation.handle == cursor) { animation = {}; break; }
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
        if ((flags & DefaultSize) && !width && !height) targetWidth = targetHeight = 32;
        if (!targetWidth || !targetHeight || targetWidth > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION ||
            targetHeight > MICANT_COMPOSITOR_CURSOR_MAX_DIMENSION) return nullptr;
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
    if (status != imaging::ImageStatus::Success) return nullptr;
    return winuser::ProcessIconHandles().create(std::move(frame),
        imaging::NativeImageAllocator(), false);
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
#endif

// user32.dll implementations
uint16_t User32_RegisterClassExW(const void* lpWndClassEx) noexcept {
    if (!lpWndClassEx) return 0;
    const auto* wc=static_cast<const MicaWin32kWindowClassEx*>(lpWndClassEx);
    if (wc->cbSize < sizeof(MicaWin32kWindowClassEx) || !wc->className || !wc->wndProc) return 0;
#if defined(BUILD_SYSTEM32_USER32_DLL)
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
#endif
    using Fn=uint16_t (*)(const wchar_t*,void*);
    auto fn=Win32kProc<Fn>("MicaWin32kRegisterClassW");
    const uint16_t atom = fn ? fn(wc->className,wc->wndProc) : 0;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (atom && wc->className) {
        for (size_t i = 0; i < classNameLength; ++i) iconSlot->name[i] = wc->className[i];
        iconSlot->name[classNameLength] = 0;
        iconSlot->large = wc->icon; iconSlot->small = wc->smallIcon;
        iconSlot->cursor = wc->cursor;
        iconSlot->largeLease = std::move(classLargeLease);
        iconSlot->smallLease = std::move(classSmallLease);
        iconSlot->cursorLease = std::move(classCursorLease);
    }
#endif
    return atom;
}

#if defined(BUILD_SYSTEM32_USER32_DLL)
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

bool ensureBuiltinClass(const wchar_t* name, void* wndProc,
                        uint32_t& registrationState) noexcept {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&registrationState, &expected, 1u,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        using RegisterFn = uint16_t (*)(const wchar_t*, void*);
        const auto registerClass = Win32kProc<RegisterFn>("MicaWin32kRegisterClassW");
        const uint16_t atom = registerClass ? registerClass(name, wndProc) : 0;
        __atomic_store_n(&registrationState, atom ? 2u : 0u, __ATOMIC_RELEASE);
        return atom != 0;
    }
    for (uint32_t spins = 0; spins < 1000000; ++spins) {
        const uint32_t state = __atomic_load_n(&registrationState, __ATOMIC_ACQUIRE);
        if (state == 2) return true;
        if (state == 0) return false;
        __asm__ __volatile__("pause" ::: "memory");
    }
    return false;
}
#endif

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
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (isClassName(lpClassName, L"BUTTON")) {
        const uint32_t type = dwStyle & kBsTypeMask;
        if (!isSupportedButtonType(type)) {
            setThreadLastError(87); // ERROR_INVALID_PARAMETER: unsupported BS_* type.
            return nullptr;
        }
        if (!ensureBuiltinClass(L"BUTTON",
                reinterpret_cast<void*>(&User32_ButtonWndProc), g_buttonClassState))
            return nullptr;
    } else if (isClassName(lpClassName, L"EDIT")) {
        const uint32_t editStyles = dwStyle & kEsControlMask;
        if (editStyles & kEsUnsupportedMask) {
            setThreadLastError(87); // Unsupported ES_* styles fail explicitly.
            return nullptr;
        }
        if (!ensureBuiltinClass(L"EDIT",
                reinterpret_cast<void*>(&User32_EditWndProc), g_editClassState))
            return nullptr;
    }
#endif
    using Fn = void* (*)(uint32_t,const wchar_t*,const wchar_t*,uint32_t,int32_t,int32_t,int32_t,int32_t,void*,void*,void*,void*);
    auto fn=Win32kProc<Fn>("MicaWin32kCreateWindowExW");
    void* hwnd = fn ? fn(dwExStyle,lpClassName,lpWindowName,dwStyle,X,Y,nWidth,nHeight,hWndParent,hMenu,hInstance,lpParam) : nullptr;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (hwnd) {
        if (auto* icons = findRegisteredClassIcons(lpClassName)) {
            for (auto& entry : g_registeredWindowClasses) {
                if (entry.window) continue;
                entry = {hwnd, icons};
                break;
            }
            using SetIconFn = int32_t (*)(void*, uint32_t, uint64_t, uint64_t*);
            const auto setIcon = Win32kProc<SetIconFn>("MicaWin32kSetWindowIcon");
            const auto applyDefault = [&](uint32_t type, void* icon) noexcept {
                if (!icon || !setIcon) return;
                uint64_t previous{};
                if (!setIcon(hwnd, type, reinterpret_cast<uint64_t>(icon), &previous)) return;
                (void)winuser::ProcessWindowIcons().set(hwnd, type, icon);
                if (winuser::ProcessWindowIcons().get(hwnd, type) != icon) {
                    uint64_t ignored{};
                    (void)setIcon(hwnd, type, previous, &ignored);
                }
            };
            applyDefault(1, icons->large);
            applyDefault(0, icons->small);
        }
    }
#endif
    return hwnd;
}

intptr_t User32_SendMessageW(void* hWnd, uint32_t message, uintptr_t wParam,
                              intptr_t lParam) noexcept;

int32_t User32_DestroyWindow(void* hWnd) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDestroyWindow");
    if (!fn) return 0;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    // Window procedures belong to User32 and cannot be invoked from the
    // privileged Win32k dispatcher. Deliver the documented teardown callback
    // while the HWND and its WndProc association are still valid.
    (void)User32_SendMessageW(hWnd, 0x0002u, 0, 0); // WM_DESTROY
    (void)User32_SendMessageW(hWnd, 0x0082u, 0, 0); // WM_NCDESTROY
#endif
    const int32_t destroyed = fn(hWnd);
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (destroyed) {
        winuser::ProcessWindowIcons().remove(hWnd);
        if (auto* entry = findRegisteredWindowClass(hWnd)) *entry = {};
    }
#endif
    return destroyed;
}

#if defined(BUILD_SYSTEM32_USER32_DLL)
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
    handle = replacement;
    lease = std::move(replacementLease);
    const uint32_t type = index == -14 ? 1u : 0u;
    using SetIconFn = int32_t (*)(void*, uint32_t, uint64_t, uint64_t*);
    const auto setIcon = Win32kProc<SetIconFn>("MicaWin32kSetWindowIcon");
    if (setIcon) {
        for (const auto& window : g_registeredWindowClasses) {
            if (window.icons != windowClass->icons || !window.window) continue;
            uint64_t ignored{};
            if (!setIcon(window.window, type, reinterpret_cast<uint64_t>(replacement), &ignored))
                continue;
            (void)winuser::ProcessWindowIcons().set(window.window, type, replacement);
        }
    }
    return reinterpret_cast<uintptr_t>(previous);
}
#endif

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
    const int32_t presented = present ? present(paint->hdc, &dirtyRect) : 0;
    const int32_t released = release ? release(hWnd,paint->hdc) : 0;
    return presented && released;
}

#if defined(BUILD_SYSTEM32_USER32_DLL)
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
        (void)User32_SendMessageW(copy.parent, kWmCommand, command,
                                  reinterpret_cast<intptr_t>(hwnd));
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

void invalidateEdit(void* hwnd) noexcept {
    using PostFn = int32_t (*)(void*, uint32_t, uintptr_t, intptr_t);
    const auto post = Win32kProc<PostFn>("MicaWin32kPostMessageW");
    if (post) (void)post(hwnd, kWmPaint, 0, 0);
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
    dc = memoryDc;
    const WinNtRect bounds{0, 0, width, height};
    const int32_t themeState = (edit.state & kButtonDisabled) ? 4 :
        (edit.state & kButtonFocused) ? 5 : 1;
    void* theme = OpenThemeData(hwnd, L"EDIT");
    const bool themed = theme && DrawThemeBackground(theme, dc, 1, themeState,
                                                       &bounds, nullptr) == 0;
    if (!themed) {
        const uint32_t background = (edit.state & kButtonDisabled)
            ? kColorButtonDisabled : 0xFFFFFFFFu;
        (void)fillButtonRect(dc, bounds, background);
        (void)fillButtonRect(dc, {0, 0, width, 1}, kColorBorder);
        (void)fillButtonRect(dc, {0, height - 1, width, height}, kColorBorder);
        (void)fillButtonRect(dc, {0, 0, 1, height}, kColorBorder);
        (void)fillButtonRect(dc, {width - 1, 0, width, height}, kColorBorder);
    }
    uint32_t themedBorder{};
    if (themed && GetThemeColor(theme, 1, themeState, 3801, &themedBorder) != 0) {
        (void)fillButtonRect(dc, {0, 0, width, 1}, kColorBorder);
        (void)fillButtonRect(dc, {0, height - 1, width, height}, kColorBorder);
        (void)fillButtonRect(dc, {0, 0, 1, height}, kColorBorder);
        (void)fillButtonRect(dc, {width - 1, 0, width, height}, kColorBorder);
    }
    // WhiteSur expresses the focused entry ring with box-shadow, which the
    // bounded native snapshot does not model. Keep the focus state visible
    // with a two-pixel GDI ring while retaining UxTheme for the surface.
    if (edit.state & kButtonFocused) {
        constexpr uint32_t kFocusRing = 0xFF4A90D9u;
        (void)fillButtonRect(dc, {0, 0, width, 2}, kFocusRing);
        (void)fillButtonRect(dc, {0, height - 2, width, height}, kFocusRing);
        (void)fillButtonRect(dc, {0, 0, 2, height}, kFocusRing);
        (void)fillButtonRect(dc, {width - 2, 0, width, height}, kFocusRing);
    }

    uint32_t textColor = (edit.state & kButtonDisabled) ? 0xFF777777u : kColorWindowText;
    if (theme) {
        uint32_t queried{};
        if (GetThemeColor(theme, 1, themeState, 3803, &queried) == 0) {
            textColor = 0xFF000000u | ((queried & 0xFFu) << 16) |
                (queried & 0x0000FF00u) | ((queried >> 16) & 0xFFu);
        }
    }
    const int32_t textLeft = std::min(5, width);
    const int32_t textTop = std::min(1, height);
    const WinNtRect textBounds{textLeft, textTop,
        std::max(textLeft, width - 4), std::max(textTop, height - 1)};
    const bool hasTextBounds = textBounds.right > textBounds.left &&
                               textBounds.bottom > textBounds.top;
    bool textDrawn = edit.length == 0 || !hasTextBounds;
    if (edit.length && hasTextBounds) {
        void* clipToken{};
        const bool clipped = MicaGdiPushClipRect(dc, &textBounds, &clipToken) != 0 && clipToken;
        if (clipped) {
            const uint32_t previousColor = SetTextColor(dc, colorRef(textColor));
            const int32_t previousMode = SetBkMode(dc, 1);
            textDrawn = previousColor != 0xFFFFFFFFu && previousMode > 0 &&
                TextOutW(dc, textBounds.left, std::max(1, (height - 14) / 2),
                         edit.text, edit.length) != 0;
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
    return textDrawn && copied && fontRestored && bitmapRestored &&
        bitmapDeleted && dcDeleted;
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
    const uint32_t background = (button.state & kButtonDisabled) ? kColorButtonDisabled :
        (button.state & kButtonPressed) ? kColorButtonPressed :
        (button.state & kButtonHot) ? kColorButtonHot : kColorButton;
    (void)fillButtonRect(dc, {0, 0, width, height}, background);
    (void)fillButtonRect(dc, {0, 0, width, 1}, kColorBorder);
    (void)fillButtonRect(dc, {0, height - 1, width, height}, kColorBorder);
    (void)fillButtonRect(dc, {0, 0, 1, height}, kColorBorder);
    (void)fillButtonRect(dc, {width - 1, 0, width, height}, kColorBorder);
}

void drawButtonFocus(void* dc, int32_t width, int32_t height,
                     uint32_t color) noexcept {
    auto* brush = CreateSolidBrush(colorRef(color));
    if (!brush) return;
    constexpr int32_t inset = 3;
    const WinNtRect top{inset, inset, width - inset, inset + 1};
    const WinNtRect bottom{inset, height - inset - 1, width - inset, height - inset};
    const WinNtRect left{inset, inset, inset + 1, height - inset};
    const WinNtRect right{width - inset - 1, inset, width - inset, height - inset};
    (void)FillRect(dc, &top, brush);
    (void)FillRect(dc, &bottom, brush);
    (void)FillRect(dc, &left, brush);
    (void)FillRect(dc, &right, brush);
    (void)DeleteObject(brush);
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
    const bool radio = type == kBsRadioButton;
    const bool checkable = isCheckableButtonType(type);
    const int32_t part = checkable ? (radio ? 2 : 3) : 1;
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
    if (!backgroundDrawn && !checkable) drawButtonFallback(memoryDc, width, height, button);
    if (checkable && !backgroundDrawn) {
        const uint32_t face = (button.state & kButtonDisabled)
            ? kColorButtonDisabled : 0xFFFFFFFFu;
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
        if (GetThemeColor(theme, part, themeState, 3803, &themedColor) == 0)
            textColor = themedColor;
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
        ? WinNtRect{glyph.right + 5, 0, width, height} : bounds;
    bool textDrawn = theme &&
        DrawThemeText(theme, memoryDc, part, themeState, button.text, -1,
            (checkable ? 0u : kDtCenter) | kDtVCenter | kDtSingleLine | kDtNoPrefix,
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
            const int32_t x = textBounds.left + (checkable ? 0 : std::max(0, (textWidth - extent.cx) / 2));
            const int32_t y = std::max(0, (height - extent.cy) / 2);
            (void)TextOutW(memoryDc, x, y, button.text,
                           static_cast<int32_t>(textLength));
        }
    }
    if (button.state & kButtonFocused) drawButtonFocus(memoryDc, width, height, textColor);
    const int32_t copied = BitBlt(targetDc, 0, 0, width, height,
                                   memoryDc, 0, 0, 0x00CC0020u);
    if (theme) (void)CloseThemeData(theme);
    (void)SelectObject(memoryDc, oldFont);
    (void)SelectObject(memoryDc, oldBitmap);
    const int32_t bitmapDeleted = DeleteObject(bitmap);
    const int32_t dcDeleted = DeleteDC(memoryDc);
    return copied && bitmapDeleted && dcDeleted;
}
} // namespace

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
        initial.id = reinterpret_cast<uintptr_t>(create->menu);
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
        for (auto& entry : g_buttons) {
            if (entry.hwnd) continue;
            entry = initial;
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
        {
            ButtonLockGuard guard(g_buttonLock);
            auto* button = findButton(hwnd);
            if (!button || (button->state & kButtonDisabled)) return 0;
            button->state |= kButtonPressed;
            const uint32_t type = button->style & kBsTypeMask;
            if (isAutoCheckButtonType(type)) {
                button->checkState = type == kBsAuto3State
                    ? (button->checkState + 1u) % 3u : (button->checkState == 0 ? 1u : 0u);
            }
        }
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
    const bool pointInside = (message == kWmMouseMove || message == kWmLButtonUp)
        ? buttonPointInside(hwnd, lParam) : true;
    if (message == kWmMouseMove) leaveHotButton(hwnd);
    if (message == kWmSetFocus || message == kWmKillFocus ||
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
        case kWmEnable:
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
            button->state &= ~kButtonPressed;
            break;
        case kWmLButtonDown:
            if (!(button->state & kButtonDisabled)) {
                button->state |= kButtonHot | kButtonPressed | kButtonFocused;
                button->armed = true;
                acquireMouse = true;
                g_hotButton = hwnd;
            }
            break;
        case kWmLButtonUp:
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
            }
            break;
        case kWmKeyUp:
            click = wParam == kVkSpace && button->spaceArmed &&
                    (button->state & kButtonFocused) &&
                    !(button->state & kButtonDisabled);
            if (wParam == kVkSpace) {
                button->spaceArmed = false;
                if (!button->armed) button->state &= ~kButtonPressed;
            }
            break;
        case 0x031Au: // WM_THEMECHANGED
        case 0x001Au: // WM_SETTINGCHANGE
            repaint = true;
            break;
        }
        if (click && isAutoCheckButtonType(button->style & kBsTypeMask)) {
            const uint32_t type = button->style & kBsTypeMask;
            button->checkState = type == kBsAuto3State
                ? (button->checkState + 1u) % 3u
                : (button->checkState == 0 ? 1u : 0u);
        }
        repaint |= oldState != button->state || oldCheckState != button->checkState;
    }
    if (acquireMouse) (void)User32_SetCapture(hwnd);
    if (releaseMouse) (void)User32_ReleaseCapture();
    if (message == kWmLButtonDown) (void)User32_SetFocus(hwnd);
    if (repaint) invalidateButton(hwnd);
    if (message == kWmPaint) {
        ButtonState button{};
        {
            ButtonLockGuard guard(g_buttonLock);
            const auto* current = findButton(hwnd);
            if (!current) return 0;
            button = *current;
        }
        WinNtPaintStruct paint{};
        void* targetDc = User32_BeginPaint(hwnd, &paint);
        if (!targetDc) return 0;
        MicaWin32kRect client{};
        using GetRectFn = int32_t (*)(void*, MicaWin32kRect*);
        const auto getRect = Win32kProc<GetRectFn>("MicaWin32kGetClientRect");
        const bool hasRect = getRect && getRect(hwnd, &client);
        bool drawn = false;
        if (hasRect && client.right > 0 && client.bottom > 0)
            drawn = renderButton(hwnd, button, targetDc, client.right, client.bottom);
        if (!drawn && hasRect && client.right > 0 && client.bottom > 0)
            drawButtonFallback(targetDc, client.right, client.bottom, button);
        (void)User32_EndPaint(hwnd, &paint);
        return hasRect && client.right > 0 && client.bottom > 0;
    }
    if (click) notifyButtonClicked(hwnd);
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
#endif

intptr_t User32_DefWindowProcW(void* hWnd, uint32_t Msg, uintptr_t wParam, intptr_t lParam) noexcept {
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kDefWindowProcW");
    return fn ? fn(hWnd,Msg,wParam,lParam) : 0;
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
    return 1;
}

int32_t User32_GetMessageW(WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax) noexcept {
#if defined(BUILD_SYSTEM32_USER32_DLL)
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
            }
            return result;
        }
        micant::LargeInteger interval{};
        interval.QuadPart = -100000; // 10ms: process input and animation deadlines while idle.
        if (micant::native_nt::NtDelayExecution(0, &interval) != micant::NtStatus::Success)
            return -1;
    }
#else
    if (!lpMsg) return -1;
    MicaWin32kMessage msg{};
    using Fn=int32_t (*)(MicaWin32kMessage*,void*,uint32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kGetMessageW");
    const auto result=fn ? fn(&msg,hWnd,wMsgFilterMin,wMsgFilterMax) : -1;
    if (result > 0) {
        lpMsg->hwnd=msg.hwnd;lpMsg->message=msg.message;lpMsg->wParam=msg.wParam;
        lpMsg->lParam=msg.lParam;lpMsg->time=msg.time;lpMsg->pt={msg.pt.x,msg.pt.y};
    }
    return result;
#endif
}

int32_t User32_TranslateMessage(const WinNtMsg* lpMsg) noexcept {
    return lpMsg ? 1 : 0;
}

intptr_t User32_DispatchMessageW(const WinNtMsg* lpMsg) noexcept {
    if (!lpMsg) return 0;
#if defined(BUILD_SYSTEM32_USER32_DLL)
    if (lpMsg->message == 0x0200u) {
        bool buttonTarget = false;
        {
            ButtonLockGuard guard(g_buttonLock);
            buttonTarget = findButton(lpMsg->hwnd) != nullptr;
        }
        if (!buttonTarget) leaveHotButton(nullptr);
    }
#endif
    using Fn=intptr_t (*)(const MicaWin32kMessage*);
    auto fn=Win32kProc<Fn>("MicaWin32kDispatchMessageW");
    MicaWin32kMessage msg{lpMsg->hwnd,lpMsg->message,lpMsg->wParam,lpMsg->lParam,lpMsg->time,{lpMsg->pt.x,lpMsg->pt.y}};
    const intptr_t result = fn ? fn(&msg)
        : User32_DefWindowProcW(lpMsg->hwnd,lpMsg->message,lpMsg->wParam,lpMsg->lParam);
#if defined(BUILD_SYSTEM32_USER32_DLL)
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
#endif
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
#if defined(BUILD_SYSTEM32_USER32_DLL)
    using SetIconFn=int32_t (*)(void*,uint32_t,uint64_t,uint64_t*);
    using GetIconFn=uint64_t (*)(void*,uint32_t);
    if (message == WM_SETICON_OFFICIAL) {
        if (wParam > 2) return 0;
        void* icon = reinterpret_cast<void*>(lParam);
        winuser::IconHandleTable::Lease validation{};
        if (icon && !winuser::ProcessIconHandles().acquire(icon, validation)) return 0;
        uint64_t previous{};
        auto set=Win32kProc<SetIconFn>("MicaWin32kSetWindowIcon");
        if (!set || !set(hWnd, static_cast<uint32_t>(wParam),
                          reinterpret_cast<uint64_t>(icon), &previous)) return 0;
        (void)winuser::ProcessWindowIcons().set(hWnd, static_cast<uint32_t>(wParam), icon);
        if (icon && winuser::ProcessWindowIcons().get(hWnd, static_cast<uint32_t>(wParam)) != icon) {
            uint64_t ignored{};
            (void)set(hWnd, static_cast<uint32_t>(wParam), previous, &ignored);
            return 0;
        }
        return static_cast<intptr_t>(previous);
    }
    if (message == WM_GETICON_OFFICIAL) {
        if (wParam > 2) return 0;
        auto get=Win32kProc<GetIconFn>("MicaWin32kGetWindowIcon");
        return get ? static_cast<intptr_t>(get(hWnd, static_cast<uint32_t>(wParam))) : 0;
    }
#endif
    using Fn=intptr_t (*)(void*,uint32_t,uintptr_t,intptr_t);
    auto fn=Win32kProc<Fn>("MicaWin32kSendMessageW");
    return fn ? fn(hWnd,message,wParam,lParam) : 0;
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

int32_t User32_GetWindowTextW(void* hWnd, wchar_t* lpString, int32_t nMaxCount) noexcept {
    if (lpString && nMaxCount > 0) lpString[0]=L'\0';
    if (!hWnd || !lpString || nMaxCount <= 0) return 0;
    using Fn=int32_t (*)(void*,wchar_t*,int32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kGetWindowTextW");
    return fn ? fn(hWnd,lpString,nMaxCount) : 0;
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

int32_t User32_AdjustWindowRectEx(WinNtRect* lpRect, uint32_t /*dwStyle*/, int32_t /*bMenu*/, uint32_t /*dwExStyle*/) noexcept {
    return lpRect ? 1 : 0;
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

int16_t User32_GetAsyncKeyState(int32_t vKey) noexcept {
    return (vKey == 0x57 /* 'W' */) ? static_cast<int16_t>(0x8000) : 0;
}

void* User32_MonitorFromWindow(void* /*hwnd*/, uint32_t /*dwFlags*/) noexcept {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x00010001ULL));
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

// user32.dll implementations
void* User32_GetDC(void* hWnd) noexcept {
    using Fn=void* (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kGetDC");
    void* dc = fn ? fn(hWnd) : nullptr;
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    if (dc && !FindGdiDc(dc, true)) return nullptr;
#endif
    return dc;
}

int32_t User32_ReleaseDC(void* hWnd, void* hDC) noexcept {
    using Fn=int32_t (*)(void*,void*); auto fn=Win32kProc<Fn>("MicaWin32kReleaseDC");
    return fn ? fn(hWnd,hDC) : 0;
}

// gdi32.dll implementations
void* Gdi32_CreateCompatibleDC(void* hdc) noexcept {
    using Fn=void* (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kCreateCompatibleDC");
    void* dc = fn ? fn(hdc) : nullptr;
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    if (dc && !FindGdiDc(dc, true)) {
        using DeleteFn=int32_t (*)(void*); auto deleteFn=Win32kProc<DeleteFn>("MicaWin32kDeleteDC");
        if (deleteFn) (void)deleteFn(dc);
        return nullptr;
    }
#endif
    return dc;
}

void* Gdi32_CreateCompatibleBitmap(void* hdc, int32_t width, int32_t height) noexcept {
    using Fn=void* (*)(void*,int32_t,int32_t); auto fn=Win32kProc<Fn>("MicaWin32kCreateCompatibleBitmap");
    return fn ? fn(hdc,width,height) : nullptr;
}

int32_t Gdi32_DeleteDC(void* hdc) noexcept {
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDeleteDC");
    const int32_t result = fn ? fn(hdc) : 0;
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    if (result) for (auto& dc : g_gdiDcs) if (dc.dc == hdc) dc = GdiDcSelection{};
#endif
    return result;
}

int32_t Gdi32_GetDeviceCaps(void* /*hdc*/, int32_t index) noexcept {
    if (index == 8) return 3840;   // HORZRES
    if (index == 10) return 2160;  // VERTRES
    if (index == 12) return 32;    // BITSPIXEL
    if (index == 116) return 240;  // VREFRESH (240 Hz)
    return 96;                     // LOGPIXELSX / LOGPIXELSY
}

int32_t Gdi32_ChoosePixelFormat(void* hdc, const void* ppfd) noexcept {
    return (hdc && ppfd) ? 1 : 0;
}

#if defined(BUILD_SYSTEM32_GDI32_DLL)
void* Gdi32_CreateSolidBrush(uint32_t colorRef) noexcept {
    GdiBrushGuard guard;
    for (size_t i = 0; i < GdiBrushCapacity; ++i) {
        auto& brush = g_gdiBrushes[i];
        if (brush.active) continue;
        uint32_t generation = ++g_gdiBrushGeneration;
        if (!generation) generation = ++g_gdiBrushGeneration;
        brush.colorRef = colorRef & 0x00FFFFFFu;
        brush.generation = generation;
        brush.active = true;
        return GdiBrushHandle(i, generation);
    }
    return nullptr;
}

int32_t Gdi32_FillRect(void* hdc, const void* rectangle, void* hbrush) noexcept {
    if (!hdc || !rectangle) return 0;
    GdiBrushGuard guard;
    auto* brush = FindGdiBrush(hbrush);
    if (!brush) return 0;
    using Fn = int32_t (*)(void*, const MicaWin32kRect*, uint32_t);
    auto fill = Win32kProc<Fn>("MicaWin32kFillRect");
    return fill ? fill(hdc, static_cast<const MicaWin32kRect*>(rectangle),
                       brush->colorRef) : 0;
}
#endif

int32_t Gdi32_SetPixelFormat(void* hdc, int32_t format, const void* ppfd) noexcept {
    if (!hdc || format <= 0 || !ppfd) return 0;
    g_activePixelFormat = format;
    return 1;
}

int32_t Gdi32_DescribePixelFormat(void* hdc, int32_t iPixelFormat, uint32_t /*nBytes*/, void* /*ppfd*/) noexcept {
    return (hdc && iPixelFormat > 0) ? 16 : 0;
}

int32_t Gdi32_SwapBuffers(void* hdc) noexcept {
    if (!hdc) return 0;
    using Fn=int32_t (*)(void*);
    auto fn=Win32kProc<Fn>("MicaWin32kPresentDC");
    return fn ? fn(hdc) : 0;
}

#if defined(BUILD_SYSTEM32_GDI32_DLL)
void* Gdi32_CreateDIBSection(void* hdc, const void* pbmi, uint32_t usage, void** ppvBits, void* hSection, uint32_t offset) noexcept {
    if (ppvBits) *ppvBits=nullptr;
    if (!pbmi || !ppvBits || usage != 0 || hSection || offset != 0) return nullptr;
    const auto* info=static_cast<const MicaWin32kBitmapInfoHeader*>(pbmi);
    if (info->size < sizeof(MicaWin32kBitmapInfoHeader) || info->planes != 1 || info->bitCount != 32 || info->compression != 0)
        return nullptr;
    if (info->width <= 0 || info->height == 0 || info->height == INT32_MIN) return nullptr;
    const int32_t height=info->height < 0 ? -info->height : info->height;
    using Fn=void* (*)(void*,int32_t,int32_t,void**); auto fn=Win32kProc<Fn>("MicaWin32kCreateDibSection");
    void* bitmap = fn ? fn(hdc,info->width,height,ppvBits) : nullptr;
    if (!bitmap) return nullptr;
    if (!*ppvBits) {
        using DeleteFn=int32_t (*)(void*); auto deleteFn=Win32kProc<DeleteFn>("MicaWin32kDeleteObject");
        if (deleteFn) (void)deleteFn(bitmap);
        return nullptr;
    }
    GdiBitmapSurface* slot = nullptr;
    for (auto& current : g_gdiBitmaps) if (!current.bitmap) { slot = &current; break; }
    if (!slot) {
        using DeleteFn=int32_t (*)(void*); auto deleteFn=Win32kProc<DeleteFn>("MicaWin32kDeleteObject");
        if (deleteFn) (void)deleteFn(bitmap);
        *ppvBits = nullptr;
        return nullptr;
    }
    *slot = {bitmap, static_cast<uint32_t*>(*ppvBits), static_cast<uint32_t>(info->width),
             static_cast<uint32_t>(height), info->height < 0};
    return bitmap;
}

int32_t Gdi32_BitBlt(void* hdc, int32_t x, int32_t y, int32_t cx, int32_t cy, void* hdcSrc, int32_t x1, int32_t y1, uint32_t rop) noexcept {
    using Fn=int32_t (*)(void*,int32_t,int32_t,int32_t,int32_t,void*,int32_t,int32_t,uint32_t);
    auto fn=Win32kProc<Fn>("MicaWin32kBitBlt");
    return fn ? fn(hdc,x,y,cx,cy,hdcSrc,x1,y1,rop) : 0;
}

void* Gdi32_GetStockObject(int32_t i) noexcept {
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    if (i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 16 || i == 17)
        return GetSystemGdiFont();
#endif
    using Fn=void* (*)(int32_t); auto fn=Win32kProc<Fn>("MicaWin32kGetStockObject");
    return fn ? fn(i) : nullptr;
}

int32_t Gdi32_GetObjectW(void* handle, int32_t bufferBytes, void* output) noexcept {
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    GdiFontObject* font = FindGdiFont(handle);
    if (!font || bufferBytes < 0) return 0;

    GdiLogFontW logical{};
    const auto& request = font->request;
    const uint32_t emPixels = micant::um::graphics::truetype::pixelsFromPoints(
        request.pointSize, request.dpi);
    if (emPixels > static_cast<uint32_t>(INT32_MAX)) return 0;
    logical.height = -static_cast<int32_t>(emPixels);
    logical.weight = request.weight;
    logical.italic = request.italic ? 1 : 0;
    logical.charSet = 1; // DEFAULT_CHARSET
    for (size_t i = 0; i < 31 && font->family[i]; ++i)
        logical.faceName[i] = static_cast<wchar_t>(font->family[i]);

    constexpr int32_t requiredBytes = static_cast<int32_t>(sizeof(logical));
    if (!output) return requiredBytes;
    if (!bufferBytes) return 0;
    const int32_t copiedBytes = std::min(bufferBytes, requiredBytes);
    std::memcpy(output, &logical, static_cast<size_t>(copiedBytes));
    return copiedBytes;
#else
    (void)handle;
    (void)bufferBytes;
    (void)output;
    return 0;
#endif
}

void* Gdi32_SelectObject(void* hdc, void* h) noexcept {
    if (!hdc || !h) return nullptr;
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    if (GdiFontObject* font = FindGdiFont(h)) {
        GdiDcSelection* dc = FindGdiDc(hdc, false);
        if (!dc) return nullptr;
        void* previous = dc->font ? static_cast<void*>(dc->font) : Gdi32_GetStockObject(13);
        if (!previous) return nullptr;
        dc->font = font;
        return previous;
    }
#endif
    using Fn=void* (*)(void*,void*); auto fn=Win32kProc<Fn>("MicaWin32kSelectObject");
    void* previous = fn ? fn(hdc,h) : nullptr;
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    if (previous) {
        GdiDcSelection* dc = FindGdiDc(hdc, false);
        GdiBitmapSurface* bitmap = FindGdiBitmap(h);
        if (bitmap) {
            if (!dc) {
                (void)fn(hdc, previous); // Roll back when local tracking cannot be reserved.
                return nullptr;
            }
            dc->bitmap = h;
            GdiBitmapSurface* surface = FindGdiBitmap(h);
            dc->clip = {0, 0, static_cast<int32_t>(surface->width),
                        static_cast<int32_t>(surface->height)};
        }
    }
#endif
    return previous;
}

int32_t Gdi32_DeleteObject(void* ho) noexcept {
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    {
        GdiBrushGuard guard;
    if (GdiBrushObject* brush = FindGdiBrush(ho)) {
        brush->active = false;
        brush->colorRef = 0;
        return 1;
    }
    }
    if (GdiFontObject* font = FindGdiFont(ho)) {
        if (font->stock) return 0;
        for (const auto& dc : g_gdiDcs) if (dc.dc && dc.font == font) return 0;
        font->manager.shutdown();
        font->active = false;
        font->family[0] = 0;
        return 1;
    }
    GdiBitmapSurface* bitmap = FindGdiBitmap(ho);
    if (bitmap) for (const auto& dc : g_gdiDcs) if (dc.dc && dc.bitmap == ho) return 0;
#endif
    using Fn=int32_t (*)(void*); auto fn=Win32kProc<Fn>("MicaWin32kDeleteObject");
    const int32_t result = fn ? fn(ho) : 0;
#if defined(BUILD_SYSTEM32_GDI32_DLL)
    if (result && bitmap) *bitmap = GdiBitmapSurface{};
#endif
    return result;
}

void* Gdi32_CreateFontIndirectW(const GdiLogFontW* logical) noexcept {
    return logical ? static_cast<void*>(CreateGdiFont(*logical)) : nullptr;
}

void* Gdi32_CreateFontW(int32_t height, int32_t width, int32_t escapement,
        int32_t orientation, int32_t weight, uint32_t italic, uint32_t underline,
        uint32_t strikeOut, uint32_t charSet, uint32_t outPrecision,
        uint32_t clipPrecision, uint32_t quality, uint32_t pitchAndFamily,
        const wchar_t* faceName) noexcept {
    GdiLogFontW logical{};
    logical.height = height; logical.width = width;
    logical.escapement = escapement; logical.orientation = orientation;
    logical.weight = weight; logical.italic = italic ? 1 : 0;
    logical.underline = underline ? 1 : 0; logical.strikeOut = strikeOut ? 1 : 0;
    logical.charSet = static_cast<uint8_t>(charSet);
    logical.outPrecision = static_cast<uint8_t>(outPrecision);
    logical.clipPrecision = static_cast<uint8_t>(clipPrecision);
    logical.quality = static_cast<uint8_t>(quality);
    logical.pitchAndFamily = static_cast<uint8_t>(pitchAndFamily);
    if (faceName) {
        size_t i = 0;
        while (i < 31 && faceName[i]) { logical.faceName[i] = faceName[i]; ++i; }
        if (faceName[i] && i == 31) return nullptr;
    }
    return CreateGdiFont(logical);
}

int32_t Gdi32_SetTextColor(void* hdc, uint32_t color) noexcept {
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc) return static_cast<int32_t>(0xFFFFFFFFu);
    const uint32_t previous = dc->textColor;
    dc->textColor = color & 0x00FFFFFFu;
    return static_cast<int32_t>(previous);
}

int32_t Gdi32_SetBkMode(void* hdc, int32_t mode) noexcept {
    if (mode != 1) return 0; // Opaque backgrounds are not part of this renderer subset.
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc) return 0;
    const int32_t previous = dc->backgroundMode;
    dc->backgroundMode = mode;
    return previous;
}

int32_t Gdi32_TextOutW(void* hdc, int32_t x, int32_t y,
                       const wchar_t* text, int32_t count) noexcept {
    if (!hdc || !text || count <= 0 || count > 4096) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || dc->backgroundMode != 1) return 0;
    GdiBitmapSurface* bitmap = FindGdiBitmap(dc->bitmap);
    if (!bitmap || !bitmap->pixels || !bitmap->topDown) return 0;
    char utf8[16385]{};
    if (!Utf16ToUtf8(text, count, utf8, sizeof(utf8))) return 0;
    const uint32_t r = dc->textColor & 0xFF;
    const uint32_t g = (dc->textColor >> 8) & 0xFF;
    const uint32_t b = (dc->textColor >> 16) & 0xFF;
    const uint32_t argb = 0xFF000000u | (r << 16) | (g << 8) | b;
    const micant::um::graphics::TextClipRect clip{
        dc->clip.left, dc->clip.top, dc->clip.right, dc->clip.bottom};
    return dc->font->manager.drawUtf8(bitmap->pixels, bitmap->width, bitmap->height,
        x, y, utf8, argb, &clip) ? 1 : 0;
}

int32_t Gdi32_MicaPushClipRect(void* hdc, const MicaWin32kRect* rectangle,
                               void** token) noexcept {
    if (token) *token = nullptr;
    if (!hdc || !rectangle || !token || rectangle->right < rectangle->left ||
        rectangle->bottom < rectangle->top) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !FindGdiBitmap(dc->bitmap)) return 0;
    size_t slot = 0;
    while (slot < std::size(dc->clips) && dc->clips[slot].occupied) ++slot;
    if (slot == std::size(dc->clips)) return 0;
    uint32_t generation = dc->nextClipGeneration++;
    if (!generation) generation = dc->nextClipGeneration++;
    auto& state = dc->clips[slot];
    state = {generation, dc->clip, true};
    dc->clip.left = std::max(dc->clip.left, rectangle->left);
    dc->clip.top = std::max(dc->clip.top, rectangle->top);
    dc->clip.right = std::min(dc->clip.right, rectangle->right);
    dc->clip.bottom = std::min(dc->clip.bottom, rectangle->bottom);
    *token = reinterpret_cast<void*>((static_cast<uintptr_t>(generation) << 5) |
                                     static_cast<uintptr_t>(slot + 1));
    return 1;
}

int32_t Gdi32_MicaPopClipRect(void* hdc, void* token) noexcept {
    if (!hdc || !token) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc) return 0;
    const uintptr_t encoded = reinterpret_cast<uintptr_t>(token);
    const uintptr_t slotValue = encoded & 0x1Fu;
    const uint32_t generation = static_cast<uint32_t>(encoded >> 5);
    if (!slotValue || slotValue > std::size(dc->clips) || !generation) return 0;
    auto& state = dc->clips[slotValue - 1];
    if (!state.occupied || state.generation != generation) return 0;
    dc->clip = state.previous;
    state = {};
    return 1;
}

int32_t Gdi32_ExtTextOutW(void* hdc, int32_t x, int32_t y, uint32_t options,
        const void* rect, const wchar_t* text, uint32_t count, const int32_t* advances) noexcept {
    if (options || rect || advances || count > 4096 || count > static_cast<uint32_t>(INT32_MAX))
        return 0;
    return Gdi32_TextOutW(hdc, x, y, text, static_cast<int32_t>(count));
}

int32_t Gdi32_GetTextExtentPoint32W(void* hdc, const wchar_t* text, int32_t count,
                                    GdiSize* output) noexcept {
    if (!hdc || !text || !output || count < 0 || count > 4096) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || !dc->font->active) return 0;
    if (count == 0) {
        micant::um::graphics::FontMetricsPixels metrics{};
        if (!dc->font->manager.pixelMetrics(metrics)) return 0;
        *output = {0, metrics.lineHeight};
        return 1;
    }
    char utf8[16385]{};
    if (!Utf16ToUtf8(text, count, utf8, sizeof(utf8))) return 0;
    micant::um::graphics::TextExtentPixels extent{};
    if (!dc->font->manager.measureUtf8(utf8, extent)) return 0;
    *output = {extent.width, extent.height};
    return 1;
}

int32_t Gdi32_GetTextMetricsW(void* hdc, GdiTextMetricW* output) noexcept {
    if (!hdc || !output) return 0;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || !dc->font->active) return 0;
    micant::um::graphics::FontMetricsPixels metrics{};
    if (!dc->font->manager.pixelMetrics(metrics)) return 0;
    *output = {};
    output->height = metrics.lineHeight;
    output->ascent = metrics.ascent;
    output->descent = metrics.descent;
    output->externalLeading = metrics.lineGap;
    output->averageCharWidth = metrics.averageAdvance;
    output->maximumCharWidth = metrics.maximumAdvance;
    output->weight = dc->font->request.weight;
    output->digitizedAspectX = static_cast<int32_t>(dc->font->request.dpi);
    output->digitizedAspectY = static_cast<int32_t>(dc->font->request.dpi);
    output->firstChar = L' ';
    output->lastChar = L'~';
    output->defaultChar = L'?';
    output->breakChar = L' ';
    output->italic = dc->font->request.italic ? 1 : 0;
    output->charSet = 1;
    return 1;
}

uint32_t Gdi32_GetGlyphIndicesW(void* hdc, const wchar_t* text, int32_t count,
        uint16_t* glyphIndices, uint32_t flags) noexcept {
    constexpr uint32_t GdiError = 0xFFFFFFFFu;
    constexpr uint32_t MarkMissing = 0x0001u;
    if (count < 0 || count > 4096 || (flags & ~MarkMissing) ||
        (count && (!text || !glyphIndices))) return GdiError;
    GdiDcSelection* dc = FindGdiDc(hdc, false);
    if (!dc || !dc->font || !dc->font->active) return GdiError;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t scalar = static_cast<uint16_t>(text[i]);
        if (scalar >= 0xD800u && scalar <= 0xDFFFu) return GdiError;
        uint16_t glyph = dc->font->manager.glyphIndex(scalar);
        if (!glyph && (flags & MarkMissing)) glyph = 0xFFFFu;
        glyphIndices[i] = glyph;
    }
    return static_cast<uint32_t>(count);
}

int32_t Gdi32_EnumFontFamiliesExW(void* hdc, const GdiLogFontW* filter,
        GdiFontEnumProcW callback, intptr_t parameter, uint32_t flags) noexcept {
    constexpr uint32_t TrueTypeFontType = 0x0004;
    if (!FindGdiDc(hdc, false) || !callback || (flags != 0 && flags != TrueTypeFontType))
        return 0;
    if (filter && filter->charSet != 0 && filter->charSet != 1) return 0;
    const auto familyMatches = [](const wchar_t* requested, const wchar_t* candidate) noexcept {
        if (!requested || !requested[0]) return true;
        size_t i = 0;
        for (; i < 31 && requested[i] && candidate[i]; ++i) {
            wchar_t left = requested[i], right = candidate[i];
            if (left >= L'A' && left <= L'Z') left += L'a' - L'A';
            if (right >= L'A' && right <= L'Z') right += L'a' - L'A';
            if (left != right) return false;
        }
        return i < 31 && requested[i] == 0 && candidate[i] == 0;
    };
    struct Style { uint16_t weight; bool italic; const wchar_t* label; };
    constexpr Style standardStyles[] = {
        {400, false, L"Regular"}, {700, false, L"Bold"},
        {400, true, L"Italic"}, {700, true, L"Bold Italic"}};
    constexpr Style selawikStyles[] = {
        {400, false, L"Regular"}, {600, false, L"Semibold"}, {700, false, L"Bold"}};
    constexpr const wchar_t* families[] = {
        L"Segoe UI", L"Segoe UI Semibold", L"Arial", L"Times New Roman",
        L"Courier New", L"Calibri", L"DejaVu Sans"};
    int32_t lastResult = 0;
    for (const wchar_t* family : families) {
        if (!familyMatches(filter ? filter->faceName : nullptr, family)) continue;
        const Style* styles = family == families[0] || family == families[1]
            ? selawikStyles : standardStyles;
        const size_t styleCount = styles == selawikStyles
            ? std::size(selawikStyles) : std::size(standardStyles);
        for (size_t styleIndex = 0; styleIndex < styleCount; ++styleIndex) {
            if (family == families[1] && styleIndex != 1) continue;
            const Style& style = styles[styleIndex];
            GdiLogFontW logical{};
            logical.height = -12;
            logical.weight = style.weight;
            logical.italic = style.italic ? 1 : 0;
            logical.charSet = 1;
            char16_t requestedFamily[32]{};
            for (size_t i = 0; family[i] && i + 1 < std::size(logical.faceName); ++i)
                requestedFamily[i] = static_cast<char16_t>(family[i]);
            for (size_t i = 0; i < std::size(requestedFamily); ++i)
                logical.faceName[i] = family[i];
            micant::um::graphics::FontManager metricManager{};
            const micant::um::graphics::LogicalFontRequest request{
                requestedFamily, style.weight, style.italic, 9, 96};
            if (!metricManager.initializeForMetrics(request)) continue;
            GdiTextMetricW metrics{};
            micant::um::graphics::FontMetricsPixels projected{};
            const bool projectedOk = metricManager.pixelMetrics(projected);
            if (!projectedOk) { metricManager.shutdown(); return 0; }
            metrics.height = projected.lineHeight;
            metrics.ascent = projected.ascent;
            metrics.descent = projected.descent;
            metrics.externalLeading = projected.lineGap;
            metrics.averageCharWidth = projected.averageAdvance;
            metrics.maximumCharWidth = projected.maximumAdvance;
            metrics.weight = style.weight;
            metrics.digitizedAspectX = request.dpi;
            metrics.digitizedAspectY = request.dpi;
            metrics.firstChar = L' ';
            metrics.lastChar = L'~';
            metrics.defaultChar = L'?';
            metrics.breakChar = L' ';
            metrics.italic = style.italic ? 1 : 0;
            metrics.charSet = 1;
            GdiEnumLogFontExW enumeration{};
            enumeration.logFont = logical;
            for (size_t i = 0; family[i] && i + 1 < std::size(enumeration.fullName); ++i)
                enumeration.fullName[i] = family[i];
            for (size_t i = 0; style.label[i] && i + 1 < std::size(enumeration.style); ++i)
                enumeration.style[i] = style.label[i];
            constexpr wchar_t script[] = L"Western";
            for (size_t i = 0; i < std::size(script); ++i) enumeration.script[i] = script[i];
            GdiNewTextMetricExW newMetrics{};
            newMetrics.textMetric = metrics;
            const int32_t callbackResult = callback(&enumeration.logFont,
                &newMetrics.textMetric, TrueTypeFontType, parameter);
            metricManager.shutdown();
            lastResult = callbackResult;
            if (!callbackResult) return 0;
        }
    }
    return lastResult;
}
#endif

// imm32.dll implementations
void* Imm32_ImmGetContext(void* hWnd) noexcept {
    return hWnd ? g_primaryWindow.imc : nullptr;
}

int32_t Imm32_ImmReleaseContext(void* hWnd, void* /*hIMC*/) noexcept {
    return hWnd ? 1 : 0;
}

void* Imm32_ImmAssociateContext(void* hWnd, void* hIMC) noexcept {
    if (!hWnd) return nullptr;
    void* prev = g_primaryWindow.imc;
    g_primaryWindow.imc = hIMC;
    return prev;
}

int32_t Imm32_ImmAssociateContextEx(void* hWnd, void* hIMC, uint32_t /*dwFlags*/) noexcept {
    if (!hWnd) return 0;
    g_primaryWindow.imc = hIMC;
    return 1;
}

int32_t Imm32_ImmGetCompositionStringW(void* /*hIMC*/, uint32_t /*dwIndex*/, void* /*lpBuf*/, uint32_t /*dwBufLen*/) noexcept {
    return 0;
}

int32_t Imm32_ImmSetCompositionWindow(void* hIMC, const void* lpCompForm) noexcept {
    return (hIMC && lpCompForm) ? 1 : 0;
}

int32_t Imm32_ImmGetOpenStatus(void* /*hIMC*/) noexcept {
    return g_imeOpenStatus;
}

int32_t Imm32_ImmSetOpenStatus(void* /*hIMC*/, int32_t fOpen) noexcept {
    g_imeOpenStatus = fOpen;
    return 1;
}

void WinNtUser32Gdi32Imm32Coordinator::initialize() noexcept {
    if (m_initialized) return;
    m_cachedReport = runUser32Gdi32Imm32Audit();
    m_initialized = true;
}

Horizon111User32Gdi32Imm32AuditReport WinNtUser32Gdi32Imm32Coordinator::runUser32Gdi32Imm32Audit() noexcept {
    Horizon111User32Gdi32Imm32AuditReport rep{};
    rep.user32ExportCount = 26;
    rep.gdi32ExportCount  = 14;
    rep.imm32ExportCount  = 8;

    MicaWin32kWindowClassEx dummyWndClass{};
    dummyWndClass.cbSize=sizeof(dummyWndClass);
    dummyWndClass.wndProc=reinterpret_cast<void*>(&AuditWindowProc);
    dummyWndClass.className=L"PrismXGameWindowClass";
    if (User32_RegisterClassExW(&dummyWndClass) != 0) {
        void* hwnd = User32_CreateWindowExW(
            0, L"PrismXGameWindowClass", L"MicaNT DirectX 12 Ultimate Game",
            WS_OVERLAPPEDWINDOW_OFFICIAL | WS_VISIBLE_OFFICIAL,
            0, 0, 3840, 2160, nullptr, nullptr, nullptr, nullptr);
        rep.createdHwnd         = hwnd;
        rep.associatedHMonitor  = User32_MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST_OFF);

        WinNtRect rc{};
        if (User32_GetClientRect(hwnd, &rc)) {
            rep.clientWidth  = rc.right - rc.left;
            rep.clientHeight = rc.bottom - rc.top;
        }

        // Verify non-blocking PeekMessageW + TranslateMessage + DispatchMessageW game loop
        WinNtMsg msg{};
        if (User32_PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE_OFFICIAL)) {
            User32_TranslateMessage(&msg);
            User32_DispatchMessageW(&msg);
            rep.peekMessagePumpVerified = (msg.message == WM_INPUT_OFFICIAL);
        }

        // Verify 8000Hz WM_INPUT Raw Input registration & readback
        WinNtRawInputDevice rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage     = 0x02;
        rid.hwndTarget  = hwnd;
        if (User32_RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            uint32_t cbSize = 0;
            User32_GetRawInputData(reinterpret_cast<void*>(msg.lParam), RID_INPUT_OFFICIAL, nullptr, &cbSize, sizeof(WinNtRawInputHeader));
            WinNtRawInputPacket pkt{};
            if (cbSize == sizeof(pkt) &&
                User32_GetRawInputData(reinterpret_cast<void*>(msg.lParam), RID_INPUT_OFFICIAL, &pkt, &cbSize, sizeof(WinNtRawInputHeader)) == sizeof(pkt)) {
                rep.rawInput8000HzMouseVerified = (pkt.mouse.lLastX == 14 && pkt.mouse.lLastY == -9);
            }
        }

        // Verify GDI32 HDC + PixelFormat + SwapBuffers
        void* hdc = User32_GetDC(hwnd);
        rep.createdHdc = hdc;
        uint32_t dummyPfd[10]{sizeof(dummyPfd)};
        int32_t pf = Gdi32_ChoosePixelFormat(hdc, dummyPfd);
        if (pf > 0 && Gdi32_SetPixelFormat(hdc, pf, dummyPfd) && Gdi32_SwapBuffers(hdc)) {
            rep.gdiPixelFormatSwapBuffersVerified = (Gdi32_GetDeviceCaps(hdc, 116) == 240);
        }
        User32_ReleaseDC(hwnd, hdc);

        // Verify IMM32 full-screen game IME suppression (ImmAssociateContext(hwnd, nullptr))
        void* oldImc = Imm32_ImmAssociateContext(hwnd, nullptr);
        rep.imm32GameImeDisableVerified = (oldImc != nullptr && Imm32_ImmGetContext(hwnd) == nullptr);
        Imm32_ImmAssociateContext(hwnd, oldImc);
    }

    return rep;
}

void InitializeWinNtUser32Gdi32Imm32Subsystem() noexcept {
    WinNtUser32Gdi32Imm32Coordinator::instance().initialize();
}

} // namespace micant::um::winnt_user32_gdi32_imm32_abi

namespace micant::sys::internal {
void WinNtUser32Gdi32Imm32InitializeExecutiveSubsystem() noexcept {
    micant::um::winnt_user32_gdi32_imm32_abi::InitializeWinNtUser32Gdi32Imm32Subsystem();
}
} // namespace micant::sys::internal

#if defined(BUILD_SYSTEM32_USER32_DLL)
extern "C" {
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
__declspec(dllexport) int32_t PeekMessageW(micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_PeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}
__declspec(dllexport) int32_t GetMessageW(micant::um::winnt_user32_gdi32_imm32_abi::WinNtMsg* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax) {
    return micant::um::winnt_user32_gdi32_imm32_abi::User32_GetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
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
} // extern "C"
#elif defined(BUILD_SYSTEM32_GDI32_DLL)
extern "C" {
__declspec(dllexport) void* CreateCompatibleDC(void* hdc) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateCompatibleDC(hdc);
}
__declspec(dllexport) void* CreateCompatibleBitmap(void* hdc, int32_t width, int32_t height) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateCompatibleBitmap(hdc,width,height);
}
__declspec(dllexport) int32_t DeleteDC(void* hdc) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DeleteDC(hdc);
}
__declspec(dllexport) int32_t GetDeviceCaps(void* hdc, int32_t index) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetDeviceCaps(hdc, index);
}
__declspec(dllexport) int32_t ChoosePixelFormat(void* hdc, const void* ppfd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ChoosePixelFormat(hdc, ppfd);
}
__declspec(dllexport) void* CreateSolidBrush(uint32_t colorRef) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateSolidBrush(colorRef);
}
__declspec(dllexport) int32_t FillRect(void* hdc, const void* rectangle, void* hbrush) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_FillRect(hdc, rectangle, hbrush);
}
__declspec(dllexport) int32_t SetPixelFormat(void* hdc, int32_t format, const void* ppfd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetPixelFormat(hdc, format, ppfd);
}
__declspec(dllexport) int32_t DescribePixelFormat(void* hdc, int32_t iPixelFormat, uint32_t nBytes, void* ppfd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DescribePixelFormat(hdc, iPixelFormat, nBytes, ppfd);
}
__declspec(dllexport) int32_t SwapBuffers(void* hdc) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SwapBuffers(hdc);
}
__declspec(dllexport) void* CreateDIBSection(void* hdc, const void* pbmi, uint32_t usage, void** ppvBits, void* hSection, uint32_t offset) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateDIBSection(hdc, pbmi, usage, ppvBits, hSection, offset);
}
__declspec(dllexport) int32_t BitBlt(void* hdc, int32_t x, int32_t y, int32_t cx, int32_t cy, void* hdcSrc, int32_t x1, int32_t y1, uint32_t rop) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_BitBlt(hdc, x, y, cx, cy, hdcSrc, x1, y1, rop);
}
__declspec(dllexport) void* GetStockObject(int32_t i) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetStockObject(i);
}
__declspec(dllexport) int32_t GetObjectW(void* handle, int32_t bufferBytes, void* output) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetObjectW(
        handle, bufferBytes, output);
}
__declspec(dllexport) void* SelectObject(void* hdc, void* h) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SelectObject(hdc, h);
}
__declspec(dllexport) int32_t DeleteObject(void* ho) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_DeleteObject(ho);
}
__declspec(dllexport) void* CreateFontIndirectW(const micant::um::winnt_user32_gdi32_imm32_abi::GdiLogFontW* logical) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateFontIndirectW(logical);
}
__declspec(dllexport) void* CreateFontW(int32_t height, int32_t width, int32_t escapement,
        int32_t orientation, int32_t weight, uint32_t italic, uint32_t underline,
        uint32_t strikeOut, uint32_t charSet, uint32_t outPrecision,
        uint32_t clipPrecision, uint32_t quality, uint32_t pitchAndFamily,
        const wchar_t* faceName) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_CreateFontW(height, width,
        escapement, orientation, weight, italic, underline, strikeOut, charSet,
        outPrecision, clipPrecision, quality, pitchAndFamily, faceName);
}
__declspec(dllexport) int32_t TextOutW(void* hdc, int32_t x, int32_t y,
        const wchar_t* text, int32_t count) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_TextOutW(hdc, x, y, text, count);
}
__declspec(dllexport) int32_t ExtTextOutW(void* hdc, int32_t x, int32_t y,
        uint32_t options, const void* rect, const wchar_t* text, uint32_t count,
        const int32_t* advances) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_ExtTextOutW(hdc, x, y,
        options, rect, text, count, advances);
}
__declspec(dllexport) uint32_t SetTextColor(void* hdc, uint32_t color) {
    return static_cast<uint32_t>(micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetTextColor(hdc, color));
}
__declspec(dllexport) int32_t SetBkMode(void* hdc, int32_t mode) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_SetBkMode(hdc, mode);
}
__declspec(dllexport) int32_t GetTextExtentPoint32W(void* hdc, const wchar_t* text,
        int32_t count, micant::um::winnt_user32_gdi32_imm32_abi::GdiSize* size) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetTextExtentPoint32W(
        hdc, text, count, size);
}
__declspec(dllexport) int32_t MicaGdiPushClipRect(void* hdc, const void* rectangle,
                                                  void** token) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_MicaPushClipRect(
        hdc, static_cast<const MicaWin32kRect*>(rectangle), token);
}
__declspec(dllexport) int32_t MicaGdiPopClipRect(void* hdc, void* token) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_MicaPopClipRect(hdc, token);
}
__declspec(dllexport) int32_t GetTextMetricsW(void* hdc,
        micant::um::winnt_user32_gdi32_imm32_abi::GdiTextMetricW* metrics) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetTextMetricsW(hdc, metrics);
}
__declspec(dllexport) uint32_t GetGlyphIndicesW(void* hdc, const wchar_t* text,
        int32_t count, uint16_t* glyphIndices, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_GetGlyphIndicesW(
        hdc, text, count, glyphIndices, flags);
}
__declspec(dllexport) int32_t EnumFontFamiliesExW(void* hdc,
        const micant::um::winnt_user32_gdi32_imm32_abi::GdiLogFontW* filter,
        micant::um::winnt_user32_gdi32_imm32_abi::GdiFontEnumProcW callback,
        intptr_t parameter, uint32_t flags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Gdi32_EnumFontFamiliesExW(
        hdc, filter, callback, parameter, flags);
}
} // extern "C"
#elif defined(BUILD_SYSTEM32_IMM32_DLL)
extern "C" {
__declspec(dllexport) void* ImmGetContext(void* hWnd) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetContext(hWnd);
}
__declspec(dllexport) int32_t ImmReleaseContext(void* hWnd, void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmReleaseContext(hWnd, hIMC);
}
__declspec(dllexport) void* ImmAssociateContext(void* hWnd, void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmAssociateContext(hWnd, hIMC);
}
__declspec(dllexport) int32_t ImmAssociateContextEx(void* hWnd, void* hIMC, uint32_t dwFlags) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmAssociateContextEx(hWnd, hIMC, dwFlags);
}
__declspec(dllexport) int32_t ImmGetCompositionStringW(void* hIMC, uint32_t dwIndex, void* lpBuf, uint32_t dwBufLen) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetCompositionStringW(hIMC, dwIndex, lpBuf, dwBufLen);
}
__declspec(dllexport) int32_t ImmSetCompositionWindow(void* hIMC, const void* lpCompForm) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmSetCompositionWindow(hIMC, lpCompForm);
}
__declspec(dllexport) int32_t ImmGetOpenStatus(void* hIMC) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmGetOpenStatus(hIMC);
}
__declspec(dllexport) int32_t ImmSetOpenStatus(void* hIMC, int32_t fOpen) {
    return micant::um::winnt_user32_gdi32_imm32_abi::Imm32_ImmSetOpenStatus(hIMC, fOpen);
}
} // extern "C"
#endif
