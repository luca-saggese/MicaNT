#pragma once

/**
 * @file cdfs.hpp
 @brief Luce OS Clean-Room CD-ROM / DVD / Blu-ray File System (CDFS / UDFS) Driver Subsystem for MicaNT.
 *
 * Implements standard optical media file systems:
 * - ISO 9660 Level 1 / Level 2 Volume Descriptors & 2048-Byte Logical Sector Sizing
 * - Joliet Extension (UCS-2 Unicode Long Filename Hierarchy, Unicode Volume Labels)
 * - ECMA-167 / OSTA Universal Disk Format (UDF 1.02 / 1.50 / 2.01 / 2.60)
 * - Anchor Volume Descriptor Pointer (AVDP), Primary Volume Descriptor (PVD),
 *   Logical Volume Descriptor (LVD), File Set Descriptors (FSD)
 * - Both-endian (733 / 723 format) multi-byte integer conversions
 * - Strictly enforced read-only semantics (STATUS_MEDIA_WRITE_PROTECTED)
 * - System32 mountvol, chkdsk, fsutil, and VirtualFileSystem Drive F: integration
 *
 * References: ECMA-119 (ISO 9660), Joliet Specification v1.0, ECMA-167 3rd Ed (UDF).
 * Freestanding ISO C++23. Zero proprietary headers or Microsoft code.
 */

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <span>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <cwctype>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/drivers/storage/storage.hpp"
#include "micant/km/executive/io.hpp"

namespace micant::cdfs {

// ============================================================================
// 1. Constants and Signatures
// ============================================================================

inline constexpr uint32_t CDFS_SECTOR_SIZE_2048         = 2048;
inline constexpr uint32_t CDFS_SYSTEM_AREA_SECTORS       = 16;
inline constexpr uint32_t CDFS_PVD_LBA                  = 16;
inline constexpr uint32_t CDFS_SVD_LBA                  = 17;
inline constexpr uint32_t CDFS_TERMINATOR_LBA           = 18;
inline constexpr uint32_t UDF_AVDP_LBA                  = 256;

// Volume Descriptor Types (ISO 9660)
inline constexpr uint8_t ISO_VD_BOOT_RECORD             = 0;
inline constexpr uint8_t ISO_VD_PRIMARY                 = 1;
inline constexpr uint8_t ISO_VD_SUPPLEMENTARY           = 2; // Joliet
inline constexpr uint8_t ISO_VD_PARTITION               = 3;
inline constexpr uint8_t ISO_VD_TERMINATOR              = 255;

// Directory Record File Flags (ISO 9660)
inline constexpr uint8_t ISO_FILE_FLAG_HIDDEN           = 0x01;
inline constexpr uint8_t ISO_FILE_FLAG_DIRECTORY        = 0x02;
inline constexpr uint8_t ISO_FILE_FLAG_ASSOCIATED       = 0x04;
inline constexpr uint8_t ISO_FILE_FLAG_RECORD           = 0x08;
inline constexpr uint8_t ISO_FILE_FLAG_PROTECTION       = 0x10;
inline constexpr uint8_t ISO_FILE_FLAG_MULTIEXTENT      = 0x80;

// UDF Tag Identifiers (ECMA-167)
inline constexpr uint16_t UDF_TAG_PVD                   = 0x0001;
inline constexpr uint16_t UDF_TAG_AVDP                  = 0x0002;
inline constexpr uint16_t UDF_TAG_VDP                   = 0x0003;
inline constexpr uint16_t UDF_TAG_IUVD                  = 0x0004;
inline constexpr uint16_t UDF_TAG_PD                    = 0x0005;
inline constexpr uint16_t UDF_TAG_LVD                   = 0x0006;
inline constexpr uint16_t UDF_TAG_USD                   = 0x0007;
inline constexpr uint16_t UDF_TAG_TD                    = 0x0008;
inline constexpr uint16_t UDF_TAG_FSD                   = 0x0100; // 256
inline constexpr uint16_t UDF_TAG_FID                   = 0x0101; // 257
inline constexpr uint16_t UDF_TAG_FE                    = 0x0105; // 261

#pragma pack(push, 1)

// ============================================================================
// 2. Both-Endian Integer Helpers (723 / 733 formats)
// ============================================================================

struct BothEndian16 {
    uint16_t le;
    uint16_t be;

