/**
 * @file shlwapi.cpp
 * @brief Selected Shell Lightweight Utility exports for MicaNT.
 *
 * Pure path parsing functions are implemented here. File-system predicates
 * remain unsupported until the user-mode file-information path is connected.
 */

#include <windows.h>
#include <shlwapi.h>
#include <cstdint>
#include <cwchar>
#include <iterator>

extern "C" {
__declspec(dllimport) DWORD WINAPI GetFileAttributesW(LPCWSTR);
}

namespace {

bool write_number(wchar_t* output, size_t capacity, uint64_t value,
                  unsigned decimals, const wchar_t* suffix) noexcept {
    if (!output || !capacity) return false;
    wchar_t digits[32]{};
    size_t count = 0;
    do { digits[count++] = static_cast<wchar_t>(L'0' + value % 10); value /= 10; }
    while (value && count < std::size(digits));
    while (count <= decimals) digits[count++] = L'0';
    size_t used = 0;
    for (size_t index = count; index; --index) {
        if (decimals && index == decimals) {
            if (used + 1 >= capacity) return false;
            output[used++] = L'.';
        }
        if (used + 1 >= capacity) return false;
        output[used++] = digits[index - 1];
    }
    while (*suffix) {
        if (used + 1 >= capacity) return false;
        output[used++] = *suffix++;
    }
    output[used] = L'\0';
    return true;
}

template <typename Char>
const Char* filename_part(const Char* path) noexcept {
    if (!path) return nullptr;
    const Char* result = path;
    for (const Char* current = path; *current; ++current) {
        if (*current == static_cast<Char>('/') ||
            *current == static_cast<Char>('\\') ||
            *current == static_cast<Char>(':')) {
            result = current + 1;
        }
    }
    return result;
}

template <typename Char>
const Char* extension_part(const Char* path) noexcept {
    if (!path) return nullptr;
    const Char* filename = filename_part(path);
    const Char* dot = nullptr;
    for (const Char* current = filename; *current; ++current) {
        if (*current == static_cast<Char>('.')) dot = current;
    }
    if (dot) return dot;
    const Char* end = filename;
    while (*end) ++end;
    return end;
}

template <typename Char>
bool append_bounded(Char* destination, size_t& length, const Char* source) noexcept {
    if (!source) return true;
    while (*source) {
        if (length + 1 >= MAX_PATH) return false;
        destination[length++] = *source++;
    }
    destination[length] = 0;
    return true;
}

template <typename Char>
Char* combine_path(Char* destination, const Char* directory, const Char* file) noexcept {
    if (!destination) return nullptr;
    Char temporary[MAX_PATH];
    temporary[0] = 0;
    size_t length = 0;
    if (file && (*file == static_cast<Char>('/') || *file == static_cast<Char>('\\'))) {
        directory = nullptr;
    }
    if (!append_bounded(temporary, length, directory)) return nullptr;
    if (length && file && *file && temporary[length - 1] != static_cast<Char>('/') &&
        temporary[length - 1] != static_cast<Char>('\\')) {
        if (length + 1 >= MAX_PATH) return nullptr;
        temporary[length++] = static_cast<Char>('\\');
    }
    temporary[length] = 0;
    if (!append_bounded(temporary, length, file)) return nullptr;
    for (size_t i = 0; i <= length; ++i) destination[i] = temporary[i];
    return destination;
}

template <typename Char>
BOOL remove_file_spec(Char* path) noexcept {
    if (!path || !*path) return FALSE;
    Char* last_separator = nullptr;
    for (Char* current = path; *current; ++current) {
        if (*current == static_cast<Char>('/') || *current == static_cast<Char>('\\')) {
            last_separator = current;
        }
    }
    if (!last_separator) return FALSE;
    if (last_separator == path) {
        last_separator[1] = 0;
        return TRUE;
    }
    // Preserve a drive root such as "C:\\".
    if (last_separator == path + 2 && path[1] == static_cast<Char>(':')) {
        last_separator[1] = 0;
    } else {
        *last_separator = 0;
    }
    return TRUE;
}

} // namespace

namespace {

template <typename Char>
bool path_exists(const Char* path, bool directory) noexcept {
    if (!path || !*path) return false;
    if constexpr (sizeof(Char) == sizeof(wchar_t)) {
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path));
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory);
    } else {
        wchar_t wide[MAX_PATH];
        const int converted = MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH);
        if (!converted) return false;
        const DWORD attributes = GetFileAttributesW(wide);
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory);
    }
}

template <typename Char>
bool compact_path(Char* destination, const Char* source, uint32_t maxChars) noexcept {
    if (!source || !destination) return false;
    destination[0] = 0;
    if (maxChars < 2) return true;
    uint32_t length = 0;
    while (source[length] && length + 1 < MAX_PATH) ++length;
    if (source[length]) return false;
    if (length + 1 < maxChars) {
        for (uint32_t i = 0; i <= length; ++i) destination[i] = source[i];
        return true;
    }
    const Char* file = filename_part(source);
    const uint32_t fileLength = length - static_cast<uint32_t>(file - source) + 1;
    const Char dots[] = {static_cast<Char>('.'), static_cast<Char>('.'),
                         static_cast<Char>('.'), 0};
    if (fileLength == length + 1) {
        if (maxChars <= 4) {
            for (uint32_t i = 0; i + 1 < maxChars; ++i) destination[i] = dots[0];
            destination[maxChars - 1] = 0;
            return true;
        }
        const uint32_t keep = maxChars - 4;
        for (uint32_t i = 0; i < keep; ++i) destination[i] = file[i];
        for (uint32_t i = 0; i < 4; ++i) destination[keep + i] = dots[i];
        return true;
    }
    const Char* separator = file - 1;
    if (fileLength + 3 > maxChars) {
        if (maxChars <= 4) {
            for (uint32_t i = 0; i + 1 < maxChars; ++i) destination[i] = dots[0];
            destination[maxChars - 1] = 0;
            return true;
        }
        destination[0] = dots[0]; destination[1] = dots[1]; destination[2] = dots[2];
        destination[3] = *separator;
        const uint32_t keep = maxChars - 8;
        for (uint32_t i = 0; i < keep; ++i) destination[4 + i] = file[i];
        destination[4 + keep] = dots[0]; destination[5 + keep] = dots[1];
        destination[6 + keep] = dots[2]; destination[7 + keep] = 0;
        return true;
    }
    const uint32_t prefix = maxChars - fileLength - 3;
    for (uint32_t i = 0; i < prefix; ++i) destination[i] = source[i];
    destination[prefix] = dots[0]; destination[prefix + 1] = dots[1];
    destination[prefix + 2] = dots[2];
    for (uint32_t i = 0; i < fileLength; ++i) destination[prefix + 3 + i] = separator[i];
    return true;
}

