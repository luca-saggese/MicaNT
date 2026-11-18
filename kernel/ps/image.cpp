#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/executive/image_metadata.hpp"
#include "micant/km/executive/section.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/core/syscalls.hpp"
#include <new>
#include <cstring>

extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;

namespace micant::sys {
NtStatus mapBootstrapImageView(mm::ProcessAddressSpace& space, uintptr_t& base, size_t size) noexcept;
NtStatus mapBootstrapFileImageView(mm::ProcessAddressSpace& space, uintptr_t& base,
                                   section::SectionObject& image,
                                   ob::ObjectHeader* sectionReference) noexcept;
NtStatus mapBootstrapRelocatedFileImageView(mm::ProcessAddressSpace& space, uintptr_t& base,
                                            section::SectionObject& image,
                                            ob::ObjectHeader* sectionReference) noexcept;
NtStatus referenceProcessImageSection(Handle handle, ob::ObjectHeader*& reference) noexcept;
}

namespace micant::ps {
namespace {
void traceImageInitializationFailure(const char* stage, NtStatus status) noexcept {
    if (status == NtStatus::Success) return;
    MicantNetbootSerialWrite("[MicaNT Ps] SEC_IMAGE initialization ");
    MicantNetbootSerialWrite(stage);
    MicantNetbootSerialWrite(" failed status=");
    MicantNetbootSerialWriteNumber(static_cast<uint32_t>(status));
    MicantNetbootSerialWrite("\r\n");
}

void traceForwarderFailure(const um::NativeUserImageSet64::ForwarderFailure& failure) noexcept {
    const auto writeField = [](const char* label, std::string_view value) noexcept {
        char text[260]{};
        const size_t length = value.size() < sizeof(text) - 1 ? value.size() : sizeof(text) - 1;
        if (length) std::memcpy(text, value.data(), length);
        MicantNetbootSerialWrite(label);
        MicantNetbootSerialWrite(text);
    };
    MicantNetbootSerialWrite("[MicaNT Ps] Forwarder resolve rejected");
    writeField(" importer=", failure.importer);
    writeField(" library=", failure.library);
    writeField(" symbol=", failure.symbol);
    MicantNetbootSerialWrite(" status=");
    MicantNetbootSerialWriteNumber(static_cast<uint32_t>(failure.status));
    MicantNetbootSerialWrite("\r\n");
}

bool initializeLoaderMetadata(const um::NativeUserImageSet64& images, uintptr_t address,
                              std::span<const std::u16string_view> paths) noexcept;

struct ScopedHandle {
    Handle value{};
    ~ScopedHandle() {
        if (value && sys::NtClose(value) != NtStatus::Success) __builtin_trap();
    }
};

struct ScopedObjectReference {
    ob::ObjectHeader* value{};
    ~ScopedObjectReference() { if (value) ob::ObDereferenceObject(value); }
};

NtStatus buildSystemDllPath(std::string_view library, char16_t (&path)[256],
                            size_t& pathLength) noexcept {
    pathLength = 0;
    if (library.size() < 5 || library.size() > 128 ||
        library[library.size() - 4] != '.' ||
        (library[library.size() - 3] != 'd' && library[library.size() - 3] != 'D') ||
        (library[library.size() - 2] != 'l' && library[library.size() - 2] != 'L') ||
        (library[library.size() - 1] != 'l' && library[library.size() - 1] != 'L'))
        return NtStatus::NotSupported;
    for (const char ch : library) {
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if (!alpha && !digit && ch != '-' && ch != '_' && ch != '.')
            return NtStatus::NotSupported;
    }
    size_t prefixLength{};
    const auto status = io::IoQuerySystemDirectory(path, sizeof(path) / sizeof(path[0]), prefixLength);
    if (status != NtStatus::Success) return status;
    if (prefixLength + 1 + library.size() >= sizeof(path) / sizeof(path[0]))
        return NtStatus::BufferTooSmall;
    path[prefixLength++] = u'\\';
    for (size_t i = 0; i < library.size(); ++i)
        path[prefixLength + i] = static_cast<char16_t>(static_cast<uint8_t>(library[i]));
    pathLength = prefixLength + library.size();
    path[pathLength] = 0;
    return NtStatus::Success;
}

NtStatus buildNtdllPath(char16_t (&path)[256], size_t& pathLength) noexcept {
    return buildSystemDllPath("ntdll.dll", path, pathLength);
}

NtStatus buildImageModuleName(std::u16string_view path, char (&storage)[260],
                              std::string_view& moduleName) noexcept {
    moduleName = {};
    size_t nameOffset{};
    for (size_t index = 0; index < path.size(); ++index)
        if (path[index] == u'\\' || path[index] == u'/') nameOffset = index + 1;
    const size_t nameLength = path.size() - nameOffset;
    if (!nameLength || nameLength >= sizeof(storage)) return NtStatus::NotSupported;
    for (size_t index = 0; index < nameLength; ++index) {
        const char16_t ch = path[nameOffset + index];
        if (!ch || ch > 0x7f || ch == u':' || ch == u'\\' || ch == u'/')
            return NtStatus::NotSupported;
        storage[index] = static_cast<char>(ch);
    }
    moduleName = {storage, nameLength};
    return NtStatus::Success;
}

struct ApiSetNamespaceV6 {
    uint32_t version, size, flags, count, entryOffset, hashOffset, hashFactor;
};
struct ApiSetHashEntryV6 { uint32_t hash, index; };
struct ApiSetNamespaceEntryV6 {
    uint32_t flags, nameOffset, nameLength, hashedLength, valueOffset, valueCount;
};
struct ApiSetValueEntryV6 {
    uint32_t flags, nameOffset, nameLength, valueOffset, valueLength;
};
static_assert(sizeof(ApiSetNamespaceV6) == 28);
static_assert(sizeof(ApiSetHashEntryV6) == 8);
static_assert(sizeof(ApiSetNamespaceEntryV6) == 24);
static_assert(sizeof(ApiSetValueEntryV6) == 20);

class ApiSetSchemaV6 {
public:
    [[nodiscard]] bool fileAbsent() const noexcept { return fileAbsent_; }
    NtStatus namespaceBlob(std::span<const uint8_t>& blob) noexcept {
        blob = {};
        const auto status = ensureLoaded();
        if (status != NtStatus::Success) return status;
        blob = {schemaBytes_, namespaceSize_};
        return NtStatus::Success;
    }

