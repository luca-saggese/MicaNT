/**
 * @file shlwapi.cpp
 * @brief Selected Shell Lightweight Utility exports for MicaNT.
 *
 * Pure path parsing functions are implemented here. File-system predicates
 * remain unsupported until the user-mode file-information path is connected.
 */

#include <windows.h>
#include <shlwapi.h>

namespace {

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

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

__declspec(dllexport) BOOL WINAPI PathFileExistsA(LPCSTR) { return FALSE; }
__declspec(dllexport) BOOL WINAPI PathFileExistsW(LPCWSTR) { return FALSE; }
__declspec(dllexport) BOOL WINAPI PathIsDirectoryA(LPCSTR) { return FALSE; }
__declspec(dllexport) BOOL WINAPI PathIsDirectoryW(LPCWSTR) { return FALSE; }

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

} // extern "C"
