#if defined(MICANT_FREESTANDING)
#include "micant/km/core/syscalls.hpp"
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/section.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include <new>

namespace micant::sys {
namespace {
mm::ProcessAddressSpace addressSpace;
ob::ObjectType sectionType;
mm::ProcessAddressSpace& currentAddressSpace() noexcept {
    auto* process = ke::KeGetCurrentProcess();
    return process ? process->getAddressSpace() : addressSpace;
}
void deleteSection(ob::ObjectHeader* header) {
    header->as<section::SectionObject>()->~SectionObject();
}
bool acquireSectionView(void* owner) noexcept {
    static_cast<ob::ObjectHeader*>(owner)->addReference();
    return true;
}
void releaseSectionView(void* owner) noexcept {
    ob::ObDereferenceObject(static_cast<ob::ObjectHeader*>(owner));
}
bool acquireFileSection(void* context) noexcept {
    if (!context) return false;
    static_cast<ob::ObjectHeader*>(context)->addReference();
    return true;
}
void releaseFileSection(void* context) noexcept {
    ob::ObDereferenceObject(static_cast<ob::ObjectHeader*>(context));
}
NtStatus readSectionFile(void* context, uint64_t offset, void* buffer,
                         size_t length, size_t* bytesRead) noexcept {
    if (!context) return NtStatus::InvalidHandle;
    auto* file = static_cast<ob::ObjectHeader*>(context)->as<io::FileObject>();
    return file->read(offset, buffer, length, bytesRead);
}
NtStatus acquireSectionImage(void* context) noexcept {
    return static_cast<ob::ObjectHeader*>(context)->as<io::FileObject>()->acquireImage();
}
void releaseSectionImage(void* context) noexcept {
    static_cast<ob::ObjectHeader*>(context)->as<io::FileObject>()->releaseImage();
}
uint64_t querySectionFileSize(void* context) noexcept {
    return static_cast<ob::ObjectHeader*>(context)->as<io::FileObject>()->size();
}
NtStatus publishSectionControl(void* context, void* candidate, bool (*retain)(void*) noexcept,
                               void (*release)(void*) noexcept, void** result) noexcept {
    return static_cast<ob::ObjectHeader*>(context)->as<io::FileObject>()->publishImageControl(
        candidate, retain, release, result);
}
}

bool initializeBootstrapSectionServices() noexcept {
    sectionType.typeId = ob::ObjectTypeId::Section;
    sectionType.validAccessMask = section::SECTION_ALL_ACCESS;
    sectionType.genericMapping = {
        ob::STANDARD_RIGHTS_READ | section::SECTION_MAP_READ | section::SECTION_QUERY,
        ob::STANDARD_RIGHTS_WRITE | section::SECTION_MAP_WRITE,
        ob::STANDARD_RIGHTS_EXECUTE | section::SECTION_MAP_EXECUTE,
        section::SECTION_ALL_ACCESS
    };
    sectionType.deleteProcedure = deleteSection;
    // A file-backed image section releases its filesystem lease in the
    // destructor; that release may wait for the stream gate. Never run it
    // from a DPC or with interrupts disabled after the clock starts.
    sectionType.deferDelete = io::IoDeferPassiveObjectDelete;
    return addressSpace.configurePagingBackend(
        {nullptr, hal::amd64::mapBootstrapManagedPage, hal::amd64::unmapBootstrapManagedPage,
         hal::amd64::mapBootstrapProtectManagedPage, hal::amd64::mapBootstrapManagedView,
         hal::amd64::unmapBootstrapManagedView, nullptr, hal::amd64::replaceBootstrapManagedPage},
        hal::amd64::BootstrapUserAddressBase, hal::amd64::BootstrapUserAddressSize);
}

// Resolve a process image candidate using the canonical section type and
// execute access. The caller owns the returned reference and must verify that
// the section is a validated SEC_IMAGE object.
NtStatus referenceProcessImageSection(Handle handle, ob::ObjectHeader*& reference) noexcept {
    reference = nullptr;
    ob::HandleTable* table{};
    Handle raw{};
    auto status = ob::detail::resolveHandle(handle, table, raw);
    if (status != NtStatus::Success) return status;
    status = ob::ObReferenceObjectByHandle(raw, *table, section::SECTION_MAP_EXECUTE, &sectionType, &reference);
    if (status == NtStatus::Success &&
        !(reference->as<section::SectionObject>()->getAllocationAttributes() & section::SEC_IMAGE)) {
        ob::ObDereferenceObject(reference);
        reference = nullptr;
        return NtStatus::InvalidFileForSection;
    }
    return status;
}

NtStatus mapBootstrapFileImageView(mm::ProcessAddressSpace& space, uintptr_t& base,
                                  section::SectionObject& object,
                                  ob::ObjectHeader* sectionReference) noexcept {
    if (!sectionReference || !(object.getAllocationAttributes() & section::SEC_IMAGE) ||
        object.getImageBase() != base || !object.getPageProtections())
        return NtStatus::InvalidImageFormat;
    const auto backing = object.getBackingReference();
    if (!backing.pfns || !backing.pageCount) return NtStatus::InvalidImageFormat;
    const size_t size = object.getSize();
    const auto status = space.mapSharedPages(base, size, mm::PAGE_READWRITE,
        backing.pfns, backing.pageCount, sectionReference, acquireSectionView, releaseSectionView,
        object.getMaximumImageProtection(), true, false, 0, nullptr, true,
        object.getImageViewProtections(), object.getSharedImagePages());
    return status;
}

NtStatus mapBootstrapRelocatedFileImageView(mm::ProcessAddressSpace& space, uintptr_t& base,
                                            section::SectionObject& object,
                                            ob::ObjectHeader* sectionReference) noexcept {
    if (!sectionReference || !(object.getAllocationAttributes() & section::SEC_IMAGE) ||
        !object.getPageProtections())
        return NtStatus::InvalidImageFormat;
    const auto backing = object.getBackingReference();
    if (!backing.pfns || !backing.pageCount) return NtStatus::InvalidImageFormat;
    return space.mapSharedPages(base, object.getSize(), mm::PAGE_READWRITE,
        backing.pfns, backing.pageCount, sectionReference, acquireSectionView, releaseSectionView,
        object.getMaximumImageProtection(), true, false, 0, nullptr, true,
        object.getImageViewProtections(), object.getSharedImagePages());
}

// Ps maps the same validated section backing used by NtMapViewOfSection. The
// VAD holds an Ob reference until the process unmaps the image.
NtStatus mapBootstrapImageView(mm::ProcessAddressSpace& space, uintptr_t& base, size_t size) noexcept {
    if (sectionType.typeId != ob::ObjectTypeId::Section) return NtStatus::Unsuccessful;
    void* body{};
    ob::ObjectHeader* header{};
    auto status = ob::ObCreateObject(ob::KernelMode, &sectionType, nullptr,
                                     sizeof(section::SectionObject), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* object = new (body) section::SectionObject(size, section::SEC_COMMIT, mm::PAGE_READWRITE);
    status = object->getInitializationStatus();
    if (status == NtStatus::Success) {
        const auto backing = object->getBackingReference();
        status = space.mapSharedPages(base, size, mm::PAGE_READWRITE, backing.pfns, backing.pageCount,
                                      header, acquireSectionView, releaseSectionView, mm::PAGE_READWRITE,
                                      true, false, 0, nullptr, true);
    }
    ob::ObDereferenceObject(header);
    return status;
}

uint32_t bootstrapSectionObjectCount() noexcept {
    return sectionType.totalNumberOfObjects.load(std::memory_order_relaxed);
}

NtStatus NtAllocateVirtualMemory(Handle process, uintptr_t* base, uintptr_t zeroBits,
                                 size_t* regionSize, uint32_t type, uint32_t protection) {
    if (!base || !regionSize || !*regionSize) return NtStatus::InvalidParameter;
    mm::ProcessAddressSpace* targetSpace{};
    ob::ObjectHeader* targetProcessReference{};
    if (zeroBits) return NtStatus::NotImplemented;
    const size_t offset = *base & mm::PageMask4KB;
    if (*regionSize > SIZE_MAX - offset || *regionSize + offset > SIZE_MAX - mm::PageMask4KB)
        return NtStatus::InvalidParameter;
    const size_t rounded = (*regionSize + offset + mm::PageMask4KB) & ~mm::PageMask4KB;
    uintptr_t target = *base;
    const auto status = currentAddressSpace().allocate(target, *regionSize, type, protection);
    if (status == NtStatus::Success) { *base = target; *regionSize = rounded; }
    return status;
}

NtStatus NtFreeVirtualMemory(Handle process, uintptr_t* base, size_t* regionSize, uint32_t type) {
    if (!base || !regionSize || !*base) return NtStatus::InvalidParameter;
    if (process != InvalidHandleValue) return NtStatus::NotImplemented;
    if (type != mm::MEM_RELEASE && type != mm::MEM_DECOMMIT) return NtStatus::NotImplemented;
    auto& space = currentAddressSpace();
    const auto* vad = space.findVad(*base);
    if (!vad) return NtStatus::InvalidParameter;
    uintptr_t start = *base;
    size_t length = *regionSize;
    if (type == mm::MEM_RELEASE || !length) {
        if (start != vad->startingAddress || (type == mm::MEM_RELEASE && length))
            return NtStatus::InvalidParameter;
        length = vad->size();
    } else {
        const size_t offset = start & mm::PageMask4KB;
        if (length > SIZE_MAX - offset || length + offset > SIZE_MAX - mm::PageMask4KB)
            return NtStatus::InvalidParameter;
        length = (length + offset + mm::PageMask4KB) & ~mm::PageMask4KB;
        start &= ~static_cast<uintptr_t>(mm::PageMask4KB);
    }
    const auto status = space.free(start, type == mm::MEM_RELEASE ? 0 : length, type);
    if (status == NtStatus::Success) { *base = start; *regionSize = length; }
    return status;
}

NtStatus NtProtectVirtualMemory(Handle process, uintptr_t* base, size_t* regionSize,
                                uint32_t protection, uint32_t* oldProtection) {
    if (process != InvalidHandleValue) return NtStatus::NotImplemented;
    if (!base || !regionSize || !oldProtection) return NtStatus::InvalidParameter;
    const auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    uintptr_t address{};
    size_t length{};
    auto result = ex::ProbeForWrite(base, sizeof(*base), alignof(uintptr_t), mode);
    if (result == NtStatus::Success)
        result = ex::ProbeForWrite(regionSize, sizeof(*regionSize), alignof(size_t), mode);
    if (result == NtStatus::Success)
        result = ex::ProbeForWrite(oldProtection, sizeof(*oldProtection), alignof(uint32_t), mode);
    if (result == NtStatus::Success) result = ex::ExSafeCopyFromUser(&address, base, sizeof(address), mode);
    if (result == NtStatus::Success) result = ex::ExSafeCopyFromUser(&length, regionSize, sizeof(length), mode);
    if (result == NtStatus::Success && (!length || address > UINTPTR_MAX - (length - 1)))
        result = NtStatus::InvalidParameter;
    if (result == NtStatus::Success) {
        const uintptr_t start = address & ~static_cast<uintptr_t>(mm::PageMask4KB);
        const uintptr_t last = (address + length - 1) | mm::PageMask4KB;
        if (last < start || last - start == UINTPTR_MAX) result = NtStatus::InvalidParameter;
        else {
            struct Output {
                uintptr_t* base;
                size_t* size;
                uint32_t* old;
                uintptr_t start;
                size_t length;
                mm::KPROCESSOR_MODE mode;
            } output{base, regionSize, oldProtection, start,
                     static_cast<size_t>(last - start + 1), mode};
            result = currentAddressSpace().protectWithCommit(start, output.length, protection,
                [](void* context, uint32_t old) noexcept {
                    auto& value = *static_cast<Output*>(context);
                    auto status = ex::ExSafeCopyToUser(value.base, &value.start, sizeof(value.start), value.mode);
                    if (status == NtStatus::Success)
                        status = ex::ExSafeCopyToUser(value.size, &value.length, sizeof(value.length), value.mode);
                    if (status == NtStatus::Success)
                        status = ex::ExSafeCopyToUser(value.old, &old, sizeof(old), value.mode);
                    return status;
                }, &output);
        }
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

NtStatus NtQueryVirtualMemory(Handle process, uintptr_t baseAddress,
                              uint32_t informationClass, void* information,
                              size_t informationLength, size_t* returnLength) noexcept {
    struct MemoryBasicInformation64 {
        uintptr_t baseAddress;
        uintptr_t allocationBase;
        uint32_t allocationProtect;
        uint16_t partitionId;
        uint16_t reserved;
        size_t regionSize;
        uint32_t state;
        uint32_t protect;
        uint32_t type;
        uint32_t padding;
    };
    static_assert(sizeof(MemoryBasicInformation64) == 48);
    if (process != InvalidHandleValue) return NtStatus::NotSupported;
    if (informationClass != 0) return NtStatus::InvalidInfoClass;
    if (!information) return NtStatus::InvalidParameter;
    if (informationLength < sizeof(MemoryBasicInformation64)) return NtStatus::InfoLengthMismatch;
    const auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    auto result = ex::ProbeForWrite(information, sizeof(MemoryBasicInformation64),
                                    alignof(MemoryBasicInformation64), mode);
    if (result == NtStatus::Success && returnLength)
        result = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(size_t), mode);
    MemoryBasicInformation64 output{};
    mm::ProcessAddressSpace::QueryRegion region{};
    if (result == NtStatus::Success && !currentAddressSpace().queryRegion(baseAddress, region))
        result = NtStatus::InvalidParameter;
    if (result == NtStatus::Success) {
        output.baseAddress = region.baseAddress;
        output.allocationBase = region.allocationBase;
        output.allocationProtect = region.allocationProtect;
        output.regionSize = region.regionSize;
        output.state = region.state;
        output.protect = region.protect;
        output.type = region.type;
        result = ex::ExSafeCopyToUser(information, &output, sizeof(output), mode);
    }
    if (result == NtStatus::Success && returnLength) {
        const size_t written = sizeof(output);
        result = ex::ExSafeCopyToUser(returnLength, &written, sizeof(written), mode);
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return result;
}

static NtStatus createSection(Handle* out, uint32_t access, ObjectAttributes* attributes,
                              LargeInteger* maximum, uint32_t protect, uint32_t allocation,
                              Handle file, mm::KPROCESSOR_MODE inputMode,
                              mm::KPROCESSOR_MODE outputMode) {
    const auto withInterruptsDisabled = [](auto&& operation) noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const auto status = operation();
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    };
    const auto copyIn = [&](void* destination, const void* source, size_t length,
                            size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            if (length && alignment > 1 &&
                (reinterpret_cast<uintptr_t>(source) & (alignment - 1)))
                return NtStatus::DatatypeMisalignment;
            return ex::ExSafeCopyFromUser(destination, source, length, inputMode);
        });
    };
    const auto copyOut = [&](void* destination, const void* source, size_t length,
                             size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            if (length && alignment > 1 &&
                (reinterpret_cast<uintptr_t>(destination) & (alignment - 1)))
                return NtStatus::DatatypeMisalignment;
            return ex::ExSafeCopyToUser(destination, source, length, outputMode);
        });
    };

    Handle* callerOutput = out;
    if (outputMode == mm::UserMode) {
        if (!out) return NtStatus::InvalidParameter;
        const auto outputProbe = withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForWrite(out, sizeof(Handle), alignof(Handle), outputMode);
        });
        if (outputProbe != NtStatus::Success) return outputProbe;
    } else if (!out) {
        return NtStatus::InvalidParameter;
    }

    ObjectAttributes capturedAttributes{};
    if (attributes) {
        const auto captureStatus = copyIn(&capturedAttributes, attributes,
                                          sizeof(capturedAttributes), alignof(ObjectAttributes));
        if (captureStatus != NtStatus::Success) return captureStatus;
        attributes = &capturedAttributes;
    }
    LargeInteger capturedMaximum{};
    if (maximum) {
        const auto captureStatus = copyIn(&capturedMaximum, maximum,
                                          sizeof(capturedMaximum), alignof(LargeInteger));
        if (captureStatus != NtStatus::Success) return captureStatus;
        maximum = &capturedMaximum;
    }

    const bool imageSection = (allocation & section::SEC_IMAGE) != 0;
    if (!out || (!imageSection && (!maximum || maximum->quadPart <= 0)) ||
        (imageSection && (allocation & ~uint32_t{section::SEC_IMAGE | section::SEC_FILE})))
        return NtStatus::InvalidParameter;
    if (maximum && maximum->quadPart < 0) return NtStatus::InvalidParameter;
    if constexpr (sizeof(size_t) < sizeof(uint64_t)) {
        if (maximum && static_cast<uint64_t>(maximum->quadPart) > SIZE_MAX)
            return NtStatus::SectionTooBig;
    }
    if (attributes) {
        if (attributes->length != sizeof(ObjectAttributes)) return NtStatus::InvalidParameter;
        if (attributes->rootDirectory || attributes->objectName || attributes->attributes ||
            attributes->securityDescriptor || attributes->securityQualityOfService)
            return NtStatus::NotImplemented;
    }
    const uint32_t mappedAccess = ob::MapGenericAccess(access, sectionType.genericMapping);
    if (mappedAccess & ~sectionType.validAccessMask) return NtStatus::NotImplemented;
    if (imageSection && !file) return NtStatus::InvalidParameter;
    if (!imageSection && file) return NtStatus::NotImplemented;
    NtStatus status = NtStatus::Success;
    ob::ObjectHeader* fileReference{};
    io::FileObject* fileObject{};
    if (imageSection) {
        // FILE_READ_DATA/WRITE_DATA/EXECUTE as required by the creation
        // protection. SEC_IMAGE page protections still come from the PE.
        uint32_t fileAccess{};
        switch (protect) {
        case mm::PAGE_READONLY: case mm::PAGE_WRITECOPY: fileAccess = 0x0001; break;
        case mm::PAGE_READWRITE: fileAccess = 0x0003; break;
        case mm::PAGE_EXECUTE: fileAccess = 0x0020; break;
        case mm::PAGE_EXECUTE_READ: case mm::PAGE_EXECUTE_WRITECOPY: fileAccess = 0x0021; break;
        case mm::PAGE_EXECUTE_READWRITE: fileAccess = 0x0023; break;
        default: return NtStatus::InvalidPageProtection;
        }
        status = io::IoReferenceFileHandle(file, fileAccess, fileObject, fileReference);
        if (status != NtStatus::Success) return status;
    }
    void* body{};
    ob::ObjectHeader* header{};
    status = ob::ObCreateObject(ob::KernelMode, &sectionType, nullptr,
                                         sizeof(section::SectionObject), &body, &header);
    if (status != NtStatus::Success) {
        if (fileReference) ob::ObDereferenceObject(fileReference);
        return status;
    }
    const size_t maximumSize = maximum && maximum->quadPart > 0
        ? static_cast<size_t>(maximum->quadPart) : 0;
    section::SectionObject* object{};
    if (imageSection) {
        section::SectionFileBacking backing{fileReference, fileObject->size(), fileObject->name(), acquireFileSection,
                                            releaseFileSection, readSectionFile, acquireSectionImage,
                                            releaseSectionImage, querySectionFileSize, publishSectionControl,
                                            fileObject->supportsImageControl()};
        object = new (body) section::SectionObject(maximumSize,
            section::SEC_FILE | section::SEC_IMAGE, protect, backing);
        ob::ObDereferenceObject(fileReference);
    } else {
        object = new (body) section::SectionObject(maximumSize, allocation, protect);
    }
    status = object->getInitializationStatus();
    Handle createdHandle{};
    ob::ObjectHeader* removed{};
    if (status == NtStatus::Success) {
        // Keep insertion, copyout and possible removal in one BSP-excluded
        // transaction. The file read above may block, but no other thread
        // may close/reuse a newly published handle before rollback.
        status = withInterruptsDisabled([&]() noexcept {
            auto& table = ob::detail::currentHandleTable();
            auto result = table.createHandle(header, createdHandle, mappedAccess);
            if (result != NtStatus::Success) return result;
            result = copyOut(callerOutput, &createdHandle, sizeof(createdHandle), alignof(Handle));
            if (result != NtStatus::Success) {
                removed = table.takeHandle(createdHandle);
                if (!removed) __builtin_trap();
            }
            return result;
        });
    }
    // Section deletion can release a filesystem lease and wait; release both
    // references only after restoring the caller's scheduling state.
    if (removed) (void)ob::ObFinishClose(removed, createdHandle);
    ob::ObDereferenceObject(header);
    return status;
}