    NtStatus resolve(std::string_view contract, std::string_view importingModule,
                     std::string_view& host) noexcept {
        host = {};
        auto status = ensureLoaded();
        if (status != NtStatus::Success) return status;
        if (contract.size() >= 4 && suffixDll(contract)) contract.remove_suffix(4);
        if (!isApiSetName(contract) || contract.size() > 255) return NtStatus::NotSupported;
        size_t lastDash = contract.size();
        for (size_t i = 0; i < contract.size(); ++i)
            if (contract[i] == '-') lastDash = i;
        if (lastDash == contract.size() || !lastDash || lastDash * sizeof(char16_t) > UINT32_MAX)
            return NtStatus::NotSupported;
        uint32_t wantedHash{};
        for (size_t i = 0; i < lastDash; ++i)
            wantedHash = wantedHash * namespace_.hashFactor + lowerAscii(contract[i]);

        size_t first = 0;
        size_t last = namespace_.count;
        while (first < last) {
            const size_t middle = first + (last - first) / 2;
            ApiSetHashEntryV6 hash{};
            if (!readAt(namespace_.hashOffset + middle * sizeof(hash), hash))
                return NtStatus::InvalidImageFormat;
            if (hash.hash < wantedHash) first = middle + 1;
            else last = middle;
        }
        for (size_t index = first; index < namespace_.count; ++index) {
            ApiSetHashEntryV6 hash{};
            if (!readAt(namespace_.hashOffset + index * sizeof(hash), hash))
                return NtStatus::InvalidImageFormat;
            if (hash.hash != wantedHash) break;
            if (hash.index >= namespace_.count) return NtStatus::InvalidImageFormat;
            ApiSetNamespaceEntryV6 entry{};
            if (!readAt(namespace_.entryOffset + static_cast<size_t>(hash.index) * sizeof(entry), entry))
                return NtStatus::InvalidImageFormat;
            if (entry.hashedLength != lastDash * sizeof(char16_t) ||
                !equalsAscii(entry.nameOffset, entry.nameLength, contract)) continue;
            return selectHost(entry, importingModule, host);
        }
        return NtStatus::NotSupported;
    }

private:
    static bool suffixDll(std::string_view name) noexcept {
        const size_t offset = name.size() - 4;
        return name[offset] == '.' && lowerAscii(name[offset + 1]) == 'd' &&
            lowerAscii(name[offset + 2]) == 'l' && lowerAscii(name[offset + 3]) == 'l';
    }
    static char lowerAscii(char value) noexcept {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
    }
    static char16_t lowerAscii(char16_t value) noexcept {
        return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
    }
    static bool isApiSetName(std::string_view name) noexcept {
        if (name.size() < 4) return false;
        return (lowerAscii(name[0]) == 'a' && lowerAscii(name[1]) == 'p' &&
                lowerAscii(name[2]) == 'i' && name[3] == '-') ||
               (lowerAscii(name[0]) == 'e' && lowerAscii(name[1]) == 'x' &&
                lowerAscii(name[2]) == 't' && name[3] == '-');
    }
    [[nodiscard]] size_t dataSize() const noexcept {
        return namespaceSize_ ? namespaceSize_ : schemaSize_;
    }
    template <typename T>
    bool readAt(size_t offset, T& value) const noexcept {
        const size_t size = dataSize();
        if (offset > size || sizeof(T) > size - offset) return false;
        std::memcpy(&value, schemaBytes_ + offset, sizeof(value));
        return true;
    }
    bool validString(uint32_t offset, uint32_t length, bool allowEmpty) const noexcept {
        const size_t size = dataSize();
        if (length > size || (length & 1) || (offset & 1) ||
            offset > size || length > size - offset || (!allowEmpty && !length))
            return false;
        return true;
    }
    char16_t characterAt(uint32_t offset, size_t index) const noexcept {
        char16_t value{};
        std::memcpy(&value, schemaBytes_ + offset + index * sizeof(char16_t), sizeof(value));
        return value;
    }
    bool equalsAscii(uint32_t offset, uint32_t length, std::string_view ascii) const noexcept {
        if (!validString(offset, length, false) || length / sizeof(char16_t) != ascii.size()) return false;
        for (size_t i = 0; i < ascii.size(); ++i) {
            const char16_t value = characterAt(offset, i);
            if (value > 0x7f || lowerAscii(value) != static_cast<char16_t>(lowerAscii(ascii[i])))
                return false;
        }
        return true;
    }
    bool validModuleString(uint32_t offset, uint32_t length, bool allowEmpty,
                          bool requireDllSuffix) const noexcept {
        if (!validString(offset, length, allowEmpty) || length / sizeof(char16_t) > 260)
            return false;
        const size_t count = length / sizeof(char16_t);
        if (!count) return allowEmpty && !requireDllSuffix;
        for (size_t index = 0; index < count; ++index) {
            const char16_t value = characterAt(offset, index);
            const char16_t lower = lowerAscii(value);
            if (!((lower >= u'a' && lower <= u'z') || (lower >= u'0' && lower <= u'9') ||
                  value == u'-' || value == u'_' || value == u'.')) return false;
        }
        if (!requireDllSuffix) return true;
        return count >= 5 && characterAt(offset, count - 4) == u'.' &&
            lowerAscii(characterAt(offset, count - 3)) == u'd' &&
            lowerAscii(characterAt(offset, count - 2)) == u'l' &&
            lowerAscii(characterAt(offset, count - 1)) == u'l';
    }
    static bool checkedArray(uint32_t offset, uint32_t count, size_t itemSize,
                             uint32_t dataSize) noexcept {
        return !(offset & 3) && offset <= dataSize &&
            count <= (dataSize - offset) / itemSize;
    }
    uint32_t hashName(uint32_t offset, uint32_t byteLength) const noexcept {
        uint32_t hash{};
        for (size_t i = 0; i < byteLength / sizeof(char16_t); ++i)
            hash = hash * namespace_.hashFactor + lowerAscii(characterAt(offset, i));
        return hash;
    }
    NtStatus selectHost(const ApiSetNamespaceEntryV6& entry, std::string_view importer,
                        std::string_view& host) noexcept {
        ApiSetValueEntryV6 fallback{};
        bool hasFallback = false;
        for (uint32_t index = 0; index < entry.valueCount; ++index) {
            ApiSetValueEntryV6 value{};
            const size_t offset = static_cast<size_t>(entry.valueOffset) +
                                  static_cast<size_t>(index) * sizeof(value);
            if (!readAt(offset, value)) return NtStatus::InvalidImageFormat;
            if (!value.nameLength) { fallback = value; hasFallback = true; continue; }
            if (nameMatchesImporter(value.nameOffset, value.nameLength, importer)) {
                if (!value.valueLength) return NtStatus::NotSupported;
                return copyHost(value, host);
            }
        }
        if (!hasFallback || !fallback.valueLength) return NtStatus::NotSupported;
        return copyHost(fallback, host);
    }
    bool nameMatchesImporter(uint32_t offset, uint32_t length,
                             std::string_view importer) const noexcept {
        if (!validString(offset, length, false)) return false;
        if (equalsAscii(offset, length, importer)) return true;
        const bool valueHasDll = length >= 8 &&
            lowerAscii(characterAt(offset, length / 2 - 3)) == u'd' &&
            lowerAscii(characterAt(offset, length / 2 - 2)) == u'l' &&
            lowerAscii(characterAt(offset, length / 2 - 1)) == u'l' &&
            characterAt(offset, length / 2 - 4) == u'.';
        if (valueHasDll) {
            const std::string_view base{importer.data(), importer.size() >= 4 ? importer.size() - 4 : importer.size()};
            return equalsAscii(offset, length - 8, base);
        }
        if (importer.size() >= 4 && suffixDll(importer))
            return equalsAscii(offset, length, importer.substr(0, importer.size() - 4));
        return false;
    }
    NtStatus copyHost(const ApiSetValueEntryV6& value, std::string_view& host) noexcept {
        if (!validString(value.valueOffset, value.valueLength, false) ||
            value.valueLength / sizeof(char16_t) >= hostBuffer_.size())
            return NtStatus::InvalidImageFormat;
        const size_t length = value.valueLength / sizeof(char16_t);
        for (size_t i = 0; i < length; ++i) {
            const char16_t valueChar = characterAt(value.valueOffset, i);
            if (valueChar > 0x7f) return NtStatus::InvalidImageFormat;
            hostBuffer_[i] = static_cast<char>(valueChar);
        }
        host = {hostBuffer_.data(), length};
        if (length < 5 || !suffixDll(host)) return NtStatus::InvalidImageFormat;
        for (const char ch : host) {
            const char lower = lowerAscii(ch);
            if (!((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') ||
                  ch == '-' || ch == '_' || ch == '.')) return NtStatus::InvalidImageFormat;
        }
        return NtStatus::Success;
    }
    NtStatus ensureLoaded() noexcept {
        if (loadAttempted_) return loadStatus_;
        loadAttempted_ = true;
        ScopedHandle fileHandle;
        char16_t schemaPath[256]{};
        size_t schemaPathLength{};
        auto status = buildSystemDllPath("apisetschema.dll", schemaPath, schemaPathLength);
        if (status != NtStatus::Success) return loadStatus_ = status;
        status = io::IoOpenBackendFile(&fileHandle.value, 0x0001,
                                       {schemaPath, schemaPathLength});
        if (status != NtStatus::Success) {
            // Only an absent leaf in the selected mounted directory proves
            // that the optional schema is missing. An unresolved path or
            // provider failure must not be converted into a null PEB map.
            fileAbsent_ = status == NtStatus::ObjectNameNotFound;
            return loadStatus_ = status;
        }
        io::FileObject* file{};
        ScopedObjectReference fileReference;
        status = io::IoReferenceFileHandle(fileHandle.value, 0x0001, file, fileReference.value);
        if (status != NtStatus::Success || !file || !file->size() || file->size() > SIZE_MAX)
            return loadStatus_ = NtStatus::InvalidImageFormat;
        const size_t fileSize = static_cast<size_t>(file->size());
        fileBytes_.reset(new (std::nothrow_t{}) uint8_t[fileSize]);
        if (!fileBytes_) return loadStatus_ = NtStatus::NoMemory;
        size_t total{};
        while (total < fileSize) {
            size_t read{};
            status = file->read(total, fileBytes_.get() + total, fileSize - total, &read);
            if (status != NtStatus::Success || !read || read > fileSize - total)
                return loadStatus_ = status == NtStatus::Success ? NtStatus::EndOfFile : status;
            total += read;
        }
        pe::PeImageView64 peView;
        status = peView.inspect({fileBytes_.get(), fileSize});
        if (status != NtStatus::Success) return loadStatus_ = status;
        pe::ImageSectionHeader schemaSection{};
        bool foundSchemaSection = false;
        for (size_t i = 0; i < peView.headers().fileHeader.numberOfSections; ++i) {
            const auto candidate = peView.section(i);
            if (candidate.getName() != ".apiset") continue;
            if (foundSchemaSection) return loadStatus_ = NtStatus::InvalidImageFormat;
            schemaSection = candidate;
            foundSchemaSection = true;
        }
        if (!foundSchemaSection || !schemaSection.sizeOfRawData ||
            schemaSection.pointerToRawData > fileSize ||
            schemaSection.sizeOfRawData > fileSize - schemaSection.pointerToRawData)
            return loadStatus_ = NtStatus::NotSupported;
        schemaBytes_ = fileBytes_.get() + schemaSection.pointerToRawData;
        schemaSize_ = schemaSection.sizeOfRawData;
        if (schemaSize_ < sizeof(namespace_)) return loadStatus_ = NtStatus::InvalidImageFormat;
        std::memcpy(&namespace_, schemaBytes_, sizeof(namespace_));
        return loadStatus_ = validateSchema();
    }
    NtStatus validateSchema() noexcept {
        if (namespace_.version != 6 || namespace_.size < sizeof(namespace_) ||
            namespace_.size > schemaSize_ || !namespace_.count ||
            !namespace_.hashFactor ||
            !checkedArray(namespace_.entryOffset, namespace_.count, sizeof(ApiSetNamespaceEntryV6), namespace_.size) ||
            !checkedArray(namespace_.hashOffset, namespace_.count, sizeof(ApiSetHashEntryV6), namespace_.size))
            return namespace_.version == 6 ? NtStatus::InvalidImageFormat : NtStatus::NotSupported;
        namespaceSize_ = namespace_.size;
        std::unique_ptr<uint8_t[]> seen(new (std::nothrow_t{}) uint8_t[namespace_.count]{});
        if (!seen) return NtStatus::NoMemory;
        uint32_t previousHash{};
        for (uint32_t index = 0; index < namespace_.count; ++index) {
            ApiSetHashEntryV6 hash{};
            if (!readAt(namespace_.hashOffset + static_cast<size_t>(index) * sizeof(hash), hash) ||
                hash.index >= namespace_.count || (index && hash.hash < previousHash) || seen[hash.index])
                return NtStatus::InvalidImageFormat;
            previousHash = hash.hash;
            seen[hash.index] = 1;
            ApiSetNamespaceEntryV6 entry{};
            if (!readAt(namespace_.entryOffset + static_cast<size_t>(hash.index) * sizeof(entry), entry) ||
                !validString(entry.nameOffset, entry.nameLength, false) ||
                !entry.hashedLength || (entry.hashedLength & 1) ||
                entry.hashedLength > entry.nameLength || !entry.valueCount || entry.valueCount > 256 ||
                !checkedArray(entry.valueOffset, entry.valueCount, sizeof(ApiSetValueEntryV6), namespace_.size) ||
                hashName(entry.nameOffset, entry.hashedLength) != hash.hash)
                return NtStatus::InvalidImageFormat;
            for (size_t c = 0; c < entry.nameLength / sizeof(char16_t); ++c) {
                const char16_t ch = characterAt(entry.nameOffset, c);
                if (ch > 0x7f || !((lowerAscii(ch) >= u'a' && lowerAscii(ch) <= u'z') ||
                    (lowerAscii(ch) >= u'0' && lowerAscii(ch) <= u'9') || ch == u'-' || ch == u'~'))
                    return NtStatus::InvalidImageFormat;
            }
            for (uint32_t valueIndex = 0; valueIndex < entry.valueCount; ++valueIndex) {
                ApiSetValueEntryV6 value{};
                if (!readAt(entry.valueOffset + static_cast<size_t>(valueIndex) * sizeof(value), value) ||
                    !validModuleString(value.nameOffset, value.nameLength, true, false) ||
                    !validModuleString(value.valueOffset, value.valueLength, false, true))
                    return NtStatus::InvalidImageFormat;
            }
        }
        return NtStatus::Success;
    }

    ApiSetNamespaceV6 namespace_{};
    std::unique_ptr<uint8_t[]> fileBytes_{};
    const uint8_t* schemaBytes_{};
    size_t schemaSize_{};
    size_t namespaceSize_{};
    std::array<char, 260> hostBuffer_{};
    bool loadAttempted_{};
    bool fileAbsent_{};
    NtStatus loadStatus_{NtStatus::NotSupported};
};

NtStatus validateApiSetMapView(const mm::ProcessAddressSpace& addressSpace,
                               uintptr_t address, size_t size) noexcept;

NtStatus publishApiSetMap(mm::ProcessAddressSpace& addressSpace, uintptr_t pebAddress,
                          uintptr_t& apiSetMapAddress, size_t& apiSetMapSize,
                          ApiSetSchemaV6& schema) noexcept {
    constexpr size_t pebApiSetMapOffset = 0x68; // ReactOS PEB64 / public AMD64 layout.
    std::span<const uint8_t> namespaceBlob;
    auto status = schema.namespaceBlob(namespaceBlob);
    if (status != NtStatus::Success) {
        // A process whose imports require an API-set has already failed
        // resolution. For an image that needs none, absence of the optional
        // schema file must not prevent native startup; expose a null PEB map
        // rather than manufacturing an API-set namespace.
        if (!schema.fileAbsent() || apiSetMapAddress || apiSetMapSize || !pebAddress ||
            !hal::amd64::bootstrapUserRange(pebAddress + pebApiSetMapOffset,
                                            sizeof(uint64_t), true)) return status;
        *reinterpret_cast<uint64_t*>(pebAddress + pebApiSetMapOffset) = 0;
        return NtStatus::Success;
    }
    if (!pebAddress || !hal::amd64::bootstrapUserRange(pebAddress + pebApiSetMapOffset,
                                                         sizeof(uint64_t), false) ||
        namespaceBlob.empty() || namespaceBlob.size() > SIZE_MAX - mm::PageMask4KB)
        return NtStatus::InvalidImageFormat;
    if (apiSetMapAddress) {
        if (apiSetMapSize != namespaceBlob.size() || !hal::amd64::bootstrapUserRange(
                apiSetMapAddress, apiSetMapSize, false))
            return NtStatus::InvalidImageFormat;
        status = validateApiSetMapView(addressSpace, apiSetMapAddress, apiSetMapSize);
        if (status != NtStatus::Success) return status;
        if (std::memcmp(reinterpret_cast<const void*>(apiSetMapAddress), namespaceBlob.data(),
                        namespaceBlob.size()) == 0) {
            *reinterpret_cast<uint64_t*>(pebAddress + pebApiSetMapOffset) = apiSetMapAddress;
            return NtStatus::Success;
        }
    }
    const size_t mapSize = (namespaceBlob.size() + mm::PageMask4KB) & ~mm::PageMask4KB;
    uintptr_t mapped{};
    status = addressSpace.allocate(mapped, mapSize, mm::MEM_RESERVE | mm::MEM_COMMIT,
                                   mm::PAGE_READWRITE);
    if (status != NtStatus::Success) return status;
    std::memset(reinterpret_cast<void*>(mapped), 0, mapSize);
    std::memcpy(reinterpret_cast<void*>(mapped), namespaceBlob.data(), namespaceBlob.size());
    uint32_t previousProtection{};
    status = addressSpace.protect(mapped, mapSize, mm::PAGE_READONLY, &previousProtection);
    if (status != NtStatus::Success) {
        const auto releaseStatus = addressSpace.free(mapped, 0, mm::MEM_RELEASE);
        if (releaseStatus != NtStatus::Success) return releaseStatus;
        return status;
    }
    const uintptr_t previousMap = apiSetMapAddress;
    apiSetMapAddress = mapped;
    apiSetMapSize = namespaceBlob.size();
    *reinterpret_cast<uint64_t*>(pebAddress + pebApiSetMapOffset) = mapped;
    if (previousMap && previousMap != mapped) {
        // A replaced image may inherit an older API-set mapping. It is no
        // longer reachable from the PEB, so release it after publication.
        const auto releaseStatus = addressSpace.free(previousMap, 0, mm::MEM_RELEASE);
        if (releaseStatus != NtStatus::Success) return releaseStatus;
    }
    return NtStatus::Success;
}

NtStatus validateApiSetMapView(const mm::ProcessAddressSpace& addressSpace,
                               uintptr_t address, size_t size) noexcept {
    if (!address || !size || size > SIZE_MAX - mm::PageMask4KB)
        return NtStatus::InvalidImageFormat;
    const auto* vad = addressSpace.findVad(address);
    const size_t mappedSize = (size + mm::PageMask4KB) & ~mm::PageMask4KB;
    if (!vad || vad->isSectionView || !vad->committed ||
        vad->protection != mm::PAGE_READONLY || address < vad->startingAddress ||
        address > vad->endingAddress ||
        mappedSize > vad->endingAddress - address + 1)
        return NtStatus::InvalidImageFormat;
    const uint64_t firstVpn = address >> mm::PageShift4KB;
    const uint64_t pageCount = mappedSize >> mm::PageShift4KB;
    uint64_t committedPages{};
    for (auto* page = vad->commitRanges.get(); page; page = page->next.get()) {
        if (page->protection != mm::PAGE_READONLY || page->firstVpn < firstVpn ||
            page->lastVpn < page->firstVpn || page->lastVpn - firstVpn >= pageCount)
            return NtStatus::InvalidImageFormat;
        const uint64_t rangePages = page->lastVpn - page->firstVpn + 1;
        if (rangePages > pageCount - committedPages) return NtStatus::InvalidImageFormat;
        committedPages += rangePages;
    }
    return committedPages == pageCount ? NtStatus::Success : NtStatus::InvalidImageFormat;
}

bool isApiSetContractName(std::string_view name) noexcept {
    if (name.size() < 4) return false;
    const auto lower = [](char value) noexcept {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
    };
    return ((lower(name[0]) == 'a' && lower(name[1]) == 'p' && lower(name[2]) == 'i') ||
            (lower(name[0]) == 'e' && lower(name[1]) == 'x' && lower(name[2]) == 't')) &&
           name[3] == '-';
}

bool isUniversalCrtApiSet(std::string_view name) noexcept {
    constexpr std::string_view prefix = "api-ms-win-crt-";
    if (name.size() < prefix.size()) return false;
    for (size_t index = 0; index < prefix.size(); ++index) {
        const char value = name[index] >= 'A' && name[index] <= 'Z'
            ? static_cast<char>(name[index] + ('a' - 'A')) : name[index];
        if (value != prefix[index]) return false;
    }
    return true;
}

struct ApiSetResolverDetach {
    um::NativeUserImageSet64* images{};
    ~ApiSetResolverDetach() {
        if (images) images->setApiSetHostResolver(nullptr, nullptr);
    }
};

NtStatus openSystemImageSection(std::u16string_view path, ScopedHandle& sectionHandle,
                               ScopedObjectReference& sectionReference,
                               section::SectionObject*& image) noexcept {
    image = nullptr;
    ScopedHandle fileHandle;
    auto status = io::IoOpenBackendFile(&fileHandle.value, 0x0021, path);
    if (status != NtStatus::Success) return status;
    status = sys::NtCreateSectionFromKernel(&sectionHandle.value, section::SECTION_ALL_ACCESS,
        nullptr, nullptr, mm::PAGE_EXECUTE_READ, section::SEC_FILE | section::SEC_IMAGE,
        fileHandle.value);
    const auto closeFileStatus = sys::NtClose(fileHandle.value);
    if (closeFileStatus == NtStatus::Success) fileHandle.value = 0;
    if (status != NtStatus::Success) return status;
    if (closeFileStatus != NtStatus::Success) return closeFileStatus;
    status = sys::referenceProcessImageSection(sectionHandle.value, sectionReference.value);
    if (status != NtStatus::Success) return status;
    image = sectionReference.value->as<section::SectionObject>();
    return NtStatus::Success;
}

NtStatus mapFileImageAtAvailableBase(mm::ProcessAddressSpace& addressSpace,
                                    section::SectionObject& image,
                                    ob::ObjectHeader* sectionReference,
                                    const pe::PeImageView64& view,
                                    uintptr_t& mappedBase) noexcept {
    mappedBase = image.getImageBase();
    // A valid PE preferred base need not fall inside this process backend's
    // managed window. Treat that case like an occupied preferred range and
    // attempt a relocatable view; mapSharedPages reports InvalidParameter for
    // an out-of-window address, which is not itself an invalid PE image.
    const bool preferredInWindow = mappedBase >= hal::amd64::BootstrapUserAddressBase &&
        mappedBase - hal::amd64::BootstrapUserAddressBase < hal::amd64::BootstrapUserAddressSize &&
        image.getSize() <= hal::amd64::BootstrapUserAddressSize -
            (mappedBase - hal::amd64::BootstrapUserAddressBase);
    auto status = preferredInWindow
        ? sys::mapBootstrapFileImageView(addressSpace, mappedBase, image, sectionReference)
        : NtStatus::Conflict;
    const bool relocatedView = status == NtStatus::Conflict;
    if (relocatedView) {
        mappedBase = 0;
        status = sys::mapBootstrapRelocatedFileImageView(addressSpace, mappedBase,
                                                          image, sectionReference);
    }
    if (status != NtStatus::Success) return status;
    const auto unmapOnFailure = [&](NtStatus failure) noexcept {
        (void)addressSpace.unmapSharedPages(mappedBase, true);
        return failure;
    };
    if (!hal::amd64::bootstrapUserRange(mappedBase, image.getSize(), false))
        return unmapOnFailure(NtStatus::InvalidImageFormat);
    if (!relocatedView) return NtStatus::Success;

    auto mappedImage = std::span<uint8_t>{reinterpret_cast<uint8_t*>(mappedBase), image.getSize()};
    status = view.relocateMappedImage(mappedImage, mappedBase);
    if (status != NtStatus::Success) return unmapOnFailure(status);
    // The private loader view remains writable until every import is bound;
    // final PE page permissions are installed before the process is exposed.
    return NtStatus::Success;
}
}

NtStatus EProcess::cloneNativeMetadata(const EProcess& source) noexcept {
    if (!source.images_) return NtStatus::Success;
    uintptr_t currentRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(currentRoot));
    if (currentRoot != getDirectoryTableBase() || images_ || !source.imagesFinalized_ || source.images_->size() > 16)
        return NtStatus::NotImplemented;
    const size_t count = source.images_->size();
    if (!count) return NtStatus::InvalidImageFormat;
    auto inputs = std::unique_ptr<OwnedImageInput[]>(new (std::nothrow_t{}) OwnedImageInput[count]);
    auto imageSet = std::unique_ptr<um::NativeUserImageSet64>(new (std::nothrow_t{}) um::NativeUserImageSet64{});
    if (!inputs || !imageSet) return NtStatus::NoMemory;
    for (size_t i = 0; i < count; ++i) {
        const auto* module = source.images_->module(i);
        if (!module) return NtStatus::InvalidImageFormat;
        const auto file = module->view.fileBytes();
        const auto name = module->name;
        const auto modulePath = source.nativeImagePaths_[i];
        if (file.empty() || name.empty() ||
            !hal::amd64::bootstrapUserRange(module->base,
                module->view.headers().optionalHeader.sizeOfHeaders, false) ||
            !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(file.data()), file.size(), false) ||
            !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(name.data()), name.size(), false) ||
            (!modulePath.empty() && !hal::amd64::bootstrapKernelRange(
                reinterpret_cast<uintptr_t>(modulePath.data()), modulePath.size() * sizeof(char16_t), false)))
            return NtStatus::NotImplemented;
        auto& owned = inputs[i];
        owned.bytes.reset(new (std::nothrow_t{}) uint8_t[file.size()]);
        owned.name.reset(new (std::nothrow_t{}) char[name.size()]);
        if (!modulePath.empty())
            owned.path.reset(new (std::nothrow_t{}) char16_t[modulePath.size()]);
        if (!owned.bytes || !owned.name || (!modulePath.empty() && !owned.path)) return NtStatus::NoMemory;
        std::memcpy(owned.bytes.get(), file.data(), file.size());
        std::memcpy(owned.name.get(), name.data(), name.size());
        if (!modulePath.empty()) std::memcpy(owned.path.get(), modulePath.data(), modulePath.size() * sizeof(char16_t));
        const auto status = imageSet->addClonedModule({owned.name.get(), name.size()},
            {owned.bytes.get(), file.size()},
            {reinterpret_cast<uint8_t*>(module->base), module->image.size()}, module->base);
        if (status != NtStatus::Success) return status;
    }
    std::unique_ptr<pe::PeImageView64::TlsLayout[]> tls;
    if (source.userTlsCount_) {
        tls.reset(new (std::nothrow_t{}) pe::PeImageView64::TlsLayout[source.userTlsCount_]);
        if (!tls) return NtStatus::NoMemory;
        size_t selected{};
        for (size_t i = 0; i < imageSet->size(); ++i) {
            const auto* module = imageSet->module(i);
            if (!module) return NtStatus::InvalidImageFormat;
            pe::PeImageView64::TlsLayout layout{};
            const auto tlsStatus = module->view.tlsLayout(module->immutableImage(), module->base, layout);
            if (tlsStatus != NtStatus::Success) return tlsStatus;
            if (!layout.present) continue;
            if (selected >= source.userTlsCount_) return NtStatus::InvalidImageFormat;
            tls[selected++] = layout;
        }
        if (selected != source.userTlsCount_) return NtStatus::InvalidImageFormat;
    }
    std::unique_ptr<char16_t[]> processPath;
    std::unique_ptr<char16_t[]> commandLine;
    std::unique_ptr<char16_t[]> currentDirectory;
    if (!source.processImagePath_.empty() && !hal::amd64::bootstrapKernelRange(
            reinterpret_cast<uintptr_t>(source.processImagePath_.data()),
            source.processImagePath_.size() * sizeof(char16_t), false)) return NtStatus::InvalidParameter;
    if (!source.processCommandLine_.empty() && !hal::amd64::bootstrapKernelRange(
            reinterpret_cast<uintptr_t>(source.processCommandLine_.data()),
            source.processCommandLine_.size() * sizeof(char16_t), false)) return NtStatus::InvalidParameter;
    if (!source.processCurrentDirectory_.empty() && !hal::amd64::bootstrapKernelRange(
            reinterpret_cast<uintptr_t>(source.processCurrentDirectory_.data()),
            source.processCurrentDirectory_.size() * sizeof(char16_t), false)) return NtStatus::InvalidParameter;
    if (!source.processImagePath_.empty()) {
        processPath.reset(new (std::nothrow_t{}) char16_t[source.processImagePath_.size()]);
        if (!processPath) return NtStatus::NoMemory;
        std::memcpy(processPath.get(), source.processImagePath_.data(), source.processImagePath_.size() * sizeof(char16_t));
    }
    if (!source.processCommandLine_.empty()) {
        commandLine.reset(new (std::nothrow_t{}) char16_t[source.processCommandLine_.size()]);
        if (!commandLine) return NtStatus::NoMemory;
        std::memcpy(commandLine.get(), source.processCommandLine_.data(), source.processCommandLine_.size() * sizeof(char16_t));
    }
    if (!source.processCurrentDirectory_.empty()) {
        currentDirectory.reset(new (std::nothrow_t{}) char16_t[source.processCurrentDirectory_.size()]);
        if (!currentDirectory) return NtStatus::NoMemory;
        std::memcpy(currentDirectory.get(), source.processCurrentDirectory_.data(),
                    source.processCurrentDirectory_.size() * sizeof(char16_t));
    }
    imageSet->finishClonedModules();
    ownedImageInputs_ = std::move(inputs);
    images_ = std::move(imageSet);
    userTlsLayouts_ = std::move(tls);
    userTlsCount_ = source.userTlsCount_;
    userPeb_ = source.userPeb_;
    userLoaderData_ = source.userLoaderData_;
    userProcessParameters_ = source.userProcessParameters_;
    userApiSetMap_ = source.userApiSetMap_;
    userApiSetMapSize_ = source.userApiSetMapSize_;
    ownedImagePath_ = std::move(processPath);
    ownedCommandLine_ = std::move(commandLine);
    ownedCurrentDirectory_ = std::move(currentDirectory);
    processImagePath_ = {ownedImagePath_.get(), source.processImagePath_.size()};
    processCommandLine_ = {ownedCommandLine_.get(), source.processCommandLine_.size()};
    processCurrentDirectory_ = {ownedCurrentDirectory_.get(), source.processCurrentDirectory_.size()};
    processParametersConfigured_ = source.processParametersConfigured_;
    imageSectionBacked_ = source.imageSectionBacked_;
    for (size_t i = 0; i < count; ++i)
        nativeImagePaths_[i] = {ownedImageInputs_[i].path.get(), source.nativeImagePaths_[i].size()};
    if (imageSectionBacked_ && !userApiSetMap_) {
        ApiSetSchemaV6 apiSetSchema;
        auto status = publishApiSetMap(addressSpace_, userPeb_, userApiSetMap_,
                                       userApiSetMapSize_, apiSetSchema);
        if (status != NtStatus::Success) return status;
    }
    if (userApiSetMap_) {
        const auto status = validateApiSetMapView(addressSpace_, userApiSetMap_, userApiSetMapSize_);
        if (status != NtStatus::Success) return status;
        constexpr size_t pebApiSetMapOffset = 0x68;
        if (!hal::amd64::bootstrapUserRange(userPeb_ + pebApiSetMapOffset,
                                             sizeof(uint64_t), false) ||
            !hal::amd64::bootstrapUserRange(userApiSetMap_, userApiSetMapSize_, false))
            return NtStatus::InvalidImageFormat;
        *reinterpret_cast<uint64_t*>(userPeb_ + pebApiSetMapOffset) = userApiSetMap_;
    }
    imagesFinalized_ = true;
    return NtStatus::Success;
}

