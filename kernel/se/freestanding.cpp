#include "micant/km/executive/se.hpp"
#include "micant/km/executive/probe.hpp"
#include <cstring>

namespace micant::se {
namespace {
ob::ObjectType g_FreestandingTokenType{};
std::atomic<uint64_t> g_NextInteractiveAuthenticationId{1};

void deleteFreestandingToken(ob::ObjectHeader* header) {
    header->as<FreestandingAccessToken>()->~FreestandingAccessToken();
}

SidValue makeSid(uint32_t authority, uint8_t count,
                uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) noexcept {
    SidValue sid{};
    sid.SubAuthorityCount = count;
    sid.IdentifierAuthority[5] = static_cast<uint8_t>(authority);
    sid.IdentifierAuthority[4] = static_cast<uint8_t>(authority >> 8);
    sid.IdentifierAuthority[3] = static_cast<uint8_t>(authority >> 16);
    sid.IdentifierAuthority[2] = static_cast<uint8_t>(authority >> 24);
    if (count > 0) sid.SubAuthority[0] = a;
    if (count > 1) sid.SubAuthority[1] = b;
    if (count > 2) sid.SubAuthority[2] = c;
    return sid;
}

bool isSystemSid(const SidValue& sid) noexcept {
    return sid.Revision == 1 && sid.SubAuthorityCount == 1 &&
        sid.IdentifierAuthority[0] == 0 && sid.IdentifierAuthority[1] == 0 &&
        sid.IdentifierAuthority[2] == 0 && sid.IdentifierAuthority[3] == 0 &&
        sid.IdentifierAuthority[4] == 0 && sid.IdentifierAuthority[5] == 5 &&
        sid.SubAuthority[0] == 18;
}

bool validSidValue(const SidValue& sid) noexcept;

bool isValidInteractiveLogonRequest(const FreestandingLogonTokenRequest& request) noexcept {
    if (!validSidValue(request.UserSid) || isSystemSid(request.UserSid) ||
        !validSidValue(request.PrimaryGroup) || request.GroupCount == 0 ||
        request.GroupCount > request.Groups.size() ||
        (request.IntegrityLevel != 0x1000 && request.IntegrityLevel != 0x2000))
        return false;

    // The first interactive-token slice allows only ordinary local-user
    // privileges. Authority-bearing privileges remain confined to bootstrap
    // system tokens until token assignment and elevation policies are wired.
    constexpr uint64_t allowedPrivileges = (uint64_t{1} << 17) | // SeBackupPrivilege
        (uint64_t{1} << 18) | // SeRestorePrivilege
        (uint64_t{1} << 19) | // SeShutdownPrivilege
        (uint64_t{1} << 20) | // SeDebugPrivilege (LSA policy-controlled)
        (uint64_t{1} << 23);  // SeChangeNotifyPrivilege
    if (request.EnabledPrivileges & ~allowedPrivileges) return false;

    constexpr uint32_t allowedGroupAttributes = 0x00000001 | 0x00000002 |
        0x00000004 | 0x00000008 | 0x00000010 | 0x00000020;
    bool primaryGroupPresent = false;
    for (size_t i = 0; i < request.GroupCount; ++i) {
        const auto& group = request.Groups[i];
        if (!validSidValue(group.Sid) || isSystemSid(group.Sid) ||
            (group.Attributes & ~allowedGroupAttributes) != 0 ||
            ((group.Attributes & 0x00000010) &&
             (group.Attributes & (0x00000002 | 0x00000004 | 0x00000008)))) return false;
        if (group.Sid.equals(request.UserSid)) return false;
        for (size_t prior = 0; prior < i; ++prior)
            if (request.Groups[prior].Sid.equals(group.Sid)) return false;
        if (group.Sid.equals(request.PrimaryGroup) &&
            (group.Attributes & 0x00000004) && !(group.Attributes & 0x00000010))
            primaryGroupPresent = true;
    }
    return primaryGroupPresent;
}

bool validSidValue(const SidValue& sid) noexcept {
    return sid.Revision == 1 && sid.SubAuthorityCount <= std::size(sid.SubAuthority);
}

bool validAccessInputs(const FreestandingAccessToken& token,
                       const FreestandingSecurityDescriptor& descriptor) noexcept {
    if (token.GroupCount > token.Groups.size() || descriptor.AceCount > descriptor.Dacl.size() ||
        !validSidValue(token.UserSid) ||
        (descriptor.OwnerPresent && !validSidValue(descriptor.Owner)) ||
        (descriptor.GroupPresent && !validSidValue(descriptor.Group))) return false;
    for (size_t i = 0; i < token.GroupCount; ++i)
        if (!validSidValue(token.Groups[i].Sid)) return false;
    for (size_t i = 0; i < descriptor.AceCount; ++i) {
        const auto& ace = descriptor.Dacl[i];
        if ((ace.Type != FreestandingAceType::Allow && ace.Type != FreestandingAceType::Deny) ||
            (ace.Flags & ~uint8_t{0x1f}) || !validSidValue(ace.Sid)) return false;
    }
    return true;
}

void addGroup(FreestandingAccessToken& token, const SidValue& sid) noexcept {
    if (token.GroupCount >= token.Groups.size()) __builtin_trap();
    token.Groups[token.GroupCount++] = TokenGroupValue{sid, 0x00000004}; // SE_GROUP_ENABLED
}

void initializeTokenType() noexcept {
    // Token creation is callable by preemptible threads; do not publish a
    // type whose access mapping or delete callback is only partly installed.
    ob::detail::HandleTableGuard guard;
    if (g_FreestandingTokenType.typeId == ob::ObjectTypeId::Token) return;
    g_FreestandingTokenType.validAccessMask = 0x000F01FF;
    g_FreestandingTokenType.genericMapping = {
        ob::READ_CONTROL | 0x0008,
        ob::READ_CONTROL | 0x0002 | 0x0001 | 0x0020 | 0x0040,
        ob::READ_CONTROL,
        ob::STANDARD_RIGHTS_REQUIRED | 0x01FF
    };
    g_FreestandingTokenType.deleteProcedure = deleteFreestandingToken;
    g_FreestandingTokenType.typeId = ob::ObjectTypeId::Token;
}
}

bool SidValue::equals(const SidValue& other) const noexcept {
    if (Revision != other.Revision || SubAuthorityCount != other.SubAuthorityCount ||
        SubAuthorityCount > std::size(SubAuthority)) return false;
    for (size_t i = 0; i < sizeof(IdentifierAuthority); ++i)
        if (IdentifierAuthority[i] != other.IdentifierAuthority[i]) return false;
    for (size_t i = 0; i < SubAuthorityCount; ++i)
        if (SubAuthority[i] != other.SubAuthority[i]) return false;
    return true;
}

bool FreestandingAccessToken::hasEnabledGroup(const SidValue& sid) const noexcept {
    for (size_t i = 0; i < GroupCount && i < Groups.size(); ++i)
        if ((Groups[i].Attributes & 0x00000004) && !(Groups[i].Attributes & 0x00000010) &&
            Groups[i].Sid.equals(sid)) return true;
    return false;
}

bool FreestandingAccessToken::matchesSid(const SidValue& sid, bool includeDenyOnly) const noexcept {
    if (UserSid.equals(sid)) return true;
    for (size_t i = 0; i < GroupCount && i < Groups.size(); ++i) {
        const uint32_t attributes = Groups[i].Attributes;
        if (!Groups[i].Sid.equals(sid)) continue;
        if (attributes & 0x00000010) {
            if (includeDenyOnly) return true;
        } else if (attributes & 0x00000004) return true;
    }
    return false;
}

NtStatus SeCreateFreestandingPrimaryToken(bool system,
    FreestandingAccessToken*& token, ob::ObjectHeader*& reference) noexcept {
    token = nullptr;
    reference = nullptr;
    initializeTokenType();
    void* body{};
    ob::ObjectHeader* header{};
    const auto status = ob::ObCreateObject(ob::KernelMode, &g_FreestandingTokenType,
        nullptr, sizeof(FreestandingAccessToken), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) FreestandingAccessToken{};
    created->IsSystem = system;
    created->IntegrityLevel = system ? 0x4000 : 0x1000;
    if (system) {
        created->UserSid = makeSid(5, 1, 18); // NT AUTHORITY\SYSTEM
        addGroup(*created, makeSid(5, 2, 32, 544)); // BUILTIN\Administrators
        created->Groups[0].Attributes |= 0x00000008; // SE_GROUP_OWNER
        addGroup(*created, makeSid(5, 2, 32, 556)); // Network Configuration Operators
        addGroup(*created, makeSid(5, 1, 11)); // Authenticated Users
        addGroup(*created, makeSid(1, 1, 0)); // Everyone
        // SeCreateTokenPrivilege, SeAssignPrimaryTokenPrivilege and
        // SeTcbPrivilege, using their
        // documented LUID low parts as the bounded bitmap index.
        created->EnabledPrivileges |= (uint64_t{1} << 2) | (uint64_t{1} << 3) | (uint64_t{1} << 7);
        created->EnabledPrivileges |= uint64_t{1} << 20; // SeDebugPrivilege
        created->EnabledPrivileges |= uint64_t{1} << 29; // SeImpersonatePrivilege
    } else {
        created->UserSid = makeSid(5, 1, 7); // NT AUTHORITY\ANONYMOUS LOGON
        addGroup(*created, makeSid(1, 1, 0)); // Everyone
    }
    created->DefaultOwner = system ? makeSid(5, 2, 32, 544) : created->UserSid;
    created->PrimaryGroup = created->UserSid;
    // Default ACLs follow the two bootstrap token kinds. Their entries are
    // kept as generic masks until the destination object's mapping is known.
    auto& defaultDacl = created->DefaultDacl;
    defaultDacl.Owner = created->DefaultOwner;
    defaultDacl.OwnerPresent = true;
    defaultDacl.Group = created->PrimaryGroup;
    defaultDacl.GroupPresent = true;
    if (system) {
        defaultDacl.Dacl[0] = {makeSid(5, 1, 18), ob::GENERIC_ALL, FreestandingAceType::Allow};
        defaultDacl.Dacl[1] = {makeSid(5, 2, 32, 544),
            ob::GENERIC_READ | ob::GENERIC_EXECUTE | ob::READ_CONTROL, FreestandingAceType::Allow};
    } else {
        defaultDacl.Dacl[0] = {makeSid(1, 1, 0), ob::GENERIC_ALL, FreestandingAceType::Allow};
        defaultDacl.Dacl[1] = {makeSid(5, 1, 7), ob::GENERIC_ALL, FreestandingAceType::Allow};
    }
    defaultDacl.AceCount = 2;
    token = created;
    reference = header;
    return NtStatus::Success;
}

NtStatus SeCreateFreestandingLogonToken(const FreestandingAccessToken* issuer,
    ob::ObjectHeader* issuerReference, const FreestandingLogonTokenRequest& request,
    FreestandingAccessToken*& token, ob::ObjectHeader*& reference) noexcept {
    token = nullptr;
    reference = nullptr;
    if (!SeIsFreestandingTokenObject(issuer, issuerReference) || !issuer->IsPrimary ||
        !issuer->IsSystem || !SeTokenHasPrivilegeFreestanding(issuer, 2) ||
        !SeTokenHasPrivilegeFreestanding(issuer, 7)) return NtStatus::PrivilegeNotHeld;
    if (!isValidInteractiveLogonRequest(request)) return NtStatus::InvalidParameter;

    initializeTokenType();
    void* body{};
    ob::ObjectHeader* header{};
    const auto status = ob::ObCreateObject(ob::KernelMode, &g_FreestandingTokenType,
        nullptr, sizeof(FreestandingAccessToken), &body, &header);
    if (status != NtStatus::Success) return status;

    auto* created = new (body) FreestandingAccessToken{};
    // Authentication IDs are unique for this boot. Session 1 is the only
    // supported interactive console session in this initial contract.
    auto nextAuthenticationId = g_NextInteractiveAuthenticationId.load(std::memory_order_relaxed);
    while (nextAuthenticationId != 0 && nextAuthenticationId != UINT64_MAX &&
           !g_NextInteractiveAuthenticationId.compare_exchange_weak(
               nextAuthenticationId, nextAuthenticationId + 1,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
    if (nextAuthenticationId == 0 || nextAuthenticationId == UINT64_MAX) {
        ob::ObDereferenceObject(header);
        return NtStatus::InvalidParameter;
    }
    created->AuthenticationId = nextAuthenticationId;
    created->SessionId = 1;
    created->UserSid = request.UserSid;
    created->DefaultOwner = request.UserSid;
    created->PrimaryGroup = request.PrimaryGroup;
    created->GroupCount = request.GroupCount;
    for (size_t i = 0; i < request.GroupCount; ++i) created->Groups[i] = request.Groups[i];
    created->IntegrityLevel = request.IntegrityLevel;
    created->EnabledPrivileges = request.EnabledPrivileges;
    created->IsPrimary = true;
    created->IsSystem = false;

    auto& dacl = created->DefaultDacl;
    dacl.Owner = created->DefaultOwner;
    dacl.OwnerPresent = true;
    dacl.Group = created->PrimaryGroup;
    dacl.GroupPresent = true;
    dacl.DaclPresent = true;
    dacl.DaclNull = false;
    dacl.Dacl[0] = {created->UserSid, ob::GENERIC_ALL, FreestandingAceType::Allow};
    dacl.Dacl[1] = {created->PrimaryGroup,
        ob::GENERIC_READ | ob::GENERIC_EXECUTE | ob::READ_CONTROL, FreestandingAceType::Allow};
    dacl.AceCount = 2;
    token = created;
    reference = header;
    return NtStatus::Success;
}

NtStatus SeCreateFreestandingImpersonationToken(const FreestandingAccessToken& source,
    FreestandingAccessToken*& token, ob::ObjectHeader*& reference) noexcept {
    token = nullptr;
    reference = nullptr;
    if (source.GroupCount > source.Groups.size()) return NtStatus::InvalidParameter;
    initializeTokenType();
    void* body{};
    ob::ObjectHeader* header{};
    const auto status = ob::ObCreateObject(ob::KernelMode, &g_FreestandingTokenType,
        nullptr, sizeof(FreestandingAccessToken), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) FreestandingAccessToken(source);
    created->IsPrimary = false;
    token = created;
    reference = header;
    return NtStatus::Success;
}

NtStatus SeDuplicateFreestandingPrimaryToken(const FreestandingAccessToken& source,
    FreestandingAccessToken*& token, ob::ObjectHeader*& reference) noexcept {
    token = nullptr;
    reference = nullptr;
    if (!source.IsPrimary || source.GroupCount > source.Groups.size())
        return NtStatus::InvalidParameter;
    initializeTokenType();
    void* body{};
    ob::ObjectHeader* header{};
    const auto status = ob::ObCreateObject(ob::KernelMode, &g_FreestandingTokenType,
        nullptr, sizeof(FreestandingAccessToken), &body, &header);
    if (status != NtStatus::Success) return status;
    auto* created = new (body) FreestandingAccessToken(source);
    created->IsPrimary = true;
    token = created;
    reference = header;
    return NtStatus::Success;
}

bool SeIsFreestandingTokenObject(const FreestandingAccessToken* token,
    ob::ObjectHeader* reference) noexcept {
    return token && reference && reference->type &&
        reference->type->typeId == ob::ObjectTypeId::Token &&
        reference->getBody() == token;
}

bool SeTokenHasPrivilegeFreestanding(const FreestandingAccessToken* token,
    uint32_t privilegeLuidLow) noexcept {
    return token && privilegeLuidLow < 64 &&
        (token->EnabledPrivileges & (uint64_t{1} << privilegeLuidLow)) != 0;
}

bool SeCanImpersonateFreestanding(const FreestandingAccessToken* caller,
    const FreestandingAccessToken* target) noexcept {
    if (!caller || !target || target->IsPrimary) return false;
    return SeTokenHasPrivilegeFreestanding(caller, 29) || caller->UserSid.equals(target->UserSid);
}

void SeDereferenceFreestandingToken(ob::ObjectHeader* reference) noexcept {
    if (reference) ob::ObDereferenceObject(reference);
}

const FreestandingAccessToken* SeGetEffectiveFreestandingToken(
    const FreestandingAccessToken* primary,
    const FreestandingAccessToken* impersonation) noexcept {
    if (impersonation && !impersonation->IsPrimary) return impersonation;
    return primary;
}

bool SeAccessCheckFreestanding(const FreestandingAccessToken* token,
    const FreestandingSecurityDescriptor& descriptor, uint32_t desiredAccess,
    uint32_t& grantedAccess) noexcept {
    grantedAccess = 0;
    if (!token || !validAccessInputs(*token, descriptor)) return false;
    if (!descriptor.DaclPresent || descriptor.DaclNull) {
        grantedAccess = desiredAccess;
        return true;
    }
    uint32_t remaining = desiredAccess;
    if (descriptor.OwnerPresent && token->matchesSid(descriptor.Owner, false)) {
        const uint32_t ownerRights = ob::READ_CONTROL | ob::WRITE_DAC;
        grantedAccess |= desiredAccess & ownerRights;
        remaining &= ~ownerRights;
    }
    for (size_t i = 0; i < descriptor.AceCount && remaining; ++i) {
        const auto& ace = descriptor.Dacl[i];
        if (ace.Flags & 0x08) continue; // INHERIT_ONLY_ACE
        const bool matched = token->matchesSid(ace.Sid, ace.Type == FreestandingAceType::Deny);
        if (!matched) continue;
        const uint32_t relevant = ace.AccessMask & remaining;
        if (!relevant) continue;
        if (ace.Type == FreestandingAceType::Deny) return false;
        remaining &= ~relevant;
        grantedAccess |= relevant;
    }
    return remaining == 0;
}

bool SeMaximumAccessFreestanding(const FreestandingAccessToken* token,
    const FreestandingSecurityDescriptor& descriptor, uint32_t validAccess,
    uint32_t& grantedAccess) noexcept {
    grantedAccess = 0;
    if (!token || !validAccessInputs(*token, descriptor)) return false;
    if (!descriptor.DaclPresent || descriptor.DaclNull) {
        grantedAccess = validAccess;
        return true;
    }
    uint32_t remaining = validAccess;
    if (descriptor.OwnerPresent && token->matchesSid(descriptor.Owner, false)) {
        const auto ownerRights = remaining & (ob::READ_CONTROL | ob::WRITE_DAC);
        grantedAccess |= ownerRights;
        remaining &= ~ownerRights;
    }
    for (size_t i = 0; i < descriptor.AceCount && remaining; ++i) {
        const auto& ace = descriptor.Dacl[i];
        if ((ace.Flags & 0x08) || !token->matchesSid(ace.Sid, ace.Type == FreestandingAceType::Deny))
            continue;
        const uint32_t relevant = ace.AccessMask & remaining;
        remaining &= ~relevant;
        if (ace.Type == FreestandingAceType::Allow) grantedAccess |= relevant;
    }
    return true;
}

NtStatus SeCaptureFreestandingSecurityDescriptor(const void* descriptor,
    ob::KPROCESSOR_MODE sourceMode, FreestandingSecurityDescriptor& captured) noexcept {
    captured = {};
    if (!descriptor || (sourceMode != ob::KernelMode && sourceMode != ob::UserMode))
        return NtStatus::InvalidParameter;
    const auto copy = [sourceMode](void* destination, uintptr_t source, size_t length) noexcept {
        uint64_t flags{};
        __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
        const auto status = ex::ExSafeCopyFromUser(destination, reinterpret_cast<const void*>(source), length, sourceMode);
        if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
        return status;
    };
    struct Header { uint8_t revision, reserved; uint16_t control; } header{};
    const uintptr_t base = reinterpret_cast<uintptr_t>(descriptor);
    if (base & 3) return NtStatus::DatatypeMisalignment;
    auto status = copy(&header, base, sizeof(header));
    if (status != NtStatus::Success) return status;
    constexpr uint16_t SelfRelative = 0x8000, DaclPresent = 0x0004, SaclPresent = 0x0010;
    if (header.revision != 1) return NtStatus::InvalidSecurityDescriptor;
    if (header.control & SaclPresent) return NtStatus::NotSupported;
    uintptr_t owner{}, group{}, dacl{};
    if (header.control & SelfRelative) {
        struct Relative { Header header; uint32_t owner, group, sacl, dacl; } relative{};
        static_assert(sizeof(Relative) == 20);
        status = copy(&relative, base, sizeof(relative));
        if (status != NtStatus::Success) return status;
        if (relative.header.revision != header.revision || relative.header.control != header.control)
            return NtStatus::InvalidSecurityDescriptor;
        const auto resolve = [base](uint32_t offset, uintptr_t& value) noexcept {
            if (!offset) { value = 0; return true; }
            if (offset < 20 || offset > 65535 || (offset & 3) || base > UINTPTR_MAX - offset) return false;
            value = base + offset;
            return true;
        };
        if (!resolve(relative.owner, owner) || !resolve(relative.group, group) ||
            ((header.control & DaclPresent) && !resolve(relative.dacl, dacl)))
            return NtStatus::InvalidSecurityDescriptor;
    } else {
        struct Absolute { Header header; uint32_t padding; uintptr_t owner, group, sacl, dacl; } absolute{};
        static_assert(sizeof(Absolute) == 40);
        if (base & 7) return NtStatus::DatatypeMisalignment;
        status = copy(&absolute, base, sizeof(absolute));
        if (status != NtStatus::Success) return status;
        if (absolute.header.revision != header.revision || absolute.header.control != header.control)
            return NtStatus::InvalidSecurityDescriptor;
        owner = absolute.owner; group = absolute.group;
        if (header.control & DaclPresent) dacl = absolute.dacl;
    }
    const auto captureSid = [&](uintptr_t address, size_t available, SidValue& sid) noexcept -> NtStatus {
        struct SidHeader { uint8_t revision, count, authority[6]; } sidHeader{};
        if (!address || (address & 3)) return NtStatus::InvalidSid;
        auto result = copy(&sidHeader, address, sizeof(sidHeader));
        if (result != NtStatus::Success) return result;
        if (sidHeader.revision != 1) return NtStatus::InvalidSid;
        if (sidHeader.count > std::size(sid.SubAuthority)) return NtStatus::NotSupported;
        const size_t bytes = sizeof(sidHeader) + size_t{sidHeader.count} * sizeof(uint32_t);
        if (bytes > available || address > UINTPTR_MAX - bytes) return NtStatus::InvalidSid;
        sid = {};
        sid.Revision = sidHeader.revision;
        sid.SubAuthorityCount = sidHeader.count;
        std::memcpy(sid.IdentifierAuthority, sidHeader.authority, sizeof(sid.IdentifierAuthority));
        if (sidHeader.count)
            return copy(sid.SubAuthority, address + sizeof(sidHeader), size_t{sidHeader.count} * sizeof(uint32_t));
        return NtStatus::Success;
    };
    if (owner) {
        status = captureSid(owner, SIZE_MAX, captured.Owner);
        if (status != NtStatus::Success) return status;
        captured.OwnerPresent = true;
    }
    if (group) {
        status = captureSid(group, SIZE_MAX, captured.Group);
        if (status != NtStatus::Success) return status;
        captured.GroupPresent = true;
    }
    captured.DaclPresent = (header.control & DaclPresent) != 0;
    captured.DaclNull = captured.DaclPresent && !dacl;
    if (!captured.DaclPresent || captured.DaclNull) return NtStatus::Success;
    if (dacl & 3) return NtStatus::InvalidAcl;
    struct AclHeader { uint8_t revision, reserved; uint16_t size, count, reserved2; } acl{};
    status = copy(&acl, dacl, sizeof(acl));
    if (status != NtStatus::Success) return status;
    if ((acl.revision != 2 && acl.revision != 4) || acl.size < sizeof(acl) || (acl.size & 3) ||
        dacl > UINTPTR_MAX - acl.size) return NtStatus::InvalidAcl;
    if (acl.count > captured.Dacl.size()) return NtStatus::NotSupported;
    size_t offset = sizeof(acl);
    for (size_t i = 0; i < acl.count; ++i) {
        struct AceHeader { uint8_t type, flags; uint16_t size; uint32_t mask; } ace{};
        if (offset > acl.size || sizeof(ace) > acl.size - offset) return NtStatus::InvalidAcl;
        status = copy(&ace, dacl + offset, sizeof(ace));
        if (status != NtStatus::Success) return status;
        if (ace.size < sizeof(ace) + 8 || (ace.size & 3) || ace.size > acl.size - offset)
            return NtStatus::InvalidAcl;
        if (ace.type > 1 || (ace.flags & ~uint8_t{0x1f})) return NtStatus::NotSupported;
        auto& result = captured.Dacl[captured.AceCount];
        result.Type = ace.type == 0 ? FreestandingAceType::Allow : FreestandingAceType::Deny;
        result.Flags = ace.flags;
        result.AccessMask = ace.mask;
        status = captureSid(dacl + offset + sizeof(ace), ace.size - sizeof(ace), result.Sid);
        if (status != NtStatus::Success) return status;
        // CREATOR_OWNER/GROUP and OWNER_RIGHTS require substitution or
        // special owner-rights processing; do not silently treat them as
        // ordinary SIDs in this bounded access-check implementation.
        if (result.Sid.equals(makeSid(3, 1, 0)) || result.Sid.equals(makeSid(3, 1, 1)) ||
            result.Sid.equals(makeSid(3, 1, 4))) return NtStatus::NotSupported;
        ++captured.AceCount;
        offset += ace.size;
    }
    return NtStatus::Success;
}

void SeInitializeNetworkConfigurationAcl(FreestandingSecurityDescriptor& descriptor) noexcept {
    descriptor = {};
    descriptor.Owner = makeSid(5, 1, 18); // NT AUTHORITY\SYSTEM
    descriptor.OwnerPresent = true;
    descriptor.Dacl[0] = {makeSid(5, 1, 18), SE_NETWORK_CONFIG_WRITE, FreestandingAceType::Allow};
    descriptor.Dacl[1] = {makeSid(5, 2, 32, 544), SE_NETWORK_CONFIG_WRITE, FreestandingAceType::Allow};
    descriptor.Dacl[2] = {makeSid(5, 2, 32, 556), SE_NETWORK_CONFIG_WRITE, FreestandingAceType::Allow};
    descriptor.AceCount = 3;
}

void SeInitializeSystemRootStoreAcl(FreestandingSecurityDescriptor& descriptor) noexcept {
    descriptor = {};
    descriptor.Owner = makeSid(5, 1, 18); // NT AUTHORITY\SYSTEM
    descriptor.OwnerPresent = true;
    descriptor.Dacl[0] = {makeSid(5, 1, 18), 0x00000003, FreestandingAceType::Allow};
    descriptor.Dacl[1] = {makeSid(5, 2, 32, 544), 0x00000003, FreestandingAceType::Allow};
    descriptor.Dacl[2] = {makeSid(5, 1, 11), 0x00000001, FreestandingAceType::Allow};
    descriptor.Dacl[3] = {makeSid(1, 1, 0), 0x00000001, FreestandingAceType::Allow};
    descriptor.AceCount = 4;
}
}
