#include "micant/km/executive/ps.hpp"
#include "micant/km/core/ke.hpp"
#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/um/native/pe.hpp"
#include <cstring>
#include <span>

extern "C" void MicantNetbootSerialWrite(const char*) noexcept;
extern "C" void MicantNetbootSerialWriteNumber(uint64_t) noexcept;

extern "C" {
extern const uint8_t MicantPsUserProgramEnd[];
__attribute__((naked, section(".uproc$a"))) void MicantPsUserProgram() noexcept {
    __asm__ __volatile__(
        "subq $0x60, %rsp\n"
        "movabsq $0x123456789abcdef0, %rax\nmovq %rax, %gs:0x1480\n"
        "cmpq %gs:0x1480, %rax\njne 1f\n"
        "movq $0, 64(%rsp)\nmovq $4096, 72(%rsp)\n"
        "movq $0x3000, 40(%rsp)\nmovq $4, 48(%rsp)\n"
        "movq $-1, %r10\nleaq 64(%rsp), %rdx\nxorq %r8, %r8\n"
        "leaq 72(%rsp), %r9\nmovl $0x18, %eax\nsyscall\n"
        "testl %eax, %eax\njnz 1f\nmovq 64(%rsp), %rax\ntestq %rax, %rax\njz 1f\n"
        "movabsq $0x123456789abcdef0, %rcx\nmovq %rcx, (%rax)\n"
        "cmpq (%rax), %rcx\njne 1f\nmovl $0xc0000022, %edx\njmp 2f\n"
        "1: movl $0xc000000d, %edx\n"
        "2: movq $-1, %r10\nmovl $0x2c, %eax\nsyscall\nud2\n");
}
__asm__(".section .uproc$z,\"xr\"\n.globl MicantPsUserProgramEnd\n"
        "MicantPsUserProgramEnd:\n.text\n");
}

namespace {
uint32_t g_userProcessFailureStage{};
#define USER_PROCESS_FAIL(stage) do { \
    g_userProcessFailureStage = (stage); \
    MicantNetbootSerialWrite("[MicaNT PsTrace] user-process probe failure stage="); \
    MicantNetbootSerialWriteNumber(stage); \
    MicantNetbootSerialWrite("\r\n"); \
    return false; \
} while (0)
struct UserStart {
    micant::ke::KThread* boot;
    uintptr_t entry;
    uintptr_t stack;
};

void enterUser(void* argument) noexcept {
    auto& start = *static_cast<UserStart*>(argument);
    if (!micant::ke::PriorityScheduler::get().readyKThread(start.boot)) __builtin_trap();
    micant::hal::amd64::enterBootstrapUserThread(start.entry, start.stack);
}
}

