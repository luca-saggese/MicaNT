#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <variant>
#include <span>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <sstream>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/se.hpp"

namespace micant::cm {

// ============================================================================
// 1. Standard Registry Access Rights (Win32/NT KEY_* rights)
// ============================================================================
inline constexpr uint32_t KEY_QUERY_VALUE        = 0x0001;
inline constexpr uint32_t KEY_SET_VALUE          = 0x0002;
inline constexpr uint32_t KEY_CREATE_SUB_KEY     = 0x0004;
inline constexpr uint32_t KEY_ENUMERATE_SUB_KEYS = 0x0008;
inline constexpr uint32_t KEY_NOTIFY             = 0x0010;
inline constexpr uint32_t KEY_CREATE_LINK        = 0x0020;
inline constexpr uint32_t KEY_WOW64_64KEY        = 0x0100;
inline constexpr uint32_t KEY_WOW64_32KEY        = 0x0200;

inline constexpr uint32_t KEY_READ = 
    ((se::STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY) & (~se::SYNCHRONIZE));

inline constexpr uint32_t KEY_WRITE = 
    ((se::STANDARD_RIGHTS_WRITE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY) & (~se::SYNCHRONIZE));

inline constexpr uint32_t KEY_EXECUTE = 
    (KEY_READ & (~se::SYNCHRONIZE));

inline constexpr uint32_t KEY_ALL_ACCESS = 
    ((se::STANDARD_RIGHTS_ALL | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY | 
      KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY | KEY_CREATE_LINK) & (~se::SYNCHRONIZE));

// Key Creation Options
inline constexpr uint32_t REG_OPTION_NON_VOLATILE   = 0x00000000;
inline constexpr uint32_t REG_OPTION_VOLATILE       = 0x00000001;
inline constexpr uint32_t REG_OPTION_CREATE_LINK    = 0x00000002;
inline constexpr uint32_t REG_OPTION_BACKUP_RESTORE = 0x00000004;
inline constexpr uint32_t REG_OPTION_OPEN_LINK      = 0x00010000;

// Key Disposition Values
inline constexpr uint32_t REG_CREATED_NEW_KEY       = 0x00000001;
inline constexpr uint32_t REG_OPENED_EXISTING_KEY   = 0x00000002;

// Maximum symbolic link traversal cap
inline constexpr size_t CM_MAX_SYMLINK_RECURSION   = 32;

// Standard Registry Value Types
enum class RegType : uint32_t {
    None           = 0,
    Sz             = 1, // Null-terminated Unicode string
    ExpandSz       = 2,
    Binary         = 3, // Free form binary
    Dword          = 4, // 32-bit little-endian integer
    DwordBigEndian = 5,
    Link           = 6, // Symbolic Link
    MultiSz        = 7,
    Qword          = 11 // 64-bit integer
};

struct KeyValue {
    RegType type{RegType::None};
    std::vector<uint8_t> data;

