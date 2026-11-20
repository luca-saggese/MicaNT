#include "micant/km/executive/ps.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include <cstring>

#if defined(MICANT_BOOTSTRAP_REACTOS_APP_TASKMGR)
extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;
#endif

namespace micant::sys {
namespace {
struct UserExceptionRecord64 {
    uint32_t code, flags;
    uintptr_t nestedRecord, exceptionAddress;
    uint32_t parameterCount, alignment;
    uint64_t information[15];
};
static_assert(sizeof(UserExceptionRecord64) == 0x98);
}

NtStatus NtRaiseException(const void* recordAddress, const void* contextAddress, bool firstChance,
                          void* trapFrame, void* floatingState, size_t floatingStateSize,
                          uint32_t xsaveMask, hal::amd64::UserSegmentState* userSegments) {
    auto* current = ke::KeGetCurrentThread();
    if (!current || !current->owningThread || !current->owningProcess ||
        !current->previousUserMode || !recordAddress || !contextAddress || !trapFrame ||
        !floatingState || floatingStateSize < 512 || floatingStateSize > 4096)
        return NtStatus::InvalidParameter;
    if (firstChance && current->userRaiseDepth >= current->userRaiseStack.size())
        return NtStatus::StackOverflow;

    UserExceptionRecord64 records[16]{};
    auto& record = records[0];
    const auto copyRecord = [](uintptr_t address, UserExceptionRecord64& out) noexcept {
        if ((address & 3) != 0) return NtStatus::DatatypeMisalignment;
        constexpr size_t prefix = offsetof(UserExceptionRecord64, information);
        auto result = ex::ExSafeCopyFromUser(&out, reinterpret_cast<const void*>(address), prefix, mm::UserMode);
        if (result != NtStatus::Success) return result;
        if (out.parameterCount > 15) return NtStatus::InvalidParameter;
        if (address > UINTPTR_MAX - prefix) return NtStatus::AccessViolation;
        return ex::ExSafeCopyFromUser(out.information,
            reinterpret_cast<const void*>(address + prefix), out.parameterCount * sizeof(uint64_t), mm::UserMode);
    };
    auto status = copyRecord(reinterpret_cast<uintptr_t>(recordAddress), record);
    if (status != NtStatus::Success) return status;
#if defined(MICANT_BOOTSTRAP_REACTOS_APP_TASKMGR)
    MicantNetbootSerialWrite("[MicaNT TaskmgrFault] code=");
    MicantNetbootSerialWriteNumber(record.code);
    MicantNetbootSerialWrite(" address=");
    MicantNetbootSerialWriteNumber(record.exceptionAddress);
    MicantNetbootSerialWrite(" access=");
    MicantNetbootSerialWriteNumber(record.information[0]);
    MicantNetbootSerialWrite(" target=");
    MicantNetbootSerialWriteNumber(record.information[1]);
    CONTEXT capturedContext{};
    const auto contextCopy = ex::ExSafeCopyFromUser(&capturedContext, contextAddress,
        offsetof(CONTEXT, FltSave), mm::UserMode);
    MicantNetbootSerialWrite(" contextRip=");
    MicantNetbootSerialWriteNumber(contextCopy == NtStatus::Success ? capturedContext.Rip : 0);
    MicantNetbootSerialWrite(" contextRsp=");
    MicantNetbootSerialWriteNumber(contextCopy == NtStatus::Success ? capturedContext.Rsp : 0);
    if (contextCopy == NtStatus::Success) {
        uint64_t returns[4]{};
        if (ex::ExSafeCopyFromUser(returns, reinterpret_cast<const void*>(capturedContext.Rsp),
                sizeof(returns), mm::UserMode) == NtStatus::Success) {
            for (size_t i = 0; i < 4; ++i) {
                MicantNetbootSerialWrite(" ret=");
                MicantNetbootSerialWriteNumber(returns[i]);
            }
        }
    }
    MicantNetbootSerialWrite(" rip=");
    MicantNetbootSerialWriteNumber(static_cast<hal::amd64::SyscallFrame*>(trapFrame)->rip);
    MicantNetbootSerialWrite("\r\n");
#endif
    if (record.parameterCount > 15) return NtStatus::InvalidParameter;
    record.code &= ~uint32_t{0x10000000}; // NT reserves this bit for internal exception codes.
    uintptr_t visited[16]{};
    visited[0] = reinterpret_cast<uintptr_t>(recordAddress);
    size_t count = 1;
    uintptr_t nested = record.nestedRecord;
    while (nested) {
        if (count == 16) return NtStatus::InvalidParameter;
        for (size_t i = 0; i < count; ++i) if (visited[i] == nested) return NtStatus::InvalidParameter;
        visited[count++] = nested;
        status = copyRecord(nested, records[count - 1]);
        if (status != NtStatus::Success) return status;
        if (records[count - 1].parameterCount > 15) return NtStatus::InvalidParameter;
        nested = records[count - 1].nestedRecord;
    }

    auto stagedFrame = *static_cast<hal::amd64::SyscallFrame*>(trapFrame);
    alignas(64) uint8_t stagedFloating[4096]{};
    std::memcpy(stagedFloating, floatingState, floatingStateSize);
    // NtContinue validates all requested register groups and CONTEXT_EX/XSTATE
    // before changing the staged frame. Raising is not a continuation, so its
    // temporary APC bookkeeping must not alter the target thread.
    const auto apcDepth = current->userApcInFlightDepth;
    const auto apcStack = current->userApcInterruptedRsp;
    const auto raiseDepth = current->userRaiseDepth;
    const auto raiseStack = current->userRaiseStack;
    hal::amd64::UserSegmentState stagedSegments{};
    if (userSegments) stagedSegments = *userSegments;
    status = NtContinue(contextAddress, false, &stagedFrame, stagedFloating, floatingStateSize,
                        xsaveMask, true, userSegments ? &stagedSegments : nullptr);
    current->userApcInFlightDepth = apcDepth;
    current->userApcInterruptedRsp = apcStack;
    current->userRaiseDepth = raiseDepth;
    current->userRaiseStack = raiseStack;
    if (status != NtStatus::Success) return status;

    if (!firstChance) {
        // No debugger port is currently supported; second chance follows the
        // real Ps process termination path rather than reporting success.
        __asm__ __volatile__("swapgs; movw $0x18, %%ax; movw %%ax, %%ds; movw %%ax, %%es"
                             : : : "rax", "memory");
        if (hal::amd64::bootstrapSchedulerClockActive()) __asm__ __volatile__("sti" : : : "memory");
        status = NtTerminateProcess(InvalidHandleValue, static_cast<NtStatus>(record.code));
        __asm__ __volatile__("cli; movw $0x2b, %%ax; movw %%ax, %%ds; movw %%ax, %%es; swapgs"
                             : : : "rax", "memory");
        return status;
    }

    uint64_t hardware[6]{0, stagedFrame.rip, stagedFrame.cs, stagedFrame.flags, stagedFrame.rsp, stagedFrame.ss};
    uint64_t registers[15]{stagedFrame.r15, stagedFrame.r14, stagedFrame.r13, stagedFrame.r12,
        stagedFrame.r11, stagedFrame.r10, stagedFrame.r9, stagedFrame.r8, stagedFrame.rdi,
        stagedFrame.rsi, stagedFrame.rbp, stagedFrame.rbx, stagedFrame.rdx, stagedFrame.rcx, stagedFrame.rax};
    status = current->owningThread->prepareUserException(record.code, UINT32_MAX,
        record.exceptionAddress, hardware, registers, stagedFloating, records, count,
        static_cast<hal::amd64::SyscallFrame*>(trapFrame)->rsp,
        userSegments ? &stagedSegments : nullptr);
    if (status != NtStatus::Success) return status;
    current->userRaiseStack[current->userRaiseDepth++] =
        static_cast<hal::amd64::SyscallFrame*>(trapFrame)->rsp;
    stagedFrame.rip = hardware[1]; stagedFrame.cs = hardware[2]; stagedFrame.flags = hardware[3];
    stagedFrame.rsp = hardware[4]; stagedFrame.ss = hardware[5];
    stagedFrame.r15 = registers[0]; stagedFrame.r14 = registers[1]; stagedFrame.r13 = registers[2];
    stagedFrame.r12 = registers[3]; stagedFrame.r11 = registers[4]; stagedFrame.r10 = registers[5];
    stagedFrame.r9 = registers[6]; stagedFrame.r8 = registers[7]; stagedFrame.rdi = registers[8];
    stagedFrame.rsi = registers[9]; stagedFrame.rbp = registers[10]; stagedFrame.rbx = registers[11];
    stagedFrame.rdx = registers[12]; stagedFrame.rcx = registers[13]; stagedFrame.rax = registers[14];
    *static_cast<hal::amd64::SyscallFrame*>(trapFrame) = stagedFrame;
    std::memcpy(floatingState, stagedFloating, floatingStateSize);
    return NtStatus::Success;
}
}

extern "C" bool MicantPsTerminateFailedUserException(uint32_t exitStatus) noexcept {
    auto* current = micant::ke::KeGetCurrentThread();
    if (!current || current->previousUserMode != true || !current->owningThread ||
        !current->owningProcess || micant::ke::KeGetCurrentIrql() != micant::ke::PASSIVE_LEVEL)
        return false;

    // A missing dispatcher, exhausted stack guarantee, or invalid user
    // delivery frame is an unhandled user exception. Terminate through Ps so
    // APC rundown, thread exit and deferred process cleanup still run.
    // The exception gate enters with IF clear and can retain user data
    // selectors; teardown callbacks must use the ordinary kernel convention.
    micant::ke::detail::ReturnKernelContext kernelContext;
    (void)current->owningProcess->terminateCurrent(static_cast<micant::NtStatus>(exitStatus));
    return false;
}
