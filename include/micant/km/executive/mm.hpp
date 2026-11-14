#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <optional>
#include <atomic>
#include <limits>
#include <new>
#if !defined(MICANT_FREESTANDING)
#include <exception>
#endif
#include "micant/shared/ntdef.hpp"

namespace micant::mm {

// ============================================================================
// 1. Standard Page & Address Space Constants
// ============================================================================

inline constexpr size_t PageSize4KB   = 4096;
inline constexpr size_t PageShift4KB  = 12;
inline constexpr size_t PageMask4KB   = PageSize4KB - 1;

inline constexpr size_t PageSize2MB   = 2 * 1024 * 1024;
inline constexpr size_t PageShift2MB  = 21;
inline constexpr size_t PageMask2MB   = PageSize2MB - 1;

inline constexpr size_t PageSize1GB   = 1024 * 1024 * 1024;
inline constexpr size_t PageShift1GB  = 30;

// Virtual Memory Allocation Types (MEM_*)
inline constexpr uint32_t MEM_COMMIT      = 0x00001000;
inline constexpr uint32_t MEM_RESERVE     = 0x00002000;
inline constexpr uint32_t MEM_DECOMMIT    = 0x00004000;
inline constexpr uint32_t MEM_RELEASE     = 0x00008000;
inline constexpr uint32_t MEM_FREE        = 0x00010000;
inline constexpr uint32_t MEM_RESET       = 0x00080000;
inline constexpr uint32_t MEM_TOP_DOWN    = 0x00100000;
inline constexpr uint32_t MEM_LARGE_PAGES = 0x20000000;
inline constexpr uint32_t MEM_PHYSICAL    = 0x00400000;

// Memory Page Protection Constants (PAGE_*)
inline constexpr uint32_t PAGE_NOACCESS          = 0x01;
inline constexpr uint32_t PAGE_READONLY          = 0x02;
inline constexpr uint32_t PAGE_READWRITE         = 0x04;
inline constexpr uint32_t PAGE_WRITECOPY         = 0x08;
inline constexpr uint32_t PAGE_EXECUTE           = 0x10;
inline constexpr uint32_t PAGE_EXECUTE_READ      = 0x20;
inline constexpr uint32_t PAGE_EXECUTE_READWRITE = 0x40;
inline constexpr uint32_t PAGE_EXECUTE_WRITECOPY = 0x80;
inline constexpr uint32_t PAGE_GUARD             = 0x100;
inline constexpr uint32_t PAGE_NOCACHE           = 0x200;
inline constexpr uint32_t PAGE_WRITECOMBINE      = 0x400;

// ============================================================================
// 2. Hardware x86-64 Page Table Entry (PTE)
// ============================================================================

union PageTableEntry {
    uint64_t value{0};
    struct {
        uint64_t present : 1;
        uint64_t writable : 1;
        uint64_t user : 1;
        uint64_t writeThrough : 1;
        uint64_t cacheDisable : 1;
        uint64_t accessed : 1;
        uint64_t dirty : 1;
        uint64_t largePage : 1;
        uint64_t global : 1;
        uint64_t available : 3;
        uint64_t pageFrameNumber : 40;
        uint64_t reservedHigh : 11;
        uint64_t noExecute : 1;
    };
    [[nodiscard]] uintptr_t getPhysicalAddress() const noexcept {
        return static_cast<uintptr_t>(pageFrameNumber) << PageShift4KB;
    }

    [[nodiscard]] static PageTableEntry create(uint64_t pfn, bool writable, bool user, bool noExecute) noexcept {
        PageTableEntry pte{};
        pte.present = 1;
        pte.writable = writable ? 1 : 0;
        pte.user = user ? 1 : 0;
        pte.noExecute = noExecute ? 1 : 0;
        pte.pageFrameNumber = pfn;
        return pte;
    }
};
static_assert(sizeof(PageTableEntry) == sizeof(uint64_t));

// ============================================================================
// 3. Physical Page Frame Number (PFN) Database
// ============================================================================

enum class PageLocation : uint8_t {
    ZeroedPageList       = 0,
    FreePageList         = 1,
    StandbyPageList      = 2,
    ModifiedPageList     = 3,
    ModifiedNoWriteList  = 4,
    ActiveAndValid       = 5,
    TransitionPage       = 6,
    BadPageList          = 7
};

struct Mmpfn {
    uint64_t pfn{0};
    uint32_t originalPte{0};
    uintptr_t pteAddress{0};
    uint32_t referenceCount{0};
    PageLocation location{PageLocation::FreePageList};
    bool modified{false};
    bool readInProgress{false};
    uint64_t flink{0};
    uint64_t blink{0};
};

struct PhysicalPageRange {
    uint64_t firstPfn{0};
    uint64_t pageCount{0};
};

using ZeroPhysicalPageRoutine = void (*)(uint64_t pfn) noexcept;

/**
 * @brief Physical Page Frame Number (PFN) Database.
 * Canonical Windows NT physical memory tracker.
 */
class PfnDatabase {
private:
    struct PageList {
        uint64_t head{0};
        uint64_t tail{0};
        size_t count{0};
    };

#if defined(MICANT_FREESTANDING)
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
#else
    mutable std::mutex mutex_;
    std::vector<Mmpfn> ownedEntries_;
#endif
    std::span<Mmpfn> entries_{};
    PageList zeroedList_{};
    PageList freeList_{};
    PageList standbyList_{};
    PageList modifiedList_{};
    size_t activeCount_{0};
    ZeroPhysicalPageRoutine zeroPhysicalPage_{nullptr};

#if defined(MICANT_FREESTANDING)
    constexpr PfnDatabase() noexcept = default;
#else
    PfnDatabase() {
        initialize(262144);
    }
#endif

    class Guard {
    public:
        explicit Guard(const PfnDatabase& database) noexcept
#if defined(MICANT_FREESTANDING)
            : lock_(database.lock_)
#else
            : lock_(database.mutex_)
#endif
        {
#if defined(MICANT_FREESTANDING)
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags_) : : "memory");
#endif
            while (lock_.test_and_set(std::memory_order_acquire)) {}
#endif
        }

        ~Guard() noexcept {
#if defined(MICANT_FREESTANDING)
            lock_.clear(std::memory_order_release);
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            if (flags_ & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
#endif
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
#if defined(MICANT_FREESTANDING)
        std::atomic_flag& lock_;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        uint64_t flags_{};
#endif
#else
        std::lock_guard<std::mutex> lock_;
#endif
    };

    [[nodiscard]] PageList* listFor(PageLocation location) noexcept {
        switch (location) {
            case PageLocation::ZeroedPageList: return &zeroedList_;
            case PageLocation::FreePageList: return &freeList_;
            case PageLocation::StandbyPageList: return &standbyList_;
            case PageLocation::ModifiedPageList: return &modifiedList_;
            default: return nullptr;
        }
    }

    void resetEntriesLocked() noexcept {
        zeroedList_ = {};
        freeList_ = {};
        standbyList_ = {};
        modifiedList_ = {};
        activeCount_ = 0;
        for (size_t i = 0; i < entries_.size(); ++i) {
            entries_[i] = Mmpfn{};
            entries_[i].pfn = static_cast<uint64_t>(i);
            entries_[i].location = PageLocation::BadPageList;
        }
    }

    void insertTailLocked(PageList& list, uint64_t pfn, PageLocation location) noexcept {
        if (pfn == 0 || pfn >= entries_.size()) return;
        auto& entry = entries_[static_cast<size_t>(pfn)];
        entry.location = location;
        entry.blink = list.tail;
        entry.flink = 0;
        if (list.tail != 0) {
            entries_[static_cast<size_t>(list.tail)].flink = pfn;
        } else {
            list.head = pfn;
        }
        list.tail = pfn;
        ++list.count;
    }

    [[nodiscard]] bool removeFromListLocked(uint64_t pfn) noexcept {
        if (pfn == 0 || pfn >= entries_.size()) return false;
        auto& entry = entries_[static_cast<size_t>(pfn)];
        PageList* list = listFor(entry.location);
        if (!list || list->count == 0) return false;

        const uint64_t previous = entry.blink;
        const uint64_t next = entry.flink;
        if (previous != 0) {
            if (previous >= entries_.size()) return false;
            entries_[static_cast<size_t>(previous)].flink = next;
        } else if (list->head == pfn) {
            list->head = next;
        } else {
            return false;
        }
        if (next != 0) {
            if (next >= entries_.size()) return false;
            entries_[static_cast<size_t>(next)].blink = previous;
        } else if (list->tail == pfn) {
            list->tail = previous;
        } else {
            return false;
        }
        entry.flink = 0;
        entry.blink = 0;
        --list->count;
        return true;
    }

    [[nodiscard]] uint64_t popHeadLocked(PageList& list) noexcept {
        const uint64_t pfn = list.head;
        if (pfn != 0) {
            (void)removeFromListLocked(pfn);
        }
        return pfn;
    }

    void releasePageLocked(uint64_t pfn) noexcept {
        if (pfn == 0 || pfn >= entries_.size()) return;
        auto& entry = entries_[static_cast<size_t>(pfn)];
        if (entry.location != PageLocation::ActiveAndValid || entry.referenceCount == 0) return;
        --entry.referenceCount;
        if (entry.referenceCount != 0) return;
        if (activeCount_ > 0) --activeCount_;
        if (entry.modified) {
            insertTailLocked(modifiedList_, pfn, PageLocation::ModifiedPageList);
        } else {
            insertTailLocked(freeList_, pfn, PageLocation::FreePageList);
        }
    }

    [[nodiscard]] bool validBootMemoryRanges(
        std::span<Mmpfn> storage,
        std::span<const PhysicalPageRange> availableRanges,
        ZeroPhysicalPageRoutine zeroPhysicalPage
    ) const noexcept {
        if (storage.empty() || !zeroPhysicalPage) return false;
        const uint64_t storagePages = static_cast<uint64_t>(storage.size());
        for (size_t i = 0; i < availableRanges.size(); ++i) {
            const auto& range = availableRanges[i];
            if (range.pageCount == 0 ||
                range.pageCount > UINT64_MAX - range.firstPfn) {
                return false;
            }
            const uint64_t end = range.firstPfn + range.pageCount;
            if (range.firstPfn >= storagePages || end > storagePages) return false;
            for (size_t j = 0; j < i; ++j) {
                const auto& previous = availableRanges[j];
                const uint64_t previousEnd = previous.firstPfn + previous.pageCount;
                if (range.firstPfn < previousEnd && previous.firstPfn < end) return false;
            }
        }
        return true;
    }

public:
    static PfnDatabase& get() noexcept {
#if defined(MICANT_FREESTANDING)
        static PfnDatabase instance;
        return instance;
#else
        static PfnDatabase* instance = new PfnDatabase();
        return *instance;
#endif
    }

    PfnDatabase(const PfnDatabase&) = delete;
    PfnDatabase& operator=(const PfnDatabase&) = delete;

    void initialize(size_t totalPages) {
#if defined(MICANT_FREESTANDING)
        (void)totalPages;
#else
        Guard guard(*this);
        initializeLocked(totalPages);
#endif
    }

    void initializeLocked(size_t totalPages) {
#if defined(MICANT_FREESTANDING)
        (void)totalPages;
#else
        ownedEntries_.resize(totalPages);
        entries_ = ownedEntries_;
        resetEntriesLocked();
        zeroPhysicalPage_ = nullptr;
        for (uint64_t i = 0; i < totalPages; ++i) {
            if (i < 256) continue;
            if ((i & 1) == 0) {
                insertTailLocked(zeroedList_, i, PageLocation::ZeroedPageList);
            } else {
                insertTailLocked(freeList_, i, PageLocation::FreePageList);
            }
        }
#endif
    }

    [[nodiscard]] NtStatus initializeFromBootMemory(
        std::span<Mmpfn> storage,
        std::span<const PhysicalPageRange> availableRanges,
        ZeroPhysicalPageRoutine zeroPhysicalPage
    ) noexcept {
        Guard guard(*this);
        if (!validBootMemoryRanges(storage, availableRanges, zeroPhysicalPage)) {
            return NtStatus::InvalidParameter;
        }
#if !defined(MICANT_FREESTANDING)
        ownedEntries_.clear();
#endif
        entries_ = storage;
        zeroPhysicalPage_ = zeroPhysicalPage;
        resetEntriesLocked();
        for (const auto& range : availableRanges) {
            const uint64_t end = range.firstPfn + range.pageCount;
            const uint64_t first = range.firstPfn == 0 ? 1 : range.firstPfn;
            for (uint64_t pfn = first; pfn < end; ++pfn) {
                insertTailLocked(freeList_, pfn, PageLocation::FreePageList);
            }
        }
        return NtStatus::Success;
    }

    [[nodiscard]] uint64_t allocateContiguousPages(size_t pageCount,
                                                    uint64_t highestPfn = UINT64_MAX,
                                                    uint64_t alignmentPages = 1) {
        Guard guard(*this);
        if (pageCount == 0 || pageCount > entries_.size() || !alignmentPages ||
            (alignmentPages & (alignmentPages - 1))) return 0;

        size_t consecutive = 0;
        uint64_t candidateStart = 0;

        for (uint64_t i = 1; i < entries_.size() && i <= highestPfn; ++i) {
            if (entries_[i].location == PageLocation::FreePageList ||
                entries_[i].location == PageLocation::ZeroedPageList) {
                if (consecutive == 0 && (i & (alignmentPages - 1))) continue;
                if (consecutive == 0) {
                    candidateStart = i;
                }
                consecutive++;
                if (consecutive == pageCount) {
                    for (uint64_t p = candidateStart; p < candidateStart + pageCount; ++p) {
                        (void)removeFromListLocked(p);
                        entries_[p].location = PageLocation::ActiveAndValid;
                        entries_[p].referenceCount = 1;
                        entries_[p].modified = false;
                        activeCount_++;
                    }
                    return candidateStart;
                }
            } else {
                consecutive = 0;
            }
        }
        return 0;
    }

    void freeContiguousPages(uint64_t startPfn, size_t pageCount) {
        Guard guard(*this);
        if (startPfn == 0 || startPfn >= entries_.size() ||
            pageCount > entries_.size() - static_cast<size_t>(startPfn)) return;
        for (size_t offset = 0; offset < pageCount; ++offset) {
            const auto& entry = entries_[static_cast<size_t>(startPfn) + offset];
            if (entry.location != PageLocation::ActiveAndValid || entry.referenceCount == 0) return;
        }
        for (size_t offset = 0; offset < pageCount; ++offset) {
            releasePageLocked(startPfn + offset);
        }
    }

    size_t flushModifiedPages() {
        Guard guard(*this);
        const size_t count = modifiedList_.count;
        while (modifiedList_.head != 0) {
            const uint64_t pfn = popHeadLocked(modifiedList_);
            if (pfn < entries_.size()) {
                entries_[static_cast<size_t>(pfn)].modified = false;
                insertTailLocked(standbyList_, pfn, PageLocation::StandbyPageList);
            }
        }
        return count;
    }

    void resetForTesting() {
#if defined(MICANT_FREESTANDING)
        return;
#else
        initialize(262144);
#endif
    }

    [[nodiscard]] uint64_t allocatePage(bool preferZeroed = true) {
        Guard guard(*this);
        uint64_t pfn = 0;

        if (preferZeroed && zeroedList_.head != 0) {
            pfn = popHeadLocked(zeroedList_);
        } else if (freeList_.head != 0) {
            pfn = popHeadLocked(freeList_);
        } else if (zeroedList_.head != 0) {
            pfn = popHeadLocked(zeroedList_);
        } else if (standbyList_.head != 0) {
            pfn = popHeadLocked(standbyList_);
        } else {
            return 0; // Out of physical memory (STATUS_NO_MEMORY)
        }

        if (pfn < entries_.size()) {
            entries_[static_cast<size_t>(pfn)].location = PageLocation::ActiveAndValid;
            entries_[static_cast<size_t>(pfn)].referenceCount = 1;
            entries_[static_cast<size_t>(pfn)].modified = false;
            activeCount_++;
        }
        return pfn;
    }

    [[nodiscard]] uint64_t allocatePageInRange(uint64_t firstPfn, uint64_t lastPfn) {
        Guard guard(*this);
        if (!firstPfn || firstPfn > lastPfn || firstPfn >= entries_.size()) return 0;
        if (lastPfn >= entries_.size()) lastPfn = entries_.size() - 1;
        for (uint64_t pfn = firstPfn; pfn <= lastPfn; ++pfn) {
            auto& entry = entries_[static_cast<size_t>(pfn)];
            if (entry.location != PageLocation::FreePageList &&
                entry.location != PageLocation::ZeroedPageList &&
                entry.location != PageLocation::StandbyPageList) continue;
            if (!removeFromListLocked(pfn)) return 0;
            entry.location = PageLocation::ActiveAndValid;
            entry.referenceCount = 1;
            entry.modified = false;
            ++activeCount_;
            return pfn;
        }
        return 0;
    }

    void freePage(uint64_t pfn) {
        Guard guard(*this);
        releasePageLocked(pfn);
    }

    void referencePage(uint64_t pfn) {
        Guard guard(*this);
        if (pfn == 0 || pfn >= entries_.size()) return;
        auto& entry = entries_[static_cast<size_t>(pfn)];
        if (entry.location == PageLocation::BadPageList ||
            entry.referenceCount == UINT32_MAX) return;
        if (entry.referenceCount == 0) {
            if (!removeFromListLocked(pfn)) return;
            entry.location = PageLocation::ActiveAndValid;
            ++activeCount_;
        }
        ++entry.referenceCount;
    }

    void zeroPage(uint64_t pfn) {
        Guard guard(*this);
        if (pfn == 0 || pfn >= entries_.size()) return;
        auto& entry = entries_[static_cast<size_t>(pfn)];
        if (entry.location == PageLocation::BadPageList) return;
#if defined(MICANT_FREESTANDING)
        if (!zeroPhysicalPage_) __builtin_trap();
#endif
        if (zeroPhysicalPage_) zeroPhysicalPage_(pfn);
        entry.modified = false;
        if (entry.referenceCount != 0) return;
        (void)removeFromListLocked(pfn);
        insertTailLocked(zeroedList_, pfn, PageLocation::ZeroedPageList);
    }

    [[nodiscard]] bool zeroAllocatedPage(uint64_t pfn) noexcept {
        Guard guard(*this);
        if (!zeroPhysicalPage_ || pfn == 0 || pfn >= entries_.size() ||
            entries_[static_cast<size_t>(pfn)].location != PageLocation::ActiveAndValid ||
            entries_[static_cast<size_t>(pfn)].referenceCount == 0) return false;
        zeroPhysicalPage_(pfn);
        entries_[static_cast<size_t>(pfn)].modified = false;
        return true;
    }

    void setModified(uint64_t pfn, bool modified) {
        Guard guard(*this);
        if (pfn < entries_.size()) {
            entries_[static_cast<size_t>(pfn)].modified = modified;
        }
    }

    [[nodiscard]] size_t getZeroedCount() const noexcept {
        Guard guard(*this);
        return zeroedList_.count;
    }
    [[nodiscard]] size_t getFreeCount() const noexcept {
        Guard guard(*this);
        return freeList_.count;
    }
    [[nodiscard]] size_t getStandbyCount() const noexcept {
        Guard guard(*this);
        return standbyList_.count;
    }
    [[nodiscard]] size_t getModifiedCount() const noexcept {
        Guard guard(*this);
        return modifiedList_.count;
    }
    [[nodiscard]] size_t getActiveCount() const noexcept {
        Guard guard(*this);
        return activeCount_;
    }
    [[nodiscard]] size_t snapshotActiveEntries(std::span<Mmpfn> destination) const noexcept {
        Guard guard(*this);
        size_t copied = 0;
        size_t total = 0;
        for (const auto& entry : entries_) {
            if (entry.location != PageLocation::ActiveAndValid || entry.referenceCount == 0) continue;
            if (copied < destination.size()) destination[copied++] = entry;
            ++total;
        }
        return total;
    }
    [[nodiscard]] size_t getTotalPages() const noexcept {
        Guard guard(*this);
        return entries_.size();
    }
};

// ============================================================================
// 4. Virtual Address Descriptor (VAD) & Balanced AVL Tree
// ============================================================================

/**
 * @brief Virtual Address Descriptor (VAD).
 * Represents a contiguous range of committed or reserved virtual memory in an NT process.
 */
struct VirtualAddressDescriptor {
    struct CommitRange {
        uint64_t firstVpn{0};
        uint64_t lastVpn{0};
        uint64_t pfn{0};
        uint32_t protection{PAGE_NOACCESS};
        uint32_t maximumProtection{PAGE_NOACCESS};
        bool ownsPfn{false};
        bool copyOnWriteArmed{false};
        bool sharedImagePage{false};
        uint64_t sharedImagePfn{0};
        std::unique_ptr<CommitRange> next;
    };
    uintptr_t startingAddress{0};
    uintptr_t endingAddress{0};
    uint64_t  startingVpn{0};
    uint64_t  endingVpn{0};
    uint32_t  protection{PAGE_NOACCESS};
    uint32_t  allocationType{MEM_RESERVE};
    bool      committed{false};
    bool      isCopyOnWrite{false};
    bool      isGuardPage{false};
    bool      isSectionView{false};
    bool      rollbackPending{false};
    bool      isImageView{false};
    bool      privateImageView{false};
    bool      ownsImagePages{false};
    bool      inheritView{false};
    uint32_t  sectionMaxProtection{PAGE_NOACCESS};
    void*     sectionContext{nullptr};
    bool (*sectionAcquire)(void*) noexcept{nullptr};
    void (*sectionRelease)(void*) noexcept{nullptr};
#if !defined(MICANT_FREESTANDING)
    std::shared_ptr<void> sectionOwner;
#endif
    uint64_t  sectionOffset{0};
    size_t    commitCharge{0};

