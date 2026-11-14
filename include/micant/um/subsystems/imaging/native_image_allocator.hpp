#pragma once

// Ring-3 allocator backed by the canonical NT virtual-memory syscalls.
#include "image_decoder.hpp"
#include "micant/shared/native_nt_api.hpp"

namespace micant::um::imaging {

inline void* AllocateImagePages(void*, size_t bytes) noexcept {
    if (!bytes) return nullptr;
    void* address{};
    size_t regionSize = bytes;
    const NtStatus status = native_nt::NtAllocateVirtualMemory(
        static_cast<Handle>(-1), &address, 0, &regionSize, 0x3000, 0x04);
    return status == NtStatus::Success ? address : nullptr;
}

inline void FreeImagePages(void*, void* address, size_t) noexcept {
    if (!address) return;
    size_t regionSize{};
    (void)native_nt::NtFreeVirtualMemory(static_cast<Handle>(-1), &address,
                                         &regionSize, 0x8000);
}

inline constexpr ImageAllocator NativeImageAllocator() noexcept {
    return {nullptr, &AllocateImagePages, &FreeImagePages};
}

} // namespace micant::um::imaging
