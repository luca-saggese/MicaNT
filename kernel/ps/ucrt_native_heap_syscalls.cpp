#include "micant/shared/native_nt_api.hpp"

extern "C" int32_t __stdcall MicantUcrtAllocateVirtualMemory(
    void** baseAddress, size_t* regionSize, uint32_t allocationType,
    uint32_t protection) noexcept {
    return static_cast<int32_t>(micant::native_nt::NtAllocateVirtualMemory(
        static_cast<micant::Handle>(-1), baseAddress, 0, regionSize,
        allocationType, protection));
}

extern "C" int32_t __stdcall MicantUcrtFreeVirtualMemory(
    void** baseAddress, size_t* regionSize, uint32_t freeType) noexcept {
    return static_cast<int32_t>(micant::native_nt::NtFreeVirtualMemory(
        static_cast<micant::Handle>(-1), baseAddress, regionSize, freeType));
}
