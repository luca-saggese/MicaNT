#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::um {

enum class DisplayTransportBackend : uint32_t {
    Unknown = 0,
    Virtio2D = 1,
    Virgl = 2,
    Gop = 3,
};

enum class DisplayCompositionBackend : uint32_t {
    Software = 1,
    VirglAvailable = 2,
    VirglAccelerated = 3,
};

#pragma pack(push, 1)
struct UserVideoInfo64 {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch{0}; // Scanout pixels per row; the corresponding byte pitch is pitch * 4.
    uint32_t bpp{32};
    uint32_t pixelFormat{1};
    uint32_t transportBackend{static_cast<uint32_t>(DisplayTransportBackend::Unknown)};
    uint32_t compositionBackend{static_cast<uint32_t>(DisplayCompositionBackend::Software)};
    uint64_t physicalBase{0};
};

struct UserCompositorViews64 {
    uint32_t magic{0x56434D4Du}; /* MMCV */
    uint16_t version{1};
    uint16_t size{16};
    uint64_t metadataAddress{};
};

struct UserPresentSurfaceParams64 {
    uint64_t surfaceBufferVa{0};
    uint32_t dstX{0};
    uint32_t dstY{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch{0};
    uint32_t reserved{0};
};

struct UserPresentCompositionParams64 {
    uint64_t baseSurfaceBufferVa{0};
    uint64_t layersVa{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch{0};
    uint32_t layerCount{0};
};
#pragma pack(pop)

static_assert(sizeof(UserVideoInfo64) == 36);
static_assert(sizeof(UserCompositorViews64) == 16);
static_assert(offsetof(UserVideoInfo64, physicalBase) == 28);
static_assert(sizeof(UserPresentSurfaceParams64) == 32);
static_assert(sizeof(UserPresentCompositionParams64) == 32);

} // namespace micant::um
