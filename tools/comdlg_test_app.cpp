// Minimal Ring 3 client for the native common file dialogs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <cstdint>

extern "C" __declspec(dllimport) void WINAPI MicaWin32kDebugInputTrace(
    uint32_t, uint32_t, uint32_t);

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

namespace {
constexpr wchar_t kClassName[] = L"MicaNTCommonDialogProbe";
constexpr int kOpenId = 101;
constexpr int kSaveId = 102;
constexpr int kStatusId = 103;
HWND g_window{};
uint32_t g_returnKeyDialogCount{};

void runDialog(bool save) {
    MicaWin32kDebugInputTrace(100, save ? 1u : 0u, 0);
    wchar_t file[260]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window;
    ofn.lpstrFilter = L"All files (*.*)\0*.*\0Text documents (*.txt)\0*.txt\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
    ofn.lpstrInitialDir = L"C:\\MicaNT\\System32";
    ofn.lpstrTitle = save ? L"Save a document" : L"Open a document";
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL accepted = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    MicaWin32kDebugInputTrace(101, save ? 1u : 0u,
                              accepted ? 1u : CommDlgExtendedError());
    wchar_t status[320]{};
    if (accepted) {
        const wchar_t* prefix = save ? L"Save selected: " : L"Open selected: ";
        size_t at = 0;
        while (prefix[at] && at + 1 < sizeof(status) / sizeof(status[0])) {
            status[at] = prefix[at];
            ++at;
        }
        for (size_t i = 0; file[i] && at + 1 < sizeof(status) / sizeof(status[0]); ++i)
            status[at++] = file[i];
        status[at] = 0;
    } else {
        (void)CommDlgExtendedError();
        const wchar_t* cancelled = save ? L"Save cancelled" : L"Open cancelled";
        size_t i = 0;
        while (cancelled[i] && i + 1 < sizeof(status) / sizeof(status[0])) {
            status[i] = cancelled[i];
            ++i;
        }
    }
    SetDlgItemTextW(g_window, kStatusId, status);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) noexcept {
    switch (message) {
    case WM_CREATE:
        MicaWin32kDebugInputTrace(102, 0, 0);
        CreateWindowExW(0, L"BUTTON", L"Open...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            24, 48, 120, 34, window, reinterpret_cast<HMENU>(kOpenId),
            GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"BUTTON", L"Save As...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            160, 48, 120, 34, window, reinterpret_cast<HMENU>(kSaveId),
            GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"STATIC", L"Choose Open or Save As to inspect the dialog.",
            WS_CHILD | WS_VISIBLE, 24, 104, 440, 28, window,
            reinterpret_cast<HMENU>(kStatusId), GetModuleHandleW(nullptr), nullptr);
        return 0;
    case WM_COMMAND:
        MicaWin32kDebugInputTrace(103, LOWORD(wParam), HIWORD(wParam));
        if (LOWORD(wParam) == kOpenId && HIWORD(wParam) == BN_CLICKED) {
            runDialog(false);
            return 0;
        }
        if (LOWORD(wParam) == kSaveId && HIWORD(wParam) == BN_CLICKED) {
            runDialog(true);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        MicaWin32kDebugInputTrace(104, static_cast<uint32_t>(wParam), 0);
        if (wParam == VK_F6) { runDialog(false); return 0; }
        if (wParam == VK_F7) { runDialog(true); return 0; }
        if (wParam == 'S') { runDialog(true); return 0; }
        if (wParam == VK_RETURN) {
            runDialog(g_returnKeyDialogCount++ != 0);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        MicaWin32kDebugInputTrace(107, static_cast<uint32_t>(lParam), 0);
        break;
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
    MicaWin32kDebugInputTrace(105, 0, 0);
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    klass.lpszClassName = kClassName;
    if (!RegisterClassExW(&klass)) return;
    g_window = CreateWindowExW(0, kClassName, L"Common Dialog Probe",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 210, 180, 520, 220,
        nullptr, nullptr, klass.hInstance, nullptr);
    MicaWin32kDebugInputTrace(106, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(g_window)), 0);
    if (!g_window) return;
    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
    SetForegroundWindow(g_window);
    SetFocus(g_window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