NtStatus EProcess::replaceMainImageFromSection(section::SectionObject& image,
                                                ob::ObjectHeader* sectionReference) noexcept {
    uintptr_t activeRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(activeRoot));
    // This replacement uses destructive VAD updates during dependency
    // closure. It is valid only while NtCreateProcessEx still owns an
    // unpublished, threadless child that will be destroyed on any failure.
    if (activeRoot != getDirectoryTableBase() || terminating_ || processPublished_ ||
        attachedThreads_ || threads_ ||
        !imagesFinalized_ || !images_ ||
        !ownedImageInputs_ || !sectionReference ||
        (image.getAllocationAttributes() & (section::SEC_FILE | section::SEC_IMAGE)) !=
            (section::SEC_FILE | section::SEC_IMAGE) ||
        images_->size() == 0 || ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    const auto* previous = images_->module(0);
    if (!previous || !previous->base ||
        image.getSize() > SIZE_MAX || !image.getFileSize() || image.getFileSize() > SIZE_MAX)
        return NtStatus::NotSupported;
    const size_t fileSize = static_cast<size_t>(image.getFileSize());
    const auto fileName = image.getFileName();
    if (fileName.empty() || fileName.size() > SIZE_MAX / sizeof(char16_t) - 1)
        return NtStatus::BufferTooSmall;
    char moduleNameStorage[260]{};
    std::string_view moduleName;
    auto status = buildImageModuleName(fileName, moduleNameStorage, moduleName);
    if (status != NtStatus::Success) return status;
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow_t{}) uint8_t[fileSize]);
    std::unique_ptr<char[]> name(new (std::nothrow_t{}) char[moduleName.size()]);
    std::unique_ptr<char16_t[]> path;
    if (!fileName.empty()) path.reset(new (std::nothrow_t{}) char16_t[fileName.size() + 1]);
    if (!bytes || !name || (!fileName.empty() && !path)) return NtStatus::NoMemory;
    std::memcpy(name.get(), moduleName.data(), moduleName.size());
    if (path) {
        for (size_t i = 0; i < fileName.size(); ++i) path[i] = fileName[i];
        path[fileName.size()] = 0;
    }
    size_t offset{};
    while (offset < fileSize) {
        size_t read{};
        const auto status = image.readFile(offset, bytes.get() + offset, fileSize - offset, &read);
        if (status != NtStatus::Success) return status;
        if (!read || read > fileSize - offset) return NtStatus::EndOfFile;
        offset += read;
    }
    pe::PeImageView64 view;
    status = view.inspect({bytes.get(), fileSize});
    if (status != NtStatus::Success || view.headers().optionalHeader.imageBase != image.getImageBase() ||
        view.headers().optionalHeader.sizeOfImage != image.getSize() ||
        !(view.headers().fileHeader.characteristics & pe::IMAGE_FILE_EXECUTABLE_IMAGE) ||
        (view.headers().fileHeader.characteristics & pe::IMAGE_FILE_DLL))
        return status == NtStatus::Success ? NtStatus::InvalidImageFormat : status;

    std::unique_ptr<char16_t[]> commandCopy;
    size_t commandLength{};
    if (userProcessParameters_) {
        auto* parameters = reinterpret_cast<detail::UserProcessParameters*>(userProcessParameters_);
        const auto command = parameters->commandLine;
        if ((command.length & 1) || command.length > command.maximumLength ||
            (command.length && (!command.buffer || !hal::amd64::bootstrapUserRange(
                command.buffer, command.length, false)))) return NtStatus::AccessViolation;
        commandLength = command.length / sizeof(char16_t);
        if (commandLength) {
            commandCopy.reset(new (std::nothrow_t{}) char16_t[commandLength]);
            if (!commandCopy) return NtStatus::NoMemory;
            std::memcpy(commandCopy.get(), reinterpret_cast<const void*>(command.buffer), command.length);
        }
    }

    const uintptr_t oldBase = previous->base;
    status = addressSpace_.unmapSharedPages(oldBase);
    if (status != NtStatus::Success) return status;
    uintptr_t mappedBase{};
    status = mapFileImageAtAvailableBase(addressSpace_, image, sectionReference, view, mappedBase);
    if (status != NtStatus::Success) return status;
    const size_t previousModuleCount = images_->size();
    status = images_->replaceClonedModule(0, {name.get(), moduleName.size()},
        {bytes.get(), fileSize},
        {reinterpret_cast<uint8_t*>(mappedBase), image.getSize()}, mappedBase);
    if (status != NtStatus::Success) {
        (void)addressSpace_.unmapSharedPages(mappedBase, true);
        return status;
    }

    ApiSetSchemaV6 apiSetSchema;
    images_->setApiSetHostResolver(&apiSetSchema,
        [](void* context, std::string_view contract, std::string_view importingModule,
           std::string_view& host) noexcept -> NtStatus {
            return static_cast<ApiSetSchemaV6*>(context)->resolve(contract, importingModule, host);
        });
    ApiSetResolverDetach detachApiSetResolver{images_.get()};

    const auto appendDependency = [&](std::string_view requestedLibrary,
                                      std::string_view importingModule) noexcept -> NtStatus {
        std::string_view canonical = requestedLibrary;
        if (isApiSetContractName(requestedLibrary)) {
            const auto apiStatus = apiSetSchema.resolve(requestedLibrary, importingModule, canonical);
            if (apiStatus != NtStatus::Success) return apiStatus;
        }
        char canonicalStorage[132]{};
        const bool hasDllSuffix = canonical.size() >= 4 && canonical[canonical.size() - 4] == '.' &&
            (canonical[canonical.size() - 3] == 'd' || canonical[canonical.size() - 3] == 'D') &&
            (canonical[canonical.size() - 2] == 'l' || canonical[canonical.size() - 2] == 'L') &&
            (canonical[canonical.size() - 1] == 'l' || canonical[canonical.size() - 1] == 'L');
        std::string_view library = canonical;
        if (!hasDllSuffix) {
            if (canonical.empty() || canonical.size() > sizeof(canonicalStorage) - 5)
                return NtStatus::NotSupported;
            std::memcpy(canonicalStorage, canonical.data(), canonical.size());
            std::memcpy(canonicalStorage + canonical.size(), ".dll", 4);
            library = {canonicalStorage, canonical.size() + 4};
        }
        if (images_->containsModule(library)) return NtStatus::Success;
        if (images_->size() == 16) return NtStatus::InsufficientResources;
        char16_t dependencyPathStorage[256]{};
        size_t dependencyPathLength{};
        auto dependencyStatus = buildSystemDllPath(library, dependencyPathStorage,
                                                   dependencyPathLength);
        if (dependencyStatus != NtStatus::Success) {
            traceImageInitializationFailure("dependency path", dependencyStatus);
            return dependencyStatus;
        }
        const std::u16string_view dependencyPath{dependencyPathStorage, dependencyPathLength};
        ScopedHandle dependencySectionHandle;
        ScopedObjectReference dependencySectionReference;
        section::SectionObject* dependencySection{};
        dependencyStatus = openSystemImageSection(dependencyPath,
            dependencySectionHandle, dependencySectionReference, dependencySection);
        if (dependencyStatus == NtStatus::ObjectNameNotFound ||
            dependencyStatus == NtStatus::NoSuchFile)
            return NtStatus::NotSupported;
        if (dependencyStatus != NtStatus::Success) return dependencyStatus;
        if (!dependencySection->getFileSize() || dependencySection->getFileSize() > SIZE_MAX ||
            !dependencySection->getSize() || dependencySection->getSize() > SIZE_MAX)
            return NtStatus::InvalidImageFormat;
        const size_t rawSize = static_cast<size_t>(dependencySection->getFileSize());
        auto rawBytes = std::unique_ptr<uint8_t[]>(new (std::nothrow_t{}) uint8_t[rawSize]);
        auto nameCopy = std::unique_ptr<char[]>(new (std::nothrow_t{}) char[library.size()]);
        auto pathCopy = std::unique_ptr<char16_t[]>(new (std::nothrow_t{}) char16_t[dependencyPath.size()]);
        if (!rawBytes || !nameCopy || !pathCopy) return NtStatus::NoMemory;
        size_t readOffset{};
        while (readOffset < rawSize) {
            size_t read{};
            dependencyStatus = dependencySection->readFile(readOffset, rawBytes.get() + readOffset,
                rawSize - readOffset, &read);
            if (dependencyStatus != NtStatus::Success) return dependencyStatus;
            if (!read || read > rawSize - readOffset) return NtStatus::EndOfFile;
            readOffset += read;
        }
        pe::PeImageView64 dependencyView;
        dependencyStatus = dependencyView.inspect({rawBytes.get(), rawSize});
        if (dependencyStatus != NtStatus::Success) return dependencyStatus;
        if (!(dependencyView.headers().fileHeader.characteristics & pe::IMAGE_FILE_DLL) ||
            dependencyView.headers().optionalHeader.imageBase != dependencySection->getImageBase() ||
            dependencyView.headers().optionalHeader.sizeOfImage != dependencySection->getSize())
            return NtStatus::InvalidImageFormat;
        // DLL entrypoints are dispatched by ntdll's RtlUserThreadStart after
        // the image set and loader lists have been published. Validate the
        // RVA here so malformed entrypoints fail before mapping the image.
        if (dependencyView.headers().optionalHeader.addressOfEntryPoint &&
            !dependencyView.executableRva(
                dependencyView.headers().optionalHeader.addressOfEntryPoint))
            return NtStatus::InvalidImageFormat;
        std::memcpy(nameCopy.get(), library.data(), library.size());
        std::memcpy(pathCopy.get(), dependencyPath.data(), dependencyPath.size() * sizeof(char16_t));
        uintptr_t dependencyBase{};
        dependencyStatus = mapFileImageAtAvailableBase(addressSpace_, *dependencySection,
            dependencySectionReference.value, dependencyView, dependencyBase);
        if (dependencyStatus != NtStatus::Success) return dependencyStatus;
        const size_t newIndex = images_->size();
        dependencyStatus = images_->addClonedModule({nameCopy.get(), library.size()},
            {rawBytes.get(), rawSize},
            {reinterpret_cast<uint8_t*>(dependencyBase), dependencySection->getSize()}, dependencyBase);
        if (dependencyStatus != NtStatus::Success) {
            (void)addressSpace_.unmapSharedPages(dependencyBase, true);
            return dependencyStatus;
        }
        auto& owned = ownedImageInputs_[newIndex];
        owned.bytes = std::move(rawBytes);
        owned.name = std::move(nameCopy);
        owned.path = std::move(pathCopy);
        nativeImagePaths_[newIndex] = {owned.path.get(), dependencyPath.size()};
        return NtStatus::Success;
    };

    for (;;) {
        const size_t previousCount = images_->size();
        for (size_t moduleIndex = 0; moduleIndex < images_->size(); ++moduleIndex) {
            if (moduleIndex != 0 && moduleIndex < previousModuleCount) continue;
            const auto* module = images_->module(moduleIndex);
            if (!module) { status = NtStatus::InvalidImageFormat; break; }
            status = module->view.forEachImportLibrary(module->immutableImage(),
                [&](std::string_view library) noexcept -> NtStatus {
                    return appendDependency(library, module->name);
                });
            if (status == NtStatus::Success)
                status = module->view.forEachDelayImportLibrary(module->immutableImage(),
                    [&](std::string_view library) noexcept -> NtStatus {
                        return appendDependency(library, module->name);
                    });
            if (status != NtStatus::Success) break;
        }
        if (status != NtStatus::Success) break;
        status = images_->forEachMissingForwarderLibrary(
            [&](std::string_view library, std::string_view importingModule) noexcept -> NtStatus {
                return appendDependency(library, importingModule);
            }, 0, 1);
        if (status == NtStatus::Success && images_->size() > previousModuleCount) {
            status = images_->forEachMissingForwarderLibrary(
                [&](std::string_view library, std::string_view importingModule) noexcept -> NtStatus {
                    return appendDependency(library, importingModule);
                }, previousModuleCount);
        }
        if (status != NtStatus::Success || images_->size() == previousCount) break;
    }
    // A file-backed parent has already restored PE permissions. Reopen only
    // each cloned IAT while this child is unpublished, then restore every
    // page's retained maximum after rebinding.
    size_t preparedModules = 0;
    if (status == NtStatus::Success) {
        for (size_t i = 1; i < previousModuleCount; ++i) {
            const auto* module = images_->module(i);
            if (!module) { status = NtStatus::InvalidImageFormat; break; }
            const auto& header = module->view.headers().optionalHeader;
            uintptr_t iat{};
            size_t iatSize{};
            if (header.numberOfRvaAndSizes > pe::IMAGE_DIRECTORY_ENTRY_IAT) {
                const auto directory = header.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IAT];
                iat = module->base + directory.virtualAddress;
                iatSize = directory.size;
            }
            if (header.numberOfRvaAndSizes > pe::IMAGE_DIRECTORY_ENTRY_IMPORT &&
                header.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IMPORT].size && !iatSize) {
                // PE images may omit IMAGE_DIRECTORY_ENTRY_IAT. The cloned
                // view is private and the child is still unpublished; open
                // the whole image temporarily, then restore every PE-derived
                // page protection after rebinding from the immutable file.
                iat = module->base;
                iatSize = module->image.size();
            }
            status = addressSpace_.prepareImageBinding(module->base, iat, iatSize,
                                                       !imageSectionBacked_);
            if (status != NtStatus::Success) break;
            ++preparedModules;
            // bindAll reads lookup RVAs from the immutable kernel snapshot,
            // including when OriginalFirstThunk aliases the user IAT.
        }
    }
    // Rebind the complete module set: cloned DLLs may have imported symbols
    // from the old executable, whose image was just removed from the child.
    if (status == NtStatus::Success) status = images_->bindAll();
    std::unique_ptr<pe::PeImageView64::TlsLayout[]> replacementTls;
    size_t replacementTlsCount{};
    if (status == NtStatus::Success) {
        replacementTls.reset(new (std::nothrow_t{}) pe::PeImageView64::TlsLayout[images_->size()]);
        if (!replacementTls) status = NtStatus::NoMemory;
    }
    size_t tlsOffset = 0x100;
    for (size_t i = 0; status == NtStatus::Success && i < images_->size(); ++i) {
        const auto* module = images_->module(i);
        if (!module) { status = NtStatus::InvalidImageFormat; break; }
        pe::PeImageView64::TlsLayout layout{};
        status = module->view.tlsLayout(module->immutableImage(), module->base, layout);
        if (status != NtStatus::Success || !layout.present) continue;
        if (layout.callbackCount || !layout.alignment || layout.alignment > 256 ||
            layout.initialData.size() > mm::PageSize4KB - 0x100 ||
            layout.zeroFill > mm::PageSize4KB - 0x100 - layout.initialData.size()) {
            status = NtStatus::NotSupported;
            break;
        }
        tlsOffset = (tlsOffset + layout.alignment - 1) & ~(layout.alignment - 1);
        const size_t length = layout.initialData.size() + layout.zeroFill;
        if (tlsOffset > mm::PageSize4KB || length > mm::PageSize4KB - tlsOffset ||
            !hal::amd64::bootstrapUserRange(module->base + layout.indexRva, sizeof(uint32_t), true)) {
            status = NtStatus::InvalidImageFormat;
            break;
        }
        tlsOffset += length;
        const uint32_t index = static_cast<uint32_t>(replacementTlsCount);
        std::memcpy(reinterpret_cast<void*>(module->base + layout.indexRva), &index, sizeof(index));
        replacementTls[replacementTlsCount++] = layout;
    }
    if (status == NtStatus::Success) {
        for (size_t i = 0; i < images_->size(); ++i) {
            const auto* module = images_->module(i);
            if (!module) { status = NtStatus::InvalidImageFormat; break; }
            status = addressSpace_.finalizeImageProtections(module->base);
            if (status != NtStatus::Success) break;
        }
    }
    if (status != NtStatus::Success) {
        for (size_t i = 1; i <= preparedModules; ++i) {
            const auto* module = images_->module(i);
            if (module) (void)addressSpace_.finalizeImageProtections(module->base);
        }
        (void)addressSpace_.unmapSharedPages(mappedBase, true);
        return status;
    }
    userTlsLayouts_ = std::move(replacementTls);
    userTlsCount_ = replacementTlsCount;
    status = publishApiSetMap(addressSpace_, userPeb_, userApiSetMap_, userApiSetMapSize_,
                              apiSetSchema);
    if (status != NtStatus::Success) {
        (void)addressSpace_.unmapSharedPages(mappedBase, true);
        return status;
    }
    if (userPeb_) {
        // PEB.ImageBaseAddress is the first image-dependent field read by
        // native startup code. The loader list and process parameters below
        // are rebuilt for the replacement image as part of the same commit.
        *reinterpret_cast<uint64_t*>(userPeb_ + 0x10) = mappedBase;
    }
    imageSectionBacked_ = true;
    ownedImageInputs_[0].bytes = std::move(bytes);
    ownedImageInputs_[0].name = std::move(name);
    ownedImageInputs_[0].path = std::move(path);
    nativeImagePaths_[0] = {ownedImageInputs_[0].path.get(), fileName.size()};
    if (!fileName.empty()) {
        processImagePath_ = nativeImagePaths_[0];
        if (userLoaderData_ && !initializeLoaderMetadata(*images_, userLoaderData_,
                std::span<const std::u16string_view>{nativeImagePaths_.data(), images_->size()}))
            return NtStatus::BufferTooSmall;
        if (userProcessParameters_) {
            auto* parameters = reinterpret_cast<detail::UserProcessParameters*>(userProcessParameters_);
            *parameters = {};
            detail::UserMetadataWriter writer(
                {reinterpret_cast<uint8_t*>(userProcessParameters_), mm::PageSize4KB},
                userProcessParameters_, sizeof(*parameters));
            if (!writer.append(processCurrentDirectory_, parameters->currentDirectory) ||
                !writer.append(processImagePath_, parameters->imagePathName) ||
                !writer.append({commandCopy ? commandCopy.get() : u"", commandLength}, parameters->commandLine))
                return NtStatus::BufferTooSmall;
            detail::UserUnicodeString emptyEnvironment{};
            if (!writer.append(std::u16string_view{u"\0", 1}, emptyEnvironment))
                return NtStatus::BufferTooSmall;
            parameters->environment = emptyEnvironment.buffer;
            ownedCommandLine_ = std::move(commandCopy);
            processCommandLine_ = {ownedCommandLine_.get(), commandLength};
            processParametersConfigured_ = true;
        }
    }
    return NtStatus::Success;
}