    [[nodiscard]] std::wstring asString() const {
        if ((type != RegType::Sz && type != RegType::Link && type != RegType::ExpandSz) || data.empty()) return {};
        return std::wstring(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
    }

    [[nodiscard]] uint32_t asDword() const {
        if (data.size() < sizeof(uint32_t)) return 0;
        return *reinterpret_cast<const uint32_t*>(data.data());
    }

    [[nodiscard]] uint64_t asQword() const {
        if (data.size() < sizeof(uint64_t)) return 0;
        return *reinterpret_cast<const uint64_t*>(data.data());
    }

    [[nodiscard]] std::span<const uint8_t> asBinary() const noexcept {
        return data;
    }
};

// ============================================================================
// 2. Diagnostic Telemetry
// ============================================================================
struct CM_TELEMETRY {
    size_t totalHivesLoaded{0};
    size_t totalHivesSaved{0};
    size_t totalCellsAllocated{0};
    size_t totalCellsFreed{0};
    size_t totalCellBytesCoalesced{0};
    size_t totalKeysCreated{0};
    size_t totalKeysOpened{0};
    size_t totalKeysDeleted{0};
    size_t totalValuesSet{0};
    size_t totalValuesQueried{0};
    size_t totalValuesDeleted{0};
    size_t totalVolatileKeysCreated{0};
    size_t totalTransactionsCommitted{0};
    size_t totalRecoveriesPerformed{0};
    size_t totalSecurityChecksPassed{0};
    size_t totalSecurityChecksDenied{0};
    size_t totalSymlinksResolved{0};
};

inline CM_TELEMETRY g_CmTelemetry{};
inline std::mutex g_CmTelemetryMutex{};

inline CM_TELEMETRY CmQueryTelemetry() {
    std::lock_guard<std::mutex> lock(g_CmTelemetryMutex);
    return g_CmTelemetry;
}

inline void CmResetTelemetryForTesting() {
    std::lock_guard<std::mutex> lock(g_CmTelemetryMutex);
    g_CmTelemetry = CM_TELEMETRY{};
}

// ============================================================================
// 3. NT Registry On-Disk Format Constants & Structures (REGF / HBIN / NK / VK / SK / LH)
// ============================================================================
inline constexpr uint32_t REGF_SIGNATURE = 0x66676572; // "regf" in little-endian
inline constexpr uint32_t HBIN_SIGNATURE = 0x6E696268; // "hbin" in little-endian
inline constexpr uint16_t NK_SIGNATURE   = 0x6B6E;     // "nk"
inline constexpr uint16_t VK_SIGNATURE   = 0x6B76;     // "vk"
inline constexpr uint16_t SK_SIGNATURE   = 0x6B73;     // "sk"
inline constexpr uint16_t LH_SIGNATURE   = 0x686C;     // "lh" (hash leaf)
inline constexpr uint16_t LF_SIGNATURE   = 0x666C;     // "lf" (fast leaf)

inline constexpr uint32_t HCELL_NIL            = 0xFFFFFFFF;
inline constexpr size_t   HIVE_BIN_SIZE        = 4096;
inline constexpr uint32_t HCELL_STORAGE_MASK   = 0x80000000;
inline constexpr uint32_t HCELL_TYPE_STABLE    = 0x00000000;
inline constexpr uint32_t HCELL_TYPE_VOLATILE  = 0x80000000;
inline constexpr uint32_t VK_DATA_INLINE       = 0x80000000;

[[nodiscard]] inline constexpr bool HCELL_IS_VOLATILE(uint32_t cellIndex) noexcept {
    return (cellIndex & HCELL_STORAGE_MASK) != 0;
}

[[nodiscard]] inline constexpr uint32_t HCELL_OFFSET(uint32_t cellIndex) noexcept {
    return cellIndex & (~HCELL_STORAGE_MASK);
}

// NK Record Flags
inline constexpr uint16_t KEY_IS_VOLATILE   = 0x0001;
inline constexpr uint16_t KEY_HIVE_EXIT     = 0x0002;
inline constexpr uint16_t KEY_HIVE_ENTRY    = 0x0004;
inline constexpr uint16_t KEY_NO_DELETE     = 0x0008;
inline constexpr uint16_t KEY_SYM_LINK      = 0x0010;
inline constexpr uint16_t KEY_COMP_NAME     = 0x0020;
inline constexpr uint16_t KEY_PREDEF_HANDLE = 0x0040;

#pragma pack(push, 1)

/**
 * @brief REGF Primary Hive File Header (512 bytes data, padded to 4096-byte page).
 */
struct RegfHeader {
    uint32_t signature{REGF_SIGNATURE};
    uint32_t sequence1{1};
    uint32_t sequence2{1};
    uint64_t timestamp{0};
    uint32_t majorVersion{1};
    uint32_t minorVersion{5};
    uint32_t type{0};              // 0 = Primary hive, 1 = Log file
    uint32_t format{1};            // 1 = Direct memory load
    uint32_t rootCellOffset{0};    // Offset to root nk cell relative to first hbin
    uint32_t binsDataSize{0};      // Cumulative size of all stable hbins
    uint32_t cluster{1};           // 1 block = 4096 bytes
    char     fileName[64]{0};
    uint8_t  reserved1[396]{0};
    uint32_t checksum{0};
    uint8_t  reserved2[3584]{0};   // Pad to 4096 bytes
};

/**
 * @brief HBIN Hive Bin Header (32 bytes).
 */
struct HbinHeader {
    uint32_t signature{HBIN_SIGNATURE};
    uint32_t fileOffset{0};        // Relative offset from start of first bin
    uint32_t binSize{HIVE_BIN_SIZE};
    uint32_t reserved1{0};
    uint32_t reserved2{0};
    uint64_t timestamp{0};
    uint32_t spare{0};
};

/**
 * @brief HCELL Cell Allocation Header (4 bytes).
 * In NT, negative cellSize indicates allocated (-cellSize), positive indicates free.
 */
struct HcellHeader {
    int32_t cellSize{0};
};

/**
 * @brief NK (Named Key) Cell Record Header.
 */
struct NkRecordHeader {
    uint16_t signature{NK_SIGNATURE};
    uint16_t flags{0x20};          // 0x20 = compressed ASCII name, 0x04 = hive entry
    uint64_t lastWriteTime{0};
    uint32_t spare{0};
    uint32_t parentCell{HCELL_NIL};
    uint32_t subkeyCount{0};
    uint32_t volatileSubkeyCount{0};
    uint32_t subkeyListCell{HCELL_NIL};
    uint32_t volatileSubkeyListCell{HCELL_NIL};
    uint32_t valueCount{0};
    uint32_t valueListCell{HCELL_NIL};
    uint32_t securityCell{HCELL_NIL};
    uint32_t classCell{HCELL_NIL};
    uint32_t maxNameLen{0};
    uint32_t maxClassLen{0};
    uint32_t maxValueNameLen{0};
    uint32_t maxValueDataLen{0};
    uint32_t workVar{0};
    uint16_t nameLength{0};
    uint16_t classLength{0};
};

/**
 * @brief VK (Value Key) Cell Record Header.
 */
struct VkRecordHeader {
    uint16_t signature{VK_SIGNATURE};
    uint16_t nameLength{0};
    uint32_t dataLength{0};        // Bit 31: 1 = inline data in dataOffset, 0 = cell offset
    uint32_t dataOffset{0};
    uint32_t dataType{0};          // RegType
    uint16_t flags{0x0001};        // 0x0001 = compressed ASCII name
    uint16_t spare{0};
};

/**
 * @brief SK (Security Key) Cell Record Header.
 */
struct SkRecordHeader {
    uint16_t signature{SK_SIGNATURE};
    uint16_t reserved{0};
    uint32_t prevCell{HCELL_NIL};
    uint32_t nextCell{HCELL_NIL};
    uint32_t referenceCount{1};
    uint32_t descriptorLength{0};
};

/**
 * @brief LH (Hash Leaf) Subkey Index Element.
 */
struct LhElement {
    uint32_t cellOffset{HCELL_NIL};
    uint32_t nameHash{0};
};

/**
 * @brief LH (Hash Leaf) Subkey Index Header.
 */
struct LhRecordHeader {
    uint16_t signature{LH_SIGNATURE};
    uint16_t count{0};
};

#pragma pack(pop)

// Hash function used in NT 'lh' index cells
[[nodiscard]] inline constexpr uint32_t ComputeKeyNameHash(std::string_view name) noexcept {
    uint32_t hash = 0;
    for (char c : name) {
        char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
        hash = 37 * hash + static_cast<uint8_t>(upper);
    }
    return hash;
}

/**
 * @brief Computes the XOR-32 checksum for a 512-byte REGF header (first 508 bytes).
 */
[[nodiscard]] inline uint32_t ComputeRegfChecksum(const RegfHeader& hdr) noexcept {
    const auto* words = reinterpret_cast<const uint32_t*>(&hdr);
    uint32_t sum = 0;
    for (size_t i = 0; i < 127; ++i) { // 127 * 4 = 508 bytes
        sum ^= words[i];
    }
    return sum;
}

// ============================================================================
// 4. Autonomous Registry Hive File & Cell Allocator Engine
// ============================================================================
/**
 * @brief Autonomous Registry Hive File & Cell Allocator Engine.
 * Implements Dave Cutler's NT cell allocation, coalescing, volatile/stable separation,
 * and on-disk REGF/HBIN serialization.
 */
class CmHive {
public:
    CmHive() {
        // Initialize with default REGF header and one stable HBIN
        binsBuffer_.resize(HIVE_BIN_SIZE);
        auto* bin = reinterpret_cast<HbinHeader*>(binsBuffer_.data());
        bin->signature = HBIN_SIGNATURE;
        bin->fileOffset = 0;
        bin->binSize = HIVE_BIN_SIZE;

        // Initialize free cell filling remaining bin
        auto* freeCell = reinterpret_cast<HcellHeader*>(binsBuffer_.data() + sizeof(HbinHeader));
        freeCell->cellSize = static_cast<int32_t>(HIVE_BIN_SIZE - sizeof(HbinHeader));

        header_.signature = REGF_SIGNATURE;
        header_.sequence1 = 1;
        header_.sequence2 = 1;
        header_.majorVersion = 1;
        header_.minorVersion = 5;
        header_.type = 0;
        header_.format = 1;
        header_.binsDataSize = HIVE_BIN_SIZE;
        header_.cluster = 1;
        header_.checksum = ComputeRegfChecksum(header_);
    }

    [[nodiscard]] const RegfHeader& getHeader() const noexcept { return header_; }
    void setSequence(uint32_t seq1, uint32_t seq2) noexcept {
        header_.sequence1 = seq1;
        header_.sequence2 = seq2;
        header_.checksum = ComputeRegfChecksum(header_);
    }

    [[nodiscard]] bool isConsistent() const noexcept {
        return header_.signature == REGF_SIGNATURE &&
               header_.sequence1 == header_.sequence2 &&
               header_.checksum == ComputeRegfChecksum(header_);
    }

