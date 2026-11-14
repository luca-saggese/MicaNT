#pragma once

#include <cstdint>
#include <array>
#include <atomic>
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
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/ob.hpp"

namespace micant::se {

#if defined(MICANT_FREESTANDING)
// Fixed-capacity primary token body for the canonical freestanding Ps path.
// It stores binary SIDs and enabled group attributes without hosted STL or
// parallel identity tables. Ob owns its lifetime through ObjectHeader.
struct SidValue {
    uint8_t Revision{1};
    uint8_t SubAuthorityCount{};
    uint8_t IdentifierAuthority[6]{};
    uint32_t SubAuthority[8]{};
    [[nodiscard]] bool equals(const SidValue& other) const noexcept;
};
struct TokenGroupValue {
    SidValue Sid{};
    uint32_t Attributes{};
};
inline constexpr uint32_t SE_NETWORK_CONFIG_WRITE = 0x00000001;
enum class FreestandingAceType : uint8_t { Deny = 0, Allow = 1 };
struct FreestandingAce {
    SidValue Sid{};
    uint32_t AccessMask{};
    FreestandingAceType Type{FreestandingAceType::Allow};
    uint8_t Flags{};
};
struct FreestandingSecurityDescriptor {
    SidValue Owner{};
    SidValue Group{};
    std::array<FreestandingAce, 16> Dacl{};
    uint8_t AceCount{};
    bool OwnerPresent{};
    bool GroupPresent{};
    bool DaclPresent{true};
    bool DaclNull{};
};
struct FreestandingAccessToken {
    static constexpr size_t MaximumGroups = 16;
    SidValue UserSid{};
    SidValue DefaultOwner{};
    SidValue PrimaryGroup{};
    std::array<TokenGroupValue, MaximumGroups> Groups{};
    uint32_t GroupCount{};
    uint32_t IntegrityLevel{};
    uint64_t EnabledPrivileges{};
    // Kernel-assigned logon identity and interactive session. Zero denotes a
    // bootstrap/system token with no user logon session.
    uint64_t AuthenticationId{};
    uint32_t SessionId{};
    bool IsPrimary{true};
    bool IsSystem{};
    // Embedded storage gives duplicated tokens independent default DACLs.
    FreestandingSecurityDescriptor DefaultDacl{};
    [[nodiscard]] bool hasEnabledGroup(const SidValue& sid) const noexcept;
    [[nodiscard]] bool matchesSid(const SidValue& sid, bool includeDenyOnly) const noexcept;
};
struct FreestandingLogonTokenRequest {
    static constexpr size_t MaximumGroups = FreestandingAccessToken::MaximumGroups;
    SidValue UserSid{};
    SidValue PrimaryGroup{};
    std::array<TokenGroupValue, MaximumGroups> Groups{};
    uint32_t GroupCount{};
    uint32_t IntegrityLevel{};
    uint64_t EnabledPrivileges{};
};
[[nodiscard]] NtStatus SeCreateFreestandingPrimaryToken(bool system,
    FreestandingAccessToken*& token, ob::ObjectHeader*& reference) noexcept;
// Kernel-only local interactive logon token issuance. The caller must pass
// the referenced primary token of the actual requesting process, never claims
// copied from user mode. Only the SYSTEM authority token carrying both
// SeCreateTokenPrivilege and SeTcbPrivilege is accepted. AuthenticationId is
// allocated by Se; this first implementation assigns console SessionId 1.
[[nodiscard]] NtStatus SeCreateFreestandingLogonToken(
    const FreestandingAccessToken* issuer, ob::ObjectHeader* issuerReference,
    const FreestandingLogonTokenRequest& request,
    FreestandingAccessToken*& token, ob::ObjectHeader*& reference) noexcept;
[[nodiscard]] NtStatus SeDuplicateFreestandingPrimaryToken(
    const FreestandingAccessToken& source, FreestandingAccessToken*& token,
    ob::ObjectHeader*& reference) noexcept;
[[nodiscard]] NtStatus SeCreateFreestandingImpersonationToken(
    const FreestandingAccessToken& source, FreestandingAccessToken*& token,
    ob::ObjectHeader*& reference) noexcept;
[[nodiscard]] bool SeIsFreestandingTokenObject(const FreestandingAccessToken* token,
    ob::ObjectHeader* reference) noexcept;
[[nodiscard]] bool SeCanImpersonateFreestanding(
    const FreestandingAccessToken* caller, const FreestandingAccessToken* target) noexcept;
[[nodiscard]] bool SeTokenHasPrivilegeFreestanding(
    const FreestandingAccessToken* token, uint32_t privilegeLuidLow) noexcept;
void SeDereferenceFreestandingToken(ob::ObjectHeader* reference) noexcept;
[[nodiscard]] const FreestandingAccessToken* SeGetEffectiveFreestandingToken(
    const FreestandingAccessToken* primary, const FreestandingAccessToken* impersonation) noexcept;
[[nodiscard]] bool SeAccessCheckFreestanding(const FreestandingAccessToken* token,
    const FreestandingSecurityDescriptor& descriptor, uint32_t desiredAccess,
    uint32_t& grantedAccess) noexcept;
[[nodiscard]] bool SeMaximumAccessFreestanding(const FreestandingAccessToken* token,
    const FreestandingSecurityDescriptor& descriptor, uint32_t validAccess,
    uint32_t& grantedAccess) noexcept;
// Capture an AMD64 absolute or self-relative SECURITY_DESCRIPTOR and its
// bounded basic DACL into owned kernel storage. Unsupported ACEs/SACLs fail.
[[nodiscard]] NtStatus SeCaptureFreestandingSecurityDescriptor(
    const void* descriptor, ob::KPROCESSOR_MODE sourceMode,
    FreestandingSecurityDescriptor& captured) noexcept;
void SeInitializeNetworkConfigurationAcl(FreestandingSecurityDescriptor& descriptor) noexcept;
void SeInitializeSystemRootStoreAcl(FreestandingSecurityDescriptor& descriptor) noexcept;
#endif

// ============================================================================
// Standard Access Rights & Masks (aligned with Win32/NT security model)
// ============================================================================
inline constexpr uint32_t DELETE                    = 0x00010000;
inline constexpr uint32_t READ_CONTROL              = 0x00020000;
inline constexpr uint32_t WRITE_DAC                 = 0x00040000;
inline constexpr uint32_t WRITE_OWNER               = 0x00080000;
inline constexpr uint32_t SYNCHRONIZE               = 0x00100000;

inline constexpr uint32_t STANDARD_RIGHTS_REQUIRED  = 0x000F0000;
inline constexpr uint32_t STANDARD_RIGHTS_READ      = READ_CONTROL;
inline constexpr uint32_t STANDARD_RIGHTS_WRITE     = READ_CONTROL;
inline constexpr uint32_t STANDARD_RIGHTS_EXECUTE   = READ_CONTROL;
inline constexpr uint32_t STANDARD_RIGHTS_ALL       = 0x001F0000;
inline constexpr uint32_t SPECIFIC_RIGHTS_ALL       = 0x0000FFFF;

inline constexpr uint32_t FILE_READ_DATA            = 0x00000001;
inline constexpr uint32_t FILE_WRITE_DATA           = 0x00000002;
inline constexpr uint32_t FILE_APPEND_DATA          = 0x00000004;
inline constexpr uint32_t FILE_EXECUTE              = 0x00000020;

inline constexpr uint32_t ACCESS_SYSTEM_SECURITY    = 0x01000000;
inline constexpr uint32_t MAXIMUM_ALLOWED           = 0x02000000;

inline constexpr uint32_t GENERIC_READ              = 0x80000000;
inline constexpr uint32_t GENERIC_WRITE             = 0x40000000;
inline constexpr uint32_t GENERIC_EXECUTE           = 0x20000000;
inline constexpr uint32_t GENERIC_ALL               = 0x10000000;

// ============================================================================
// Generic Rights Mapping (standard NT GENERIC_MAPPING)
// ============================================================================
struct GenericMapping {
    uint32_t genericRead{0};
    uint32_t genericWrite{0};
    uint32_t genericExecute{0};
    uint32_t genericAll{0};
};

inline void RtlMapGenericMask(uint32_t& accessMask, const GenericMapping& mapping) noexcept {
    if (accessMask & GENERIC_ALL) {
        accessMask |= mapping.genericAll;
    }
    if (accessMask & GENERIC_READ) {
        accessMask |= mapping.genericRead;
    }
    if (accessMask & GENERIC_WRITE) {
        accessMask |= mapping.genericWrite;
    }
    if (accessMask & GENERIC_EXECUTE) {
        accessMask |= mapping.genericExecute;
    }
    accessMask &= ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
}

inline constexpr GenericMapping FILE_GENERIC_MAPPING{
    .genericRead = STANDARD_RIGHTS_READ | FILE_READ_DATA | SYNCHRONIZE,
    .genericWrite = STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | SYNCHRONIZE,
    .genericExecute = STANDARD_RIGHTS_EXECUTE | FILE_EXECUTE | SYNCHRONIZE,
    .genericAll = STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL | SYNCHRONIZE
};

// Privilege Set & Attributes
inline constexpr uint32_t PRIVILEGE_SET_ALL_NECESSARY = 1;

struct LuidAndAttributes {
    Luid luid{0, 0};
    uint32_t attributes{0};
};

struct PrivilegeSet {
    uint32_t privilegeCount{0};
    uint32_t control{0};
    std::vector<LuidAndAttributes> privileges;
};

// Security Descriptor Control Flags
inline constexpr uint16_t SE_OWNER_DEFAULTED        = 0x0001;
inline constexpr uint16_t SE_GROUP_DEFAULTED        = 0x0002;
inline constexpr uint16_t SE_DACL_PRESENT           = 0x0004;
inline constexpr uint16_t SE_DACL_DEFAULTED         = 0x0008;
inline constexpr uint16_t SE_SACL_PRESENT           = 0x0010;
inline constexpr uint16_t SE_SACL_DEFAULTED         = 0x0020;
inline constexpr uint16_t SE_DACL_AUTO_INHERIT_REQ  = 0x0100;
inline constexpr uint16_t SE_SACL_AUTO_INHERIT_REQ  = 0x0200;
inline constexpr uint16_t SE_DACL_AUTO_INHERITED    = 0x0400;
inline constexpr uint16_t SE_SACL_AUTO_INHERITED    = 0x0800;
inline constexpr uint16_t SE_DACL_PROTECTED         = 0x1000;
inline constexpr uint16_t SE_SACL_PROTECTED         = 0x2000;
inline constexpr uint16_t SE_RM_CONTROL_VALID       = 0x4000;
inline constexpr uint16_t SE_SELF_RELATIVE          = 0x8000;

// Token Privilege Attribute Flags
inline constexpr uint32_t SE_PRIVILEGE_ENABLED_BY_DEFAULT = 0x00000001;
inline constexpr uint32_t SE_PRIVILEGE_ENABLED            = 0x00000002;
inline constexpr uint32_t SE_PRIVILEGE_REMOVED            = 0x00000004;
inline constexpr uint32_t SE_PRIVILEGE_USED_FOR_ACCESS    = 0x80000000;

// Token Group Attribute Flags
inline constexpr uint32_t SE_GROUP_MANDATORY              = 0x00000001;
inline constexpr uint32_t SE_GROUP_ENABLED_BY_DEFAULT     = 0x00000002;
inline constexpr uint32_t SE_GROUP_ENABLED                = 0x00000004;
inline constexpr uint32_t SE_GROUP_OWNER                  = 0x00000008;
inline constexpr uint32_t SE_GROUP_USE_FOR_DENY_ONLY      = 0x00000010;
inline constexpr uint32_t SE_GROUP_INTEGRITY              = 0x00000020;
inline constexpr uint32_t SE_GROUP_RESOURCE               = 0x20000000;
inline constexpr uint32_t SE_GROUP_LOGON_ID               = 0xC0000000;

// ACE Inheritance & Audit Flags
inline constexpr uint8_t OBJECT_INHERIT_ACE               = 0x01;
inline constexpr uint8_t CONTAINER_INHERIT_ACE            = 0x02;
inline constexpr uint8_t NO_PROPAGATE_INHERIT_ACE         = 0x04;
inline constexpr uint8_t INHERIT_ONLY_ACE                 = 0x08;
inline constexpr uint8_t INHERITED_ACE                    = 0x10;
inline constexpr uint8_t SUCCESSFUL_ACCESS_ACE_FLAG       = 0x40;
inline constexpr uint8_t FAILED_ACCESS_ACE_FLAG           = 0x80;

// Mandatory Integrity Control (MIC) Label Policy Flags
inline constexpr uint32_t SYSTEM_MANDATORY_LABEL_NO_WRITE_UP   = 0x00000001;
inline constexpr uint32_t SYSTEM_MANDATORY_LABEL_NO_READ_UP    = 0x00000002;
inline constexpr uint32_t SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP = 0x00000004;

// Mandatory Integrity RIDs (Authority 16: SECURITY_MANDATORY_LABEL_AUTHORITY)
inline constexpr uint32_t SECURITY_MANDATORY_UNTRUSTED_RID         = 0x00000000; // S-1-16-0
inline constexpr uint32_t SECURITY_MANDATORY_LOW_RID               = 0x00001000; // S-1-16-4096
inline constexpr uint32_t SECURITY_MANDATORY_MEDIUM_RID            = 0x00002000; // S-1-16-8192
inline constexpr uint32_t SECURITY_MANDATORY_MEDIUM_PLUS_RID       = 0x00002100; // S-1-16-8448
inline constexpr uint32_t SECURITY_MANDATORY_HIGH_RID              = 0x00003000; // S-1-16-12288
inline constexpr uint32_t SECURITY_MANDATORY_SYSTEM_RID            = 0x00004000; // S-1-16-16384
inline constexpr uint32_t SECURITY_MANDATORY_PROTECTED_PROCESS_RID = 0x00005000; // S-1-16-20480

// Standard NT Privilege Strings
inline constexpr std::wstring_view SE_DEBUG_NAME             = L"SeDebugPrivilege";
inline constexpr std::wstring_view SE_SHUTDOWN_NAME          = L"SeShutdownPrivilege";
inline constexpr std::wstring_view SE_BACKUP_NAME            = L"SeBackupPrivilege";
inline constexpr std::wstring_view SE_RESTORE_NAME           = L"SeRestorePrivilege";
inline constexpr std::wstring_view SE_SECURITY_NAME          = L"SeSecurityPrivilege";
inline constexpr std::wstring_view SE_TAKE_OWNERSHIP_NAME    = L"SeTakeOwnershipPrivilege";
inline constexpr std::wstring_view SE_TCB_NAME               = L"SeTcbPrivilege";
inline constexpr std::wstring_view SE_IMPERSONATE_NAME       = L"SeImpersonatePrivilege";
inline constexpr std::wstring_view SE_CHANGE_NOTIFY_NAME     = L"SeChangeNotifyPrivilege";
inline constexpr std::wstring_view SE_SYSTEMTIME_NAME        = L"SeSystemtimePrivilege";
inline constexpr std::wstring_view SE_SYSTEM_ENVIRONMENT_NAME= L"SeSystemEnvironmentPrivilege";
inline constexpr std::wstring_view SE_ASSIGNPRIMARYTOKEN_NAME= L"SeAssignPrimaryTokenPrivilege";
inline constexpr std::wstring_view SE_INCREASE_QUOTA_NAME    = L"SeIncreaseQuotaPrivilege";
inline constexpr std::wstring_view SE_LOAD_DRIVER_NAME       = L"SeLoadDriverPrivilege";
inline constexpr std::wstring_view SE_CREATE_TOKEN_NAME      = L"SeCreateTokenPrivilege";
inline constexpr std::wstring_view SE_LOCK_MEMORY_NAME       = L"SeLockMemoryPrivilege";

enum class ProcessorMode : uint8_t {
    KernelMode = 0,
    UserMode   = 1
};

/**
 * @brief Security Identifier (SID).
 * Variable-length numerical value used to uniquely identify security principals.
 * Canonical string format: S-1-<IdentifierAuthority>-<SubAuth1>-<SubAuth2>...
 */
class Sid {
public:
    Sid() : revision_(1), identifierAuthority_{0, 0, 0, 0, 0, 0} {}

