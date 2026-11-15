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
#include "micant/shared/theme_snapshot.h"
#include "micant/shared/native_uxtheme_api.h"
#include "gui_common.hpp"
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
constexpr uint32_t MENU_H = 390;
constexpr size_t MAX_APP_WINDOWS = 8;

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
    bool visualThemeReady{};
    char themeProfile[96]{};
    bool themeGdiDrawReported{};
    bool themeGdiEditDrawReported{};
    bool themeTextDrawReported{};
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
    const uint32_t accent = color(13, MICANT_THEME_HAS_BACKGROUND, rgb(36, 91, 174));
    const uint32_t accentForeground = color(13, MICANT_THEME_HAS_FOREGROUND, rgb(255, 255, 255));
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
    const uint32_t rowForeground = windowStyle &&
        (windowStyle->flags & MICANT_THEME_HAS_FOREGROUND)
        ? themePixel(windowStyle->foreground) : entryForeground;
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
        else fill(p, w, h, 174, y, 16, 16, rgb(236, 190, 67));
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
        if (!g.explorerRowsThemePixelReported && i == 0 && app.view == View::ControlPanel) {
            const size_t right = w < 360 ? w : 360;
            bool themedInkFound = false;
            for (size_t rowY = static_cast<size_t>(y + 4);
                 rowY < static_cast<size_t>(y + 12) && rowY < h && !themedInkFound;
                 ++rowY) {
                for (size_t column = 199; column < right; ++column) {
                    if (p[rowY * w + column] == rowForeground) {
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
        button.iconColor = rgb(236, 190, 67);
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
                                  g.wallpaperPixels, &icons);
    if (g.fontReady) micant_gui::DrawStringClipped(g.desktopPixels, g.screenW,
        g.screenH - TASKBAR_H, 420, 48, "MicaNT — Città, perché? e\xCC\x81 Ĉ ƀ", rgb(255, 255, 255));

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
    const bool hadPrevious = g.visualThemeReady;
    const MicaThemeSnapshot previousTheme = g.visualTheme;
    const auto restorePreviousSnapshot = [&]() noexcept {
        if (hadPrevious) (void)MicaWin32kSetThemeSnapshot(&previousTheme);
    };
    wchar_t path[512]{};
    size_t cursor = 0, profileBytes = 0;
    char profile[96]{};
    if (selectedProfile) {
        while (selectedProfile[profileBytes] && profileBytes < sizeof(profile) - 1) {
            profile[profileBytes] = selectedProfile[profileBytes];
            ++profileBytes;
        }
        if (selectedProfile[profileBytes]) return;
    } else {
        if (!appendSystemRoot(path, std::size(path), cursor) ||
            !appendWide(path, std::size(path), cursor,
                        L"\\System\\Themes\\active.profile", 29) ||
            !readBoundedFile(path, profile, sizeof(profile) - 1, profileBytes)) {
            PrintLine("[MicaNT Theme] Active profile could not be read");
            return;
        }
    }
    while (profileBytes && (profile[profileBytes - 1] == '\r' ||
                            profile[profileBytes - 1] == '\n' ||
                            profile[profileBytes - 1] == ' ' ||
                            profile[profileBytes - 1] == '\t')) --profileBytes;
    size_t first = 0;
    while (first < profileBytes && (profile[first] == ' ' || profile[first] == '\t')) ++first;
    if (first) {
        for (size_t i = first; i < profileBytes; ++i) profile[i - first] = profile[i];
        profileBytes -= first;
    }
    if (!profileBytes || profileBytes > 80) {
        PrintLine("[MicaNT Theme] Active profile length rejected");
        return;
    }
    size_t separators = 0;
    for (size_t i = 0; i < profileBytes; ++i) {
        const char c = profile[i];
        if (c == '/') { ++separators; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            PrintLine("[MicaNT Theme] Active profile path rejected");
            return;
        }
    }
    if (separators != 1) {
        PrintLine("[MicaNT Theme] Active profile format rejected");
        return;
    }
    cursor = 0;
    if (!appendSystemRoot(path, std::size(path), cursor) ||
        !appendWide(path, std::size(path), cursor, L"\\System\\Themes\\", 15)) return;
    for (size_t i = 0; i < profileBytes; ++i) {
        if (cursor + 1 >= std::size(path)) return;
        path[cursor++] = profile[i] == '/' ? L'\\' : static_cast<wchar_t>(profile[i]);
    }
    if (!appendWide(path, std::size(path), cursor, L"\\native.style", 13)) return;
    MicaThemeSnapshot candidate{};
    size_t styleBytes = 0;
    if (!readBoundedFile(path, &candidate, sizeof(candidate), styleBytes)) {
        PrintLine("[MicaNT Theme] Compiled style snapshot could not be read");
        return;
    }
    if (!MicaThemeSnapshotIsValid(&candidate, styleBytes)) {
        PrintLine("[MicaNT Theme] Compiled style snapshot rejected");
        return;
    }
    if (!MicaWin32kSetThemeSnapshot(&candidate)) {
        PrintLine("[MicaNT Theme] Win32k rejected the style snapshot");
        return;
    }
    MicaThemeSnapshot published{};
    if (!MicaWin32kGetThemeSnapshot(&published) ||
        std::memcmp(&candidate, &published, sizeof(candidate)) != 0) {
        restorePreviousSnapshot();
        PrintLine("[MicaNT Theme] Win32k theme snapshot round-trip failed");
        return;
    }
    const auto& button = candidate.styles[0];
    uint32_t queriedColor{};
    const uint32_t packedColor = button.background;
    const uint32_t expectedColor = ((packedColor >> 16) & 0xFFu) |
        (packedColor & 0xFF00u) | ((packedColor & 0xFFu) << 16);
    MicaThemeHandle themeHandle = IsThemeActive()
        ? OpenThemeData(nullptr, L"BUTTON") : nullptr;
    const uint32_t invalidSystemColor = 0xFFFFFFFFu;
    const bool systemColorQueryPassed = themeHandle &&
        GetThemeSysColor(themeHandle, 15) == expectedColor &&
        GetThemeSysColor(nullptr, 15) == expectedColor &&
        GetThemeSysColor(themeHandle, 0x7FFFFFFF) == invalidSystemColor;
    const bool queried = themeHandle && (button.flags & MICANT_THEME_HAS_BACKGROUND) &&
        GetThemeColor(themeHandle, 1, 1, 3802, &queriedColor) == 0 &&
        queriedColor == expectedColor;
    uint32_t buttonAlpha = button.background >> 24;
    if (button.flags & MICANT_THEME_HAS_OPACITY)
        buttonAlpha = (buttonAlpha * button.opacity + 127u) / 255u;
    int32_t transparent = static_cast<int32_t>(0xA5A5A5A5u);
    const bool transparentQueryPassed = themeHandle &&
        GetThemeBool(themeHandle, 1, 1, 2203, &transparent) == 0 &&
        transparent == (buttonAlpha < 255u ? 1 : 0);
    int32_t invalidDrawRect[4]{0, 0, 8, 8};
    const int32_t dispatchFailureStatus = themeHandle
        ? DrawThemeBackground(themeHandle, reinterpret_cast<void*>(1), 1, 1,
                              invalidDrawRect, nullptr) : 0;
    const bool dispatchFailureReported = themeHandle &&
        dispatchFailureStatus == static_cast<int32_t>(0x80004005u);
    int32_t unsupportedOrigin = 0x5A5A5A5A;
    const bool unsupportedOriginFailed = themeHandle &&
        GetThemePropertyOrigin(themeHandle, 1, 1, 4001, &unsupportedOrigin) ==
            static_cast<int32_t>(0x80004001u) && unsupportedOrigin == 0;
    int32_t unsupportedBoolean = 1;
    const bool unsupportedBooleanFailed = themeHandle &&
        GetThemeBool(themeHandle, 1, 1, 0x7FFFFFFF, &unsupportedBoolean) ==
            static_cast<int32_t>(0x80004001u) && unsupportedBoolean == 0;
    int32_t backgroundType = static_cast<int32_t>(0xA5A5A5A5u);
    const bool backgroundTypeQueryPassed = themeHandle &&
        GetThemeEnumValue(themeHandle, 1, 1, 4001, &backgroundType) == 0 &&
        backgroundType == 1;
    int32_t unsupportedEnum = 1;
    const bool unsupportedEnumFailed = themeHandle &&
        GetThemeEnumValue(themeHandle, 1, 1, 0x7FFFFFFF, &unsupportedEnum) ==
            static_cast<int32_t>(0x80004001u) && unsupportedEnum == 0;
    uint8_t themeFontOutput[92];
    std::memset(themeFontOutput, 0xA5, sizeof(themeFontOutput));
    const int32_t unsupportedFont = themeHandle
        ? GetThemeFont(themeHandle, nullptr, 1, 1, 210, themeFontOutput)
        : 0;
    bool themeFontOutputCleared = true;
    for (uint8_t value : themeFontOutput) themeFontOutputCleared &= value == 0;
    MicaThemeMargins rejectedMargins{1, 2, 3, 4};
    const int32_t unsupportedMargin = themeHandle ? GetThemeMargins(themeHandle,
        nullptr, 1, 1, 0x7FFFFFFF, nullptr, &rejectedMargins) : 0;
    const bool closed = themeHandle && CloseThemeData(themeHandle) == 0;
    uint32_t staleColor = 0xA5A5A5A5u;
    const bool staleHandleRejected = closed &&
        GetThemeColor(themeHandle, 1, 1, 3802, &staleColor) ==
            static_cast<int32_t>(0x80070006u) && staleColor == 0;
    MicaThemeHandle capacityHandles[64]{};
    size_t openedHandles = 0;
    for (; openedHandles < std::size(capacityHandles); ++openedHandles) {
        capacityHandles[openedHandles] = OpenThemeData(nullptr, L"BUTTON");
        if (!capacityHandles[openedHandles]) break;
    }
    const bool capacityReached = openedHandles == std::size(capacityHandles);
    const bool capacityFailureReported = capacityReached &&
        OpenThemeData(nullptr, L"BUTTON") == nullptr;
    bool capacityHandlesClosed = true;
    for (size_t index = 0; index < openedHandles; ++index)
        capacityHandlesClosed &= CloseThemeData(capacityHandles[index]) == 0;
    MicaThemeHandle reusedHandle = capacityHandlesClosed
        ? OpenThemeData(nullptr, L"BUTTON") : nullptr;
    const bool capacityRecovered = reusedHandle && CloseThemeData(reusedHandle) == 0;
    if (!queried || !closed) {
        restorePreviousSnapshot();
        PrintLine("[MicaNT Theme] UxTheme DLL handle/query path failed");
        return;
    }
    PrintLine("[MicaNT Theme] UxTheme DLL handle/color query passed");
    PrintLine(systemColorQueryPassed
        ? "[MicaNT Theme] GetThemeSysColor resolves themed button face"
        : "[MicaNT Theme] GetThemeSysColor result mismatch");
    if (staleHandleRejected)
        PrintLine("[MicaNT Theme] Closed UxTheme handle rejected and output cleared");
    else
        PrintLine("[MicaNT Theme] Closed UxTheme handle remained usable");
    if (capacityFailureReported && capacityHandlesClosed && capacityRecovered)
        PrintLine("[MicaNT Theme] UxTheme handle exhaustion fails and recovers cleanly");
    else
        PrintLine("[MicaNT Theme] UxTheme handle exhaustion or cleanup mismatch");
    if (transparentQueryPassed && unsupportedBooleanFailed)
        PrintLine("[MicaNT Theme] UxTheme TMT_TRANSPARENT follows alpha and opacity");
    else
        PrintLine("[MicaNT Theme] UxTheme transparent boolean query mismatch");
    if (dispatchFailureReported)
        PrintLine("[MicaNT Theme] UxTheme GDI dispatch failure surfaces E_FAIL");
    else
        PrintLine("[MicaNT Theme] UxTheme GDI dispatch failure status mismatch");
    if (unsupportedOriginFailed)
        PrintLine("[MicaNT Theme] Unsupported property origin clears output");
    else
        PrintLine("[MicaNT Theme] Unsupported property origin output mismatch");
    if (backgroundTypeQueryPassed && unsupportedEnumFailed)
        PrintLine("[MicaNT Theme] UxTheme TMT_BGTYPE reports borderfill");
    else
        PrintLine("[MicaNT Theme] UxTheme background enum query mismatch");
    if (unsupportedFont == static_cast<int32_t>(0x80004001u) && themeFontOutputCleared)
        PrintLine("[MicaNT Theme] Unsupported font query clears LOGFONTW output");
    else
        PrintLine("[MicaNT Theme] Unsupported font query output mismatch");
    if (unsupportedMargin == static_cast<int32_t>(0x80070490u) &&
        rejectedMargins.cxLeftWidth == 0 && rejectedMargins.cxRightWidth == 0 &&
        rejectedMargins.cyTopHeight == 0 && rejectedMargins.cyBottomHeight == 0)
        PrintLine("[MicaNT Theme] UxTheme failed margin query clears its output");
    else
        PrintLine("[MicaNT Theme] UxTheme failed margin query output mismatch");
    struct ThemeRect { int32_t left, top, right, bottom; };
    uint32_t rejectedColor = 0xA5A5A5A5u;
    int32_t rejectedSize[2]{static_cast<int32_t>(0xA5A5A5A5u),
                            static_cast<int32_t>(0xA5A5A5A5u)};
    int32_t rejectedInt = static_cast<int32_t>(0xA5A5A5A5u);
    ThemeRect inputRect{0, 0, 100, 40};
    ThemeRect rejectedContent{1, 2, 3, 4};
    ThemeRect rejectedExtent{1, 2, 3, 4};
    const int32_t invalidTheme = static_cast<int32_t>(0x80070006u);
    const bool queryFailuresClearOutputs =
        GetThemeColor(nullptr, 1, 1, 3802, &rejectedColor) == invalidTheme &&
        rejectedColor == 0 &&
        GetThemePartSize(nullptr, nullptr, 1, 1, nullptr, 0, rejectedSize) == invalidTheme &&
        rejectedSize[0] == 0 && rejectedSize[1] == 0 &&
        GetThemeInt(nullptr, 1, 1, 2403, &rejectedInt) == invalidTheme && rejectedInt == 0 &&
        GetThemeBackgroundContentRect(nullptr, nullptr, 1, 1, &inputRect,
                                      &rejectedContent) == invalidTheme &&
        rejectedContent.left == 0 && rejectedContent.top == 0 &&
        rejectedContent.right == 0 && rejectedContent.bottom == 0 &&
        GetThemeBackgroundExtent(nullptr, nullptr, 1, 1, &inputRect,
                                &rejectedExtent) == invalidTheme &&
        rejectedExtent.left == 0 && rejectedExtent.top == 0 &&
        rejectedExtent.right == 0 && rejectedExtent.bottom == 0;
    rejectedContent = {1, 2, 3, 4};
    rejectedExtent = {1, 2, 3, 4};
    const int32_t invalidArgument = static_cast<int32_t>(0x80070057u);
    const bool invalidRectQueriesClearOutputs =
        GetThemeBackgroundContentRect(nullptr, nullptr, 1, 1, nullptr,
                                      &rejectedContent) == invalidArgument &&
        rejectedContent.left == 0 && rejectedContent.top == 0 &&
        rejectedContent.right == 0 && rejectedContent.bottom == 0 &&
        GetThemeBackgroundExtent(nullptr, nullptr, 1, 1, nullptr,
                                &rejectedExtent) == invalidArgument &&
        rejectedExtent.left == 0 && rejectedExtent.top == 0 &&
        rejectedExtent.right == 0 && rejectedExtent.bottom == 0;
    if (queryFailuresClearOutputs && invalidRectQueriesClearOutputs)
        PrintLine("[MicaNT Theme] Failed UxTheme color, size, int and rect queries clear outputs");
    else
        PrintLine("[MicaNT Theme] Failed UxTheme query output contract mismatch");
    void* animationFromDc = reinterpret_cast<void*>(1);
    void* animationToDc = reinterpret_cast<void*>(1);
    const auto animationBuffer = BeginBufferedAnimation(nullptr, nullptr, nullptr, 0,
        nullptr, nullptr, &animationFromDc, &animationToDc);
    const int32_t unsupportedAnimation = EndBufferedAnimation(nullptr, 0);
    const int32_t unsupportedStop = BufferedPaintStopAllAnimations(nullptr);
    if (!animationBuffer && !animationFromDc && !animationToDc &&
        unsupportedAnimation == static_cast<int32_t>(0x80004001u) &&
        unsupportedStop == static_cast<int32_t>(0x80004001u))
        PrintLine("[MicaNT Theme] Buffered animation failure outputs cleared; E_NOTIMPL surfaced");
    else
        PrintLine("[MicaNT Theme] Buffered animation failure contract mismatch");
    g.visualTheme = candidate;
    g.visualThemeReady = true;
    for (size_t i = 0; i < profileBytes; ++i) g.themeProfile[i] = profile[i];
    g.themeProfile[profileBytes] = '\0';
}

void createThemeProgressProbe() noexcept {
    struct InitCommonControlsExArgs { uint32_t size, classes; } args{
        sizeof(InitCommonControlsExArgs), 0x00000020u};
    if (!InitCommonControlsEx(&args)) {
        PrintLine("[MicaNT Theme] comctl32 progress class registration failed");
        return;
    }
    constexpr uint32_t kSetRange32 = 0x0406u;
    constexpr uint32_t kSetPosition = 0x0402u;
    constexpr uint32_t kGetPosition = 0x0408u;
    g.themeProgressProbe = CreateWindowExW(0, L"msctls_progress32", L"",
        WS_POPUP | WS_VISIBLE, 800, 60, 180, 20,
        nullptr, nullptr, nullptr, nullptr);
    if (!g.themeProgressProbe) {
        PrintLine("[MicaNT Theme] comctl32 progress window creation failed");
        return;
    }
    (void)SendMessageW(g.themeProgressProbe, kSetRange32, 0, 100);
    (void)SendMessageW(g.themeProgressProbe, kSetPosition, 50, 0);
    UpdateWindow(g.themeProgressProbe);
    const bool positionPassed =
        SendMessageW(g.themeProgressProbe, kGetPosition, 0, 0) == 50;
    HDC source = GetDC(g.themeProgressProbe);
    HDC target = CreateCompatibleDC(nullptr);
    void* dibBits = nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 180;
    info.bmiHeader.biHeight = -20;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HBITMAP bitmap = target
        ? CreateDIBSection(target, &info, DIB_RGB_COLORS, &dibBits, nullptr, 0) : nullptr;
    void* previousBitmap = target && bitmap ? SelectObject(target, bitmap) : nullptr;
    const bool copied = source && target && bitmap && dibBits &&
        BitBlt(target, 0, 0, 180, 20, source, 0, 0, SRCCOPY);
    const auto* pixels = static_cast<const uint32_t*>(dibBits);
    const auto* trough = themeStyle(7, 0);
    const auto* fill = themeStyle(7, 6);
    const uint32_t expectedTrough = trough &&
        (trough->flags & MICANT_THEME_HAS_BACKGROUND)
        ? themePixel(trough->background, 0xFF000000u) : 0xFF000000u;
    const uint32_t expectedFill = fill &&
        (fill->flags & MICANT_THEME_HAS_BACKGROUND)
        ? themePixel(fill->background, 0xFF000000u) : 0xFF000000u;
    const bool pixelsPassed = copied && trough && fill &&
        (trough->flags & MICANT_THEME_HAS_BACKGROUND) &&
        (fill->flags & MICANT_THEME_HAS_BACKGROUND) &&
        pixels[10u * 180u + 20u] == expectedFill &&
        pixels[10u * 180u + 150u] == expectedTrough;
    if (source) (void)ReleaseDC(g.themeProgressProbe, source);
    if (target && previousBitmap) (void)SelectObject(target, previousBitmap);
    if (target) (void)DeleteDC(target);
    if (bitmap) (void)DeleteObject(bitmap);
    if (positionPassed && pixelsPassed) {
        PrintLine("[MicaNT Theme] Production comctl32 progress control uses WhiteSur pixels");
        g.commonControlsProgressReported = true;
    } else {
        PrintThemeWin32Error("[MicaNT Theme] Progress position", positionPassed);
        PrintThemeWin32Error("[MicaNT Theme] Progress DIB copy", copied);
        PrintThemeWin32Error("[MicaNT Theme] Progress style flags",
            (trough ? trough->flags : 0u) | (fill ? fill->flags << 16 : 0u));
        if (copied && pixels) {
            PrintThemeWin32Error("[MicaNT Theme] Progress fill pixel", pixels[10u * 180u + 20u]);
            PrintThemeWin32Error("[MicaNT Theme] Progress trough pixel", pixels[10u * 180u + 150u]);
        }
        PrintThemeWin32Error("[MicaNT Theme] Progress expected fill", expectedFill);
        PrintThemeWin32Error("[MicaNT Theme] Progress expected trough", expectedTrough);
        PrintLine("[MicaNT Theme] Production comctl32 progress range or pixel check failed");
    }
}

void selectNextThemeProfile() noexcept {
    constexpr const char* profiles[] = {
        "WhiteSur/Light", "WhiteSur/Dark", "WhiteSur/LightPurple"
    };
    size_t current = 0;
    const auto equalProfile = [](const char* left, const char* right) noexcept {
        if (!left || !right) return false;
        size_t i = 0;
        while (i < 96 && left[i] && right[i] && left[i] == right[i]) ++i;
        return i < 96 && left[i] == right[i];
    };
    for (size_t i = 0; i < std::size(profiles); ++i)
        if (equalProfile(g.themeProfile, profiles[i])) current = i;
    const char* next = profiles[(current + 1) % std::size(profiles)];
    const bool hadTheme = g.visualThemeReady;
    const auto oldTheme = g.visualTheme;
    char oldProfile[sizeof(g.themeProfile)]{};
    std::memcpy(oldProfile, g.themeProfile, sizeof(oldProfile));
    loadActiveVisualTheme(next);
    if (!equalProfile(g.themeProfile, next)) {
        g.visualTheme = oldTheme;
        g.visualThemeReady = hadTheme;
        std::memcpy(g.themeProfile, oldProfile, sizeof(oldProfile));
        return;
    }
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
    PrintLine(equalProfile(next, profiles[1])
        ? "[MicaNT Theme] Runtime profile switch to Dark passed"
        : equalProfile(next, profiles[2])
            ? "[MicaNT Theme] Runtime profile switch to Light Purple passed"
            : "[MicaNT Theme] Runtime profile switch to Light passed");
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
                                    IconId::Network, IconId::TrashEmpty};
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
    constexpr uint32_t kInitialWindowWidth = 500;
    constexpr uint32_t kInitialWindowHeight = 320;
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
        DestroyWindow(app->hwnd);
        if (app->dc) DeleteDC(app->dc);
        if (appBitmap) DeleteObject(appBitmap);
        *app = {};
        return;
    }
    drawAppWindow(*app);
    redrawDesktop();
    present(app->hwnd);
    if (view == View::MyComputer && !g.themeButtonProbe) {
        constexpr uintptr_t kThemeButtonId = 0x771u;
        g.themeButtonProbe = CreateWindowExW(0, L"BUTTON", L"Theme button",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 530, 300, 120, 40,
            app->hwnd, reinterpret_cast<HMENU>(kThemeButtonId), nullptr, nullptr);
        if (g.themeButtonProbe) {
            ShowWindow(g.themeButtonProbe, SW_SHOW);
            UpdateWindow(g.themeButtonProbe);
            constexpr UINT kBmGetState = 0x00F2u;
            constexpr UINT kBmClick = 0x00F5u;
            const bool initialState = SendMessageW(g.themeButtonProbe,
                kBmGetState, 0, 0) == 0;
            (void)SendMessageW(g.themeButtonProbe, kBmClick, 0, 0);
            if (initialState && g.themeButtonProbeReported)
                PrintLine("[MicaNT Theme] Production User32 BUTTON painted and BN_CLICKED delivered");
            else
                PrintLine("[MicaNT Theme] Production User32 BUTTON state/notification mismatch");
        } else {
            PrintLine("[MicaNT Theme] Production User32 BUTTON creation failed");
        }
    }
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
    const uint64_t ok = DoSyscall(0x83, reinterpret_cast<uint64_t>(&value));
    if (ok != 1 || value.second == g.clockSecond) return;
    g.clockSecond = value.second;
    g.systemTime = value;
    uint8_t hour = value.hour;
    const bool pm = hour >= 12;
    hour = static_cast<uint8_t>(hour % 12);
    if (!hour) hour = 12;
    g.clockText[0] = static_cast<char>('0' + hour / 10);
    g.clockText[1] = static_cast<char>('0' + hour % 10);
    g.clockText[2] = ':';
    g.clockText[3] = static_cast<char>('0' + value.minute / 10);
    g.clockText[4] = static_cast<char>('0' + value.minute % 10);
    g.clockText[5] = ' ';
    g.clockText[6] = pm ? 'P' : 'A';
    g.clockText[7] = 'M';
    g.clockText[8] = '\0';
    for (auto& app : g.apps) {
        if (!app.open || app.view != View::DateTime) continue;
        drawAppWindow(app);
        present(app.hwnd);
    }
    redrawTaskbar();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) return 1;
    if (message == WM_COMMAND && reinterpret_cast<HWND>(lParam) == g.themeButtonProbe &&
        LOWORD(wParam) == 0x771u && HIWORD(wParam) == BN_CLICKED) {
        g.themeButtonProbeReported = true;
        return 0;
    }
    if (hwnd == g.desktop &&
        (message == 0x001A /* WM_SETTINGCHANGE */ ||
         message == 0x031A /* WM_THEMECHANGED */)) {
        // WM_SETTINGCHANGE can announce changed persisted preferences. A
        // WM_THEMECHANGED broadcast follows an already-published snapshot;
        // reloading active.profile here would replace a session-only runtime
        // selection with the stale persisted profile that caused the switch.
        if (message == 0x001A /* WM_SETTINGCHANGE */)
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
        if (hwnd == g.desktop) { source = g.desktopDc; width = g.screenW; height = g.screenH - TASKBAR_H; }
        else if (hwnd == g.taskbar) { source = g.taskbarDc; width = g.screenW; height = TASKBAR_H; }
        else if (hwnd == g.startMenu) { source = g.menuDc; width = MENU_W; height = MENU_H; }
        else if (auto* app = appFromHwnd(hwnd)) { source = app->dc; width = app->width; height = app->height; }
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(hwnd, &paint);
        if (target && source && width && height)
            BitBlt(target, 0, 0, static_cast<int>(width), static_cast<int>(height), source, 0, 0, SRCCOPY);
        EndPaint(hwnd, &paint);
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
    loadActiveVisualTheme();
    PrintLine(g.visualThemeReady
        ? "[MicaNT Theme] Native style snapshot accepted"
        : "[MicaNT Theme] Base fallback active");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
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

    const int32_t taskbarY = static_cast<int32_t>(g.screenH - TASKBAR_H);
    g.taskbar = CreateWindowExW(WS_EX_TOPMOST, kClassName, kTaskbarTitle,
        WS_POPUP | WS_VISIBLE, 0, taskbarY, static_cast<int32_t>(g.screenW), TASKBAR_H,
        nullptr, nullptr, nullptr, nullptr);
    if (!g.taskbar) for (;;) __asm__ __volatile__("pause");
    createThemeProgressProbe();
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
    uint64_t lastClockUpdate = GetTickCount64();
    for (;;) {
        MSG msg{};
        const int32_t result = GetMessageW(&msg, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else if (result == 0) {
            break;
        } else {
            Sleep(10);
        }
        const uint64_t now = GetTickCount64();
        if (now >= lastClockUpdate && now - lastClockUpdate >= 1000) {
            updateClock();
            lastClockUpdate = now;
        }
    }
}
