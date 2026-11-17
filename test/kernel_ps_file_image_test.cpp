// End-to-end verification of the file-backed SEC_IMAGE path: the image is
// opened through the mounted FAT32 volume, turned into a SEC_FILE|SEC_IMAGE
// section by the canonical Mm path, and used to initialize a real process.
#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/section.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"

namespace micant::sys {
NtStatus referenceProcessImageSection(Handle handle, ob::ObjectHeader*& reference) noexcept;
}

namespace {

void reportFailure(const char* stage, micant::NtStatus status) noexcept {
    auto write = [](char value) noexcept {
        uint8_t ready{};
        do { __asm__ __volatile__("inb %1, %0" : "=a"(ready) : "Nd"(uint16_t{0x3fd})); }
        while (!(ready & 0x20));
        __asm__ __volatile__("outb %0, %1" : : "a"(static_cast<uint8_t>(value)), "Nd"(uint16_t{0x3f8}));
    };
    for (; *stage; ++stage) write(*stage);
    write('=');
    static constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) write(digits[(static_cast<uint32_t>(status) >> shift) & 0xf]);
    write('\r'); write('\n');
}

struct ScopedHandle {
    micant::Handle value{};
    micant::ob::HandleTable* ownerTable{};
    ~ScopedHandle() {
        if (!value) return;
        const auto status = ownerTable ? ownerTable->closeHandle(value)
                                       : micant::sys::NtClose(value);
        if (status != micant::NtStatus::Success) __builtin_trap();
    }
};

struct ScopedReference {
    micant::ob::ObjectHeader* value{};
    ~ScopedReference() {
        if (value) micant::ob::ObDereferenceObject(value);
    }
};

// Build the system DLL path the same way the Ps loader does, so the test
// exercises the real mounted-volume lookup instead of a hardcoded path.
micant::NtStatus buildSystemPath(const char* leaf, char16_t* output, size_t capacity,
                                 size_t& length) noexcept {
    using namespace micant;
    length = 0;
    if (!leaf || !output || !capacity) return NtStatus::InvalidParameter;
    char16_t directory[256]{};
    size_t directoryLength{};
    const auto status = io::IoQuerySystemDirectory(directory,
        sizeof(directory) / sizeof(directory[0]), directoryLength);
    if (status != NtStatus::Success) return status;
    if (!directoryLength || directoryLength >= sizeof(directory) / sizeof(directory[0]))
        return NtStatus::InvalidDeviceState;
    size_t written{};
    for (size_t index = 0; index < directoryLength; ++index) {
        if (!directory[index]) break;
        if (written + 1 >= capacity) return NtStatus::BufferTooSmall;
        output[written++] = directory[index];
    }
    if (!written || output[written - 1] != u'\\') {
        if (written + 1 >= capacity) return NtStatus::BufferTooSmall;
        output[written++] = u'\\';
    }
    for (size_t index = 0; leaf[index]; ++index) {
        if (written + 1 >= capacity) return NtStatus::BufferTooSmall;
        output[written++] = static_cast<char16_t>(static_cast<uint8_t>(leaf[index]));
    }
    output[written] = 0;
    length = written;
    return NtStatus::Success;
}

// Process and section deletion is deferred to the existing passive reaper
// once the scheduler clock runs. Let that owner finish before taking or
// comparing the PFN baseline.
bool drainDeferredCleanup(size_t expectedActivePages) noexcept {
    using namespace micant;
    auto* current = ke::KeGetCurrentThread();
    if (!current || !hal::amd64::bootstrapSchedulerClockActive()) return false;
    uint64_t previousRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(previousRoot));
    if (!hal::amd64::switchProcessPagingRootByPhysical(
            hal::amd64::bootstrapKernelPagingRoot())) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const auto priority = current->priority;
    current->priority = ke::PRIORITY_IDLE;
    if (ke::g_CurrentCpu.serviceDeferredCleanup)
        ke::g_CurrentCpu.serviceDeferredCleanup();
    const uint64_t start = hal::amd64::bootstrapSchedulerClockTicks();
    __asm__ __volatile__("sti" : : : "memory");
    bool drained = false;
    for (size_t spin = 0; spin < 50000000 &&
         hal::amd64::bootstrapSchedulerClockTicks() - start < 100; ++spin) {
        if (!io::IoDeferredFileDeletesPending() && !ps::detail::deferredCleanupPending() &&
            mm::PfnDatabase::get().getActiveCount() == expectedActivePages) {
            drained = true;
            break;
        }
        __asm__ __volatile__("pause");
    }
    __asm__ __volatile__("cli" : : : "memory");
    current->priority = priority;
    const bool restored = hal::amd64::switchProcessPagingRootByPhysical(previousRoot);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return drained && restored;
}

} // namespace