    // AVL Tree Topology
    VirtualAddressDescriptor* parent{nullptr};
    std::unique_ptr<VirtualAddressDescriptor> left{nullptr};
    std::unique_ptr<VirtualAddressDescriptor> right{nullptr};
    int height{1};

    // Demand-Paged Physical Page Frame Map (VPN -> PFN)
    std::unordered_map<uint64_t, uint64_t> physicalPages;
    std::unique_ptr<CommitRange> commitRanges;

    ~VirtualAddressDescriptor() {
        if (sectionRelease) sectionRelease(sectionContext);
    }

    [[nodiscard]] size_t size() const noexcept {
        return endingAddress >= startingAddress ? (endingAddress - startingAddress + 1) : 0;
    }

    [[nodiscard]] size_t pageCount() const noexcept {
        return (size() + PageMask4KB) / PageSize4KB;
    }

    [[nodiscard]] bool contains(uintptr_t addr) const noexcept {
        return addr >= startingAddress && addr <= endingAddress;
    }

    [[nodiscard]] bool containsVpn(uint64_t vpn) const noexcept {
        return vpn >= startingVpn && vpn <= endingVpn;
    }
};

/**
 * @brief Balanced AVL Tree for Fast O(log N) Virtual Address Descriptor Lookups.
 * Clean-room implementation of Dave Cutler's MM_AVL_TABLE.
 */
class MmAvlTable {
private:
    std::unique_ptr<VirtualAddressDescriptor> root_{nullptr};
    size_t count_{0};
#if defined(MICANT_FREESTANDING)
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
#else
    mutable std::mutex mutex_;
#endif

    class Guard {
    public:
        explicit Guard(const MmAvlTable& table) noexcept
#if defined(MICANT_FREESTANDING)
            : lock_(table.lock_)
#else
            : lock_(table.mutex_)
#endif
        {
#if defined(MICANT_FREESTANDING)
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags_) : : "memory");
#endif
            while (lock_.test_and_set(std::memory_order_acquire)) {}
#endif
        }
        ~Guard() noexcept {
#if defined(MICANT_FREESTANDING)
            lock_.clear(std::memory_order_release);
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            if (flags_ & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
#endif
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    private:
#if defined(MICANT_FREESTANDING)
        std::atomic_flag& lock_;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        uint64_t flags_{};
#endif
#else
        std::lock_guard<std::mutex> lock_;
#endif
    };

    static int getHeight(const VirtualAddressDescriptor* node) noexcept {
        return node ? node->height : 0;
    }

    static int getBalanceFactor(const VirtualAddressDescriptor* node) noexcept {
        return node ? (getHeight(node->left.get()) - getHeight(node->right.get())) : 0;
    }

    static void updateHeight(VirtualAddressDescriptor* node) noexcept {
        if (node) {
            node->height = 1 + std::max(getHeight(node->left.get()), getHeight(node->right.get()));
        }
    }

    static std::unique_ptr<VirtualAddressDescriptor> rotateRight(std::unique_ptr<VirtualAddressDescriptor> y) {
        if (!y || !y->left) return y;
        auto x = std::move(y->left);
        y->left = std::move(x->right);
        if (y->left) y->left->parent = y.get();

        x->parent = y->parent;
        y->parent = x.get();
        x->right = std::move(y);

        updateHeight(x->right.get());
        updateHeight(x.get());
        return x;
    }

    static std::unique_ptr<VirtualAddressDescriptor> rotateLeft(std::unique_ptr<VirtualAddressDescriptor> x) {
        if (!x || !x->right) return x;
        auto y = std::move(x->right);
        x->right = std::move(y->left);
        if (x->right) x->right->parent = x.get();

        y->parent = x->parent;
        x->parent = y.get();
        y->left = std::move(x);

        updateHeight(y->left.get());
        updateHeight(y.get());
        return y;
    }

    std::unique_ptr<VirtualAddressDescriptor> insertNode(
        std::unique_ptr<VirtualAddressDescriptor> node,
        std::unique_ptr<VirtualAddressDescriptor> newNode
    ) {
        if (!node) {
            count_++;
            return newNode;
        }

        uint64_t newVpn = newNode->startingVpn;

        if (newVpn < node->startingVpn) {
            newNode->parent = node.get();
            node->left = insertNode(std::move(node->left), std::move(newNode));
        } else {
            newNode->parent = node.get();
            node->right = insertNode(std::move(node->right), std::move(newNode));
        }

        updateHeight(node.get());
        int balance = getBalanceFactor(node.get());

        // Left-Left Case
        if (balance > 1 && node->left && newVpn < node->left->startingVpn) {
            return rotateRight(std::move(node));
        }
        // Right-Right Case
        if (balance < -1 && node->right && newVpn > node->right->startingVpn) {
            return rotateLeft(std::move(node));
        }
        // Left-Right Case
        if (balance > 1 && node->left && newVpn > node->left->startingVpn) {
            node->left = rotateLeft(std::move(node->left));
            return rotateRight(std::move(node));
        }
        // Right-Left Case
        if (balance < -1 && node->right && newVpn < node->right->startingVpn) {
            node->right = rotateRight(std::move(node->right));
            return rotateLeft(std::move(node));
        }

        return node;
    }

    static VirtualAddressDescriptor* findMin(VirtualAddressDescriptor* node) noexcept {
        while (node && node->left) {
            node = node->left.get();
        }
        return node;
    }

    static const VirtualAddressDescriptor* nextNode(const VirtualAddressDescriptor* node) noexcept {
        if (node->right) return findMin(node->right.get());
        const auto* parent = node->parent;
        while (parent && node == parent->right.get()) {
            node = parent;
            parent = parent->parent;
        }
        return parent;
    }

    static std::unique_ptr<VirtualAddressDescriptor> rebalance(
        std::unique_ptr<VirtualAddressDescriptor> node
    ) {
        updateHeight(node.get());
        const int balance = getBalanceFactor(node.get());
        if (balance > 1) {
            if (getBalanceFactor(node->left.get()) < 0)
                node->left = rotateLeft(std::move(node->left));
            return rotateRight(std::move(node));
        }
        if (balance < -1) {
            if (getBalanceFactor(node->right.get()) > 0)
                node->right = rotateRight(std::move(node->right));
            return rotateLeft(std::move(node));
        }
        return node;
    }

    static std::unique_ptr<VirtualAddressDescriptor> detachMin(
        std::unique_ptr<VirtualAddressDescriptor>& subtree
    ) {
        if (!subtree->left) {
            auto minimum = std::move(subtree);
            subtree = std::move(minimum->right);
            if (subtree) subtree->parent = minimum->parent;
            return minimum;
        }
        auto minimum = detachMin(subtree->left);
        subtree = rebalance(std::move(subtree));
        return minimum;
    }

    std::unique_ptr<VirtualAddressDescriptor> removeNode(
        std::unique_ptr<VirtualAddressDescriptor> node,
        uintptr_t startingAddress,
        bool& removed
    ) {
        if (!node) return nullptr;

        uint64_t targetVpn = startingAddress >> PageShift4KB;
        if (targetVpn < node->startingVpn) {
            node->left = removeNode(std::move(node->left), startingAddress, removed);
        } else if (targetVpn > node->startingVpn) {
            node->right = removeNode(std::move(node->right), startingAddress, removed);
        } else {
            // Node found
            removed = true;

            if (!node->left || !node->right) {
                if (count_ > 0) count_--;
                auto temp = node->left ? std::move(node->left) : std::move(node->right);
                if (temp) temp->parent = node->parent;
                return temp;
            }

            // Relink the successor itself: surviving VADs keep their identity,
            // section ownership and PFN-backed descriptor allocation.
            auto successor = detachMin(node->right);
            successor->parent = node->parent;
            successor->left = std::move(node->left);
            successor->left->parent = successor.get();
            successor->right = std::move(node->right);
            if (successor->right) successor->right->parent = successor.get();
            node = std::move(successor);
            if (count_ > 0) count_--;
        }

        return rebalance(std::move(node));
    }

    void collectInOrder(const VirtualAddressDescriptor* node, std::vector<VirtualAddressDescriptor*>& out) const {
        if (!node) return;
        collectInOrder(node->left.get(), out);
        out.push_back(const_cast<VirtualAddressDescriptor*>(node));
        collectInOrder(node->right.get(), out);
    }

public:
    MmAvlTable() = default;

    [[nodiscard]] std::vector<VirtualAddressDescriptor*> inOrder() const {
        Guard lock(*this);
        std::vector<VirtualAddressDescriptor*> result;
        collectInOrder(root_.get(), result);
        return result;
    }

    bool insert(std::unique_ptr<VirtualAddressDescriptor>&& vad) {
        Guard lock(*this);
        if (!vad || vad->startingAddress > vad->endingAddress ||
            (vad->startingAddress & PageMask4KB) != 0 ||
            (vad->endingAddress & PageMask4KB) != PageMask4KB) return false;
        const auto* curr = root_.get();
        while (curr) {
            if (vad->endingAddress < curr->startingAddress) {
                curr = curr->left.get();
            } else if (vad->startingAddress > curr->endingAddress) {
                curr = curr->right.get();
            } else {
                return false;
            }
        }
        vad->startingVpn = vad->startingAddress >> PageShift4KB;
        vad->endingVpn = vad->endingAddress >> PageShift4KB;
        root_ = insertNode(std::move(root_), std::move(vad));
        return true;
    }

    bool remove(uintptr_t startingAddress) {
        Guard lock(*this);
        bool removed = false;
        root_ = removeNode(std::move(root_), startingAddress, removed);
        return removed;
    }

    [[nodiscard]] VirtualAddressDescriptor* find(uintptr_t address) noexcept {
        Guard lock(*this);
        VirtualAddressDescriptor* curr = root_.get();
        uint64_t vpn = address >> PageShift4KB;

        while (curr) {
            if (curr->contains(address)) {
                return curr;
            }
            if (vpn < curr->startingVpn) {
                curr = curr->left.get();
            } else {
                curr = curr->right.get();
            }
        }
        return nullptr;
    }

    [[nodiscard]] const VirtualAddressDescriptor* find(uintptr_t address) const noexcept {
        Guard lock(*this);
        const VirtualAddressDescriptor* curr = root_.get();
        uint64_t vpn = address >> PageShift4KB;

        while (curr) {
            if (curr->contains(address)) {
                return curr;
            }
            if (vpn < curr->startingVpn) {
                curr = curr->left.get();
            } else {
                curr = curr->right.get();
            }
        }
        return nullptr;
    }

    [[nodiscard]] uintptr_t nextStartingAddress(uintptr_t address) const noexcept {
        Guard lock(*this);
        const VirtualAddressDescriptor* current = root_.get();
        uintptr_t next{};
        while (current) {
            if (current->startingAddress > address) {
                next = current->startingAddress;
                current = current->left.get();
            } else {
                current = current->right.get();
            }
        }
        return next;
    }

    [[nodiscard]] uintptr_t findFreeRange(
        size_t size,
        size_t alignment = PageSize4KB,
        bool topDown = false,
        uintptr_t minAddr = 0x0000000000010000ULL,
        uintptr_t maxAddr = 0x00007FFFFFFFFFFFULL
    ) const {
        Guard lock(*this);

        const size_t align = std::max(alignment, PageSize4KB);
        if (size == 0 || size > std::numeric_limits<size_t>::max() - PageMask4KB ||
            (align & (align - 1)) != 0 || minAddr > maxAddr) return 0;
        const size_t alignedSize = (size + PageMask4KB) & ~PageMask4KB;
        const uintptr_t maxValue = std::numeric_limits<uintptr_t>::max();
        auto fit = [&](uintptr_t low, uintptr_t high) -> uintptr_t {
            if (low > high || alignedSize - 1 > high - low) return 0;
            if (topDown) {
                const uintptr_t start = (high - (alignedSize - 1)) & ~(align - 1);
                return start >= low ? start : 0;
            }
            if (low > maxValue - (align - 1)) return 0;
            const uintptr_t start = (low + align - 1) & ~(align - 1);
            return start <= high && alignedSize - 1 <= high - start ? start : 0;
        };

        uintptr_t low = minAddr;
        uintptr_t best = 0;
        for (const auto* vad = findMin(root_.get()); vad; vad = nextNode(vad)) {
            if (vad->endingAddress < low) continue;
            if (vad->startingAddress > maxAddr) break;
            if (vad->startingAddress > low) {
                uintptr_t candidate = fit(low, vad->startingAddress - 1);
                if (candidate) {
                    if (!topDown) return candidate;
                    best = candidate;
                }
            }
            if (vad->endingAddress >= maxAddr) return best;
            low = vad->endingAddress + 1;
        }
        uintptr_t candidate = fit(low, maxAddr);
        return candidate ? candidate : best;
    }

    [[nodiscard]] size_t size() const noexcept {
        Guard lock(*this);
        return count_;
    }

    [[nodiscard]] uintptr_t firstAddress() const noexcept {
        Guard lock(*this);
        const auto* node = findMin(root_.get());
        return node ? node->startingAddress : 0;
    }

    [[nodiscard]] uintptr_t nextAddress(uintptr_t current) const noexcept {
        Guard lock(*this);
        const VirtualAddressDescriptor* candidate{};
        auto* node = root_.get();
        while (node) {
            if (node->startingAddress > current) { candidate = node; node = node->left.get(); }
            else node = node->right.get();
        }
        return candidate ? candidate->startingAddress : 0;
    }

    void clear() {
        Guard lock(*this);
        root_.reset();
        count_ = 0;
    }
};

// ============================================================================
// 5. Working Set Metrics (MMWSL)
// ============================================================================

struct WorkingSetList {
    size_t residentPages{0};
    size_t peakResidentPages{0};
    size_t commitCharge{0};
    size_t peakCommitCharge{0};
    size_t workingSetMinimum{50};
    size_t workingSetMaximum{1000};
};

// ============================================================================
// 6. Hardware Paging Engine (4-Level PML4 Table Simulation)
// ============================================================================

class HardwarePagingEngine {
private:
    mutable std::mutex mutex_;
    // Simulated Page Directories: CR3 -> (VA -> PTE)
    std::unordered_map<uintptr_t, std::unordered_map<uintptr_t, PageTableEntry>> pageTables_;

public:
    HardwarePagingEngine() = default;

