#include "micant/km/core/ke.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/executive/probe.hpp"
#include "micant/km/executive/ps.hpp"

#include <limits>

#if defined(MICANT_KERNEL_PERF_DIAGNOSTICS)
extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;
#endif

namespace micant::ke {

#if defined(MICANT_KERNEL_PERF_DIAGNOSTICS)
namespace {
struct PerfWindow {
    uint64_t ticks{}, idle{}, idleCurrent{}, idleUser{}, pidZero{}, compositor{}, other{}, noThread{};
    uint64_t ring0{}, ring3{}, compRing0{}, compRing3{};
    uint64_t compWaiting{}, compReady{}, compRunning{};
    uint64_t switches{}, switchMismatches{}, compWait100ns{};
    uint64_t compositorUser100ns{}, compositorKernel100ns{};
    uint64_t processorUserStart{}, processorKernelStart{}, processorIdleStart{};
    uint64_t threadUserStart{}, threadKernelStart{};
    uint64_t tickStart{};
    uint64_t waitTimeouts{}, waitWakeups{};
    uint64_t pidZeroCpl3{}, lastUserRip{}, lastUserThread{}, lastIdleThread{};
    uint64_t lastPidZeroThread{}, lastPidZeroRip{};
    uint64_t otherPid{}, otherTid{}, otherOwnerPid{};
    uint32_t otherState{};
} g_perfWindow{};
bool g_compositorWaitActive{};
uint64_t g_compositorWaitStarted100ns{};

bool isCompositorThread(const KThread* thread) noexcept {
    return thread && thread->owningProcess &&
        thread->owningProcess->processImageName() == u"compositor.exe";
}
}
#endif

bool KeQuerySystemTime100ns(uint64_t& systemTime100ns) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    return hal::amd64::queryBootstrapSystemTime(systemTime100ns) ==
        hal::amd64::BootstrapClockStatus::Success;
#else
    (void)systemTime100ns;
    return false;
#endif
}

bool KeQueryUptime100ns(uint64_t& uptime100ns) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    return hal::amd64::queryBootstrapUptime(uptime100ns) ==
        hal::amd64::BootstrapClockStatus::Success;
#else
    (void)uptime100ns;
    return false;
#endif
}

bool KeQueryPerformanceCounterValue(uint64_t& counter,
                                    uint64_t& frequency) noexcept {
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    return hal::amd64::queryBootstrapPerformanceCounter(counter, frequency) ==
        hal::amd64::BootstrapClockStatus::Success;
#else
    (void)counter;
    (void)frequency;
    return false;
#endif
}

