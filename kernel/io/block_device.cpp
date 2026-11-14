#include "micant/km/executive/io_block_device.hpp"
#include <atomic>
#include <limits>
#include <new>

namespace micant::io {
namespace {

ob::ObjectType g_blockDeviceType{};
std::atomic_flag g_blockDeviceTypeLock = ATOMIC_FLAG_INIT;
std::atomic<bool> g_blockDeviceTypeInitialized{false};

void lockType() noexcept {
    while (g_blockDeviceTypeLock.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause");
#endif
    }
}

void unlockType() noexcept {
    g_blockDeviceTypeLock.clear(std::memory_order_release);
}

void deleteBlockDevice(ob::ObjectHeader* header) noexcept {
    header->as<BlockDeviceObject>()->~BlockDeviceObject();
}

bool validBackend(const BlockDeviceBackend& backend) noexcept {
    return backend.read && backend.release &&
           backend.totalBlocks != 0 && backend.blockSize >= 512 &&
           backend.blockSize <= 65536 &&
           (backend.blockSize & (backend.blockSize - 1)) == 0 &&
           backend.totalBlocks <= UINT64_MAX / backend.blockSize;
}

} // namespace

NtStatus BlockDeviceObject::read(uint64_t lba, uint32_t blockCount, void* buffer,
                                 uint32_t* blocksRead) const noexcept {
    if (!blocksRead) return NtStatus::InvalidParameter;
    *blocksRead = 0;
    if (blockCount == 0) return NtStatus::Success;
    if (!buffer || !backend_.read) return NtStatus::InvalidParameter;
    if (lba >= backend_.totalBlocks ||
        blockCount > backend_.totalBlocks - lba) return NtStatus::EndOfFile;
    if (blockCount > SIZE_MAX / backend_.blockSize) return NtStatus::BufferOverflow;

    uint32_t transferred{};
    const NtStatus status = backend_.read(backend_.context, lba, blockCount,
                                          buffer, &transferred);
    if (transferred > blockCount) return NtStatus::Unsuccessful;
    *blocksRead = transferred;
    if (status != NtStatus::Success) return status;
    return transferred == blockCount ? NtStatus::Success : NtStatus::EndOfFile;
}

NtStatus BlockDeviceObject::write(uint64_t lba, uint32_t blockCount, const void* buffer,
                                  uint32_t* blocksWritten) const noexcept {
    return writeInternal(lba, blockCount, buffer, blocksWritten, false);
}
NtStatus BlockDeviceObject::writeMounted(uint64_t lba, uint32_t blockCount, const void* buffer,
                                         uint32_t* blocksWritten) const noexcept {
    return writeInternal(lba, blockCount, buffer, blocksWritten, true);
}
NtStatus BlockDeviceObject::flush() const noexcept {
    if (!backend_.flush) return NtStatus::NotSupported;
    auto state = imageLeaseState_.load(std::memory_order_acquire);
    for (;;) {
        if (state & WriterBit) return NtStatus::DeviceBusy;
        if (imageLeaseState_.compare_exchange_weak(state, state | WriterBit,
                std::memory_order_acq_rel, std::memory_order_acquire)) break;
    }
    const NtStatus status = backend_.flush(backend_.context);
    imageLeaseState_.fetch_and(~WriterBit, std::memory_order_release);
    return status;
}
bool BlockDeviceObject::acquireImageLease() const noexcept {
    auto state = imageLeaseState_.load(std::memory_order_acquire);
    for (;;) {
        if ((state & WriterBit) || state == WriterBit - 1) return false;
        if (imageLeaseState_.compare_exchange_weak(state, state + 1,
                std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    }
}
void BlockDeviceObject::releaseImageLease() const noexcept {
    const auto state = imageLeaseState_.fetch_sub(1, std::memory_order_acq_rel);
    if (!(state & ~WriterBit)) __builtin_trap();
}
NtStatus BlockDeviceObject::writeInternal(uint64_t lba, uint32_t blockCount, const void* buffer,
                                           uint32_t* blocksWritten, bool mounted) const noexcept {
    if (!blocksWritten) return NtStatus::InvalidParameter;
    *blocksWritten = 0;
    if (blockCount == 0) return NtStatus::Success;
    if (!buffer) return NtStatus::InvalidParameter;
    if (!backend_.write) return NtStatus::MediaWriteProtected;
    if (lba >= backend_.totalBlocks || blockCount > backend_.totalBlocks - lba)
        return NtStatus::EndOfFile;
    if (blockCount > SIZE_MAX / backend_.blockSize) return NtStatus::BufferOverflow;
    auto state = imageLeaseState_.load(std::memory_order_acquire);
    for (;;) {
        if (state & WriterBit) return NtStatus::DeviceBusy;
        if (!mounted && state) return NtStatus::SharingViolation;
        if (imageLeaseState_.compare_exchange_weak(state, state | WriterBit,
                std::memory_order_acq_rel, std::memory_order_acquire)) break;
    }
    uint32_t transferred{};
    const NtStatus status = backend_.write(backend_.context, lba, blockCount,
                                            buffer, &transferred);
    imageLeaseState_.fetch_and(~WriterBit, std::memory_order_release);
    if (transferred > blockCount) return NtStatus::Unsuccessful;
    *blocksWritten = transferred;
    if (status != NtStatus::Success) return status;
    return transferred == blockCount ? NtStatus::Success : NtStatus::DeviceDataError;
}

NtStatus IoInitializeBlockDevices() noexcept {
    lockType();
    if (!g_blockDeviceTypeInitialized.load(std::memory_order_relaxed)) {
        g_blockDeviceType.typeId = ob::ObjectTypeId::BlockDevice;
        g_blockDeviceType.validAccessMask = 0x001F01FF;
        g_blockDeviceType.genericMapping = {0x001200A9, 0x00120116,
                                            0x001200A0, 0x001F01FF};
        g_blockDeviceType.deleteProcedure = deleteBlockDevice;
        g_blockDeviceTypeInitialized.store(true, std::memory_order_release);
    }
    unlockType();
    return NtStatus::Success;
}

NtStatus IoCreateBlockDevice(Handle* deviceHandle, uint32_t desiredAccess,
                             const BlockDeviceBackend& backend) noexcept {
    if (deviceHandle) *deviceHandle = 0;
    if (!deviceHandle || !validBackend(backend)) {
        if (backend.release) backend.release(backend.context);
        return NtStatus::InvalidParameter;
    }
    if (!g_blockDeviceTypeInitialized.load(std::memory_order_acquire)) {
        backend.release(backend.context);
        return NtStatus::InvalidDeviceRequest;
    }

    constexpr uint32_t readOnlyAccess = 0x001200A9; // FILE_GENERIC_READ / execute
    constexpr uint32_t writeDataAccess = 0x00000002;
    const uint32_t mappedAccess = ob::MapGenericAccess(desiredAccess,
                                                       g_blockDeviceType.genericMapping);
    if ((mappedAccess & ~g_blockDeviceType.validAccessMask) != 0 ||
        (mappedAccess & ~(readOnlyAccess | writeDataAccess)) != 0 ||
        ((mappedAccess & writeDataAccess) && !backend.write)) {
        backend.release(backend.context);
        return ((mappedAccess & writeDataAccess) && !backend.write)
            ? NtStatus::MediaWriteProtected : NtStatus::AccessDenied;
    }

    void* body{};
    ob::ObjectHeader* header{};
    NtStatus status = ob::ObCreateObject(ob::KernelMode, &g_blockDeviceType, nullptr,
                                          sizeof(BlockDeviceObject), &body, &header);
    if (status != NtStatus::Success) {
        backend.release(backend.context);
        return status;
    }

    new (body) BlockDeviceObject(backend);
    status = ob::detail::currentHandleTable().createHandle(header, *deviceHandle,
                                                            mappedAccess);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus IoReferenceBlockDeviceHandle(Handle deviceHandle, uint32_t desiredAccess,
                                      BlockDeviceObject*& device,
                                      ob::ObjectHeader*& reference) noexcept {
    device = nullptr;
    reference = nullptr;
    ob::HandleTable* table{};
    Handle raw{};
    NtStatus status = ob::detail::resolveHandle(deviceHandle, table, raw);
    if (status != NtStatus::Success) return status;
    status = ob::ObReferenceObjectByHandle(raw, *table, desiredAccess,
                                            &g_blockDeviceType, &reference);
    if (status == NtStatus::Success) device = reference->as<BlockDeviceObject>();
    return status;
}

NtStatus IoReadBlockDeviceHandle(Handle deviceHandle, uint64_t lba,
                                 uint32_t blockCount, void* buffer,
                                 uint32_t* blocksRead) noexcept {
    if (!blocksRead) return NtStatus::InvalidParameter;
    *blocksRead = 0;
    BlockDeviceObject* device{};
    ob::ObjectHeader* reference{};
    const NtStatus status = IoReferenceBlockDeviceHandle(deviceHandle, 0x0001,
                                                          device, reference);
    if (status != NtStatus::Success) return status;
    const NtStatus readStatus = device->read(lba, blockCount, buffer, blocksRead);
    ob::ObDereferenceObject(reference);
    return readStatus;
}

NtStatus IoWriteBlockDeviceHandle(Handle deviceHandle, uint64_t lba,
                                  uint32_t blockCount, const void* buffer,
                                  uint32_t* blocksWritten) noexcept {
    if (!blocksWritten) return NtStatus::InvalidParameter;
    *blocksWritten = 0;
    BlockDeviceObject* device{};
    ob::ObjectHeader* reference{};
    const NtStatus status = IoReferenceBlockDeviceHandle(deviceHandle, 0x0002,
                                                          device, reference);
    if (status != NtStatus::Success) return status;
    const NtStatus writeStatus = device->write(lba, blockCount, buffer, blocksWritten);
    ob::ObDereferenceObject(reference);
    return writeStatus;
}

NtStatus IoFlushBlockDeviceHandle(Handle deviceHandle) noexcept {
    BlockDeviceObject* device{};
    ob::ObjectHeader* reference{};
    const NtStatus status = IoReferenceBlockDeviceHandle(deviceHandle, 0x0002,
                                                          device, reference);
    if (status != NtStatus::Success) return status;
    const NtStatus flushStatus = device->flush();
    ob::ObDereferenceObject(reference);
    return flushStatus;
}

} // namespace micant::io