    void mapPage(uintptr_t cr3, uintptr_t va, uint64_t pfn, uint32_t protect, bool user = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        uintptr_t pageVa = va & ~PageMask4KB;
        bool writable = (protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) != 0;
        bool noExec   = (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0;

        PageTableEntry pte = PageTableEntry::create(pfn, writable, user, noExec);
        pageTables_[cr3][pageVa] = pte;
    }

    void unmapPage(uintptr_t cr3, uintptr_t va) {
        std::lock_guard<std::mutex> lock(mutex_);
        uintptr_t pageVa = va & ~PageMask4KB;
        auto it = pageTables_.find(cr3);
        if (it != pageTables_.end()) {
            it->second.erase(pageVa);
        }
    }

    [[nodiscard]] std::optional<PageTableEntry> queryPte(uintptr_t cr3, uintptr_t va) const {
        std::lock_guard<std::mutex> lock(mutex_);
        uintptr_t pageVa = va & ~PageMask4KB;
        auto it = pageTables_.find(cr3);
        if (it != pageTables_.end()) {
            auto pit = it->second.find(pageVa);
            if (pit != it->second.end()) {
                return pit->second;
            }
        }
        return std::nullopt;
    }

    void modifyProtection(uintptr_t cr3, uintptr_t va, uint32_t newProtect) {
        std::lock_guard<std::mutex> lock(mutex_);
        uintptr_t pageVa = va & ~PageMask4KB;
        auto it = pageTables_.find(cr3);
        if (it != pageTables_.end()) {
            auto pit = it->second.find(pageVa);
            if (pit != it->second.end()) {
                pit->second.writable = (newProtect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) ? 1 : 0;
                pit->second.noExecute = (newProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ? 0 : 1;
            }
        }
    }
};

// ============================================================================
// 7. Process Virtual Address Space & Demand Paging
// ============================================================================

/**
 * @brief Memory Manager address space abstraction.
 * Integrates VAD AVL trees, physical PFN database, working set quotas, and demand-paging.
 */
class ProcessAddressSpace {
public:
    static constexpr uintptr_t UserSpaceMin = 0x0000000000010000ULL;
    static constexpr uintptr_t UserSpaceMax = 0x00007FFFFFFFFFFFULL;
    static constexpr size_t AllocationGranularity = 64 * 1024;

    struct PagingBackend {
        void* context{nullptr};
        // A failed operation must leave the page mapping unchanged.
        bool (*map)(void*, uintptr_t, uint64_t, uint32_t) noexcept {nullptr};
        bool (*unmap)(void*, uintptr_t) noexcept {nullptr};
        bool (*protect)(void*, uintptr_t, uint32_t) noexcept {nullptr};
        // Failure must leave every leaf unmapped; success maps the whole view.
        bool (*mapBatch)(void*, uintptr_t, const uint64_t*, size_t, uint32_t) noexcept {nullptr};
        // Failure must leave the entire view mapped.
        bool (*unmapBatch)(void*, uintptr_t, size_t) noexcept {nullptr};
        // Change only the hardware guard state of an already committed page.
        bool (*guard)(void*, uintptr_t, bool) noexcept {nullptr};
        // Replace a present user leaf atomically, only if it still references
        // the expected PFN; failure leaves the original mapping unchanged.
        bool (*replace)(void*, uintptr_t, uint64_t, uint64_t, uint32_t) noexcept {nullptr};
    };

    struct QueryRegion {
        uintptr_t baseAddress{};
        uintptr_t allocationBase{};
        size_t regionSize{};
        uint32_t allocationProtect{};
        uint32_t state{};
        uint32_t protect{};
        uint32_t type{};
    };

    ProcessAddressSpace() : nextFreeAddress_(0x0000000100000000ULL) {}

    // The backend and PFN database must outlive this address space.
    [[nodiscard]] bool configurePagingBackend(PagingBackend backend, uintptr_t base, size_t size) noexcept {
        Guard lock(*this);
        if (backend_.map || vadTree_.size() != 0 || !backend.map || !backend.unmap ||
            base < UserSpaceMin || (base & PageMask4KB) || size == 0 ||
            (size & PageMask4KB) || base > UserSpaceMax || size - 1 > UserSpaceMax - base)
            return false;
        backend_ = backend;
        backendBase_ = base;
        backendEnd_ = base + size - 1;
        nextFreeAddress_ = base;
        return true;
    }

    ~ProcessAddressSpace() {
        if (!backend_.map || close() == NtStatus::Success) return;
        // Destruction cannot preserve ownership of mappings that failed cleanup.
        ownershipInvariantFailure();
    }

    struct DeferredOwnerRelease {
        void* context{};
        void (*release)(void*) noexcept{};
        uintptr_t startingAddress{};
#if !defined(MICANT_FREESTANDING)
        std::shared_ptr<void> hostedOwner;
#endif
        DeferredOwnerRelease() = default;
        DeferredOwnerRelease(const DeferredOwnerRelease&) = delete;
        DeferredOwnerRelease& operator=(const DeferredOwnerRelease&) = delete;
        DeferredOwnerRelease(DeferredOwnerRelease&& other) noexcept
            : context(other.context), release(other.release), startingAddress(other.startingAddress)
#if !defined(MICANT_FREESTANDING)
            , hostedOwner(std::move(other.hostedOwner))
#endif
        {
            other.context = nullptr;
            other.release = nullptr;
            other.startingAddress = 0;
        }
        DeferredOwnerRelease& operator=(DeferredOwnerRelease&& other) noexcept {
            if (this == &other) return *this;
            releaseNow();
            context = other.context;
            release = other.release;
            startingAddress = other.startingAddress;
#if !defined(MICANT_FREESTANDING)
            hostedOwner = std::move(other.hostedOwner);
#endif
            other.context = nullptr;
            other.release = nullptr;
            other.startingAddress = 0;
            return *this;
        }
        ~DeferredOwnerRelease() { releaseNow(); }
        void releaseNow() noexcept {
            auto* savedContext = context;
            auto savedRelease = release;
            context = nullptr;
            release = nullptr;
            startingAddress = 0;
            if (savedRelease) savedRelease(savedContext);
#if !defined(MICANT_FREESTANDING)
            hostedOwner.reset();
#endif
        }
    };

    // Remove one lowest VAD and transfer its owner reference to the caller.
    // The caller releases it after leaving Mm locks and the target CR3.
    [[nodiscard]] NtStatus closeOne(DeferredOwnerRelease& owner, bool& done) noexcept {
        Guard lock(*this);
        if (owner.context || owner.release || owner.startingAddress
#if !defined(MICANT_FREESTANDING)
            || owner.hostedOwner
#endif
        ) ownershipInvariantFailure();
        done = false;
        if (!backend_.map) return NtStatus::NotImplemented;
        const uintptr_t base = vadTree_.firstAddress();
        if (!base) { done = true; return NtStatus::Success; }
        auto* vad = vadTree_.find(base);
        if (!vad) ownershipInvariantFailure();
        if (!releaseBackendPages(*vad)) return NtStatus::Unsuccessful;
        deferSectionOwner(*vad, owner);
        owner.startingAddress = base;
        // The lowest node has no left child; removal cannot replace it with a
        // different VAD or release another owner's reference.
        if (!vadTree_.remove(base)) ownershipInvariantFailure();
        return NtStatus::Success;
    }

    [[nodiscard]] NtStatus close() noexcept {
        if (!backend_.map) return NtStatus::NotImplemented;
        for (;;) {
            DeferredOwnerRelease owner;
            bool done{};
            const auto status = closeOne(owner, done);
            owner.releaseNow();
            if (status != NtStatus::Success) return status;
            if (done) return NtStatus::Success;
        }
    }

    NtStatus allocate(uintptr_t& baseAddress, size_t size, uint32_t allocationType, uint32_t protect) {
        Guard lock(*this);
        if (backend_.map) return allocateBackend(baseAddress, size, allocationType, protect);
#if defined(MICANT_FREESTANDING)
        return NtStatus::NotImplemented;
#endif
        if (size == 0 || size > std::numeric_limits<size_t>::max() - PageMask4KB)
            return NtStatus::InvalidParameter;
        if ((allocationType & ~(MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN)) != 0 ||
            !(allocationType & (MEM_COMMIT | MEM_RESERVE)))
            return NtStatus::NotImplemented;
        if (!(allocationType & MEM_RESERVE) && baseAddress != 0)
            return NtStatus::NotImplemented;
        if (baseAddress != 0 && (baseAddress & PageMask4KB))
            return NtStatus::NotImplemented;

        size_t alignedSize = (size + PageMask4KB) & ~PageMask4KB;

        uintptr_t targetAddr = baseAddress;
        if (targetAddr == 0) {
            bool topDown = (allocationType & MEM_TOP_DOWN) != 0;
            targetAddr = vadTree_.findFreeRange(alignedSize, AllocationGranularity, topDown, nextFreeAddress_, UserSpaceMax);
            if (targetAddr == 0) {
                targetAddr = vadTree_.findFreeRange(alignedSize, AllocationGranularity, topDown, UserSpaceMin, UserSpaceMax);
            }
            if (targetAddr == 0) {
                return NtStatus::NoMemory;
            }
        }
        if (targetAddr < UserSpaceMin || targetAddr > UserSpaceMax ||
            alignedSize - 1 > UserSpaceMax - targetAddr)
            return NtStatus::InvalidParameter;

        auto vad = std::make_unique<VirtualAddressDescriptor>();
        vad->startingAddress = targetAddr;
        vad->endingAddress = targetAddr + alignedSize - 1;
        vad->protection = protect;
        vad->allocationType = allocationType;
        vad->committed = (allocationType & MEM_COMMIT) != 0;
        vad->isCopyOnWrite = (protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_WRITECOPY);
        vad->isGuardPage = (protect & PAGE_GUARD) != 0;
        vad->commitCharge = vad->committed ? vad->pageCount() : 0;

        const size_t commitCharge = vad->commitCharge;
        if (!vadTree_.insert(std::move(vad))) return NtStatus::Conflict;
        if (commitCharge) {
            workingSet_.commitCharge += commitCharge;
            workingSet_.peakCommitCharge = std::max(workingSet_.peakCommitCharge, workingSet_.commitCharge);
        }

        if (baseAddress == 0)
            nextFreeAddress_ = targetAddr + alignedSize <= UserSpaceMax
                ? targetAddr + alignedSize : UserSpaceMin;
        baseAddress = targetAddr;
        return NtStatus::Success;
    }

    NtStatus mapSectionView(
        uintptr_t& baseAddress,
        size_t size,
        uint32_t protect,
        const std::unordered_map<size_t, uint64_t>& sharedPfns,
        void* sectionContext = nullptr,
        uint64_t sectionOffset = 0,
        std::shared_ptr<void> sectionOwner = {}
    ) {
        DeferredOwnerRelease deferredOwner;
        Guard lock(*this);
        if (backend_.map) return NtStatus::NotImplemented;
#if defined(MICANT_FREESTANDING)
        return NtStatus::NotImplemented;
#endif
        if (size == 0 || size > std::numeric_limits<size_t>::max() - PageMask4KB)
            return NtStatus::InvalidParameter;

        size_t alignedSize = (size + PageMask4KB) & ~PageMask4KB;
        uintptr_t targetAddr = baseAddress;

        if (targetAddr == 0) {
            targetAddr = vadTree_.findFreeRange(alignedSize, PageSize4KB, false, nextFreeAddress_, UserSpaceMax);
            if (!targetAddr)
                targetAddr = vadTree_.findFreeRange(alignedSize, PageSize4KB, false, UserSpaceMin, UserSpaceMax);
            if (!targetAddr) return NtStatus::NoMemory;
        } else {
            if (targetAddr & PageMask4KB) return NtStatus::InvalidParameter;
        }
        if (targetAddr < UserSpaceMin || targetAddr > UserSpaceMax ||
            alignedSize - 1 > UserSpaceMax - targetAddr)
            return NtStatus::InvalidParameter;

        auto vad = std::make_unique<VirtualAddressDescriptor>();
        vad->startingAddress = targetAddr;
        vad->startingVpn = targetAddr >> PageShift4KB;
        vad->endingVpn = (targetAddr + alignedSize - 1) >> PageShift4KB;
        vad->endingAddress = targetAddr + alignedSize - 1;
        vad->protection = protect;
        vad->allocationType = MEM_COMMIT;
        vad->committed = true;
        vad->isCopyOnWrite = (protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_WRITECOPY);
        vad->isGuardPage = false;
        vad->commitCharge = vad->pageCount();
        vad->isSectionView = true;
        vad->sectionContext = sectionContext;
#if !defined(MICANT_FREESTANDING)
        vad->sectionOwner = std::move(sectionOwner);
#else
        (void)sectionOwner;
#endif
        vad->sectionOffset = sectionOffset;

        auto& pfnDb = PfnDatabase::get();
        size_t pageOffset = sectionOffset >> PageShift4KB;
        size_t numPages = alignedSize >> PageShift4KB;
        if (numPages - 1 > std::numeric_limits<size_t>::max() - pageOffset)
            return NtStatus::InvalidParameter;

        auto* mappedVad = vad.get();
        if (!vadTree_.insert(std::move(vad))) {
            deferSectionOwner(*vad, deferredOwner);
            return NtStatus::Conflict;
        }

        for (size_t i = 0; i < numPages; ++i) {
            uint64_t vpn = mappedVad->startingVpn + i;
            auto it = sharedPfns.find(pageOffset + i);
            if (it != sharedPfns.end()) {
                uint64_t pfn = it->second;
                mappedVad->physicalPages[vpn] = pfn;
                pfnDb.referencePage(pfn);
                pagingEngine_.mapPage(directoryTableBase_, vpn << PageShift4KB, pfn, protect, true);
                workingSet_.residentPages++;
            }
        }

        workingSet_.commitCharge += mappedVad->commitCharge;
        workingSet_.peakCommitCharge = std::max(workingSet_.peakCommitCharge, workingSet_.commitCharge);
        workingSet_.peakResidentPages = std::max(workingSet_.peakResidentPages, workingSet_.residentPages);

        if (baseAddress == 0)
            nextFreeAddress_ = targetAddr + alignedSize <= UserSpaceMax
                ? targetAddr + alignedSize : UserSpaceMin;
        baseAddress = targetAddr;
        return NtStatus::Success;
    }

    // The caller holds a backing reference while this method acquires its own.
    // This is an internal paging operation, not the NtMapViewOfSection contract.
    NtStatus mapSharedPages(uintptr_t& baseAddress, size_t size, uint32_t protect,
                            const uint64_t* pfns, size_t pageCount, void* owner,
                            bool (*acquire)(void*) noexcept,
                            void (*release)(void*) noexcept,
                            uint32_t maxProtection = PAGE_NOACCESS,
                            bool imageView = false, bool inheritView = false,
                            uint64_t sectionOffset = 0,
                            const uint32_t* pageProtections = nullptr,
                            bool privateImageView = false,
                            const uint32_t* pageMaximumProtections = nullptr,
                            const uint8_t* sharedImagePages = nullptr) noexcept {
        // One prior failed mapping may be recovered before this operation;
        // this operation may then remove its own VAD on rollback. Keep both
        // references alive until the Guard has restored its lock/IRQL state.
        DeferredOwnerRelease recoveredOwner;
        DeferredOwnerRelease deferredOwner;
        Guard lock(*this);
        if (!backend_.map) return NtStatus::NotImplemented;
        if (!recoverFailedSectionMapping(recoveredOwner)) return NtStatus::Unsuccessful;
        if (!size || size > std::numeric_limits<size_t>::max() - PageMask4KB ||
            !pfns || !owner || !acquire || !release) return NtStatus::InvalidParameter;
        if (protect != PAGE_READONLY && protect != PAGE_READWRITE &&
            !(imageView && (protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE)))
            return NtStatus::NotImplemented;
        if (maxProtection == PAGE_NOACCESS) maxProtection = protect;
        if ((maxProtection != PAGE_READONLY && maxProtection != PAGE_READWRITE &&
             !(imageView && (maxProtection == PAGE_EXECUTE_READ || maxProtection == PAGE_EXECUTE_READWRITE))) ||
            (protect == PAGE_READWRITE && maxProtection != PAGE_READWRITE &&
             !privateImageView && !imageView))
            return NtStatus::SectionProtection;
        const size_t length = (size + PageMask4KB) & ~PageMask4KB;
        const size_t count = length >> PageShift4KB;
        if (count > pageCount) return NtStatus::InvalidParameter;
        if (pageProtections) {
            for (size_t i = 0; i < count; ++i) {
                const auto pageProtection = pageProtections[i];
                if (pageProtection != PAGE_READONLY && pageProtection != PAGE_READWRITE &&
                    pageProtection != PAGE_EXECUTE_READ && pageProtection != PAGE_EXECUTE_READWRITE &&
                    !(imageView && pageProtection == PAGE_NOACCESS)) return NtStatus::InvalidPageProtection;
                if (!imageView && (pageProtection == PAGE_EXECUTE_READ || pageProtection == PAGE_EXECUTE_READWRITE))
                    return NtStatus::InvalidPageProtection;
            }
        }
        if (pageMaximumProtections) {
            for (size_t i = 0; i < count; ++i) {
                const auto pageProtection = pageMaximumProtections[i];
                if (pageProtection != PAGE_READONLY && pageProtection != PAGE_READWRITE &&
                    pageProtection != PAGE_EXECUTE_READ && pageProtection != PAGE_EXECUTE_READWRITE &&
                    !(imageView && pageProtection == PAGE_NOACCESS)) return NtStatus::InvalidPageProtection;
                if (!imageView && (pageProtection == PAGE_EXECUTE_READ || pageProtection == PAGE_EXECUTE_READWRITE))
                    return NtStatus::InvalidPageProtection;
            }
        }
        if (!privateImageView) {
            for (size_t i = 0; i < count; ++i) {
                const uint32_t maximum = pageMaximumProtections ? pageMaximumProtections[i] :
                    (pageProtections ? pageProtections[i] : maxProtection);
                const uint32_t initial = pageProtections ? pageProtections[i] : protect;
                if (!protectionWithinMaximum(initial, maximum)) return NtStatus::SectionProtection;
            }
        }
        uintptr_t start = baseAddress;
        if (!start) {
            start = vadTree_.findFreeRange(length, AllocationGranularity, false, backendBase_, backendEnd_);
            if (!start) return NtStatus::NoMemory;
        }
        if ((start & PageMask4KB) || start < backendBase_ || start > backendEnd_ ||
            length - 1 > backendEnd_ - start) return NtStatus::InvalidParameter;
        std::unique_ptr<VirtualAddressDescriptor> node(new (std::nothrow_t{}) VirtualAddressDescriptor{});
        if (!node) return NtStatus::NoMemory;
        if (!acquire(owner)) return NtStatus::Unsuccessful;
        node->sectionContext = owner;
        node->sectionAcquire = acquire;
        node->sectionRelease = release;
        node->startingAddress = start;
        node->endingAddress = start + length - 1;
        node->protection = protect;
        node->allocationType = MEM_COMMIT;
        node->isSectionView = true;
        node->isImageView = imageView;
        node->privateImageView = privateImageView;
        node->inheritView = inheritView;
        node->sectionOffset = sectionOffset;
        node->sectionMaxProtection = maxProtection;
        auto* vad = node.get();
        if (!vadTree_.insert(std::move(node))) {
            deferSectionOwner(*node, deferredOwner);
            return NtStatus::Conflict;
        }
        if (backend_.mapBatch && !pageProtections && !pageMaximumProtections && !privateImageView) {
            std::unique_ptr<VirtualAddressDescriptor::CommitRange> pending;
            for (size_t i = 0; i < count; ++i) {
                std::unique_ptr<VirtualAddressDescriptor::CommitRange> page(
                    new (std::nothrow_t{}) VirtualAddressDescriptor::CommitRange{});
                if (!page || !pfns[i]) {
                    deferSectionOwner(*vad, deferredOwner);
                    if (!vadTree_.remove(start)) ownershipInvariantFailure();
                    return page ? NtStatus::InvalidParameter : NtStatus::NoMemory;
                }
                page->firstVpn = page->lastVpn = (start >> PageShift4KB) + i;
                page->pfn = pfns[i];
                page->protection = protect;
                page->maximumProtection = maxProtection;
                page->next = std::move(pending);
                pending = std::move(page);
            }
            if (!backend_.mapBatch(backend_.context, start, pfns, count, protect)) {
                deferSectionOwner(*vad, deferredOwner);
                if (!vadTree_.remove(start)) ownershipInvariantFailure();
                return NtStatus::Unsuccessful;
            }
            vad->commitRanges = std::move(pending);
            vad->commitCharge = count;
            vad->committed = true;
            workingSet_.commitCharge += count;
            workingSet_.residentPages += count;
            workingSet_.peakCommitCharge = std::max(workingSet_.peakCommitCharge, workingSet_.commitCharge);
            workingSet_.peakResidentPages = std::max(workingSet_.peakResidentPages, workingSet_.residentPages);
            baseAddress = start;
            return NtStatus::Success;
        }
        NtStatus failure = NtStatus::Success;
        for (size_t i = 0; i < count; ++i) {
            std::unique_ptr<VirtualAddressDescriptor::CommitRange> page(
                new (std::nothrow_t{}) VirtualAddressDescriptor::CommitRange{});
            if (!page) { failure = NtStatus::NoMemory; break; }
            page->firstVpn = page->lastVpn = (start >> PageShift4KB) + i;
            page->pfn = pfns[i];
            page->sharedImagePage = imageView && sharedImagePages && sharedImagePages[i];
            page->sharedImagePfn = page->sharedImagePage ? pfns[i] : 0;
            page->protection = privateImageView ? protect :
                (pageProtections ? pageProtections[i] : protect);
            page->maximumProtection = pageMaximumProtections ? pageMaximumProtections[i] :
                (pageProtections ? pageProtections[i] : maxProtection);
            if (!page->pfn) { failure = NtStatus::InvalidParameter; break; }
            if (imageView && (privateImageView || (!page->sharedImagePage &&
                             (page->protection == PAGE_READWRITE ||
                              page->protection == PAGE_EXECUTE_READWRITE)))) {
                auto& db = PfnDatabase::get();
                const uint64_t privatePfn = db.allocatePage(true);
                if (!privatePfn || !db.zeroAllocatedPage(privatePfn)) {
                    if (privatePfn) db.freePage(privatePfn);
                    failure = NtStatus::NoMemory;
                    break;
                }
                std::memcpy(reinterpret_cast<void*>(privatePfn << PageShift4KB),
                            reinterpret_cast<const void*>(page->pfn << PageShift4KB), PageSize4KB);
                page->pfn = privatePfn;
                page->ownsPfn = true;
            }
            if (!page->pfn || !backend_.map(backend_.context, start + i * PageSize4KB,
                                            page->pfn, page->protection)) {
                if (page->ownsPfn) PfnDatabase::get().freePage(page->pfn);
                failure = NtStatus::Unsuccessful;
                break;
            }
            page->next = std::move(vad->commitRanges);
            vad->commitRanges = std::move(page);
            ++vad->commitCharge;
            ++workingSet_.commitCharge;
            ++workingSet_.residentPages;
        }
        vad->committed = vad->commitCharge != 0;
        if (failure != NtStatus::Success) {
            // Retain the VAD and backing if an unmap fails: live PTEs must not
            // outlive their physical pages. close() can retry cleanup.
            if (releaseBackendPages(*vad)) {
                deferSectionOwner(*vad, deferredOwner);
                if (!vadTree_.remove(start)) ownershipInvariantFailure();
            } else {
                vad->rollbackPending = true;
            }
            return failure;
        }
        workingSet_.peakCommitCharge = std::max(workingSet_.peakCommitCharge, workingSet_.commitCharge);
        workingSet_.peakResidentPages = std::max(workingSet_.peakResidentPages, workingSet_.residentPages);
        baseAddress = start;
        return NtStatus::Success;
    }

    // The process must remain unpublished while its cloned image imports are
    // rebound. Its pages are already private; temporarily permit writes only
    // to the import-address-table range and retain each page's PE maximum.
    NtStatus prepareImageBinding(uintptr_t address, uintptr_t iat, size_t length,
                                 bool preserveCurrentPermissions = false) noexcept {
        Guard lock(*this);
        auto* vad = vadTree_.find(address);
        if (!vad || vad->startingAddress != address || !vad->isSectionView ||
            !vad->isImageView || (!vad->ownsImagePages && !vad->privateImageView))
            return NtStatus::InvalidParameter;
        const bool wasPrivate = vad->privateImageView;
        struct PreviousMaximum {
            VirtualAddressDescriptor::CommitRange* page;
            uint32_t maximum;
            std::unique_ptr<PreviousMaximum> next;
        };
        std::unique_ptr<PreviousMaximum> previous;
        // Bootstrap image views did not retain per-page PE maxima; capture
        // their current permissions before temporarily opening the IAT.
        if (preserveCurrentPermissions) {
            for (auto* page = vad->commitRanges.get(); page; page = page->next.get()) {
                auto entry = std::unique_ptr<PreviousMaximum>(new (std::nothrow_t{})
                    PreviousMaximum{page, page->maximumProtection, {}});
                if (!entry) return NtStatus::NoMemory;
                entry->next = std::move(previous);
                previous = std::move(entry);
            }
            for (auto* page = vad->commitRanges.get(); page; page = page->next.get())
                page->maximumProtection = page->protection;
        }
        vad->privateImageView = true;
        if (!length) return NtStatus::Success;
        uint32_t old{};
        const auto status = protectBackend(iat, length, PAGE_READWRITE, &old);
        if (status != NtStatus::Success) {
            vad->privateImageView = wasPrivate;
            for (auto* entry = previous.get(); entry; entry = entry->next.get())
                entry->page->maximumProtection = entry->maximum;
        }
        return status;
    }

    // Complete a private image loader view after relocation and import
    // binding. Each page retains its PE-derived maximum protection; no user
    // thread may execute the unpublished process until this succeeds.
    NtStatus finalizeImageProtections(uintptr_t address) noexcept {
        Guard lock(*this);
        if (!backend_.protect) return NtStatus::NotImplemented;
        auto* vad = vadTree_.find(address);
        if (!vad || vad->startingAddress != address || !vad->isImageView ||
            !vad->isSectionView || !vad->privateImageView) return NtStatus::InvalidParameter;
        struct Change {
            VirtualAddressDescriptor::CommitRange* page;
            uint32_t oldProtection;
            bool oldCopyOnWriteArmed;
            uint64_t oldPfn;
            bool replaced;
            std::unique_ptr<Change> next;
        };
        std::unique_ptr<Change> changes;
        for (auto* page = vad->commitRanges.get(); page; page = page->next.get()) {
            auto change = std::unique_ptr<Change>(new (std::nothrow_t{}) Change{
                page, page->protection, page->copyOnWriteArmed, page->pfn, false, {}});
            if (!change) return NtStatus::NoMemory;
            change->next = std::move(changes);
            changes = std::move(change);
        }
        auto* change = changes.get();
        for (; change; change = change->next.get()) {
            const uintptr_t va = change->page->firstVpn << PageShift4KB;
            const uint32_t logical = change->page->maximumProtection;
            const bool copy = logical == PAGE_WRITECOPY || logical == PAGE_EXECUTE_WRITECOPY;
            const bool armed = copy && !change->page->ownsPfn;
            const uint32_t hardware = logical == PAGE_EXECUTE_WRITECOPY
                ? (armed ? PAGE_EXECUTE_READ : PAGE_EXECUTE_READWRITE)
                : logical == PAGE_WRITECOPY ? (armed ? PAGE_READONLY : PAGE_READWRITE) : logical;
            if (change->page->sharedImagePage && change->page->ownsPfn) {
                if (!backend_.replace ||
                    !change->page->sharedImagePfn ||
                    !backend_.replace(backend_.context, va, change->oldPfn,
                                      change->page->sharedImagePfn, hardware)) break;
                change->replaced = true;
                change->page->pfn = change->page->sharedImagePfn;
            } else if (!backend_.protect(backend_.context, va, hardware)) break;
            change->page->protection = logical;
            change->page->copyOnWriteArmed = armed;
        }
        if (change) {
            for (auto* rollback = changes.get(); rollback != change; rollback = rollback->next.get()) {
                const uintptr_t va = rollback->page->firstVpn << PageShift4KB;
                const uint32_t oldHardware = (rollback->oldProtection == PAGE_EXECUTE_WRITECOPY ||
                                              (rollback->oldProtection == PAGE_EXECUTE_READWRITE && rollback->oldCopyOnWriteArmed))
                    ? (rollback->oldCopyOnWriteArmed ? PAGE_EXECUTE_READ : PAGE_EXECUTE_READWRITE)
                    : (rollback->oldProtection == PAGE_WRITECOPY ||
                       (rollback->oldProtection == PAGE_READWRITE && rollback->oldCopyOnWriteArmed))
                        ? (rollback->oldCopyOnWriteArmed ? PAGE_READONLY : PAGE_READWRITE)
                        : rollback->oldProtection;
                if (rollback->replaced) {
                    if (!backend_.replace(backend_.context, va, rollback->page->pfn,
                                          rollback->oldPfn, oldHardware)) ownershipInvariantFailure();
                    rollback->page->pfn = rollback->oldPfn;
                } else if (!backend_.protect(backend_.context, va, oldHardware)) ownershipInvariantFailure();
                rollback->page->protection = rollback->oldProtection;
                rollback->page->copyOnWriteArmed = rollback->oldCopyOnWriteArmed;
            }
            return NtStatus::Unsuccessful;
        }
        for (auto* committed = changes.get(); committed; committed = committed->next.get()) {
            if (committed->replaced) {
                PfnDatabase::get().freePage(committed->oldPfn);
                committed->page->ownsPfn = false;
            }
        }
        vad->privateImageView = false;
        return NtStatus::Success;
    }

    NtStatus unmapSharedPages(uintptr_t address, bool rollbackOnFailure = false) noexcept {
        DeferredOwnerRelease owner{};
        {
            Guard lock(*this);
            if (!backend_.map) return NtStatus::NotImplemented;
            auto* vad = vadTree_.find(address);
            if (!vad || !vad->isSectionView) return NtStatus::InvalidParameter;
            const uintptr_t start = vad->startingAddress;
            if (!releaseBackendPages(*vad)) {
                // A failed loader rollback must keep both the live PTEs and
                // their backing. Retry before another section mapping; the
                // process teardown path can also drain the retained VAD.
                if (rollbackOnFailure) vad->rollbackPending = true;
                return NtStatus::Unsuccessful;
            }
            deferSectionOwner(*vad, owner);
            if (!vadTree_.remove(start)) ownershipInvariantFailure();
        }
        owner.releaseNow();
        return NtStatus::Success;
    }

    NtStatus free(uintptr_t baseAddress, size_t size, uint32_t freeType) {
        DeferredOwnerRelease deferredOwner;
        Guard lock(*this);
        if (backend_.map) {
            auto* vad = vadTree_.find(baseAddress);
            if (!vad) return NtStatus::InvalidParameter;
            if (vad->isSectionView) return NtStatus::Conflict;
            if (freeType == MEM_RELEASE) {
                if (vad->startingAddress != baseAddress || size != 0)
                    return NtStatus::InvalidParameter;
                if (!releaseBackendPages(*vad)) return NtStatus::Unsuccessful;
                deferSectionOwner(*vad, deferredOwner);
                if (!vadTree_.remove(baseAddress)) ownershipInvariantFailure();
                return NtStatus::Success;
            }
            if (freeType != MEM_DECOMMIT) return NtStatus::NotImplemented;
            uintptr_t start{}, end{};
            if (size == 0) {
                if (baseAddress != vad->startingAddress) return NtStatus::InvalidParameter;
                start = vad->startingAddress;
                end = vad->endingAddress;
            } else if (!pageRange(baseAddress, size, start, end)) {
                return NtStatus::InvalidParameter;
            }
            if (start < vad->startingAddress || end > vad->endingAddress)
                return NtStatus::Conflict;
            return releaseBackendPages(*vad, start >> PageShift4KB, end >> PageShift4KB)
                ? NtStatus::Success : NtStatus::Unsuccessful;
        }
#if defined(MICANT_FREESTANDING)
        return NtStatus::NotImplemented;
#endif
        VirtualAddressDescriptor* vad = vadTree_.find(baseAddress);
        if (!vad) return NtStatus::InvalidParameter;

        if (freeType & MEM_RELEASE) {
            if (freeType != MEM_RELEASE || size != 0 || baseAddress != vad->startingAddress)
                return NtStatus::InvalidParameter;
            // Unmap and free all physical page frames allocated for this VAD
            auto& pfnDb = PfnDatabase::get();
            for (const auto& [vpn, pfn] : vad->physicalPages) {
                pagingEngine_.unmapPage(directoryTableBase_, vpn << PageShift4KB);
                pfnDb.freePage(pfn);
                if (workingSet_.residentPages > 0) workingSet_.residentPages--;
            }
            vad->physicalPages.clear();

            if (vad->committed && workingSet_.commitCharge >= vad->commitCharge) {
                workingSet_.commitCharge -= vad->commitCharge;
            }

            const uintptr_t removedAddress = vad->startingAddress;
            deferSectionOwner(*vad, deferredOwner);
            if (!vadTree_.remove(removedAddress)) ownershipInvariantFailure();
            return NtStatus::Success;
        } else if (freeType & MEM_DECOMMIT) {
            auto& pfnDb = PfnDatabase::get();
            for (const auto& [vpn, pfn] : vad->physicalPages) {
                pagingEngine_.unmapPage(directoryTableBase_, vpn << PageShift4KB);
                pfnDb.freePage(pfn);
                if (workingSet_.residentPages > 0) workingSet_.residentPages--;
            }
            vad->physicalPages.clear();

            if (vad->committed && workingSet_.commitCharge >= vad->commitCharge) {
                workingSet_.commitCharge -= vad->commitCharge;
            }
            vad->committed = false;
            vad->commitCharge = 0;
            return NtStatus::Success;
        }

        (void)size;
        return NtStatus::InvalidParameter;
    }

    NtStatus protect(uintptr_t baseAddress, size_t size, uint32_t newProtect, uint32_t* oldProtect) {
        Guard lock(*this);
        if (backend_.map) return protectBackend(baseAddress, size, newProtect, oldProtect);
#if defined(MICANT_FREESTANDING)
        return NtStatus::NotImplemented;
#endif
        VirtualAddressDescriptor* vad = vadTree_.find(baseAddress);
        if (!vad) return NtStatus::InvalidParameter;

        if (oldProtect) {
            *oldProtect = vad->protection;
        }

        vad->protection = newProtect;
        vad->isCopyOnWrite = (newProtect == PAGE_WRITECOPY || newProtect == PAGE_EXECUTE_WRITECOPY);
        vad->isGuardPage = (newProtect & PAGE_GUARD) != 0;

        // Propagate updated protection to hardware page tables
        for (const auto& [vpn, pfn] : vad->physicalPages) {
            pagingEngine_.modifyProtection(directoryTableBase_, vpn << PageShift4KB, newProtect);
        }

        (void)size;
        return NtStatus::Success;
    }

    // Publish syscall outputs while the per-page change list remains live;
    // a failed copyout restores each page's individual prior protection.
    NtStatus protectWithCommit(uintptr_t baseAddress, size_t size, uint32_t newProtect,
                               NtStatus (*commit)(void*, uint32_t) noexcept,
                               void* context) noexcept {
        if (!commit) return NtStatus::InvalidParameter;
        Guard lock(*this);
        if (!backend_.map) return NtStatus::NotImplemented;
        uint32_t old{};
        return protectBackend(baseAddress, size, newProtect, &old, commit, context);
    }

    [[nodiscard]] size_t getRegionCount() const noexcept {
        return vadTree_.size();
    }

    [[nodiscard]] VirtualAddressDescriptor* findVad(uintptr_t addr) noexcept {
        return vadTree_.find(addr);
    }

    [[nodiscard]] const VirtualAddressDescriptor* findVad(uintptr_t addr) const noexcept {
        return vadTree_.find(addr);
    }

    [[nodiscard]] bool queryRegion(uintptr_t address, QueryRegion& result) const noexcept {
        if (address > UserSpaceMax) return false;
        Guard lock(*this);
        const uintptr_t page = address & ~static_cast<uintptr_t>(PageMask4KB);
        const auto* vad = vadTree_.find(address);
        if (!vad) {
            const uintptr_t next = vadTree_.nextStartingAddress(page);
            const uintptr_t end = next ? next - 1 : UserSpaceMax;
            if (end < page) return false;
            result = {page, 0, static_cast<size_t>(end - page + 1), PAGE_NOACCESS,
                      0x00010000U, PAGE_NOACCESS, 0};
            return true;
        }

        constexpr uint32_t MemCommit = 0x00001000U;
        constexpr uint32_t MemReserve = 0x00002000U;
        constexpr uint32_t MemPrivate = 0x00020000U;
        constexpr uint32_t MemMapped = 0x00040000U;
        constexpr uint32_t MemImage = 0x01000000U;
        const uint32_t type = vad->isImageView ? MemImage :
            (vad->isSectionView ? MemMapped : MemPrivate);
        const size_t pageCount = vad->pageCount();
        if (vad->commitCharge == 0 || vad->commitCharge == pageCount) {
            const uint32_t state = vad->commitCharge ? MemCommit : MemReserve;
            result = {vad->startingAddress, vad->startingAddress, vad->size(),
                vad->protection, state, state == MemCommit ? vad->protection : PAGE_NOACCESS, type};
            return true;
        }

        const uint64_t vpn = page >> PageShift4KB;
        const VirtualAddressDescriptor::CommitRange* committed = vad->commitRanges.get();
        while (committed && !(vpn >= committed->firstVpn && vpn <= committed->lastVpn))
            committed = committed->next.get();
        const uint32_t state = committed ? MemCommit : MemReserve;
        result = {page, vad->startingAddress, PageSize4KB, vad->protection, state,
            committed ? committed->protection : PAGE_NOACCESS, type};
        return true;
    }

    NtStatus setStackGuard(uintptr_t address, bool guarded) noexcept {
        Guard lock(*this);
        if (!backend_.guard || (address & PageMask4KB)) return NtStatus::InvalidParameter;
        auto* vad = vadTree_.find(address);
        if (!vad || vad->isSectionView) return NtStatus::InvalidParameter;
        auto* page = vad->commitRanges.get();
        while (page && page->firstVpn != (address >> PageShift4KB)) page = page->next.get();
        if (!page) return NtStatus::Conflict;
        const uint32_t expected = PAGE_READWRITE | (guarded ? 0 : PAGE_GUARD);
        if (page->protection != expected) return NtStatus::InvalidParameter;
        if (!backend_.guard(backend_.context, address, guarded)) return NtStatus::Unsuccessful;
        page->protection = PAGE_READWRITE | (guarded ? PAGE_GUARD : 0);
        return NtStatus::Success;
    }

    // Clone private committed pages while the single BSP is excluded. The
    // caller supplies root switches and restores its original root afterward.
    // ViewShare sections retain their owner and PFNs, ViewUnmap sections are
    // omitted. Image pages are copied eagerly with private PFN ownership.
    NtStatus clonePrivateTo(ProcessAddressSpace& target, uintptr_t sourceRoot,
                            uintptr_t targetRoot, bool (*switchRoot)(uintptr_t) noexcept) noexcept {
        if (&target == this || !backend_.map || !target.backend_.map || !sourceRoot || !targetRoot || !switchRoot)
            return NtStatus::InvalidParameter;
        Guard sourceGuard(*this);
        std::unique_ptr<uint8_t[]> page(new (std::nothrow_t{}) uint8_t[PageSize4KB]);
        if (!page) return NtStatus::NoMemory;
        for (uintptr_t address = vadTree_.firstAddress(); address; address = vadTree_.nextAddress(address)) {
            const auto* vad = vadTree_.find(address);
            if (!vad) return NtStatus::Unsuccessful;
            if (vad->rollbackPending) return NtStatus::Unsuccessful;
            if (vad->isSectionView && !vad->isImageView) {
                if (!vad->inheritView) continue;
                const size_t count = vad->pageCount();
                std::unique_ptr<uint64_t[]> frames(new (std::nothrow_t{}) uint64_t[count]{});
                std::unique_ptr<uint32_t[]> protections(new (std::nothrow_t{}) uint32_t[count]{});
                std::unique_ptr<uint32_t[]> maximumProtections(new (std::nothrow_t{}) uint32_t[count]{});
                if (!frames || !protections || !maximumProtections) return NtStatus::NoMemory;
                const uint64_t firstVpn = vad->startingAddress >> PageShift4KB;
                for (auto* committed = vad->commitRanges.get(); committed; committed = committed->next.get()) {
                    if (committed->firstVpn < firstVpn || committed->firstVpn - firstVpn >= count)
                        return NtStatus::Unsuccessful;
                    const auto index = static_cast<size_t>(committed->firstVpn - firstVpn);
                    frames[index] = committed->pfn;
                    protections[index] = committed->protection;
                    maximumProtections[index] = committed->maximumProtection != PAGE_NOACCESS
                        ? committed->maximumProtection : vad->sectionMaxProtection;
                }
                for (size_t i = 0; i < count; ++i) if (!frames[i]) return NtStatus::Unsuccessful;
                if (!switchRoot(targetRoot)) return NtStatus::Unsuccessful;
                uintptr_t base = vad->startingAddress;
                auto status = target.mapSharedPages(base, vad->size(), vad->protection, frames.get(), count,
                    vad->sectionContext, vad->sectionAcquire, vad->sectionRelease,
                    vad->sectionMaxProtection, false, true, vad->sectionOffset,
                    protections.get(), false, maximumProtections.get());
                if (status != NtStatus::Success) return status;
                for (size_t i = 0; i < count;) {
                    const size_t start = i;
                    const auto protection = protections[i++];
                    while (i < count && protections[i] == protection) ++i;
                    if (protection == vad->protection) continue;
                    uint32_t old{};
                    status = target.protect(base + start * PageSize4KB, (i - start) * PageSize4KB,
                                            protection, &old);
                    if (status != NtStatus::Success) return status;
                }
                continue;
            }
            if (!switchRoot(targetRoot)) return NtStatus::Unsuccessful;
            uintptr_t base = vad->startingAddress;
            auto status = target.allocate(base, vad->size(), MEM_RESERVE, vad->protection);
            if (status != NtStatus::Success) return status;
            const size_t pageCount = vad->pageCount();
            std::unique_ptr<uint8_t[]> committedPages(new (std::nothrow_t{}) uint8_t[pageCount]{});
            std::unique_ptr<uint32_t[]> pageMaximumProtections(
                new (std::nothrow_t{}) uint32_t[pageCount]{});
            if (!committedPages || !pageMaximumProtections) return NtStatus::NoMemory;
            for (auto* committed = vad->commitRanges.get(); committed; committed = committed->next.get()) {
                const uint64_t first = vad->startingAddress >> PageShift4KB;
                if (committed->firstVpn < first || committed->firstVpn - first >= pageCount)
                    return NtStatus::Unsuccessful;
                const auto pageIndex = static_cast<size_t>(committed->firstVpn - first);
                pageMaximumProtections[pageIndex] = (vad->isImageView || committed->maximumProtection != PAGE_NOACCESS)
                    ? committed->maximumProtection : vad->sectionMaxProtection;
                uint8_t encoded{};
                if (committed->protection == PAGE_READWRITE) encoded = 1;
                else if (committed->protection == PAGE_READONLY) encoded = 2;
                else if (committed->protection == PAGE_EXECUTE_READ) encoded = 3;
                else if (vad->isImageView && committed->protection == PAGE_EXECUTE_READWRITE) encoded = 6;
                else if (vad->isImageView && committed->protection == PAGE_WRITECOPY) encoded = 8;
                else if (vad->isImageView && committed->protection == PAGE_EXECUTE_WRITECOPY) encoded = 9;
                else if (vad->isImageView && committed->protection == PAGE_NOACCESS) encoded = 5;
                else if (committed->protection == (PAGE_READWRITE | PAGE_GUARD)) {
                    if (!backend_.guard || !target.backend_.guard) return NtStatus::NotImplemented;
                    encoded = 4;
                }
                else return NtStatus::NotImplemented;
                committedPages[static_cast<size_t>(committed->firstVpn - first)] = encoded;
            }
            // Allocate adjacent pages together; in the common fully committed
            // case this avoids repeatedly scanning an expanding commit list.
            for (size_t i = 0; i < pageCount;) {
                if (!committedPages[i]) { ++i; continue; }
                const size_t start = i;
                while (i < pageCount && committedPages[i]) ++i;
                uintptr_t destination = base + start * PageSize4KB;
                status = target.allocate(destination, (i - start) * PageSize4KB, MEM_COMMIT, PAGE_READWRITE);
                if (status != NtStatus::Success) return status;
            }
            for (auto* committed = vad->commitRanges.get(); committed; committed = committed->next.get()) {
                const uintptr_t va = committed->firstVpn << PageShift4KB;
                if (!switchRoot(sourceRoot)) return NtStatus::Unsuccessful;
                const bool guarded = committed->protection == (PAGE_READWRITE | PAGE_GUARD);
                // The caller excludes interrupts on the BSP. Temporarily make
                // the source guard readable, then restore it before changing
                // roots; the source VAD never changes its visible protection.
                if (guarded && !backend_.guard(backend_.context, va, false)) return NtStatus::Unsuccessful;
                if (committed->protection == PAGE_NOACCESS) {
                    // The managed backend retains the PFN for an inaccessible
                    // image PTE. Copy through its supervisor physical mapping.
                    std::memcpy(page.get(), reinterpret_cast<const void*>(
                        committed->pfn << PageShift4KB), PageSize4KB);
                } else {
                    std::memcpy(page.get(), reinterpret_cast<const void*>(va), PageSize4KB);
                }
                if (guarded && !backend_.guard(backend_.context, va, true)) ownershipInvariantFailure();
                if (!switchRoot(targetRoot)) return NtStatus::Unsuccessful;
                std::memcpy(reinterpret_cast<void*>(va), page.get(), PageSize4KB);
                if (guarded) {
                    status = target.setStackGuard(va, true);
                    if (status != NtStatus::Success) return status;
                }
            }
            for (size_t i = 0; i < pageCount;) {
                const uint8_t protection = committedPages[i];
                const size_t start = i++;
                while (i < pageCount && committedPages[i] == protection) ++i;
                if (protection < 2 || protection == 4) continue;
                uint32_t old{};
                status = target.protect(base + start * PageSize4KB, (i - start) * PageSize4KB,
                    protection == 2 ? PAGE_READONLY : protection == 5 ? PAGE_NOACCESS :
                    (protection == 6 || protection == 9) ? PAGE_EXECUTE_READWRITE :
                    protection == 8 ? PAGE_READWRITE : PAGE_EXECUTE_READ, &old);
                if (status != NtStatus::Success) return status;
            }
            if (vad->isImageView) {
                Guard targetGuard(target);
                auto* copied = target.vadTree_.find(base);
                if (!copied) return NtStatus::Unsuccessful;
                copied->isSectionView = true;
                copied->isImageView = true;
                copied->privateImageView = vad->privateImageView;
                copied->ownsImagePages = true;
                copied->sectionMaxProtection = vad->sectionMaxProtection;
                // Shared image pages must keep both their control-area PFNs
                // and the section owner alive in the cloned address space.
                bool hasSharedImagePages = false;
                for (auto* source = vad->commitRanges.get(); source; source = source->next.get())
                    hasSharedImagePages |= source->sharedImagePage && !source->ownsPfn;
                if (hasSharedImagePages) {
                    if (!vad->sectionContext || !vad->sectionAcquire || !vad->sectionRelease ||
                        !target.backend_.replace || !vad->sectionAcquire(vad->sectionContext))
                        return NtStatus::Unsuccessful;
                    copied->sectionContext = vad->sectionContext;
                    copied->sectionAcquire = vad->sectionAcquire;
                    copied->sectionRelease = vad->sectionRelease;
                }
                const uint64_t firstVpn = vad->startingAddress >> PageShift4KB;
                for (auto* committed = copied->commitRanges.get(); committed;
                     committed = committed->next.get()) {
                    if (committed->firstVpn < firstVpn ||
                        committed->firstVpn - firstVpn >= pageCount)
                        ownershipInvariantFailure();
                    const auto index = static_cast<size_t>(committed->firstVpn - firstVpn);
                    committed->maximumProtection = pageMaximumProtections[index];
                    if (hasSharedImagePages) {
                        auto* source = vad->commitRanges.get();
                        while (source && source->firstVpn != committed->firstVpn) source = source->next.get();
                        if (!source) return NtStatus::Unsuccessful;
                        if (source->sharedImagePage && !source->ownsPfn) {
                            const uint32_t protection = source->protection;
                            const bool copy = protection == PAGE_WRITECOPY ||
                                              protection == PAGE_EXECUTE_WRITECOPY;
                            if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
                                protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
                                protection != PAGE_NOACCESS && !copy)
                                return NtStatus::NotSupported;
                            const uint32_t hardware = protection == PAGE_WRITECOPY ? PAGE_READONLY :
                                protection == PAGE_EXECUTE_WRITECOPY ? PAGE_EXECUTE_READ : protection;
                            if (!target.backend_.replace(target.backend_.context,
                                    committed->firstVpn << PageShift4KB, committed->pfn,
                                    source->pfn, hardware)) return NtStatus::Unsuccessful;
                            PfnDatabase::get().freePage(committed->pfn);
                            committed->pfn = source->pfn;
                            committed->ownsPfn = false;
                            committed->sharedImagePage = true;
                            committed->sharedImagePfn = source->sharedImagePfn;
                            committed->protection = protection;
                            committed->copyOnWriteArmed = copy;
                        }
                    }
                    // Cloned image pages are already private. Preserve the
                    // logical write-copy protection with a writable PTE; no
                    // further COW fault is needed in the child.
                    if (committed->ownsPfn && (committedPages[index] == 8 || committedPages[index] == 9)) {
                        committed->protection = committedPages[index] == 8
                            ? PAGE_WRITECOPY : PAGE_EXECUTE_WRITECOPY;
                        committed->copyOnWriteArmed = false;
                    }
                }
            }
        }
        return NtStatus::Success;
    }

    /**
     * @brief High-Performance Demand Paging & Page Fault Handler (Vector 14 / #PF).
     *
     * @param faultingAddress Faulting virtual address
     * @param isWrite True if access was a write, false for read
     * @param isUser True if user-mode fault, false for kernel-mode
     * @param isExecute True if instruction fetch fault
     */
    NtStatus handlePageFault(uintptr_t faultingAddress, bool isWrite, bool isUser, bool isExecute) {
        Guard lock(*this);
        if (backend_.map) {
            if (!isUser || !isWrite || isExecute || !backend_.replace) return NtStatus::AccessViolation;
            return resolveCopyOnWriteLocked(faultingAddress);
        }
#if defined(MICANT_FREESTANDING)
        return NtStatus::NotImplemented;
#endif

        // 1. Locate VAD in AVL Tree in O(log N)
        VirtualAddressDescriptor* vad = vadTree_.find(faultingAddress);
        if (!vad) {
            return NtStatus::AccessViolation; // 0xC0000005
        }

        // 2. Validate Access Rights Against Protection
        if (vad->protection == PAGE_NOACCESS) {
            return NtStatus::AccessViolation;
        }
        if (isWrite && (vad->protection == PAGE_READONLY || vad->protection == PAGE_EXECUTE_READ)) {
            return NtStatus::AccessViolation;
        }
        if (isExecute && (vad->protection & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
            return NtStatus::AccessViolation;
        }

        // 3. Handle Guard Page (Stack Growth)
        if (vad->isGuardPage) {
            vad->isGuardPage = false;
            vad->protection &= ~PAGE_GUARD;
            return NtStatus::GuardPageViolation; // 0x80000001 (triggers stack expansion)
        }

        uint64_t faultVpn = faultingAddress >> PageShift4KB;
        auto& pfnDb = PfnDatabase::get();

        // 4. Handle Copy-On-Write (COW)
        if (vad->isCopyOnWrite && isWrite) {
            uint64_t privatePfn = pfnDb.allocatePage(true);
            if (privatePfn == 0) return NtStatus::NoMemory;

            vad->physicalPages[faultVpn] = privatePfn;
            pagingEngine_.mapPage(directoryTableBase_, faultVpn << PageShift4KB, privatePfn, PAGE_READWRITE, isUser);
            vad->isCopyOnWrite = false;
            vad->protection = PAGE_READWRITE;
            return NtStatus::Success;
        }

        // 5. Demand-Zero Page Allocation
        if (vad->committed) {
            auto it = vad->physicalPages.find(faultVpn);
            if (it == vad->physicalPages.end()) {
                uint64_t newPfn = pfnDb.allocatePage(true);
                if (newPfn == 0) return NtStatus::NoMemory;

                vad->physicalPages[faultVpn] = newPfn;
                pagingEngine_.mapPage(directoryTableBase_, faultVpn << PageShift4KB, newPfn, vad->protection, isUser);

                workingSet_.residentPages++;
                workingSet_.peakResidentPages = std::max(workingSet_.peakResidentPages, workingSet_.residentPages);
            }
            return NtStatus::Success;
        }

        return NtStatus::AccessViolation;
    }

    // User-buffer probing may need to materialize a write-copy page before a
    // kernel copy. Do not spin if the same address space is already locked
    // by the caller (for example a protection transaction's output copy).
    NtStatus tryResolveCopyOnWriteForProbe(uintptr_t address) noexcept {
#if defined(MICANT_FREESTANDING) && defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        if (lock_.test_and_set(std::memory_order_acquire)) {
            if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
            return NtStatus::Retry;
        }
        const auto result = backend_.map && backend_.replace
            ? resolveCopyOnWriteLocked(address) : NtStatus::AccessViolation;
        lock_.clear(std::memory_order_release);
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return result;
#else
        (void)address;
        return NtStatus::AccessViolation;
#endif
    }

    [[nodiscard]] const WorkingSetList& getWorkingSet() const noexcept { return workingSet_; }
    [[nodiscard]] size_t getCommitCharge() const noexcept { return workingSet_.commitCharge; }
    [[nodiscard]] size_t getResidentPages() const noexcept { return workingSet_.residentPages; }
    [[nodiscard]] uintptr_t getDirectoryTableBase() const noexcept { return directoryTableBase_; }
    void setDirectoryTableBase(uintptr_t cr3) noexcept { directoryTableBase_ = cr3; }

    size_t trimWorkingSet(size_t targetPages) {
        Guard lock(*this);
        if (backend_.map) return 0;
#if defined(MICANT_FREESTANDING)
        return 0;
#endif
        size_t trimmed = 0;
        auto& pfnDb = PfnDatabase::get();

        std::vector<VirtualAddressDescriptor*> vads = vadTree_.inOrder();
        for (auto* vad : vads) {
            if (!vad || vad->physicalPages.empty()) continue;

            for (auto it = vad->physicalPages.begin(); it != vad->physicalPages.end(); ) {
                if (trimmed >= targetPages) break;
                uint64_t vpn = it->first;
                uint64_t pfn = it->second;

                pagingEngine_.unmapPage(directoryTableBase_, vpn << PageShift4KB);
                pfnDb.freePage(pfn);
                it = vad->physicalPages.erase(it);

                if (workingSet_.residentPages > 0) {
                    workingSet_.residentPages--;
                }
                trimmed++;
            }
            if (trimmed >= targetPages) break;
        }

        return trimmed;
    }

private:
    NtStatus resolveCopyOnWriteLocked(uintptr_t address) noexcept {
        auto* vad = vadTree_.find(address);
        if (!vad || !vad->isSectionView || !vad->isImageView || vad->rollbackPending || !backend_.replace)
            return NtStatus::AccessViolation;
        const uint64_t vpn = address >> PageShift4KB;
        for (auto* page = vad->commitRanges.get(); page; page = page->next.get()) {
            if (page->firstVpn != vpn) continue;
            if (!page->copyOnWriteArmed ||
                (page->protection != PAGE_WRITECOPY && page->protection != PAGE_EXECUTE_WRITECOPY &&
                 page->protection != PAGE_READWRITE && page->protection != PAGE_EXECUTE_READWRITE))
                return NtStatus::AccessViolation;
            auto& db = PfnDatabase::get();
            const uint64_t privatePfn = db.allocatePage(true);
            if (!privatePfn) return NtStatus::NoMemory;
            if (!db.zeroAllocatedPage(privatePfn)) {
                db.freePage(privatePfn);
                return NtStatus::Unsuccessful;
            }
            std::memcpy(reinterpret_cast<void*>(privatePfn << PageShift4KB),
                        reinterpret_cast<const void*>(page->pfn << PageShift4KB), PageSize4KB);
            const uint32_t hardwareProtection = (page->protection == PAGE_EXECUTE_WRITECOPY ||
                                                 page->protection == PAGE_EXECUTE_READWRITE)
                ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
            if (!backend_.replace(backend_.context, vpn << PageShift4KB,
                                  page->pfn, privatePfn, hardwareProtection)) {
                db.freePage(privatePfn);
                return NtStatus::Unsuccessful;
            }
            page->pfn = privatePfn;
            page->ownsPfn = true;
            page->sharedImagePage = false;
            page->sharedImagePfn = 0;
            page->copyOnWriteArmed = false;
            return NtStatus::Success;
        }
        return NtStatus::AccessViolation;
    }

    static bool protectionWithinMaximum(uint32_t requested, uint32_t maximum) noexcept {
        if (requested == maximum) return true;
        if (requested == PAGE_NOACCESS) return true;
        if (maximum == PAGE_WRITECOPY)
            return requested == PAGE_READONLY || requested == PAGE_READWRITE;
        if (maximum == PAGE_EXECUTE_WRITECOPY)
            return requested == PAGE_READONLY || requested == PAGE_READWRITE ||
                   requested == PAGE_WRITECOPY || requested == PAGE_EXECUTE_READ ||
                   requested == PAGE_EXECUTE_READWRITE;
        if (requested == PAGE_WRITECOPY)
            return maximum == PAGE_READONLY || maximum == PAGE_READWRITE ||
                   maximum == PAGE_EXECUTE_READ || maximum == PAGE_EXECUTE_READWRITE;
        if (requested == PAGE_EXECUTE_WRITECOPY)
            return maximum == PAGE_EXECUTE_READ || maximum == PAGE_EXECUTE_READWRITE;
        if (maximum == PAGE_EXECUTE_READWRITE)
            return requested == PAGE_READONLY || requested == PAGE_READWRITE || requested == PAGE_EXECUTE_READ;
        return requested == PAGE_READONLY &&
            (maximum == PAGE_READWRITE || maximum == PAGE_EXECUTE_READ);
    }

    [[noreturn]] static void ownershipInvariantFailure() noexcept {
#if defined(MICANT_FREESTANDING)
        __builtin_trap();
#else
        std::terminate();
#endif
    }

    class Guard {
    public:
        explicit Guard(const ProcessAddressSpace& space) noexcept
#if defined(MICANT_FREESTANDING)
            : lock_(space.lock_)
#else
            : lock_(space.mutex_)
#endif
        {
#if defined(MICANT_FREESTANDING)
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags_) : : "memory");
#endif
            while (lock_.test_and_set(std::memory_order_acquire)) {}
#endif
        }
        ~Guard() noexcept {
#if defined(MICANT_FREESTANDING)
            lock_.clear(std::memory_order_release);
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
            if (flags_ & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
#endif
#endif
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    private:
#if defined(MICANT_FREESTANDING)
        std::atomic_flag& lock_;
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
        uint64_t flags_{};
#endif
#else
        std::lock_guard<std::mutex> lock_;
#endif
    };

    static bool pageRange(uintptr_t address, size_t size, uintptr_t& start,
                          uintptr_t& end) noexcept {
        if (!size || address > std::numeric_limits<uintptr_t>::max() - (size - 1))
            return false;
        start = address & ~static_cast<uintptr_t>(PageMask4KB);
        end = (address + size - 1) | static_cast<uintptr_t>(PageMask4KB);
        return true;
    }

    [[nodiscard]] bool releaseBackendPages(
        VirtualAddressDescriptor& vad, uint64_t firstVpn = 0,
        uint64_t lastVpn = std::numeric_limits<uint64_t>::max()) noexcept {
        auto& db = PfnDatabase::get();
        // Image views can contain privately allocated pages even when the
        // VAD as a whole does not own every image page. Release those pages
        // individually instead of discarding their ownership records.
        if (vad.isSectionView && !vad.isImageView && !vad.ownsImagePages && backend_.mapBatch && backend_.unmapBatch && firstVpn == 0 &&
            lastVpn == std::numeric_limits<uint64_t>::max() && vad.commitCharge) {
            if (!backend_.unmapBatch(backend_.context, vad.startingAddress, vad.pageCount()))
                return false;
            if (workingSet_.commitCharge < vad.commitCharge || workingSet_.residentPages < vad.commitCharge)
                ownershipInvariantFailure();
            workingSet_.commitCharge -= vad.commitCharge;
            workingSet_.residentPages -= vad.commitCharge;
            vad.commitRanges.reset();
            vad.commitCharge = 0;
            vad.committed = false;
            return true;
        }
        bool success = true;
        auto* link = &vad.commitRanges;
        while (*link) {
            auto& page = **link;
            if (page.firstVpn < firstVpn || page.firstVpn > lastVpn) {
                link = &page.next;
                continue;
            }
            if (!backend_.unmap(backend_.context, page.firstVpn << PageShift4KB)) {
                success = false;
                link = &page.next;
                continue;
            }
            if (!vad.commitCharge || !workingSet_.commitCharge || !workingSet_.residentPages)
                ownershipInvariantFailure();
            if (!vad.isSectionView || page.ownsPfn || (vad.ownsImagePages && !page.sharedImagePage))
                db.freePage(page.pfn);
            --vad.commitCharge;
            --workingSet_.commitCharge;
            --workingSet_.residentPages;
            *link = std::move(page.next);
        }
        vad.committed = vad.commitCharge != 0;
        return success;
    }

    // Failed rollback retains the backing until the backend can remove every
    // PTE. Retry it before allowing another section mapping; the address of
    // an automatically selected failed view never has to be known by callers.
    [[nodiscard]] bool recoverFailedSectionMapping(DeferredOwnerRelease& deferredOwner) noexcept {
        VirtualAddressDescriptor* vad{};
        for (uintptr_t address = vadTree_.firstAddress(); address; address = vadTree_.nextAddress(address)) {
            auto* candidate = vadTree_.find(address);
            if (!candidate) ownershipInvariantFailure();
            if (candidate->rollbackPending) { vad = candidate; break; }
        }
        if (!vad) return true;
        if (!vad->isSectionView) ownershipInvariantFailure();
        if (!releaseBackendPages(*vad)) return false;
        const uintptr_t base = vad->startingAddress;
        deferSectionOwner(*vad, deferredOwner);
        if (!vadTree_.remove(base)) ownershipInvariantFailure();
        // A callback can only be released after dropping the guard. If more
        // than one rollback was pending, reject this mapping and allow the
        // caller to retry after this owner has been released safely.
        for (uintptr_t address = vadTree_.firstAddress(); address; address = vadTree_.nextAddress(address)) {
            const auto* candidate = vadTree_.find(address);
            if (!candidate) ownershipInvariantFailure();
            if (candidate->rollbackPending) return false;
        }
        return true;
    }

    static void deferSectionOwner(VirtualAddressDescriptor& vad,
                                 DeferredOwnerRelease& deferredOwner) noexcept {
        if (deferredOwner.context || deferredOwner.release || deferredOwner.startingAddress
#if !defined(MICANT_FREESTANDING)
            || deferredOwner.hostedOwner
#endif
        ) ownershipInvariantFailure();
        deferredOwner.context = vad.sectionContext;
        deferredOwner.release = vad.sectionRelease;
        vad.sectionContext = nullptr;
        vad.sectionRelease = nullptr;
#if !defined(MICANT_FREESTANDING)
        deferredOwner.hostedOwner = std::move(vad.sectionOwner);
#endif
    }

    NtStatus protectBackend(uintptr_t address, size_t size, uint32_t protection,
                            uint32_t* oldProtection,
                            NtStatus (*commit)(void*, uint32_t) noexcept = nullptr,
                            void* commitContext = nullptr) noexcept {
        if (!backend_.protect) return NtStatus::NotImplemented;
        if (!oldProtection) return NtStatus::InvalidParameter;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_NOACCESS &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_WRITECOPY)
            return NtStatus::NotImplemented;
        uintptr_t start{}, end{};
        if (!pageRange(address, size, start, end)) return NtStatus::InvalidParameter;
        auto* vad = vadTree_.find(start);
        if (!vad || end > vad->endingAddress) return NtStatus::Conflict;
        if (vad->isSectionView && !vad->isImageView &&
            (protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE))
            return NtStatus::NotImplemented;
        const bool copyOnWrite = protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_WRITECOPY;
        if (copyOnWrite && (!vad->isSectionView || !vad->isImageView || !backend_.replace))
            return NtStatus::NotImplemented;

        struct Change {
            VirtualAddressDescriptor::CommitRange* page;
            uint32_t oldProtection;
            bool oldCopyOnWriteArmed;
            std::unique_ptr<Change> next;
        };
        std::unique_ptr<Change> changes;
        const uint64_t firstVpn = start >> PageShift4KB;
        const uint64_t lastVpn = end >> PageShift4KB;
        size_t found{};
        uint32_t firstProtection{};
        for (auto* page = vad->commitRanges.get(); page; page = page->next.get()) {
            if (page->firstVpn < firstVpn || page->firstVpn > lastVpn) continue;
            const uint32_t maximum = (vad->isImageView || page->maximumProtection != PAGE_NOACCESS)
                ? page->maximumProtection : vad->sectionMaxProtection;
            // An image loader may temporarily request PAGE_READWRITE to apply
            // relocations to an RX image page. The write must be private, and
            // executable permission may never be added to a non-executable
            // subsection by this exception to the original PE maximum.
            const bool imagePrivateWrite = vad->isImageView && maximum != PAGE_NOACCESS &&
                (protection == PAGE_READWRITE ||
                 (protection == PAGE_EXECUTE_READWRITE &&
                  (maximum == PAGE_EXECUTE_READ || maximum == PAGE_EXECUTE_READWRITE ||
                   maximum == PAGE_EXECUTE_WRITECOPY)));
            if (vad->isSectionView && !vad->privateImageView &&
                !protectionWithinMaximum(protection, maximum) && !imagePrivateWrite)
                return NtStatus::SectionProtection;
            if (page->firstVpn == firstVpn) firstProtection = page->protection;
            std::unique_ptr<Change> change(new (std::nothrow_t{}) Change{
                page, page->protection, page->copyOnWriteArmed, {}});
            if (!change) return NtStatus::NoMemory;
            change->next = std::move(changes);
            changes = std::move(change);
            ++found;
        }
        if (found != lastVpn - firstVpn + 1) return NtStatus::Conflict;
        auto* change = changes.get();
        for (; change; change = change->next.get()) {
            const uintptr_t va = change->page->firstVpn << PageShift4KB;
            const bool writableImage = vad->isImageView &&
                (protection == PAGE_READWRITE || protection == PAGE_EXECUTE_READWRITE || copyOnWrite);
            const bool armed = writableImage && !change->page->ownsPfn &&
                (!change->page->sharedImagePage || copyOnWrite);
            const uint32_t hardwareProtection = !copyOnWrite && !armed ? protection :
                (protection == PAGE_EXECUTE_WRITECOPY || protection == PAGE_EXECUTE_READWRITE)
                    ? (armed ? PAGE_EXECUTE_READ : PAGE_EXECUTE_READWRITE)
                    : (armed ? PAGE_READONLY : PAGE_READWRITE);
            if (!backend_.protect(backend_.context, va, hardwareProtection)) break;
            change->page->protection = protection;
            change->page->copyOnWriteArmed = armed;
        }
        NtStatus commitStatus = NtStatus::Success;
        if (!change && commit) commitStatus = commit(commitContext, firstProtection);
        if (change || commitStatus != NtStatus::Success) {
            for (auto* rollback = changes.get(); rollback != change; rollback = rollback->next.get()) {
                const uintptr_t va = rollback->page->firstVpn << PageShift4KB;
                const uint32_t oldHardware = (rollback->oldProtection == PAGE_EXECUTE_WRITECOPY ||
                                              (rollback->oldProtection == PAGE_EXECUTE_READWRITE && rollback->oldCopyOnWriteArmed))
                    ? (rollback->oldCopyOnWriteArmed ? PAGE_EXECUTE_READ : PAGE_EXECUTE_READWRITE)
                    : (rollback->oldProtection == PAGE_WRITECOPY ||
                       (rollback->oldProtection == PAGE_READWRITE && rollback->oldCopyOnWriteArmed))
                        ? (rollback->oldCopyOnWriteArmed ? PAGE_READONLY : PAGE_READWRITE)
                        : rollback->oldProtection;
                if (!backend_.protect(backend_.context, va, oldHardware))
                    ownershipInvariantFailure();
                rollback->page->protection = rollback->oldProtection;
                rollback->page->copyOnWriteArmed = rollback->oldCopyOnWriteArmed;
            }
            return change ? NtStatus::Unsuccessful : commitStatus;
        }
        *oldProtection = firstProtection;
        return NtStatus::Success;
    }

