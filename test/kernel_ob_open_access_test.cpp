#include "micant/km/executive/ob.hpp"

namespace {
using namespace micant;
constexpr uint32_t kRead = 0x1;
constexpr uint32_t kWrite = 0x2;
constexpr uint32_t kValid = kRead | kWrite;
int g_descriptorToken{};
bool g_deny{};
bool g_failOpen{};
bool g_malformedGrant{};
uint32_t g_lastGranted{};
uint32_t g_failureStage{};
uint32_t g_closeCount{};

NtStatus checkAccess(void* descriptor, uint32_t desired, uint32_t* granted) noexcept {
    if (descriptor != &g_descriptorToken || !granted) return NtStatus::InvalidParameter;
    if (g_deny || (desired & ~(kRead | ob::MAXIMUM_ALLOWED))) return NtStatus::AccessDenied;
    *granted = g_malformedGrant ? 0x80000000u : (desired & ob::MAXIMUM_ALLOWED ? kRead : desired);
    return NtStatus::Success;
}

NtStatus onOpen(ob::ObjectHeader*, uint32_t access) noexcept {
    g_lastGranted = access;
    return g_failOpen ? NtStatus::Unsuccessful : NtStatus::Success;
}
void onClose(ob::ObjectHeader*, Handle) noexcept { ++g_closeCount; }
}