NtStatus NtCreateSection(Handle* out, uint32_t access, ObjectAttributes* attributes,
                         LargeInteger* maximum, uint32_t protect, uint32_t allocation,
                         Handle file) {
    const auto* current = ke::KeGetCurrentThread();
    const mm::KPROCESSOR_MODE callerMode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    return createSection(out, access, attributes, maximum, protect, allocation, file, callerMode, callerMode);
}

NtStatus NtCreateSectionFromKernel(Handle* out, uint32_t access,
                                   ObjectAttributes* attributes, LargeInteger* maximum,
                                   uint32_t protect, uint32_t allocation, Handle file) {
    return createSection(out, access, attributes, maximum, protect, allocation, file,
                         mm::KernelMode, mm::KernelMode);
}

NtStatus NtCreateSectionCaptured(Handle* userOut, uint32_t access,
                                 ObjectAttributes* attributes, LargeInteger* maximum,
                                 uint32_t protect, uint32_t allocation, Handle file) {
    return createSection(userOut, access, attributes, maximum, protect, allocation, file,
                         mm::KernelMode, mm::UserMode);
}

static NtStatus mapViewOfSection(Handle handle, Handle process, uintptr_t* base, uintptr_t zeroBits,
                                 size_t commitSize, LargeInteger* offset, size_t* viewSize,
                                 uint32_t inherit, uint32_t allocation, uint32_t protection,
                                 mm::KPROCESSOR_MODE callerMode) {
    const auto withInterruptsDisabled = [](auto&& operation) noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const auto status = operation();
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    };
    const auto copyIn = [&](void* destination, const void* source, size_t length,
                            size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            if (length && alignment > 1 &&
                (reinterpret_cast<uintptr_t>(source) & (alignment - 1)))
                return NtStatus::DatatypeMisalignment;
            return ex::ExSafeCopyFromUser(destination, source, length, callerMode);
        });
    };
    const auto copyOut = [&](void* destination, const void* source, size_t length,
                             size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            if (length && alignment > 1 &&
                (reinterpret_cast<uintptr_t>(destination) & (alignment - 1)))
                return NtStatus::DatatypeMisalignment;
            return ex::ExSafeCopyToUser(destination, source, length, callerMode);
        });
    };
    const auto probeWrite = [&](void* destination, size_t length, size_t alignment) noexcept {
        return withInterruptsDisabled([&]() noexcept {
            return ex::ProbeForWrite(destination, length, alignment, callerMode);
        });
    };
    if (!base || !viewSize) return NtStatus::InvalidParameter;
    uintptr_t capturedBase{};
    size_t capturedViewSize{};
    LargeInteger capturedOffset{};
    auto status = copyIn(&capturedBase, base, sizeof(capturedBase), alignof(uintptr_t));
    if (status != NtStatus::Success) return status;
    status = copyIn(&capturedViewSize, viewSize, sizeof(capturedViewSize), alignof(size_t));
    if (status != NtStatus::Success) return status;
    if (offset) {
        status = copyIn(&capturedOffset, offset, sizeof(capturedOffset), alignof(LargeInteger));
        if (status != NtStatus::Success) return status;
    }
    status = probeWrite(base, sizeof(*base), alignof(uintptr_t));
    if (status != NtStatus::Success) return status;
    status = probeWrite(viewSize, sizeof(*viewSize), alignof(size_t));
    if (status != NtStatus::Success) return status;
    if (offset) {
        status = probeWrite(offset, sizeof(*offset), alignof(LargeInteger));
        if (status != NtStatus::Success) return status;
    }
    if (inherit != 1 && inherit != 2) return NtStatus::InvalidParameter;
    if (zeroBits || commitSize || allocation)
        return NtStatus::NotImplemented;
    const bool copyProtection = protection == mm::PAGE_WRITECOPY || protection == mm::PAGE_EXECUTE_WRITECOPY;
    const bool executableProtection = protection == mm::PAGE_EXECUTE_READ ||
                                      protection == mm::PAGE_EXECUTE_READWRITE ||
                                      protection == mm::PAGE_EXECUTE_WRITECOPY;
    if (protection != mm::PAGE_READONLY && protection != mm::PAGE_READWRITE &&
        !executableProtection && !copyProtection)
        return NtStatus::NotImplemented;
    const uint32_t desired = protection == mm::PAGE_EXECUTE_READWRITE
        ? section::SECTION_MAP_EXECUTE | section::SECTION_MAP_WRITE : executableProtection
        ? section::SECTION_MAP_EXECUTE | section::SECTION_MAP_READ :
        protection == mm::PAGE_READWRITE ? section::SECTION_MAP_WRITE : section::SECTION_MAP_READ;
    ob::ObjectHeader* header{};
    ob::HandleTable* table{};
    Handle raw{};
    status = ob::detail::resolveHandle(handle, table, raw);
    if (status == NtStatus::Success)
        status = ob::ObReferenceObjectByHandle(raw, *table, desired, &sectionType, &header);
    if (status != NtStatus::Success) return status;
    auto* object = header->as<section::SectionObject>();
    const bool imageView = object->getAllocationAttributes() & section::SEC_IMAGE;
    if (imageView) {
        const auto* pageProtections = object->getImageViewProtections();
        if (!pageProtections) {
            ob::ObDereferenceObject(header);
            return NtStatus::InvalidImageFormat;
        }
        // Access is checked against the protection requested by the caller;
        // SEC_IMAGE derives the actual view protections from the PE sections.
    }
    if (!imageView && executableProtection) {
        ob::ObDereferenceObject(header);
        return NtStatus::SectionProtection;
    }
    if (!imageView && copyProtection) {
        ob::ObDereferenceObject(header);
        return NtStatus::NotImplemented;
    }
    if (offset && capturedOffset.quadPart < 0) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidParameter;
    }
    const uint64_t sectionOffset = offset ? static_cast<uint64_t>(capturedOffset.quadPart) &
        ~(static_cast<uint64_t>(mm::ProcessAddressSpace::AllocationGranularity) - 1) : 0;
    if (imageView && sectionOffset != 0) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidViewSize;
    }
    if (!imageView && protection == mm::PAGE_READWRITE && object->getPageProtection() != mm::PAGE_READWRITE) {
        ob::ObDereferenceObject(header);
        return NtStatus::SectionProtection;
    }
    if (object->getSize() > SIZE_MAX - mm::PageMask4KB) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidViewSize;
    }
    const size_t sectionSize = (object->getSize() + mm::PageMask4KB) & ~mm::PageMask4KB;
    if (sectionOffset >= sectionSize) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidViewSize;
    }
    const size_t remaining = sectionSize - static_cast<size_t>(sectionOffset);
    size_t length = capturedViewSize ? capturedViewSize : remaining;
    if (imageView && length != remaining) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidViewSize;
    }
    if (!length || length > remaining || length > SIZE_MAX - mm::PageMask4KB) {
        const auto failure = length > remaining ? NtStatus::InvalidViewSize : NtStatus::InvalidParameter;
        ob::ObDereferenceObject(header);
        return failure;
    }
    length = (length + mm::PageMask4KB) & ~mm::PageMask4KB;
    uintptr_t target = capturedBase ? capturedBase & ~(static_cast<uintptr_t>(mm::ProcessAddressSpace::AllocationGranularity) - 1)
                             : imageView ? object->getImageBase() : 0;
    const auto backing = object->getBackingReference();
    const size_t firstPage = static_cast<size_t>(sectionOffset) >> mm::PageShift4KB;
    if (!backing.pfns || firstPage >= backing.pageCount ||
        (length >> mm::PageShift4KB) > backing.pageCount - firstPage) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidImageFormat;
    }
    const auto* imagePageProtections = imageView ? object->getImageViewProtections() : nullptr;
    mm::ProcessAddressSpace* targetSpace{};
    ob::ObjectHeader* targetProcessReference{};
    ps::EProcess* targetProcessForMapping{};
    if (imageView && !imagePageProtections) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidImageFormat;
    }
    if (process == InvalidHandleValue) {
        targetSpace = &currentAddressSpace();
    } else {
        status = ps::detail::referenceProcessHandle(process, 0x0008u,
            targetProcessForMapping, targetProcessReference);
        if (status != NtStatus::Success) {
            ob::ObDereferenceObject(header);
            return status;
        }
        if (!targetProcessForMapping || targetProcessForMapping->isTerminating()) {
            if (targetProcessReference) ob::ObDereferenceObject(targetProcessReference);
            ob::ObDereferenceObject(header);
            return NtStatus::ProcessIsTerminating;
        }
        targetSpace = &targetProcessForMapping->getAddressSpace();
    }
    // SEC_IMAGE ignores Win32Protect for actual page permissions. Keep the
    // PE subsection protections for every page, including when the caller
    // requested PAGE_READONLY or PAGE_READWRITE.
    const uint32_t maximumProtection = imageView
        ? object->getMaximumImageProtection() : protection;
    const uint32_t* pageProtections = imageView ? imagePageProtections + firstPage : nullptr;
    const uint32_t initialProtection = imageView && copyProtection
        ? (executableProtection ? mm::PAGE_EXECUTE_READ : mm::PAGE_READONLY) : protection;
    // SEC_IMAGE may be mapped at a caller-specified alternate base. When no
    // base is specified, fall back to an available range if the PE preferred
    // range is occupied or outside this address-space backend's window.
    const bool preferredInWindow = target >= hal::amd64::BootstrapUserAddressBase &&
        target - hal::amd64::BootstrapUserAddressBase < hal::amd64::BootstrapUserAddressSize &&
        length <= hal::amd64::BootstrapUserAddressSize - (target - hal::amd64::BootstrapUserAddressBase);
    bool imageNotAtBase = false;
    bool attachedToTarget = false;
    if (targetProcessForMapping && ke::KeGetCurrentProcess() != targetProcessForMapping) {
        ke::KAPC_STATE attachState{};
        ke::KeStackAttachProcess(targetProcessForMapping, &attachState);
        attachedToTarget = ke::KeGetCurrentProcess() == targetProcessForMapping;
        if (!attachedToTarget) status = NtStatus::Unsuccessful;
        if (attachedToTarget) {
            status = withInterruptsDisabled([&]() noexcept {
                auto& space = *targetSpace;
                auto result = imageView && !capturedBase && !preferredInWindow ? NtStatus::Conflict :
                    space.mapSharedPages(target, length, initialProtection, backing.pfns + firstPage,
                        backing.pageCount - firstPage, header, acquireSectionView, releaseSectionView,
                        maximumProtection, imageView, inherit == 1, sectionOffset, pageProtections, false, nullptr,
                        imageView ? object->getSharedImagePages() + firstPage : nullptr);
                if (imageView && !capturedBase && result == NtStatus::Conflict) {
                    target = 0;
                    result = space.mapSharedPages(target, length, initialProtection, backing.pfns + firstPage,
                        backing.pageCount - firstPage, header, acquireSectionView, releaseSectionView,
                        maximumProtection, true, inherit == 1, sectionOffset, pageProtections, false, nullptr,
                        object->getSharedImagePages() + firstPage);
                }
                return result;
            });
            ke::KeUnstackDetachProcess(&attachState);
            attachedToTarget = false;
        }
    } else status = withInterruptsDisabled([&]() noexcept {
        auto& space = *targetSpace;
        auto result = imageView && !capturedBase && !preferredInWindow ? NtStatus::Conflict :
            space.mapSharedPages(target, length, initialProtection, backing.pfns + firstPage,
                backing.pageCount - firstPage, header, acquireSectionView, releaseSectionView,
                maximumProtection, imageView, inherit == 1, sectionOffset, pageProtections, false, nullptr,
                imageView ? object->getSharedImagePages() + firstPage : nullptr);
        if (imageView && !capturedBase && result == NtStatus::Conflict) {
            target = 0;
            result = space.mapSharedPages(target, length, initialProtection, backing.pfns + firstPage,
                backing.pageCount - firstPage, header, acquireSectionView, releaseSectionView,
                maximumProtection, true, inherit == 1, sectionOffset, pageProtections, false, nullptr,
                object->getSharedImagePages() + firstPage);
        }
        if (result != NtStatus::Success) return result;
        imageNotAtBase = imageView && target != object->getImageBase();
        return result;
    });
    if (status == NtStatus::Success) {
        imageNotAtBase = imageView && target != object->getImageBase();
        LargeInteger returnedOffset{};
        returnedOffset.quadPart = static_cast<int64_t>(sectionOffset);
        status = copyOut(base, &target, sizeof(target), alignof(uintptr_t));
        if (status == NtStatus::Success)
            status = copyOut(viewSize, &length, sizeof(length), alignof(size_t));
        if (status == NtStatus::Success && offset)
            status = copyOut(offset, &returnedOffset, sizeof(returnedOffset), alignof(LargeInteger));
        if (status != NtStatus::Success) {
            ke::KAPC_STATE rollbackAttachState{};
            bool rollbackAttached = false;
            if (targetProcessForMapping && ke::KeGetCurrentProcess() != targetProcessForMapping) {
                ke::KeStackAttachProcess(targetProcessForMapping, &rollbackAttachState);
                rollbackAttached = ke::KeGetCurrentProcess() == targetProcessForMapping;
            }
            if (!targetProcessForMapping || rollbackAttached ||
                ke::KeGetCurrentProcess() == targetProcessForMapping)
                (void)targetSpace->unmapSharedPages(target, true);
            if (rollbackAttached) ke::KeUnstackDetachProcess(&rollbackAttachState);
        }
    }
    if (targetProcessReference) ob::ObDereferenceObject(targetProcessReference);
    ob::ObDereferenceObject(header);
    return status == NtStatus::Success && imageNotAtBase ? NtStatus::ImageNotAtBase : status;
}