    NtStatus allocateBackend(uintptr_t& baseAddress, size_t size, uint32_t type, uint32_t protect) noexcept {
        if (!size || size > std::numeric_limits<size_t>::max() - PageMask4KB)
            return NtStatus::InvalidParameter;
        if ((type & ~(MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN)) ||
            !(type & (MEM_RESERVE | MEM_COMMIT))) return NtStatus::NotImplemented;
        if (protect != PAGE_READONLY && protect != PAGE_READWRITE && protect != PAGE_EXECUTE_READ &&
            protect != PAGE_EXECUTE_READWRITE)
            return NtStatus::NotImplemented;

        uintptr_t start = baseAddress;
        size_t length = size;
        if (start) {
            const size_t offset = start & PageMask4KB;
            if (length > std::numeric_limits<size_t>::max() - offset - PageMask4KB)
                return NtStatus::InvalidParameter;
            length += offset;
            start &= ~static_cast<uintptr_t>(PageMask4KB);
        }
        length = (length + PageMask4KB) & ~PageMask4KB;
        const bool reserve = (type & MEM_RESERVE) != 0 || baseAddress == 0;
        if (!start) {
            start = vadTree_.findFreeRange(length, AllocationGranularity, (type & MEM_TOP_DOWN) != 0,
                                           backendBase_, backendEnd_);
            if (!start) return NtStatus::NoMemory;
        }
        if (start < backendBase_ || start > backendEnd_ || length - 1 > backendEnd_ - start)
            return NtStatus::InvalidParameter;
        const uintptr_t end = start + length - 1;

        VirtualAddressDescriptor* vad = nullptr;
        if (reserve) {
            std::unique_ptr<VirtualAddressDescriptor> node(new (std::nothrow_t{}) VirtualAddressDescriptor{});
            if (!node) return NtStatus::NoMemory;
            node->startingAddress = start;
            node->endingAddress = end;
            node->protection = protect;
            node->allocationType = type;
            vad = node.get();
            if (!vadTree_.insert(std::move(node))) return NtStatus::Conflict;
        } else {
            vad = vadTree_.find(start);
            if (!vad || end > vad->endingAddress || vad->isSectionView) return NtStatus::Conflict;
        }
        if (!(type & MEM_COMMIT)) {
            baseAddress = start;
            return NtStatus::Success;
        }

        auto& db = PfnDatabase::get();
        std::unique_ptr<VirtualAddressDescriptor::CommitRange> pending;
        NtStatus failure = NtStatus::Success;
        for (uintptr_t va = start;; va += PageSize4KB) {
            const uint64_t vpn = va >> PageShift4KB;
            auto* existing = vad->commitRanges.get();
            while (existing && existing->firstVpn != vpn) existing = existing->next.get();
            if (!existing) {
                std::unique_ptr<VirtualAddressDescriptor::CommitRange> page(
                    new (std::nothrow_t{}) VirtualAddressDescriptor::CommitRange{});
                if (!page) { failure = NtStatus::NoMemory; break; }
                page->firstVpn = page->lastVpn = vpn;
                page->protection = protect;
                page->pfn = db.allocatePage();
                if (!page->pfn) { failure = NtStatus::NoMemory; break; }
                if (!db.zeroAllocatedPage(page->pfn)) {
                    db.freePage(page->pfn);
                    failure = NtStatus::Unsuccessful;
                    break;
                }
                if (!backend_.map(backend_.context, va, page->pfn, protect)) {
                    db.freePage(page->pfn);
                    failure = NtStatus::Unsuccessful;
                    break;
                }
                page->next = std::move(pending);
                pending = std::move(page);
            }
            if (va == (end & ~static_cast<uintptr_t>(PageMask4KB))) break;
        }

        while (pending) {
            auto page = std::move(pending);
            pending = std::move(page->next);
            if (failure != NtStatus::Success &&
                backend_.unmap(backend_.context, page->firstVpn << PageShift4KB)) {
                db.freePage(page->pfn);
                continue;
            }
            page->next = std::move(vad->commitRanges);
            vad->commitRanges = std::move(page);
            ++vad->commitCharge;
            ++workingSet_.commitCharge;
            ++workingSet_.residentPages;
        }
        vad->committed = vad->commitCharge != 0;
        workingSet_.peakCommitCharge = std::max(workingSet_.peakCommitCharge, workingSet_.commitCharge);
        workingSet_.peakResidentPages = std::max(workingSet_.peakResidentPages, workingSet_.residentPages);
        if (failure != NtStatus::Success) {
            if (reserve && vad->commitCharge == 0) vadTree_.remove(start);
            return failure;
        }
        baseAddress = start;
        return NtStatus::Success;
    }

