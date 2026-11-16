#pragma once

/**
 * @file fat32.hpp
 * @brief Clean-Room FAT32 Filesystem Driver (fastfat.sys / fat32).
 *
 * Implements standard Windows NT FastFAT filesystem operations:
 * - BPB (BIOS Parameter Block) formatting & parsing
 * - FAT32 table traversal, cluster allocation, and chain linking
 * - 32-byte directory entries and Long File Name (LFN) unicode reconstruction
 * - Cluster-to-sector mapping and block I/O dispatching
 * - File creation, directory creation, read, write, resize, and traversal
 *
 * References: Microsoft Extensible Firmware Initiative FAT32 File System Specification.
 */

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <span>
#include <cstring>
#include <algorithm>
#include <cwctype>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/drivers/storage/storage.hpp"

namespace micant::fat32 {

// End-Of-Cluster (EOC) markers
inline constexpr uint32_t FAT32_CLUSTER_FREE   = 0x00000000;
inline constexpr uint32_t FAT32_CLUSTER_BAD    = 0x0FFFFFF7;
inline constexpr uint32_t FAT32_CLUSTER_EOC_MIN= 0x0FFFFFF8;
inline constexpr uint32_t FAT32_CLUSTER_EOC    = 0x0FFFFFFF;
inline constexpr uint32_t FAT32_CLUSTER_MASK   = 0x0FFFFFFF;

// Attributes
inline constexpr uint8_t ATTR_READ_ONLY = 0x01;
inline constexpr uint8_t ATTR_HIDDEN    = 0x02;
inline constexpr uint8_t ATTR_SYSTEM    = 0x04;
inline constexpr uint8_t ATTR_VOLUME_ID = 0x08;
inline constexpr uint8_t ATTR_DIRECTORY = 0x10;
inline constexpr uint8_t ATTR_ARCHIVE   = 0x20;
inline constexpr uint8_t ATTR_LONG_NAME = 0x0F;

#pragma pack(push, 1)

/**
 * @brief FAT32 Boot Sector & BIOS Parameter Block (BPB).
 */
struct BootSector {
    uint8_t  jmpBoot[3];           // 0xEB 0x58 0x90
    char     oemName[8];           // "MicaNT  "
    uint16_t bytesPerSector;       // 512
    uint8_t  sectorsPerCluster;     // 8 (4KB) or 1 (512B)
    uint16_t reservedSectorCount;   // 32
    uint8_t  numFats;               // 2
    uint16_t rootEntryCount;        // 0
    uint16_t totalSectors16;        // 0
    uint8_t  mediaType;             // 0xF8
    uint16_t fatSize16;             // 0
    uint16_t sectorsPerTrack;       // 63
    uint16_t numHeads;              // 255
    uint32_t hiddenSectors;         // 0
    uint32_t totalSectors32;        // Total sector count
    // Extended FAT32 Fields
    uint32_t fatSize32;             // Sectors per FAT
    uint16_t extFlags;              // 0
    uint16_t fsVersion;             // 0
    uint32_t rootCluster;           // 2
    uint16_t fsInfoSector;          // 1
    uint16_t backupBootSector;      // 6
    uint8_t  reserved[12];
    uint8_t  driveNumber;           // 0x80
    uint8_t  reserved1;
    uint8_t  bootSignature;         // 0x29
    uint32_t volumeId;              // e.g. 0x20261001
    char     volumeLabel[11];       // "MicaNT DISK"
    char     fsType[8];             // "FAT32   "
    uint8_t  bootCode[420];
    uint16_t signature;             // 0xAA55
};

/**
 * @brief FAT32 FSInfo Structure (Sector 1).
 */
struct FsInfoSector {
    uint32_t leadSig;              // 0x41615252 ("RRaA")
    uint8_t  reserved1[480];
    uint32_t structSig;            // 0x61417272 ("rrAa")
    uint32_t freeCount;            // Free cluster count
    uint32_t nextFree;             // Next free cluster hint
    uint8_t  reserved2[12];
    uint32_t trailSig;             // 0xAA550000
};

/**
 * @brief 32-byte Standard Directory Entry.
 */
struct DirectoryEntry {
    char     name[11];             // 8.3 space-padded
    uint8_t  attr;                 // ATTR_*
    uint8_t  ntReserved;
    uint8_t  creationTimeTenth;
    uint16_t creationTime;
    uint16_t creationDate;
    uint16_t lastAccessDate;
    uint16_t firstClusterHigh;
    uint16_t writeTime;
    uint16_t writeDate;
    uint16_t firstClusterLow;
    uint32_t fileSize;

    [[nodiscard]] uint32_t getFirstCluster() const noexcept {
        return (static_cast<uint32_t>(firstClusterHigh) << 16) | firstClusterLow;
    }