NtStatus NtMapViewOfSection(Handle handle, Handle process, uintptr_t* base, uintptr_t zeroBits,
                            size_t commitSize, LargeInteger* offset, size_t* viewSize,
                            uint32_t inherit, uint32_t allocation, uint32_t protection) {
    const auto* current = ke::KeGetCurrentThread();
    const mm::KPROCESSOR_MODE callerMode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    return mapViewOfSection(handle, process, base, zeroBits, commitSize, offset, viewSize,
                            inherit, allocation, protection, callerMode);
}

NtStatus NtMapViewOfSectionFromKernel(Handle handle, Handle process, uintptr_t* base,
                                      uintptr_t zeroBits, size_t commitSize, LargeInteger* offset,
                                      size_t* viewSize, uint32_t inherit, uint32_t allocation,
                                      uint32_t protection) {
    return mapViewOfSection(handle, process, base, zeroBits, commitSize, offset, viewSize,
                            inherit, allocation, protection, mm::KernelMode);
}

NtStatus NtUnmapViewOfSection(Handle process, uintptr_t base) {
    mm::ProcessAddressSpace* targetSpace{};
    ob::ObjectHeader* processReference{};
    if (process == InvalidHandleValue) {
        targetSpace = &currentAddressSpace();
    } else {
        ps::EProcess* target{};
        const auto referenceStatus = ps::detail::referenceProcessHandle(
            process, 0x0008u, target, processReference);
        if (referenceStatus != NtStatus::Success) return referenceStatus;
        if (!target || target->isTerminating()) {
            if (processReference) ob::ObDereferenceObject(processReference);
            return NtStatus::ProcessIsTerminating;
        }
        targetSpace = &target->getAddressSpace();
    }
    const auto result = targetSpace->unmapSharedPages(base);
    if (processReference) ob::ObDereferenceObject(processReference);
    return result == NtStatus::InvalidParameter ? NtStatus::NotMappedView : result;
}

