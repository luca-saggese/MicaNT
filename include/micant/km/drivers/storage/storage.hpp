#pragma once

/**
 * @file storage.hpp
 * @brief Clean-Room Block Device & Partition Storage Subsystem (storage / disk / partmgr).
 *
 * Implements the standard Windows NT storage stack abstractions:
 * - IBlockDevice (Abstract block I/O contract)
 * - RamDiskDevice (In-memory block device)
 * - PartitionDevice (Slice-based sub-device wrapper)
 * - MBR (Master Boot Record) & GPT (GUID Partition Table) parsers
 *
 * References: Microsoft Learn Storage Driver Architecture & UEFI Specification.
 */

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <cstring>
#include <algorithm>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/driver.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/drivers/storage/partition_table.hpp"

namespace micant::storage {

// Standard Sector Sizing
inline constexpr uint32_t SECTOR_SIZE_512  = 512;
inline constexpr uint32_t SECTOR_SIZE_2048 = 2048; // CD-ROM / DVD / Blu-ray / ISO 9660 / UDF
inline constexpr uint32_t SECTOR_SIZE_4K   = 4096;

// MBR Partition Types
inline constexpr uint8_t MBR_TYPE_EMPTY       = 0x00;
inline constexpr uint8_t MBR_TYPE_FAT12       = 0x01;
inline constexpr uint8_t MBR_TYPE_FAT16_SM    = 0x04;
inline constexpr uint8_t MBR_TYPE_EXTENDED    = 0x05;
inline constexpr uint8_t MBR_TYPE_FAT16       = 0x06;
inline constexpr uint8_t MBR_TYPE_NTFS_EXFAT  = 0x07;
inline constexpr uint8_t MBR_TYPE_FAT32_CHS   = 0x0B;
inline constexpr uint8_t MBR_TYPE_FAT32_LBA   = 0x0C;
inline constexpr uint8_t MBR_TYPE_FAT16_LBA   = 0x0E;
inline constexpr uint8_t MBR_TYPE_GPT_PROTECT = 0xEE;

inline constexpr uint16_t MBR_SIGNATURE       = 0xAA55;
inline constexpr uint64_t GPT_SIGNATURE       = 0x5452415020494645ULL; // "EFI PART"

// Canonical NT Disk & Storage IOCTL Base Codes
inline constexpr uint32_t IOCTL_DISK_BASE    = 0x00000007; // FILE_DEVICE_DISK
inline constexpr uint32_t IOCTL_STORAGE_BASE = 0x0000002D; // FILE_DEVICE_MASS_STORAGE

// Standard NT IOCTL Functions (Matching Windows NT DDK/WDK winioctl.h / ntdddisk.h)
inline constexpr uint32_t IOCTL_DISK_GET_DRIVE_GEOMETRY =
    driver::CTL_CODE(IOCTL_DISK_BASE, 0x0000, driver::METHOD_BUFFERED, driver::FILE_ANY_ACCESS); // 0x00070000

inline constexpr uint32_t IOCTL_DISK_GET_PARTITION_INFO =
    driver::CTL_CODE(IOCTL_DISK_BASE, 0x0001, driver::METHOD_BUFFERED, driver::FILE_READ_ACCESS); // 0x00074004

inline constexpr uint32_t IOCTL_DISK_IS_WRITABLE =
    driver::CTL_CODE(IOCTL_DISK_BASE, 0x0009, driver::METHOD_BUFFERED, driver::FILE_ANY_ACCESS); // 0x00070024

inline constexpr uint32_t IOCTL_DISK_GET_LENGTH_INFO =
    driver::CTL_CODE(IOCTL_DISK_BASE, 0x0017, driver::METHOD_BUFFERED, driver::FILE_READ_ACCESS); // 0x0007405C

inline constexpr uint32_t IOCTL_DISK_GET_DRIVE_GEOMETRY_EX =
    driver::CTL_CODE(IOCTL_DISK_BASE, 0x0028, driver::METHOD_BUFFERED, driver::FILE_ANY_ACCESS); // 0x000700A0

inline constexpr uint32_t IOCTL_STORAGE_GET_DEVICE_NUMBER =
    driver::CTL_CODE(IOCTL_STORAGE_BASE, 0x0420, driver::METHOD_BUFFERED, driver::FILE_ANY_ACCESS); // 0x002D1080

inline constexpr uint32_t IOCTL_DISK_GET_PARTITION_INFO_EX =
    driver::CTL_CODE(IOCTL_DISK_BASE, 0x0012, driver::METHOD_BUFFERED, driver::FILE_ANY_ACCESS); // 0x00070048

enum class MEDIA_TYPE : uint32_t {
    Unknown = 0,
    F5_1Pt2_512,
    F3_1Pt44_512,
    F3_2Pt88_512,
    F3_20Pt8_512,
    F3_720_512,
    F5_360_512,
    F5_320_512,
    F5_320_1024,
    F5_180_512,
    F5_160_512,
    RemovableMedia,
    FixedMedia
};

struct DISK_GEOMETRY {
    LargeInteger Cylinders{};
    MEDIA_TYPE MediaType{MEDIA_TYPE::FixedMedia};
    uint32_t TracksPerCylinder{255};
    uint32_t SectorsPerTrack{63};
    uint32_t BytesPerSector{512};
};

struct GET_LENGTH_INFORMATION {
    LargeInteger Length{};
};

struct DISK_GEOMETRY_EX {
    DISK_GEOMETRY Geometry{};
    LargeInteger DiskSize{};
    uint8_t Data[1]{0};
};

struct STORAGE_DEVICE_NUMBER {
    uint32_t DeviceType{IOCTL_DISK_BASE};
    uint32_t DeviceNumber{0};
    uint32_t PartitionNumber{0};
};

enum class PARTITION_STYLE : uint32_t {
    Mbr = 0,
    Gpt = 1,
    Raw = 2
};

struct PARTITION_INFORMATION {
    LargeInteger StartingOffset{};
    LargeInteger PartitionLength{};
    uint32_t HiddenSectors{0};
    uint32_t PartitionNumber{0};
    uint8_t PartitionType{0};
    bool BootIndicator{false};
    bool RecognizedPartition{false};
    bool RewritePartition{false};
};

struct PARTITION_INFORMATION_MBR {
    uint8_t PartitionType{0};
    bool BootIndicator{false};
    bool RecognizedPartition{false};
    uint32_t HiddenSectors{0};
    GUID PartitionId{};
};

struct PARTITION_INFORMATION_GPT {
    GUID PartitionType{};
    GUID PartitionId{};
    uint64_t Attributes{0};
    // Windows WCHAR is 16 bits on every host architecture.
    char16_t Name[36]{0};
};

struct PARTITION_INFORMATION_EX {
    PARTITION_STYLE PartitionStyle{PARTITION_STYLE::Mbr};
    uint16_t PartitionOrdinal{0};
    LargeInteger StartingOffset{};
    LargeInteger PartitionLength{};
    uint32_t PartitionNumber{0};
    bool RewritePartition{false};
    bool IsServicePartition{false};
    union {
        PARTITION_INFORMATION_MBR Mbr;
        PARTITION_INFORMATION_GPT Gpt;
    };
};

/**
 * @brief Abstract Block Device Interface (IBlockDevice).
 */
class IBlockDevice {
public:
    virtual ~IBlockDevice() = default;