    void setFirstCluster(uint32_t cluster) noexcept {
        firstClusterHigh = static_cast<uint16_t>((cluster >> 16) & 0xFFFF);
        firstClusterLow  = static_cast<uint16_t>(cluster & 0xFFFF);
    }
};

/**
 * @brief 32-byte Long File Name (LFN) Entry.
 */
struct LfnEntry {
    uint8_t  order;                // Sequence number (or'd with 0x40 for last entry)
    char16_t name1[5];             // Chars 1-5
    uint8_t  attr;                 // Always 0x0F
    uint8_t  type;                 // 0
    uint8_t  checksum;             // Checksum of short 8.3 name
    char16_t name2[6];             // Chars 6-11
    uint16_t firstClusterLow;      // Must be 0
    char16_t name3[2];             // Chars 12-13
};

#pragma pack(pop)

struct FatFileInfo {
    std::wstring name;
    uint32_t attributes{0};
    uint32_t firstCluster{0};
    uint32_t fileSize{0};
    bool isDirectory{false};
};

/**
 * @brief Clean-room FAT32 Filesystem Driver.
 */
class Fat32FileSystem {
public:
    Fat32FileSystem() = default;

    /**
     * @brief Formats a block device as FAT32.
     */
    static NtStatus format(
        storage::IBlockDevice& device,
        std::string_view volumeLabel = "MicaNT",
        uint8_t sectorsPerCluster = 8 // 4 KB clusters for 512-byte sectors
    ) {
        if (sectorsPerCluster == 0) {
            sectorsPerCluster = 8;
        }

        uint32_t bytesPerSec = device.getBlockSize();
        uint32_t totalSectors = static_cast<uint32_t>(device.getTotalBlocks());
        uint16_t reservedSectors = 32;
        uint8_t numFats = 2;

        // Calculate FAT size
        // Total data sectors = totalSectors - reserved - (numFats * fatSize)
        // Clusters = dataSectors / sectorsPerCluster
        // FatSize = (Clusters * 4 + bytesPerSec - 1) / bytesPerSec
        uint32_t clusterEstimate = totalSectors / sectorsPerCluster;
        uint32_t fatSize = (clusterEstimate * 4 + bytesPerSec - 1) / bytesPerSec;
        if (fatSize == 0) fatSize = 32;

        // 1. Build Boot Sector
        std::vector<uint8_t> bootSecBytes(bytesPerSec, 0);
        auto* bs = reinterpret_cast<BootSector*>(bootSecBytes.data());
        bs->jmpBoot[0] = 0xEB; bs->jmpBoot[1] = 0x58; bs->jmpBoot[2] = 0x90;
        std::memcpy(bs->oemName, "MicaNT  ", 8);
        bs->bytesPerSector = static_cast<uint16_t>(bytesPerSec);
        bs->sectorsPerCluster = sectorsPerCluster;
        bs->reservedSectorCount = reservedSectors;
        bs->numFats = numFats;
        bs->mediaType = 0xF8;
        bs->sectorsPerTrack = 63;
        bs->numHeads = 255;
        bs->totalSectors32 = totalSectors;
        bs->fatSize32 = fatSize;
        bs->rootCluster = 2;
        bs->fsInfoSector = 1;
        bs->backupBootSector = 6;
        bs->driveNumber = 0x80;
        bs->bootSignature = 0x29;
        bs->volumeId = 0x20261001;
        std::memset(bs->volumeLabel, ' ', 11);
        std::memcpy(bs->volumeLabel, volumeLabel.data(), std::min<size_t>(11, volumeLabel.size()));
        std::memcpy(bs->fsType, "FAT32   ", 8);
        bs->signature = 0xAA55;

        // Write primary boot sector and backup boot sector
        NtStatus st = device.writeBlocks(0, 1, bootSecBytes.data());
        if (!NT_SUCCESS(st)) return st;
        st = device.writeBlocks(6, 1, bootSecBytes.data());
        if (!NT_SUCCESS(st)) return st;

        // 2. Build FSInfo Sector (Sector 1)
        std::vector<uint8_t> fsInfoBytes(bytesPerSec, 0);
        auto* fsi = reinterpret_cast<FsInfoSector*>(fsInfoBytes.data());
        fsi->leadSig = 0x41615252;
        fsi->structSig = 0x61417272;
        uint32_t totalDataSectors = totalSectors - reservedSectors - (numFats * fatSize);
        uint32_t totalClusters = totalDataSectors / sectorsPerCluster;
        fsi->freeCount = (totalClusters > 1) ? (totalClusters - 1) : 0; // Cluster 2 is root dir
        fsi->nextFree = 3;
        fsi->trailSig = 0xAA550000;

        st = device.writeBlocks(1, 1, fsInfoBytes.data());
        if (!NT_SUCCESS(st)) return st;

        // 3. Clear FATs & allocate clusters 0, 1, 2 (root dir)
        std::vector<uint8_t> fatSector(bytesPerSec, 0);
        auto* fatEntries = reinterpret_cast<uint32_t*>(fatSector.data());
        fatEntries[0] = 0x0FFFFF00 | 0xF8; // Media type
        fatEntries[1] = 0x0FFFFFFF;        // EOC marker
        fatEntries[2] = 0x0FFFFFFF;        // Root dir EOC

        // Write FAT1 sector 0
        uint32_t fat1Start = reservedSectors;
        st = device.writeBlocks(fat1Start, 1, fatSector.data());
        if (!NT_SUCCESS(st)) return st;

        // Write FAT2 sector 0
        uint32_t fat2Start = reservedSectors + fatSize;
        st = device.writeBlocks(fat2Start, 1, fatSector.data());
        if (!NT_SUCCESS(st)) return st;

        // Zero out rest of FAT1 and FAT2
        std::memset(fatSector.data(), 0, bytesPerSec);
        for (uint32_t i = 1; i < fatSize; ++i) {
            (void)device.writeBlocks(fat1Start + i, 1, fatSector.data());
            (void)device.writeBlocks(fat2Start + i, 1, fatSector.data());
        }

        // 4. Zero out Root Directory cluster (Cluster 2)
        uint32_t firstDataSec = reservedSectors + (numFats * fatSize);
        std::vector<uint8_t> clusterZero(sectorsPerCluster * bytesPerSec, 0);
        // Write Volume Label entry into root directory
        auto* rootDir = reinterpret_cast<DirectoryEntry*>(clusterZero.data());
        std::memset(rootDir->name, ' ', 11);
        std::memcpy(rootDir->name, volumeLabel.data(), std::min<size_t>(11, volumeLabel.size()));
        rootDir->attr = ATTR_VOLUME_ID;
        rootDir->setFirstCluster(0);
        rootDir->fileSize = 0;

        return device.writeBlocks(firstDataSec, sectorsPerCluster, clusterZero.data());
    }

