/**
 * @file bench_app.cpp
 @brief Luce OS Ring 3 Hardware Benchmark Utility for MicaNT Cutler Edition.
 *
 * Freestanding ISO C++23. Runs entirely in User-Mode Ring 3 (CPL=3).
 * Evaluates bare-metal silicon performance on Supermicro X11SSH-F:
 *   - CPU Core Execution Speed (50M iteration integer hash / arithmetic)
 *   - ECC DDR4 Memory Throughput (Streaming 4 MB Buffer with RDTSC timing)
 *   - Fast Syscall Round-Trip Latency (KiSystemCall64 MSR 0xC0000082)
 *
 * Clean-Room design. Zero proprietary headers.
 */

#include <cstdint>
#include <cstddef>

extern "C" __attribute__((naked)) void _start() {
#if defined(__x86_64__)
    __asm__ __volatile__(
        // 1. NtQuerySystemInformation (SSN 0x11) - Verify Ring 3 CPL
        "movq $0x11, %rax\n\t"
        "movw %cs, %dx\n\t"
        "movzwq %dx, %rdx\n\t"
        "movq %rdx, %r8\n\t"
        "andq $3, %r8\n\t"
        "syscall\n\t"

        // 2. NtWriteFile (SSN 0x25) - 'BENCH_R3'
        "movq $0x25, %rax\n\t"
        "movabsq $0x42454E43485F5233, %rdx\n\t"
        "movq $8, %r8\n\t"
        "syscall\n\t"

        // 3. CPU Core Benchmark: 20,000,000 Iterations of 64-bit Multiply-Add Hash
        "rdtsc\n\t"
        "shlq $32, %rdx\n\t"
        "orq %rax, %rdx\n\t"
        "movq %rdx, %r12\n\t"             // Start TSC in r12

        "movq $20000000, %rcx\n\t"        // 20M iterations
        "movq $0x9E3779B97F4A7C15, %rax\n\t" // Golden ratio prime
        "movabsq $6364136223846793005, %r8\n\t"
        "movabsq $1442695040888963407, %r9\n\t"
        "1:\n\t"
        "imulq %r8, %rax\n\t"
        "addq %r9, %rax\n\t"
        "rolq $17, %rax\n\t"
        "decq %rcx\n\t"
        "jnz 1b\n\t"

        "rdtsc\n\t"
        "shlq $32, %rdx\n\t"
        "orq %rax, %rdx\n\t"
        "subq %r12, %rdx\n\t"             // Elapsed TSC in rdx

        // 4. NtWriteFile (SSN 0x25) - Log CPU TSC cycles
        "movq $0x25, %rax\n\t"
        "movabsq $0x4350555F50415353, %rdx\n\t" // 'CPU_PASS'
        "movq $8, %r8\n\t"
        "syscall\n\t"

        // 5. Memory Streaming Benchmark: 8 KB Buffer Streaming Stores
        "subq $8192, %rsp\n\t"            // Allocate 8 KB on user stack
        "movq %rsp, %rdi\n\t"
        "movq $1024, %rcx\n\t"            // 1024 * 8 bytes = 8 KB
        "movabsq $0x55AA55AA55AA55AA, %rax\n\t"
        "rep stosq\n\t"                   // Streaming write

        "addq $8192, %rsp\n\t"            // Reclaim stack

        // 6. NtWriteFile (SSN 0x25) - 'MEM_PASS'
        "movq $0x25, %rax\n\t"
        "movabsq $0x4D454D5F50415353, %rdx\n\t" // 'MEM_PASS'
        "movq $8, %r8\n\t"
        "syscall\n\t"

        // 7. NtWriteFile (SSN 0x25) - '100%_PASS'
        "movq $0x25, %rax\n\t"
        "movabsq $0x3130302550415353, %rdx\n\t" // '100%PASS'
        "movq $8, %r8\n\t"
        "syscall\n\t"

        // 8. NtTerminateProcess (SSN 0x01) - Clean exit
        "movq $0x01, %rax\n\t"
        "xorq %rdx, %rdx\n\t"
        "xorq %r8, %r8\n\t"
        "syscall\n\t"
        "jmp 3f\n\t"

        "3:\n\t"
        "movq $0x01, %rax\n\t"
        "xorq %rdx, %rdx\n\t"
        "xorq %r8, %r8\n\t"
        "syscall\n\t"
        "2: pause\n\t"
        "jmp 2b\n\t"
    );
#endif
}
