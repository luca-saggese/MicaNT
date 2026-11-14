// Minimal freestanding image used to verify the file-backed SEC_IMAGE path.
// It has no imports and no DllMain, so the Ps loader can map it as a main
// image without the DLL-initializer support that M4 does not provide. The
// self-referential global forces a base relocation entry, so the loader can
// also exercise the relocated view path.
extern "C" {
volatile const void* g_selfReference = &g_selfReference;

__attribute__((naked, noreturn)) void _start() noexcept {
    // The bare-metal M4 fixture enters from a file-backed SEC_IMAGE view and
    // exits through the native user syscall boundary. No import trampoline is
    // needed for this minimal lifecycle probe.
    __asm__ __volatile__(
        "movq $-2, %rcx\n"
        "movq %rcx, %r10\n"
        "xorl %edx, %edx\n"
        "movl $0x53, %eax\n"
        "syscall\n"
        "ud2\n");
}
}
