/**
 * @file theme_test_app.cpp
 * @brief Ring 3 consumer for WhiteSur, UxTheme and Win32 control verification.
 *
 * This application deliberately uses the production Win32 and common-control
 * classes. It is a test client, not an alternate control renderer.
 */

#define WIN32_LEAN_AND_MEAN
#include "micant/shared/ntdef.hpp"
#include <windows.h>
#include "micant/shared/native_uxtheme_api.h"
#include "micant/um/subsystems/theme/window_decoration_assets.hpp"
#define MICANT_WIN32K_IMPORTS
#include "micant/shared/win32k_client.h"
#include "gui_common.hpp"

extern "C" __declspec(dllimport) int32_t InitCommonControlsEx(const void*) noexcept;

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(value);
    return destination;
}
extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* output = static_cast<uint8_t*>(destination);
    const auto* input = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < count; ++i) output[i] = input[i];
    return destination;
}
extern "C" int memcmp(const void* left, const void* right, size_t count) {
    const auto* a = static_cast<const uint8_t*>(left);
    const auto* b = static_cast<const uint8_t*>(right);
    for (size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return static_cast<int>(a[i]) - static_cast<int>(b[i]);
    return 0;
}

namespace {
constexpr wchar_t kClassName[] = L"MicaNTThemeTestWindow";
constexpr wchar_t kStressClassName[] = L"MicaNTWindowGrowthProbe";
constexpr UINT kLightId = 1010;
constexpr UINT kDarkId = 1011;
constexpr UINT kBaseId = 1015;
constexpr UINT kCheckId = 1020;
constexpr UINT kThreeStateId = 1021;
constexpr UINT kDisabledCheckId = 1022;
constexpr UINT kRadioOneId = 1023;
constexpr UINT kRadioTwoId = 1024;
constexpr UINT kThemeQueueProbeMessage = 0x8002u;
constexpr UINT kBmSetCheck = 0x00F1u;
constexpr UINT kPbmSetPos = 0x0402u;
constexpr UINT kPbmSetRange32 = 0x0406u;
constexpr UINT kTcmInsertItemW = 0x133Eu;
constexpr UINT kTcmSetCurSel = 0x130Cu;
constexpr UINT kSbmSetRange = 0x00E2u;
constexpr UINT kSbmSetPos = 0x00E0u;
constexpr UINT kTvmInsertItemW = 0x1132u;
constexpr UINT kTvmExpand = 0x1102u;
constexpr UINT kTvmGetCount = 0x1105u;
constexpr UINT kTvmSelectItem = 0x110Bu;
constexpr intptr_t kTvRoot = -0x10000;
constexpr intptr_t kTvLast = -0x0FFFE;
constexpr UINT kTvifText = 0x0001u;
constexpr UINT kTvSelect = 0x0009u;
constexpr UINT kTvExpandFlag = 0x0002u;
HWND g_window{};
HWND g_focusEdit{};
MicaThemeHandle g_initialThemeHandle{};
uint32_t g_initialThemeColor{};
bool g_themeLifetimeVerified{};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

bool exerciseWindowTableGrowth(HINSTANCE instance) {
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = instance;
    klass.lpszClassName = kStressClassName;
    if (!RegisterClassExW(&klass)) return false;

    HWND windows[24]{};
    size_t created = 0;
    for (; created < 24; ++created) {
        windows[created] = CreateWindowExW(0, kStressClassName, L"Window growth probe",
            WS_OVERLAPPEDWINDOW,
            12 + static_cast<int>(created % 6) * 120,
            12 + static_cast<int>(created / 6) * 100, 96, 72,
            nullptr, nullptr, instance, nullptr);
        if (!windows[created]) break;
        (void)ShowWindow(windows[created], SW_SHOW);
    }
    bool resized = created == 24;
    for (size_t pass = 0; pass < 3; ++pass) {
        for (size_t index = 0; index < created; ++index) {
            const int x = 24 + static_cast<int>(index % 6) * 120;
            const int y = 24 + static_cast<int>(index / 6) * 100;
            const int width = pass == 1 ? 112 : (index % 2 ? 80 : 112);
            const int height = pass == 1 ? 84 : (index % 2 ? 64 : 84);
            if (!SetWindowPos(windows[index], nullptr, x, y, width, height, 0))
                resized = false;
        }
    }
    for (size_t index = created; index > 0; --index)
        if (!DestroyWindow(windows[index - 1])) resized = false;
    constexpr int probeX[3]{16, 48, 760};
    constexpr int probeY[3]{16, 40, 560};
    constexpr int probeWidth[3]{1, 700, 96};
    constexpr int probeHeight[3]{1, 500, 72};
    HWND geometryProbes[3]{};
    size_t geometryCreated = 0;
    for (; geometryCreated < 3; ++geometryCreated) {
        geometryProbes[geometryCreated] = CreateWindowExW(0, kStressClassName,
            L"Window geometry probe", WS_OVERLAPPEDWINDOW,
            probeX[geometryCreated], probeY[geometryCreated],
            probeWidth[geometryCreated], probeHeight[geometryCreated],
            nullptr, nullptr, instance, nullptr);
        if (!geometryProbes[geometryCreated]) break;
        (void)ShowWindow(geometryProbes[geometryCreated], SW_SHOW);
        if (!SetWindowPos(geometryProbes[geometryCreated], nullptr,
                probeX[geometryCreated], probeY[geometryCreated],
                geometryCreated == 0 ? 40 : 112,
                geometryCreated == 0 ? 32 : 84, 0) ||
            !SetWindowPos(geometryProbes[geometryCreated], nullptr,
                probeX[geometryCreated], probeY[geometryCreated],
                probeWidth[geometryCreated], probeHeight[geometryCreated], 0))
            resized = false;
    }
    if (geometryCreated != 3) resized = false;
    for (size_t index = geometryCreated; index > 0; --index)
        if (!DestroyWindow(geometryProbes[index - 1])) resized = false;
    if (resized) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Dynamic window table: 24 windows created, resized, and destroyed");
        return true;
    }
    micant_gui::PrintLine("[MicaNT ThemeTest] Dynamic window table stress failed");
    return false;
}
MicaThemeSnapshot g_activeSnapshot{};
MicaThemeSnapshot g_candidateSnapshot{};
MicaThemeSnapshot g_rollbackSnapshot{};
MicaThemeSnapshot g_observedSnapshot{};
MicaThemeCaptionImageSet g_activeCaptionImages{};
MicaThemeCaptionImageSet g_candidateCaptionImages{};
MicaThemeCaptionImageSet g_rollbackCaptionImages{};
bool g_hasSnapshot{};

struct InitCommonControlsExArgs { DWORD size, classes; };
struct TabItemW {
    UINT mask, state, stateMask;
    wchar_t* text;
    int32_t textCapacity, image;
    LPARAM parameter;
};
struct TreeItemW {
    UINT mask;
    void* item;
    UINT state, stateMask;
    wchar_t* text;
    int32_t textCapacity, image, selectedImage, children;
    LPARAM parameter;
};
struct TreeInsertW { void* parent; void* insertAfter; TreeItemW item; };

void createTreeControl(HWND parent, DWORD child) noexcept {
    HWND tree = CreateWindowExW(0, L"SysTreeView32", L"", child,
        462, 202, 244, 176, parent, reinterpret_cast<HMENU>(1080), nullptr, nullptr);
    if (!tree) return;
    const auto insert = [&](void* parentItem, wchar_t* label) noexcept -> LPARAM {
        TreeInsertW entry{};
        entry.parent = parentItem;
        entry.insertAfter = reinterpret_cast<void*>(kTvLast);
        entry.item.mask = kTvifText;
        entry.item.text = label;
        entry.item.textCapacity = 96;
        return SendMessageW(tree, kTvmInsertItemW, 0,
                            reinterpret_cast<LPARAM>(&entry));
    };
    wchar_t rootText[] = L"WhiteSur";
    wchar_t lightText[] = L"Light";
    wchar_t darkText[] = L"Dark";
    wchar_t baseText[] = L"Base fallback";
    const LPARAM root = insert(reinterpret_cast<void*>(kTvRoot), rootText);
    const LPARAM light = root ? insert(reinterpret_cast<void*>(root), lightText) : 0;
    const LPARAM dark = root ? insert(reinterpret_cast<void*>(root), darkText) : 0;
    const LPARAM base = insert(reinterpret_cast<void*>(kTvRoot), baseText);
    if (root) (void)SendMessageW(tree, kTvmExpand, static_cast<WPARAM>(root), kTvExpandFlag);
    if (light) (void)SendMessageW(tree, kTvmSelectItem, kTvSelect, light);
    const bool populated = root && light && dark && base &&
        SendMessageW(tree, kTvmGetCount, 0, 0) == 4;
    micant_gui::PrintLine(populated
        ? "[MicaNT ThemeTest] Production TreeView created with expanded hierarchy"
        : "[MicaNT ThemeTest] Production TreeView initialization failed");
    (void)UpdateWindow(tree);
}

void createControlMatrix(HWND parent) noexcept {
    const DWORD child = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    HWND push = CreateWindowExW(0, L"BUTTON", L"Push button", child,
        18, 20, 142, 30, parent, reinterpret_cast<HMENU>(1030), nullptr, nullptr);
    HWND defaultButton = CreateWindowExW(0, L"BUTTON", L"Default button", child | 0x00000001u,
        170, 20, 142, 30, parent, reinterpret_cast<HMENU>(1031), nullptr, nullptr);
    HWND disabledButton = CreateWindowExW(0, L"BUTTON", L"Disabled button", child | WS_DISABLED,
        322, 20, 142, 30, parent, reinterpret_cast<HMENU>(1032), nullptr, nullptr);
    if (push) (void)UpdateWindow(push);
    if (defaultButton) (void)UpdateWindow(defaultButton);
    if (disabledButton) (void)UpdateWindow(disabledButton);

    HWND check = CreateWindowExW(0, L"BUTTON", L"Checked checkbox",
        child | 0x00000003u, 18, 66, 176, 28, parent,
        reinterpret_cast<HMENU>(kCheckId), nullptr, nullptr);
    HWND threeState = CreateWindowExW(0, L"BUTTON", L"Mixed checkbox",
        child | 0x00000006u, 202, 66, 176, 28, parent,
        reinterpret_cast<HMENU>(kThreeStateId), nullptr, nullptr);
    HWND disabledCheck = CreateWindowExW(0, L"BUTTON", L"Disabled checkbox",
        child | WS_DISABLED | 0x00000003u, 386, 66, 176, 28, parent,
        reinterpret_cast<HMENU>(kDisabledCheckId), nullptr, nullptr);
    if (check) (void)SendMessageW(check, kBmSetCheck, 1, 0);
    if (threeState) (void)SendMessageW(threeState, kBmSetCheck, 2, 0);
    if (check) (void)UpdateWindow(check);
    if (threeState) (void)UpdateWindow(threeState);
    if (disabledCheck) (void)UpdateWindow(disabledCheck);

    HWND radioOne = CreateWindowExW(0, L"BUTTON", L"Selected radio",
        child | 0x00000009u, 18, 108, 170, 28, parent,
        reinterpret_cast<HMENU>(kRadioOneId), nullptr, nullptr);
    HWND radioTwo = CreateWindowExW(0, L"BUTTON", L"Unselected radio",
        child | 0x00000009u, 202, 108, 180, 28, parent,
        reinterpret_cast<HMENU>(kRadioTwoId), nullptr, nullptr);
    if (radioOne) (void)SendMessageW(radioOne, kBmSetCheck, 1, 0);
    if (radioTwo) (void)SendMessageW(radioTwo, kBmSetCheck, 0, 0);
    if (radioOne) (void)UpdateWindow(radioOne);
    if (radioTwo) (void)UpdateWindow(radioTwo);

    g_focusEdit = CreateWindowExW(0, L"EDIT", L"Text entry with focus",
        child, 18, 154, 250, 30, parent,
        reinterpret_cast<HMENU>(1040), nullptr, nullptr);
    HWND password = CreateWindowExW(0, L"EDIT", L"WhiteSur",
        child | 0x00000020u, 280, 154, 180, 30, parent,
        reinterpret_cast<HMENU>(1041), nullptr, nullptr);
    HWND disabledEdit = CreateWindowExW(0, L"EDIT", L"Disabled text",
        child | WS_DISABLED, 472, 154, 180, 30, parent,
        reinterpret_cast<HMENU>(1042), nullptr, nullptr);
    if (g_focusEdit) (void)UpdateWindow(g_focusEdit);
    if (password) (void)UpdateWindow(password);
    if (disabledEdit) (void)UpdateWindow(disabledEdit);

    HWND tabs = CreateWindowExW(0, L"SysTabControl32", L"",
        child, 18, 202, 426, 176, parent,
        reinterpret_cast<HMENU>(1050), nullptr, nullptr);
    if (tabs) {
        wchar_t first[] = L"General";
        wchar_t second[] = L"Appearance";
        wchar_t third[] = L"Advanced";
        TabItemW item{};
        item.mask = 1u;
        item.text = first;
        (void)SendMessageW(tabs, kTcmInsertItemW, 0,
                           reinterpret_cast<LPARAM>(&item));
        item.text = second;
        (void)SendMessageW(tabs, kTcmInsertItemW, 1,
                           reinterpret_cast<LPARAM>(&item));
        item.text = third;
        (void)SendMessageW(tabs, kTcmInsertItemW, 2,
                           reinterpret_cast<LPARAM>(&item));
        (void)SendMessageW(tabs, kTcmSetCurSel, 1, 0);
        (void)UpdateWindow(tabs);
    }
    createTreeControl(parent, child);

    HWND progress = CreateWindowExW(0, L"msctls_progress32", L"",
        child, 18, 394, 430, 28, parent,
        reinterpret_cast<HMENU>(1060), nullptr, nullptr);
    if (progress) {
        (void)SendMessageW(progress, kPbmSetRange32, 0, 100);
        (void)SendMessageW(progress, kPbmSetPos, 64, 0);
        (void)UpdateWindow(progress);
    }
    HWND horizontal = CreateWindowExW(0, L"SCROLLBAR", L"",
        child, 18, 440, 620, 24, parent,
        reinterpret_cast<HMENU>(1070), nullptr, nullptr);
    if (horizontal) {
        (void)SendMessageW(horizontal, kSbmSetRange, 0, 100);
        (void)SendMessageW(horizontal, kSbmSetPos, 0, 35);
        (void)UpdateWindow(horizontal);
    }
    HWND vertical = CreateWindowExW(0, L"SCROLLBAR", L"",
        child | 0x00000001u, 654, 202, 24, 176, parent,
        reinterpret_cast<HMENU>(1071), nullptr, nullptr);
    if (vertical) {
        (void)SendMessageW(vertical, kSbmSetRange, 0, 100);
        (void)SendMessageW(vertical, kSbmSetPos, 0, 35);
        (void)UpdateWindow(vertical);
    }
    micant_gui::PrintLine("[MicaNT ThemeTest] Production common-control matrix created");
}

void createThemeControls(HWND window) noexcept {
    HWND light = CreateWindowExW(0, L"BUTTON", L"WhiteSur Light", WS_CHILD | WS_VISIBLE,
        690, 18, 146, 28, window, reinterpret_cast<HMENU>(kLightId), nullptr, nullptr);
    HWND dark = CreateWindowExW(0, L"BUTTON", L"WhiteSur Dark", WS_CHILD | WS_VISIBLE,
        690, 52, 146, 28, window, reinterpret_cast<HMENU>(kDarkId), nullptr, nullptr);
    HWND base = CreateWindowExW(0, L"BUTTON", L"Base fallback", WS_CHILD | WS_VISIBLE,
        690, 86, 146, 28, window, reinterpret_cast<HMENU>(kBaseId), nullptr, nullptr);
    if (light) (void)UpdateWindow(light);
    if (dark) (void)UpdateWindow(dark);
    if (base) (void)UpdateWindow(base);
    createControlMatrix(window);
    (void)InvalidateRect(window, nullptr, TRUE);
    (void)UpdateWindow(window);
}

bool appendSystemRoot(wchar_t* path, size_t capacity, size_t& cursor) noexcept {
    const auto* shared = reinterpret_cast<const micant::KUserSharedData*>(
        micant::UserSharedDataAddress);
    size_t length = 0;
    while (length < std::size(shared->ntSystemRoot) && shared->ntSystemRoot[length]) ++length;
    if (length < 3 || length >= capacity || shared->ntSystemRoot[1] != u':' ||
        shared->ntSystemRoot[2] != u'\\') return false;
    for (; cursor < length; ++cursor)
        path[cursor] = static_cast<wchar_t>(shared->ntSystemRoot[cursor]);
    path[cursor] = L'\0';
    return true;
}

bool appendWide(wchar_t* path, size_t capacity, size_t& cursor,
                const wchar_t* suffix, size_t length) noexcept {
    if (length > capacity || cursor > capacity - length - 1) return false;
    for (size_t i = 0; i < length; ++i) path[cursor++] = suffix[i];
    path[cursor] = L'\0';
    return true;
}

[[gnu::noinline]] bool readSnapshot(const wchar_t* profile, MicaThemeSnapshot& snapshot,
                  MicaThemeCaptionImageSet& captionImages) noexcept {
    wchar_t path[512]{};
    size_t cursor = 0;
    if (!appendSystemRoot(path, std::size(path), cursor) ||
        !appendWide(path, std::size(path), cursor, L"\\System\\Themes\\", 15)) return false;
    for (size_t i = 0; profile[i]; ++i) {
        if (cursor + 1 >= std::size(path)) return false;
        path[cursor++] = profile[i] == L'/' ? L'\\' : profile[i];
    }
    if (!appendWide(path, std::size(path), cursor, L"\\native.style", 13)) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    bool ok = GetFileSizeEx(file, &length) &&
        length.QuadPart == static_cast<LONGLONG>(sizeof(snapshot));
    DWORD amount = 0;
    if (ok) ok = ReadFile(file, &snapshot, sizeof(snapshot), &amount, nullptr) &&
        amount == sizeof(snapshot);
    CloseHandle(file);
    if (!ok || !MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot)) || cursor < 13)
        return false;
    cursor -= 13;
    path[cursor] = L'\0';
    bool dark = false;
    for (size_t index = 0; profile[index]; ++index)
        if (profile[index] == L'D' && profile[index + 1] == L'a' &&
            profile[index + 2] == L'r' && profile[index + 3] == L'k') dark = true;
    micant::um::theme::LoadWhiteSurCaptionImages(captionImages, path, dark);
    return MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot));
}