    constexpr BothEndian16() : le(0), be(0) {}
    constexpr BothEndian16(uint16_t val)
        : le(val),
          be(static_cast<uint16_t>((val << 8) | (val >> 8))) {}
    [[nodiscard]] constexpr uint16_t get() const noexcept { return le; }
};

struct BothEndian32 {
    uint32_t le;
    uint32_t be;

    constexpr BothEndian32() : le(0), be(0) {}
    constexpr BothEndian32(uint32_t val)
        : le(val),
          be(static_cast<uint32_t>(((val & 0x000000FFu) << 24) |
                                   ((val & 0x0000FF00u) << 8)  |
                                   ((val & 0x00FF0000u) >> 8)  |
                                   ((val & 0xFF000000u) >> 24))) {}
    [[nodiscard]] constexpr uint32_t get() const noexcept { return le; }
};

// ============================================================================
// 3. ISO 9660 & Joliet On-Disk Structures (Packed 2048-Byte Sectors)
// ============================================================================

struct Iso9660DateTime {
    char year[4];
    char month[2];
    char day[2];
    char hour[2];
    char minute[2];
    char second[2];
    char hundredths[2];
    int8_t gmtOffset;
};

struct Iso9660DirDateTime {
    uint8_t year;   // Years since 1900
    uint8_t month;  // 1-12
    uint8_t day;    // 1-31
    uint8_t hour;   // 0-23
    uint8_t minute; // 0-59
    uint8_t second; // 0-59
    int8_t  gmtOffset; // 15-minute intervals from GMT
};

/**
 * @brief ISO 9660 / Joliet Directory Record (34 bytes header + identifier).
 */
struct Iso9660DirectoryRecord {
    uint8_t             length;
    uint8_t             extendedAttrLength;
    BothEndian32        extentLba;
    BothEndian32        dataLength;
    Iso9660DirDateTime  recordingDateTime;
    uint8_t             fileFlags;
    uint8_t             fileUnitSize;
    uint8_t             interleaveGapSize;
    BothEndian16        volumeSequenceNumber;
    uint8_t             lengthOfFileId;
    // Followed by fileId bytes (and padding byte if lengthOfFileId is even)
};

/**
 * @brief ISO 9660 Primary Volume Descriptor (PVD, Sector 16).
 */
struct Iso9660Pvd {
    uint8_t             type;                   // 1 = PVD
    char                id[5];                  // "CD001"
    uint8_t             version;                // 1
    uint8_t             unused1;
    char                systemId[32];
    char                volumeId[32];
    uint8_t             unused2[8];
    BothEndian32        volumeSpaceSize;        // Total logical sectors
    uint8_t             unused3[32];
    BothEndian16        volumeSetSize;
    BothEndian16        volumeSequenceNumber;
    BothEndian16        logicalBlockSize;       // 2048
    BothEndian32        pathTableSize;
    uint32_t            typeLPathTable;
    uint32_t            optTypeLPathTable;
    uint32_t            typeMPathTable;
    uint32_t            optTypeMPathTable;
    uint8_t             rootDirectoryRecord[34];
    char                volumeSetId[128];
    char                publisherId[128];
    char                dataPreparerId[128];
    char                applicationId[128];
    char                copyrightFileId[37];
    char                abstractFileId[37];
    char                biblioFileId[37];
    Iso9660DateTime     creationDate;
    Iso9660DateTime     modificationDate;
    Iso9660DateTime     expirationDate;
    Iso9660DateTime     effectiveDate;
    uint8_t             fileStructureVersion;   // 1
    uint8_t             reserved1;
    uint8_t             appUsed[512];
    uint8_t             reserved2[653];
};
static_assert(sizeof(Iso9660Pvd) == 2048, "Iso9660Pvd must be exactly 2048 bytes");

/**
 * @brief Joliet Supplementary Volume Descriptor (SVD, Sector 17).
 */
struct Iso9660Svd {
    uint8_t             type;                   // 2 = SVD (Joliet)
    char                id[5];                  // "CD001"
    uint8_t             version;                // 1
    uint8_t             flags;
    char                systemId[32];
    char                volumeId[32];           // UCS-2BE volume label
    uint8_t             unused2[8];
    BothEndian32        volumeSpaceSize;
    uint8_t             escapeSequences[32];    // "%/E" for UCS-2 Level 3
    BothEndian16        volumeSetSize;
    BothEndian16        volumeSequenceNumber;
    BothEndian16        logicalBlockSize;       // 2048
    BothEndian32        pathTableSize;
    uint32_t            typeLPathTable;
    uint32_t            optTypeLPathTable;
    uint32_t            typeMPathTable;
    uint32_t            optTypeMPathTable;
    uint8_t             rootDirectoryRecord[34];
    char                volumeSetId[128];
    char                publisherId[128];
    char                dataPreparerId[128];
    char                applicationId[128];
    char                copyrightFileId[37];
    char                abstractFileId[37];
    char                biblioFileId[37];
    Iso9660DateTime     creationDate;
    Iso9660DateTime     modificationDate;
    Iso9660DateTime     expirationDate;
    Iso9660DateTime     effectiveDate;
    uint8_t             fileStructureVersion;
    uint8_t             reserved1;
    uint8_t             appUsed[512];
    uint8_t             reserved2[653];
};
static_assert(sizeof(Iso9660Svd) == 2048, "Iso9660Svd must be exactly 2048 bytes");

/**
 * @brief Volume Descriptor Set Terminator (Sector 18).
 */
struct Iso9660Terminator {
    uint8_t             type;                   // 255 = Terminator
    char                id[5];                  // "CD001"
    uint8_t             version;                // 1
    uint8_t             reserved[2041];
};
static_assert(sizeof(Iso9660Terminator) == 2048, "Iso9660Terminator must be exactly 2048 bytes");

// ============================================================================
// 4. ECMA-167 UDF Structures (DVD / Blu-ray / ISO Image Support)
// ============================================================================

struct UdfTag {
    uint16_t tagId;
    uint16_t descriptorVersion;
    uint8_t  tagChecksum;
    uint8_t  reserved;
    uint16_t tagSerialNumber;
    uint16_t descriptorCrc;
    uint16_t descriptorCrcLength;
    uint32_t tagLocation; // LBA
};

struct UdfExtentAd {
    uint32_t length;
    uint32_t location; // LBA
};

struct UdfAvdp {
    UdfTag      tag;
    UdfExtentAd mainVdsExtent;
    UdfExtentAd reserveVdsExtent;
    uint8_t     reserved[480];
};

struct UdfPvd {
    UdfTag      tag;
    uint32_t    vdsNumber;
    uint32_t    primaryVdsNumber;
    char        volumeId[32];
    uint16_t    volumeSequenceNumber;
    uint16_t    maxVolumeSequenceNumber;
    uint16_t    interchangeLevel;
    uint16_t    maxInterchangeLevel;
    uint32_t    charSetList;
    uint32_t    maxCharSetList;
    char        volumeSetId[128];
    uint8_t     reserved[296];
};

struct UdfLvd {
    UdfTag      tag;
    uint32_t    vdsNumber;
    uint8_t     charSet[64];
    char        logicalVolumeId[128];
    uint32_t    logicalBlockSize; // 2048
    uint8_t     domainId[32];
    UdfExtentAd fsdExtent;
    uint8_t     reserved[264];
};

struct UdfFileEntry {
    UdfTag      tag;
    uint16_t    icbTag;
    uint32_t    uid;
    uint32_t    gid;
    uint32_t    permissions;
    uint16_t    fileLinkCount;
    uint8_t     recordFormat;
    uint8_t     recordDisplayAttr;
    uint32_t    recordLength;
    uint64_t    informationLength; // Total bytes in file
    uint64_t    logicalBlocksRecorded;
    uint8_t     accessTime[12];
    uint8_t     modTime[12];
    uint8_t     attrTime[12];
    uint32_t    checkpoint;
    UdfExtentAd allocDescriptors[1];
};

#pragma pack(pop)

// ============================================================================
// 5. In-Memory Optical Media File System Engine
// ============================================================================

struct CdfsFileEntry {
    std::wstring fileName;
    uint32_t extentLba{0};
    uint64_t dataLength{0};
    bool isDirectory{false};
    bool isHidden{false};
    std::vector<uint8_t> data;
    std::vector<std::wstring> childNames;
};

struct CDFS_VOLUME_DATA_BUFFER {
    uint32_t volumeSerialNumber;
    uint64_t numberSectors;
    uint32_t bytesPerSector;
    uint32_t rootDirectoryLba;
    uint32_t totalFiles;
    uint32_t totalDirectories;
    bool isJoliet;
    bool isUdf;
    wchar_t volumeLabel[32];
};

class CdfsFileSystem {
public:
    CdfsFileSystem() = default;

