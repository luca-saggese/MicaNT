#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <span>
#include <mutex>
#include <random>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/crypto_primitives.hpp"
#include "micant/km/executive/se.hpp"

namespace micant::sam {

// ============================================================================
// Standard SAM User Account Control Flags (win32metadata compatible)
// ============================================================================
inline constexpr uint32_t USER_ACCOUNT_DISABLED                = 0x0001;
inline constexpr uint32_t USER_HOME_DIRECTORY_REQUIRED         = 0x0002;
inline constexpr uint32_t USER_PASSWORD_NOT_REQUIRED           = 0x0004;
inline constexpr uint32_t USER_TEMP_DUPLICATE_ACCOUNT          = 0x0008;
inline constexpr uint32_t USER_NORMAL_ACCOUNT                  = 0x0010;
inline constexpr uint32_t USER_MNS_LOGON_ACCOUNT               = 0x0020;
inline constexpr uint32_t USER_INTERDOMAIN_TRUST_ACCOUNT       = 0x0040;
inline constexpr uint32_t USER_WORKSTATION_TRUST_ACCOUNT       = 0x0080;
inline constexpr uint32_t USER_SERVER_TRUST_ACCOUNT            = 0x0100;
inline constexpr uint32_t USER_DONT_EXPIRE_PASSWORD            = 0x0200;
inline constexpr uint32_t USER_ACCOUNT_AUTO_LOCKED             = 0x0400;
inline constexpr uint32_t USER_PASSWORD_EXPIRED                = 0x0800;

// Standard Well-Known Relative Identifiers (RIDs)
inline constexpr uint32_t DOMAIN_USER_RID_ADMIN                = 500;
inline constexpr uint32_t DOMAIN_USER_RID_GUEST                = 501;
inline constexpr uint32_t DOMAIN_GROUP_RID_ADMINS              = 512;
inline constexpr uint32_t DOMAIN_GROUP_RID_USERS               = 513;
inline constexpr uint32_t DOMAIN_GROUP_RID_GUESTS              = 514;
inline constexpr uint32_t DOMAIN_ALIAS_RID_ADMINS              = 544; // Builtin Administrators
inline constexpr uint32_t DOMAIN_ALIAS_RID_USERS               = 545; // Builtin Users
inline constexpr uint32_t DOMAIN_ALIAS_RID_GUESTS              = 546; // Builtin Guests

// ============================================================================
// Clean-Room Cryptographic Hash Utilities (RFC 1320 MD4 & SHA-256)
// ============================================================================

namespace crypto {

/**
 * @brief Clean-room MD4 implementation conforming to RFC 1320.
 * Used for standard Windows NT-Hash (MD4 of little-endian UTF-16 password bytes).
 */
class Md4 {
public:
    static std::vector<uint8_t> hash(std::span<const uint8_t> input) {
        uint32_t A = 0x67452301;
        uint32_t B = 0xefcdab89;
        uint32_t C = 0x98badcfe;
        uint32_t D = 0x10325476;

        uint64_t bitLen = static_cast<uint64_t>(input.size()) * 8;
        std::vector<uint8_t> msg(input.begin(), input.end());
        msg.push_back(0x80);
        while ((msg.size() % 64) != 56) {
            msg.push_back(0x00);
        }
        for (int i = 0; i < 8; ++i) {
            msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF));
        }

        auto F = [](uint32_t x, uint32_t y, uint32_t z) -> uint32_t { return (x & y) | (~x & z); };
        auto G = [](uint32_t x, uint32_t y, uint32_t z) -> uint32_t { return (x & y) | (x & z) | (y & z); };
        auto H = [](uint32_t x, uint32_t y, uint32_t z) -> uint32_t { return x ^ y ^ z; };
        auto rotl = [](uint32_t val, int shift) -> uint32_t { return (val << shift) | (val >> (32 - shift)); };

        for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
            uint32_t X[16];
            for (int i = 0; i < 16; ++i) {
                X[i] = static_cast<uint32_t>(msg[chunk + i * 4]) |
                       (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 8) |
                       (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 16) |
                       (static_cast<uint32_t>(msg[chunk + i * 4 + 3]) << 24);
            }

            uint32_t a = A, b = B, c = C, d = D;

