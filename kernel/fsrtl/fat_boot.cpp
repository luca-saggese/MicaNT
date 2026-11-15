#include "micant/km/fsrtl/fat_boot.hpp"
#include "micant/km/executive/io_block_device.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/core/sync_baremetal.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/se.hpp"
#include "micant/shared/boot_driver.hpp"
#include "micant/km/drivers/storage/partition_table.hpp"
#include <cstring>

namespace micant::fsrtl::fat {
namespace {
constexpr int32_t STATUS_SUCCESS = 0;
constexpr int32_t STATUS_DEVICE_NOT_READY = static_cast<int32_t>(0xC00000A3u);
constexpr int32_t STATUS_DEVICE_DATA_ERROR = static_cast<int32_t>(0xC000009Cu);
constexpr int32_t STATUS_UNRECOGNIZED_VOLUME = static_cast<int32_t>(0xC000014Fu);
constexpr size_t kSectorSize = 512, kMaximumPath = 256;
constexpr uint32_t kMaximumSectorsPerCommand = 128;
struct FatDirEntry {
    char Name[128]{};
    char ShortName[13]{};
    uint32_t FirstCluster{};
    uint32_t Size{};
    uint32_t DirectorySector{};
    uint16_t DirectoryOffset{};
    uint8_t RawName[11]{};
    bool Directory{};
};

struct FatVolume {
    uint64_t PartitionStart{};
    uint64_t PartitionSectors{};
    uint64_t TotalSectors{};
    uint32_t FatStart{};
    uint32_t DataStart{};
    uint32_t SectorsPerFat{};
    uint32_t RootCluster{};
    uint32_t DataClusterCount{};
    uint32_t CachedFatSector{UINT32_MAX};
    uint16_t ReservedSectors{};
    uint16_t BytesPerSector{};
    uint8_t SectorsPerCluster{};
    uint8_t NumberOfFats{};
    char Drive{};
    bool Mounted{};
    uint8_t FatCache[kSectorSize]{};
};

struct Mount;
// One stream identity per on-disk directory entry, shared by all file objects
// opened through either its long name or its short alias. The filesystem gate
// protects the identity, entry and reference count.
struct FileContext {
    Mount* owner{}; FatVolume* volume{}; FatDirEntry entry{}; bool inUse{};
    bool pendingCreate{}; size_t references{}; size_t imageReferences{};
    bool directory{}; uint32_t directoryCluster{};
    void* imageControl{};
    bool (*retainControl)(void*) noexcept{};
    void (*releaseControl)(void*) noexcept{};
};
struct Mount { const void* owner{}; io::BlockDeviceObject* device{}; ob::ObjectHeader* reference{}; uint64_t sectors{}, token{}; FatVolume volumes[3]{}; uint32_t volumeCount{}; FileContext files[32]{}; bool offline{}; };
Mount mounts[4]{};
Mount* current{};
uint64_t nextToken{1};
uint64_t systemRootMount{};
char systemRootDrive{};
bool initialized{};
se::FreestandingSecurityDescriptor g_rootStoreAcl{};
sync::detail::BootstrapEvent gate{false, true};
struct Gate { bool held{}; Gate() noexcept : held(gate.wait() == NtStatus::Success) {} ~Gate() { if (held) (void)gate.set(); } };

bool rootStoreAccessAllowed(uint32_t desiredAccess) noexcept {
    auto* currentThread = ke::KeGetCurrentThread();
    auto* process = ke::KeGetCurrentProcess();
    if (!currentThread || !currentThread->owningThread || !process ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return false;
    se::FreestandingAccessToken* primary{};
    se::FreestandingAccessToken* impersonation{};
    ob::ObjectHeader* primaryReference{};
    ob::ObjectHeader* impersonationReference{};
    if (process->referencePrimaryToken(primary, primaryReference) != NtStatus::Success)
        return false;
    const auto status = currentThread->owningThread->referenceImpersonationToken(
        impersonation, impersonationReference);
    if (status != NtStatus::Success) {
        se::SeDereferenceFreestandingToken(primaryReference);
        return false;
    }
    const auto* effective = se::SeGetEffectiveFreestandingToken(primary, impersonation);
    uint32_t granted{};
    const uint32_t requested = desiredAccess & 0x00000003u;
    const bool allowed = se::SeAccessCheckFreestanding(effective, g_rootStoreAcl,
                                                        requested, granted) &&
                         (granted & requested) == requested;
    if (impersonationReference) se::SeDereferenceFreestandingToken(impersonationReference);
    se::SeDereferenceFreestandingToken(primaryReference);
    return allowed;
}
alignas(512) uint8_t g_sectorBuffer[kSectorSize]{};
micant::storage::partition_table::Extent g_partitionExtents[128]{};
uint8_t g_partitionScratch[8192]{};
alignas(4096) uint8_t g_clusterBuffer[65536]{};
uint32_t g_writeAllocatedClusters[4096]{};
#define g_volumes (current->volumes)
#define g_volumeCount (current->volumeCount)
uint16_t loadLe16(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t loadLe32(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

int32_t readIoBlocks(uint64_t lba, uint32_t count, void* buffer) noexcept {
    if (!current || !current->device || current->offline) return STATUS_DEVICE_NOT_READY;
    uint32_t transferred{};
    const auto status = current->device->read(lba, count, buffer, &transferred);
    return status == NtStatus::Success && transferred == count ? STATUS_SUCCESS :
        (status == NtStatus::Success ? STATUS_DEVICE_DATA_ERROR : static_cast<int32_t>(status));
}

int32_t writeIoBlocks(uint64_t lba, uint32_t count, const void* buffer) noexcept {
    if (!current || !current->device || current->offline) return STATUS_DEVICE_NOT_READY;
    uint32_t transferred{};
    const auto status = current->device->writeMounted(lba, count, buffer, &transferred);
    return status == NtStatus::Success && transferred == count ? STATUS_SUCCESS :
        (status == NtStatus::Success ? STATUS_DEVICE_DATA_ERROR : static_cast<int32_t>(status));
}
uint32_t sectorsPerCluster(const FatVolume& volume) noexcept {
    return volume.SectorsPerCluster;
}

bool readVolume(FatVolume& volume, uint64_t relativeLba, uint32_t count,
                void* output) noexcept {
    if (!volume.Mounted || !output || count == 0 || relativeLba >= volume.TotalSectors ||
        count > volume.TotalSectors - relativeLba ||
        volume.PartitionStart > UINT64_MAX - relativeLba) return false;
    const int32_t status = readIoBlocks(volume.PartitionStart + relativeLba, count, output);
    return status == STATUS_SUCCESS;
}

bool writeVolume(FatVolume& volume, uint64_t relativeLba, uint32_t count,
                 const void* input) noexcept {
    if (!volume.Mounted || !input || !count || relativeLba >= volume.TotalSectors ||
        count > volume.TotalSectors - relativeLba ||
        volume.PartitionStart > UINT64_MAX - relativeLba) return false;
    return writeIoBlocks(volume.PartitionStart + relativeLba, count, input) == STATUS_SUCCESS;
}

bool clusterToLba(const FatVolume& volume, uint32_t cluster,
                  uint32_t& lba) noexcept {
    if (cluster < 2 || cluster - 2 >= volume.DataClusterCount) return false;
    const uint64_t value = static_cast<uint64_t>(volume.DataStart) +
        static_cast<uint64_t>(cluster - 2) * volume.SectorsPerCluster;
    if (value >= volume.TotalSectors || value > UINT32_MAX) return false;
    lba = static_cast<uint32_t>(value);
    return true;
}

bool readFatEntry(FatVolume& volume, uint32_t cluster, uint32_t& value) noexcept {
    if (cluster < 2 || cluster - 2 >= volume.DataClusterCount) return false;
    const uint64_t byteOffset = static_cast<uint64_t>(cluster) * 4;
    const uint64_t fatSector64 = static_cast<uint64_t>(volume.FatStart) +
                                 byteOffset / kSectorSize;
    if (fatSector64 >= static_cast<uint64_t>(volume.FatStart) + volume.SectorsPerFat ||
        fatSector64 > UINT32_MAX) return false;
    const uint32_t fatSector = static_cast<uint32_t>(fatSector64);
    if (volume.CachedFatSector != fatSector) {
        if (!readVolume(volume, fatSector, 1, volume.FatCache)) return false;
        volume.CachedFatSector = fatSector;
    }
    value = loadLe32(volume.FatCache + (byteOffset % kSectorSize)) & 0x0FFFFFFFu;
    return true;
}

uint32_t nextCluster(FatVolume& volume, uint32_t cluster) noexcept {
    uint32_t value{};
    return readFatEntry(volume, cluster, value) ? value : 0x0FFFFFFFu;
}

bool mountVolume(FatVolume& volume, uint64_t startLba,
                 uint64_t partitionSectors, char drive) noexcept {
    volume = {};
    if (!partitionSectors || startLba >= current->sectors ||
        partitionSectors > current->sectors - startLba) return false;
    volume.PartitionStart = startLba;
    volume.PartitionSectors = partitionSectors;
    if (readIoBlocks(startLba, 1, g_sectorBuffer) != STATUS_SUCCESS) return false;
    if (g_sectorBuffer[510] != 0x55 || g_sectorBuffer[511] != 0xAA) return false;

    volume.BytesPerSector = loadLe16(g_sectorBuffer + 11);
    volume.SectorsPerCluster = g_sectorBuffer[13];
    volume.ReservedSectors = loadLe16(g_sectorBuffer + 14);
    volume.NumberOfFats = g_sectorBuffer[16];
    volume.SectorsPerFat = loadLe32(g_sectorBuffer + 36);
    volume.RootCluster = loadLe32(g_sectorBuffer + 44);
    const uint32_t total32 = loadLe32(g_sectorBuffer + 32);
    const uint16_t total16 = loadLe16(g_sectorBuffer + 19);
    volume.TotalSectors = total32 ? total32 : total16;
    if (volume.BytesPerSector != kSectorSize || !volume.SectorsPerCluster ||
        volume.SectorsPerCluster > 128 ||
        (volume.SectorsPerCluster & (volume.SectorsPerCluster - 1)) != 0 ||
        !volume.ReservedSectors || !volume.NumberOfFats || volume.NumberOfFats > 2 || !volume.SectorsPerFat ||
        !volume.TotalSectors || volume.TotalSectors > partitionSectors ||
        volume.RootCluster < 2 || volume.PartitionStart > UINT64_MAX - volume.TotalSectors)
        return false;
    const uint64_t dataStart = static_cast<uint64_t>(volume.ReservedSectors) +
        static_cast<uint64_t>(volume.NumberOfFats) * volume.SectorsPerFat;
    if (dataStart >= volume.TotalSectors || dataStart > UINT32_MAX) return false;
    const uint64_t clusterCount = (volume.TotalSectors - dataStart) /
                                  volume.SectorsPerCluster;
    if (clusterCount == 0 || clusterCount > 0x0FFFFFF5u ||
        clusterCount > (static_cast<uint64_t>(volume.SectorsPerFat) * kSectorSize) / 4 - 2)
        return false;
    volume.FatStart = volume.ReservedSectors;
    volume.DataStart = static_cast<uint32_t>(dataStart);
    volume.DataClusterCount = static_cast<uint32_t>(clusterCount);
    if (volume.RootCluster - 2 >= volume.DataClusterCount) return false;
    volume.CachedFatSector = UINT32_MAX;
    volume.Drive = drive;
    volume.Mounted = true;
    return true;
}

bool equalAsciiInsensitive(const char* left, const char* right) noexcept {
    size_t i{};
    while (left[i] && right[i]) {
        char a = left[i], b = right[i];
        if (a >= 'a' && a <= 'z') a = static_cast<char>(a - ('a' - 'A'));
        if (b >= 'a' && b <= 'z') b = static_cast<char>(b - ('a' - 'A'));
        if (a != b) return false;
        ++i;
    }
    return left[i] == right[i];
}

// The freestanding runtime supplies memcpy/memcmp but no strchr. Keep path
// separator scans inline so this TU links without a hosted libc.
bool containsPathSeparator(const char* text) noexcept {
    if (!text) return false;
    for (const char* cursor = text; *cursor; ++cursor)
        if (*cursor == '\\' || *cursor == '/') return true;
    return false;
}

void formatShortName(const uint8_t* entry, char* output, size_t outputSize) noexcept {
    size_t at{};
    for (size_t i = 0; i < 8 && entry[i] != ' '; ++i)
        if (at + 1 < outputSize) output[at++] = static_cast<char>(entry[i]);
    size_t ext{};
    while (ext < 3 && entry[8 + ext] == ' ') ++ext;
    if (ext < 3 && at + 1 < outputSize) output[at++] = '.';
    for (size_t i = ext; i < 3 && entry[8 + i] != ' '; ++i)
        if (at + 1 < outputSize) output[at++] = static_cast<char>(entry[8 + i]);
    output[at] = 0;
}

NtStatus findInDirectory(FatVolume& volume, uint32_t directoryCluster,
                         const char* component, FatDirEntry& found) noexcept {
    char longName[256]{};
    uint8_t lfnExpected{}, lfnChecksum{};
    bool lfnValid{};
    const uint32_t maxClusters = volume.DataClusterCount + 1;
    uint32_t cluster = directoryCluster;
    for (uint32_t traversed = 0; traversed < maxClusters; ++traversed) {
        if (cluster < 2 || cluster - 2 >= volume.DataClusterCount) return NtStatus::DeviceDataError;
        const uint32_t sectors = sectorsPerCluster(volume);
        uint32_t clusterLba{};
        if (!clusterToLba(volume, cluster, clusterLba) ||
            !readVolume(volume, clusterLba, sectors, g_clusterBuffer)) return NtStatus::DeviceDataError;
        const size_t entries = static_cast<size_t>(sectors) * kSectorSize / 32;
        for (size_t i = 0; i < entries; ++i) {
            const uint8_t* item = g_clusterBuffer + i * 32;
            if (item[0] == 0) return NtStatus::ObjectNameNotFound;
            if (item[0] == 0xE5) { longName[0] = 0; lfnValid = false; continue; }
            if (item[11] == 0x0F) {
                const uint8_t sequence = item[0] & 0x1F;
                if (sequence == 0 || sequence > 19 || (item[0] & 0xA0) || item[12] ||
                    loadLe16(item + 26)) { longName[0] = 0; lfnValid = false; continue; }
                if (item[0] & 0x40) {
                    std::memset(longName, 0, sizeof(longName));
                    lfnExpected = sequence; lfnChecksum = item[13]; lfnValid = true;
                }
                if (!lfnValid || lfnExpected != sequence || item[13] != lfnChecksum) {
                    longName[0] = 0; lfnValid = false; continue;
                }
                static constexpr uint8_t offsets[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
                const size_t base = static_cast<size_t>(sequence - 1) * 13;
                for (size_t c = 0; c < 13 && base + c + 1 < sizeof(longName); ++c) {
                    const uint16_t character = loadLe16(item + offsets[c]);
                    if (character == 0 || character == 0xFFFF) {
                        longName[base + c] = 0;
                        break;
                    }
                    if (character > 0x7f) { lfnValid = false; break; }
                    longName[base + c] = static_cast<char>(character);
                }
                --lfnExpected;
                continue;
            }
            if (item[11] & 0x08) { longName[0] = 0; lfnValid = false; continue; }

            FatDirEntry candidate{};
            std::memcpy(candidate.RawName, item, sizeof(candidate.RawName));
            candidate.Directory = (item[11] & 0x10) != 0;
            candidate.FirstCluster = (static_cast<uint32_t>(loadLe16(item + 20)) << 16) |
                                      loadLe16(item + 26);
            candidate.Size = loadLe32(item + 28);
            candidate.DirectorySector = clusterLba +
                static_cast<uint32_t>((i * 32) / kSectorSize);
            candidate.DirectoryOffset = static_cast<uint16_t>((i * 32) % kSectorSize);
            formatShortName(item, candidate.ShortName, sizeof(candidate.ShortName));
            uint8_t shortChecksum{};
            for (unsigned c = 0; c < 11; ++c)
                shortChecksum = static_cast<uint8_t>(((shortChecksum & 1u) << 7) +
                    (shortChecksum >> 1) + item[c]);
            const char* name = lfnValid && !lfnExpected && shortChecksum == lfnChecksum && longName[0]
                ? longName : candidate.ShortName;
            size_t n{};
            while (name[n] && n + 1 < sizeof(candidate.Name)) {
                candidate.Name[n] = name[n];
                ++n;
            }
            candidate.Name[n] = 0;
            longName[0] = 0;
            lfnValid = false;
            if (equalAsciiInsensitive(component, candidate.Name) ||
                equalAsciiInsensitive(component, candidate.ShortName)) {
                found = candidate;
                return NtStatus::Success;
            }
        }
        uint32_t next{};
        if (!readFatEntry(volume, cluster, next)) return NtStatus::DeviceDataError;
        if (next >= 0x0FFFFFF8u) return NtStatus::ObjectNameNotFound;
        cluster = next;
    }
    return NtStatus::DeviceDataError;
}

NtStatus findFile(FatVolume& volume, const char* path, FatDirEntry& result) noexcept {
    if (!volume.Mounted || !path) return NtStatus::InvalidParameter;
    const char* current = path;
    while (*current == '\\' || *current == '/') ++current;
    uint32_t directory = volume.RootCluster;
    if (*current == 0) {
        result = {};
        result.Directory = true;
        result.FirstCluster = volume.RootCluster;
        return NtStatus::Success;
    }
    for (;;) {
        char component[128]{};
        size_t length{};
        while (*current && *current != '\\' && *current != '/') {
            if (length + 1 >= sizeof(component)) return NtStatus::InvalidParameter;
            component[length++] = *current++;
        }
        component[length] = 0;
        while (*current == '\\' || *current == '/') ++current;
        if (!length || equalAsciiInsensitive(component, ".") ||
            equalAsciiInsensitive(component, "..")) return NtStatus::InvalidParameter;
        FatDirEntry entry{};
        const auto status = findInDirectory(volume, directory, component, entry);
        if (status != NtStatus::Success) {
            if (status == NtStatus::ObjectNameNotFound && *current)
                return NtStatus::ObjectPathNotFound;
            return status;
        }
        if (*current == 0) { result = entry; return NtStatus::Success; }
        if (!entry.Directory) return NtStatus::ObjectPathNotFound;
        if (entry.FirstCluster < 2) return NtStatus::DeviceDataError;
        directory = entry.FirstCluster;
    }
}

bool readFileAt(FatVolume& volume, const FatDirEntry& entry, uint64_t offset,
                void* output, size_t length, size_t& bytesRead) noexcept {
    bytesRead = 0;
    if (entry.Directory || (!output && length)) return false;
    const uint32_t fileSize = __atomic_load_n(&entry.Size, __ATOMIC_ACQUIRE);
    if (offset >= fileSize || length == 0) return true;
    const uint32_t clusterBytes = static_cast<uint32_t>(volume.BytesPerSector) *
                                  volume.SectorsPerCluster;
    const size_t wanted = static_cast<size_t>((fileSize - offset) < length
        ? (fileSize - offset) : length);
    uint64_t skip = offset / clusterBytes;
    uint32_t cluster = entry.FirstCluster;
    const uint32_t maxClusters = volume.DataClusterCount + 1;
    if (skip >= maxClusters) return false;
    while (skip--) {
        if (cluster < 2 || cluster - 2 >= volume.DataClusterCount) return false;
        cluster = nextCluster(volume, cluster);
    }
    size_t within = static_cast<size_t>(offset % clusterBytes);
    auto* destination = static_cast<uint8_t*>(output);
    uint32_t traversed{};
    while (bytesRead < wanted) {
        if (cluster < 2 || cluster - 2 >= volume.DataClusterCount ||
            ++traversed > maxClusters) return false;
        uint32_t clusterLba{};
        if (!clusterToLba(volume, cluster, clusterLba) ||
            !readVolume(volume, clusterLba,
                        volume.SectorsPerCluster, g_clusterBuffer)) return false;
        const size_t amount = (wanted - bytesRead) < (clusterBytes - within)
            ? wanted - bytesRead : clusterBytes - within;
        std::memcpy(destination + bytesRead, g_clusterBuffer + within, amount);
        bytesRead += amount;
        within = 0;
        if (bytesRead < wanted) cluster = nextCluster(volume, cluster);
    }
    return true;
}

bool refreshFileEntry(FatVolume& volume, FatDirEntry& entry) noexcept {
    if (entry.DirectoryOffset > kSectorSize - 32) return false;
    uint8_t sector[kSectorSize]{};
    if (!readVolume(volume, entry.DirectorySector, 1, sector)) return false;
    const uint8_t* item = sector + entry.DirectoryOffset;
    if (item[0] == 0 || item[0] == 0xE5 || item[11] == 0x0F ||
        (item[11] & 0x10) || std::memcmp(item, entry.RawName, sizeof(entry.RawName)) != 0)
        return false;
    entry.FirstCluster = (static_cast<uint32_t>(loadLe16(item + 20)) << 16) |
                         loadLe16(item + 26);
    __atomic_store_n(&entry.Size, loadLe32(item + 28), __ATOMIC_RELEASE);
    return true;
}

bool writeFatEntry(FatVolume& volume, uint32_t cluster, uint32_t value) noexcept {
    if (cluster < 2 || cluster - 2 >= volume.DataClusterCount) return false;
    const uint64_t byteOffset = static_cast<uint64_t>(cluster) * 4;
    const uint32_t sectorOffset = static_cast<uint32_t>(byteOffset / kSectorSize);
    const uint32_t entryOffset = static_cast<uint32_t>(byteOffset % kSectorSize);
    if (sectorOffset >= volume.SectorsPerFat || entryOffset > kSectorSize - 4) return false;
    uint8_t originals[2][kSectorSize]{};
    uint64_t targets[2]{};
    for (uint32_t fat = 0; fat < volume.NumberOfFats; ++fat) {
        const uint64_t target = static_cast<uint64_t>(volume.FatStart) +
            static_cast<uint64_t>(fat) * volume.SectorsPerFat + sectorOffset;
        if (target > UINT32_MAX || !readVolume(volume, target, 1, originals[fat])) return false;
        targets[fat] = target;
    }
    uint32_t written{};
    for (uint32_t fat = 0; fat < volume.NumberOfFats; ++fat) {
        uint8_t sector[kSectorSize];
        std::memcpy(sector, originals[fat], sizeof(sector));
        const uint32_t previous = loadLe32(sector + entryOffset);
        const uint32_t merged = (previous & 0xF0000000u) | (value & 0x0FFFFFFFu);
        sector[entryOffset] = static_cast<uint8_t>(merged);
        sector[entryOffset + 1] = static_cast<uint8_t>(merged >> 8);
        sector[entryOffset + 2] = static_cast<uint8_t>(merged >> 16);
        sector[entryOffset + 3] = static_cast<uint8_t>(merged >> 24);
        if (!writeVolume(volume, targets[fat], 1, sector)) {
            while (written) {
                --written;
                (void)writeVolume(volume, targets[written], 1, originals[written]);
            }
            volume.CachedFatSector = UINT32_MAX;
            return false;
        }
        ++written;
    }
    volume.CachedFatSector = UINT32_MAX;
    return true;
}

bool allocateCluster(FatVolume& volume, uint32_t& allocated) noexcept {
    allocated = 0;
    for (uint32_t cluster = 2; cluster < volume.DataClusterCount + 2; ++cluster) {
        uint32_t entry{};
        if (!readFatEntry(volume, cluster, entry)) return false;
        if (entry != 0) continue;
        if (!writeFatEntry(volume, cluster, 0x0FFFFFFFu)) return false;
        uint32_t lba{};
        std::memset(g_clusterBuffer, 0,
                    static_cast<size_t>(volume.SectorsPerCluster) * kSectorSize);
        if (!clusterToLba(volume, cluster, lba) ||
            !writeVolume(volume, lba, volume.SectorsPerCluster, g_clusterBuffer)) {
            (void)writeFatEntry(volume, cluster, 0);
            return false;
        }
        allocated = cluster;
        return true;
    }
    return false;
}

bool writeDirectorySlot(FatVolume& volume, uint32_t sectorNumber,
                        uint16_t offset, const uint8_t entry[32]) noexcept {
    if (offset > kSectorSize - 32) return false;
    uint8_t sector[kSectorSize]{};
    if (!readVolume(volume, sectorNumber, 1, sector)) return false;
    std::memcpy(sector + offset, entry, 32);
    return writeVolume(volume, sectorNumber, 1, sector);
}

NtStatus createRootFile(FatVolume& volume, const char* path, FatDirEntry& result) noexcept {
    if (!path || !*path || containsPathSeparator(path)) return NtStatus::NotSupported;
    uint8_t shortName[11];
    std::memset(shortName, ' ', sizeof(shortName));
    size_t baseLength{}, extensionLength{};
    bool inExtension{};
    for (const char* cursor = path; *cursor; ++cursor) {
        if (*cursor == '.') {
            if (inExtension) return NtStatus::NotSupported;
            inExtension = true;
            continue;
        }
        char value = *cursor;
        if (value >= 'a' && value <= 'z') value = static_cast<char>(value - ('a' - 'A'));
        const bool legal = (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '_' || value == '-';
        if (!legal) return NtStatus::NotSupported;
        if (!inExtension) {
            if (baseLength == 8) return NtStatus::NotSupported;
            shortName[baseLength++] = static_cast<uint8_t>(value);
        } else {
            if (extensionLength == 3) return NtStatus::NotSupported;
            shortName[8 + extensionLength++] = static_cast<uint8_t>(value);
        }
    }
    if (!baseLength) return NtStatus::NotSupported;
    uint32_t cluster = volume.RootCluster;
    for (uint32_t traversed = 0; traversed <= volume.DataClusterCount; ++traversed) {
        if (cluster < 2 || cluster - 2 >= volume.DataClusterCount) return NtStatus::DeviceDataError;
        uint32_t clusterLba{};
        if (!clusterToLba(volume, cluster, clusterLba) ||
            !readVolume(volume, clusterLba, volume.SectorsPerCluster, g_clusterBuffer)) return NtStatus::DeviceDataError;
        const size_t count = static_cast<size_t>(volume.SectorsPerCluster) * kSectorSize / 32;
        for (size_t index = 0; index < count; ++index) {
            const uint8_t* existing = g_clusterBuffer + index * 32;
            if (existing[0] != 0 && existing[0] != 0xE5) continue;
            uint8_t diskEntry[32]{};
            std::memcpy(diskEntry, shortName, sizeof(shortName));
            diskEntry[11] = 0x20;
            const uint32_t relativeSector = clusterLba +
                static_cast<uint32_t>((index * 32) / kSectorSize);
            const uint16_t offset = static_cast<uint16_t>((index * 32) % kSectorSize);
            if (existing[0] == 0) {
                // FAT's zero entry terminates the entire directory. Preserve
                // that terminator after consuming it, including across a
                // cluster boundary, before making the new entry visible.
                uint32_t nextSector{};
                uint16_t nextOffset{};
                bool hasNext = false;
                if (index + 1 < count) {
                    nextSector = clusterLba + static_cast<uint32_t>(((index + 1) * 32) / kSectorSize);
                    nextOffset = static_cast<uint16_t>(((index + 1) * 32) % kSectorSize);
                    hasNext = true;
                } else {
                    uint32_t nextClusterValue{};
                    if (!readFatEntry(volume, cluster, nextClusterValue)) return NtStatus::DeviceDataError;
                    if (nextClusterValue < 0x0FFFFFF8u) {
                        if (!clusterToLba(volume, nextClusterValue, nextSector))
                            return NtStatus::DeviceDataError;
                        hasNext = true;
                    }
                }
                if (hasNext) {
                    uint8_t endMarker[32]{};
                    if (!writeDirectorySlot(volume, nextSector, nextOffset, endMarker))
                        return NtStatus::DeviceDataError;
                }
            }
            if (!writeDirectorySlot(volume, relativeSector, offset, diskEntry)) return NtStatus::DeviceDataError;
            result = {};
            formatShortName(diskEntry, result.ShortName, sizeof(result.ShortName));
            std::memcpy(result.Name, result.ShortName, sizeof(result.ShortName));
            result.DirectorySector = relativeSector;
            result.DirectoryOffset = offset;
            std::memcpy(result.RawName, shortName, sizeof(shortName));
            return NtStatus::Success;
        }
        uint32_t next{};
        if (!readFatEntry(volume, cluster, next)) return NtStatus::DeviceDataError;
        if (next >= 0x0FFFFFF8u) return NtStatus::DiskFull;
        cluster = next;
    }
    return NtStatus::DeviceDataError;
}

bool writeFileAt(FatVolume& volume, FatDirEntry& entry, uint64_t offset,
                 const void* input, size_t length, size_t& transferred) noexcept {
    transferred = 0;
    const uint32_t currentSize = __atomic_load_n(&entry.Size, __ATOMIC_ACQUIRE);
    if (entry.Directory || (!input && length) || offset > UINT32_MAX ||
        length > UINT32_MAX - offset || offset != currentSize) return false;
    if (!length) return true;
    const uint32_t clusterBytes = static_cast<uint32_t>(volume.BytesPerSector) *
                                 volume.SectorsPerCluster;
    const uint64_t endOffset = offset + length;
    const uint64_t lastIndex = (endOffset - 1) / clusterBytes;
    if (lastIndex >= volume.DataClusterCount || lastIndex >= 4096) return false;
    const uint32_t originalSize = currentSize;
    uint32_t allocatedCount{};
    // The rollback boundary is the last pre-existing cluster, not the most
    // recently linked cluster. Later links may themselves be newly allocated.
    uint32_t originalTail{};
    uint32_t firstCluster = entry.FirstCluster;
    uint32_t cluster = firstCluster;
    auto rollback = [&]() noexcept {
        // If the old chain cannot be detached, retain its allocated tail;
        // freeing those clusters would leave a live file pointing at free
        // space that another file could subsequently claim.
        if (allocatedCount && originalTail && !writeFatEntry(volume, originalTail, 0x0FFFFFFFu)) return;
        while (allocatedCount) (void)writeFatEntry(volume, g_writeAllocatedClusters[--allocatedCount], 0);
    };
    if (!cluster) {
        if (currentSize != 0) return false; // Corrupt nonempty directory entry.
        if (!allocateCluster(volume, cluster)) return false;
        g_writeAllocatedClusters[allocatedCount++] = cluster;
        firstCluster = cluster;
    }
    for (uint64_t index = 0; index < lastIndex; ++index) {
        uint32_t next{};
        if (!readFatEntry(volume, cluster, next)) { rollback(); return false; }
        if (next >= 2 && next < 0x0FFFFFF8u) {
            if (next - 2 >= volume.DataClusterCount || next >= 0x0FFFFFF0u) {
                rollback(); return false;
            }
            cluster = next;
            continue;
        }
        if (next < 0x0FFFFFF8u) { rollback(); return false; }
        uint32_t added{};
        if (allocatedCount == 4096 || !allocateCluster(volume, added)) { rollback(); return false; }
        if (!allocatedCount) originalTail = cluster;
        if (!writeFatEntry(volume, cluster, added)) {
            // A failed write can have reached storage despite its error. Do
            // not return the new cluster to the free pool unless the link is
            // conclusively restored to end-of-chain first.
            if (writeFatEntry(volume, cluster, 0x0FFFFFFFu))
                (void)writeFatEntry(volume, added, 0);
            rollback();
            return false;
        }
        cluster = added;
        g_writeAllocatedClusters[allocatedCount++] = added;
    }
    uint64_t logicalCluster = (offset / clusterBytes);
    cluster = firstCluster;
    while (logicalCluster--) {
        uint32_t next{};
        if (!readFatEntry(volume, cluster, next)) { rollback(); return false; }
        if (next < 2 || next >= 0x0FFFFFF0u || next - 2 >= volume.DataClusterCount) {
            rollback(); return false;
        }
        cluster = next;
    }
    const auto* source = static_cast<const uint8_t*>(input);
    size_t position = 0;
    size_t within = static_cast<size_t>(offset % clusterBytes);
    while (position < length) {
        uint32_t clusterLba{};
        if (!clusterToLba(volume, cluster, clusterLba) ||
            !readVolume(volume, clusterLba, volume.SectorsPerCluster, g_clusterBuffer)) {
            rollback(); transferred = 0; return false;
        }
        const size_t amount = (length - position) < (clusterBytes - within)
            ? length - position : clusterBytes - within;
        std::memcpy(g_clusterBuffer + within, source + position, amount);
        if (!writeVolume(volume, clusterLba, volume.SectorsPerCluster, g_clusterBuffer)) {
            rollback(); transferred = 0; return false;
        }
        position += amount;
        transferred += amount;
        within = 0;
        if (position < length) {
            uint32_t next{};
            if (!readFatEntry(volume, cluster, next)) { rollback(); transferred = 0; return false; }
            if (next < 2 || next >= 0x0FFFFFF0u || next - 2 >= volume.DataClusterCount) {
                rollback(); transferred = 0; return false;
            }
            cluster = next;
        }
    }
    const uint32_t newSize = static_cast<uint32_t>(endOffset > originalSize ? endOffset : originalSize);
    uint8_t directorySector[kSectorSize]{};
    if (!readVolume(volume, entry.DirectorySector, 1, directorySector)) {
        rollback(); transferred = 0; return false;
    }
    uint8_t* diskEntry = directorySector + entry.DirectoryOffset;
    diskEntry[20] = static_cast<uint8_t>(firstCluster >> 16);
    diskEntry[21] = static_cast<uint8_t>(firstCluster >> 24);
    diskEntry[26] = static_cast<uint8_t>(firstCluster);
    diskEntry[27] = static_cast<uint8_t>(firstCluster >> 8);
    diskEntry[28] = static_cast<uint8_t>(newSize);
    diskEntry[29] = static_cast<uint8_t>(newSize >> 8);
    diskEntry[30] = static_cast<uint8_t>(newSize >> 16);
    diskEntry[31] = static_cast<uint8_t>(newSize >> 24);
    if (!writeVolume(volume, entry.DirectorySector, 1, directorySector)) {
        // The directory sector may have reached the device even if the
        // write/flush reported failure. Retain the allocated chain: freeing
        // it could turn a published directory entry into a cross-link.
        transferred = 0;
        return false;
    }
    entry.FirstCluster = firstCluster;
    __atomic_store_n(&entry.Size, newSize, __ATOMIC_RELEASE);
    return true;
}

bool readPartitionTable(void* context, uint64_t lba, uint16_t count, void* buffer) noexcept {
    auto* mount = static_cast<Mount*>(context);
    if (!mount || mount != current || !count) return false;
    return readIoBlocks(lba, count, buffer) == STATUS_SUCCESS;
}

int32_t mountPartitions() noexcept {
    using namespace micant::storage::partition_table;
    const Device device{current, &readPartitionTable, current->sectors, kSectorSize,
                        g_partitionScratch, sizeof(g_partitionScratch)};
    Result result{};
    const Status parseStatus = discover(device, g_partitionExtents,
        sizeof(g_partitionExtents) / sizeof(g_partitionExtents[0]), result);
    if (parseStatus != Status::Success) {
        if (parseStatus == Status::IoError) return STATUS_DEVICE_DATA_ERROR;
        // A FAT superfloppy uses the same 0x55AA trailer as an MBR. Its BPB
        // boot code can occupy bytes where MBR records normally live; only
        // accept raw LBA 0 if the complete FAT volume validation succeeds.
        if (readIoBlocks(0, 1, g_sectorBuffer) == STATUS_SUCCESS) {
            bool protective{};
            if (g_sectorBuffer[510] == 0x55 && g_sectorBuffer[511] == 0xAA)
                for (size_t i = 0; i < 4; ++i) protective |= g_sectorBuffer[446 + i * 16 + 4] == 0xEE;
            if (!protective && mountVolume(g_volumes[0], 0, current->sectors, 'C')) {
                g_volumeCount = 1;
                return STATUS_SUCCESS;
            }
        }
        return STATUS_UNRECOGNIZED_VOLUME;
    }
    if (result.kind == TableKind::None) {
        if (!mountVolume(g_volumes[0], 0, current->sectors, 'C'))
            return STATUS_UNRECOGNIZED_VOLUME;
        g_volumeCount = 1;
        return STATUS_SUCCESS;
    }
    if (result.kind == TableKind::Mbr && result.count == 0 &&
        mountVolume(g_volumes[0], 0, current->sectors, 'C')) {
        g_volumeCount = 1;
        return STATUS_SUCCESS;
    }

    static constexpr uint8_t basicDataGuid[16] = {
        0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,
        0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
    static constexpr uint8_t efiSystemGuid[16] = {
        0x28,0x73,0x2A,0xC1,0xF8,0x1F,0xD2,0x11,
        0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
    static constexpr char driveLetters[3] = {'S', 'C', 'D'};
    bool mounted{};
    for (size_t i = 0; i < result.count && g_volumeCount < 3; ++i) {
        const Extent& extent = g_partitionExtents[i];
        if (result.kind == TableKind::Mbr) {
            if (extent.mbrType != 0x0B && extent.mbrType != 0x0C && extent.mbrType != 0xEF)
                continue;
        } else {
            const bool recognized = std::memcmp(extent.typeGuid, basicDataGuid, 16) == 0 ||
                                    std::memcmp(extent.typeGuid, efiSystemGuid, 16) == 0;
            if (!recognized) continue;
        }
        if (mountVolume(g_volumes[g_volumeCount], extent.firstLba,
                        extent.sectorCount, driveLetters[g_volumeCount])) {
            ++g_volumeCount;
            mounted = true;
        }
    }
    return mounted ? STATUS_SUCCESS : STATUS_UNRECOGNIZED_VOLUME;
}

bool convertPath(const char16_t* name, size_t length, char (&path)[kMaximumPath],
                 FatVolume*& volume) noexcept {
    volume = nullptr;
    if (!name || !length) return false;
    size_t offset{};
    if (length >= 4 && name[0] == u'\\' && name[1] == u'?' &&
        name[2] == u'?' && name[3] == u'\\') offset = 4;
    else if (length >= 4 && name[0] == u'\\' && name[1] == u'\\' &&
             name[2] == u'?' && name[3] == u'\\') offset = 4;

    if (length - offset < 2 || name[offset + 1] != u':') return false;
    const char16_t drive = name[offset];
    for (uint32_t i = 0; i < 3; ++i)
        if (g_volumes[i].Mounted && (drive == g_volumes[i].Drive ||
            drive == static_cast<char16_t>(g_volumes[i].Drive + ('a' - 'A'))))
            volume = &g_volumes[i];
    if (!volume) return false;
    offset += 2;
    while (offset < length && (name[offset] == u'\\' || name[offset] == u'/')) ++offset;
    if (offset == length) {
        path[0] = 0;
        return true;
    }
    size_t output{};
    size_t component{};
    for (size_t i = offset; i < length; ++i) {
        const char16_t ch = name[i];
        if (ch == 0 || ch > 0x7F || output + 1 >= sizeof(path)) return false;
        if (ch == u'\\' || ch == u'/') {
            if (!component || component > 127) return false;
            if (component == 1 && path[output - 1] == '.') return false;
            if (component == 2 && path[output - 1] == '.' && path[output - 2] == '.') return false;
            component = 0;
            path[output++] = '\\';
        } else {
            ++component;
            path[output++] = static_cast<char>(ch);
        }
    }
    if (!component || component > 127) return false;
    path[output] = 0;
    return true;
}


#undef g_volumes
#undef g_volumeCount
NtStatus readFile(void* opaque, uint64_t offset, void* buffer, size_t length, size_t* done) noexcept {
    if (!opaque || !done || (!buffer && length)) return NtStatus::InvalidParameter;
    *done = 0; Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.owner || file.owner->offline) return NtStatus::DeviceNotReady;
    current = file.owner;
    if (!refreshFileEntry(*file.volume, file.entry)) { current = nullptr; return NtStatus::DeviceDataError; }
    size_t transferred{};
    const bool ok = readFileAt(*file.volume, file.entry, offset, buffer, length, transferred);
    current = nullptr; *done = transferred;
    return !ok ? NtStatus::DeviceDataError : transferred < length ? NtStatus::EndOfFile : NtStatus::Success;
}
NtStatus writeFile(void* opaque, uint64_t offset, const void* buffer,
                   size_t length, size_t* done) noexcept {
    if (!opaque || !done || (!buffer && length)) return NtStatus::InvalidParameter;
    *done = 0; Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.owner || file.owner->offline) return NtStatus::DeviceNotReady;
    if (file.imageReferences) return NtStatus::SharingViolation;
    current = file.owner;
    if (!refreshFileEntry(*file.volume, file.entry)) { current = nullptr; return NtStatus::DeviceDataError; }
    const uint32_t currentSize = __atomic_load_n(&file.entry.Size, __ATOMIC_ACQUIRE);
    if (offset != currentSize || offset > UINT32_MAX || length > UINT32_MAX - offset)
        { current = nullptr; return NtStatus::InvalidParameter; }
    size_t transferred{};
    const bool ok = writeFileAt(*file.volume, file.entry, offset, buffer, length, transferred);
    current = nullptr; *done = transferred;
    return ok ? NtStatus::Success : NtStatus::DeviceDataError;
}
NtStatus flushFile(void* opaque) noexcept {
    if (!opaque) return NtStatus::InvalidParameter;
    Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.owner || file.owner->offline || !file.owner->device)
        return NtStatus::DeviceNotReady;
    if (!file.owner->device->writable()) return NtStatus::MediaWriteProtected;
    current = file.owner;
    const NtStatus status = file.owner->device->flush();
    current = nullptr;
    return status;
}
NtStatus acquireImage(void* opaque) noexcept {
    if (!opaque) return NtStatus::InvalidParameter;
    Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.owner || file.owner->offline || file.pendingCreate)
        return NtStatus::DeviceNotReady;
    if (file.imageReferences == static_cast<size_t>(-1)) return NtStatus::NoMemory;
    if (!file.owner->device->acquireImageLease()) return NtStatus::DeviceBusy;
    ++file.imageReferences;
    return NtStatus::Success;
}
void releaseImage(void* opaque) noexcept {
    if (!opaque) __builtin_trap();
    void* control{}; void (*release)(void*) noexcept{};
    {
        Gate guard; if (!guard.held) __builtin_trap();
        auto& file = *static_cast<FileContext*>(opaque);
        if (!file.inUse || !file.imageReferences) __builtin_trap();
        file.owner->device->releaseImageLease();
        if (!--file.imageReferences) {
            control = file.imageControl; release = file.releaseControl;
            file.imageControl = nullptr; file.retainControl = nullptr; file.releaseControl = nullptr;
        }
    }
    if (control) release(control);
}
NtStatus publishImageControl(void* opaque, void* candidate, bool (*retain)(void*) noexcept,
                             void (*release)(void*) noexcept, void** output) noexcept {
    if (!opaque || !candidate || !retain || !release || !output) return NtStatus::InvalidParameter;
    Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.imageReferences) return NtStatus::InvalidDeviceState;
    if (file.imageControl) {
        if (file.retainControl != retain || file.releaseControl != release ||
            !retain(file.imageControl)) return NtStatus::InvalidDeviceState;
        *output = file.imageControl;
        return NtStatus::Success;
    }
    if (!retain(candidate)) return NtStatus::NoMemory;
    file.imageControl = candidate; file.retainControl = retain; file.releaseControl = release;
    *output = candidate;
    return NtStatus::Success;
}
uint64_t fileSize(void* opaque) noexcept {
    if (!opaque) return 0;
    auto& file = *static_cast<FileContext*>(opaque);
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return __atomic_load_n(&file.entry.Size, __ATOMIC_ACQUIRE);
    Gate guard;
    if (!guard.held || !file.inUse || !file.owner || file.owner->offline) return 0;
    current = file.owner;
    (void)refreshFileEntry(*file.volume, file.entry);
    current = nullptr;
    return __atomic_load_n(&file.entry.Size, __ATOMIC_ACQUIRE);
}

// Caller holds the filesystem gate. The installation layout is the one
// staged by MicaNT; the drive is discovered from the mounted filesystem,
// never guessed from the order of devices or from a hard-coded drive letter.
void publishSystemRoot(char drive) noexcept {
    auto* shared = hal::amd64::bootstrapKuserSharedData();
    if (!shared) return;
    auto* root = shared->ntSystemRoot;
    constexpr char16_t suffix[] = u":\\MicaNT";
    if (__atomic_load_n(&root[0], __ATOMIC_ACQUIRE) == static_cast<char16_t>(drive)) {
        if (!drive) return;
        bool unchanged = true;
        for (size_t i = 0; i < sizeof(suffix) / sizeof(suffix[0]); ++i)
            if (root[i + 1] != suffix[i]) { unchanged = false; break; }
        if (unchanged) return;
    }
    __atomic_store_n(&root[0], char16_t{}, __ATOMIC_RELEASE);
    for (size_t i = 0; i < sizeof(suffix) / sizeof(suffix[0]); ++i) root[i + 1] = suffix[i];
    if (drive) __atomic_store_n(&root[0], static_cast<char16_t>(drive), __ATOMIC_RELEASE);
}

NtStatus selectSystemRootLocked() noexcept {
    if (systemRootMount) {
        for (auto& mount : mounts) {
            if (mount.token != systemRootMount || mount.offline) continue;
            for (const auto& volume : mount.volumes) {
                if (volume.Mounted && volume.Drive == systemRootDrive) {
                    publishSystemRoot(systemRootDrive);
                    return NtStatus::Success;
                }
            }
        }
        publishSystemRoot(0);
        // Once selected, removing the system volume must not silently
        // redirect executable or trust-store lookups to another disk.
        return NtStatus::DeviceNotReady;
    }
    uint64_t foundMount{};
    char foundDrive{};
    for (auto& mount : mounts) {
        if (!mount.token || mount.offline) continue;
        current = &mount;
        for (auto& volume : mount.volumes) {
            if (!volume.Mounted || !volume.Drive) continue;
            FatDirEntry marker{};
            const auto markerStatus = findFile(volume, "MicaNT\\System32\\ntdll.dll", marker);
            if (markerStatus == NtStatus::ObjectNameNotFound || markerStatus == NtStatus::ObjectPathNotFound)
                continue;
            if (markerStatus != NtStatus::Success) { current = nullptr; return markerStatus; }
            if (marker.Directory || !marker.Size) continue;
            if (foundMount) { current = nullptr; return NtStatus::ObjectNameCollision; }
            foundMount = mount.token;
            foundDrive = volume.Drive;
        }
    }
    current = nullptr;
    if (!foundMount) { publishSystemRoot(0); return NtStatus::ObjectPathNotFound; }
    systemRootMount = foundMount;
    systemRootDrive = foundDrive;
    publishSystemRoot(foundDrive);
    return NtStatus::Success;
}

NtStatus commitFileCreate(void* opaque) noexcept {
    if (!opaque) return NtStatus::InvalidParameter;
    Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.owner || file.owner->offline || !file.volume || !file.pendingCreate)
        return NtStatus::InvalidDeviceState;
    current = file.owner;
    const bool valid = refreshFileEntry(*file.volume, file.entry) &&
        !file.entry.FirstCluster && !file.entry.Size;
    current = nullptr;
    if (!valid) return NtStatus::DeviceDataError;
    file.pendingCreate = false;
    return NtStatus::Success;
}
void releaseFile(void* opaque) noexcept {
    if (!opaque) return; Gate guard; if (!guard.held) __builtin_trap();
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.references) __builtin_trap();
    if (--file.references) return;
    if (file.imageReferences) __builtin_trap();
    if (file.pendingCreate && file.inUse && file.owner && !file.owner->offline && file.volume) {
        // Never retire a directory slot if another handle has acquired it or
        // if the on-disk identity/size has changed since creation.
        bool shared = false;
        for (const auto& other : file.owner->files) {
            if (&other != &file && other.inUse && other.volume == file.volume &&
                other.entry.DirectorySector == file.entry.DirectorySector &&
                other.entry.DirectoryOffset == file.entry.DirectoryOffset) { shared = true; break; }
        }
        current = file.owner;
        if (!shared && refreshFileEntry(*file.volume, file.entry) &&
            !file.entry.FirstCluster && !file.entry.Size) {
            uint8_t deleted[32]{}; deleted[0] = 0xE5;
            // A failed write leaves an empty file rather than risking data
            // belonging to a concurrent or independently modified entry.
            (void)writeDirectorySlot(*file.volume, file.entry.DirectorySector,
                                     file.entry.DirectoryOffset, deleted);
        }
        current = nullptr;
    }
    file = {};
}

NtStatus queryDirectoryEntry(void* opaque, size_t requestedIndex,
                             FileBothDirInformation* output) noexcept {
    if (!opaque || !output) return NtStatus::InvalidParameter;
    Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    auto& file = *static_cast<FileContext*>(opaque);
    if (!file.inUse || !file.directory || !file.owner || file.owner->offline || !file.volume)
        return NtStatus::InvalidHandle;
    current = file.owner;
    uint32_t cluster = file.directoryCluster;
    const uint32_t maxClusters = file.volume->DataClusterCount + 1;
    size_t index = 0;
    for (uint32_t traversed = 0; traversed < maxClusters; ++traversed) {
        if (cluster < 2 || cluster - 2 >= file.volume->DataClusterCount) {
            current = nullptr;
            return NtStatus::DeviceDataError;
        }
        const uint32_t sectors = sectorsPerCluster(*file.volume);
        uint32_t clusterLba{};
        if (!clusterToLba(*file.volume, cluster, clusterLba) ||
            !readVolume(*file.volume, clusterLba, sectors, g_clusterBuffer)) {
            current = nullptr;
            return NtStatus::DeviceDataError;
        }
        const size_t entryCount = static_cast<size_t>(sectors) * kSectorSize / 32;
        for (size_t i = 0; i < entryCount; ++i) {
            const uint8_t* item = g_clusterBuffer + i * 32;
            if (item[0] == 0) { current = nullptr; return NtStatus::NoMoreFiles; }
            if (item[0] == 0xE5 || item[11] == 0x0F || (item[11] & 0x08)) continue;
            FatDirEntry entry{};
            formatShortName(item, entry.ShortName, sizeof(entry.ShortName));
            if (!entry.ShortName[0] || equalAsciiInsensitive(entry.ShortName, ".") ||
                equalAsciiInsensitive(entry.ShortName, "..")) continue;
            if (index++ != requestedIndex) continue;
            *output = {};
            for (size_t c = 0; entry.ShortName[c] && c < std::size(output->fileName) - 1; ++c)
                output->fileName[c] = static_cast<wchar_t>(static_cast<uint8_t>(entry.ShortName[c]));
            size_t nameLength = 0;
            while (nameLength < std::size(output->fileName) && output->fileName[nameLength]) ++nameLength;
            output->fileNameLength = static_cast<uint32_t>(nameLength * sizeof(wchar_t));
            output->fileAttributes = (item[11] & 0x10u) ? 0x10u : 0x80u;
            output->endOfFile.quadPart = loadLe32(item + 28);
            output->allocationSize.quadPart = output->endOfFile.quadPart;
            current = nullptr;
            return NtStatus::Success;
        }
        uint32_t next{};
        if (!readFatEntry(*file.volume, cluster, next)) {
            current = nullptr;
            return NtStatus::DeviceDataError;
        }
        if (next >= 0x0FFFFFF8u) { current = nullptr; return NtStatus::NoMoreFiles; }
        cluster = next;
    }
    current = nullptr;
    return NtStatus::DeviceDataError;
}

NtStatus openFile(uint64_t targetMount, std::u16string_view name, bool openIf, uint32_t desiredAccess,
                  io::FileBackend& backend) noexcept {
    backend = {}; Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    // Root-store protection follows the selected system volume, not a
    // letter that might belong to another device.
    // Discovery is initiated explicitly by IoQuerySystemDirectory after the
    // boot providers have mounted. Ordinary file opens must not select an
    // early removable volume before the system disk is available.
    const auto rootStatus = systemRootMount ? selectSystemRootLocked() : NtStatus::DeviceNotReady;
    char pathBuffer[kMaximumPath]{}; FatVolume* volume{};
    for (auto& mount : mounts) {
        if (!mount.token || mount.offline || (targetMount && mount.token != targetMount)) continue;
        current = &mount;
        if (!convertPath(name.data(), name.size(), pathBuffer, volume)) continue;
        if (rootStatus != NtStatus::Success && equalAsciiInsensitive(pathBuffer, "ROOT.DER")) {
            current = nullptr;
            return rootStatus;
        }
        bool isSystemRootStore = mount.token == systemRootMount && volume->Drive == systemRootDrive &&
            equalAsciiInsensitive(pathBuffer, "ROOT.DER");
        if (isSystemRootStore && !rootStoreAccessAllowed(desiredAccess)) {
            current = nullptr;
            return NtStatus::AccessDenied;
        }
        FatDirEntry entry{};
        bool created{};
        const auto lookup = findFile(*volume, pathBuffer, entry);
        if (lookup != NtStatus::Success) {
            if (lookup != NtStatus::ObjectNameNotFound) { current = nullptr; return lookup; }
            if (openIf && !mount.device->writable()) {
                current = nullptr;
                return NtStatus::MediaWriteProtected;
            }
            if (!openIf) { current = nullptr; return NtStatus::ObjectNameNotFound; }
            const auto createStatus = createRootFile(*volume, pathBuffer, entry);
            if (createStatus != NtStatus::Success) { current = nullptr; return createStatus; }
            created = true;
        }
        // FAT long names and their 8.3 aliases designate the same directory
        // entry. Enforce the store ACL on the resolved short name as well as
        // the input path, otherwise an existing LFN can bypass the check.
        if (rootStatus != NtStatus::Success && !containsPathSeparator(pathBuffer) &&
            (equalAsciiInsensitive(entry.ShortName, "ROOT.DER") ||
             equalAsciiInsensitive(entry.Name, "ROOT.DER"))) {
            current = nullptr;
            return rootStatus;
        }
        if (mount.token == systemRootMount && volume->Drive == systemRootDrive && !containsPathSeparator(pathBuffer) &&
            (equalAsciiInsensitive(entry.ShortName, "ROOT.DER") ||
             equalAsciiInsensitive(entry.Name, "ROOT.DER"))) {
            isSystemRootStore = true;
            if (!rootStoreAccessAllowed(desiredAccess)) {
                current = nullptr;
                return NtStatus::AccessDenied;
            }
        }
        if (entry.Directory) {
            FileContext* directory{};
            for (auto& candidate : mount.files) if (!candidate.inUse) {
                candidate = {&mount, volume, entry, true, false, 1, 0};
                candidate.directory = true;
                candidate.directoryCluster = entry.FirstCluster;
                directory = &candidate;
                break;
            }
            if (!directory) { current = nullptr; return NtStatus::NoMemory; }
            backend = {directory, 0, name, readFile, releaseFile};
            backend.queryDirectory = queryDirectoryEntry;
            backend.directory = true;
            current = nullptr;
            return NtStatus::Success;
        }
        if ((desiredAccess & 0x00000002u) && !mount.device->writable()) {
            current = nullptr;
            return NtStatus::MediaWriteProtected;
        }
        // Do not expose an entry until the creating caller has obtained and
        // published its handle. This also makes abort safe under the gate.
        for (const auto& file : mount.files) {
            if (file.inUse && file.pendingCreate && file.volume == volume &&
                file.entry.DirectorySector == entry.DirectorySector &&
                file.entry.DirectoryOffset == entry.DirectoryOffset) {
                current = nullptr;
                return NtStatus::DeviceBusy;
            }
        }
        FileContext* stream{};
        for (auto& file : mount.files) {
            if (file.inUse && file.volume == volume &&
                file.entry.DirectorySector == entry.DirectorySector &&
                file.entry.DirectoryOffset == entry.DirectoryOffset) {
                stream = &file;
                break;
            }
        }
        if (!stream) for (auto& file : mount.files) if (!file.inUse) {
            file = {&mount, volume, entry, true, created, 0};
            stream = &file;
            break;
        }
        if (stream) {
            if (stream->references == static_cast<size_t>(-1)) {
                current = nullptr;
                return NtStatus::NoMemory;
            }
            ++stream->references;
            backend = {stream, entry.Size, name, readFile, releaseFile};
            backend.write = mount.device->writable() ? writeFile : nullptr;
            backend.flush = mount.device->writable() ? flushFile : nullptr;
            backend.created = created;
            if (created) backend.commitCreate = commitFileCreate;
            backend.querySize = fileSize;
            backend.acquireImage = acquireImage;
            backend.releaseImage = releaseImage;
            backend.publishImageControl = publishImageControl;
            if (isSystemRootStore) backend.securityDescriptor = &g_rootStoreAcl;
            current = nullptr; return NtStatus::Success;
        }
        if (created) {
            uint8_t deletedEntry[32]{};
            deletedEntry[0] = 0xE5;
            if (!writeDirectorySlot(*volume, entry.DirectorySector, entry.DirectoryOffset, deletedEntry)) {
                current = nullptr;
                return NtStatus::DeviceDataError;
            }
        }
        current = nullptr; return NtStatus::NoMemory;
    }
    current = nullptr; return NtStatus::ObjectPathNotFound;
}
}
NtStatus initialize() noexcept {
    if (initialized) return NtStatus::Conflict;
    se::SeInitializeSystemRootStoreAcl(g_rootStoreAcl);
    initialized = true;
    return NtStatus::Success;
}
NtStatus systemDirectory(char16_t* output, size_t capacity, size_t& length) noexcept {
    length = 0;
    if (!output) return NtStatus::InvalidParameter;
    if (!initialized) return NtStatus::DeviceNotReady;
    constexpr char16_t suffix[] = u":\\MicaNT\\System32";
    constexpr size_t chars = sizeof(suffix) / sizeof(suffix[0]);
    if (capacity < chars + 1) return NtStatus::BufferTooSmall;
    Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
    const auto status = selectSystemRootLocked();
    if (status != NtStatus::Success) return status;
    output[0] = static_cast<char16_t>(systemRootDrive);
    for (size_t i = 0; i < chars; ++i) output[i + 1] = suffix[i];
    length = chars; // suffix includes the terminating NUL.
    return NtStatus::Success;
}
NtStatus open(std::u16string_view name, bool openIf, uint32_t desiredAccess,
              io::FileBackend& backend) noexcept {
    if (!initialized) return NtStatus::DeviceNotReady;
    return openFile(0, name, openIf, desiredAccess, backend);
}
NtStatus readMountedFile(const void* owner, uint64_t token,
                         std::u16string_view relativePath, void* buffer,
                         size_t capacity, size_t& bytesRead) noexcept {
    bytesRead = 0;
    if (!owner || !token || relativePath.empty() || !buffer || !capacity ||
        relativePath.size() > kMaximumPath - 4 || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    char drives[3]{};
    uint32_t driveCount{};
    {
        Gate guard;
        if (!guard.held) return NtStatus::DeviceNotReady;
        for (auto& mount : mounts) if (mount.token == token && mount.owner == owner && !mount.offline) {
            for (uint32_t i = 0; i < mount.volumeCount && driveCount < 3; ++i)
                if (mount.volumes[i].Mounted) drives[driveCount++] = mount.volumes[i].Drive;
            break;
        }
    }
    if (!driveCount) return NtStatus::InvalidHandle;
    char16_t absolute[kMaximumPath]{};
    for (size_t i = 0; i < relativePath.size(); ++i) {
        const char16_t ch = relativePath[i];
        if (!ch || ch == u':' || ch == u'/' || (ch == u'\\' && i == 0))
            return NtStatus::InvalidParameter;
        absolute[i + 3] = ch;
    }
    NtStatus status = NtStatus::ObjectNameNotFound;
    io::FileBackend backend{};
    for (uint32_t i = 0; i < driveCount; ++i) {
        absolute[0] = static_cast<char16_t>(drives[i]);
        absolute[1] = u':'; absolute[2] = u'\\';
        status = openFile(token, std::u16string_view(absolute, relativePath.size() + 3),
                          false, 1, backend);
        if (status == NtStatus::Success) break;
        if (status != NtStatus::ObjectNameNotFound && status != NtStatus::ObjectPathNotFound)
            return status;
    }
    if (status != NtStatus::Success) return status;
    if (!backend.read || !backend.release) return NtStatus::InvalidDeviceState;
    if (backend.size > capacity) { backend.release(backend.context); return NtStatus::BufferTooSmall; }
    const auto readStatus = backend.read(backend.context, 0, buffer,
                                          static_cast<size_t>(backend.size), &bytesRead);
    backend.release(backend.context);
    return readStatus;
}
NtStatus mount(const void* owner, Handle handle, uint64_t& token) noexcept {
    token = 0; if (!owner) return NtStatus::InvalidParameter;
    if (!initialized) return NtStatus::DeviceNotReady;
    io::BlockDeviceObject* device{}; ob::ObjectHeader* reference{};
    auto status = io::IoReferenceBlockDeviceHandle(handle, 1, device, reference);
    if (status != NtStatus::Success) return status;
    if (device->blockSize() != kSectorSize) { ob::ObDereferenceObject(reference); return NtStatus::NotSupported; }
    {
        Gate guard;
        if (!guard.held) status = NtStatus::DeviceNotReady;
        else {
            // A second mount of the same device would create a second stream
            // identity for the same directory slots and bypass image leases.
            for (const auto& mounted : mounts) {
                if (mounted.token && mounted.device == device) {
                    status = NtStatus::DeviceBusy;
                    break;
                }
            }
            Mount* slot{}; for (auto& m : mounts) if (!m.token) { slot = &m; break; }
            if (status != NtStatus::Success) { /* retain existing mount */ }
            else if (!slot || !nextToken) status = NtStatus::InsufficientResources;
            else {
                *slot = {}; slot->owner = owner; slot->device = device; slot->reference = reference; slot->sectors = device->totalBlocks();
                current = slot;
                status = static_cast<NtStatus>(mountPartitions());
                current = nullptr;
                if (status == NtStatus::Success) {
                    bool used[26]{};
                    for (const auto& other : mounts) if (other.token)
                        for (uint32_t i = 0; i < other.volumeCount; ++i)
                            if (other.volumes[i].Drive >= 'A' && other.volumes[i].Drive <= 'Z') used[other.volumes[i].Drive - 'A'] = true;
                    for (uint32_t i = 0; i < slot->volumeCount; ++i) {
                        auto& drive = slot->volumes[i].Drive;
                        if (drive < 'A' || drive > 'Z' || used[drive - 'A']) {
                            drive = 0; for (unsigned j = 2; j < 26; ++j) if (!used[j]) { drive = static_cast<char>('A' + j); break; }
                        }
                        if (!drive) { status = NtStatus::InsufficientResources; break; }
                        used[drive - 'A'] = true;
                    }
                }
                if (status == NtStatus::Success) { slot->token = nextToken++; token = slot->token; reference = nullptr; }
                else *slot = {};
            }
        }
    }
    if (reference) ob::ObDereferenceObject(reference);
    return status;
}
NtStatus unmount(const void* owner, uint64_t token) noexcept {
    if (!owner || !token) return NtStatus::InvalidParameter;
    ob::ObjectHeader* reference{}; NtStatus status = NtStatus::InvalidHandle;
    {
        Gate guard; if (!guard.held) return NtStatus::DeviceNotReady;
        for (auto& m : mounts) if (m.token == token && m.owner == owner) {
            m.offline = true;
            if (systemRootMount == token) {
                publishSystemRoot(0);
            }
            for (const auto& file : m.files) if (file.inUse) return NtStatus::DeviceBusy;
            reference = m.reference; m = {}; status = NtStatus::Success; break;
        }
    }
    if (reference) ob::ObDereferenceObject(reference);
    return status;
}
}