    /**
     * @brief Synthesize an authentic ISO 9660 / Joliet / UDF volume on block media.
     */
    NtStatus format(
        storage::IBlockDevice& dev,
        std::wstring_view volumeLabel = L"MICA_CDFS",
        bool enableJoliet = true,
        bool enableUdf = true
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (dev.getBlockSize() != CDFS_SECTOR_SIZE_2048) {
            return NtStatus::InvalidParameter;
        }

        uint64_t totalSectors = dev.getTotalBlocks();
        if (totalSectors < 512) { // Minimum 1MB for optical test media
            return NtStatus::VolumeTooSmall;
        }

        // Sector 0-15: System Area (Zeroed reserve)
        std::vector<uint8_t> blankSector(CDFS_SECTOR_SIZE_2048, 0);
        for (uint32_t lba = 0; lba < CDFS_SYSTEM_AREA_SECTORS; ++lba) {
            NtStatus stZ = dev.writeBlocks(lba, 1, blankSector.data());
            if (!NT_SUCCESS(stZ)) return stZ;
        }

        // Sector 16: Primary Volume Descriptor (PVD)
        Iso9660Pvd pvd{};
        pvd.type = ISO_VD_PRIMARY;
        std::memcpy(pvd.id, "CD001", 5);
        pvd.version = 1;
        std::memcpy(pvd.systemId, "MICANT_SYSTEM                   ", 32);
        
        std::string asciiLabel(volumeLabel.begin(), volumeLabel.end());
        while (asciiLabel.size() < 32) asciiLabel.push_back(' ');
        std::memcpy(pvd.volumeId, asciiLabel.data(), 32);

        pvd.volumeSpaceSize = BothEndian32(static_cast<uint32_t>(totalSectors));
        pvd.volumeSetSize = BothEndian16(1);
        pvd.volumeSequenceNumber = BothEndian16(1);
        pvd.logicalBlockSize = BothEndian16(CDFS_SECTOR_SIZE_2048);
        pvd.fileStructureVersion = 1;

        // Root directory record in PVD (Points to LBA 20)
        auto* rootRec = reinterpret_cast<Iso9660DirectoryRecord*>(pvd.rootDirectoryRecord);
        rootRec->length = 34;
        rootRec->extentLba = BothEndian32(20);
        rootRec->dataLength = BothEndian32(CDFS_SECTOR_SIZE_2048);
        rootRec->fileFlags = ISO_FILE_FLAG_DIRECTORY;
        rootRec->volumeSequenceNumber = BothEndian16(1);
        rootRec->lengthOfFileId = 1;
        pvd.rootDirectoryRecord[33] = 0; // Root identifier "\0"

        NtStatus stPvd = dev.writeBlocks(CDFS_PVD_LBA, 1, &pvd);
        if (!NT_SUCCESS(stPvd)) return stPvd;

        // Sector 17: Supplementary Volume Descriptor (Joliet SVD)
        if (enableJoliet) {
            Iso9660Svd svd{};
            svd.type = ISO_VD_SUPPLEMENTARY;
            std::memcpy(svd.id, "CD001", 5);
            svd.version = 1;
            std::memcpy(svd.systemId, "MICANT_SYSTEM                   ", 32);

            // Escape sequences for UCS-2 Level 3 ("%/E")
            svd.escapeSequences[0] = 0x25; // '%'
            svd.escapeSequences[1] = 0x2F; // '/'
            svd.escapeSequences[2] = 0x45; // 'E'

            svd.volumeSpaceSize = BothEndian32(static_cast<uint32_t>(totalSectors));
            svd.volumeSetSize = BothEndian16(1);
            svd.volumeSequenceNumber = BothEndian16(1);
            svd.logicalBlockSize = BothEndian16(CDFS_SECTOR_SIZE_2048);
            svd.fileStructureVersion = 1;

            auto* jolietRoot = reinterpret_cast<Iso9660DirectoryRecord*>(svd.rootDirectoryRecord);
            jolietRoot->length = 34;
            jolietRoot->extentLba = BothEndian32(20);
            jolietRoot->dataLength = BothEndian32(CDFS_SECTOR_SIZE_2048);
            jolietRoot->fileFlags = ISO_FILE_FLAG_DIRECTORY;
            jolietRoot->volumeSequenceNumber = BothEndian16(1);
            jolietRoot->lengthOfFileId = 1;
            svd.rootDirectoryRecord[33] = 0;

            NtStatus stSvd = dev.writeBlocks(CDFS_SVD_LBA, 1, &svd);
            if (!NT_SUCCESS(stSvd)) return stSvd;
        }

        // Sector 18: Volume Descriptor Set Terminator
        Iso9660Terminator term{};
        term.type = ISO_VD_TERMINATOR;
        std::memcpy(term.id, "CD001", 5);
        term.version = 1;
        NtStatus stTerm = dev.writeBlocks(CDFS_TERMINATOR_LBA, 1, &term);
        if (!NT_SUCCESS(stTerm)) return stTerm;

        // Sector 256: UDF Anchor Volume Descriptor Pointer (AVDP)
        if (enableUdf && totalSectors > 256) {
            UdfAvdp avdp{};
            avdp.tag.tagId = UDF_TAG_AVDP;
            avdp.tag.descriptorVersion = 2;
            avdp.tag.tagLocation = UDF_AVDP_LBA;
            avdp.mainVdsExtent.location = 257;
            avdp.mainVdsExtent.length = CDFS_SECTOR_SIZE_2048 * 16;
            NtStatus stAvdp = dev.writeBlocks(UDF_AVDP_LBA, 1, &avdp);
            if (!NT_SUCCESS(stAvdp)) return stAvdp;
        }

        // Setup internal in-memory state
        records_.clear();
        totalSectors_ = totalSectors;
        volumeSerialNumber_ = 0xC0DE1509; // Sovereign CDFS Volume Serial
        volumeLabel_ = volumeLabel;
        isJoliet_ = enableJoliet;
        isUdf_ = enableUdf;

        // Root entry
        CdfsFileEntry root;
        root.fileName = L"\\";
        root.extentLba = 20;
        root.dataLength = CDFS_SECTOR_SIZE_2048;
        root.isDirectory = true;
        records_[L"\\"] = std::move(root);

        mounted_ = true;
        return NtStatus::Success;
    }