    /**
     * @brief Mounts a FAT32 filesystem from a block device.
     */
    NtStatus mount(std::shared_ptr<storage::IBlockDevice> device) {
        if (!device) return NtStatus::InvalidParameter;

        std::lock_guard<std::mutex> lock(mutex_);
        device_ = std::move(device);

        // Read Sector 0 (Boot Sector)
        std::vector<uint8_t> bootSec(device_->getBlockSize());
        NtStatus st = device_->readBlocks(0, 1, bootSec.data());
        if (!NT_SUCCESS(st)) return st;

        const auto* bs = reinterpret_cast<const BootSector*>(bootSec.data());
        if (bs->signature != 0xAA55) return NtStatus::UnrecognizedVolume;

        bytesPerSector_ = bs->bytesPerSector ? bs->bytesPerSector : 512;
        sectorsPerCluster_ = bs->sectorsPerCluster ? bs->sectorsPerCluster : 1;
        bytesPerCluster_ = bytesPerSector_ * sectorsPerCluster_;
        reservedSectors_ = bs->reservedSectorCount;
        numFats_ = bs->numFats;
        fatSize_ = bs->fatSize32 ? bs->fatSize32 : bs->fatSize16;
        rootCluster_ = bs->rootCluster ? bs->rootCluster : 2;
        firstDataSector_ = reservedSectors_ + (numFats_ * fatSize_);
        totalSectors_ = bs->totalSectors32 ? bs->totalSectors32 : bs->totalSectors16;

        uint32_t dataSectors = static_cast<uint32_t>(totalSectors_ - firstDataSector_);
        totalClusters_ = dataSectors / sectorsPerCluster_;

        mounted_ = true;
        return NtStatus::Success;
    }

    [[nodiscard]] bool isMounted() const noexcept { return mounted_; }
    [[nodiscard]] uint32_t getBytesPerCluster() const noexcept { return bytesPerCluster_; }
    [[nodiscard]] uint32_t getRootCluster() const noexcept { return rootCluster_; }
    [[nodiscard]] uint32_t getTotalClusters() const noexcept { return totalClusters_; }

    /**
     * @brief Reads a cluster entry from the primary FAT table.
     */
    NtStatus readFatEntry(uint32_t cluster, uint32_t& outNextCluster) {
        if (!mounted_ || !device_) return NtStatus::DeviceNotReady;
        if (cluster < 2 || cluster >= totalClusters_ + 2) {
            return NtStatus::EndOfFile;
        }

        uint32_t fatOffsetBytes = cluster * 4;
        uint32_t fatSectorIndex = reservedSectors_ + (fatOffsetBytes / bytesPerSector_);
        uint32_t offsetInSector = fatOffsetBytes % bytesPerSector_;

        std::vector<uint8_t> sec(bytesPerSector_);
        NtStatus st = device_->readBlocks(fatSectorIndex, 1, sec.data());
        if (!NT_SUCCESS(st)) return st;

        uint32_t val = *reinterpret_cast<const uint32_t*>(sec.data() + offsetInSector);
        outNextCluster = val & FAT32_CLUSTER_MASK;
        return NtStatus::Success;
    }

    /**
     * @brief Writes a cluster entry to all FAT copies.
     */
    NtStatus writeFatEntry(uint32_t cluster, uint32_t nextCluster) {
        if (!mounted_ || !device_) return NtStatus::DeviceNotReady;

        uint32_t fatOffsetBytes = cluster * 4;
        uint32_t offsetInSector = fatOffsetBytes % bytesPerSector_;
        uint32_t secOffsetFromFatStart = fatOffsetBytes / bytesPerSector_;

        for (uint8_t f = 0; f < numFats_; ++f) {
            uint32_t fatStart = reservedSectors_ + (f * fatSize_);
            uint32_t targetSector = fatStart + secOffsetFromFatStart;

            std::vector<uint8_t> sec(bytesPerSector_);
            NtStatus st = device_->readBlocks(targetSector, 1, sec.data());
            if (!NT_SUCCESS(st)) return st;

            uint32_t currentVal = *reinterpret_cast<uint32_t*>(sec.data() + offsetInSector);
            uint32_t newVal = (currentVal & 0xF0000000) | (nextCluster & FAT32_CLUSTER_MASK);
            *reinterpret_cast<uint32_t*>(sec.data() + offsetInSector) = newVal;

            st = device_->writeBlocks(targetSector, 1, sec.data());
            if (!NT_SUCCESS(st)) return st;
        }
        return NtStatus::Success;
    }

