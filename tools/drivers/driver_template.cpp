/**
 * @file driver_template.cpp
 * Luce OS Kernel Driver stub for MicaNT System32 Drivers.
 *
 * Freestanding ISO C++23. Clean-Room design.
 */

#include <cstdint>

extern "C" {

int64_t DriverEntry(void* /*DriverObject*/, void* /*RegistryPath*/) noexcept {
    // NTSTATUS STATUS_SUCCESS = 0
    return 0;
}

} // extern "C"