            // Round 1
            a = rotl(a + F(b, c, d) + X[0], 3);   d = rotl(d + F(a, b, c) + X[1], 7);
            c = rotl(c + F(d, a, b) + X[2], 11);  b = rotl(b + F(c, d, a) + X[3], 19);
            a = rotl(a + F(b, c, d) + X[4], 3);   d = rotl(d + F(a, b, c) + X[5], 7);
            c = rotl(c + F(d, a, b) + X[6], 11);  b = rotl(b + F(c, d, a) + X[7], 19);
            a = rotl(a + F(b, c, d) + X[8], 3);   d = rotl(d + F(a, b, c) + X[9], 7);
            c = rotl(c + F(d, a, b) + X[10], 11); b = rotl(b + F(c, d, a) + X[11], 19);
            a = rotl(a + F(b, c, d) + X[12], 3);  d = rotl(d + F(a, b, c) + X[13], 7);
            c = rotl(c + F(d, a, b) + X[14], 11); b = rotl(b + F(c, d, a) + X[15], 19);

            // Round 2
            a = rotl(a + G(b, c, d) + X[0] + 0x5a827999, 3);   d = rotl(d + G(a, b, c) + X[4] + 0x5a827999, 5);
            c = rotl(c + G(d, a, b) + X[8] + 0x5a827999, 9);   b = rotl(b + G(c, d, a) + X[12] + 0x5a827999, 13);
            a = rotl(a + G(b, c, d) + X[1] + 0x5a827999, 3);   d = rotl(d + G(a, b, c) + X[5] + 0x5a827999, 5);
            c = rotl(c + G(d, a, b) + X[9] + 0x5a827999, 9);   b = rotl(b + G(c, d, a) + X[13] + 0x5a827999, 13);
            a = rotl(a + G(b, c, d) + X[2] + 0x5a827999, 3);   d = rotl(d + G(a, b, c) + X[6] + 0x5a827999, 5);
            c = rotl(c + G(d, a, b) + X[10] + 0x5a827999, 9);  b = rotl(b + G(c, d, a) + X[14] + 0x5a827999, 13);
            a = rotl(a + G(b, c, d) + X[3] + 0x5a827999, 3);   d = rotl(d + G(a, b, c) + X[7] + 0x5a827999, 5);
            c = rotl(c + G(d, a, b) + X[11] + 0x5a827999, 9);  b = rotl(b + G(c, d, a) + X[15] + 0x5a827999, 13);

            // Round 3
            a = rotl(a + H(b, c, d) + X[0] + 0x6ed9eba1, 3);   d = rotl(d + H(a, b, c) + X[8] + 0x6ed9eba1, 9);
            c = rotl(c + H(d, a, b) + X[4] + 0x6ed9eba1, 11);  b = rotl(b + H(c, d, a) + X[12] + 0x6ed9eba1, 15);
            a = rotl(a + H(b, c, d) + X[2] + 0x6ed9eba1, 3);   d = rotl(d + H(a, b, c) + X[10] + 0x6ed9eba1, 9);
            c = rotl(c + H(d, a, b) + X[6] + 0x6ed9eba1, 11);  b = rotl(b + H(c, d, a) + X[14] + 0x6ed9eba1, 15);
            a = rotl(a + H(b, c, d) + X[1] + 0x6ed9eba1, 3);   d = rotl(d + H(a, b, c) + X[9] + 0x6ed9eba1, 9);
            c = rotl(c + H(d, a, b) + X[5] + 0x6ed9eba1, 11);  b = rotl(b + H(c, d, a) + X[13] + 0x6ed9eba1, 15);
            a = rotl(a + H(b, c, d) + X[3] + 0x6ed9eba1, 3);   d = rotl(d + H(a, b, c) + X[11] + 0x6ed9eba1, 9);
            c = rotl(c + H(d, a, b) + X[7] + 0x6ed9eba1, 11);  b = rotl(b + H(c, d, a) + X[15] + 0x6ed9eba1, 15);

            A += a; B += b; C += c; D += d;
        }