    MmAvlTable vadTree_;
    WorkingSetList workingSet_;
    HardwarePagingEngine pagingEngine_;
    uintptr_t directoryTableBase_{0x1000};
    uintptr_t nextFreeAddress_{0x0000000100000000ULL};
    PagingBackend backend_{};
    uintptr_t backendBase_{0};
    uintptr_t backendEnd_{0};
#if defined(MICANT_FREESTANDING)
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
#else
    mutable std::mutex mutex_;
#endif
};

// ============================================================================
// 8. Top-Level Executive Memory Manager Handlers
// ============================================================================

/**
 * @brief Dispatcher for Vector 14 Page Faults (#PF).
 */
inline NtStatus MmAccessFault(
    ProcessAddressSpace& addressSpace,
    uintptr_t faultingAddress,
    bool isWrite,
    bool isUser,
    bool isExecute
) {
    return addressSpace.handlePageFault(faultingAddress, isWrite, isUser, isExecute);
}

/**
 * @brief Phase 0 / Phase 1 Memory Manager Initialization.
 */
inline NtStatus MmInitSystem(uint32_t phase, const void* /*loaderBlock*/ = nullptr) {
    if (phase == 0) {
        PfnDatabase::get().initialize(262144);
        return NtStatus::Success;
    }
    return NtStatus::Success;
}

// ============================================================================
// 9. Memory Descriptor Lists (MDL) Architecture & Lifecycle
// ============================================================================
enum class LOCK_OPERATION : uint32_t {
    IoReadAccess   = 0,
    IoWriteAccess  = 1,
    IoModifyAccess = 2
};

enum class MEMORY_CACHING_TYPE : uint32_t {
    MmNonCached             = 0,
    MmCached                = 1,
    MmWriteCombined         = 2,
    MmHardwareCoherentCached= 3,
    MmNonCachedUnordered    = 4,
    MmUSWCCached            = 5,
    MmMaximumCacheType      = 6
};

inline constexpr uint8_t KernelMode = 0;
inline constexpr uint8_t UserMode   = 1;
using KPROCESSOR_MODE = uint8_t;

inline constexpr int16_t MDL_MAPPED_TO_SYSTEM_VA       = 0x0001;
inline constexpr int16_t MDL_PAGES_LOCKED              = 0x0002;
inline constexpr int16_t MDL_SOURCE_IS_NONPAGED_POOL   = 0x0004;
inline constexpr int16_t MDL_ALLOCATED_FIXED_SIZE      = 0x0008;
inline constexpr int16_t MDL_PARTIAL                   = 0x0010;
inline constexpr int16_t MDL_IO_PAGE_READ              = 0x0020;
inline constexpr int16_t MDL_WRITE_OPERATION           = 0x0040;
inline constexpr int16_t MDL_PARENT_MAPPED_SYSTEM_VA   = 0x0080;
inline constexpr int16_t MDL_FREE_EXTRA_PTES           = 0x0100;
inline constexpr int16_t MDL_DESCRIBES_AWE             = 0x0200;
inline constexpr int16_t MDL_IO_SPACE                  = 0x0400;
inline constexpr int16_t MDL_NETWORK_HEADER            = 0x0800;
inline constexpr int16_t MDL_MAPPING_CAN_FAIL          = 0x1000;
inline constexpr int16_t MDL_ALLOCATED_MUST_SUCCEED   = 0x2000;

struct MDL {
    struct MDL* next{nullptr};
    int16_t size{0};
    int16_t mdlFlags{0};
    ProcessAddressSpace* process{nullptr};
    void* mappedSystemVa{nullptr};
    void* startVa{nullptr};
    uint32_t byteOffset{0};
    uint32_t byteCount{0};
    std::vector<uint64_t> pages;
};
using PMDL = MDL*;

inline PMDL IoAllocateMdl(
    void* virtualAddress,
    uint32_t length,
    bool secondaryBuffer = false,
    bool chargeQuota = false,
    void* irp = nullptr
) {
    (void)secondaryBuffer;
    (void)chargeQuota;
    (void)irp;

    uintptr_t va = reinterpret_cast<uintptr_t>(virtualAddress);
    uint32_t offset = static_cast<uint32_t>(va & PageMask4KB);
    uintptr_t startPage = va & ~PageMask4KB;
    size_t pageCount = (offset + length + PageSize4KB - 1) >> PageShift4KB;

    auto* mdl = new MDL();
    mdl->size = static_cast<int16_t>(sizeof(MDL) + pageCount * sizeof(uint64_t));
    mdl->mdlFlags = 0;
    mdl->startVa = reinterpret_cast<void*>(startPage);
    mdl->byteOffset = offset;
    mdl->byteCount = length;
    mdl->pages.resize(pageCount, 0);

    return mdl;
}

inline void IoFreeMdl(PMDL mdl) {
    if (!mdl) return;
    delete mdl;
}

inline void MmBuildMdlForNonPagedPool(PMDL mdl) {
    if (!mdl) return;
    mdl->mdlFlags |= (MDL_SOURCE_IS_NONPAGED_POOL | MDL_PAGES_LOCKED);
    mdl->mappedSystemVa = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(mdl->startVa) + mdl->byteOffset
    );