extern "C" bool MicantTestObOpenAccess() noexcept {
    using namespace micant;
    ob::ObjectHeader zeroReference{};
    zeroReference.pointerCount.store(0, std::memory_order_relaxed);
    if (zeroReference.tryAddReference() || zeroReference.pointerCount.load(std::memory_order_relaxed) != 0)
        return false;
    ob::ObjectType type{};
    type.validAccessMask = kValid;
    type.genericMapping.genericRead = kRead;
    type.genericMapping.genericWrite = kWrite;
    type.genericMapping.genericExecute = kRead;
    type.genericMapping.genericAll = kValid;
    type.openProcedure = onOpen;
    type.closeProcedure = onClose;
    ob::ObjectHeader object{};
    object.type = &type;
    object.securityDescriptor = &g_descriptorToken;
    // Freestanding tables have a fixed 4096-entry backing store. Keep test
    // fixtures out of the bootstrap stack so this access test does not change
    // the runtime self-test's PFN baseline through stack growth.
    static ob::HandleTable table;
    ob::ObSetAccessCheckProcedure(checkAccess);
    g_failureStage = 1;

    Handle handle{};
    bool ok = ob::ObOpenObjectByPointer(&object, 0, ob::GENERIC_READ, &type, table, handle) == NtStatus::Success &&
              handle && g_lastGranted == kRead && object.handleCount == 1;
    ob::ObjectHeader* reference{};
    if (ok) ok = ob::ObReferenceObjectByHandle(handle, table, kRead, &type, &reference) == NtStatus::Success;
    if (reference) ob::ObDereferenceObject(reference);
    reference = nullptr;
    if (ok) ok = ob::ObReferenceObjectByHandle(handle, table, ob::GENERIC_READ, &type, &reference) ==
        NtStatus::Success;
    if (reference) ob::ObDereferenceObject(reference);
    reference = nullptr;
    if (ok) ok = ob::ObReferenceObjectByHandle(handle, table, ob::GENERIC_WRITE, &type, &reference) ==
        NtStatus::AccessDenied && !reference;
    Handle callbackDuplicate{};
    if (ok) ok = table.duplicateHandle(handle, table, callbackDuplicate, kRead) == NtStatus::Success &&
        callbackDuplicate && g_lastGranted == kRead;
    if (callbackDuplicate) (void)table.closeHandle(callbackDuplicate);
    const uint32_t priorDuplicateCloseCount = g_closeCount;
    g_failOpen = true;
    Handle rejectedDuplicate = 0xdead;
    if (ok) ok = table.duplicateHandle(handle, table, rejectedDuplicate, kRead) == NtStatus::Unsuccessful &&
        rejectedDuplicate == 0 && table.getActiveHandleCount() == 1 && object.handleCount == 1 &&
        g_closeCount == priorDuplicateCloseCount + 1;
    g_failOpen = false;
    Handle maximumDuplicate{};
    if (ok) ok = table.duplicateHandle(handle, table, maximumDuplicate, ob::MAXIMUM_ALLOWED) ==
        NtStatus::Success && maximumDuplicate;
    uint32_t duplicateGranted{};
    if (ok) ok = table.lookup(maximumDuplicate, &duplicateGranted) != nullptr && duplicateGranted == kRead;
    if (maximumDuplicate) (void)table.closeHandle(maximumDuplicate);
    Handle sameAccessDuplicate{};
    // DUPLICATE_SAME_ACCESS copies the exact source grant. DesiredAccess is
    // ignored by that option, even if it contains a bit outside the type mask.
    if (ok) ok = table.duplicateHandle(handle, table, sameAccessDuplicate, 0x80000000u,
                                       false, 2) == NtStatus::Success && sameAccessDuplicate;
    duplicateGranted = 0;
    if (ok) ok = table.lookup(sameAccessDuplicate, &duplicateGranted) == &object &&
        duplicateGranted == kRead;
    if (sameAccessDuplicate) (void)table.closeHandle(sameAccessDuplicate);
    if (handle) (void)table.closeHandle(handle);
    ok = ok && object.handleCount == 0;
    if (!ok) return false;
    g_failureStage = 2;

    handle = 0;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, ob::MAXIMUM_ALLOWED, &type, table, handle) ==
         NtStatus::Success && handle && g_lastGranted == kRead;
    if (handle) (void)table.closeHandle(handle);
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, ob::MAXIMUM_ALLOWED | kWrite,
         &type, table, handle) == NtStatus::AccessDenied && handle == 0;
    g_malformedGrant = true;
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, ob::MAXIMUM_ALLOWED, &type, table, handle) ==
         NtStatus::AccessDenied && handle == 0;
    g_malformedGrant = false;
    if (!ok) return false;
    g_failureStage = 3;
    object.securityDescriptor = nullptr;
    handle = 0;
    uint32_t grantedMaximum{};
    ob::ObjectHeader* maximumReference{};
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, ob::MAXIMUM_ALLOWED, &type, table, handle) ==
         NtStatus::Success && handle && table.referenceHandle(handle, 0, &type, &maximumReference,
         &grantedMaximum) == NtStatus::Success && grantedMaximum == kValid;
    if (maximumReference) ob::ObDereferenceObject(maximumReference);
    if (handle) (void)table.closeHandle(handle);
    object.securityDescriptor = &g_descriptorToken;
    if (!ok) return false;
    g_failureStage = 4;

    g_deny = true;
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, kRead, &type, table, handle) == NtStatus::AccessDenied &&
         handle == 0 && table.getActiveHandleCount() == 0;
    g_deny = false;
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, 0x4, &type, table, handle) == NtStatus::AccessDenied &&
         handle == 0;
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0x8, kRead, &type, table, handle) == NtStatus::InvalidParameter &&
         handle == 0;
    ob::ObjectType otherType{};
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, kRead, &otherType, table, handle) ==
         NtStatus::ObjectTypeMismatch && handle == 0;
    handle = 0xdead;
    ok = ok && table.createHandle(nullptr, handle, kRead) == NtStatus::InvalidParameter && handle == 0;
    handle = 0xdead;
    ok = ok && table.createHandle(&object, handle, 0x80000000u) == NtStatus::AccessDenied &&
         handle == 0 && table.getActiveHandleCount() == 0;
    handle = 0xdead;
    ok = ok && table.duplicateHandle(0x1000, table, handle, 0) == NtStatus::InvalidHandle && handle == 0;
    ok = ok && table.setHandleAttributes(0x1000, 0x8) == NtStatus::InvalidParameter;
    if (!ok) return false;
    g_failureStage = 5;

    g_failOpen = true;
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, 0, kRead, &type, table, handle) == NtStatus::Unsuccessful &&
         handle == 0 && table.getActiveHandleCount() == 0 && object.handleCount == 0;
    handle = 0xdead;
    ok = ok && ob::ObOpenObjectByPointer(&object, ob::OBJ_PROTECT_CLOSE, kRead, &type, table, handle) ==
         NtStatus::Unsuccessful && handle == 0 && table.getActiveHandleCount() == 0 && object.handleCount == 0;
    g_failOpen = false;
    if (!ok) return false;
    g_failureStage = 6;

    static ob::HandleTable inheritanceSource, inheritanceTarget;
    Handle inheritanceHandle{};
    if (inheritanceSource.createHandle(&object, inheritanceHandle, kRead, OBJ_INHERIT) != NtStatus::Success)
        return false;
    const uint32_t priorCloseCount = g_closeCount;
    g_failOpen = true;
    ok = inheritanceTarget.inheritHandlesFrom(inheritanceSource) == NtStatus::Unsuccessful &&
         inheritanceTarget.getActiveHandleCount() == 0 && object.handleCount == 1 &&
         g_closeCount == priorCloseCount + 1;
    g_failOpen = false;
    if (ok) ok = inheritanceTarget.inheritHandlesFrom(inheritanceSource) == NtStatus::Success &&
         inheritanceTarget.getActiveHandleCount() == 1 && object.handleCount == 2;
    if (ok) ok = inheritanceTarget.closeHandle(inheritanceHandle) == NtStatus::Success &&
         object.handleCount == 1 && g_closeCount == priorCloseCount + 2;
    if (ok) ok = inheritanceSource.closeHandle(inheritanceHandle) == NtStatus::Success &&
         object.handleCount == 0 && g_closeCount == priorCloseCount + 3;
    if (!ok) return false;
    g_failureStage = 7;
    handle = 0;
    ok = ok && table.createHandle(&object, handle, kRead, ob::OBJ_PROTECT_CLOSE) == NtStatus::Success &&
         table.closeHandle(handle) == NtStatus::HandleNotClosable && object.handleCount == 1;
    table.closeAllHandles();
    ok = ok && object.handleCount == 0;
    if (!ok) return false;
    g_failureStage = 8;
    handle = 0xdead;
    ok = ok && table.createHandle(&object, handle, kRead) == NtStatus::ProcessIsTerminating && handle == 0;
    ob::ObSetAccessCheckProcedure(nullptr);
    g_failureStage = ok ? 0 : 9;
    return ok;
}

extern "C" uint32_t MicantObOpenAccessFailureStage() noexcept { return g_failureStage; }