extern "C" bool MicantTestPsUserProcess() noexcept {
    using namespace micant;
    using namespace micant::ke;
    auto& scheduler = PriorityScheduler::get();
    auto* boot = KeGetCurrentThread();
    g_userProcessFailureStage = 0;
    if (!boot) USER_PROCESS_FAIL(13);
    if (scheduler.getReadyThreadCount()) USER_PROCESS_FAIL(14);
    const size_t active = mm::PfnDatabase::get().getActiveCount();
    uintptr_t previous{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(previous));
    {
        ps::EProcess process;
        ps::EThread thread;
        if (process.initializeAddressSpace() != NtStatus::Success ||
            !hal::amd64::switchProcessPagingRootByPhysical(process.getDirectoryTableBase())) USER_PROCESS_FAIL(2);
        uintptr_t code{};
        uintptr_t stack{};
        uintptr_t teb{};
        auto& space = process.getAddressSpace();
        if (space.allocate(code, 2 * mm::PageSize4KB, mm::MEM_RESERVE | mm::MEM_COMMIT,
                           mm::PAGE_READWRITE) != NtStatus::Success ||
            space.allocate(stack, mm::PageSize4KB, mm::MEM_RESERVE | mm::MEM_COMMIT,
                           mm::PAGE_READWRITE) != NtStatus::Success ||
            space.allocate(teb, 2 * mm::PageSize4KB, mm::MEM_RESERVE | mm::MEM_COMMIT,
                           mm::PAGE_READWRITE) != NtStatus::Success) USER_PROCESS_FAIL(3);
        auto* userTeb = reinterpret_cast<um::Teb64*>(teb);
        userTeb->self = teb;
        userTeb->stackBase = stack + mm::PageSize4KB;
        userTeb->stackLimit = stack;
        const auto* program = reinterpret_cast<const uint8_t*>(MicantPsUserProgram);
        const size_t programSize = reinterpret_cast<uintptr_t>(MicantPsUserProgramEnd) -
                                   reinterpret_cast<uintptr_t>(program);
        if (!programSize || programSize > mm::PageSize4KB) USER_PROCESS_FAIL(4);
        static constinit uint8_t file[512 + mm::PageSize4KB]{};
        pe::ImageDosHeader dos{};
        dos.e_magic = pe::DOS_MAGIC;
        dos.e_lfanew = 64;
        std::memcpy(file, &dos, sizeof(dos));
        pe::ImageNtHeaders64 nt{};
        nt.signature = pe::NT_SIGNATURE;
        nt.fileHeader.machine = pe::MACHINE_AMD64;
        nt.fileHeader.numberOfSections = 1;
        nt.fileHeader.sizeOfOptionalHeader = sizeof(pe::ImageOptionalHeader64);
        nt.fileHeader.characteristics = pe::IMAGE_FILE_EXECUTABLE_IMAGE;
        nt.optionalHeader.magic = pe::PE32PLUS_MAGIC;
        nt.optionalHeader.imageBase = code;
        nt.optionalHeader.addressOfEntryPoint = mm::PageSize4KB;
        nt.optionalHeader.sectionAlignment = mm::PageSize4KB;
        nt.optionalHeader.fileAlignment = 512;
        nt.optionalHeader.sizeOfImage = 2 * mm::PageSize4KB;
        nt.optionalHeader.sizeOfHeaders = 512;
        nt.optionalHeader.subsystem = pe::IMAGE_SUBSYSTEM_WINDOWS_CUI;
        std::memcpy(file + 64, &nt, sizeof(nt));
        pe::ImageSectionHeader text{};
        text.virtualAddress = mm::PageSize4KB;
        text.misc.virtualSize = mm::PageSize4KB;
        text.pointerToRawData = 512;
        text.sizeOfRawData = mm::PageSize4KB;
        text.characteristics = pe::IMAGE_SCN_MEM_EXECUTE | pe::IMAGE_SCN_MEM_READ;
        std::memcpy(file + 64 + sizeof(nt), &text, sizeof(text));
        std::memcpy(file + 512, program, programSize);
        pe::PeImageView64 image;
        if (image.inspect(std::span<const uint8_t>{file}) != NtStatus::Success ||
            image.map(std::span<uint8_t>{reinterpret_cast<uint8_t*>(code), 2 * mm::PageSize4KB}, code) !=
                NtStatus::Success) USER_PROCESS_FAIL(5);
        uint32_t old{};
        if (space.protect(code, mm::PageSize4KB, mm::PAGE_READONLY, &old) != NtStatus::Success ||
            old != mm::PAGE_READWRITE ||
            space.protect(code + mm::PageSize4KB, mm::PageSize4KB, mm::PAGE_EXECUTE_READ, &old) != NtStatus::Success ||
            old != mm::PAGE_READWRITE || !hal::amd64::switchProcessPagingRootByPhysical(previous)) USER_PROCESS_FAIL(6);
        UserStart start{boot, code + nt.optionalHeader.addressOfEntryPoint, stack + mm::PageSize4KB - 16};
        if (thread.initialize(process, 32768, enterUser, &start) != NtStatus::Success) USER_PROCESS_FAIL(7);
        thread.kernelThread()->tebAddress = teb;
        if (thread.ready() != NtStatus::Success) USER_PROCESS_FAIL(8);
        auto* next = scheduler.selectNextThread();
        if (next != thread.kernelThread()) USER_PROCESS_FAIL(9);
        const KIRQL oldIrql = KfRaiseIrql(DISPATCH_LEVEL);
        const bool switched = KiSwapContext(boot, next);
        KeLowerIrql(oldIrql);
        uint32_t switchFailure{};
        if (!switched) switchFailure |= 1u << 0;
        if (KeGetCurrentThread() != boot) switchFailure |= 1u << 1;
        if (!process.isTerminating()) switchFailure |= 1u << 2;
        if (process.exitStatus() != NtStatus::AccessDenied) switchFailure |= 1u << 3;
        if (next->state != ThreadState::Terminated) switchFailure |= 1u << 4;
        if (cpu::cpuRdmsr(cpu::MSR_GS_BASE) != boot->tebAddress) switchFailure |= 1u << 5;
        // Other kernel threads can become runnable while this CPL3 probe is
        // executing and being preempted. The handoff invariant is that the
        // bootstrap thread itself was dequeued before it resumed.
        if (boot->readyQueued) switchFailure |= 1u << 6;
        if (!hal::amd64::switchProcessPagingRootByPhysical(process.getDirectoryTableBase())) switchFailure |= 1u << 7;
        if (switchFailure) {
            MicantNetbootSerialWrite("[MicaNT PsTrace] switch failure mask=");
            MicantNetbootSerialWriteNumber(switchFailure);
            MicantNetbootSerialWrite(" gs=");
            MicantNetbootSerialWriteNumber(cpu::cpuRdmsr(cpu::MSR_GS_BASE));
            MicantNetbootSerialWrite(" teb=");
            MicantNetbootSerialWriteNumber(boot->tebAddress);
            MicantNetbootSerialWrite("\r\n");
            USER_PROCESS_FAIL(10);
        }
        const bool tlsValue = userTeb->tlsSlots[0] == 0x123456789abcdef0ULL;
        if (!hal::amd64::switchProcessPagingRootByPhysical(previous) || !tlsValue ||
            thread.reap() != NtStatus::Success ||
            process.closeAddressSpace() != NtStatus::Success) USER_PROCESS_FAIL(11);
    }
    if (mm::PfnDatabase::get().getActiveCount() != active) USER_PROCESS_FAIL(12);
    return true;
}

extern "C" uint32_t MicantPsUserProcessFailureStage() noexcept { return g_userProcessFailureStage; }
#undef USER_PROCESS_FAIL
