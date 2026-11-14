/**
 * @file comctl32.cpp
 * @brief Sovereign Common Controls Subsystem DLL (comctl32.dll) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 */

#include <cstdint>

extern "C" {

bool DllMain(void*, unsigned long, void*) noexcept { return true; }

__declspec(dllexport) bool InitCommonControlsEx(const void*) noexcept { return false; }
__declspec(dllexport) void* CreatePropertySheetPageW(const void*) noexcept { return nullptr; }
__declspec(dllexport) int64_t PropertySheetW(const void*) noexcept { return 0; }

} // extern "C"
