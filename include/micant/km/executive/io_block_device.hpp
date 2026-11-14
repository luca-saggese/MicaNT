#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/ob.hpp"

namespace micant::io {

using BlockDeviceReadRoutine = NtStatus (*)(void* context, uint64_t lba,
                                             uint32_t blockCount, void* buffer,
                                             uint32_t* blocksRead) noexcept;
using BlockDeviceWriteRoutine = NtStatus (*)(void* context, uint64_t lba,
                                              uint32_t blockCount, const void* buffer,
                                              uint32_t* blocksWritten) noexcept;
using BlockDeviceFlushRoutine = NtStatus (*)(void* context) noexcept;
using BlockDeviceReleaseRoutine = void (*)(void* context) noexcept;

// Io consumes context when create is called and releases it on failure or
// once when the final Ob reference ends. A missing write callback is read-only.
// Providers return Success only after the submitted blocks are synchronized to
// their storage medium.
struct BlockDeviceBackend {
    void* context{};
    uint64_t totalBlocks{};
    uint32_t blockSize{};
    BlockDeviceReadRoutine read{};
    BlockDeviceReleaseRoutine release{};
    BlockDeviceWriteRoutine write{};
    // Optional cache flush/barrier. Missing means durability cannot be
    // established and IoFlushBlockDeviceHandle returns NotSupported.
    BlockDeviceFlushRoutine flush{};
};

class BlockDeviceObject {
public:
    explicit BlockDeviceObject(BlockDeviceBackend backend) noexcept : backend_(backend) {}
    ~BlockDeviceObject() { if (backend_.release) backend_.release(backend_.context); }
    BlockDeviceObject(const BlockDeviceObject&) = delete;
    BlockDeviceObject& operator=(const BlockDeviceObject&) = delete;

    [[nodiscard]] uint64_t totalBlocks() const noexcept { return backend_.totalBlocks; }
    [[nodiscard]] uint32_t blockSize() const noexcept { return backend_.blockSize; }
    [[nodiscard]] bool writable() const noexcept { return backend_.write != nullptr; }
    [[nodiscard]] uint64_t capacityBytes() const noexcept {
        return backend_.totalBlocks * backend_.blockSize;
    }

    NtStatus read(uint64_t lba, uint32_t blockCount, void* buffer,
                  uint32_t* blocksRead) const noexcept;
    NtStatus write(uint64_t lba, uint32_t blockCount, const void* buffer,
                   uint32_t* blocksWritten) const noexcept;
    // Mounted filesystem writes remain subject to its stream leases. Raw
    // writers are excluded while any image lease exists on this device.
    NtStatus writeMounted(uint64_t lba, uint32_t blockCount, const void* buffer,
                          uint32_t* blocksWritten) const noexcept;
    NtStatus flush() const noexcept;
    bool acquireImageLease() const noexcept;
    void releaseImageLease() const noexcept;

private:
    NtStatus writeInternal(uint64_t, uint32_t, const void*, uint32_t*, bool) const noexcept;
    static constexpr uint64_t WriterBit = uint64_t{1} << 63;
    mutable std::atomic<uint64_t> imageLeaseState_{};
    BlockDeviceBackend backend_{};
};

// Initialize the Io-owned Ob type once during kernel startup before creating
// backend objects. Handles use the current canonical handle table.
NtStatus IoInitializeBlockDevices() noexcept;
NtStatus IoCreateBlockDevice(Handle* deviceHandle, uint32_t desiredAccess,
                             const BlockDeviceBackend& backend) noexcept;
NtStatus IoReferenceBlockDeviceHandle(Handle deviceHandle, uint32_t desiredAccess,
                                      BlockDeviceObject*& device,
                                      ob::ObjectHeader*& reference) noexcept;
NtStatus IoReadBlockDeviceHandle(Handle deviceHandle, uint64_t lba,
                                 uint32_t blockCount, void* buffer,
                                 uint32_t* blocksRead) noexcept;
NtStatus IoWriteBlockDeviceHandle(Handle deviceHandle, uint64_t lba,
                                  uint32_t blockCount, const void* buffer,
                                  uint32_t* blocksWritten) noexcept;
NtStatus IoFlushBlockDeviceHandle(Handle deviceHandle) noexcept;

} // namespace micant::io