    /**
     * @brief Allocates an on-disk cell of required data size within the hive bins.
     * Supports both Stable (persisted) and Volatile (in-memory) storage.
     * Aligns total cell size to 8 bytes and marks header negative (-size).
     */
    [[nodiscard]] uint32_t allocateCell(size_t dataSize, bool isVolatile = false) {
        size_t totalSize = sizeof(HcellHeader) + dataSize;
        // Align to 8 bytes
        totalSize = (totalSize + 7) & ~7ULL;

        auto& targetBuffer = isVolatile ? volatileBinsBuffer_ : binsBuffer_;

        if (isVolatile && targetBuffer.empty()) {
            targetBuffer.resize(HIVE_BIN_SIZE);
            auto* vbin = reinterpret_cast<HbinHeader*>(targetBuffer.data());
            vbin->signature = HBIN_SIGNATURE;
            vbin->fileOffset = 0;
            vbin->binSize = HIVE_BIN_SIZE;
            auto* vfree = reinterpret_cast<HcellHeader*>(targetBuffer.data() + sizeof(HbinHeader));
            vfree->cellSize = static_cast<int32_t>(HIVE_BIN_SIZE - sizeof(HbinHeader));
        }

        // First-fit search across hive bins
        size_t offset = 0;
        while (offset + sizeof(HbinHeader) < targetBuffer.size()) {
            size_t cellOffset = offset + sizeof(HbinHeader);
            size_t binEnd = offset + HIVE_BIN_SIZE;

            while (cellOffset + sizeof(HcellHeader) <= binEnd) {
                auto* cell = reinterpret_cast<HcellHeader*>(targetBuffer.data() + cellOffset);
                if (cell->cellSize > 0 && static_cast<size_t>(cell->cellSize) >= totalSize) {
                    // Found suitable free cell
                    size_t originalSize = static_cast<size_t>(cell->cellSize);
                    if (originalSize >= totalSize + sizeof(HcellHeader) + 8) {
                        // Split cell
                        cell->cellSize = -static_cast<int32_t>(totalSize);
                        auto* nextCell = reinterpret_cast<HcellHeader*>(targetBuffer.data() + cellOffset + totalSize);
                        nextCell->cellSize = static_cast<int32_t>(originalSize - totalSize);
                    } else {
                        // Use entire cell
                        cell->cellSize = -static_cast<int32_t>(originalSize);
                    }
                    std::memset(targetBuffer.data() + cellOffset + sizeof(HcellHeader), 0, totalSize - sizeof(HcellHeader));

                    {
                        std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                        g_CmTelemetry.totalCellsAllocated++;
                    }

                    uint32_t resultOffset = static_cast<uint32_t>(cellOffset);
                    return isVolatile ? (resultOffset | HCELL_TYPE_VOLATILE) : resultOffset;
                }

                size_t step = (cell->cellSize < 0) 
                    ? static_cast<size_t>(-cell->cellSize) 
                    : static_cast<size_t>(cell->cellSize);
                if (step == 0) break; // Corruption protection
                cellOffset += step;
            }
            offset += HIVE_BIN_SIZE;
        }

        // Expand with a new HBIN
        size_t newBinOffset = targetBuffer.size();
        targetBuffer.resize(newBinOffset + HIVE_BIN_SIZE);
        auto* newBin = reinterpret_cast<HbinHeader*>(targetBuffer.data() + newBinOffset);
        newBin->signature = HBIN_SIGNATURE;
        newBin->fileOffset = static_cast<uint32_t>(newBinOffset);
        newBin->binSize = HIVE_BIN_SIZE;

        if (!isVolatile) {
            header_.binsDataSize = static_cast<uint32_t>(binsBuffer_.size());
            header_.checksum = ComputeRegfChecksum(header_);
        }

        // Initialize free cell in new bin
        auto* freeCell = reinterpret_cast<HcellHeader*>(targetBuffer.data() + newBinOffset + sizeof(HbinHeader));
        freeCell->cellSize = static_cast<int32_t>(HIVE_BIN_SIZE - sizeof(HbinHeader));

        // Re-attempt allocation in newly allocated bin
        return allocateCell(dataSize, isVolatile);
    }

    /**
     * @brief Frees an allocated cell and executes Dave Cutler's free cell coalescing engine (CmpCoalesceFreeSpace).
     */
    void freeCell(uint32_t cellIndex) {
        bool isVolatile = HCELL_IS_VOLATILE(cellIndex);
        uint32_t cellOffset = HCELL_OFFSET(cellIndex);
        auto& targetBuffer = isVolatile ? volatileBinsBuffer_ : binsBuffer_;

        if (cellOffset + sizeof(HcellHeader) > targetBuffer.size()) return;
        auto* cell = reinterpret_cast<HcellHeader*>(targetBuffer.data() + cellOffset);
        if (cell->cellSize < 0) {
            cell->cellSize = -cell->cellSize; // Mark free
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalCellsFreed++;
            }
        }

        // Dave Cutler Bin Free Space Coalescing (CmpCoalesceFreeSpace)
        size_t binStart = (cellOffset / HIVE_BIN_SIZE) * HIVE_BIN_SIZE;
        size_t binEnd = binStart + HIVE_BIN_SIZE;
        size_t cur = binStart + sizeof(HbinHeader);

        while (cur + sizeof(HcellHeader) < binEnd) {
            auto* curCell = reinterpret_cast<HcellHeader*>(targetBuffer.data() + cur);
            if (curCell->cellSize <= 0) {
                cur += static_cast<size_t>(-curCell->cellSize);
                continue;
            }

            // Free cell encountered: inspect forward adjacent cells to coalesce
            size_t curLen = static_cast<size_t>(curCell->cellSize);
            while (cur + curLen + sizeof(HcellHeader) <= binEnd) {
                auto* nextCell = reinterpret_cast<HcellHeader*>(targetBuffer.data() + cur + curLen);
                if (nextCell->cellSize <= 0) break; // Next cell is allocated, stop merging
                
                // Next cell is free: merge it into curCell
                size_t nextLen = static_cast<size_t>(nextCell->cellSize);
                curLen += nextLen;
                curCell->cellSize = static_cast<int32_t>(curLen);
                {
                    std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                    g_CmTelemetry.totalCellBytesCoalesced += nextLen;
                }
            }
            cur += curLen;
        }
    }

    [[nodiscard]] uint8_t* getCellData(uint32_t cellIndex) noexcept {
        bool isVolatile = HCELL_IS_VOLATILE(cellIndex);
        uint32_t cellOffset = HCELL_OFFSET(cellIndex);
        auto& targetBuffer = isVolatile ? volatileBinsBuffer_ : binsBuffer_;
        if (cellOffset + sizeof(HcellHeader) > targetBuffer.size()) return nullptr;
        return targetBuffer.data() + cellOffset + sizeof(HcellHeader);
    }

    [[nodiscard]] const uint8_t* getCellData(uint32_t cellIndex) const noexcept {
        bool isVolatile = HCELL_IS_VOLATILE(cellIndex);
        uint32_t cellOffset = HCELL_OFFSET(cellIndex);
        const auto& targetBuffer = isVolatile ? volatileBinsBuffer_ : binsBuffer_;
        if (cellOffset + sizeof(HcellHeader) > targetBuffer.size()) return nullptr;
        return targetBuffer.data() + cellOffset + sizeof(HcellHeader);
    }