NtStatus rollbackMappedViewFromKernel(Handle process, uintptr_t base) noexcept {
    if (process == InvalidHandleValue)
        return currentAddressSpace().unmapSharedPages(base, true);
    return NtUnmapViewOfSection(process, base);
}

bool verifyBootstrapSectionOwnership() noexcept {
    const size_t activeBefore = mm::PfnDatabase::get().getActiveCount();
    const auto objectsBefore = sectionType.totalNumberOfObjects.load(std::memory_order_relaxed);
    LargeInteger maximum{};
    maximum.quadPart = mm::PageSize4KB;
    Handle handle{};
    if (NtCreateSectionFromKernel(&handle, section::SECTION_MAP_READ, nullptr, &maximum,
                        mm::PAGE_READONLY, section::SEC_COMMIT, 0) != NtStatus::Success) return false;
    uintptr_t base{};
    size_t size = mm::PageSize4KB;
    const auto mapStatus = NtMapViewOfSectionFromKernel(handle, InvalidHandleValue, &base, 0, 0,
                                               nullptr, &size, 2, 0, mm::PAGE_READONLY);
    if (mapStatus != NtStatus::Success) { NtClose(handle); return false; }
    if (NtClose(handle) != NtStatus::Success ||
        sectionType.totalNumberOfObjects.load(std::memory_order_relaxed) != objectsBefore + 1) return false;
    if (NtUnmapViewOfSection(InvalidHandleValue, base) != NtStatus::Success) return false;
    return sectionType.totalNumberOfObjects.load(std::memory_order_relaxed) == objectsBefore &&
           mm::PfnDatabase::get().getActiveCount() == activeBefore;
}
}
#else
#include "micant/km/core/ntos_internal.hpp"
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/section.hpp"
#include "micant/km/drivers/fs/fs.hpp"
#include <unordered_map>
#include <memory>
#include <limits>
#include <mutex>

