/**
 * @file wintrust_system32.cpp
 * @brief wintrust DLL translation unit for the shared ABI implementation.
 */
#include "bcrypt_crypt32_secur32_wintrust_impl.inc"

extern "C" int __stdcall DllMainCRTStartup(void*, unsigned long, void*) noexcept {
    return 1;
}