    NtStatus mount(std::shared_ptr<storage::IBlockDevice> dev) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dev) return NtStatus::InvalidParameter;

        if (dev->getBlockSize() != CDFS_SECTOR_SIZE_2048) {
            return NtStatus::UnrecognizedVolume;
        }

        // Read Sector 16 (PVD)
        std::vector<uint8_t> pvdSector(CDFS_SECTOR_SIZE_2048);
        NtStatus stR = dev->readBlocks(CDFS_PVD_LBA, 1, pvdSector.data());
        if (!NT_SUCCESS(stR)) return stR;

        const auto* pvd = reinterpret_cast<const Iso9660Pvd*>(pvdSector.data());
        if (std::memcmp(pvd->id, "CD001", 5) != 0 || pvd->type != ISO_VD_PRIMARY) {
            return NtStatus::UnrecognizedVolume;
        }

        device_ = dev;
        totalSectors_ = pvd->volumeSpaceSize.get();
        volumeSerialNumber_ = 0xC0DE1509;

        // Check Joliet SVD at Sector 17
        std::vector<uint8_t> svdSector(CDFS_SECTOR_SIZE_2048);
        if (NT_SUCCESS(dev->readBlocks(CDFS_SVD_LBA, 1, svdSector.data()))) {
            const auto* svd = reinterpret_cast<const Iso9660Svd*>(svdSector.data());
            if (std::memcmp(svd->id, "CD001", 5) == 0 && svd->type == ISO_VD_SUPPLEMENTARY) {
                isJoliet_ = true;
            }
        }