        std::vector<uint8_t> digest(16);
        for (int i = 0; i < 4; ++i) digest[i + 0] = static_cast<uint8_t>((A >> (i * 8)) & 0xFF);
        for (int i = 0; i < 4; ++i) digest[i + 4] = static_cast<uint8_t>((B >> (i * 8)) & 0xFF);
        for (int i = 0; i < 4; ++i) digest[i + 8] = static_cast<uint8_t>((C >> (i * 8)) & 0xFF);
        for (int i = 0; i < 4; ++i) digest[i + 12] = static_cast<uint8_t>((D >> (i * 8)) & 0xFF);
        return digest;
    }
};

/**
 * @brief Computes standard 16-byte NT-Hash from UTF-16LE password.
 */
inline std::vector<uint8_t> computeNtHash(std::wstring_view password) {
    std::vector<uint8_t> utf16Bytes;
    utf16Bytes.reserve(password.size() * 2);
    for (wchar_t ch : password) {
        utf16Bytes.push_back(static_cast<uint8_t>(ch & 0xFF));
        utf16Bytes.push_back(static_cast<uint8_t>((ch >> 8) & 0xFF));
    }
    return Md4::hash(utf16Bytes);
}

/**
 * @brief Helper to convert binary buffer to uppercase hex string.
 */
inline std::wstring toHexString(std::span<const uint8_t> bytes) {
    std::wostringstream wos;
    wos << std::hex << std::setfill(L'0');
    for (uint8_t b : bytes) {
        wos << std::setw(2) << static_cast<uint32_t>(b);
    }
    return wos.str();
}

} // namespace crypto

// ============================================================================
// SAM User Account Entity
// ============================================================================

struct SamUser {
    uint32_t rid{0};
    std::wstring accountName;
    std::wstring fullName;
    std::wstring comment;
    // Versioned PBKDF2-HMAC-SHA-256 verifier. NT hash storage is intentionally
    // absent; legacy challenge-response authentication is unsupported.
    uint32_t passwordVerifierVersion{1};
    uint32_t passwordVerifierIterations{micant::shared::crypto::Pbkdf2Sha256::MIN_ITERATIONS};
    std::array<uint8_t, 16> passwordSalt{};
    std::array<uint8_t, 32> passwordVerifier{};
    bool hasPassword{false};
    uint32_t primaryGroupRid{DOMAIN_ALIAS_RID_USERS};
    uint32_t userFlags{USER_NORMAL_ACCOUNT};
    uint32_t badPasswordCount{0};
    uint32_t logonCount{0};
    uint64_t passwordLastSetTimestamp{0};
    se::Sid userSid;
};

// ============================================================================
// SAM Group / Alias Entity
// ============================================================================

struct SamGroup {
    uint32_t rid{0};
    std::wstring name;
    std::wstring comment;
    std::vector<uint32_t> memberRids;
    se::Sid groupSid;
};

// ============================================================================
// Security Accounts Manager (SAM) Database Engine
// ============================================================================

class SamDatabase {
public:
    struct PasswordPolicy {
        size_t minimumLength{12};
        uint32_t minimumCharacterClasses{3};
        uint32_t maximumFailedAttempts{5};
    };

    static SamDatabase& get() {
        static SamDatabase instance;
        return instance;
    }

    explicit SamDatabase(se::Sid domainSid = se::Sid(5, {21, 1988, 1993, 2026}))
        : domainSid_(std::move(domainSid)), nextUserRid_(1000) {
        initializeDefaultAccounts();
    }

    [[nodiscard]] const se::Sid& getDomainSid() const noexcept { return domainSid_; }

    /**
     * @brief Creates a complete Sid for a given RID under the domain authority.
     */
    [[nodiscard]] se::Sid makeUserSid(uint32_t rid) const {
        auto subAuths = domainSid_.getSubAuthorities();
        subAuths.push_back(rid);
        // Authority 5
        return se::Sid(5, {subAuths[0], subAuths[1], subAuths[2], subAuths[3]});
    }

    /**
     * @brief Creates a Builtin Alias Sid (e.g. S-1-5-32-544 for Administrators).
     */
    [[nodiscard]] static se::Sid makeBuiltinSid(uint32_t rid) {
        return se::Sid(5, {32, rid});
    }

