#include "micant/km/core/ntos_internal.hpp"
#include "micant/km/executive/ps.hpp"
#if !defined(MICANT_FREESTANDING)
#include "micant/km/executive/po.hpp"
#endif
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/perf_taskmgr.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/shared/system_information.h"
#include <cstring>
#include <new>

#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST)
extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
#define TASKMGR_QUERY_TRACE(text) MicantNetbootSerialWrite(text)
#else
#define TASKMGR_QUERY_TRACE(text) ((void)0)
#endif

namespace micant::sys {

namespace {
class ScopedQueryInterruptDisable {
public:
    ScopedQueryInterruptDisable() noexcept {
#if defined(MICANT_BAREMETAL) && defined(__x86_64__)
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags_) : : "memory");
#endif
    }
    ~ScopedQueryInterruptDisable() {
#if defined(MICANT_BAREMETAL) && defined(__x86_64__)
        if (flags_ & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
    }
    ScopedQueryInterruptDisable(const ScopedQueryInterruptDisable&) = delete;
    ScopedQueryInterruptDisable& operator=(const ScopedQueryInterruptDisable&) = delete;

private:
    uint64_t flags_{};
};
} // namespace

#if !defined(MICANT_FREESTANDING)
NtStatus NtTerminateProcess(Handle /*processHandle*/, NtStatus /*exitStatus*/) {
    return NtStatus::NotImplemented;
}

NtStatus NtShutdownSystem(uint32_t action) {
    auto shutdownAction = static_cast<po::ShutdownAction>(action);
    return po::PowerManager::get().shutdownSystem(shutdownAction);
}
#endif

