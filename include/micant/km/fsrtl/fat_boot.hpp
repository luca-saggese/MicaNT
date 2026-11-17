#pragma once
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace micant::io { struct FileBackend; }

namespace micant::fsrtl::fat {
struct MountedVolumeSnapshot {
    char Drive{};
    uint64_t PartitionStartSector{};
    uint64_t TotalSectors{};
    NtStatus NtdllStatus{NtStatus::DeviceNotReady};
    NtStatus FileImageStatus{NtStatus::DeviceNotReady};
};
NtStatus initialize() noexcept;
NtStatus open(std::u16string_view name, bool openIf, uint32_t desiredAccess,
              io::FileBackend& backend) noexcept;
// Resolve the installed system directory on the unique mounted volume that
// actually contains the system NTDLL. Returns an absolute DOS path.
NtStatus systemDirectory(char16_t* output, size_t capacity, size_t& length) noexcept;
// Snapshot mounted FAT volumes and probe the two files used to select and
// verify the system root. This is a diagnostic/query path; it does not select
// or alter the system-root mount.
NtStatus enumerateMountedVolumes(MountedVolumeSnapshot* output, size_t capacity,
                                 size_t& count) noexcept;
NtStatus mount(const void* owner, Handle blockHandle, uint64_t& token) noexcept;
// Marks the volume offline immediately. Busy means open files still pin it;
// the owner must retry before releasing the underlying controller.
NtStatus unmount(const void* owner, uint64_t token) noexcept;
// Read one bounded file from a specific mounted volume. The mount owner and
// token are checked so a removable volume can be verified without selecting
// a different globally mounted filesystem by drive-letter order.
NtStatus readMountedFile(const void* owner, uint64_t token,
                         std::u16string_view relativePath, void* buffer,
                         size_t capacity, size_t& bytesRead) noexcept;
}