namespace {
// Only public winternl.h fields are populated. Paths come from the caller;
// the in-memory bootstrap has no filesystem from which to infer them.
struct UserListEntry { uint64_t next; uint64_t previous; };
using detail::UserUnicodeString;
struct UserLoaderData {
    uint32_t length;
    uint8_t initialized;
    uint8_t reserved1[3];
    uint64_t ssHandle;
    UserListEntry loadOrder;
    UserListEntry memoryOrder;
    UserListEntry initializationOrder;
};
struct UserLoaderEntry {
    UserListEntry loadOrder;
    UserListEntry memoryOrder;
    UserListEntry initializationOrder;
    uint64_t dllBase;
    uint64_t entryPoint;
    uint32_t sizeOfImage;
    uint32_t reserved1;
    UserUnicodeString fullDllName;
    UserUnicodeString baseDllName;
    uint32_t flags;
    uint16_t loadCount;
    uint16_t tlsIndex;
    UserListEntry hashLinks;
    uint32_t timestamp;
    uint32_t reserved2;
};
static_assert(sizeof(UserLoaderData) == 0x40);
static_assert(offsetof(UserLoaderData, loadOrder) == 0x10);
static_assert(offsetof(UserLoaderData, memoryOrder) == 0x20);
static_assert(offsetof(UserLoaderData, initializationOrder) == 0x30);
static_assert(offsetof(UserLoaderEntry, loadOrder) == 0x00);
static_assert(offsetof(UserLoaderEntry, memoryOrder) == 0x10);
static_assert(offsetof(UserLoaderEntry, initializationOrder) == 0x20);
static_assert(offsetof(UserLoaderEntry, dllBase) == 0x30);
static_assert(offsetof(UserLoaderEntry, entryPoint) == 0x38);
static_assert(offsetof(UserLoaderEntry, sizeOfImage) == 0x40);
static_assert(offsetof(UserLoaderEntry, fullDllName) == 0x48);
static_assert(offsetof(UserLoaderEntry, baseDllName) == 0x58);

bool initializeLoaderMetadata(const um::NativeUserImageSet64& images, uintptr_t address,
                              std::span<const std::u16string_view> paths) noexcept {
    constexpr size_t entriesOffset = sizeof(UserLoaderData);
    if (images.size() > 16 || paths.size() != images.size() ||
        entriesOffset + images.size() * sizeof(UserLoaderEntry) > mm::PageSize4KB)
        return false;
    const size_t stringsOffset = entriesOffset + images.size() * sizeof(UserLoaderEntry);
    auto bytes = std::span<uint8_t>{reinterpret_cast<uint8_t*>(address), mm::PageSize4KB};
    detail::UserMetadataWriter writer(bytes, address, stringsOffset);
    // Validate every length before writing any entries or string bytes.
    size_t remaining = bytes.size() - stringsOffset;
    for (const auto path : paths) {
        if (path.empty()) continue;
        if (path.size() > (UINT16_MAX - sizeof(char16_t)) / sizeof(char16_t)) return false;
        const size_t length = (path.size() + 1) * sizeof(char16_t);
        if (length > remaining) return false;
        remaining -= length;
        size_t baseOffset{};
        for (size_t i = 0; i < path.size(); ++i)
            if (path[i] == u'\\' || path[i] == u'/') baseOffset = i + 1;
        const size_t baseLength = path.size() - baseOffset;
        if (baseLength > (UINT16_MAX - sizeof(char16_t)) / sizeof(char16_t)) return false;
        const size_t baseBytes = (baseLength + 1) * sizeof(char16_t);
        if (baseBytes > remaining) return false;
        remaining -= baseBytes;
    }
    auto* loader = reinterpret_cast<UserLoaderData*>(address);
    auto* entries = reinterpret_cast<UserLoaderEntry*>(address + entriesOffset);
    *loader = {};
    loader->length = sizeof(UserLoaderData);
    const uintptr_t loadHead = address + offsetof(UserLoaderData, loadOrder);
    const uintptr_t memoryHead = address + offsetof(UserLoaderData, memoryOrder);
    const uintptr_t initHead = address + offsetof(UserLoaderData, initializationOrder);
    size_t memoryOrder[16]{};
    size_t initOrder[16]{};
    size_t initCount{};
    uint8_t visited[16]{};
    const auto findDependency = [&](std::string_view requested) noexcept -> size_t {
        if (requested.starts_with("api-ms-win-crt-"))
            requested = "ucrtbase.dll";
        for (size_t moduleIndex = 0; moduleIndex < images.size(); ++moduleIndex) {
            const auto* candidate = images.module(moduleIndex);
            if (!candidate || candidate->name.size() != requested.size()) continue;
            bool equal = true;
            for (size_t byte = 0; byte < requested.size(); ++byte) {
                char left = candidate->name[byte], right = requested[byte];
                if (left >= 'A' && left <= 'Z') left = static_cast<char>(left + ('a' - 'A'));
                if (right >= 'A' && right <= 'Z') right = static_cast<char>(right + ('a' - 'A'));
                if (left != right) { equal = false; break; }
            }
            if (equal) return moduleIndex;
        }
        return SIZE_MAX;
    };
    const auto visitModule = [&](auto&& visit, size_t moduleIndex) noexcept -> bool {
        if (moduleIndex >= images.size()) return false;
        if (visited[moduleIndex] == 2) return true;
        if (visited[moduleIndex] == 1) return false;
        visited[moduleIndex] = 1;
        const auto* module = images.module(moduleIndex);
        if (!module) return false;
        const auto importStatus = module->view.forEachImportLibrary(module->immutableImage(),
            [&](std::string_view library) noexcept -> NtStatus {
                size_t dependency = findDependency(library);
                if (dependency == SIZE_MAX) {
                    if (library.size() <= 4 || library.substr(library.size() - 4) != ".dll")
                        return NtStatus::InvalidImageFormat;
                    char name[132]{};
                    if (library.size() >= sizeof(name)) return NtStatus::InvalidImageFormat;
                    std::memcpy(name, library.data(), library.size() - 4);
                    std::memcpy(name + library.size() - 4, ".dll", 4);
                    dependency = findDependency({name, library.size()});
                }
                if (dependency == SIZE_MAX || !visit(visit, dependency))
                    return NtStatus::InvalidImageFormat;
                return NtStatus::Success;
            });
        if (importStatus != NtStatus::Success) return false;
        visited[moduleIndex] = 2;
        if (moduleIndex != 0 &&
            (module->view.headers().fileHeader.characteristics & pe::IMAGE_FILE_DLL))
            initOrder[initCount++] = moduleIndex;
        return true;
    };
    for (size_t i = 0; i < images.size(); ++i) {
        const auto* module = images.module(i);
        if (!module) return false;
        entries[i] = {};
        entries[i].dllBase = module->base;
        const auto& optional = module->view.headers().optionalHeader;
        if (optional.addressOfEntryPoint && module->base <= UINTPTR_MAX - optional.addressOfEntryPoint)
            entries[i].entryPoint = module->base + optional.addressOfEntryPoint;
        entries[i].sizeOfImage = optional.sizeOfImage;
        entries[i].flags = (module->view.headers().fileHeader.characteristics & pe::IMAGE_FILE_DLL)
            ? 0x00000004U : 0;
        entries[i].loadCount = 1;
        entries[i].timestamp = module->view.headers().fileHeader.timeDateStamp;
        if (!writer.append(paths[i], entries[i].fullDllName)) return false;
        size_t baseOffset{};
        for (size_t c = 0; c < paths[i].size(); ++c)
            if (paths[i][c] == u'\\' || paths[i][c] == u'/') baseOffset = c + 1;
        if (!writer.append(paths[i].substr(baseOffset), entries[i].baseDllName)) return false;
        memoryOrder[i] = i;
        size_t position = i;
        while (position && images.module(memoryOrder[position - 1])->base > module->base) {
            memoryOrder[position] = memoryOrder[position - 1];
            --position;
        }
        memoryOrder[position] = i;
    }
    for (size_t i = 0; i < images.size(); ++i)
        if (!visitModule(visitModule, i)) return false;
    const auto entryLink = [&](size_t index, size_t offset) {
        return address + entriesOffset + index * sizeof(UserLoaderEntry) + offset;
    };
    const auto initializeList = [&](UserListEntry& head, uintptr_t headAddress,
                                    const size_t* order, size_t count, size_t offset) {
        head.next = count ? entryLink(order[0], offset) : headAddress;
        head.previous = count ? entryLink(order[count - 1], offset) : headAddress;
        for (size_t i = 0; i < count; ++i) {
            auto& link = *reinterpret_cast<UserListEntry*>(entryLink(order[i], offset));
            link.previous = i ? entryLink(order[i - 1], offset) : headAddress;
            link.next = i + 1 < count ? entryLink(order[i + 1], offset) : headAddress;
        }
    };
    size_t loadOrder[16]{};
    for (size_t i = 0; i < images.size(); ++i) loadOrder[i] = i;
    initializeList(loader->loadOrder, loadHead, loadOrder, images.size(), offsetof(UserLoaderEntry, loadOrder));
    initializeList(loader->memoryOrder, memoryHead, memoryOrder, images.size(), offsetof(UserLoaderEntry, memoryOrder));
    initializeList(loader->initializationOrder, initHead, initOrder, initCount,
                    offsetof(UserLoaderEntry, initializationOrder));
    return true;
}

bool active(const EProcess& process) noexcept {
    uintptr_t root{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(root));
    return root && root == process.getDirectoryTableBase();
}

// The bootstrap loader requires page-aligned, contiguous readable sections.
NtStatus validateProtectionLayout(const pe::PeImageView64& view) noexcept {
    const auto& header = view.headers().optionalHeader;
    size_t end = (static_cast<size_t>(header.sizeOfHeaders) + mm::PageMask4KB) & ~mm::PageMask4KB;
    for (size_t i = 0; i < view.headers().fileHeader.numberOfSections; ++i) {
        const auto section = view.section(i);
        const size_t size = section.misc.virtualSize > section.sizeOfRawData ? section.misc.virtualSize : section.sizeOfRawData;
        if (!size) continue;
        if ((section.characteristics & pe::IMAGE_SCN_MEM_NOT_CACHED) ||
            ((section.characteristics & pe::IMAGE_SCN_MEM_WRITE) &&
             (section.characteristics & pe::IMAGE_SCN_MEM_SHARED))) return NtStatus::NotSupported;
        if (section.virtualAddress != end || size > SIZE_MAX - mm::PageMask4KB ||
            !(section.characteristics & pe::IMAGE_SCN_MEM_READ)) return NtStatus::NotSupported;
        const size_t rounded = (size + mm::PageMask4KB) & ~mm::PageMask4KB;
        if (end > header.sizeOfImage || rounded > header.sizeOfImage - end) return NtStatus::InvalidImageFormat;
        end += rounded;
    }
    return end == header.sizeOfImage ? NtStatus::Success : NtStatus::NotSupported;
}
}

