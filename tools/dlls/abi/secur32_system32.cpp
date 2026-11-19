/**
 * @file secur32_system32.cpp
 * @brief secur32 DLL translation unit for the shared ABI implementation.
 */
#include "bcrypt_crypt32_secur32_wintrust_impl.inc"

extern "C" int __stdcall DllMainCRTStartup(void*, unsigned long, void*) noexcept {
    return 1;
}
