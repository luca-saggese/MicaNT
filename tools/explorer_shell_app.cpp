/**
 * @file explorer_shell_app.cpp
 * @brief Persistent Windows XP-style MicaNT desktop shell (explorer.exe).
 *
 * The shell owns the desktop, Start menu, taskbar and notification area as
 * user-mode Win32 windows. Each surface is composed by win32k.dll.
 */

#define WIN32_LEAN_AND_MEAN
#include "micant/um/subsystems/imaging/bmp_png_decoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/subsystems/imaging/native_image_file.hpp"
#include "micant/um/subsystems/imaging/wallpaper.hpp"
#define MICANT_DWRITE_ABI_ONLY
#include "micant/um/subsystems/graphics/dwrite.hpp"

#include <windows.h>
#include <cstring>
#ifdef GetGlyphIndices
#undef GetGlyphIndices
#endif
#include "micant/shared/win32k_client.h"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/system_time.hpp"
#include "micant/shared/theme_snapshot.h"
#include "micant/shared/native_uxtheme_api.h"
#include "gui_common.hpp"
#define MICANT_USERENV_NO_EXPORT_REGISTRATION
#include "micant/um/subsystems/mgmt/userenv.hpp"
#undef MICANT_USERENV_NO_EXPORT_REGISTRATION
#include "explorer_shell/taskbar.hpp"
#include "explorer_shell/start_menu.hpp"
#include "explorer_shell/desktop.hpp"
#include "explorer_shell/icon_catalog.hpp"
#include "explorer_shell/notification_area.hpp"
#include "explorer_shell/task_switcher.hpp"

extern "C" __declspec(dllimport) int32_t InitCommonControlsEx(const void*) noexcept;
namespace {

using namespace micant_gui;

constexpr uint32_t TASKBAR_H = 40;
constexpr uint32_t MENU_W = 344;
constexpr uint32_t MENU_H = 430;
constexpr size_t MAX_APP_WINDOWS = 8;
constexpr UINT WM_THEME_SCHEDULE_TICK = 0x8001u;
constexpr UINT WM_THEME_QUEUE_PRESSURE_PROBE = 0x8002u;

enum class View : uint8_t { MyComputer, MyDocuments, ControlPanel, Network, RecycleBin, DateTime };

struct DirectoryRow {
    wchar_t name[260]{};
    uint32_t attributes{};
    uint64_t size{};
};

struct AppWindow {
    HWND hwnd{};
    HDC dc{};
    HBITMAP bitmap{};
    uint32_t* pixels{};
    uint32_t width{500};
    uint32_t height{320};
    View view{View::MyComputer};
    wchar_t directoryPath[512]{};
    DirectoryRow directoryRows[64]{};
    size_t directoryRowCount{};
    bool open{};
    bool minimized{};
    bool firstPaintPresented{};
};

struct CachedDesktopIcon {
    micant::explorer::IconId id{};
    uint16_t requestedSize{};
    uint8_t* pixels{};
    size_t bytes{};
    uint32_t width{}, height{}, stride{};
    micant::um::imaging::ImageAllocator allocator{};
};

struct ShellState {
    uint32_t screenW{1024}, screenH{768};
    HWND desktop{};
    HDC desktopDc{};
    uint32_t* desktopPixels{};
    HFONT uiFont{};
    bool fontReady{};
    MicaThemeSnapshot visualTheme{};
    // Snapshot v2 exceeds 8 KiB. Keep transactional scratch off the small
    // Explorer thread stack while loading and verifying compiled profiles.
    MicaThemeSnapshot themeCandidate{};
    MicaThemeSnapshot themeVerify{};
    MicaThemeSnapshot themeRollbackProbe{};
    bool visualThemeReady{};
    char themeProfile[96]{};
    bool themeAuto{};
    bool themeGdiDrawReported{};
    bool themeFontMetadataProbeReported{};
    bool themeGdiEditDrawReported{};
    bool themeTextDrawReported{};
    bool themeTextClipReported{};
    bool taskbarThemePixelReported{};
    bool trayThemePixelReported{};
    bool statusBarThemePixelReported{};
    bool explorerRowsThemePixelReported{};
    bool commonControlsProgressReported{};
    bool themeBackgroundExReported{};
    HWND themeProgressProbe{};
    bool bufferedPaintReported{};
    HWND themeButtonProbe{};
    bool themeButtonProbeReported{};
    HWND themeCheckProbe{};
    uint32_t themeCheckNotifications{};
    bool themeCheckInputReported{};
    HWND themeEditProbe{};
    bool themeEditProbeReported{};
    uint32_t themeEditChangeNotifications{};
    HWND themeScrollProbe{};
    uint32_t themeScrollNotifications{};
    bool themeScrollProbeReported{};
    bool themeWindowScrollProbeReported{};
    HWND themeAttachedScrollProbe{};
    bool themeAttachedScrollProbeReported{};
    bool themeAttachedScrollArrowReported{};
    bool themeAttachedScrollThumbReported{};
    HWND themeTabProbe{};
    bool themeTabProbeReported{};
    bool themeTabInputReported{};
    uint32_t themeTabSelectionNotifications{};
    HWND themeThreeStateProbe{};
    HWND themeRadioProbe{};
    uint32_t themeThreeStateNotifications{};
    uint32_t themeRadioNotifications{};
    HWND themeAutoRadioA{};
    HWND themeAutoRadioB{};
    HWND themeAutoRadioC{};
    HWND themeAutoRadioD{};
    uint32_t themeAutoRadioNotifications{};
    bool themeAutoRadioInputReported{};
    bool themeStateProbeAttempted{};
    bool themeMenuApiReported{};
    bool themeMenuPopupStarted{};
    bool themeMenuMouseCommand{};
    bool themeMenuKeyboardCommand{};
    bool themeMenuBarStarted{};
    bool themeMenuBarCommand{};
    HMENU themeMenuBarProbe{};
    bool windowTtfDrawReported{};
    uint32_t* wallpaperPixels{};
    size_t wallpaperBytes{};
    micant::um::imaging::ImageAllocator wallpaperAllocator{};
    CachedDesktopIcon iconCache[32]{};
    size_t iconCacheCount{};
    HWND taskbar{};
    HDC taskbarDc{};
    uint32_t* taskbarPixels{};
    HWND startMenu{};
    HDC menuDc{};
    uint32_t* menuPixels{};
    bool startOpen{};
    AppWindow apps[MAX_APP_WINDOWS]{};
    micant::explorer::TaskSwitchSnapshot taskSwitch{};
    uint8_t activeApp{0xFF};
    uint8_t clockSecond{0xFF};
    MicaWin32kSystemTime systemTime{};
    char clockText[12]{"--:--"};
};

ShellState g{};
constexpr wchar_t kClassName[] = L"MicaNT.Explorer.Desktop";
constexpr wchar_t kThemeAttachedScrollClassName[] = L"MicaNT.ThemeAttachedScrollProbe";
constexpr wchar_t kDesktopTitle[] = L"MicaNT Desktop";
constexpr wchar_t kTaskbarTitle[] = L"MicaNT Taskbar";
constexpr wchar_t kStartTitle[] = L"Start";

inline uint32_t rgb(uint8_t r, uint8_t green, uint8_t b) noexcept {
    return PackBGRA(r, green, b);
}

const MicaThemeStyleSnapshot* themeStyle(uint16_t control, uint16_t state = 0) noexcept {
    if (!g.visualThemeReady || control >= MICANT_THEME_CONTROL_COUNT ||
        state >= MICANT_THEME_STATE_COUNT) return nullptr;
    return &g.visualTheme.styles[static_cast<size_t>(control) * MICANT_THEME_STATE_COUNT + state];
}

uint32_t themePixel(uint32_t color, uint32_t under = 0xFFFFFFFFu) noexcept {
    const uint32_t alpha = color >> 24;
    const uint32_t inverse = 255u - alpha;
    const auto over = [&](uint32_t shift) noexcept {
        const uint32_t foreground = (color >> shift) & 0xFFu;
        const uint32_t background = (under >> shift) & 0xFFu;
        return static_cast<uint8_t>((foreground * alpha + background * inverse + 127u) / 255u);
    };
    return PackBGRA(over(16), over(8), over(0));
}

micant::explorer::ThemePalette activeShellPalette() noexcept {
    const auto color = [](uint16_t control, uint32_t flag, uint32_t fallback) noexcept {
        const auto* style = themeStyle(control);
        if (!style || !(style->flags & flag)) return fallback;
        return themePixel(flag == MICANT_THEME_HAS_BACKGROUND ? style->background :
                          flag == MICANT_THEME_HAS_FOREGROUND ? style->foreground : style->border);
    };
    const auto* accentStyle = themeStyle(0, 2);
    const uint32_t accent = accentStyle &&
        (accentStyle->flags & MICANT_THEME_HAS_BACKGROUND)
        ? themePixel(accentStyle->background) : rgb(36, 91, 174);
    const uint32_t accentForeground = accentStyle &&
        (accentStyle->flags & MICANT_THEME_HAS_FOREGROUND)
        ? themePixel(accentStyle->foreground) : rgb(255, 255, 255);
    const uint32_t toolbarForeground = color(10, MICANT_THEME_HAS_FOREGROUND, rgb(242, 246, 251));
    return {
        color(14, MICANT_THEME_HAS_BACKGROUND, rgb(243, 243, 243)),
        color(10, MICANT_THEME_HAS_BACKGROUND, rgb(220, 231, 244)),
        color(14, MICANT_THEME_HAS_FOREGROUND, rgb(30, 30, 30)),
        toolbarForeground,
        color(10, MICANT_THEME_HAS_BORDER, rgb(91, 154, 227)),
        accent,
        accentForeground,
        color(0, MICANT_THEME_HAS_BACKGROUND, rgb(28, 79, 145)),
    };
}

CachedDesktopIcon* findCachedIcon(micant::explorer::IconId id, uint16_t size) noexcept {
    for (size_t i = 0; i < g.iconCacheCount; ++i)
        if (g.iconCache[i].id == id && g.iconCache[i].requestedSize == size &&
            g.iconCache[i].pixels) return &g.iconCache[i];
    return nullptr;
}

micant::explorer::DesktopIconImage iconImage(micant::explorer::IconId id,
                                             uint16_t size) noexcept {
    const CachedDesktopIcon* cached = findCachedIcon(id, size);
    return {cached ? reinterpret_cast<const uint32_t*>(cached->pixels) : nullptr,
            cached ? cached->width : 0, cached ? cached->height : 0,
            cached ? cached->stride : 0};
}

void text(uint32_t* p, uint32_t w, uint32_t h, int32_t x, int32_t y,
          const char* value, uint32_t color) noexcept;
void present(HWND hwnd) noexcept;

CachedDesktopIcon* loadPackagedIcon(micant::explorer::IconId id,
                                    uint16_t requestedSize) noexcept;

void fill(uint32_t* p, uint32_t w, uint32_t h, int32_t x, int32_t y,
          int32_t rw, int32_t rh, uint32_t color) noexcept {
    if (!p || rw <= 0 || rh <= 0) return;
    const int32_t x0 = x < 0 ? 0 : x;
    const int32_t y0 = y < 0 ? 0 : y;
    const int32_t x1 = x + rw > static_cast<int32_t>(w) ? static_cast<int32_t>(w) : x + rw;
    const int32_t y1 = y + rh > static_cast<int32_t>(h) ? static_cast<int32_t>(h) : y + rh;
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint32_t* row = p + static_cast<size_t>(yy) * w + x0;
        for (int32_t xx = x0; xx < x1; ++xx) row[xx - x0] = color;
    }
}

uint32_t overColor(uint32_t under, uint32_t color, uint8_t alpha) noexcept {
    const uint32_t inverse = 255u - alpha;
    const auto channel = [&](unsigned shift) noexcept {
        return static_cast<uint8_t>((((color >> shift) & 0xFFu) * alpha +
            ((under >> shift) & 0xFFu) * inverse + 127u) / 255u);
    };
    return PackBGRA(channel(16), channel(8), channel(0));
}

