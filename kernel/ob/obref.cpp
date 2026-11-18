#include "micant/km/executive/ob.hpp"
#if defined(MICANT_FREESTANDING)
#include "micant/km/core/ke.hpp"
#include "micant/km/executive/ps.hpp"
#endif
#if !defined(MICANT_FREESTANDING)
#include "micant/km/core/ntos_internal.hpp"
#include "micant/km/core/sync.hpp"
#endif

namespace micant::sys {

static ob::HandleTable g_KernelHandleTable;
#if defined(MICANT_FREESTANDING)
// Bootstrap CPL3 probes without a Ps process must not share kernel handles.
static ob::HandleTable g_BootstrapUserHandleTable;
#endif
static Handle g_NextExecutiveHandle = 0x100;

namespace internal {

Handle ObAllocateExecutiveHandle() noexcept {
    Handle h = g_NextExecutiveHandle;
    g_NextExecutiveHandle += 4;
    return h;
}

NtStatus ObCloseGenericHandle(Handle h) {
    return g_KernelHandleTable.closeHandle(h);
}

} // namespace internal

#if !defined(MICANT_FREESTANDING)
// Native NtClose is not exported until all owning subsystem close paths exist.
NtStatus NtClose(Handle handle) {
    if (handle == 0 || handle == InvalidHandleValue) {
        return NtStatus::InvalidHandle;
    }

    bool wasSync = sync::DispatcherRegistry::get().lookup(handle) != nullptr;
    sync::DispatcherRegistry::get().unregister(handle);

    bool hadIo    = internal::IoCloseKernelHandle(handle);
    bool hadSec   = internal::MmCloseSectionHandle(handle);
    bool hadKey   = internal::CmCloseKeyHandle(handle);
    bool hadToken = internal::SeCloseTokenHandle(handle);
    bool hadPort  = internal::LpcClosePortHandle(handle);

    if (wasSync || hadIo || hadSec || hadKey || hadToken || hadPort) {
        return NtStatus::Success;
    }
    return internal::ObCloseGenericHandle(handle);
}
#else
namespace internal { bool CmCloseKeyHandle(Handle h); }

NtStatus NtClose(Handle handle) {
    if (internal::CmCloseKeyHandle(handle)) return NtStatus::Success;
    ob::HandleTable* table{};
    Handle raw{};
    const auto status = ob::detail::resolveHandle(handle, table, raw);
    return status == NtStatus::Success ? table->closeHandle(raw) : status;
}

extern "C" __declspec(dllexport) NtStatus ZwClose(Handle handle) noexcept {
    ob::HandleTable* table{};
    Handle raw{};
    if (ke::KeGetCurrentIrql() != ke::PASSIVE_LEVEL) return NtStatus::InvalidParameter;
    const auto status = ob::detail::resolveHandle(handle, table, raw);
    return status == NtStatus::Success ? table->closeHandle(raw) : status;
}
#endif

} // namespace micant::sys

namespace micant::ob::detail {

HandleTable& kernelHandleTable() noexcept {
    return sys::g_KernelHandleTable;
}

#if defined(MICANT_FREESTANDING)
HandleTable& currentHandleTable() noexcept {
    const auto* thread = ke::KeGetCurrentThread();
    // An attached thread operates in the attached process's handle table;
    // kernel handles remain separately tagged and resolved below.
    if (auto* process = ke::KeGetCurrentProcess()) return process->getHandleTable();
    if (thread && thread->previousUserMode) return sys::g_BootstrapUserHandleTable;
    return kernelHandleTable();
}

Handle encodeKernelHandle(Handle handle) noexcept {
    constexpr uintptr_t tag = uintptr_t{1} << (sizeof(Handle) * 8 - 1);
    return static_cast<Handle>(static_cast<uintptr_t>(handle) | tag);
}

NtStatus resolveHandle(Handle handle, HandleTable*& table, Handle& untagged, bool kernelAccess) noexcept {
    constexpr uintptr_t tag = uintptr_t{1} << (sizeof(Handle) * 8 - 1);
    const auto value = static_cast<uintptr_t>(handle);
    if (value & tag) {
        const auto* thread = ke::KeGetCurrentThread();
        if (!kernelAccess && thread && thread->previousUserMode) return NtStatus::InvalidHandle;
        table = &kernelHandleTable();
        untagged = static_cast<Handle>(value & ~tag);
    } else {
        table = &currentHandleTable();
        untagged = handle;
    }
    return NtStatus::Success;
}
#endif

} // namespace micant::ob::detail