// Verifies that a real PE file on the mounted FAT32 volume becomes a
// validated SEC_IMAGE section and initializes a process image.
extern "C" bool MicantTestPsFileBackedImage() noexcept {
    using namespace micant;
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) {
        reportFailure("[MicaNT Ps] SEC_IMAGE test IRQL", NtStatus::InvalidDeviceState);
        return false;
    }

    if (!drainDeferredCleanup(mm::PfnDatabase::get().getActiveCount())) {
        reportFailure("[MicaNT Ps] SEC_IMAGE initial cleanup", NtStatus::Unsuccessful);
        return false;
    }
    const size_t activeBefore = mm::PfnDatabase::get().getActiveCount();
    char16_t imagePath[256]{};
    size_t imagePathLength{};
    const auto pathStatus = buildSystemPath("fileimage.exe", imagePath,
        sizeof(imagePath) / sizeof(imagePath[0]), imagePathLength);
    if (pathStatus != NtStatus::Success) {
        reportFailure("[MicaNT Ps] SEC_IMAGE system path", pathStatus);
        return false;
    }

    {
        // The file must be reachable through the canonical Io backend, which
        // is what proves the volume is mounted and the stream identity
        // resolves.
        ScopedHandle fileHandle;
        const auto openStatus = io::IoOpenBackendFile(&fileHandle.value, 0x0021,
            {imagePath, imagePathLength});
        if (openStatus != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE file open", openStatus);
            return false;
        }
        if (io::IoFlushBuffersFile(fileHandle.value) != NtStatus::AccessDenied) {
            reportFailure("[MicaNT Io] read-only file flush access check", NtStatus::AccessDenied);
            return false;
        }
        ScopedHandle flushHandle;
        const auto flushOpenStatus = io::IoOpenBackendFile(&flushHandle.value, 0x0023,
            {imagePath, imagePathLength});
        if (flushOpenStatus != NtStatus::Success) {
            reportFailure("[MicaNT Io] durable file open", flushOpenStatus);
            return false;
        }
        const auto flushStatus = io::IoFlushBuffersFile(flushHandle.value);
        if (flushStatus != NtStatus::Success) {
            reportFailure("[MicaNT Io] FAT/AHCI file flush", flushStatus);
            return false;
        }
        reportFailure("[MicaNT Io] FAT/AHCI file flush", NtStatus::Success);

        ScopedHandle sectionHandle;
        auto status = sys::NtCreateSectionFromKernel(&sectionHandle.value,
            section::SECTION_ALL_ACCESS, nullptr, nullptr, mm::PAGE_EXECUTE_READ,
            section::SEC_FILE | section::SEC_IMAGE, fileHandle.value);
        if (status != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE section create", status);
            return false;
        }

        ScopedReference sectionReference;
        status = sys::referenceProcessImageSection(sectionHandle.value, sectionReference.value);
        if (status != NtStatus::Success || !sectionReference.value) {
            reportFailure("[MicaNT Ps] SEC_IMAGE section reference", status);
            return false;
        }
        auto* image = sectionReference.value->as<section::SectionObject>();
        if (!image || !image->getFileSize() || !image->getSize() || !image->getImageBase() ||
            image->getFileName().empty()) {
            reportFailure("[MicaNT Ps] SEC_IMAGE section metadata", NtStatus::InvalidImageFormat);
            return false;
        }
        // The section must have been built from the file bytes, not from a
        // synthetic in-memory PE: the recorded name is the opened path.
        if (image->getFileName().size() != imagePathLength) {
            reportFailure("[MicaNT Ps] SEC_IMAGE source path", NtStatus::InvalidImageFormat);
            return false;
        }
        for (size_t index = 0; index < imagePathLength; ++index) {
            if (image->getFileName()[index] != imagePath[index]) {
                reportFailure("[MicaNT Ps] SEC_IMAGE source path mismatch", NtStatus::InvalidImageFormat);
                return false;
            }
        }

        ps::EProcess process;
        status = process.initializeAddressSpace();
        if (status != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE address-space creation", status);
            return false;
        }
        uintptr_t previous{};
        __asm__ __volatile__("movq %%cr3, %0" : "=r"(previous));
        if (!hal::amd64::switchProcessPagingRootByPhysical(process.getDirectoryTableBase())) {
            reportFailure("[MicaNT Ps] SEC_IMAGE address-space attach", NtStatus::Unsuccessful);
            return false;
        }
        const auto imageStatus = process.initializeMainImageFromSection(*image,
                                                                        sectionReference.value);
        const bool restored = hal::amd64::switchProcessPagingRootByPhysical(previous);
        if (!restored) {
            reportFailure("[MicaNT Ps] SEC_IMAGE address-space restore", NtStatus::Unsuccessful);
            return false;
        }
        if (imageStatus != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE main image initialization", imageStatus);
            (void)process.closeAddressSpace();
            return false;
        }
        // The loader must have produced a finalized native image set whose
        // main module is the file we opened.
        if (!process.nativeImagesFinalized() || !process.nativeImages() ||
            !process.nativeImages()->size() || !process.userPebAddress()) {
            reportFailure("[MicaNT Ps] SEC_IMAGE loader state", NtStatus::InvalidImageFormat);
            (void)process.closeAddressSpace();
            return false;
        }
        const auto* module = process.nativeImages()->module(0);
        if (!module || !module->base || module->name != "fileimage.exe") {
            reportFailure("[MicaNT Ps] SEC_IMAGE main module", NtStatus::InvalidImageFormat);
            (void)process.closeAddressSpace();
            return false;
        }
        status = process.closeAddressSpace();
        if (status != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE address-space teardown", status);
            return false;
        }

        // Exercise the same M4 ownership chain used by a native executable:
        // create an Ob-owned process from this section, start its mapped PE
        // entry point as a real CPL3 thread, and wait for both lifetimes.
        ScopedHandle parentHandle;
        ps::EProcess* parentProcess{};
        status = ps::detail::createProcessHandle(ob::SYNCHRONIZE | 0x0080,
            parentHandle.value, parentProcess);
        if (status != NtStatus::Success || !parentHandle.value || !parentProcess) {
            reportFailure("[MicaNT Ps] SEC_IMAGE parent creation", status);
            return false;
        }
        ScopedHandle processHandle;
        status = sys::NtCreateProcessExFromKernel(&processHandle.value,
            ob::SYNCHRONIZE | 0x0001 | 0x0002 | 0x0008 | 0x0040,
            nullptr, parentHandle.value, 0, sectionHandle.value, 0, 0, false);
        if (status != NtStatus::Success || !processHandle.value) {
            reportFailure("[MicaNT Ps] SEC_IMAGE process creation", status);
            return false;
        }

        ScopedReference processObjectReference;
        ps::EProcess* createdProcess{};
        status = ps::detail::referenceProcessHandle(processHandle.value, 0,
            createdProcess, processObjectReference.value);
        if (status != NtStatus::Success || !createdProcess ||
            !createdProcess->nativeImagesFinalized() || !createdProcess->nativeImages()) {
            reportFailure("[MicaNT Ps] SEC_IMAGE process reference/images", status);
            return false;
        }
        const auto* mainModule = createdProcess->nativeImages()->module(0);
        if (!mainModule || !mainModule->base ||
            mainModule->view.headers().optionalHeader.addressOfEntryPoint >= mainModule->image.size() ||
            mainModule->base > UINTPTR_MAX - mainModule->view.headers().optionalHeader.addressOfEntryPoint) {
            reportFailure("[MicaNT Ps] SEC_IMAGE process entry point", NtStatus::InvalidImageFormat);
            return false;
        }
        const uintptr_t entry = mainModule->base +
            mainModule->view.headers().optionalHeader.addressOfEntryPoint;

        ScopedHandle threadHandle;
        threadHandle.ownerTable = &createdProcess->getHandleTable();
        ps::EThread* createdThread{};
        auto* callerThread = ke::KeGetCurrentThread();
        if (!callerThread || !callerThread->directoryTableBase) {
            reportFailure("[MicaNT Ps] SEC_IMAGE caller context", NtStatus::InvalidDeviceState);
            return false;
        }
        const auto previousAttachDepth = callerThread->processAttachDepth;
        const auto previousRoot = callerThread->directoryTableBase;
        ke::KAPC_STATE attachState{};
        ke::KeStackAttachProcess(createdProcess, &attachState);
        const bool attachAcquired = callerThread->processAttachDepth == previousAttachDepth + 1;
        const bool attached = attachAcquired && callerThread->attachedProcess == createdProcess &&
            callerThread->directoryTableBase == createdProcess->getDirectoryTableBase() &&
            callerThread->processAttachDepth == previousAttachDepth + 1;
        if (attached) {
            status = ps::detail::createUserThreadHandle(*createdProcess, entry,
                32768, 8 * mm::PageSize4KB, true,
                ob::SYNCHRONIZE | 0x0001 | 0x0040,
                threadHandle.value, createdThread);
            if (status == NtStatus::Success && (!threadHandle.value || !createdThread))
                status = NtStatus::Unsuccessful;
            if (status == NtStatus::Success) status = createdThread->ready();
        } else {
            status = NtStatus::Unsuccessful;
        }
        if (attachAcquired) ke::KeUnstackDetachProcess(&attachState);
        if (callerThread->processAttachDepth != previousAttachDepth ||
            callerThread->directoryTableBase != previousRoot || callerThread->attachedProcess) {
            reportFailure("[MicaNT Ps] SEC_IMAGE attach restoration", NtStatus::Unsuccessful);
            return false;
        }
        if (status != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE initial thread create/ready", status);
            return false;
        }
        const auto waitAttachDepth = callerThread->processAttachDepth;
        ke::KAPC_STATE waitAttachState{};
        ke::KeStackAttachProcess(createdProcess, &waitAttachState);
        const bool waitAttachAcquired = callerThread->processAttachDepth == waitAttachDepth + 1;
        const bool waitAttached = waitAttachAcquired && callerThread->attachedProcess == createdProcess &&
            callerThread->directoryTableBase == createdProcess->getDirectoryTableBase() &&
            callerThread->processAttachDepth == waitAttachDepth + 1;
        if (waitAttached) status = sys::NtWaitForSingleObject(threadHandle.value, false, nullptr);
        else status = NtStatus::Unsuccessful;
        const auto threadExitStatus = waitAttached ? createdThread->exitStatus() : status;
        if (waitAttached && status == NtStatus::Success) {
            const auto closeStatus = sys::NtClose(threadHandle.value);
            if (closeStatus != NtStatus::Success) status = closeStatus;
            else threadHandle.value = 0;
        }
        if (waitAttachAcquired) ke::KeUnstackDetachProcess(&waitAttachState);
        if (callerThread->processAttachDepth != waitAttachDepth ||
            callerThread->directoryTableBase != previousRoot || callerThread->attachedProcess) {
            reportFailure("[MicaNT Ps] SEC_IMAGE wait detach", NtStatus::Unsuccessful);
            return false;
        }
        if (status != NtStatus::Success || threadExitStatus != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE initial thread exit/wait",
                status != NtStatus::Success ? status : threadExitStatus);
            return false;
        }
        status = sys::NtWaitForSingleObject(processHandle.value, false, nullptr);
        if (status != NtStatus::Success || createdProcess->exitStatus() != NtStatus::Success ||
            createdProcess->activeThreadCount() != 0) {
            reportFailure("[MicaNT Ps] SEC_IMAGE process exit/wait",
                status != NtStatus::Success ? status : createdProcess->exitStatus());
            return false;
        }
        status = sys::NtClose(processHandle.value);
        if (status != NtStatus::Success) {
            reportFailure("[MicaNT Ps] SEC_IMAGE process handle close", status);
            return false;
        }
        processHandle.value = 0;
    }

    if (!drainDeferredCleanup(activeBefore)) {
        reportFailure("[MicaNT Ps] SEC_IMAGE final cleanup", NtStatus::Unsuccessful);
        return false;
    }
    // Every PFN the image path acquired must be released again.
    const size_t activeAfter = mm::PfnDatabase::get().getActiveCount();
    if (activeAfter != activeBefore) {
        reportFailure("[MicaNT Mm] File-backed image PFN accounting delta",
            static_cast<NtStatus>(activeAfter > activeBefore
                ? activeAfter - activeBefore : activeBefore - activeAfter));
        return false;
    }
    return true;
}
