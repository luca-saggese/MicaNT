// LuceOS platform backend for the upstream doomgeneric engine.
// Copyright notices for the engine remain in third_party/doomgeneric/LICENSE.
#include <cstdint>
#include <cstddef>

#include "micant/shared/win32k_client.h"
extern "C" {
#include "doomgeneric.h"
#include "doomkeys.h"
}

extern "C" {
__declspec(dllimport) uint16_t __stdcall RegisterClassExW(const MicaWin32kWindowClassEx*);
__declspec(dllimport) void* __stdcall CreateWindowExW(uint32_t, const wchar_t*, const wchar_t*, uint32_t,
    int32_t, int32_t, int32_t, int32_t, void*, void*, void*, void*);
__declspec(dllimport) int32_t __stdcall DestroyWindow(void*);
__declspec(dllimport) int32_t __stdcall ShowWindow(void*, int32_t);
__declspec(dllimport) int32_t __stdcall UpdateWindow(void*);
__declspec(dllimport) int32_t __stdcall InvalidateRect(void*, const MicaWin32kRect*, int32_t);
__declspec(dllimport) void* __stdcall BeginPaint(void*, void*);
__declspec(dllimport) int32_t __stdcall EndPaint(void*, const void*);
__declspec(dllimport) int32_t __stdcall PeekMessageW(MicaWin32kMessage*, void*, uint32_t, uint32_t, uint32_t);
__declspec(dllimport) int32_t __stdcall TranslateMessage(const MicaWin32kMessage*);
__declspec(dllimport) intptr_t __stdcall DispatchMessageW(const MicaWin32kMessage*);
__declspec(dllimport) intptr_t __stdcall DefWindowProcW(void*, uint32_t, uintptr_t, intptr_t);
__declspec(dllimport) void __stdcall PostQuitMessage(int32_t);
__declspec(dllimport) void* __stdcall GetDC(void*);
__declspec(dllimport) int32_t __stdcall ReleaseDC(void*, void*);
__declspec(dllimport) int32_t __stdcall SetWindowTextW(void*, const wchar_t*);
__declspec(dllimport) uint64_t __stdcall GetTickCount64();
__declspec(dllimport) void __stdcall Sleep(uint32_t);
__declspec(dllimport) void* __stdcall CreateCompatibleDC(void*);
__declspec(dllimport) void* __stdcall CreateDIBSection(void*, const void*, uint32_t,
    void**, void*, uint32_t);
__declspec(dllimport) void* __stdcall SelectObject(void*, void*);
__declspec(dllimport) int32_t __stdcall BitBlt(void*, int32_t, int32_t, int32_t, int32_t,
    void*, int32_t, int32_t, uint32_t);
__declspec(dllimport) int32_t __stdcall DeleteDC(void*);
__declspec(dllimport) int32_t __stdcall DeleteObject(void*);
__declspec(dllimport) void __stdcall ExitProcess(uint32_t);
void doomgeneric_Create(int, char**);
void doomgeneric_Tick();
}

