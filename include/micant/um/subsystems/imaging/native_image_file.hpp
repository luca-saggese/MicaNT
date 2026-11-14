#pragma once

// Bounded Ring-3 image-file reader over the native NT file APIs. The caller's
// token and filesystem access check govern the open; the returned frame owns
// only user-mode memory allocated by the supplied allocator.
#include "codec_registry.hpp"
#include "micant/shared/native_nt_api.hpp"

#include <algorithm>
#include <limits>

namespace micant::um::imaging {

inline ImageStatus DecodeImageFileWithAccess(const wchar_t* path, ImageFrame& output,
                                             const ImageAllocator& allocator,
                                             uint32_t desiredAccess,
                                             ImageLimits limits = {}) noexcept {
    if (!path || !*path || !allocator.allocate || !allocator.deallocate)
        return ImageStatus::MalformedData;
    size_t pathChars = 0;
    while (path[pathChars]) {
        if (++pathChars > 32767) return ImageStatus::ImageTooLarge;
    }
    const size_t pathBytes = pathChars * sizeof(wchar_t);
    if (pathBytes > std::numeric_limits<uint16_t>::max() - sizeof(wchar_t))
        return ImageStatus::ImageTooLarge;

    UnicodeString name{};
    name.length = static_cast<uint16_t>(pathBytes);
    name.maximumLength = static_cast<uint16_t>(pathBytes + sizeof(wchar_t));
    name.buffer = path;
    ObjectAttributes attributes{};
    attributes.objectName = &name;
    attributes.attributes = OBJ_CASE_INSENSITIVE;

    Handle file{};
    IoStatusBlock ioStatus{};
    if ((desiredAccess & 0x80000001u) == 0) return ImageStatus::AccessDenied;
    // The implementation uses a synchronous NT handle and requires SYNCHRONIZE;
    // retain all caller-requested rights while adding only that required bit.
    desiredAccess |= 0x00100000u;
    NtStatus status = native_nt::NtCreateFile(&file, desiredAccess, &attributes,
        &ioStatus, nullptr, 0, 1, 1, 0x20, nullptr, 0);
    if (status != NtStatus::Success) {
        return status == NtStatus::AccessDenied ? ImageStatus::AccessDenied : ImageStatus::IoFailure;
    }
    struct FileHandleGuard {
        Handle handle;
        ~FileHandleGuard() { (void)native_nt::NtClose(handle); }
    } fileGuard{file};

    FileStandardInformation standard{};
    status = native_nt::NtQueryInformationFile(file, &ioStatus, &standard,
        sizeof(standard), FileInformationClass::FileStandardInformation);
    if (status != NtStatus::Success) return ImageStatus::IoFailure;
    if (standard.directory || standard.endOfFile.quadPart <= 0)
        return ImageStatus::MalformedData;
    const uint64_t fileBytes = static_cast<uint64_t>(standard.endOfFile.quadPart);
    if (fileBytes > limits.maxEncodedBytes || fileBytes > std::numeric_limits<size_t>::max())
        return ImageStatus::ImageTooLarge;
    if (fileBytes > limits.maxIntermediateBytes) return ImageStatus::ImageTooLarge;

    ImageBuffer encoded{};
    if (!encoded.allocate(static_cast<size_t>(fileBytes), allocator))
        return ImageStatus::OutOfMemory;
    size_t completed = 0;
    while (completed < encoded.size()) {
        const uint32_t request = static_cast<uint32_t>(std::min<size_t>(
            encoded.size() - completed, 64 * 1024));
        LargeInteger offset{};
        offset.quadPart = static_cast<int64_t>(completed);
        ioStatus = {};
        status = native_nt::NtReadFile(file, 0, nullptr, nullptr, &ioStatus,
            encoded.data() + completed, request, &offset, nullptr);
        if (status != NtStatus::Success) return ImageStatus::IoFailure;
        if (ioStatus.information == 0) return ImageStatus::TruncatedInput;
        if (ioStatus.information > request) return ImageStatus::IoFailure;
        completed += static_cast<size_t>(ioStatus.information);
    }

    limits.maxIntermediateBytes -= fileBytes;
    limits.maxDecodedBytes = std::min(limits.maxDecodedBytes, limits.maxIntermediateBytes);
    const auto decoded = DecodeImage(encoded.span(), output, allocator, limits);
    if (decoded != ImageStatus::Success) return decoded;
    return ImageStatus::Success;
}

// Preserve the image-file reader's existing read-only behavior for callers
// that do not need to supply a WIC/NT desired access mask.
inline ImageStatus DecodeImageFile(const wchar_t* path, ImageFrame& output,
                                  const ImageAllocator& allocator,
                                  ImageLimits limits = {}) noexcept {
    return DecodeImageFileWithAccess(path, output, allocator, 0x00100081u, limits);
}

} // namespace micant::um::imaging