    [[nodiscard]] virtual NtStatus readBlocks(uint64_t lba, uint32_t count, void* buffer) = 0;
    [[nodiscard]] virtual NtStatus writeBlocks(uint64_t lba, uint32_t count, const void* buffer) = 0;
    // Providers without a real media-cache flush must preserve this failure.
    [[nodiscard]] virtual NtStatus flushBlocks() { return NtStatus::NotSupported; }
    [[nodiscard]] virtual uint32_t getBlockSize() const noexcept = 0;
    [[nodiscard]] virtual uint64_t getTotalBlocks() const noexcept = 0;
    [[nodiscard]] virtual uint64_t getTotalBytes() const noexcept {
        return getTotalBlocks() * getBlockSize();
    }
    [[nodiscard]] virtual const std::wstring& getDeviceName() const noexcept = 0;

    [[nodiscard]] virtual DISK_GEOMETRY getGeometry() const noexcept {
        uint32_t bytesPerSector = getBlockSize();
        if (bytesPerSector == 0) bytesPerSector = SECTOR_SIZE_512;
        uint64_t totalBlocks = getTotalBlocks();
        uint32_t sectorsPerTrack = 63;
        uint32_t tracksPerCylinder = 255;
        uint64_t totalCylinders = (totalBlocks > 0) ? (totalBlocks / (sectorsPerTrack * tracksPerCylinder)) : 0;
        if (totalCylinders == 0 && totalBlocks > 0) totalCylinders = 1;

        DISK_GEOMETRY geom{};
        geom.Cylinders.quadPart = static_cast<int64_t>(totalCylinders);
        geom.MediaType = MEDIA_TYPE::FixedMedia;
        geom.TracksPerCylinder = tracksPerCylinder;
        geom.SectorsPerTrack = sectorsPerTrack;
        geom.BytesPerSector = bytesPerSector;
        return geom;
    }