    Sid(uint64_t authority, std::initializer_list<uint32_t> subAuthorities)
        : revision_(1) {
        for (int i = 5; i >= 0; --i) {
            identifierAuthority_[i] = static_cast<uint8_t>(authority & 0xFF);
            authority >>= 8;
        }
        subAuthorities_.assign(subAuthorities.begin(), subAuthorities.end());
    }

    [[nodiscard]] uint8_t getRevision() const noexcept { return revision_; }
    [[nodiscard]] uint64_t getIdentifierAuthority() const noexcept {
        uint64_t auth = 0;
        for (uint8_t b : identifierAuthority_) {
            auth = (auth << 8) | b;
        }
        return auth;
    }
    [[nodiscard]] const std::vector<uint32_t>& getSubAuthorities() const noexcept {
        return subAuthorities_;
    }

    [[nodiscard]] std::wstring toString() const {
        std::wostringstream wos;
        wos << L"S-" << static_cast<uint32_t>(revision_) << L"-" << getIdentifierAuthority();
        for (uint32_t sa : subAuthorities_) {
            wos << L"-" << sa;
        }
        return wos.str();
    }

    bool operator==(const Sid& other) const noexcept {
        return revision_ == other.revision_ &&
               std::equal(identifierAuthority_, identifierAuthority_ + 6, other.identifierAuthority_) &&
               subAuthorities_ == other.subAuthorities_;
    }