bool KeQueryProcessorPerformanceSnapshot(ProcessorPerformanceSnapshot& snapshot) noexcept {
    snapshot = {};
#if defined(MICANT_BAREMETAL) && defined(MICANT_FREESTANDING) && \
    (defined(_M_X64) || defined(__x86_64__))
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const bool available = hal::amd64::bootstrapSchedulerClockActive() &&
        g_CurrentCpu.processorPerformanceTimeAvailable;
    if (available) {
        snapshot.idleTime100ns = g_CurrentCpu.processorIdleTime100ns;
        snapshot.kernelTime100ns = g_CurrentCpu.processorKernelTime100ns;
        snapshot.userTime100ns = g_CurrentCpu.processorUserTime100ns;
        // The current canonical bare-metal scheduler brings up only the BSP.
        snapshot.processorCount = 1;
        snapshot.available = true;
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return snapshot.available;
#else
    return false;
#endif
}

void KeAccountClockInterruptTime(bool interruptedUserMode) noexcept {
#if defined(MICANT_BAREMETAL) && defined(MICANT_FREESTANDING) && \
    (defined(_M_X64) || defined(__x86_64__))
    constexpr uint64_t tickDuration100ns = 100'000; // Canonical 100 Hz clock tick.
    auto& cpu = g_CurrentCpu;
#if defined(MICANT_KERNEL_PERF_DIAGNOSTICS)
    auto& window = g_perfWindow;
    const auto* thread = cpu.currentThread;
    ++window.ticks;
    if (interruptedUserMode) ++window.ring3; else ++window.ring0;
    const bool compositor = isCompositorThread(thread);
    if (!thread) ++window.noThread;
    else if (compositor) {
        ++window.compositor;
        if (interruptedUserMode) ++window.compRing3; else ++window.compRing0;
        if (thread->state == ThreadState::Waiting) ++window.compWaiting;
        else if (thread->state == ThreadState::Ready) ++window.compReady;
        else if (thread->state == ThreadState::Running) ++window.compRunning;
        if (interruptedUserMode) window.compositorUser100ns += 100'000;
        else window.compositorKernel100ns += 100'000;
    } else if (thread == cpu.idleThread) {
        ++window.idleCurrent;
        if (interruptedUserMode) ++window.idleUser;
        else ++window.idle;
    } else if (thread->pid == 0) {
        ++window.pidZero;
        ++window.other;
        window.otherPid = thread->pid;
        window.otherTid = thread->tid;
        window.otherOwnerPid = thread->owningProcess ? thread->owningProcess->processId() : 0;
        window.otherState = static_cast<uint32_t>(thread->state);
    }
    else if (thread) {
        ++window.other;
        window.otherPid = thread->pid;
        window.otherTid = thread->tid;
        window.otherOwnerPid = thread->owningProcess ? thread->owningProcess->processId() : 0;
        window.otherState = static_cast<uint32_t>(thread->state);
    }

    if (window.tickStart == 0) {
        window.tickStart = 1;
        window.processorUserStart = cpu.processorUserTime100ns;
        window.processorKernelStart = cpu.processorKernelTime100ns;
        window.processorIdleStart = cpu.processorIdleTime100ns;
    }
    if (window.ticks >= 500) {
        uint64_t now{};
        (void)hal::amd64::queryBootstrapUptime(now);
        if (g_compositorWaitActive && now >= g_compositorWaitStarted100ns) {
            window.compWait100ns += now - g_compositorWaitStarted100ns;
            g_compositorWaitStarted100ns = now;
        }
        MicantNetbootSerialWrite("[MicaNT KePerf] ticks="); MicantNetbootSerialWriteNumber(window.ticks);
        MicantNetbootSerialWrite(" idle="); MicantNetbootSerialWriteNumber(window.idle);
        MicantNetbootSerialWrite(" idle_current="); MicantNetbootSerialWriteNumber(window.idleCurrent);
        MicantNetbootSerialWrite(" idle_user="); MicantNetbootSerialWriteNumber(window.idleUser);
        MicantNetbootSerialWrite(" pid_zero="); MicantNetbootSerialWriteNumber(window.pidZero);
        MicantNetbootSerialWrite(" pid_zero_cpl3="); MicantNetbootSerialWriteNumber(window.pidZeroCpl3);
        MicantNetbootSerialWrite(" user_rip="); MicantNetbootSerialWriteNumber(window.lastUserRip);
        MicantNetbootSerialWrite(" user_thread="); MicantNetbootSerialWriteNumber(window.lastUserThread);
        MicantNetbootSerialWrite(" idle_thread="); MicantNetbootSerialWriteNumber(window.lastIdleThread);
        MicantNetbootSerialWrite(" pid_zero_thread="); MicantNetbootSerialWriteNumber(window.lastPidZeroThread);
        MicantNetbootSerialWrite(" pid_zero_rip="); MicantNetbootSerialWriteNumber(window.lastPidZeroRip);
        MicantNetbootSerialWrite(" compositor="); MicantNetbootSerialWriteNumber(window.compositor);
        MicantNetbootSerialWrite(" other="); MicantNetbootSerialWriteNumber(window.other);
        MicantNetbootSerialWrite(" no_thread="); MicantNetbootSerialWriteNumber(window.noThread);
        MicantNetbootSerialWrite(" ring0="); MicantNetbootSerialWriteNumber(window.ring0);
        MicantNetbootSerialWrite(" ring3="); MicantNetbootSerialWriteNumber(window.ring3);
        MicantNetbootSerialWrite(" comp_r0="); MicantNetbootSerialWriteNumber(window.compRing0);
        MicantNetbootSerialWrite(" comp_r3="); MicantNetbootSerialWriteNumber(window.compRing3);
        MicantNetbootSerialWrite(" comp_running="); MicantNetbootSerialWriteNumber(window.compRunning);
        MicantNetbootSerialWrite(" comp_ready="); MicantNetbootSerialWriteNumber(window.compReady);
        MicantNetbootSerialWrite(" comp_waiting_ticks="); MicantNetbootSerialWriteNumber(window.compWaiting);
        MicantNetbootSerialWrite(" comp_wait_ms="); MicantNetbootSerialWriteNumber(window.compWait100ns / 10000);
        MicantNetbootSerialWrite(" other_pid="); MicantNetbootSerialWriteNumber(window.otherPid);
        MicantNetbootSerialWrite(" other_owner="); MicantNetbootSerialWriteNumber(window.otherOwnerPid);
        MicantNetbootSerialWrite(" other_tid="); MicantNetbootSerialWriteNumber(window.otherTid);
        MicantNetbootSerialWrite(" other_state="); MicantNetbootSerialWriteNumber(window.otherState);
        MicantNetbootSerialWrite(" comp_user100ns="); MicantNetbootSerialWriteNumber(window.compositorUser100ns);
        MicantNetbootSerialWrite(" comp_kernel100ns="); MicantNetbootSerialWriteNumber(window.compositorKernel100ns);
        MicantNetbootSerialWrite(" switches="); MicantNetbootSerialWriteNumber(window.switches);
        MicantNetbootSerialWrite(" switch_mismatch="); MicantNetbootSerialWriteNumber(window.switchMismatches);
        MicantNetbootSerialWrite(" wait_timeout="); MicantNetbootSerialWriteNumber(window.waitTimeouts);
        MicantNetbootSerialWrite(" wait_wakeup="); MicantNetbootSerialWriteNumber(window.waitWakeups);
        MicantNetbootSerialWrite(" cpu_user="); MicantNetbootSerialWriteNumber(cpu.processorUserTime100ns - window.processorUserStart);
        MicantNetbootSerialWrite(" cpu_kernel="); MicantNetbootSerialWriteNumber(cpu.processorKernelTime100ns - window.processorKernelStart);
        MicantNetbootSerialWrite(" cpu_idle="); MicantNetbootSerialWriteNumber(cpu.processorIdleTime100ns - window.processorIdleStart);
        MicantNetbootSerialWrite("\r\n");
        window = {};
        window.tickStart = 1;
        window.processorUserStart = cpu.processorUserTime100ns;
        window.processorKernelStart = cpu.processorKernelTime100ns;
        window.processorIdleStart = cpu.processorIdleTime100ns;
        (void)now;
    }
#endif
    if (!cpu.currentThread) return;
    auto& modeTime = interruptedUserMode ? cpu.processorUserTime100ns
                                         : cpu.processorKernelTime100ns;
    const bool idleTick = !interruptedUserMode && cpu.currentThread == cpu.idleThread;
    if (modeTime > std::numeric_limits<uint64_t>::max() - tickDuration100ns ||
        (idleTick && cpu.processorIdleTime100ns >
            std::numeric_limits<uint64_t>::max() - tickDuration100ns)) {
        cpu.processorPerformanceTimeAvailable = false;
        return;
    }
    modeTime += tickDuration100ns;
    if (idleTick) cpu.processorIdleTime100ns += tickDuration100ns;
    auto& threadTime = interruptedUserMode ? cpu.currentThread->userTime100ns
                                           : cpu.currentThread->kernelTime100ns;
    if (threadTime > std::numeric_limits<uint64_t>::max() - tickDuration100ns)
        threadTime = std::numeric_limits<uint64_t>::max();
    else
        threadTime += tickDuration100ns;
    cpu.processorPerformanceTimeAvailable = true;
#else
    (void)interruptedUserMode;
#endif
}

#if defined(MICANT_KERNEL_PERF_DIAGNOSTICS)
void KeRecordContextSwitch(KThread* outgoing, KThread* incoming) noexcept {
    auto& window = g_perfWindow;
    ++window.switches;
    uint64_t now{};
    (void)hal::amd64::queryBootstrapUptime(now);
    if (isCompositorThread(outgoing) && outgoing->state == ThreadState::Waiting) {
        if (!g_compositorWaitActive) {
            g_compositorWaitStarted100ns = now;
            g_compositorWaitActive = true;
        }
    }
    if (isCompositorThread(incoming) && g_compositorWaitActive) {
        if (now >= g_compositorWaitStarted100ns)
            window.compWait100ns += now - g_compositorWaitStarted100ns;
        g_compositorWaitActive = false;
    }
}
void KeRecordContextResume(KThread* outgoing) noexcept {
    if (KeGetCurrentThread() != outgoing || outgoing->state != ThreadState::Running)
        ++g_perfWindow.switchMismatches;
}
void KeRecordCompositorWaitResult(bool timeout) noexcept {
    if (timeout) ++g_perfWindow.waitTimeouts;
    else ++g_perfWindow.waitWakeups;
}
void KeRecordTimerContext(uint64_t savedCs, uint64_t savedRip) noexcept {
    auto& window = g_perfWindow;
    const auto* thread = g_CurrentCpu.currentThread;
    window.lastIdleThread = reinterpret_cast<uintptr_t>(g_CurrentCpu.idleThread);
    if ((savedCs & 3) == 3) {
        window.lastUserRip = savedRip;
        window.lastUserThread = reinterpret_cast<uintptr_t>(thread);
        if (thread && thread->pid == 0 && !thread->owningProcess) ++window.pidZeroCpl3;
    } else if (thread && thread->pid == 0 && !thread->owningProcess) {
        window.lastPidZeroThread = reinterpret_cast<uintptr_t>(thread);
        window.lastPidZeroRip = savedRip;
    }
}
#endif

} // namespace micant::ke

