// Small Ring 3 probe for the STATIC textbox path used by ReactOS Calc.
// It exercises creation text, WM_SETTEXT/GetWindowText and the normal
// User32/GDI paint path without loading or replacing Calc.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

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
constexpr wchar_t kClassName[] = L"MicaNTTextboxProbe";
HWND g_window{};
HWND g_textbox{};
HWND g_setButton{};
bool g_showEight{};
int g_textLength{};
bool g_setTextSucceeded{};
bool g_controlFound{};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) noexcept {
    switch (message) {
    case WM_CREATE: {
        g_textbox = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            24, 52, 300, 34, window, reinterpret_cast<HMENU>(101),
            GetModuleHandleW(nullptr), nullptr);
        g_setButton = CreateWindowExW(0, L"BUTTON", L"Set text to 8",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            24, 104, 140, 34, window, reinterpret_cast<HMENU>(102),
            GetModuleHandleW(nullptr), nullptr);
        if (!g_textbox || !g_setButton) return -1;
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        auto dc = reinterpret_cast<HDC>(wParam);
        if (reinterpret_cast<HWND>(lParam) == g_textbox) {
            SetTextColor(dc, RGB(0, 0, 0));
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 102 && HIWORD(wParam) == BN_CLICKED) {
            g_showEight = !g_showEight;
            g_setTextSucceeded = SetDlgItemTextW(
                window, 101, g_showEight ? L"8" : L"0") != FALSE;
            wchar_t readback[16]{};
            g_textLength = GetDlgItemTextW(window, 101, readback, 16);
            InvalidateRect(window, nullptr, TRUE);
            UpdateWindow(window);
            InvalidateRect(g_textbox, nullptr, TRUE);
            UpdateWindow(g_textbox);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (dc) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0));
            const wchar_t heading[] = L"STATIC textbox: expected black 0 on white";
            TextOutW(dc, 24, 20, heading,
                static_cast<int>(sizeof(heading) / sizeof(heading[0]) - 1));
            const wchar_t setStatus[] = L"SetDlgItemTextW success:";
            TextOutW(dc, 24, 154, setStatus,
                static_cast<int>(sizeof(setStatus) / sizeof(setStatus[0]) - 1));
            const wchar_t setResult[] = {g_setTextSucceeded ? L'1' : L'0', L'\0'};
            TextOutW(dc, 205, 154, setResult, 1);
            const wchar_t itemStatus[] = L"GetDlgItem found:";
            TextOutW(dc, 24, 176, itemStatus,
                static_cast<int>(sizeof(itemStatus) / sizeof(itemStatus[0]) - 1));
            const wchar_t itemResult[] = {g_controlFound ? L'1' : L'0', L'\0'};
            TextOutW(dc, 205, 176, itemResult, 1);
            const wchar_t status[] = L"GetDlgItemTextW length:";
            TextOutW(dc, 24, 198, status,
                static_cast<int>(sizeof(status) / sizeof(status[0]) - 1));
            const wchar_t result[] = {g_textLength == 1 ? L'1' : L'0', L'\0'};
            TextOutW(dc, 205, 198, result, 1);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

extern "C" void _start() noexcept {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    windowClass.lpszClassName = kClassName;
    if (!RegisterClassExW(&windowClass)) return;
    g_window = CreateWindowExW(0, kClassName, L"Textbox paint probe",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 64, 64, 500, 310,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!g_window) return;
    // Like Calc's WM_INITDIALOG callback, set the child text after all
    // controls exist and the parent HWND has finished creation.
    g_controlFound = GetDlgItem(g_window, 101) != nullptr;
    g_setTextSucceeded = SetDlgItemTextW(g_window, 101, L"0") != FALSE;
    wchar_t readback[16]{};
    g_textLength = GetDlgItemTextW(g_window, 101, readback, 16);
    InvalidateRect(g_window, nullptr, TRUE);
    UpdateWindow(g_window);
    InvalidateRect(g_textbox, nullptr, TRUE);
    UpdateWindow(g_textbox);
    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
