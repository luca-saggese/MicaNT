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

#if defined(MICANT_BOOTSTRAP_TASKMGR_SELFTEST) || defined(MICANT_BOOTSTRAP_REACTOS_APP_TASKMGR)
extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;
#define TASKMGR_QUERY_TRACE(text) MicantNetbootSerialWrite(text)
#define TASKMGR_QUERY_NUMBER(value) MicantNetbootSerialWriteNumber(static_cast<uint64_t>(value))
#else
#define TASKMGR_QUERY_TRACE(text) ((void)0)
#define TASKMGR_QUERY_NUMBER(value) ((void)0)
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
    constexpr uint32_t systemBasicInformationClass = 0;
    constexpr uint32_t systemProcessInformationClass = 5;
    constexpr uint32_t systemPerformanceInformationClass = 2;
    constexpr uint32_t systemProcessorPerformanceInformationClass = 8;
    constexpr uint32_t systemTimeOfDayInformationClass = 3;
    constexpr uint32_t systemFileCacheInformationClass = 21;
    constexpr uint32_t systemMemoryAccountingClass = SystemMemoryAccountingInformationClass;
    if (systemInformationClass != systemBasicInformationClass &&
        systemInformationClass != systemProcessInformationClass &&
        systemInformationClass != systemPerformanceInformationClass &&
        systemInformationClass != systemProcessorPerformanceInformationClass &&
        systemInformationClass != systemTimeOfDayInformationClass &&
        systemInformationClass != systemFileCacheInformationClass &&
        systemInformationClass != systemMemoryAccountingClass)
        return NtStatus::InvalidInfoClass;

    if (systemInformationClass == systemProcessInformationClass ||
        systemInformationClass == systemPerformanceInformationClass ||
        systemInformationClass == systemProcessorPerformanceInformationClass ||
        systemInformationClass == systemTimeOfDayInformationClass ||
        systemInformationClass == systemFileCacheInformationClass) {
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrData] NtQuerySystemInformation class=");
        TASKMGR_QUERY_NUMBER(systemInformationClass);
        TASKMGR_QUERY_TRACE(" length="); TASKMGR_QUERY_NUMBER(systemInformationLength);
        TASKMGR_QUERY_TRACE(" buffer="); TASKMGR_QUERY_NUMBER(systemInformation ? 1 : 0);
        TASKMGR_QUERY_TRACE("\r\n");
    }
    ScopedQueryInterruptDisable interruptGuard;
#if defined(MICANT_FREESTANDING)
    auto* current = ke::KeGetCurrentThread();
    const auto mode = current && current->previousUserMode ? mm::UserMode : mm::KernelMode;
#else
    const auto mode = mm::KernelMode;
