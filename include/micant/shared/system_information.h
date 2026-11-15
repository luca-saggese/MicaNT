#pragma once

#include <cstddef>
#include <cstdint>

namespace micant {

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
