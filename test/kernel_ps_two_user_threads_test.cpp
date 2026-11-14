#include "micant/km/executive/ps.hpp"
#include "micant/km/executive/ps_objects.hpp"
#include "micant/km/executive/image_metadata.hpp"
#include "micant/km/executive/io_file.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/core/syscalls.hpp"
#include "micant/km/core/passive_work.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/um/native/pe.hpp"
#include <cstring>
#include <span>

namespace micant::sys {
uint32_t bootstrapSectionObjectCount() noexcept;
uint32_t bootstrapEventObjectCount() noexcept;
}

namespace {
size_t readyNonPassiveWorkThreadCount() noexcept {
    auto count = micant::ke::PriorityScheduler::get().getReadyThreadCount();
    for (const auto& worker : micant::ke::passive_work::detail::workers)
        if (worker.readyQueued && count) --count;
    return count;
}

void serialWrite(const char* text) noexcept {
    while (*text) {
        uint8_t ready{};
        do {
            __asm__ __volatile__("inb %1, %0" : "=a"(ready) : "Nd"(uint16_t{0x3fd}));
        } while (!(ready & 0x20));
        __asm__ __volatile__("outb %0, %1" : : "a"(static_cast<uint8_t>(*text++)), "Nd"(uint16_t{0x3f8}));
    }
}
void serialWriteHex(uint32_t value) noexcept {
    static constexpr char digits[] = "0123456789ABCDEF";
    char text[8]{};
    for (size_t i = 0; i < sizeof(text); ++i)
        text[i] = digits[(value >> ((sizeof(text) - 1 - i) * 4)) & 0x0f];
    for (char c : text) {
        uint8_t ready{};
        do { __asm__ __volatile__("inb %1, %0" : "=a"(ready) : "Nd"(uint16_t{0x3fd})); }
        while (!(ready & 0x20));
        __asm__ __volatile__("outb %0, %1" : : "a"(static_cast<uint8_t>(c)), "Nd"(uint16_t{0x3f8}));
    }
}
bool drainDeferredDeletes(uintptr_t kernelRoot, uintptr_t processRoot) noexcept {
    if (!micant::hal::amd64::switchProcessPagingRootByPhysical(kernelRoot)) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; sti" : "=r"(flags) : : "memory");
    const uint64_t start = micant::hal::amd64::bootstrapSchedulerClockTicks();
    for (size_t spin = 0; spin < 50000000 && micant::io::IoDeferredFileDeletesPending() &&
         micant::hal::amd64::bootstrapSchedulerClockTicks() - start < 100; ++spin)
        __asm__ __volatile__("pause");
    __asm__ __volatile__("cli" : : : "memory");
    const bool drained = !micant::io::IoDeferredFileDeletesPending();
    const bool restored = micant::hal::amd64::switchProcessPagingRootByPhysical(processRoot);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return drained && restored;
}
bool waitForPfnBaselineAndDeferredDeletes(uintptr_t kernelRoot, uintptr_t processRoot,
                                          std::span<const micant::mm::Mmpfn> baseline,
                                          std::span<micant::mm::Mmpfn> observed) noexcept {
    if (baseline.size() > observed.size()) return false;
    auto* current = micant::ke::KeGetCurrentThread();
    if (!current) return false;
    if (!micant::hal::amd64::switchProcessPagingRootByPhysical(kernelRoot)) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    if (micant::ke::g_CurrentCpu.serviceDeferredCleanup)
        micant::ke::g_CurrentCpu.serviceDeferredCleanup();
    const auto originalPriority = current->priority;
    // Let the normal-priority deferred reaper run while this bootstrap test waits.
    current->priority = micant::ke::PRIORITY_IDLE;
    if (!(flags & (uint64_t{1} << 9)))
    __asm__ __volatile__("sti" : : : "memory");
    const uint64_t start = micant::hal::amd64::bootstrapSchedulerClockTicks();
    uint64_t lastSnapshotTick = UINT64_MAX;
    bool drained = false;
    for (size_t spin = 0; spin < 50000000 &&
         micant::hal::amd64::bootstrapSchedulerClockTicks() - start < 100; ++spin) {
        const auto tick = micant::hal::amd64::bootstrapSchedulerClockTicks();
        if (tick != lastSnapshotTick) {
            lastSnapshotTick = tick;
            const auto count = micant::mm::PfnDatabase::get().snapshotActiveEntries(observed);
            bool matchesBaseline = count == baseline.size() && count <= observed.size();
            for (size_t i = 0; matchesBaseline && i < baseline.size(); ++i) {
                bool found = false;
                for (size_t j = 0; j < count; ++j) {
                    if (observed[j].pfn == baseline[i].pfn) {
                        found = true;
                        break;
                    }
                }
                matchesBaseline = found;
            }
            if (matchesBaseline && !micant::io::IoDeferredFileDeletesPending() &&
                !micant::ps::detail::deferredCleanupPending()) {
                drained = true;
                break;
            }
        }
        __asm__ __volatile__("pause");
    }
    __asm__ __volatile__("cli" : : : "memory");
    const auto observedCount = drained ? baseline.size() : micant::mm::PfnDatabase::get().snapshotActiveEntries(observed);
    const auto elapsedTicks = micant::hal::amd64::bootstrapSchedulerClockTicks() - start;
    const bool pendingFileDeletes = micant::io::IoDeferredFileDeletesPending();
    current->priority = originalPriority;
    const bool restored = micant::hal::amd64::switchProcessPagingRootByPhysical(processRoot);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    if (!drained) {
        size_t reported = 0;
        if (observedCount <= observed.size()) {
            for (size_t i = 0; i < observedCount && reported < 16; ++i) {
                bool found = false;
                for (const auto& entry : baseline) {
                    if (entry.pfn == observed[i].pfn) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ++reported;
                }
            }
            for (const auto& entry : baseline) {
                if (reported == 16) break;
                bool found = false;
                for (size_t i = 0; i < observedCount; ++i) {
                    if (observed[i].pfn == entry.pfn) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ++reported;
                }
            }
        } else {
        }
    }
    return drained && restored;
}

struct ProcessAddressSpaceCleanup {
    micant::ps::EProcess& process;
    uintptr_t kernelRoot;
    bool active{true};

    ~ProcessAddressSpaceCleanup() {
        if (!active || !process.getDirectoryTableBase()) return;
        if (!micant::hal::amd64::switchProcessPagingRootByPhysical(kernelRoot)) {
            return;
        }
        const auto status = process.closeAddressSpace();
        if (status != micant::NtStatus::Success) {
        }
    }
};
constexpr std::u16string_view executablePath = u"\\??\\C:\\MicaNTTest\\fixture.exe";
constexpr std::u16string_view dllPath = u"\\??\\C:\\MicaNTTest\\ntdll.dll";
constexpr std::u16string_view tlsPath = u"\\??\\C:\\MicaNTTest\\tls.dll";
constexpr std::u16string_view commandLine = u"fixture.exe --test";
const uint8_t nativeDll[]{
#include "ntdll-process.inc"
};
}

