#include "micant/km/core/ke.hpp"

#if defined(MICANT_FREESTANDING) && (defined(_M_X64) || defined(__x86_64__))
// Standard XSAVE layout, limited to x87/SSE/AVX/AVX-512 and 4096 bytes.
// Frames use the CPUID size. A zero XSAVE header restores architectural init
// state for a new thread. The FXSAVE fallback needs explicit x87/MXCSR defaults.
struct alignas(64) InitialExtendedState {
    uint16_t controlWord{0x037f};
    uint8_t reserved[22]{};
    uint32_t mxcsr{0x1f80};
    uint8_t remaining[4096 - 28]{};
};
static_assert(offsetof(InitialExtendedState, mxcsr) == 24);
static_assert(sizeof(InitialExtendedState) == 4096);
extern "C" {
uint32_t MicantAmd64XsaveMask{};
uint32_t MicantAmd64ExtendedSize{512};
constinit InitialExtendedState MicantAmd64InitialState{};
}

extern "C" bool MicantAmd64ConfigureExtendedState() noexcept {
    static uint64_t configuredMask = ~uint64_t{0};
    uint64_t cr4{};
    __asm__ __volatile__("movq %%cr4, %0" : "=r"(cr4));
    uint64_t mask = 0;
    uint32_t stateSize = 512;
    if (cr4 & (uint64_t{1} << 18)) {
        uint32_t low{}, high{};
        __asm__ __volatile__("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        mask = uint64_t{low} | (uint64_t{high} << 32);
        if ((mask & 3) != 3 || (mask & ~uint64_t{0xe7})) return false;
        uint32_t eax = 0, ebx{}, ecx{}, edx{};
        __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        if (eax < 0x0d) return false;
        eax = 0x0d; ecx = 0;
        __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
        if ((mask & ~(uint64_t{edx} << 32 | eax)) || ebx < 576 || ebx > 4096) return false;
        stateSize = ebx;
    }
    if (configuredMask != ~uint64_t{0}) return configuredMask == mask;
    MicantAmd64ExtendedSize = stateSize;
    MicantAmd64XsaveMask = static_cast<uint32_t>(mask);
    configuredMask = mask;
    return true;
}

extern "C" [[noreturn]] void MicantAmd64BootstrapThreadDispatch(
    micant::ke::detail::BootstrapThreadEntry entry, void* argument) noexcept {
    if (!entry) __builtin_trap();
    // A freshly-created kernel thread starts at PASSIVE_LEVEL even when the
    // dispatcher selected it from a DISPATCH_LEVEL context switch.
    micant::ke::g_CurrentCpu.currentIrql = micant::ke::PASSIVE_LEVEL;
    entry(argument);
    __builtin_trap();
}

extern "C" __attribute__((naked)) void MicantAmd64BootstrapThread() noexcept {
    __asm__ __volatile__(
        "movq %r13, %rdx\n"
        "movq %r12, %rcx\n"
        "callq MicantAmd64BootstrapThreadDispatch\n"
        "ud2\n"
    );
}

extern "C" __attribute__((naked)) bool MicantAmd64SwitchContext(
    micant::ke::KThreadContext*, micant::ke::KThreadContext*) noexcept {
    __asm__ __volatile__(
        "movq %rbx, 24(%rcx)\n"
        "movq %rbp, 32(%rcx)\n"
        "movq %rsi, 40(%rcx)\n"
        "movq %rdi, 48(%rcx)\n"
        "movq %r12, 56(%rcx)\n"
        "movq %r13, 64(%rcx)\n"
        "movq %r14, 72(%rcx)\n"
        "movq %r15, 80(%rcx)\n"
        "leaq 1f(%rip), %rax\n"
        "movq %rax, 0(%rcx)\n"
        "movq %rsp, 8(%rcx)\n"
        "pushfq\n"
        "popq %rax\n"
        "movq %rax, 16(%rcx)\n"
        // The old stack pointer is already saved above. Reserve an aligned
        // frame below it; it stays resident until this thread is resumed.
        "movl MicantAmd64ExtendedSize(%rip), %eax\n"
        "addq $64, %rax\n"
        "subq %rax, %rsp\n"
        "andq $-64, %rsp\n"
        "movq %rsp, 256(%rcx)\n"
        "cmpl $0, MicantAmd64XsaveMask(%rip)\nje 2f\n"
        "movq $0, 512(%rsp)\nmovq $0, 520(%rsp)\n"
        "movq $0, 528(%rsp)\nmovq $0, 536(%rsp)\n"
        "movq $0, 544(%rsp)\nmovq $0, 552(%rsp)\n"
        "movq $0, 560(%rsp)\nmovq $0, 568(%rsp)\n"
        "movq %rdx, %r10\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xsave64 (%rsp)\nmovq %r10, %rdx\njmp 3f\n"
        "2: fxsave64 (%rsp)\n"
        "3: movq %rdx, %r11\n"
        "movq 256(%r11), %r10\n"
        "testq %r10, %r10\njnz 4f\n"
        "leaq MicantAmd64InitialState(%rip), %r10\n"
        "4: cmpl $0, MicantAmd64XsaveMask(%rip)\nje 5f\n"
        "movl MicantAmd64XsaveMask(%rip), %eax\nxorl %edx, %edx\n"
        "xrstor64 (%r10)\njmp 6f\n"
        "5: fxrstor64 (%r10)\n"
        "6: movq %r11, %rax\n"
        "movq 24(%rax), %rbx\n"
        "movq 32(%rax), %rbp\n"
        "movq 40(%rax), %rsi\n"
        "movq 48(%rax), %rdi\n"
        "movq 56(%rax), %r12\n"
        "movq 64(%rax), %r13\n"
        "movq 72(%rax), %r14\n"
        "movq 80(%rax), %r15\n"
        "movq 8(%rax), %rsp\n"
        "jmp *0(%rax)\n"
        "1:\n"
        "movl $1, %eax\n"
        "retq\n"
    );
}
#endif
