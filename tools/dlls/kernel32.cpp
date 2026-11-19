/**
 * @file kernel32.cpp
 * @brief Compatibility source name for the canonical Kernel32 DLL wrapper.
 *
 * The former direct syscall implementation used placeholder process, file,
 * and standard-stream handles. Keep stage builds on the same native-backed
 * per-DLL ABI implementation as `kernel32_boot`.
 */
#include "abi/kernel32_system32.cpp"