bool publishSnapshot(const MicaThemeSnapshot& candidate,
                     const MicaThemeCaptionImageSet* captionImages = nullptr) noexcept {
    if (!MicaThemeSnapshotIsValid(&candidate, sizeof(candidate)) ||
        !MicaWin32kSetThemeSnapshot(&candidate, captionImages)) return false;
    if (!MicaWin32kGetThemeSnapshot(&g_observedSnapshot) ||
        __builtin_memcmp(&candidate, &g_observedSnapshot, sizeof(candidate)) != 0) {
        if (g_hasSnapshot)
            (void)MicaWin32kSetThemeSnapshot(&g_activeSnapshot, &g_activeCaptionImages);
        return false;
    }
    g_activeSnapshot = candidate;
    g_activeCaptionImages = captionImages
        ? *captionImages : MicaThemeCaptionImageSet{};
    g_hasSnapshot = true;
    return true;
}

[[gnu::noinline]] bool selectTheme(const wchar_t* profile, const char* label) noexcept {
    MicaThemeSnapshot& candidate = g_candidateSnapshot;
    if (!readSnapshot(profile, candidate, g_candidateCaptionImages) ||
        !publishSnapshot(candidate, &g_candidateCaptionImages)) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Theme load rejected; prior snapshot retained");
        if (g_hasSnapshot && MicaWin32kGetThemeSnapshot(&g_observedSnapshot) &&
            __builtin_memcmp(&g_activeSnapshot, &g_observedSnapshot,
                             sizeof(g_observedSnapshot)) == 0) {
            micant_gui::PrintLine("[MicaNT ThemeTest] Rejected theme preserved the active snapshot");
            InvalidateRect(g_window, nullptr, TRUE);
            UpdateWindow(g_window);
        }
        return false;
    }
    if (g_window) {
        InvalidateRect(g_window, nullptr, TRUE);
        UpdateWindow(g_window);
    }
    micant_gui::PrintLine(label);
    if (g_initialThemeHandle && !g_themeLifetimeVerified) {
        uint32_t oldHandleColor{}, newHandleColor{};
        MicaThemeHandle current = OpenThemeData(g_window, L"BUTTON");
        if (current &&
            GetThemeColor(g_initialThemeHandle, 1, 1, 3802, &oldHandleColor) == 0 &&
            GetThemeColor(current, 1, 1, 3802, &newHandleColor) == 0 &&
            oldHandleColor == g_initialThemeColor && newHandleColor != oldHandleColor) {
            g_themeLifetimeVerified = true;
            micant_gui::PrintLine("[MicaNT ThemeTest] Open HTHEME retained old snapshot; new HTHEME sees new theme");
        } else {
            micant_gui::PrintLine("[MicaNT ThemeTest] HTHEME snapshot lifetime check failed");
        }
        if (current) (void)CloseThemeData(current);
    }
    return true;
}