    /**
     * @brief Creates a new user account in the SAM database.
     */
    NTSTATUS createUser(std::wstring_view accountName,
                        std::wstring_view password,
                        std::wstring_view fullName = L"",
                        std::wstring_view comment = L"",
                        bool isAdmin = false) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!validAccountName(accountName) || !passwordMeetsPolicy(password)) return STATUS_ACCOUNT_RESTRICTION;

        std::wstring nameLower = toLower(accountName);
        if (usersByName_.contains(nameLower)) {
            return STATUS_USER_EXISTS;
        }

        if (nextUserRid_ < 1000 || nextUserRid_ == UINT32_MAX) return STATUS_ACCOUNT_RESTRICTION;
        uint32_t rid = nextUserRid_++;
        SamUser user;
        user.rid = rid;
        user.accountName = std::wstring(accountName);
        user.fullName = std::wstring(fullName);
        user.comment = std::wstring(comment);
        if (!createPasswordVerifier(password, user)) return STATUS_NOT_SUPPORTED;
        user.primaryGroupRid = isAdmin ? DOMAIN_ALIAS_RID_ADMINS : DOMAIN_ALIAS_RID_USERS;
        user.userFlags = USER_NORMAL_ACCOUNT | USER_DONT_EXPIRE_PASSWORD;
        user.userSid = makeUserSid(rid);
        user.passwordLastSetTimestamp = 13370000000ULL;

        usersByRid_[rid] = user;
        usersByName_[nameLower] = rid;
        wipe(user.passwordSalt);
        wipe(user.passwordVerifier);

        // Add to groups
        addUserToGroupInternal(rid, DOMAIN_ALIAS_RID_USERS);
        if (isAdmin) {
            addUserToGroupInternal(rid, DOMAIN_ALIAS_RID_ADMINS);
        }

        return STATUS_SUCCESS;
    }

    /**
     * @brief Verifies plaintext credentials against the stored NT-Hash.
     */
    NTSTATUS verifyCredentials(std::wstring_view accountName, std::wstring_view password, uint32_t& outRid) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::wstring nameLower = toLower(accountName);
        auto it = usersByName_.find(nameLower);
        if (it == usersByName_.end()) {
            return STATUS_NO_SUCH_USER;
        }

        SamUser& user = usersByRid_[it->second];
        if (user.userFlags & USER_ACCOUNT_DISABLED) {
            return STATUS_ACCOUNT_DISABLED;
        }
        if (user.userFlags & USER_ACCOUNT_AUTO_LOCKED) {
            return STATUS_ACCOUNT_LOCKED_OUT;
        }
        if (!user.hasPassword) return STATUS_PASSWORD_EXPIRED;

        std::array<uint8_t, 32> inputVerifier{};
        if (!derivePasswordVerifier(password, user, inputVerifier)) return STATUS_NOT_SUPPORTED;
        if (constantTimeEqual(inputVerifier, user.passwordVerifier)) {
            wipe(inputVerifier);
            user.badPasswordCount = 0;
            user.logonCount++;
            outRid = user.rid;
            return STATUS_SUCCESS;
        }

        wipe(inputVerifier);

        if (user.badPasswordCount != UINT32_MAX) ++user.badPasswordCount;
        if (user.badPasswordCount >= passwordPolicy_.maximumFailedAttempts) {
            user.userFlags |= USER_ACCOUNT_AUTO_LOCKED;
        }
        return STATUS_LOGON_FAILURE;
    }

    /**
     * @brief Updates user account password.
     */
    NTSTATUS setPassword(std::wstring_view accountName, std::wstring_view newPassword) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!passwordMeetsPolicy(newPassword)) return STATUS_ACCOUNT_RESTRICTION;

        std::wstring nameLower = toLower(accountName);
        auto it = usersByName_.find(nameLower);
        if (it == usersByName_.end()) {
            return STATUS_NO_SUCH_USER;
        }

        SamUser& user = usersByRid_[it->second];
        SamUser replacement = user;
        if (!createPasswordVerifier(newPassword, replacement)) return STATUS_NOT_SUPPORTED;
        user.passwordSalt = replacement.passwordSalt;
        user.passwordVerifier = replacement.passwordVerifier;
        user.passwordVerifierVersion = replacement.passwordVerifierVersion;
        user.passwordVerifierIterations = replacement.passwordVerifierIterations;
        user.hasPassword = true;
        user.passwordLastSetTimestamp = 0; // replaced with a trusted system time when that service is available
        wipe(replacement.passwordSalt);
        wipe(replacement.passwordVerifier);
        user.badPasswordCount = 0;
        user.userFlags &= ~USER_ACCOUNT_AUTO_LOCKED;
        return STATUS_SUCCESS;
    }

    /**
     * @brief Deletes a user account from SAM.
     */
    NTSTATUS deleteUser(std::wstring_view accountName) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wstring nameLower = toLower(accountName);
        auto it = usersByName_.find(nameLower);
        if (it == usersByName_.end()) {
            return STATUS_NO_SUCH_USER;
        }
        uint32_t rid = it->second;
        if (rid == DOMAIN_USER_RID_ADMIN || rid == DOMAIN_USER_RID_GUEST) {
            return STATUS_ACCESS_DENIED;
        }
        usersByName_.erase(it);
        usersByRid_.erase(rid);

        for (auto& [_, g] : groupsByRid_) {
            std::erase(g.memberRids, rid);
        }
        return STATUS_SUCCESS;
    }

    /**
     * @brief Unlocks a locked user account.
     */
    NTSTATUS unlockUser(std::wstring_view accountName) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wstring nameLower = toLower(accountName);
        auto it = usersByName_.find(nameLower);
        if (it == usersByName_.end()) {
            return STATUS_NO_SUCH_USER;
        }
        SamUser& user = usersByRid_[it->second];
        user.badPasswordCount = 0;
        user.userFlags &= ~USER_ACCOUNT_AUTO_LOCKED;
        return STATUS_SUCCESS;
    }

    NTSTATUS setAccountEnabled(std::wstring_view accountName, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = usersByName_.find(toLower(accountName));
        if (it == usersByName_.end()) return STATUS_NO_SUCH_USER;
        auto& user = usersByRid_.at(it->second);
        if (enabled && !user.hasPassword) return STATUS_PASSWORD_EXPIRED;
        if (enabled) user.userFlags &= ~USER_ACCOUNT_DISABLED;
        else user.userFlags |= USER_ACCOUNT_DISABLED;
        return STATUS_SUCCESS;
    }

    /**
     * @brief Look up user by account name.
     */
    [[nodiscard]] std::optional<SamUser> getUser(std::wstring_view accountName) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wstring nameLower = toLower(accountName);
        auto it = usersByName_.find(nameLower);
        if (it == usersByName_.end()) return std::nullopt;
        auto uit = usersByRid_.find(it->second);
        if (uit == usersByRid_.end()) return std::nullopt;
        return uit->second;
    }

    [[nodiscard]] std::optional<SamUser> getUserByName(std::wstring_view accountName) const {
        return getUser(accountName);
    }

    /**
     * @brief Look up user by RID.
     */
    [[nodiscard]] std::optional<SamUser> getUserByRid(uint32_t rid) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = usersByRid_.find(rid);
        if (it == usersByRid_.end()) return std::nullopt;
        return it->second;
    }

    /**
     * @brief Look up group by RID.
     */
    [[nodiscard]] std::optional<SamGroup> getGroupByRid(uint32_t rid) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = groupsByRid_.find(rid);
        if (it == groupsByRid_.end()) return std::nullopt;
        return it->second;
    }

    /**
     * @brief Enumerates all registered users.
     */
    [[nodiscard]] std::vector<SamUser> enumerateUsers() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SamUser> list;
        list.reserve(usersByRid_.size());
        for (const auto& [_, u] : usersByRid_) {
            list.push_back(u);
        }
        return list;
    }

    /**
     * @brief Enumerates all registered security groups/aliases.
     */
    [[nodiscard]] std::vector<SamGroup> enumerateGroups() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SamGroup> list;
        list.reserve(groupsByRid_.size());
        for (const auto& [_, g] : groupsByRid_) {
            list.push_back(g);
        }
        return list;
    }

    /**
     * @brief Assigns a user to a group.
     */
    NTSTATUS addUserToGroup(uint32_t userRid, uint32_t groupRid) {
        std::lock_guard<std::mutex> lock(mutex_);
        return addUserToGroupInternal(userRid, groupRid);
    }

    /**
     * @brief Retrieves all group SIDs for a given user RID.
     */
    [[nodiscard]] std::vector<se::Sid> getGroupSidsForUser(uint32_t userRid) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<se::Sid> result;

        // Base groups assigned to all authenticated interactive sessions
        result.push_back(se::Sid::everyone());            // S-1-1-0
        result.push_back(se::Sid::authenticatedUsers());  // S-1-5-11
        result.push_back(se::Sid(5, {2}));                // S-1-5-2 (Interactive)

        for (const auto& [_, g] : groupsByRid_) {
            if (std::find(g.memberRids.begin(), g.memberRids.end(), userRid) != g.memberRids.end()) {
                result.push_back(g.groupSid);
            }
        }
        return result;
    }