NtStatus NtQuerySystemInformation(
    uint32_t systemInformationClass,
    void* systemInformation,
    uint32_t systemInformationLength,
    uint32_t* returnLength
) {
    constexpr uint32_t systemProcessInformationClass = 5;
    constexpr uint32_t systemPerformanceInformationClass = 2;
    constexpr uint32_t systemProcessorPerformanceInformationClass = 8;
    if (systemInformationClass != systemProcessInformationClass &&
        systemInformationClass != systemPerformanceInformationClass &&
        systemInformationClass != systemProcessorPerformanceInformationClass)
        return NtStatus::InvalidInfoClass;

    TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] NtQuerySystemInformation class accepted\r\n");
    ScopedQueryInterruptDisable interruptGuard;
    auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
    NtStatus status = NtStatus::Success;

    if (systemInformationClass == systemProcessorPerformanceInformationClass) {
        const auto snapshot = km::perf::QueryProcessorPerformanceSnapshot();
        if ((snapshot.availability & km::perf::ProcessorPerformanceAvailableTimes) == 0 ||
            snapshot.processorCount != 1)
            return NtStatus::NotSupported;
        constexpr uint32_t recordSize = sizeof(SystemProcessorPerformanceInformation);
        const uint32_t required = recordSize;
        if (returnLength) {
            status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
            if (status != NtStatus::Success) return status;
        }
        if (systemInformationLength < required) return NtStatus::InfoLengthMismatch;
        if (!systemInformation) return NtStatus::AccessViolation;
        status = ex::ProbeForWrite(systemInformation, required,
            alignof(SystemProcessorPerformanceInformation), mode);
        if (status != NtStatus::Success) return status;
        if (snapshot.idleTime100ns > static_cast<uint64_t>(INT64_MAX) ||
            snapshot.kernelTime100ns > static_cast<uint64_t>(INT64_MAX) ||
            snapshot.userTime100ns > static_cast<uint64_t>(INT64_MAX))
            return NtStatus::NotSupported;
        SystemProcessorPerformanceInformation information{};
        information.IdleTime = static_cast<int64_t>(snapshot.idleTime100ns);
        information.KernelTime = static_cast<int64_t>(snapshot.kernelTime100ns);
        information.UserTime = static_cast<int64_t>(snapshot.userTime100ns);
        return ex::ExSafeCopyToUser(systemInformation, &information, required, mode);
    }

    if (systemInformationClass == systemPerformanceInformationClass) {
        constexpr uint32_t required = sizeof(SystemPerformanceInformation);
        if (returnLength) {
            status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
            if (status != NtStatus::Success) return status;
        }
        if (systemInformationLength < required) return NtStatus::InfoLengthMismatch;
        if (!systemInformation) return NtStatus::AccessViolation;
        status = ex::ProbeForWrite(systemInformation, required,
                                   alignof(SystemPerformanceInformation), mode);
        if (status != NtStatus::Success) return status;

        const auto snapshot = km::perf::QuerySystemMemorySnapshot();
        constexpr uint32_t requiredAvailability =
            km::perf::MemorySnapshotAvailableZeroed |
            km::perf::MemorySnapshotAvailableFree |
            km::perf::MemorySnapshotAvailableStandby;
        if ((snapshot.availability & requiredAvailability) != requiredAvailability)
            return NtStatus::NotSupported;
        const uint64_t zeroedPages = snapshot.zeroedBytes / mm::PageSize4KB;
        const uint64_t freePages = snapshot.freeBytes / mm::PageSize4KB;
        const uint64_t standbyPages = snapshot.standbyBytes / mm::PageSize4KB;
        if (zeroedPages > UINT32_MAX || freePages > UINT32_MAX - zeroedPages ||
            standbyPages > UINT32_MAX - zeroedPages - freePages)
            return NtStatus::NotSupported;
        const uint32_t availablePages = static_cast<uint32_t>(zeroedPages + freePages + standbyPages);
        SystemPerformanceInformation information{};
        information.AvailablePages = availablePages;
        // The public structure contains many counters with no canonical
        // accounting owner yet. They remain zeroed; only AvailablePages is
        // currently exported as measured data.
        return ex::ExSafeCopyToUser(systemInformation, &information, required, mode);
    }

    if (returnLength)
        status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
    if (status != NtStatus::Success)
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] ReturnLength probe failed\r\n");
    if (status != NtStatus::Success) return status;

    size_t count = 0, threadCount = 0;
    status = ps::detail::captureProcessSnapshot({}, {}, count, threadCount);
    TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] Process registry count captured\r\n");
    if (status != NtStatus::InfoLengthMismatch && status != NtStatus::Success) return status;
    if (count > SIZE_MAX / sizeof(ps::detail::ProcessSnapshotEntry) ||
        threadCount > SIZE_MAX / sizeof(Handle))
        return NtStatus::InfoLengthMismatch;

    auto* snapshot = count ? new (std::nothrow_t{}) ps::detail::ProcessSnapshotEntry[count] : nullptr;
    if (count && !snapshot) return NtStatus::NoMemory;
    auto* threadIds = threadCount ? new (std::nothrow_t{}) Handle[threadCount] : nullptr;
    if (threadCount && !threadIds) { delete[] snapshot; return NtStatus::NoMemory; }
    size_t capturedCount = count;
    size_t capturedThreadCount = threadCount;
    status = ps::detail::captureProcessSnapshot({snapshot, count}, {threadIds, threadCount},
                                                capturedCount, capturedThreadCount);
    if (status != NtStatus::Success)
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] Process object snapshot failed\r\n");
    if (status != NtStatus::Success) {
        delete[] snapshot;
        delete[] threadIds;
        if (returnLength) {
            const size_t fixedBytes = sizeof(SystemProcessInformation);
            const bool overflow = capturedCount > UINT32_MAX / fixedBytes ||
                capturedThreadCount > UINT32_MAX / sizeof(SystemThreadInformation);
            const size_t requiredBytes = overflow ? UINT32_MAX
                : capturedCount * fixedBytes + capturedThreadCount * sizeof(SystemThreadInformation);
            const uint32_t required = requiredBytes <= UINT32_MAX
                ? static_cast<uint32_t>(requiredBytes) : UINT32_MAX;
            const auto copyStatus = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
            if (copyStatus != NtStatus::Success) return copyStatus;
        }
        return status;
    }

    size_t requiredSize = 0;
    for (size_t i = 0; i < capturedCount; ++i) {
        const size_t threadBytes = static_cast<size_t>(snapshot[i].threadCount) * sizeof(SystemThreadInformation);
        const size_t nameBytes = snapshot[i].imageNameLength
            ? (static_cast<size_t>(snapshot[i].imageNameLength) + 1) * sizeof(char16_t) : 0;
        const size_t unalignedBytes = sizeof(SystemProcessInformation) + threadBytes + nameBytes;
        if (unalignedBytes < threadBytes || unalignedBytes < nameBytes ||
            unalignedBytes > SIZE_MAX - (alignof(uintptr_t) - 1)) {
            delete[] snapshot; delete[] threadIds;
            return NtStatus::InfoLengthMismatch;
        }
        const size_t recordBytes = (unalignedBytes + alignof(uintptr_t) - 1) & ~(alignof(uintptr_t) - 1);
        if (recordBytes > UINT32_MAX ||
            requiredSize > UINT32_MAX - recordBytes) {
            delete[] snapshot; delete[] threadIds;
            return NtStatus::InfoLengthMismatch;
        }
        requiredSize += recordBytes;
    }
    const uint32_t required = static_cast<uint32_t>(requiredSize);
    if (returnLength) {
        status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
        if (status != NtStatus::Success) { delete[] snapshot; delete[] threadIds; return status; }
    }
    if (systemInformationLength < required) {
        delete[] snapshot; delete[] threadIds;
        return NtStatus::InfoLengthMismatch;
    }
    if (required && !systemInformation) {
        delete[] snapshot; delete[] threadIds;
        return NtStatus::AccessViolation;
    }
    status = ex::ProbeForWrite(systemInformation, required, alignof(SystemProcessInformation), mode);
    if (status != NtStatus::Success)
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] Process output buffer probe failed\r\n");
    if (status != NtStatus::Success) { delete[] snapshot; delete[] threadIds; return status; }

    auto* records = required ? new (std::nothrow_t{}) uint8_t[required] : nullptr;
    if (required && !records) { delete[] snapshot; delete[] threadIds; return NtStatus::NoMemory; }
    size_t recordOffset = 0;
    const uintptr_t outputBase = reinterpret_cast<uintptr_t>(systemInformation);
    if (required && outputBase > UINTPTR_MAX - required) {
        delete[] records; delete[] snapshot; delete[] threadIds;
        return NtStatus::AccessViolation;
    }
    for (size_t i = 0; i < capturedCount; ++i) {
        const size_t threadBytes = static_cast<size_t>(snapshot[i].threadCount) * sizeof(SystemThreadInformation);
        const size_t nameBytes = snapshot[i].imageNameLength
            ? (static_cast<size_t>(snapshot[i].imageNameLength) + 1) * sizeof(char16_t) : 0;
        const size_t unalignedBytes = sizeof(SystemProcessInformation) + threadBytes + nameBytes;
        const size_t recordBytes = (unalignedBytes + alignof(uintptr_t) - 1) & ~(alignof(uintptr_t) - 1);
        auto* record = reinterpret_cast<SystemProcessInformation*>(records + recordOffset);
        std::memset(record, 0, recordBytes);
        record->NextEntryOffset = i + 1 < capturedCount ? static_cast<uint32_t>(recordBytes) : 0;
        record->NumberOfThreads = snapshot[i].threadCount;
        record->UniqueProcessId = static_cast<uintptr_t>(snapshot[i].processId);
        record->InheritedFromUniqueProcessId = static_cast<uintptr_t>(snapshot[i].parentProcessId);
        record->HandleCount = snapshot[i].handleCount;
        record->WorkingSetSize = snapshot[i].workingSetBytes;
        if (snapshot[i].imageNameLength) {
            const size_t imageNameOffset = sizeof(SystemProcessInformation) + threadBytes;
            record->ImageName.Length = static_cast<uint16_t>(snapshot[i].imageNameLength * sizeof(char16_t));
            record->ImageName.MaximumLength = static_cast<uint16_t>(record->ImageName.Length + sizeof(char16_t));
            record->ImageName.Buffer = outputBase + recordOffset + imageNameOffset;
            std::memcpy(records + recordOffset + imageNameOffset, snapshot[i].imageName,
                        record->ImageName.Length);
        }
        // Session, CPU times, and private/peak working set data are not
        // exported until their metadata/accounting owners provide them.
        auto* outputThreads = reinterpret_cast<SystemThreadInformation*>(
            reinterpret_cast<uint8_t*>(record) + sizeof(SystemProcessInformation));
        for (size_t t = 0; t < snapshot[i].threadCount; ++t) {
            auto& outputThread = outputThreads[t];
            outputThread = {};
            outputThread.UniqueProcessId = static_cast<uintptr_t>(snapshot[i].processId);
            outputThread.UniqueThreadId = static_cast<uintptr_t>(threadIds[snapshot[i].firstThreadIndex + t]);
        }
        recordOffset += recordBytes;
    }
    status = ex::ExSafeCopyToUser(systemInformation, records, required, mode);
    if (status != NtStatus::Success)
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] Process output copy failed\r\n");
    delete[] records;
    delete[] snapshot;
    delete[] threadIds;
    return status;
}

#undef TASKMGR_QUERY_TRACE

#if !defined(MICANT_FREESTANDING)
NtStatus NtQueryInformationProcess(
    Handle /*processHandle*/,
    ProcessInformationClass processInformationClass,
    void* processInformation,
    uint32_t processInformationLength,
    uint32_t* /*returnLength*/
) {
    if (!processInformation) return NtStatus::InvalidParameter;
    if (processInformationClass == ProcessInformationClass::ProcessBasicInformation) {
        if (processInformationLength < sizeof(ProcessBasicInformation)) return NtStatus::InfoLengthMismatch;
    }
    return NtStatus::NotImplemented;
}

NtStatus NtQueryInformationThread(Handle, ThreadInformationClass, void*, uint32_t, uint32_t*) {
    return NtStatus::NotImplemented;
}

NtStatus NtAlertThread(Handle) { return NtStatus::NotImplemented; }
NtStatus NtTestAlert() { return NtStatus::NotImplemented; }
#endif

} // namespace micant::sys
