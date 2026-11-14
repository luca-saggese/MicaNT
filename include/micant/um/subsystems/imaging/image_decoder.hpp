#pragma once

// Private Ring-3 imaging contract. Codec implementations accept an immutable,
// length-delimited byte span and publish pixels only after a complete decode.
#include <cstdint>
#include <span>
#include <cstddef>
#include <utility>

namespace micant::um::imaging {

enum class ImageStatus : uint32_t {
    Success,
    UnknownFormat,
    UnsupportedFeature,
    MalformedData,
    TruncatedInput,
    ImageTooLarge,
    OutOfMemory,
    AccessDenied,
    IoFailure,
    Cancelled,
};

enum class ImageFormat : uint32_t { Unknown, Bmp, Jpeg, Png, Tiff };
enum class PixelFormat : uint32_t { Bgra8Premultiplied };
enum class ImageColorModel : uint32_t { Unknown, Gray, Rgb, YCbCr };

using ImageAllocateRoutine = void* (*)(void* context, size_t bytes) noexcept;
using ImageDeallocateRoutine = void (*)(void* context, void* address, size_t bytes) noexcept;

struct ImageAllocator {
    void* context{};
    ImageAllocateRoutine allocate{};
    ImageDeallocateRoutine deallocate{};
};

class ImageBuffer {
public:
    ImageBuffer() noexcept = default;
    ~ImageBuffer() { reset(); }
    ImageBuffer(const ImageBuffer&) = delete;
    ImageBuffer& operator=(const ImageBuffer&) = delete;
    ImageBuffer(ImageBuffer&& other) noexcept { take(other); }
    ImageBuffer& operator=(ImageBuffer&& other) noexcept {
        if (this != &other) { reset(); take(other); }
        return *this;
    }

    [[nodiscard]] bool allocate(size_t bytes, const ImageAllocator& allocator) noexcept {
        if (bytes == 0) { reset(); return true; }
        if (!allocator.allocate || !allocator.deallocate) return false;
        void* memory = allocator.allocate(allocator.context, bytes);
        if (!memory) return false;
        reset();
        data_ = static_cast<uint8_t*>(memory);
        size_ = bytes;
        allocator_ = allocator;
        return true;
    }
    void reset() noexcept {
        if (data_ && allocator_.deallocate)
            allocator_.deallocate(allocator_.context, data_, size_);
        data_ = nullptr; size_ = 0; allocator_ = {};
    }
    [[nodiscard]] uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] uint8_t& operator[](size_t index) noexcept { return data_[index]; }
    [[nodiscard]] const uint8_t& operator[](size_t index) const noexcept { return data_[index]; }
    [[nodiscard]] std::span<uint8_t> span() noexcept { return {data_, size_}; }
    [[nodiscard]] std::span<const uint8_t> span() const noexcept { return {data_, size_}; }

    // Transfer the page range to a longer-lived owner such as a process-owned
    // desktop cache. The caller must release it through the owning allocator
    // or let the process address-space teardown reclaim it.
    [[nodiscard]] uint8_t* releaseOwnership(size_t& size,
                                           ImageAllocator& allocator) noexcept {
        size = size_;
        allocator = allocator_;
        uint8_t* result = data_;
        data_ = nullptr;
        size_ = 0;
        allocator_ = {};
        return result;
    }

private:
    void take(ImageBuffer& other) noexcept {
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        allocator_ = std::exchange(other.allocator_, {});
    }
    uint8_t* data_{};
    size_t size_{};
    ImageAllocator allocator_{};
};

struct ImageLimits {
    uint64_t maxEncodedBytes{64ull * 1024 * 1024};
    uint64_t maxPixels{64ull * 1024 * 1024};
    uint64_t maxDecodedBytes{256ull * 1024 * 1024};
    uint64_t maxIntermediateBytes{320ull * 1024 * 1024};
};

struct ImageFrame {
    uint32_t width{};
    uint32_t height{};
    uint32_t stride{};
    ImageFormat sourceFormat{ImageFormat::Unknown};
    PixelFormat format{PixelFormat::Bgra8Premultiplied};
    ImageColorModel sourceColor{ImageColorModel::Unknown};
    uint8_t bitsPerChannel{};
    uint16_t orientation{1};
    uint32_t frameCount{1};
    bool sourceHadAlpha{};
    ImageBuffer pixels;
};

ImageStatus ProbeImage(std::span<const uint8_t> input, ImageFormat& format) noexcept;
ImageStatus DecodeImage(std::span<const uint8_t> input, ImageFrame& output,
                        const ImageAllocator& allocator,
                        ImageLimits limits = {}) noexcept;

} // namespace micant::um::imaging