    // Well-Known SIDs
    [[nodiscard]] static Sid null() { return Sid(0, {0}); }                     // S-1-0-0
    [[nodiscard]] static Sid everyone() { return Sid(1, {0}); }                 // S-1-1-0 (World)
    [[nodiscard]] static Sid local() { return Sid(2, {0}); }                    // S-1-2-0
    [[nodiscard]] static Sid creatorOwner() { return Sid(3, {0}); }             // S-1-3-0
    [[nodiscard]] static Sid authenticatedUsers() { return Sid(5, {11}); }      // S-1-5-11
    [[nodiscard]] static Sid localSystem() { return Sid(5, {18}); }             // S-1-5-18
    [[nodiscard]] static Sid localService() { return Sid(5, {19}); }            // S-1-5-19
    [[nodiscard]] static Sid networkService() { return Sid(5, {20}); }          // S-1-5-20
    [[nodiscard]] static Sid administrators() { return Sid(5, {32, 544}); }     // S-1-5-32-544 (Builtin Administrators)
    [[nodiscard]] static Sid users() { return Sid(5, {32, 545}); }              // S-1-5-32-545 (Builtin Users)

    // Integrity Level SIDs (Authority 16 = SECURITY_MANDATORY_LABEL_AUTHORITY)
    [[nodiscard]] static Sid fromIntegrityRid(uint32_t rid) { return Sid(16, {rid}); }
    [[nodiscard]] static Sid integrityUntrusted() { return fromIntegrityRid(SECURITY_MANDATORY_UNTRUSTED_RID); }
    [[nodiscard]] static Sid integrityLow() { return fromIntegrityRid(SECURITY_MANDATORY_LOW_RID); }
    [[nodiscard]] static Sid integrityMedium() { return fromIntegrityRid(SECURITY_MANDATORY_MEDIUM_RID); }
    [[nodiscard]] static Sid integrityMediumPlus() { return fromIntegrityRid(SECURITY_MANDATORY_MEDIUM_PLUS_RID); }
    [[nodiscard]] static Sid integrityHigh() { return fromIntegrityRid(SECURITY_MANDATORY_HIGH_RID); }
    [[nodiscard]] static Sid integritySystem() { return fromIntegrityRid(SECURITY_MANDATORY_SYSTEM_RID); }
    [[nodiscard]] static Sid integrityProtectedProcess() { return fromIntegrityRid(SECURITY_MANDATORY_PROTECTED_PROCESS_RID); }

    [[nodiscard]] size_t getBinaryLength() const noexcept {
        return 8 + subAuthorities_.size() * sizeof(uint32_t);
    }

    [[nodiscard]] std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer(getBinaryLength());
        buffer[0] = revision_;
        buffer[1] = static_cast<uint8_t>(subAuthorities_.size());
        std::copy(identifierAuthority_, identifierAuthority_ + 6, buffer.begin() + 2);
        for (size_t i = 0; i < subAuthorities_.size(); ++i) {
            uint32_t sa = subAuthorities_[i];
            size_t offset = 8 + i * 4;
            buffer[offset] = static_cast<uint8_t>(sa & 0xFF);
            buffer[offset + 1] = static_cast<uint8_t>((sa >> 8) & 0xFF);
            buffer[offset + 2] = static_cast<uint8_t>((sa >> 16) & 0xFF);
            buffer[offset + 3] = static_cast<uint8_t>((sa >> 24) & 0xFF);
        }
        return buffer;
    }

    static std::optional<Sid> deserialize(std::span<const uint8_t> data, size_t& bytesRead) {
        if (data.size() < 8) return std::nullopt;
        uint8_t rev = data[0];
        uint8_t count = data[1];
        if (data.size() < static_cast<size_t>(8 + count * 4)) return std::nullopt;
        Sid sid;
        sid.revision_ = rev;
        std::copy(data.begin() + 2, data.begin() + 8, sid.identifierAuthority_);
        sid.subAuthorities_.resize(count);
        for (size_t i = 0; i < count; ++i) {
            size_t offset = 8 + i * 4;
            uint32_t sa = static_cast<uint32_t>(data[offset]) |
                          (static_cast<uint32_t>(data[offset + 1]) << 8) |
                          (static_cast<uint32_t>(data[offset + 2]) << 16) |
                          (static_cast<uint32_t>(data[offset + 3]) << 24);
            sid.subAuthorities_[i] = sa;
        }
        bytesRead = 8 + count * 4;
        return sid;
    }

private:
    uint8_t revision_{1};
    uint8_t identifierAuthority_[6]{0};
    std::vector<uint32_t> subAuthorities_;
};

// ACE Types
enum class AceType : uint8_t {
    AccessAllowed        = 0x00,
    AccessDenied         = 0x01,
    SystemAudit          = 0x02,
    SystemAlarm          = 0x03,
    SystemMandatoryLabel = 0x11
};

/**
 * @brief Access Control Entry (ACE).
 */
struct Ace {
    AceType type{AceType::AccessAllowed};
    uint8_t flags{0};
    uint32_t mask{0};
    Sid sid;
};

/**
 * @brief Access Control List (ACL).
 * Manages an ordered collection of ACEs.
 */
class Acl {
public:
    Acl() = default;

    void addAllowedAce(const Sid& sid, uint32_t mask, uint8_t flags = 0) {
        aces_.push_back(Ace{ .type = AceType::AccessAllowed, .flags = flags, .mask = mask, .sid = sid });
    }

    void addDeniedAce(const Sid& sid, uint32_t mask, uint8_t flags = 0) {
        aces_.push_back(Ace{ .type = AceType::AccessDenied, .flags = flags, .mask = mask, .sid = sid });
    }

    void addAuditAce(const Sid& sid, uint32_t mask, uint8_t flags = SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG) {
        aces_.push_back(Ace{ .type = AceType::SystemAudit, .flags = flags, .mask = mask, .sid = sid });
    }

    void addMandatoryLabelAce(const Sid& integritySid, uint32_t policyMask, uint8_t flags = 0) {
        aces_.push_back(Ace{ .type = AceType::SystemMandatoryLabel, .flags = flags, .mask = policyMask, .sid = integritySid });
    }

    void addAce(Ace ace) {
        aces_.push_back(std::move(ace));
    }

    [[nodiscard]] size_t getAceCount() const noexcept { return aces_.size(); }
    [[nodiscard]] const std::vector<Ace>& getAces() const noexcept { return aces_; }
    [[nodiscard]] std::vector<Ace>& getAces() noexcept { return aces_; }
    void clear() noexcept { aces_.clear(); }

    [[nodiscard]] size_t getBinaryLength() const noexcept {
        size_t total = 8; // ACL header: revision (1), sbz1 (1), aclSize (2), aceCount (2), sbz2 (2)
        for (const auto& ace : aces_) {
            total += 8 + ace.sid.getBinaryLength(); // ACE header (8): type (1), flags (1), size (2), mask (4)
        }
        return total;
    }

    [[nodiscard]] std::vector<uint8_t> serialize() const {
        size_t totalSize = getBinaryLength();
        std::vector<uint8_t> buffer(totalSize, 0);
        buffer[0] = 2; // ACL_REVISION
        buffer[1] = 0; // Sbz1
        buffer[2] = static_cast<uint8_t>(totalSize & 0xFF);
        buffer[3] = static_cast<uint8_t>((totalSize >> 8) & 0xFF);
        buffer[4] = static_cast<uint8_t>(aces_.size() & 0xFF);
        buffer[5] = static_cast<uint8_t>((aces_.size() >> 8) & 0xFF);
        buffer[6] = 0; // Sbz2
        buffer[7] = 0;

        size_t offset = 8;
        for (const auto& ace : aces_) {
            size_t sidLen = ace.sid.getBinaryLength();
            size_t aceSize = 8 + sidLen;
            buffer[offset] = static_cast<uint8_t>(ace.type);
            buffer[offset + 1] = ace.flags;
            buffer[offset + 2] = static_cast<uint8_t>(aceSize & 0xFF);
            buffer[offset + 3] = static_cast<uint8_t>((aceSize >> 8) & 0xFF);
            buffer[offset + 4] = static_cast<uint8_t>(ace.mask & 0xFF);
            buffer[offset + 5] = static_cast<uint8_t>((ace.mask >> 8) & 0xFF);
            buffer[offset + 6] = static_cast<uint8_t>((ace.mask >> 16) & 0xFF);
            buffer[offset + 7] = static_cast<uint8_t>((ace.mask >> 24) & 0xFF);

            auto sidBytes = ace.sid.serialize();
            std::copy(sidBytes.begin(), sidBytes.end(), buffer.begin() + offset + 8);
            offset += aceSize;
        }
        return buffer;
    }