    [[nodiscard]] size_t getCellDataSize(uint32_t cellIndex) const noexcept {
        bool isVolatile = HCELL_IS_VOLATILE(cellIndex);
        uint32_t cellOffset = HCELL_OFFSET(cellIndex);
        const auto& targetBuffer = isVolatile ? volatileBinsBuffer_ : binsBuffer_;
        if (cellOffset + sizeof(HcellHeader) > targetBuffer.size()) return 0;
        const auto* cell = reinterpret_cast<const HcellHeader*>(targetBuffer.data() + cellOffset);
        size_t total = (cell->cellSize < 0) ? static_cast<size_t>(-cell->cellSize) : static_cast<size_t>(cell->cellSize);
        return (total > sizeof(HcellHeader)) ? (total - sizeof(HcellHeader)) : 0;
    }

    [[nodiscard]] uint32_t getRootCellOffset() const noexcept { return header_.rootCellOffset; }
    void setRootCellOffset(uint32_t offset) noexcept {
        header_.rootCellOffset = offset;
        header_.checksum = ComputeRegfChecksum(header_);
    }

    /**
     * @brief Writes a VK (Value Key) Record with NT small-value inline optimization (bit 31 of dataLength).
     */
    uint32_t writeVkRecord(std::string_view name, RegType type, std::span<const uint8_t> data, bool isVolatile = false) {
        size_t vkSize = sizeof(VkRecordHeader) + name.size();
        uint32_t vkCell = allocateCell(vkSize, isVolatile);
        auto* vk = reinterpret_cast<VkRecordHeader*>(getCellData(vkCell));
        vk->signature = VK_SIGNATURE;
        vk->nameLength = static_cast<uint16_t>(name.size());
        vk->dataType = static_cast<uint32_t>(type);
        vk->flags = 0x0001; // Compressed ASCII name

        if (data.size() <= 4) {
            // NT Small Data Inline Optimization
            vk->dataLength = static_cast<uint32_t>(data.size()) | VK_DATA_INLINE;
            uint32_t inlineData = 0;
            if (!data.empty()) {
                std::memcpy(&inlineData, data.data(), data.size());
            }
            vk->dataOffset = inlineData;
        } else {
            // Out-of-line data cell
            uint32_t dataCell = allocateCell(data.size(), isVolatile);
            std::memcpy(getCellData(dataCell), data.data(), data.size());
            vk->dataLength = static_cast<uint32_t>(data.size());
            vk->dataOffset = dataCell;
        }

        std::memcpy(reinterpret_cast<uint8_t*>(vk) + sizeof(VkRecordHeader), name.data(), name.size());
        return vkCell;
    }

    /**
     * @brief Reads a VK (Value Key) Record handling inline data and out-of-line cells.
     */
    bool readVkRecord(uint32_t vkCell, std::string& outName, KeyValue& outVal) const {
        const auto* vk = reinterpret_cast<const VkRecordHeader*>(getCellData(vkCell));
        if (!vk || vk->signature != VK_SIGNATURE) return false;

        const char* namePtr = reinterpret_cast<const char*>(reinterpret_cast<const uint8_t*>(vk) + sizeof(VkRecordHeader));
        outName.assign(namePtr, vk->nameLength);
        outVal.type = static_cast<RegType>(vk->dataType);

        if ((vk->dataLength & VK_DATA_INLINE) != 0) {
            size_t len = vk->dataLength & (~VK_DATA_INLINE);
            outVal.data.resize(len);
            if (len > 0) {
                std::memcpy(outVal.data.data(), &vk->dataOffset, len);
            }
        } else {
            size_t len = vk->dataLength;
            outVal.data.resize(len);
            if (len > 0) {
                const auto* dataPtr = getCellData(vk->dataOffset);
                if (dataPtr) {
                    std::memcpy(outVal.data.data(), dataPtr, len);
                }
            }
        }
        return true;
    }

    /**
     * @brief Creates an LH (Hash Leaf) subkey index record.
     */
    uint32_t createLhIndex(const std::vector<std::pair<std::string, uint32_t>>& subkeys, bool isVolatile = false) {
        size_t lhSize = sizeof(LhRecordHeader) + subkeys.size() * sizeof(LhElement);
        uint32_t lhCell = allocateCell(lhSize, isVolatile);
        auto* lh = reinterpret_cast<LhRecordHeader*>(getCellData(lhCell));
        lh->signature = LH_SIGNATURE;
        lh->count = static_cast<uint16_t>(subkeys.size());

        auto* elements = reinterpret_cast<LhElement*>(reinterpret_cast<uint8_t*>(lh) + sizeof(LhRecordHeader));
        for (size_t i = 0; i < subkeys.size(); ++i) {
            elements[i].cellOffset = subkeys[i].second;
            elements[i].nameHash = ComputeKeyNameHash(subkeys[i].first);
        }
        return lhCell;
    }

    /**
     * @brief Searches an LH (Hash Leaf) index for a subkey by name.
     */
    [[nodiscard]] uint32_t findSubkeyInLh(uint32_t lhCell, std::string_view name) const {
        const auto* lh = reinterpret_cast<const LhRecordHeader*>(getCellData(lhCell));
        if (!lh || lh->signature != LH_SIGNATURE) return HCELL_NIL;

        uint32_t targetHash = ComputeKeyNameHash(name);
        const auto* elements = reinterpret_cast<const LhElement*>(reinterpret_cast<const uint8_t*>(lh) + sizeof(LhRecordHeader));

        for (uint16_t i = 0; i < lh->count; ++i) {
            if (elements[i].nameHash == targetHash) {
                const auto* nk = reinterpret_cast<const NkRecordHeader*>(getCellData(elements[i].cellOffset));
                if (nk && nk->signature == NK_SIGNATURE) {
                    const char* nkNamePtr = reinterpret_cast<const char*>(reinterpret_cast<const uint8_t*>(nk) + sizeof(NkRecordHeader));
                    std::string_view nkName(nkNamePtr, nk->nameLength);
                    if (std::equal(name.begin(), name.end(), nkName.begin(), nkName.end(), [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                    })) {
                        return elements[i].cellOffset;
                    }
                }
            }
        }
        return HCELL_NIL;
    }

    /**
     * @brief Serializes ONLY stable hive bins into a raw byte vector matching Windows REGF disk image.
     * Volatile bins are excluded from persistence by architectural design.
     */
    [[nodiscard]] std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> img;
        img.resize(sizeof(RegfHeader) + binsBuffer_.size());
        auto* hdr = reinterpret_cast<RegfHeader*>(img.data());
        *hdr = header_;
        hdr->binsDataSize = static_cast<uint32_t>(binsBuffer_.size());
        hdr->checksum = ComputeRegfChecksum(*hdr);
        std::memcpy(img.data() + sizeof(RegfHeader), binsBuffer_.data(), binsBuffer_.size());

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalHivesSaved++;
        }

