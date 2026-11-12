#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/executive/image_metadata.hpp"
#include <new>
#include <cstring>

namespace micant::sys {
NtStatus mapBootstrapImageView(mm::ProcessAddressSpace& space, uintptr_t& base, size_t size) noexcept;
}

namespace micant::ps {
namespace {
// Only public winternl.h fields are populated. Paths come from the caller;
// the in-memory bootstrap has no filesystem from which to infer them.
struct UserListEntry { uint64_t next; uint64_t previous; };
using detail::UserUnicodeString;
struct UserLoaderData { uint8_t reserved1[8]; uint64_t reserved2[3]; UserListEntry memoryOrder; };
struct UserLoaderEntry {
    uint64_t reserved1[2];
    UserListEntry memoryOrder;
    uint64_t reserved2[2];
    uint64_t dllBase;
    uint64_t reserved3[2];
    UserUnicodeString fullDllName;
    uint8_t reserved4[8];
    uint64_t reserved5[3];
    uint64_t checksum;
    uint32_t timestamp;
};
static_assert(offsetof(UserLoaderData, memoryOrder) == 0x20);
static_assert(offsetof(UserLoaderEntry, memoryOrder) == 0x10);
static_assert(offsetof(UserLoaderEntry, dllBase) == 0x30);
static_assert(offsetof(UserLoaderEntry, fullDllName) == 0x48);

bool initializeLoaderMetadata(const um::NativeUserImageSet64& images, uintptr_t address,
                              std::span<const std::u16string_view> paths) noexcept {
    constexpr size_t entriesOffset = 0x40;
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
    }
    auto* loader = reinterpret_cast<UserLoaderData*>(address);
    auto* entries = reinterpret_cast<UserLoaderEntry*>(address + entriesOffset);
    const uintptr_t head = address + offsetof(UserLoaderData, memoryOrder);
    size_t order[16]{};
    for (size_t i = 0; i < images.size(); ++i) {
        const auto* module = images.module(i);
        if (!module) return false;
        entries[i] = {};
        entries[i].dllBase = module->base;
        entries[i].checksum = module->view.headers().optionalHeader.checkSum;
        entries[i].timestamp = module->view.headers().fileHeader.timeDateStamp;
        if (!writer.append(paths[i], entries[i].fullDllName)) return false;
        order[i] = i;
        size_t position = i;
        while (position && images.module(order[position - 1])->base > module->base) {
            order[position] = order[position - 1];
            --position;
        }
        order[position] = i;
    }
    const auto linkAddress = [&](size_t index) {
        return address + entriesOffset + index * sizeof(UserLoaderEntry) + offsetof(UserLoaderEntry, memoryOrder);
    };
    loader->memoryOrder.next = images.size() ? linkAddress(order[0]) : head;
    loader->memoryOrder.previous = images.size() ? linkAddress(order[images.size() - 1]) : head;
    for (size_t i = 0; i < images.size(); ++i) {
        auto& link = entries[order[i]].memoryOrder;
        link.previous = i ? linkAddress(order[i - 1]) : head;
        link.next = i + 1 < images.size() ? linkAddress(order[i + 1]) : head;
    }
    return true;
}

bool active(const EProcess& process) noexcept {
    uintptr_t root{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(root));
    return root && root == process.getDirectoryTableBase();
}

// Limit the bootstrap loader to page-aligned, contiguous RO/RW/RX sections.
// Unrepresented gaps and W+X pages require protection semantics not yet
// provided by the native image-section backend.
NtStatus validateProtectionLayout(const pe::PeImageView64& view) noexcept {
    const auto& header = view.headers().optionalHeader;
    size_t end = (static_cast<size_t>(header.sizeOfHeaders) + mm::PageMask4KB) & ~mm::PageMask4KB;
    for (size_t i = 0; i < view.headers().fileHeader.numberOfSections; ++i) {
        const auto section = view.section(i);
        const size_t size = section.misc.virtualSize > section.sizeOfRawData ? section.misc.virtualSize : section.sizeOfRawData;
        if (!size) continue;
        if (section.virtualAddress != end || size > SIZE_MAX - mm::PageMask4KB ||
            !(section.characteristics & pe::IMAGE_SCN_MEM_READ) ||
            ((section.characteristics & pe::IMAGE_SCN_MEM_EXECUTE) &&
             (section.characteristics & pe::IMAGE_SCN_MEM_WRITE))) return NtStatus::NotSupported;
        const size_t rounded = (size + mm::PageMask4KB) & ~mm::PageMask4KB;
        if (end > header.sizeOfImage || rounded > header.sizeOfImage - end) return NtStatus::InvalidImageFormat;
        end += rounded;
    }
    return end == header.sizeOfImage ? NtStatus::Success : NtStatus::NotSupported;
}
}

NtStatus EProcess::loadNativeImage(std::string_view name, std::span<const uint8_t> file,
                                   std::u16string_view fullPath) noexcept {
    if (!active(*this) || terminating_ || imagesFinalized_) return NtStatus::InvalidParameter;
    if (fullPath.size() > (UINT16_MAX - sizeof(char16_t)) / sizeof(char16_t) ||
        fullPath.size() > mm::PageSize4KB / sizeof(char16_t) - 1) return NtStatus::BufferTooSmall;
    pe::PeImageView64 view;
    auto status = view.inspect(file);
    if (status != NtStatus::Success) return status;
    status = validateProtectionLayout(view);
    if (status != NtStatus::Success) return status;
    const uintptr_t base = view.headers().optionalHeader.imageBase;
    const size_t size = view.headers().optionalHeader.sizeOfImage;
    if (!images_) {
        images_.reset(new (std::nothrow_t{}) um::NativeUserImageSet64{});
        if (!images_) return NtStatus::NoMemory;
    }
    uintptr_t target = base;
    status = sys::mapBootstrapImageView(addressSpace_, target, size);
    if (status != NtStatus::Success) return status;
    status = images_->addModule(name, file, {reinterpret_cast<uint8_t*>(target), size}, target);
    if (status != NtStatus::Success) {
        if (addressSpace_.unmapSharedPages(target) != NtStatus::Success) __builtin_trap();
        return status;
    }
    nativeImagePaths_[images_->size() - 1] = fullPath;
    return NtStatus::Success;
}

