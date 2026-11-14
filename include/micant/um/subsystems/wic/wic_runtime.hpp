#pragma once

// Isolated Ring-3 WIC compatibility adapter. This header deliberately depends
// only on the shared imaging service and mode-neutral NT ABI. The implemented
// WIC slice decodes BMP/JPEG/PNG files and encodes BMP/PNG/JPEG to caller-owned
// COM IStream instances; metadata, arbitrary streams as decoder inputs, and
// unsupported codec variants return explicit HRESULTs.
#if !defined(MICANT_NATIVE_USERMODE)
#define MICANT_WIC_DEFINED_NATIVE_USERMODE
#define MICANT_NATIVE_USERMODE 1
#endif
#include "micant/um/subsystems/imaging/codec_registry.hpp"
#include "micant/um/subsystems/imaging/image_encoder.hpp"
#include "micant/um/subsystems/imaging/jpeg_encoder.hpp"
#include "micant/um/subsystems/imaging/native_image_allocator.hpp"
#include "micant/um/subsystems/imaging/native_image_file.hpp"
#include "micant/um/subsystems/imaging/raster_encoder.hpp"
#include "micant/shared/ntdef.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#if defined(MICANT_WIC_DEFINED_NATIVE_USERMODE)
#undef MICANT_NATIVE_USERMODE
#undef MICANT_WIC_DEFINED_NATIVE_USERMODE
#endif

namespace micant::um::wic {

using HRESULT = int32_t;
using ULONG = uint32_t;

inline constexpr HRESULT S_OK = 0;
inline constexpr HRESULT S_FALSE = 1;
inline constexpr HRESULT E_NOTIMPL = static_cast<HRESULT>(0x80004001u);
inline constexpr HRESULT E_NOINTERFACE = static_cast<HRESULT>(0x80004002u);
inline constexpr HRESULT E_POINTER = static_cast<HRESULT>(0x80004003u);
inline constexpr HRESULT E_ABORT = static_cast<HRESULT>(0x80004004u);
inline constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
inline constexpr HRESULT E_OUTOFMEMORY = static_cast<HRESULT>(0x8007000Eu);
inline constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057u);
inline constexpr HRESULT E_ACCESSDENIED = static_cast<HRESULT>(0x80070005u);
inline constexpr HRESULT WINCODEC_ERR_UNKNOWNIMAGEFORMAT = static_cast<HRESULT>(0x88982F07u);
inline constexpr HRESULT WINCODEC_ERR_VALUEOVERFLOW = static_cast<HRESULT>(0x88982F05u);
inline constexpr HRESULT WINCODEC_ERR_BADIMAGE = static_cast<HRESULT>(0x88982F60u);
inline constexpr HRESULT WINCODEC_ERR_STREAMREAD = static_cast<HRESULT>(0x88982F72u);
inline constexpr HRESULT WINCODEC_ERR_UNSUPPORTEDOPERATION = static_cast<HRESULT>(0x88982F81u);
inline constexpr HRESULT WINCODEC_ERR_COMPONENTNOTFOUND = static_cast<HRESULT>(0x88982F50u);
inline constexpr HRESULT WINCODEC_ERR_WRONGSTATE = static_cast<HRESULT>(0x88982F04u);