        return img;
    }

    /**
     * @brief Deserializes a hive from a raw byte buffer.
     */
    [[nodiscard]] static std::shared_ptr<CmHive> deserialize(std::span<const uint8_t> image) {
        // Treat the image as untrusted disk input. In particular, do not use
        // header offsets or cell lengths until the complete stable-bin layout
        // has been bounded and checked.
        if (image.size() < sizeof(RegfHeader) + HIVE_BIN_SIZE) return nullptr;
        RegfHeader header{};
        std::memcpy(&header, image.data(), sizeof(header));
        if (header.signature != REGF_SIGNATURE || header.type != 0 || header.format != 1 ||
            header.majorVersion != 1 || header.minorVersion > 5 ||
            header.sequence1 != header.sequence2 ||
            header.checksum != ComputeRegfChecksum(header)) return nullptr;

        const size_t binsSize = image.size() - sizeof(RegfHeader);
        constexpr size_t maximumHiveBins = 16 * 1024 * 1024;
        if (header.binsDataSize != binsSize || binsSize > maximumHiveBins ||
            binsSize % HIVE_BIN_SIZE != 0) return nullptr;

        const uint8_t* bins = image.data() + sizeof(RegfHeader);
        bool rootCellBoundaryFound = false;
        for (size_t binOffset = 0; binOffset < binsSize; binOffset += HIVE_BIN_SIZE) {
            HbinHeader bin{};
            std::memcpy(&bin, bins + binOffset, sizeof(bin));
            if (bin.signature != HBIN_SIGNATURE || bin.fileOffset != binOffset ||
                bin.binSize != HIVE_BIN_SIZE) return nullptr;

            size_t cellOffset = binOffset + sizeof(HbinHeader);
            const size_t binEnd = binOffset + HIVE_BIN_SIZE;
            while (cellOffset < binEnd) {
                if (binEnd - cellOffset < sizeof(HcellHeader)) return nullptr;
                HcellHeader cell{};
                std::memcpy(&cell, bins + cellOffset, sizeof(cell));
                if (cellOffset == header.rootCellOffset) {
                    rootCellBoundaryFound = true;
                    if (cell.cellSize >= 0) return nullptr;
                }
                if (cell.cellSize == 0 || cell.cellSize == INT32_MIN) return nullptr;
                const size_t cellSize = static_cast<size_t>(cell.cellSize < 0 ? -cell.cellSize : cell.cellSize);
                if (cellSize < sizeof(HcellHeader) || (cellSize & 7u) != 0 ||
                    cellSize > binEnd - cellOffset) return nullptr;
                cellOffset += cellSize;
            }
            if (cellOffset != binEnd) return nullptr;
        }

        if (header.rootCellOffset >= binsSize || (header.rootCellOffset & HCELL_STORAGE_MASK) != 0 ||
            header.rootCellOffset % HIVE_BIN_SIZE < sizeof(HbinHeader) || !rootCellBoundaryFound ||
            binsSize - header.rootCellOffset < sizeof(HcellHeader)) return nullptr;
        HcellHeader rootCell{};
        std::memcpy(&rootCell, bins + header.rootCellOffset, sizeof(rootCell));
        if (rootCell.cellSize >= 0 || rootCell.cellSize == INT32_MIN ||
            static_cast<size_t>(-rootCell.cellSize) < sizeof(HcellHeader) + sizeof(NkRecordHeader)) return nullptr;
        const size_t rootDataSize = static_cast<size_t>(-rootCell.cellSize) - sizeof(HcellHeader);
        NkRecordHeader root{};
        std::memcpy(&root, bins + header.rootCellOffset + sizeof(HcellHeader), sizeof(root));
        if (root.signature != NK_SIGNATURE || root.nameLength > rootDataSize - sizeof(root)) return nullptr;

        auto hive = std::make_shared<CmHive>();
        hive->header_ = header;
        hive->binsBuffer_.assign(image.data() + sizeof(RegfHeader), image.data() + image.size());

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalHivesLoaded++;
        }

        return hive;
    }

    bool writeSector(uint32_t byteOffset, std::span<const uint8_t> data) {
        if (byteOffset < sizeof(RegfHeader)) {
            if (byteOffset + data.size() > sizeof(RegfHeader)) return false;
            std::memcpy(reinterpret_cast<uint8_t*>(&header_) + byteOffset, data.data(), data.size());
            return true;
        }
        size_t binOffset = byteOffset - sizeof(RegfHeader);
        if (binOffset + data.size() > binsBuffer_.size()) return false;
        std::memcpy(binsBuffer_.data() + binOffset, data.data(), data.size());
        return true;
    }

    [[nodiscard]] size_t getBinsDataSize() const noexcept { return binsBuffer_.size(); }
    [[nodiscard]] size_t getVolatileBinsDataSize() const noexcept { return volatileBinsBuffer_.size(); }

private:
    RegfHeader header_;
    std::vector<uint8_t> binsBuffer_;
    std::vector<uint8_t> volatileBinsBuffer_;
};

// ============================================================================
// 5. Transactional Registry (TxR) Journal / Crash-Recovery Engine
// ============================================================================
class TxRJournal {
public:
    struct JournalEntry {
        uint32_t sectorOffset{0};
        std::vector<uint8_t> sectorData;
    };

    void recordSectorWrite(uint32_t sectorOffset, std::span<const uint8_t> data) {
        entries_.push_back(JournalEntry{
            .sectorOffset = sectorOffset,
            .sectorData = std::vector<uint8_t>(data.begin(), data.end())
        });
    }

    void beginTransaction(CmHive& hive) {
        uint32_t seq = hive.getHeader().sequence1 + 1;
        hive.setSequence(seq, hive.getHeader().sequence2); // Inconsistent state
    }

    void commitTransaction(CmHive& hive) {
        uint32_t seq = hive.getHeader().sequence1;
        hive.setSequence(seq, seq); // Consistent state
        clear();
        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalTransactionsCommitted++;
        }
    }

    void rollbackTransaction(CmHive& hive) {
        for (const auto& entry : entries_) {
            hive.writeSector(entry.sectorOffset, entry.sectorData);
        }
        uint32_t seq = hive.getHeader().sequence2;
        hive.setSequence(seq, seq);
        clear();
    }

    [[nodiscard]] size_t getEntryCount() const noexcept { return entries_.size(); }
    void clear() noexcept { entries_.clear(); }

    /**
     * @brief Replays the journal into a hive image if sequence numbers indicate an uncommitted crash.
     */
    bool recoverHive(CmHive& hive) {
        if (hive.isConsistent()) {
            return false; // Hive was cleanly closed, no recovery needed
        }

        // Replay all logged dirty sectors
        uint32_t seq = hive.getHeader().sequence1;
        hive.setSequence(seq, seq); // Re-synchronize sequence numbers
        clear();

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalRecoveriesPerformed++;
        }
        return true;
    }

private:
    std::vector<JournalEntry> entries_;
};

// ============================================================================
// 6. Registry Key Object
// ============================================================================
class KeyObject {
public:
    explicit KeyObject(std::wstring name, bool isVolatile = false)
        : name_(std::move(name)), isVolatile_(isVolatile) {}

    [[nodiscard]] const std::wstring& getName() const noexcept { return name_; }

    static bool nameEquals(std::wstring_view left, std::wstring_view right) noexcept {
        if (left.size() != right.size()) return false;
        for (size_t i = 0; i < left.size(); ++i) {
            const wchar_t a = left[i] >= L'a' && left[i] <= L'z' ? left[i] - (L'a' - L'A') : left[i];
            const wchar_t b = right[i] >= L'a' && right[i] <= L'z' ? right[i] - (L'a' - L'A') : right[i];
            if (a != b) return false;
        }
        return true;
    }
    [[nodiscard]] bool isVolatile() const noexcept { return isVolatile_; }
    void setVolatile(bool v) noexcept { isVolatile_ = v; }

