// The Doom WAD file-class adapter uses the guest's real NT file and read
// services. It exists because doomgeneric's generic class is stdio based.
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include "w_file.h"

extern "C" {
__declspec(dllimport) void* __stdcall CreateFileW(const wchar_t*, uint32_t, uint32_t,
    void*, uint32_t, uint32_t, void*);
__declspec(dllimport) int32_t __stdcall GetFileSizeEx(void*, int64_t*);
__declspec(dllimport) int32_t __stdcall CloseHandle(void*);
__declspec(dllimport) int32_t __stdcall NtReadFile(void*, void*, void*, void*,
    void*, void*, uint32_t, int64_t*, uint32_t*);
}

extern "C" void DG_SetLastReadError(const char* message);

struct WadFile {
    wad_file_t publicFile;
    void* handle;
};
struct IoStatusBlock { uintptr_t status; uintptr_t information; };
static_assert(sizeof(IoStatusBlock) == 16);
static wad_file_t* OpenNativeWad(char* path);
static void CloseNativeWad(wad_file_t* file);
static size_t ReadNativeWad(wad_file_t* file, unsigned int offset,
                            void* buffer, size_t bytes);
extern "C" wad_file_class_t stdc_wad_file;

static wad_file_t* OpenNativeWad(char* path) {
    if (!path) return nullptr;
    wchar_t widePath[512]{};
    size_t i{};
    for (; path[i] && i + 1 < sizeof(widePath) / sizeof(widePath[0]); ++i) {
        const unsigned char byte = static_cast<unsigned char>(path[i]);
        if (byte >= 0x80) return nullptr;
        widePath[i] = static_cast<wchar_t>(byte);
    }
    if (path[i] != '\0') return nullptr;
    void* handle = CreateFileW(widePath, 0x80000000u, 1u, nullptr, 3u, 0x80u, nullptr);
    if (!handle || handle == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) return nullptr;
    int64_t size{};
    if (!GetFileSizeEx(handle, &size) || size < 12 || size > UINT32_MAX) {
        (void)CloseHandle(handle);
        return nullptr;
    }
    auto* file = static_cast<WadFile*>(std::malloc(sizeof(WadFile)));
    if (!file) { (void)CloseHandle(handle); return nullptr; }
    file->publicFile.file_class = &stdc_wad_file;
    file->publicFile.mapped = nullptr;
    file->publicFile.length = static_cast<unsigned int>(size);
    file->handle = handle;
    return &file->publicFile;
}

static void CloseNativeWad(wad_file_t* publicFile) {
    if (!publicFile) return;
    auto* file = reinterpret_cast<WadFile*>(publicFile);
    (void)CloseHandle(file->handle);
    std::free(file);
}

static size_t ReadNativeWad(wad_file_t* publicFile, unsigned int offset,
                            void* buffer, size_t bytes) {
    if (!publicFile || (!buffer && bytes) || offset > publicFile->length) {
        DG_SetLastReadError("WAD read invalid file, buffer, or offset");
        return 0;
    }
    auto* file = reinterpret_cast<WadFile*>(publicFile);
    const size_t available = publicFile->length - offset;
    if (bytes > available) bytes = available;
    if (bytes > UINT32_MAX) return 0;
    int64_t byteOffset = offset;
    IoStatusBlock io{};
    const int32_t status = NtReadFile(file->handle, nullptr, nullptr, nullptr,
        &io, buffer, static_cast<uint32_t>(bytes), &byteOffset, nullptr);
    if (io.information > bytes) {
        char message[128]{};
        (void)std::snprintf(message, sizeof(message),
            "NtReadFile invalid count status=0x%08x iosb=0x%08x off=%u len=%u info=%llu",
            static_cast<unsigned>(status), static_cast<unsigned>(io.status), offset,
            static_cast<unsigned>(bytes), static_cast<unsigned long long>(io.information));
        DG_SetLastReadError(message);
        return 0;
    }
    // NT I/O may report bytes transferred in the IOSB even when the final
    // status is an error. Preserve that count so the WAD reader can accept a
    // complete transfer and reject a short one itself.
    if (io.information) return static_cast<size_t>(io.information);
    if (status < 0 || io.status != 0) {
        char message[128]{};
        (void)std::snprintf(message, sizeof(message),
            "NtReadFile status=0x%08x iosb=0x%08x off=%u len=%u info=0",
            static_cast<unsigned>(status), static_cast<unsigned>(io.status), offset,
            static_cast<unsigned>(bytes));
        DG_SetLastReadError(message);
        return 0;
    }
    if (!io.information) {
        char message[128]{};
        (void)std::snprintf(message, sizeof(message),
            "NtReadFile empty success off=%u len=%u file=%u",
            offset, static_cast<unsigned>(bytes), publicFile->length);
        DG_SetLastReadError(message);
        return 0;
    }
    const size_t transferred = static_cast<size_t>(io.information);
    char message[128]{};
    (void)std::snprintf(message, sizeof(message),
        "WAD read returned %llu off=%u len=%u",
        static_cast<unsigned long long>(transferred), offset,
        static_cast<unsigned>(bytes));
    DG_SetLastReadError(message);
    return transferred;
}

extern "C" wad_file_class_t stdc_wad_file{
    OpenNativeWad, CloseNativeWad, ReadNativeWad
};
