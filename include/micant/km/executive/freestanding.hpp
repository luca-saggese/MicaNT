#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::ex {

using BootstrapLogRoutine = void (*)(const char*) noexcept;
using BootstrapReserveHeapRangeRoutine = uintptr_t (*)(size_t pageCount) noexcept;
using BootstrapReleaseHeapRangeRoutine = bool (*)(uintptr_t address, size_t pageCount) noexcept;
using BootstrapMapHeapPageRoutine = bool (*)(uintptr_t address, uint64_t pfn) noexcept;
using BootstrapUnmapHeapPageRoutine = bool (*)(uintptr_t address) noexcept;

// Bootstrap-only: single-CPU with interrupts disabled; not IRQL or scheduler support.
bool initializeBootstrapRuntime(
    BootstrapLogRoutine log,
    BootstrapReserveHeapRangeRoutine reserveRange,
    BootstrapReleaseHeapRangeRoutine releaseRange,
    BootstrapMapHeapPageRoutine mapPage,
    BootstrapUnmapHeapPageRoutine unmapPage
) noexcept;
bool verifyBootstrapRuntime() noexcept;

// Private kernel lifecycle, under the same single-CPU/IRQ-off bootstrap contract.
// Initialize AFTER the heap and BEFORE using any dynamically initialized global.
// Initialization is one-shot. A failed C initializer permits teardown, not retry.
bool initializeBootstrapCxxRuntime() noexcept;
// Final shutdown only: globals become unusable; the PFN heap must remain available.
// Reentrant/repeated dispatch returns false. Never call this on a continuing boot.
bool shutdownBootstrapCxxRuntime() noexcept;
// Registration storage is bounded; overflow/invalid-phase atexit calls are fatal,
// because compiler-generated destructor registration does not check failure.
[[noreturn]] void stopBootstrapRuntime(const char* message) noexcept;

#if defined(MICANT_KERNEL_CXX_LIFECYCLE_SELFTEST)
namespace test {
bool verifyCxxBeforeInitialization() noexcept;
bool verifyCxxAfterInitialization() noexcept;
bool verifyCxxAfterDestruction() noexcept;
} // namespace test
#endif

} // namespace micant::ex