template <typename Char>
bool remove_extension(Char* path) noexcept {
    if (!path) return false;
    Char* filename = const_cast<Char*>(filename_part(path));
    Char* dot = nullptr;
    for (Char* p = filename; *p; ++p) if (*p == static_cast<Char>('.')) dot = p;
    if (!dot) return false;
    *dot = 0;
    return true;
}

} // namespace

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) BOOL WINAPI PathFileExistsA(LPCSTR path) { return path_exists(path, false) || path_exists(path, true); }
__declspec(dllexport) BOOL WINAPI PathFileExistsW(LPCWSTR path) { return path_exists(path, false) || path_exists(path, true); }
__declspec(dllexport) BOOL WINAPI PathIsDirectoryA(LPCSTR path) { return path_exists(path, true); }
__declspec(dllexport) BOOL WINAPI PathIsDirectoryW(LPCWSTR path) { return path_exists(path, true); }

__declspec(dllexport) BOOL WINAPI PathCompactPathExA(LPSTR destination, LPCSTR source,
                                                       UINT maxChars, DWORD) {
    if (!destination || !source) return FALSE;
    wchar_t wideSource[MAX_PATH];
    wchar_t wideDestination[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, source, -1, wideSource, MAX_PATH) ||
        !compact_path(wideDestination, wideSource, maxChars) ||
        !WideCharToMultiByte(CP_ACP, 0, wideDestination, -1, destination,
                             static_cast<int>(maxChars), nullptr, nullptr)) {
        destination[0] = 0;
        return FALSE;
    }
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI PathCompactPathExW(LPWSTR destination, LPCWSTR source,
                                                       UINT maxChars, DWORD) {
    return compact_path(destination, source, maxChars) ? TRUE : FALSE;
}

__declspec(dllexport) LPSTR WINAPI PathFindFileNameA(LPCSTR path) {
    return const_cast<LPSTR>(filename_part(path));
}

__declspec(dllexport) LPWSTR WINAPI PathFindFileNameW(LPCWSTR path) {
    return const_cast<LPWSTR>(filename_part(path));
}

__declspec(dllexport) LPSTR WINAPI PathFindExtensionA(LPCSTR path) {
    return const_cast<LPSTR>(extension_part(path));
}

__declspec(dllexport) LPWSTR WINAPI PathFindExtensionW(LPCWSTR path) {
    return const_cast<LPWSTR>(extension_part(path));
}

__declspec(dllexport) LPSTR WINAPI PathCombineA(LPSTR destination, LPCSTR directory, LPCSTR file) {
    return combine_path(destination, directory, file);
}

__declspec(dllexport) LPWSTR WINAPI PathCombineW(LPWSTR destination, LPCWSTR directory, LPCWSTR file) {
    return combine_path(destination, directory, file);
}

__declspec(dllexport) BOOL WINAPI PathRemoveFileSpecA(LPSTR path) {
    return remove_file_spec(path);
}

__declspec(dllexport) BOOL WINAPI PathRemoveFileSpecW(LPWSTR path) {
    return remove_file_spec(path);
}

__declspec(dllexport) void WINAPI PathRemoveExtensionA(LPSTR path) {
    (void)remove_extension(path);
}

__declspec(dllexport) void WINAPI PathRemoveExtensionW(LPWSTR path) {
    (void)remove_extension(path);
}

__declspec(dllexport) LPWSTR WINAPI StrFormatByteSizeW(LONGLONG bytes,
        LPWSTR output, UINT outputChars) {
    if (!output || !outputChars) return nullptr;
    const bool negative = bytes < 0;
    const uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(bytes + 1)) + 1u : static_cast<uint64_t>(bytes);
    static constexpr const wchar_t* units[] = {
        L" bytes", L" KB", L" MB", L" GB", L" TB", L" PB"};
    uint64_t unit = 1;
    size_t unitIndex = 0;
    while (unitIndex + 1 < std::size(units) && magnitude >= unit * 1024u) {
        unit *= 1024u;
        ++unitIndex;
    }
    const unsigned decimals = unitIndex == 0 ? 0u : 2u;
    const uint64_t scaled = decimals
        ? (magnitude / unit) * 100u + ((magnitude % unit) * 100u + unit / 2u) / unit
        : magnitude;
    size_t offset = 0;
    if (negative) {
        if (outputChars < 2) { output[0] = L'\0'; return nullptr; }
        output[offset++] = L'-';
    }
    if (!write_number(output + offset, outputChars - offset, scaled, decimals,
            units[unitIndex])) {
        output[0] = L'\0';
        return nullptr;
    }
    return output;
}

} // extern "C"
