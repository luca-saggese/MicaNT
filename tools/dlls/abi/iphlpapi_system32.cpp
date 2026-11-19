/**
 * @file iphlpapi_system32.cpp
 * @brief iphlpapi DLL translation unit for the shared ABI implementation.
 */
#include "ntdll_ws2_iphlpapi_winhttp_impl.inc"

extern "C" int __stdcall DllMainCRTStartup(void*, unsigned long, void*) noexcept {
    return 1;
}