extern "C" {
extern const uint8_t MicantTwoUserThreadsEnd[];
extern const uint8_t MicantNativeThreadStart[];
extern const uint8_t MicantGrowStackThreadStart[];
extern const uint8_t MicantInheritedEventThreadStart[];
void MicantRing3ProcessCreateStart() noexcept;
__attribute__((naked, section(".uthreads$a"))) void MicantTwoUserThreads() noexcept {
    __asm__ __volatile__(
        "movq %rsp, %r14\nmovq 0(%r14), %rbx\nmovq 8(%r14), %r12\nmovq 16(%r14), %r13\nmovq 24(%r14), %r15\n"
        "subq $0x60, %rsp\n"
        "movq %gs:0x60, %rax\ntestq %rax, %rax\njz 9f\ncmpb $0, 2(%rax)\njne 9f\n"
        "movq 24(%rax), %rax\ntestq %rax, %rax\njz 9f\ncmpq $0, 32(%rax)\nje 9f\n"
        // Static PE TLS vector slot zero points to a private template copy.
        "movq %gs:0x58, %rax\ntestq %rax, %rax\njz 9f\nmovq (%rax), %rax\ntestq %rax, %rax\njz 9f\n"
        "movabsq $0x8877665544332211, %rdx\ncmpq %rdx, (%rax)\njne 9f\ncmpq $0, 8(%rax)\njne 9f\n"
        "testq %r13, %r13\njnz 4f\nmovq $0x1234, (%rax)\njmp 5f\n"
        "4: movq $0x5678, (%rax)\n5:\n"
        "movq %gs:0x58, %rax\nmovq 8(%rax), %rax\ntestq %rax, %rax\njz 9f\n"
        "movabsq $0x1020304050607080, %rdx\ncmpq %rdx, (%rax)\njne 9f\n"
        "cmpq $0, 8(%rax)\njne 9f\ntestq %r13, %r13\njnz 11f\n"
        "movq $0xabcd, (%rax)\njmp 12f\n11: movq $0xdcba, (%rax)\n12:\n"
        // Exercise the public thread query through the loaded NTDLL from each
        // real CPL3 thread.
        "subq $0x88, %rsp\nmovq $0, 0x78(%rsp)\nmovq $-2, %rcx\nxorl %edx, %edx\n"
        "leaq 0x40(%rsp), %r8\nmovq $48, %r9\nleaq 0x78(%rsp), %rax\nmovq %rax, 32(%rsp)\n"
        "callq *32(%r15)\ntestl %eax, %eax\njz 21f\nmovl %eax, %edx\njmp 8f\n21:\n"
        "cmpq $48, 0x78(%rsp)\nje 22f\nmovl $0xc0000002, %edx\njmp 8f\n22:\n"
        "movq %gs:0x30, %rax\ncmpq %rax, 0x48(%rsp)\nje 23f\nmovl $0xc0000003, %edx\njmp 8f\n23:\n"
        "movq %gs:0x40, %rax\ncmpq %rax, 0x50(%rsp)\nje 24f\nmovl $0xc0000004, %edx\njmp 8f\n24:\n"
        "movq %gs:0x48, %rax\ncmpq %rax, 0x58(%rsp)\nje 25f\nmovl $0xc0000005, %edx\njmp 8f\n25:\n"
        "addq $0x88, %rsp\n"
        "movabsq $0x1111222233334444, %rax\ntestq %r13, %r13\njz 1f\n"
        "movabsq $0xaaaabbbbccccdddd, %rax\n"
        "1: movq %rax, %gs:0x1480\nmovq 32(%r14), %rdx\ntestq %rdx, %rdx\njz 6f\n"
        // Both contexts must advance before either makes a blocking syscall.
        "movabsq $0x5a5a12345678abcd, %rdi\nxorq %r13, %rdi\nmovq %rdi, %xmm6\n"
        "7: movq %xmm6, %rax\ncmpq %rdi, %rax\njne 9f\n"
        "incq (%rdx,%r13,8)\ncmpq $0, (%rdx)\nje 7b\ncmpq $0, 8(%rdx)\nje 7b\n"
        "movabsq $0x5a5a12345678abcd, %rax\nxorq %r13, %rax\ncmpq %rdi, %rax\njne 9f\n"
        // The early diagnostic syscall path clears IF until the runtime
        // clock is active, so exercise the output probes after preemption.
        "6: movq $-2, %rcx\nleaq 49(%rsp), %rdx\nmovq %rcx, %r10\nmovl $0x52, %eax\nsyscall\n"
        "cmpl $0x80000002, %eax\njne 9f\n"
        "leaq 49(%rsp), %rcx\nxorl %edx, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "movq %rcx, %r10\nmovl $0xb4, %eax\nsyscall\ncmpl $0x80000002, %eax\njne 9f\n"
        "testq %r13, %r13\njnz 3f\n"
        "cmpq $0, 48(%r14)\nje 42f\n"
        // CPL3 native wait timeout coverage: a relative one millisecond wait
        // on an unsignaled event returns STATUS_TIMEOUT.
        "movabsq $0x400020003700, %r8\nmovq $-10000, (%r8)\nmovq %rbx, %rcx\nxorl %edx, %edx\n"
        "callq *8(%r15)\ncmpq $0x102, %rax\njne 31f\n"
        "42: movabsq $0x400020003708, %rax\nmovq $1, (%rax)\n"
        "movabsq $0x400020003708, %rax\nmovq $1, (%rax)\n"
        // NtAlertThread marks this thread alerted; an alertable delay consumes
        // that alert and returns STATUS_ALERTED without sleeping.
        "movq $-2, %rcx\ncallq *112(%r15)\ntestl %eax, %eax\njnz 33f\n"
        // The current-thread pseudo handle is accepted, while a null thread
        // handle must fail without changing the alert state.
        "xorl %ecx, %ecx\ncallq *112(%r15)\ncmpl $0xc0000008, %eax\nje .Lmicant_alert_invalid_handle\n"
        "movl $0xc0000250, %edx\njmp 8f\n.Lmicant_alert_invalid_handle:\n"
        "movabsq $0x400020003700, %rdx\nmovq $-10000, (%rdx)\nmovl $1, %ecx\n"
        "callq *104(%r15)\ncmpq $0x101, %rax\njne 34f\n"
        // A queued user APC must break an alertable wait with USER_APC and
        // run its callback on the real CPL3 return path before the caller
        // resumes after the syscall.
        "movabsq $0x400020003700, %rax\nmovq $0, (%rax)\n"
        "movq $-2, %rcx\nleaq MicantTestUserApc(%rip), %rdx\n"
        "xorl %r8d, %r8d\nxorl %r9d, %r9d\nmovq $0, 0x20(%rsp)\ncallq *120(%r15)\n"
        "testl %eax, %eax\njz 35f\nmovl %eax, %edx\njmp 8f\n35:\n"
        // Invalid handles are rejected before a KAPC is allocated or queued.
        "xorl %ecx, %ecx\nleaq MicantTestUserApc(%rip), %rdx\n"
        "xorl %r8d, %r8d\nxorl %r9d, %r9d\nmovq $0, 0x20(%rsp)\ncallq *120(%r15)\n"
        "cmpl $0xc0000008, %eax\nje .Lmicant_queue_invalid_handle\n"
        "movl $0xc0000251, %edx\njmp 8f\n.Lmicant_queue_invalid_handle:\n"
        "movq %rbx, %rcx\nmovl $1, %edx\nxorl %r8d, %r8d\ncallq *8(%r15)\n"
        "cmpq $0xc0, %rax\nje 36f\nmovl $0xc000024c, %edx\njmp 8f\n36:\n"
        "movabsq $0x400020003700, %rax\ncmpq $0x41504321, (%rax)\nje 37f\n"
        "movl $0xc000024d, %edx\njmp 8f\n37:\n"
        // Alertable delay is another user APC return boundary; deliver the
        // next queued routine before returning STATUS_USER_APC to this code.
        "movq $-2, %rcx\nleaq MicantTestUserApcSecond(%rip), %rdx\n"
        "xorl %r8d, %r8d\nxorl %r9d, %r9d\nmovq $0, 0x20(%rsp)\ncallq *120(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_delay_apc_queued\nmovl %eax, %edx\njmp 8f\n"
        ".Lmicant_delay_apc_queued:\nmovabsq $0x400020003710, %rdx\nmovq $-10000, (%rdx)\n"
        "movl $1, %ecx\ncallq *104(%r15)\ncmpq $0xc0, %rax\n"
        "je .Lmicant_delay_apc_returned\nmovl $0xc000024e, %edx\njmp 8f\n"
        ".Lmicant_delay_apc_returned:\nmovabsq $0x400020003700, %rax\n"
        "cmpq $0x41504322, (%rax)\nje .Lmicant_delay_apc_called\n"
        "movl $0xc000024f, %edx\njmp 8f\n.Lmicant_delay_apc_called:\n"
        // Thread A allocates and checks a page through the native syscall.
        "movq $0, 64(%rsp)\nmovq $4096, 72(%rsp)\n"
        "movq $0x3000, 32(%rsp)\nmovq $4, 40(%rsp)\n"
        "movq $-1, %rcx\nleaq 64(%rsp), %rdx\nxorl %r8d, %r8d\n"
        "leaq 72(%rsp), %r9\ncallq *0(%r15)\n"
        "testl %eax, %eax\njnz 9f\nmovq 64(%rsp), %rax\ntestq %rax, %rax\njz 9f\n"
        "movq $0x12345678, (%rax)\ncmpq $0x12345678, (%rax)\njne 9f\n"
        // Thread A blocks on the first event and later wakes thread B.
        "movq %rbx, %rcx\nxorl %edx, %edx\nxorl %r8d, %r8d\ncallq *8(%r15)\n"
        "testl %eax, %eax\njnz 9f\nmovabsq $0x1111222233334444, %rax\n"
        "cmpq %gs:0x1480, %rax\njne 9f\n"
        "movq %gs:0x58, %rax\nmovq (%rax), %rax\ncmpq $0x1234, (%rax)\njne 9f\n"
        "movq %gs:0x58, %rax\nmovq 8(%rax), %rax\ncmpq $0xabcd, (%rax)\njne 9f\n"
        "cmpq $0, 40(%r14)\njne 10f\n"
        "movq %r12, %rcx\nxorl %edx, %edx\ncallq *16(%r15)\n"
        "testl %eax, %eax\njnz 9f\n10: movl $0xc0000022, %edx\njmp 8f\n"
        // Thread B signals A, then blocks until A signals its event.
        "3: movabsq $0x400020003708, %r10\n39: cmpq $0, (%r10)\nje 39b\n"
        "movq %rbx, %rcx\nxorl %edx, %edx\ncallq *16(%r15)\n"
        "testl %eax, %eax\njnz 9f\n"
        "movq %r12, %rcx\nxorl %edx, %edx\nxorl %r8d, %r8d\ncallq *8(%r15)\n"
        "ud2\n"
        "31: movl $0xc0000101, %edx\njmp 8f\n"
        "33: movl %eax, %edx\njmp 8f\n"
        "34: movl $0xc0000104, %edx\njmp 8f\n"
        "9: movl $0xc000000d, %edx\n"
        "8: movq $-1, %rcx\ncallq *24(%r15)\nud2\n"
        ".globl MicantNativeThreadStart\n"
        "MicantNativeThreadStart: movq $0x12345678, (%rcx)\nmovl $0xc0000022, %eax\nretq\n"
        ".globl MicantApcThreadStart\n"
        // SYSCALL overwrites RCX with the return RIP; retain the thread
        // argument in a Windows-ABI nonvolatile register across NtTestAlert.
        "MicantApcThreadStart: pushq %r12\nmovq %rcx, %r12\n"
        "movl $0x170, %eax\nsyscall\n"
        "movabsq $0x400020003700, %r10\ncmpq $0x41504322, (%r10)\njne 43f\n"
        "movq $0x12345678, (%r12)\nmovl $0xc0000123, %eax\npopq %r12\nretq\n"
        "43: movl $0xc0000022, %eax\npopq %r12\nretq\n"
        ".globl MicantTestUserApc\n"
        "MicantTestUserApc: movabsq $0x400020003700, %rax\nmovq $0x41504321, (%rax)\nretq\n"
        ".globl MicantTestUserApcSecond\n"
        "MicantTestUserApcSecond: movabsq $0x400020003700, %rax\n"
        "cmpq $0x41504321, (%rax)\njne 46f\nmovq $0x41504322, (%rax)\nretq\n"
        "46: movq $0x415043ff, (%rax)\nretq\n"
        ".globl MicantApcFaultThreadStart\n"
        // The invalid APC must fault before this thread can return.
        "MicantApcFaultThreadStart: movl $0x170, %eax\nsyscall\n"
        "xorl %eax, %eax\nretq\n"
        ".globl MicantApcFaultSentinel\n"
        // If a later APC is incorrectly dispatched, its distinct exit status
        // makes that visible to the parent process.
        "MicantApcFaultSentinel: movq $-2, %rcx\nmovq %rcx, %r10\n"
        "movl $0xc0000001, %edx\nmovl $0x53, %eax\nsyscall\nud2\n"
        ".globl MicantGrowStackThreadStart\n"
        "MicantGrowStackThreadStart: movabsq $0x5a5a12345678abcd, %rax\nmovq %rax, %xmm0\n"
        // Probe downward page by page through the initial commit and across
        // multiple guard pages. A single 8 KiB frame remains inside the
        // guaranteed exception-dispatch commit and does not exercise growth.
        "movl $10, %r8d\n1: subq $4096, %rsp\nmovb %r8b, (%rsp)\n"
        "decl %r8d\njnz 1b\naddq $40960, %rsp\n"
        "movq %xmm0, %rdx\ncmpq %rax, %rdx\njne 13f\n"
        "movq %gs:0x10, %rdx\nmovq %rdx, 8(%rcx)\n"
        "movq $0x12345678, (%rcx)\nmovl $0xc0000022, %eax\nretq\n"
        "13: movl $0xc000000d, %eax\nretq\n"
        // A child created with handle inheritance receives the source handle
        // at the same value. The noninheritable handle must remain absent.
        ".globl MicantInheritedEventThreadStart\n"
        "MicantInheritedEventThreadStart: pushq %r12\npushq %r15\nmovq %rcx, %r12\n"
        "movq 0(%r12), %r15\nmovq 16(%r12), %rcx\nxorl %edx, %edx\ncallq *16(%r15)\n"
        "testl %eax, %eax\njnz .Lmicant_inherit_thread_done\n"
        "movq 24(%r12), %rcx\nxorl %edx, %edx\ncallq *16(%r15)\n"
        "cmpl $0xc0000008, %eax\nje .Lmicant_inherit_thread_success\n"
        "movl $0xc0000280, %eax\njmp .Lmicant_inherit_thread_done\n"
        ".Lmicant_inherit_thread_success: xorl %eax, %eax\n"
        ".Lmicant_inherit_thread_done: popq %r15\npopq %r12\nretq\n");
}
__asm__(".section .uthreads$z,\"xr\"\n.globl MicantTwoUserThreadsEnd\n"
        "MicantTwoUserThreadsEnd:\n.text\n");
__attribute__((naked, section(".uthreads$a"))) void MicantRing3ProcessCreateStart() noexcept {
    __asm__ __volatile__(
        "pushq %r12\npushq %r13\npushq %r15\nmovq %rcx, %r12\nmovq 0(%r12), %r15\n"
        "xorl %r13d, %r13d\nsubq $0x180, %rsp\n"
        "movabsq $0x400020003700, %rax\nmovq $0, (%rax)\n"
        "movq $0, 0x60(%rsp)\nmovq $0, 0x68(%rsp)\nmovq $0, 0x70(%rsp)\nmovq $0, 0x78(%rsp)\n"
        // The freestanding Ps namespace is not implemented. A named
        // ObjectAttributes input must fail explicitly before publication.
        "movq $0, 0x130(%rsp)\nmovq $0, 0x138(%rsp)\nmovq $0, 0x140(%rsp)\n"
        "movq $0, 0x148(%rsp)\nmovq $0, 0x150(%rsp)\nmovq $0, 0x158(%rsp)\n"
        "movl $48, 0x130(%rsp)\nmovq $1, 0x140(%rsp)\n"
        "leaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\nleaq 0x130(%rsp), %r8\nmovq $-1, %r9\n"
        "movq $0, 0x20(%rsp)\nmovq $0, 0x28(%rsp)\nmovq $0, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $0, 0x40(%rsp)\ncallq *40(%r15)\n"
        "cmpl $0xc0000002, %eax\nje 27f\nmovl $0xc0000227, %r13d\njmp 90f\n"
        "27: cmpq $0, 0x60(%rsp)\nje 28f\nmovl $0xc0000228, %r13d\njmp 90f\n28:\n"
        // Invalid output addresses must be rejected before process allocation.
        "movq $0x1000, %rcx\nmovl $0x0010043b, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "xorl %eax, %eax\nmovq %rax, 0x20(%rsp)\nmovq %rax, 0x28(%rsp)\n"
        "movq %rax, 0x30(%rsp)\nmovq %rax, 0x38(%rsp)\nmovq %rax, 0x40(%rsp)\n"
        "callq *40(%r15)\ncmpl $0xc0000005, %eax\nje .Lmicant_process_bad_output\n"
        "movl $0xc0000258, %r13d\njmp 90f\n.Lmicant_process_bad_output:\n"
        // Unsupported process-creation options must fail before publishing an
        // object or treating opaque debug/exception ports as live handles.
        "leaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "movq $2, 0x20(%rsp)\nxorl %eax, %eax\nmovq %rax, 0x28(%rsp)\n"
        "movq %rax, 0x30(%rsp)\nmovq %rax, 0x38(%rsp)\nmovq %rax, 0x40(%rsp)\n"
        "callq *40(%r15)\ncmpl $0xc0000002, %eax\nje 68f\nmovl $0xc0000246, %r13d\njmp 90f\n"
        "68: cmpq $0, 0x60(%rsp)\nje 69f\nmovl $0xc0000242, %r13d\njmp 90f\n69:\n"
        // Thread creation preflights its output before stack, TEB, or
        // process-membership resources are published.
        "movq $0x1000, %rcx\nmovl $0x00100052, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "leaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "movq $0, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\nmovq $0, 0x38(%rsp)\n"
        "movq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\nmovq $0, 0x50(%rsp)\n"
        "callq *48(%r15)\ncmpl $0xc0000005, %eax\nje .Lmicant_thread_bad_output\n"
        "movl $0xc0000259, %r13d\njmp 90f\n.Lmicant_thread_bad_output:\n"
        "leaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "movq $0, 0x20(%rsp)\nmovq $0, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $0, 0x40(%rsp)\ncallq *40(%r15)\n"
        "cmpl $0xc0000002, %eax\nje 70f\nmovl $0xc0000247, %r13d\njmp 90f\n"
        "70: cmpq $0, 0x60(%rsp)\nje 71f\nmovl $0xc0000243, %r13d\njmp 90f\n71:\n"
        "leaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "movq $0, 0x30(%rsp)\nmovq $1, 0x38(%rsp)\ncallq *40(%r15)\n"
        "cmpl $0xc0000002, %eax\nje 72f\nmovl $0xc0000248, %r13d\njmp 90f\n"
        "72: cmpq $0, 0x60(%rsp)\nje 73f\nmovl $0xc0000244, %r13d\njmp 90f\n73:\n"
        "leaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "movq $0, 0x38(%rsp)\nmovq $1, 0x40(%rsp)\ncallq *40(%r15)\n"
        "cmpl $0xc0000002, %eax\nje 74f\nmovl $0xc0000249, %r13d\njmp 90f\n"
        "74: cmpq $0, 0x60(%rsp)\nje 75f\nmovl $0xc0000245, %r13d\njmp 90f\n75:\n"
        // A caller-supplied self-relative empty DACL must deny creation and
        // leave the output handle unpublished. SD at rsp+0x160, ACL at +20.
        "xorl %eax, %eax\nmovq %rax, 0x160(%rsp)\nmovq %rax, 0x168(%rsp)\n"
        "movq %rax, 0x170(%rsp)\nmovq %rax, 0x178(%rsp)\n"
        "movb $1, 0x160(%rsp)\nmovw $0x8004, 0x162(%rsp)\n"
        "movl $20, 0x170(%rsp)\nmovb $2, 0x174(%rsp)\nmovw $8, 0x176(%rsp)\n"
        "movl $48, 0x130(%rsp)\nmovq $0, 0x138(%rsp)\nmovq $0, 0x140(%rsp)\n"
        "movl $0, 0x148(%rsp)\nleaq 0x160(%rsp), %rax\nmovq %rax, 0x150(%rsp)\n"
        "movq $0, 0x158(%rsp)\nmovq $0, 0x60(%rsp)\n"
        "leaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\nleaq 0x130(%rsp), %r8\n"
        "movq $-1, %r9\nxorl %eax, %eax\nmovq %rax, 0x20(%rsp)\n"
        "movq %rax, 0x28(%rsp)\nmovq %rax, 0x30(%rsp)\n"
        "movq %rax, 0x38(%rsp)\nmovq %rax, 0x40(%rsp)\ncallq *40(%r15)\n"
        "cmpl $0xc0000022, %eax\nje .Lmicant_process_sd_denied\n"
        "movl $0xc0000250, %r13d\njmp 90f\n.Lmicant_process_sd_denied:\n"
        "cmpq $0, 0x60(%rsp)\nje .Lmicant_process_sd_unpublished\n"
        "movl $0xc0000251, %r13d\njmp 90f\n.Lmicant_process_sd_unpublished:\n"
        // The same caller-supplied empty DACL must deny a real CPL3
        // NtCreateThreadEx request before publishing its output handle.
        "movq $0, 0x78(%rsp)\nleaq 0x78(%rsp), %rcx\nmovl $0x00100052, %edx\n"
        "leaq 0x130(%rsp), %r8\nmovq $-1, %r9\n"
        "leaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "leaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\n"
        "movq $0, 0x50(%rsp)\ncallq *48(%r15)\n"
        "cmpl $0xc0000022, %eax\nje .Lmicant_thread_sd_denied\n"
        "movl $0xc0000254, %r13d\njmp 90f\n.Lmicant_thread_sd_denied:\n"
        "cmpq $0, 0x78(%rsp)\nje .Lmicant_thread_sd_unpublished\n"
        "movl $0xc0000255, %r13d\njmp 90f\n.Lmicant_thread_sd_unpublished:\n"
        // Restore the deliberately named ObjectAttributes used by the later
        // NtCreateThreadEx namespace rejection case.
        "xorl %eax, %eax\nmovq %rax, 0x130(%rsp)\nmovq %rax, 0x138(%rsp)\n"
        "movq %rax, 0x140(%rsp)\nmovq %rax, 0x148(%rsp)\n"
        "movq %rax, 0x150(%rsp)\nmovq %rax, 0x158(%rsp)\n"
        "movl $48, 0x130(%rsp)\nmovq $1, 0x140(%rsp)\n"
        "leaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "movq $0, 0x20(%rsp)\nmovq $0, 0x28(%rsp)\nmovq $0, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $0, 0x40(%rsp)\ncallq *40(%r15)\n"
        "testl %eax, %eax\njz 1f\nmovl %eax, %r13d\njmp 90f\n1:\n"
        "movq 0x60(%rsp), %rcx\nxorl %edx, %edx\nleaq 0x80(%rsp), %r8\nmovl $48, %r9d\n"
        "leaq 0x120(%rsp), %rax\nmovq %rax, 0x20(%rsp)\ncallq *80(%r15)\n"
        "testl %eax, %eax\njz 2f\nmovl $0xc0000212, %r13d\njmp 90f\n2:\n"
        "cmpl $48, 0x120(%rsp)\nje 3f\nmovl $0xc0000201, %r13d\njmp 90f\n3:\n"
        "cmpq $0, 0x88(%rsp)\njne 4f\nmovl $0xc0000202, %r13d\njmp 90f\n4:\n"
        "movq %gs:0x40, %rax\ncmpq %rax, 0xa0(%rsp)\njne 5f\nmovl $0xc0000203, %r13d\njmp 90f\n5:\n"
        "movq 0xa0(%rsp), %rax\nmovq %rax, 0xe0(%rsp)\nmovq $0, 0xe8(%rsp)\n"
        "movq $0, 0xf0(%rsp)\nmovq $0, 0xf8(%rsp)\nmovq $0, 0x100(%rsp)\n"
        "movq $0, 0x108(%rsp)\nmovq $0, 0x110(%rsp)\nmovq $0, 0x118(%rsp)\nmovl $48, 0xf0(%rsp)\n"
        "leaq 0x68(%rsp), %rcx\nmovl $0x400, %edx\nleaq 0xf0(%rsp), %r8\nleaq 0xe0(%rsp), %r9\n"
        "callq *64(%r15)\ntestl %eax, %eax\njz 6f\nmovl $0xc0000213, %r13d\njmp 90f\n6:\n"
        // NtDuplicateObject must cross the CPL3 syscall boundary, preserve
        // the source grant for DUPLICATE_SAME_ACCESS, and publish only a live
        // target handle. The next calls cover access denial, an invalid
        // source and an unwritable output pointer.
        "movq $0, 0x78(%rsp)\nmovq $-1, %rcx\nmovq 0x68(%rsp), %rdx\n"
        "movq $-1, %r8\nleaq 0x78(%rsp), %r9\nxorl %eax, %eax\n"
        "movq %rax, 0x20(%rsp)\nmovq $2, 0x28(%rsp)\nmovq $2, 0x30(%rsp)\n"
        "callq *144(%r15)\ntestl %eax, %eax\njz .Lmicant_dup_ok\n"
        "movl $0xc0000271, %r13d\njmp 90f\n.Lmicant_dup_ok:\n"
        "cmpq $0, 0x78(%rsp)\njne .Lmicant_dup_handle\nmovl $0xc0000272, %r13d\njmp 90f\n"
        ".Lmicant_dup_handle:\nmovq 0x78(%rsp), %rcx\ncallq *88(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_dup_access_denied\nmovl %eax, %r13d\njmp 90f\n"
        ".Lmicant_dup_access_denied:\nmovq $0, 0x78(%rsp)\nmovq $-1, %rcx\n"
        "movq 0x68(%rsp), %rdx\nmovq $-1, %r8\nleaq 0x78(%rsp), %r9\n"
        "movq $1, 0x20(%rsp)\nxorl %eax, %eax\nmovq %rax, 0x28(%rsp)\n"
        "movq %rax, 0x30(%rsp)\ncallq *144(%r15)\ncmpl $0xc0000022, %eax\nje .Lmicant_dup_bad_source\n"
        "movl $0xc0000274, %r13d\njmp 90f\n.Lmicant_dup_bad_source:\n"
        "cmpq $0, 0x78(%rsp)\nje .Lmicant_dup_invalid_source\nmovl $0xc0000275, %r13d\njmp 90f\n"
        ".Lmicant_dup_invalid_source:\nmovq $0, 0x78(%rsp)\nmovq $-1, %rcx\n"
        "movq $0x7fffffff, %rdx\nmovq $-1, %r8\nleaq 0x78(%rsp), %r9\n"
        "xorl %eax, %eax\nmovq %rax, 0x20(%rsp)\nmovq %rax, 0x28(%rsp)\nmovq $2, 0x30(%rsp)\n"
        "callq *144(%r15)\ncmpl $0xc0000008, %eax\nje .Lmicant_dup_copyout\n"
        "movl $0xc0000276, %r13d\njmp 90f\n.Lmicant_dup_copyout:\n"
        "movq $-1, %rcx\nmovq 0x68(%rsp), %rdx\nmovq $-1, %r8\nmovq $0x1000, %r9\n"
        "movq $0, 0x20(%rsp)\nmovq $0, 0x28(%rsp)\nmovq $2, 0x30(%rsp)\n"
        "callq *144(%r15)\ncmpl $0xc0000005, %eax\nje .Lmicant_duplicate_complete\n"
        "movl $0xc0000277, %r13d\njmp 90f\n.Lmicant_duplicate_complete:\n"
        // DUPLICATE_CLOSE_SOURCE retires the source after a successful copy,
        // and also closes it when target access validation fails.
        "movq $0, 0x78(%rsp)\nmovq $-1, %rcx\nmovq 0x68(%rsp), %rdx\nmovq $-1, %r8\n"
        "leaq 0x78(%rsp), %r9\nxorl %eax, %eax\nmovq %rax, 0x20(%rsp)\n"
        "movq %rax, 0x28(%rsp)\nmovq $3, 0x30(%rsp)\ncallq *144(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_dup_close_source_success\nmovl $0xc0000284, %r13d\njmp 90f\n"
        ".Lmicant_dup_close_source_success:\ncmpq $0, 0x78(%rsp)\njne .Lmicant_dup_close_source_value\n"
        "movl $0xc0000285, %r13d\njmp 90f\n.Lmicant_dup_close_source_value:\n"
        "movq 0x78(%rsp), %rcx\ncallq *88(%r15)\ntestl %eax, %eax\njz .Lmicant_dup_close_source_test\n"
        "movl $0xc0000286, %r13d\njmp 90f\n.Lmicant_dup_close_source_test:\n"
        "movq $0, 0x100(%rsp)\nmovq 0x68(%rsp), %rcx\nxorl %edx, %edx\n"
        "leaq 0x100(%rsp), %r8\ncallq *8(%r15)\ncmpl $0xc0000008, %eax\nje .Lmicant_dup_close_source_reopen\n"
        "movl $0xc0000287, %r13d\njmp 90f\n.Lmicant_dup_close_source_reopen:\n"
        "leaq 0x68(%rsp), %rcx\nmovl $0x400, %edx\nleaq 0xf0(%rsp), %r8\nleaq 0xe0(%rsp), %r9\n"
        "callq *64(%r15)\ntestl %eax, %eax\njz .Lmicant_dup_close_source_failed\nmovl $0xc0000288, %r13d\njmp 90f\n"
        ".Lmicant_dup_close_source_failed:\nmovq $0, 0x78(%rsp)\nmovq $-1, %rcx\n"
        "movq 0x68(%rsp), %rdx\nmovq $-1, %r8\nleaq 0x78(%rsp), %r9\n"
        "movq $1, 0x20(%rsp)\nxorl %eax, %eax\nmovq %rax, 0x28(%rsp)\n"
        "movq $1, 0x30(%rsp)\ncallq *144(%r15)\ncmpl $0xc0000022, %eax\nje .Lmicant_dup_close_source_failed_status\n"
        "movl $0xc0000289, %r13d\njmp 90f\n.Lmicant_dup_close_source_failed_status:\n"
        "cmpq $0, 0x78(%rsp)\nje .Lmicant_dup_close_source_failed_wait\nmovl $0xc000028a, %r13d\njmp 90f\n"
        ".Lmicant_dup_close_source_failed_wait:\nmovq $0, 0x100(%rsp)\nmovq 0x68(%rsp), %rcx\n"
        "xorl %edx, %edx\nleaq 0x100(%rsp), %r8\ncallq *8(%r15)\n"
        "cmpl $0xc0000008, %eax\nje .Lmicant_dup_close_source_reopen2\nmovl $0xc000028b, %r13d\njmp 90f\n"
        ".Lmicant_dup_close_source_reopen2:\nleaq 0x68(%rsp), %rcx\nmovl $0x400, %edx\n"
        "leaq 0xf0(%rsp), %r8\nleaq 0xe0(%rsp), %r9\ncallq *64(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_dup_close_source_complete\nmovl $0xc000028c, %r13d\njmp 90f\n"
        ".Lmicant_dup_close_source_complete:\n"
        // Exercise OBJ_INHERIT through the real process-create syscall. The
        // child sets the inherited event after the parent closes its source
        // handle, then confirms a second, noninheritable event is absent.
        "movq $0, 0x70(%rsp)\nmovq $0, 0x78(%rsp)\nmovq $0, 0x80(%rsp)\n"
        "movq $0, 0x88(%rsp)\nmovq $0, 0x90(%rsp)\nmovq $0, 0x98(%rsp)\n"
        "xorl %eax, %eax\nmovq %rax, 0x130(%rsp)\nmovq %rax, 0x138(%rsp)\n"
        "movq %rax, 0x140(%rsp)\nmovq %rax, 0x148(%rsp)\nmovq %rax, 0x150(%rsp)\n"
        "movq %rax, 0x158(%rsp)\nmovl $48, 0x130(%rsp)\nmovl $2, 0x140(%rsp)\n"
        "leaq 0x78(%rsp), %rcx\nmovl $0x00100002, %edx\nleaq 0x130(%rsp), %r8\nxorl %r9d, %r9d\n"
        "movq $0, 0x20(%rsp)\ncallq *152(%r15)\ntestl %eax, %eax\njz .Lmicant_inherit_first_event\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_first_event:\n"
        "movl $0, 0x140(%rsp)\nleaq 0x70(%rsp), %rcx\nmovl $0x00100002, %edx\n"
        "leaq 0x130(%rsp), %r8\nxorl %r9d, %r9d\nmovq $0, 0x20(%rsp)\n"
        "callq *152(%r15)\ntestl %eax, %eax\njz .Lmicant_inherit_second_event\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_second_event:\n"
        // Keep a parent wait reference to the inheritable event; do not mark
        // that duplicate inheritable itself.
        "leaq 0x80(%rsp), %r9\nmovq $-1, %rcx\nmovq 0x78(%rsp), %rdx\nmovq $-1, %r8\n"
        "xorl %eax, %eax\nmovq %rax, 0x20(%rsp)\nmovq %rax, 0x28(%rsp)\nmovq $2, 0x30(%rsp)\n"
        "callq *144(%r15)\ntestl %eax, %eax\njz .Lmicant_inherit_observer\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_observer:\n"
        "movq 0x78(%rsp), %rax\nmovq %rax, 16(%r12)\nmovq 0x70(%rsp), %rax\nmovq %rax, 24(%r12)\n"
        "leaq 0x88(%rsp), %rcx\nmovl $0x00100003, %edx\nxorl %r8d, %r8d\nmovq $-1, %r9\n"
        "movq $4, 0x20(%rsp)\nxorl %eax, %eax\nmovq %rax, 0x28(%rsp)\nmovq %rax, 0x30(%rsp)\n"
        "movq %rax, 0x38(%rsp)\nmovq %rax, 0x40(%rsp)\ncallq *40(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_inherit_child_created\nmovl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n"
        ".Lmicant_inherit_child_created:\nmovq 0x78(%rsp), %rcx\ncallq *88(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_inherit_source_closed\nmovl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n"
        ".Lmicant_inherit_source_closed:\nmovq $0, 0x78(%rsp)\nmovq $0, 0x100(%rsp)\n"
        "movq 16(%r12), %rcx\nxorl %edx, %edx\nleaq 0x100(%rsp), %r8\ncallq *8(%r15)\n"
        "cmpl $0xc0000008, %eax\nje .Lmicant_inherit_source_invalid\nmovl $0xc0000281, %r13d\njmp .Lmicant_inherit_cleanup\n"
        ".Lmicant_inherit_source_invalid:\nleaq 0x90(%rsp), %rcx\nmovl $0x00100000, %edx\nxorl %r8d, %r8d\n"
        "movq 0x88(%rsp), %r9\nleaq MicantInheritedEventThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "movq %r12, %rax\nmovq %rax, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\n"
        "movq $0, 0x50(%rsp)\ncallq *48(%r15)\ntestl %eax, %eax\njz .Lmicant_inherit_thread_created\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_thread_created:\n"
        "leaq 0x98(%rsp), %rdx\nmovq 0x90(%rsp), %rcx\ncallq *56(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_inherit_resumed\nmovl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n"
        ".Lmicant_inherit_resumed:\ncmpq $1, 0x98(%rsp)\nje .Lmicant_inherit_wait_thread\n"
        "movl $0xc0000282, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_wait_thread:\n"
        "movq $-10000000, 0x100(%rsp)\nmovq 0x90(%rsp), %rcx\nxorl %edx, %edx\n"
        "leaq 0x100(%rsp), %r8\ncallq *8(%r15)\ntestl %eax, %eax\njz .Lmicant_inherit_wait_event\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_wait_event:\n"
        "movq $-10000000, 0x100(%rsp)\nmovq 0x80(%rsp), %rcx\nxorl %edx, %edx\n"
        "leaq 0x100(%rsp), %r8\ncallq *8(%r15)\ntestl %eax, %eax\njz .Lmicant_inherit_noninherit\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_noninherit:\n"
        "movq $0, 0x100(%rsp)\nmovq 0x70(%rsp), %rcx\nxorl %edx, %edx\n"
        "leaq 0x100(%rsp), %r8\ncallq *8(%r15)\ncmpl $0x102, %eax\nje .Lmicant_inherit_cleanup_success\n"
        "movl $0xc0000283, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_cleanup_success:\n"
        "movq 0x90(%rsp), %rcx\ncallq *88(%r15)\nmovq $0, 0x90(%rsp)\n"
        "movq 0x70(%rsp), %rcx\ncallq *88(%r15)\nmovq $0, 0x70(%rsp)\n"
        "movq 0x80(%rsp), %rcx\ncallq *88(%r15)\nmovq $0, 0x80(%rsp)\n"
        "movq 0x88(%rsp), %rcx\nmovl $0, %edx\ncallq *24(%r15)\n"
        "testl %eax, %eax\njz .Lmicant_inherit_terminated\ncmp $0xc000010a, %eax\nje .Lmicant_inherit_terminated\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_terminated:\n"
        "movq $-10000000, 0x100(%rsp)\nmovq 0x88(%rsp), %rcx\nxorl %edx, %edx\n"
        "leaq 0x100(%rsp), %r8\ncallq *8(%r15)\ntestl %eax, %eax\njz .Lmicant_inherit_close_child\n"
        "movl %eax, %r13d\njmp .Lmicant_inherit_cleanup\n.Lmicant_inherit_close_child:\n"
        "movq 0x88(%rsp), %rcx\ncallq *88(%r15)\nmovq $0, 0x88(%rsp)\n"
        "xorl %r13d, %r13d\njmp .Lmicant_inherit_done\n"
        ".Lmicant_inherit_cleanup:\nmovq 0x88(%rsp), %rcx\ntestq %rcx, %rcx\njz .Lmicant_inherit_cleanup_thread\n"
        "movl $0, %edx\ncallq *24(%r15)\n.Lmicant_inherit_cleanup_thread:\n"
        "movq 0x90(%rsp), %rcx\ntestq %rcx, %rcx\njz .Lmicant_inherit_cleanup_child\n"
        "callq *88(%r15)\n.Lmicant_inherit_cleanup_child:\nmovq 0x88(%rsp), %rcx\n"
        "testq %rcx, %rcx\njz .Lmicant_inherit_cleanup_observer\ncallq *88(%r15)\n"
        ".Lmicant_inherit_cleanup_observer:\nmovq 0x80(%rsp), %rcx\ntestq %rcx, %rcx\n"
        "jz .Lmicant_inherit_cleanup_noninherit\ncallq *88(%r15)\n"
        ".Lmicant_inherit_cleanup_noninherit:\nmovq 0x70(%rsp), %rcx\ntestq %rcx, %rcx\njz .Lmicant_inherit_cleanup_source\n"
        "callq *88(%r15)\n.Lmicant_inherit_cleanup_source:\nmovq 0x78(%rsp), %rcx\n"
        "testq %rcx, %rcx\njz .Lmicant_inherit_done\ncallq *88(%r15)\n"
        ".Lmicant_inherit_done:\n"
        // The opened parent handle grants query access only; process creation
        // must require PROCESS_CREATE_PROCESS and publish no child on denial.
        "leaq 0x78(%rsp), %rcx\nmovq $0, 0x78(%rsp)\nmovl $0x0010043b, %edx\n"
        "xorl %r8d, %r8d\nmovq 0x68(%rsp), %r9\nxorl %eax, %eax\n"
        "movq %rax, 0x20(%rsp)\nmovq %rax, 0x28(%rsp)\nmovq %rax, 0x30(%rsp)\n"
        "movq %rax, 0x38(%rsp)\nmovq %rax, 0x40(%rsp)\ncallq *40(%r15)\n"
        "cmpl $0xc0000022, %eax\nje 76f\nmovl $0xc000024a, %r13d\njmp 90f\n"
        "76: cmpq $0, 0x78(%rsp)\nje 77f\nmovl $0xc000024b, %r13d\njmp 90f\n77:\n"
        // NtCreateThreadEx must reject unsupported named Ps objects before
        // it references the restricted process handle or publishes a handle.
        "leaq 0x70(%rsp), %rcx\nmovl $0x00100042, %edx\nleaq 0x130(%rsp), %r8\n"
        "movq 0x68(%rsp), %r9\nleaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "leaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\nmovq $0, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\n"
        "movq $0, 0x50(%rsp)\ncallq *48(%r15)\ncmpl $0xc0000002, %eax\nje 29f\n"
        "movl $0xc0000229, %r13d\njmp 90f\n29: cmpq $0, 0x70(%rsp)\nje 30f\n"
        "movl $0xc000022a, %r13d\njmp 90f\n30:\n"
        // Unsupported NtCreateThreadEx flags must fail before publishing an
        // object handle.
        "movq $0, 0x70(%rsp)\nleaq 0x70(%rsp), %rcx\nmovl $0x00100042, %edx\n"
        "xorl %r8d, %r8d\nmovq 0x60(%rsp), %r9\n"
        "leaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "leaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\nmovq $2, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\n"
        "movq $0, 0x50(%rsp)\ncallq *48(%r15)\ncmpl $0xc0000002, %eax\nje 31f\n"
        "movl $0xc0000260, %r13d\njmp 90f\n31: cmpq $0, 0x70(%rsp)\nje 32f\n"
        "movl $0xc0000261, %r13d\njmp 90f\n32:\n"
        // Exercise stack reserve/commit validation through real CPL3 syscalls.
        // A rejected request must leave its output handle unpublished.
        "movq $0, 0x70(%rsp)\nleaq 0x70(%rsp), %rcx\nmovl $0x00100042, %edx\n"
        "xorl %r8d, %r8d\nmovq 0x60(%rsp), %r9\n"
        "leaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "leaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $8192, 0x40(%rsp)\nmovq $4096, 0x48(%rsp)\n"
        "movq $0, 0x50(%rsp)\ncallq *48(%r15)\ncmpl $0xc000000d, %eax\nje 50f\n"
        "movl %eax, %r13d\njmp 90f\n50: cmpq $0, 0x70(%rsp)\nje 51f\n"
        "movl $0xc000023c, %r13d\njmp 90f\n51:\n"
        "movq $0, 0x70(%rsp)\nleaq 0x70(%rsp), %rcx\nmovl $0x00100042, %edx\n"
        "xorl %r8d, %r8d\nmovq 0x60(%rsp), %r9\n"
        "leaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "leaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $0x1001000, 0x48(%rsp)\n"
        "movq $0, 0x50(%rsp)\ncallq *48(%r15)\ncmpl $0xc000000d, %eax\nje 52f\n"
        "movl %eax, %r13d\njmp 90f\n52: cmpq $0, 0x70(%rsp)\nje 53f\n"
        "movl $0xc000023d, %r13d\njmp 90f\n53:\n"
        // Stack rounding must reject size_t overflow without publishing an
        // object handle.
        "movq $0, 0x70(%rsp)\nleaq 0x70(%rsp), %rcx\nmovl $0x00100042, %edx\n"
        "xorl %r8d, %r8d\nmovq 0x60(%rsp), %r9\n"
        "leaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "leaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovabsq $0xfffffffffffffff0, %rax\n"
        "movq %rax, 0x40(%rsp)\nmovq %rax, 0x48(%rsp)\nmovq $0, 0x50(%rsp)\n"
        "callq *48(%r15)\ncmpl $0xc000000d, %eax\nje 54f\n"
        "movl $0xc0000262, %r13d\njmp 90f\n54: cmpq $0, 0x70(%rsp)\nje 55f\n"
        "movl $0xc0000263, %r13d\njmp 90f\n55:\n"
        "leaq 0x70(%rsp), %rcx\nmovl $0x00100042, %edx\nxorl %r8d, %r8d\nmovq 0x68(%rsp), %r9\n"
        "leaq MicantNativeThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\nleaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\n"
        "movq $1, 0x30(%rsp)\nmovq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\nmovq $0, 0x50(%rsp)\n"
        "callq *48(%r15)\ncmpl $0xc0000022, %eax\nje 7f\nmovl $0xc0000204, %r13d\njmp 90f\n7:\n"
        "movq 0x68(%rsp), %rcx\ncallq *88(%r15)\ntestl %eax, %eax\njz 8f\nmovl $0xc0000214, %r13d\njmp 90f\n8:\n"
        "leaq 0x70(%rsp), %rcx\nmovl $0x00100052, %edx\nxorl %r8d, %r8d\nmovq 0x60(%rsp), %r9\n"
        "leaq MicantApcThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\nleaq 16(%r12), %rax\nmovq %rax, 0x28(%rsp)\n"
        "movq $1, 0x30(%rsp)\nmovq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\nmovq $0, 0x50(%rsp)\n"
        "callq *48(%r15)\ntestl %eax, %eax\njz 9f\nmovl $0xc0000215, %r13d\njmp 90f\n9:\n"
        // A zero-time wait on the newly created suspended thread must time
        // out and leave it available for APC setup and resume.
        "movq $0, 0x100(%rsp)\nmovq 0x70(%rsp), %rcx\nxorl %edx, %edx\n"
        "leaq 0x100(%rsp), %r8\ncallq *8(%r15)\n"
        "cmpl $0x00000102, %eax\nje 33f\nmovl $0xc0000264, %r13d\njmp 90f\n33:\n"
        // Queue a user APC to the suspended child thread before its first
        // user instruction; NtTestAlert at the entry requests delivery.
        "movq 0x70(%rsp), %rcx\nleaq MicantTestUserApc(%rip), %rdx\nxorl %r8d, %r8d\n"
        "xorl %r9d, %r9d\nmovq $0, 0x20(%rsp)\ncallq *120(%r15)\n"
        "testl %eax, %eax\njz 25f\nmovl $0xc0000225, %r13d\njmp 90f\n25:\n"
        "movq 0x70(%rsp), %rcx\nleaq MicantTestUserApcSecond(%rip), %rdx\n"
        "xorl %r8d, %r8d\nxorl %r9d, %r9d\nmovq $0, 0x20(%rsp)\ncallq *120(%r15)\n"
        "testl %eax, %eax\njz 32f\nmovl $0xc000022b, %r13d\njmp 90f\n32:\n"
        "movq 0x70(%rsp), %rcx\nxorl %edx, %edx\ncallq *56(%r15)\ntestl %eax, %eax\njz 10f\nmovl $0xc0000216, %r13d\njmp 90f\n10:\n"
        "movq 0x70(%rsp), %rcx\nxorl %edx, %edx\nxorl %r8d, %r8d\ncallq *8(%r15)\ntestl %eax, %eax\njz 11f\nmovl $0xc0000217, %r13d\njmp 90f\n11:\n"
        "movq 0x70(%rsp), %rcx\nxorl %edx, %edx\nleaq 0xb0(%rsp), %r8\nmovl $48, %r9d\n"
        "leaq 0x120(%rsp), %rax\nmovq %rax, 0x20(%rsp)\ncallq *32(%r15)\ntestl %eax, %eax\njz 12f\nmovl $0xc0000218, %r13d\njmp 90f\n12:\n"
        "cmpl $0xc0000123, 0xb0(%rsp)\nje 26f\nmovl $0xc0000226, %r13d\njmp 90f\n26:\n"
        "cmpq $0, 0xb8(%rsp)\njne 13f\nmovl $0xc0000205, %r13d\njmp 90f\n13:\n"
        "movq 0xc0(%rsp), %rax\ncmpq 0xa0(%rsp), %rax\nje 14f\nmovl $0xc0000206, %r13d\njmp 90f\n14:\n"
        "leaq 0x78(%rsp), %rcx\nmovl $0x40, %edx\nleaq 0xf0(%rsp), %r8\nleaq 0xc0(%rsp), %r9\ncallq *72(%r15)\n"
        "testl %eax, %eax\njz 15f\nmovl %eax, %r13d\njmp 90f\n15:\n"
        "movq 0x78(%rsp), %rcx\ncallq *88(%r15)\ntestl %eax, %eax\njz 16f\nmovl $0xc000021a, %r13d\njmp 90f\n16:\n"
        "movq 0x70(%rsp), %rcx\ncallq *88(%r15)\ntestl %eax, %eax\njz 17f\nmovl $0xc000021b, %r13d\njmp 90f\n17:\n"
        "movq 0x60(%rsp), %rcx\nxorl %edx, %edx\ncallq *24(%r15)\ntestl %eax, %eax\njz 18f\n"
        "cmpl $0xc000010a, %eax\nje 18f\nmovl $0xc000021c, %r13d\njmp 90f\n18:\n"
        "movq 0x60(%rsp), %rcx\ncallq *88(%r15)\ntestl %eax, %eax\njz 19f\nmovl $0xc000021d, %r13d\njmp 90f\n19:\n"
        // A separate child lets the current Ring 3 test process observe a
        // callback fault that terminates its target process. Queue an invalid
        // APC followed by a sentinel that would terminate with another status.
        "movq $0, 0x60(%rsp)\nleaq 0x60(%rsp), %rcx\nmovl $0x0010043b, %edx\n"
        "xorl %r8d, %r8d\nmovq $-1, %r9\nxorl %eax, %eax\n"
        "movq %rax, 0x20(%rsp)\nmovq %rax, 0x28(%rsp)\nmovq %rax, 0x30(%rsp)\n"
        "movq %rax, 0x38(%rsp)\nmovq %rax, 0x40(%rsp)\ncallq *40(%r15)\n"
        "testl %eax, %eax\njz 54f\nmovl %eax, %r13d\njmp 90f\n54: cmpq $0, 0x60(%rsp)\njne 55f\n"
        "movl $0xc000023e, %r13d\njmp 90f\n55:\n"
        "movq $0, 0x70(%rsp)\nleaq 0x70(%rsp), %rcx\nmovl $0x00100052, %edx\n"
        "xorl %r8d, %r8d\nmovq 0x60(%rsp), %r9\n"
        "leaq MicantApcFaultThreadStart(%rip), %rax\nmovq %rax, 0x20(%rsp)\n"
        "xorl %eax, %eax\nmovq %rax, 0x28(%rsp)\nmovq $1, 0x30(%rsp)\n"
        "movq $0, 0x38(%rsp)\nmovq $4096, 0x40(%rsp)\nmovq $32768, 0x48(%rsp)\n"
        "movq $0, 0x50(%rsp)\ncallq *48(%r15)\ntestl %eax, %eax\njz 56f\n"
        "movl %eax, %r13d\njmp 90f\n56: cmpq $0, 0x70(%rsp)\njne 57f\n"
        "movl $0xc000023f, %r13d\njmp 90f\n57:\n"
        "movq 0x70(%rsp), %rcx\nmovq $1, %rdx\nxorl %r8d, %r8d\nxorl %r9d, %r9d\n"
        "movq $0, 0x20(%rsp)\ncallq *120(%r15)\ntestl %eax, %eax\njz 58f\n"
        "movl %eax, %r13d\njmp 90f\n58:\n"
        "movq 0x70(%rsp), %rcx\nleaq MicantApcFaultSentinel(%rip), %rdx\n"
        "xorl %r8d, %r8d\nxorl %r9d, %r9d\nmovq $0, 0x20(%rsp)\n"
        "callq *120(%r15)\ntestl %eax, %eax\njz 59f\nmovl %eax, %r13d\njmp 90f\n59:\n"
        "movq $0, 0x58(%rsp)\nmovq 0x70(%rsp), %rcx\nleaq 0x58(%rsp), %rdx\n"
        "callq *56(%r15)\ntestl %eax, %eax\njz 60f\nmovl %eax, %r13d\njmp 90f\n60:\n"
        "cmpl $1, 0x58(%rsp)\nje 61f\nmovl $0xc0000240, %r13d\njmp 90f\n61:\n"
        "movq 0x70(%rsp), %rcx\nxorl %edx, %edx\nxorl %r8d, %r8d\ncallq *8(%r15)\n"
        "testl %eax, %eax\njz 62f\nmovl %eax, %r13d\njmp 90f\n62:\n"
        "movq 0x60(%rsp), %rcx\nxorl %edx, %edx\nxorl %r8d, %r8d\ncallq *8(%r15)\n"
        "testl %eax, %eax\njz 63f\nmovl %eax, %r13d\njmp 90f\n63:\n"
        "movq 0x70(%rsp), %rcx\nxorl %edx, %edx\nleaq 0x80(%rsp), %r8\nmovl $48, %r9d\n"
        "leaq 0x120(%rsp), %rax\nmovq %rax, 0x20(%rsp)\ncallq *32(%r15)\n"
        "testl %eax, %eax\njz 64f\nmovl %eax, %r13d\njmp 90f\n64:\n"
        "cmpl $0xc0000005, 0x80(%rsp)\nje 65f\nmovl $0xc0000241, %r13d\njmp 90f\n65:\n"
        "movq 0x70(%rsp), %rcx\ncallq *88(%r15)\ntestl %eax, %eax\njz 66f\n"
        "movl %eax, %r13d\njmp 90f\n66: movq 0x60(%rsp), %rcx\ncallq *88(%r15)\n"
        "testl %eax, %eax\njz 67f\nmovl %eax, %r13d\njmp 90f\n67:\n"
        // Exercise the exported NTDLL NtTerminateThread wrapper from CPL3.
        // The status and marker are observed by the kernel waiter after the
        // syscall switches away; returning from the wrapper is a fixture bug.
        "movq $0x12345678, 8(%r12)\nmovq $-2, %rcx\nmovl $0xc0000022, %edx\n"
        "callq *136(%r15)\nmovl $0xc0000270, 8(%r12)\nxorl %r13d, %r13d\njmp 91f\n"
        "90: movl %r13d, 8(%r12)\n"
        "movq 0x60(%rsp), %rcx\ntestq %rcx, %rcx\njz 20f\nxorl %edx, %edx\ncallq *24(%r15)\n"
        "movq 0x78(%rsp), %rcx\ntestq %rcx, %rcx\njz 21f\ncallq *88(%r15)\n21:\n"
        "movq 0x70(%rsp), %rcx\ntestq %rcx, %rcx\njz 22f\ncallq *88(%r15)\n22:\n"
        "movq 0x68(%rsp), %rcx\ntestq %rcx, %rcx\njz 20f\ncallq *88(%r15)\n20:\n"
        "91: addq $0x180, %rsp\npopq %r15\npopq %r13\npopq %r12\nmovl $0xc0000022, %eax\nretq\n");
}
}

