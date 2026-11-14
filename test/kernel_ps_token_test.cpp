#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/core/ke.hpp"

namespace {
micant::se::SidValue makeLogonTestSid(uint32_t first, uint32_t second) noexcept {
    micant::se::SidValue sid{};
    sid.SubAuthorityCount = 2;
    sid.IdentifierAuthority[5] = 5;
    sid.SubAuthority[0] = first;
    sid.SubAuthority[1] = second;
    return sid;
}

micant::se::FreestandingLogonTokenRequest makeLogonTestRequest() noexcept {
    micant::se::FreestandingLogonTokenRequest request{};
    request.UserSid = makeLogonTestSid(21, 1002);
    request.PrimaryGroup = makeLogonTestSid(32, 545);
    request.Groups[0] = {request.PrimaryGroup, 0x00000004};
    request.GroupCount = 1;
    request.IntegrityLevel = 0x2000;
    return request;
}
}

extern "C" bool MicantTestPsTokenAssignment() noexcept {
    using namespace micant;
    using namespace micant::ps;
    if (!ke::KeGetCurrentThread() || ke::KeGetCurrentThread()->previousUserMode) return false;

    EProcess* system{};
    ob::ObjectHeader* systemReference{};
    EProcess* target{};
    ob::ObjectHeader* targetReference{};
    EProcess* terminatingTarget{};
    ob::ObjectHeader* terminatingReference{};
    se::FreestandingAccessToken* issued{};
    ob::ObjectHeader* issuedReference{};
    auto status = detail::referenceSystemProcess(system, systemReference);
    bool valid = status == NtStatus::Success && system && systemReference;
    if (valid) status = detail::createProcessObject(target, targetReference);
    valid = valid && status == NtStatus::Success && target && targetReference;
    if (valid) status = detail::createProcessObject(terminatingTarget, terminatingReference);
    valid = valid && status == NtStatus::Success && terminatingTarget && terminatingReference;

    bool attachedSystem = false;
    ke::KAPC_STATE systemAttach{};
    if (valid) {
        ke::KeStackAttachProcess(system, &systemAttach);
        attachedSystem = ke::KeGetCurrentProcess() == system;
        valid = attachedSystem;
    }
    if (valid) valid = PsCreateFreestandingLogonToken(makeLogonTestRequest(), issued,
        issuedReference) == NtStatus::Success && issued && issuedReference;

    // The user target has no assignment privilege. The call must consult the
    // actual attached process token rather than trust the kernel caller mode.
    bool attachedTarget = false;
    ke::KAPC_STATE targetAttach{};
    if (valid) {
        ke::KeStackAttachProcess(target, &targetAttach);
        attachedTarget = ke::KeGetCurrentProcess() == target;
        valid = attachedTarget &&
            PsAssignFreestandingPrimaryToken(*target, targetReference, *issued,
                issuedReference) == NtStatus::PrivilegeNotHeld;
    }
    if (attachedTarget) {
        ke::KeUnstackDetachProcess(&targetAttach);
        attachedTarget = false;
    }

    if (valid) valid = PsAssignFreestandingPrimaryToken(*system, systemReference,
        *issued, issuedReference) == NtStatus::AccessDenied;
    if (valid) valid = terminatingTarget->terminateOther(NtStatus::Cancelled) == NtStatus::Success &&
        terminatingTarget->isTerminating() &&
        PsAssignFreestandingPrimaryToken(*terminatingTarget, terminatingReference,
            *issued, issuedReference) == NtStatus::ProcessIsTerminating;
    if (valid) valid = PsAssignFreestandingPrimaryToken(*target, targetReference,
        *issued, issuedReference) == NtStatus::Success;

    // Drop the creator's reference and prove the EProcess-owned reference
    // keeps the assigned token alive until the target is torn down.
    if (issuedReference) {
        se::SeDereferenceFreestandingToken(issuedReference);
        issuedReference = nullptr;
        issued = nullptr;
    }
    se::FreestandingAccessToken* assigned{};
    ob::ObjectHeader* assignedReference{};
    if (valid) valid = target->referencePrimaryToken(assigned, assignedReference) ==
        NtStatus::Success && assigned && assignedReference &&
        assigned->AuthenticationId != 0 && assigned->SessionId == 1 &&
        assigned->UserSid.equals(makeLogonTestRequest().UserSid);
    if (assignedReference) se::SeDereferenceFreestandingToken(assignedReference);

    if (attachedSystem) ke::KeUnstackDetachProcess(&systemAttach);
    if (terminatingReference) ob::ObDereferenceObject(terminatingReference);
    if (targetReference) ob::ObDereferenceObject(targetReference);
    if (systemReference) ob::ObDereferenceObject(systemReference);
    return valid;
}