private:
    static bool constantTimeEqual(std::span<const uint8_t> left,
                                  std::span<const uint8_t> right) noexcept {
        if (left.size() != right.size()) return false;
        uint8_t difference = 0;
        for (size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
        return difference == 0;
    }

    static bool validAccountName(std::wstring_view name) noexcept {
        if (name.empty() || name.size() > 64 || name == L"." || name == L"..") return false;
        for (wchar_t ch : name)
            if (ch < 0x20 || ch == L'\\' || ch == L'/' || ch == L':' || ch == L'@') return false;
        return true;
    }

    bool passwordMeetsPolicy(std::wstring_view password) const noexcept {
        if (password.size() < passwordPolicy_.minimumLength || password.size() > 1024) return false;
        bool upper = false, lower = false, digit = false, punctuation = false;
        for (wchar_t ch : password) {
            upper |= ch >= L'A' && ch <= L'Z';
            lower |= ch >= L'a' && ch <= L'z';
            digit |= ch >= L'0' && ch <= L'9';
            punctuation |= !((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
                             (ch >= L'0' && ch <= L'9'));
        }
        return static_cast<uint32_t>(upper) + static_cast<uint32_t>(lower) +
            static_cast<uint32_t>(digit) + static_cast<uint32_t>(punctuation) >=
            passwordPolicy_.minimumCharacterClasses;
    }

    static void wipe(std::span<uint8_t> bytes) noexcept {
        volatile uint8_t* p = bytes.data();
        for (size_t i = 0; i < bytes.size(); ++i) p[i] = 0;
    }

    static bool encodePassword(std::wstring_view password, std::vector<uint8_t>& bytes) {
        // Windows WCHAR is UTF-16. Convert scalar wchar_t values to UTF-16LE
        // explicitly so hosted 32-bit wchar_t builds use the same encoding.
        bytes.clear();
        if (password.size() > 1024) return false;
        bytes.reserve(password.size() * 4);
        for (uint32_t cp : password) {
            if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
            if (cp <= 0xffff) {
                bytes.push_back(static_cast<uint8_t>(cp));
                bytes.push_back(static_cast<uint8_t>(cp >> 8));
            } else {
                cp -= 0x10000;
                const uint16_t high = static_cast<uint16_t>(0xd800 | (cp >> 10));
                const uint16_t low = static_cast<uint16_t>(0xdc00 | (cp & 0x3ff));
                bytes.push_back(static_cast<uint8_t>(high)); bytes.push_back(static_cast<uint8_t>(high >> 8));
                bytes.push_back(static_cast<uint8_t>(low)); bytes.push_back(static_cast<uint8_t>(low >> 8));
            }
        }
        return !bytes.empty();
    }

    static bool derivePasswordVerifier(std::wstring_view password, const SamUser& user,
                                      std::array<uint8_t, 32>& verifier) {
        if (user.passwordVerifierVersion != 1 ||
            user.passwordVerifierIterations < micant::shared::crypto::Pbkdf2Sha256::MIN_ITERATIONS ||
            user.passwordVerifierIterations > micant::shared::crypto::Pbkdf2Sha256::MAX_ITERATIONS) return false;
        std::vector<uint8_t> passwordBytes;
        if (!encodePassword(password, passwordBytes)) return false;
        const bool ok = micant::shared::crypto::Pbkdf2Sha256::derive(passwordBytes, user.passwordSalt,
            user.passwordVerifierIterations, verifier);
        wipe(passwordBytes);
        return ok;
    }

    static bool createPasswordVerifier(std::wstring_view password, SamUser& user) {
        // SamDatabase is currently only a hosted adapter. Native user-mode
        // SAM must obtain salt through its authorized kernel RNG interface.
        // Do not let this host-only source become the freestanding contract.
        try {
            std::random_device source;
            std::uniform_int_distribution<unsigned> byte(0, 255);
            for (auto& value : user.passwordSalt) value = static_cast<uint8_t>(byte(source));
        } catch (...) {
            user.passwordSalt.fill(0);
            return false;
        }
        return derivePasswordVerifier(password, user, user.passwordVerifier);
    }

    void initializeDefaultAccounts() {
        // Builtin Groups
        groupsByRid_[DOMAIN_ALIAS_RID_ADMINS] = SamGroup{
            .rid = DOMAIN_ALIAS_RID_ADMINS,
            .name = L"Administrators",
            .comment = L"Members have complete and unrestricted access to the computer/domain",
            .memberRids = {DOMAIN_USER_RID_ADMIN},
            .groupSid = se::Sid::administrators()
        };

        groupsByRid_[DOMAIN_ALIAS_RID_USERS] = SamGroup{
            .rid = DOMAIN_ALIAS_RID_USERS,
            .name = L"Users",
            .comment = L"Users are prevented from making accidental or intentional system-wide changes",
            .memberRids = {DOMAIN_USER_RID_ADMIN},
            .groupSid = se::Sid::users()
        };

        groupsByRid_[DOMAIN_ALIAS_RID_GUESTS] = SamGroup{
            .rid = DOMAIN_ALIAS_RID_GUESTS,
            .name = L"Guests",
            .comment = L"Guests have the same access as members of the Users group by default",
            .memberRids = {DOMAIN_USER_RID_GUEST},
            .groupSid = se::Sid(5, {32, DOMAIN_ALIAS_RID_GUESTS})
        };

        // Administrator (RID 500)
        SamUser admin;
        admin.rid = DOMAIN_USER_RID_ADMIN;
        admin.accountName = L"Administrator";
        admin.fullName = L"Built-in account for administering the computer";
        admin.comment = L"MicaNT Built-in Superuser";
        admin.primaryGroupRid = DOMAIN_ALIAS_RID_ADMINS;
        admin.userFlags = USER_NORMAL_ACCOUNT | USER_ACCOUNT_DISABLED | USER_PASSWORD_EXPIRED;
        admin.userSid = makeUserSid(DOMAIN_USER_RID_ADMIN);
        usersByRid_[DOMAIN_USER_RID_ADMIN] = admin;
        usersByName_[L"administrator"] = DOMAIN_USER_RID_ADMIN;

        // Guest (RID 501)
        SamUser guest;
        guest.rid = DOMAIN_USER_RID_GUEST;
        guest.accountName = L"Guest";
        guest.fullName = L"Built-in account for guest access";
        guest.comment = L"Disabled by default";
        guest.primaryGroupRid = DOMAIN_ALIAS_RID_GUESTS;
        guest.userFlags = USER_NORMAL_ACCOUNT | USER_ACCOUNT_DISABLED;
        guest.userSid = makeUserSid(DOMAIN_USER_RID_GUEST);
        usersByRid_[DOMAIN_USER_RID_GUEST] = guest;
        usersByName_[L"guest"] = DOMAIN_USER_RID_GUEST;

        // MicaNT Default Interactive User (RID 1000)
        nextUserRid_ = 1001;
    }

    NTSTATUS addUserToGroupInternal(uint32_t userRid, uint32_t groupRid) {
        auto git = groupsByRid_.find(groupRid);
        if (git == groupsByRid_.end()) return STATUS_NO_SUCH_ALIAS;
        auto& members = git->second.memberRids;
        if (std::find(members.begin(), members.end(), userRid) == members.end()) {
            members.push_back(userRid);
        }
        return STATUS_SUCCESS;
    }

    static std::wstring toLower(std::wstring_view str) {
        std::wstring s(str);
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    mutable std::mutex mutex_;
    PasswordPolicy passwordPolicy_{};
    se::Sid domainSid_;
    uint32_t nextUserRid_{1000};
    std::unordered_map<uint32_t, SamUser> usersByRid_;
    std::unordered_map<std::wstring, uint32_t> usersByName_;
    std::unordered_map<uint32_t, SamGroup> groupsByRid_;
};

} // namespace micant::sam
