#include "micant/um/subsystems/winuser/uxtheme.hpp"
#include "micant/um/subsystems/theme/native_theme.hpp"
#include "micant/um/subsystems/theme/window_decoration_assets.hpp"
#include "micant/shared/native_nt_api.hpp"

#include <cstdlib>
#include <new>
#include <__verbose_abort>

_LIBCPP_BEGIN_NAMESPACE_STD
[[noreturn]] void __libcpp_verbose_abort(const char*, ...) noexcept {
    // STL contract failures are fatal in this freestanding Ring 3 DLL; never
    // continue with corrupted parser or image-decoder state.
    for (;;) __asm__ __volatile__("pause" ::: "memory");
}
_LIBCPP_END_NAMESPACE_STD

namespace {
using namespace micant::um::theme;

constexpr size_t kCssCapacity = kMaxCssBytes;
uint8_t g_cssBytes[kCssCapacity]{};
MicaThemeSnapshot g_candidateSnapshot{};
MicaThemeSnapshot g_activeSnapshot{};
MicaThemeCaptionImageSet g_candidateImages{};
MicaThemeCaptionImageSet g_activeImages{};
char g_activeProfile[96]{};
uint32_t g_themeLoadLock{};
bool g_themeInitialized{};

struct ThemeLoadGuard {
    ThemeLoadGuard() noexcept {
        while (__atomic_exchange_n(&g_themeLoadLock, 1u, __ATOMIC_ACQUIRE))
            __asm__ __volatile__("pause" ::: "memory");
    }
    ~ThemeLoadGuard() noexcept {
        __atomic_store_n(&g_themeLoadLock, 0u, __ATOMIC_RELEASE);
    }
};

bool append(wchar_t* path, size_t capacity, size_t& cursor,
            const wchar_t* suffix) noexcept {
    for (size_t index = 0; suffix[index]; ++index) {
        if (cursor + 1 >= capacity) return false;
        path[cursor++] = suffix[index];
    }
    path[cursor] = L'\0';
    return true;
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

int32_t readFile(const wchar_t* path, void* output, size_t capacity,
                 size_t& bytesRead) noexcept {
    bytesRead = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 40;
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize)) {
        CloseHandle(file);
        return 41;
    }
    if (fileSize.QuadPart <= 0) {
        CloseHandle(file);
        return 42;
    }
    if (static_cast<uint64_t>(fileSize.QuadPart) > capacity) {
        CloseHandle(file);
        return 43;
    }
    const size_t expected = static_cast<size_t>(fileSize.QuadPart);
    while (bytesRead < expected) {
        const DWORD request = static_cast<DWORD>(expected - bytesRead > 65536
            ? 65536 : expected - bytesRead);
        DWORD chunkRead{};
        if (!ReadFile(file, static_cast<uint8_t*>(output) + bytesRead,
                      request, &chunkRead, nullptr) || chunkRead != request) {
            CloseHandle(file);
            bytesRead = 0;
            return 44;
        }
        bytesRead += static_cast<size_t>(chunkRead);
    }
    if (!bytesRead) {
        CloseHandle(file);
        bytesRead = 0;
        return 42;
    }
    CloseHandle(file);
    return 1;
}