    [[nodiscard]] virtual uint64_t getStartLba() const noexcept { return 0; }
    [[nodiscard]] virtual uint32_t getPartitionNumber() const noexcept { return 0; }
    [[nodiscard]] virtual uint8_t getPartitionType() const noexcept { return 0; }
};

/**
 * @brief RamDisk Block Device.
 * High-speed in-memory block storage simulating physical disk media.
 */
class RamDiskDevice : public IBlockDevice {
public:
    RamDiskDevice(std::wstring_view name, uint64_t totalBytes, uint32_t blockSize = SECTOR_SIZE_512)
        : name_(name), blockSize_(blockSize) {
        if (blockSize_ == 0) blockSize_ = SECTOR_SIZE_512;
        totalBlocks_ = (totalBytes + blockSize_ - 1) / blockSize_;
        storage_.resize(static_cast<size_t>(totalBlocks_ * blockSize_), 0);
    }

    [[nodiscard]] NtStatus readBlocks(uint64_t lba, uint32_t count, void* buffer) override {
        if (!buffer || count == 0) return NtStatus::InvalidParameter;
        if (lba + count > totalBlocks_) return NtStatus::EndOfFile;

        std::lock_guard<std::mutex> lock(mutex_);
        size_t byteOffset = static_cast<size_t>(lba * blockSize_);
        size_t byteCount = static_cast<size_t>(count * blockSize_);
        std::memcpy(buffer, storage_.data() + byteOffset, byteCount);
        return NtStatus::Success;
    }

    [[nodiscard]] NtStatus writeBlocks(uint64_t lba, uint32_t count, const void* buffer) override {
        if (!buffer || count == 0) return NtStatus::InvalidParameter;
        if (lba + count > totalBlocks_) return NtStatus::DiskFull;

        std::lock_guard<std::mutex> lock(mutex_);
        size_t byteOffset = static_cast<size_t>(lba * blockSize_);
        size_t byteCount = static_cast<size_t>(count * blockSize_);
        std::memcpy(storage_.data() + byteOffset, buffer, byteCount);
        return NtStatus::Success;
    }

    [[nodiscard]] uint32_t getBlockSize() const noexcept override { return blockSize_; }
    [[nodiscard]] uint64_t getTotalBlocks() const noexcept override { return totalBlocks_; }
    [[nodiscard]] const std::wstring& getDeviceName() const noexcept override { return name_; }

    [[nodiscard]] uint8_t* rawData() noexcept { return storage_.data(); }
    [[nodiscard]] const uint8_t* rawData() const noexcept { return storage_.data(); }

private:
    std::wstring name_;
    uint32_t blockSize_{SECTOR_SIZE_512};
    uint64_t totalBlocks_{0};
    std::vector<uint8_t> storage_;
    mutable std::mutex mutex_;
};

/**
 * @brief Partition Device.
 * Wraps an offset slice of a parent block device representing a disk partition.
 */
class PartitionDevice : public IBlockDevice {
public:
    PartitionDevice(
        std::wstring_view name,
        std::shared_ptr<IBlockDevice> parentDevice,
        uint64_t startLba,
        uint64_t blockCount
    ) : name_(name),
        parentDevice_(std::move(parentDevice)),
        startLba_(startLba),
        blockCount_(blockCount) {}

    [[nodiscard]] NtStatus readBlocks(uint64_t lba, uint32_t count, void* buffer) override {
        if (!parentDevice_) return NtStatus::DeviceNotReady;
        if (lba + count > blockCount_) return NtStatus::EndOfFile;
        return parentDevice_->readBlocks(startLba_ + lba, count, buffer);
    }