namespace {
__attribute__((noinline)) bool testNativePeerThread(micant::Handle process, uintptr_t entry) noexcept {
    using namespace micant;
    Handle peer{};
    ClientId client{};
    uintptr_t teb{};
    size_t clientLength{}, tebLength{};
    struct Attributes { size_t totalLength; PsAttribute entries[2]; } attrs{
        sizeof(Attributes), {{PS_ATTRIBUTE_CLIENT_ID, sizeof(client), reinterpret_cast<uintptr_t>(&client), &clientLength},
                             {PS_ATTRIBUTE_TEB_ADDRESS, sizeof(teb), reinterpret_cast<uintptr_t>(&teb), &tebLength}}};
    Handle restricted{};
    const auto duplicateStatus = ob::detail::currentHandleTable().duplicateHandle(process,
        ob::detail::currentHandleTable(), restricted, ob::SYNCHRONIZE);
    const auto restrictedCreateStatus = duplicateStatus == NtStatus::Success
        ? sys::NtCreateThreadEx(&peer, ob::SYNCHRONIZE, nullptr, restricted,
            entry, 0, 0, 0, mm::PageSize4KB, mm::PageSize4KB, nullptr)
        : duplicateStatus;
    const auto restrictedCloseStatus = restricted ? sys::NtClose(restricted) : NtStatus::Success;
    const auto threadObjectsBeforeStackValidation = ps::detail::bootstrapThreadObjectCount();
    const auto readyThreadsBeforeStackValidation = ke::PriorityScheduler::get().getReadyThreadCount();
    Handle invalidCommitThread{};
    const auto invalidCommitStatus = sys::NtCreateThreadEx(&invalidCommitThread,
        ob::SYNCHRONIZE, nullptr, process, entry, 0, 1, 0,
        2 * mm::PageSize4KB, mm::PageSize4KB, nullptr);
    Handle oversizedReserveThread{};
    const auto oversizedReserveStatus = sys::NtCreateThreadEx(&oversizedReserveThread,
        ob::SYNCHRONIZE, nullptr, process, entry, 0, 1, 0,
        mm::PageSize4KB, hal::amd64::BootstrapManagedSize + mm::PageSize4KB, nullptr);
    auto malformedAttributes = attrs;
    malformedAttributes.entries[1].attribute = PS_ATTRIBUTE_CLIENT_ID;
    Handle malformedAttributeThread{};
    const auto threadObjectsBeforeAttributeValidation = ps::detail::bootstrapThreadObjectCount();
    const auto readyThreadsBeforeAttributeValidation = ke::PriorityScheduler::get().getReadyThreadCount();
    const auto malformedAttributeStatus = sys::NtCreateThreadEx(&malformedAttributeThread,
        ob::SYNCHRONIZE, nullptr, process, entry, 0, 1, 0,
        mm::PageSize4KB, 32768, &malformedAttributes);
    if (duplicateStatus != NtStatus::Success || restrictedCreateStatus != NtStatus::AccessDenied ||
        restrictedCloseStatus != NtStatus::Success ||
        invalidCommitStatus != NtStatus::InvalidParameter || invalidCommitThread ||
        oversizedReserveStatus != NtStatus::InvalidParameter || oversizedReserveThread ||
        ps::detail::bootstrapThreadObjectCount() != threadObjectsBeforeStackValidation ||
        ke::PriorityScheduler::get().getReadyThreadCount() != readyThreadsBeforeStackValidation ||
        malformedAttributeStatus != NtStatus::InvalidParameter || malformedAttributeThread ||
        ps::detail::bootstrapThreadObjectCount() != threadObjectsBeforeAttributeValidation ||
        ke::PriorityScheduler::get().getReadyThreadCount() != readyThreadsBeforeAttributeValidation) {
        serialWrite("[MicaNT Ps] NtCreateThreadEx stack validation status/handle/count/ready=");
        serialWriteHex(static_cast<uint32_t>(invalidCommitStatus)); serialWrite("/");
        serialWriteHex(static_cast<uint32_t>(oversizedReserveStatus)); serialWrite("/");
        serialWriteHex(static_cast<uint32_t>(invalidCommitThread)); serialWrite("/");
        serialWriteHex(static_cast<uint32_t>(oversizedReserveThread)); serialWrite("/");
        serialWriteHex(static_cast<uint32_t>(ps::detail::bootstrapThreadObjectCount() -
                                               threadObjectsBeforeStackValidation));
        serialWrite("/");
        serialWriteHex(static_cast<uint32_t>(ke::PriorityScheduler::get().getReadyThreadCount() -
                                               readyThreadsBeforeStackValidation));
        serialWrite("\r\n");
        return false;
    }
    const auto invalidPcStatus = sys::NtCreateThreadEx(&peer, ob::SYNCHRONIZE, nullptr, process,
        1, 0, 1, 0, mm::PageSize4KB, 32768, nullptr);
    if (invalidPcStatus != NtStatus::Success || !peer) return false;
    ps::EThread* invalidThread{};
    ob::ObjectHeader* invalidReference{};
    if (ps::detail::referenceThreadHandle(peer, ob::SYNCHRONIZE, invalidThread, invalidReference) != NtStatus::Success ||
        !invalidThread || invalidThread->cancelBeforeStart() != NtStatus::Success ||
        invalidThread->reap() != NtStatus::Success) return false;
    ob::ObDereferenceObject(invalidReference);
    if (sys::NtClose(peer) != NtStatus::Success) return false;
    peer = {};
    if (sys::NtCreateThreadEx(&peer, ob::SYNCHRONIZE | 2, nullptr, process,
            entry, 0x1234, 1, 0, mm::PageSize4KB, 32768, &attrs) != NtStatus::Success)
        return false;
    ps::EThread* object{};
    ob::ObjectHeader* reference{};
    uint32_t count{};
    if (ps::detail::referenceThreadHandle(peer, ob::SYNCHRONIZE, object, reference) != NtStatus::Success ||
        !object || object->kernelThread()->state != ke::ThreadState::Initialized ||
        client.uniqueThread != object->kernelThread()->tid || !client.uniqueProcess ||
        teb != object->userTebAddress() || clientLength != sizeof(client) || tebLength != sizeof(teb) ||
        sys::NtResumeThread(peer, &count) != NtStatus::Success || count != 1 ||
        sys::NtResumeThread(peer, &count) != NtStatus::Success || count != 0 ||
        object->cancelBeforeStart() != NtStatus::Success || object->reap() != NtStatus::Success)
        return false;
    ob::ObDereferenceObject(reference);
    return sys::NtClose(peer) == NtStatus::Success;
}

__attribute__((noinline)) bool testClonedImageLifetime(micant::Handle process) noexcept {
    using namespace micant;
    static mm::Mmpfn baselineEntries[512]{};
    static mm::Mmpfn rollbackEntries[512]{};
    const auto baselineEntryCount = mm::PfnDatabase::get().snapshotActiveEntries(baselineEntries);
    ob::detail::HandleTableGuard interruptGuard;
    ps::EProcess* child{};
    ob::ObjectHeader* reference{};
    if (ps::detail::referenceProcessHandle(process, ob::SYNCHRONIZE, child, reference) != NtStatus::Success ||
        !child || !child->nativeImagesFinalized() || !child->nativeImages()) return false;
    uintptr_t original{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(original));
    if (!hal::amd64::switchProcessPagingRootByPhysical(child->getDirectoryTableBase())) return false;
    uint64_t thunk{};
    const bool valid = child->userPebAddress() &&
        hal::amd64::bootstrapUserRange(child->userPebAddress(), 1, false) &&
        child->nativeImages()->resolve("ntdll.dll", "RtlUserThreadStart", 0, thunk) == NtStatus::Success &&
        hal::amd64::bootstrapUserRange(thunk, 1, false, true);
    const auto* image = child->nativeImages()->module(0);
    const uintptr_t entry = image ? image->base + image->view.headers().optionalHeader.addressOfEntryPoint : 0;
    if (!hal::amd64::switchProcessPagingRootByPhysical(original)) return false;
    Handle threadHandle{};
    if (!valid || !entry || sys::NtCreateThreadEx(&threadHandle, ob::SYNCHRONIZE, nullptr, process,
            entry, 0, 1, 0, mm::PageSize4KB, 32768, nullptr) != NtStatus::Success) return false;
    ps::EThread* thread{};
    ob::ObjectHeader* threadReference{};
    if (ps::detail::referenceThreadHandle(threadHandle, ob::SYNCHRONIZE, thread, threadReference) != NtStatus::Success ||
        !thread || !hal::amd64::switchProcessPagingRootByPhysical(child->getDirectoryTableBase())) return false;
    const auto tls = thread->userTlsStorage();
    const auto* teb = reinterpret_cast<const um::Teb64*>(thread->userTebAddress());
    const auto* tlsVector = teb ? reinterpret_cast<const uint64_t*>(teb->threadLocalStoragePointer) : nullptr;
    const bool threadValid = teb && teb->processEnvironmentBlock == child->userPebAddress() && tls &&
        *reinterpret_cast<const uint64_t*>(tls) == 0x8877665544332211ULL && tlsVector && tlsVector[1] &&
        *reinterpret_cast<const uint64_t*>(tlsVector[1]) == 0x1020304050607080ULL;
    if (!hal::amd64::switchProcessPagingRootByPhysical(original)) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const auto rolledBack = thread->rollbackBeforeStart();
    const auto rollbackEntryCount = mm::PfnDatabase::get().snapshotActiveEntries(rollbackEntries);
    if (baselineEntryCount <= 512 && rollbackEntryCount <= 512) {
        for (size_t i = 0; i < rollbackEntryCount; ++i) {
            bool existed = false;
            for (size_t j = 0; j < baselineEntryCount; ++j) {
                if (rollbackEntries[i].pfn == baselineEntries[j].pfn) {
                    existed = true;
                    break;
                }
            }
            if (!existed) {
            }
        }
    } else {
    }
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    ob::ObDereferenceObject(threadReference);
    ob::ObDereferenceObject(reference);
    const auto threadCloseStatus = sys::NtClose(threadHandle);
    if (!threadValid || rolledBack != NtStatus::Success || threadCloseStatus != NtStatus::Success ||
        sys::NtTerminateProcess(process, NtStatus::Success) != NtStatus::Success) return false;
    // Once the runtime clock is active process teardown is deferred to the
    // reaper. Wait on the process object before checking the PFN baseline.
    if (hal::amd64::bootstrapSchedulerClockActive()) {
        auto* current = ke::KeGetCurrentThread();
        if (!current || !hal::amd64::switchProcessPagingRootByPhysical(current->directoryTableBase)) return false;
        if (ke::g_CurrentCpu.serviceDeferredCleanup) ke::g_CurrentCpu.serviceDeferredCleanup();
        const auto waited = sys::NtWaitForSingleObject(process, false, nullptr);
        if (!hal::amd64::switchProcessPagingRootByPhysical(original) || waited != NtStatus::Success) return false;
    }
    const auto closeStatus = sys::NtClose(process);
    const auto finalEntryCount = mm::PfnDatabase::get().snapshotActiveEntries(rollbackEntries);
    if (baselineEntryCount <= 512 && finalEntryCount <= 512) {
        for (size_t i = 0; i < finalEntryCount; ++i) {
            bool existed = false;
            for (size_t j = 0; j < baselineEntryCount; ++j) {
                if (rollbackEntries[i].pfn == baselineEntries[j].pfn) {
                    existed = true;
                    break;
                }
            }
            if (!existed) {
            }
        }
    } else {
    }
    return closeStatus == NtStatus::Success;
}

alignas(16) uint8_t launcherStack[16384]{};
void launch(void* argument) noexcept {
    using namespace micant::ke;
    auto* boot = static_cast<KThread*>(argument);
    auto* current = KeGetCurrentThread();
    KeLowerIrql(PASSIVE_LEVEL);
    if (!PriorityScheduler::get().readyKThread(boot)) __builtin_trap();
    current->state = ThreadState::Terminated;
    KfRaiseIrql(DISPATCH_LEVEL);
    auto* next = PriorityScheduler::get().selectNextThread();
    if (!next || !KiSwapContext(current, next)) __builtin_trap();
    __builtin_trap();
}

__attribute__((noinline)) bool testNativeThreadExecution(micant::Handle processHandle,
    micant::Handle initialHandle, uintptr_t imageEntry, uintptr_t bootRoot, uintptr_t enclosingRoot,
    bool growStack = false, bool ring3ProcessCreate = false, uintptr_t importTable = 0) noexcept {
    using namespace micant;
    using namespace micant::ke;
    ob::detail::HandleTableGuard interruptGuard;
    auto* boot = KeGetCurrentThread();
    ps::EProcess* process{};
    ob::ObjectHeader* processReference{};
    ps::EThread* initial{};
    ob::ObjectHeader* initialReference{};
    if ((ring3ProcessCreate && !importTable) || !boot ||
        ps::detail::referenceProcessHandle(processHandle, 0, process, processReference) != NtStatus::Success ||
        ps::detail::referenceThreadHandle(initialHandle, 0, initial, initialReference) != NtStatus::Success ||
        !process || !initial || !initial->kernelThread()) return false;
    if (!hal::amd64::switchProcessPagingRootByPhysical(process->getDirectoryTableBase())) return false;
    uintptr_t resultAddress{};
    const auto allocated = process->getAddressSpace().allocate(resultAddress, mm::PageSize4KB,
        mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE);
    if (allocated == NtStatus::Success && ring3ProcessCreate) {
        *reinterpret_cast<uint64_t*>(resultAddress) = importTable;
        *reinterpret_cast<uint64_t*>(resultAddress + 8) = 0;
    }
    if (!hal::amd64::switchProcessPagingRootByPhysical(enclosingRoot) || allocated != NtStatus::Success) return false;
    const uintptr_t start = growStack ? reinterpret_cast<uintptr_t>(MicantGrowStackThreadStart) :
        ring3ProcessCreate ? reinterpret_cast<uintptr_t>(MicantRing3ProcessCreateStart) :
                             reinterpret_cast<uintptr_t>(MicantNativeThreadStart);
    const uintptr_t offset = start -
                             reinterpret_cast<uintptr_t>(MicantTwoUserThreads);
    Handle peer{};
    uintptr_t initialStackLimit{};
    if (growStack) {
        if (!hal::amd64::switchProcessPagingRootByPhysical(process->getDirectoryTableBase())) return false;
        uint64_t thunk{};
        auto result = process->nativeImages()->resolve("ntdll.dll", "RtlUserThreadStart", 0, thunk);
        ps::EThread* created{};
        if (result == NtStatus::Success)
            result = ps::detail::createUserThreadHandle(*process, thunk, 32768, 24 * mm::PageSize4KB,
                hal::amd64::bootstrapSchedulerClockActive(), ob::SYNCHRONIZE | 2, peer, created, mm::PageSize4KB);
        if (result == NtStatus::Success) result = created->configureUserStart(imageEntry + offset, resultAddress);
        if (result == NtStatus::Success) result = created->suspendInitially();
        if (result == NtStatus::Success)
            initialStackLimit = reinterpret_cast<const um::Teb64*>(created->userTebAddress())->stackLimit;
        if (!hal::amd64::switchProcessPagingRootByPhysical(enclosingRoot) || result != NtStatus::Success) return false;
    } else {
        const auto created = sys::NtCreateThreadEx(&peer, ob::SYNCHRONIZE | 2, nullptr, processHandle, imageEntry + offset,
               resultAddress, 1, 0, mm::PageSize4KB, 8 * mm::PageSize4KB, nullptr);
        if (created != NtStatus::Success) return false;
    }
    ps::EThread* thread{};
    ob::ObjectHeader* reference{};
    if (ps::detail::referenceThreadHandle(peer, ob::SYNCHRONIZE, thread, reference) != NtStatus::Success ||
        !thread || !thread->kernelThread()) return false;
    auto& scheduler = PriorityScheduler::get();
    auto* first = initial->kernelThread();
    const auto firstPriority = first->priority;
    if (!scheduler.removeReadyThread(first)) return false;
    first->state = ThreadState::Ready;
    first->priority = PRIORITY_IDLE;
    if (!scheduler.readyKThread(first)) return false;
    thread->kernelThread()->priority = boot->priority + 1;
    uint32_t count{};
    if (sys::NtResumeThread(peer, &count) != NtStatus::Success || count != 1 ||
        !hal::amd64::switchProcessPagingRootByPhysical(bootRoot)) return false;
    KThread launcher;
    launcher.directoryTableBase = bootRoot;
    if (!detail::prepareBootstrapThread(launcher, launcherStack, sizeof(launcherStack), launch, boot)) return false;
    const auto old = KfRaiseIrql(DISPATCH_LEVEL);
    const bool switched = KiSwapContext(boot, &launcher);
    KeLowerIrql(old);
    const bool waited = switched && sys::NtWaitForSingleObject(peer, false, nullptr) == NtStatus::Success;
    const bool terminated = waited && thread->exitStatus() == NtStatus::AccessDenied;
    if (!scheduler.removeReadyThread(first)) return false;
    first->state = ThreadState::Ready;
    first->priority = firstPriority;
    if (!scheduler.readyKThread(first) || !hal::amd64::switchProcessPagingRootByPhysical(process->getDirectoryTableBase()))
        return false;
    if (growStack) {
    }
    const bool called = (ring3ProcessCreate
            ? *reinterpret_cast<const uint64_t*>(resultAddress + 8) == 0x12345678
            : *reinterpret_cast<const uint64_t*>(resultAddress) == 0x12345678) &&
        (!growStack || *(reinterpret_cast<const uint64_t*>(resultAddress) + 1) ==
                           initialStackLimit - 5 * mm::PageSize4KB);
    if (ring3ProcessCreate && !called) {
        serialWrite("[MicaNT Ps] Ring 3 process test status=");
        serialWriteHex(static_cast<uint32_t>(*reinterpret_cast<const uint64_t*>(resultAddress + 8)));
        serialWrite(" thread exit=");
        serialWriteHex(static_cast<uint32_t>(thread->exitStatus()));
        serialWrite(" entry offset=");
        serialWriteHex(static_cast<uint32_t>(offset));
        serialWrite("\r\n");
    }
    const auto freed = process->getAddressSpace().free(resultAddress, 0, mm::MEM_RELEASE);
    if (!hal::amd64::switchProcessPagingRootByPhysical(enclosingRoot)) return false;
    const auto reaped = thread->kernelThread() ? thread->reap() : NtStatus::Success;
    ob::ObDereferenceObject(reference);
    ob::ObDereferenceObject(initialReference);
    ob::ObDereferenceObject(processReference);
    return terminated && called && freed == NtStatus::Success && reaped == NtStatus::Success &&
        sys::NtClose(peer) == NtStatus::Success;
}
}

