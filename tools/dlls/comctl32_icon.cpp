// Ring 3 Comctl32 image-list core. Handles and pixel storage are private to
// this process. Pixel acquisition and presentation go through GDI/User32 DLLs.
#include "micant/shared/native_nt_api.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <atomic>


namespace {
using micant::Handle;
using micant::NtStatus;
constexpr uint32_t MaxLists = 32;
constexpr uint32_t MaxImagesPerList = 64;
constexpr uint32_t MaxImagesTotal = 128;
constexpr uint32_t MaxDimension = 256;
constexpr size_t MaxBytesTotal = 16u * 1024u * 1024u;
constexpr uint64_t HandleTag = 0x4d49000000000000ull;
constexpr uint32_t RopSrcCopy = 0x00cc0020u;
constexpr uint32_t DiNormal = 0x0003u;

struct BitmapInfoHeader {
    uint32_t size{40};
    int32_t width{};
    int32_t height{};
    uint16_t planes{1};
    uint16_t bitCount{32};
    uint32_t compression{};
    uint32_t imageSize{};
    int32_t xPixelsPerMeter{}, yPixelsPerMeter{};
    uint32_t colorsUsed{}, colorsImportant{};
};
struct BitmapInfo { BitmapInfoHeader header{}; uint32_t color{}; };

extern "C" __declspec(dllimport) void* CreateCompatibleDC(void*) noexcept;
extern "C" __declspec(dllimport) int32_t DeleteDC(void*) noexcept;
extern "C" __declspec(dllimport) void* CreateDIBSection(void*, const void*, uint32_t,
                                                          void**, void*, uint32_t) noexcept;
extern "C" __declspec(dllimport) void* SelectObject(void*, void*) noexcept;
extern "C" __declspec(dllimport) int32_t DeleteObject(void*) noexcept;
extern "C" __declspec(dllimport) int32_t BitBlt(void*, int32_t, int32_t, int32_t, int32_t,
                                                  void*, int32_t, int32_t, uint32_t) noexcept;
extern "C" __declspec(dllimport) int32_t DrawIconEx(void*, int32_t, int32_t, void*, int32_t,
                                                     int32_t, uint32_t, void*, uint32_t) noexcept;

struct Image {
    void* pixels{};
    size_t bytes{};
    uint32_t references{};
    bool active{};
    bool retired{};
};
struct ImageList {
    uint32_t generation{};
    uint32_t width{}, height{}, flags{}, count{}, operations{};
    uint8_t order[MaxImagesPerList]{};
    bool active{}, retiring{}, busy{};
};
ImageList g_lists[MaxLists]{};
Image g_images[MaxImagesTotal]{};
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
size_t g_bytes{};
size_t g_pendingBytes{};

struct Guard {
    Guard() noexcept { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Guard() { g_lock.clear(std::memory_order_release); }
};

uint64_t makeHandle(uint32_t index, uint32_t generation) noexcept {
    return HandleTag | (static_cast<uint64_t>(generation) << 8) | (index + 1u);
}
bool decodeHandle(void* handle, uint32_t& index, uint32_t& generation) noexcept {
    const uint64_t value = reinterpret_cast<uintptr_t>(handle);
    if ((value & 0xffff000000000000ull) != HandleTag || !(value & 0xffu)) return false;
    index = static_cast<uint32_t>((value & 0xffu) - 1u);
    generation = static_cast<uint32_t>((value >> 8) & 0xffffffffu);
    return index < MaxLists && generation != 0;
}
ImageList* lookup(void* handle, uint32_t& index) noexcept {
    uint32_t generation{};
    if (!decodeHandle(handle, index, generation)) return nullptr;
    ImageList& list = g_lists[index];
    return list.active && list.generation == generation ? &list : nullptr;
}

bool allocate(size_t bytes, void*& out) noexcept {
    out = nullptr;
    if (!bytes || bytes > MaxBytesTotal) return false;
    size_t size = bytes;
    NtStatus status = micant::native_nt::NtAllocateVirtualMemory(
        static_cast<Handle>(-1), &out, 0, &size,
        0x3000u, 0x04u);
    return status == NtStatus::Success && out;
}
void release(void* address, size_t bytes) noexcept {
    if (!address) return;
    size_t size = bytes;
    (void)micant::native_nt::NtFreeVirtualMemory(
        static_cast<Handle>(-1), &address, &size, 0x8000u);
}
bool reserveBytes(size_t bytes) noexcept {
    Guard guard;
    if (!bytes || bytes > MaxBytesTotal || g_bytes > MaxBytesTotal - bytes ||
        g_pendingBytes > MaxBytesTotal - g_bytes - bytes) return false;
    g_pendingBytes += bytes;
    return true;
}
void cancelReservation(size_t bytes) noexcept {
    Guard guard;
    if (bytes <= g_pendingBytes) g_pendingBytes -= bytes;
}

bool makeSurface(uint32_t width, uint32_t height, void*& bitmap, void*& dc,
                 uint32_t*& pixels) noexcept {
    bitmap = dc = nullptr; pixels = nullptr;
    if (!width || !height || width > MaxDimension || height > MaxDimension) return false;
    BitmapInfo info{};
    info.header.width = static_cast<int32_t>(width);
    info.header.height = -static_cast<int32_t>(height);
    info.header.imageSize = width * height * 4u;
    dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    bitmap = CreateDIBSection(dc, &info, 0, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!bitmap || !pixels) {
        if (bitmap) (void)DeleteObject(bitmap);
        (void)DeleteDC(dc); bitmap = dc = nullptr; pixels = nullptr; return false;
    }
    return true;
}
void destroySurface(void* bitmap, void* dc, void* previous) noexcept {
    if (dc && previous) (void)SelectObject(dc, previous);
    if (bitmap) (void)DeleteObject(bitmap);
    if (dc) (void)DeleteDC(dc);
}

int32_t publish(void* handle, uint32_t width, uint32_t height,
                const uint32_t* sourcePixels) noexcept {
    if (!sourcePixels || !width || !height || width > MaxDimension || height > MaxDimension)
        return -1;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    if (!reserveBytes(bytes)) return -1;
    void* copy{};
    if (!allocate(bytes, copy)) { cancelReservation(bytes); return -1; }
    std::memcpy(copy, sourcePixels, bytes);

    int32_t result = -1;
    {
        Guard guard;
        uint32_t listIndex{};
        ImageList* list = lookup(handle, listIndex);
        if (list && !list->busy && list->width == width && list->height == height &&
            list->count < MaxImagesPerList) {
            uint32_t imageIndex = MaxImagesTotal;
            for (uint32_t i = 0; i < MaxImagesTotal; ++i)
                if (!g_images[i].active && !g_images[i].retired && !g_images[i].pixels) {
                    imageIndex = i; break;
                }
            if (imageIndex != MaxImagesTotal) {
                Image& image = g_images[imageIndex];
                image.pixels = copy; image.bytes = bytes; image.active = true;
                list->order[list->count] = static_cast<uint8_t>(imageIndex);
                result = static_cast<int32_t>(list->count++);
                g_pendingBytes -= bytes; g_bytes += bytes; copy = nullptr;
            }
        }
    }
    if (copy) { cancelReservation(bytes); release(copy, bytes); }
    return result;
}

int32_t captureBitmap(void* himl, void* sourceBitmap, void* sourceMask) noexcept {
    if (!sourceBitmap || sourceMask) return -1;
    uint32_t width{}, height{};
    {
        Guard guard;
        uint32_t index{}; ImageList* list = lookup(himl, index);
        if (!list || list->busy) return -1;
        width = list->width; height = list->height;
    }
    void* dib{}; void* destDc{}; uint32_t* pixels{};
    if (!makeSurface(width, height, dib, destDc, pixels)) return -1;
    void* oldDest = SelectObject(destDc, dib);
    void* sourceDc = CreateCompatibleDC(nullptr);
    if (!oldDest || !sourceDc) {
        if (sourceDc) (void)DeleteDC(sourceDc);
        destroySurface(dib, destDc, oldDest); return -1;
    }
    void* oldSource = SelectObject(sourceDc, sourceBitmap);
    const bool copied = oldSource && BitBlt(destDc, 0, 0, static_cast<int32_t>(width),
        static_cast<int32_t>(height), sourceDc, 0, 0, RopSrcCopy) != 0;
    if (oldSource) (void)SelectObject(sourceDc, oldSource);
    (void)DeleteDC(sourceDc);
    const int32_t index = copied ? publish(himl, width, height, pixels) : -1;
    destroySurface(dib, destDc, oldDest);
    return index;
}

int32_t captureIcon(void* himl, void* icon, int32_t replaceIndex) noexcept {
    if (!icon) return -1;
    uint32_t width{}, height{};
    {
        Guard guard;
        uint32_t index{}; ImageList* list = lookup(himl, index);
        if (!list || list->busy || (replaceIndex >= 0 &&
            static_cast<uint32_t>(replaceIndex) >= list->count)) return -1;
        width = list->width; height = list->height;
    }
    void* dib{}; void* dc{}; uint32_t* pixels{};
    if (!makeSurface(width, height, dib, dc, pixels)) return -1;
    void* previous = SelectObject(dc, dib);
    const bool drawn = previous && DrawIconEx(dc, 0, 0, icon, static_cast<int32_t>(width),
        static_cast<int32_t>(height), 0, nullptr, DiNormal) != 0;
    int32_t result = -1;
    if (drawn && replaceIndex < 0) result = publish(himl, width, height, pixels);
    if (drawn && replaceIndex >= 0) {
        const size_t bytes = static_cast<size_t>(width) * height * 4;
        void* copy{};
        const bool reserved = reserveBytes(bytes);
        if (reserved && allocate(bytes, copy)) {
            std::memcpy(copy, pixels, bytes);
            void* reclaim{}; size_t reclaimBytes{};
            {
                Guard guard; uint32_t listIndex{}; ImageList* list = lookup(himl, listIndex);
                uint32_t imageIndex = MaxImagesTotal;
                if (list && !list->busy && static_cast<uint32_t>(replaceIndex) < list->count)
                    for (uint32_t i = 0; i < MaxImagesTotal; ++i)
                        if (!g_images[i].active && !g_images[i].retired && !g_images[i].pixels) {
                            imageIndex = i; break;
                        }
                if (imageIndex < MaxImagesTotal) {
                    Image& replacement = g_images[imageIndex];
                    replacement.pixels = copy; replacement.bytes = bytes; replacement.active = true;
                    const uint32_t oldIndex = list->order[replaceIndex];
                    list->order[replaceIndex] = static_cast<uint8_t>(imageIndex);
                    Image& old = g_images[oldIndex]; old.active = false; old.retired = true;
                    if (!old.references && old.pixels) {
                        reclaim = old.pixels; reclaimBytes = old.bytes;
                        g_bytes -= old.bytes; old = {};
                    }
                    g_pendingBytes -= bytes; g_bytes += bytes; copy = nullptr; result = replaceIndex;
                }
            }
            if (reclaim) release(reclaim, reclaimBytes);
            if (copy) { cancelReservation(bytes); release(copy, bytes); }
        } else if (reserved) cancelReservation(bytes);
    }
    destroySurface(dib, dc, previous);
    return result;
}
} // namespace

extern "C" {
__declspec(dllexport) void* ImageList_Create(int32_t cx, int32_t cy, uint32_t flags,
                                              int32_t initial, int32_t grow) noexcept {
    constexpr uint32_t Supported = 0x0000003cu; // ILC_COLOR*; mask plane unsupported.
    if (cx <= 0 || cy <= 0 || cx > static_cast<int32_t>(MaxDimension) ||
        cy > static_cast<int32_t>(MaxDimension) || initial < 0 || initial > 64 ||
        grow < 0 || grow > 64 || (flags & ~Supported)) return nullptr;
    const uint32_t color = flags & 0x3cu;
    if (color != 0 && color != 0x04 && color != 0x08 && color != 0x10 &&
        color != 0x18 && color != 0x20) return nullptr;
    Guard guard;
    for (uint32_t i = 0; i < MaxLists; ++i) {
        ImageList& list = g_lists[i];
        if (list.active || list.retiring) continue;
        uint32_t generation = list.generation + 1;
        if (!generation) generation = 1;
        list = {};
        list.generation = generation; list.width = static_cast<uint32_t>(cx);
        list.height = static_cast<uint32_t>(cy); list.flags = flags; list.active = true;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(makeHandle(i, generation)));
    }
    return nullptr;
}

__declspec(dllexport) int32_t ImageList_Destroy(void* himl) noexcept {
    void* freeImages[MaxImagesPerList]{}; size_t freeBytes[MaxImagesPerList]{};
    uint32_t freeCount{};
    {
        Guard guard;
        uint32_t index{}; ImageList* list = lookup(himl, index);
        if (!list || list->operations || list->busy) return 0;
        list->active = false; list->retiring = true;
        for (uint32_t n = 0; n < list->count; ++n) {
            Image& image = g_images[list->order[n]];
            image.active = false; image.retired = true;
            if (!image.references && image.pixels) {
                freeImages[freeCount] = image.pixels; freeBytes[freeCount++] = image.bytes;
                g_bytes -= image.bytes; image = {};
            }
        }
        list->count = 0;
    }
    for (uint32_t i = 0; i < freeCount; ++i) release(freeImages[i], freeBytes[i]);
    {
        Guard guard;
        uint32_t index{}; uint32_t generation{};
        if (decodeHandle(himl, index, generation) && g_lists[index].generation == generation)
            g_lists[index].retiring = false;
    }
    return 1;
}

__declspec(dllexport) int32_t ImageList_GetImageCount(void* himl) noexcept {
    Guard guard; uint32_t index{}; ImageList* list = lookup(himl, index);
    return list && !list->busy ? static_cast<int32_t>(list->count) : 0;
}
__declspec(dllexport) int32_t ImageList_Add(void* himl, void* bitmap, void* mask) noexcept {
    return captureBitmap(himl, bitmap, mask);
}
__declspec(dllexport) int32_t ImageList_AddIcon(void* himl, void* icon) noexcept {
    return captureIcon(himl, icon, -1);
}
__declspec(dllexport) int32_t ImageList_ReplaceIcon(void* himl, int32_t index,
                                                     void* icon) noexcept {
    return captureIcon(himl, icon, index);
}
__declspec(dllexport) int32_t ImageList_GetIconSize(void* himl, int32_t* cx,
                                                     int32_t* cy) noexcept {
    if (!cx || !cy) return 0;
    Guard guard; uint32_t index{}; ImageList* list = lookup(himl, index);
    if (!list || list->busy) return 0;
    *cx = static_cast<int32_t>(list->width); *cy = static_cast<int32_t>(list->height);
    return 1;
}
__declspec(dllexport) int32_t ImageList_SetIconSize(void* himl, int32_t cx,
                                                    int32_t cy) noexcept {
    if (cx <= 0 || cy <= 0 || cx > static_cast<int32_t>(MaxDimension) ||
        cy > static_cast<int32_t>(MaxDimension)) return 0;
    Guard guard; uint32_t index{}; ImageList* list = lookup(himl, index);
    if (!list || list->count || list->operations || list->busy) return 0;
    list->width = static_cast<uint32_t>(cx); list->height = static_cast<uint32_t>(cy);
    return 1;
}
__declspec(dllexport) int32_t ImageList_Remove(void* himl, int32_t index) noexcept {
    void* releasePixels[MaxImagesPerList]{};
    size_t releaseBytes[MaxImagesPerList]{};
    uint32_t releaseCount{};
    {
        Guard guard; uint32_t listIndex{}; ImageList* list = lookup(himl, listIndex);
        if (!list || list->busy || index < -1 || (index >= 0 &&
            static_cast<uint32_t>(index) >= list->count)) return 0;
        if (index == -1) {
            while (list->count) {
                const uint32_t imageIndex = list->order[--list->count];
                Image& image = g_images[imageIndex]; image.active = false; image.retired = true;
                if (!image.references && image.pixels) {
                    releasePixels[releaseCount] = image.pixels;
                    releaseBytes[releaseCount++] = image.bytes;
                    g_bytes -= image.bytes; image = {};
                }
            }
        } else {
            const uint32_t imageIndex = list->order[index];
            for (uint32_t i = static_cast<uint32_t>(index) + 1; i < list->count; ++i)
                list->order[i - 1] = list->order[i];
            --list->count;
            Image& image = g_images[imageIndex]; image.active = false; image.retired = true;
            if (!image.references && image.pixels) {
                releasePixels[releaseCount] = image.pixels;
                releaseBytes[releaseCount++] = image.bytes;
                g_bytes -= image.bytes; image = {};
            }
        }
    }
    for (uint32_t i = 0; i < releaseCount; ++i) release(releasePixels[i], releaseBytes[i]);
    return 1;
}
__declspec(dllexport) int32_t ImageList_Draw(void* himl, int32_t index, void* destination,
                                              int32_t x, int32_t y, uint32_t style) noexcept {
    if (!destination || (style != 0 && style != 1)) return 0;
    uint32_t imageIndex{}, width{}, height{}; const uint32_t* pixels{};
    {
        Guard guard; uint32_t listIndex{}; ImageList* list = lookup(himl, listIndex);
        if (!list || list->busy || index < 0 || static_cast<uint32_t>(index) >= list->count)
            return 0;
        imageIndex = list->order[index]; Image& image = g_images[imageIndex];
        if (!image.active || !image.pixels) return 0;
        ++image.references; ++list->operations;
        pixels = static_cast<const uint32_t*>(image.pixels);
        width = list->width; height = list->height;
    }
    void* dib{}; void* dc{}; uint32_t* dibPixels{}; bool copied{};
    if (makeSurface(width, height, dib, dc, dibPixels)) {
        std::memcpy(dibPixels, pixels, static_cast<size_t>(width) * height * 4);
        void* previous = SelectObject(dc, dib);
        copied = previous && BitBlt(destination, x, y, static_cast<int32_t>(width),
            static_cast<int32_t>(height), dc, 0, 0, RopSrcCopy) != 0;
        destroySurface(dib, dc, previous);
    }
    void* reclaim{}; size_t reclaimBytes{};
    {
        Guard guard;
        Image& image = g_images[imageIndex];
        ImageList* list = nullptr;
        uint32_t listIndex{};
        if (lookup(himl, listIndex)) list = &g_lists[listIndex];
        if (image.references) --image.references;
        if (list && list->operations) --list->operations;
        if (image.retired && !image.references && image.pixels) {
            reclaim = image.pixels; reclaimBytes = image.bytes;
            g_bytes -= image.bytes; image = {};
        }
    }
    if (reclaim) release(reclaim, reclaimBytes);
    return copied ? 1 : 0;
}
} // extern "C"