namespace micant::sys {

static mm::ProcessAddressSpace g_KernelAddressSpace;
static std::mutex g_AddressSpaceServiceMutex;
static std::unordered_map<Handle, std::shared_ptr<section::SectionObject>> g_KernelSections;
static Handle g_NextSectionHandle = 0x300;

namespace {
bool pageRange(uintptr_t address, size_t size, uintptr_t& start, size_t& length) {
    if (!size) return false;
    const size_t offset = address & mm::PageMask4KB;
    if (size > std::numeric_limits<size_t>::max() - offset ||
        size + offset > std::numeric_limits<size_t>::max() - mm::PageMask4KB)
        return false;
    start = address & ~static_cast<uintptr_t>(mm::PageMask4KB);
    length = (size + offset + mm::PageMask4KB) & ~static_cast<size_t>(mm::PageMask4KB);
    return length && start <= std::numeric_limits<uintptr_t>::max() - (length - 1);
}
NtStatus validateProcessHandle(Handle processHandle) {
    if (processHandle == InvalidHandleValue) return NtStatus::Success;
    return processHandle == 0 ? NtStatus::InvalidHandle : NtStatus::NotImplemented;
}
}

namespace internal {

bool MmCloseSectionHandle(Handle h) {
    return g_KernelSections.erase(h) > 0;
}

} // namespace internal

NtStatus NtAllocateVirtualMemory(
    Handle processHandle,
    uintptr_t* baseAddress,
    uintptr_t zeroBits,
    size_t* regionSize,
    uint32_t allocationType,
    uint32_t protect
) {
    if (!baseAddress || !regionSize || *regionSize == 0) {
        return NtStatus::InvalidParameter;
    }
    if (const auto status = validateProcessHandle(processHandle); !NT_SUCCESS(status)) return status;
    if (zeroBits != 0) return NtStatus::NotImplemented;
    if (protect != mm::PAGE_READONLY && protect != mm::PAGE_READWRITE)
        return NtStatus::NotImplemented;

    uintptr_t addr;
    size_t size;
    if (!pageRange(*baseAddress, *regionSize, addr, size)) return NtStatus::InvalidParameter;
    std::lock_guard lock(g_AddressSpaceServiceMutex);
    NtStatus status = g_KernelAddressSpace.allocate(addr, size, allocationType, protect);
    if (NT_SUCCESS(status)) {
        *baseAddress = addr;
        *regionSize = size;
    }
    return status;
}

NtStatus NtFreeVirtualMemory(
    Handle processHandle,
    uintptr_t* baseAddress,
    size_t* regionSize,
    uint32_t freeType
) {
    if (!baseAddress || !regionSize || *baseAddress == 0) {
        return NtStatus::InvalidParameter;
    }
    if (const auto status = validateProcessHandle(processHandle); !NT_SUCCESS(status)) return status;
    if (freeType != mm::MEM_RELEASE && freeType != mm::MEM_DECOMMIT)
        return NtStatus::NotImplemented;
    std::lock_guard lock(g_AddressSpaceServiceMutex);
    const auto* vad = g_KernelAddressSpace.findVad(*baseAddress);
    if (!vad) return NtStatus::InvalidParameter;
    uintptr_t addr = *baseAddress;
    size_t size = *regionSize;
    if (freeType == mm::MEM_RELEASE || size == 0) {
        if (addr != vad->startingAddress || (freeType == mm::MEM_RELEASE && size != 0))
            return NtStatus::InvalidParameter;
        size = vad->endingAddress - vad->startingAddress + 1;
    } else if (!pageRange(addr, size, addr, size)) {
        return NtStatus::InvalidParameter;
    }
    // The hosted fallback currently decommits an entire VAD, never a subrange.
    if (freeType == mm::MEM_DECOMMIT &&
        (addr != vad->startingAddress || size != vad->endingAddress - vad->startingAddress + 1))
        return NtStatus::NotImplemented;
    const NtStatus status = g_KernelAddressSpace.free(addr, freeType == mm::MEM_RELEASE ? 0 : size, freeType);
    if (NT_SUCCESS(status)) {
        *baseAddress = addr;
        *regionSize = size;
    }
    return status;
}

NtStatus NtProtectVirtualMemory(
    Handle processHandle,
    uintptr_t* baseAddress,
    size_t* regionSize,
    uint32_t newProtect,
    uint32_t* oldProtect
) {
    if (!baseAddress || !regionSize || !oldProtect) {
        return NtStatus::InvalidParameter;
    }
    if (const auto status = validateProcessHandle(processHandle); !NT_SUCCESS(status)) return status;
    uintptr_t addr;
    size_t size;
    if (!pageRange(*baseAddress, *regionSize, addr, size)) return NtStatus::InvalidParameter;
    std::lock_guard lock(g_AddressSpaceServiceMutex);
    const auto* vad = g_KernelAddressSpace.findVad(addr);
    if (!vad) return NtStatus::InvalidParameter;
    // The hosted fallback only supports changes to an entire committed VAD.
    const bool imageExecuteProtection = vad->isImageView &&
        newProtect == mm::PAGE_EXECUTE_READ;
    if (addr != vad->startingAddress || size != vad->endingAddress - vad->startingAddress + 1 ||
        !vad->committed || (newProtect != mm::PAGE_READONLY && newProtect != mm::PAGE_READWRITE &&
                            !imageExecuteProtection))
        return NtStatus::NotImplemented;
    uint32_t previous;
    const NtStatus status = g_KernelAddressSpace.protect(addr, size, newProtect, &previous);
    if (NT_SUCCESS(status)) {
        *baseAddress = addr;
        *regionSize = size;
        *oldProtect = previous;
    }
    return status;
}

NtStatus NtCreateSection(
    Handle* sectionHandle,
    uint32_t /*desiredAccess*/,
    ObjectAttributes* /*objectAttributes*/,
    LargeInteger* maximumSize,
    uint32_t sectionPageProtection,
    uint32_t allocationAttributes,
    Handle fileHandle
) {
    if (!sectionHandle) return NtStatus::InvalidParameter;
    size_t size = maximumSize ? static_cast<size_t>(maximumSize->quadPart) : 4096;

    fs::FileObject* fileObj = nullptr;
    if (fileHandle != 0) {
        fileObj = LookupKernelFileObject(fileHandle);
        if (fileObj) {
            if (size == 0 || (maximumSize && maximumSize->quadPart == 0)) {
                size = fileObj->getFileSize();
            }
        }
    }
    if (size == 0) size = 4096;

    auto sec = std::make_shared<section::SectionObject>(size, allocationAttributes, sectionPageProtection);
    if (!NT_SUCCESS(sec->getInitializationStatus())) return sec->getInitializationStatus();
    if (fileObj && fileObj->getFileSize() > 0) {
        const NtStatus status = sec->loadData(fileObj->getData().data(), fileObj->getFileSize());
        if (!NT_SUCCESS(status)) return status;
    }

    Handle h = g_NextSectionHandle++;
    g_KernelSections[h] = sec;
    *sectionHandle = h;
    return NtStatus::Success;
}

NtStatus NtMapViewOfSection(
    Handle sectionHandle,
    Handle /*processHandle*/,
    uintptr_t* baseAddress,
    uintptr_t /*zeroBits*/,
    size_t /*commitSize*/,
    LargeInteger* /*sectionOffset*/,
    size_t* viewSize,
    uint32_t /*inheritDisposition*/,
    uint32_t /*allocationType*/,
    uint32_t /*win32Protect*/
) {
    if (!baseAddress || !viewSize) return NtStatus::InvalidParameter;
    auto it = g_KernelSections.find(sectionHandle);
    if (it == g_KernelSections.end() || !it->second) return NtStatus::InvalidHandle;
    return NtStatus::NotImplemented;
}

NtStatus NtUnmapViewOfSection(Handle /*processHandle*/, uintptr_t baseAddress) {
    std::lock_guard lock(g_AddressSpaceServiceMutex);
    return g_KernelAddressSpace.free(baseAddress, 0, mm::MEM_RELEASE);
}

} // namespace micant::sys
#endif
