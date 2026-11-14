#pragma once

/**
 * @file lsass.hpp
 * @brief MicaNT Local Security Authority Subsystem Service (LSASS).
 *
 * Implements Dave Cutler's NT local security policy, logon session tracking,
 * authentication packages (MSV1_0 local authentication), and executive security
 * token synthesis according to clean-room Windows NT specifications.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <chrono>
#include <span>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/se.hpp"
#include "micant/um/subsystems/security/sam.hpp"

namespace micant::lsass {

// ============================================================================
// Standard Windows NT Security Logon Types
// ============================================================================

enum class SecurityLogonType : uint32_t {
    Undefined          = 0,
    Interactive        = 2,  // Console interactive logon (Winlogon)
    Network            = 3,  // Network logon (SMB, RPC, etc.)
    Batch              = 4,  // Batch scheduled task
    Service            = 5,  // Windows Service logon
    Proxy              = 6,  // Proxy logon
    Unlock             = 7,  // Workstation unlock
    NetworkCleartext   = 8,  // Network logon with cleartext credentials
    NewCredentials     = 9,  // Runas /netonly
    RemoteInteractive  = 10, // Remote Desktop / Terminal Services
    CachedInteractive  = 11  // Cached domain credentials logon
};

inline std::wstring_view logonTypeToString(SecurityLogonType type) noexcept {
    switch (type) {
        case SecurityLogonType::Interactive:       return L"Interactive";
        case SecurityLogonType::Network:           return L"Network";
        case SecurityLogonType::Batch:             return L"Batch";
        case SecurityLogonType::Service:           return L"Service";
        case SecurityLogonType::Unlock:            return L"Unlock";
        case SecurityLogonType::NetworkCleartext:  return L"NetworkCleartext";
        case SecurityLogonType::NewCredentials:    return L"NewCredentials";
        case SecurityLogonType::RemoteInteractive: return L"RemoteInteractive";
        case SecurityLogonType::CachedInteractive: return L"CachedInteractive";
        default:                                   return L"Undefined";
    }
}

// ============================================================================
// LSA Logon Session Structure
// ============================================================================

struct LsaLogonSession {
    Luid logonId{0, 0};
    std::wstring userName;
    std::wstring domainName;
    uint32_t userRid{0};
    se::Sid userSid;
    SecurityLogonType logonType{SecurityLogonType::Interactive};
    uint64_t logonTime{0}; // 100-nanosecond intervals since 1601
    std::shared_ptr<se::TokenObject> token;
    std::wstring authenticationPackage;
};

// ============================================================================
// Clean-Room Challenge-Response Cryptographic Synthesizer
// ============================================================================

namespace crypto {

/**
 * @brief Computes a standard 16-byte challenge-response digest from NT-Hash and challenge.
 * Conforms to RFC 1320 MD4 clean-room digest over NT-Hash and server challenge.
 */
inline std::vector<uint8_t> computeChallengeResponse(
    std::span<const uint8_t> ntHash,
    std::span<const uint8_t> challenge
) {
    std::vector<uint8_t> combined;
    combined.reserve(ntHash.size() + challenge.size());
    combined.insert(combined.end(), ntHash.begin(), ntHash.end());
    combined.insert(combined.end(), challenge.begin(), challenge.end());
    return sam::crypto::Md4::hash(combined);
}

} // namespace crypto

// ============================================================================
// LSA Authentication Package Interface
// ============================================================================

class ILsaAuthenticationPackage {
public:
    virtual ~ILsaAuthenticationPackage() = default;

    [[nodiscard]] virtual std::wstring_view getPackageName() const noexcept = 0;

    virtual NTSTATUS authenticate(
        const std::wstring& domain,
        const std::wstring& username,
        const std::wstring& password,
        SecurityLogonType logonType,
        std::shared_ptr<se::TokenObject>& outToken,
        uint32_t& outUserRid
    ) = 0;

    virtual NTSTATUS challengeResponse(
        const std::wstring& domain,
        const std::wstring& username,
        std::span<const uint8_t> challenge,
        std::span<const uint8_t> response,
        SecurityLogonType logonType,
        std::shared_ptr<se::TokenObject>& outToken,
        uint32_t& outUserRid
    ) = 0;
};

// ============================================================================
// Built-in MSV1_0 Local Authentication Package
// ============================================================================