    /**
     * @brief Allocates an unallocated cluster from the FAT table.
     */
    NtStatus allocateCluster(uint32_t& outCluster) {
        if (!mounted_ || !device_) return NtStatus::DeviceNotReady;

        for (uint32_t c = nextFreeHint_; c < totalClusters_ + 2; ++c) {
            uint32_t entryVal = 0;
            if (NT_SUCCESS(readFatEntry(c, entryVal)) && entryVal == FAT32_CLUSTER_FREE) {
                writeFatEntry(c, FAT32_CLUSTER_EOC);
                // Zero out the newly allocated cluster's sectors
                std::vector<uint8_t> zeroCluster(bytesPerCluster_, 0);
                writeCluster(c, zeroCluster.data());
                nextFreeHint_ = c + 1;
                outCluster = c;
                return NtStatus::Success;
            }
        }
        // Wrap around search from cluster 3
        for (uint32_t c = 3; c < nextFreeHint_; ++c) {
            uint32_t entryVal = 0;
            if (NT_SUCCESS(readFatEntry(c, entryVal)) && entryVal == FAT32_CLUSTER_FREE) {
                writeFatEntry(c, FAT32_CLUSTER_EOC);
                std::vector<uint8_t> zeroCluster(bytesPerCluster_, 0);
                writeCluster(c, zeroCluster.data());
                nextFreeHint_ = c + 1;
                outCluster = c;
                return NtStatus::Success;
            }
        }
        return NtStatus::DiskFull;
    }

    /**
     * @brief Reads a full cluster from data area.
     */
    NtStatus readCluster(uint32_t cluster, void* buffer) {
        if (!mounted_ || !device_) return NtStatus::DeviceNotReady;
        if (cluster < 2) return NtStatus::InvalidParameter;

        uint64_t sector = firstDataSector_ + (static_cast<uint64_t>(cluster - 2) * sectorsPerCluster_);
        return device_->readBlocks(sector, sectorsPerCluster_, buffer);
    }

    /**
     * @brief Writes a full cluster into data area.
     */
    NtStatus writeCluster(uint32_t cluster, const void* buffer) {
        if (!mounted_ || !device_) return NtStatus::DeviceNotReady;
        if (cluster < 2) return NtStatus::InvalidParameter;

        uint64_t sector = firstDataSector_ + (static_cast<uint64_t>(cluster - 2) * sectorsPerCluster_);
        return device_->writeBlocks(sector, sectorsPerCluster_, buffer);
    }

    /**
     * @brief Traverses a cluster chain and collects all clusters.
     */
    NtStatus getClusterChain(uint32_t startCluster, std::vector<uint32_t>& outChain) {
        outChain.clear();
        if (startCluster < 2) return NtStatus::Success;

        uint32_t current = startCluster;
        while (current >= 2 && current < FAT32_CLUSTER_EOC_MIN) {
            outChain.push_back(current);
            uint32_t next = 0;
            NtStatus st = readFatEntry(current, next);
            if (!NT_SUCCESS(st)) return st;
            if (next == current || next == FAT32_CLUSTER_FREE || next == FAT32_CLUSTER_BAD) break;
            current = next;
        }
        return NtStatus::Success;
    }

    /**
     * @brief Frees a cluster chain in the FAT table.
     */
    NtStatus freeClusterChain(uint32_t startCluster) {
        if (startCluster < 2) return NtStatus::Success;
        std::vector<uint32_t> chain;
        getClusterChain(startCluster, chain);
        for (uint32_t c : chain) {
            writeFatEntry(c, FAT32_CLUSTER_FREE);
        }
        return NtStatus::Success;
    }