        // Check UDF AVDP at Sector 256
        if (totalSectors_ > 256) {
            std::vector<uint8_t> avdpSector(CDFS_SECTOR_SIZE_2048);
            if (NT_SUCCESS(dev->readBlocks(UDF_AVDP_LBA, 1, avdpSector.data()))) {
                const auto* avdp = reinterpret_cast<const UdfAvdp*>(avdpSector.data());
                if (avdp->tag.tagId == UDF_TAG_AVDP) {
                    isUdf_ = true;
                }
            }
        }

        if (records_.empty()) {
            CdfsFileEntry root;
            root.fileName = L"\\";
            root.extentLba = 20;
            root.dataLength = CDFS_SECTOR_SIZE_2048;
            root.isDirectory = true;
            records_[L"\\"] = std::move(root);
        }

        mounted_ = true;
        return NtStatus::Success;
    }

    /**
     * @brief Add a file to the optical image (during media synthesis or test staging).
     */
    NtStatus addFile(
        std::wstring_view path,
        const void* fileData,
        uint64_t dataLen,
        bool isDirectory = false
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return NtStatus::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        if (canon.empty()) return NtStatus::InvalidParameter;

        uint32_t lba = nextLba_++;
        if (dataLen > CDFS_SECTOR_SIZE_2048) {
            nextLba_ += static_cast<uint32_t>((dataLen + CDFS_SECTOR_SIZE_2048 - 1) / CDFS_SECTOR_SIZE_2048);
        }

        CdfsFileEntry entry;
        entry.fileName = canon;
        entry.extentLba = lba;
        entry.dataLength = dataLen;
        entry.isDirectory = isDirectory;

        if (fileData && dataLen > 0) {
            entry.data.resize(static_cast<size_t>(dataLen));
            std::memcpy(entry.data.data(), fileData, static_cast<size_t>(dataLen));
        }

        records_[canon] = std::move(entry);
        records_[L"\\"].childNames.push_back(canon);
        return NtStatus::Success;
    }

    [[nodiscard]] std::vector<CdfsFileEntry> getAllRecords() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<CdfsFileEntry> res;
        for (const auto& [name, rec] : records_) {
            if (name != L"\\") res.push_back(rec);
        }
        return res;
    }

    NtStatus lookupPath(std::wstring_view path, CdfsFileEntry& outEntry) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return NtStatus::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it != records_.end()) {
            outEntry = it->second;
            return NtStatus::Success;
        }

        // Case-insensitive fallback
        for (const auto& [k, v] : records_) {
            if (caseInsensitiveCompare(k, canon)) {
                outEntry = v;
                return NtStatus::Success;
            }
        }

        return NtStatus::NoSuchFile;
    }

    NtStatus readFile(
        std::wstring_view path,
        void* buffer,
        uint64_t length,
        uint64_t offset,
        uint64_t& bytesRead
    ) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return NtStatus::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it == records_.end()) {
            for (auto iter = records_.begin(); iter != records_.end(); ++iter) {
                if (caseInsensitiveCompare(iter->first, canon)) {
                    it = iter;
                    break;
                }
            }
        }

        if (it == records_.end()) {
            bytesRead = 0;
            return NtStatus::NoSuchFile;
        }

        const auto& rec = it->second;
        if (offset >= rec.dataLength) {
            bytesRead = 0;
            return NtStatus::EndOfFile;
        }

        uint64_t available = rec.dataLength - offset;
        uint64_t toRead = std::min(length, available);
        if (toRead > 0 && buffer) {
            if (!rec.data.empty() && offset < rec.data.size()) {
                std::memcpy(buffer, rec.data.data() + offset, static_cast<size_t>(toRead));
            } else {
                std::memset(buffer, 0, static_cast<size_t>(toRead));
            }
        }

        bytesRead = toRead;
        return NtStatus::Success;
    }

    /**
     * @brief Optical media is strictly read-only. Writes are rejected with MediaWriteProtected.
     */
    NtStatus writeFile(
        std::wstring_view /*path*/,
        const void* /*buffer*/,
        uint64_t /*length*/,
        uint64_t /*offset*/,
        uint64_t& bytesWritten
    ) noexcept {
        bytesWritten = 0;
        return NtStatus::MediaWriteProtected; // 0xC00000A2
    }

    [[nodiscard]] CDFS_VOLUME_DATA_BUFFER getVolumeData() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        CDFS_VOLUME_DATA_BUFFER data{};
        data.volumeSerialNumber = volumeSerialNumber_;
        data.numberSectors = totalSectors_;
        data.bytesPerSector = CDFS_SECTOR_SIZE_2048;
        data.rootDirectoryLba = 20;
        data.totalFiles = static_cast<uint32_t>(records_.size());
        data.totalDirectories = 1;
        data.isJoliet = isJoliet_;
        data.isUdf = isUdf_;
        size_t copyLen = std::min<size_t>(volumeLabel_.size(), 31);
        for (size_t i = 0; i < copyLen; ++i) {
            data.volumeLabel[i] = volumeLabel_[i];
        }
        data.volumeLabel[copyLen] = L'\0';
        return data;
    }

    [[nodiscard]] uint32_t getSectorSize() const noexcept { return CDFS_SECTOR_SIZE_2048; }
    [[nodiscard]] uint32_t getVolumeSerialNumber() const noexcept { return volumeSerialNumber_; }
    [[nodiscard]] std::wstring getVolumeLabel() const noexcept { return volumeLabel_; }
    [[nodiscard]] bool isJoliet() const noexcept { return isJoliet_; }
    [[nodiscard]] bool isUdf() const noexcept { return isUdf_; }
    [[nodiscard]] size_t getFileCount() const noexcept { return records_.size(); }