    auto& pfnDb = PfnDatabase::get();
    for (size_t i = 0; i < mdl->pages.size(); ++i) {
        uint64_t pfn = pfnDb.allocatePage(false);
        mdl->pages[i] = pfn;
    }
}

inline NtStatus MmProbeAndLockPages(
    PMDL mdl,
    KPROCESSOR_MODE accessMode,
    LOCK_OPERATION operation
) {
    if (!mdl) return NtStatus::InvalidParameter;
    if (mdl->mdlFlags & MDL_PAGES_LOCKED) return NtStatus::Success;

    (void)accessMode;
    if (operation == LOCK_OPERATION::IoWriteAccess || operation == LOCK_OPERATION::IoModifyAccess) {
        mdl->mdlFlags |= MDL_WRITE_OPERATION;
    }

    auto& pfnDb = PfnDatabase::get();
    for (size_t i = 0; i < mdl->pages.size(); ++i) {
        uint64_t pfn = pfnDb.allocatePage(false);
        mdl->pages[i] = pfn;
    }

    mdl->mdlFlags |= MDL_PAGES_LOCKED;
    return NtStatus::Success;
}

inline void MmUnlockPages(PMDL mdl) {
    if (!mdl) return;
    if ((mdl->mdlFlags & MDL_PAGES_LOCKED) == 0) return;

    auto& pfnDb = PfnDatabase::get();
    for (uint64_t pfn : mdl->pages) {
        pfnDb.freePage(pfn);
    }
    std::fill(mdl->pages.begin(), mdl->pages.end(), 0);
    mdl->mdlFlags &= ~MDL_PAGES_LOCKED;
}