class Msv1_0Package : public ILsaAuthenticationPackage {
public:
    [[nodiscard]] std::wstring_view getPackageName() const noexcept override {
        return L"MSV1_0";
    }

    NTSTATUS authenticate(
        const std::wstring& domain,
        const std::wstring& username,
        const std::wstring& password,
        SecurityLogonType logonType,
        std::shared_ptr<se::TokenObject>& outToken,
        uint32_t& outUserRid
    ) override {
        (void)domain;
        (void)logonType;

        uint32_t rid = 0;
        NTSTATUS status = sam::SamDatabase::get().verifyCredentials(username, password, rid);
        if (status != STATUS_SUCCESS) {
            return status;
        }

        auto userOpt = sam::SamDatabase::get().getUserByRid(rid);
        if (!userOpt) {
            return STATUS_NO_SUCH_USER;
        }

        outUserRid = rid;
        outToken = buildSecurityToken(*userOpt);
        return STATUS_SUCCESS;
    }

    NTSTATUS challengeResponse(
        const std::wstring& domain,
        const std::wstring& username,
        std::span<const uint8_t> challenge,
        std::span<const uint8_t> response,
        SecurityLogonType logonType,
        std::shared_ptr<se::TokenObject>& outToken,
        uint32_t& outUserRid
    ) override {
        (void)domain;
        (void)username;
        (void)logonType;
        (void)challenge;
        (void)response;
        (void)outToken;
        (void)outUserRid;
        // NT hash based legacy challenge-response is deliberately disabled;
        // this implementation does not persist a reversible-to-NT-hash
        // credential verifier or advertise NTLM support.
        return STATUS_NOT_SUPPORTED;
    }

private:
    [[nodiscard]] std::shared_ptr<se::TokenObject> buildSecurityToken(const sam::SamUser& user) {
        auto token = std::make_shared<se::TokenObject>(user.userSid);

        // Group memberships
        auto groupSids = sam::SamDatabase::get().getGroupSidsForUser(user.rid);
        for (const auto& gSid : groupSids) {
            token->addGroup(gSid);
        }

        // Privilege assignment
        bool isAdmin = token->hasSid(se::Sid::administrators()) ||
                       (user.primaryGroupRid == sam::DOMAIN_ALIAS_RID_ADMINS);

        if (isAdmin) {
            token->addPrivilege(se::SE_DEBUG_NAME, se::SE_PRIVILEGE_ENABLED | se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_SHUTDOWN_NAME, se::SE_PRIVILEGE_ENABLED | se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_BACKUP_NAME, se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_RESTORE_NAME, se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_SECURITY_NAME, se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_TAKE_OWNERSHIP_NAME, se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_SYSTEM_ENVIRONMENT_NAME, se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_CHANGE_NOTIFY_NAME, se::SE_PRIVILEGE_ENABLED | se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_IMPERSONATE_NAME, se::SE_PRIVILEGE_ENABLED | se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_INCREASE_QUOTA_NAME, se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_LOAD_DRIVER_NAME, se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        } else {
            token->addPrivilege(se::SE_CHANGE_NOTIFY_NAME, se::SE_PRIVILEGE_ENABLED | se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
            token->addPrivilege(se::SE_SHUTDOWN_NAME, se::SE_PRIVILEGE_ENABLED | se::SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        }

        return token;
    }
};

// ============================================================================
// Local Security Authority (LSASS) Core Engine
// ============================================================================

class LocalSecurityAuthority {
public:
    static LocalSecurityAuthority& get() {
        static LocalSecurityAuthority instance;
        return instance;
    }

    LocalSecurityAuthority(const LocalSecurityAuthority&) = delete;
    LocalSecurityAuthority& operator=(const LocalSecurityAuthority&) = delete;

    /**
     * @brief Registers an authentication package with LSASS.
     */
    void registerAuthenticationPackage(std::shared_ptr<ILsaAuthenticationPackage> pkg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pkg) {
            std::wstring nameLower = toLower(pkg->getPackageName());
            packages_[nameLower] = std::move(pkg);
        }
    }

    /**
     * @brief Looks up a registered authentication package by name.
     */
    [[nodiscard]] std::shared_ptr<ILsaAuthenticationPackage> getAuthenticationPackage(std::wstring_view name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = packages_.find(toLower(name));
        if (it != packages_.end()) {
            return it->second;
        }
        return nullptr;
    }

    /**
     * @brief Performs standard user logon and creates a logon session + token.
     */
    NTSTATUS logonUser(
        const std::wstring& domain,
        const std::wstring& username,
        const std::wstring& password,
        SecurityLogonType logonType,
        std::wstring_view packageName,
        std::shared_ptr<se::TokenObject>& outToken,
        Luid& outLogonId
    ) {
        outToken.reset();
        outLogonId = Luid{0, 0};
        // This adapter authenticates local interactive accounts only. It has
        // no service, batch, network, cached, or domain authority; accepting
        // those labels would let callers misrepresent the logon context.
        if (logonType != SecurityLogonType::Interactive) return STATUS_NOT_SUPPORTED;
        if (!domain.empty() && toLower(domain) != L"micant") return STATUS_LOGON_FAILURE;

        std::shared_ptr<ILsaAuthenticationPackage> pkg;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = packages_.find(toLower(packageName));
            if (it == packages_.end()) {
                return STATUS_NOT_IMPLEMENTED;
            }
            pkg = it->second;
        }

        uint32_t userRid = 0;
        NTSTATUS status = pkg->authenticate(domain, username, password, logonType, outToken, userRid);
        if (status != STATUS_SUCCESS) {
            return status;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        outLogonId = Luid(static_cast<uint32_t>(nextLogonId_++), 0);
        outToken->setAuthenticationId(outLogonId);
        outToken->setSessionId(logonType == SecurityLogonType::Interactive ? 1 : 0);

        LsaLogonSession session{
            .logonId = outLogonId,
            .userName = username,
            .domainName = domain.empty() ? L"MICANT" : domain,
            .userRid = userRid,
            .userSid = outToken->getUserSid(),
            .logonType = logonType,
            .logonTime = getCurrentTimestamp(),
            .token = outToken,
            .authenticationPackage = std::wstring(packageName)
        };

        sessions_[outLogonId.toUint64()] = session;
        return STATUS_SUCCESS;
    }

    /**
     * @brief Performs challenge-response logon and creates a logon session + token.
     */
    NTSTATUS logonUserWithChallenge(
        const std::wstring& domain,
        const std::wstring& username,
        std::span<const uint8_t> challenge,
        std::span<const uint8_t> response,
        SecurityLogonType logonType,
        std::wstring_view packageName,
        std::shared_ptr<se::TokenObject>& outToken,
        Luid& outLogonId
    ) {
        std::shared_ptr<ILsaAuthenticationPackage> pkg;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = packages_.find(toLower(packageName));
            if (it == packages_.end()) {
                return STATUS_NOT_IMPLEMENTED;
            }
            pkg = it->second;
        }

        uint32_t userRid = 0;
        NTSTATUS status = pkg->challengeResponse(domain, username, challenge, response, logonType, outToken, userRid);
        if (status != STATUS_SUCCESS) {
            return status;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        outLogonId = Luid(static_cast<uint32_t>(nextLogonId_++), 0);
        outToken->setAuthenticationId(outLogonId);
        outToken->setSessionId(logonType == SecurityLogonType::Interactive ? 1 : 0);

        LsaLogonSession session{
            .logonId = outLogonId,
            .userName = username,
            .domainName = domain.empty() ? L"MICANT" : domain,
            .userRid = userRid,
            .userSid = outToken->getUserSid(),
            .logonType = logonType,
            .logonTime = getCurrentTimestamp(),
            .token = outToken,
            .authenticationPackage = std::wstring(packageName)
        };

        sessions_[outLogonId.toUint64()] = session;
        return STATUS_SUCCESS;
    }

    /**
     * @brief Terminates a logon session and clears its token.
     */
    NTSTATUS logoffUser(Luid logonId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(logonId.toUint64());
        if (it == sessions_.end()) {
            return STATUS_NO_SUCH_LOGON_SESSION;
        }
        sessions_.erase(it);
        return STATUS_SUCCESS;
    }

    /**
     * @brief Enumerates all active logon sessions.
     */
    [[nodiscard]] std::vector<Luid> enumerateLogonSessions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Luid> result;
        result.reserve(sessions_.size());
        for (const auto& [id, _] : sessions_) {
            result.push_back(Luid::fromUint64(id));
        }
        return result;
    }

    /**
     * @brief Retrieves data for a specific logon session.
     */
    [[nodiscard]] std::optional<LsaLogonSession> getLogonSessionData(Luid logonId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(logonId.toUint64());
        if (it != sessions_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /**
     * @brief Resolves a SID to an account name and domain.
     */
    [[nodiscard]] bool lookupAccountSid(
        const se::Sid& sid,
        std::wstring& outName,
        std::wstring& outDomain
    ) const {
        if (sid == se::Sid::localSystem()) {
            outName = L"SYSTEM";
            outDomain = L"NT AUTHORITY";
            return true;
        }
        if (sid == se::Sid::everyone()) {
            outName = L"Everyone";
            outDomain = L"";
            return true;
        }
        if (sid == se::Sid::authenticatedUsers()) {
            outName = L"Authenticated Users";
            outDomain = L"NT AUTHORITY";
            return true;
        }
        if (sid == se::Sid::administrators()) {
            outName = L"Administrators";
            outDomain = L"BUILTIN";
            return true;
        }
        if (sid == se::Sid::users()) {
            outName = L"Users";
            outDomain = L"BUILTIN";
            return true;
        }

        // Check local SAM domain users
        const auto& subAuths = sid.getSubAuthorities();
        if (!subAuths.empty()) {
            uint32_t rid = subAuths.back();
            auto userOpt = sam::SamDatabase::get().getUserByRid(rid);
            if (userOpt) {
                outName = userOpt->accountName;
                outDomain = L"MICANT";
                return true;
            }
            auto groupOpt = sam::SamDatabase::get().getGroupByRid(rid);
            if (groupOpt) {
                outName = groupOpt->name;
                outDomain = L"MICANT";
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Resolves an account name to a SID and domain.
     */
    [[nodiscard]] bool lookupAccountName(
        const std::wstring& name,
        se::Sid& outSid,
        std::wstring& outDomain
    ) const {
        std::wstring n = toLower(name);
        if (n == L"system") {
            outSid = se::Sid::localSystem();
            outDomain = L"NT AUTHORITY";
            return true;
        }
        if (n == L"everyone") {
            outSid = se::Sid::everyone();
            outDomain = L"";
            return true;
        }
        if (n == L"administrators") {
            outSid = se::Sid::administrators();
            outDomain = L"BUILTIN";
            return true;
        }
        if (n == L"users") {
            outSid = se::Sid::users();
            outDomain = L"BUILTIN";
            return true;
        }

        auto userOpt = sam::SamDatabase::get().getUserByName(name);
        if (userOpt) {
            outSid = userOpt->userSid;
            outDomain = L"MICANT";
            return true;
        }

        return false;
    }

    /**
     * @brief Simulates LSA Named Pipe and ALPC port status.
     */
    [[nodiscard]] bool isRpcServerRunning() const noexcept {
        return true;
    }

    [[nodiscard]] std::wstring_view getPipeName() const noexcept {
        return L"\\\\.\\pipe\\lsass";
    }

    [[nodiscard]] std::wstring_view getAlpcPortName() const noexcept {
        return L"\\LsaAuthenticationPort";
    }

private:
    LocalSecurityAuthority() {
        registerAuthenticationPackage(std::make_shared<Msv1_0Package>());

        // Initialize Local System logon session (LUID 0x3E7 == 999)
        Luid systemLuid(0x3E7, 0);
        auto sysToken = se::TokenObject::createSystemToken();
        sysToken->setAuthenticationId(systemLuid);
        sysToken->setSessionId(0);

        LsaLogonSession sysSession{
            .logonId = systemLuid,
            .userName = L"SYSTEM",
            .domainName = L"NT AUTHORITY",
            .userRid = 18,
            .userSid = se::Sid::localSystem(),
            .logonType = SecurityLogonType::Service,
            .logonTime = getCurrentTimestamp(),
            .token = sysToken,
            .authenticationPackage = L"SYSTEM"
        };
        sessions_[systemLuid.toUint64()] = sysSession;
    }

    static std::wstring toLower(std::wstring_view str) {
        std::wstring s(str);
        std::transform(s.begin(), s.end(), s.begin(), ::towlower);
        return s;
    }

    static uint64_t getCurrentTimestamp() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto duration = now.time_since_epoch();
        return duration_cast<nanoseconds>(duration).count() / 100 + 116444736000000000ULL;
    }

    mutable std::mutex mutex_;
    uint64_t nextLogonId_{1000};
    std::unordered_map<std::wstring, std::shared_ptr<ILsaAuthenticationPackage>> packages_;
    std::unordered_map<uint64_t, LsaLogonSession> sessions_;
};

} // namespace micant::lsass