bool resolveProfile(const char* requested, char (&profile)[96]) noexcept {
    size_t length{};
    if (requested) {
        while (requested[length] && length < sizeof(profile) - 1) {
            profile[length] = requested[length];
            ++length;
        }
        if (requested[length]) return false;
    } else {
        wchar_t path[512]{};
        size_t cursor{};
        if (!appendSystemRoot(path, std::size(path), cursor) ||
            !append(path, std::size(path), cursor, L"\\System\\Themes\\active.profile"))
            return false;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER fileSize{};
        DWORD bytes{};
        const bool readOk = GetFileSizeEx(file, &fileSize) && fileSize.QuadPart > 0 &&
            fileSize.QuadPart < static_cast<int64_t>(sizeof(profile)) &&
            ReadFile(file, profile, static_cast<DWORD>(fileSize.QuadPart), &bytes, nullptr) &&
            bytes == static_cast<DWORD>(fileSize.QuadPart);
        CloseHandle(file);
        if (!readOk) return false;
        length = bytes;
    }
    while (length && (profile[length - 1] == '\r' || profile[length - 1] == '\n' ||
                       profile[length - 1] == ' ' || profile[length - 1] == '\t')) --length;
    size_t first{};
    while (first < length && (profile[first] == ' ' || profile[first] == '\t')) ++first;
    if (first) {
        for (size_t index = first; index < length; ++index) profile[index - first] = profile[index];
        length -= first;
    }
    if (!length || length > 80) return false;
    bool previousSlash = true;
    size_t segmentLength{};
    bool segmentDots = true;
    for (size_t index = 0; index < length; ++index) {
        const char value = profile[index];
        if (value == '/') {
            if (previousSlash || !segmentLength || (segmentLength == 2 && segmentDots)) return false;
            previousSlash = true;
            segmentLength = 0;
            segmentDots = true;
            continue;
        }
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-')) return false;
        previousSlash = false;
        segmentDots = false;
        ++segmentLength;
    }
    if (previousSlash || (segmentLength == 2 && segmentDots)) return false;
    profile[length] = '\0';
    return true;
}

int32_t loadGtkTheme(const char* requested) noexcept {
    ThemeLoadGuard guard;
    char profile[96]{};
    if (!resolveProfile(requested, profile)) return 2;
    wchar_t directory[512]{};
    size_t cursor{};
    if (!appendSystemRoot(directory, std::size(directory), cursor) ||
        !append(directory, std::size(directory), cursor, L"\\System\\Themes\\")) return 3;
    for (size_t index = 0; profile[index]; ++index) {
        if (cursor + 1 >= std::size(directory)) return 3;
        directory[cursor++] = profile[index] == '/' ? L'\\' : static_cast<wchar_t>(profile[index]);
    }
    directory[cursor] = L'\0';
    wchar_t cssPath[512]{};
    size_t cssCursor{};
    while (cssCursor <= cursor) { cssPath[cssCursor] = directory[cssCursor]; ++cssCursor; }
    // Reuse the directory terminator when appending the filename; retaining
    // it would make CreateFileW open the directory rather than gtk.css.
    cssCursor = cursor;
    if (!append(cssPath, std::size(cssPath), cssCursor, L"\\gtk.css")) return 3;
    size_t cssLength{};
    const int32_t cssReadStatus = readFile(cssPath, g_cssBytes,
                                           sizeof(g_cssBytes), cssLength);
    if (cssReadStatus != 1) return cssReadStatus;

    CompiledCss parsed{};
    CssParseError parseError{};
    if (compileGtkCss(std::string_view(reinterpret_cast<const char*>(g_cssBytes), cssLength),
                      parsed, &parseError) != CssStatus::Success) return 5;
    NativeTheme nativeTheme{};
    if (!buildNativeTheme(parsed, nativeTheme, {36, 36, 36, 255}, {})) return 6;
    g_candidateSnapshot = makeThemeSnapshot(nativeTheme);
    if (!MicaThemeSnapshotIsValid(&g_candidateSnapshot, sizeof(g_candidateSnapshot))) return 7;

    micant::um::theme::LoadWhiteSurCaptionImages(g_candidateImages, directory,
        std::string_view(profile).find("Dark") != std::string_view::npos);
    if (!MicaWin32kSetThemeSnapshot(&g_candidateSnapshot, &g_candidateImages)) return 8;

    g_activeSnapshot = g_candidateSnapshot;
    g_activeImages = g_candidateImages;
    for (size_t index = 0; index < sizeof(g_activeProfile); ++index)
        g_activeProfile[index] = profile[index];
    g_themeInitialized = true;
    return 1;
}
}