NtStatus EProcess::loadNativeImage(std::string_view name, std::span<const uint8_t> file,
                                   std::u16string_view fullPath) noexcept {
    ob::detail::HandleTableGuard guard;
    if (!active(*this) || terminating_ || imagesFinalized_) {
        return NtStatus::InvalidParameter;
    }
    if (fullPath.size() > (UINT16_MAX - sizeof(char16_t)) / sizeof(char16_t) ||
        fullPath.size() > mm::PageSize4KB / sizeof(char16_t) - 1) return NtStatus::BufferTooSmall;
    if (file.empty()) return NtStatus::InvalidImageFormat;
    if (name.empty() ||
        !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(name.data()), name.size(), false) ||
        !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(file.data()), file.size(), false) ||
        (!fullPath.empty() && !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(fullPath.data()),
                                        fullPath.size() * sizeof(char16_t), false))) {
        return NtStatus::InvalidParameter;
    }
    pe::PeImageView64 view;
    auto status = view.inspect(file);
    if (status != NtStatus::Success) return status;
    if ((view.headers().fileHeader.characteristics & pe::IMAGE_FILE_DLL) &&
        view.headers().optionalHeader.addressOfEntryPoint) return NtStatus::NotSupported;
    status = validateProtectionLayout(view);
    if (status != NtStatus::Success) return status;
    // Reject an existing module name before allocating/mapping section pages;
    // addModule still performs the same check as a final invariant.
    if (images_ && images_->containsModule(name)) return NtStatus::ObjectNameCollision;
    const uintptr_t base = view.headers().optionalHeader.imageBase;
    const size_t size = view.headers().optionalHeader.sizeOfImage;
    if (images_ && images_->size() == 16) return NtStatus::InsufficientResources;
    auto newImages = std::unique_ptr<um::NativeUserImageSet64>{};
    if (!images_) {
        newImages.reset(new (std::nothrow_t{}) um::NativeUserImageSet64{});
        if (!newImages) return NtStatus::NoMemory;
    }
    auto newInputs = std::unique_ptr<OwnedImageInput[]>{};
    if (!ownedImageInputs_) {
        newInputs.reset(new (std::nothrow_t{}) OwnedImageInput[16]);
        if (!newInputs) return NtStatus::NoMemory;
    }
    auto* imageSet = images_ ? images_.get() : newImages.get();
    auto* inputSet = ownedImageInputs_ ? ownedImageInputs_.get() : newInputs.get();
    auto& owned = inputSet[imageSet->size()];
    std::unique_ptr<uint8_t[]> copiedBytes;
    std::unique_ptr<char[]> copiedName;
    std::unique_ptr<char16_t[]> copiedPath;
    if (owned.bytes.get() != file.data()) {
        copiedBytes.reset(new (std::nothrow_t{}) uint8_t[file.size()]);
        if (!copiedBytes) return NtStatus::NoMemory;
        std::memcpy(copiedBytes.get(), file.data(), file.size());
        file = {copiedBytes.get(), file.size()};
    }
    if (owned.name.get() != name.data()) {
        copiedName.reset(new (std::nothrow_t{}) char[name.size()]);
        if (!copiedName) return NtStatus::NoMemory;
        std::memcpy(copiedName.get(), name.data(), name.size());
        name = {copiedName.get(), name.size()};
    }
    if (!fullPath.empty() && owned.path.get() != fullPath.data()) {
        copiedPath.reset(new (std::nothrow_t{}) char16_t[fullPath.size()]);
        if (!copiedPath) return NtStatus::NoMemory;
        std::memcpy(copiedPath.get(), fullPath.data(), fullPath.size() * sizeof(char16_t));
        fullPath = {copiedPath.get(), fullPath.size()};
    }
    uintptr_t target = base;
    const bool preferredInWindow = target >= hal::amd64::BootstrapUserAddressBase &&
        target - hal::amd64::BootstrapUserAddressBase < hal::amd64::BootstrapUserAddressSize &&
        size <= hal::amd64::BootstrapUserAddressSize -
            (target - hal::amd64::BootstrapUserAddressBase);
    status = preferredInWindow
        ? sys::mapBootstrapImageView(addressSpace_, target, size)
        : NtStatus::Conflict;
    const bool relocatedView = status == NtStatus::Conflict;
    if (relocatedView) {
        target = 0;
        status = sys::mapBootstrapImageView(addressSpace_, target, size);
    }
    if (status != NtStatus::Success) return status;
    if (!hal::amd64::bootstrapUserRange(target, size, false)) {
        (void)addressSpace_.unmapSharedPages(target, true);
        return NtStatus::InvalidImageFormat;
    }
    // addModule maps the PE bytes into the user VAD and applies base
    // relocations as one operation. At this point the Mm staging view is
    // still zero-filled; relocating it before that copy would parse an empty
    // relocation block and reject otherwise valid images.
    status = imageSet->addModule(name, file, {reinterpret_cast<uint8_t*>(target), size}, target);
    if (status != NtStatus::Success) {
        (void)addressSpace_.unmapSharedPages(target, true);
        return status;
    }
    if (copiedBytes) owned.bytes = std::move(copiedBytes);
    if (copiedName) owned.name = std::move(copiedName);
    if (copiedPath) owned.path = std::move(copiedPath);
    if (newInputs) ownedImageInputs_ = std::move(newInputs);
    if (newImages) images_ = std::move(newImages);
    if (imageSet->size() == 1) captureProcessImageName(name);
    nativeImagePaths_[imageSet->size() - 1] = fullPath;
    return NtStatus::Success;
}

