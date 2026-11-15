#include "micant/km/executive/perf_taskmgr.hpp"

#include "micant/km/executive/mm.hpp"

#include <limits>

namespace micant::km::perf {
namespace {

bool pagesToBytes(size_t pages, uint64_t& bytes) noexcept {
    constexpr uint64_t pageSize = static_cast<uint64_t>(micant::mm::PageSize4KB);
    if (pages > std::numeric_limits<uint64_t>::max() / pageSize) {
        bytes = 0;
        return false;
    }
    bytes = static_cast<uint64_t>(pages) * pageSize;
    return true;
}

} // namespace

MemorySnapshot QuerySystemMemorySnapshot() noexcept {
    MemorySnapshot snapshot{};
    const auto diagnostics = micant::mm::MmQuerySystemMemoryDiagnostics();
    if (diagnostics.totalPhysicalPages == 0) return snapshot;

    if (!pagesToBytes(diagnostics.totalPhysicalPages, snapshot.usableTotalBytes) ||
        !pagesToBytes(diagnostics.activePages, snapshot.activeBytes) ||
        !pagesToBytes(diagnostics.zeroedPages, snapshot.zeroedBytes) ||
        !pagesToBytes(diagnostics.freePages, snapshot.freeBytes) ||
        !pagesToBytes(diagnostics.standbyPages, snapshot.standbyBytes) ||
        !pagesToBytes(diagnostics.modifiedPages, snapshot.modifiedBytes)) {
        return MemorySnapshot{};
    }

    snapshot.availability = MemorySnapshotAvailableTotal |
                            MemorySnapshotAvailableActive |
                            MemorySnapshotAvailableZeroed |
                            MemorySnapshotAvailableFree |
                            MemorySnapshotAvailableStandby |
                            MemorySnapshotAvailableModified;
    return snapshot;
}

} // namespace micant::km::perf
