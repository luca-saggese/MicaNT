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
#include <array>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/native_nt_api.hpp"
#ifndef MICANT_USERENV_NO_EXPORT_REGISTRATION
#include "micant/um/native/ldr.hpp"
#endif

namespace micant::userenv {

using BOOL = int32_t;
using DWORD = uint32_t;
using HANDLE = void*;

inline BOOL WINAPI GetUserProfileDirectoryW(HANDLE hToken, wchar_t* lpProfileDir, DWORD* lpcchSize) noexcept {
    if (!lpcchSize) return 0;

    // Current profile names are derived from the caller's real token SID. The
    // native query currently accepts the current-process token pseudo handle;
    // other token handles fail through NtQueryInformationToken instead of
    // being mapped to a shared default account.
    constexpr size_t kMaxTokenInformation =
        sizeof(micant::native_nt::NativeTokenUser) + 8 + 8 * sizeof(uint32_t);
    std::array<uint8_t, kMaxTokenInformation> tokenInformation{};
    uint32_t returnedBytes = 0;
    const auto queryStatus = micant::native_nt::NtQueryInformationToken(
        reinterpret_cast<micant::Handle>(hToken),
        micant::native_nt::TokenUserInformationClass,
        tokenInformation.data(), static_cast<uint32_t>(tokenInformation.size()),
        &returnedBytes);
    if (queryStatus != micant::NtStatus::Success ||
        returnedBytes < sizeof(micant::native_nt::NativeTokenUser) + 8 ||
        returnedBytes > tokenInformation.size()) return 0;

    micant::native_nt::NativeTokenUser tokenUser{};
    std::memcpy(&tokenUser, tokenInformation.data(), sizeof(tokenUser));
    const uintptr_t bufferBegin = reinterpret_cast<uintptr_t>(tokenInformation.data());
    const uintptr_t bufferEnd = bufferBegin + returnedBytes;
    const uintptr_t sidAddress = reinterpret_cast<uintptr_t>(tokenUser.user.sid);
    if (bufferEnd < bufferBegin ||
        sidAddress < bufferBegin + sizeof(tokenUser) || sidAddress > bufferEnd - 8)
        return 0;

    const auto* sid = reinterpret_cast<const uint8_t*>(sidAddress);
    const uint8_t revision = sid[0];
    const uint8_t subAuthorityCount = sid[1];
    if (revision != 1 || subAuthorityCount > 8) return 0;
    const size_t sidBytes = 8u + static_cast<size_t>(subAuthorityCount) * sizeof(uint32_t);
    if (sidAddress > bufferEnd || sidBytes > bufferEnd - sidAddress) return 0;

    auto appendDecimal = [](wchar_t* output, size_t capacity, size_t& cursor,
                            uint64_t value) noexcept {
        wchar_t reverse[20]{};
        size_t digits = 0;
        do {
            reverse[digits++] = static_cast<wchar_t>(L'0' + value % 10u);
            value /= 10u;
        } while (value && digits < sizeof(reverse) / sizeof(reverse[0]));
        if (cursor > capacity || digits > capacity - cursor) return false;
        while (digits) output[cursor++] = reverse[--digits];
        return true;
    };

    constexpr size_t kProfilePathCapacity = 128;
    wchar_t profilePath[kProfilePathCapacity]{};
    size_t cursor = 0;
    constexpr wchar_t prefix[] = L"C:\\Users\\";
    for (size_t i = 0; i < sizeof(prefix) / sizeof(prefix[0]) - 1; ++i)
        profilePath[cursor++] = prefix[i];
    profilePath[cursor++] = L'S';
    profilePath[cursor++] = L'-';
    if (!appendDecimal(profilePath, kProfilePathCapacity - 1,
                       cursor, revision)) return 0;
    profilePath[cursor++] = L'-';
    uint64_t authority = 0;
    for (size_t i = 2; i < 8; ++i) authority = (authority << 8) | sid[i];
    if (!appendDecimal(profilePath, kProfilePathCapacity - 1,
                       cursor, authority)) return 0;
    for (uint8_t index = 0; index < subAuthorityCount; ++index) {
        const size_t offset = 8u + static_cast<size_t>(index) * sizeof(uint32_t);
        const uint32_t subAuthority = static_cast<uint32_t>(sid[offset]) |
            (static_cast<uint32_t>(sid[offset + 1]) << 8) |
            (static_cast<uint32_t>(sid[offset + 2]) << 16) |
            (static_cast<uint32_t>(sid[offset + 3]) << 24);
        if (cursor >= kProfilePathCapacity - 1) return 0;
        profilePath[cursor++] = L'-';
        if (!appendDecimal(profilePath, kProfilePathCapacity - 1,
                           cursor, subAuthority)) return 0;
    }
    profilePath[cursor] = L'\0';

    if (cursor >= UINT32_MAX) return 0;
    const DWORD requiredCharacters = static_cast<DWORD>(cursor + 1);
    if (!lpProfileDir || *lpcchSize < requiredCharacters) {
        *lpcchSize = requiredCharacters;
        return 0;
    }
    std::memcpy(lpProfileDir, profilePath, requiredCharacters * sizeof(wchar_t));
    *lpcchSize = requiredCharacters;
    return 1;
}

#ifndef MICANT_USERENV_NO_EXPORT_REGISTRATION
inline void InitializeUserenvSubsystemExports() {
    auto& ldr = ldr::DynamicLoader::get();
    ldr.registerExport("userenv.dll", "GetUserProfileDirectoryW", reinterpret_cast<void*>(GetUserProfileDirectoryW));
}
#endif

} // namespace micant::userenv