static bool drainPreviousDeferredCleanup() noexcept {
    using namespace micant;
    if (!ps::detail::deferredCleanupPending() && !io::IoDeferredFileDeletesPending())
        return true;
    auto* current = ke::KeGetCurrentThread();
    if (!current || !hal::amd64::bootstrapSchedulerClockActive()) return false;
    uintptr_t previousRoot{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(previousRoot));
    if (!hal::amd64::switchProcessPagingRootByPhysical(
            hal::amd64::bootstrapKernelPagingRoot())) return false;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const auto priority = current->priority;
    current->priority = ke::PRIORITY_IDLE;
    if (ke::g_CurrentCpu.serviceDeferredCleanup)
        ke::g_CurrentCpu.serviceDeferredCleanup();
    const auto start = hal::amd64::bootstrapSchedulerClockTicks();
    __asm__ __volatile__("sti" : : : "memory");
    bool drained = false;
    for (size_t spin = 0; spin < 50000000 &&
         hal::amd64::bootstrapSchedulerClockTicks() - start < 100; ++spin) {
        if (!ps::detail::deferredCleanupPending() && !io::IoDeferredFileDeletesPending()) {
            drained = true;
            break;
        }
        __asm__ __volatile__("pause");
    }
    __asm__ __volatile__("cli" : : : "memory");
    current->priority = priority;
    const bool restored = hal::amd64::switchProcessPagingRootByPhysical(previousRoot);
    if (flags & (uint64_t{1} << 9)) __asm__ __volatile__("sti" : : : "memory");
    return drained && restored;
}

