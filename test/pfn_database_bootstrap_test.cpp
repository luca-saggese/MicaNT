#include "micant/km/executive/mm.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

using namespace micant::mm;

constexpr size_t TestPageCount = 10;
using PhysicalPage = std::array<std::byte, PageSize4KB>;

std::array<PhysicalPage, TestPageCount> physicalBacking{};

void ZeroPhysicalPage(uint64_t pfn) noexcept {
    if (pfn >= physicalBacking.size()) return;
    physicalBacking[static_cast<size_t>(pfn)].fill(std::byte{0});
}

void FillPhysicalBacking(std::byte value) {
    for (auto& page : physicalBacking) page.fill(value);
}

void TestBootMemoryInitialization() {
    auto& database = PfnDatabase::get();
#if defined(MICANT_FREESTANDING)
    assert(database.getTotalPages() == 0);
    assert(database.allocatePage() == 0);
#endif
    std::array<Mmpfn, TestPageCount> metadata{};
    const std::array<PhysicalPageRange, 3> ranges{{
        {0, 3},
        {4, 2},
        {8, 2},
    }};

    FillPhysicalBacking(std::byte{0xA5});
    assert(database.initializeFromBootMemory(metadata, ranges, ZeroPhysicalPage) == micant::NtStatus::Success);
    assert(database.getTotalPages() == TestPageCount);
    assert(database.getFreeCount() == 6);
    assert(database.getZeroedCount() == 0);
    assert(metadata[0].location == PageLocation::BadPageList);
    assert(metadata[3].location == PageLocation::BadPageList);
    assert(metadata[6].location == PageLocation::BadPageList);
    assert(metadata[7].location == PageLocation::BadPageList);
    database.freePage(0);
    database.freePage(3);
    database.referencePage(6);
    assert(database.getFreeCount() == 6);

    database.zeroPage(0);
    assert(physicalBacking[0][0] == std::byte{0xA5});
    database.zeroPage(4);
    for (std::byte value : physicalBacking[4]) assert(value == std::byte{0});
    assert(metadata[4].location == PageLocation::ZeroedPageList);
    assert(database.getZeroedCount() == 1);
    assert(database.getFreeCount() == 5);

    const uint64_t page = database.allocatePage();
    assert(page == 4);
    database.referencePage(page);
    database.freePage(page);
    assert(database.getActiveCount() == 1);
    database.freePage(page);
    assert(database.getActiveCount() == 0);
    assert(database.getFreeCount() == 6);

    assert(database.allocatePage(false) == 1);
    database.setModified(1, true);
    database.freePage(1);
    assert(database.getModifiedCount() == 1);
    assert(database.flushModifiedPages() == 1);
    assert(database.getStandbyCount() == 1);

    assert(database.allocateContiguousPages(3) == 0);
    const uint64_t contiguous = database.allocateContiguousPages(2);
    assert(contiguous == 4);
    assert(database.getActiveCount() == 2);
    database.freeContiguousPages(contiguous, 2);
    assert(database.getActiveCount() == 0);

    const size_t freeBeforeInvalidInit = database.getFreeCount();
    const size_t zeroedBeforeInvalidInit = database.getZeroedCount();
    const std::array<PhysicalPageRange, 2> overlapping{{{1, 3}, {3, 2}}};
    const std::array<PhysicalPageRange, 1> outOfBounds{{{9, 2}}};
    const std::array<PhysicalPageRange, 1> overflowing{{{UINT64_MAX - 1, 4}}};
    const std::array<PhysicalPageRange, 1> emptyRange{{{2, 0}}};
    assert(database.initializeFromBootMemory(metadata, overlapping, ZeroPhysicalPage) == micant::NtStatus::InvalidParameter);
    assert(database.initializeFromBootMemory(metadata, outOfBounds, ZeroPhysicalPage) == micant::NtStatus::InvalidParameter);
    assert(database.initializeFromBootMemory(metadata, overflowing, ZeroPhysicalPage) == micant::NtStatus::InvalidParameter);
    assert(database.initializeFromBootMemory(metadata, emptyRange, ZeroPhysicalPage) == micant::NtStatus::InvalidParameter);
    assert(database.initializeFromBootMemory(metadata, ranges, nullptr) == micant::NtStatus::InvalidParameter);
    assert(database.getTotalPages() == TestPageCount);
    assert(database.getFreeCount() == freeBeforeInvalidInit);
    assert(database.getZeroedCount() == zeroedBeforeInvalidInit);
    assert(metadata[1].location == PageLocation::StandbyPageList);

    std::array<Mmpfn, 4> reinitializedMetadata{};
    const std::array<PhysicalPageRange, 2> reinitializedRanges{{{1, 1}, {3, 1}}};
    assert(database.initializeFromBootMemory(reinitializedMetadata, reinitializedRanges, ZeroPhysicalPage) == micant::NtStatus::Success);
    assert(database.getTotalPages() == reinitializedMetadata.size());
    assert(database.getFreeCount() == 2);
    assert(database.allocatePage(false) == 1);
    assert(database.allocatePage(false) == 3);
    assert(database.allocatePage(false) == 0);
    assert(database.allocateContiguousPages(2) == 0);
}

#if !defined(MICANT_FREESTANDING)
void TestHostedLegacyInitialization() {
    auto& database = PfnDatabase::get();
    database.initialize(1024);
    assert(database.getTotalPages() == 1024);
    assert(database.getFreeCount() == 384);
    assert(database.getZeroedCount() == 384);
    assert(database.allocatePage() == 256);
    database.resetForTesting();
    assert(database.getTotalPages() == 262144);
}
#endif

} // namespace

int main() {
    TestBootMemoryInitialization();
#if !defined(MICANT_FREESTANDING)
    TestHostedLegacyInitialization();
#endif
    return 0;
}
