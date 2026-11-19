/**
 * @file shell32.cpp
 @brief Luce OS Windows Shell API DLL (shell32.dll) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 */

#include <cstdint>
#include <cstddef>
#include "micant/um/subsystems/winbase/winnt_ntdll_ws2_iphlpapi_winhttp_abi.hpp"

struct ShellMsg { void* hwnd; uint32_t message; uintptr_t wParam; intptr_t lParam;
                  uint32_t time; int32_t x, y; };
struct ShellWndClassEx {
    uint32_t cbSize, style;
    void* wndProc;
    int32_t classExtra, windowExtra;
    void* instance; void* icon; void* cursor; void* background;
    const wchar_t* menuName; const wchar_t* className; void* smallIcon;
};
struct ShellRect { int32_t left, top, right, bottom; };
struct ShellPaintStruct {
    void* hdc; int32_t erase; ShellRect paint; int32_t restore, increment;
    uint8_t reserved[32];
};
extern "C" {
__declspec(dllimport) uint16_t RegisterClassExW(const ShellWndClassEx*);
__declspec(dllimport) void* CreateWindowExW(uint32_t, const wchar_t*, const wchar_t*,
    uint32_t, int32_t, int32_t, int32_t, int32_t, void*, void*, void*, void*);
__declspec(dllimport) int32_t GetSystemMetrics(int32_t);
__declspec(dllimport) micant::NTSTATUS __stdcall RtlGetVersion(
    micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtOsVersionInfoExW*);
__declspec(dllimport) int32_t ShowWindow(void*, int32_t);
__declspec(dllimport) int32_t DestroyWindow(void*);
__declspec(dllimport) int32_t GetMessageW(ShellMsg*, void*, uint32_t, uint32_t);
__declspec(dllimport) int32_t TranslateMessage(const ShellMsg*);
__declspec(dllimport) intptr_t DispatchMessageW(const ShellMsg*);
__declspec(dllimport) void PostQuitMessage(int32_t);
__declspec(dllimport) intptr_t DefWindowProcW(void*, uint32_t, uintptr_t, intptr_t);
__declspec(dllimport) void* BeginPaint(void*, ShellPaintStruct*);
__declspec(dllimport) int32_t EndPaint(void*, const ShellPaintStruct*);
__declspec(dllimport) int32_t GetClientRect(void*, ShellRect*);
__declspec(dllimport) int32_t FillRect(void*, const ShellRect*, void*);
__declspec(dllimport) void* GetStockObject(int32_t);
__declspec(dllimport) void* SelectObject(void*, void*);
__declspec(dllimport) uint32_t SetTextColor(void*, uint32_t);
__declspec(dllimport) int32_t SetBkMode(void*, int32_t);
__declspec(dllimport) int32_t TextOutW(void*, int32_t, int32_t, const wchar_t*, int32_t);
__declspec(dllimport) int32_t DrawIconEx(void*, int32_t, int32_t, void*, int32_t,
                                         int32_t, uint32_t, void*, uint32_t);
__declspec(dllimport) void* GlobalLock(void*);
__declspec(dllimport) int32_t GlobalUnlock(void*);
__declspec(dllimport) void* GlobalFree(void*);
__declspec(dllimport) intptr_t GetWindowLongPtrW(void*, int32_t);
__declspec(dllimport) intptr_t SetWindowLongPtrW(void*, int32_t, intptr_t);
__declspec(dllimport) void SetLastError(uint32_t);
__declspec(dllimport) uint32_t GetLastError();
__declspec(dllimport) int32_t MultiByteToWideChar(uint32_t, uint32_t,
    const char*, int32_t, wchar_t*, int32_t);
__declspec(dllimport) int32_t CreateDirectoryW(const wchar_t*, void*);
struct ShellStartupInfoW { uint32_t cb; wchar_t* reserved; wchar_t* desktop; wchar_t* title;
    uint32_t x,y,xSize,ySize,xCountChars,yCountChars,fillAttribute,flags;
    uint16_t showWindow,reserved2Size; uint8_t* reserved2;
    void* standardInput; void* standardOutput; void* standardError; };
struct ShellProcessInformation { void* process; void* thread; uint32_t processId; uint32_t threadId; };
__declspec(dllimport) int32_t __stdcall CreateProcessW(const wchar_t*, wchar_t*, void*, void*,
    int32_t, uint32_t, void*, const wchar_t*, ShellStartupInfoW*, ShellProcessInformation*);
__declspec(dllimport) int32_t __stdcall CloseHandle(void*);
}

