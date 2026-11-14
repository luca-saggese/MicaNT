// Bounded Ring-3 Shell32 icon extraction subset. Resource parsing and pixel
// decoding use shared validators; User32 owns published HICON objects.
#include "micant/um/subsystems/imaging/ico_decoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/native/pe.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

extern "C" {
__declspec(dllimport) void* LoadImageW(void*, const wchar_t*, uint32_t,
                                      int32_t, int32_t, uint32_t);
__declspec(dllimport) void* CreateIconFromResourceEx(const uint8_t*, uint32_t,
    int32_t, uint32_t, int32_t, int32_t, uint32_t);
__declspec(dllimport) int32_t DestroyIcon(void*);
}

namespace {
using namespace micant;
using namespace micant::um;
using namespace micant::um::imaging;

constexpr uint32_t kImageIcon = 1;
constexpr uint32_t kLoadFromFile = 0x0010;
constexpr uint32_t kGroupIconType = 14;
constexpr size_t kMaxGroups = 256;

void write16(uint8_t* output, uint16_t value) noexcept {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}
void write32(uint8_t* output, uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i) output[i] = static_cast<uint8_t>(value >> (i * 8));
}

bool equalAsciiInsensitive(const wchar_t* left, const wchar_t* right) noexcept {
    while (*left && *right) {
        wchar_t a = *left++, b = *right++;
        if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
        if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
        if (a != b) return false;
    }
    return *left == 0 && *right == 0;
}

bool isIcoPath(const wchar_t* path) noexcept {
    if (!path) return false;
    const wchar_t* extension = nullptr;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/' || *p == L':') extension = nullptr;
        else if (*p == L'.') extension = p;
    }
    return extension && equalAsciiInsensitive(extension, L".ico");
}

ImageLimits shellIconLimits() noexcept {
    ImageLimits limits{};
    limits.maxEncodedBytes = 32ull * 1024 * 1024;
    limits.maxPixels = 16ull * 1024 * 1024;
    limits.maxDecodedBytes = 64ull * 1024 * 1024;
    limits.maxIntermediateBytes = 96ull * 1024 * 1024;
    return limits;
}

bool getGroupIds(std::span<const uint8_t> file,
                 std::array<uint16_t, kMaxGroups>& ids,
                 size_t& count) noexcept {
    count = 0;
    pe::PeImageView64 image{};
    if (image.inspect(file) != NtStatus::Success) return false;
    pe::PeResourceView resources(image);
    const NtStatus status = resources.numericNames(
        {kGroupIconType, {}, false}, ids, count);
    return status == NtStatus::Success && count <= ids.size();
}

bool resolveGroup(std::span<const uint8_t> file, int32_t iconIndex,
                  uint16_t& groupId, size_t* resourceCount = nullptr) noexcept {
    std::array<uint16_t, kMaxGroups> ids{};
    size_t count{};
    if (!getGroupIds(file, ids, count)) return false;
    if (resourceCount) *resourceCount = count;
    if (iconIndex < 0) {
        const int64_t resourceId = -static_cast<int64_t>(iconIndex);
        if (!resourceId || resourceId > UINT16_MAX) return false;
        for (size_t i = 0; i < count; ++i) {
            if (ids[i] == resourceId) { groupId = ids[i]; return true; }
        }
        return false;
    }
    if (static_cast<uint32_t>(iconIndex) >= count) return false;
    groupId = ids[static_cast<size_t>(iconIndex)];
    return true;
}

void* makeIconFromPe(const wchar_t* path, int32_t iconIndex,
                     uint32_t width, uint32_t height) noexcept {
    if (!path || !*path || !width || !height) return nullptr;
    const ImageAllocator allocator = NativeImageAllocator();
    ImageLimits limits = shellIconLimits();
    ImageBuffer file{};
    if (ReadIconSourceFile(path, file, allocator, limits) != ImageStatus::Success)
        return nullptr;
    uint16_t groupId{};
    if (!resolveGroup(file.span(), iconIndex, groupId)) return nullptr;
    ImageFrame frame{};
    if (DecodePeGroupIcon(file.span(), groupId, 0, width, height,
                          frame, allocator, limits) != ImageStatus::Success)
        return nullptr;

    uint64_t xorBytes = static_cast<uint64_t>(frame.width) * frame.height * 4;
    const uint64_t andStride = ((static_cast<uint64_t>(frame.width) + 31) / 32) * 4;
    const uint64_t andBytes = andStride * frame.height;
    const uint64_t total = 40 + xorBytes + andBytes;
    if (!frame.width || !frame.height || frame.width > 4096 || frame.height > 4096 ||
        xorBytes > SIZE_MAX || andBytes > SIZE_MAX || total > limits.maxEncodedBytes ||
        total > UINT32_MAX || frame.stride < static_cast<uint64_t>(frame.width) * 4 ||
        frame.pixels.size() < static_cast<uint64_t>(frame.stride) * frame.height)
        return nullptr;

    ImageBuffer dib{};
    if (!dib.allocate(static_cast<size_t>(total), allocator)) return nullptr;
    auto* bytes = dib.data();
    write32(bytes, 40);
    write32(bytes + 4, frame.width);
    write32(bytes + 8, frame.height * 2);
    write16(bytes + 12, 1);
    write16(bytes + 14, 32);
    write32(bytes + 16, 0);
    write32(bytes + 20, static_cast<uint32_t>(xorBytes));
    // The decoder consumes bottom-up XOR rows followed by a bottom-up AND mask.
    uint8_t* xorPixels = bytes + 40;
    for (uint32_t y = 0; y < frame.height; ++y) {
        const uint8_t* src = frame.pixels.data() + static_cast<size_t>(y) * frame.stride;
        uint8_t* dst = xorPixels + static_cast<size_t>(frame.height - 1 - y) * frame.width * 4;
        for (uint32_t x = 0; x < frame.width; ++x) {
            const uint8_t alpha = src[x * 4 + 3];
            for (unsigned channel = 0; channel < 3; ++channel) {
                const uint32_t premultiplied = src[x * 4 + channel];
                dst[x * 4 + channel] = alpha
                    ? static_cast<uint8_t>((premultiplied * 255u + alpha / 2u) / alpha)
                    : 0;
            }
            dst[x * 4 + 3] = alpha;
        }
    }
    // Zero AND bits preserve the decoded alpha channel.
    for (uint64_t i = 0; i < andBytes; ++i) bytes[40 + xorBytes + i] = 0;
    return CreateIconFromResourceEx(bytes, static_cast<uint32_t>(dib.size()), 1,
                                    0x00030000u, static_cast<int32_t>(width),
                                    static_cast<int32_t>(height), 0);
}