void EProcess::captureProcessImageName(std::string_view name) noexcept {
    if (processImageNameLength_ || name.empty()) return;
    size_t basenameOffset = 0;
    for (size_t i = 0; i < name.size(); ++i)
        if (name[i] == '/' || name[i] == '\\') basenameOffset = i + 1;
    name.remove_prefix(basenameOffset);
    const size_t length = name.size() < std::size(processImageName_)
        ? name.size() : std::size(processImageName_) - 1;
    for (size_t i = 0; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(name[i]);
        if (byte > 0x7f) return;
        processImageName_[i] = static_cast<char16_t>(byte);
    }
    processImageName_[length] = u'\0';
    processImageNameLength_ = static_cast<uint16_t>(length);
}

NtStatus EProcess::setNativeProcessParameters(std::u16string_view imagePath,
                                               std::u16string_view commandLine,
                                               std::u16string_view currentDirectory) noexcept {
    // Process parameters are owned metadata captured by the creator before
    // the unpublished child is attached for image mapping. This setter does
    // not touch the child's address space, so requiring its CR3 here rejects
    // every normal NtCreateUserProcess caller running in the parent.
    if (terminating_ || imagesFinalized_) return NtStatus::InvalidParameter;
    const size_t capacity = mm::PageSize4KB - sizeof(detail::UserProcessParameters);
    const auto bytes = [](std::u16string_view text) noexcept {
        return text.empty() ? size_t{0} : (text.size() + 1) * sizeof(char16_t);
    };
    if (imagePath.size() > capacity / sizeof(char16_t) || commandLine.size() > capacity / sizeof(char16_t) ||
        currentDirectory.size() > capacity / sizeof(char16_t) ||
        bytes(imagePath) > capacity || bytes(commandLine) > capacity - bytes(imagePath) ||
        bytes(currentDirectory) > capacity - bytes(imagePath) - bytes(commandLine))
        return NtStatus::BufferTooSmall;
    processImagePath_ = imagePath;
    processCommandLine_ = commandLine;
    processCurrentDirectory_ = currentDirectory;
    processParametersConfigured_ = true;
    return NtStatus::Success;
}