[[gnu::noinline]] bool selectBaseTheme() noexcept {
    MicaThemeSnapshot& base = g_candidateSnapshot;
    base = {};
    base.magic = MICANT_THEME_SNAPSHOT_MAGIC;
    base.version = MICANT_THEME_SNAPSHOT_VERSION;
    base.headerSize = static_cast<uint16_t>(offsetof(MicaThemeSnapshot, styles));
    base.totalSize = sizeof(base);
    base.controlCount = MICANT_THEME_CONTROL_COUNT;
    base.stateCount = MICANT_THEME_STATE_COUNT;
    if (publishSnapshot(base)) {
        if (g_window) {
            InvalidateRect(g_window, nullptr, TRUE);
            UpdateWindow(g_window);
        }
        micant_gui::PrintLine("[MicaNT ThemeTest] Base snapshot published");
        return true;
    }
    micant_gui::PrintLine("[MicaNT ThemeTest] Base snapshot was not published");
    return false;
}

[[gnu::noinline]] bool loadActiveTheme() noexcept {
    micant_gui::PrintLine("[MicaNT ThemeTest] Reading active.profile");
    wchar_t path[512]{};
    size_t cursor = 0;
    if (!appendSystemRoot(path, std::size(path), cursor) ||
        !appendWide(path, std::size(path), cursor,
                    L"\\System\\Themes\\active.profile", 29)) {
        micant_gui::PrintLine("[MicaNT ThemeTest] System root lookup unavailable");
        return false;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Active profile unavailable; Base remains active");
        return false;
    }
    char profile[96]{};
    LARGE_INTEGER length{};
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart > 0 &&
        length.QuadPart < static_cast<LONGLONG>(sizeof(profile));
    DWORD amount = 0;
    while (ok && amount < static_cast<DWORD>(length.QuadPart)) {
        DWORD received = 0;
        ok = ReadFile(file, profile + amount,
            static_cast<DWORD>(length.QuadPart) - amount, &received, nullptr) && received > 0;
        amount += received;
    }
    CloseHandle(file);
    if (!ok) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Active profile was not read");
        return false;
    }
    while (amount && (profile[amount - 1] == '\r' || profile[amount - 1] == '\n' ||
                      profile[amount - 1] == ' ' || profile[amount - 1] == '\t')) --amount;
    if (amount == 14 && __builtin_memcmp(profile, "WhiteSur/Light", 14) == 0)
        return selectTheme(L"WhiteSur/Light", "[MicaNT ThemeTest] Active WhiteSur Light loaded");
    else if (amount == 13 && __builtin_memcmp(profile, "WhiteSur/Dark", 13) == 0)
        return selectTheme(L"WhiteSur/Dark", "[MicaNT ThemeTest] Active WhiteSur Dark loaded");
    else {
        micant_gui::PrintLine("[MicaNT ThemeTest] Active profile unsupported; Base remains active");
        return false;
    }
}