#endif
    NtStatus status = NtStatus::Success;

    if (systemInformationClass == systemTimeOfDayInformationClass) {
        constexpr uint32_t required = sizeof(SystemTimeOfDayInformation);
        if (returnLength) {
            status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
            if (status != NtStatus::Success) return status;
        }
        if (systemInformationLength < required) return NtStatus::InfoLengthMismatch;
        if (!systemInformation) return NtStatus::AccessViolation;
        status = ex::ProbeForWrite(systemInformation, required,
            alignof(SystemTimeOfDayInformation), mode);
        if (status != NtStatus::Success) return status;

        uint64_t now{}, uptime{};
        if (!ke::KeQuerySystemTime100ns(now) || !ke::KeQueryUptime100ns(uptime) ||
            now > static_cast<uint64_t>(INT64_MAX) || uptime > now)
            return NtStatus::NotSupported;
        SystemTimeOfDayInformation information{};
        information.currentTime = static_cast<int64_t>(now);
        information.bootTime = static_cast<int64_t>(now - uptime);
        return ex::ExSafeCopyToUser(systemInformation, &information, required, mode);
    }

    if (systemInformationClass == systemFileCacheInformationClass) {
        constexpr uint32_t required = sizeof(SystemFileCacheInformation);
        if (returnLength) {
            status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
            if (status != NtStatus::Success) return status;
        }
        if (systemInformationLength < required) return NtStatus::InfoLengthMismatch;
        if (!systemInformation) return NtStatus::AccessViolation;
        status = ex::ProbeForWrite(systemInformation, required,
            alignof(SystemFileCacheInformation), mode);
        if (status != NtStatus::Success) return status;

        // The current I/O and memory managers have no file-cache owner, so the
        // cache is empty. Keep this query tied to that explicit subsystem state.
        const SystemFileCacheInformation emptyCache{};
        return ex::ExSafeCopyToUser(systemInformation, &emptyCache, required, mode);
    }

    if (systemInformationClass == systemBasicInformationClass) {
        constexpr uint32_t required = sizeof(SystemBasicInformation);
        if (returnLength) {
            status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
            if (status != NtStatus::Success) return status;
        }
        if (systemInformationLength < required) return NtStatus::InfoLengthMismatch;
        if (!systemInformation) return NtStatus::AccessViolation;
        status = ex::ProbeForWrite(systemInformation, required, alignof(SystemBasicInformation), mode);
        if (status != NtStatus::Success) return status;
        const auto memory = mm::MmQuerySystemMemoryDiagnostics();
        if (!memory.totalPhysicalPages || memory.totalPhysicalPages > UINT32_MAX)
            return NtStatus::NotSupported;
        SystemBasicInformation information{};
        information.pageSize = static_cast<uint32_t>(mm::PageSize4KB);
        information.numberOfPhysicalPages = static_cast<uint32_t>(memory.totalPhysicalPages);
        information.lowestPhysicalPageNumber = 1;
        information.highestPhysicalPageNumber = information.numberOfPhysicalPages;
        information.allocationGranularity = 64 * 1024;
        information.minimumUserModeAddress = 0x10000;
        information.maximumUserModeAddress = 0x00007ffffffe0000ULL;
        const auto processorSnapshot = km::perf::QueryProcessorPerformanceSnapshot();
        const uint32_t processorCount = processorSnapshot.processorCount;
        if (processorCount == 0 || processorCount > 64) return NtStatus::NotSupported;
        information.activeProcessorsAffinityMask = processorCount == 64
            ? UINT64_MAX : ((uint64_t{1} << processorCount) - 1);
        information.numberOfProcessors = static_cast<int8_t>(processorCount);
        return ex::ExSafeCopyToUser(systemInformation, &information, required, mode);
    }

    if (systemInformationClass == systemMemoryAccountingClass) {
        constexpr uint32_t required = sizeof(SystemMemoryAccountingInformation);
        if (returnLength) {
            status = ex::ProbeForWrite(returnLength, sizeof(*returnLength), alignof(uint32_t), mode);
            if (status != NtStatus::Success) return status;
            status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
            if (status != NtStatus::Success) return status;
        }
        if (systemInformationLength < required) return NtStatus::InfoLengthMismatch;
        if (!systemInformation) return NtStatus::AccessViolation;
        status = ex::ProbeForWrite(systemInformation, required,
            alignof(SystemMemoryAccountingInformation), mode);
        if (status != NtStatus::Success) return status;
        const auto diagnostics = mm::MmQuerySystemMemoryDiagnostics();
        if (diagnostics.totalPhysicalPages == 0) return NtStatus::NotSupported;
        SystemMemoryAccountingInformation report{};
        report.availability = SystemMemoryManagedAvailable | SystemMemoryActiveAvailable |
            SystemMemoryZeroedAvailable | SystemMemoryFreeAvailable |
            SystemMemoryStandbyAvailable | SystemMemoryModifiedAvailable |
            SystemMemoryReservedAvailable | SystemMemoryBadAvailable;
        report.managedPages = diagnostics.totalPhysicalPages;
        report.activePages = diagnostics.activePages;
        report.zeroedPages = diagnostics.zeroedPages;
        report.freePages = diagnostics.freePages;
        report.standbyPages = diagnostics.standbyPages;
        report.modifiedPages = diagnostics.modifiedPages;
        report.reservedPages = diagnostics.reservedPages;
        report.badPages = diagnostics.badPages;
        return ex::ExSafeCopyToUser(systemInformation, &report, sizeof(report), mode);
    }

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
    TASKMGR_QUERY_TRACE("[MicaNT TaskmgrData] process snapshot probe status=");
    TASKMGR_QUERY_NUMBER(static_cast<uint32_t>(status));
    TASKMGR_QUERY_TRACE(" processes="); TASKMGR_QUERY_NUMBER(count);
    TASKMGR_QUERY_TRACE(" threads="); TASKMGR_QUERY_NUMBER(threadCount);
    TASKMGR_QUERY_TRACE("\r\n");
    if (status != NtStatus::InfoLengthMismatch && status != NtStatus::Success) return status;
    if (count > SIZE_MAX / sizeof(ps::detail::ProcessSnapshotEntry) ||
        threadCount > SIZE_MAX / sizeof(ps::detail::ProcessSnapshotThreadEntry))
        return NtStatus::InfoLengthMismatch;

    auto* snapshot = count ? new (std::nothrow_t{}) ps::detail::ProcessSnapshotEntry[count] : nullptr;
    if (count && !snapshot) return NtStatus::NoMemory;
    auto* threads = threadCount
        ? new (std::nothrow_t{}) ps::detail::ProcessSnapshotThreadEntry[threadCount] : nullptr;
    if (threadCount && !threads) { delete[] snapshot; return NtStatus::NoMemory; }
    size_t capturedCount = count;
    size_t capturedThreadCount = threadCount;
    status = ps::detail::captureProcessSnapshot({snapshot, count}, {threads, threadCount},
                                                capturedCount, capturedThreadCount);
    TASKMGR_QUERY_TRACE("[MicaNT TaskmgrData] process snapshot capture status=");
    TASKMGR_QUERY_NUMBER(static_cast<uint32_t>(status));
    TASKMGR_QUERY_TRACE(" processes="); TASKMGR_QUERY_NUMBER(capturedCount);
    TASKMGR_QUERY_TRACE(" threads="); TASKMGR_QUERY_NUMBER(capturedThreadCount);
    TASKMGR_QUERY_TRACE("\r\n");
    if (status != NtStatus::Success)
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] Process object snapshot failed\r\n");
    if (status != NtStatus::Success) {
        delete[] snapshot;
        delete[] threads;
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
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrData] process pid=");
        TASKMGR_QUERY_NUMBER(snapshot[i].processId);
        TASKMGR_QUERY_TRACE(" nameChars="); TASKMGR_QUERY_NUMBER(snapshot[i].imageNameLength);
        TASKMGR_QUERY_TRACE(" threads="); TASKMGR_QUERY_NUMBER(snapshot[i].threadCount);
        TASKMGR_QUERY_TRACE(" handles="); TASKMGR_QUERY_NUMBER(snapshot[i].handleCount);
        TASKMGR_QUERY_TRACE(" wsBytes="); TASKMGR_QUERY_NUMBER(snapshot[i].workingSetBytes);
        TASKMGR_QUERY_TRACE(" peakWsBytes="); TASKMGR_QUERY_NUMBER(snapshot[i].peakWorkingSetBytes);
        TASKMGR_QUERY_TRACE(" privateCommitPages="); TASKMGR_QUERY_NUMBER(snapshot[i].privateCommitPages);
        TASKMGR_QUERY_TRACE("\r\n");
        const size_t threadBytes = static_cast<size_t>(snapshot[i].threadCount) * sizeof(SystemThreadInformation);
        const size_t nameBytes = snapshot[i].imageNameLength
            ? (static_cast<size_t>(snapshot[i].imageNameLength) + 1) * sizeof(char16_t) : 0;
        const size_t unalignedBytes = sizeof(SystemProcessInformation) + threadBytes + nameBytes;
        if (unalignedBytes < threadBytes || unalignedBytes < nameBytes ||
            unalignedBytes > SIZE_MAX - (alignof(uintptr_t) - 1)) {
            delete[] snapshot; delete[] threads;
            return NtStatus::InfoLengthMismatch;
        }
        const size_t recordBytes = (unalignedBytes + alignof(uintptr_t) - 1) & ~(alignof(uintptr_t) - 1);
        if (recordBytes > UINT32_MAX ||
            requiredSize > UINT32_MAX - recordBytes) {
            delete[] snapshot; delete[] threads;
            return NtStatus::InfoLengthMismatch;
        }
        requiredSize += recordBytes;
    }
    const uint32_t required = static_cast<uint32_t>(requiredSize);
    if (returnLength) {
        status = ex::ExSafeCopyToUser(returnLength, &required, sizeof(required), mode);
        if (status != NtStatus::Success) { delete[] snapshot; delete[] threads; return status; }
    }
    if (systemInformationLength < required) {
        delete[] snapshot; delete[] threads;
        return NtStatus::InfoLengthMismatch;
    }
    if (required && !systemInformation) {
        delete[] snapshot; delete[] threads;
        return NtStatus::AccessViolation;
    }
    status = ex::ProbeForWrite(systemInformation, required, alignof(SystemProcessInformation), mode);
    if (status != NtStatus::Success)
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] Process output buffer probe failed\r\n");
    if (status != NtStatus::Success) { delete[] snapshot; delete[] threads; return status; }

    auto* records = required ? new (std::nothrow_t{}) uint8_t[required] : nullptr;
    if (required && !records) { delete[] snapshot; delete[] threads; return NtStatus::NoMemory; }
    size_t recordOffset = 0;
    const uintptr_t outputBase = reinterpret_cast<uintptr_t>(systemInformation);
    if (required && outputBase > UINTPTR_MAX - required) {
        delete[] records; delete[] snapshot; delete[] threads;
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
        record->PeakWorkingSetSize = snapshot[i].peakWorkingSetBytes;
        // The public record carries a byte count here, while Ps snapshots
        // commit in pages. Preserve the ABI unit and fail instead of wrapping.
        if (snapshot[i].privateCommitPages > UINTPTR_MAX / mm::PageSize4KB) {
            delete[] records; delete[] snapshot; delete[] threads;
            return NtStatus::NotSupported;
        }
        record->PrivatePageCount = snapshot[i].privateCommitPages * mm::PageSize4KB;
        if (snapshot[i].readOperationCount > static_cast<uint64_t>(INT64_MAX) ||
            snapshot[i].writeOperationCount > static_cast<uint64_t>(INT64_MAX) ||
            snapshot[i].readTransferCount > static_cast<uint64_t>(INT64_MAX) ||
            snapshot[i].writeTransferCount > static_cast<uint64_t>(INT64_MAX)) {
            delete[] records; delete[] snapshot; delete[] threads;
            return NtStatus::NotSupported;
        }
        record->ReadOperationCount = static_cast<int64_t>(snapshot[i].readOperationCount);
        record->WriteOperationCount = static_cast<int64_t>(snapshot[i].writeOperationCount);
        record->ReadTransferCount = static_cast<int64_t>(snapshot[i].readTransferCount);
        record->WriteTransferCount = static_cast<int64_t>(snapshot[i].writeTransferCount);
        if (snapshot[i].userTime100ns > static_cast<uint64_t>(INT64_MAX) ||
            snapshot[i].kernelTime100ns > static_cast<uint64_t>(INT64_MAX)) {
            delete[] records; delete[] snapshot; delete[] threads;
            return NtStatus::NotSupported;
        }
        record->UserTime = static_cast<int64_t>(snapshot[i].userTime100ns);
        record->KernelTime = static_cast<int64_t>(snapshot[i].kernelTime100ns);
        if (snapshot[i].imageNameLength) {
            const size_t imageNameOffset = sizeof(SystemProcessInformation) + threadBytes;
            record->ImageName.Length = static_cast<uint16_t>(snapshot[i].imageNameLength * sizeof(char16_t));
            record->ImageName.MaximumLength = static_cast<uint16_t>(record->ImageName.Length + sizeof(char16_t));
            record->ImageName.Buffer = outputBase + recordOffset + imageNameOffset;
            std::memcpy(records + recordOffset + imageNameOffset, snapshot[i].imageName,
                        record->ImageName.Length);
        }
        // Session and private/peak working set data remain unavailable until
        // their metadata/accounting owners provide them.
        auto* outputThreads = reinterpret_cast<SystemThreadInformation*>(
            reinterpret_cast<uint8_t*>(record) + sizeof(SystemProcessInformation));
        for (size_t t = 0; t < snapshot[i].threadCount; ++t) {
            auto& outputThread = outputThreads[t];
            outputThread = {};
            const auto& thread = threads[snapshot[i].firstThreadIndex + t];
            if (thread.userTime100ns > static_cast<uint64_t>(INT64_MAX) ||
                thread.kernelTime100ns > static_cast<uint64_t>(INT64_MAX)) {
                delete[] records; delete[] snapshot; delete[] threads;
                return NtStatus::NotSupported;
            }
            outputThread.UserTime = static_cast<int64_t>(thread.userTime100ns);
            outputThread.KernelTime = static_cast<int64_t>(thread.kernelTime100ns);
            outputThread.UniqueProcessId = static_cast<uintptr_t>(snapshot[i].processId);
            outputThread.UniqueThreadId = static_cast<uintptr_t>(thread.threadId);
        }
        recordOffset += recordBytes;
    }
    status = ex::ExSafeCopyToUser(systemInformation, records, required, mode);
    TASKMGR_QUERY_TRACE("[MicaNT TaskmgrData] process query returned status=");
    TASKMGR_QUERY_NUMBER(static_cast<uint32_t>(status));
    TASKMGR_QUERY_TRACE(" records="); TASKMGR_QUERY_NUMBER(capturedCount);
    TASKMGR_QUERY_TRACE(" bytes="); TASKMGR_QUERY_NUMBER(required);
    TASKMGR_QUERY_TRACE("\r\n");
    if (status != NtStatus::Success)
        TASKMGR_QUERY_TRACE("[MicaNT TaskmgrTest] Process output copy failed\r\n");
    delete[] records;
    delete[] snapshot;
    delete[] threads;
    return status;
}

#undef TASKMGR_QUERY_TRACE
#undef TASKMGR_QUERY_NUMBER

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