namespace {
constexpr int32_t kMaxPath = 260;
constexpr uint32_t kCsidlFlagCreate = 0x8000u;
constexpr uint32_t kCsidlLocalAppData = 0x001Cu;
constexpr uint32_t kCsidlAppData = 0x001Au;
constexpr uint32_t kCsidlPersonal = 0x0005u;
constexpr uint32_t kCsidlProfile = 0x0028u;
constexpr uint32_t kCsidlDesktopDirectory = 0x0010u;

bool appendPath(wchar_t* output, size_t& used, const wchar_t* text) noexcept {
    while (*text) {
        if (used + 1 >= static_cast<size_t>(kMaxPath)) return false;
        output[used++] = *text++;
    }
    output[used] = L'\0';
    return true;
}

bool folderPath(uint32_t folder, wchar_t* output) noexcept {
    const wchar_t* suffix = nullptr;
    switch (folder & 0x00ffu) {
        case kCsidlProfile: suffix = L""; break;
        case kCsidlLocalAppData: suffix = L"\\AppData\\Local"; break;
        case kCsidlAppData: suffix = L"\\AppData\\Roaming"; break;
        case kCsidlPersonal: suffix = L"\\Documents"; break;
        case kCsidlDesktopDirectory: suffix = L"\\Desktop"; break;
        default: return false;
    }
    size_t used = 0;
    output[0] = L'\0';
    constexpr wchar_t profile[] = L"C:\\Users\\Default";
    if (!appendPath(output, used, profile) || !appendPath(output, used, suffix))
        return false;
    return true;
}

constexpr wchar_t kAboutClassName[] = L"LuceOS_ShellAboutDialog";
uint16_t g_aboutClassAtom{};
const wchar_t* g_aboutApplication{};
const wchar_t* g_aboutAdditionalText{};
void* g_aboutIcon{};
wchar_t g_aboutVersionText[128]{};
wchar_t g_aboutCopyrightText[64]{};

void appendNumber(wchar_t* output, size_t capacity, size_t& used,
                  uint32_t value) noexcept {
    wchar_t digits[10]{};
    size_t count{};
    do { digits[count++] = static_cast<wchar_t>(L'0' + value % 10); value /= 10; }
    while (value && count < sizeof(digits) / sizeof(digits[0]));
    while (count && used + 1 < capacity) output[used++] = digits[--count];
}

void initializeAboutVersion() noexcept {
    g_aboutVersionText[0] = L'\0';
    g_aboutCopyrightText[0] = L'\0';
    micant::um::winnt_ntdll_ws2_iphlpapi_winhttp_abi::WinNtOsVersionInfoExW version{};
    if (RtlGetVersion(&version) < 0) return;
    size_t used{};
    constexpr size_t versionCapacity = sizeof(g_aboutVersionText) / sizeof(wchar_t);
    const wchar_t prefix[] = L"Version ";
    for (size_t i = 0; prefix[i] && used + 1 < versionCapacity; ++i)
        g_aboutVersionText[used++] = prefix[i];
    appendNumber(g_aboutVersionText, versionCapacity, used, version.dwMajorVersion);
    if (used + 1 < versionCapacity) g_aboutVersionText[used++] = L'.';
    appendNumber(g_aboutVersionText, versionCapacity, used, version.dwMinorVersion);
    const wchar_t buildPrefix[] = L" (Build ";
    for (size_t i = 0; buildPrefix[i] && used + 1 < versionCapacity; ++i)
        g_aboutVersionText[used++] = buildPrefix[i];
    appendNumber(g_aboutVersionText, versionCapacity, used, version.dwBuildNumber);
    if (used + 2 < versionCapacity) {
        g_aboutVersionText[used++] = L')';
        g_aboutVersionText[used] = L'\0';
    }
    constexpr wchar_t copyright[] = L"Copyright 2026 LuceOS";
    size_t copyrightIndex{};
    while (copyright[copyrightIndex] && copyrightIndex + 1 <
           sizeof(g_aboutCopyrightText) / sizeof(wchar_t)) {
        g_aboutCopyrightText[copyrightIndex] = copyright[copyrightIndex];
        ++copyrightIndex;
    }
    g_aboutCopyrightText[copyrightIndex] = L'\0';
}

void drawAboutContents(void* window) noexcept {
    ShellPaintStruct paint{};
    void* dc = BeginPaint(window, &paint);
    if (!dc) return;
    (void)SelectObject(dc, GetStockObject(17)); // DEFAULT_GUI_FONT
    ShellRect client{};
    if (GetClientRect(window, &client)) {
        (void)FillRect(dc, &client, GetStockObject(0)); // WHITE_BRUSH
        (void)SetBkMode(dc, 1); // TRANSPARENT
        (void)SetTextColor(dc, 0x00000000u);
        if (g_aboutIcon) (void)DrawIconEx(dc, 16, 16, g_aboutIcon, 32, 32, 0, nullptr, 0);
        if (g_aboutApplication) {
            size_t length{};
            while (g_aboutApplication[length] && length < 511) ++length;
            (void)TextOutW(dc, g_aboutIcon ? 60 : 20, 20, g_aboutApplication,
                           static_cast<int32_t>(length));
        }
        if (g_aboutVersionText[0])
            (void)TextOutW(dc, 20, 43, g_aboutVersionText,
                static_cast<int32_t>([] (const wchar_t* text) noexcept {
                    size_t n{}; while (text[n] && n < 127) ++n; return n;
                }(g_aboutVersionText)));
        (void)TextOutW(dc, 20, 64, g_aboutCopyrightText,
            static_cast<int32_t>([] (const wchar_t* text) noexcept {
                size_t n{}; while (text[n] && n < 63) ++n; return n;
            }(g_aboutCopyrightText)));
        if (g_aboutAdditionalText && *g_aboutAdditionalText) {
            size_t length{};
            while (g_aboutAdditionalText[length] && length < 511) ++length;
            (void)TextOutW(dc, 20, 88, g_aboutAdditionalText,
                           static_cast<int32_t>(length));
        }
    }
    (void)EndPaint(window, &paint);
}

intptr_t __stdcall ShellAboutWindowProc(void* window, uint32_t message,
                                       uintptr_t wParam, intptr_t lParam) noexcept {
    constexpr uint32_t WM_CLOSE = 0x0010, WM_KEYDOWN = 0x0100;
    constexpr uint32_t WM_PAINT = 0x000F, WM_COMMAND = 0x0111;
    constexpr uint32_t WM_NCCREATE = 0x0081;
    constexpr uintptr_t VK_RETURN = 0x0D, VK_ESCAPE = 0x1B;
    if (message == WM_CLOSE || (message == WM_COMMAND &&
        ((wParam & 0xffffu) == 1u || (wParam & 0xffffu) == 2u)) ||
        (message == WM_KEYDOWN &&
        (wParam == VK_RETURN || wParam == VK_ESCAPE))) {
        DestroyWindow(window);
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_PAINT) { drawAboutContents(window); return 0; }
    if (message == WM_NCCREATE) return 1;
    return DefWindowProcW(window, message, wParam, lParam);
}
}