    /**
     * @brief Lists all files/directories in the specified directory cluster.
     */
    NtStatus readDirectory(uint32_t dirCluster, std::vector<FatFileInfo>& outEntries) {
        outEntries.clear();
        if (!mounted_) return NtStatus::DeviceNotReady;

        std::vector<uint32_t> chain;
        NtStatus st = getClusterChain(dirCluster, chain);
        if (!NT_SUCCESS(st)) return st;

        std::array<char16_t, 260> lfnUnits{};
        size_t lfnUnitCount = 0;
        uint8_t lfnChecksum = 0;
        uint8_t expectedLfnOrder = 0;
        bool lfnValid = false;

        std::vector<uint8_t> clusterBuf(bytesPerCluster_);

        for (uint32_t c : chain) {
            st = readCluster(c, clusterBuf.data());
            if (!NT_SUCCESS(st)) return st;

            size_t entryCount = bytesPerCluster_ / sizeof(DirectoryEntry);
            const auto* entries = reinterpret_cast<const DirectoryEntry*>(clusterBuf.data());

            for (size_t i = 0; i < entryCount; ++i) {
                const auto& de = entries[i];
                uint8_t firstByte = static_cast<uint8_t>(de.name[0]);

                if (firstByte == 0x00) {
                    return NtStatus::Success; // No more entries
                }
                if (firstByte == 0xE5) {
                    lfnValid = false;
                    expectedLfnOrder = 0;
                    continue; // Deleted entry
                }

                // Check if LFN entry
                if (de.attr == ATTR_LONG_NAME) {
                    const auto* lfn = reinterpret_cast<const LfnEntry*>(&de);
                    const uint8_t rawOrder = lfn->order;
                    const uint8_t order = rawOrder & 0x1F;
                    const bool isLast = (rawOrder & 0x40) != 0;
                    const bool validEntry = (rawOrder & 0xA0) == 0 && order != 0 && order <= 20 &&
                        lfn->type == 0 && lfn->firstClusterLow == 0;
                    if (!validEntry || (isLast && expectedLfnOrder != 0) ||
                        (!isLast && (expectedLfnOrder == 0 || order != expectedLfnOrder)) ||
                        (isLast && order > 20)) {
                        lfnValid = false;
                        expectedLfnOrder = 0;
                        continue;
                    }
                    if (isLast) {
                        lfnUnits.fill(0xFFFF);
                        lfnUnitCount = static_cast<size_t>(order) * 13;
                        lfnChecksum = lfn->checksum;
                        expectedLfnOrder = order;
                        lfnValid = true;
                    }
                    if (!lfnValid || lfn->checksum != lfnChecksum || order != expectedLfnOrder) {
                        lfnValid = false;
                        expectedLfnOrder = 0;
                        continue;
                    }
                    const size_t base = static_cast<size_t>(order - 1) * 13;
                    for (size_t k = 0; k < 5; ++k) lfnUnits[base + k] = lfn->name1[k];
                    for (size_t k = 0; k < 6; ++k) lfnUnits[base + 5 + k] = lfn->name2[k];
                    for (size_t k = 0; k < 2; ++k) lfnUnits[base + 11 + k] = lfn->name3[k];
                    expectedLfnOrder = static_cast<uint8_t>(order - 1);
                    continue;
                }

                // Volume ID entry (skip unless directory)
                if ((de.attr & ATTR_VOLUME_ID) && !(de.attr & ATTR_DIRECTORY)) {
                    lfnValid = false;
                    expectedLfnOrder = 0;
                    continue;
                }

                FatFileInfo info{};
                info.attributes = de.attr;
                info.firstCluster = de.getFirstCluster();
                info.fileSize = de.fileSize;
                info.isDirectory = (de.attr & ATTR_DIRECTORY) != 0;

                if (lfnValid && expectedLfnOrder == 0 && lfnChecksum == calculateShortNameChecksum(de.name)) {
                    size_t length = 0;
                    while (length < lfnUnitCount && lfnUnits[length] != 0 && lfnUnits[length] != 0xFFFF) ++length;
                    // FAT LFN stores UTF-16. This interface uses wchar_t; reject invalid
                    // surrogate pairs instead of silently corrupting a path.
                    bool validUtf16 = length != 0 && length <= 255;
                    for (size_t n = 0; n < length; ++n) {
                        const uint16_t u = lfnUnits[n];
                        if (u >= 0xD800 && u <= 0xDBFF) {
                            if (n + 1 >= length || lfnUnits[n + 1] < 0xDC00 || lfnUnits[n + 1] > 0xDFFF) { validUtf16 = false; break; }
                            ++n;
                        } else if (u >= 0xDC00 && u <= 0xDFFF) { validUtf16 = false; break; }
                    }
                    if (validUtf16) {
                        for (size_t n = 0; n < length; ++n) info.name.push_back(static_cast<wchar_t>(lfnUnits[n]));
                    } else info.name = formatShortName(de.name);
                } else {
                    info.name = formatShortName(de.name);
                }
                lfnValid = false;
                expectedLfnOrder = 0;

                // Skip "." and ".." in root listing or general results if desired, or keep them
                if (info.name != L"." && info.name != L"..") {
                    outEntries.push_back(info);
                }
            }
        }
        return NtStatus::Success;
    }

    /**
     * @brief Finds a file or directory by traversing a path from root.
     */
    NtStatus findPath(std::wstring_view path, FatFileInfo& outInfo, uint32_t* outParentDirCluster = nullptr) {
        if (!mounted_) return NtStatus::DeviceNotReady;

        std::vector<std::wstring> parts = splitPath(path);
        if (parts.empty()) {
            // Root directory requested
            outInfo.name = L"";
            outInfo.attributes = ATTR_DIRECTORY;
            outInfo.firstCluster = rootCluster_;
            outInfo.fileSize = 0;
            outInfo.isDirectory = true;
            if (outParentDirCluster) *outParentDirCluster = rootCluster_;
            return NtStatus::Success;
        }

        uint32_t currentDirCluster = rootCluster_;
        for (size_t p = 0; p < parts.size(); ++p) {
            const auto& part = parts[p];
            std::vector<FatFileInfo> entries;
            NtStatus st = readDirectory(currentDirCluster, entries);
            if (!NT_SUCCESS(st)) return st;

            bool found = false;
            for (const auto& item : entries) {
                if (iequals(item.name, part)) {
                    if (p + 1 == parts.size()) {
                        outInfo = item;
                        if (outParentDirCluster) *outParentDirCluster = currentDirCluster;
                        return NtStatus::Success;
                    } else if (item.isDirectory) {
                        currentDirCluster = item.firstCluster;
                        found = true;
                        break;
                    } else {
                        return NtStatus::NotADirectory;
                    }
                }
            }
            if (!found) return NtStatus::NoSuchFile;
        }
        return NtStatus::NoSuchFile;
    }