NtStatus EProcess::finalizeNativeImages() noexcept {
    if (!active(*this) || terminating_ || !images_) return NtStatus::InvalidParameter;
    if (imagesFinalized_) return NtStatus::Success;
    // Reject oversized metadata before import binding or protection changes so
    // an oversized path set cannot leave the image partially finalized.
    if (images_->size() > nativeImagePaths_.size()) return NtStatus::BufferTooSmall;
    size_t remaining = mm::PageSize4KB - 0x40 - images_->size() * sizeof(UserLoaderEntry);
    for (size_t i = 0; i < images_->size(); ++i) {
        const auto path = nativeImagePaths_[i];
        if (path.empty()) continue;
        const size_t bytes = (path.size() + 1) * sizeof(char16_t);
        if (bytes > remaining) return NtStatus::BufferTooSmall;
        remaining -= bytes;
    }
    uintptr_t tlsIndexAddresses[16]{};
    size_t tlsCount = 0;
    size_t tlsOffset = 0x100;
    for (size_t i = 0; i < images_->size(); ++i) {
        const auto* module = images_->module(i);
        if (!module) __builtin_trap();
        pe::PeImageView64::TlsLayout candidate{};
        const auto status = module->view.tlsLayout(module->immutableImage(), module->base, candidate);
        if (status != NtStatus::Success) {
            traceImageInitializationFailure("TLS inspection", status);
            return status;
        }
        if (!candidate.present) continue;
        if (tlsCount == 16 || candidate.alignment > 256 ||
            candidate.initialData.size() > mm::PageSize4KB - 0x100 ||
            candidate.zeroFill > mm::PageSize4KB - 0x100 - candidate.initialData.size())
            return NtStatus::NotImplemented;
        tlsOffset = (tlsOffset + candidate.alignment - 1) & ~(candidate.alignment - 1);
        const size_t tlsLength = candidate.initialData.size() + candidate.zeroFill;
        if (tlsOffset > mm::PageSize4KB || tlsLength > mm::PageSize4KB - tlsOffset)
            return NtStatus::NotImplemented;
        tlsOffset += tlsLength;
        bool writableIndex = false;
        for (size_t j = 0; j < module->view.headers().fileHeader.numberOfSections; ++j) {
            const auto section = module->view.section(j);
            const size_t size = section.misc.virtualSize > section.sizeOfRawData ? section.misc.virtualSize : section.sizeOfRawData;
            if ((section.characteristics & pe::IMAGE_SCN_MEM_WRITE) && candidate.indexRva >= section.virtualAddress &&
                candidate.indexRva - section.virtualAddress <= size &&
                sizeof(uint32_t) <= size - (candidate.indexRva - section.virtualAddress)) {
                writableIndex = true;
                break;
            }
        }
        if (!writableIndex) return NtStatus::NotSupported;
        tlsIndexAddresses[tlsCount++] = module->base + candidate.indexRva;
    }
    std::unique_ptr<pe::PeImageView64::TlsLayout[]> selectedTls;
    if (tlsCount) {
        selectedTls.reset(new (std::nothrow_t{}) pe::PeImageView64::TlsLayout[tlsCount]);
        if (!selectedTls) return NtStatus::NoMemory;
        size_t selected = 0;
        for (size_t i = 0; i < images_->size(); ++i) {
            const auto* module = images_->module(i);
            pe::PeImageView64::TlsLayout candidate{};
            if (!module || module->view.tlsLayout(module->immutableImage(), module->base, candidate) != NtStatus::Success)
                __builtin_trap();
            if (candidate.present) selectedTls[selected++] = candidate;
        }
        if (selected != tlsCount) __builtin_trap();
    }
    // Validate all currently rejectable TLS metadata before binding imports.
    // A failed preflight must leave the IAT untouched so callers may correct
    // image metadata or retry finalization without seeing already-patched RVAs.
    const auto binding = images_->bindAll();
    if (binding != NtStatus::Success) {
        traceImageInitializationFailure("import binding", binding);
        const auto failure = images_->bindingFailure();
        MicantNetbootSerialWrite("[MicaNT Ps] SEC_IMAGE missing import ");
        char failureText[128]{};
        const size_t libraryLength = failure.library.size() < sizeof(failureText) - 1
            ? failure.library.size() : sizeof(failureText) - 1;
        if (libraryLength) std::memcpy(failureText, failure.library.data(), libraryLength);
        MicantNetbootSerialWrite(failureText);
        MicantNetbootSerialWrite("!");
        std::memset(failureText, 0, sizeof(failureText));
        const size_t symbolLength = failure.symbol.size() < sizeof(failureText) - 1
            ? failure.symbol.size() : sizeof(failureText) - 1;
        if (symbolLength) std::memcpy(failureText, failure.symbol.data(), symbolLength);
        MicantNetbootSerialWrite(failureText);
        if (failure.ordinal) {
            MicantNetbootSerialWrite(" ordinal=");
            MicantNetbootSerialWriteNumber(failure.ordinal);
        }
        MicantNetbootSerialWrite(" importer=");
        std::memset(failureText, 0, sizeof(failureText));
        const size_t importerLength = failure.importer.size() < sizeof(failureText) - 1
            ? failure.importer.size() : sizeof(failureText) - 1;
        if (importerLength) std::memcpy(failureText, failure.importer.data(), importerLength);
        MicantNetbootSerialWrite(failureText);
        MicantNetbootSerialWrite(" loaded=");
        MicantNetbootSerialWriteNumber(images_->size());
        for (size_t moduleIndex = 0; moduleIndex < images_->size(); ++moduleIndex) {
            const auto* loaded = images_->module(moduleIndex);
            if (!loaded) continue;
            MicantNetbootSerialWrite(" ");
            std::memset(failureText, 0, sizeof(failureText));
            const size_t loadedLength = loaded->name.size() < sizeof(failureText) - 1
                ? loaded->name.size() : sizeof(failureText) - 1;
            if (loadedLength) std::memcpy(failureText, loaded->name.data(), loadedLength);
            MicantNetbootSerialWrite(failureText);
        }
        MicantNetbootSerialWrite("\r\n");
        return binding;
    }
    // Keep a compact module map in the boot log while bringing up file-backed
    // third-party PEs. This lets an exception RIP be attributed to the exact
    // image that owns it instead of guessing from the process allocation range.
    for (size_t moduleIndex = 0; moduleIndex < images_->size(); ++moduleIndex) {
        const auto* loaded = images_->module(moduleIndex);
        if (!loaded) continue;
        MicantNetbootSerialWrite("[MicaNT Ps] SEC_IMAGE module ");
        char moduleText[128]{};
        const size_t moduleLength = loaded->name.size() < sizeof(moduleText) - 1
            ? loaded->name.size() : sizeof(moduleText) - 1;
        if (moduleLength) std::memcpy(moduleText, loaded->name.data(), moduleLength);
        MicantNetbootSerialWrite(moduleText);
        MicantNetbootSerialWrite(" base=");
        MicantNetbootSerialWriteNumber(loaded->base);
        MicantNetbootSerialWrite(" size=");
        MicantNetbootSerialWriteNumber(loaded->view.headers().optionalHeader.sizeOfImage);
        MicantNetbootSerialWrite("\r\n");
    }
    for (size_t i = 0; i < images_->size() && !imageSectionBacked_; ++i) {
        const auto* module = images_->module(i);
        if (!module) __builtin_trap();
        uint32_t old{};
        const auto headerSize = (static_cast<size_t>(module->view.headers().optionalHeader.sizeOfHeaders) + mm::PageMask4KB) & ~mm::PageMask4KB;
        auto status = addressSpace_.protect(module->base, headerSize, mm::PAGE_READONLY, &old);
        if (status != NtStatus::Success) {
            traceImageInitializationFailure("image header protection", status);
            return status;
        }
        for (size_t j = 0; j < module->view.headers().fileHeader.numberOfSections; ++j) {
            const auto section = module->view.section(j);
            const size_t size = section.misc.virtualSize > section.sizeOfRawData ? section.misc.virtualSize : section.sizeOfRawData;
            if (!size) continue;
            const auto protection = ((section.characteristics & pe::IMAGE_SCN_MEM_EXECUTE) &&
                (section.characteristics & pe::IMAGE_SCN_MEM_WRITE)) ? mm::PAGE_EXECUTE_READWRITE :
                (section.characteristics & pe::IMAGE_SCN_MEM_EXECUTE) ? mm::PAGE_EXECUTE_READ :
                (section.characteristics & pe::IMAGE_SCN_MEM_WRITE) ? mm::PAGE_READWRITE : mm::PAGE_READONLY;
            status = addressSpace_.protect(module->base + section.virtualAddress,
                (size + mm::PageMask4KB) & ~mm::PageMask4KB, protection, &old);
            if (status != NtStatus::Success) {
                traceImageInitializationFailure("image section protection", status);
                return status;
            }
        }
        // Bootstrap images are loaded into private staging pages. Preserve
        // their now-installed PE protections as per-page maxima and close the
        // staging view before exposing the process.
        status = addressSpace_.prepareImageBinding(module->base, 0, 0, true);
        if (status != NtStatus::Success) {
            traceImageInitializationFailure("image binding preparation", status);
            return status;
        }
        status = addressSpace_.finalizeImageProtections(module->base);
        if (status != NtStatus::Success) {
            traceImageInitializationFailure("image protection finalization", status);
            return status;
        }
    }
    // Other process startup state remains uninitialized.
    if (!userPeb_) {
        auto status = addressSpace_.allocate(userPeb_, mm::PageSize4KB,
            mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
        if (status != NtStatus::Success) {
            traceImageInitializationFailure("PEB allocation", status);
            return status;
        }
    }
    if (!userLoaderData_) {
        auto status = addressSpace_.allocate(userLoaderData_, mm::PageSize4KB,
            mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
        if (status != NtStatus::Success) {
            traceImageInitializationFailure("loader data allocation", status);
            return status;
        }
    }
    if (!initializeLoaderMetadata(*images_, userLoaderData_,
            std::span<const std::u16string_view>{nativeImagePaths_.data(), images_->size()}))
        return NtStatus::BufferTooSmall;
    if (processParametersConfigured_) {
        if (!userProcessParameters_) {
            auto status = addressSpace_.allocate(userProcessParameters_, mm::PageSize4KB,
                mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
            if (status != NtStatus::Success) {
                traceImageInitializationFailure("process parameters allocation", status);
                return status;
            }
        }
        auto* parameters = reinterpret_cast<detail::UserProcessParameters*>(userProcessParameters_);
        *parameters = {};
        detail::UserMetadataWriter writer(
            {reinterpret_cast<uint8_t*>(userProcessParameters_), mm::PageSize4KB},
            userProcessParameters_, sizeof(*parameters));
        if (!writer.append(processCurrentDirectory_, parameters->currentDirectory) ||
            !writer.append(processImagePath_, parameters->imagePathName) ||
            !writer.append(processCommandLine_, parameters->commandLine)) return NtStatus::BufferTooSmall;
        detail::UserUnicodeString emptyEnvironment{};
        if (!writer.append(std::u16string_view{u"\0", 1}, emptyEnvironment))
            return NtStatus::BufferTooSmall;
        parameters->environment = emptyEnvironment.buffer;
    }
    const auto* mainImage = images_->module(0);
    if (!mainImage) return NtStatus::InvalidImageFormat;
    *reinterpret_cast<uint64_t*>(userPeb_ + 0x10) = mainImage->base;
    *reinterpret_cast<uint64_t*>(userPeb_ + 0x18) = userLoaderData_;
    *reinterpret_cast<uint64_t*>(userPeb_ + 0x20) = userProcessParameters_;
    for (size_t i = 0; i < tlsCount; ++i) {
        const auto index = static_cast<uint32_t>(i);
        std::memcpy(reinterpret_cast<void*>(tlsIndexAddresses[i]), &index, sizeof(index));
    }
    userTlsLayouts_ = std::move(selectedTls);
    userTlsCount_ = tlsCount;
    if (imageSectionBacked_) {
        for (size_t i = 0; i < images_->size(); ++i) {
            const auto* module = images_->module(i);
            if (!module) return NtStatus::InvalidImageFormat;
            const auto status = addressSpace_.finalizeImageProtections(module->base);
            if (status != NtStatus::Success) return status;
        }
    }
    processImagePath_ = {};
    processCommandLine_ = {};
    processCurrentDirectory_ = {};
    imagesFinalized_ = true;
    return NtStatus::Success;
}

NtStatus EProcess::initializeMainImageFromSection(section::SectionObject& image,
                                                   ob::ObjectHeader* sectionReference) noexcept {
    if (!active(*this) || terminating_ || images_ || imagesFinalized_ || !sectionReference ||
        (image.getAllocationAttributes() & (section::SEC_FILE | section::SEC_IMAGE)) !=
            (section::SEC_FILE | section::SEC_IMAGE) ||
        ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL)
        return NtStatus::InvalidParameter;
    if (!image.getFileSize() || image.getFileSize() > SIZE_MAX || !image.getSize() ||
        image.getSize() > SIZE_MAX || !image.getImageBase() || image.getFileName().empty())
        return NtStatus::InvalidImageFormat;

    const auto filePath = image.getFileName();
    if (filePath.size() > (UINT16_MAX - sizeof(char16_t)) / sizeof(char16_t) ||
        filePath.size() > mm::PageSize4KB / sizeof(char16_t) - 1 ||
        !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(filePath.data()),
                                           filePath.size() * sizeof(char16_t), false))
        return NtStatus::BufferTooSmall;

    std::unique_ptr<char16_t[]> commandLine(new (std::nothrow_t{}) char16_t[filePath.size()]);
    std::unique_ptr<OwnedImageInput[]> inputs(new (std::nothrow_t{}) OwnedImageInput[16]);
    auto imageSet = std::unique_ptr<um::NativeUserImageSet64>(
        new (std::nothrow_t{}) um::NativeUserImageSet64{});
    if (!commandLine || !inputs || !imageSet) {
        traceImageInitializationFailure("initial storage", NtStatus::NoMemory);
        return NtStatus::NoMemory;
    }
    ApiSetSchemaV6 apiSetSchema;
    imageSet->setApiSetHostResolver(&apiSetSchema,
        [](void* context, std::string_view contract, std::string_view importingModule,
           std::string_view& host) noexcept -> NtStatus {
            auto status = static_cast<ApiSetSchemaV6*>(context)->resolve(
                contract, importingModule, host);
            if (status != NtStatus::Success && isUniversalCrtApiSet(contract)) {
                host = "ucrtbase.dll";
                return NtStatus::Success;
            }
            return status;
        });
    ApiSetResolverDetach detachApiSetResolver{imageSet.get()};

    for (size_t i = 0; i < filePath.size(); ++i) {
        commandLine[i] = filePath[i];
    }

    const auto appendImage = [&](section::SectionObject& sectionImage,
                                 ob::ObjectHeader* sectionObjectReference,
                                 std::string_view moduleName,
                                 std::u16string_view modulePath,
                                 bool executable, size_t index) noexcept -> NtStatus {
        if (!sectionObjectReference || moduleName.empty() || modulePath.empty() ||
            sectionImage.getFileSize() == 0 || sectionImage.getFileSize() > SIZE_MAX ||
            sectionImage.getSize() == 0 || !sectionImage.getImageBase() ||
            modulePath.size() > SIZE_MAX / sizeof(char16_t) ||
            !hal::amd64::bootstrapKernelRange(reinterpret_cast<uintptr_t>(modulePath.data()),
                                               modulePath.size() * sizeof(char16_t), false))
            return NtStatus::InvalidImageFormat;
        const size_t rawSize = static_cast<size_t>(sectionImage.getFileSize());
        auto rawBytes = std::unique_ptr<uint8_t[]>(new (std::nothrow_t{}) uint8_t[rawSize]);
        auto nameCopy = std::unique_ptr<char[]>(new (std::nothrow_t{}) char[moduleName.size()]);
        auto pathCopy = std::unique_ptr<char16_t[]>(new (std::nothrow_t{}) char16_t[modulePath.size()]);
        if (!rawBytes || !nameCopy || !pathCopy) {
            traceImageInitializationFailure("dependency file-buffer allocation", NtStatus::NoMemory);
            return NtStatus::NoMemory;
        }
        size_t total{};
        while (total < rawSize) {
            size_t read{};
            const auto readStatus = sectionImage.readFile(total, rawBytes.get() + total,
                                                           rawSize - total, &read);
            if (readStatus != NtStatus::Success) return readStatus;
            if (!read || read > rawSize - total) return NtStatus::EndOfFile;
            total += read;
        }
        pe::PeImageView64 view;
        auto status = view.inspect({rawBytes.get(), rawSize});
        if (status != NtStatus::Success) return status;
        const auto& optional = view.headers().optionalHeader;
        const uint16_t characteristics = view.headers().fileHeader.characteristics;
        if (optional.imageBase != sectionImage.getImageBase() ||
            optional.sizeOfImage != sectionImage.getSize() ||
            !(characteristics & pe::IMAGE_FILE_EXECUTABLE_IMAGE) ||
            static_cast<bool>(characteristics & pe::IMAGE_FILE_DLL) == executable ||
            (executable && (!optional.addressOfEntryPoint ||
                            !view.executableRva(optional.addressOfEntryPoint))))
            return NtStatus::InvalidImageFormat;
        if (!executable && optional.addressOfEntryPoint &&
            !view.executableRva(optional.addressOfEntryPoint))
            return NtStatus::InvalidImageFormat;
        std::memcpy(nameCopy.get(), moduleName.data(), moduleName.size());
        std::memcpy(pathCopy.get(), modulePath.data(), modulePath.size() * sizeof(char16_t));
        uintptr_t mappedBase{};
        status = mapFileImageAtAvailableBase(addressSpace_, sectionImage,
            sectionObjectReference, view, mappedBase);
        if (status != NtStatus::Success) {
            char failedModule[128]{};
            const size_t failedModuleLength = moduleName.size() < sizeof(failedModule) - 1
                ? moduleName.size() : sizeof(failedModule) - 1;
            if (failedModuleLength)
                std::memcpy(failedModule, moduleName.data(), failedModuleLength);
            MicantNetbootSerialWrite("[MicaNT Ps] Failed dependency image module=");
            MicantNetbootSerialWrite(failedModule);
            MicantNetbootSerialWrite(" image-bytes=");
            MicantNetbootSerialWriteNumber(sectionImage.getSize());
            MicantNetbootSerialWrite(" status=");
            MicantNetbootSerialWriteNumber(static_cast<uint32_t>(status));
            MicantNetbootSerialWrite("\r\n");
            traceImageInitializationFailure("dependency section mapping", status);
            return status;
        }
        status = imageSet->addClonedModule({nameCopy.get(), moduleName.size()},
            {rawBytes.get(), rawSize},
            {reinterpret_cast<uint8_t*>(mappedBase), sectionImage.getSize()}, mappedBase);
        if (status != NtStatus::Success) {
            (void)addressSpace_.unmapSharedPages(mappedBase, true);
            traceImageInitializationFailure("dependency metadata allocation", status);
            return status;
        }
        inputs[index].bytes = std::move(rawBytes);
        inputs[index].name = std::move(nameCopy);
        inputs[index].path = std::move(pathCopy);
        nativeImagePaths_[index] = {inputs[index].path.get(), modulePath.size()};
        return NtStatus::Success;
    };

    const auto appendSystemDependency = [&](std::string_view requestedLibrary,
                                            std::string_view importingModule) noexcept -> NtStatus {
        std::string_view canonical = requestedLibrary;
        if (isApiSetContractName(requestedLibrary)) {
            const auto apiSetStatus = apiSetSchema.resolve(requestedLibrary, importingModule, canonical);
            if (apiSetStatus != NtStatus::Success) {
                // The generated freestanding runtime exposes Microsoft's CRT
                // API-set family through ucrtbase.dll. Other API-set families
                // still require the mounted ApiSetSchema and fail explicitly.
                if (!isUniversalCrtApiSet(requestedLibrary)) return apiSetStatus;
                canonical = "ucrtbase.dll";
            }
        }
        char canonicalLibraryStorage[132]{};
        std::string_view library = canonical;
        const bool hasDllSuffix = canonical.size() >= 4 &&
            canonical[canonical.size() - 4] == '.' &&
            (canonical[canonical.size() - 3] == 'd' || canonical[canonical.size() - 3] == 'D') &&
            (canonical[canonical.size() - 2] == 'l' || canonical[canonical.size() - 2] == 'L') &&
            (canonical[canonical.size() - 1] == 'l' || canonical[canonical.size() - 1] == 'L');
        if (!hasDllSuffix) {
            if (canonical.empty() || canonical.size() > sizeof(canonicalLibraryStorage) - 5)
                return NtStatus::NotSupported;
            std::memcpy(canonicalLibraryStorage, canonical.data(), canonical.size());
            std::memcpy(canonicalLibraryStorage + canonical.size(), ".dll", 4);
            library = {canonicalLibraryStorage, canonical.size() + 4};
        }
        if (imageSet->containsModule(library)) return NtStatus::Success;
        if (imageSet->size() == 16) return NtStatus::InsufficientResources;
        char16_t dependencyPathStorage[256]{};
        size_t dependencyPathLength{};
        auto dependencyStatus = buildSystemDllPath(library, dependencyPathStorage,
                                                   dependencyPathLength);
        if (dependencyStatus != NtStatus::Success) return dependencyStatus;
        const std::u16string_view dependencyPath{dependencyPathStorage, dependencyPathLength};
        ScopedHandle dependencySectionHandle;
        ScopedObjectReference dependencySectionReference;
        section::SectionObject* dependencySection{};
        dependencyStatus = openSystemImageSection(dependencyPath,
            dependencySectionHandle, dependencySectionReference, dependencySection);
        if (dependencyStatus != NtStatus::Success) {
            char missingLibrary[132]{};
            const size_t missingLength = library.size() < sizeof(missingLibrary) - 1
                ? library.size() : sizeof(missingLibrary) - 1;
            if (missingLength)
                std::memcpy(missingLibrary, library.data(), missingLength);
            MicantNetbootSerialWrite("[MicaNT Ps] SEC_IMAGE dependency DLL unavailable name=");
            MicantNetbootSerialWrite(missingLibrary);
            MicantNetbootSerialWrite(" status=");
            MicantNetbootSerialWriteNumber(static_cast<uint32_t>(dependencyStatus));
            MicantNetbootSerialWrite("\r\n");
            traceImageInitializationFailure("dependency section", dependencyStatus);
        }
        if (dependencyStatus == NtStatus::ObjectNameNotFound ||
            dependencyStatus == NtStatus::NoSuchFile)
            return NtStatus::NotSupported;
        if (dependencyStatus != NtStatus::Success) return dependencyStatus;
        dependencyStatus = appendImage(*dependencySection, dependencySectionReference.value,
                           library, dependencyPath, false, imageSet->size());
        traceImageInitializationFailure("dependency image mapping", dependencyStatus);
        return dependencyStatus;
    };

    char moduleNameStorage[260]{};
    std::string_view moduleName;
    NtStatus status = buildImageModuleName(filePath, moduleNameStorage, moduleName);
    if (status != NtStatus::Success) return status;
    status = appendImage(image, sectionReference, moduleName, filePath, true, 0);
    if (status != NtStatus::Success) {
        traceImageInitializationFailure("main image mapping", status);
        return status;
    }
    captureProcessImageName(moduleName);

    char16_t ntdllPathStorage[256]{};
    size_t ntdllPathLength{};
    status = buildNtdllPath(ntdllPathStorage, ntdllPathLength);
    if (status != NtStatus::Success) return status;
    const std::u16string_view ntdllPath{ntdllPathStorage, ntdllPathLength};
    ScopedHandle ntdllSectionHandle;
    ScopedObjectReference ntdllSectionReference;
    section::SectionObject* ntdllSection{};
    status = openSystemImageSection(ntdllPath, ntdllSectionHandle,
                                    ntdllSectionReference, ntdllSection);
    if (status != NtStatus::Success) return status;
    status = appendImage(*ntdllSection, ntdllSectionReference.value, "ntdll.dll", ntdllPath,
                         false, 1);
    if (status != NtStatus::Success) {
        traceImageInitializationFailure("ntdll mapping", status);
        return status;
    }

    for (size_t moduleIndex = 0; moduleIndex < imageSet->size(); ++moduleIndex) {
        const auto* module = imageSet->module(moduleIndex);
        if (!module) return NtStatus::InvalidImageFormat;
        status = module->view.forEachImportLibrary(module->immutableImage(),
            [&](std::string_view library) noexcept -> NtStatus {
                return appendSystemDependency(library, module->name);
            });
        if (status == NtStatus::Success)
            status = module->view.forEachDelayImportLibrary(module->immutableImage(),
                [&](std::string_view library) noexcept -> NtStatus {
                    return appendSystemDependency(library, module->name);
                });
        if (status != NtStatus::Success) {
            traceImageInitializationFailure("dependency loading", status);
            return status;
        }
    }

    // Import resolution can discover another image only by following an
    // export-forwarder string. Load those real file-backed modules too, then
    // rescan because their own imports may add further dependencies.
    for (;;) {
        const size_t previousCount = imageSet->size();
        um::NativeUserImageSet64::ForwarderFailure forwarderFailure{};
        status = imageSet->forEachMissingForwarderLibrary(
            [&](std::string_view library, std::string_view importingModule) noexcept -> NtStatus {
                return appendSystemDependency(library, importingModule);
            }, 0, SIZE_MAX, &forwarderFailure);
        if (status != NtStatus::Success) {
            traceForwarderFailure(forwarderFailure);
            traceImageInitializationFailure("forwarder loading", status);
            return status;
        }
        if (imageSet->size() == previousCount) break;
    }

    ownedImageInputs_ = std::move(inputs);
    images_ = std::move(imageSet);
    ownedCommandLine_ = std::move(commandLine);
    processImagePath_ = {ownedImageInputs_[0].path.get(), filePath.size()};
    processCommandLine_ = {ownedCommandLine_.get(), filePath.size()};
    processParametersConfigured_ = true;
    imageSectionBacked_ = true;

    status = finalizeNativeImages();
    if (status == NtStatus::NoSuchFile) return NtStatus::NotSupported;
    if (status != NtStatus::Success) {
        traceImageInitializationFailure("image finalization", status);
        return status;
    }
    status = publishApiSetMap(addressSpace_, userPeb_, userApiSetMap_, userApiSetMapSize_,
                              apiSetSchema);
    if (status != NtStatus::Success) {
        traceImageInitializationFailure("API-set map", status);
        return status;
    }
    uint64_t routine{};
    status = images_->resolve("ntdll.dll", "RtlUserThreadStart", 0, routine);
    if (status == NtStatus::Success)
        status = images_->resolve("ntdll.dll", "KiUserExceptionDispatcher", 0, routine);
    traceImageInitializationFailure("NTDLL entry resolution", status);
    return status;
}
}