    static std::optional<Acl> deserialize(std::span<const uint8_t> data, size_t& bytesRead) {
        if (data.size() < 8) return std::nullopt;
        uint16_t aclSize = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
        uint16_t aceCount = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
        if (data.size() < aclSize || aclSize < 8) return std::nullopt;

        Acl acl;
        size_t offset = 8;
        for (uint16_t i = 0; i < aceCount; ++i) {
            if (offset + 8 > aclSize) return std::nullopt;
            AceType type = static_cast<AceType>(data[offset]);
            uint8_t flags = data[offset + 1];
            uint16_t aceSize = static_cast<uint16_t>(data[offset + 2]) | (static_cast<uint16_t>(data[offset + 3]) << 8);
            uint32_t mask = static_cast<uint32_t>(data[offset + 4]) |
                            (static_cast<uint32_t>(data[offset + 5]) << 8) |
                            (static_cast<uint32_t>(data[offset + 6]) << 16) |
                            (static_cast<uint32_t>(data[offset + 7]) << 24);
            if (offset + aceSize > aclSize || aceSize < 8) return std::nullopt;

            size_t sidBytesRead = 0;
            auto sidOpt = Sid::deserialize(data.subspan(offset + 8, aceSize - 8), sidBytesRead);
            if (!sidOpt) return std::nullopt;

            acl.aces_.push_back(Ace{
                .type = type,
                .flags = flags,
                .mask = mask,
                .sid = std::move(*sidOpt)
            });
            offset += aceSize;
        }
        bytesRead = aclSize;
        return acl;
    }

private:
    std::vector<Ace> aces_;
};

/**
 * @brief Security Descriptor.
 * Contains security information associated with an object: Owner, Group, DACL, SACL, and Control Flags.
 */
class SecurityDescriptor {
public:
    SecurityDescriptor() = default;

    void setOwner(Sid owner) { 
        owner_ = std::move(owner); 
        control_ &= ~SE_OWNER_DEFAULTED;
    }
    [[nodiscard]] const std::optional<Sid>& getOwner() const noexcept { return owner_; }

    void setGroup(Sid group) { 
        group_ = std::move(group); 
        control_ &= ~SE_GROUP_DEFAULTED;
    }
    [[nodiscard]] const std::optional<Sid>& getGroup() const noexcept { return group_; }

    void setDacl(Acl dacl) { 
        dacl_ = std::move(dacl); 
        daclPresent_ = true; 
        control_ |= SE_DACL_PRESENT;
    }
    void removeDacl() noexcept {
        dacl_.reset();
        daclPresent_ = false;
        control_ &= ~SE_DACL_PRESENT;
    }
    [[nodiscard]] const std::optional<Acl>& getDacl() const noexcept { return dacl_; }
    [[nodiscard]] bool hasDacl() const noexcept { return daclPresent_; }

    void setSacl(Acl sacl) {
        sacl_ = std::move(sacl);
        saclPresent_ = true;
        control_ |= SE_SACL_PRESENT;
    }
    void removeSacl() noexcept {
        sacl_.reset();
        saclPresent_ = false;
        control_ &= ~SE_SACL_PRESENT;
    }
    [[nodiscard]] const std::optional<Acl>& getSacl() const noexcept { return sacl_; }
    [[nodiscard]] bool hasSacl() const noexcept { return saclPresent_; }

    [[nodiscard]] uint16_t getControl() const noexcept { return control_; }
    void setControl(uint16_t control) noexcept { control_ = control; }

    // Mandatory Integrity Control Helpers (encoded in SACL as SystemMandatoryLabel ACE)
    void setIntegrityLevel(uint32_t integrityRid, uint32_t policy = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) {
        if (!sacl_) {
            sacl_ = Acl();
            saclPresent_ = true;
            control_ |= SE_SACL_PRESENT;
        }
        for (auto& ace : sacl_->getAces()) {
            if (ace.type == AceType::SystemMandatoryLabel) {
                ace.sid = Sid::fromIntegrityRid(integrityRid);
                ace.mask = policy;
                return;
            }
        }
        sacl_->addMandatoryLabelAce(Sid::fromIntegrityRid(integrityRid), policy);
    }

    [[nodiscard]] uint32_t getIntegrityLevel() const noexcept {
        if (sacl_) {
            for (const auto& ace : sacl_->getAces()) {
                if (ace.type == AceType::SystemMandatoryLabel) {
                    const auto& sub = ace.sid.getSubAuthorities();
                    if (!sub.empty()) {
                        return sub[0];
                    }
                }
            }
        }
        return SECURITY_MANDATORY_MEDIUM_RID; // Standard NT object default
    }

    [[nodiscard]] uint32_t getIntegrityPolicy() const noexcept {
        if (sacl_) {
            for (const auto& ace : sacl_->getAces()) {
                if (ace.type == AceType::SystemMandatoryLabel) {
                    return ace.mask;
                }
            }
        }
        return SYSTEM_MANDATORY_LABEL_NO_WRITE_UP;
    }

