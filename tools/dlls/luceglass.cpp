#define MICANT_WIN32K_IMPORTS
#include "micant/shared/win32k_client.h"

extern "C" __declspec(dllexport) int32_t LuceGlassSetWindow(
    void* hwnd, const MicaLiquidGlassDescriptor* descriptor) noexcept {
    if (!hwnd || !descriptor) return 0;
    return MicaWin32kSetLiquidGlass(hwnd, descriptor);
}

extern "C" __declspec(dllexport) int32_t LuceGlassClearWindow(
    void* hwnd, uint64_t generation) noexcept {
    if (!hwnd || !generation) return 0;
    MicaLiquidGlassDescriptor descriptor{};
    descriptor.magic = MICANT_LIQUID_GLASS_MAGIC;
    descriptor.version = MICANT_LIQUID_GLASS_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.generation = generation;
    return MicaWin32kSetLiquidGlass(hwnd, &descriptor);
}