static bool runUserThreads(bool preemptive, size_t threadCount) noexcept {
    using namespace micant;
    using namespace micant::ke;
    struct Progress {
        const char* stage{"entry"};
        bool complete{};
        ~Progress() {
            if (complete) return;
            serialWrite("[MicaNT Ps] User worker test failed at stage: ");
            serialWrite(stage);
            serialWrite("\r\n");
        }
    } progress;
    auto& scheduler = PriorityScheduler::get();
    auto* boot = KeGetCurrentThread();
    if (!boot || readyNonPassiveWorkThreadCount() || threadCount < 2 || threadCount > 3 ||
        (preemptive && threadCount != 2)) return false;
    // A lifetime baseline must not include retired objects from earlier tests.
    if (!drainPreviousDeferredCleanup()) {
        serialWrite("[MicaNT Ps] Deferred cleanup did not drain before thread PFN snapshot\r\n");
        return false;
    }
    const size_t active = mm::PfnDatabase::get().getActiveCount();
    const uint32_t sectionsBefore = sys::bootstrapSectionObjectCount();
    const uint32_t eventsBefore = sys::bootstrapEventObjectCount();
    uintptr_t previous{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(previous));
    {
        progress.stage = "process and image setup";
        auto processStorage = std::unique_ptr<ps::EProcess>(new (std::nothrow_t{}) ps::EProcess{});
        if (!processStorage) return false;
        auto& process = *processStorage;
        if (process.initializeAddressSpace() != NtStatus::Success) return false;
        progress.stage = "event handle setup";
        ProcessAddressSpaceCleanup processCleanup{process, previous};
        ps::EThread first;
        ps::EThread second;
        ps::EThread third;
        struct FailureTrace {
            Progress& progress;
            ~FailureTrace() {
                if (!progress.complete) {
                    serialWrite("[MicaNT Ps] User thread stage before object teardown: ");
                    serialWrite(progress.stage);
                    serialWrite("\r\n");
                }
            }
        } failureTrace{progress};
        Handle sourceFirst{}, sourceSecond{}, eventFirst{}, eventSecond{};
        constexpr uint32_t access = ob::SYNCHRONIZE | 2;
        auto& source = ob::detail::currentHandleTable();
        if (sys::NtCreateEvent(&sourceFirst, access, nullptr, 1, false) != NtStatus::Success ||
            sys::NtCreateEvent(&sourceSecond, access, nullptr, 0, false) != NtStatus::Success ||
            source.duplicateHandle(sourceFirst, process.getHandleTable(), eventFirst) != NtStatus::Success ||
            source.duplicateHandle(sourceSecond, process.getHandleTable(), eventSecond) != NtStatus::Success ||
            sys::NtClose(sourceFirst) != NtStatus::Success || sys::NtClose(sourceSecond) != NtStatus::Success ||
            !hal::amd64::switchProcessPagingRootByPhysical(process.getDirectoryTableBase())) return false;
        progress.stage = "synthetic image construction";
        const uintptr_t code = hal::amd64::BootstrapManagedBase;
        constexpr uintptr_t dataRva = 3 * mm::PageSize4KB;
        const auto* program = reinterpret_cast<const uint8_t*>(MicantTwoUserThreads);
        const size_t programSize = reinterpret_cast<uintptr_t>(MicantTwoUserThreadsEnd) - reinterpret_cast<uintptr_t>(program);
        if (!programSize || programSize > 2 * mm::PageSize4KB) return false;
        static constinit uint8_t file[512 + 3 * mm::PageSize4KB]{};
        pe::ImageDosHeader dos{};
        dos.e_magic = pe::DOS_MAGIC;
        dos.e_lfanew = 64;
        std::memcpy(file, &dos, sizeof(dos));
        static constinit pe::ImageNtHeaders64 nt{};
        nt = {};
        nt.signature = pe::NT_SIGNATURE;
        nt.fileHeader.machine = pe::MACHINE_AMD64;
        nt.fileHeader.numberOfSections = 2;
        nt.fileHeader.sizeOfOptionalHeader = sizeof(pe::ImageOptionalHeader64);
        nt.fileHeader.characteristics = pe::IMAGE_FILE_EXECUTABLE_IMAGE;
        nt.optionalHeader.magic = pe::PE32PLUS_MAGIC;
        nt.optionalHeader.imageBase = code;
        nt.optionalHeader.addressOfEntryPoint = mm::PageSize4KB;
        nt.optionalHeader.sectionAlignment = mm::PageSize4KB;
        nt.optionalHeader.fileAlignment = 512;
        nt.optionalHeader.sizeOfImage = 4 * mm::PageSize4KB;
        nt.optionalHeader.sizeOfHeaders = 512;
        nt.optionalHeader.subsystem = pe::IMAGE_SUBSYSTEM_WINDOWS_CUI;
        nt.optionalHeader.numberOfRvaAndSizes = 16;
        nt.optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_TLS] =
            {dataRva, sizeof(pe::ImageTlsDirectory64)};
        nt.optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IMPORT] =
            {dataRva + 0x80, 2 * sizeof(pe::ImageImportDescriptor)};
        std::memcpy(file + 64, &nt, sizeof(nt));
        pe::ImageSectionHeader text{};
        text.virtualAddress = mm::PageSize4KB;
        text.misc.virtualSize = 2 * mm::PageSize4KB;
        text.pointerToRawData = 512;
        text.sizeOfRawData = 2 * mm::PageSize4KB;
        text.characteristics = pe::IMAGE_SCN_MEM_EXECUTE | pe::IMAGE_SCN_MEM_READ;
        std::memcpy(file + 64 + sizeof(nt), &text, sizeof(text));
        pe::ImageSectionHeader data{};
        data.virtualAddress = dataRva;
        data.misc.virtualSize = mm::PageSize4KB;
        data.pointerToRawData = 512 + 2 * mm::PageSize4KB;
        data.sizeOfRawData = mm::PageSize4KB;
        data.characteristics = pe::IMAGE_SCN_MEM_READ | pe::IMAGE_SCN_MEM_WRITE;
        std::memcpy(file + 64 + sizeof(nt) + sizeof(text), &data, sizeof(data));
        std::memcpy(file + 512, program, programSize);
        pe::ImageTlsDirectory64 tlsDirectory{};
        tlsDirectory.startAddressOfRawData = code + dataRva + 0x40;
        tlsDirectory.endAddressOfRawData = tlsDirectory.startAddressOfRawData + 8;
        tlsDirectory.addressOfIndex = code + dataRva + 0x50;
        tlsDirectory.sizeOfZeroFill = 8;
        tlsDirectory.characteristics = 5u << 20;
        std::memcpy(file + 512 + 2 * mm::PageSize4KB, &tlsDirectory, sizeof(tlsDirectory));
        constexpr uint64_t seed = 0x8877665544332211ULL;
        std::memcpy(file + 512 + 2 * mm::PageSize4KB + 0x40, &seed, sizeof(seed));
        constexpr uint32_t uninitializedTlsIndex = 0xdeadbeef;
        std::memcpy(file + 512 + 2 * mm::PageSize4KB + 0x50, &uninitializedTlsIndex, sizeof(uninitializedTlsIndex));
        auto* rawData = file + 512 + 2 * mm::PageSize4KB;
        pe::ImageImportDescriptor imports{};
        imports.originalFirstThunk = dataRva + 0xe0;
        imports.name = dataRva + 0xc0;
        imports.firstThunk = dataRva + 0x120;
        std::memcpy(rawData + 0x80, &imports, sizeof(imports));
        constexpr char dllName[] = "ntdll.dll";
        std::memcpy(rawData + 0xc0, dllName, sizeof(dllName));
        constexpr const char* names[]{"NtAllocateVirtualMemory", "NtWaitForSingleObject", "NtSetEvent",
            "NtTerminateProcess", "NtQueryInformationThread", "NtCreateProcessEx", "NtCreateThreadEx",
            "NtResumeThread", "NtOpenProcess", "NtOpenThread", "NtQueryInformationProcess", "NtClose",
            "NtQuerySystemTime", "NtDelayExecution", "NtAlertThread", "NtQueueApcThread", "NtTestAlert",
            "NtTerminateThread", "NtDuplicateObject", "NtCreateEvent"};
        for (size_t i = 0; i < std::size(names); ++i) {
            const uint64_t rva = dataRva + 0x200 + i * 0x40;
            std::memcpy(rawData + 0xe0 + i * 8, &rva, sizeof(rva));
            std::memcpy(rawData + 0x120 + i * 8, &rva, sizeof(rva));
            std::memcpy(rawData + 0x202 + i * 0x40, names[i], std::strlen(names[i]) + 1);
        }
        const size_t beforeImage = mm::PfnDatabase::get().getActiveCount();
        progress.stage = "main PE image mapping";
        static constexpr uint8_t invalidImage[]{0};
        if (process.loadNativeImage("bad.exe", {}) != NtStatus::InvalidImageFormat ||
            process.loadNativeImage("bad.exe", invalidImage) != NtStatus::InvalidImageFormat ||
            process.nativeImages() ||
            mm::PfnDatabase::get().getActiveCount() != beforeImage ||
            process.loadNativeImage("fixture.exe", std::span<const uint8_t>{file}, executablePath) != NtStatus::Success)
            return false;
        const auto* images = process.nativeImages();
        const auto* image = images ? images->module(0) : nullptr;
        const auto* imageVad = process.getAddressSpace().findVad(code);
        if (!image || !imageVad || !imageVad->isSectionView || !imageVad->isImageView) return false;
        const uint8_t originalMagic = file[0];
        file[0] = 0;
        const bool ownsFile = image->view.fileBytes()[0] == originalMagic;
        file[0] = originalMagic;
        if (!ownsFile) return false;
        pe::PeImageView64::TlsLayout tlsLayout{};
        progress.stage = "main PE TLS validation";
        if (image->view.tlsLayout(std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(code), 4 * mm::PageSize4KB},
                            code, tlsLayout) != NtStatus::Success || !tlsLayout.present || tlsLayout.callbackCount ||
            tlsLayout.indexRva != dataRva + 0x50) return false;
        const auto unboundFirst = *reinterpret_cast<uint64_t*>(code + imports.firstThunk);
        if (process.finalizeNativeImages() != NtStatus::NoSuchFile ||
            *reinterpret_cast<uint64_t*>(code + imports.firstThunk) != unboundFirst) return false;
        pe::PeImageView64 dll;
        if (dll.inspect(std::span<const uint8_t>{nativeDll}) != NtStatus::Success) return false;
        const size_t beforeDuplicate = mm::PfnDatabase::get().getActiveCount();
        const auto duplicateStatus = process.loadNativeImage("fixture.exe", std::span<const uint8_t>{nativeDll});
        if (duplicateStatus != NtStatus::ObjectNameCollision ||
            mm::PfnDatabase::get().getActiveCount() != beforeDuplicate) return false;
        text.characteristics |= pe::IMAGE_SCN_MEM_WRITE;
        std::memcpy(file + 64 + sizeof(nt), &text, sizeof(text));
        const auto wxStatus = process.loadNativeImage("wx.exe", std::span<const uint8_t>{file});
        text.characteristics &= ~pe::IMAGE_SCN_MEM_WRITE;
        std::memcpy(file + 64 + sizeof(nt), &text, sizeof(text));
        if (!drainDeferredDeletes(previous, process.getDirectoryTableBase())) return false;
        // W+X is a valid PE protection; this image still conflicts with the
        // fixture already mapped at the same preferred address.
        if (wxStatus != NtStatus::Conflict || mm::PfnDatabase::get().getActiveCount() != beforeDuplicate)
            return false;
        if (process.loadNativeImage("ntdll.dll", std::span<const uint8_t>{nativeDll}, dllPath) != NtStatus::Success)
            return false;
        progress.stage = "TLS module construction";
        static constinit uint8_t tlsDllFile[1024]{};
        constexpr uintptr_t tlsDllBase = hal::amd64::BootstrapManagedBase + 0xe00000;
        static constinit pe::ImageNtHeaders64 tlsNt{};
        tlsNt = {};
        tlsNt.signature = pe::NT_SIGNATURE;
        tlsNt.fileHeader.machine = pe::MACHINE_AMD64;
        tlsNt.fileHeader.numberOfSections = 1;
        tlsNt.fileHeader.sizeOfOptionalHeader = sizeof(pe::ImageOptionalHeader64);
        tlsNt.fileHeader.characteristics = pe::IMAGE_FILE_EXECUTABLE_IMAGE | 0x2000;
        tlsNt.optionalHeader.magic = pe::PE32PLUS_MAGIC;
        tlsNt.optionalHeader.imageBase = tlsDllBase;
        tlsNt.optionalHeader.sectionAlignment = mm::PageSize4KB;
        tlsNt.optionalHeader.fileAlignment = 512;
        tlsNt.optionalHeader.sizeOfImage = 2 * mm::PageSize4KB;
        tlsNt.optionalHeader.sizeOfHeaders = 512;
        tlsNt.optionalHeader.numberOfRvaAndSizes = 16;
        tlsNt.optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_TLS] =
            {mm::PageSize4KB, sizeof(pe::ImageTlsDirectory64)};
        std::memcpy(tlsDllFile, &dos, sizeof(dos));
        std::memcpy(tlsDllFile + 64, &tlsNt, sizeof(tlsNt));
        pe::ImageSectionHeader tlsData{};
        tlsData.virtualAddress = mm::PageSize4KB;
        tlsData.misc.virtualSize = mm::PageSize4KB;
        tlsData.pointerToRawData = 512;
        tlsData.sizeOfRawData = 512;
        tlsData.characteristics = pe::IMAGE_SCN_MEM_READ | pe::IMAGE_SCN_MEM_WRITE;
        std::memcpy(tlsDllFile + 64 + sizeof(tlsNt), &tlsData, sizeof(tlsData));
        pe::ImageTlsDirectory64 secondTls{};
        secondTls.startAddressOfRawData = tlsDllBase + mm::PageSize4KB + 0x80;
        secondTls.endAddressOfRawData = secondTls.startAddressOfRawData + 8;
        secondTls.addressOfIndex = tlsDllBase + mm::PageSize4KB + 0x90;
        secondTls.sizeOfZeroFill = 8;
        secondTls.characteristics = 5u << 20;
        std::memcpy(tlsDllFile + 512, &secondTls, sizeof(secondTls));
        constexpr uint64_t secondSeed = 0x1020304050607080ULL;
        std::memcpy(tlsDllFile + 512 + 0x80, &secondSeed, sizeof(secondSeed));
        std::memcpy(tlsDllFile + 512 + 0x90, &uninitializedTlsIndex, sizeof(uninitializedTlsIndex));
        if (process.loadNativeImage("tls.dll", std::span<const uint8_t>{tlsDllFile}, tlsPath) != NtStatus::Success)
            return false;
        if (process.setNativeProcessParameters(executablePath, commandLine) != NtStatus::Success) return false;
        // Finalization reads the immutable image snapshot, not the writable
        // process mapping. Inject the unsupported metadata into that snapshot.
        auto* loadedTls = reinterpret_cast<pe::ImageTlsDirectory64*>(image->metadata.get() + dataRva);
        auto* callbacks = reinterpret_cast<uint64_t*>(image->metadata.get() + dataRva + 0x68);
        loadedTls->addressOfCallBacks = code + dataRva + 0x68;
        callbacks[0] = code + mm::PageSize4KB;
        callbacks[1] = 0;
        const auto callbackStatus = process.finalizeNativeImages();
        loadedTls->addressOfCallBacks = 0;
        callbacks[0] = 0;
        const bool callbackRejectedWithoutTlsMutation =
            *reinterpret_cast<uint32_t*>(code + tlsLayout.indexRva) == uninitializedTlsIndex &&
            *reinterpret_cast<uint32_t*>(tlsDllBase + mm::PageSize4KB + 0x90) == uninitializedTlsIndex;
        const auto finalizeStatus = process.finalizeNativeImages();
        if (callbackStatus != NtStatus::NotImplemented || !callbackRejectedWithoutTlsMutation ||
            finalizeStatus != NtStatus::Success || process.userTlsLayouts().size() != 2 ||
            *reinterpret_cast<uint32_t*>(code + tlsLayout.indexRva) != 0 ||
            *reinterpret_cast<uint32_t*>(tlsDllBase + mm::PageSize4KB + 0x90) != 1 ||
            sys::bootstrapSectionObjectCount() != sectionsBefore + 3 ||
            hal::amd64::bootstrapUserRange(code, 1, true) ||
            hal::amd64::bootstrapUserRange(code + mm::PageSize4KB, 1, true) ||
            !hal::amd64::bootstrapUserRange(code + mm::PageSize4KB, 1, false, true) ||
            !hal::amd64::bootstrapUserRange(code + dataRva, 1, true) ||
            process.loadNativeImage("late.dll", std::span<const uint8_t>{nativeDll}) != NtStatus::InvalidParameter)
            return false;
        uintptr_t counters{};
        if (preemptive && process.getAddressSpace().allocate(counters, mm::PageSize4KB,
                mm::MEM_RESERVE | mm::MEM_COMMIT, mm::PAGE_READWRITE) != NtStatus::Success) return false;
        for (size_t i = 0; i < 5; ++i) {
            uint64_t address{};
            if (images->resolve("ntdll.dll", names[i], 0, address) != NtStatus::Success ||
                *reinterpret_cast<uint64_t*>(code + imports.firstThunk + i * 8) != address) return false;
        }
        const uintptr_t entry = code + nt.optionalHeader.addressOfEntryPoint;
        auto invalidTls = tlsLayout;
        invalidTls.callbackCount = 1;
        progress.stage = "thread PFN baseline";
        if (!drainPreviousDeferredCleanup()) return false;
        static mm::Mmpfn beforeThreadEntries[1024]{};
        static mm::Mmpfn finalThreadEntries[1024]{};
        const size_t beforeThread = mm::PfnDatabase::get().getActiveCount();
        const auto beforeThreadEntryCount =
            mm::PfnDatabase::get().snapshotActiveEntries(beforeThreadEntries);
        const auto callbackFailure =
            first.initializeUser(process, entry, 32768, 32768, &invalidTls);
        if (callbackFailure != NtStatus::NotImplemented ||
            mm::PfnDatabase::get().getActiveCount() != beforeThread) return false;
        invalidTls = tlsLayout;
        invalidTls.alignment = 3; // Fails after user pages were allocated; reap must roll them back.
        const auto alignmentFailure =
            first.initializeUser(process, entry, 32768, 32768, &invalidTls);
        if (alignmentFailure != NtStatus::InvalidParameter ||
            first.kernelThread() || mm::PfnDatabase::get().getActiveCount() != beforeThread) return false;
        // Reserve enough space for the user exception dispatcher while
        // committing only its guaranteed initial stack area.
        const auto partialStackStatus = first.initializeUser(
            process, entry, 32768, 8 * mm::PageSize4KB, mm::PageSize4KB, false);
        if (partialStackStatus != NtStatus::Success) return false;
        const auto* partialTeb = reinterpret_cast<const um::Teb64*>(first.userTebAddress());
        if (!partialTeb || !hal::amd64::bootstrapUserRange(partialTeb->stackLimit, 1, true) ||
            hal::amd64::bootstrapUserRange(partialTeb->stackLimit - mm::PageSize4KB, 1, false) ||
            first.resolveUserStackGuard(partialTeb->stackLimit - mm::PageSize4KB) != NtStatus::GuardPageViolation)
            return false;
        const auto partialStackReapStatus = first.reap();
        if (partialStackReapStatus != NtStatus::Success ||
            mm::PfnDatabase::get().getActiveCount() != beforeThread) return false;
        // Compose a separate process and initial user thread through the
        // private Ps factory, then roll it back before its entry executes.
        Handle createdProcess{}, createdThread{};
        const ps::detail::BootstrapProcessImage processImages[]{
            {"fixture.exe", std::span<const uint8_t>{file}, executablePath},
            {"ntdll.dll", std::span<const uint8_t>{nativeDll}, dllPath},
            {"tls.dll", std::span<const uint8_t>{tlsDllFile}, tlsPath},
        };
        const auto invalidFactoryStatus = ps::detail::createBootstrapUserProcess({}, executablePath, commandLine, 32768,
                32768, preemptive, ob::SYNCHRONIZE, ob::SYNCHRONIZE,
                createdProcess, createdThread);
        if (invalidFactoryStatus != NtStatus::InvalidParameter || createdProcess || createdThread) return false;
        progress.stage = "bootstrap process factory";
        const auto factoryStatus = ps::detail::createBootstrapUserProcess(processImages, executablePath, commandLine, 32768,
                32768, preemptive, ob::SYNCHRONIZE | 1 | 2 | 0x80, ob::SYNCHRONIZE,
                createdProcess, createdThread);
        if (factoryStatus != NtStatus::Success || !createdProcess || !createdThread) return false;
        progress.stage = "native peer thread rights";
        if (!testNativePeerThread(createdProcess, entry)) return false;
        progress.stage = "native thread execution";
        if (!testNativeThreadExecution(createdProcess, createdThread, entry, previous,
                                       process.getDirectoryTableBase())) return false;
        progress.stage = "native stack growth";
        if (!testNativeThreadExecution(createdProcess, createdThread, entry, previous,
                                       process.getDirectoryTableBase(), true)) return false;
        progress.stage = "Ring 3 process and thread syscalls";
        if (!testNativeThreadExecution(createdProcess, createdThread, entry, previous,
                process.getDirectoryTableBase(), false, true, code + imports.firstThunk)) return false;
        progress.stage = "kernel clone lifetime";
        Handle clonedProcess{};
        const auto cloneStatus = sys::NtCreateProcessEx(&clonedProcess,
            ob::SYNCHRONIZE | 1 | 2, nullptr, createdProcess, 0, 0, 0, 0, false);
        if (cloneStatus != NtStatus::Success) {
            serialWrite("[MicaNT Ps] Kernel process clone status=");
            serialWriteHex(static_cast<uint32_t>(cloneStatus));
            serialWrite("\r\n");
            return false;
        }
        progress.stage = "kernel clone parent terminate";
        const auto parentTerminateStatus = sys::NtTerminateProcess(createdProcess, NtStatus::Success);
        if (parentTerminateStatus != NtStatus::Success &&
            parentTerminateStatus != NtStatus::ProcessIsTerminating) return false;
        progress.stage = "kernel clone thread close";
        const auto closeThreadStatus = sys::NtClose(createdThread);
        if (closeThreadStatus != NtStatus::Success) return false;
        progress.stage = "kernel clone parent close";
        const auto closeProcessStatus = sys::NtClose(createdProcess);
        if (closeProcessStatus != NtStatus::Success) return false;
        progress.stage = "clone image lifetime";
        const auto cloneLifetime = testClonedImageLifetime(clonedProcess);
        if (!cloneLifetime) return false;
        if (!waitForPfnBaselineAndDeferredDeletes(previous, process.getDirectoryTableBase(),
                std::span<const mm::Mmpfn>{beforeThreadEntries, beforeThreadEntryCount},
                finalThreadEntries)) return false;
        if (mm::PfnDatabase::get().getActiveCount() != beforeThread) return false;
        // Exercise Ob ownership for a user context before the real workers.
        progress.stage = "Ob-owned user thread lifecycle";
        Handle objectHandle{};
        ps::EThread* objectThread{};
        const auto objectCreateStatus = ps::detail::createUserThreadHandle(process, entry, 32768, 32768,
                                               preemptive, ob::SYNCHRONIZE,
                                               objectHandle, objectThread);
        if (objectCreateStatus != NtStatus::Success || !objectThread) return false;
        const auto objectCancelStatus = objectThread->cancelBeforeStart();
        if (objectCancelStatus != NtStatus::Success) return false;
        LargeInteger objectPoll{};
        const auto objectWaitStatus = sys::NtWaitForSingleObject(objectHandle, false, &objectPoll);
        const auto objectReapStatus = objectThread->reap();
        const auto objectCloseStatus = sys::NtClose(objectHandle);
        if (objectWaitStatus != NtStatus::Success || objectReapStatus != NtStatus::Success ||
            objectCloseStatus != NtStatus::Success) {
            return false;
        }
        if (!waitForPfnBaselineAndDeferredDeletes(previous, process.getDirectoryTableBase(),
                std::span<const mm::Mmpfn>{beforeThreadEntries, beforeThreadEntryCount},
                finalThreadEntries)) return false;
        ps::EThread* threads[]{&first, &second, &third};
        progress.stage = "user workers initialization";
        uintptr_t tebs[3]{}, tlsBlocks[3]{}, tlsSecond[3]{};
        for (size_t i = 0; i < threadCount; ++i) {
            if (threads[i]->initializeUser(process, entry, 32768, 8 * mm::PageSize4KB,
                                           mm::PageSize4KB, preemptive) != NtStatus::Success)
                return false;
            tebs[i] = threads[i]->userTebAddress();
            const auto* teb = reinterpret_cast<const um::Teb64*>(tebs[i]);
            if (!teb || teb->uniqueProcessId != process.processId() ||
                !teb->uniqueThreadId || teb->uniqueThreadId != threads[i]->kernelThread()->tid ||
                (i && teb->uniqueThreadId == reinterpret_cast<const um::Teb64*>(tebs[0])->uniqueThreadId))
                return false;
            tlsBlocks[i] = threads[i]->userTlsStorage();
            tlsSecond[i] = reinterpret_cast<const uint64_t*>(teb->threadLocalStoragePointer)[1];
            auto* arguments = reinterpret_cast<uint64_t*>(threads[i]->userStackPointer());
            arguments[0] = eventFirst;
            arguments[1] = eventSecond;
            arguments[2] = i;
            arguments[3] = code + imports.firstThunk;
            arguments[4] = counters;
            arguments[5] = threadCount == 3 || preemptive ? 1 : 0;
            arguments[6] = preemptive ? 1 : 0;
        }
        progress.stage = "user workers ready";
        if (!hal::amd64::switchProcessPagingRootByPhysical(previous)) return false;
        for (size_t i = 0; i < threadCount; ++i) {
            threads[i]->kernelThread()->priority = boot->priority + 1;
            if (!threads[i]->kernelThread()->priority || threads[i]->ready() != NtStatus::Success) return false;
        }
        if (preemptive) {
            first.kernelThread()->quantumRemaining = 1;
            second.kernelThread()->quantumRemaining = 1000;
        }
        auto launcher = std::unique_ptr<KThread>(new (std::nothrow_t{}) KThread{});
        if (!launcher) return false;
        launcher->directoryTableBase = g_CurrentCpu.currentCr3;
        if (!detail::prepareBootstrapThread(*launcher, launcherStack, sizeof(launcherStack), launch, boot)) return false;
        progress.stage = "user workers execution and termination";
        const KIRQL oldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        const bool switched = KiSwapContext(boot, launcher.get());
        KeLowerIrql(oldIrql);
        if (!switched) {
            serialWrite("[MicaNT Ps] User workers did not switch back to launcher\r\n");
            return false;
        }
        if (!process.isTerminating()) {
            serialWrite("[MicaNT Ps] User workers left process unterminated\r\n");
            return false;
        }
        if (process.exitStatus() != NtStatus::AccessDenied) {
            serialWrite("[MicaNT Ps] User workers exit status=0x");
            serialWriteHex(static_cast<uint32_t>(process.exitStatus()));
            serialWrite("\r\n");
            return false;
        }
        if (readyNonPassiveWorkThreadCount()) {
            serialWrite("[MicaNT Ps] User workers left runnable non-passive work\r\n");
            return false;
        }
        if (!hal::amd64::switchProcessPagingRootByPhysical(process.getDirectoryTableBase())) {
            serialWrite("[MicaNT Ps] User worker process root could not be restored\r\n");
            return false;
        }
        progress.stage = "user workers PEB and TLS validation";
        bool tls = process.userPebAddress() != 0;
        const uintptr_t loaderAddress = *reinterpret_cast<const uint64_t*>(process.userPebAddress() + 0x18);
        if (!hal::amd64::bootstrapUserRange(loaderAddress, mm::PageSize4KB, false)) return false;
        const uintptr_t listHead = loaderAddress + 0x20;
        const auto* head = reinterpret_cast<const uint64_t*>(listHead);
        uintptr_t link = head[0];
        uintptr_t previousLink = listHead;
        const uintptr_t expectedBases[]{code, tlsDllBase, dll.headers().optionalHeader.imageBase};
        const std::u16string_view expectedPaths[]{executablePath, tlsPath, dllPath};
        const auto matches = [](const ps::detail::UserUnicodeString& value, std::u16string_view expected) {
            return value.length == expected.size() * sizeof(char16_t) &&
                value.maximumLength == value.length + sizeof(char16_t) &&
                hal::amd64::bootstrapUserRange(value.buffer, value.maximumLength, false) &&
                std::memcmp(reinterpret_cast<const void*>(value.buffer), expected.data(), value.length) == 0 &&
                *reinterpret_cast<const char16_t*>(value.buffer + value.length) == 0;
        };
        const auto parametersAddress = *reinterpret_cast<const uint64_t*>(process.userPebAddress() + 0x20);
        if (!hal::amd64::bootstrapUserRange(parametersAddress, sizeof(ps::detail::UserProcessParameters), false)) return false;
        const auto& parameters = *reinterpret_cast<const ps::detail::UserProcessParameters*>(parametersAddress);
        tls = tls && matches(parameters.imagePathName, executablePath) && matches(parameters.commandLine, commandLine);
        size_t pathIndex = 0;
        for (const auto expectedBase : expectedBases) {
            if (link == listHead || !hal::amd64::bootstrapUserRange(link, 0x48, false)) {
                tls = false;
                break;
            }
            const auto* entry = reinterpret_cast<const uint64_t*>(link);
            if (entry[1] != previousLink || entry[4] != expectedBase ||
                !matches(*reinterpret_cast<const ps::detail::UserUnicodeString*>(link + 0x38), expectedPaths[pathIndex++])) {
                tls = false;
                break;
            }
            previousLink = link;
            link = entry[0];
        }
        tls = tls && link == listHead && head[1] == previousLink;
        for (size_t i = 0; i < threadCount; ++i) {
            const auto* teb = reinterpret_cast<um::Teb64*>(tebs[i]);
            tls = tls && threads[i]->kernelThread()->state == ThreadState::Terminated &&
                  teb->processEnvironmentBlock == process.userPebAddress() &&
                  teb->tlsSlots[0] == (i == 0 ? 0x1111222233334444ULL : 0xaaaabbbbccccddddULL) &&
                  *reinterpret_cast<uint64_t*>(tlsBlocks[i]) == (i == 0 ? 0x1234u : 0x5678u) &&
                  *reinterpret_cast<uint64_t*>(tlsSecond[i]) == (i == 0 ? 0xabcdu : 0xdcbau);
        }
        tls = tls && (!preemptive || (reinterpret_cast<volatile uint64_t*>(counters)[0] &&
                                      reinterpret_cast<volatile uint64_t*>(counters)[1]));
        if (!hal::amd64::switchProcessPagingRootByPhysical(previous) || !tls) return false;
        progress.stage = "user workers reap";
        for (size_t i = 0; i < threadCount; ++i)
            if (threads[i]->reap() != NtStatus::Success) return false;
        progress.stage = "user process address space close";
        const auto processCloseStatus = process.closeAddressSpace();
        if (processCloseStatus != NtStatus::Success) return false;
        processCleanup.active = false;
    }
    progress.stage = "final resource recovery";
    if (!drainPreviousDeferredCleanup()) return false;
    const auto finalPfnCount = mm::PfnDatabase::get().getActiveCount();
    const auto finalSectionCount = sys::bootstrapSectionObjectCount();
    const auto finalEventCount = sys::bootstrapEventObjectCount();
    const bool resourcesRestored = finalPfnCount == active &&
        finalSectionCount == sectionsBefore && finalEventCount == eventsBefore;
    progress.complete = resourcesRestored;
    return resourcesRestored;
}

extern "C" bool MicantTestPsTwoUserThreads() noexcept { return runUserThreads(false, 2); }
extern "C" bool MicantTestPsThreeUserThreads() noexcept { return runUserThreads(false, 3); }
extern "C" bool MicantTestPsUserPreemption() noexcept { return runUserThreads(true, 2); }