[[gnu::noinline]] bool verifyThemeQueueRollback() noexcept {
    if (!g_window || !g_hasSnapshot) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Theme notification queue probe unavailable");
        return false;
    }
    g_rollbackSnapshot = g_activeSnapshot;
    MicaThemeSnapshot& candidate = g_candidateSnapshot;
    if (!readSnapshot(L"WhiteSur/Dark", candidate, g_candidateCaptionImages)) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Theme notification queue probe could not read Dark");
        return false;
    }
    size_t queued = 0;
    while (queued < 512 && PostMessageW(g_window, kThemeQueueProbeMessage, 0, 0)) ++queued;
    g_rollbackCaptionImages = g_activeCaptionImages;
    const bool rejected = queued != 0 && !MicaWin32kSetThemeSnapshot(
        &candidate, &g_candidateCaptionImages);
    const bool preserved = rejected && MicaWin32kGetThemeSnapshot(&g_observedSnapshot) &&
        __builtin_memcmp(&g_rollbackSnapshot, &g_observedSnapshot,
                         sizeof(g_rollbackSnapshot)) == 0;
    MSG message{};
    size_t drained = 0;
    while (drained <= queued && PeekMessageW(&message, g_window,
            kThemeQueueProbeMessage, kThemeQueueProbeMessage, PM_REMOVE)) ++drained;
    const bool queueDrained = queued == drained;
    const bool recovered = queueDrained &&
        publishSnapshot(candidate, &g_candidateCaptionImages) &&
        publishSnapshot(g_rollbackSnapshot, &g_rollbackCaptionImages);
    if (preserved && queueDrained && recovered) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Full notification queue rejected theme; active snapshot intact");
        micant_gui::PrintLine("[MicaNT ThemeTest] Valid theme publish recovered after queue drain");
        InvalidateRect(g_window, nullptr, TRUE);
        UpdateWindow(g_window);
        return true;
    } else {
        // Keep the last known good theme if queue setup or recovery fails.
        (void)MicaWin32kSetThemeSnapshot(&g_rollbackSnapshot, &g_rollbackCaptionImages);
        micant_gui::PrintLine("[MicaNT ThemeTest] Theme notification queue rollback probe failed");
        return false;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case kLightId:
                selectTheme(L"WhiteSur/Light", "[MicaNT ThemeTest] WhiteSur Light snapshot published");
                return 0;
            case kDarkId:
                selectTheme(L"WhiteSur/Dark", "[MicaNT ThemeTest] WhiteSur Dark snapshot published");
                return 0;
            case kBaseId:
                (void)selectBaseTheme();
                return 0;
            }
        }
        return 0;
    case WM_HSCROLL:
        return 0;
    case 0x031Au: // WM_THEMECHANGED
        InvalidateRect(window, nullptr, TRUE);
        UpdateWindow(window);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

