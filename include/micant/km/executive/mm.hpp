#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <optional>
#include <atomic>
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
            // This bootstrap-only lock is valid only while interrupts are disabled.
            while (lock_.test_and_set(std::memory_order_acquire)) {}
#endif
        }

        ~Guard() noexcept {
#if defined(MICANT_FREESTANDING)
            lock_.clear(std::memory_order_release);
#endif
        }

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
#if defined(MICANT_FREESTANDING)
        std::atomic_flag& lock_;
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

    [[nodiscard]] uint64_t allocateContiguousPages(size_t pageCount) {
        Guard guard(*this);
        if (pageCount == 0 || pageCount > entries_.size()) return 0;

        size_t consecutive = 0;
        uint64_t candidateStart = 0;

        for (uint64_t i = 1; i < entries_.size(); ++i) {
            if (entries_[i].location == PageLocation::FreePageList ||
                entries_[i].location == PageLocation::ZeroedPageList) {
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
    void*     sectionContext{nullptr};
    uint64_t  sectionOffset{0};
    size_t    commitCharge{0};

    // AVL Tree Topology
    VirtualAddressDescriptor* parent{nullptr};
    std::unique_ptr<VirtualAddressDescriptor> left{nullptr};
    std::unique_ptr<VirtualAddressDescriptor> right{nullptr};
    int height{1};

    // Demand-Paged Physical Page Frame Map (VPN -> PFN)
    std::unordered_map<uint64_t, uint64_t> physicalPages;

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
    mutable std::mutex mutex_;

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

            // Node with two children
            VirtualAddressDescriptor* temp = findMin(node->right.get());
            // Copy data from successor
            uintptr_t succStart = temp->startingAddress;
            node->startingAddress = temp->startingAddress;
            node->endingAddress = temp->endingAddress;
            node->startingVpn = temp->startingVpn;
            node->endingVpn = temp->endingVpn;
            node->protection = temp->protection;
            node->allocationType = temp->allocationType;
            node->committed = temp->committed;
            node->isCopyOnWrite = temp->isCopyOnWrite;
            node->isGuardPage = temp->isGuardPage;
            node->commitCharge = temp->commitCharge;
            node->physicalPages = std::move(temp->physicalPages);

            bool subRemoved = false;
            node->right = removeNode(std::move(node->right), succStart, subRemoved);
        }

        updateHeight(node.get());
        int balance = getBalanceFactor(node.get());

        if (balance > 1 && getBalanceFactor(node->left.get()) >= 0) {
            return rotateRight(std::move(node));
        }
        if (balance > 1 && getBalanceFactor(node->left.get()) < 0) {
            node->left = rotateLeft(std::move(node->left));
            return rotateRight(std::move(node));
        }
        if (balance < -1 && getBalanceFactor(node->right.get()) <= 0) {
            return rotateLeft(std::move(node));
        }
        if (balance < -1 && getBalanceFactor(node->right.get()) > 0) {
            node->right = rotateRight(std::move(node->right));
            return rotateLeft(std::move(node));
        }

        return node;
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
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<VirtualAddressDescriptor*> result;
        collectInOrder(root_.get(), result);
        return result;
    }

    void insert(std::unique_ptr<VirtualAddressDescriptor> vad) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!vad) return;
        vad->startingVpn = vad->startingAddress >> PageShift4KB;
        vad->endingVpn = vad->endingAddress >> PageShift4KB;
        root_ = insertNode(std::move(root_), std::move(vad));
    }

    bool remove(uintptr_t startingAddress) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool removed = false;
        root_ = removeNode(std::move(root_), startingAddress, removed);
        return removed;
    }

    [[nodiscard]] VirtualAddressDescriptor* find(uintptr_t address) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
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
        std::lock_guard<std::mutex> lock(mutex_);
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

    [[nodiscard]] uintptr_t findFreeRange(
        size_t size,
        size_t alignment = PageSize4KB,
        bool topDown = false,
        uintptr_t minAddr = 0x0000000000010000ULL,
        uintptr_t maxAddr = 0x00007FFFFFFFFFFFULL
    ) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<VirtualAddressDescriptor*> sorted;
        collectInOrder(root_.get(), sorted);

        size_t align = std::max(alignment, PageSize4KB);
        size_t alignedSize = (size + PageMask4KB) & ~PageMask4KB;

        if (!topDown) {
            // Bottom-Up Search
            uintptr_t candidate = (minAddr + align - 1) & ~(align - 1);
            for (const auto* vad : sorted) {
                if (candidate + alignedSize <= vad->startingAddress) {
                    return candidate;
                }
                candidate = (vad->endingAddress + 1 + align - 1) & ~(align - 1);
            }
            if (candidate + alignedSize <= maxAddr) {
                return candidate;
            }
        } else {
            // Top-Down Search
            uintptr_t candidate = (maxAddr - alignedSize) & ~(align - 1);
            for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
                const auto* vad = *it;
                if (candidate >= vad->endingAddress) {
                    return candidate;
                }
                if (vad->startingAddress >= alignedSize) {
                    candidate = (vad->startingAddress - alignedSize) & ~(align - 1);
                } else {
                    break;
                }
            }
            if (candidate >= minAddr) {
                return candidate;
            }
        }

        return 0; // Out of virtual address space
    }

    [[nodiscard]] size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
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

    ProcessAddressSpace() : nextFreeAddress_(0x0000000100000000ULL) {}

    NtStatus allocate(uintptr_t& baseAddress, size_t size, uint32_t allocationType, uint32_t protect) {
        if (size == 0) return NtStatus::InvalidParameter;

        std::lock_guard<std::mutex> lock(mutex_);
        size_t alignedSize = (size + PageMask4KB) & ~PageMask4KB;

        uintptr_t targetAddr = baseAddress;
        if (targetAddr == 0) {
            bool topDown = (allocationType & MEM_TOP_DOWN) != 0;
            targetAddr = vadTree_.findFreeRange(alignedSize, PageSize4KB, topDown, nextFreeAddress_, UserSpaceMax);
            if (targetAddr == 0) {
                targetAddr = vadTree_.findFreeRange(alignedSize, PageSize4KB, topDown, UserSpaceMin, UserSpaceMax);
            }
            if (targetAddr == 0) {
                return NtStatus::NoMemory;
            }
            nextFreeAddress_ = targetAddr + alignedSize + PageSize4KB;
        }

        auto vad = std::make_unique<VirtualAddressDescriptor>();
        vad->startingAddress = targetAddr;
        vad->endingAddress = targetAddr + alignedSize - 1;
        vad->protection = protect;
        vad->allocationType = allocationType;
        vad->committed = (allocationType & MEM_COMMIT) != 0;
        vad->isCopyOnWrite = (protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_WRITECOPY);
        vad->isGuardPage = (protect & PAGE_GUARD) != 0;
        vad->commitCharge = vad->committed ? vad->pageCount() : 0;

        if (vad->committed) {
            workingSet_.commitCharge += vad->commitCharge;
            workingSet_.peakCommitCharge = std::max(workingSet_.peakCommitCharge, workingSet_.commitCharge);
        }

        vadTree_.insert(std::move(vad));
        baseAddress = targetAddr;
        return NtStatus::Success;
    }

    NtStatus mapSectionView(
        uintptr_t& baseAddress,
        size_t size,
        uint32_t protect,
        const std::unordered_map<size_t, uint64_t>& sharedPfns,
        void* sectionContext = nullptr,
        uint64_t sectionOffset = 0
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size == 0) return NtStatus::InvalidParameter;

        size_t alignedSize = (size + PageMask4KB) & ~PageMask4KB;
        uintptr_t targetAddr = baseAddress;

        if (targetAddr == 0) {
            targetAddr = nextFreeAddress_;
            nextFreeAddress_ += alignedSize + PageSize4KB;
        } else {
            if (targetAddr & PageMask4KB) return NtStatus::InvalidParameter;
            if (vadTree_.find(targetAddr)) return NtStatus::Conflict;
        }

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
        vad->sectionOffset = sectionOffset;

        auto& pfnDb = PfnDatabase::get();
        size_t pageOffset = sectionOffset >> PageShift4KB;
        size_t numPages = alignedSize >> PageShift4KB;

        for (size_t i = 0; i < numPages; ++i) {
            uint64_t vpn = vad->startingVpn + i;
            auto it = sharedPfns.find(pageOffset + i);
            if (it != sharedPfns.end()) {
                uint64_t pfn = it->second;
                vad->physicalPages[vpn] = pfn;
                pfnDb.referencePage(pfn);
                pagingEngine_.mapPage(directoryTableBase_, vpn << PageShift4KB, pfn, protect, true);
                workingSet_.residentPages++;
            }
        }

        workingSet_.commitCharge += vad->commitCharge;
        workingSet_.peakCommitCharge = std::max(workingSet_.peakCommitCharge, workingSet_.commitCharge);
        workingSet_.peakResidentPages = std::max(workingSet_.peakResidentPages, workingSet_.residentPages);

        vadTree_.insert(std::move(vad));
        baseAddress = targetAddr;
        return NtStatus::Success;
    }

    NtStatus free(uintptr_t baseAddress, size_t size, uint32_t freeType) {
        std::lock_guard<std::mutex> lock(mutex_);
        VirtualAddressDescriptor* vad = vadTree_.find(baseAddress);
        if (!vad) return NtStatus::InvalidParameter;

        if (freeType & MEM_RELEASE) {
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

            vadTree_.remove(vad->startingAddress);
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
        std::lock_guard<std::mutex> lock(mutex_);
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

    [[nodiscard]] size_t getRegionCount() const noexcept {
        return vadTree_.size();
    }

    [[nodiscard]] VirtualAddressDescriptor* findVad(uintptr_t addr) noexcept {
        return vadTree_.find(addr);
    }

    [[nodiscard]] const VirtualAddressDescriptor* findVad(uintptr_t addr) const noexcept {
        return vadTree_.find(addr);
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
        std::lock_guard<std::mutex> lock(mutex_);

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

    [[nodiscard]] const WorkingSetList& getWorkingSet() const noexcept { return workingSet_; }
    [[nodiscard]] size_t getCommitCharge() const noexcept { return workingSet_.commitCharge; }
    [[nodiscard]] size_t getResidentPages() const noexcept { return workingSet_.residentPages; }
    [[nodiscard]] uintptr_t getDirectoryTableBase() const noexcept { return directoryTableBase_; }
    void setDirectoryTableBase(uintptr_t cr3) noexcept { directoryTableBase_ = cr3; }

    size_t trimWorkingSet(size_t targetPages) {
        std::lock_guard<std::mutex> lock(mutex_);
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
    MmAvlTable vadTree_;
    WorkingSetList workingSet_;
    HardwarePagingEngine pagingEngine_;
    uintptr_t directoryTableBase_{0x1000};
    uintptr_t nextFreeAddress_{0x0000000100000000ULL};
    mutable std::mutex mutex_;
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