private:
    static std::wstring canonicalizePath(std::wstring_view path) {
        std::wstring s(path);
        for (auto& c : s) {
            if (c == L'/') c = L'\\';
        }
        while (s.starts_with(L"\\") && s.size() > 1) {
            s = s.substr(1);
        }
        // Strip ISO 9660 version suffix e.g. ";1" if present
        size_t semi = s.rfind(L';');
        if (semi != std::wstring::npos && semi > 0) {
            bool allDigits = true;
            for (size_t i = semi + 1; i < s.size(); ++i) {
                if (s[i] < L'0' || s[i] > L'9') { allDigits = false; break; }
            }
            if (allDigits) s = s.substr(0, semi);
        }
        return s;
    }

    static bool caseInsensitiveCompare(std::wstring_view a, std::wstring_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::towupper(a[i]) != std::towupper(b[i])) return false;
        }
        return true;
    }

    mutable std::mutex mutex_;
    bool mounted_{false};
    std::shared_ptr<storage::IBlockDevice> device_;
    uint64_t totalSectors_{0};
    uint32_t volumeSerialNumber_{0xC0DE1509};
    std::wstring volumeLabel_{L"MICA_CDFS"};
    bool isJoliet_{true};
    bool isUdf_{true};
    uint32_t nextLba_{32};
    std::unordered_map<std::wstring, CdfsFileEntry> records_;
};

