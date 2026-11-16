/**
 * @file theme_test_app.cpp
 * @brief Ring 3 consumer for WhiteSur, UxTheme and Win32 control verification.
 *
 * This application deliberately uses the production Win32 and common-control
 * classes. It is a test client, not an alternate control renderer.
 */

#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <windows.h>
#include "micant/shared/native_uxtheme_api.h"
#include "gui_common.hpp"

extern "C" __declspec(dllimport) int32_t InitCommonControlsEx(const void*) noexcept;

namespace {
constexpr wchar_t kClassName[] = L"MicaNTThemeTestWindow";
constexpr UINT kButtonId = 1001;
constexpr UINT kCheckId = 1002;
constexpr UINT kRadioAId = 1003;
constexpr UINT kRadioBId = 1004;
constexpr UINT kEditId = 1005;
constexpr UINT kScrollId = 1006;
constexpr UINT kTabId = 1007;
constexpr UINT kProgressId = 1008;
HWND g_window{};
HWND g_progress{};
uint32_t g_progressValue{};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        const struct { uint32_t size, classes; } init{sizeof(init), 0x00000028u};
        if (!InitCommonControlsEx(&init)) return -1;
        CreateWindowExW(0, L"BUTTON", L"BUTTON: click me",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 48, 150, 32,
            window, reinterpret_cast<HMENU>(kButtonId), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Checkbox",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 190, 48, 150, 32,
            window, reinterpret_cast<HMENU>(kCheckId), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Radio A",
            WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON, 20, 92, 120, 28,
            window, reinterpret_cast<HMENU>(kRadioAId), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Radio B",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 145, 92, 120, 28,
            window, reinterpret_cast<HMENU>(kRadioBId), nullptr, nullptr);
        CreateWindowExW(0, L"EDIT", L"Edit control",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 20, 132, 300, 30,
            window, reinterpret_cast<HMENU>(kEditId), nullptr, nullptr);
        CreateWindowExW(0, L"SCROLLBAR", L"",
            WS_CHILD | WS_VISIBLE | SBS_HORZ, 20, 174, 300, 20,
            window, reinterpret_cast<HMENU>(kScrollId), nullptr, nullptr);
        SendMessageW(window, WM_USER + 1, 0, 0);
        g_progress = CreateWindowExW(0, L"msctls_progress32", L"",
            WS_CHILD | WS_VISIBLE, 20, 270, 300, 24,
            window, reinterpret_cast<HMENU>(kProgressId), nullptr, nullptr);
        if (g_progress) {
            SendMessageW(g_progress, 0x0401u, 0, MAKELPARAM(0, 100));
            SendMessageW(g_progress, 0x0402u, 0, 35);
        }
        CreateWindowExW(0, L"STATIC", L"Production Win32 controls — theme_test.exe",
            WS_CHILD | WS_VISIBLE, 20, 16, 440, 24, window, nullptr, nullptr, nullptr);
        return 0;
    }
    case WM_USER + 1: {
        HWND tab = CreateWindowExW(0, L"SysTabControl32", L"",
            WS_CHILD | WS_VISIBLE, 20, 210, 300, 44,
            window, reinterpret_cast<HMENU>(kTabId), nullptr, nullptr);
        if (tab) {
            struct TabItem { uint32_t mask; int32_t state; uint32_t stateMask;
                wchar_t* text; int32_t image; intptr_t lParam; };
            TabItem first{1, 0, 0, const_cast<wchar_t*>(L"General"), -1, 0};
            TabItem second{1, 0, 0, const_cast<wchar_t*>(L"Details"), -1, 0};
            SendMessageW(tab, 0x133Eu, 0, reinterpret_cast<LPARAM>(&first));
            SendMessageW(tab, 0x133Eu, 1, reinterpret_cast<LPARAM>(&second));
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kButtonId && HIWORD(wParam) == BN_CLICKED) {
            g_progressValue = (g_progressValue + 20) % 101;
            if (g_progress) SendMessageW(g_progress, 0x0402u, 0, g_progressValue);
        }
        return 0;
    case WM_HSCROLL:
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
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = kClassName;
    if (!RegisterClassExW(&klass)) return;
    g_window = CreateWindowExW(0, kClassName, L"MicaNT Theme Test",
        WS_OVERLAPPEDWINDOW, 80, 80, 520, 360, nullptr, nullptr,
        klass.hInstance, nullptr);
    if (!g_window) return;
    HMENU menu = CreateMenu();
    if (menu) {
        AppendMenuW(menu, 0, 1, L"Theme test");
        SetMenu(g_window, menu);
    }
    MicaThemeHandle theme = OpenThemeData(g_window, L"BUTTON");
    if (theme) (void)CloseThemeData(theme);
    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
    micant_gui::PrintLine("[MicaNT ThemeTest] Production control window started");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
