#pragma once

#include <cstddef>
#include <cstdint>

namespace micant {

// MicaNT extension carried by NtQuerySystemInformation. This is a versioned
// diagnostic ABI, not a Windows SYSTEM_INFORMATION_CLASS. Fields without a
// corresponding availability bit are intentionally unknown, not zero-valued.
inline constexpr uint32_t SystemMemoryAccountingInformationClass = 0x1000;
inline constexpr uint32_t SystemMemoryAccountingVersion = 1;
enum SystemMemoryAccountingAvailability : uint32_t {
    SystemMemoryManagedAvailable   = 1u << 0,
    SystemMemoryActiveAvailable    = 1u << 1,
    SystemMemoryZeroedAvailable    = 1u << 2,
    SystemMemoryFreeAvailable      = 1u << 3,
    SystemMemoryStandbyAvailable   = 1u << 4,
    SystemMemoryModifiedAvailable  = 1u << 5,
    SystemMemoryReservedAvailable  = 1u << 6,
    SystemMemoryBadAvailable       = 1u << 7,
};
struct SystemMemoryAccountingInformation {
    uint32_t version{SystemMemoryAccountingVersion};
    uint32_t size{128};
    uint32_t availability{};
    uint32_t reserved{};
    uint64_t managedPages{};
    uint64_t activePages{};
    uint64_t zeroedPages{};
    uint64_t freePages{};
    uint64_t standbyPages{};
    uint64_t modifiedPages{};
    uint64_t reservedPages{};
    uint64_t badPages{};
    uint64_t sharedPages{};
    uint64_t pageTablePages{};
    uint64_t kernelPages{};
    uint64_t driverDmaPages{};
    uint64_t compositorSurfacePages{};
    uint64_t unattributedPages{};
};
static_assert(sizeof(SystemMemoryAccountingInformation) == 128);

// AMD64 SYSTEM_BASIC_INFORMATION used by NtQuerySystemInformation class 0.
struct SystemBasicInformation {
    uint32_t reserved{};
    uint32_t timerResolution{};
    uint32_t pageSize{};
    uint32_t numberOfPhysicalPages{};
    uint32_t lowestPhysicalPageNumber{};
    uint32_t highestPhysicalPageNumber{};
    uint32_t allocationGranularity{};
    uintptr_t minimumUserModeAddress{};
    uintptr_t maximumUserModeAddress{};
    uintptr_t activeProcessorsAffinityMask{};
    int8_t numberOfProcessors{};
    uint8_t padding[7]{};
};
static_assert(sizeof(SystemBasicInformation) == 64);

// AMD64 SYSTEM_PROCESS_INFORMATION fixed record. Native callers append
// NumberOfThreads SYSTEM_THREAD_INFORMATION records after this 256-byte part.
struct SystemUnicodeString {
    uint16_t Length;
    uint16_t MaximumLength;
    uint32_t Padding;
    uintptr_t Buffer;
};

struct SystemThreadInformation {
    int64_t KernelTime;
    int64_t UserTime;
    int64_t CreateTime;
    uint32_t WaitTime;
    uint32_t Padding;
    uintptr_t StartAddress;
    uintptr_t UniqueProcessId;
    uintptr_t UniqueThreadId;
    int32_t Priority;
    int32_t BasePriority;
    uint32_t ContextSwitches;
    uint32_t ThreadState;
    uint32_t WaitReason;
    uint32_t Padding2;
};

// Windows 10/11 AMD64 SYSTEM_PERFORMANCE_INFORMATION is 344 bytes. This
// mode-neutral view names the one field currently backed by canonical Mm
// accounting; callers must treat the opaque tail as unavailable.
struct SystemPerformanceInformation {
    uint8_t ReservedBeforeAvailablePages[44];
    uint32_t AvailablePages;
    uint8_t ReservedAfterAvailablePages[296];
};
static_assert(offsetof(SystemPerformanceInformation, AvailablePages) == 44);
static_assert(sizeof(SystemPerformanceInformation) == 344);

// SystemProcessorPerformanceInformation (class 8), AMD64. The two reserved
// LARGE_INTEGER slots and trailing ULONG preserve the public native layout;
// only the first three time fields are currently accounted by Ke.
struct SystemProcessorPerformanceInformation {
    int64_t IdleTime;
    int64_t KernelTime;
    int64_t UserTime;
    int64_t Reserved1[2];
    uint32_t Reserved2;
    uint32_t Padding;
};
static_assert(sizeof(SystemProcessorPerformanceInformation) == 48);
static_assert(offsetof(SystemProcessorPerformanceInformation, KernelTime) == 8);
static_assert(offsetof(SystemProcessorPerformanceInformation, UserTime) == 16);

struct SystemProcessInformation {
    uint32_t NextEntryOffset;
    uint32_t NumberOfThreads;
    int64_t WorkingSetPrivateSize;
    uint32_t HardFaultCount;
    uint32_t NumberOfThreadsHighWatermark;
    uint64_t CycleTime;
    int64_t CreateTime;
    int64_t UserTime;
    int64_t KernelTime;
    SystemUnicodeString ImageName;
    int32_t BasePriority;
    uint32_t Padding;
    uintptr_t UniqueProcessId;
    uintptr_t InheritedFromUniqueProcessId;
    uint32_t HandleCount;
    uint32_t SessionId;
    uintptr_t PageDirectoryBase;
    uintptr_t PeakVirtualSize;
    uintptr_t VirtualSize;
    uint32_t PageFaultCount;
    uint32_t Padding2;
    uintptr_t PeakWorkingSetSize;
    uintptr_t WorkingSetSize;
    uintptr_t QuotaPeakPagedPoolUsage;
    uintptr_t QuotaPagedPoolUsage;
    uintptr_t QuotaPeakNonPagedPoolUsage;
    uintptr_t QuotaNonPagedPoolUsage;
    uintptr_t PagefileUsage;
    uintptr_t PeakPagefileUsage;
    uintptr_t PrivatePageCount;
    int64_t ReadOperationCount;
    int64_t WriteOperationCount;
    int64_t OtherOperationCount;
    int64_t ReadTransferCount;
    int64_t WriteTransferCount;
    int64_t OtherTransferCount;
};

static_assert(sizeof(SystemUnicodeString) == 16);
static_assert(offsetof(SystemProcessInformation, ImageName) == 56);
static_assert(offsetof(SystemProcessInformation, UniqueProcessId) == 80);
static_assert(offsetof(SystemProcessInformation, HandleCount) == 96);
static_assert(offsetof(SystemProcessInformation, PageDirectoryBase) == 104);
static_assert(offsetof(SystemProcessInformation, PeakVirtualSize) == 112);
static_assert(offsetof(SystemProcessInformation, WorkingSetSize) == 144);
static_assert(offsetof(SystemProcessInformation, PrivatePageCount) == 200);
static_assert(offsetof(SystemProcessInformation, ReadOperationCount) == 208);
static_assert(sizeof(SystemProcessInformation) == 256);
static_assert(sizeof(SystemThreadInformation) == 80);

} // namespace micant