inline constexpr GUID IID_IUnknown{0x00000000, 0x0000, 0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
inline constexpr GUID CLSID_WICImagingFactory{0x317D06E8, 0x5F24, 0x433D,
    {0xBD, 0xF7, 0x79, 0xCE, 0x68, 0xD8, 0xAB, 0xC2}};
inline constexpr GUID IID_IWICImagingFactory{0xEC5EC888, 0xC19E, 0x4CF0,
    {0xB3, 0x90, 0xDA, 0x60, 0x27, 0x93, 0x60, 0xB3}};
inline constexpr GUID IID_IWICBitmapSource{0x00000120, 0xA8F2, 0x4877,
    {0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94}};
inline constexpr GUID IID_IWICBitmapDecoder{0x9EDDE9E7, 0x8DEE, 0x47EA,
    {0x99, 0xDF, 0xE6, 0xFA, 0xF2, 0xED, 0x44, 0xBF}};
inline constexpr GUID IID_IWICBitmapFrameDecode{0x3B16811B, 0x6A43, 0x4EC9,
    {0xA8, 0x13, 0x3D, 0x93, 0x0C, 0x13, 0xB9, 0x40}};
inline constexpr GUID IID_IWICFormatConverter{0x00000301, 0xA8F2, 0x4877,
    {0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94}};
inline constexpr GUID IID_IWICBitmapEncoder{0x00000103, 0xA8F2, 0x4877,
    {0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94}};
inline constexpr GUID IID_IWICBitmapFrameEncode{0x00000105, 0xA8F2, 0x4877,
    {0xBA, 0x0A, 0xFD, 0x2B, 0x66, 0x45, 0xFB, 0x94}};

inline constexpr GUID GUID_WICPixelFormat32bppPBGRA{0x6FDDC324, 0x4E03, 0x4BFE,
    {0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x10}};
inline constexpr GUID GUID_ContainerFormatBmp{0x0AF1D87E, 0xFCFE, 0x4188,
    {0xBD, 0xEB, 0xA7, 0x90, 0x64, 0x71, 0xCB, 0xE3}};
inline constexpr GUID GUID_ContainerFormatJpeg{0x19E4A5AA, 0x5662, 0x4FC5,
    {0xA0, 0xC0, 0x17, 0x58, 0x02, 0x8E, 0x10, 0x57}};
inline constexpr GUID GUID_ContainerFormatPng{0x1B7CFAF4, 0x713F, 0x473C,
    {0xBB, 0xCD, 0x61, 0x37, 0x42, 0x5F, 0xAE, 0xAF}};

struct WICRect { int32_t X{}, Y{}, Width{}, Height{}; };
struct IUnknown {
    virtual HRESULT __stdcall QueryInterface(const GUID&, void**) = 0;
    virtual ULONG __stdcall AddRef() = 0;
    virtual ULONG __stdcall Release() = 0;
};
struct IWICBitmapSource : IUnknown {
    virtual HRESULT __stdcall GetSize(uint32_t*, uint32_t*) = 0;
    virtual HRESULT __stdcall GetPixelFormat(GUID*) = 0;
    virtual HRESULT __stdcall GetResolution(double*, double*) = 0;
    virtual HRESULT __stdcall CopyPalette(void*) = 0;
    virtual HRESULT __stdcall CopyPixels(const WICRect*, uint32_t, uint32_t, uint8_t*) = 0;
};
struct IWICBitmapFrameDecode : IWICBitmapSource {
    virtual HRESULT __stdcall GetMetadataQueryReader(void**) = 0;
    virtual HRESULT __stdcall GetColorContexts(uint32_t, void**, uint32_t*) = 0;
    virtual HRESULT __stdcall GetThumbnail(IWICBitmapSource**) = 0;
};
struct IWICBitmapDecoder : IUnknown {
    virtual HRESULT __stdcall QueryCapability(void*, uint32_t*) = 0;
    virtual HRESULT __stdcall Initialize(void*, uint32_t) = 0;
    virtual HRESULT __stdcall GetContainerFormat(GUID*) = 0;
    virtual HRESULT __stdcall GetDecoderInfo(void**) = 0;
    virtual HRESULT __stdcall CopyPalette(void*) = 0;
    virtual HRESULT __stdcall GetMetadataQueryReader(void**) = 0;
    virtual HRESULT __stdcall GetPreview(IWICBitmapSource**) = 0;
    virtual HRESULT __stdcall GetColorContexts(uint32_t, void**, uint32_t*) = 0;
    virtual HRESULT __stdcall GetThumbnail(IWICBitmapSource**) = 0;
    virtual HRESULT __stdcall GetFrameCount(uint32_t*) = 0;
    virtual HRESULT __stdcall GetFrame(uint32_t, IWICBitmapFrameDecode**) = 0;
};
struct IWICFormatConverter : IWICBitmapSource {
    virtual HRESULT __stdcall Initialize(IWICBitmapSource*, const GUID&, int32_t, void*, double, int32_t) = 0;
    virtual HRESULT __stdcall CanConvert(const GUID&, const GUID&, int32_t*) = 0;
};
struct IWICBitmapEncoder : IUnknown {
    virtual HRESULT __stdcall Initialize(void*, uint32_t) = 0;
    virtual HRESULT __stdcall GetContainerFormat(GUID*) = 0;
    virtual HRESULT __stdcall GetEncoderInfo(void**) = 0;
    virtual HRESULT __stdcall SetColorContexts(uint32_t, void**) = 0;
    virtual HRESULT __stdcall SetPalette(void*) = 0;
    virtual HRESULT __stdcall SetThumbnail(IWICBitmapSource*) = 0;
    virtual HRESULT __stdcall SetPreview(IWICBitmapSource*) = 0;
    virtual HRESULT __stdcall CreateNewFrame(void**, void**) = 0;
    virtual HRESULT __stdcall Commit() = 0;
    virtual HRESULT __stdcall GetMetadataQueryWriter(void**) = 0;
};
struct IWICBitmapFrameEncode : IUnknown {
    virtual HRESULT __stdcall Initialize(void*) = 0;
    virtual HRESULT __stdcall SetSize(uint32_t, uint32_t) = 0;
    virtual HRESULT __stdcall SetResolution(double, double) = 0;
    virtual HRESULT __stdcall SetPixelFormat(GUID*) = 0;
    virtual HRESULT __stdcall SetColorContexts(uint32_t, void**) = 0;
    virtual HRESULT __stdcall SetPalette(void*) = 0;
    virtual HRESULT __stdcall SetThumbnail(IWICBitmapSource*) = 0;
    virtual HRESULT __stdcall WritePixels(uint32_t, uint32_t, uint32_t, uint8_t*) = 0;
    virtual HRESULT __stdcall WriteSource(IWICBitmapSource*, const WICRect*) = 0;
    virtual HRESULT __stdcall Commit() = 0;
    virtual HRESULT __stdcall GetMetadataQueryWriter(void**) = 0;
};
struct IStream : IUnknown {
    virtual HRESULT __stdcall Read(void*, uint32_t, uint32_t*) = 0;
    virtual HRESULT __stdcall Write(const void*, uint32_t, uint32_t*) = 0;
    virtual HRESULT __stdcall Seek(LargeInteger, uint32_t, LargeInteger*) = 0;
    virtual HRESULT __stdcall SetSize(LargeInteger) = 0;
    virtual HRESULT __stdcall CopyTo(IStream*, LargeInteger, LargeInteger*, LargeInteger*) = 0;
    virtual HRESULT __stdcall Commit(uint32_t) = 0;
    virtual HRESULT __stdcall Revert() = 0;
    virtual HRESULT __stdcall LockRegion(LargeInteger, LargeInteger, uint32_t) = 0;
    virtual HRESULT __stdcall UnlockRegion(LargeInteger, LargeInteger, uint32_t) = 0;
    virtual HRESULT __stdcall Stat(void*, uint32_t) = 0;
    virtual HRESULT __stdcall Clone(IStream**) = 0;
};
struct IWICImagingFactory : IUnknown {
    virtual HRESULT __stdcall CreateDecoderFromFilename(const wchar_t*, const GUID*, uint32_t, uint32_t, IWICBitmapDecoder**) = 0;
    virtual HRESULT __stdcall CreateDecoderFromStream(IStream*, const GUID*, uint32_t, IWICBitmapDecoder**) = 0;
    virtual HRESULT __stdcall CreateDecoderFromFileHandle(uintptr_t, const GUID*, uint32_t, IWICBitmapDecoder**) = 0;
    virtual HRESULT __stdcall CreateComponentInfo(const GUID&, void**) = 0;
    virtual HRESULT __stdcall CreateDecoder(const GUID&, const GUID*, IWICBitmapDecoder**) = 0;
    virtual HRESULT __stdcall CreateEncoder(const GUID&, const GUID*, IWICBitmapEncoder**) = 0;
    virtual HRESULT __stdcall CreatePalette(void**) = 0;
    virtual HRESULT __stdcall CreateFormatConverter(IWICFormatConverter**) = 0;
    virtual HRESULT __stdcall CreateBitmapScaler(void**) = 0;
    virtual HRESULT __stdcall CreateBitmapClipper(void**) = 0;
    virtual HRESULT __stdcall CreateBitmapFlipRotator(void**) = 0;
    virtual HRESULT __stdcall CreateStream(void**) = 0;
    virtual HRESULT __stdcall CreateColorContext(void**) = 0;
    virtual HRESULT __stdcall CreateColorTransformer(void**) = 0;
    virtual HRESULT __stdcall CreateBitmap(uint32_t, uint32_t, const GUID&, uint32_t, void**) = 0;
    virtual HRESULT __stdcall CreateBitmapFromSource(IWICBitmapSource*, uint32_t, void**) = 0;
    virtual HRESULT __stdcall CreateBitmapFromSourceRect(IWICBitmapSource*, uint32_t, uint32_t, uint32_t, uint32_t, void**) = 0;
    virtual HRESULT __stdcall CreateBitmapFromMemory(uint32_t, uint32_t, const GUID&, uint32_t, uint32_t, uint8_t*, void**) = 0;
    virtual HRESULT __stdcall CreateBitmapFromHBITMAP(void*, void*, int32_t, void**) = 0;
    virtual HRESULT __stdcall CreateBitmapFromHICON(void*, void**) = 0;
    virtual HRESULT __stdcall CreateComponentEnumerator(uint32_t, uint32_t, void**) = 0;
    virtual HRESULT __stdcall CreateFastMetadataEncoderFromDecoder(IWICBitmapDecoder*, void**) = 0;
    virtual HRESULT __stdcall CreateFastMetadataEncoderFromFrameDecode(IWICBitmapFrameDecode*, void**) = 0;
    virtual HRESULT __stdcall CreateQueryWriter(const GUID&, const GUID*, void**) = 0;
    virtual HRESULT __stdcall CreateQueryWriterFromReader(void*, const GUID*, void**) = 0;
};

namespace detail {
using namespace imaging;
inline std::atomic<uint32_t> g_liveObjects{0};

inline HRESULT statusToHr(ImageStatus status) noexcept {
    switch (status) {
        case ImageStatus::Success: return S_OK;
        case ImageStatus::UnknownFormat: return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
        case ImageStatus::UnsupportedFeature: return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        case ImageStatus::MalformedData:
        case ImageStatus::TruncatedInput: return WINCODEC_ERR_BADIMAGE;
        case ImageStatus::ImageTooLarge: return WINCODEC_ERR_VALUEOVERFLOW;
        case ImageStatus::OutOfMemory: return E_OUTOFMEMORY;
        case ImageStatus::AccessDenied: return E_ACCESSDENIED;
        case ImageStatus::IoFailure: return WINCODEC_ERR_STREAMREAD;
        case ImageStatus::Cancelled: return E_ABORT;
    }
    return E_FAIL;
}

inline const GUID* containerGuid(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::Bmp: return &GUID_ContainerFormatBmp;
        case ImageFormat::Jpeg: return &GUID_ContainerFormatJpeg;
        case ImageFormat::Png: return &GUID_ContainerFormatPng;
        default: return nullptr;
    }
}

template<class T, class... Args>
T* createObject(Args&&... args) noexcept {
    void* storage = AllocateImagePages(nullptr, sizeof(T));
    if (!storage) return nullptr;
    return ::new (storage) T(static_cast<Args&&>(args)...);
}

template<class T>
void destroyObject(T* object) noexcept {
    if (!object) return;
    object->~T();
    void* address = object;
    FreeImagePages(nullptr, address, sizeof(T));
}

inline bool sourceBounds(const ImageFrame& frame, const WICRect* rect,
                         uint32_t& x, uint32_t& y, uint32_t& width,
                         uint32_t& height) noexcept {
    if (!rect) { x = 0; y = 0; width = frame.width; height = frame.height; return true; }
    if (rect->X < 0 || rect->Y < 0 || rect->Width <= 0 || rect->Height <= 0) return false;
    x = static_cast<uint32_t>(rect->X); y = static_cast<uint32_t>(rect->Y);
    width = static_cast<uint32_t>(rect->Width); height = static_cast<uint32_t>(rect->Height);
    return x <= frame.width && width <= frame.width - x &&
           y <= frame.height && height <= frame.height - y;
}

inline bool sourceBounds(uint32_t sourceWidth, uint32_t sourceHeight, const WICRect* rect,
                         uint32_t& x, uint32_t& y, uint32_t& width,
                         uint32_t& height) noexcept {
    if (!rect) { x = 0; y = 0; width = sourceWidth; height = sourceHeight; return true; }
    if (rect->X < 0 || rect->Y < 0 || rect->Width <= 0 || rect->Height <= 0) return false;
    x = static_cast<uint32_t>(rect->X); y = static_cast<uint32_t>(rect->Y);
    width = static_cast<uint32_t>(rect->Width); height = static_cast<uint32_t>(rect->Height);
    return x <= sourceWidth && width <= sourceWidth - x &&
           y <= sourceHeight && height <= sourceHeight - y;
}

inline imaging::ImageStatus decodeStream(IStream* stream, imaging::ImageFrame& output,
                                          imaging::ImageFormat expected = imaging::ImageFormat::Unknown) noexcept {
    if (!stream) return imaging::ImageStatus::MalformedData;
    const auto allocator = imaging::NativeImageAllocator();
    imaging::ImageLimits limits{};
    if (!limits.maxEncodedBytes || limits.maxEncodedBytes > SIZE_MAX ||
        limits.maxEncodedBytes > limits.maxIntermediateBytes)
        return imaging::ImageStatus::ImageTooLarge;
    imaging::ImageBuffer encoded{};
    if (!encoded.allocate(static_cast<size_t>(limits.maxEncodedBytes), allocator))
        return imaging::ImageStatus::OutOfMemory;

    size_t length = 0;
    while (length < encoded.size()) {
        const uint32_t request = static_cast<uint32_t>(std::min<size_t>(
            encoded.size() - length, 64 * 1024));
        uint32_t read{};
        const HRESULT result = stream->Read(encoded.data() + length, request, &read);
        if (result < 0 || read > request) return imaging::ImageStatus::IoFailure;
        if (read == 0) break;
        length += read;
    }
    if (length == encoded.size()) {
        uint8_t extra{};
        uint32_t read{};
        const HRESULT result = stream->Read(&extra, 1, &read);
        if (result < 0 || read > 1) return imaging::ImageStatus::IoFailure;
        if (read) return imaging::ImageStatus::ImageTooLarge;
    }
    if (length == 0) return imaging::ImageStatus::TruncatedInput;
    if (length > limits.maxIntermediateBytes) return imaging::ImageStatus::ImageTooLarge;
    imaging::ImageFormat format{};
    const auto probe = imaging::ProbeImage({encoded.data(), length}, format);
    if (probe != imaging::ImageStatus::Success) return probe;
    if (expected != imaging::ImageFormat::Unknown && format != expected)
        return imaging::ImageStatus::UnknownFormat;
    limits.maxIntermediateBytes -= length;
    limits.maxDecodedBytes = std::min(limits.maxDecodedBytes, limits.maxIntermediateBytes);
    imaging::ImageFrame decoded{};
    const auto status = imaging::DecodeImage({encoded.data(), length}, decoded, allocator, limits);
    if (status == imaging::ImageStatus::Success) output = static_cast<imaging::ImageFrame&&>(decoded);
    return status;
}

inline imaging::ImageStatus decodeFileHandle(uintptr_t rawHandle, imaging::ImageFrame& output,
        imaging::ImageFormat expected = imaging::ImageFormat::Unknown) noexcept {
    if (!rawHandle || rawHandle == UINTPTR_MAX) return imaging::ImageStatus::MalformedData;
    const auto allocator = imaging::NativeImageAllocator();
    imaging::ImageLimits limits{};
    const auto handle = static_cast<Handle>(rawHandle);
    IoStatusBlock io{};
    FileStandardInformation standard{};
    NtStatus status = native_nt::NtQueryInformationFile(handle, &io, &standard,
        sizeof(standard), FileInformationClass::FileStandardInformation);
    if (status != NtStatus::Success) return imaging::ImageStatus::IoFailure;
    if (standard.directory || standard.endOfFile.quadPart <= 0)
        return imaging::ImageStatus::MalformedData;
    const uint64_t length = static_cast<uint64_t>(standard.endOfFile.quadPart);
    if (length > limits.maxEncodedBytes || length > limits.maxIntermediateBytes ||
        length > SIZE_MAX) return imaging::ImageStatus::ImageTooLarge;
    imaging::ImageBuffer encoded{};
    if (!encoded.allocate(static_cast<size_t>(length), allocator))
        return imaging::ImageStatus::OutOfMemory;
    size_t completed = 0;
    while (completed < encoded.size()) {
        const uint32_t request = static_cast<uint32_t>(std::min<size_t>(
            encoded.size() - completed, 64 * 1024));
        LargeInteger offset{};
        offset.quadPart = static_cast<int64_t>(completed);
        io = {};
        status = native_nt::NtReadFile(handle, 0, nullptr, nullptr, &io,
            encoded.data() + completed, request, &offset, nullptr);
        if (status != NtStatus::Success) return imaging::ImageStatus::IoFailure;
        if (io.information == 0) return imaging::ImageStatus::TruncatedInput;
        if (io.information > request) return imaging::ImageStatus::IoFailure;
        completed += static_cast<size_t>(io.information);
    }
    imaging::ImageFormat format{};
    const auto probe = imaging::ProbeImage(encoded.span(), format);
    if (probe != imaging::ImageStatus::Success) return probe;
    if (expected != imaging::ImageFormat::Unknown && format != expected)
        return imaging::ImageStatus::UnknownFormat;
    limits.maxIntermediateBytes -= length;
    limits.maxDecodedBytes = std::min(limits.maxDecodedBytes, limits.maxIntermediateBytes);
    imaging::ImageFrame decoded{};
    const auto decodeStatus = imaging::DecodeImage(encoded.span(), decoded, allocator, limits);
    if (decodeStatus == imaging::ImageStatus::Success)
        output = static_cast<imaging::ImageFrame&&>(decoded);
    return decodeStatus;
}
} // namespace detail

