#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "micant/km/executive/freestanding.hpp"
#include "micant/km/executive/mm.hpp"

extern "C" size_t strlen(const char* text) {
    const auto* bytes = static_cast<const volatile char*>(text);
    size_t count{};
    while (bytes[count]) ++count;
    return count;
}

namespace {

using micant::ex::BootstrapLogRoutine;
using micant::ex::BootstrapReserveHeapRangeRoutine;
using micant::ex::BootstrapReleaseHeapRangeRoutine;
using micant::ex::BootstrapMapHeapPageRoutine;
using micant::ex::BootstrapUnmapHeapPageRoutine;
using micant::mm::PfnDatabase;

struct AllocationHeader {
    uint64_t cookie;
    size_t pageCount;
    size_t requestedSize;
    size_t alignment;
    uintptr_t baseAddress;
};

constexpr uint64_t kAllocationCookie = UINT64_C(0x4D6963614E544865);
constexpr size_t kDefaultAlignment =
    alignof(std::max_align_t) < 16 ? 16 : alignof(std::max_align_t);
static_assert((kDefaultAlignment & (kDefaultAlignment - 1)) == 0);

BootstrapLogRoutine g_bootstrapLog{};
BootstrapReserveHeapRangeRoutine g_reserveHeapRange{};
BootstrapReleaseHeapRangeRoutine g_releaseHeapRange{};
BootstrapMapHeapPageRoutine g_mapHeapPage{};
BootstrapUnmapHeapPageRoutine g_unmapHeapPage{};
bool g_runtimeInitialized{};
uint64_t g_rejectionLogCount{};
using CInitializer = int (*)();
using CxxInitializer = void (*)();

enum class CxxPhase : uint8_t {
    Cold, Initializing, Ready, Failed, Terminating, Terminated
};

constexpr size_t kMaximumInitializers = 4096;
constexpr size_t kMaximumDestructors = 1024;
constinit CxxPhase g_cxxPhase{CxxPhase::Cold};
constinit CxxInitializer g_destructors[kMaximumDestructors]{};
constinit size_t g_destructorCount{};

#if defined(_WIN32)
// PE linker ordering and compiler ABI, not a hosted CRT implementation:
// https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-initialization
__attribute__((section(".CRT$XIA"), used))
CInitializer const g_cInitializersBegin[]{nullptr};
__attribute__((section(".CRT$XIZ"), used))
CInitializer const g_cInitializersEnd[]{nullptr};
__attribute__((section(".CRT$XCA"), used))
CxxInitializer const g_cxxInitializersBegin[]{nullptr};
__attribute__((section(".CRT$XCZ"), used))
CxxInitializer const g_cxxInitializersEnd[]{nullptr};

#if defined(__MINGW32__)
// The GNU PE target emits .ctors instead of .CRT$XCU. Its linker supplies this
// sentinel-bounded table even with -nostdlib; no MinGW runtime is called.
extern "C" CxxInitializer __CTOR_LIST__[];
#endif
#endif

[[nodiscard]] bool isPowerOfTwo(size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

void logMessage(BootstrapLogRoutine log, const char* message) noexcept {
    if (log) log(message);
}

void rejectAllocation(const char* reason) noexcept {
    if (g_bootstrapLog) {
        g_bootstrapLog(reason);
        ++g_rejectionLogCount;
    }
}

void logHexValue(const char* label, uintptr_t value) noexcept {
    if (!g_bootstrapLog) return;
    char buffer[48]{};
    constexpr char digits[] = "0123456789ABCDEF";
    size_t index{};
    while (label[index] && index + sizeof(value) * 2 + 4 < sizeof(buffer)) {
        buffer[index] = label[index];
        ++index;
    }
    buffer[index++] = '0';
    buffer[index++] = 'x';
    for (size_t nibble = sizeof(value) * 2; nibble != 0; --nibble) {
        const auto shift = static_cast<unsigned>((nibble - 1) * 4);
        buffer[index++] = digits[(value >> shift) & 0xFu];
    }
    buffer[index++] = '\r';
    buffer[index++] = '\n';
    g_bootstrapLog(buffer);
}

[[noreturn]] void stop(const char* reason) noexcept {
    logMessage(g_bootstrapLog, "ExRuntime FATAL: ");
    logMessage(g_bootstrapLog, reason);
    __builtin_trap();
}

void requireInitialized() noexcept {
    if (!g_runtimeInitialized) {
        stop("ExHeap: pre-init allocator use rejected; stopping\r\n");
    }
}

#if defined(_WIN32)
template<typename Initializer>
[[nodiscard]] size_t initializerCount(
    const Initializer* begin, const Initializer* end
) noexcept {
    const auto first = reinterpret_cast<uintptr_t>(begin);
    const auto last = reinterpret_cast<uintptr_t>(end);
    if (last < first || (last - first) % sizeof(Initializer) != 0 ||
        (last - first) / sizeof(Initializer) > kMaximumInitializers) {
        stop("ExCxx: invalid PE initializer range; stopping\r\n");
    }
    return (last - first) / sizeof(Initializer);
}

template<typename Initializer>
[[nodiscard]] Initializer initializerAt(
    const Initializer* begin, size_t index
) noexcept {
    // The PE linker, not the C++ array object model, joins these contributions.
    const auto address = reinterpret_cast<uintptr_t>(begin) +
        index * sizeof(Initializer);
    return *reinterpret_cast<const Initializer*>(address);
}
#endif

[[nodiscard]] uint64_t allocationCookie(
    uintptr_t userAddress,
    const AllocationHeader& header
) noexcept {
    return kAllocationCookie ^ static_cast<uint64_t>(userAddress) ^
        static_cast<uint64_t>(header.pageCount) ^
        static_cast<uint64_t>(header.requestedSize) ^
        static_cast<uint64_t>(header.alignment) ^
        static_cast<uint64_t>(header.baseAddress);
}

[[nodiscard]] AllocationHeader* validatedHeader(void* pointer) noexcept {
    const uintptr_t userAddress = reinterpret_cast<uintptr_t>(pointer);
    if (userAddress < sizeof(AllocationHeader)) return nullptr;

    auto* header = reinterpret_cast<AllocationHeader*>(
        userAddress - sizeof(AllocationHeader));
    if (header->cookie != allocationCookie(userAddress, *header) ||
        header->pageCount == 0 ||
        header->alignment < kDefaultAlignment ||
        !isPowerOfTwo(header->alignment) ||
        (userAddress & (header->alignment - 1)) != 0 ||
        header->pageCount > static_cast<size_t>(-1) / micant::mm::PageSize4KB) {
        return nullptr;
    }

    const size_t mappedBytes = header->pageCount * micant::mm::PageSize4KB;
    if (header->baseAddress > static_cast<uintptr_t>(-1) - mappedBytes) {
        return nullptr;
    }
    const uintptr_t allocationEnd = header->baseAddress + mappedBytes;
    const uintptr_t headerAddress = reinterpret_cast<uintptr_t>(header);
    if (headerAddress < header->baseAddress +
            header->pageCount * sizeof(uint64_t) ||
        headerAddress > allocationEnd - sizeof(AllocationHeader) ||
        userAddress > allocationEnd ||
        header->requestedSize > allocationEnd - userAddress) {
        return nullptr;
    }
    return header;
}

[[nodiscard]] void* allocateMemory(size_t requestedSize, size_t alignment) noexcept {
    requireInitialized();
    if (!isPowerOfTwo(alignment)) {
        rejectAllocation("ExHeap: allocation rejected (invalid alignment)\r\n");
        return nullptr;
    }
    if (alignment < kDefaultAlignment) alignment = kDefaultAlignment;

    constexpr size_t pageSize = micant::mm::PageSize4KB;
    const size_t usableSize = requestedSize == 0 ? 1 : requestedSize;
    if (alignment - 1 > static_cast<size_t>(-1) - sizeof(AllocationHeader) ||
        usableSize > static_cast<size_t>(-1) - sizeof(AllocationHeader) -
            (alignment - 1)) {
        rejectAllocation("ExHeap: allocation rejected (size overflow)\r\n");
        return nullptr;
    }

    const size_t baseBytes = sizeof(AllocationHeader) + alignment - 1 + usableSize;
    size_t pageCount = baseBytes / (pageSize - sizeof(uint64_t)) +
        static_cast<size_t>(baseBytes % (pageSize - sizeof(uint64_t)) != 0);
    if (pageCount == 0 || pageCount > static_cast<size_t>(-1) / pageSize ||
        pageCount > static_cast<size_t>(-1) / sizeof(uint64_t)) {
        rejectAllocation("ExHeap: allocation rejected (page count overflow)\r\n");
        return nullptr;
    }

    auto& database = PfnDatabase::get();
    const uintptr_t baseAddress = g_reserveHeapRange(pageCount);
    if (!baseAddress) {
        rejectAllocation("ExHeap: allocation rejected (kernel VA range unavailable)\r\n");
        return nullptr;
    }
    const size_t mappedBytes = pageCount * pageSize;
    if (baseAddress > static_cast<uintptr_t>(-1) - mappedBytes) {
        (void)g_releaseHeapRange(baseAddress, pageCount);
        rejectAllocation("ExHeap: allocation rejected (address overflow)\r\n");
        return nullptr;
    }
    const size_t pageListBytes = pageCount * sizeof(uint64_t);
    const uintptr_t minimumUserAddress = baseAddress + pageListBytes + sizeof(AllocationHeader);
    if (minimumUserAddress > static_cast<uintptr_t>(-1) - (alignment - 1)) {
        (void)g_releaseHeapRange(baseAddress, pageCount);
        rejectAllocation("ExHeap: allocation rejected (address overflow)\r\n");
        return nullptr;
    }
    const uintptr_t userAddress =
        (minimumUserAddress + alignment - 1) & ~static_cast<uintptr_t>(alignment - 1);
    const uintptr_t allocationEnd = baseAddress + mappedBytes;
    if (userAddress > allocationEnd || usableSize > allocationEnd - userAddress) {
        (void)g_releaseHeapRange(baseAddress, pageCount);
        rejectAllocation("ExHeap: allocation rejected (mapping too small)\r\n");
        return nullptr;
    }

    auto* pfns = reinterpret_cast<uint64_t*>(baseAddress);
    size_t mappedPages{};
    for (; mappedPages < pageCount; ++mappedPages) {
        const uint64_t pfn = database.allocatePage(false);
        if (!pfn) break;
        const uintptr_t pageAddress = baseAddress + mappedPages * pageSize;
        if (!g_mapHeapPage(pageAddress, pfn)) {
            database.freePage(pfn);
            break;
        }
        pfns[mappedPages] = pfn;
    }
    if (mappedPages != pageCount) {
        while (mappedPages) {
            --mappedPages;
            const uintptr_t pageAddress = baseAddress + mappedPages * pageSize;
            const uint64_t pfn = pfns[mappedPages];
            if (!g_unmapHeapPage(pageAddress))
                stop("ExHeap: failed to unmap a partial allocation; stopping\r\n");
            database.freePage(pfn);
        }
        if (!g_releaseHeapRange(baseAddress, pageCount))
            stop("ExHeap: failed to return a partial kernel VA range; stopping\r\n");
        rejectAllocation("ExHeap: allocation rejected (physical pages unavailable)\r\n");
        return nullptr;
    }

    auto* header = reinterpret_cast<AllocationHeader*>(
        userAddress - sizeof(AllocationHeader));
    header->pageCount = pageCount;
    header->requestedSize = requestedSize;
    header->alignment = alignment;
    header->baseAddress = baseAddress;
    header->cookie = allocationCookie(userAddress, *header);
    return reinterpret_cast<void*>(userAddress);
}

void releaseMemory(void* pointer) noexcept {
    if (!pointer) return;
    requireInitialized();

    AllocationHeader* header = validatedHeader(pointer);
    if (!header) {
        logHexValue("ExHeap invalid free pointer=", reinterpret_cast<uintptr_t>(pointer));
        logHexValue("ExHeap invalid free caller=",
            reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
        stop("ExHeap: invalid free rejected; stopping\r\n");
    }

    const size_t pageCount = header->pageCount;
    const uintptr_t baseAddress = header->baseAddress;
    auto* pfns = reinterpret_cast<uint64_t*>(baseAddress);
    header->cookie = 0;
    auto& database = PfnDatabase::get();
    for (size_t remaining = pageCount; remaining != 0; --remaining) {
        const size_t index = remaining - 1;
        const uint64_t pfn = pfns[index];
        const uintptr_t pageAddress = baseAddress + index * micant::mm::PageSize4KB;
        if (!g_unmapHeapPage(pageAddress))
            stop("ExHeap: failed to unmap a released allocation; stopping\r\n");
        database.freePage(pfn);
    }
    (void)g_releaseHeapRange(baseAddress, pageCount);
}

[[nodiscard]] bool fillAndVerify(void* memory, size_t size, uint8_t seed) noexcept {
    auto* bytes = static_cast<volatile uint8_t*>(memory);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<uint8_t>(seed + index * 13);
    }
    bool matches = true;
    for (size_t index = 0; index < size; ++index) {
        if (bytes[index] != static_cast<uint8_t>(seed + index * 13)) {
            matches = false;
        }
    }
    return matches;
}

[[nodiscard]] size_t availablePageCount(PfnDatabase& database) noexcept {
    return database.getFreeCount() + database.getZeroedCount() + database.getStandbyCount();
}

[[nodiscard]] bool verifyExhaustionRollback(PfnDatabase& database,
                                            size_t expectedAvailable) noexcept {
    const size_t capacity = availablePageCount(database);
    if (capacity < 4 || capacity > static_cast<size_t>(-1) / sizeof(uint64_t)) return false;
    auto* heldPfns = static_cast<uint64_t*>(::operator new[](
        capacity * sizeof(uint64_t), std::nothrow_t{}));
    if (!heldPfns) return false;
    void* primingAllocation = allocateMemory(1, kDefaultAlignment);
    if (!primingAllocation) {
        ::operator delete[](heldPfns, std::nothrow_t{});
        return false;
    }

    const size_t availableAfterSetup = availablePageCount(database);
    const size_t heldTarget = availableAfterSetup ? availableAfterSetup - 1 : 0;
    size_t heldCount{};
    for (; heldCount < heldTarget; ++heldCount) {
        const uint64_t pfn = database.allocatePage(false);
        if (!pfn) break;
        heldPfns[heldCount] = pfn;
    }
    const size_t activeBeforeRollback = database.getActiveCount();
    const uint64_t rejectionsBefore = g_rejectionLogCount;
    void* partiallyMapped = allocateMemory(5000, kDefaultAlignment);
    if (partiallyMapped) releaseMemory(partiallyMapped);
    const bool rollbackOkay = heldCount == heldTarget &&
        availablePageCount(database) == 1 &&
        database.getActiveCount() == activeBeforeRollback &&
        !partiallyMapped && g_rejectionLogCount == rejectionsBefore + 1;

    while (heldCount) database.freePage(heldPfns[--heldCount]);
    ::operator delete[](heldPfns, std::nothrow_t{});
    releaseMemory(primingAllocation);
    return rollbackOkay && availablePageCount(database) == expectedAvailable;
}

} // namespace

namespace {

using CommitRange = micant::mm::VirtualAddressDescriptor::CommitRange;
std::atomic_flag g_commitRangePoolLock = ATOMIC_FLAG_INIT;
void* g_commitRangeFreeList{};
void* g_commitRangePoolPages{};

void lockCommitRangePool() noexcept {
    while (g_commitRangePoolLock.test_and_set(std::memory_order_acquire)) {}
}

void unlockCommitRangePool() noexcept {
    g_commitRangePoolLock.clear(std::memory_order_release);
}

void addCommitRangePoolPage(void* page) noexcept {
    *static_cast<void**>(page) = g_commitRangePoolPages;
    g_commitRangePoolPages = page;
    constexpr size_t alignment = alignof(CommitRange) > alignof(void*)
        ? alignof(CommitRange) : alignof(void*);
    constexpr size_t stride = (sizeof(CommitRange) + alignment - 1) & ~(alignment - 1);
    uintptr_t cursor = (reinterpret_cast<uintptr_t>(page) + sizeof(void*) + alignment - 1) &
        ~static_cast<uintptr_t>(alignment - 1);
    const uintptr_t end = reinterpret_cast<uintptr_t>(page) + 4000;
    while (cursor <= end && stride <= end - cursor) {
        *reinterpret_cast<void**>(cursor) = g_commitRangeFreeList;
        g_commitRangeFreeList = reinterpret_cast<void*>(cursor);
        cursor += stride;
    }
}

} // namespace

void* micant::mm::VirtualAddressDescriptor::CommitRange::operator new(
    size_t size, const std::nothrow_t&) noexcept {
    if (size != sizeof(CommitRange)) return nullptr;
    lockCommitRangePool();
    if (!g_commitRangeFreeList) {
        unlockCommitRangePool();
        void* page = allocateMemory(4000, kDefaultAlignment);
        if (!page) return nullptr;
        lockCommitRangePool();
        addCommitRangePoolPage(page);
    }
    void* slot = g_commitRangeFreeList;
    g_commitRangeFreeList = *static_cast<void**>(slot);
    unlockCommitRangePool();
    return slot;
}

void micant::mm::VirtualAddressDescriptor::CommitRange::operator delete(void* pointer) noexcept {
    if (!pointer) return;
    lockCommitRangePool();
    *static_cast<void**>(pointer) = g_commitRangeFreeList;
    g_commitRangeFreeList = pointer;
    unlockCommitRangePool();
}

void micant::mm::VirtualAddressDescriptor::CommitRange::operator delete(
    void* pointer, const std::nothrow_t&) noexcept {
    CommitRange::operator delete(pointer);
}

namespace micant::ex {

[[noreturn]] void stopBootstrapRuntime(const char* message) noexcept {
    stop(message);
}

bool initializeBootstrapCxxRuntime() noexcept {
    if (!g_runtimeInitialized || g_cxxPhase != CxxPhase::Cold) {
        logMessage(g_bootstrapLog, "ExCxx: initialization rejected (heap or phase)\r\n");
        return false;
    }
#if !defined(_WIN32)
    logMessage(g_bootstrapLog, "ExCxx: initialization rejected (requires PE tables)\r\n");
    return false;
#else
    const size_t cCount = initializerCount(g_cInitializersBegin, g_cInitializersEnd);
    const size_t cxxCount = initializerCount(
        g_cxxInitializersBegin, g_cxxInitializersEnd);
#if defined(__MINGW32__)
    if (reinterpret_cast<uintptr_t>(__CTOR_LIST__[0]) != static_cast<uintptr_t>(-1)) {
        stop("ExCxx: invalid GNU PE constructor sentinel; stopping\r\n");
    }
    size_t gnuCount{};
    while (gnuCount < kMaximumInitializers && __CTOR_LIST__[gnuCount + 1]) {
        ++gnuCount;
    }
    if (gnuCount == kMaximumInitializers) {
        stop("ExCxx: unterminated GNU PE constructor table; stopping\r\n");
    }
#endif
    g_cxxPhase = CxxPhase::Initializing;
    for (size_t index = 1; index < cCount; ++index) {
        const auto initializer = initializerAt(g_cInitializersBegin, index);
        if (initializer && initializer() != 0) {
            g_cxxPhase = CxxPhase::Failed;
            logMessage(g_bootstrapLog, "ExCxx: C initializer failed; teardown required\r\n");
            return false;
        }
    }
    for (size_t index = 1; index < cxxCount; ++index) {
        const auto initializer = initializerAt(g_cxxInitializersBegin, index);
        if (initializer) initializer();
    }
#if defined(__MINGW32__)
    for (size_t index = gnuCount; index != 0; --index) {
        __CTOR_LIST__[index]();
    }
#endif
    g_cxxPhase = CxxPhase::Ready;
    logMessage(g_bootstrapLog, "ExCxx: kernel global constructors dispatched\r\n");
    return true;
#endif
}

bool shutdownBootstrapCxxRuntime() noexcept {
    if (g_cxxPhase != CxxPhase::Ready && g_cxxPhase != CxxPhase::Failed) {
        logMessage(g_bootstrapLog, "ExCxx: teardown rejected (phase)\r\n");
        return false;
    }
    g_cxxPhase = CxxPhase::Terminating;
    while (g_destructorCount != 0) {
        const auto destructor = g_destructors[--g_destructorCount];
        g_destructors[g_destructorCount] = nullptr;
        destructor();
    }
    g_cxxPhase = CxxPhase::Terminated;
    logMessage(g_bootstrapLog, "ExCxx: kernel atexit callbacks dispatched\r\n");
    return true;
}

bool initializeBootstrapRuntime(
    BootstrapLogRoutine log,
    BootstrapReserveHeapRangeRoutine reserveRange,
    BootstrapReleaseHeapRangeRoutine releaseRange,
    BootstrapMapHeapPageRoutine mapPage,
    BootstrapUnmapHeapPageRoutine unmapPage
) noexcept {
    if (g_runtimeInitialized) {
        logMessage(g_bootstrapLog,
                   "ExHeap: bootstrap initialization rejected (already initialized)\r\n");
        return false;
    }
    if (!log || !reserveRange || !releaseRange || !mapPage || !unmapPage) {
        logMessage(log, "ExHeap: bootstrap initialization rejected (missing callback)\r\n");
        return false;
    }

    auto& database = PfnDatabase::get();
    if (database.getTotalPages() == 0 ||
        database.getFreeCount() + database.getZeroedCount() == 0) {
        logMessage(log, "ExHeap: bootstrap initialization rejected (PFN database unavailable)\r\n");
        return false;
    }

    g_bootstrapLog = log;
    g_reserveHeapRange = reserveRange;
    g_releaseHeapRange = releaseRange;
    g_mapHeapPage = mapPage;
    g_unmapHeapPage = unmapPage;
    g_runtimeInitialized = true;
    g_bootstrapLog("ExHeap: PFN-backed bootstrap allocator initialized\r\n");
    return true;
}

bool verifyBootstrapRuntime() noexcept {
    requireInitialized();

    auto& database = PfnDatabase::get();
    const size_t initialActive = database.getActiveCount();
    const size_t initialFree = database.getFreeCount();
    const size_t initialZeroed = database.getZeroedCount();
    const size_t initialAvailable = initialFree + initialZeroed;
    const uint64_t initialRejections = g_rejectionLogCount;
    size_t expectedRejections = 0;
    bool okay = true;

    auto* overlap = static_cast<uint8_t*>(malloc(64));
    if (!overlap || (reinterpret_cast<uintptr_t>(overlap) & (kDefaultAlignment - 1))) {
        okay = false;
    }
    if (overlap) {
        if (database.getActiveCount() <= initialActive ||
            database.getFreeCount() + database.getZeroedCount() >= initialAvailable) {
            okay = false;
        }
        auto* bytes = static_cast<volatile uint8_t*>(overlap);
        for (size_t index = 0; index < 64; ++index) bytes[index] = static_cast<uint8_t>(index);
        memmove(overlap + 8, overlap, 32);
        for (size_t index = 0; index < 64; ++index) {
            const uint8_t expected = index >= 8 && index < 40
                ? static_cast<uint8_t>(index - 8)
                : static_cast<uint8_t>(index);
            if (bytes[index] != expected) okay = false;
        }

        for (size_t index = 0; index < 64; ++index) bytes[index] = static_cast<uint8_t>(index);
        memmove(overlap, overlap + 8, 32);
        for (size_t index = 0; index < 64; ++index) {
            const uint8_t expected = index < 32
                ? static_cast<uint8_t>(index + 8)
                : static_cast<uint8_t>(index);
            if (bytes[index] != expected) okay = false;
        }
    }

    auto* zeroed = static_cast<uint8_t*>(calloc(67, 3));
    if (!zeroed || (reinterpret_cast<uintptr_t>(zeroed) & (kDefaultAlignment - 1))) {
        okay = false;
    }
    if (zeroed) {
        auto* bytes = static_cast<volatile uint8_t*>(zeroed);
        for (size_t index = 0; index < 201; ++index) {
            if (bytes[index] != 0) okay = false;
            bytes[index] = static_cast<uint8_t>(0xA5 ^ index);
        }
        for (size_t index = 0; index < 201; ++index) {
            if (bytes[index] != static_cast<uint8_t>(0xA5 ^ index)) okay = false;
        }
    }

    auto* moving = static_cast<uint8_t*>(realloc(nullptr, 96));
    if (!moving) {
        okay = false;
    } else {
        okay = fillAndVerify(moving, 96, 7) && okay;
        auto* grown = static_cast<uint8_t*>(realloc(moving, 256));
        if (!grown) {
            okay = false;
        } else {
            moving = grown;
            auto* bytes = static_cast<volatile uint8_t*>(moving);
            for (size_t index = 0; index < 96; ++index) {
                if (bytes[index] != static_cast<uint8_t>(7 + index * 13)) okay = false;
            }
            for (size_t index = 96; index < 256; ++index) {
                bytes[index] = static_cast<uint8_t>(index ^ 0x5A);
            }
            for (size_t index = 96; index < 256; ++index) {
                if (bytes[index] != static_cast<uint8_t>(index ^ 0x5A)) okay = false;
            }
            auto* shrunk = static_cast<uint8_t*>(realloc(moving, 64));
            if (!shrunk) {
                okay = false;
            } else {
                moving = shrunk;
                auto* shrunkBytes = static_cast<volatile uint8_t*>(moving);
                for (size_t index = 0; index < 64; ++index) {
                    if (shrunkBytes[index] != static_cast<uint8_t>(7 + index * 13)) {
                        okay = false;
                    }
                }
            }
        }

        if (moving) {
            void* rejected = realloc(moving, static_cast<size_t>(-1));
            ++expectedRejections;
            if (rejected) {
                okay = false;
                moving = static_cast<uint8_t*>(rejected);
            } else {
                auto* bytes = static_cast<volatile uint8_t*>(moving);
                for (size_t index = 0; index < 64; ++index) {
                    if (bytes[index] != static_cast<uint8_t>(7 + index * 13)) okay = false;
                }
            }
            void* zeroResult = realloc(moving, 0);
            if (zeroResult) {
                okay = false;
                free(zeroResult);
            }
            moving = nullptr;
        }
    }

    void* ordinary = ::operator new(79);
    if (!ordinary) {
        okay = false;
    } else {
        if (reinterpret_cast<uintptr_t>(ordinary) & (kDefaultAlignment - 1)) okay = false;
        okay = fillAndVerify(ordinary, 79, 19) && okay;
        ::operator delete(ordinary);
    }

    void* array = ::operator new[](93);
    if (!array) {
        okay = false;
    } else {
        if (reinterpret_cast<uintptr_t>(array) & (kDefaultAlignment - 1)) okay = false;
        okay = fillAndVerify(array, 93, 23) && okay;
        ::operator delete[](array);
    }

    constexpr size_t overAlignment = 256;
    void* aligned = ::operator new(
        137, static_cast<std::align_val_t>(overAlignment), std::nothrow_t{});
    if (!aligned) {
        okay = false;
    } else {
        if (reinterpret_cast<uintptr_t>(aligned) & (overAlignment - 1)) okay = false;
        okay = fillAndVerify(aligned, 137, 31) && okay;
        ::operator delete(aligned, static_cast<std::align_val_t>(overAlignment), std::nothrow_t{});
    }

    void* alignedArray = ::operator new[](
        151, static_cast<std::align_val_t>(overAlignment), std::nothrow_t{});
    if (!alignedArray) {
        okay = false;
    } else {
        if (reinterpret_cast<uintptr_t>(alignedArray) & (overAlignment - 1)) okay = false;
        okay = fillAndVerify(alignedArray, 151, 37) && okay;
        ::operator delete[](alignedArray, static_cast<std::align_val_t>(overAlignment),
                            std::nothrow_t{});
    }

    free(overlap);
    free(zeroed);
    free(nullptr);
    if (realloc(nullptr, 0) != nullptr) okay = false;

    void* overflow = malloc(static_cast<size_t>(-1));
    ++expectedRejections;
    if (overflow) {
        okay = false;
        free(overflow);
    }
    void* callocOverflow = calloc(static_cast<size_t>(-1), 2);
    ++expectedRejections;
    if (callocOverflow) {
        okay = false;
        free(callocOverflow);
    }
    void* nothrowOverflow = ::operator new(static_cast<size_t>(-1), std::nothrow_t{});
    ++expectedRejections;
    if (nothrowOverflow) {
        okay = false;
        ::operator delete(nothrowOverflow, std::nothrow_t{});
    }

    if (verifyExhaustionRollback(database, initialAvailable)) {
        ++expectedRejections;
    } else {
        okay = false;
    }

    const size_t finalActive = database.getActiveCount();
    const size_t finalFree = database.getFreeCount();
    const size_t finalZeroed = database.getZeroedCount();
    const size_t finalAvailable = finalFree + finalZeroed;
    if (finalActive != initialActive || finalAvailable != initialAvailable ||
        finalFree < initialFree ||
        g_rejectionLogCount - initialRejections != expectedRejections) {
        okay = false;
    }

    if (okay) {
        g_bootstrapLog("ExHeap: bootstrap runtime self-test passed\r\n");
    } else {
        g_bootstrapLog("ExHeap: bootstrap runtime self-test rejected\r\n");
    }
    return okay;
}

} // namespace micant::ex

// Compiler ABI only; intentionally not a public Windows kernel export.
extern "C" int atexit(void (*callback)()) {
    if (!callback) {
        logMessage(g_bootstrapLog, "ExCxx: null atexit callback rejected\r\n");
        return -1;
    }
    if (!g_runtimeInitialized ||
        (g_cxxPhase != CxxPhase::Initializing && g_cxxPhase != CxxPhase::Ready)) {
        stop("ExCxx: atexit outside live initialization/runtime; stopping\r\n");
    }
    if (g_destructorCount == kMaximumDestructors) {
        stop("ExCxx: destructor registry exhausted; stopping\r\n");
    }
    g_destructors[g_destructorCount++] = callback;
    return 0;
}

extern "C" void* malloc(size_t size) {
    return allocateMemory(size, kDefaultAlignment);
}

extern "C" void* calloc(size_t count, size_t size) {
    requireInitialized();
    if (size != 0 && count > static_cast<size_t>(-1) / size) {
        rejectAllocation("ExHeap: allocation rejected (calloc size overflow)\r\n");
        return nullptr;
    }
    const size_t total = count * size;
    void* memory = allocateMemory(total, kDefaultAlignment);
    if (memory) memset(memory, 0, total);
    return memory;
}

extern "C" void* realloc(void* pointer, size_t size) {
    if (!pointer) {
        return size == 0 ? nullptr : allocateMemory(size, kDefaultAlignment);
    }
    requireInitialized();
    if (size == 0) {
        releaseMemory(pointer);
        return nullptr;
    }

    AllocationHeader* header = validatedHeader(pointer);
    if (!header) stop("ExHeap: invalid realloc rejected; stopping\r\n");
    const size_t oldSize = header->requestedSize;
    const size_t alignment = header->alignment;
    void* replacement = allocateMemory(size, alignment);
    if (!replacement) return nullptr;
    memcpy(replacement, pointer, oldSize < size ? oldSize : size);
    releaseMemory(pointer);
    return replacement;
}

extern "C" void free(void* pointer) {
    releaseMemory(pointer);
}

void* operator new(size_t size) {
    if (void* memory = allocateMemory(size, kDefaultAlignment)) return memory;
    stop("ExHeap: throwing allocation rejected; stopping\r\n");
}

void* operator new[](size_t size) {
    if (void* memory = allocateMemory(size, kDefaultAlignment)) return memory;
    stop("ExHeap: throwing allocation rejected; stopping\r\n");
}

void* operator new(size_t size, std::align_val_t alignment) {
    if (void* memory = allocateMemory(size, static_cast<size_t>(alignment))) return memory;
    stop("ExHeap: throwing aligned allocation rejected; stopping\r\n");
}

void* operator new[](size_t size, std::align_val_t alignment) {
    if (void* memory = allocateMemory(size, static_cast<size_t>(alignment))) return memory;
    stop("ExHeap: throwing aligned allocation rejected; stopping\r\n");
}

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return allocateMemory(size, kDefaultAlignment);
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return allocateMemory(size, kDefaultAlignment);
}

void* operator new(
    size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateMemory(size, static_cast<size_t>(alignment));
}

void* operator new[](
    size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateMemory(size, static_cast<size_t>(alignment));
}

__attribute__((noinline)) void operator delete(void* pointer) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete[](void* pointer) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete(void* pointer, size_t) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete[](void* pointer, size_t) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete(void* pointer, std::align_val_t) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete[](void* pointer, std::align_val_t) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete(void* pointer, size_t, std::align_val_t) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete[](void* pointer, size_t, std::align_val_t) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete(
    void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    releaseMemory(pointer);
}
__attribute__((noinline)) void operator delete[](
    void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    releaseMemory(pointer);
}
