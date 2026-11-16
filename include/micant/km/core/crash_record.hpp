#pragma once

// Fixed-size, allocator-free evidence captured at the fatal kernel boundary.
// This is intentionally separate from the hosted trap.hpp diagnostic engine.

#include <cstddef>
#include <cstdint>
#include "micant/km/core/ke.hpp"

namespace micant::ke {

inline constexpr uint32_t CrashRecordSignature = 0x48535243; // "CRSH" on disk
inline constexpr uint16_t CrashRecordVersion = 1;
inline constexpr uint16_t CrashRecordHeaderSize = 248;
inline constexpr size_t CrashStackCaptureBytes = 512;

struct CrashRecord final {
    uint32_t signature{CrashRecordSignature};
    uint16_t version{CrashRecordVersion};
    uint16_t size{CrashRecordHeaderSize};
    uint32_t bugCheckCode{};
    uint32_t flags{};
    uint64_t parameters[4]{};
    uint64_t buildId{}; // zero means the build system supplied no identity
    uint32_t processorNumber{};
    uint32_t pid{};
    uint32_t tid{};
    uint32_t stackBytes{};
    uint64_t rip{}, rsp{}, rflags{}, cr2{};
    // AMD64 CONTEXT general-register order. RSP appears here and in the
    // dedicated control-register section, as in the type-7 wire contract.
    uint64_t gpr[16]{}; // RAX,RCX,RDX,RBX,RSP,RBP,RSI,RDI,R8..R15
    uint64_t stackLimit{}, stackBase{};
    uint8_t stack[CrashStackCaptureBytes]{};
    uint32_t crc32{};
};

static_assert(sizeof(CrashRecord) <= 1024);
static_assert(offsetof(CrashRecord, stack) == CrashRecordHeaderSize);

[[nodiscard]] inline uint32_t crashRecordCrc32(const void* bytes, size_t length) noexcept {
    const auto* data = static_cast<const uint8_t*>(bytes);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Caller must pass the trap frame saved by the architecture entry path.
// The stack is read only when RSP is inside the current thread's known kernel
// stack bounds. At most 512 bytes are copied; no unwind or allocation occurs.
inline void captureCrashRecord(CrashRecord& out, uint32_t code,
                               uint64_t p1, uint64_t p2, uint64_t p3,
                               uint64_t p4, const KTrapFrame* frame,
                               uint64_t buildId = 0) noexcept {
    out = {};
    out.size = CrashRecordHeaderSize;
    out.bugCheckCode = code;
    out.parameters[0] = p1;
    out.parameters[1] = p2;
    out.parameters[2] = p3;
    out.parameters[3] = p4;
    out.buildId = buildId;
    out.processorNumber = g_CurrentCpu.processorNumber;

    auto* thread = g_CurrentCpu.currentThread;
    if (thread) {
        out.pid = static_cast<uint32_t>(thread->pid);
        out.tid = static_cast<uint32_t>(thread->tid);
        out.stackLimit = thread->kernelStackBase;
        out.stackBase = thread->kernelStackTop;
    }

    if (frame) {
        out.rip = frame->rip;
        out.rsp = frame->rsp;
        out.rflags = frame->eFlags;
        out.gpr[0] = frame->rax; out.gpr[1] = frame->rcx;
        out.gpr[2] = frame->rdx; out.gpr[3] = frame->rbx;
        out.gpr[4] = frame->rsp; out.gpr[5] = frame->rbp;
        out.gpr[6] = frame->rsi; out.gpr[7] = frame->rdi;
        out.gpr[8] = frame->r8; out.gpr[9] = frame->r9;
        out.gpr[10] = frame->r10; out.gpr[11] = frame->r11;
        out.gpr[12] = frame->r12; out.gpr[13] = frame->r13;
        out.gpr[14] = frame->r14; out.gpr[15] = frame->r15;
        out.cr2 = frame->faultAddress;
    }
#if defined(MICANT_BAREMETAL) && (defined(_M_X64) || defined(__x86_64__))
    else {
        // For explicit KeBugCheckEx calls without an exception frame, preserve
        // the available instruction/stack/flags and current CR2.
        uint64_t stackPointer{}, instructionPointer{}, flags{}, faultAddress{};
        __asm__ __volatile__("mov %%rsp, %0; lea 0f(%%rip), %1; pushfq; pop %2; mov %%cr2, %3; 0:"
                             : "=r"(stackPointer), "=r"(instructionPointer), "=r"(flags), "=r"(faultAddress)
                             : : "memory");
        out.rsp = stackPointer;
        out.rip = instructionPointer;
        out.rflags = flags;
        out.cr2 = faultAddress;
        out.flags |= 1u; // register context is a call-site snapshot, not trap state
    }
#endif

    if (thread && out.rsp >= out.stackLimit && out.rsp < out.stackBase) {
        const uint64_t available = out.stackBase - out.rsp;
        const size_t count = available < CrashStackCaptureBytes
            ? static_cast<size_t>(available) : CrashStackCaptureBytes;
        const auto* source = reinterpret_cast<const volatile uint8_t*>(out.rsp);
        for (size_t i = 0; i < count; ++i) out.stack[i] = source[i];
        out.stackBytes = static_cast<uint32_t>(count);
    }
    out.crc32 = 0;
    out.crc32 = crashRecordCrc32(&out, offsetof(CrashRecord, crc32));
}

} // namespace micant::ke
