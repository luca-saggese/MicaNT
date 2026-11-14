#pragma once

// Encode in user mode, then save through the caller's ordinary NT file access.
#include "codec_registry.hpp"
#include "micant/shared/native_nt_api.hpp"

#include <algorithm>
#include <limits>

namespace micant::um::imaging {

inline ImageStatus EncodeImageFile(const wchar_t* path, const ImageFrame& frame,
                                   ImageFormat format, const ImageAllocator& allocator,
                                   ImageEncodeOptions options = {},
                                   ImageLimits limits = {}) noexcept {
    if (!path || !*path) return ImageStatus::MalformedData;
    size_t pathChars = 0;
    while (path[pathChars]) {
        if (++pathChars > 32767) return ImageStatus::ImageTooLarge;
    }
    const size_t pathBytes = pathChars * sizeof(wchar_t);
    if (pathBytes > std::numeric_limits<uint16_t>::max() - sizeof(wchar_t))
        return ImageStatus::ImageTooLarge;

    EncodedImage encoded{};
    const auto encodeStatus = EncodeImage(frame, format, encoded, allocator, options, limits);
    if (encodeStatus != ImageStatus::Success) return encodeStatus;
    if (!encoded.byteLength || encoded.byteLength > encoded.bytes.size() ||
        encoded.byteLength > limits.maxEncodedBytes)
        return ImageStatus::ImageTooLarge;

    UnicodeString name{};
    name.length = static_cast<uint16_t>(pathBytes);
    name.maximumLength = static_cast<uint16_t>(pathBytes + sizeof(wchar_t));
    name.buffer = path;
    ObjectAttributes attributes{};
    attributes.length = sizeof(ObjectAttributes);
    attributes.objectName = &name;
    attributes.attributes = OBJ_CASE_INSENSITIVE;

    Handle file{};
    IoStatusBlock ioStatus{};
    // FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE.
    // FILE_OVERWRITE_IF keeps access checks in the filesystem and truncates old content.
    NtStatus status = native_nt::NtCreateFile(&file, 0x00100116u, &attributes,
        &ioStatus, nullptr, 0, 0x7, 5, 0x20, nullptr, 0);
    if (status != NtStatus::Success)
        return status == NtStatus::AccessDenied ? ImageStatus::AccessDenied : ImageStatus::IoFailure;
    struct FileHandleGuard {
        Handle handle;
        ~FileHandleGuard() { if (handle) (void)native_nt::NtClose(handle); }
        NtStatus close() noexcept {
            if (!handle) return NtStatus::Success;
            const Handle owned = handle;
            handle = {};
            return native_nt::NtClose(owned);
        }
    } fileGuard{file};

    size_t completed = 0;
    while (completed < encoded.byteLength) {
        const uint32_t request = static_cast<uint32_t>(std::min<size_t>(
            encoded.byteLength - completed, 64 * 1024));
        LargeInteger offset{};
        offset.quadPart = static_cast<int64_t>(completed);
        ioStatus = {};
        status = native_nt::NtWriteFile(file, 0, nullptr, nullptr, &ioStatus,
            encoded.bytes.data() + completed, request, &offset, nullptr);
        if (status != NtStatus::Success || ioStatus.information == 0 ||
            ioStatus.information > request)
            return ImageStatus::IoFailure;
        completed += static_cast<size_t>(ioStatus.information);
    }
    ioStatus = {};
    status = native_nt::NtFlushBuffersFile(file, &ioStatus);
    if (status != NtStatus::Success) return ImageStatus::IoFailure;
    return fileGuard.close() == NtStatus::Success ? ImageStatus::Success : ImageStatus::IoFailure;
}

} // namespace micant::um::imaging