void* loadIconPath(const wchar_t* path, int32_t index,
                   uint32_t width, uint32_t height) noexcept {
    if (!path || !*path) return nullptr;
    if (isIcoPath(path)) {
        // A standalone ICO is one image; the only supported index is zero.
        if (index != 0) return nullptr;
        return LoadImageW(nullptr, path, kImageIcon, static_cast<int32_t>(width),
                          static_cast<int32_t>(height), kLoadFromFile);
    }
    return makeIconFromPe(path, index, width, height);
}

struct ShellFileInfoW {
    void* icon;
    int32_t iconIndex;
    uint32_t attributes;
    wchar_t displayName[260];
    wchar_t typeName[80];
};

} // namespace

extern "C" {

bool DllMain(void*, unsigned long, void*) noexcept { return true; }

__declspec(dllexport) void* ExtractIconW(void*, const wchar_t* path,
                                         uint32_t iconIndex) noexcept {
    const int32_t signedIndex = static_cast<int32_t>(iconIndex);
    if (signedIndex == -1) {
        if (!path || !*path) return nullptr;
        if (isIcoPath(path)) return reinterpret_cast<void*>(uintptr_t{1});
        ImageBuffer file{};
        if (ReadIconSourceFile(path, file, NativeImageAllocator(), shellIconLimits()) !=
            ImageStatus::Success) return nullptr;
        std::array<uint16_t, kMaxGroups> ids{};
        size_t count{};
        if (!getGroupIds(file.span(), ids, count)) return nullptr;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(count));
    }
    return loadIconPath(path, signedIndex, 32, 32);
}

__declspec(dllexport) uint32_t ExtractIconExW(const wchar_t* path, int32_t iconIndex,
    void** largeIcons, void** smallIcons, uint32_t iconCount) noexcept {
    if (!path || !*path || iconCount > 64 ||
        (iconCount && !largeIcons && !smallIcons)) return 0;
    if (iconCount == 0) {
        if (isIcoPath(path)) return 1;
        ImageBuffer file{};
        if (ReadIconSourceFile(path, file, NativeImageAllocator(), shellIconLimits()) !=
            ImageStatus::Success) return 0;
        std::array<uint16_t, kMaxGroups> ids{};
        size_t count{};
        return getGroupIds(file.span(), ids, count) ? static_cast<uint32_t>(count) : 0;
    }
    const uint32_t requested = iconIndex < 0 && iconCount > 1 ? 1 : iconCount;
    uint32_t extracted = 0;
    for (; extracted < requested; ++extracted) {
        if (largeIcons) largeIcons[extracted] = nullptr;
        if (smallIcons) smallIcons[extracted] = nullptr;
        const int64_t current = static_cast<int64_t>(iconIndex) + extracted;
        if (current < INT32_MIN || current > INT32_MAX) break;
        void* large = largeIcons ? loadIconPath(path, static_cast<int32_t>(current), 32, 32) : nullptr;
        void* small = smallIcons ? loadIconPath(path, static_cast<int32_t>(current), 16, 16) : nullptr;
        if ((largeIcons && !large) || (smallIcons && !small)) {
            if (large) (void)DestroyIcon(large);
            if (small) (void)DestroyIcon(small);
            break;
        }
        if (largeIcons) largeIcons[extracted] = large;
        if (smallIcons) smallIcons[extracted] = small;
    }
    return extracted;
}

__declspec(dllexport) uintptr_t SHGetFileInfoW(const wchar_t* path,
    uint32_t, void* output, uint32_t outputBytes, uint32_t flags) noexcept {
    constexpr uint32_t kShgfiIcon = 0x00000100u;
    constexpr uint32_t kShgfiSmallIcon = 0x00000001u;
    constexpr uint32_t kShgfiDisplayName = 0x00000200u;
    if (!path || !*path || !output || outputBytes < sizeof(ShellFileInfoW) ||
        !(flags & kShgfiIcon) || (flags & ~(kShgfiIcon | kShgfiSmallIcon | kShgfiDisplayName)))
        return 0;
    auto* info = static_cast<ShellFileInfoW*>(output);
    *info = {};
    info->icon = loadIconPath(path, 0, (flags & kShgfiSmallIcon) ? 16u : 32u,
                              (flags & kShgfiSmallIcon) ? 16u : 32u);
    if (!info->icon) return 0;
    if (flags & kShgfiDisplayName) {
        const wchar_t* leaf = path;
        for (const wchar_t* p = path; *p; ++p)
            if (*p == L'\\' || *p == L'/' || *p == L':') leaf = p + 1;
        size_t index = 0;
        while (leaf[index] && index + 1 < 260) {
            info->displayName[index] = leaf[index]; ++index;
        }
        info->displayName[index] = 0;
    }
    return reinterpret_cast<uintptr_t>(info->icon);
}

} // extern "C"