namespace {
constexpr uint32_t kWmDestroy = 0x0002;
constexpr uint32_t kWmPaint = 0x000F;
constexpr uint32_t kWmClose = 0x0010;
constexpr uint32_t kWmQuit = 0x0012;
constexpr uint32_t kWmKeyDown = 0x0100;
constexpr uint32_t kWmKeyUp = 0x0101;
constexpr uint32_t kPmRemove = 1;
constexpr uint32_t kSrcCopy = 0x00CC0020;
constexpr uint32_t kWindowStyle = 0x10CF0000; // WS_OVERLAPPEDWINDOW | WS_VISIBLE
constexpr int kWidth = DOOMGENERIC_RESX;
constexpr int kHeight = DOOMGENERIC_RESY;
constexpr size_t kQueueCapacity = 64;

struct BitmapInfo {
    MicaWin32kBitmapInfoHeader header;
    uint32_t colors[3];
};
struct PaintStruct {
    void* dc;
    int32_t erase;
    MicaWin32kRect paint;
    int32_t restore;
    int32_t incrementalUpdate;
    uint8_t reserved[32];
};
static_assert(sizeof(PaintStruct) == 72);
struct KeyEvent { int pressed; unsigned char key; };

void* s_window{};
void* s_memoryDc{};
void* s_bitmap{};
void* s_oldBitmap{};
uint32_t* s_pixels{};
bool s_quit{};
KeyEvent s_keys[kQueueCapacity]{};
size_t s_keyRead{};
size_t s_keyWrite{};
uint32_t s_frameStart{};

unsigned char toDoomKey(uint32_t key) noexcept {
    switch (key) {
    case 0x1B: return KEY_ESCAPE;
    case 0x0D: return KEY_ENTER;
    case 0x25: return KEY_LEFTARROW;
    case 0x27: return KEY_RIGHTARROW;
    case 0x26: return KEY_UPARROW;
    case 0x28: return KEY_DOWNARROW;
    case 0x11: return KEY_FIRE;
    case 0x20: return KEY_USE;
    case 0x10: return KEY_RSHIFT;
    case 0x08: return KEY_BACKSPACE;
    case 0x09: return KEY_TAB;
    default:
        return key >= 'A' && key <= 'Z' ? static_cast<unsigned char>(key + ('a' - 'A'))
                                         : static_cast<unsigned char>(key);
    }
}

void queueKey(bool pressed, uint32_t virtualKey) noexcept {
    const size_t next = (s_keyWrite + 1) % kQueueCapacity;
    if (next == s_keyRead) return;
    s_keys[s_keyWrite] = {pressed ? 1 : 0, toDoomKey(virtualKey)};
    s_keyWrite = next;
}

void present(void* windowDc) noexcept {
    if (windowDc && s_memoryDc)
        (void)BitBlt(windowDc, 0, 0, kWidth, kHeight, s_memoryDc, 0, 0, kSrcCopy);
}

void releaseGraphics() noexcept {
    if (s_memoryDc && s_oldBitmap) (void)SelectObject(s_memoryDc, s_oldBitmap);
    if (s_bitmap) (void)DeleteObject(s_bitmap);
    if (s_memoryDc) (void)DeleteDC(s_memoryDc);
    s_bitmap = s_memoryDc = s_oldBitmap = nullptr;
    s_pixels = nullptr;
}

intptr_t __stdcall doomWindowProc(void* hwnd, uint32_t message,
                                 uintptr_t wParam, intptr_t lParam) noexcept {
    switch (message) {
    case kWmClose:
        (void)DestroyWindow(hwnd);
        return 0;
    case kWmDestroy:
        releaseGraphics();
        s_window = nullptr;
        s_quit = true;
        PostQuitMessage(0);
        return 0;
    case kWmKeyDown:
        if ((lParam & (1LL << 30)) == 0) queueKey(true, static_cast<uint32_t>(wParam));
        return 0;
    case kWmKeyUp:
        queueKey(false, static_cast<uint32_t>(wParam));
        return 0;
    case kWmPaint:
        {
        PaintStruct paint{};
        paint.dc = BeginPaint(hwnd, &paint);
        present(paint.dc);
        (void)EndPaint(hwnd, &paint);
        return 0;
        }
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void cleanup() noexcept {
    if (s_window) (void)DestroyWindow(s_window);
    if (s_memoryDc || s_bitmap) releaseGraphics();
    s_window = nullptr;
}
}

extern "C" void DG_ShowEngineError(const char* message) {
    if (!message) return;
    wchar_t wide[128]{};
    size_t index{};
    for (; index + 1 < sizeof(wide) / sizeof(wide[0]) && message[index]; ++index)
        wide[index] = static_cast<unsigned char>(message[index]);
    if (!s_window) {
        static const wchar_t errorClassName[] = L"LuceOS.DoomError";
        MicaWin32kWindowClassEx errorClass{};
        errorClass.cbSize = sizeof(errorClass);
        errorClass.wndProc = reinterpret_cast<void*>(&DefWindowProcW);
        errorClass.className = errorClassName;
        (void)RegisterClassExW(&errorClass);
        s_window = CreateWindowExW(0, errorClassName, wide, kWindowStyle,
                                   100, 100, 700, 180, nullptr, nullptr, nullptr, nullptr);
    }
    if (!s_window) return;
    (void)SetWindowTextW(s_window, wide);
    (void)ShowWindow(s_window, 5);
    (void)UpdateWindow(s_window);
}

extern "C" void DG_Init() {
    static const wchar_t className[] = L"LuceOS.DoomGeneric";
    static const wchar_t windowName[] = L"DOOM — Freedoom";
    MicaWin32kWindowClassEx windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.wndProc = reinterpret_cast<void*>(&doomWindowProc);
    windowClass.className = className;
    if (!RegisterClassExW(&windowClass)) {
        DG_ShowEngineError("RegisterClassExW failed");
        s_quit = true; return;
    }
    s_window = CreateWindowExW(0, className, windowName, kWindowStyle,
        80, 60, kWidth + 16, kHeight + 39, nullptr, nullptr, nullptr, nullptr);
    if (!s_window) {
        DG_ShowEngineError("CreateWindowExW failed");
        s_quit = true; return;
    }
    (void)ShowWindow(s_window, 5);
    (void)UpdateWindow(s_window);
    s_memoryDc = CreateCompatibleDC(nullptr);
    BitmapInfo info{};
    info.header.size = sizeof(info.header);
    info.header.width = kWidth;
    info.header.height = -kHeight;
    info.header.planes = 1;
    info.header.bitCount = 32;
    void* bits{};
    s_bitmap = CreateDIBSection(nullptr, &info, 0, &bits, nullptr, 0);
    if (!s_memoryDc || !s_bitmap || !bits) {
        DG_ShowEngineError("GDI DIB initialization failed");
        s_quit = true; return;
    }
    s_pixels = static_cast<uint32_t*>(bits);
    s_oldBitmap = SelectObject(s_memoryDc, s_bitmap);
    if (!s_oldBitmap) {
        DG_ShowEngineError("SelectObject failed");
        s_quit = true; return;
    }
    s_frameStart = static_cast<uint32_t>(GetTickCount64());
    (void)InvalidateRect(s_window, nullptr, 0);
    (void)UpdateWindow(s_window);
}

extern "C" void DG_DrawFrame() {
    MicaWin32kMessage message{};
    while (PeekMessageW(&message, nullptr, 0, 0, kPmRemove)) {
        if (message.message == kWmQuit) { s_quit = true; break; }
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }
    if (DG_ScreenBuffer && s_pixels)
        for (size_t i = 0; i < static_cast<size_t>(kWidth) * kHeight; ++i)
            s_pixels[i] = static_cast<uint32_t>(DG_ScreenBuffer[i]) | 0xFF000000u;
    if (s_window) (void)InvalidateRect(s_window, nullptr, 0);

    // Vanilla Doom advances at 35 tics/second. Sleep the remainder of one
    // tic using the native blocking delay so the render loop never spins.
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    const uint32_t elapsed = now - s_frameStart;
    if (elapsed < 28) Sleep(28 - elapsed);
    s_frameStart = static_cast<uint32_t>(GetTickCount64());
}

extern "C" void DG_SleepMs(uint32_t milliseconds) { Sleep(milliseconds); }
extern "C" uint32_t DG_GetTicksMs() { return static_cast<uint32_t>(GetTickCount64()); }
extern "C" int DG_GetKey(int* pressed, unsigned char* key) {
    if (!pressed || !key || s_keyRead == s_keyWrite) return 0;
    const KeyEvent event = s_keys[s_keyRead];
    s_keyRead = (s_keyRead + 1) % kQueueCapacity;
    *pressed = event.pressed;
    *key = event.key;
    return 1;
}
extern "C" void DG_SetWindowTitle(const char* title) {
    if (!s_window || !title) return;
    wchar_t wide[128]{};
    size_t index{};
    for (; index + 1 < sizeof(wide) / sizeof(wide[0]) && title[index]; ++index)
        wide[index] = static_cast<unsigned char>(title[index]);
    (void)SetWindowTextW(s_window, wide);
}

extern "C" void _start() {
    static char executable[] = "C:\\MicaNT\\System32\\doom.exe";
    static char iwadSwitch[] = "-iwad";
    static char iwadPath[] = "C:\\MicaNT\\System\\Games\\Freedoom\\freedoom1.wad";
    char* arguments[]{executable, iwadSwitch, iwadPath};
    doomgeneric_Create(3, arguments);
    while (!s_quit) doomgeneric_Tick();
    cleanup();
    ExitProcess(0);
    __builtin_unreachable();
}