inline void* MmMapLockedPagesSpecifyCache(
    PMDL mdl,
    KPROCESSOR_MODE accessMode,
    MEMORY_CACHING_TYPE cacheType,
    void* requestedAddress,
    uint32_t bugCheckOnFailure,
    uint32_t priority
) {
    (void)accessMode;
    (void)cacheType;
    (void)requestedAddress;
    (void)bugCheckOnFailure;
    (void)priority;

    if (!mdl || (mdl->mdlFlags & MDL_PAGES_LOCKED) == 0) return nullptr;

    mdl->mappedSystemVa = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(mdl->startVa) + mdl->byteOffset
    );
    mdl->mdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;
    return mdl->mappedSystemVa;
}

inline void MmUnmapLockedPages(void* baseAddress, PMDL mdl) {
    (void)baseAddress;
    if (!mdl) return;
    mdl->mappedSystemVa = nullptr;
    mdl->mdlFlags &= ~MDL_MAPPED_TO_SYSTEM_VA;
}

inline void* MmGetSystemAddressForMdlSafe(PMDL mdl, uint32_t priority) {
    if (!mdl) return nullptr;
    if (mdl->mdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL)) {
        return mdl->mappedSystemVa;
    }
    return MmMapLockedPagesSpecifyCache(mdl, KernelMode, MEMORY_CACHING_TYPE::MmCached, nullptr, 0, priority);
}