extern "C" {
struct ShellExecuteInfoW {
    uint32_t cbSize, fMask;
    void* hwnd;
    const wchar_t* lpVerb;
    const wchar_t* lpFile;
    const wchar_t* lpParameters;
    const wchar_t* lpDirectory;
    int32_t nShow;
    void* hInstApp;
    void* lpIDList;
    const wchar_t* lpClass;
    void* hkeyClass;
    uint32_t dwHotKey;
    void* iconOrMonitor;
    void* hProcess;
};
static int32_t launchShellTarget(const wchar_t* file, const wchar_t* parameters,
        const wchar_t* directory, uint32_t show, void** process) noexcept {
    if (process) *process = nullptr;
    if (!file || !*file) { SetLastError(87); return 0; }
    wchar_t command[256]{};
    size_t n = 0;
    for (const wchar_t* p = file; *p; ++p) {
        if (n + 2 >= std::size(command)) { SetLastError(206); return 0; }
        command[n++] = *p;
    }
    if (parameters && *parameters) {
        command[n++] = L' ';
        for (const wchar_t* p = parameters; *p; ++p) {
            if (n + 2 >= std::size(command)) { SetLastError(206); return 0; }
            command[n++] = *p;
        }
    }
    ShellStartupInfoW startup{};
    startup.cb = sizeof(startup);
    startup.showWindow = static_cast<uint16_t>(show);
    ShellProcessInformation info{};
    if (!CreateProcessW(file, command, nullptr, nullptr, 0, 0, nullptr,
            directory, &startup, &info)) return 0;
    if (process) *process = info.process;
    else (void)CloseHandle(info.process);
    (void)CloseHandle(info.thread);
    SetLastError(0);
    return 1;
}
__declspec(dllexport) int32_t WINAPI ShellExecuteExW(ShellExecuteInfoW* info) noexcept {
    constexpr uint32_t supportedMask = 0x00000040u | 0x00000100u | 0x00000400u;
    if (!info || info->cbSize < sizeof(ShellExecuteInfoW) || !info->lpFile ||
        (info->fMask & ~supportedMask) || info->lpIDList || info->lpClass ||
        info->hkeyClass || info->dwHotKey) {
        SetLastError(info ? 50 : 87);
        return 0;
    }
    if (info->lpVerb && *info->lpVerb &&
        !((*info->lpVerb == L'o' || *info->lpVerb == L'O') &&
          (info->lpVerb[1] == L'p' || info->lpVerb[1] == L'P') &&
          (info->lpVerb[2] == L'e' || info->lpVerb[2] == L'E') &&
          (info->lpVerb[3] == L'n' || info->lpVerb[3] == L'N') && !info->lpVerb[4])) {
        SetLastError(1155); // no association/verb handler
        return 0;
    }
    void* process{};
    if (!launchShellTarget(info->lpFile, info->lpParameters, info->lpDirectory,
            static_cast<uint32_t>(info->nShow),
            (info->fMask & 0x00000040u) ? &process : nullptr)) return 0;
    info->hInstApp = reinterpret_cast<void*>(33);
    info->hProcess = process;
    return 1;
}
__declspec(dllexport) void* WINAPI ShellExecuteW(void*, const wchar_t* verb,
        const wchar_t* file, const wchar_t* parameters, const wchar_t* directory,
        int32_t show) noexcept {
    if (verb && *verb && !((*verb == L'o' || *verb == L'O') &&
        (verb[1] == L'p' || verb[1] == L'P') && (verb[2] == L'e' || verb[2] == L'E') &&
        (verb[3] == L'n' || verb[3] == L'N') && !verb[4])) {
        SetLastError(1155);
        return reinterpret_cast<void*>(31);
    }
    return reinterpret_cast<void*>(launchShellTarget(file, parameters, directory,
        static_cast<uint32_t>(show), nullptr) ? 33 : 2);
}

__declspec(dllexport) int32_t WINAPI Shell_NotifyIconW(uint32_t operation,
        void* notificationData) noexcept {
    // Notification icons belong to the Explorer notification-area service.
    // This isolated Taskmgr profile intentionally boots without Explorer, so
    // accepting NIM_ADD here would claim visibility that the shell cannot give.
    if (!notificationData || operation > 4) {
        SetLastError(87);
        return 0;
    }
    SetLastError(120); // ERROR_CALL_NOT_IMPLEMENTED: no tray service is running.
    return 0;
}

__declspec(dllexport) void WINAPI DragAcceptFiles(void* window, int32_t accept) noexcept {
    if (!window) {
        SetLastError(1400); // ERROR_INVALID_WINDOW_HANDLE
        return;
    }
    constexpr int32_t GWL_EXSTYLE = -20;
    constexpr uint32_t WS_EX_ACCEPTFILES = 0x00000010u;
    const uint32_t current = static_cast<uint32_t>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    const uint32_t updated = accept ? (current | WS_EX_ACCEPTFILES) :
                                      (current & ~WS_EX_ACCEPTFILES);
    (void)SetWindowLongPtrW(window, GWL_EXSTYLE, static_cast<intptr_t>(updated));
}

struct ShellDropFiles {
    uint32_t offset;
    int32_t x;
    int32_t y;
    int32_t fNC;
    int32_t fWide;
};

__declspec(dllexport) void WINAPI DragFinish(void* drop) noexcept {
    if (drop) (void)GlobalFree(drop);
}

__declspec(dllexport) uint32_t WINAPI DragQueryFileW(
    void* drop, uint32_t fileIndex, wchar_t* output, uint32_t outputChars) noexcept {
    if (!drop) {
        SetLastError(6); // ERROR_INVALID_HANDLE
        return 0;
    }
    auto* header = static_cast<const ShellDropFiles*>(GlobalLock(drop));
    if (!header || header->offset < sizeof(ShellDropFiles)) {
        if (header) (void)GlobalUnlock(drop);
        SetLastError(6);
        return 0;
    }
    const auto* payload = reinterpret_cast<const uint8_t*>(header) + header->offset;
    uint32_t count = 0;
    const wchar_t* selected = nullptr;
    uint32_t currentIndex = 0;
    if (header->fWide) {
        const auto* cursor = reinterpret_cast<const wchar_t*>(payload);
        while (*cursor) {
            if (fileIndex == UINT32_MAX) ++count;
            else if (currentIndex == fileIndex) selected = cursor;
            while (*cursor) ++cursor;
            ++cursor;
            ++currentIndex;
        }
        if (fileIndex == UINT32_MAX) {
            (void)GlobalUnlock(drop);
            return count;
        }
        if (selected) {
            uint32_t length = 0;
            while (selected[length]) ++length;
            if (output && outputChars) {
                const uint32_t copied = length < outputChars - 1 ? length : outputChars - 1;
                for (uint32_t i = 0; i < copied; ++i) output[i] = selected[i];
                output[copied] = L'\0';
            }
            (void)GlobalUnlock(drop);
            return length;
        }
    } else {
        const auto* cursor = reinterpret_cast<const char*>(payload);
        while (*cursor) {
            if (fileIndex == UINT32_MAX) ++count;
            else if (currentIndex == fileIndex) selected =
                reinterpret_cast<const wchar_t*>(cursor);
            while (*cursor) ++cursor;
            ++cursor;
            ++currentIndex;
        }
        if (fileIndex == UINT32_MAX) {
            (void)GlobalUnlock(drop);
            return count;
        }
        if (selected) {
            const auto* narrow = reinterpret_cast<const char*>(selected);
            const int32_t required = MultiByteToWideChar(0, 0, narrow, -1, nullptr, 0);
            if (required <= 0) {
                (void)GlobalUnlock(drop);
                return 0;
            }
            if (output && outputChars) {
                if (MultiByteToWideChar(0, 0, narrow, -1, output,
                        static_cast<int32_t>(outputChars)) == 0)
                    output[0] = L'\0';
            }
            (void)GlobalUnlock(drop);
            return static_cast<uint32_t>(required - 1);
        }
    }
    (void)GlobalUnlock(drop);
    if (output && outputChars) output[0] = L'\0';
    return 0;
}

__declspec(dllexport) int32_t WINAPI SHGetFolderPathW(
    void*, int32_t csidl, void*, uint32_t, wchar_t* output) noexcept {
    if (!output) return static_cast<int32_t>(0x80070057u); // E_INVALIDARG
    wchar_t path[kMaxPath]{};
    const uint32_t id = static_cast<uint32_t>(csidl);
    if (!folderPath(id, path)) {
        output[0] = L'\0';
        return static_cast<int32_t>(0x80070057u);
    }
    size_t length = 0;
    while (path[length]) ++length;
    for (size_t i = 0; i <= length; ++i) output[i] = path[i];
    if (id & kCsidlFlagCreate) {
        // SHGetFolderPath's CREATE flag creates the requested leaf directory.
        if (!CreateDirectoryW(path, nullptr)) {
            output[0] = L'\0';
            return static_cast<int32_t>(0x80070005u); // E_ACCESSDENIED
        }
    }
    return 0; // S_OK
}

__declspec(dllexport) void WINAPI SHAddToRecentDocs(uint32_t, const void*) noexcept {
    // Recent-document persistence is owned by Explorer. Until its recent-item
    // store is connected, this shell notification intentionally has no effect.
}

// ShellAboutW is a real shell operation: present the caller's about text in a
// modal information dialog and report whether the dialog was acknowledged.
// The shell does not claim success when the underlying window operation fails.
__declspec(dllexport) int ShellAboutW(void* owner, const wchar_t* application,
                                      const wchar_t* additionalText, void* icon) noexcept {
    if (application && !*application) return 0;
    g_aboutApplication = application;
    g_aboutAdditionalText = additionalText;
    g_aboutIcon = icon;
    initializeAboutVersion();
    if (!g_aboutClassAtom) {
        ShellWndClassEx klass{};
        klass.cbSize = sizeof(klass);
        klass.wndProc = reinterpret_cast<void*>(&ShellAboutWindowProc);
        klass.icon = icon;
        klass.className = kAboutClassName;
        g_aboutClassAtom = RegisterClassExW(&klass);
        if (!g_aboutClassAtom) return 0;
    }
    wchar_t caption[512]{};
    size_t used{};
    const wchar_t titlePrefix[] = L"About ";
    for (size_t i = 0; titlePrefix[i] && used + 1 < 512; ++i)
        caption[used++] = titlePrefix[i];
    if (application) {
        size_t appIndex{};
        for (; application[appIndex] && used + 1 < 512; ++appIndex)
            caption[used++] = application[appIndex];
        if (application[appIndex]) return 0;
    }
    caption[used] = L'\0';
    constexpr uint32_t WS_OVERLAPPED = 0, WS_CAPTION = 0x00C00000u;
    constexpr uint32_t WS_SYSMENU = 0x00080000u, WS_VISIBLE = 0x10000000u;
    constexpr int32_t width = 440, height = 220;
    const int32_t screenWidth = GetSystemMetrics(0);
    const int32_t screenHeight = GetSystemMetrics(1);
    const int32_t x = screenWidth > width ? (screenWidth - width) / 2 : 0;
    const int32_t y = screenHeight > height ? (screenHeight - height) / 2 : 0;
    void* dialog = CreateWindowExW(0, kAboutClassName, caption,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, width, height, owner, nullptr, nullptr, nullptr);
    if (!dialog) return 0;
    ShellRect client{};
    if (!GetClientRect(dialog, &client)) {
        (void)DestroyWindow(dialog);
        return 0;
    }
    constexpr int32_t buttonWidth = 84, buttonHeight = 28, bottomMargin = 10;
    const int32_t clientWidth = client.right - client.left;
    const int32_t clientHeight = client.bottom - client.top;
    const int32_t buttonX = client.left + (clientWidth - buttonWidth) / 2;
    const int32_t buttonY = client.top + clientHeight - buttonHeight - bottomMargin;
    constexpr uint32_t WS_CHILD = 0x40000000u, BS_DEFPUSHBUTTON = 0x00000001u;
    void* okButton = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, buttonX, buttonY,
        buttonWidth, buttonHeight, dialog, reinterpret_cast<void*>(1), nullptr, nullptr);
    if (!okButton) {
        (void)DestroyWindow(dialog);
        return 0;
    }
    ShellMsg message{};
    int32_t result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }
    if (result < 0) {
        (void)DestroyWindow(dialog);
        return 0;
    }
    return 1;
}

} // extern "C"