    [[nodiscard]] NtStatus writeBlocks(uint64_t lba, uint32_t count, const void* buffer) override {
        if (!parentDevice_) return NtStatus::DeviceNotReady;
        if (lba + count > blockCount_) return NtStatus::DiskFull;
        return parentDevice_->writeBlocks(startLba_ + lba, count, buffer);
    }

    [[nodiscard]] NtStatus flushBlocks() override {
        return parentDevice_ ? parentDevice_->flushBlocks() : NtStatus::DeviceNotReady;
    }

    [[nodiscard]] uint32_t getBlockSize() const noexcept override {
        return parentDevice_ ? parentDevice_->getBlockSize() : SECTOR_SIZE_512;
    }

    [[nodiscard]] uint64_t getTotalBlocks() const noexcept override { return blockCount_; }
    [[nodiscard]] uint64_t getStartLba() const noexcept override { return startLba_; }
    [[nodiscard]] uint32_t getPartitionNumber() const noexcept override { return 1; }
    [[nodiscard]] uint8_t getPartitionType() const noexcept override { return MBR_TYPE_FAT32_LBA; }
    [[nodiscard]] const std::wstring& getDeviceName() const noexcept override { return name_; }

private:
    std::wstring name_;
    std::shared_ptr<IBlockDevice> parentDevice_;
    uint64_t startLba_{0};
    uint64_t blockCount_{0};
};

#pragma pack(push, 1)

/**
 * @brief MBR 16-byte Partition Table Entry.
 */
struct MbrPartitionEntry {
    uint8_t  bootIndicator;    // 0x80 = Active / Bootable
    uint8_t  startHead;
    uint8_t  startSector : 6;
    uint8_t  startCylinderHigh : 2;
    uint8_t  startCylinderLow;
    uint8_t  partitionType;    // 0x0B/0x0C = FAT32, 0x07 = NTFS/exFAT
    uint8_t  endHead;
    uint8_t  endSector : 6;
    uint8_t  endCylinderHigh : 2;
    uint8_t  endCylinderLow;
    uint32_t startLba;
    uint32_t sectorCount;
};

/**
 * @brief Master Boot Record (Sector 0).
 */
struct MasterBootRecord {
    uint8_t bootstrapCode[446];
    MbrPartitionEntry partitions[4];
    uint16_t signature; // 0xAA55
};

/**
 * @brief GPT Header (LBA 1).
 */
struct GptHeader {
    uint64_t signature; // "EFI PART" (0x5452415020494645ULL)
    uint32_t revision;
    uint32_t headerSize;
    uint32_t headerCrc32;
    uint32_t reserved;
    uint64_t currentLba;
    uint64_t backupLba;
    uint64_t firstUsableLba;
    uint64_t lastUsableLba;
    uint8_t  diskGuid[16];
    uint64_t partitionEntryLba;
    uint32_t numPartitionEntries;
    uint32_t sizeOfPartitionEntry;
    uint32_t partitionEntryArrayCrc32;
};

/**
 * @brief GPT 128-byte Partition Entry.
 */
struct GptPartitionEntry {
    uint8_t  partitionTypeGuid[16];
    uint8_t  uniquePartitionGuid[16];
    uint64_t startingLba;
    uint64_t endingLba;
    uint64_t attributes;
    wchar_t  partitionName[36];
};

#pragma pack(pop)

/**
 * @brief Partition Manager Utilities.
 */
class PartitionManager {
    static bool readForPartitionTable(void* context, uint64_t lba, uint16_t count, void* buffer) noexcept {
        auto* device = static_cast<IBlockDevice*>(context);
        return device && NT_SUCCESS(device->readBlocks(lba, count, buffer));
    }