    [[nodiscard]] bool hasMandatoryLabel() const noexcept {
        if (sacl_) {
            for (const auto& ace : sacl_->getAces()) {
                if (ace.type == AceType::SystemMandatoryLabel) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::vector<uint8_t> makeSelfRelative() const {
        size_t headerSize = 20; // 1 + 1 + 2 + 4 + 4 + 4 + 4
        size_t ownerSize = owner_ ? owner_->getBinaryLength() : 0;
        size_t groupSize = group_ ? group_->getBinaryLength() : 0;
        size_t saclSize = (saclPresent_ && sacl_) ? sacl_->getBinaryLength() : 0;
        size_t daclSize = (daclPresent_ && dacl_) ? dacl_->getBinaryLength() : 0;

        size_t total = headerSize + ownerSize + groupSize + saclSize + daclSize;
        std::vector<uint8_t> buffer(total, 0);

        uint16_t ctrl = control_ | SE_SELF_RELATIVE;
        buffer[0] = 1; // Revision
        buffer[1] = 0; // Sbz1
        buffer[2] = static_cast<uint8_t>(ctrl & 0xFF);
        buffer[3] = static_cast<uint8_t>((ctrl >> 8) & 0xFF);

        size_t currOffset = headerSize;
        uint32_t ownerOff = 0;
        if (owner_) {
            ownerOff = static_cast<uint32_t>(currOffset);
            auto b = owner_->serialize();
            std::copy(b.begin(), b.end(), buffer.begin() + currOffset);
            currOffset += ownerSize;
        }

        uint32_t groupOff = 0;
        if (group_) {
            groupOff = static_cast<uint32_t>(currOffset);
            auto b = group_->serialize();
            std::copy(b.begin(), b.end(), buffer.begin() + currOffset);
            currOffset += groupSize;
        }

        uint32_t saclOff = 0;
        if (saclPresent_ && sacl_) {
            saclOff = static_cast<uint32_t>(currOffset);
            auto b = sacl_->serialize();
            std::copy(b.begin(), b.end(), buffer.begin() + currOffset);
            currOffset += saclSize;
        }

        uint32_t daclOff = 0;
        if (daclPresent_ && dacl_) {
            daclOff = static_cast<uint32_t>(currOffset);
            auto b = dacl_->serialize();
            std::copy(b.begin(), b.end(), buffer.begin() + currOffset);
            currOffset += daclSize;
        }

        auto writeU32 = [&](size_t off, uint32_t val) {
            buffer[off] = static_cast<uint8_t>(val & 0xFF);
            buffer[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            buffer[off + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            buffer[off + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
        };
        writeU32(4, ownerOff);
        writeU32(8, groupOff);
        writeU32(12, saclOff);
        writeU32(16, daclOff);

        return buffer;
    }

    static std::optional<SecurityDescriptor> fromSelfRelative(std::span<const uint8_t> buffer) {
        if (buffer.size() < 20) return std::nullopt;
        if (buffer[0] != 1) return std::nullopt; // Revision 1
        uint16_t ctrl = static_cast<uint16_t>(buffer[2]) | (static_cast<uint16_t>(buffer[3]) << 8);

        auto readU32 = [&](size_t off) -> uint32_t {
            return static_cast<uint32_t>(buffer[off]) |
                   (static_cast<uint32_t>(buffer[off + 1]) << 8) |
                   (static_cast<uint32_t>(buffer[off + 2]) << 16) |
                   (static_cast<uint32_t>(buffer[off + 3]) << 24);
        };

        uint32_t ownerOff = readU32(4);
        uint32_t groupOff = readU32(8);
        uint32_t saclOff = readU32(12);
        uint32_t daclOff = readU32(16);

        SecurityDescriptor sd;
        sd.control_ = ctrl;

        if (ownerOff > 0 && ownerOff < buffer.size()) {
            size_t bytesRead = 0;
            auto sid = Sid::deserialize(buffer.subspan(ownerOff), bytesRead);
            if (sid) sd.owner_ = std::move(sid);
        }

        if (groupOff > 0 && groupOff < buffer.size()) {
            size_t bytesRead = 0;
            auto sid = Sid::deserialize(buffer.subspan(groupOff), bytesRead);
            if (sid) sd.group_ = std::move(sid);
        }

        if ((ctrl & SE_SACL_PRESENT) && saclOff > 0 && saclOff < buffer.size()) {
            size_t bytesRead = 0;
            auto acl = Acl::deserialize(buffer.subspan(saclOff), bytesRead);
            if (acl) {
                sd.sacl_ = std::move(acl);
                sd.saclPresent_ = true;
            }
        }

        if ((ctrl & SE_DACL_PRESENT) && daclOff > 0 && daclOff < buffer.size()) {
            size_t bytesRead = 0;
            auto acl = Acl::deserialize(buffer.subspan(daclOff), bytesRead);
            if (acl) {
                sd.dacl_ = std::move(acl);
                sd.daclPresent_ = true;
            }
        } else if (ctrl & SE_DACL_PRESENT) {
            sd.daclPresent_ = true; // Null DACL
        }

        return sd;
    }

private:
    std::optional<Sid> owner_;
    std::optional<Sid> group_;
    std::optional<Acl> dacl_;
    std::optional<Acl> sacl_;
    bool daclPresent_{false};
    bool saclPresent_{false};
    uint16_t control_{0};
};

/**
 * @brief Token Type (Primary or Impersonation).
 */
enum class TokenType : uint32_t {
    Primary = 1,
    Impersonation = 2
};

/**
 * @brief Security Impersonation Level.
 */
enum class SecurityImpersonationLevel : uint32_t {
    Anonymous = 0,
    Identification = 1,
    Impersonation = 2,
    Delegation = 3
};

/**
 * @brief Token Group Entry with NT Attribute Flags.
 */
struct TokenGroup {
    Sid sid;
    uint32_t attributes{SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT};
};

/**
 * @brief Process / Thread Access Token.
 * Represents user identity, group memberships, assigned administrative privileges, and integrity level.
 */
class TokenObject {
public:
    explicit TokenObject(Sid userSid, TokenType type = TokenType::Primary)
        : userSid_(std::move(userSid)), tokenType_(type) {}

    [[nodiscard]] const Sid& getUserSid() const noexcept { return userSid_; }
    [[nodiscard]] TokenType getTokenType() const noexcept { return tokenType_; }
    void setTokenType(TokenType t) noexcept { tokenType_ = t; }

    [[nodiscard]] const std::vector<Sid>& getGroupSids() const noexcept { 
        return cachedGroupSids_; 
    }
    [[nodiscard]] const std::vector<TokenGroup>& getGroups() const noexcept {
        return groups_;
    }

    void addGroup(Sid group, uint32_t attributes = SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT) {
        cachedGroupSids_.push_back(group);
        groups_.push_back(TokenGroup{ .sid = std::move(group), .attributes = attributes });
    }

    [[nodiscard]] bool hasSid(const Sid& sid, bool allowDenyOnly = true) const {
        if (userSid_ == sid) return true;
        for (const auto& g : groups_) {
            if (g.sid == sid) {
                if ((g.attributes & SE_GROUP_USE_FOR_DENY_ONLY) != 0) {
                    if (allowDenyOnly) return true;
                    continue; // Cannot be used for access allow
                }
                if ((g.attributes & SE_GROUP_ENABLED) != 0) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] uint32_t getIntegrityLevel() const noexcept { return integrityLevel_; }
    void setIntegrityLevel(uint32_t level) noexcept { integrityLevel_ = level; }
    [[nodiscard]] Sid getIntegritySid() const noexcept { return Sid::fromIntegrityRid(integrityLevel_); }

    void addPrivilege(std::wstring_view name, uint32_t attributes = SE_PRIVILEGE_ENABLED) {
        privileges_[std::wstring(name)] = attributes;
    }

    void removePrivilege(std::wstring_view name) {
        privileges_.erase(std::wstring(name));
    }

    [[nodiscard]] bool hasPrivilege(std::wstring_view name, bool mustBeEnabled = true) const {
        auto it = privileges_.find(std::wstring(name));
        if (it == privileges_.end()) return false;
        if (mustBeEnabled) {
            return (it->second & SE_PRIVILEGE_ENABLED) != 0;
        }
        return true;
    }

    bool enablePrivilege(std::wstring_view name, bool enable = true) {
        auto it = privileges_.find(std::wstring(name));
        if (it == privileges_.end()) return false;
        if (enable) {
            it->second |= SE_PRIVILEGE_ENABLED;
        } else {
            it->second &= ~SE_PRIVILEGE_ENABLED;
        }
        return true;
    }

    [[nodiscard]] const std::unordered_map<std::wstring, uint32_t>& getPrivileges() const noexcept {
        return privileges_;
    }

    [[nodiscard]] Luid getAuthenticationId() const noexcept { return authId_; }
    void setAuthenticationId(Luid id) noexcept { authId_ = id; }

    [[nodiscard]] uint32_t getSessionId() const noexcept { return sessionId_; }
    void setSessionId(uint32_t id) noexcept { sessionId_ = id; }

    [[nodiscard]] SecurityImpersonationLevel getImpersonationLevel() const noexcept { return impersonationLevel_; }
    void setImpersonationLevel(SecurityImpersonationLevel level) noexcept { impersonationLevel_ = level; }

    [[nodiscard]] const std::vector<Sid>& getRestrictedSids() const noexcept { return restrictedSids_; }
    [[nodiscard]] bool isRestricted() const noexcept { return isRestricted_; }

    /**
     * @brief Creates a restricted token (NtFilterToken).
     */
    [[nodiscard]] std::shared_ptr<TokenObject> createRestrictedToken(
        const std::vector<Sid>& sidsToDisable,
        const std::vector<std::wstring>& privilegesToRemove,
        const std::vector<Sid>& restrictingSids = {}
    ) const {
        auto clone = std::make_shared<TokenObject>(userSid_, tokenType_);
        clone->impersonationLevel_ = impersonationLevel_;
        clone->authId_ = authId_;
        clone->sessionId_ = sessionId_;
        clone->integrityLevel_ = integrityLevel_;

        // Groups: mark disabled SIDs as deny-only
        for (const auto& g : groups_) {
            uint32_t attr = g.attributes;
            bool shouldDisable = std::any_of(sidsToDisable.begin(), sidsToDisable.end(),
                [&g](const Sid& s) { return s == g.sid; });
            if (shouldDisable) {
                attr &= ~SE_GROUP_ENABLED;
                attr |= SE_GROUP_USE_FOR_DENY_ONLY;
            }
            clone->addGroup(g.sid, attr);
        }

        // Privileges: copy all except removed
        for (const auto& [name, attr] : privileges_) {
            bool shouldRemove = std::any_of(privilegesToRemove.begin(), privilegesToRemove.end(),
                [&name](const std::wstring& p) { return p == name; });
            if (!shouldRemove) {
                clone->addPrivilege(name, attr);
            }
        }

        if (!restrictingSids.empty()) {
            clone->isRestricted_ = true;
            clone->restrictedSids_ = restrictingSids;
        }

        return clone;
    }

    /**
     * @brief Simulates standard NT UAC token filtering: drops admin privileges,
     * marks administrators group as SE_GROUP_USE_FOR_DENY_ONLY, and sets Medium integrity.
     */
    [[nodiscard]] std::shared_ptr<TokenObject> filterTokenForUac() const {
        std::vector<Sid> sidsToDisable = { Sid::administrators() };
        std::vector<std::wstring> privsToRemove = {
            std::wstring(SE_DEBUG_NAME),
            std::wstring(SE_SHUTDOWN_NAME),
            std::wstring(SE_BACKUP_NAME),
            std::wstring(SE_RESTORE_NAME),
            std::wstring(SE_SECURITY_NAME),
            std::wstring(SE_TAKE_OWNERSHIP_NAME),
            std::wstring(SE_TCB_NAME),
            std::wstring(SE_SYSTEM_ENVIRONMENT_NAME),
            std::wstring(SE_ASSIGNPRIMARYTOKEN_NAME),
            std::wstring(SE_INCREASE_QUOTA_NAME),
            std::wstring(SE_LOAD_DRIVER_NAME),
            std::wstring(SE_CREATE_TOKEN_NAME),
            std::wstring(SE_LOCK_MEMORY_NAME)
        };

        auto filtered = createRestrictedToken(sidsToDisable, privsToRemove);
        filtered->setIntegrityLevel(SECURITY_MANDATORY_MEDIUM_RID);
        return filtered;
    }

    // Factory Helpers
    [[nodiscard]] static std::shared_ptr<TokenObject> createSystemToken() {
        auto token = std::make_shared<TokenObject>(Sid::localSystem());
        token->setIntegrityLevel(SECURITY_MANDATORY_SYSTEM_RID);
        token->addGroup(Sid::administrators());
        token->addGroup(Sid::authenticatedUsers());
        token->addGroup(Sid::everyone());
        token->addPrivilege(SE_DEBUG_NAME, SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        token->addPrivilege(SE_SHUTDOWN_NAME, SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        token->addPrivilege(SE_TCB_NAME, SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        token->addPrivilege(SE_IMPERSONATE_NAME, SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        token->addPrivilege(SE_CHANGE_NOTIFY_NAME, SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        return token;
    }

    [[nodiscard]] static std::shared_ptr<TokenObject> createAdminToken(Sid userSid) {
        auto token = std::make_shared<TokenObject>(std::move(userSid));
        token->setIntegrityLevel(SECURITY_MANDATORY_HIGH_RID);
        token->addGroup(Sid::administrators());
        token->addGroup(Sid::authenticatedUsers());
        token->addGroup(Sid::everyone());
        token->addPrivilege(SE_DEBUG_NAME, SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        token->addPrivilege(SE_SHUTDOWN_NAME, SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        token->addPrivilege(SE_CHANGE_NOTIFY_NAME, SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        return token;
    }

    [[nodiscard]] static std::shared_ptr<TokenObject> createUserToken(Sid userSid) {
        auto token = std::make_shared<TokenObject>(std::move(userSid));
        token->setIntegrityLevel(SECURITY_MANDATORY_MEDIUM_RID);
        token->addGroup(Sid::users());
        token->addGroup(Sid::authenticatedUsers());
        token->addGroup(Sid::everyone());
        token->addPrivilege(SE_CHANGE_NOTIFY_NAME, SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT);
        return token;
    }

private:
    Sid userSid_;
    std::vector<TokenGroup> groups_;
    std::vector<Sid> cachedGroupSids_;
    std::unordered_map<std::wstring, uint32_t> privileges_;
    TokenType tokenType_{TokenType::Primary};
    SecurityImpersonationLevel impersonationLevel_{SecurityImpersonationLevel::Impersonation};
    Luid authId_{0, 0};
    uint32_t sessionId_{0};
    uint32_t integrityLevel_{SECURITY_MANDATORY_MEDIUM_RID};
    bool isRestricted_{false};
    std::vector<Sid> restrictedSids_;
};

/**
 * @brief Security Audit Record.
 */
struct SecurityAuditRecord {
    Sid userSid;
    std::wstring objectName;
    uint32_t desiredAccess{0};
    uint32_t grantedAccess{0};
    bool accessGranted{false};
};

/**
 * @brief Security Subject Context (SECURITY_SUBJECT_CONTEXT).
 * Captures process and thread client impersonation tokens for access checking.
 */
struct SecuritySubjectContext {
    std::shared_ptr<TokenObject> processToken;
    std::shared_ptr<TokenObject> clientToken;
    SecurityImpersonationLevel impersonationLevel{SecurityImpersonationLevel::Anonymous};

    [[nodiscard]] const std::shared_ptr<TokenObject>& getEffectiveToken() const noexcept {
        if (clientToken) return clientToken;
        return processToken;
    }

    [[nodiscard]] bool isImpersonating() const noexcept {
        return clientToken != nullptr;
    }
};

inline void SeCaptureSubjectContext(
    SecuritySubjectContext& subjectContext,
    std::shared_ptr<TokenObject> processToken,
    std::shared_ptr<TokenObject> clientToken = nullptr
) noexcept {
    subjectContext.processToken = std::move(processToken);
    subjectContext.clientToken = std::move(clientToken);
    if (subjectContext.clientToken) {
        subjectContext.impersonationLevel = subjectContext.clientToken->getImpersonationLevel();
    } else {
        subjectContext.impersonationLevel = SecurityImpersonationLevel::Anonymous;
    }
}

inline void SeReleaseSubjectContext(SecuritySubjectContext& subjectContext) noexcept {
    subjectContext.processToken.reset();
    subjectContext.clientToken.reset();
    subjectContext.impersonationLevel = SecurityImpersonationLevel::Anonymous;
}

/**
 * @brief Security Reference Monitor (SRM) Subsystem Telemetry.
 */
struct SeTelemetry {
    uint64_t totalAccessChecks{0};
    uint64_t totalAccessChecksGranted{0};
    uint64_t totalAccessChecksDenied{0};
    uint64_t totalPrivilegeChecks{0};
    uint64_t totalMaximumAllowedEvaluations{0};
    uint64_t totalInheritedAcesPropagated{0};
    uint64_t totalSecurityDescriptorsAssigned{0};
    uint64_t totalIntegrityViolations{0};
    uint64_t totalBackupRestoreOverrides{0};
    uint64_t totalTokenImpersonations{0};
};

inline SeTelemetry g_SeTelemetry{};
inline std::mutex g_SeTelemetryMutex;

inline SeTelemetry SeQueryTelemetry() {
    std::lock_guard<std::mutex> lk(g_SeTelemetryMutex);
    return g_SeTelemetry;
}

inline void SeResetTelemetryForTesting() {
    std::lock_guard<std::mutex> lk(g_SeTelemetryMutex);
    g_SeTelemetry = SeTelemetry{};
}

/**
 * @brief Security Reference Monitor (SRM).
 * Performs Dave Cutler's NT access validation algorithm against Security Descriptors,
 * Mandatory Integrity Control (MIC), and SACL Auditing.
 */
class SecurityReferenceMonitor {
public:
    inline static std::vector<SecurityAuditRecord> s_AuditLog;

    static const std::vector<SecurityAuditRecord>& getAuditLog() noexcept { return s_AuditLog; }
    static void clearAuditLog() noexcept { s_AuditLog.clear(); }

    [[nodiscard]] static bool SeSinglePrivilegeCheck(
        std::wstring_view privName,
        const TokenObject& token,
        ProcessorMode mode = ProcessorMode::UserMode
    ) noexcept {
        std::lock_guard<std::mutex> lk(g_SeTelemetryMutex);
        g_SeTelemetry.totalPrivilegeChecks++;
        if (mode == ProcessorMode::KernelMode) {
            return true;
        }
        return token.hasPrivilege(privName, true);
    }

    [[nodiscard]] static bool SePrivilegeCheck(
        const std::vector<std::wstring>& privNames,
        const TokenObject& token,
        bool allRequired = true,
        ProcessorMode mode = ProcessorMode::UserMode
    ) noexcept {
        std::lock_guard<std::mutex> lk(g_SeTelemetryMutex);
        g_SeTelemetry.totalPrivilegeChecks++;
        if (mode == ProcessorMode::KernelMode) {
            return true;
        }
        if (privNames.empty()) {
            return true;
        }
        if (allRequired) {
            for (const auto& priv : privNames) {
                if (!token.hasPrivilege(priv, true)) {
                    return false;
                }
            }
            return true;
        } else {
            for (const auto& priv : privNames) {
                if (token.hasPrivilege(priv, true)) {
                    return true;
                }
            }
            return false;
        }
    }

    [[nodiscard]] static NtStatus SeAccessCheck(
        const SecurityDescriptor& secDesc,
        const SecuritySubjectContext& subjectContext,
        uint32_t desiredAccess,
        const GenericMapping& genericMapping,
        uint32_t& grantedAccess,
        std::wstring_view objectName = L"",
        ProcessorMode mode = ProcessorMode::UserMode
    ) noexcept {
        std::lock_guard<std::mutex> lk(g_SeTelemetryMutex);
        g_SeTelemetry.totalAccessChecks++;

        grantedAccess = 0;
        if (mode == ProcessorMode::KernelMode) {
            grantedAccess = (desiredAccess & MAXIMUM_ALLOWED) ? (STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL) : desiredAccess;
            g_SeTelemetry.totalAccessChecksGranted++;
            return NtStatus::Success;
        }

        if (subjectContext.isImpersonating()) {
            g_SeTelemetry.totalTokenImpersonations++;
            if (subjectContext.impersonationLevel < SecurityImpersonationLevel::Impersonation) {
                g_SeTelemetry.totalAccessChecksDenied++;
                return NtStatus::BadImpersonationLevel;
            }
        }

        const auto& token = subjectContext.getEffectiveToken();
        if (!token) {
            g_SeTelemetry.totalAccessChecksDenied++;
            return NtStatus::AccessDenied;
        }

        return accessCheckInternal(*token, secDesc, desiredAccess, genericMapping, grantedAccess, objectName);
    }

    [[nodiscard]] static NtStatus SeAccessCheck(
        const SecurityDescriptor& secDesc,
        const TokenObject& token,
        uint32_t desiredAccess,
        const GenericMapping& genericMapping,
        uint32_t& grantedAccess,
        std::wstring_view objectName = L"",
        ProcessorMode mode = ProcessorMode::UserMode
    ) noexcept {
        std::lock_guard<std::mutex> lk(g_SeTelemetryMutex);
        g_SeTelemetry.totalAccessChecks++;

        grantedAccess = 0;
        if (mode == ProcessorMode::KernelMode) {
            grantedAccess = (desiredAccess & MAXIMUM_ALLOWED) ? (STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL) : desiredAccess;
            g_SeTelemetry.totalAccessChecksGranted++;
            return NtStatus::Success;
        }

        return accessCheckInternal(token, secDesc, desiredAccess, genericMapping, grantedAccess, objectName);
    }

    [[nodiscard]] static NtStatus accessCheck(
        const TokenObject& token,
        const SecurityDescriptor& secDesc,
        uint32_t desiredAccess,
        uint32_t& grantedAccess,
        std::wstring_view objectName = L""
    ) noexcept {
        return SeAccessCheck(secDesc, token, desiredAccess, FILE_GENERIC_MAPPING, grantedAccess, objectName, ProcessorMode::UserMode);
    }

private:
    [[nodiscard]] static NtStatus accessCheckInternal(
        const TokenObject& token,
        const SecurityDescriptor& secDesc,
        uint32_t desiredAccess,
        const GenericMapping& genericMapping,
        uint32_t& grantedAccess,
        std::wstring_view objectName = L""
    ) noexcept {
        grantedAccess = 0;
        if (desiredAccess == 0) {
            g_SeTelemetry.totalAccessChecksGranted++;
            return NtStatus::Success;
        }

        uint32_t remainingDesired = desiredAccess;
        bool isMaximumAllowed = (remainingDesired & MAXIMUM_ALLOWED) != 0;
        remainingDesired &= ~MAXIMUM_ALLOWED;
        RtlMapGenericMask(remainingDesired, genericMapping);

        // 1. Mandatory Integrity Control (MIC) Check
        if (secDesc.hasMandatoryLabel()) {
            uint32_t objIntegrity = secDesc.getIntegrityLevel();
            uint32_t subIntegrity = token.getIntegrityLevel();
            uint32_t policy = secDesc.getIntegrityPolicy();

            if (subIntegrity < objIntegrity) {
                // Write access check
                constexpr uint32_t WRITE_RIGHTS = GENERIC_WRITE | WRITE_DAC | WRITE_OWNER | DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA;
                if ((policy & SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) &&
                    ((desiredAccess & (GENERIC_WRITE | GENERIC_ALL)) != 0 || (remainingDesired & WRITE_RIGHTS) != 0)) {
                    g_SeTelemetry.totalIntegrityViolations++;
                    g_SeTelemetry.totalAccessChecksDenied++;
                    recordAudit(token.getUserSid(), objectName, desiredAccess, 0, false, secDesc);
                    return NtStatus::AccessDenied;
                }
                // Read access check
                constexpr uint32_t READ_RIGHTS = GENERIC_READ | FILE_READ_DATA;
                if ((policy & SYSTEM_MANDATORY_LABEL_NO_READ_UP) &&
                    ((desiredAccess & (GENERIC_READ | GENERIC_ALL)) != 0 || (remainingDesired & READ_RIGHTS) != 0)) {
                    g_SeTelemetry.totalIntegrityViolations++;
                    g_SeTelemetry.totalAccessChecksDenied++;
                    recordAudit(token.getUserSid(), objectName, desiredAccess, 0, false, secDesc);
                    return NtStatus::AccessDenied;
                }
                // Execute access check
                constexpr uint32_t EXEC_RIGHTS = GENERIC_EXECUTE | FILE_EXECUTE;
                if ((policy & SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP) &&
                    ((desiredAccess & (GENERIC_EXECUTE | GENERIC_ALL)) != 0 || (remainingDesired & EXEC_RIGHTS) != 0)) {
                    g_SeTelemetry.totalIntegrityViolations++;
                    g_SeTelemetry.totalAccessChecksDenied++;
                    recordAudit(token.getUserSid(), objectName, desiredAccess, 0, false, secDesc);
                    return NtStatus::AccessDenied;
                }
            }
        }

        // 2. ACCESS_SYSTEM_SECURITY privilege check (requires SeSecurityPrivilege)
        if (remainingDesired & ACCESS_SYSTEM_SECURITY) {
            if (!token.hasPrivilege(SE_SECURITY_NAME, true)) {
                g_SeTelemetry.totalAccessChecksDenied++;
                recordAudit(token.getUserSid(), objectName, desiredAccess, 0, false, secDesc);
                return NtStatus::PrivilegeNotHeld;
            }
            grantedAccess |= ACCESS_SYSTEM_SECURITY;
            remainingDesired &= ~ACCESS_SYSTEM_SECURITY;
        }

        // 3. WRITE_OWNER with SeTakeOwnershipPrivilege
        if ((remainingDesired & WRITE_OWNER) && token.hasPrivilege(SE_TAKE_OWNERSHIP_NAME, true)) {
            grantedAccess |= WRITE_OWNER;
            remainingDesired &= ~WRITE_OWNER;
        }

        // 4. SeBackupPrivilege / SeRestorePrivilege overrides
        if (token.hasPrivilege(SE_BACKUP_NAME, true)) {
            uint32_t backupRights = remainingDesired & (READ_CONTROL | FILE_READ_DATA);
            if (backupRights != 0) {
                grantedAccess |= backupRights;
                remainingDesired &= ~backupRights;
                g_SeTelemetry.totalBackupRestoreOverrides++;
            }
        }
        if (token.hasPrivilege(SE_RESTORE_NAME, true)) {
            uint32_t restoreRights = remainingDesired & (WRITE_DAC | WRITE_OWNER | DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA);
            if (restoreRights != 0) {
                grantedAccess |= restoreRights;
                remainingDesired &= ~restoreRights;
                g_SeTelemetry.totalBackupRestoreOverrides++;
            }
        }

        // 5. Owner privilege: Owner always has READ_CONTROL and WRITE_DAC
        if (secDesc.getOwner() && token.hasSid(*secDesc.getOwner(), false)) {
            uint32_t ownerGrant = remainingDesired & (READ_CONTROL | WRITE_DAC);
            grantedAccess |= ownerGrant;
            remainingDesired &= ~ownerGrant;
        }

        // 6. MAXIMUM_ALLOWED evaluation
        if (isMaximumAllowed) {
            g_SeTelemetry.totalMaximumAllowedEvaluations++;
            uint32_t maxAllowed = grantedAccess;

            if (secDesc.getOwner() && token.hasSid(*secDesc.getOwner(), false)) {
                maxAllowed |= (READ_CONTROL | WRITE_DAC);
            }
            if (token.hasPrivilege(SE_TAKE_OWNERSHIP_NAME, true)) {
                maxAllowed |= WRITE_OWNER;
            }
            if (token.hasPrivilege(SE_SECURITY_NAME, true)) {
                maxAllowed |= ACCESS_SYSTEM_SECURITY;
            }
            if (token.hasPrivilege(SE_BACKUP_NAME, true)) {
                maxAllowed |= (READ_CONTROL | FILE_READ_DATA);
            }
            if (token.hasPrivilege(SE_RESTORE_NAME, true)) {
                maxAllowed |= (WRITE_DAC | WRITE_OWNER | DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA);
            }

            if (!secDesc.hasDacl()) {
                maxAllowed |= (STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL);
            } else if (secDesc.getDacl() && secDesc.getDacl()->getAceCount() > 0) {
                uint32_t allowed = 0;
                uint32_t denied = 0;
                for (const auto& ace : secDesc.getDacl()->getAces()) {
                    uint32_t mask = ace.mask;
                    RtlMapGenericMask(mask, genericMapping);
                    if (ace.type == AceType::AccessDenied) {
                        if (token.hasSid(ace.sid, true)) {
                            denied |= mask;
                        }
                    } else if (ace.type == AceType::AccessAllowed) {
                        if (token.hasSid(ace.sid, false)) {
                            allowed |= (mask & ~denied);
                        }
                    }
                }
                maxAllowed |= allowed;
            }

            // Apply Mandatory Integrity restriction to maxAllowed
            if (secDesc.hasMandatoryLabel()) {
                uint32_t objIntegrity = secDesc.getIntegrityLevel();
                uint32_t subIntegrity = token.getIntegrityLevel();
                uint32_t policy = secDesc.getIntegrityPolicy();
                if (subIntegrity < objIntegrity) {
                    if (policy & SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) {
                        maxAllowed &= ~(WRITE_DAC | WRITE_OWNER | DELETE | FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE);
                    }
                    if (policy & SYSTEM_MANDATORY_LABEL_NO_READ_UP) {
                        maxAllowed &= ~(READ_CONTROL | FILE_READ_DATA | GENERIC_READ);
                    }
                    if (policy & SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP) {
                        maxAllowed &= ~(FILE_EXECUTE | GENERIC_EXECUTE);
                    }
                }
            }

            if ((remainingDesired & ~maxAllowed) != 0 || maxAllowed == 0) {
                g_SeTelemetry.totalAccessChecksDenied++;
                recordAudit(token.getUserSid(), objectName, desiredAccess, 0, false, secDesc);
                return NtStatus::AccessDenied;
            }

            grantedAccess = maxAllowed;
            g_SeTelemetry.totalAccessChecksGranted++;
            recordAudit(token.getUserSid(), objectName, desiredAccess, grantedAccess, true, secDesc);
            return NtStatus::Success;
        }

        // 7. Check if all desired rights already satisfied
        if (remainingDesired == 0) {
            g_SeTelemetry.totalAccessChecksGranted++;
            recordAudit(token.getUserSid(), objectName, desiredAccess, grantedAccess, true, secDesc);
            return NtStatus::Success;
        }

        // 8. Check DACL presence
        if (!secDesc.hasDacl()) {
            // Null DACL = Full access granted to everyone
            grantedAccess |= remainingDesired;
            g_SeTelemetry.totalAccessChecksGranted++;
            recordAudit(token.getUserSid(), objectName, desiredAccess, grantedAccess, true, secDesc);
            return NtStatus::Success;
        }

        const auto& daclOpt = secDesc.getDacl();
        if (!daclOpt || daclOpt->getAceCount() == 0) {
            // Empty DACL = Access denied to everyone
            g_SeTelemetry.totalAccessChecksDenied++;
            recordAudit(token.getUserSid(), objectName, desiredAccess, 0, false, secDesc);
            return NtStatus::AccessDenied;
        }

        // 9. Traverse DACL ACEs in canonical order
        for (const auto& ace : daclOpt->getAces()) {
            uint32_t aceMask = ace.mask;
            RtlMapGenericMask(aceMask, genericMapping);

            if (ace.type == AceType::AccessDenied) {
                // Deny ACE matches even if group is deny-only
                if (token.hasSid(ace.sid, true)) {
                    if ((remainingDesired & aceMask) != 0) {
                        grantedAccess = 0;
                        g_SeTelemetry.totalAccessChecksDenied++;
                        recordAudit(token.getUserSid(), objectName, desiredAccess, 0, false, secDesc);
                        return NtStatus::AccessDenied;
                    }
                }
            } else if (ace.type == AceType::AccessAllowed) {
                // Allow ACE matches ONLY IF group is NOT deny-only
                if (token.hasSid(ace.sid, false)) {
                    uint32_t matched = (remainingDesired & aceMask);
                    grantedAccess |= matched;
                    remainingDesired &= ~matched;

                    if (remainingDesired == 0) {
                        g_SeTelemetry.totalAccessChecksGranted++;
                        recordAudit(token.getUserSid(), objectName, desiredAccess, grantedAccess, true, secDesc);
                        return NtStatus::Success;
                    }
                }
            }
        }

        bool success = (remainingDesired == 0);
        if (success) {
            g_SeTelemetry.totalAccessChecksGranted++;
        } else {
            g_SeTelemetry.totalAccessChecksDenied++;
        }
        recordAudit(token.getUserSid(), objectName, desiredAccess, grantedAccess, success, secDesc);
        return success ? NtStatus::Success : NtStatus::AccessDenied;
    }

    static void recordAudit(
        const Sid& sid,
        std::wstring_view objName,
        uint32_t desired,
        uint32_t granted,
        bool success,
        const SecurityDescriptor& sd
    ) noexcept {
        if (!sd.hasSacl()) return;
        const auto& saclOpt = sd.getSacl();
        if (!saclOpt) return;

        for (const auto& ace : saclOpt->getAces()) {
            if (ace.type == AceType::SystemAudit) {
                bool logSuccess = (ace.flags & SUCCESSFUL_ACCESS_ACE_FLAG) && success;
                bool logFailure = (ace.flags & FAILED_ACCESS_ACE_FLAG) && !success;
                if (logSuccess || logFailure) {
                    s_AuditLog.push_back(SecurityAuditRecord{
                        .userSid = sid,
                        .objectName = std::wstring(objName),
                        .desiredAccess = desired,
                        .grantedAccess = granted,
                        .accessGranted = success
                    });
                    break;
                }
            }
        }
    }
};

/**
 * @brief Computes inherited security descriptor according to Windows NT inheritance rules (SeAssignSecurity).
 */
inline NtStatus SeAssignSecurity(
    const SecurityDescriptor* parentDescriptor,
    const SecurityDescriptor* explicitDescriptor,
    bool isDirectory,
    const TokenObject& subjectToken,
    const GenericMapping& genericMapping,
    SecurityDescriptor& newDescriptor
) {
    std::lock_guard<std::mutex> lk(g_SeTelemetryMutex);
    g_SeTelemetry.totalSecurityDescriptorsAssigned++;

    // 1. Assign Owner
    if (explicitDescriptor && explicitDescriptor->getOwner()) {
        newDescriptor.setOwner(*explicitDescriptor->getOwner());
    } else {
        newDescriptor.setOwner(subjectToken.getUserSid());
    }

    // 2. Assign Group
    if (explicitDescriptor && explicitDescriptor->getGroup()) {
        newDescriptor.setGroup(*explicitDescriptor->getGroup());
    } else if (!subjectToken.getGroupSids().empty()) {
        newDescriptor.setGroup(subjectToken.getGroupSids().front());
    } else {
        newDescriptor.setGroup(subjectToken.getUserSid());
    }

    // 3. Assign SACL & Mandatory Label
    if (explicitDescriptor && explicitDescriptor->hasSacl() && explicitDescriptor->getSacl()) {
        newDescriptor.setSacl(*explicitDescriptor->getSacl());
    } else if (parentDescriptor && parentDescriptor->hasSacl() && parentDescriptor->getSacl()) {
        Acl newSacl;
        for (const auto& ace : parentDescriptor->getSacl()->getAces()) {
            if (ace.type == AceType::SystemMandatoryLabel) {
                newSacl.addAce(ace);
            } else {
                bool shouldInherit = isDirectory ? (ace.flags & (CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE))
                                                 : (ace.flags & OBJECT_INHERIT_ACE);
                if (shouldInherit) {
                    Ace childAce = ace;
                    childAce.flags |= INHERITED_ACE;
                    if (ace.flags & NO_PROPAGATE_INHERIT_ACE) {
                        childAce.flags &= ~(CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE);
                    }
                    newSacl.addAce(std::move(childAce));
                    g_SeTelemetry.totalInheritedAcesPropagated++;
                }
            }
        }
        if (newSacl.getAceCount() > 0) {
            newDescriptor.setSacl(std::move(newSacl));
        }
    } else {
        newDescriptor.setIntegrityLevel(subjectToken.getIntegrityLevel());
    }

    // 4. Assign DACL
    Acl finalDacl;
    bool daclProtected = explicitDescriptor && (explicitDescriptor->getControl() & SE_DACL_PROTECTED);

    std::vector<Ace> explicitDeny;
    std::vector<Ace> explicitAllow;
    if (explicitDescriptor && explicitDescriptor->hasDacl() && explicitDescriptor->getDacl()) {
        for (const auto& ace : explicitDescriptor->getDacl()->getAces()) {
            Ace mappedAce = ace;
            RtlMapGenericMask(mappedAce.mask, genericMapping);
            if (mappedAce.type == AceType::AccessDenied) {
                explicitDeny.push_back(mappedAce);
            } else if (mappedAce.type == AceType::AccessAllowed) {
                explicitAllow.push_back(mappedAce);
            }
        }
    }

    std::vector<Ace> inheritedDeny;
    std::vector<Ace> inheritedAllow;
    if (!daclProtected && parentDescriptor && parentDescriptor->hasDacl() && parentDescriptor->getDacl()) {
        for (const auto& ace : parentDescriptor->getDacl()->getAces()) {
            bool shouldInherit = isDirectory ? (ace.flags & (CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE))
                                             : (ace.flags & OBJECT_INHERIT_ACE);
            if (shouldInherit) {
                Ace childAce = ace;
                childAce.flags |= INHERITED_ACE;
                if (ace.flags & NO_PROPAGATE_INHERIT_ACE) {
                    childAce.flags &= ~(CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE);
                }
                childAce.flags &= ~INHERIT_ONLY_ACE;
                RtlMapGenericMask(childAce.mask, genericMapping);

                if (childAce.type == AceType::AccessDenied) {
                    inheritedDeny.push_back(childAce);
                } else if (childAce.type == AceType::AccessAllowed) {
                    inheritedAllow.push_back(childAce);
                }
                g_SeTelemetry.totalInheritedAcesPropagated++;
            }
        }
    }

    // Canonical DACL Order: Explicit Deny -> Explicit Allow -> Inherited Deny -> Inherited Allow
    for (auto& a : explicitDeny) finalDacl.addAce(std::move(a));
    for (auto& a : explicitAllow) finalDacl.addAce(std::move(a));
    for (auto& a : inheritedDeny) finalDacl.addAce(std::move(a));
    for (auto& a : inheritedAllow) finalDacl.addAce(std::move(a));

    if (finalDacl.getAceCount() > 0 || (explicitDescriptor && explicitDescriptor->hasDacl())) {
        newDescriptor.setDacl(std::move(finalDacl));
        uint16_t ctrl = newDescriptor.getControl() | SE_DACL_PRESENT;
        if (!inheritedDeny.empty() || !inheritedAllow.empty()) {
            ctrl |= SE_DACL_AUTO_INHERITED;
        }
        newDescriptor.setControl(ctrl);
    }

    return NtStatus::Success;
}

/**
 * @brief Validates a security descriptor's structural integrity.
 */
inline bool RtlValidSecurityDescriptor(const SecurityDescriptor& sd) noexcept {
    if (sd.hasDacl() && sd.getDacl().has_value()) {
        const auto& dacl = *sd.getDacl();
        for (const auto& ace : dacl.getAces()) {
            if (ace.sid.getRevision() == 0) return false;
        }
    }
    return true;
}

} // namespace micant::se