extern "C" void _start() noexcept {
    InitCommonControlsExArgs commonControls{sizeof(commonControls), 0x0000002Au};
    if (!InitCommonControlsEx(&commonControls))
        micant_gui::PrintLine("[MicaNT ThemeTest] Common control class initialization failed");
    else
        micant_gui::PrintLine("[MicaNT ThemeTest] Progress, tab, and tree classes initialized");
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = kClassName;
    if (!RegisterClassExW(&klass)) {
        return;
    }
    g_window = CreateWindowExW(0, kClassName, L"MicaNT Theme Test",
        WS_OVERLAPPEDWINDOW, 72, 56, 860, 570, nullptr, nullptr,
        klass.hInstance, nullptr);
    if (!g_window) {
        return;
    }
    HMENU menu = CreateMenu();
    if (menu) {
        AppendMenuW(menu, 0, 1, L"Theme test");
        SetMenu(g_window, menu);
    }
    MicaThemeHandle theme = OpenThemeData(g_window, L"BUTTON");
    if (theme) (void)CloseThemeData(theme);
    // Establish the public Base snapshot first so a missing, unreadable, or
    // invalid active profile has a real fallback to preserve.
    if (!selectBaseTheme()) {
        return;
    }
    (void)loadActiveTheme();
    g_initialThemeHandle = OpenThemeData(g_window, L"BUTTON");
    if (!g_initialThemeHandle ||
        GetThemeColor(g_initialThemeHandle, 1, 1, 3802, &g_initialThemeColor) != 0) {
        micant_gui::PrintLine("[MicaNT ThemeTest] Could not pin the initial HTHEME snapshot");
        return;
    }
    micant_gui::PrintLine("[MicaNT ThemeTest] Initial HTHEME snapshot pinned");
    (void)ShowWindow(g_window, SW_SHOW);
    createThemeControls(g_window);
    if (g_focusEdit) (void)SetFocus(g_focusEdit);
    UpdateWindow(g_window);
    micant_gui::PrintLine("[MicaNT ThemeTest] Production control window started");
    micant_gui::PrintLine("[MicaNT ThemeTest] Dynamic window table stress starting");
    (void)exerciseWindowTableGrowth(klass.hInstance);
    micant_gui::PrintLine("[MicaNT ThemeTest] Dynamic window table stress returned");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