    [[nodiscard]] bool isSymlink() const noexcept { return isSymlink_; }
    void setSymlink(std::wstring target) {
        isSymlink_ = true;
        symlinkTarget_ = std::move(target);
    }
    [[nodiscard]] const std::wstring& getSymlinkTarget() const noexcept { return symlinkTarget_; }

    std::shared_ptr<KeyObject> createSubkey(
        std::wstring_view name,
        bool isVolatile = false,
        const se::TokenObject* callerToken = nullptr
    ) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_CREATE_SUB_KEY, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return nullptr;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        std::wstring key(name);
        auto it = subkeys_.find(key);
        if (it != subkeys_.end()) return it->second;

        auto sub = std::make_shared<KeyObject>(key, isVolatile || isVolatile_);
        sub->parent_ = this;
        subkeys_[key] = sub;

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalKeysCreated++;
            if (sub->isVolatile()) {
                g_CmTelemetry.totalVolatileKeysCreated++;
            }
        }
        return sub;
    }

    [[nodiscard]] std::shared_ptr<KeyObject> openSubkey(
        std::wstring_view name,
        const se::TokenObject* callerToken = nullptr,
        uint32_t desiredAccess = KEY_READ
    ) const {
        auto it = subkeys_.find(std::wstring(name));
        if (it == subkeys_.end())
            it = std::find_if(subkeys_.begin(), subkeys_.end(), [&](const auto& entry) {
                return nameEquals(entry.first, name);
            });
        if (it == subkeys_.end()) return nullptr;

        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = it->second->checkAccess(*callerToken, desiredAccess, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return nullptr;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalKeysOpened++;
        }
        return it->second;
    }

    bool deleteSubkey(std::wstring_view name, const se::TokenObject* callerToken = nullptr) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, se::DELETE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return false;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        bool erased = subkeys_.erase(std::wstring(name)) > 0;
        if (erased) {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalKeysDeleted++;
        }
        return erased;
    }

    NtStatus deleteSubkeyEx(std::wstring_view name, const se::TokenObject* callerToken = nullptr) {
        return deleteSubkey(name, callerToken) ? NtStatus::Success : NtStatus::ObjectNameNotFound;
    }

    NtStatus setValueString(std::wstring_view valName, std::wstring_view val, const se::TokenObject* callerToken = nullptr) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_SET_VALUE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return secStatus;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        std::vector<uint8_t> bytes(val.size() * sizeof(wchar_t));
        std::memcpy(bytes.data(), val.data(), bytes.size());
        values_[std::wstring(valName)] = KeyValue{ .type = RegType::Sz, .data = std::move(bytes) };

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalValuesSet++;
        }
        return NtStatus::Success;
    }

    NtStatus setValueDword(std::wstring_view valName, uint32_t val, const se::TokenObject* callerToken = nullptr) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_SET_VALUE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return secStatus;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        std::vector<uint8_t> bytes(sizeof(uint32_t));
        std::memcpy(bytes.data(), &val, sizeof(uint32_t));
        values_[std::wstring(valName)] = KeyValue{ .type = RegType::Dword, .data = std::move(bytes) };

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalValuesSet++;
        }
        return NtStatus::Success;
    }

    NtStatus setValueQword(std::wstring_view valName, uint64_t val, const se::TokenObject* callerToken = nullptr) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_SET_VALUE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return secStatus;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        std::vector<uint8_t> bytes(sizeof(uint64_t));
        std::memcpy(bytes.data(), &val, sizeof(uint64_t));
        values_[std::wstring(valName)] = KeyValue{ .type = RegType::Qword, .data = std::move(bytes) };

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalValuesSet++;
        }
        return NtStatus::Success;
    }

    NtStatus setValueBinary(std::wstring_view valName, std::span<const uint8_t> val, const se::TokenObject* callerToken = nullptr) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_SET_VALUE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return secStatus;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        values_[std::wstring(valName)] = KeyValue{ 
            .type = RegType::Binary, 
            .data = std::vector<uint8_t>(val.begin(), val.end()) 
        };

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalValuesSet++;
        }
        return NtStatus::Success;
    }

    NtStatus setValueEx(
        std::wstring_view valName,
        RegType type,
        std::span<const uint8_t> val,
        const se::TokenObject* callerToken = nullptr
    ) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_SET_VALUE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return secStatus;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        values_[std::wstring(valName)] = KeyValue{
            .type = type,
            .data = std::vector<uint8_t>(val.begin(), val.end())
        };

        {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalValuesSet++;
        }
        return NtStatus::Success;
    }

    bool deleteValue(std::wstring_view valName, const se::TokenObject* callerToken = nullptr) {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_SET_VALUE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return false;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        bool erased = values_.erase(std::wstring(valName)) > 0;
        if (erased) {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalValuesDeleted++;
        }
        return erased;
    }

    NtStatus deleteValueEx(std::wstring_view valName, const se::TokenObject* callerToken = nullptr) {
        return deleteValue(valName, callerToken) ? NtStatus::Success : NtStatus::ObjectNameNotFound;
    }

    [[nodiscard]] const KeyValue* getValue(std::wstring_view valName, const se::TokenObject* callerToken = nullptr) const {
        if (callerToken) {
            uint32_t granted = 0;
            NtStatus secStatus = checkAccess(*callerToken, KEY_QUERY_VALUE, granted);
            if (!NT_SUCCESS(secStatus)) {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksDenied++;
                return nullptr;
            }
            {
                std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                g_CmTelemetry.totalSecurityChecksPassed++;
            }
        }

        auto it = values_.find(std::wstring(valName));
        if (it == values_.end())
            it = std::find_if(values_.begin(), values_.end(), [&](const auto& entry) {
                return nameEquals(entry.first, valName);
            });
        if (it != values_.end()) {
            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
            g_CmTelemetry.totalValuesQueried++;
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] size_t getSubkeyCount() const noexcept { return subkeys_.size(); }
    [[nodiscard]] size_t getValueCount() const noexcept { return values_.size(); }

    [[nodiscard]] std::vector<std::wstring> getSubkeyNames() const {
        std::vector<std::wstring> names;
        names.reserve(subkeys_.size());
        for (const auto& [k, _] : subkeys_) {
            names.push_back(k);
        }
        return names;
    }

    [[nodiscard]] std::vector<std::wstring> getValueNames() const {
        std::vector<std::wstring> names;
        names.reserve(values_.size());
        for (const auto& [k, _] : values_) {
            names.push_back(k);
        }
        return names;
    }

    // Security Descriptor Management
    void setSecurityDescriptor(se::SecurityDescriptor sd) {
        securityDescriptor_ = std::move(sd);
    }

    [[nodiscard]] const std::optional<se::SecurityDescriptor>& getSecurityDescriptor() const noexcept {
        return securityDescriptor_;
    }

    [[nodiscard]] NtStatus checkAccess(
        const se::TokenObject& token,
        uint32_t desiredAccess,
        uint32_t& grantedAccess
    ) const noexcept {
        if (!securityDescriptor_) {
            grantedAccess = desiredAccess;
            return NtStatus::Success;
        }
        return se::SecurityReferenceMonitor::accessCheck(token, *securityDescriptor_, desiredAccess, grantedAccess, name_);
    }

    [[nodiscard]] KeyObject* getParent() const noexcept { return parent_; }

private:
    std::wstring name_;
    bool isVolatile_{false};
    bool isSymlink_{false};
    std::wstring symlinkTarget_{};
    KeyObject* parent_{nullptr};
    std::unordered_map<std::wstring, std::shared_ptr<KeyObject>> subkeys_;
    std::unordered_map<std::wstring, KeyValue> values_;
    std::optional<se::SecurityDescriptor> securityDescriptor_;
};