    static NtStatus mapPartitionStatus(partition_table::Status status) noexcept {
        switch (status) {
        case partition_table::Status::Success: return NtStatus::Success;
        case partition_table::Status::InvalidArgument: return NtStatus::InvalidParameter;
        case partition_table::Status::IoError: return NtStatus::DeviceDataError;
        case partition_table::Status::Corrupt: return NtStatus::UnrecognizedVolume;
        case partition_table::Status::TooManyPartitions: return NtStatus::BufferTooSmall;
        }
        return NtStatus::UnrecognizedVolume;
    }

public:
    static NtStatus parseMbr(IBlockDevice& device, std::vector<MbrPartitionEntry>& outPartitions) {
        outPartitions.clear();
        std::vector<partition_table::Extent> entries(128);
        partition_table::Result result{};
        std::vector<uint8_t> scratch(8192);
        const partition_table::Device source{&device, &readForPartitionTable,
            device.getTotalBlocks(), device.getBlockSize(), scratch.data(), scratch.size()};
        const NtStatus status = mapPartitionStatus(partition_table::parseMbr(
            source, entries.data(), entries.size(), result));
        if (!NT_SUCCESS(status)) return status;
        for (size_t i = 0; i < result.count; ++i) {
            MbrPartitionEntry entry{};
            entry.bootIndicator = entries[i].bootable ? 0x80 : 0;
            entry.partitionType = entries[i].mbrType;
            entry.startLba = static_cast<uint32_t>(entries[i].firstLba);
            entry.sectorCount = static_cast<uint32_t>(entries[i].sectorCount);
            outPartitions.push_back(entry);
        }
        return NtStatus::Success;
    }

    static NtStatus writeMbr(IBlockDevice& device, const std::vector<MbrPartitionEntry>& partitions) {
        if (partitions.size() > 4) return NtStatus::InvalidParameter;
        if (device.getBlockSize() < sizeof(MasterBootRecord)) return NtStatus::InvalidParameter;

        std::vector<uint8_t> sector(device.getBlockSize(), 0);
        MasterBootRecord mbr{};
        mbr.signature = MBR_SIGNATURE;

        for (size_t i = 0; i < partitions.size(); ++i) {
            mbr.partitions[i] = partitions[i];
        }
        std::memcpy(sector.data(), &mbr, sizeof(mbr));

        return device.writeBlocks(0, 1, sector.data());
    }

