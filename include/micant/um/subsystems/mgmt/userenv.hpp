// ============================================================================
// MicaNT: Windows User Environment Subsystem (userenv.dll)
// (userenv.hpp)
//
// Modern Clean-Room Implementation in pure ISO C++23.
// Provides User Profile Directory path discovery conforming to win32metadata.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstring>
#include "micant/shared/ntdef.hpp"
#include "micant/um/native/ldr.hpp"

namespace micant::userenv {

using BOOL = int32_t;
using DWORD = uint32_t;
using HANDLE = void*;

inline BOOL WINAPI GetUserProfileDirectoryW(HANDLE hToken, wchar_t* lpProfileDir, DWORD* lpcchSize) noexcept {
    (void)hToken;
    (void)lpProfileDir;
    if (!lpcchSize) return 0;
    // Profile lookup is not implemented until token identity is connected to
    // the per-SID profile manager. Never report the old shared admin path as
    // if it belonged to the supplied token.
    *lpcchSize = 0;
    return 0;
}

inline void InitializeUserenvSubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("userenv.dll", "GetUserProfileDirectoryW", reinterpret_cast<void*>(GetUserProfileDirectoryW));
}

} // namespace micant::userenv