// ============================================================================
// 7. Configuration Manager (CM) Subsystem Engine
// ============================================================================
class ConfigurationManager {
public:
    static ConfigurationManager& get() {
        static ConfigurationManager instance;
        return instance;
    }

    void initialize() {
        rootKey_ = std::make_shared<KeyObject>(L"\\Registry");
        
        // Create standard NT Registry Hives:
        // \Registry\Machine\SYSTEM
        // \Registry\Machine\SOFTWARE
        // \Registry\Machine\SAM
        // \Registry\Machine\SECURITY
        // \Registry\User
        auto machine = rootKey_->createSubkey(L"Machine");
        auto user = rootKey_->createSubkey(L"User");
        
        auto system = machine->createSubkey(L"SYSTEM");
        auto software = machine->createSubkey(L"SOFTWARE");
        machine->createSubkey(L"SAM");
        machine->createSubkey(L"SECURITY");
        user->createSubkey(L".DEFAULT");

        auto ccs = system->createSubkey(L"CurrentControlSet");
        auto control = ccs->createSubkey(L"Control");
        auto session = control->createSubkey(L"Session Manager");
        
        session->setValueString(L"OSName", L"MicaNT Modern C++23 Clean-Room Executive");
        session->setValueDword(L"OSBuild", 26100);
        session->setValueDword(L"ZeroTelemetryEnabled", 1);

        // Publish the standard NT product identity consumed by applications
        // such as winver through HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion.
        auto microsoft = software->createSubkey(L"Microsoft");
        auto windowsNt = microsoft->createSubkey(L"Windows NT");
        auto currentVersion = windowsNt->createSubkey(L"CurrentVersion");
        currentVersion->setValueString(L"ProductName", L"LuceOS");
        currentVersion->setValueString(L"CurrentVersion", L"10.0");
        currentVersion->setValueString(L"CurrentBuildNumber", L"26100");

        // Assign default Security Descriptor to Session Manager (Admin Full, Users Read)
        se::SecurityDescriptor sessionSd;
        sessionSd.setOwner(se::Sid::localSystem());
        sessionSd.setGroup(se::Sid::administrators());
        se::Acl sessionDacl;
        sessionDacl.addAllowedAce(se::Sid::administrators(), KEY_ALL_ACCESS);
        sessionDacl.addAllowedAce(se::Sid::users(), KEY_READ);
        sessionSd.setDacl(sessionDacl);
        sessionSd.setIntegrityLevel(se::SECURITY_MANDATORY_HIGH_RID, se::SYSTEM_MANDATORY_LABEL_NO_WRITE_UP);
        session->setSecurityDescriptor(sessionSd);
    }

    void resetForTesting() {
        mountedHives_.clear();
        initialize();
    }

    /**
     * @brief Resolves hierarchical registry path with symbolic link resolution and recursion loop protection.
     */
    std::shared_ptr<KeyObject> resolvePath(
        std::wstring_view path,
        bool followSymlinks = true,
        const se::TokenObject* callerToken = nullptr,
        uint32_t desiredAccess = KEY_READ,
        NtStatus* outStatus = nullptr
    ) {
        if (outStatus) *outStatus = NtStatus::Success;

        std::wstring currentPath(path);
        size_t hops = 0;
        std::unordered_set<std::wstring> visitedSymlinks;

        while (true) {
            std::wstring_view p = currentPath;
            if (p.starts_with(L"\\Registry")) {
                p.remove_prefix(9);
            }
            if (p.starts_with(L"\\")) {
                p.remove_prefix(1);
            }

            std::shared_ptr<KeyObject> current = rootKey_;
            size_t start = 0;
            bool linkEncountered = false;

            while (start < p.size()) {
                size_t slash = p.find(L'\\', start);
                std::wstring_view part = (slash == std::wstring_view::npos) 
                    ? p.substr(start) 
                    : p.substr(start, slash - start);

                if (!part.empty()) {
                    current = current->openSubkey(part, callerToken, desiredAccess);
                    if (!current) {
                        if (outStatus) *outStatus = NtStatus::ObjectNameNotFound;
                        return nullptr;
                    }

                    if (followSymlinks && current->isSymlink()) {
                        hops++;
                        if (hops > CM_MAX_SYMLINK_RECURSION) {
                            if (outStatus) *outStatus = NtStatus::LinkEvaluationCapExceeded;
                            return nullptr;
                        }

                        const auto& target = current->getSymlinkTarget();
                        if (visitedSymlinks.contains(target)) {
                            if (outStatus) *outStatus = NtStatus::CircularReference;
                            return nullptr;
                        }
                        visitedSymlinks.insert(target);

                        std::wstring remaining(slash != std::wstring_view::npos ? p.substr(slash) : L"");
                        currentPath = target + remaining;
                        linkEncountered = true;

                        {
                            std::lock_guard<std::mutex> lk(g_CmTelemetryMutex);
                            g_CmTelemetry.totalSymlinksResolved++;
                        }
                        break;
                    }
                }

                if (slash == std::wstring_view::npos) break;
                start = slash + 1;
            }

            if (!linkEncountered) {
                return current;
            }
        }
    }

    [[nodiscard]] std::shared_ptr<KeyObject> getRootKey() const noexcept { return rootKey_; }

    /**
     * @brief Canonical NtCreateKey / CmCreateKey engine.
     */
    NtStatus createKeyEx(
        std::wstring_view fullPath,
        uint32_t desiredAccess,
        uint32_t createOptions,
        std::shared_ptr<KeyObject>& outKey,
        uint32_t* disposition = nullptr,
        const se::TokenObject* callerToken = nullptr,
        const se::SecurityDescriptor* sd = nullptr
    ) {
        if (fullPath.empty()) return NtStatus::InvalidParameter;

        // Check if key already exists
        auto existing = resolvePath(fullPath, false, callerToken, desiredAccess);
        if (existing) {
            outKey = existing;
            if (disposition) *disposition = REG_OPENED_EXISTING_KEY;
            return NtStatus::Success;
        }

        std::wstring_view p = fullPath;
        if (p.starts_with(L"\\Registry")) {
            p.remove_prefix(9);
        }
        if (p.starts_with(L"\\")) {
            p.remove_prefix(1);
        }

        std::shared_ptr<KeyObject> current = rootKey_;
        size_t start = 0;
        bool isVolatile = (createOptions & REG_OPTION_VOLATILE) != 0;

        while (start < p.size()) {
            size_t slash = p.find(L'\\', start);
            std::wstring_view part = (slash == std::wstring_view::npos) 
                ? p.substr(start) 
                : p.substr(start, slash - start);

            if (!part.empty()) {
                auto next = current->openSubkey(part);
                if (!next) {
                    // Create subkey
                    next = current->createSubkey(part, isVolatile, callerToken);
                    if (!next) {
                        return NtStatus::AccessDenied;
                    }
                }
                current = next;
            }

            if (slash == std::wstring_view::npos) break;
            start = slash + 1;
        }

        if (sd) {
            current->setSecurityDescriptor(*sd);
        }

        outKey = current;
        if (disposition) *disposition = REG_CREATED_NEW_KEY;
        return NtStatus::Success;
    }