    /**
     * @brief Reads bytes from a file.
     */
    NtStatus readFile(
        std::wstring_view path,
        uint64_t offset,
        size_t length,
        std::vector<uint8_t>& outBuffer
    ) {
        outBuffer.clear();
        FatFileInfo info{};
        NtStatus st = findPath(path, info);
        if (!NT_SUCCESS(st)) return st;
        if (info.isDirectory) return NtStatus::FileIsADirectory;
        if (offset >= info.fileSize) return NtStatus::EndOfFile;

        size_t bytesToRead = std::min<size_t>(length, static_cast<size_t>(info.fileSize - offset));
        outBuffer.resize(bytesToRead);

        std::vector<uint32_t> chain;
        st = getClusterChain(info.firstCluster, chain);
        if (!NT_SUCCESS(st)) return st;

        size_t startClusterIdx = static_cast<size_t>(offset / bytesPerCluster_);
        size_t offsetInStartCluster = static_cast<size_t>(offset % bytesPerCluster_);

        std::vector<uint8_t> clusterBuf(bytesPerCluster_);
        size_t bytesRead = 0;

        for (size_t i = startClusterIdx; i < chain.size() && bytesRead < bytesToRead; ++i) {
            st = readCluster(chain[i], clusterBuf.data());
            if (!NT_SUCCESS(st)) return st;

            size_t srcOffset = (i == startClusterIdx) ? offsetInStartCluster : 0;
            size_t chunk = std::min<size_t>(bytesPerCluster_ - srcOffset, bytesToRead - bytesRead);

            std::memcpy(outBuffer.data() + bytesRead, clusterBuf.data() + srcOffset, chunk);
            bytesRead += chunk;
        }

        return NtStatus::Success;
    }

    /**
     * @brief Creates a new file or overwrites an existing file with data.
     */
    NtStatus createFile(
        std::wstring_view path,
        std::span<const uint8_t> initialData,
        uint32_t attributes = ATTR_ARCHIVE
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return NtStatus::DeviceNotReady;

        std::vector<std::wstring> parts = splitPath(path);
        if (parts.empty()) return NtStatus::InvalidParameter;

        // Traverse or locate parent directory
        uint32_t parentCluster = rootCluster_;
        if (parts.size() > 1) {
            std::wstring parentPath;
            for (size_t i = 0; i < parts.size() - 1; ++i) {
                if (i > 0) parentPath += L"\\";
                parentPath += parts[i];
            }
            FatFileInfo parentInfo{};
            NtStatus st = findPath(parentPath, parentInfo);
            if (!NT_SUCCESS(st)) return st;
            if (!parentInfo.isDirectory) return NtStatus::NotADirectory;
            parentCluster = parentInfo.firstCluster;
        }

        const std::wstring& fileName = parts.back();
        if (!isValidLongName(fileName)) return NtStatus::InvalidParameter;

        // 1. Allocate cluster chain for file data
        uint32_t firstCluster = 0;
        if (!initialData.empty()) {
            size_t numClusters = (initialData.size() + bytesPerCluster_ - 1) / bytesPerCluster_;
            uint32_t prevCluster = 0;

            for (size_t c = 0; c < numClusters; ++c) {
                uint32_t newClus = 0;
                NtStatus st = allocateCluster(newClus);
                if (!NT_SUCCESS(st)) {
                    if (firstCluster) freeClusterChain(firstCluster);
                    return st;
                }
                if (c == 0) firstCluster = newClus;
                if (prevCluster != 0) {
                    writeFatEntry(prevCluster, newClus);
                }
                prevCluster = newClus;

                size_t offset = c * bytesPerCluster_;
                size_t chunk = std::min<size_t>(bytesPerCluster_, initialData.size() - offset);
                std::vector<uint8_t> cBuf(bytesPerCluster_, 0);
                std::memcpy(cBuf.data(), initialData.data() + offset, chunk);
                writeCluster(newClus, cBuf.data());
            }
            writeFatEntry(prevCluster, FAT32_CLUSTER_EOC);
        }

        // 2. Add directory entry to parent directory
        return addDirectoryEntry(parentCluster, fileName, firstCluster, static_cast<uint32_t>(initialData.size()), static_cast<uint8_t>(attributes));
    }