namespace micant::sys {

NtStatus NtQuerySystemTime(LargeInteger* systemTime) noexcept {
    if (!systemTime) return NtStatus::AccessViolation;
    if (ex::ProbeForWrite(systemTime, sizeof(*systemTime), alignof(LargeInteger),
                          mm::UserMode) != NtStatus::Success)
        return NtStatus::AccessViolation;
    uint64_t value{};
    if (!ke::KeQuerySystemTime100ns(value) || value > static_cast<uint64_t>(INT64_MAX))
        return NtStatus::DeviceNotReady;
    LargeInteger result{};
    result.quadPart = static_cast<int64_t>(value);
    return ex::ExSafeCopyToUser(systemTime, &result, sizeof(result), mm::UserMode);
}

NtStatus NtQueryPerformanceCounter(LargeInteger* performanceCounter,
                                   LargeInteger* performanceFrequency) {
    if (!performanceCounter) return NtStatus::AccessViolation;
    if (ex::ProbeForWrite(performanceCounter, sizeof(*performanceCounter),
                          alignof(LargeInteger), mm::UserMode) != NtStatus::Success ||
        (performanceFrequency && ex::ProbeForWrite(performanceFrequency,
            sizeof(*performanceFrequency), alignof(LargeInteger), mm::UserMode) != NtStatus::Success))
        return NtStatus::AccessViolation;
    uint64_t counter{}, frequency{};
    if (!ke::KeQueryPerformanceCounterValue(counter, frequency) ||
        counter > static_cast<uint64_t>(INT64_MAX) ||
        frequency > static_cast<uint64_t>(INT64_MAX))
        return NtStatus::DeviceNotReady;
    LargeInteger countResult{}, frequencyResult{};
    countResult.quadPart = static_cast<int64_t>(counter);
    frequencyResult.quadPart = static_cast<int64_t>(frequency);
    NtStatus status = ex::ExSafeCopyToUser(performanceCounter, &countResult,
                                           sizeof(countResult), mm::UserMode);
    if (status != NtStatus::Success || !performanceFrequency) return status;
    return ex::ExSafeCopyToUser(performanceFrequency, &frequencyResult,
                                sizeof(frequencyResult), mm::UserMode);
}

} // namespace micant::sys