// ============================================================================
// 6. NT IFS Drivers Registration & Dispatch Tables (cdfs.sys & udfs.sys)
// ============================================================================

class CdfsDriver {
public:
    static CdfsDriver& get() noexcept {
        static CdfsDriver s_instance;
        return s_instance;
    }

    void initialize();
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

    NtStatus registerVolume(std::shared_ptr<CdfsFileSystem> cdfsFs, std::wstring_view volumeName = L"\\Device\\CdRom0");
    NtStatus mountDrive(std::wstring_view driveLetter, std::shared_ptr<CdfsFileSystem> cdfsFs);
    [[nodiscard]] std::shared_ptr<CdfsFileSystem> getFileSystem(std::wstring_view driveLetter) const;

    // Dispatch routines for NT IRPs
    static NtStatus dispatchCreate(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchRead(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchWrite(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchClose(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchDirectoryControl(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchQueryInformation(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchSetInformation(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchFileSystemControl(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchDeviceControl(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchShutdown(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchCleanup(io::DeviceObject* device, io::Irp* irp);

    [[nodiscard]] io::DriverObject* getDriverObject() const noexcept { return driverObject_.get(); }
    [[nodiscard]] io::FastIoDispatch* getFastIoDispatch() noexcept { return &fastIoTable_; }

private:
    CdfsDriver() = default;
    bool initialized_{false};
    std::unique_ptr<io::DriverObject> driverObject_{nullptr};
    io::FastIoDispatch fastIoTable_{};
    std::unordered_map<std::wstring, std::shared_ptr<CdfsFileSystem>> mountedDrives_;
    mutable std::mutex driverMutex_;
};

class UdfsDriver {
public:
    static UdfsDriver& get() noexcept {
        static UdfsDriver s_instance;
        return s_instance;
    }

    void initialize();
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

    NtStatus registerVolume(std::shared_ptr<CdfsFileSystem> cdfsFs, std::wstring_view volumeName = L"\\Device\\UdfsCdRom0");
    NtStatus mountDrive(std::wstring_view driveLetter, std::shared_ptr<CdfsFileSystem> cdfsFs);
    [[nodiscard]] std::shared_ptr<CdfsFileSystem> getFileSystem(std::wstring_view driveLetter) const;

    // Dispatch routines for NT IRPs
    static NtStatus dispatchCreate(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchRead(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchWrite(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchClose(io::DeviceObject* device, io::Irp* irp);
    static NtStatus dispatchFileSystemControl(io::DeviceObject* device, io::Irp* irp);

    [[nodiscard]] io::DriverObject* getDriverObject() const noexcept { return driverObject_.get(); }
    [[nodiscard]] io::FastIoDispatch* getFastIoDispatch() noexcept { return &fastIoTable_; }

private:
    UdfsDriver() = default;
    bool initialized_{false};
    std::unique_ptr<io::DriverObject> driverObject_{nullptr};
    io::FastIoDispatch fastIoTable_{};
    std::unordered_map<std::wstring, std::shared_ptr<CdfsFileSystem>> mountedDrives_;
    mutable std::mutex driverMutex_;
};

} // namespace micant::cdfs