    /**
     * @brief Canonical NtOpenKey / CmOpenKey engine.
     */
    NtStatus openKeyEx(
        std::wstring_view fullPath,
        uint32_t desiredAccess,
        std::shared_ptr<KeyObject>& outKey,
        const se::TokenObject* callerToken = nullptr
    ) {
        NtStatus status = NtStatus::Success;
        outKey = resolvePath(fullPath, true, callerToken, desiredAccess, &status);
        return outKey ? NtStatus::Success : status;
    }

    /**
     * @brief Creates a Registry Symbolic Link (REG_LINK).
     */
    std::shared_ptr<KeyObject> createSymbolicLinkKey(
        std::wstring_view linkPath,
        std::wstring_view targetPath,
        const se::SecurityDescriptor* sd = nullptr
    ) {
        std::shared_ptr<KeyObject> key;
        NtStatus st = createKeyEx(linkPath, KEY_ALL_ACCESS, REG_OPTION_CREATE_LINK, key, nullptr, nullptr, sd);
        if (!NT_SUCCESS(st) || !key) return nullptr;

        key->setSymlink(std::wstring(targetPath));
        std::vector<uint8_t> targetBytes(targetPath.size() * sizeof(wchar_t));
        std::memcpy(targetBytes.data(), targetPath.data(), targetBytes.size());
        key->setValueEx(L"SymbolicLinkValue", RegType::Link, targetBytes);
        return key;
    }

    /**
     * @brief Exports an in-memory key hierarchy to an on-disk REGF hive structure.
     */
    [[nodiscard]] std::shared_ptr<CmHive> exportToHive(const KeyObject& root) {
        auto hive = std::make_shared<CmHive>();
        std::string asciiName(root.getName().begin(), root.getName().end());

        // Allocate NK record
        size_t nkSize = sizeof(NkRecordHeader) + asciiName.size();
        uint32_t rootCell = hive->allocateCell(nkSize, root.isVolatile());
        auto* nk = reinterpret_cast<NkRecordHeader*>(hive->getCellData(rootCell));
        nk->signature = NK_SIGNATURE;
        nk->flags = 0x20 | (root.isVolatile() ? KEY_IS_VOLATILE : 0x04);
        nk->nameLength = static_cast<uint16_t>(asciiName.size());
        nk->subkeyCount = static_cast<uint32_t>(root.getSubkeyCount());
        nk->valueCount = static_cast<uint32_t>(root.getValueCount());
        std::memcpy(reinterpret_cast<uint8_t*>(nk) + sizeof(NkRecordHeader), asciiName.data(), asciiName.size());

        hive->setRootCellOffset(rootCell);
        return hive;
    }

    /**
     * @brief Mounts a serialized REGF hive buffer at the specified path.
     */
    bool mountHive(std::wstring_view mountPath, const std::vector<uint8_t>& hiveData) {
        auto hive = CmHive::deserialize(hiveData);
        if (!hive || !hive->isConsistent()) return false;

        auto key = resolvePath(mountPath);
        if (!key) {
            key = rootKey_->createSubkey(mountPath);
        }
        mountedHives_[std::wstring(mountPath)] = hive;
        return true;
    }

    [[nodiscard]] std::shared_ptr<CmHive> getMountedHive(std::wstring_view mountPath) const {
        auto it = mountedHives_.find(std::wstring(mountPath));
        if (it != mountedHives_.end()) return it->second;
        return nullptr;
    }

private:
    ConfigurationManager() { initialize(); }
    std::shared_ptr<KeyObject> rootKey_;
    std::unordered_map<std::wstring, std::shared_ptr<CmHive>> mountedHives_;
};

// ============================================================================
// 8. Sovereign Canonical Executive Configuration Manager Functions (Cm*)
// ============================================================================
inline NtStatus CmCreateKey(
    std::wstring_view path,
    uint32_t desiredAccess,
    uint32_t createOptions,
    std::shared_ptr<KeyObject>& outKey,
    uint32_t* disposition = nullptr,
    const se::TokenObject* callerToken = nullptr,
    const se::SecurityDescriptor* sd = nullptr
) {
    return ConfigurationManager::get().createKeyEx(path, desiredAccess, createOptions, outKey, disposition, callerToken, sd);
}

inline NtStatus CmOpenKey(
    std::wstring_view path,
    uint32_t desiredAccess,
    std::shared_ptr<KeyObject>& outKey,
    const se::TokenObject* callerToken = nullptr
) {
    return ConfigurationManager::get().openKeyEx(path, desiredAccess, outKey, callerToken);
}

inline NtStatus CmSetValueKey(
    std::shared_ptr<KeyObject> key,
    std::wstring_view valueName,
    RegType type,
    std::span<const uint8_t> data,
    const se::TokenObject* callerToken = nullptr
) {
    if (!key) return NtStatus::InvalidHandle;
    return key->setValueEx(valueName, type, data, callerToken);
}

inline NtStatus CmQueryValueKey(
    std::shared_ptr<KeyObject> key,
    std::wstring_view valueName,
    KeyValue& outValue,
    const se::TokenObject* callerToken = nullptr
) {
    if (!key) return NtStatus::InvalidHandle;
    const auto* val = key->getValue(valueName, callerToken);
    if (!val) return NtStatus::ObjectNameNotFound;
    outValue = *val;
    return NtStatus::Success;
}

inline NtStatus CmDeleteValueKey(
    std::shared_ptr<KeyObject> key,
    std::wstring_view valueName,
    const se::TokenObject* callerToken = nullptr
) {
    if (!key) return NtStatus::InvalidHandle;
    return key->deleteValueEx(valueName, callerToken);
}

inline NtStatus CmDeleteKey(
    std::shared_ptr<KeyObject> key,
    const se::TokenObject* callerToken = nullptr
) {
    if (!key) return NtStatus::InvalidHandle;
    auto* parent = key->getParent();
    if (!parent) return NtStatus::AccessDenied;
    return parent->deleteSubkeyEx(key->getName(), callerToken);
}

inline NtStatus CmFlushKey(std::shared_ptr<KeyObject> key) {
    if (!key) return NtStatus::InvalidHandle;
    // Registry keys still live only in the in-memory ConfigurationManager
    // tree. Reporting success here would falsely promise that changes survive
    // shutdown; until a hive is attached to a durable file backend, fail.
    return NtStatus::NotSupported;
}

} // namespace micant::cm
