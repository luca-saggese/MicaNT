#pragma once

#include <cstdint>

namespace micant::km::perf {

inline constexpr uint32_t MemorySnapshotVersion = 1;

enum MemorySnapshotAvailability : uint32_t {
    MemorySnapshotAvailableNone       = 0,
    MemorySnapshotAvailableTotal      = 1u << 0,
    MemorySnapshotAvailableActive     = 1u << 1,
    MemorySnapshotAvailableZeroed    = 1u << 2,
    MemorySnapshotAvailableFree       = 1u << 3,
    MemorySnapshotAvailableStandby    = 1u << 4,
    MemorySnapshotAvailableModified   = 1u << 5,
};

// Fixed-size, kernel-private snapshot. `usableTotalBytes` is the PFN
// database's tracked usable physical memory; it is not firmware-installed RAM.
// The remaining fields are disjoint PFN page-list/accounting totals reported by
// Mm. A zero availability mask means Mm has not published a usable snapshot or
// a checked page-to-byte conversion failed.
struct MemorySnapshot {
    uint32_t version{MemorySnapshotVersion};
    uint32_t size{64};
    uint32_t availability{MemorySnapshotAvailableNone};
    uint32_t reserved{0};
    uint64_t usableTotalBytes{0};
    uint64_t activeBytes{0};
    uint64_t zeroedBytes{0};
    uint64_t freeBytes{0};
    uint64_t standbyBytes{0};
    uint64_t modifiedBytes{0};
};

static_assert(sizeof(MemorySnapshot) == 64);

// Captures only the canonical Mm PFN diagnostics. No installed-memory,
// commit, pool, CPU, device, or per-process values are inferred here.
[[nodiscard]] MemorySnapshot QuerySystemMemorySnapshot() noexcept;

inline constexpr uint32_t ProcessorPerformanceAvailableNone = 0;
inline constexpr uint32_t ProcessorPerformanceAvailableTimes = 1u << 0;
struct ProcessorPerformanceSnapshot {
    uint32_t version{1};
    uint32_t size{40};
    uint32_t availability{ProcessorPerformanceAvailableNone};
    uint32_t processorCount{};
    uint64_t idleTime100ns{};
    uint64_t kernelTime100ns{};
    uint64_t userTime100ns{};
};
static_assert(sizeof(ProcessorPerformanceSnapshot) == 40);
// Reports tick-sampled BSP times while the canonical scheduler clock runs.
[[nodiscard]] ProcessorPerformanceSnapshot QueryProcessorPerformanceSnapshot() noexcept;

} // namespace micant::km::perf