NtStatus EProcess::setNativeProcessParameters(std::u16string_view imagePath,
                                               std::u16string_view commandLine) noexcept {
    if (!active(*this) || terminating_ || imagesFinalized_) return NtStatus::InvalidParameter;
    const size_t capacity = mm::PageSize4KB - sizeof(detail::UserProcessParameters);
    const auto bytes = [](std::u16string_view text) noexcept {
        return text.empty() ? size_t{0} : (text.size() + 1) * sizeof(char16_t);
    };
    if (imagePath.size() > capacity / sizeof(char16_t) || commandLine.size() > capacity / sizeof(char16_t) ||
        bytes(imagePath) > capacity || bytes(commandLine) > capacity - bytes(imagePath))
        return NtStatus::BufferTooSmall;
    processImagePath_ = imagePath;
    processCommandLine_ = commandLine;
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
    const auto binding = images_->bindAll();
    if (binding != NtStatus::Success) return binding;
    uintptr_t tlsIndexAddresses[16]{};
    size_t tlsCount = 0;
    size_t tlsOffset = 0x100;
    for (size_t i = 0; i < images_->size(); ++i) {
        const auto* module = images_->module(i);
        if (!module) __builtin_trap();
        pe::PeImageView64::TlsLayout candidate{};
        const auto status = module->view.tlsLayout(module->image, module->base, candidate);
        if (status != NtStatus::Success) return status;
        if (!candidate.present) continue;
        if (tlsCount == 16 || candidate.callbackCount || candidate.alignment > 256 ||
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
            if (!module || module->view.tlsLayout(module->image, module->base, candidate) != NtStatus::Success)
                __builtin_trap();
            if (candidate.present) selectedTls[selected++] = candidate;
        }
        if (selected != tlsCount) __builtin_trap();
    }
    for (size_t i = 0; i < images_->size(); ++i) {
        const auto* module = images_->module(i);
        if (!module) __builtin_trap();
        uint32_t old{};
        const auto headerSize = (static_cast<size_t>(module->view.headers().optionalHeader.sizeOfHeaders) + mm::PageMask4KB) & ~mm::PageMask4KB;
        auto status = addressSpace_.protect(module->base, headerSize, mm::PAGE_READONLY, &old);
        if (status != NtStatus::Success) return status;
        for (size_t j = 0; j < module->view.headers().fileHeader.numberOfSections; ++j) {
            const auto section = module->view.section(j);
            const size_t size = section.misc.virtualSize > section.sizeOfRawData ? section.misc.virtualSize : section.sizeOfRawData;
            if (!size) continue;
            const auto protection = (section.characteristics & pe::IMAGE_SCN_MEM_EXECUTE) ? mm::PAGE_EXECUTE_READ :
                (section.characteristics & pe::IMAGE_SCN_MEM_WRITE) ? mm::PAGE_READWRITE : mm::PAGE_READONLY;
            status = addressSpace_.protect(module->base + section.virtualAddress,
                (size + mm::PageMask4KB) & ~mm::PageMask4KB, protection, &old);
            if (status != NtStatus::Success) return status;
        }
    }
    // Other process startup state remains uninitialized.
    if (!userPeb_) {
        auto status = addressSpace_.allocate(userPeb_, mm::PageSize4KB,
            mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
        if (status != NtStatus::Success) return status;
    }
    if (!userLoaderData_) {
        auto status = addressSpace_.allocate(userLoaderData_, mm::PageSize4KB,
            mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
        if (status != NtStatus::Success) return status;
    }
    if (!initializeLoaderMetadata(*images_, userLoaderData_,
            std::span<const std::u16string_view>{nativeImagePaths_.data(), images_->size()}))
        return NtStatus::BufferTooSmall;
    if (processParametersConfigured_) {
        if (!userProcessParameters_) {
            auto status = addressSpace_.allocate(userProcessParameters_, mm::PageSize4KB,
                mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
            if (status != NtStatus::Success) return status;
        }
        auto* parameters = reinterpret_cast<detail::UserProcessParameters*>(userProcessParameters_);
        *parameters = {};
        detail::UserMetadataWriter writer(
            {reinterpret_cast<uint8_t*>(userProcessParameters_), mm::PageSize4KB},
            userProcessParameters_, sizeof(*parameters));
        if (!writer.append(processImagePath_, parameters->imagePathName) ||
            !writer.append(processCommandLine_, parameters->commandLine)) return NtStatus::BufferTooSmall;
    }
    *reinterpret_cast<uint64_t*>(userPeb_ + 0x18) = userLoaderData_;
    *reinterpret_cast<uint64_t*>(userPeb_ + 0x20) = userProcessParameters_;
    for (size_t i = 0; i < tlsCount; ++i) {
        const auto index = static_cast<uint32_t>(i);
        std::memcpy(reinterpret_cast<void*>(tlsIndexAddresses[i]), &index, sizeof(index));
    }
    userTlsLayouts_ = std::move(selectedTls);
    userTlsCount_ = tlsCount;
    nativeImagePaths_ = {};
    processImagePath_ = {};
    processCommandLine_ = {};
    imagesFinalized_ = true;
    return NtStatus::Success;
}
}