    /**
     * @brief Creates a directory.
     */
    NtStatus createDirectory(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return NtStatus::DeviceNotReady;

        std::vector<std::wstring> parts = splitPath(path);
        if (parts.empty()) return NtStatus::InvalidParameter;

        uint32_t parentCluster = rootCluster_;
        if (parts.size() > 1) {
            std::wstring parentPath;
            for (size_t i = 0; i < parts.size() - 1; ++i) {
                if (i > 0) parentPath += L"\\";
                parentPath += parts[i];
            }
            FatFileInfo parentInfo{};
            NtStatus st = findPath(parentPath, parentInfo);
            if (!NT_SUCCESS(st)) return st;
            if (!parentInfo.isDirectory) return NtStatus::NotADirectory;
            parentCluster = parentInfo.firstCluster;
        }

        const std::wstring& dirName = parts.back();
        if (!isValidLongName(dirName)) return NtStatus::InvalidParameter;

        // Allocate a cluster for the directory entries
        uint32_t newDirCluster = 0;
        NtStatus st = allocateCluster(newDirCluster);
        if (!NT_SUCCESS(st)) return st;

        // Initialize "." and ".." entries inside the new directory cluster
        std::vector<uint8_t> dirClusterBuf(bytesPerCluster_, 0);
        auto* dotEntry = reinterpret_cast<DirectoryEntry*>(dirClusterBuf.data());
        std::memset(dotEntry->name, ' ', 11);
        dotEntry->name[0] = '.';
        dotEntry->attr = ATTR_DIRECTORY;
        dotEntry->setFirstCluster(newDirCluster);

        auto* dotDotEntry = dotEntry + 1;
        std::memset(dotDotEntry->name, ' ', 11);
        dotDotEntry->name[0] = '.'; dotDotEntry->name[1] = '.';
        dotDotEntry->attr = ATTR_DIRECTORY;
        dotDotEntry->setFirstCluster((parentCluster == rootCluster_) ? 0 : parentCluster);

        st = writeCluster(newDirCluster, dirClusterBuf.data());
        if (!NT_SUCCESS(st)) {
            freeClusterChain(newDirCluster);
            return st;
        }

        // Add to parent
        return addDirectoryEntry(parentCluster, dirName, newDirCluster, 0, ATTR_DIRECTORY);
    }

private:
    static bool isValidLongName(std::wstring_view name) noexcept {
        if (name.empty() || name.size() > 255 || name.back() == L' ' || name.back() == L'.') return false;
        for (wchar_t ch : name) {
            if (ch < 0x20 || ch == L'"' || ch == L'*' || ch == L'/' || ch == L':' ||
                ch == L'<' || ch == L'>' || ch == L'?' || ch == L'\\' || ch == L'|') return false;
            if (static_cast<uint32_t>(ch) > 0xFFFF || (ch >= 0xD800 && ch <= 0xDFFF)) return false;
        }
        return name != L"." && name != L"..";
    }

    static uint8_t calculateShortNameChecksum(const char name[11]) noexcept {
        uint8_t sum = 0;
        for (int i = 0; i < 11; ++i) {
            sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + static_cast<uint8_t>(name[i]));
        }
        return sum;
    }

    static std::wstring formatShortName(const char name[11]) {
        std::wstring s;
        // Base name (first 8 chars)
        for (int i = 0; i < 8 && name[i] != ' '; ++i) {
            s.push_back(static_cast<wchar_t>(static_cast<unsigned char>(name[i])));
        }
        // Extension (last 3 chars)
        if (name[8] != ' ') {
            s.push_back(L'.');
            for (int i = 8; i < 11 && name[i] != ' '; ++i) {
                s.push_back(static_cast<wchar_t>(static_cast<unsigned char>(name[i])));
            }
        }
        return s;
    }

    static void createShortName(const std::wstring& lfn, char outShort[11]) {
        std::memset(outShort, ' ', 11);
        size_t dotPos = lfn.find_last_of(L'.');
        std::wstring base = (dotPos != std::wstring::npos) ? lfn.substr(0, dotPos) : lfn;
        std::wstring ext  = (dotPos != std::wstring::npos) ? lfn.substr(dotPos + 1) : L"";

        size_t bLen = std::min<size_t>(6, base.size());
        for (size_t i = 0; i < bLen; ++i) {
            outShort[i] = static_cast<char>(std::towupper(base[i]));
        }
        outShort[6] = '~';
        outShort[7] = '1';

        size_t eLen = std::min<size_t>(3, ext.size());
        for (size_t i = 0; i < eLen; ++i) {
            outShort[8 + i] = static_cast<char>(std::towupper(ext[i]));
        }
    }