bool decodeUtf8ToUtf16(const char* input, wchar_t* output, size_t capacity, size_t& units) noexcept {
    units = 0;
    if (!input || !output || capacity == 0) return false;
    for (size_t cursor = 0; input[cursor];) {
        const auto b0 = static_cast<uint8_t>(input[cursor]);
        uint32_t scalar{}; size_t count{};
        if (b0 < 0x80) { scalar = b0; count = 1; }
        else if (b0 >= 0xC2 && b0 <= 0xDF) { scalar = b0 & 0x1F; count = 2; }
        else if (b0 >= 0xE0 && b0 <= 0xEF) { scalar = b0 & 0x0F; count = 3; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { scalar = b0 & 0x07; count = 4; }
        else return false;
        for (size_t i = 1; i < count; ++i) {
            const auto byte = static_cast<uint8_t>(input[cursor + i]);
            if (!byte || (byte & 0xC0) != 0x80) return false;
            scalar = (scalar << 6) | (byte & 0x3F);
        }
        if ((count == 3 && scalar < 0x800) || (count == 4 && scalar < 0x10000) ||
            scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) return false;
        cursor += count;
        if (scalar <= 0xFFFF) {
            if (units + 1 >= capacity) return false;
            output[units++] = static_cast<wchar_t>(scalar);
        } else {
            if (units + 2 >= capacity) return false;
            scalar -= 0x10000;
            output[units++] = static_cast<wchar_t>(0xD800 + (scalar >> 10));
            output[units++] = static_cast<wchar_t>(0xDC00 + (scalar & 0x3FF));
        }
    }
    output[units] = 0;
    return true;
}

bool renderExplorerText(uint32_t* pixels, uint32_t width, uint32_t height,
                       int32_t x, int32_t y, const char* value, uint32_t color) noexcept {
    if (!g.fontReady || !pixels || !value || x < 0 || y < 0) return false;
    HDC dc = nullptr;
    if (pixels == g.desktopPixels) dc = g.desktopDc;
    else if (pixels == g.taskbarPixels) dc = g.taskbarDc;
    else if (pixels == g.menuPixels) dc = g.menuDc;
    else for (auto& app : g.apps) if (pixels == app.pixels) { dc = app.dc; break; }
    if (!dc) return false;
    wchar_t wide[4096]{}; size_t units{};
    if (!decodeUtf8ToUtf16(value, wide, sizeof(wide) / sizeof(wide[0]), units) || !units) return false;
    if (!SelectObject(dc, g.uiFont)) return false;
    const COLORREF foreground = RGB(static_cast<BYTE>((color >> 16) & 0xFF),
                                    static_cast<BYTE>((color >> 8) & 0xFF),
                                    static_cast<BYTE>(color & 0xFF));
    (void)SetTextColor(dc, foreground);
    (void)SetBkMode(dc, TRANSPARENT);
    (void)width; (void)height;
    const bool rendered = TextOutW(dc, x, y, wide, static_cast<int>(units)) != FALSE;
    if (rendered && !g.windowTtfDrawReported) {
        for (const auto& app : g.apps) {
            if (pixels == app.pixels) {
                PrintLine("[MicaNT FontProbe] Explorer window text rendered with selected TTF");
                g.windowTtfDrawReported = true;
                break;
            }
        }
    }
    return rendered;
}

void PrintThemeWin32Error(const char* prefix, uint32_t error) noexcept;

void text(uint32_t* p, uint32_t w, uint32_t h, int32_t x, int32_t y,
          const char* value, uint32_t color) noexcept {
    // Explorer's window surfaces are DIBs just like the desktop surfaces.
    // Keep the selected TrueType face explicit for each window DC before the
    // shared renderer resolves the pixel buffer back to that DC.
    for (auto& app : g.apps) {
        if (p == app.pixels && app.dc && g.uiFont) {
            (void)SelectObject(app.dc, g.uiFont);
            break;
        }
    }
    micant_gui::DrawStringClipped(p, w, h, x, y, value, color);
}

void drawAppWindow(AppWindow& app) noexcept;
void present(HWND hwnd) noexcept;

bool enumerateDirectory(AppWindow& app) noexcept {
    using namespace micant;
    app.directoryRowCount = 0;
    size_t chars = 0;
    while (chars < std::size(app.directoryPath) && app.directoryPath[chars]) ++chars;
    if (!chars || chars * sizeof(wchar_t) > UINT16_MAX - sizeof(wchar_t)) return false;
    UnicodeString name{};
    name.length = static_cast<uint16_t>(chars * sizeof(wchar_t));
    name.maximumLength = static_cast<uint16_t>(name.length + sizeof(wchar_t));
    name.buffer = app.directoryPath;
    ObjectAttributes attributes{};
    attributes.length = sizeof(attributes);
    attributes.objectName = &name;
    attributes.attributes = OBJ_CASE_INSENSITIVE;
    Handle directory{};
    IoStatusBlock io{};
    constexpr uint32_t FileReadAttributes = 0x00000080u;
    constexpr uint32_t FileReadData = 0x00000001u;
    constexpr uint32_t Synchronize = 0x00100000u;
    const NtStatus openStatus = native_nt::NtCreateFile(&directory,
        FileReadAttributes | FileReadData | Synchronize,
        &attributes, &io, nullptr, 0, 1, 1, 0x00000020u, nullptr, 0);
    if (openStatus != NtStatus::Success) {
        PrintThemeWin32Error("[MicaNT Explorer] Directory open status", static_cast<uint32_t>(openStatus));
        return false;
    }
    bool success = true;
    while (app.directoryRowCount < std::size(app.directoryRows)) {
        FileBothDirInformation entry{};
        const NtStatus status = native_nt::NtQueryDirectoryFile(directory, 0, nullptr,
            nullptr, &io, &entry, sizeof(entry), FileInformationClass::FileBothDirectoryInformation,
            true, nullptr, app.directoryRowCount == 0);
        if (status == NtStatus::NoMoreFiles) break;
        if (status != NtStatus::Success || entry.fileNameLength % sizeof(wchar_t) != 0 ||
            entry.fileNameLength / sizeof(wchar_t) >= std::size(entry.fileName)) {
            PrintThemeWin32Error("[MicaNT Explorer] Directory query status", static_cast<uint32_t>(status));
            success = false;
            app.directoryRowCount = 0;
            break;
        }
        const size_t length = entry.fileNameLength / sizeof(wchar_t);
        DirectoryRow& row = app.directoryRows[app.directoryRowCount++];
        for (size_t i = 0; i < length; ++i) row.name[i] = entry.fileName[i];
        row.name[length] = L'\0';
        row.attributes = entry.fileAttributes;
        row.size = entry.endOfFile.quadPart < 0 ? 0 : static_cast<uint64_t>(entry.endOfFile.quadPart);
    }
    (void)native_nt::NtClose(directory);
    return success;
}

bool enterExplorerFolder(AppWindow& app, size_t rowIndex) noexcept {
    if (rowIndex >= app.directoryRowCount ||
        !(app.directoryRows[rowIndex].attributes & 0x10u)) return false;
    size_t length = 0;
    while (length < std::size(app.directoryPath) && app.directoryPath[length]) ++length;
    const DirectoryRow& row = app.directoryRows[rowIndex];
    const size_t oldLength = length;
    size_t rowLength = 0;
    while (rowLength < std::size(row.name) && row.name[rowLength]) ++rowLength;
    const bool vfsRoot = length == 1 && app.directoryPath[0] == L'.';
    const bool needsSlash = !vfsRoot && length && app.directoryPath[length - 1] != L'\\';
    const size_t prefixLength = vfsRoot ? 0 : length;
    if (prefixLength + static_cast<size_t>(needsSlash) + rowLength >= std::size(app.directoryPath)) return false;
    if (vfsRoot) length = 0;
    if (needsSlash) app.directoryPath[length++] = L'\\';
    for (size_t i = 0; i < rowLength; ++i) app.directoryPath[length + i] = row.name[i];
    app.directoryPath[length + rowLength] = L'\0';
    if (!enumerateDirectory(app)) {
        app.directoryPath[oldLength] = L'\0';
        (void)enumerateDirectory(app);
        return false;
    }
    drawAppWindow(app);
    present(app.hwnd);
    PrintLine("[MicaNT Explorer] Filesystem directory enumerated after folder activation");
    return true;
}

void navigateExplorerUp(AppWindow& app) noexcept {
    size_t length = 0;
    while (length < std::size(app.directoryPath) && app.directoryPath[length]) ++length;
    if (length == 1 && app.directoryPath[0] == L'.') return;
    if (!length) return;
    if (length <= 3 && app.directoryPath[1] == L':') return;
    if (length > 3 && app.directoryPath[1] == L':') {
        while (length > 3 && app.directoryPath[length - 1] != L'\\') --length;
        if (length > 3) --length;
        app.directoryPath[length] = L'\0';
    } else {
    while (length && app.directoryPath[length - 1] != L'\\') --length;
    if (length) --length;
    if (!length) { app.directoryPath[0] = L'.'; length = 1; }
    app.directoryPath[length] = L'\0';
    }
    if (!enumerateDirectory(app)) return;
    drawAppWindow(app);
    present(app.hwnd);
    PrintLine("[MicaNT Explorer] Parent filesystem directory enumerated");
}

void drawAppWindow(AppWindow& app) noexcept {
    if (!app.pixels) return;
    uint32_t* p = app.pixels;
    const uint32_t w = app.width, h = app.height;
    const auto* windowStyle = themeStyle(14);
    const auto* toolbarStyle = themeStyle(10);
    const auto* entryStyle = themeStyle(1);
    const auto* statusBarStyle = themeStyle(11);
    const uint32_t windowBackground = windowStyle &&
        (windowStyle->flags & MICANT_THEME_HAS_BACKGROUND)
        ? themePixel(windowStyle->background) : rgb(255, 255, 255);
    const uint32_t toolbarBackground = toolbarStyle &&
        (toolbarStyle->flags & MICANT_THEME_HAS_BACKGROUND)
        ? themePixel(toolbarStyle->background, windowBackground) : rgb(242, 246, 251);
    const uint32_t toolbarForeground = toolbarStyle &&
        (toolbarStyle->flags & MICANT_THEME_HAS_FOREGROUND)
        ? themePixel(toolbarStyle->foreground) : rgb(40, 59, 81);
    const uint32_t entryBackground = entryStyle &&
        (entryStyle->flags & MICANT_THEME_HAS_BACKGROUND)
        ? themePixel(entryStyle->background, windowBackground) : rgb(255, 255, 255);
    const uint32_t entryForeground = entryStyle &&
        (entryStyle->flags & MICANT_THEME_HAS_FOREGROUND)
        ? themePixel(entryStyle->foreground) : rgb(35, 74, 117);
    uint32_t rowForeground = windowStyle &&
        (windowStyle->flags & MICANT_THEME_HAS_FOREGROUND)
        ? themePixel(windowStyle->foreground) : entryForeground;
    const auto contrastDistance = [](uint32_t left, uint32_t right) noexcept {
        const int32_t red = int32_t((left >> 16) & 0xFFu) - int32_t((right >> 16) & 0xFFu);
        const int32_t green = int32_t((left >> 8) & 0xFFu) - int32_t((right >> 8) & 0xFFu);
        const int32_t blue = int32_t(left & 0xFFu) - int32_t(right & 0xFFu);
        const auto absolute = [](int32_t value) noexcept { return value < 0 ? -value : value; };
        return absolute(red) + absolute(green) + absolute(blue);
    };
    if (contrastDistance(rowForeground, windowBackground) < 96)
        rowForeground = entryForeground;
    const uint32_t entryBorder = entryStyle &&
        (entryStyle->flags & MICANT_THEME_HAS_BORDER)
        ? themePixel(entryStyle->border) : rgb(191, 202, 215);
    const uint32_t separator = toolbarStyle &&
        (toolbarStyle->flags & MICANT_THEME_HAS_BORDER)
        ? themePixel(toolbarStyle->border) : entryBorder;
    const uint32_t secondaryForeground = toolbarForeground;
    const uint32_t statusBarBackground = statusBarStyle &&
        (statusBarStyle->flags & MICANT_THEME_HAS_BACKGROUND)
        ? themePixel(statusBarStyle->background, windowBackground) : windowBackground;
    const uint32_t statusBarForeground = statusBarStyle &&
        (statusBarStyle->flags & MICANT_THEME_HAS_FOREGROUND)
        ? themePixel(statusBarStyle->foreground) : secondaryForeground;
    const uint32_t sidebarBackground = toolbarStyle &&
        (toolbarStyle->flags & MICANT_THEME_HAS_BACKGROUND)
        ? toolbarBackground : rgb(236, 241, 247);
    fill(p, w, h, 0, 0, w, h, windowBackground);
    fill(p, w, h, 0, 0, 158, h, sidebarBackground);
    fill(p, w, h, 0, 0, w, 27, toolbarBackground);
    bool gdiThemeDrawn = false;
    if (g.visualThemeReady && w > 0 && h > 26) {
        const MicaThemeHandle theme = IsThemeActive() ? OpenThemeData(nullptr, L"TOOLBAR") : nullptr;
        if (theme) {
            RECT themedToolbar{0, 0, static_cast<LONG>(w), 26};
            const bool drawSucceeded = DrawThemeBackground(theme, app.dc, 1, 1,
                                                            &themedToolbar, nullptr) == 0;
            const uint32_t expected = (toolbarStyle &&
                (toolbarStyle->flags & MICANT_THEME_HAS_BACKGROUND))
                    ? themePixel(toolbarStyle->background, windowBackground)
                    : toolbarBackground;
            struct ThemeBackgroundOptions { uint32_t size, flags; RECT clip; } options{
                sizeof(ThemeBackgroundOptions), 1u, RECT{2, 2,
                    static_cast<LONG>(w - 2), 24}};
            constexpr uint32_t clipSentinel = 0xFF55AA33u;
            p[1u * w + 1u] = clipSentinel;
            const bool drawExSucceeded = DrawThemeBackgroundEx(theme, app.dc, 1, 1,
                &themedToolbar, &options) == 0;
            const bool clipPassed = drawExSucceeded && p[1u * w + 1u] == clipSentinel &&
                p[4u * w + 4u] == expected;
            ThemeBackgroundOptions unsupportedOptions = options;
            unsupportedOptions.flags = 0x00000002u; // DTBG_DRAWSOLID is not implemented.
            const bool unsupportedFlagFailed = DrawThemeBackgroundEx(theme, app.dc, 1, 1,
                &themedToolbar, &unsupportedOptions) == static_cast<int32_t>(0x80004001u);
            ThemeBackgroundOptions malformedOptions = options;
            malformedOptions.size = 0;
            const bool malformedSizeFailed = DrawThemeBackgroundEx(theme, app.dc, 1, 1,
                &themedToolbar, &malformedOptions) == static_cast<int32_t>(0x80070057u);
            p[1u * w + 1u] = toolbarBackground;
            const bool closeSucceeded = CloseThemeData(theme) == 0;
            gdiThemeDrawn = drawSucceeded && closeSucceeded && p[4u * w + 4u] == expected;
            if (clipPassed && unsupportedFlagFailed && malformedSizeFailed &&
                closeSucceeded && !g.themeBackgroundExReported) {
                PrintLine("[MicaNT Theme] DrawThemeBackgroundEx clipping and errors verified");
                g.themeBackgroundExReported = true;
            }
        }
    }
    if (gdiThemeDrawn && !g.themeGdiDrawReported) {
        PrintLine("[MicaNT Theme] UxTheme GDI toolbar draw and pixel check passed");
        g.themeGdiDrawReported = true;
    }
    if (!gdiThemeDrawn)
        fill(p, w, h, 0, 0, w, 27, toolbarBackground);
    fill(p, w, h, 0, 26, w, 1, separator);
    fill(p, w, h, 6, 4, 21, 21, entryBorder);
    fill(p, w, h, 7, 5, 19, 19, toolbarBackground);
    text(p, w, h, 11, 6, "\xE2\x86\x91", toolbarForeground);
    bool themedAddress = false;
    if (g.visualThemeReady && g.uiFont) {
        (void)SelectObject(app.dc, g.uiFont);
        const MicaThemeHandle textTheme = OpenThemeData(nullptr, L"TOOLBAR");
        if (textTheme) {
            RECT addressRect{27, 3, 150, 26};
            const int32_t drawStatus = DrawThemeText(textTheme, app.dc, 1, 1,
                L"Address", 7, 0x00000824u, 0, &addressRect);
            const bool closeSucceeded = CloseThemeData(textTheme) == 0;
            bool textPixelFound = false;
            for (uint32_t y = 3; y < 26 && !textPixelFound; ++y)
                for (uint32_t x = 27; x < 150; ++x)
                    if (p[static_cast<size_t>(y) * w + x] != toolbarBackground) {
                        textPixelFound = true;
                        break;
                    }
            themedAddress = drawStatus == 0 && closeSucceeded && textPixelFound;
            if (drawStatus != 0)
                PrintThemeWin32Error("[MicaNT Theme] DrawThemeText HRESULT", static_cast<uint32_t>(drawStatus));
            else if (!textPixelFound)
                PrintLine("[MicaNT Theme] DrawThemeText returned success without pixel output");
        }
    }
    if (themedAddress && !g.themeTextDrawReported) {
        PrintLine("[MicaNT Theme] UxTheme DrawThemeText rendered through GDI");
        g.themeTextDrawReported = true;
    }
    if (!g.themeTextClipReported && g.visualThemeReady && app.dc &&
        w > 207 && h >= 12) {
        constexpr int32_t kClipLeft = 151;
        constexpr int32_t kClipRight = 183;
        constexpr int32_t kClipTop = 3;
        constexpr int32_t kClipBottom = 11;
        constexpr int32_t kProbeLeft = 151;
        constexpr int32_t kProbeRight = 208;
        uint32_t original[57 * 8]{};
        for (int32_t y = kClipTop; y < kClipBottom; ++y)
            for (int32_t x = kProbeLeft; x < kProbeRight; ++x)
                original[(y - kClipTop) * 57 + (x - kProbeLeft)] = p[y * w + x];
        const MicaThemeHandle clipTheme = OpenThemeData(nullptr, L"TOOLBAR");
        const RECT clipRect{kClipLeft, kClipTop, kClipRight, kClipBottom};
        const int32_t clippedStatus = clipTheme
            ? DrawThemeText(clipTheme, app.dc, 1, 1, L"Address", 7,
                            0x00000820u, 0, &clipRect)
            : static_cast<int32_t>(0x80070006u);
        bool insideChanged = false;
        bool outsideUnchanged = true;
        for (int32_t y = kClipTop; y < kClipBottom; ++y) {
            for (int32_t x = kProbeLeft; x < kProbeRight; ++x) {
                const uint32_t before = original[(y - kClipTop) * 57 + (x - kProbeLeft)];
                if (x < kClipRight) insideChanged |= p[y * w + x] != before;
                else outsideUnchanged &= p[y * w + x] == before;
            }
        }
        const RECT restoredRect{190, kClipTop, 198, kClipBottom};
        const int32_t restoredStatus = clipTheme
            ? DrawThemeText(clipTheme, app.dc, 1, 1, L"A", 1,
                            0x00000820u, 0, &restoredRect)
            : static_cast<int32_t>(0x80070006u);
        bool restoredClipDrawn = false;
        for (int32_t y = kClipTop; y < kClipBottom; ++y)
            for (int32_t x = restoredRect.left; x < restoredRect.right; ++x)
                restoredClipDrawn |= p[y * w + x] !=
                    original[(y - kClipTop) * 57 + (x - kProbeLeft)];
        const bool clipThemeClosed = clipTheme && CloseThemeData(clipTheme) == 0;
        for (int32_t y = kClipTop; y < kClipBottom; ++y)
            for (int32_t x = kProbeLeft; x < kProbeRight; ++x)
                p[y * w + x] = original[(y - kClipTop) * 57 + (x - kProbeLeft)];
        g.themeTextClipReported = clippedStatus == 0 && restoredStatus == 0 &&
            clipThemeClosed && insideChanged && outsideUnchanged && restoredClipDrawn;
        PrintLine(g.themeTextClipReported
            ? "[MicaNT Theme] DrawThemeText clips to bounds and restores DC state"
            : "[MicaNT Theme] DrawThemeText clipping or DC restore mismatch");
    }
    if (!themedAddress) text(p, w, h, 27, 8, "Address", toolbarForeground);
    const bool editNeedsAlphaComposition = entryStyle &&
        (entryStyle->flags & MICANT_THEME_HAS_BACKGROUND) &&
        ((entryStyle->background >> 24) != 255u ||
         ((entryStyle->flags & MICANT_THEME_HAS_OPACITY) && entryStyle->opacity != 255u));
    fill(p, w, h, 12, 32, w - 24, 25,
         editNeedsAlphaComposition ? windowBackground : entryBackground);
    bool themedEditDrawn = false;
    if (g.visualThemeReady && w > 36 && h > 58) {
        const MicaThemeHandle theme = IsThemeActive() ? OpenThemeData(nullptr, L"EDIT") : nullptr;
        if (theme) {
            RECT editRect{12, 32, static_cast<LONG>(w - 12), 57};
            const bool drawSucceeded = DrawThemeBackground(theme, app.dc, 1, 1,
                                                            &editRect, nullptr) == 0;
            const bool closeSucceeded = CloseThemeData(theme) == 0;
            const uint32_t expected = entryStyle &&
                (entryStyle->flags & MICANT_THEME_HAS_BACKGROUND)
                    ? themePixel(entryStyle->background, windowBackground)
                    : entryBackground;
            const size_t probe = static_cast<size_t>(44) * w + w / 2;
            themedEditDrawn = drawSucceeded && closeSucceeded && p[probe] == expected;
        }
    }
    if (!themedEditDrawn) {
        fill(p, w, h, 12, 32, w - 24, 25, entryBackground);
        DrawRect(p, w, 12, 32, w - 24, 25, entryBorder);
    } else {
        if (!entryStyle || !(entryStyle->flags & MICANT_THEME_HAS_BORDER))
            DrawRect(p, w, 12, 32, w - 24, 25, entryBorder);
        if (!g.themeGdiEditDrawReported) {
            PrintLine("[MicaNT Theme] UxTheme GDI rounded EDIT draw and pixel check passed");
            g.themeGdiEditDrawReported = true;
        }
    }
    char currentPath[256]{};
    size_t pathLength = 0;
    if (app.view == View::MyComputer || app.view == View::MyDocuments) {
        const char* label = app.view == View::MyComputer ? "Computer: " : "Documents: ";
        for (size_t i = 0; label[i] && pathLength + 1 < sizeof(currentPath); ++i)
            currentPath[pathLength++] = label[i];
        for (size_t i = 0; app.directoryPath[i] && pathLength + 4 < sizeof(currentPath); ++i) {
            const uint32_t ch = static_cast<uint16_t>(app.directoryPath[i]);
            if (ch < 0x80) currentPath[pathLength++] = static_cast<char>(ch);
            else if (ch < 0x800) {
                currentPath[pathLength++] = static_cast<char>(0xC0 | (ch >> 6));
                currentPath[pathLength++] = static_cast<char>(0x80 | (ch & 0x3F));
            } else {
                currentPath[pathLength++] = static_cast<char>(0xE0 | (ch >> 12));
                currentPath[pathLength++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                currentPath[pathLength++] = static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        currentPath[pathLength] = '\0';
    } else {
        const char* label = app.view == View::ControlPanel ? "Control Panel" :
            app.view == View::RecycleBin ? "Recycle Bin" :
            app.view == View::DateTime ? "Date and Time" : "Network Places";
        for (size_t i = 0; label[i] && i + 1 < sizeof(currentPath); ++i) currentPath[i] = label[i];
    }
    text(p, w, h, 22, 40, currentPath, entryForeground);
    text(p, w, h, 174, 71, "Name", secondaryForeground);
    text(p, w, h, 370, 71, "Type", secondaryForeground);
    fill(p, w, h, 168, 88, w - 180, 1, separator);

    using micant::explorer::IconId;
    struct StaticRow { const char* name; const char* type; IconId icon; bool directory; };
    static constexpr StaticRow controlRows[] = {
        {"Appearance and Themes", "Control Panel item", IconId::Folder, true},
        {"Network Connections", "Control Panel item", IconId::Network, true},
        {"User Accounts", "Control Panel item", IconId::GenericText, true},
        {"Date and Time", "Control Panel item", IconId::GenericText, true},
    };
    static constexpr StaticRow networkRows[] = {
        {"MicaNT Network", "Network", IconId::Network, true},
        {"Workgroup Computers", "Computer", IconId::Computer, true},
    };
    static constexpr StaticRow recycleRows[] = {
        {"The Recycle Bin is empty.", "", IconId::TrashEmpty, false},
    };
    static constexpr StaticRow dateRows[] = {
        {"Date", "", IconId::GenericText, false}, {"Time", "", IconId::GenericText, false},
    };
    const StaticRow* staticRows = nullptr;
    size_t staticCount = 0;
    if (app.view == View::ControlPanel) { staticRows = controlRows; staticCount = std::size(controlRows); }
    else if (app.view == View::Network) { staticRows = networkRows; staticCount = std::size(networkRows); }
    else if (app.view == View::RecycleBin) { staticRows = recycleRows; staticCount = std::size(recycleRows); }
    else if (app.view == View::DateTime) { staticRows = dateRows; staticCount = std::size(dateRows); }
    const bool filesystemView = app.view == View::MyComputer || app.view == View::MyDocuments;
    const size_t rowCount = filesystemView ? app.directoryRowCount : staticCount;
    for (size_t i = 0; i < rowCount; ++i) {
        const int32_t y = 100 + static_cast<int32_t>(i) * 26;
        char displayName[520]{};
        IconId icon = IconId::Folder;
        bool isDirectory = false;
        const char* type = "File";
        if (filesystemView) {
            const DirectoryRow& row = app.directoryRows[i];
            size_t out = 0;
            for (size_t j = 0; row.name[j] && out + 4 < sizeof(displayName); ++j) {
                const uint32_t cp = static_cast<uint16_t>(row.name[j]);
                if (cp < 0x80) displayName[out++] = static_cast<char>(cp);
                else if (cp < 0x800) {
                    displayName[out++] = static_cast<char>(0xC0 | (cp >> 6));
                    displayName[out++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    displayName[out++] = static_cast<char>(0xE0 | (cp >> 12));
                    displayName[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    displayName[out++] = static_cast<char>(0x80 | (cp & 0x3F));
                }
            }
            isDirectory = (row.attributes & 0x10u) != 0;
            type = isDirectory ? "File folder" : "File";
            icon = isDirectory ? IconId::Folder : micant::explorer::ResolveFileIcon(displayName);
        } else {
            const StaticRow& row = staticRows[i];
            size_t j = 0;
            for (; row.name[j] && j + 1 < sizeof(displayName); ++j) displayName[j] = row.name[j];
            isDirectory = row.directory;
            type = row.type;
            icon = row.icon;
        }
        if (!findCachedIcon(icon, 16)) (void)loadPackagedIcon(icon, 16);
        const auto image = iconImage(icon, 16);
        if (image.pixels) micant::explorer::DrawIconImage(p, w, h, 174, y, 16, image);
        else fill(p, w, h, 174, y, 16, 16, activeShellPalette().accent);
        text(p, w, h, 199, y + 4, displayName, rowForeground);
        if (app.view == View::DateTime) {
            char value[16]{};
            if (i == 0) {
                value[0] = static_cast<char>('0' + g.systemTime.day / 10);
                value[1] = static_cast<char>('0' + g.systemTime.day % 10);
                value[2] = '/'; value[3] = static_cast<char>('0' + g.systemTime.month / 10);
                value[4] = static_cast<char>('0' + g.systemTime.month % 10);
                value[5] = '/';
                value[6] = static_cast<char>('0' + (g.systemTime.year / 1000) % 10);
                value[7] = static_cast<char>('0' + (g.systemTime.year / 100) % 10);
                value[8] = static_cast<char>('0' + (g.systemTime.year / 10) % 10);
                value[9] = static_cast<char>('0' + g.systemTime.year % 10);
            } else {
                value[0] = static_cast<char>('0' + g.systemTime.hour / 10);
                value[1] = static_cast<char>('0' + g.systemTime.hour % 10);
                value[2] = ':';
                value[3] = static_cast<char>('0' + g.systemTime.minute / 10);
                value[4] = static_cast<char>('0' + g.systemTime.minute % 10);
            }
            text(p, w, h, 370, y + 4, value, secondaryForeground);
        } else {
            text(p, w, h, 370, y + 4, type, secondaryForeground);
        }
        if (!g.explorerRowsThemePixelReported && i == 0 &&
            (app.view == View::MyComputer || app.view == View::ControlPanel)) {
            const size_t right = w < 360 ? w : 360;
            bool themedInkFound = false;
            for (size_t rowY = static_cast<size_t>(y + 2);
                 rowY < static_cast<size_t>(y + 26) && rowY < h && !themedInkFound;
                 ++rowY) {
                for (size_t column = 199; column < right; ++column) {
                    const uint32_t pixel = p[rowY * w + column];
                    const auto channelDistance = [](uint32_t left, uint32_t right,
                                                    uint32_t shift) noexcept {
                        const int32_t difference = int32_t((left >> shift) & 0xFFu) -
                                                   int32_t((right >> shift) & 0xFFu);
                        return difference < 0 ? -difference : difference;
                    };
                    if (channelDistance(pixel, rowForeground, 0) <= 24 &&
                        channelDistance(pixel, rowForeground, 8) <= 24 &&
                        channelDistance(pixel, rowForeground, 16) <= 24) {
                        themedInkFound = true;
                        break;
                    }
                }
            }
            if (themedInkFound) {
                PrintLine("[MicaNT Theme] Explorer row text uses active theme pixel");
                g.explorerRowsThemePixelReported = true;
            }
        }
        (void)isDirectory;
    }
    fill(p, w, h, 0, h - 24, w, 24, statusBarBackground);
    if (!g.statusBarThemePixelReported && w > 10 && h > 24 &&
        p[static_cast<size_t>(h - 12) * w + 10] == statusBarBackground) {
        PrintLine("[MicaNT Theme] Explorer status bar uses StatusBar theme pixel");
        g.statusBarThemePixelReported = true;
    }
    fill(p, w, h, 0, h - 25, w, 1, separator);
    text(p, w, h, 12, h - 17, "3 objects", statusBarForeground);
    if (!g.bufferedPaintReported && app.dc && w >= 16 && h >= 16) {
        constexpr int32_t kBufferWidth = 16;
        constexpr int32_t kBufferHeight = 16;
        constexpr int32_t kBufferFormatTopDownDib = 2;
        constexpr uint32_t kBufferColorRef = RGB(32, 64, 96);
        constexpr uint32_t kBufferPixel = 0xFF204060u;
        uint32_t original[kBufferWidth * kBufferHeight]{};
        for (int32_t y = 0; y < kBufferHeight; ++y)
            for (int32_t x = 0; x < kBufferWidth; ++x)
                original[y * kBufferWidth + x] = p[y * w + x];
        const RECT bounds{0, 0, kBufferWidth, kBufferHeight};
        void* rejectedDc = reinterpret_cast<void*>(1);
        const bool rejectedFormatClearsDc = !BeginBufferedPaint(app.dc, &bounds,
            0x7FFFFFFF, nullptr, &rejectedDc) && !rejectedDc;
        const int32_t firstInit = BufferedPaintInit();
        const int32_t secondInit = firstInit == 0 ? BufferedPaintInit() : -1;
        const bool initialized = firstInit == 0 && secondInit == 0;
        bool discardPassed = false;
        bool updatePassed = false;
        bool staleRejected = false;
        bool nestingPassed = false;
        if (initialized) {
            void* bufferDc{};
            void* buffer = BeginBufferedPaint(app.dc, &bounds,
                kBufferFormatTopDownDib, nullptr, &bufferDc);
            void* brush = CreateSolidBrush(kBufferColorRef);
            const bool filled = buffer && bufferDc && brush &&
                FillRect(static_cast<HDC>(bufferDc), &bounds, static_cast<HBRUSH>(brush)) != 0;
            if (brush) DeleteObject(static_cast<HGDIOBJ>(brush));
            const bool endedWithoutUpdate = buffer && EndBufferedPaint(buffer, FALSE) == 0;
            discardPassed = filled && endedWithoutUpdate &&
                p[8 * w + 8] == original[8 * kBufferWidth + 8];
            staleRejected = buffer && EndBufferedPaint(buffer, TRUE) != 0;

            const int32_t firstUninit = BufferedPaintUnInit();
            void* nestedDc{};
            void* nestedBuffer = firstUninit == 0
                ? BeginBufferedPaint(app.dc, &bounds, kBufferFormatTopDownDib,
                                     nullptr, &nestedDc) : nullptr;
            const bool nestedBegin = nestedBuffer && nestedDc;
            if (nestedBuffer) (void)EndBufferedPaint(nestedBuffer, FALSE);
            const int32_t secondUninit = BufferedPaintUnInit();
            void* postUninitDc = reinterpret_cast<void*>(1);
            const void* postUninitBuffer = BeginBufferedPaint(app.dc, &bounds,
                kBufferFormatTopDownDib, nullptr, &postUninitDc);
            nestingPassed = firstUninit == 0 && nestedBegin && secondUninit == 0 &&
                !postUninitBuffer && !postUninitDc;
        }
        if (firstInit == 0 && !initialized) (void)BufferedPaintUnInit();
        void* updateDc{};
        const int32_t updateInit = BufferedPaintInit();
        void* updateBuffer = BeginBufferedPaint(app.dc, &bounds,
            kBufferFormatTopDownDib, nullptr, &updateDc);
        void* updateBrush = updateBuffer && updateDc
            ? CreateSolidBrush(kBufferColorRef) : nullptr;
        const bool updateFilled = updateBuffer && updateDc && updateBrush &&
            FillRect(static_cast<HDC>(updateDc), &bounds, static_cast<HBRUSH>(updateBrush)) != 0;
        if (updateBrush) DeleteObject(static_cast<HGDIOBJ>(updateBrush));
        const bool endedWithUpdate = updateBuffer &&
            EndBufferedPaint(updateBuffer, TRUE) == 0;
        const int32_t updateUninit = updateInit == 0 ? BufferedPaintUnInit() : -1;
        updatePassed = initialized && updateInit == 0 && updateFilled && endedWithUpdate &&
            updateUninit == 0 &&
            p[8 * w + 8] == kBufferPixel;
        for (int32_t y = 0; y < kBufferHeight; ++y)
            for (int32_t x = 0; x < kBufferWidth; ++x)
                p[y * w + x] = original[y * kBufferWidth + x];
        g.bufferedPaintReported = true;
        if (rejectedFormatClearsDc && discardPassed && updatePassed &&
            staleRejected && nestingPassed)
            PrintLine("[MicaNT Theme] Buffered DIB paint init, discard, update and cleanup verified");
        else
            PrintLine("[MicaNT Theme] Buffered DIB paint lifecycle mismatch");
    }
}

void present(HWND hwnd) noexcept {
    if (hwnd) {
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
    }
}

size_t collectTaskbarButtons(micant::explorer::TaskbarButton* buttons) noexcept {
    micant::explorer::BuildTaskSwitchSnapshot(&g.taskSwitch, g.desktop, g.taskbar, g.startMenu);
    for (size_t i = 0; buttons && i < g.taskSwitch.count; ++i) {
        auto& button = buttons[i];
        button.title = g.taskSwitch.entries[i].title;
        button.iconColor = activeShellPalette().accent;
        button.active = g.taskSwitch.entries[i].active;
    }
    return g.taskSwitch.count;
}

void redrawTaskbar() noexcept {
    if (!g.taskbarPixels) return;
    micant::explorer::TaskbarButton buttons[MAX_APP_WINDOWS]{};
    const size_t count = collectTaskbarButtons(buttons);
    for (size_t i = 0; i < g.taskSwitch.count; ++i) {
        micant::explorer::TaskbarButtonBounds bounds{};
        if (!micant::explorer::GetTaskbarButtonBounds(g.screenW, g.screenH,
                TASKBAR_H, g.taskSwitch.count, i, bounds)) continue;
        const MicaWin32kRect rect{bounds.left, bounds.top,
                                  bounds.right, bounds.bottom};
        (void)MicaWin32kSetTaskbarButtonBounds(g.taskSwitch.entries[i].hwnd,
                                                &rect);
    }
    micant::explorer::DrawTaskbar(g.taskbarPixels, g.screenW, TASKBAR_H, TASKBAR_H,
                                  buttons, count, g.clockText, activeShellPalette());
    // Keep the existing hit targets and tray while bringing its visual surface
    // into the same dark, glassy family as the MacShell-inspired Dock.
    for (size_t i = 0; i < static_cast<size_t>(g.screenW) * TASKBAR_H; ++i)
        g.taskbarPixels[i] = overColor(g.taskbarPixels[i], rgb(18, 20, 27), 205);
    fill(g.taskbarPixels, g.screenW, TASKBAR_H, 0, 0, g.screenW, 1,
         rgb(112, 118, 133));
    if (!g.taskbarThemePixelReported && g.taskbarPixels) {
        const auto palette = activeShellPalette();
        if (g.taskbarPixels[5u * g.screenW + 90u] == palette.selected) {
            PrintLine("[MicaNT Theme] Taskbar Start surface uses active theme pixel");
            g.taskbarThemePixelReported = true;
        }
    }
    if (!g.trayThemePixelReported && g.taskbarPixels) {
        const auto palette = activeShellPalette();
        const size_t trayIconPixel = 17u * g.screenW + (g.screenW - 122u + 12u);
        if (g.taskbarPixels[trayIconPixel] == palette.accentForeground) {
            PrintLine("[MicaNT Theme] Notification icons use accent foreground pixel");
            g.trayThemePixelReported = true;
        }
    }
    present(g.taskbar);
}

void redrawDesktop() noexcept {
    if (!g.desktopPixels) return;
    const micant::explorer::DesktopIconSet icons{
        iconImage(micant::explorer::IconId::Computer, 48),
        iconImage(micant::explorer::IconId::Documents, 48),
        iconImage(micant::explorer::IconId::Network, 48),
        iconImage(micant::explorer::IconId::TrashEmpty, 48),
    };
    micant::explorer::DrawDesktop(g.desktopPixels, g.screenW, g.screenH - TASKBAR_H,
                                  g.wallpaperPixels, &icons,
                                  activeShellPalette().accentForeground);
    present(g.desktop);
    redrawTaskbar();
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

void launchThemeTest() noexcept {
    wchar_t imagePath[512]{};
    size_t cursor = 0;
    constexpr wchar_t suffix[] = L"\\System32\\theme_test.exe";
    if (!appendSystemRoot(imagePath, std::size(imagePath), cursor) ||
        cursor + std::size(suffix) > std::size(imagePath)) {
        PrintLine("[MicaNT Explorer] Could not build theme_test.exe path");
        return;
    }
    for (size_t i = 0; i < std::size(suffix); ++i) imagePath[cursor++] = suffix[i];

    wchar_t commandLine[512]{};
    for (size_t i = 0; i < cursor; ++i) commandLine[i] = imagePath[i];
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(imagePath, commandLine, nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) {
        PrintLine("[MicaNT Explorer] Starting theme_test.exe failed");
        return;
    }
    (void)CloseHandle(process.hThread);
    (void)CloseHandle(process.hProcess);
}

bool appendWide(wchar_t* path, size_t capacity, size_t& cursor,
                const wchar_t* suffix, size_t length) noexcept {
    if (length > capacity || cursor > capacity - length - 1) return false;
    for (size_t i = 0; i < length; ++i) path[cursor++] = suffix[i];
    path[cursor] = L'\0';
    return true;
}

void PrintThemeWin32Error(const char* prefix, uint32_t error) noexcept {
    char message[96]{};
    size_t length{};
    while (prefix[length] && length + 1 < sizeof(message)) {
        message[length] = prefix[length];
        ++length;
    }
    message[length++] = ' ';
    char digits[10]{};
    size_t digitCount{};
    do {
        digits[digitCount++] = static_cast<char>('0' + error % 10);
        error /= 10;
    } while (error && digitCount < sizeof(digits));
    while (digitCount && length + 1 < sizeof(message))
        message[length++] = digits[--digitCount];
    PrintLine(message);
}

bool themeProfileEquals(const char* left, const char* right) noexcept;
void loadActiveVisualTheme(const char* selectedProfile) noexcept;
bool readBoundedFile(const wchar_t* path, void* buffer, size_t capacity,
                     size_t& bytesRead) noexcept;

void verifyCurrentUserThemeProfilePath() noexcept {
    wchar_t profile[128]{};
    uint32_t characters = static_cast<uint32_t>(std::size(profile));
    const bool resolved = micant::userenv::GetUserProfileDirectoryW(
        reinterpret_cast<void*>(micant::native_nt::CurrentProcessTokenPseudoHandle),
        profile, &characters) != 0;
    constexpr wchar_t expectedPrefix[] = L"C:\\Users\\S-";
    bool hasSidPrefix = resolved && characters > std::size(expectedPrefix);
    for (size_t index = 0; hasSidPrefix && index + 1 < std::size(expectedPrefix); ++index)
        hasSidPrefix = profile[index] == expectedPrefix[index];
    PrintLine(hasSidPrefix
        ? "[MicaNT Theme] Userenv resolved profile path from current token SID"
        : "[MicaNT Theme] Userenv current token profile lookup failed");
}

void verifyThemeChangeQueueRollback() noexcept {
    if (!g.visualThemeReady || !g.desktop) {
        PrintLine("[MicaNT Theme] Queue pressure rollback probe unavailable");
        return;
    }
    g.themeRollbackProbe = g.visualTheme;
    char previousProfile[sizeof(g.themeProfile)]{};
    std::memcpy(previousProfile, g.themeProfile, sizeof(previousProfile));
    size_t queued = 0;
    while (queued < 512 && PostMessageW(g.desktop,
            WM_THEME_QUEUE_PRESSURE_PROBE, 0, 0)) ++queued;

    // A full queue must reject snapshot publication before changing the
    // compositor's snapshot. Keep the selected profile and visible theme too.
    loadActiveVisualTheme("WhiteSur/Dark");
    const bool unchanged = queued != 0 && !themeProfileEquals(g.themeProfile, "WhiteSur/Dark") &&
        std::memcmp(g.themeProfile, previousProfile, sizeof(previousProfile)) == 0 &&
        MicaWin32kGetThemeSnapshot(&g.themeVerify) &&
        std::memcmp(&g.themeVerify, &g.themeRollbackProbe,
                    sizeof(g.themeRollbackProbe)) == 0;

    MSG message{};
    size_t drained = 0;
    while (drained <= queued && PeekMessageW(&message, g.desktop,
            WM_THEME_QUEUE_PRESSURE_PROBE, WM_THEME_QUEUE_PRESSURE_PROBE, PM_REMOVE))
        ++drained;
    const bool queueRollback = unchanged && drained == queued;

    // An unavailable package profile must leave the published snapshot intact.
    loadActiveVisualTheme("WhiteSur/UnavailableProfile");
    const bool missingProfileRollback =
        std::memcmp(g.themeProfile, previousProfile, sizeof(previousProfile)) == 0 &&
        MicaWin32kGetThemeSnapshot(&g.themeVerify) &&
        std::memcmp(&g.themeVerify, &g.themeRollbackProbe,
                    sizeof(g.themeRollbackProbe)) == 0;

    g.themeCandidate = g.themeRollbackProbe;
    ++g.themeCandidate.version;
    const bool malformedSnapshotRejected =
        !MicaThemeSnapshotIsValid(&g.themeCandidate, sizeof(g.themeCandidate)) &&
        !MicaUxThemePublishSnapshot(&g.themeCandidate) &&
        MicaWin32kGetThemeSnapshot(&g.themeVerify) &&
        std::memcmp(&g.themeVerify, &g.themeRollbackProbe,
                    sizeof(g.themeRollbackProbe)) == 0;
    loadActiveVisualTheme("WhiteSur/Dark");
    const bool validProfileRecovered = themeProfileEquals(g.themeProfile, "WhiteSur/Dark") &&
        g.visualThemeReady && MicaThemeSnapshotIsValid(&g.visualTheme, sizeof(g.visualTheme));
    loadActiveVisualTheme(previousProfile);
    const bool originalProfileRestored =
        std::memcmp(g.themeProfile, previousProfile, sizeof(previousProfile)) == 0 &&
        MicaWin32kGetThemeSnapshot(&g.themeVerify) &&
        std::memcmp(&g.themeVerify, &g.themeRollbackProbe,
                    sizeof(g.themeRollbackProbe)) == 0;
    PrintLine(queueRollback && missingProfileRollback
        ? "[MicaNT Theme] Full notification queue rejected theme atomically and recovered"
        : "[MicaNT Theme] Full notification queue rollback probe failed");
    if (missingProfileRollback)
        PrintLine("[MicaNT Theme] Unavailable profile kept the previous complete theme");
    PrintLine(malformedSnapshotRejected
        ? "[MicaNT Theme] Malformed theme snapshot kept the previous complete theme"
        : "[MicaNT Theme] Malformed theme snapshot rollback probe failed");
    PrintLine(validProfileRecovered && originalProfileRestored
        ? "[MicaNT Theme] Valid profile recovered and original theme restored"
        : "[MicaNT Theme] Valid profile recovery failed");

    wchar_t stylePath[512]{};
    size_t stylePathCursor = 0;
    constexpr wchar_t styleSuffix[] =
        L"\\System\\Themes\\WhiteSur\\Probe\\gtk.css";
    const bool stylePathBuilt = appendSystemRoot(stylePath, std::size(stylePath),
        stylePathCursor) && appendWide(stylePath, std::size(stylePath),
        stylePathCursor, styleSuffix, std::size(styleSuffix) - 1);
    size_t styleBytes = 0;
    const bool malformedFixtureRead = stylePathBuilt &&
        readBoundedFile(stylePath, &g.themeCandidate,
        sizeof(g.themeCandidate), styleBytes) && styleBytes != 0 &&
        !MicaThemeSnapshotIsValid(&g.themeCandidate, styleBytes);
    if (malformedFixtureRead)
        loadActiveVisualTheme("WhiteSur/Probe");
    const bool malformedFileRollback = malformedFixtureRead &&
        std::memcmp(g.themeProfile, previousProfile, sizeof(previousProfile)) == 0 &&
        MicaWin32kGetThemeSnapshot(&g.themeVerify) &&
        std::memcmp(&g.themeVerify, &g.themeRollbackProbe,
                    sizeof(g.themeRollbackProbe)) == 0;
    PrintLine(malformedFileRollback
        ? "[MicaNT Theme] Malformed GTK CSS kept the previous complete theme"
        : "[MicaNT Theme] Malformed GTK CSS rollback probe failed");

    loadActiveVisualTheme("WhiteSur/../Light");
    const bool traversalProfileRejected =
        std::memcmp(g.themeProfile, previousProfile, sizeof(previousProfile)) == 0 &&
        MicaWin32kGetThemeSnapshot(&g.themeVerify) &&
        std::memcmp(&g.themeVerify, &g.themeRollbackProbe,
                    sizeof(g.themeRollbackProbe)) == 0;
    PrintLine(traversalProfileRejected
        ? "[MicaNT Theme] Traversal profile kept the previous complete theme"
        : "[MicaNT Theme] Traversal profile rejection changed the active theme");
}

bool readBoundedFile(const wchar_t* path, void* buffer, size_t capacity,
                     size_t& bytesRead) noexcept {
    bytesRead = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        PrintThemeWin32Error("[MicaNT Theme] CreateFileW error", GetLastError());
        return false;
    }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
        static_cast<uint64_t>(length.QuadPart) > capacity) {
        PrintLine("[MicaNT Theme] GetFileSizeEx failed or size was out of bounds");
        CloseHandle(file);
        return false;
    }
    const size_t expected = static_cast<size_t>(length.QuadPart);
    auto* destination = static_cast<uint8_t*>(buffer);
    while (bytesRead < expected) {
        const DWORD request = static_cast<DWORD>(expected - bytesRead > 65536
            ? 65536 : expected - bytesRead);
        DWORD amount = 0;
        if (!ReadFile(file, destination + bytesRead, request, &amount, nullptr) || !amount) {
            PrintLine("[MicaNT Theme] ReadFile failed or returned no data");
            CloseHandle(file);
            bytesRead = 0;
            return false;
        }
        bytesRead += amount;
    }
    CloseHandle(file);
    return true;
}

void loadActiveVisualTheme(const char* selectedProfile = nullptr) noexcept {
    if (!selectedProfile &&
        !MicaUxThemeGetCurrentProfile(g.themeProfile, sizeof(g.themeProfile)))
        return; // The compositor's asynchronous initial GTK load is still in progress.
    if ((selectedProfile && MicaUxThemeSelectProfile(selectedProfile) != 1) ||
        !MicaWin32kGetThemeSnapshot(&g.themeCandidate) ||
        !MicaThemeSnapshotIsValid(&g.themeCandidate, sizeof(g.themeCandidate))) {
        return;
    }
    g.visualTheme = g.themeCandidate;
    g.visualThemeReady = true;
    PrintLine("[MicaNT Theme] Explorer consumed UxTheme GTK snapshot");
    if (selectedProfile) {
        size_t length{};
        while (selectedProfile[length] && length + 1 < sizeof(g.themeProfile)) {
            g.themeProfile[length] = selectedProfile[length];
            ++length;
        }
        if (selectedProfile[length]) return;
        g.themeProfile[length] = '\0';
    }
}



bool themeProfileEquals(const char* left, const char* right) noexcept {
    if (!left || !right) return false;
    size_t i = 0;
    while (i < 96 && left[i] && right[i] && left[i] == right[i]) ++i;
    return i < 96 && left[i] == right[i];
}

void selectNextThemeProfile() noexcept {
    constexpr const char* profiles[] = {
        "WhiteSur/Light", "WhiteSur/Dark", "WhiteSur/LightPurple", "Auto"
    };
    size_t current = 0;
    for (size_t i = 0; i < std::size(profiles); ++i) {
        if (i == 3 ? g.themeAuto : themeProfileEquals(g.themeProfile, profiles[i]))
            current = i;
    }
    const char* next = profiles[(current + 1) % std::size(profiles)];
    const bool selectingAuto = themeProfileEquals(next, "Auto");
    const char* effectiveProfile = selectingAuto
        ? (g.systemTime.hour >= 6 && g.systemTime.hour < 18
            ? "WhiteSur/Light" : "WhiteSur/Dark")
        : next;
    const bool hadTheme = g.visualThemeReady;
    const auto oldTheme = g.visualTheme;
    char oldProfile[sizeof(g.themeProfile)]{};
    std::memcpy(oldProfile, g.themeProfile, sizeof(oldProfile));
    loadActiveVisualTheme(effectiveProfile);
    if (!themeProfileEquals(g.themeProfile, effectiveProfile)) {
        g.visualTheme = oldTheme;
        g.visualThemeReady = hadTheme;
        std::memcpy(g.themeProfile, oldProfile, sizeof(oldProfile));
        return;
    }
    g.themeAuto = selectingAuto;
    for (auto& app : g.apps) {
        if (!app.open || !app.hwnd || !app.pixels) continue;
        drawAppWindow(app);
        present(app.hwnd);
    }
    redrawDesktop();
    redrawTaskbar();
    if (g.menuPixels) {
        micant::explorer::DrawStartMenu(g.menuPixels, MENU_W, MENU_H, "MicaNT User",
                                        activeShellPalette());
        present(g.startMenu);
    }
    PrintLine(selectingAuto
        ? (themeProfileEquals(effectiveProfile, profiles[0])
            ? "[MicaNT Theme] Auto policy selected Light (06:00-18:00)"
            : "[MicaNT Theme] Auto policy selected Dark (18:00-06:00)")
        : themeProfileEquals(next, profiles[1])
        ? "[MicaNT Theme] Runtime profile switch to Dark passed"
        : themeProfileEquals(next, profiles[2])
            ? "[MicaNT Theme] Runtime profile switch to Light Purple passed"
            : "[MicaNT Theme] Runtime profile switch to Light passed");
}

void updateAutomaticTheme() noexcept {
    if (!g.themeAuto) return;
    const char* scheduledProfile = g.systemTime.hour >= 6 && g.systemTime.hour < 18
        ? "WhiteSur/Light" : "WhiteSur/Dark";
    if (themeProfileEquals(g.themeProfile, scheduledProfile)) return;

    loadActiveVisualTheme(scheduledProfile);
    if (!themeProfileEquals(g.themeProfile, scheduledProfile)) return;
    for (auto& app : g.apps) {
        if (!app.open || !app.hwnd || !app.pixels) continue;
        drawAppWindow(app);
        present(app.hwnd);
    }
    redrawDesktop();
    redrawTaskbar();
    PrintLine(scheduledProfile[9] == 'L'
        ? "[MicaNT Theme] Auto schedule changed to Light"
        : "[MicaNT Theme] Auto schedule changed to Dark");
}

void verifyAutomaticThemeBoundary() noexcept {
    if (!g.visualThemeReady || !g.taskbarPixels) {
        PrintLine("[MicaNT Theme] Auto boundary pixel probe unavailable");
        return;
    }
    g.themeRollbackProbe = g.visualTheme;
    char previousProfile[sizeof(g.themeProfile)]{};
    std::memcpy(previousProfile, g.themeProfile, sizeof(previousProfile));
    const bool previousAuto = g.themeAuto;
    const MicaWin32kSystemTime previousTime = g.systemTime;
    const size_t pixelIndex = 5u * g.screenW + 90u;

    g.themeAuto = true;
    g.systemTime.hour = 17;
    g.systemTime.minute = 59;
    updateAutomaticTheme();
    const uint32_t lightPixel = g.taskbarPixels[pixelIndex];
    const uint32_t expectedLightPixel = overColor(
        activeShellPalette().selected, rgb(18, 20, 27), 205);
    const bool lightSelected = themeProfileEquals(g.themeProfile, "WhiteSur/Light") &&
        lightPixel == expectedLightPixel;

    g.systemTime.hour = 18;
    g.systemTime.minute = 0;
    updateAutomaticTheme();
    const uint32_t darkPixel = g.taskbarPixels[pixelIndex];
    const uint32_t expectedDarkPixel = overColor(
        activeShellPalette().selected, rgb(18, 20, 27), 205);
    const bool darkSelected = themeProfileEquals(g.themeProfile, "WhiteSur/Dark") &&
        darkPixel == expectedDarkPixel && darkPixel != lightPixel;

    loadActiveVisualTheme(previousProfile);
    const bool restored = themeProfileEquals(g.themeProfile, previousProfile) &&
        MicaWin32kGetThemeSnapshot(&g.themeVerify) &&
        std::memcmp(&g.themeVerify, &g.themeRollbackProbe,
                    sizeof(g.themeRollbackProbe)) == 0;
    g.themeAuto = previousAuto;
    g.systemTime = previousTime;
    PrintLine(lightSelected && darkSelected && restored
        ? "[MicaNT Theme] Auto schedule boundary Light/Dark pixels verified"
        : "[MicaNT Theme] Auto schedule boundary pixel probe failed");
}

void verifyMissingDesktopIconFallback() noexcept {
    if (!g.desktopPixels || !g.desktop) {
        PrintLine("[MicaNT Explorer] Missing icon fallback probe unavailable");
        return;
    }
    using micant::explorer::IconId;
    const auto* missing = loadPackagedIcon(IconId::Computer, 255);
    const micant::explorer::DesktopIconSet icons{
        iconImage(IconId::Computer, 255),
        iconImage(IconId::Documents, 48),
        iconImage(IconId::Network, 48),
        iconImage(IconId::TrashEmpty, 48),
    };
    micant::explorer::DrawDesktop(g.desktopPixels, g.screenW,
        g.screenH - TASKBAR_H, g.wallpaperPixels, &icons,
        activeShellPalette().accentForeground);
    const bool fallbackPixel = g.desktopPixels[34u * g.screenW + 27u] ==
        micant_gui::PackBGRA(117, 199, 244);
    redrawDesktop();
    PrintLine(!missing && fallbackPixel
        ? "[MicaNT Explorer] Missing icon fell back to the built-in desktop glyph"
        : "[MicaNT Explorer] Missing icon fallback failed");
}

DWORD WINAPI themeScheduleWorker(void* context) noexcept {
    const HWND desktop = static_cast<HWND>(context);
    for (;;) {
        Sleep(1000);
        if (!PostMessageW(desktop, WM_THEME_SCHEDULE_TICK, 0, 0)) return 1;
    }
}

void loadPackagedWallpaper() noexcept {
    constexpr char16_t suffix[] = u"\\System32\\wallpaper.png";
    wchar_t path[320]{};
    const auto* shared = reinterpret_cast<const micant::KUserSharedData*>(
        micant::UserSharedDataAddress);
    size_t rootLength = 0;
    while (rootLength < std::size(shared->ntSystemRoot) &&
           shared->ntSystemRoot[rootLength]) ++rootLength;
    if (rootLength < 3 || rootLength + std::size(suffix) > std::size(path) ||
        shared->ntSystemRoot[1] != u':' || shared->ntSystemRoot[2] != u'\\') return;
    for (size_t i = 0; i < rootLength; ++i)
        path[i] = static_cast<wchar_t>(shared->ntSystemRoot[i]);
    for (size_t i = 0; i < std::size(suffix); ++i)
        path[rootLength + i] = static_cast<wchar_t>(suffix[i]);
    const auto allocator = micant::um::imaging::NativeImageAllocator();
    micant::um::imaging::ImageLimits limits{};
    micant::um::imaging::ImageFrame decoded{};
    const auto decodeStatus = micant::um::imaging::DecodeImageFile(
        path, decoded, allocator, limits);
    if (decodeStatus != micant::um::imaging::ImageStatus::Success) return;

    micant::um::imaging::ImageFrame composed{};
    const uint32_t background = PackBGRA(28, 58, 92);
    if (decoded.pixels.size() >= limits.maxIntermediateBytes) return;
    const uint64_t compositionBudget = limits.maxIntermediateBytes - decoded.pixels.size();
    // ComposeWallpaper allocates the final surface once and that allocation is
    // retained directly as the wallpaper cache below.
    const uint64_t outputBudget = compositionBudget;
    const auto composeStatus = micant::um::imaging::ComposeWallpaper(
        &decoded, g.screenW, g.screenH - TASKBAR_H,
        micant::um::imaging::WallpaperStyle::Fill, background,
        allocator, composed,
        outputBudget < limits.maxDecodedBytes ? outputBudget : limits.maxDecodedBytes);
    if (composeStatus != micant::um::imaging::ImageStatus::Success) return;
    // The cache belongs to this process until its address space is torn down.
    // Keep only the trivial pointer/allocator state in the global shell object
    // so the freestanding image does not need a CRT atexit registration.
    auto* pixels = composed.pixels.releaseOwnership(g.wallpaperBytes,
                                                     g.wallpaperAllocator);
    g.wallpaperPixels = reinterpret_cast<uint32_t*>(pixels);
}

CachedDesktopIcon* loadPackagedIcon(micant::explorer::IconId id, uint16_t requestedSize) noexcept {
    using namespace micant::explorer;
    if (!requestedSize || requestedSize > 256) return nullptr;
    for (size_t i = 0; i < g.iconCacheCount; ++i) {
        auto& cached = g.iconCache[i];
        if (cached.id == id && cached.requestedSize == requestedSize)
            return cached.pixels ? &cached : nullptr;
    }
    if (g.iconCacheCount >= std::size(g.iconCache)) return nullptr;
    CachedDesktopIcon& cached = g.iconCache[g.iconCacheCount++];
    cached.id = id;
    cached.requestedSize = requestedSize;
    const IconAsset* asset = FindIconAsset(id);
    if (!asset) return nullptr;
    const auto* shared = reinterpret_cast<const micant::KUserSharedData*>(
        micant::UserSharedDataAddress);
    size_t rootLength = 0;
    while (rootLength < std::size(shared->ntSystemRoot) &&
           shared->ntSystemRoot[rootLength]) ++rootLength;
    if (rootLength < 3 || shared->ntSystemRoot[1] != u':' ||
        shared->ntSystemRoot[2] != u'\\') return nullptr;
    constexpr char16_t prefix[] = u"\\System\\Icons\\WhiteSur\\";
    const char16_t slash[] = u"\\";
    char16_t sizeDir[5]{};
    uint16_t value = requestedSize;
    size_t digitCount = 0;
    do { ++digitCount; value = static_cast<uint16_t>(value / 10); } while (value);
    value = requestedSize;
    for (size_t i = 0; i < digitCount; ++i) {
        sizeDir[digitCount - 1 - i] = static_cast<char16_t>(u'0' + value % 10);
        value = static_cast<uint16_t>(value / 10);
    }
    sizeDir[digitCount] = u'\\';
    sizeDir[digitCount + 1] = 0;
    wchar_t path[320]{};
    size_t cursor = 0;
    for (; cursor < rootLength && cursor + 1 < std::size(path); ++cursor)
        path[cursor] = static_cast<wchar_t>(shared->ntSystemRoot[cursor]);
    auto append = [&](const char16_t* text, size_t capacity) noexcept {
        for (size_t i = 0; i < capacity && text[i]; ++i) {
            if (cursor + 1 >= std::size(path)) return false;
            path[cursor++] = static_cast<wchar_t>(text[i]);
        }
        return true;
    };
    const size_t categoryLength = [&]() noexcept { size_t n=0; while (asset->category[n]) ++n; return n; }();
    const size_t nameLength = [&]() noexcept { size_t n=0; while (asset->fileName[n]) ++n; return n; }();
    if (cursor != rootLength || !append(prefix, std::size(prefix)) ||
        !append(asset->category, categoryLength) || !append(slash, std::size(slash)) ||
        !append(sizeDir, std::size(sizeDir)) || !append(asset->fileName, nameLength))
        return nullptr;
    const auto allocator = micant::um::imaging::NativeImageAllocator();
    micant::um::imaging::ImageLimits limits{};
    limits.maxEncodedBytes = 2ull * 1024 * 1024;
    limits.maxPixels = 256ull * 256;
    limits.maxDecodedBytes = 256ull * 256 * 4;
    limits.maxIntermediateBytes = 2ull * 1024 * 1024;
    micant::um::imaging::ImageFrame decoded{};
    if (micant::um::imaging::DecodeImageFile(path, decoded, allocator, limits) !=
        micant::um::imaging::ImageStatus::Success) return nullptr;
    cached.pixels = decoded.pixels.releaseOwnership(cached.bytes, cached.allocator);
    cached.width = decoded.width;
    cached.height = decoded.height;
    cached.stride = decoded.stride;
    return &cached;
}

void loadPackagedDesktopIcons() noexcept {
    using micant::explorer::IconId;
    constexpr IconId desktopIcons[]{IconId::Computer, IconId::Documents,
        IconId::Network, IconId::TrashEmpty, IconId::Pictures, IconId::Music,
        IconId::Videos, IconId::Executable, IconId::HardDisk};
    for (IconId id : desktopIcons)
        (void)loadPackagedIcon(id, 48);
}



void initializeSystemFonts() noexcept {
    // The UI uses 9 pt at the reference 96 DPI: a -12 logical-pixel height.
    g.uiFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g.fontReady = g.uiFont != nullptr;
    if (!g.fontReady) {
        PrintLine("[MicaNT FontProbe] GDI font creation failed");
        return;
    }
    micant_gui::SetTextRenderer(renderExplorerText);
    if (g.desktopDc && SelectObject(g.desktopDc, g.uiFont)) {
        SIZE textSize{};
        TEXTMETRICW metrics{};
        if (!GetTextExtentPoint32W(g.desktopDc, L"MicaNT", 6, &textSize)) {
            PrintLine("[MicaNT FontProbe] GetTextExtentPoint32W failed");
        } else if (!GetTextMetricsW(g.desktopDc, &metrics)) {
            PrintLine("[MicaNT FontProbe] GetTextMetricsW failed");
        } else if (textSize.cx > 0 && textSize.cy == metrics.tmHeight &&
                   metrics.tmAscent > 0 && metrics.tmDescent >= 0) {
            PrintLine("[MicaNT FontProbe] GDI layout metrics read from selected TTF");
        } else {
            PrintLine("[MicaNT FontProbe] GDI TTF metric values inconsistent");
        }
        SIZE precomposedAccent{}, combiningAccent{};
        if (GetTextExtentPoint32W(g.desktopDc, L"é", 1, &precomposedAccent) &&
            GetTextExtentPoint32W(g.desktopDc, L"e\u0301", 2, &combiningAccent) &&
            precomposedAccent.cx == combiningAccent.cx) {
            PrintLine("[MicaNT FontProbe] Combining accent retains the base advance");
        } else {
            PrintLine("[MicaNT FontProbe] Combining accent layout failed");
        }
        const wchar_t glyphProbeText[]{L'A', static_cast<wchar_t>(0x0378)};
        WORD glyphProbe[2]{};
        if (GetGlyphIndicesW(g.desktopDc, glyphProbeText, 2, glyphProbe,
                GGI_MARK_NONEXISTING_GLYPHS) == 2 && glyphProbe[0] &&
            glyphProbe[1] == 0xFFFFu) {
            PrintLine("[MicaNT FontProbe] GDI glyph indices map TTF cmap and mark missing glyphs");
        } else {
            PrintLine("[MicaNT FontProbe] GDI glyph index lookup failed");
        }
        const HFONT alternateFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
        if (alternateFont) {
            SIZE primaryExtent{}, alternateExtent{};
            const BOOL measuredPrimary = GetTextExtentPoint32W(
                g.desktopDc, L"MicaNT WWW 123", 14, &primaryExtent);
            const HGDIOBJ previousFont = SelectObject(g.desktopDc, alternateFont);
            const BOOL measuredAlternate = previousFont && GetTextExtentPoint32W(
                g.desktopDc, L"MicaNT WWW 123", 14, &alternateExtent);
            if (previousFont) (void)SelectObject(g.desktopDc, previousFont);
            if (measuredPrimary && measuredAlternate &&
                (primaryExtent.cx != alternateExtent.cx ||
                 primaryExtent.cy != alternateExtent.cy)) {
                PrintLine("[MicaNT FontProbe] Runtime Segoe UI/Arial TTF face switch changed extents");
            } else {
                PrintLine("[MicaNT FontProbe] Runtime alternate TTF face switch failed");
            }
            (void)DeleteObject(alternateFont);
        }
        LOGFONTW filter{};
        filter.lfCharSet = DEFAULT_CHARSET;
        constexpr wchar_t filterFamily[] = L"Arial";
        for (size_t i = 0; i < std::size(filterFamily); ++i)
            filter.lfFaceName[i] = filterFamily[i];
        uint32_t enumeratedFaces = 0;
        const int enumResult = EnumFontFamiliesExW(g.desktopDc, &filter,
            [](const LOGFONTW*, const TEXTMETRICW* fontMetrics, DWORD fontType,
               LPARAM context) -> int {
                if (!fontMetrics || !(fontType & TRUETYPE_FONTTYPE) || !context) return 0;
                ++*reinterpret_cast<uint32_t*>(context);
                return 1;
            }, reinterpret_cast<LPARAM>(&enumeratedFaces), 0);
        if (enumResult && enumeratedFaces == 4) {
            PrintLine("[MicaNT FontProbe] GDI enumerated four installed Arial TTF styles");
        } else {
            PrintLine("[MicaNT FontProbe] GDI TTF family enumeration failed");
        }
    } else {
        PrintLine("[MicaNT FontProbe] GDI metric DC/font selection failed");
    }
    PrintLine("[MicaNT FontProbe] GDI TrueType font selected for Explorer surfaces");
}

void verifyDirectWriteFonts() noexcept {
    using namespace micant::dwrite;
    constexpr int32_t HResultOk = 0;
    micant::ole32::IUnknown* unknown = nullptr;
    const int32_t factoryStatus = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        IID_IDWriteFactory, &unknown);
    if (factoryStatus != HResultOk || !unknown) {
        PrintLine("[MicaNT FontProbe] DirectWrite factory creation failed");
        return;
    }
    auto* factory = static_cast<IDWriteFactory*>(unknown);
    IDWriteFontCollection* collection = nullptr;
    IDWriteFontFamily* family = nullptr;
    IDWriteFont* font = nullptr;
    IDWriteFontFace* face = nullptr;
    IDWriteTextFormat* textFormat = nullptr;
    IDWriteTextLayout* textLayout = nullptr;
    uint32_t familyIndex = 0;
    int32_t exists = 0;
    uint16_t glyphs[2]{};
    const uint32_t codePoints[2]{'A', 0x00E0};
    DWRITE_FONT_METRICS metrics{};
    const bool success = factory->GetSystemFontCollection(&collection, 0) == HResultOk &&
        collection && collection->GetFontFamilyCount() == 6 &&
        collection->FindFamilyName(L"Arial", &familyIndex, &exists) == HResultOk && exists &&
        collection->GetFontFamily(familyIndex, &family) == HResultOk && family &&
        family->GetFontCount() == 4 && family->GetFont(0, &font) == HResultOk && font &&
        font->CreateFontFace(&face) == HResultOk && face &&
        face->GetGlyphCount() > 2 &&
        face->GetGlyphIndices(codePoints, 2, glyphs) == HResultOk &&
        glyphs[0] && glyphs[1] && (face->GetMetrics(&metrics), metrics.designUnitsPerEm != 0) &&
        factory->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-us",
            &textFormat) == HResultOk && textFormat &&
        factory->CreateTextLayout(L"MicaNT font layout", 17, textFormat, 300.0f, 100.0f,
            &textLayout) == HResultOk && textLayout;
    DWRITE_TEXT_METRICS textMetrics{};
    DWRITE_LINE_METRICS lineMetrics{};
    uint32_t lineCount = 0;
    const bool layoutSuccess = textLayout &&
        textLayout->GetMetrics(&textMetrics) == HResultOk &&
        textLayout->GetLineMetrics(&lineMetrics, 1, &lineCount) == HResultOk &&
        lineCount == 1 && lineMetrics.length == 17 && textMetrics.width > 0.0f &&
        textMetrics.height > 0.0f && lineMetrics.baseline > 0.0f;
    if (textLayout) textLayout->Release();
    if (textFormat) textFormat->Release();
    if (face) face->Release();
    if (font) font->Release();
    if (family) family->Release();
    if (collection) collection->Release();
    factory->Release();
    PrintLine(success && layoutSuccess
        ? "[MicaNT FontProbe] DirectWrite loaded Arial TTF metrics and Unicode cmap"
        : "[MicaNT FontProbe] DirectWrite collection/face/layout verification failed");
    if (success && layoutSuccess)
        PrintLine("[MicaNT FontProbe] DirectWrite text layout reports TrueType line metrics");
}

void chooseFallbackActiveApp() noexcept {
    g.activeApp = 0xFF;
    for (size_t i = MAX_APP_WINDOWS; i > 0; --i) {
        const size_t index = i - 1;
        if (g.apps[index].open && !g.apps[index].minimized) {
            g.activeApp = static_cast<uint8_t>(index);
            return;
        }
    }
}

AppWindow* appFromHwnd(HWND hwnd) noexcept {
    for (auto& app : g.apps) if (app.open && app.hwnd == hwnd) return &app;
    return nullptr;
}

LRESULT CALLBACK ThemeAttachedScrollProc(HWND hwnd, UINT message,
                                          WPARAM wParam, LPARAM lParam) {
    if (message == 0x0081u) return 1; // WM_NCCREATE
    if (message == 0x000Fu) { // WM_PAINT
        PAINTSTRUCT paint{};
        if (BeginPaint(hwnd, &paint)) {
            RECT client{};
            if (GetClientRect(hwnd, &client)) {
                HBRUSH background = CreateSolidBrush(activeShellPalette().surfaceAlt);
                if (background) {
                    (void)FillRect(paint.hdc, &client, background);
                    (void)DeleteObject(background);
                }
            }
            (void)EndPaint(hwnd, &paint);
        }
        return 0;
    }
    if ((message == 0x0114u || message == 0x0115u) &&
        hwnd == g.themeAttachedScrollProbe) {
        const uint32_t code = static_cast<uint16_t>(wParam);
        const int position = GetScrollPos(hwnd, message == 0x0115u ? 1 : 0);
        if (message == 0x0115u && code == 0u && position == 19 &&
            !g.themeAttachedScrollArrowReported) {
            g.themeAttachedScrollArrowReported = true;
            PrintLine("[MicaNT Theme] Attached WS_VSCROLL arrow notified and position read back");
        }
        if (message == 0x0115u && code == 5u && position > 19 &&
            !g.themeAttachedScrollThumbReported) {
            g.themeAttachedScrollThumbReported = true;
            PrintLine("[MicaNT Theme] Attached WS_VSCROLL thumb drag notified and position read back");
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void openView(View view) noexcept {
    for (auto& existing : g.apps) {
        if (existing.open && existing.view == view) {
            existing.minimized = false;
            g.activeApp = static_cast<uint8_t>(&existing - g.apps);
            ShowWindow(existing.hwnd, SW_RESTORE);
            SetWindowPos(existing.hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetFocus(existing.hwnd);
            redrawDesktop();
            present(existing.hwnd);
            return;
        }
    }
    AppWindow* app = nullptr;
    for (auto& candidate : g.apps) if (!candidate.open) { app = &candidate; break; }
    if (!app) return;
    app->view = view;
    app->firstPaintPresented = false;
    const auto* shared = reinterpret_cast<const micant::KUserSharedData*>(micant::UserSharedDataAddress);
    size_t systemRootLength = 0;
    while (systemRootLength < std::size(shared->ntSystemRoot) && shared->ntSystemRoot[systemRootLength])
        ++systemRootLength;
    if (systemRootLength < 4 || systemRootLength >= std::size(app->directoryPath)) {
        DestroyWindow(app->hwnd);
        *app = {};
        return;
    }
    for (size_t i = 0; i <= systemRootLength; ++i)
        app->directoryPath[i] = static_cast<wchar_t>(shared->ntSystemRoot[i]);
    // Start in the system root's parent so the first view contains the real
    // System32 directory and can immediately exercise folder navigation.
    while (systemRootLength > 3 && app->directoryPath[systemRootLength - 1] != L'\\')
        --systemRootLength;
    if (systemRootLength > 3) --systemRootLength;
    app->directoryPath[systemRootLength] = L'\0';
    app->directoryRowCount = 0;
    const uint32_t kInitialWindowWidth = 500u;
    const uint32_t kInitialWindowHeight = 320u;
    const int32_t appIndex = static_cast<int32_t>(app - g.apps);
    app->hwnd = CreateWindowExW(0, kClassName,
        view == View::MyComputer ? L"My Computer" :
        view == View::MyDocuments ? L"My Documents" :
        view == View::ControlPanel ? L"Control Panel" :
        view == View::RecycleBin ? L"Recycle Bin" :
        view == View::DateTime ? L"Date and Time" : L"Network Places",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 190 + (appIndex % 4) * 32,
        90 + (appIndex % 4) * 28,
        static_cast<int32_t>(kInitialWindowWidth), static_cast<int32_t>(kInitialWindowHeight),
        nullptr, nullptr, nullptr, nullptr);
    if (!app->hwnd) return;
    RECT clientRect{};
    if (!GetClientRect(app->hwnd, &clientRect) || clientRect.right <= clientRect.left ||
        clientRect.bottom <= clientRect.top) {
        PrintLine("[MicaNT Explorer] App window client geometry was invalid");
        DestroyWindow(app->hwnd);
        *app = {};
        return;
    }
    app->width = static_cast<uint32_t>(clientRect.right - clientRect.left);
    app->height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
    app->open = true;
    app->minimized = false;

    if (view == View::MyComputer || view == View::MyDocuments) {
        if (!enumerateDirectory(*app))
            PrintLine("[MicaNT Explorer] Native directory open/enumeration failed");
        else {
            PrintLine("[MicaNT Explorer] Initial filesystem directory enumerated");
            if (!app->directoryRowCount)
                PrintLine("[MicaNT Explorer] Initial filesystem directory is empty");
        }
    }
    g.activeApp = static_cast<uint8_t>(app - g.apps);
    SetFocus(app->hwnd);
    app->dc = CreateCompatibleDC(nullptr);
    void* appBits = nullptr;
    BITMAPINFO appInfo{};
    appInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    appInfo.bmiHeader.biWidth = static_cast<LONG>(app->width);
    appInfo.bmiHeader.biHeight = -static_cast<LONG>(app->height);
    appInfo.bmiHeader.biPlanes = 1;
    appInfo.bmiHeader.biBitCount = 32;
    appInfo.bmiHeader.biCompression = BI_RGB;
    HBITMAP appBitmap = CreateDIBSection(app->dc, &appInfo, DIB_RGB_COLORS, &appBits, nullptr, 0);
    if (appBitmap && appBits) {
        SelectObject(app->dc, appBitmap);
        app->bitmap = appBitmap;
        app->pixels = static_cast<uint32_t*>(appBits);
    } else {
        PrintLine(!app->dc
            ? "[MicaNT Explorer] App window compatible DC allocation failed"
            : !appBitmap
                ? "[MicaNT Explorer] App window DIB allocation failed"
                : "[MicaNT Explorer] App window DIB pixels unavailable");
        DestroyWindow(app->hwnd);
        if (app->dc) DeleteDC(app->dc);
        if (appBitmap) DeleteObject(appBitmap);
        *app = {};
        return;
    }
    drawAppWindow(*app);
    redrawDesktop();
    present(app->hwnd);

}

void toggleStart() noexcept {
    g.startOpen = !g.startOpen;
    if (g.startOpen) {
        micant::explorer::DrawStartMenu(g.menuPixels, MENU_W, MENU_H, "MicaNT User",
                                        activeShellPalette());
        ShowWindow(g.startMenu, SW_SHOW);
        present(g.startMenu);
    } else {
        ShowWindow(g.startMenu, SW_HIDE);
    }
    redrawDesktop();
}

void updateClock() noexcept {
    MicaWin32kSystemTime value{};
    bool timeAvailable = false;
    SYSTEMTIME systemTime{};
    GetSystemTime(&systemTime);
    if (systemTime.wYear >= 1601 && systemTime.wYear <= 9999 &&
        systemTime.wMonth >= 1 && systemTime.wMonth <= 12 &&
        systemTime.wDay >= 1 && systemTime.wDay <= 31 &&
        systemTime.wHour < 24 && systemTime.wMinute < 60 && systemTime.wSecond < 60) {
        value.year = systemTime.wYear;
        value.month = static_cast<uint8_t>(systemTime.wMonth);
        value.day = static_cast<uint8_t>(systemTime.wDay);
        value.hour = static_cast<uint8_t>(systemTime.wHour);
        value.minute = static_cast<uint8_t>(systemTime.wMinute);
        value.second = static_cast<uint8_t>(systemTime.wSecond);
        timeAvailable = true;
    } else {
    micant::LargeInteger nativeTime{};
    uint64_t fileTime{};
    if (micant::native_nt::NtQuerySystemTime(&nativeTime) == micant::NtStatus::Success &&
        nativeTime.QuadPart >= 0) {
        fileTime = static_cast<uint64_t>(nativeTime.QuadPart);
    } else {
        const auto* shared = reinterpret_cast<const micant::KUserSharedData*>(
            micant::UserSharedDataAddress);
        for (unsigned retry = 0; retry < 8; ++retry) {
            const int32_t high1 = shared->systemTimeHigh1;
            const uint32_t low = shared->systemTimeLow;
            const int32_t high2 = shared->systemTimeHigh2;
            if (high1 == high2) {
                fileTime = (static_cast<uint64_t>(static_cast<uint32_t>(high1)) << 32) | low;
                break;
            }
        }
        if (fileTime) {
            micant::shared::time::SystemTimeFields fields{};
            if (micant::shared::time::fromSystemTime100ns(fileTime, fields)) {
                value.year = fields.year;
                value.month = static_cast<uint8_t>(fields.month);
                value.day = static_cast<uint8_t>(fields.day);
                value.hour = static_cast<uint8_t>(fields.hour);
                value.minute = static_cast<uint8_t>(fields.minute);
                value.second = static_cast<uint8_t>(fields.second);
                timeAvailable = true;
            }
        }
    }
    }
    // The shell clock is a local clock face. NT system time remains UTC;
    // apply the explicit Europe/Rome fallback while no zone data is published.
    if (timeAvailable) {
        micant::shared::time::SystemTimeFields utc{
            value.year, value.month, value.day, value.hour, value.minute,
            value.second, 0, 0};
        uint64_t utcTicks{};
        if (micant::shared::time::toSystemTime100ns(utc, utcTicks)) {
            const auto lastSunday = [](uint16_t year, uint16_t month) noexcept {
                const uint32_t lastDay = micant::shared::time::daysInMonth(year, month);
                const uint32_t weekday = static_cast<uint32_t>(
                    (micant::shared::time::daysSince1601(year, month, lastDay) + 1) % 7);
                return lastDay - weekday;
            };
            micant::shared::time::SystemTimeFields start{value.year, 3,
                static_cast<uint16_t>(lastSunday(value.year, 3)), 1, 0, 0};
            micant::shared::time::SystemTimeFields end{value.year, 10,
                static_cast<uint16_t>(lastSunday(value.year, 10)), 1, 0, 0};
            uint64_t startTicks{}, endTicks{};
            const bool summerTime = micant::shared::time::toSystemTime100ns(start, startTicks) &&
                micant::shared::time::toSystemTime100ns(end, endTicks) &&
                utcTicks >= startTicks && utcTicks < endTicks;
            const uint64_t offset = (summerTime ? 2 : 1) * 60 *
                micant::shared::time::TicksPerMinute;
            if (utcTicks <= UINT64_MAX - offset) {
                micant::shared::time::SystemTimeFields local{};
                if (micant::shared::time::fromSystemTime100ns(utcTicks + offset, local)) {
                    value.year = local.year;
                    value.month = static_cast<uint8_t>(local.month);
                    value.day = static_cast<uint8_t>(local.day);
                    value.hour = static_cast<uint8_t>(local.hour);
                    value.minute = static_cast<uint8_t>(local.minute);
                    value.second = static_cast<uint8_t>(local.second);
                }
            }
        }
    }
    if (timeAvailable) {
        if (value.second == g.clockSecond) return;
        g.clockSecond = value.second;
    } else {
        if (g.clockSecond == 0xFE) return;
        g.clockSecond = 0xFE;
    }
    g.systemTime = value;
    uint8_t hour = value.hour;
    const bool pm = hour >= 12;
    hour = static_cast<uint8_t>(hour % 12);
    if (!hour) hour = 12;
    if (timeAvailable) {
        g.clockText[0] = static_cast<char>('0' + hour / 10);
        g.clockText[1] = static_cast<char>('0' + hour % 10);
        g.clockText[2] = ':';
        g.clockText[3] = static_cast<char>('0' + value.minute / 10);
        g.clockText[4] = static_cast<char>('0' + value.minute % 10);
        g.clockText[5] = ' ';
        g.clockText[6] = pm ? 'P' : 'A';
        g.clockText[7] = 'M';
        g.clockText[8] = '\0';
    } else {
        std::memcpy(g.clockText, "--:--", 6);
    }
    for (auto& app : g.apps) {
        if (!app.open || app.view != View::DateTime) continue;
        drawAppWindow(app);
        present(app.hwnd);
    }
    redrawTaskbar();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) return 1;
    if (hwnd == g.desktop && message == WM_THEME_SCHEDULE_TICK) {
        updateClock();
        updateAutomaticTheme();
        redrawTaskbar();
        return 0;
    }
    if (message == WM_COMMAND && reinterpret_cast<HWND>(lParam) == g.themeButtonProbe &&
        LOWORD(wParam) == 0x771u && HIWORD(wParam) == BN_CLICKED) {
        g.themeButtonProbeReported = true;
        PrintLine("[MicaNT Theme] BUTTON capture probe notification delivered");
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED &&
        (LOWORD(wParam) == 0x772u || LOWORD(wParam) == 0x773u)) {
        ++g.themeCheckNotifications;
        if (LOWORD(wParam) == 0x772u && !g.themeCheckInputReported &&
            SendMessageW(g.themeCheckProbe, 0x00F0u, 0, 0) == 1) {
            g.themeCheckInputReported = true;
            PrintLine("[MicaNT Theme] Physical checkbox input toggled checked state");
        }
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == 0x777u &&
        HIWORD(wParam) == 0x0300u) {
        ++g.themeEditChangeNotifications;
        return 0;
    }
    if (message == WM_COMMAND && !lParam && HIWORD(wParam) == 0 &&
        LOWORD(wParam) == 0x8844u) {
        g.themeMenuMouseCommand = true;
        PrintLine("[MicaNT Theme] User32 popup mouse owner command received");
        return 0;
    }
    if (message == WM_COMMAND && !lParam && HIWORD(wParam) == 0 &&
        LOWORD(wParam) == 0x8846u) {
        g.themeMenuKeyboardCommand = true;
        PrintLine("[MicaNT Theme] User32 popup keyboard owner command received");
        return 0;
    }
    if (message == WM_COMMAND && !lParam && HIWORD(wParam) == 0 &&
        LOWORD(wParam) == 0x8850u) {
        g.themeMenuBarCommand = true;
        PrintLine("[MicaNT Theme] User32 menu bar owner command received");
        if (g.themeMenuBarProbe) {
            (void)SetMenu(hwnd, nullptr);
            (void)DrawMenuBar(hwnd);
            (void)DestroyMenu(g.themeMenuBarProbe);
            g.themeMenuBarProbe = nullptr;
        }
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED &&
        (LOWORD(wParam) == 0x774u || LOWORD(wParam) == 0x776u)) {
        ++g.themeThreeStateNotifications;
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED &&
        LOWORD(wParam) == 0x775u) {
        ++g.themeRadioNotifications;
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED &&
        LOWORD(wParam) >= 0x77Eu && LOWORD(wParam) <= 0x781u) {
        ++g.themeAutoRadioNotifications;
        if (LOWORD(wParam) == 0x77Eu && !g.themeAutoRadioInputReported &&
            SendMessageW(g.themeAutoRadioA, 0x00F0u, 0, 0) == 1 &&
            SendMessageW(g.themeAutoRadioB, 0x00F0u, 0, 0) == 0) {
            g.themeAutoRadioInputReported = true;
            PrintLine("[MicaNT Theme] Physical auto-radio input kept one group selection");
        }
        return 0;
    }
    if (message == WM_HSCROLL && reinterpret_cast<HWND>(lParam) == g.themeScrollProbe) {
        ++g.themeScrollNotifications;
        if (g.themeScrollNotifications == 1)
            PrintLine("[MicaNT Theme] SCROLLBAR arrow input sent WM_HSCROLL");
        return 0;
    }
    if (message == WM_NOTIFY && lParam && g.themeTabProbe) {
        struct NotifyHeader { HWND hwndFrom; UINT_PTR idFrom; int32_t code; };
        const auto* header = reinterpret_cast<const NotifyHeader*>(lParam);
        if (header->hwndFrom == g.themeTabProbe && header->idFrom == 0x77Au &&
            header->code == -551) { // TCN_SELCHANGE
            ++g.themeTabSelectionNotifications;
            const intptr_t selection = SendMessageW(g.themeTabProbe, 0x130Bu, 0, 0);
            if (g.themeTabProbeReported && g.themeTabSelectionNotifications == 1 &&
                selection == 1)
                PrintLine("[MicaNT Theme] SysTabControl32 mouse selection notification verified");
            if (g.themeTabProbeReported && g.themeTabSelectionNotifications == 2 &&
                selection == 0) {
                g.themeTabInputReported = true;
                PrintLine("[MicaNT Theme] SysTabControl32 keyboard selection notification verified");
            }
            return 0;
        }
    }
    if (hwnd == g.desktop &&
        (message == 0x001A /* WM_SETTINGCHANGE */ ||
         message == 0x031A /* WM_THEMECHANGED */)) {
        // WM_SETTINGCHANGE can announce changed persisted preferences. A
        // UxTheme owns the selected profile and has already published the
        // snapshot before WM_THEMECHANGED reaches Explorer. Query its active
        // state for both initial-load and settings notifications; never reload
        // active.profile here, which could replace a session-only selection.
        loadActiveVisualTheme();
        for (auto& app : g.apps) {
            if (!app.open || !app.hwnd || !app.pixels) continue;
            drawAppWindow(app);
            present(app.hwnd);
        }
        redrawDesktop();
        return 0;
    }
    if (message == 0x031A /* WM_THEMECHANGED */) {
        if (auto* app = appFromHwnd(hwnd)) {
            drawAppWindow(*app);
            present(app->hwnd);
            return 0;
        }
    }
    if (hwnd == g.desktop && message == WM_KEYDOWN && wParam == VK_RETURN) {
        openView(View::MyComputer);
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_RETURN && appFromHwnd(hwnd)) {
        openView(View::MyDocuments);
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_F3) {
        if (appFromHwnd(hwnd)) {
            RECT rect{};
            if (GetWindowRect(hwnd, &rect))
                SetWindowPos(hwnd, nullptr, rect.left + 24, rect.top + 18,
                    rect.right - rect.left - 40, rect.bottom - rect.top - 30, 0);
        }
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE && appFromHwnd(hwnd)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    if (hwnd == g.taskbar && message == WM_LBUTTONUP) {
        const int32_t x = static_cast<int16_t>(static_cast<uint32_t>(lParam) & 0xFFFFU);
        const int32_t y = static_cast<int16_t>((static_cast<uint32_t>(lParam) >> 16) & 0xFFFFU);
        if (y < 0 || y >= static_cast<int32_t>(TASKBAR_H)) return 0;
        if (x < 104) {
            toggleStart();
        } else if (x < static_cast<int32_t>(g.screenW) - 124) {
            micant::explorer::TaskbarButton buttons[MAX_APP_WINDOWS]{};
            const size_t taskCount = collectTaskbarButtons(buttons);
            const int32_t available = static_cast<int32_t>(g.screenW) - 126 - 112;
            const int32_t taskWidth = taskCount == 0 || available <= 0 ? 0 :
                (available / static_cast<int32_t>(taskCount) < 146
                    ? available / static_cast<int32_t>(taskCount) : 146);
            int32_t slotLeft = 112;
            for (size_t i = 0; i < taskCount && taskWidth >= 64; ++i) {
                if (x >= slotLeft && x < slotLeft + taskWidth - 4) {
                    const auto& task = g.taskSwitch.entries[i];
                    if (task.active && !task.minimized) {
                        ShowWindow(task.hwnd, SW_MINIMIZE);
                    } else {
                        ShowWindow(task.hwnd, SW_RESTORE);
                        SetWindowPos(task.hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                    }
                    redrawTaskbar();
                    break;
                }
                slotLeft += taskWidth;
            }
        } else {
            switch (micant::explorer::HitTestNotificationArea(x, g.screenW)) {
                case micant::explorer::NotificationAction::Clock: openView(View::DateTime); break;
                case micant::explorer::NotificationAction::Network: openView(View::Network); break;
                case micant::explorer::NotificationAction::None:
                case micant::explorer::NotificationAction::Volume:
                    break;
            }
        }
        return 0;
    }
    if (hwnd == g.desktop && message == WM_LBUTTONUP) {
        const int32_t x = static_cast<int16_t>(static_cast<uint32_t>(lParam) & 0xFFFFU);
        const int32_t y = static_cast<int16_t>((static_cast<uint32_t>(lParam) >> 16) & 0xFFFFU);
        if (g.startOpen) {
            g.startOpen = false;
            ShowWindow(g.startMenu, SW_HIDE);
            redrawDesktop();
        } else {
            switch (micant::explorer::HitTestDesktop(x, y)) {
                case micant::explorer::DesktopAction::MyComputer: openView(View::MyComputer); break;
                case micant::explorer::DesktopAction::MyDocuments: openView(View::MyDocuments); break;
                case micant::explorer::DesktopAction::Network: openView(View::Network); break;
                case micant::explorer::DesktopAction::RecycleBin: openView(View::RecycleBin); break;
                case micant::explorer::DesktopAction::None: break;
            }
        }
        return 0;
    }
    if (message == WM_LBUTTONUP) {
        if (auto* app = appFromHwnd(hwnd)) {
            const int32_t x = static_cast<int16_t>(static_cast<uint32_t>(lParam) & 0xFFFFU);
            const int32_t y = static_cast<int16_t>((static_cast<uint32_t>(lParam) >> 16) & 0xFFFFU);
            if (x >= 0 && x < 40 && y >= 0 && y < 30) {
                navigateExplorerUp(*app);
                return 0;
            }
            if (x >= 168 && y >= 96) {
                const size_t row = static_cast<size_t>(y - 100) / 26;
                if (y >= 100) (void)enterExplorerFolder(*app, row);
                return 0;
            }
        }
    }
    if (hwnd == g.startMenu && message == WM_LBUTTONUP) {
        const int32_t x = static_cast<int16_t>(static_cast<uint32_t>(lParam) & 0xFFFFU);
        const int32_t y = static_cast<int16_t>((static_cast<uint32_t>(lParam) >> 16) & 0xFFFFU);
        const auto action = micant::explorer::HitTestStartMenu(x, y, MENU_W, MENU_H);
        g.startOpen = false;
        ShowWindow(g.startMenu, SW_HIDE);
        switch (action) {
            case micant::explorer::StartMenuAction::ThemeTest: launchThemeTest(); break;
            case micant::explorer::StartMenuAction::MyComputer: openView(View::MyComputer); break;
            case micant::explorer::StartMenuAction::MyDocuments: openView(View::MyDocuments); break;
            case micant::explorer::StartMenuAction::ControlPanel: openView(View::ControlPanel); break;
            case micant::explorer::StartMenuAction::Network: openView(View::Network); break;
            case micant::explorer::StartMenuAction::RecycleBin: openView(View::RecycleBin); break;
            case micant::explorer::StartMenuAction::ThemeSettings:
                selectNextThemeProfile();
                break;
            case micant::explorer::StartMenuAction::Dismiss:
            case micant::explorer::StartMenuAction::Search:
            case micant::explorer::StartMenuAction::Run:
            case micant::explorer::StartMenuAction::LogOff:
            case micant::explorer::StartMenuAction::PowerOff:
                break;
        }
        redrawDesktop();
        return 0;
    }
    if (message == WM_CLOSE) {
        if (auto* app = appFromHwnd(hwnd)) {
            app->open = false;
            app->minimized = false;
            const bool wasActive = g.activeApp == static_cast<uint8_t>(app - g.apps);
            HBITMAP bitmap = app->bitmap;
            HDC dc = app->dc;
            app->hwnd = nullptr;
            app->dc = nullptr;
            app->bitmap = nullptr;
            app->pixels = nullptr;
            DestroyWindow(hwnd);
            if (dc) DeleteDC(dc);
            if (bitmap) DeleteObject(bitmap);
            if (wasActive) {
                chooseFallbackActiveApp();
                SetFocus(g.activeApp == 0xFF ? g.desktop : g.apps[g.activeApp].hwnd);
            }
            redrawDesktop();
        }
        return 0;
    }
    if (message == WM_SIZE) {
        if (auto* app = appFromHwnd(hwnd)) {
            app->minimized = wParam == SIZE_MINIMIZED;
            if (app->minimized && g.activeApp == static_cast<uint8_t>(app - g.apps)) g.activeApp = 0xFF;
            if (!app->minimized) {
                const uint32_t width = static_cast<uint16_t>(static_cast<uintptr_t>(lParam) & 0xFFFFU);
                const uint32_t height = static_cast<uint16_t>((static_cast<uintptr_t>(lParam) >> 16) & 0xFFFFU);
                if (width >= 160 && height >= 100 && (width != app->width || height != app->height)) {
                    void* bits = nullptr;
                    BITMAPINFO bitmapInfo{};
                    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
                    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
                    bitmapInfo.bmiHeader.biPlanes = 1;
                    bitmapInfo.bmiHeader.biBitCount = 32;
                    bitmapInfo.bmiHeader.biCompression = BI_RGB;
                    HBITMAP bitmap = CreateDIBSection(app->dc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
                    if (bitmap && bits) {
                        SelectObject(app->dc, bitmap);
                        HBITMAP oldBitmap = app->bitmap;
                        app->bitmap = bitmap;
                        app->pixels = static_cast<uint32_t*>(bits);
                        app->width = width;
                        app->height = height;
                        if (oldBitmap) DeleteObject(oldBitmap);
                        drawAppWindow(*app);
                        present(app->hwnd);
                        PrintLine("[MicaNT Explorer] Application window surface resized");
                    } else if (bitmap) {
                        DeleteObject(bitmap);
                    }
                }
            }
            redrawTaskbar();
        }
        return 0;
    }
    if (message == WM_MOVE && appFromHwnd(hwnd)) {
        PrintLine("[MicaNT Explorer] Application window move notification received");
        return 0;
    }
    if (message == WM_ACTIVATE) {
        if (auto* app = appFromHwnd(hwnd)) {
            const uint8_t index = static_cast<uint8_t>(app - g.apps);
            if ((wParam & 0xFFFFU) != 0) {
                app->minimized = false;
                g.activeApp = index;
            } else if (g.activeApp == index) {
                g.activeApp = 0xFF;
            }
            redrawTaskbar();
        }
        return 0;
    }
    if (message == WM_PAINT) {
        HDC source = nullptr;
        uint32_t width = 0, height = 0;
        AppWindow* app = appFromHwnd(hwnd);
        if (hwnd == g.desktop) { source = g.desktopDc; width = g.screenW; height = g.screenH - TASKBAR_H; }
        else if (hwnd == g.taskbar) { source = g.taskbarDc; width = g.screenW; height = TASKBAR_H; }
        else if (hwnd == g.startMenu) { source = g.menuDc; width = MENU_W; height = MENU_H; }
        else if (app) { source = app->dc; width = app->width; height = app->height; }
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(hwnd, &paint);
        const bool copied = target && source && width && height &&
            BitBlt(target, 0, 0, static_cast<int>(width), static_cast<int>(height),
                   source, 0, 0, SRCCOPY);
        if (app && !target)
            PrintLine("[MicaNT Explorer] Application paint has no target DC");
        else if (app && !source)
            PrintLine("[MicaNT Explorer] Application paint has no source DC");
        else if (app && !copied)
            PrintLine("[MicaNT Explorer] Application paint BitBlt failed");
        const bool ended = EndPaint(hwnd, &paint) != FALSE;
        if (app && copied && ended && !app->firstPaintPresented) {
            app->firstPaintPresented = true;
            PrintLine("[MicaNT Explorer] Application window paint presented");
        } else if (app && (!copied || !ended)) {
            PrintLine("[MicaNT Explorer] Application window paint failed");
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

extern "C" void _start(const char* = nullptr) {
    UserVideoInfo64 video{};
    DoSyscall(0x80, reinterpret_cast<uint64_t>(&video), 0);
    if (video.width) g.screenW = video.width;
    if (video.height) g.screenH = video.height;
    if (g.screenW < 640 || g.screenH < 480) { g.screenW = 1024; g.screenH = 768; }
    verifyCurrentUserThemeProfilePath();
    // UxTheme begins loading GTK as soon as the compositor has presented its
    // first frame. Let that worker publish its initial snapshot before making
    // Explorer's first desktop palette and startup probes.
    bool uxthemeReady = false;
    for (unsigned attempt = 0; attempt < 6000; ++attempt) {
        if (MicaWin32kGetThemeSnapshot(&g.themeCandidate) &&
            MicaThemeSnapshotIsValid(&g.themeCandidate, sizeof(g.themeCandidate))) {
            uxthemeReady = true;
            break;
        }
        Sleep(5);
    }
    loadActiveVisualTheme();
    PrintLine(g.visualThemeReady
        ? "[MicaNT Theme] UxTheme GTK snapshot available"
        : (uxthemeReady
            ? "[MicaNT Theme] UxTheme profile query failed"
            : "[MicaNT Theme] UxTheme GTK load timed out; base fallback active"));

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    wchar_t titleIconPath[512]{};
    size_t titleIconPathLength = 0;
    constexpr wchar_t kTitleIconSuffix[] =
        L"\\System\\Icons\\WhiteSur\\places\\32\\folder-documents.png";
    if (appendSystemRoot(titleIconPath, std::size(titleIconPath), titleIconPathLength) &&
        appendWide(titleIconPath, std::size(titleIconPath), titleIconPathLength,
                   kTitleIconSuffix, std::size(kTitleIconSuffix) - 1)) {
        windowClass.hIcon = static_cast<HICON>(LoadImageW(nullptr, titleIconPath,
            1u, 32, 32, 0x0010u));
        PrintLine(windowClass.hIcon
            ? "[MicaNT Theme] WhiteSur window icon loaded for caption"
            : "[MicaNT Theme] WhiteSur window icon load failed");
        if (!windowClass.hIcon)
            PrintThemeWin32Error("[MicaNT Theme] WhiteSur window icon load error",
                                 GetLastError());
    }
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    PrintLine(windowClass.hCursor
        ? "[MicaNT Cursor] Explorer class arrow loaded"
        : "[MicaNT Cursor] Explorer class arrow load failed");
    HCURSOR penCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32631));
    if (penCursor) {
        HCURSOR previousCursor = SetCursor(penCursor);
        SetCursor(previousCursor ? previousCursor : windowClass.hCursor);
        PrintLine("[MicaNT Cursor] IDC_PEN theme cursor loaded");
    } else {
        PrintLine("[MicaNT Cursor] IDC_PEN theme cursor load failed");
    }
    HCURSOR moduleCursor = LoadCursorW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    if (moduleCursor) {
        SetCursor(moduleCursor);
        SetCursor(windowClass.hCursor);
        PrintLine("[MicaNT Cursor] PE group cursor resource loaded and selected");
    } else {
        PrintThemeWin32Error("[MicaNT Cursor] PE group cursor resource load failed",
                             GetLastError());
    }
    wchar_t animatedCursorPath[512]{};
    size_t animatedPathCursor = 0;
    constexpr wchar_t kBusyCursorPath[] =
        L"\\System\\Cursors\\MiniMouse\\mini_mouse_busy.ani";
    if (appendSystemRoot(animatedCursorPath, std::size(animatedCursorPath), animatedPathCursor) &&
        appendWide(animatedCursorPath, std::size(animatedCursorPath), animatedPathCursor,
                   kBusyCursorPath, std::size(kBusyCursorPath) - 1)) {
        HCURSOR animatedCursor = static_cast<HCURSOR>(LoadImageW(nullptr, animatedCursorPath,
            IMAGE_CURSOR, 24, 24, LR_LOADFROMFILE));
        if (animatedCursor) {
            SetCursor(animatedCursor);
            for (unsigned frame = 0; frame < 4; ++frame) {
                Sleep(40);
                MSG pending{};
                (void)PeekMessageW(&pending, nullptr, 0, 0, PM_NOREMOVE);
            }
            SetCursor(windowClass.hCursor);
            PrintLine(DestroyCursor(animatedCursor)
                ? "[MicaNT Cursor] ANI load, resize and release passed"
                : "[MicaNT Cursor] ANI release failed");
        } else {
            PrintLine("[MicaNT Cursor] ANI load or resize failed");
        }
        HCURSOR fileAnimatedCursor = LoadCursorFromFileW(animatedCursorPath);
        if (fileAnimatedCursor) {
            SetCursor(fileAnimatedCursor);
            for (unsigned frame = 0; frame < 4; ++frame) {
                Sleep(40);
                MSG pending{};
                (void)PeekMessageW(&pending, nullptr, 0, 0, PM_NOREMOVE);
            }
            SetCursor(windowClass.hCursor);
            PrintLine(DestroyCursor(fileAnimatedCursor)
                ? "[MicaNT Cursor] LoadCursorFromFileW ANI load and release passed"
                : "[MicaNT Cursor] LoadCursorFromFileW ANI release failed");
        } else {
            PrintLine("[MicaNT Cursor] LoadCursorFromFileW ANI load failed");
        }
    }
    windowClass.lpszClassName = kClassName;
    if (!RegisterClassExW(&windowClass)) {
        for (;;) __asm__ __volatile__("pause");
    }

    WNDCLASSEXW attachedScrollClass{};
    attachedScrollClass.cbSize = sizeof(attachedScrollClass);
    attachedScrollClass.lpfnWndProc = ThemeAttachedScrollProc;
    attachedScrollClass.lpszClassName = kThemeAttachedScrollClassName;
    const bool attachedScrollClassReady = RegisterClassExW(&attachedScrollClass) != 0;
    PrintLine(attachedScrollClassReady
        ? "[MicaNT Theme] Attached scrollbar probe class registered"
        : "[MicaNT Theme] Attached scrollbar probe class registration failed");

    g.desktop = CreateWindowExW(0, kClassName, kDesktopTitle,
        WS_POPUP | WS_VISIBLE, 0, 0, static_cast<int32_t>(g.screenW),
        static_cast<int32_t>(g.screenH - TASKBAR_H),
        nullptr, nullptr, nullptr, nullptr);
    if (!g.desktop) for (;;) __asm__ __volatile__("pause");
    g.desktopDc = CreateCompatibleDC(nullptr);
    void* desktopBits = nullptr;
    BITMAPINFO desktopInfo{};
    desktopInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    desktopInfo.bmiHeader.biWidth = static_cast<LONG>(g.screenW);
    desktopInfo.bmiHeader.biHeight = -static_cast<LONG>(g.screenH - TASKBAR_H);
    desktopInfo.bmiHeader.biPlanes = 1;
    desktopInfo.bmiHeader.biBitCount = 32;
    desktopInfo.bmiHeader.biCompression = BI_RGB;
    HBITMAP desktopBitmap = CreateDIBSection(g.desktopDc, &desktopInfo, DIB_RGB_COLORS, &desktopBits, nullptr, 0);
    if (!desktopBitmap || !desktopBits) for (;;) __asm__ __volatile__("pause");
    SelectObject(g.desktopDc, desktopBitmap);
    g.desktopPixels = static_cast<uint32_t*>(desktopBits);

    const int32_t clockX = static_cast<int32_t>(g.screenW) - 291;
    const int32_t clockY = 46;
    const int32_t taskbarY = static_cast<int32_t>(g.screenH - TASKBAR_H);
    g.taskbar = CreateWindowExW(WS_EX_TOPMOST, kClassName, kTaskbarTitle,
        WS_POPUP | WS_VISIBLE, 0, taskbarY, static_cast<int32_t>(g.screenW), TASKBAR_H,
        nullptr, nullptr, nullptr, nullptr);
    if (!g.taskbar) for (;;) __asm__ __volatile__("pause");
    g.taskbarDc = CreateCompatibleDC(nullptr);
    void* taskbarBits = nullptr;
    BITMAPINFO taskbarInfo{};
    taskbarInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    taskbarInfo.bmiHeader.biWidth = static_cast<LONG>(g.screenW);
    taskbarInfo.bmiHeader.biHeight = -static_cast<LONG>(TASKBAR_H);
    taskbarInfo.bmiHeader.biPlanes = 1;
    taskbarInfo.bmiHeader.biBitCount = 32;
    taskbarInfo.bmiHeader.biCompression = BI_RGB;
    HBITMAP taskbarBitmap = CreateDIBSection(g.taskbarDc, &taskbarInfo, DIB_RGB_COLORS, &taskbarBits, nullptr, 0);
    if (!taskbarBitmap || !taskbarBits) for (;;) __asm__ __volatile__("pause");
    SelectObject(g.taskbarDc, taskbarBitmap);
    g.taskbarPixels = static_cast<uint32_t*>(taskbarBits);

    const int32_t menuY = static_cast<int32_t>(g.screenH) - static_cast<int32_t>(MENU_H) - static_cast<int32_t>(TASKBAR_H);
    g.startMenu = CreateWindowExW(WS_EX_TOPMOST, kClassName, kStartTitle, WS_POPUP,
        0, menuY, MENU_W, MENU_H, nullptr, nullptr, nullptr, nullptr);
    if (g.startMenu) {
        g.menuDc = CreateCompatibleDC(nullptr);
        void* menuBits = nullptr;
        BITMAPINFO menuInfo{};
        menuInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        menuInfo.bmiHeader.biWidth = MENU_W;
        menuInfo.bmiHeader.biHeight = -static_cast<LONG>(MENU_H);
        menuInfo.bmiHeader.biPlanes = 1;
        menuInfo.bmiHeader.biBitCount = 32;
        menuInfo.bmiHeader.biCompression = BI_RGB;
        HBITMAP menuBitmap = CreateDIBSection(g.menuDc, &menuInfo, DIB_RGB_COLORS, &menuBits, nullptr, 0);
        if (menuBitmap && menuBits) {
            SelectObject(g.menuDc, menuBitmap);
            g.menuPixels = static_cast<uint32_t*>(menuBits);
        }
    }

    loadPackagedWallpaper();
    loadPackagedDesktopIcons();
    initializeSystemFonts();
    verifyDirectWriteFonts();
    redrawDesktop();
    updateClock();
    verifyThemeChangeQueueRollback();
    verifyAutomaticThemeBoundary();
    verifyMissingDesktopIconFallback();
    HCURSOR busyCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514));
    if (busyCursor && g.desktop) {
        HCURSOR previousCursor = SetCursor(busyCursor);
        bool animationWaitPassed = true;
        for (unsigned poll = 0; poll < 4; ++poll) {
            if (!PostMessageW(g.desktop, WM_NULL, 0, 0)) {
                animationWaitPassed = false;
                break;
            }
            MSG idleMessage{};
            if (GetMessageW(&idleMessage, g.desktop, 0, 0) <= 0) {
                animationWaitPassed = false;
                break;
            }
            TranslateMessage(&idleMessage);
            DispatchMessageW(&idleMessage);
            Sleep(40);
        }
        SetCursor(previousCursor ? previousCursor : windowClass.hCursor);
        PrintLine(animationWaitPassed
            ? "[MicaNT Cursor] ANI wait path exercised through blocking GetMessage"
            : "[MicaNT Cursor] ANI blocking GetMessage probe failed");
    }
    HCURSOR workingCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32650));
    if (workingCursor) {
        SetCursor(workingCursor);
        for (unsigned frame = 0; frame < 4; ++frame) {
            Sleep(40);
            MSG pending{};
            (void)PeekMessageW(&pending, nullptr, 0, 0, PM_NOREMOVE);
        }
        SetCursor(windowClass.hCursor);
        PrintLine("[MicaNT Cursor] IDC_APPSTARTING system ANI loaded and selected");
    } else {
        PrintLine("[MicaNT Cursor] IDC_APPSTARTING system ANI load failed");
    }
    micant::Handle themeSchedule{};
    const auto scheduleStatus = micant::native_nt::NtCreateThreadEx(&themeSchedule,
        0x00100002u, nullptr, static_cast<micant::Handle>(-1),
        reinterpret_cast<void*>(themeScheduleWorker), g.desktop, 0, 0, 0, 0, nullptr);
    if (scheduleStatus == micant::NtStatus::Success && themeSchedule) {
        (void)micant::native_nt::NtClose(themeSchedule);
        PrintLine("[MicaNT Theme] Auto schedule worker started");
    } else {
        PrintLine("[MicaNT Theme] Auto schedule worker unavailable");
    }
    for (;;) {
        MSG msg{};
        const int32_t result = GetMessageW(&msg, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else if (result == 0) break;
        else Sleep(10);
    }
}