    static NtStatus parseGpt(IBlockDevice& device, std::vector<GptPartitionEntry>& outEntries) {
        outEntries.clear();
        std::vector<partition_table::Extent> entries(256);
        partition_table::Result result{};
        std::vector<uint8_t> scratch(8192);
        const partition_table::Device source{&device, &readForPartitionTable,
            device.getTotalBlocks(), device.getBlockSize(), scratch.data(), scratch.size()};
        auto status = partition_table::validateGptAt(source, 1, entries.data(), entries.size(), result);
        if (status != partition_table::Status::Success && device.getTotalBlocks() > 1)
            status = partition_table::validateGptAt(source, device.getTotalBlocks() - 1,
                                                     entries.data(), entries.size(), result);
        const NtStatus mapped = mapPartitionStatus(status);
        if (!NT_SUCCESS(mapped)) return mapped;
        for (size_t i = 0; i < result.count; ++i) {
            GptPartitionEntry entry{};
            std::memcpy(entry.partitionTypeGuid, entries[i].typeGuid, 16);
            std::memcpy(entry.uniquePartitionGuid, entries[i].uniqueGuid, 16);
            entry.startingLba = entries[i].firstLba;
            entry.endingLba = entries[i].firstLba + entries[i].sectorCount - 1;
            entry.attributes = entries[i].attributes;
            for (size_t unit = 0; unit < 36; ++unit)
                entry.partitionName[unit] = static_cast<wchar_t>(entries[i].name[unit]);
            outEntries.push_back(entry);
        }
        return NtStatus::Success;
    }
};

/**
 * @brief Dispatch routine for \Driver\Disk.
 * Implements canonical Windows NT disk.sys IRP handling.
 */
inline NtStatus DiskDriverDispatch(io::DeviceObject* dev, io::Irp* irp) {
    if (!irp) return NtStatus::InvalidParameter;
    auto complete = [irp](NtStatus status, uintptr_t information = 0) {
        irp->ioStatus.status = status;
        irp->ioStatus.information = information;
        return status;
    };
    if (!dev) return complete(NtStatus::InvalidParameter);
    auto* blockDevice = static_cast<IBlockDevice*>(dev->deviceExtension);
    if (!blockDevice) return complete(NtStatus::DeviceNotReady);

    switch (irp->majorFunction) {
        case io::IRP_MJ_CREATE:
            return complete(NtStatus::Success, 1); // FILE_OPENED
        case io::IRP_MJ_CLOSE:
            return complete(NtStatus::Success);

        case io::IRP_MJ_READ: {
            if (irp->length == 0) return complete(NtStatus::Success);
            if (!irp->userBuffer || irp->byteOffset.quadPart < 0) return complete(NtStatus::InvalidParameter);
            uint32_t blockSize = blockDevice->getBlockSize();
            if (blockSize == 0 || static_cast<uint64_t>(irp->byteOffset.quadPart) % blockSize != 0 || irp->length % blockSize != 0)
                return complete(NtStatus::InvalidParameter);
            uint64_t lba = static_cast<uint64_t>(irp->byteOffset.quadPart) / blockSize;
            uint32_t blockCount = irp->length / blockSize;

            NtStatus st = blockDevice->readBlocks(lba, blockCount, irp->userBuffer);
            return complete(st, NT_SUCCESS(st) ? irp->length : 0);
        }

        case io::IRP_MJ_WRITE: {
            if (irp->length == 0) return complete(NtStatus::Success);
            if (!irp->userBuffer || irp->byteOffset.quadPart < 0) return complete(NtStatus::InvalidParameter);
            uint32_t blockSize = blockDevice->getBlockSize();
            if (blockSize == 0 || static_cast<uint64_t>(irp->byteOffset.quadPart) % blockSize != 0 || irp->length % blockSize != 0)
                return complete(NtStatus::InvalidParameter);
            uint64_t lba = static_cast<uint64_t>(irp->byteOffset.quadPart) / blockSize;
            uint32_t blockCount = irp->length / blockSize;

            NtStatus st = blockDevice->writeBlocks(lba, blockCount, irp->userBuffer);
            return complete(st, NT_SUCCESS(st) ? irp->length : 0);
        }

        case io::IRP_MJ_FLUSH_BUFFERS:
            return complete(blockDevice->flushBlocks());

        case io::IRP_MJ_DEVICE_CONTROL: {
            uint32_t ioctl = irp->byteOffset.lowPart;
            switch (ioctl) {
                case IOCTL_DISK_GET_DRIVE_GEOMETRY: {
                    if (!irp->userBuffer) return complete(NtStatus::InvalidParameter);
                    if (irp->length < sizeof(DISK_GEOMETRY)) return complete(NtStatus::BufferTooSmall);
                    auto geom = blockDevice->getGeometry();
                    std::memcpy(irp->userBuffer, &geom, sizeof(DISK_GEOMETRY));
                    return complete(NtStatus::Success, sizeof(DISK_GEOMETRY));
                }

                case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX: {
                    constexpr size_t minimumLength = sizeof(DISK_GEOMETRY) + sizeof(LargeInteger);
                    if (!irp->userBuffer) return complete(NtStatus::InvalidParameter);
                    if (irp->length < minimumLength) return complete(NtStatus::BufferTooSmall);
                    DISK_GEOMETRY_EX geomEx{};
                    geomEx.Geometry = blockDevice->getGeometry();
                    geomEx.DiskSize.quadPart = static_cast<int64_t>(blockDevice->getTotalBytes());
                    std::memcpy(irp->userBuffer, &geomEx, minimumLength);
                    return complete(NtStatus::Success, minimumLength);
                }

                case IOCTL_DISK_GET_LENGTH_INFO: {
                    if (!irp->userBuffer) return complete(NtStatus::InvalidParameter);
                    if (irp->length < sizeof(GET_LENGTH_INFORMATION)) return complete(NtStatus::BufferTooSmall);
                    GET_LENGTH_INFORMATION lenInfo{};
                    lenInfo.Length.quadPart = static_cast<int64_t>(blockDevice->getTotalBytes());
                    std::memcpy(irp->userBuffer, &lenInfo, sizeof(GET_LENGTH_INFORMATION));
                    return complete(NtStatus::Success, sizeof(GET_LENGTH_INFORMATION));
                }

                case IOCTL_DISK_IS_WRITABLE: {
                    return complete(NtStatus::Success);
                }

                case IOCTL_DISK_GET_PARTITION_INFO: {
                    if (!irp->userBuffer) return complete(NtStatus::InvalidParameter);
                    if (irp->length < sizeof(PARTITION_INFORMATION)) return complete(NtStatus::BufferTooSmall);
                    PARTITION_INFORMATION partInfo{};
                    uint64_t startLba = blockDevice->getStartLba();
                    uint32_t blockSize = blockDevice->getBlockSize();
                    partInfo.StartingOffset.quadPart = static_cast<int64_t>(startLba * blockSize);
                    partInfo.PartitionLength.quadPart = static_cast<int64_t>(blockDevice->getTotalBytes());
                    partInfo.HiddenSectors = static_cast<uint32_t>(startLba);
                    partInfo.PartitionNumber = blockDevice->getPartitionNumber();
                    partInfo.PartitionType = blockDevice->getPartitionType();
                    partInfo.BootIndicator = (partInfo.PartitionNumber == 1);
                    partInfo.RecognizedPartition = true;
                    std::memcpy(irp->userBuffer, &partInfo, sizeof(PARTITION_INFORMATION));
                    return complete(NtStatus::Success, sizeof(PARTITION_INFORMATION));
                }

                case IOCTL_DISK_GET_PARTITION_INFO_EX: {
                    if (!irp->userBuffer) return complete(NtStatus::InvalidParameter);
                    if (irp->length < sizeof(PARTITION_INFORMATION_EX)) return complete(NtStatus::BufferTooSmall);
                    PARTITION_INFORMATION_EX partInfoEx{};
                    partInfoEx.PartitionStyle = PARTITION_STYLE::Mbr;
                    uint64_t startLba = blockDevice->getStartLba();
                    uint32_t blockSize = blockDevice->getBlockSize();
                    partInfoEx.StartingOffset.quadPart = static_cast<int64_t>(startLba * blockSize);
                    partInfoEx.PartitionLength.quadPart = static_cast<int64_t>(blockDevice->getTotalBytes());
                    partInfoEx.PartitionNumber = blockDevice->getPartitionNumber();
                    partInfoEx.Mbr.PartitionType = blockDevice->getPartitionType();
                    partInfoEx.Mbr.BootIndicator = (partInfoEx.PartitionNumber == 1);
                    partInfoEx.Mbr.RecognizedPartition = true;
                    partInfoEx.Mbr.HiddenSectors = static_cast<uint32_t>(startLba);
                    std::memcpy(irp->userBuffer, &partInfoEx, sizeof(PARTITION_INFORMATION_EX));
                    return complete(NtStatus::Success, sizeof(PARTITION_INFORMATION_EX));
                }

                case IOCTL_STORAGE_GET_DEVICE_NUMBER: {
                    if (!irp->userBuffer) return complete(NtStatus::InvalidParameter);
                    if (irp->length < sizeof(STORAGE_DEVICE_NUMBER)) return complete(NtStatus::BufferTooSmall);
                    STORAGE_DEVICE_NUMBER devNum{
                        .DeviceType = IOCTL_DISK_BASE,
                        .DeviceNumber = 0,
                        .PartitionNumber = blockDevice->getPartitionNumber()
                    };
                    std::memcpy(irp->userBuffer, &devNum, sizeof(STORAGE_DEVICE_NUMBER));
                    return complete(NtStatus::Success, sizeof(STORAGE_DEVICE_NUMBER));
                }

                default:
                    return complete(NtStatus::InvalidDeviceRequest);
            }
        }

        default:
            return complete(NtStatus::InvalidDeviceRequest);
    }
}

/**
 * @brief Initializes a DriverObject with the standard \Driver\Disk dispatch routines.
 */
inline void InitializeDiskDriver(io::DriverObject& driver) {
    driver.driverName = L"\\Driver\\Disk";
    driver.setDispatch(io::IRP_MJ_CREATE, DiskDriverDispatch);
    driver.setDispatch(io::IRP_MJ_CLOSE, DiskDriverDispatch);
    driver.setDispatch(io::IRP_MJ_READ, DiskDriverDispatch);
    driver.setDispatch(io::IRP_MJ_WRITE, DiskDriverDispatch);
    driver.setDispatch(io::IRP_MJ_DEVICE_CONTROL, DiskDriverDispatch);
}

} // namespace micant::storage
