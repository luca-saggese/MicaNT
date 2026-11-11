#pragma once

#include <cstddef>
#include <cstdint>

namespace micant::ex {

using BootstrapLogRoutine = void (*)(const char*) noexcept;
using BootstrapMapPagesRoutine = void* (*)(uint64_t firstPfn, size_t pageCount) noexcept;

// Bootstrap-only: single-CPU with interrupts disabled; not IRQL or scheduler support.
bool initializeBootstrapRuntime(
    BootstrapLogRoutine log,
    BootstrapMapPagesRoutine mapPages
) noexcept;
bool verifyBootstrapRuntime() noexcept;

} // namespace micant::ex