class BitmapFrame final : public IWICBitmapFrameDecode {
public:
    explicit BitmapFrame(imaging::ImageFrame&& image) noexcept : image_(static_cast<imaging::ImageFrame&&>(image)) {
        detail::g_liveObjects.fetch_add(1, std::memory_order_relaxed);
    }
    HRESULT __stdcall QueryInterface(const GUID& iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IWICBitmapSource || iid == IID_IWICBitmapFrameDecode)
            *out = static_cast<IWICBitmapFrameDecode*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG __stdcall AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG __stdcall Release() override {
        const ULONG count = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!count) { detail::g_liveObjects.fetch_sub(1, std::memory_order_relaxed); detail::destroyObject(this); }
        return count;
    }
    HRESULT __stdcall GetSize(uint32_t* width, uint32_t* height) override {
        if (!width || !height) return E_POINTER;
        *width = image_.width; *height = image_.height; return S_OK;
    }
    HRESULT __stdcall GetPixelFormat(GUID* format) override {
        if (!format) return E_POINTER;
        *format = GUID_WICPixelFormat32bppPBGRA; return S_OK;
    }
    HRESULT __stdcall GetResolution(double* x, double* y) override {
        if (!x || !y) return E_POINTER;
        *x = 96.0; *y = 96.0; return S_OK;
    }
    HRESULT __stdcall CopyPalette(void*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    HRESULT __stdcall CopyPixels(const WICRect* rect, uint32_t stride,
                                 uint32_t bufferBytes, uint8_t* buffer) override {
        if (!buffer) return E_POINTER;
        uint32_t x{}, y{}, width{}, height{};
        if (!detail::sourceBounds(image_, rect, x, y, width, height)) return E_INVALIDARG;
        const uint64_t rowBytes = static_cast<uint64_t>(width) * 4;
        const uint64_t required = height ? static_cast<uint64_t>(stride) * (height - 1) + rowBytes : 0;
        if (rowBytes > UINT32_MAX || stride < rowBytes || required > bufferBytes) return E_INVALIDARG;
        for (uint32_t row = 0; row < height; ++row) {
            const uint8_t* source = image_.pixels.data() + static_cast<size_t>(y + row) * image_.stride + static_cast<size_t>(x) * 4;
            std::memcpy(buffer + static_cast<size_t>(row) * stride, source, static_cast<size_t>(rowBytes));
        }
        return S_OK;
    }
    HRESULT __stdcall GetMetadataQueryReader(void** reader) override {
        if (!reader) return E_POINTER; *reader = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall GetColorContexts(uint32_t count, void** contexts, uint32_t* actual) override {
        if (count && !contexts) return E_POINTER;
        if (!actual) return E_POINTER;
        *actual = 0; return S_OK;
    }
    HRESULT __stdcall GetThumbnail(IWICBitmapSource** thumbnail) override {
        if (!thumbnail) return E_POINTER; *thumbnail = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    const imaging::ImageFrame& frame() const noexcept { return image_; }
private:
    ~BitmapFrame() { }
    std::atomic<ULONG> refs_{1};
    imaging::ImageFrame image_;
};

class BitmapDecoder final : public IWICBitmapDecoder {
public:
    BitmapDecoder(BitmapFrame* frame, const GUID& container) noexcept : frame_(frame), container_(container) {
        if (frame_) frame_->AddRef();
        initialized_ = frame_ != nullptr;
        detail::g_liveObjects.fetch_add(1, std::memory_order_relaxed);
    }
    explicit BitmapDecoder(imaging::ImageFormat expected) noexcept : expected_(expected) {
        if (const auto* container = detail::containerGuid(expected_)) container_ = *container;
        detail::g_liveObjects.fetch_add(1, std::memory_order_relaxed);
    }
    HRESULT __stdcall QueryInterface(const GUID& iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IWICBitmapDecoder) return E_NOINTERFACE;
        *out = static_cast<IWICBitmapDecoder*>(this); AddRef(); return S_OK;
    }
    ULONG __stdcall AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG __stdcall Release() override {
        const ULONG count = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!count) { detail::g_liveObjects.fetch_sub(1, std::memory_order_relaxed); detail::destroyObject(this); }
        return count;
    }
    HRESULT __stdcall QueryCapability(void*, uint32_t*) override { return E_NOTIMPL; }
    HRESULT __stdcall Initialize(void* stream, uint32_t cacheOption) override {
        if (!stream) return E_POINTER;
        if (initialized_ || cacheOption > 2) return WINCODEC_ERR_WRONGSTATE;
        imaging::ImageFrame image{};
        const auto status = detail::decodeStream(static_cast<IStream*>(stream), image, expected_);
        if (status != imaging::ImageStatus::Success) return detail::statusToHr(status);
        const GUID* detectedContainer = detail::containerGuid(image.sourceFormat);
        if (!detectedContainer) return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
        auto* frame = detail::createObject<BitmapFrame>(static_cast<imaging::ImageFrame&&>(image));
        if (!frame) return E_OUTOFMEMORY;
        frame_ = frame;
        container_ = *detectedContainer;
        initialized_ = true;
        return S_OK;
    }
    HRESULT __stdcall GetContainerFormat(GUID* format) override {
        if (!format) return E_POINTER;
        if (!initialized_) return WINCODEC_ERR_WRONGSTATE;
        *format = container_; return S_OK;
    }
    HRESULT __stdcall GetDecoderInfo(void** info) override {
        if (!info) return E_POINTER; *info = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall CopyPalette(void*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    HRESULT __stdcall GetMetadataQueryReader(void** reader) override {
        if (!reader) return E_POINTER; *reader = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall GetPreview(IWICBitmapSource** preview) override {
        if (!preview) return E_POINTER; *preview = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall GetColorContexts(uint32_t count, void** contexts, uint32_t* actual) override {
        if (count && !contexts) return E_POINTER;
        if (!actual) return E_POINTER; *actual = 0; return S_OK;
    }
    HRESULT __stdcall GetThumbnail(IWICBitmapSource** thumbnail) override {
        if (!thumbnail) return E_POINTER; *thumbnail = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall GetFrameCount(uint32_t* count) override {
        if (!count) return E_POINTER;
        if (!initialized_) return WINCODEC_ERR_WRONGSTATE;
        *count = 1; return S_OK;
    }
    HRESULT __stdcall GetFrame(uint32_t index, IWICBitmapFrameDecode** frame) override {
        if (!frame) return E_POINTER; *frame = nullptr;
        if (!initialized_) return WINCODEC_ERR_WRONGSTATE;
        if (index != 0 || !frame_) return E_INVALIDARG;
        frame_->AddRef(); *frame = frame_; return S_OK;
    }
private:
    ~BitmapDecoder() { if (frame_) frame_->Release(); }
    std::atomic<ULONG> refs_{1};
    BitmapFrame* frame_{};
    GUID container_{};
    imaging::ImageFormat expected_{imaging::ImageFormat::Unknown};
    bool initialized_{};
};

class FormatConverter final : public IWICFormatConverter {
public:
    FormatConverter() noexcept { detail::g_liveObjects.fetch_add(1, std::memory_order_relaxed); }
    HRESULT __stdcall QueryInterface(const GUID& iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IWICBitmapSource && iid != IID_IWICFormatConverter)
            return E_NOINTERFACE;
        *out = static_cast<IWICFormatConverter*>(this); AddRef(); return S_OK;
    }
    ULONG __stdcall AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG __stdcall Release() override {
        const ULONG count = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!count) { if (source_) source_->Release(); detail::g_liveObjects.fetch_sub(1, std::memory_order_relaxed); detail::destroyObject(this); }
        return count;
    }
    HRESULT __stdcall Initialize(IWICBitmapSource* source, const GUID& target,
                                 int32_t, void*, double, int32_t) override {
        if (!source) return E_POINTER;
        if (target != GUID_WICPixelFormat32bppPBGRA) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        GUID sourceFormat{}; const HRESULT result = source->GetPixelFormat(&sourceFormat);
        if (result != S_OK) return result;
        if (sourceFormat != GUID_WICPixelFormat32bppPBGRA) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        if (source_) source_->Release(); source_ = source; source_->AddRef(); initialized_ = true; return S_OK;
    }
    HRESULT __stdcall CanConvert(const GUID& source, const GUID& destination, int32_t* can) override {
        if (!can) return E_POINTER;
        *can = source == GUID_WICPixelFormat32bppPBGRA && destination == GUID_WICPixelFormat32bppPBGRA;
        return S_OK;
    }
    HRESULT __stdcall GetSize(uint32_t* width, uint32_t* height) override {
        return initialized_ ? source_->GetSize(width, height) : WINCODEC_ERR_WRONGSTATE;
    }
    HRESULT __stdcall GetPixelFormat(GUID* format) override {
        if (!format) return E_POINTER;
        if (!initialized_) return WINCODEC_ERR_WRONGSTATE;
        *format = GUID_WICPixelFormat32bppPBGRA; return S_OK;
    }
    HRESULT __stdcall GetResolution(double* x, double* y) override {
        return initialized_ ? source_->GetResolution(x, y) : WINCODEC_ERR_WRONGSTATE;
    }
    HRESULT __stdcall CopyPalette(void* palette) override {
        return initialized_ ? source_->CopyPalette(palette) : WINCODEC_ERR_WRONGSTATE;
    }
    HRESULT __stdcall CopyPixels(const WICRect* rect, uint32_t stride, uint32_t size, uint8_t* buffer) override {
        if (!buffer) return E_POINTER;
        return initialized_ ? source_->CopyPixels(rect, stride, size, buffer) : WINCODEC_ERR_WRONGSTATE;
    }
private:
    ~FormatConverter() { }
    std::atomic<ULONG> refs_{1};
    IWICBitmapSource* source_{};
    bool initialized_{};
};

class BitmapFrameEncode final : public IWICBitmapFrameEncode {
public:
    explicit BitmapFrameEncode(const imaging::ImageAllocator& allocator) noexcept : allocator_(allocator) {
        detail::g_liveObjects.fetch_add(1, std::memory_order_relaxed);
    }
    HRESULT __stdcall QueryInterface(const GUID& iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IWICBitmapFrameEncode) return E_NOINTERFACE;
        *out = static_cast<IWICBitmapFrameEncode*>(this); AddRef(); return S_OK;
    }
    ULONG __stdcall AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG __stdcall Release() override {
        const ULONG count = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!count) { detail::g_liveObjects.fetch_sub(1, std::memory_order_relaxed); detail::destroyObject(this); }
        return count;
    }
    HRESULT __stdcall Initialize(void* options) override { return options ? E_NOTIMPL : (initialized_ = true, S_OK); }
    HRESULT __stdcall SetSize(uint32_t width, uint32_t height) override {
        if (!initialized_ || writtenRows_ || committed_) return WINCODEC_ERR_WRONGSTATE;
        if (!width || !height) return E_INVALIDARG;
        const uint64_t stride = static_cast<uint64_t>(width) * 4;
        const uint64_t pixels = static_cast<uint64_t>(width) * height;
        const uint64_t bytes = stride * height;
        if (stride > UINT32_MAX || pixels > limits_.maxPixels || bytes > limits_.maxDecodedBytes || bytes > SIZE_MAX)
            return WINCODEC_ERR_VALUEOVERFLOW;
        frame_.pixels.reset();
        if (!frame_.pixels.allocate(static_cast<size_t>(bytes), allocator_)) return E_OUTOFMEMORY;
        frame_.width = width; frame_.height = height; frame_.stride = static_cast<uint32_t>(stride);
        frame_.sourceFormat = imaging::ImageFormat::Unknown;
        frame_.format = imaging::PixelFormat::Bgra8Premultiplied;
        frame_.sourceHadAlpha = true;
        return S_OK;
    }
    HRESULT __stdcall SetResolution(double dpiX, double dpiY) override {
        if (!initialized_ || dpiX <= 0 || dpiY <= 0) return E_INVALIDARG;
        return S_OK;
    }
    HRESULT __stdcall SetPixelFormat(GUID* format) override {
        if (!format) return E_POINTER;
        if (!initialized_ || !frame_.width) return WINCODEC_ERR_WRONGSTATE;
        if (*format != GUID_WICPixelFormat32bppPBGRA) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        pixelFormatSet_ = true; return S_OK;
    }
    HRESULT __stdcall SetColorContexts(uint32_t count, void**) override {
        return count == 0 ? S_OK : WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall SetPalette(void*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    HRESULT __stdcall SetThumbnail(IWICBitmapSource*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    HRESULT __stdcall WritePixels(uint32_t lineCount, uint32_t stride, uint32_t bufferBytes, uint8_t* buffer) override {
        if (!buffer) return E_POINTER;
        if (!pixelFormatSet_ || !frame_.width || !lineCount || writtenRows_ > frame_.height ||
            lineCount > frame_.height - writtenRows_) return WINCODEC_ERR_WRONGSTATE;
        const uint64_t rowBytes = static_cast<uint64_t>(frame_.width) * 4;
        const uint64_t required = static_cast<uint64_t>(stride) * lineCount;
        if (stride < rowBytes || required > bufferBytes) return E_INVALIDARG;
        for (uint32_t y = 0; y < lineCount; ++y)
            std::memcpy(frame_.pixels.data() + static_cast<size_t>(writtenRows_ + y) * frame_.stride,
                        buffer + static_cast<size_t>(y) * stride, static_cast<size_t>(rowBytes));
        writtenRows_ += lineCount; return S_OK;
    }
    HRESULT __stdcall WriteSource(IWICBitmapSource* source, const WICRect* rect) override {
        if (!source) return E_POINTER;
        if (!initialized_ || writtenRows_) return WINCODEC_ERR_WRONGSTATE;
        uint32_t sourceWidth{}, sourceHeight{};
        HRESULT result = source->GetSize(&sourceWidth, &sourceHeight);
        if (result != S_OK) return result;
        uint32_t x{}, y{}, width = sourceWidth, height = sourceHeight;
        if (rect && !detail::sourceBounds(sourceWidth, sourceHeight, rect, x, y, width, height))
            return E_INVALIDARG;
        if (!frame_.width) { result = SetSize(width, height); if (result != S_OK) return result; }
        if (frame_.width != width || frame_.height != height) return E_INVALIDARG;
        GUID format{}; result = source->GetPixelFormat(&format);
        if (result != S_OK) return result;
        if (format != GUID_WICPixelFormat32bppPBGRA) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        if (!pixelFormatSet_) { GUID target = GUID_WICPixelFormat32bppPBGRA; result = SetPixelFormat(&target); if (result != S_OK) return result; }
        WICRect selected{static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(width), static_cast<int32_t>(height)};
        const uint64_t bytes = static_cast<uint64_t>(frame_.stride) * frame_.height;
        if (bytes > UINT32_MAX) return WINCODEC_ERR_VALUEOVERFLOW;
        result = source->CopyPixels(&selected, frame_.stride, static_cast<uint32_t>(bytes), frame_.pixels.data());
        if (result == S_OK) writtenRows_ = frame_.height;
        return result;
    }
    HRESULT __stdcall Commit() override {
        if (!pixelFormatSet_ || writtenRows_ != frame_.height || !frame_.pixels.data())
            return WINCODEC_ERR_WRONGSTATE;
        committed_ = true; return S_OK;
    }
    HRESULT __stdcall GetMetadataQueryWriter(void** writer) override {
        if (!writer) return E_POINTER; *writer = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    const imaging::ImageFrame& frame() const noexcept { return frame_; }
    bool committed() const noexcept { return committed_; }
private:
    ~BitmapFrameEncode() { }
    std::atomic<ULONG> refs_{1};
    imaging::ImageAllocator allocator_{};
    imaging::ImageLimits limits_{};
    imaging::ImageFrame frame_{};
    uint32_t writtenRows_{};
    bool initialized_{};
    bool pixelFormatSet_{};
    bool committed_{};
};

class BitmapEncoder final : public IWICBitmapEncoder {
public:
    BitmapEncoder(const imaging::ImageAllocator& allocator, imaging::ImageFormat format) noexcept
        : allocator_(allocator), format_(format) {
        if (const auto* guid = detail::containerGuid(format_)) container_ = *guid;
        detail::g_liveObjects.fetch_add(1, std::memory_order_relaxed);
    }
    HRESULT __stdcall QueryInterface(const GUID& iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IWICBitmapEncoder) return E_NOINTERFACE;
        *out = static_cast<IWICBitmapEncoder*>(this); AddRef(); return S_OK;
    }
    ULONG __stdcall AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG __stdcall Release() override {
        const ULONG count = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!count) { detail::g_liveObjects.fetch_sub(1, std::memory_order_relaxed); detail::destroyObject(this); }
        return count;
    }
    HRESULT __stdcall Initialize(void* stream, uint32_t cacheOption) override {
        if (!stream) return E_POINTER;
        if (initialized_ || cacheOption > 2) return E_INVALIDARG;
        stream_ = static_cast<IStream*>(stream); stream_->AddRef(); initialized_ = true; return S_OK;
    }
    HRESULT __stdcall GetContainerFormat(GUID* format) override {
        if (!format) return E_POINTER; *format = container_; return S_OK;
    }
    HRESULT __stdcall GetEncoderInfo(void** info) override {
        if (!info) return E_POINTER; *info = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall SetColorContexts(uint32_t count, void**) override {
        return count == 0 ? S_OK : WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT __stdcall SetPalette(void*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    HRESULT __stdcall SetThumbnail(IWICBitmapSource*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    HRESULT __stdcall SetPreview(IWICBitmapSource*) override { return WINCODEC_ERR_UNSUPPORTEDOPERATION; }
    HRESULT __stdcall CreateNewFrame(void** frame, void** options) override {
        if (!frame) return E_POINTER; *frame = nullptr;
        if (options) *options = nullptr;
        if (!initialized_ || frame_) return WINCODEC_ERR_WRONGSTATE;
        auto* created = detail::createObject<BitmapFrameEncode>(allocator_);
        if (!created) return E_OUTOFMEMORY;
        frame_ = created; frame_->AddRef(); *frame = static_cast<IWICBitmapFrameEncode*>(created);
        return S_OK;
    }
    HRESULT __stdcall Commit() override {
        if (!initialized_ || !stream_ || !frame_ || !frame_->committed()) return WINCODEC_ERR_WRONGSTATE;
        imaging::EncodedImage encoded{};
        const auto encodeStatus = imaging::EncodeImage(frame_->frame(), format_, encoded,
            allocator_, {.quality = 90});
        if (encodeStatus != imaging::ImageStatus::Success) return detail::statusToHr(encodeStatus);
        size_t offset = 0;
        while (offset < encoded.byteLength) {
            const uint32_t request = static_cast<uint32_t>(std::min<size_t>(encoded.byteLength - offset, 64 * 1024));
            uint32_t written{};
            const HRESULT result = stream_->Write(encoded.bytes.data() + offset, request, &written);
            if (result != S_OK) return result;
            if (written == 0 || written > request) return WINCODEC_ERR_STREAMREAD;
            offset += written;
        }
        return stream_->Commit(0);
    }
    HRESULT __stdcall GetMetadataQueryWriter(void** writer) override {
        if (!writer) return E_POINTER; *writer = nullptr; return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
private:
    ~BitmapEncoder() { if (frame_) frame_->Release(); if (stream_) stream_->Release(); }
    std::atomic<ULONG> refs_{1};
    imaging::ImageAllocator allocator_{};
    imaging::ImageFormat format_{imaging::ImageFormat::Unknown};
    GUID container_{};
    IStream* stream_{};
    BitmapFrameEncode* frame_{};
    bool initialized_{};
};

class ImagingFactory final : public IWICImagingFactory {
public:
    ImagingFactory() noexcept { detail::g_liveObjects.fetch_add(1, std::memory_order_relaxed); }
    HRESULT __stdcall QueryInterface(const GUID& iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IWICImagingFactory) return E_NOINTERFACE;
        *out = static_cast<IWICImagingFactory*>(this); AddRef(); return S_OK;
    }
    ULONG __stdcall AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG __stdcall Release() override {
        const ULONG count = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!count) { detail::g_liveObjects.fetch_sub(1, std::memory_order_relaxed); detail::destroyObject(this); }
        return count;
    }
    HRESULT __stdcall CreateDecoderFromFilename(const wchar_t* path, const GUID* vendor,
        uint32_t desiredAccess, uint32_t metadataOptions, IWICBitmapDecoder** decoder) override {
        if (!decoder) return E_POINTER; *decoder = nullptr;
        if (!path || !*path) return E_INVALIDARG;
        if (vendor || metadataOptions > 1) return E_INVALIDARG;
        imaging::ImageFrame image{};
        const auto status = imaging::DecodeImageFileWithAccess(
            path, image, imaging::NativeImageAllocator(), desiredAccess);
        if (status != imaging::ImageStatus::Success) return detail::statusToHr(status);
        const GUID* container = detail::containerGuid(image.sourceFormat);
        if (!container) return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
        auto* frame = detail::createObject<BitmapFrame>(static_cast<imaging::ImageFrame&&>(image));
        if (!frame) return E_OUTOFMEMORY;
        auto* result = detail::createObject<BitmapDecoder>(frame, *container);
        frame->Release();
        if (!result) return E_OUTOFMEMORY;
        *decoder = result; return S_OK;
    }
    HRESULT __stdcall CreateDecoderFromStream(IStream* stream, const GUID* vendor,
        uint32_t metadataOptions, IWICBitmapDecoder** decoder) override {
        if (!decoder) return E_POINTER;
        *decoder = nullptr;
        if (!stream || vendor || metadataOptions > 1) return E_INVALIDARG;
        imaging::ImageFrame image{};
        const auto status = detail::decodeStream(stream, image);
        if (status != imaging::ImageStatus::Success) return detail::statusToHr(status);
        const GUID* container = detail::containerGuid(image.sourceFormat);
        if (!container) return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
        auto* frame = detail::createObject<BitmapFrame>(static_cast<imaging::ImageFrame&&>(image));
        if (!frame) return E_OUTOFMEMORY;
        auto* result = detail::createObject<BitmapDecoder>(frame, *container);
        frame->Release();
        if (!result) return E_OUTOFMEMORY;
        *decoder = result;
        return S_OK;
    }
    HRESULT __stdcall CreateDecoderFromFileHandle(uintptr_t handle, const GUID* vendor,
        uint32_t metadataOptions, IWICBitmapDecoder** decoder) override {
        if (!decoder) return E_POINTER;
        *decoder = nullptr;
        if (!handle || vendor || metadataOptions > 1) return E_INVALIDARG;
        imaging::ImageFrame image{};
        const auto status = detail::decodeFileHandle(handle, image);
        if (status != imaging::ImageStatus::Success) return detail::statusToHr(status);
        const GUID* container = detail::containerGuid(image.sourceFormat);
        if (!container) return WINCODEC_ERR_UNKNOWNIMAGEFORMAT;
        auto* frame = detail::createObject<BitmapFrame>(static_cast<imaging::ImageFrame&&>(image));
        if (!frame) return E_OUTOFMEMORY;
        auto* result = detail::createObject<BitmapDecoder>(frame, *container);
        frame->Release();
        if (!result) return E_OUTOFMEMORY;
        *decoder = result;
        return S_OK;
    }
    HRESULT __stdcall CreateComponentInfo(const GUID&, void** info) override {
        if (!info) return E_POINTER; *info = nullptr; return WINCODEC_ERR_COMPONENTNOTFOUND;
    }
    HRESULT __stdcall CreateDecoder(const GUID& container, const GUID* vendor, IWICBitmapDecoder** decoder) override {
        if (!decoder) return E_POINTER;
        *decoder = nullptr;
        if (vendor) return E_INVALIDARG;
        imaging::ImageFormat format = imaging::ImageFormat::Unknown;
        if (container == GUID_ContainerFormatBmp) format = imaging::ImageFormat::Bmp;
        else if (container == GUID_ContainerFormatPng) format = imaging::ImageFormat::Png;
        else if (container == GUID_ContainerFormatJpeg) format = imaging::ImageFormat::Jpeg;
        if (format == imaging::ImageFormat::Unknown) return WINCODEC_ERR_COMPONENTNOTFOUND;
        auto* result = detail::createObject<BitmapDecoder>(format);
        if (!result) return E_OUTOFMEMORY;
        *decoder = result;
        return S_OK;
    }
    HRESULT __stdcall CreateEncoder(const GUID& container, const GUID* vendor, IWICBitmapEncoder** encoder) override {
        if (!encoder) return E_POINTER; *encoder = nullptr;
        if (vendor) return E_INVALIDARG;
        imaging::ImageFormat format = imaging::ImageFormat::Unknown;
        if (container == GUID_ContainerFormatBmp) format = imaging::ImageFormat::Bmp;
        else if (container == GUID_ContainerFormatPng) format = imaging::ImageFormat::Png;
        else if (container == GUID_ContainerFormatJpeg) format = imaging::ImageFormat::Jpeg;
        if (format == imaging::ImageFormat::Unknown) return WINCODEC_ERR_COMPONENTNOTFOUND;
        auto* result = detail::createObject<BitmapEncoder>(imaging::NativeImageAllocator(), format);
        if (!result) return E_OUTOFMEMORY;
        *encoder = result; return S_OK;
    }
    HRESULT __stdcall CreatePalette(void** palette) override { if (!palette) return E_POINTER; *palette = nullptr; return E_NOTIMPL; }
    HRESULT __stdcall CreateFormatConverter(IWICFormatConverter** converter) override {
        if (!converter) return E_POINTER; *converter = nullptr;
        auto* result = detail::createObject<FormatConverter>();
        if (!result) return E_OUTOFMEMORY;
        *converter = result; return S_OK;
    }
    HRESULT __stdcall CreateBitmapScaler(void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmapClipper(void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmapFlipRotator(void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateStream(void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateColorContext(void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateColorTransformer(void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmap(uint32_t, uint32_t, const GUID&, uint32_t, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmapFromSource(IWICBitmapSource*, uint32_t, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmapFromSourceRect(IWICBitmapSource*, uint32_t, uint32_t, uint32_t, uint32_t, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmapFromMemory(uint32_t, uint32_t, const GUID&, uint32_t, uint32_t, uint8_t*, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmapFromHBITMAP(void*, void*, int32_t, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateBitmapFromHICON(void*, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateComponentEnumerator(uint32_t, uint32_t, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateFastMetadataEncoderFromDecoder(IWICBitmapDecoder*, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateFastMetadataEncoderFromFrameDecode(IWICBitmapFrameDecode*, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateQueryWriter(const GUID&, const GUID*, void** value) override { return notImplemented(value); }
    HRESULT __stdcall CreateQueryWriterFromReader(void*, const GUID*, void** value) override { return notImplemented(value); }
private:
    static HRESULT notImplemented(void** value) noexcept {
        if (!value) return E_POINTER; *value = nullptr; return E_NOTIMPL;
    }
    ~ImagingFactory() { }
    std::atomic<ULONG> refs_{1};
};

} // namespace micant::um::wic

extern "C" __declspec(dllexport) micant::um::wic::HRESULT __stdcall
WICCreateImagingFactory_Proxy(uint32_t version, micant::um::wic::IWICImagingFactory** factory) noexcept {
    using namespace micant::um::wic;
    if (!factory) return E_POINTER;
    *factory = nullptr;
    if (version != 0 && version != 0x0236) return E_INVALIDARG;
    auto* result = detail::createObject<ImagingFactory>();
    if (!result) return E_OUTOFMEMORY;
    *factory = result; return S_OK;
}

extern "C" __declspec(dllexport) micant::um::wic::HRESULT __stdcall
DllCanUnloadNow() noexcept {
    return micant::um::wic::detail::g_liveObjects.load(std::memory_order_acquire) == 0
        ? micant::um::wic::S_OK : micant::um::wic::S_FALSE;
}