    static std::vector<std::wstring> splitPath(std::wstring_view path) {
        std::vector<std::wstring> parts;
        std::wstring cur;
        for (wchar_t c : path) {
            if (c == L'\\' || c == L'/') {
                if (!cur.empty()) {
                    // Strip leading drive prefix if present (e.g. "C:")
                    if (cur.size() == 2 && cur[1] == L':') {
                        cur.clear();
                        continue;
                    }
                    parts.push_back(cur);
                    cur.clear();
                }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) {
            if (!(cur.size() == 2 && cur[1] == L':')) {
                parts.push_back(cur);
            }
        }
        return parts;
    }

    static bool iequals(std::wstring_view a, std::wstring_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::towlower(a[i]) != std::towlower(b[i])) return false;
        }
        return true;
    }

    NtStatus addDirectoryEntry(
        uint32_t dirCluster,
        const std::wstring& fileName,
        uint32_t firstCluster,
        uint32_t fileSize,
        uint8_t attributes
    ) {
        char shortName[11];
        createShortName(fileName, shortName);
        uint8_t chksum = calculateShortNameChecksum(shortName);

        // Build LFN entries
        size_t lfnChars = fileName.size();
        size_t lfnCount = (lfnChars + 12) / 13;
        size_t totalEntriesNeeded = lfnCount + 1; // LFNs + 1 8.3 entry

        std::vector<uint32_t> chain;
        NtStatus st = getClusterChain(dirCluster, chain);
        if (!NT_SUCCESS(st)) return st;

        // Search for consecutive free entry slots
        for (uint32_t c : chain) {
            std::vector<uint8_t> clusterBuf(bytesPerCluster_);
            st = readCluster(c, clusterBuf.data());
            if (!NT_SUCCESS(st)) return st;

            size_t entryCount = bytesPerCluster_ / sizeof(DirectoryEntry);
            auto* entries = reinterpret_cast<DirectoryEntry*>(clusterBuf.data());

            for (size_t i = 0; i + totalEntriesNeeded <= entryCount; ++i) {
                bool slotAvailable = true;
                for (size_t k = 0; k < totalEntriesNeeded; ++k) {
                    uint8_t fb = static_cast<uint8_t>(entries[i + k].name[0]);
                    if (fb != 0x00 && fb != 0xE5) {
                        slotAvailable = false;
                        break;
                    }
                }

                if (slotAvailable) {
                    // Write LFN entries in reverse sequence (N down to 1)
                    for (size_t seq = lfnCount; seq >= 1; --seq) {
                        size_t lfnIdx = i + (lfnCount - seq);
                        auto* lfn = reinterpret_cast<LfnEntry*>(&entries[lfnIdx]);
                        std::memset(lfn, 0xFF, sizeof(LfnEntry));
                        lfn->order = static_cast<uint8_t>(seq);
                        if (seq == lfnCount) lfn->order |= 0x40; // Last LFN marker
                        lfn->attr = ATTR_LONG_NAME;
                        lfn->type = 0;
                        lfn->checksum = chksum;
                        lfn->firstClusterLow = 0;

                        size_t charOffset = (seq - 1) * 13;
                        for (int k = 0; k < 5; ++k) {
                            lfn->name1[k] = (charOffset + k < lfnChars) ? static_cast<char16_t>(fileName[charOffset + k]) : ((charOffset + k == lfnChars) ? 0 : 0xFFFF);
                        }
                        for (int k = 0; k < 6; ++k) {
                            lfn->name2[k] = (charOffset + 5 + k < lfnChars) ? static_cast<char16_t>(fileName[charOffset + 5 + k]) : ((charOffset + 5 + k == lfnChars) ? 0 : 0xFFFF);
                        }
                        for (int k = 0; k < 2; ++k) {
                            lfn->name3[k] = (charOffset + 11 + k < lfnChars) ? static_cast<char16_t>(fileName[charOffset + 11 + k]) : ((charOffset + 11 + k == lfnChars) ? 0 : 0xFFFF);
                        }
                    }

                    // Write short directory entry
                    size_t shortIdx = i + lfnCount;
                    auto* de = &entries[shortIdx];
                    std::memcpy(de->name, shortName, 11);
                    de->attr = attributes;
                    de->ntReserved = 0;
                    de->creationTime = 0x6800; // 13:00
                    de->creationDate = 0x5D41; // 2026-10-01
                    de->writeTime = 0x6800;
                    de->writeDate = 0x5D41;
                    de->lastAccessDate = 0x5D41;
                    de->setFirstCluster(firstCluster);
                    de->fileSize = fileSize;

                    return writeCluster(c, clusterBuf.data());
                }
            }
        }

        // If no slot was found in existing clusters, allocate a new cluster for the directory
        uint32_t newClus = 0;
        st = allocateCluster(newClus);
        if (!NT_SUCCESS(st)) return st;

        writeFatEntry(chain.back(), newClus);
        writeFatEntry(newClus, FAT32_CLUSTER_EOC);

        std::vector<uint8_t> newClusBuf(bytesPerCluster_, 0);
        writeCluster(newClus, newClusBuf.data());

        // Re-call now that cluster chain is extended
        return addDirectoryEntry(dirCluster, fileName, firstCluster, fileSize, attributes);
    }

    std::shared_ptr<storage::IBlockDevice> device_;
    mutable std::mutex mutex_;
    bool mounted_{false};
    uint32_t bytesPerSector_{512};
    uint8_t sectorsPerCluster_{8};
    uint32_t bytesPerCluster_{4096};
    uint16_t reservedSectors_{32};
    uint8_t numFats_{2};
    uint32_t fatSize_{32};
    uint32_t rootCluster_{2};
    uint64_t firstDataSector_{0};
    uint32_t totalSectors_{0};
    uint32_t totalClusters_{0};
    uint32_t nextFreeHint_{3};
};

} // namespace micant::fat32
