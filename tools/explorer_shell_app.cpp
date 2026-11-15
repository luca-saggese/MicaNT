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
#ifdef GetGlyphIndices
#undef GetGlyphIndices
#endif
#include "micant/shared/win32k_client.h"
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/theme_snapshot.h"
#include "gui_common.hpp"
#include "explorer_shell/taskbar.hpp"
#include "explorer_shell/start_menu.hpp"
#include "explorer_shell/desktop.hpp"
#include "explorer_shell/icon_catalog.hpp"
#include "explorer_shell/notification_area.hpp"
#include "explorer_shell/task_switcher.hpp"

namespace {

using namespace micant_gui;

constexpr uint32_t TASKBAR_H = 40;
constexpr uint32_t MENU_W = 344;
constexpr uint32_t MENU_H = 390;
constexpr size_t MAX_APP_WINDOWS = 8;

enum class View : uint8_t { MyComputer, MyDocuments, ControlPanel, Network, RecycleBin, DateTime };

struct AppWindow {
    HWND hwnd{};
    HDC dc{};
    HBITMAP bitmap{};
    uint32_t* pixels{};
    uint32_t width{500};
    uint32_t height{320};
    View view{View::MyComputer};
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
    uint8_t appCount{};
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
    return TextOutW(dc, x, y, wide, static_cast<int>(units)) != FALSE;
}

void text(uint32_t* p, uint32_t w, uint32_t h, int32_t x, int32_t y,
          const char* value, uint32_t color) noexcept {
    micant_gui::DrawStringClipped(p, w, h, x, y, value, color);
}

void drawAppWindow(AppWindow& app) noexcept {
    if (!app.pixels) return;
    uint32_t* p = app.pixels;
    const uint32_t w = app.width, h = app.height;
    const auto* windowStyle = themeStyle(14);
    const auto* toolbarStyle = themeStyle(10);
    const auto* entryStyle = themeStyle(1);
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
    const uint32_t entryBorder = entryStyle &&
        (entryStyle->flags & MICANT_THEME_HAS_BORDER)
        ? themePixel(entryStyle->border) : rgb(191, 202, 215);
    fill(p, w, h, 0, 0, w, h, windowBackground);
    fill(p, w, h, 0, 0, 158, h, rgb(236, 241, 247));
    fill(p, w, h, 0, 0, w, 27, toolbarBackground);
    fill(p, w, h, 0, 26, w, 1, rgb(207, 216, 228));
    fill(p, w, h, 12, 8, 8, 8, rgb(53, 121, 191));
    text(p, w, h, 27, 8, "Address", toolbarForeground);
    fill(p, w, h, 12, 32, w - 24, 25, entryBackground);
    DrawRect(p, w, 12, 32, w - 24, 25, entryBorder);
    const char* address = app.view == View::MyComputer ? "My Computer" :
                          app.view == View::MyDocuments ? "C:\\Documents and Settings\\MicaNT\\My Documents" :
                          app.view == View::ControlPanel ? "Control Panel" :
                          app.view == View::RecycleBin ? "Recycle Bin" :
                          app.view == View::DateTime ? "Date and Time" : "Network Places";
    text(p, w, h, 22, 40, address, entryForeground);
    text(p, w, h, 174, 71, "Name", rgb(82, 98, 116));
    text(p, w, h, 370, 71, "Type", rgb(82, 98, 116));
    fill(p, w, h, 168, 88, w - 180, 1, rgb(211, 218, 226));

    const char* rowsComputer[] = {"Local Disk (C:)", "Data Volume (D:)", "Linux Root (E:)"};
    const char* rowsDocuments[] = {"My Pictures", "My Music", "Readme.txt"};
    const char* rowsControl[] = {"Appearance and Themes", "Network Connections", "User Accounts", "Date and Time"};
    const char* rowsNetwork[] = {"MicaNT Network", "Workgroup Computers"};
    const char* rowsRecycle[] = {"The Recycle Bin is empty."};
    const char* rowsDateTime[] = {"Date", "Time"};
    const char** rows = rowsComputer;
    size_t count = 3;
    if (app.view == View::MyDocuments) rows = rowsDocuments;
    else if (app.view == View::ControlPanel) { rows = rowsControl; count = 4; }
    else if (app.view == View::Network) { rows = rowsNetwork; count = 2; }
    else if (app.view == View::RecycleBin) { rows = rowsRecycle; count = 1; }
    else if (app.view == View::DateTime) { rows = rowsDateTime; count = 2; }
    using micant::explorer::IconId;
    IconId rowIcons[4]{IconId::Folder, IconId::Folder, IconId::Folder, IconId::Folder};
    if (app.view == View::MyComputer) {
        rowIcons[0] = IconId::HardDisk; rowIcons[1] = IconId::HardDisk;
        rowIcons[2] = IconId::RemovableDrive;
    } else if (app.view == View::MyDocuments) {
        rowIcons[0] = IconId::Pictures; rowIcons[1] = IconId::Music;
        rowIcons[2] = micant::explorer::ResolveFileIcon(rows[2]);
    } else if (app.view == View::ControlPanel) {
        rowIcons[0] = IconId::Folder; rowIcons[1] = IconId::Network;
        rowIcons[2] = IconId::GenericText; rowIcons[3] = IconId::GenericText;
    } else if (app.view == View::Network) {
        rowIcons[0] = IconId::Network; rowIcons[1] = IconId::Computer;
    } else if (app.view == View::RecycleBin) {
        rowIcons[0] = IconId::TrashEmpty;
    } else {
        rowIcons[0] = IconId::GenericText; rowIcons[1] = IconId::GenericText;
    }
    for (size_t i = 0; i < count; ++i) {
        const int32_t y = 100 + static_cast<int32_t>(i) * 26;
        if (!findCachedIcon(rowIcons[i], 16)) (void)loadPackagedIcon(rowIcons[i], 16);
        const auto image = iconImage(rowIcons[i], 16);
        if (image.pixels) micant::explorer::DrawIconImage(p, w, h, 174, y, 16, image);
        else fill(p, w, h, 174, y, 16, 16, rgb(236, 190, 67));
        text(p, w, h, 199, y + 4, rows[i], rgb(34, 42, 53));
        if (app.view == View::DateTime) {
            char value[16]{};
            if (i == 0) {
                value[0] = static_cast<char>('0' + g.systemTime.day / 10);
                value[1] = static_cast<char>('0' + g.systemTime.day % 10);
                value[2] = '/';
                value[3] = static_cast<char>('0' + g.systemTime.month / 10);
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
            text(p, w, h, 370, y + 4, value, rgb(100, 108, 118));
        } else {
            text(p, w, h, 370, y + 4, app.view == View::MyComputer ? "Disk drive" : "System folder", rgb(100, 108, 118));
        }
    }
    fill(p, w, h, 0, h - 24, w, 24, rgb(244, 246, 249));
    fill(p, w, h, 0, h - 25, w, 1, rgb(212, 219, 228));
    text(p, w, h, 12, h - 17, "3 objects", rgb(90, 98, 108));
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
    micant::explorer::DrawTaskbar(g.taskbarPixels, g.screenW, TASKBAR_H, TASKBAR_H,
                                  buttons, count, g.clockText);
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

void loadActiveVisualTheme() noexcept {
    wchar_t path[512]{};
    size_t cursor = 0, profileBytes = 0;
    char profile[96]{};
    if (!appendSystemRoot(path, std::size(path), cursor) ||
        !appendWide(path, std::size(path), cursor,
                    L"\\System\\Themes\\active.profile", 29) ||
        !readBoundedFile(path, profile, sizeof(profile) - 1, profileBytes)) {
        PrintLine("[MicaNT Theme] Active profile could not be read");
        return;
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
    g.visualTheme = candidate;
    g.visualThemeReady = true;
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
    constexpr uint32_t kInitialWindowWidth = 500;
    constexpr uint32_t kInitialWindowHeight = 320;
    app->hwnd = CreateWindowExW(0, kClassName,
        view == View::MyComputer ? L"My Computer" :
        view == View::MyDocuments ? L"My Documents" :
        view == View::ControlPanel ? L"Control Panel" :
        view == View::RecycleBin ? L"Recycle Bin" :
        view == View::DateTime ? L"Date and Time" : L"Network Places",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 190 + static_cast<int32_t>(g.appCount % 4) * 32,
        90 + static_cast<int32_t>(g.appCount % 4) * 28,
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
    g.activeApp = static_cast<uint8_t>(app - g.apps);
    SetFocus(app->hwnd);
    ++g.appCount;
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
}

void toggleStart() noexcept {
    g.startOpen = !g.startOpen;
    if (g.startOpen) {
        micant::explorer::DrawStartMenu(g.menuPixels, MENU_W, MENU_H, "MicaNT User");
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
    if (hwnd == g.desktop &&
        (message == 0x001A /* WM_SETTINGCHANGE */ ||
         message == 0x031A /* WM_THEMECHANGED */)) {
        loadActiveVisualTheme();
        for (auto& app : g.apps) {
            if (!app.open || !app.hwnd || !app.pixels) continue;
            drawAppWindow(app);
            present(app.hwnd);
        }
        redrawDesktop();
        return 0;
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
                    } else if (bitmap) {
                        DeleteObject(bitmap);
                    }
                }
            }
            redrawTaskbar();
        }
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
    uint32_t clockPollDivider = 0;
    for (;;) {
        // Clock reads cross into the kernel and query the RTC; the taskbar only
        // needs seconds precision, so avoid doing that work on every idle spin.
        if (++clockPollDivider == 2000000U) {
            clockPollDivider = 0;
            updateClock();
        }
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        __asm__ __volatile__("pause");
    }
}