inline uint32_t MmGetMdlByteCount(const PMDL mdl) {
    return mdl ? mdl->byteCount : 0;
}

inline uint32_t MmGetMdlByteOffset(const PMDL mdl) {
    return mdl ? mdl->byteOffset : 0;
}

inline void* MmGetMdlVirtualAddress(const PMDL mdl) {
    return mdl ? reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mdl->startVa) + mdl->byteOffset) : nullptr;
}

inline uint64_t* MmGetMdlPfnArray(PMDL mdl) {
    return mdl && !mdl->pages.empty() ? mdl->pages.data() : nullptr;
}

// ============================================================================
// 10. Contiguous Physical Memory Allocator
// ============================================================================
inline void* MmAllocateContiguousMemory(size_t numberOfBytes, uintptr_t highestAcceptableAddress) {
    (void)highestAcceptableAddress;
    size_t pageCount = (numberOfBytes + PageSize4KB - 1) >> PageShift4KB;
    uint64_t startPfn = PfnDatabase::get().allocateContiguousPages(pageCount);
    if (startPfn == 0) return nullptr;
    return reinterpret_cast<void*>(startPfn << PageShift4KB);
}

inline void* MmAllocateContiguousMemorySpecifyCache(
    size_t numberOfBytes,
    uintptr_t lowestAcceptableAddress,
    uintptr_t highestAcceptableAddress,
    uintptr_t boundaryAddressMultiple,
    MEMORY_CACHING_TYPE cacheType
) {
    (void)lowestAcceptableAddress;
    (void)highestAcceptableAddress;
    (void)boundaryAddressMultiple;
    (void)cacheType;
    return MmAllocateContiguousMemory(numberOfBytes, highestAcceptableAddress);
}

inline void MmFreeContiguousMemory(void* baseAddress) {
    if (!baseAddress) return;
    uint64_t startPfn = reinterpret_cast<uintptr_t>(baseAddress) >> PageShift4KB;
    PfnDatabase::get().freePage(startPfn);
}

inline void MmFreeContiguousMemorySpecifyCache(void* baseAddress, size_t numberOfBytes, MEMORY_CACHING_TYPE cacheType) {
    (void)cacheType;
    if (!baseAddress || numberOfBytes == 0) return;
    size_t pageCount = (numberOfBytes + PageSize4KB - 1) >> PageShift4KB;
    uint64_t startPfn = reinterpret_cast<uintptr_t>(baseAddress) >> PageShift4KB;
    PfnDatabase::get().freeContiguousPages(startPfn, pageCount);
}

// ============================================================================
// 11. Working Set Trimming & Modified Page Writer
// ============================================================================
inline size_t MmTrimProcessWorkingSet(ProcessAddressSpace& addressSpace, size_t pagesToTrim) {
    return addressSpace.trimWorkingSet(pagesToTrim);
}

inline size_t MmFlushModifiedPageList() {
    return PfnDatabase::get().flushModifiedPages();
}

// ============================================================================
// 12. Memory Manager Telemetry & Diagnostics
// ============================================================================
struct MM_SYSTEM_DIAGNOSTICS {
    size_t totalPhysicalPages{0};
    size_t activePages{0};
    size_t zeroedPages{0};
    size_t freePages{0};
    size_t standbyPages{0};
    size_t modifiedPages{0};
};

inline MM_SYSTEM_DIAGNOSTICS MmQuerySystemMemoryDiagnostics() {
    auto& pfnDb = PfnDatabase::get();
    MM_SYSTEM_DIAGNOSTICS diag{};
    diag.totalPhysicalPages = pfnDb.getTotalPages();
    diag.activePages = pfnDb.getActiveCount();
    diag.zeroedPages = pfnDb.getZeroedCount();
    diag.freePages = pfnDb.getFreeCount();
    diag.standbyPages = pfnDb.getStandbyCount();
    diag.modifiedPages = pfnDb.getModifiedCount();
    return diag;
}

inline void MmResetDiagnosticsForTesting() {
    PfnDatabase::get().resetForTesting();
}

} // namespace micant::mm