void* operator new(size_t size) {
    return malloc(size ? size : 1);
}
void operator delete(void* block) noexcept {
    free(block);
}
void operator delete(void* block, size_t) noexcept { ::operator delete(block); }

extern "C" {

bool DllMain(void*, unsigned long, void*) noexcept { return true; }

__declspec(dllexport) int32_t WINAPI MicaUxThemeInitialize(void) noexcept {
    return loadGtkTheme(nullptr);
}
__declspec(dllexport) int32_t WINAPI MicaUxThemeSelectProfile(const char* profile) noexcept {
    if (!profile) return 0;
    return loadGtkTheme(profile);
}
__declspec(dllexport) int32_t WINAPI MicaUxThemeGetCurrentProfile(
        char* buffer, size_t capacity) noexcept {
    if (!buffer || !capacity) return 0;
    char persistedProfile[96]{};
    const char* current = g_themeInitialized ? g_activeProfile : persistedProfile;
    if (!g_themeInitialized && !resolveProfile(nullptr, persistedProfile)) return 0;
    size_t length{};
    while (length < sizeof(g_activeProfile) && current[length]) ++length;
    if (length + 1 > capacity) return 0;
    for (size_t index = 0; index <= length; ++index) buffer[index] = current[index];
    return static_cast<int32_t>(length);
}
__declspec(dllexport) int32_t WINAPI MicaUxThemePublishSnapshot(
        const void* input) noexcept {
    ThemeLoadGuard guard;
    if (!input) return 0;
    const auto& snapshot = *static_cast<const MicaThemeSnapshot*>(input);
    if (!MicaThemeSnapshotIsValid(&snapshot, sizeof(snapshot)) ||
        !MicaWin32kSetThemeSnapshot(&snapshot, &g_activeImages)) return 0;
    g_activeSnapshot = snapshot;
    return 1;
}

__declspec(dllexport) void* WINAPI OpenThemeData(void* hwnd, const wchar_t* classList) noexcept {
    return micant::uxtheme::OpenThemeData(hwnd, classList);
}
__declspec(dllexport) void* WINAPI OpenThemeDataForDpi(void* hwnd, const wchar_t* classList,
                                                        uint32_t dpi) noexcept {
    return micant::uxtheme::OpenThemeDataForDpi(hwnd, classList, dpi);
}
__declspec(dllexport) int32_t WINAPI CloseThemeData(void* theme) noexcept {
    return micant::uxtheme::CloseThemeData(theme);
}
__declspec(dllexport) int32_t WINAPI GetThemeColor(void* theme, int32_t part, int32_t state,
                                                   int32_t property, uint32_t* color) noexcept {
    return micant::uxtheme::GetThemeColor(theme, part, state, property, color);
}
__declspec(dllexport) int32_t WINAPI GetThemeFont(void* theme, void* dc, int32_t part,
        int32_t state, int32_t property, void* font) noexcept {
    return micant::uxtheme::GetThemeFont(theme, dc, part, state, property, font);
}
__declspec(dllexport) int32_t WINAPI GetThemePartSize(void* theme, void* dc, int32_t part,
                                                      int32_t state, void* rect, int32_t sizeType,
                                                      void* size) noexcept {
    return micant::uxtheme::GetThemePartSize(theme, dc, part, state, rect, sizeType, size);
}
__declspec(dllexport) int32_t WINAPI GetThemeMargins(void* theme, void* dc, int32_t part,
                                                     int32_t state, int32_t property,
                                                     const void* rect,
                                                     micant::uxtheme::MARGINS* margins) noexcept {
    return micant::uxtheme::GetThemeMargins(theme, dc, part, state, property, rect, margins);
}
__declspec(dllexport) int32_t WINAPI GetThemeInt(void* theme, int32_t part, int32_t state,
                                                 int32_t property, int32_t* value) noexcept {
    return micant::uxtheme::GetThemeInt(theme, part, state, property, value);
}
__declspec(dllexport) int32_t WINAPI DrawThemeBackground(void* theme, void* dc, int32_t part,
                                                         int32_t state, const void* rect,
                                                         const void* clip) noexcept {
    return micant::uxtheme::DrawThemeBackground(theme, dc, part, state, rect, clip);
}
__declspec(dllexport) int32_t WINAPI GetThemeBackgroundContentRect(void* theme, void* dc,
        int32_t part, int32_t state, const void* bounds, void* content) noexcept {
    return micant::uxtheme::GetThemeBackgroundContentRect(theme, dc, part, state, bounds, content);
}
__declspec(dllexport) int32_t WINAPI GetThemeBackgroundExtent(void* theme, void* dc,
        int32_t part, int32_t state, const void* content, void* extent) noexcept {
    return micant::uxtheme::GetThemeBackgroundExtent(theme, dc, part, state, content, extent);
}
__declspec(dllexport) int32_t WINAPI DrawThemeParentBackground(void* hwnd, void* dc,
        const void* rect) noexcept {
    return micant::uxtheme::DrawThemeParentBackground(hwnd, dc, rect);
}
__declspec(dllexport) int32_t WINAPI DrawThemeText(void* theme, void* dc, int32_t part,
        int32_t state, const wchar_t* text, int32_t charCount, uint32_t flags,
        uint32_t flags2, const void* rect) noexcept {
    return micant::uxtheme::DrawThemeText(theme, dc, part, state, text, charCount,
                                          flags, flags2, rect);
}
__declspec(dllexport) int32_t WINAPI DrawThemeTextEx(void* theme, void* dc, int32_t part,
        int32_t state, const wchar_t* text, int32_t charCount, uint32_t flags,
        void* rect, void* options) noexcept {
    return micant::uxtheme::DrawThemeTextEx(theme, dc, part, state, text, charCount,
                                            flags, rect, options);
}
__declspec(dllexport) int32_t WINAPI SetWindowTheme(void* hwnd, const wchar_t* subAppName,
        const wchar_t* subIdList) noexcept {
    return micant::uxtheme::SetWindowTheme(hwnd, subAppName, subIdList);
}
__declspec(dllexport) int32_t WINAPI EnableThemeDialogTexture(void* hwnd,
        uint32_t flags) noexcept {
    return micant::uxtheme::EnableThemeDialogTexture(hwnd, flags);
}
__declspec(dllexport) int32_t WINAPI GetThemeTransitionDuration(void* theme, int32_t part,
        int32_t fromState, int32_t toState, int32_t property, uint32_t* duration) noexcept {
    return micant::uxtheme::GetThemeTransitionDuration(theme, part, fromState,
                                                       toState, property, duration);
}
__declspec(dllexport) void* WINAPI BeginBufferedAnimation(void* hwnd, void* targetDc,
        const void* targetRect, int32_t format, void* paintParams, void* animationParams,
        void** fromDc, void** toDc) noexcept {
    return micant::uxtheme::BeginBufferedAnimation(hwnd, targetDc, targetRect, format,
        paintParams, animationParams, fromDc, toDc);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintRenderAnimation(void* hwnd,
        void* targetDc) noexcept {
    return micant::uxtheme::BufferedPaintRenderAnimation(hwnd, targetDc);
}
__declspec(dllexport) uint32_t WINAPI GetThemeSysColor(void* theme, int32_t colorId) noexcept {
    return micant::uxtheme::GetThemeSysColor(theme, colorId);
}
__declspec(dllexport) int32_t WINAPI GetThemeSysFont(void* theme, int32_t fontId,
        void* font) noexcept {
    return micant::uxtheme::GetThemeSysFont(theme, fontId, font);
}
__declspec(dllexport) int32_t WINAPI GetThemeBackgroundRegion(void* theme, void* dc,
        int32_t part, int32_t state, const void* rect, void** region) noexcept {
    return micant::uxtheme::GetThemeBackgroundRegion(theme, dc, part, state, rect, region);
}
__declspec(dllexport) int32_t WINAPI GetThemeBool(void* theme, int32_t part, int32_t state,
        int32_t property, int32_t* value) noexcept {
    return micant::uxtheme::GetThemeBool(theme, part, state, property, value);
}
__declspec(dllexport) int32_t WINAPI GetThemePropertyOrigin(void* theme, int32_t part,
        int32_t state, int32_t property, int32_t* origin) noexcept {
    return micant::uxtheme::GetThemePropertyOrigin(theme, part, state, property, origin);
}
__declspec(dllexport) int32_t WINAPI GetThemeEnumValue(void* theme, int32_t part,
        int32_t state, int32_t property, int32_t* value) noexcept {
    return micant::uxtheme::GetThemeEnumValue(theme, part, state, property, value);
}
__declspec(dllexport) int32_t WINAPI GetCurrentThemeName(wchar_t* themeFile,
        int32_t maxThemeChars, wchar_t* colorName, int32_t maxColorChars,
        wchar_t* sizeName, int32_t maxSizeChars) noexcept {
    return micant::uxtheme::GetCurrentThemeName(themeFile, maxThemeChars, colorName,
                                               maxColorChars, sizeName, maxSizeChars);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintInit() noexcept {
    return micant::uxtheme::BufferedPaintInit();
}
__declspec(dllexport) int32_t WINAPI BufferedPaintUnInit() noexcept {
    return micant::uxtheme::BufferedPaintUnInit();
}
__declspec(dllexport) void* WINAPI BeginBufferedPaint(void* targetDc, const void* targetRect,
        int32_t format, void* paintParams, void** paintDc) noexcept {
    return micant::uxtheme::BeginBufferedPaint(targetDc, targetRect, format,
                                               paintParams, paintDc);
}
__declspec(dllexport) int32_t WINAPI EndBufferedPaint(void* buffer,
        int32_t updateTarget) noexcept {
    return micant::uxtheme::EndBufferedPaint(buffer, updateTarget);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintSetAlpha(void* buffer,
        const void* targetRect, uint8_t alpha) noexcept {
    return micant::uxtheme::BufferedPaintSetAlpha(buffer, targetRect, alpha);
}
__declspec(dllexport) int32_t WINAPI DrawThemeBackgroundEx(void* theme, void* dc, int32_t part,
        int32_t state, const void* rect, const void* options) noexcept {
    return micant::uxtheme::DrawThemeBackgroundEx(theme, dc, part, state, rect, options);
}
__declspec(dllexport) int32_t WINAPI IsThemeActive() noexcept {
    return micant::uxtheme::IsThemeActive();
}
__declspec(dllexport) int32_t WINAPI IsAppThemed() noexcept {
    return micant::uxtheme::IsAppThemed();
}
__declspec(dllexport) int32_t WINAPI IsThemePartDefined(void* theme, int32_t part,
                                                       int32_t state) noexcept {
    return micant::uxtheme::IsThemePartDefined(theme, part, state);
}
__declspec(dllexport) int32_t WINAPI IsThemeBackgroundPartiallyTransparent(void* theme,
        int32_t part, int32_t state) noexcept {
    return micant::uxtheme::IsThemeBackgroundPartiallyTransparent(theme, part, state);
}
__declspec(dllexport) int32_t WINAPI EndBufferedAnimation(void* animation,
        int32_t updateTarget) noexcept {
    return micant::uxtheme::EndBufferedAnimation(animation, updateTarget);
}
__declspec(dllexport) int32_t WINAPI BufferedPaintStopAllAnimations(void* hwnd) noexcept {
    return micant::uxtheme::BufferedPaintStopAllAnimations(hwnd);
}

}
