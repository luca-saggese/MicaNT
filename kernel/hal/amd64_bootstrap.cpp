#include "micant/km/hal/amd64_bootstrap.hpp"
#include "micant/km/hal/cpu.hpp"
#include "micant/shared/user_graphics.hpp"
#include "micant/shared/user_syscall.hpp"
#include "micant/shared/win32k_client.h"
#include "micant/km/executive/mm.hpp"
#include "micant/km/executive/usermode.hpp"
#include "micant/km/core/ke.hpp"

namespace {

constexpr uint64_t kPresent = 1;
constexpr uint64_t kWritable = 2;
constexpr uint64_t kUser = 4;
constexpr uint64_t kNoExecute = uint64_t{1} << 63;
constexpr uint64_t kPhysicalMask = 0x000FFFFFFFFFF000ULL;
constexpr uint64_t kOwnedTable = uint64_t{1} << 9;
constexpr uint64_t kUserBase = uint64_t{128} << 39;
constexpr uint64_t kUserTebBase = kUserBase + 0x8000;
constexpr uint64_t kDeviceBase = uint64_t{129} << 39;
constexpr uint64_t kTestBase = uint64_t{130} << 39;
constexpr uint64_t kUserStackBase = kUserBase + 4096;
alignas(4096) uint64_t g_pml4[512]{};
uint64_t g_deviceNext = kDeviceBase;
bool g_addressSpaceReady{};
micant::hal::amd64::ProcessPagingRoot* g_processPagingRoots{};
bool g_ownedSharedRootSlots[2]{};
alignas(4096) uint8_t g_userCode[4096]{};
alignas(4096) uint8_t g_userStack[4 * 4096]{};
struct alignas(4096) BootstrapTebStorage {
    micant::um::Teb64 teb{};
    uint8_t padding[8192 - sizeof(micant::um::Teb64)]{};
};
static_assert(sizeof(BootstrapTebStorage) == 8192);
BootstrapTebStorage g_userTebStorage{};
alignas(4096) uint8_t g_nativeProbeImage[8192]{};
alignas(4096) uint8_t g_ntdllProbeImage[0x20000]{};
const uint8_t g_ntdllProbeFile[]{
#include "ntdll-probe.inc"
};
uint8_t g_nativeProbeFile[512 + 4096]{};
micant::um::NativeUserImageSet64 g_nativeProbeModules{};
alignas(16) uint8_t g_kernelStack[32768]{};
volatile bool g_probeSucceeded{};
uint32_t g_probeMode{};
uint64_t g_probeReturnRip{};
micant::hal::amd64::UserServiceRoutine g_dispatch{};
bool g_schedulerClockActive{};
bool g_clockIrqlMaskReady{};
alignas(16) uint8_t g_syscallStack[65536]{};
struct SyscallCpuState {
    uint64_t userRsp;
    uint64_t kernelRsp;
};
SyscallCpuState g_syscallCpu{};
struct SyscallFrame {
    uint64_t r15, r14, r13, r12, r10, r9, r8, rdx;
    uint64_t rsi, rdi, rbp, rbx, rax;
    uint64_t rip, cs, flags, rsp, ss;
};
static_assert(sizeof(SyscallFrame) == 144);
static_assert(offsetof(SyscallFrame, rax) == 96);
static_assert(offsetof(SyscallFrame, rip) == 104);
void (*g_log)(const char*) noexcept{};

#pragma pack(push, 1)
struct InterruptGate {
    uint16_t offsetLow{};
    uint16_t selector{};
    uint8_t ist{};
    uint8_t access{};
    uint16_t offsetMiddle{};
    uint32_t offsetHigh{};
    uint32_t reserved{};
};
struct Idtr {
    uint16_t limit;
    uint64_t base;
};
#pragma pack(pop)

alignas(16) InterruptGate g_idt[256]{};
alignas(16) InterruptGate g_managedFaultIdt[256]{};
uintptr_t g_managedFaultAddress{};
uintptr_t g_managedFaultRip{};
uintptr_t g_managedFaultResume{};
uint64_t g_managedFaultError{};
volatile bool g_managedFaultArmed{};
volatile bool g_managedFaultObserved{};

uint64_t walk(uint64_t root, uint64_t address) noexcept {
    auto* table = reinterpret_cast<const uint64_t*>(root & kPhysicalMask);
    for (int shift = 39; shift >= 12; shift -= 9) {
        const uint64_t entry = table[(address >> shift) & 511];
        if ((entry & kPresent) == 0) {
            return 0;
        }
        if ((shift == 30 || shift == 21) && (entry & 128)) {
            const uint64_t offsetMask = (uint64_t{1} << shift) - 1;
            return (entry & kPhysicalMask & ~offsetMask) | (address & offsetMask);
        }
        if (shift == 12) {
            return (entry & kPhysicalMask) | (address & 4095);
        }
        table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
    }
    return 0;
}

uint64_t* activePagingRoot() noexcept {
    uint64_t cr3{};
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(cr3));
    return reinterpret_cast<uint64_t*>(cr3 & kPhysicalMask);
}

bool mappedRange(uint64_t address, size_t size, bool user, bool writable,
                 bool executable) noexcept {
    if (!g_addressSpaceReady || !size || size - 1 > UINT64_MAX - address) {
        return false;
    }
    const uint64_t end = address + size - 1;
    const uint64_t region = address >> 47;
    if ((region != 0 && region != 0x1FFFF) || (end >> 47) != region) {
        return false;
    }
    for (uint64_t page = address & ~uint64_t{4095};; page += 4096) {
        const uint64_t* table = activePagingRoot();
        bool userAccessible = true;
        for (int shift = 39; shift >= 12; shift -= 9) {
            const uint64_t entry = table[(page >> shift) & 511];
            userAccessible = userAccessible && (entry & kUser);
            if (!(entry & kPresent) || (user && !userAccessible) ||
                (writable && !(entry & kWritable)) ||
                (executable && (entry & kNoExecute))) {
                return false;
            }
            if (shift > 12 && (entry & 128)) {
                if (user || (shift != 30 && shift != 21)) return false;
                break;
            }
            if (shift == 12) break;
            table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
        }
        if (userAccessible != user) return false;
        if (end - page < 4096) return true;
    }
}

bool bootstrapPagingContext() noexcept {
    // The bootstrap has only its BSP online; no remote TLB shootdown exists.
    uint64_t flags{}, cr3{};
    __asm__ __volatile__("pushfq; popq %0; movq %%cr3, %1"
                         : "=r"(flags), "=r"(cr3) : : "memory");
    if (!g_addressSpaceReady || (flags & (uint64_t{1} << 9)) ||
        micant::mm::PfnDatabase::get().getTotalPages() == 0) return false;
    if ((cr3 & kPhysicalMask) == reinterpret_cast<uint64_t>(g_pml4)) return true;
    for (const auto* root = g_processPagingRoots; root; root = root->next)
        if ((root->pfn << micant::mm::PageShift4KB) == (cr3 & kPhysicalMask)) return true;
    return false;
}

bool pagingContextMatches(void* context) noexcept {
    if (!bootstrapPagingContext()) return false;
    const auto active = reinterpret_cast<uint64_t>(activePagingRoot());
    if (!context) return active == reinterpret_cast<uint64_t>(g_pml4);
    for (const auto* root = g_processPagingRoots; root; root = root->next)
        if (root == context) return (root->pfn << micant::mm::PageShift4KB) == active;
    return false;
}

void invalidatePage(uint64_t address) noexcept {
    __asm__ __volatile__("invlpg (%0)" : : "r"(address) : "memory");
}

bool ownedAddress(uint64_t address, bool user) noexcept {
    const uint64_t slot = address >> 39;
    return user ? slot == 128 : slot == 129 || slot == 130;
}

bool emptyTable(const uint64_t* table) noexcept {
    for (size_t i = 0; i < 512; ++i) {
        if (table[i] != 0) return false;
    }
    return true;
}

bool mapPage(uint64_t address, uint64_t physical, uint64_t flags,
             int failAfter = -1) noexcept {
    if (!bootstrapPagingContext() || (address & 4095) || (physical & 4095) ||
        physical > kPhysicalMask || !ownedAddress(address, (flags & kUser) != 0)) {
        return false;
    }
    auto& database = micant::mm::PfnDatabase::get();
    uint64_t* createdEntries[3]{};
    uint64_t createdPfns[3]{};
    size_t createdCount{};
    uint64_t* table = activePagingRoot();
    for (int shift = 39; shift > 12; shift -= 9) {
        uint64_t& entry = table[(address >> shift) & 511];
        if (entry == 0) {
            const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
            if (!pfn) goto rollback;
            if (failAfter > 0) --failAfter;
            const uint64_t page = pfn << 12;
            if (page > kPhysicalMask || !mappedRange(page, 4096, false, true, false) ||
                walk(reinterpret_cast<uint64_t>(activePagingRoot()), page) != page) {
                database.freePage(pfn);
                goto rollback;
            }
            database.zeroPage(pfn);
            entry = page | kPresent | kWritable | kOwnedTable | (flags & kUser);
            createdEntries[createdCount] = &entry;
            createdPfns[createdCount++] = pfn;
        }
        if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) ||
            !(entry & kWritable) || ((entry & kUser) != (flags & kUser)) ||
            (entry & kNoExecute)) {
            goto rollback;
        }
        table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
    }
    {
        uint64_t& leaf = table[(address >> 12) & 511];
        if (leaf != 0) goto rollback;
        const auto pte = micant::mm::PageTableEntry::create(
            (physical & kPhysicalMask) >> 12, (flags & kWritable) != 0,
            (flags & kUser) != 0, (flags & kNoExecute) != 0);
        leaf = pte.value | (flags & ~(kPresent | kWritable | kUser | kNoExecute));
        invalidatePage(address);
        return true;
    }
rollback:
    while (createdCount) {
        --createdCount;
        *createdEntries[createdCount] = 0;
        invalidatePage(address);
        database.freePage(createdPfns[createdCount]);
    }
    return false;
}

bool unmapPage(uint64_t address, bool user) noexcept {
    if (!bootstrapPagingContext() || (address & 4095) || !ownedAddress(address, user)) {
        return false;
    }
    uint64_t* parents[3]{};
    uint64_t* tables[3]{};
    uint64_t* table = activePagingRoot();
    size_t level{};
    for (int shift = 39; shift > 12; shift -= 9) {
        uint64_t* entry = &table[(address >> shift) & 511];
        if (!(*entry & kPresent) || !(*entry & kOwnedTable) || (*entry & 128) ||
            ((*entry & kUser) != (user ? kUser : 0))) return false;
        parents[level] = entry;
        table = reinterpret_cast<uint64_t*>(*entry & kPhysicalMask);
        tables[level++] = table;
    }
    uint64_t& leaf = table[(address >> 12) & 511];
    if (!(leaf & kPresent)) return false;
    leaf = 0;
    invalidatePage(address);
    auto& database = micant::mm::PfnDatabase::get();
    while (level && emptyTable(tables[level - 1])) {
        // Process roots share these supervisor branches. Keep their top-level
        // tables pinned while any process root can still reference them.
        const uint64_t slot = address >> 39;
        if (level == 1 && g_processPagingRoots && !user && (slot == 129 || slot == 130)) break;
        --level;
        const uint64_t pfn = (*parents[level] & kPhysicalMask) >> 12;
        *parents[level] = 0;
        invalidatePage(address);
        database.freePage(pfn);
    }
    return true;
}

[[noreturn]] void stopOnTrap() noexcept {
    if (g_log) {
        g_log("[MicaNT Hal] FATAL: unexpected bootstrap trap\r\n");
    }
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

void logTrapValue(const char* label, uint64_t value) noexcept {
    if (!g_log) return;
    constexpr char digits[] = "0123456789ABCDEF";
    char message[64]{};
    size_t index = 0;
    while (label[index] && index < sizeof(message) - 19) {
        message[index] = label[index];
        ++index;
    }
    for (int shift = 60; shift >= 0; shift -= 4) {
        message[index++] = digits[(value >> shift) & 0xF];
    }
    message[index++] = '\r';
    message[index++] = '\n';
    g_log(message);
}

} // namespace

extern "C" {
volatile uint64_t MicantTimerTicks{};
volatile uint32_t* MicantTimerEoi{};
volatile bool MicantTimerPreemptionEnabled{};
volatile bool MicantTimerDispatchFailed{};
micant::ke::KThread* MicantTimerBootThread{};
bool MicantTimerBootQueued{};

void MicantBootstrapTimerDispatch() noexcept {
    MicantTimerTicks = MicantTimerTicks + 1;
    *MicantTimerEoi = 0;
    if (!MicantTimerPreemptionEnabled) return;
    using namespace micant::ke;
    const KIRQL previous = KeGetCurrentIrql();
    if (previous >= DISPATCH_LEVEL) return;
    KfRaiseIrql(DISPATCH_LEVEL);
    auto* current = KeGetCurrentThread();
    auto& scheduler = PriorityScheduler::get();
    const uintptr_t expectedSyscallStack = current && current->kernelStackTop ? current->kernelStackTop :
        reinterpret_cast<uintptr_t>(g_syscallStack + sizeof(g_syscallStack));
    const uintptr_t expectedInterruptStack = current && current->kernelStackTop ? current->kernelStackTop :
        reinterpret_cast<uintptr_t>(g_kernelStack + sizeof(g_kernelStack));
    if (!current || g_syscallCpu.kernelRsp != expectedSyscallStack ||
        micant::cpu::CpuHardwareEngine::get().getTss().rsp0 != expectedInterruptStack)
        MicantTimerDispatchFailed = true;
    if (MicantTimerBootThread && MicantTimerTicks >= 24 && !MicantTimerBootQueued) {
        if (!scheduler.readyKThread(MicantTimerBootThread)) MicantTimerDispatchFailed = true;
        else MicantTimerBootQueued = true;
    }
    auto* next = scheduler.selectOnClockTick(current);
    if (next && !KiSwapContext(current, next, true)) {
        if (!scheduler.cancelQuantumPreemption(current, next)) __builtin_trap();
        MicantTimerDispatchFailed = true;
    }
    KeLowerIrql(previous);
}

__attribute__((naked)) void MicantBootstrapTimerInterrupt() noexcept {
    __asm__ __volatile__(
        "cld\n"
        "pushq %rax\npushq %rcx\npushq %rdx\npushq %rbx\npushq %rbp\n"
        "pushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %r12\nsubq $544, %rsp\nandq $-16, %rsp\n"
        "fxsave64 32(%rsp)\ncallq MicantBootstrapTimerDispatch\n"
        "fxrstor64 32(%rsp)\nmovq %r12, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\n"
        "popq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rbx\n"
        "popq %rdx\npopq %rcx\npopq %rax\niretq\n");
}

__attribute__((naked)) void MicantBootstrapSpuriousInterrupt() noexcept {
    __asm__ __volatile__("iretq\n");
}

extern char MicantManagedReadFault[], MicantManagedReadResume[];
extern char MicantManagedWriteFault[], MicantManagedWriteResume[];

__attribute__((naked)) void MicantManagedReadProbe(uintptr_t) noexcept {
    __asm__ __volatile__(
        ".globl MicantManagedReadFault\nMicantManagedReadFault:\n"
        "movb (%rcx), %al\n"
        ".globl MicantManagedReadResume\nMicantManagedReadResume:\nretq\n");
}

__attribute__((naked)) void MicantManagedWriteProbe(uintptr_t) noexcept {
    __asm__ __volatile__(
        ".globl MicantManagedWriteFault\nMicantManagedWriteFault:\n"
        "movb $0x5a, (%rcx)\n"
        ".globl MicantManagedWriteResume\nMicantManagedWriteResume:\nretq\n");
}

void MicantManagedPageFault(uint64_t* frame) noexcept {
    uint64_t address{};
    __asm__ __volatile__("movq %%cr2, %0" : "=r"(address));
    if (!g_managedFaultArmed || address != g_managedFaultAddress ||
        frame[0] != g_managedFaultError || frame[1] != g_managedFaultRip ||
        (frame[2] & 3) != 0) {
        if (g_log) g_log("[MicaNT Mm] Unexpected managed probe page fault\r\n");
        logTrapValue("[MicaNT Mm] PF CR2=0x", address);
        logTrapValue("[MicaNT Mm] PF error=0x", frame[0]);
        logTrapValue("[MicaNT Mm] PF RIP=0x", frame[1]);
        logTrapValue("[MicaNT Mm] PF CS=0x", frame[2]);
        stopOnTrap();
    }
    g_managedFaultArmed = false;
    g_managedFaultObserved = true;
    frame[1] = g_managedFaultResume;
}

__attribute__((naked)) void MicantManagedPageFaultEntry() {
    __asm__ __volatile__(
        "cld\n"
        "pushq %rax\npushq %rcx\npushq %rdx\n"
        "pushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\n"
        "leaq 64(%rsp), %rcx\nmovq %rsp, %r12\n"
        "andq $-16, %rsp\nsubq $32, %rsp\ncallq MicantManagedPageFault\n"
        "movq %r12, %rsp\n"
        "popq %r12\npopq %r11\npopq %r10\npopq %r9\n"
        "popq %r8\npopq %rdx\npopq %rcx\npopq %rax\n"
        "addq $8, %rsp\niretq\n");
}

uint64_t MicantProbeKernelRsp{};
uint64_t MicantProbeUserRip = kUserBase;
uint64_t MicantProbeUserRsp =
    kUserStackBase + sizeof(g_userStack) - 16;

void MicantBootstrapDispatch(SyscallFrame* frame) noexcept {
    using micant::hal::amd64::bootstrapUserRange;
    if (!g_dispatch) {
        if (g_log) g_log("[MicaNT Hal] Bootstrap syscall dispatcher is unset\r\n");
        stopOnTrap();
    }
    if (!bootstrapUserRange(frame->rip, 1, false, true)) {
        if (g_log) g_log("[MicaNT Hal] Syscall return address is not user executable\r\n");
        stopOnTrap();
    }
    if (!bootstrapUserRange(frame->rsp, sizeof(uint64_t), true)) {
        if (g_log) g_log("[MicaNT Hal] Syscall stack pointer is not user writable\r\n");
        stopOnTrap();
    }
    const micant::hal::amd64::UserServiceArguments args{
        frame->rax, frame->r10, frame->rdx, frame->r8, frame->r9, frame->rsp
    };
    frame->rax = g_dispatch(args);
    // Preserve user IF only after the BSP scheduler clock owns IRQ routing.
    frame->flags = (frame->flags & (g_schedulerClockActive ? 0xED5 : 0xCD5)) | 2;
}

__attribute__((naked)) void MicantBootstrapSyscallEntry() {
    __asm__ __volatile__(
        "swapgs\n"
        "movq %rsp, %gs:0\nmovq %gs:8, %rsp\n"
        "pushq $0x2b\npushq %gs:0\npushq %r11\npushq $0x33\npushq %rcx\n"
        "pushq %rax\npushq %rbx\npushq %rbp\npushq %rdi\npushq %rsi\n"
        "pushq %rdx\npushq %r8\npushq %r9\npushq %r10\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq %rsp, %rcx\nsubq $288, %rsp\n"
        "movdqu %xmm0, 32(%rsp)\nmovdqu %xmm1, 48(%rsp)\n"
        "movdqu %xmm2, 64(%rsp)\nmovdqu %xmm3, 80(%rsp)\n"
        "movdqu %xmm4, 96(%rsp)\nmovdqu %xmm5, 112(%rsp)\n"
        "movdqu %xmm6, 128(%rsp)\nmovdqu %xmm7, 144(%rsp)\n"
        "movdqu %xmm8, 160(%rsp)\nmovdqu %xmm9, 176(%rsp)\n"
        "movdqu %xmm10, 192(%rsp)\nmovdqu %xmm11, 208(%rsp)\n"
        "movdqu %xmm12, 224(%rsp)\nmovdqu %xmm13, 240(%rsp)\n"
        "movdqu %xmm14, 256(%rsp)\nmovdqu %xmm15, 272(%rsp)\n"
        "callq MicantBootstrapDispatch\n"
        "movdqu 32(%rsp), %xmm0\nmovdqu 48(%rsp), %xmm1\n"
        "movdqu 64(%rsp), %xmm2\nmovdqu 80(%rsp), %xmm3\n"
        "movdqu 96(%rsp), %xmm4\nmovdqu 112(%rsp), %xmm5\n"
        "movdqu 128(%rsp), %xmm6\nmovdqu 144(%rsp), %xmm7\n"
        "movdqu 160(%rsp), %xmm8\nmovdqu 176(%rsp), %xmm9\n"
        "movdqu 192(%rsp), %xmm10\nmovdqu 208(%rsp), %xmm11\n"
        "movdqu 224(%rsp), %xmm12\nmovdqu 240(%rsp), %xmm13\n"
        "movdqu 256(%rsp), %xmm14\nmovdqu 272(%rsp), %xmm15\n"
        "addq $288, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\n"
        "popq %r10\npopq %r9\npopq %r8\npopq %rdx\n"
        "popq %rsi\npopq %rdi\npopq %rbp\npopq %rbx\npopq %rax\n"
        "swapgs\niretq\n"
    );
}

extern const uint8_t MicantSyscallProbeEnd[];
extern const uint8_t MicantSyscallClientEnd[];
extern const uint8_t MicantSyscallClientBreakpoint[];

__attribute__((noinline, section(".uclient$a")))
void MicantSyscallClientProgram() {
    auto* video = reinterpret_cast<micant::um::UserVideoInfo64*>(kUserBase + 4096);
    auto* message = reinterpret_cast<char*>(kUserBase + 4096 + 64);
    if (micant::sys::invokeUserService(0x80, reinterpret_cast<uint64_t>(video)) ||
        video->width != 1024 || video->height != 768 || video->physicalBase) {
        __builtin_trap();
    }
    auto* request = reinterpret_cast<MicaWin32kRequest*>(kUserBase + 8192);
    auto clearRequest = [request]() {
        auto* bytes = reinterpret_cast<volatile uint8_t*>(request);
        for (size_t index = 0; index < sizeof(*request); ++index) {
            bytes[index] = 0;
        }
    };
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_REGISTER_CLASS;
    volatile uint16_t* className = request->className;
    className[0] = 'M'; className[1] = 'i'; className[2] = 'c';
    className[3] = 'a'; className[4] = 'N'; className[5] = 'T';
    className[6] = '.'; className[7] = 'P'; className[8] = 'r';
    className[9] = 'o'; className[10] = 'b'; className[11] = 'e';
    className[12] = 'W'; className[13] = 'i'; className[14] = 'n';
    className[15] = 'd'; className[16] = 'o'; className[17] = 'w';
    className[18] = 0;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->result == 0) {
        __builtin_trap();
    }
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_CREATE_WINDOW;
    className = request->className;
    className[0] = 'M'; className[1] = 'i'; className[2] = 'c';
    className[3] = 'a'; className[4] = 'N'; className[5] = 'T';
    className[6] = '.'; className[7] = 'P'; className[8] = 'r';
    className[9] = 'o'; className[10] = 'b'; className[11] = 'e';
    className[12] = 'W'; className[13] = 'i'; className[14] = 'n';
    className[15] = 'd'; className[16] = 'o'; className[17] = 'w';
    className[18] = 0;
    volatile uint16_t* title = request->title;
    title[0] = 'C'; title[1] = 'P'; title[2] = 'L'; title[3] = '3';
    title[4] = ' '; title[5] = 'W'; title[6] = 'i'; title[7] = 'n';
    title[8] = '3'; title[9] = '2'; title[10] = 'k'; title[11] = ' ';
    title[12] = 'P'; title[13] = 'r'; title[14] = 'o'; title[15] = 'b';
    title[16] = 'e'; title[17] = 0;
    request->args[1] = 0x10000000;
    request->args[2] = 10;
    request->args[3] = 20;
    request->args[4] = 320;
    request->args[5] = 240;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->result == 0) {
        __builtin_trap();
    }
    const uint64_t window = request->result;
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_GET_CLIENT_RECT;
    request->args[0] = window;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->rect.right != 320 || request->rect.bottom != 240) {
        __builtin_trap();
    }
    clearRequest();
    request->size = sizeof(*request);
    request->version = MICANT_WIN32K_REQUEST_VERSION;
    request->operation = MICANT_WIN32K_OP_DESTROY_WINDOW;
    request->args[0] = window;
    if (micant::sys::invokeUserService(
            0x86, reinterpret_cast<uint64_t>(request)) != 0 ||
        request->result != 1) {
        __builtin_trap();
    }
    message[0] = 'C'; message[1] = 'P'; message[2] = 'L';
    message[3] = '3'; message[4] = '\r'; message[5] = '\n';
    if (micant::sys::invokeUserService(0x25, reinterpret_cast<uint64_t>(message), 6)) {
        __builtin_trap();
    }
    // The loader resolves these addresses from the actual NTDLL PE exports.
    auto* exports = reinterpret_cast<const uint64_t*>(kUserBase + 4096 + 768);
    using Create = int32_t (*)(void**, uint32_t, void*, int64_t*, uint32_t, uint32_t, void*);
    using Map = int32_t (*)(void*, void*, void**, uintptr_t, size_t, int64_t*, size_t*, uint32_t, uint32_t, uint32_t);
    using Unmap = int32_t (*)(void*, void*);
    using Close = int32_t (*)(void*);
    using Allocate = int32_t (*)(void*, void**, uintptr_t, size_t*, uint32_t, uint32_t);
    using Free = int32_t (*)(void*, void**, size_t*, uint32_t);
    using CreateEvent = int32_t (*)(void**, uint32_t, void*, uint32_t, uint8_t);
    using SetEvent = int32_t (*)(void*, int32_t*);
    using Wait = int32_t (*)(void*, uint8_t, int64_t*);
    auto create = reinterpret_cast<Create>(exports[0]);
    auto map = reinterpret_cast<Map>(exports[1]);
    auto unmap = reinterpret_cast<Unmap>(exports[2]);
    auto close = reinterpret_cast<Close>(exports[3]);
    auto allocate = reinterpret_cast<Allocate>(exports[4]);
    auto freeMemory = reinterpret_cast<Free>(exports[5]);
    auto createEvent = reinterpret_cast<CreateEvent>(exports[6]);
    auto setEvent = reinterpret_cast<SetEvent>(exports[7]);
    auto wait = reinterpret_cast<Wait>(exports[8]);
    void* event{};
    int64_t poll = 0;
    int32_t previous = -1;
    if (createEvent(nullptr, 0x100002, nullptr, 1, 0) != static_cast<int32_t>(0xc0000005u) ||
        createEvent(&event, 0x100002, nullptr, 1, 0) || !event ||
        setEvent(event, reinterpret_cast<int32_t*>(1)) != static_cast<int32_t>(0xc0000005u) ||
        wait(event, 0, reinterpret_cast<int64_t*>(1)) != static_cast<int32_t>(0xc0000005u) ||
        wait(event, 0, nullptr) != static_cast<int32_t>(0xc0000002u) ||
        wait(event, 0, &poll) != 0x102 || setEvent(event, &previous) || previous != 0 ||
        wait(event, 0, &poll) || wait(event, 0, &poll) != 0x102 ||
        close(event) || setEvent(event, nullptr) != static_cast<int32_t>(0xc0000008u)) __builtin_trap();
    void* section{};
    void* first{};
    void* second{};
    int64_t maximum = 4096;
    size_t firstSize = 4096, secondSize = 4096;
    micant::ObjectAttributes attributes{};
    auto* current = reinterpret_cast<void*>(~uint64_t{0});
    void* privateMemory{};
    size_t privateSize = 37;
    if (allocate(current, nullptr, 0, &privateSize, 0x3000, 4) != static_cast<int32_t>(0xc0000005u) ||
        allocate(current, &privateMemory, 0, &privateSize, 0x3000, 4) ||
        !privateMemory || privateSize != 4096 ||
        *static_cast<volatile uint64_t*>(privateMemory) != 0) __builtin_trap();
    *static_cast<volatile uint64_t*>(privateMemory) = 0x123456789abcdef0ULL;
    if (*static_cast<volatile uint64_t*>(privateMemory) != 0x123456789abcdef0ULL) __builtin_trap();
    size_t invalidRelease = 1;
    if (freeMemory(current, &privateMemory, &invalidRelease, 0x8000) != static_cast<int32_t>(0xc000000du) ||
        invalidRelease != 1 || *static_cast<volatile uint64_t*>(privateMemory) != 0x123456789abcdef0ULL)
        __builtin_trap();
    size_t decommitSize = 1;
    if (freeMemory(current, &privateMemory, &decommitSize, 0x4000) || decommitSize != 4096)
        __builtin_trap();
    size_t recommitSize = 4096;
    if (allocate(current, &privateMemory, 0, &recommitSize, 0x1000, 4) ||
        *static_cast<volatile uint64_t*>(privateMemory) != 0) __builtin_trap();
    size_t releaseSize = 0;
    if (freeMemory(current, &privateMemory, &releaseSize, 0x8000) || releaseSize != 4096)
        __builtin_trap();
    if (create(nullptr, 4, nullptr, &maximum, 4, 0x08000000, nullptr) !=
            static_cast<int32_t>(0xc0000005u) ||
        create(&section, 4, &attributes, &maximum, 4, 0x08000000, nullptr) ||
        map(section, current, &first, 0, 0, nullptr, &firstSize, 2, 0, 4) !=
            static_cast<int32_t>(0xc0000022u) ||
        map(section, current, &first, 0, 0, nullptr, &firstSize, 1, 0, 2) !=
            static_cast<int32_t>(0xc0000002u) ||
        first || close(section)) __builtin_trap();
    section = nullptr;
    if (create(&section, 6, nullptr, &maximum, 4, 0x08000000, nullptr) ||
        map(section, current, &first, 0, 0, nullptr, &firstSize, 2, 0, 4) ||
        map(section, current, &second, 0, 0, nullptr, &secondSize, 2, 0, 2)) __builtin_trap();
    *static_cast<volatile uint64_t*>(first) = 0x12345678;
    if (*static_cast<volatile uint64_t*>(second) != 0x12345678 ||
        unmap(current, first) || close(section) ||
        *static_cast<volatile uint64_t*>(second) != 0x12345678 ||
        unmap(current, second) ||
        unmap(current, second) != static_cast<int32_t>(0xc0000019u)) __builtin_trap();
    __asm__ __volatile__(
        ".globl MicantSyscallClientBreakpoint\nMicantSyscallClientBreakpoint:\n"
        "int3\nud2\n"
    );
    __builtin_unreachable();
}

__asm__(".section .uclient$z,\"xr\"\n"
        ".globl MicantSyscallClientEnd\nMicantSyscallClientEnd:\n.text\n");

__attribute__((naked)) void MicantSyscallProbeProgram() {
    __asm__ __volatile__(
        "movabsq $0x400000001000, %r10\n"
        "movq $0x1234, %rdx\nmovq $0x5678, %r8\nmovq $0x9abc, %r9\n"
        "movq $0x1357, %rbx\nmovq $0x2468, %r12\n"
        "movq %rdx, %xmm0\nmovq %r8, %xmm6\nmovq %r9, %xmm15\n"
        "std\nmovl $0x80, %eax\nsyscall\ncld\n"
        "testq %rax, %rax\njnz 9f\n"
        "cmpl $1024, (%r10)\njne 9f\ncmpl $768, 4(%r10)\njne 9f\n"
        "cmpq $0, 24(%r10)\njne 9f\n" // No physical display pointer escapes.
        "cmpq $0x1234, %rdx\njne 9f\ncmpq $0x5678, %r8\njne 9f\n"
        "cmpq $0x9abc, %r9\njne 9f\ncmpq $0x1357, %rbx\njne 9f\n"
        "cmpq $0x2468, %r12\njne 9f\n"
        "movq %xmm0, %rax\ncmpq %rdx, %rax\njne 9f\n"
        "movq %xmm6, %rax\ncmpq %r8, %rax\njne 9f\n"
        "movq %xmm15, %rax\ncmpq %r9, %rax\njne 9f\n"
        "xorq %r10, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Null output.
        "movabsq $0x400000000000, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Read-only code.
        "movabsq $0x400000004ff0, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Crosses into an unmapped page.
        "movq $0x1000, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Supervisor address.
        "movq $-16, %r10\nmovl $0x80, %eax\nsyscall\n"
        "cmpl $0xc0000005, %eax\njne 9f\n" // Wraparound.
        "movabsq $0x400000001000, %r10\n"
        "movabsq $0x00000a0d334c5043, %rax\nmovq %rax, 64(%r10)\n"
        "addq $64, %r10\nmovq $6, %rdx\nmovl $0x25, %eax\nsyscall\n"
        "testq %rax, %rax\njnz 9f\ncmpq $6, %rdx\njne 9f\n"
        "movabsq $0x400000001000, %r10\n"
        "leaq 128(%r10), %rax\nmovq %rax, (%r10)\n"
        "movq $0, 8(%r10)\n"
        "movl $2, 16(%r10)\nmovl $2, 20(%r10)\n"
        "movl $2, 24(%r10)\nmovl $0, 28(%r10)\n"
        "movabsq $0xff112233ff112233, %rax\n"
        "movq %rax, 128(%r10)\nmovq %rax, 136(%r10)\n"
        "movl $0x81, %eax\nsyscall\ntestq %rax, %rax\njnz 9f\n"
        "movl $0, 16(%r10)\nmovl $0x81, %eax\nsyscall\n"
        "cmpl $0xc000000d, %eax\njne 9f\n" // Zero width.
        "movl $2, 16(%r10)\nmovq $0, (%r10)\n"
        "movl $0x81, %eax\nsyscall\ncmpl $0xc0000005, %eax\njne 9f\n"
        "movabsq $0x400000004ff8, %rax\nmovq %rax, (%r10)\n"
        "movl $0x81, %eax\nsyscall\ncmpl $0xc0000005, %eax\njne 9f\n"
        // Windows x64 syscall stack slots: return address, home area, then
        // arguments five onward. The probe supplies this frame explicitly.
        "subq $96, %rsp\n"
        "movabsq $0x400000001100, %rbx\n"
        "movq $4096, 8(%rbx)\nmovq $0, 16(%rbx)\nmovq $0, 24(%rbx)\n"
        "movq $4096, 32(%rbx)\nmovq $4096, 40(%rbx)\n"
        "movq $4, 40(%rsp)\nmovq $0x08000000, 48(%rsp)\nmovq $0, 56(%rsp)\n"
        // A read-only handle cannot create a writable view.
        "leaq 0(%rbx), %r10\nmovq $4, %rdx\nxorq %r8, %r8\nleaq 8(%rbx), %r9\n"
        "movl $0x4a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $0, 40(%rsp)\nmovq $0, 48(%rsp)\nleaq 32(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq $2, 64(%rsp)\nmovq $0, 72(%rsp)\nmovq $4, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 16(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ncmpl $0xc0000022, %eax\njne 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $4, 40(%rsp)\nmovq $0x08000000, 48(%rsp)\nmovq $0, 56(%rsp)\n"
        "leaq 0(%rbx), %r10\nmovq $6, %rdx\nxorq %r8, %r8\nleaq 8(%rbx), %r9\n"
        "movl $0x4a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $0, 40(%rsp)\nmovq $0, 48(%rsp)\nleaq 32(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq $2, 64(%rsp)\nmovq $0, 72(%rsp)\nmovq $4, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 16(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "leaq 40(%rbx), %rax\nmovq %rax, 56(%rsp)\nmovq $2, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 24(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 16(%rbx), %rax\nmovq $0x12345678, (%rax)\n"
        "movq 24(%rbx), %rax\ncmpq $0x12345678, (%rax)\njne 9f\n"
        "movq $-1, %r10\nmovq 16(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ncmpl $0xc0000008, %eax\njne 9f\n"
        "movq 24(%rbx), %rax\ncmpq $0x12345678, (%rax)\njne 9f\n"
        "movq $-1, %r10\nmovq 24(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        // Map the first and seventeenth pages of a larger section. The input
        // offset is rounded down to the 64 KiB allocation granularity.
        "movq $69632, 8(%rbx)\nmovq $0, 16(%rbx)\nmovq $0, 24(%rbx)\n"
        "movq $4096, 32(%rbx)\nmovq $4096, 40(%rbx)\nmovq $65537, 48(%rbx)\n"
        "movq $4, 40(%rsp)\nmovq $0x08000000, 48(%rsp)\nmovq $0, 56(%rsp)\n"
        "leaq 0(%rbx), %r10\nmovq $6, %rdx\nxorq %r8, %r8\nleaq 8(%rbx), %r9\n"
        "movl $0x4a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $0, 40(%rsp)\nmovq $0, 48(%rsp)\nleaq 32(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq $2, 64(%rsp)\nmovq $0, 72(%rsp)\nmovq $4, 80(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 16(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "leaq 48(%rbx), %rax\nmovq %rax, 48(%rsp)\nleaq 40(%rbx), %rax\nmovq %rax, 56(%rsp)\n"
        "movq 0(%rbx), %r10\nmovq $-1, %rdx\nleaq 24(%rbx), %r8\nxorq %r9, %r9\n"
        "movl $0x28, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "cmpq $65536, 48(%rbx)\njne 9f\n"
        "movq 16(%rbx), %rax\nmovq $123, (%rax)\nmovq 24(%rbx), %rax\ncmpq $0, (%rax)\njne 9f\n"
        "movq $-1, %r10\nmovq 16(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq $-1, %r10\nmovq 24(%rbx), %rdx\nmovl $0x2a, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "movq 0(%rbx), %r10\nmovl $0x0f, %eax\nsyscall\ntestl %eax, %eax\njnz 9f\n"
        "addq $96, %rsp\n"
        "movl $0x7fffffff, %eax\nsyscall\n"
        "cmpl $0xc00000bb, %eax\njne 9f\n"
        "int3\n9: ud2\n"
        ".globl MicantSyscallProbeEnd\nMicantSyscallProbeEnd:\n"
    );
}

[[noreturn]] __attribute__((naked)) void MicantProbeReturn() {
    __asm__ __volatile__(
        "movq MicantProbeKernelRsp(%rip), %rsp\n"
        "movdqu 0(%rsp), %xmm6\nmovdqu 16(%rsp), %xmm7\n"
        "movdqu 32(%rsp), %xmm8\nmovdqu 48(%rsp), %xmm9\n"
        "movdqu 64(%rsp), %xmm10\nmovdqu 80(%rsp), %xmm11\n"
        "movdqu 96(%rsp), %xmm12\nmovdqu 112(%rsp), %xmm13\n"
        "movdqu 128(%rsp), %xmm14\nmovdqu 144(%rsp), %xmm15\n"
        "addq $160, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\n"
        "popq %rsi\npopq %rdi\npopq %rbp\npopq %rbx\n"
        "retq\n"
    );
}

void MicantProbeTrap(const uint64_t* frame) noexcept {
    // The trap frame is written by the CPU, not supplied by user memory.
    g_probeSucceeded = (g_probeMode == 0 || g_probeMode == 5 ||
                        g_probeMode == 6 || g_probeMode == 7) &&
                       frame[0] == g_probeReturnRip &&
                       frame[1] == micant::cpu::SELECTOR_UCODE64 &&
                       frame[4] == micant::cpu::SELECTOR_UDATA64 &&
                       (g_probeMode == 6
                           ? micant::hal::amd64::bootstrapUserRange(frame[3], 8, true)
                           : frame[3] == MicantProbeUserRsp) &&
                       (frame[2] & 0x3000) == 0;
    if (!g_probeSucceeded) {
        if (g_log) {
            if (frame[0] != g_probeReturnRip) {
                g_log("[MicaNT Hal] CPL3 probe trap RIP mismatch\r\n");
            }
            if (frame[1] != micant::cpu::SELECTOR_UCODE64 ||
                frame[4] != micant::cpu::SELECTOR_UDATA64) {
                g_log("[MicaNT Hal] CPL3 probe trap selector mismatch\r\n");
            }
            if ((g_probeMode == 6 &&
                 !micant::hal::amd64::bootstrapUserRange(frame[3], 8, true)) ||
                (g_probeMode != 6 && frame[3] != MicantProbeUserRsp)) {
                g_log("[MicaNT Hal] CPL3 probe trap stack mismatch\r\n");
            }
            if (frame[2] & 0x3000) {
                g_log("[MicaNT Hal] CPL3 probe trap privilege flags mismatch\r\n");
            }
        }
        stopOnTrap();
    }
    if (g_log && g_probeMode == 6) {
        g_log("[MicaNT Hal] CPL3 syscall probe breakpoint accepted\r\n");
    }
    MicantProbeReturn();
}

void MicantProbePageFault(const uint64_t* frame) noexcept {
    uint64_t faultAddress{};
    __asm__ __volatile__("movq %%cr2, %0" : "=r"(faultAddress));
    const uint64_t expectedAddress = g_probeMode == 1
        ? reinterpret_cast<uint64_t>(g_pml4)
        : g_probeMode == 2 ? kUserBase
        : g_probeMode == 3 ? kUserBase + 4096 : kDeviceBase;
    const uint64_t expectedError = g_probeMode == 2 ? 7 : g_probeMode == 3 ? 21 : 5;
    const uint64_t expectedRip = g_probeMode == 3 ? expectedAddress : kUserBase + 10;
    g_probeSucceeded = g_probeMode >= 1 && g_probeMode <= 4 &&
                       faultAddress == expectedAddress &&
                       frame[0] == expectedError &&
                       frame[1] == expectedRip &&
                       frame[2] == micant::cpu::SELECTOR_UCODE64 &&
                       frame[5] == micant::cpu::SELECTOR_UDATA64 &&
                       frame[4] == MicantProbeUserRsp &&
                       (frame[3] & 0x3000) == 0;
    if (!g_probeSucceeded) {
        if (g_log) {
            g_log("[MicaNT Hal] Unexpected bootstrap page fault\r\n");
        }
        logTrapValue("[MicaNT Hal] PF CR2=0x", faultAddress);
        logTrapValue("[MicaNT Hal] PF error=0x", frame[0]);
        logTrapValue("[MicaNT Hal] PF RIP=0x", frame[1]);
        logTrapValue("[MicaNT Hal] PF CS=0x", frame[2]);
        stopOnTrap();
    }
    MicantProbeReturn();
}

void MicantProbeUnexpectedInvalidOpcode() noexcept {
    if (g_log) {
        g_log("[MicaNT Hal] Unexpected bootstrap invalid opcode (#UD)\r\n");
    }
    stopOnTrap();
}

void MicantProbeUnexpectedGeneralProtection() noexcept {
    if (g_log) {
        g_log("[MicaNT Hal] Unexpected bootstrap general protection fault (#GP)\r\n");
    }
    stopOnTrap();
}

__attribute__((naked)) void MicantProbeTrapEntry() {
    __asm__ __volatile__(
        "cld\nmovq %rsp, %rcx\nandq $-16, %rsp\nsubq $32, %rsp\ncallq MicantProbeTrap\n"
        "ud2\n"
    );
}

__attribute__((naked)) void MicantProbeFaultEntry() {
    __asm__ __volatile__(
        "cli\ncld\nandq $-16, %rsp\nsubq $32, %rsp\ncallq MicantProbeUnexpectedTrap\nud2\n"
    );
}

__attribute__((naked)) void MicantProbeInvalidOpcodeEntry() {
    __asm__ __volatile__(
        "cli\ncld\nandq $-16, %rsp\nsubq $32, %rsp\n"
        "callq MicantProbeUnexpectedInvalidOpcode\nud2\n"
    );
}

__attribute__((naked)) void MicantProbeGeneralProtectionEntry() {
    __asm__ __volatile__(
        "cli\ncld\nandq $-16, %rsp\nsubq $32, %rsp\n"
        "callq MicantProbeUnexpectedGeneralProtection\nud2\n"
    );
}

void MicantProbeUnexpectedTrap() noexcept {
    stopOnTrap();
}

__attribute__((naked)) void MicantProbePageFaultEntry() {
    __asm__ __volatile__(
        "cld\nmovq %rsp, %rcx\nandq $-16, %rsp\nsubq $32, %rsp\n"
        "callq MicantProbePageFault\nud2\n"
    );
}

__attribute__((naked)) void MicantProbeEnter() {
    __asm__ __volatile__(
        "pushq %rbx\npushq %rbp\npushq %rdi\npushq %rsi\n"
        "pushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "subq $160, %rsp\n"
        "movdqu %xmm6, 0(%rsp)\nmovdqu %xmm7, 16(%rsp)\n"
        "movdqu %xmm8, 32(%rsp)\nmovdqu %xmm9, 48(%rsp)\n"
        "movdqu %xmm10, 64(%rsp)\nmovdqu %xmm11, 80(%rsp)\n"
        "movdqu %xmm12, 96(%rsp)\nmovdqu %xmm13, 112(%rsp)\n"
        "movdqu %xmm14, 128(%rsp)\nmovdqu %xmm15, 144(%rsp)\n"
        "movq %rsp, MicantProbeKernelRsp(%rip)\n"
        "movw $0x2b, %ax\nmovw %ax, %ds\nmovw %ax, %es\n"
        "pushq $0x2b\n"
        "movq MicantProbeUserRsp(%rip), %rax\npushq %rax\n"
        "pushq $2\npushq $0x33\n"
        "movq MicantProbeUserRip(%rip), %rax\npushq %rax\niretq\n"
    );
}
}

namespace micant::hal::amd64 {

void setBootstrapClockIrqlMask(uint8_t irql) noexcept {
    if (!g_clockIrqlMaskReady) return;
    // Only vector 0xd1 (APIC priority class 13) is routed at present.
    const uint64_t priority = irql >= ke::HIGH_LEVEL ? 15 :
        irql >= ke::CLOCK_LEVEL ? 13 : 0;
    __asm__ __volatile__("movq %0, %%cr8" : : "r"(priority) : "memory");
}

void setBootstrapThreadEntryStack(uintptr_t stackTop, uintptr_t userTeb) noexcept {
    g_syscallCpu.kernelRsp = stackTop ? stackTop :
        reinterpret_cast<uintptr_t>(g_syscallStack + sizeof(g_syscallStack));
    auto& hardware = cpu::CpuHardwareEngine::get();
    if (hardware.isInitialized()) {
        hardware.setKernelStack(stackTop ? stackTop :
            reinterpret_cast<uintptr_t>(g_kernelStack + sizeof(g_kernelStack)));
    }
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, userTeb);
}

uintptr_t bootstrapThreadSyscallStack() noexcept { return g_syscallCpu.kernelRsp; }

extern "C" __attribute__((naked, noreturn)) void MicantEnterBootstrapUserThread(
    uintptr_t, uintptr_t, bool) noexcept {
    __asm__ __volatile__(
        "movw $0x2b, %ax\nmovw %ax, %ds\nmovw %ax, %es\n"
        "pushq $0x2b\npushq %rdx\nmovq $2, %rax\ntestb %r8b, %r8b\njz 1f\n"
        "orq $0x200, %rax\n1: pushq %rax\npushq $0x33\npushq %rcx\niretq\n");
}

[[noreturn]] void enterBootstrapUserThread(uintptr_t entry, uintptr_t userStack, bool interruptible) noexcept {
    const auto* thread = ke::KeGetCurrentThread();
    if (!g_dispatch || (interruptible && !MicantTimerPreemptionEnabled) ||
        !thread || !thread->owningProcess || !thread->kernelStackTop ||
        g_syscallCpu.kernelRsp != thread->kernelStackTop || userStack < 8 ||
        !bootstrapUserRange(entry, 1, false, true) || !bootstrapUserRange(userStack - 8, 8, true))
        __builtin_trap();
    if (thread->tebAddress && !bootstrapUserRange(thread->tebAddress, 1, true)) __builtin_trap();
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, thread->tebAddress);
    MicantEnterBootstrapUserThread(entry, userStack, interruptible);
}

bool initializeBootstrapAddressSpace(void (*log)(const char*) noexcept) noexcept {
    g_log = log;
    if (g_addressSpaceReady) {
        return true;
    }
    uint64_t cr3{};
    uint64_t cr4{};
    __asm__ __volatile__("movq %%cr3, %0; movq %%cr4, %1" : "=r"(cr3), "=r"(cr4));
    if (cr4 & (uint64_t{1} << 12)) {
        return false;
    }
    const auto* oldRoot = reinterpret_cast<const uint64_t*>(cr3 & kPhysicalMask);
    if (oldRoot[128] || oldRoot[129] ||
        walk(cr3, reinterpret_cast<uint64_t>(g_pml4)) !=
            reinterpret_cast<uint64_t>(g_pml4)) {
        return false;
    }
    for (size_t index = 0; index < 512; ++index) {
        g_pml4[index] = oldRoot[index] & ~kUser;
    }
    cpu::cpuWrmsr(cpu::MSR_EFER, cpu::cpuRdmsr(cpu::MSR_EFER) | cpu::EFER_NXE);
    // CR3 reload alone can retain firmware global translations.
    __asm__ __volatile__("movq %0, %%cr4" : : "r"(cr4 & ~uint64_t{128}) : "memory");
    __asm__ __volatile__("movq %0, %%cr3" : : "r"(g_pml4) : "memory");
    __asm__ __volatile__("movq %0, %%cr4" : : "r"(cr4) : "memory");
    g_addressSpaceReady = true;
    return true;
}

ProcessPagingRoot::~ProcessPagingRoot() {
    if (pfn || next) __builtin_trap();
}

static void reclaimUnusedSharedRootSlots() noexcept {
    if (g_processPagingRoots) return;
    auto& database = mm::PfnDatabase::get();
    for (size_t index = 0; index < 2; ++index) {
        if (!g_ownedSharedRootSlots[index]) continue;
        uint64_t& entry = g_pml4[129 + index];
        if (!entry) { g_ownedSharedRootSlots[index] = false; continue; }
        auto* table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
        if (!emptyTable(table)) continue;
        const uint64_t pfn = (entry & kPhysicalMask) >> mm::PageShift4KB;
        entry = 0;
        invalidatePage((129 + index) << 39);
        database.freePage(pfn);
        g_ownedSharedRootSlots[index] = false;
    }
}

static bool createProcessPagingRootImpl(ProcessPagingRoot& root, int failAfter) noexcept {
    if (!bootstrapPagingContext() || root.pfn || root.next) return false;
    uint64_t cr4{};
    __asm__ __volatile__("movq %%cr4, %0" : "=r"(cr4));
    if (cr4 & (uint64_t{1} << 17)) return false; // PCID is not supported here.
    auto& database = mm::PfnDatabase::get();
    for (size_t index = 0; index < 2; ++index) {
        uint64_t& entry = g_pml4[129 + index];
        if (!entry) {
            const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
            if (!pfn) { reclaimUnusedSharedRootSlots(); return false; }
            if (failAfter > 0) --failAfter;
            const uint64_t physical = pfn << mm::PageShift4KB;
            if (physical > kPhysicalMask || !mappedRange(physical, 4096, false, true, false) ||
                walk(reinterpret_cast<uint64_t>(g_pml4), physical) != physical) {
                database.freePage(pfn);
                reclaimUnusedSharedRootSlots();
                return false;
            }
            database.zeroPage(pfn);
            entry = physical | kPresent | kWritable | kOwnedTable;
            g_ownedSharedRootSlots[index] = true;
        }
        if (!(entry & kPresent) || !(entry & kWritable) || !(entry & kOwnedTable) ||
            (entry & (kUser | kNoExecute | 128))) {
            reclaimUnusedSharedRootSlots();
            return false;
        }
    }
    const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
    if (!pfn) { reclaimUnusedSharedRootSlots(); return false; }
    const uint64_t physical = pfn << mm::PageShift4KB;
    if (physical > kPhysicalMask || !mappedRange(physical, 4096, false, true, false) ||
        walk(reinterpret_cast<uint64_t>(g_pml4), physical) != physical) {
        database.freePage(pfn);
        reclaimUnusedSharedRootSlots();
        return false;
    }
    database.zeroPage(pfn);
    auto* table = reinterpret_cast<uint64_t*>(physical);
    for (size_t i = 0; i < 512; ++i) table[i] = i == 128 ? 0 : g_pml4[i] & ~kUser;
    root.pfn = pfn;
    root.next = g_processPagingRoots;
    g_processPagingRoots = &root;
    return true;
}

bool createProcessPagingRoot(ProcessPagingRoot& root) noexcept {
    return createProcessPagingRootImpl(root, -1);
}

bool verifyProcessPagingRootRollback() noexcept {
    if (!bootstrapPagingContext() || g_processPagingRoots || g_pml4[129] || g_pml4[130])
        return false;
    const size_t active = mm::PfnDatabase::get().getActiveCount();
    for (int failure = 0; failure < 3; ++failure) {
        ProcessPagingRoot root;
        if (createProcessPagingRootImpl(root, failure) || root.pfn || root.next ||
            g_pml4[129] || g_pml4[130] || mm::PfnDatabase::get().getActiveCount() != active)
            return false;
    }
    return true;
}

bool switchProcessPagingRoot(const ProcessPagingRoot* root) noexcept {
    uint64_t flags{}, cr3{}, cr4{};
    __asm__ __volatile__("pushfq; popq %0; movq %%cr3, %1; movq %%cr4, %2"
                         : "=r"(flags), "=r"(cr3), "=r"(cr4) : : "memory");
    if (!g_addressSpaceReady || (flags & (uint64_t{1} << 9)) || (cr4 & (uint64_t{1} << 17)))
        return false;
    bool currentKnown = (cr3 & kPhysicalMask) == reinterpret_cast<uint64_t>(g_pml4);
    bool targetKnown = root == nullptr;
    for (const auto* item = g_processPagingRoots; item; item = item->next) {
        if ((item->pfn << mm::PageShift4KB) == (cr3 & kPhysicalMask)) currentKnown = true;
        if (item == root) targetKnown = true;
    }
    if (!currentKnown || !targetKnown) return false;
    const uint64_t target = root ? root->pfn << mm::PageShift4KB : reinterpret_cast<uint64_t>(g_pml4);
    __asm__ __volatile__("movq %0, %%cr3" : : "r"(target) : "memory");
    return true;
}

bool switchProcessPagingRootByPhysical(uintptr_t physicalRoot) noexcept {
    if (physicalRoot == reinterpret_cast<uintptr_t>(g_pml4))
        return switchProcessPagingRoot(nullptr);
    for (const auto* root = g_processPagingRoots; root; root = root->next) {
        if ((root->pfn << mm::PageShift4KB) == physicalRoot)
            return switchProcessPagingRoot(root);
    }
    return false;
}

bool destroyProcessPagingRoot(ProcessPagingRoot& root) noexcept {
    if (!bootstrapPagingContext() || !root.pfn) return false;
    if (reinterpret_cast<uint64_t>(activePagingRoot()) == (root.pfn << mm::PageShift4KB)) return false;
    auto** link = &g_processPagingRoots;
    while (*link && *link != &root) link = &(*link)->next;
    if (!*link) return false;
    auto* table = reinterpret_cast<const uint64_t*>(root.pfn << mm::PageShift4KB);
    if (table[128]) return false;
    *link = root.next;
    const uint64_t pfn = root.pfn;
    root.pfn = 0;
    root.next = nullptr;
    mm::PfnDatabase::get().freePage(pfn);
    reclaimUnusedSharedRootSlots();
    return true;
}

uint64_t translateBootstrapAddress(const void* address) noexcept {
    if (!g_addressSpaceReady) {
        return 0;
    }
    return walk(reinterpret_cast<uint64_t>(g_pml4),
                reinterpret_cast<uint64_t>(address));
}

bool bootstrapUserRange(uint64_t address, size_t size, bool writable,
                        bool executable) noexcept {
    if (!g_addressSpaceReady || size == 0 || address < kUserBase ||
        address >= kUserBase + (uint64_t{1} << 39) ||
        size > kUserBase + (uint64_t{1} << 39) - address) {
        return false;
    }
    return mappedRange(address, size, true, writable, executable);
}

bool bootstrapKernelRange(uint64_t address, size_t size, bool writable) noexcept {
    return mappedRange(address, size, false, writable, false);
}

bool mapBootstrapManagedPage(void* context, uintptr_t address, uint64_t pfn,
                             uint32_t protection) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (address & mm::PageMask4KB) || !pfn ||
        pfn > (kPhysicalMask >> mm::PageShift4KB) ||
        (protection != mm::PAGE_READWRITE && protection != mm::PAGE_READONLY &&
         protection != mm::PAGE_EXECUTE_READ)) {
        return false;
    }
    return mapPage(address, pfn << mm::PageShift4KB, kUser |
                   (protection == mm::PAGE_EXECUTE_READ ? 0 : kNoExecute) |
                   (protection == mm::PAGE_READWRITE ? kWritable : 0));
}

static bool mapBootstrapManagedViewImpl(void* context, uintptr_t address, const uint64_t* pfns,
                                        size_t count, uint32_t protection, int failAfter) noexcept {
    if (!pagingContextMatches(context) || !pfns || !count ||
        address < BootstrapManagedBase || (address & mm::PageMask4KB) ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        count > (BootstrapManagedSize - (address - BootstrapManagedBase)) / mm::PageSize4KB ||
        (protection != mm::PAGE_READONLY && protection != mm::PAGE_READWRITE)) return false;

    // This 16 MiB aligned window needs at most eight PTs, one PD and one
    // PDPT. Upper levels are installed without any present leaf PTEs.
    uint64_t* createdEntries[16]{};
    uint64_t createdPfns[16]{};
    size_t createdCount{};
    auto& database = mm::PfnDatabase::get();
    bool prepared = true;
    for (size_t i = 0; i < count && prepared; ++i) {
        if (!pfns[i] || pfns[i] > (kPhysicalMask >> mm::PageShift4KB)) {
            prepared = false;
            break;
        }
        const uintptr_t va = address + i * mm::PageSize4KB;
        uint64_t* table = activePagingRoot();
        for (int shift = 39; shift > 12; shift -= 9) {
            uint64_t& entry = table[(va >> shift) & 511];
            if (!entry) {
                if (createdCount == 16) { prepared = false; break; }
                const uint64_t pfn = failAfter == 0 ? 0 : database.allocatePage();
                if (!pfn) { prepared = false; break; }
                if (failAfter > 0) --failAfter;
                const uint64_t physical = pfn << 12;
                if (physical > kPhysicalMask || !mappedRange(physical, 4096, false, true, false) ||
                    walk(reinterpret_cast<uint64_t>(activePagingRoot()), physical) != physical) {
                    database.freePage(pfn);
                    prepared = false;
                    break;
                }
                database.zeroPage(pfn);
                entry = physical | kPresent | kWritable | kOwnedTable | kUser;
                createdEntries[createdCount] = &entry;
                createdPfns[createdCount++] = pfn;
            }
            if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) ||
                !(entry & kWritable) || !(entry & kUser) || (entry & kNoExecute)) {
                prepared = false;
                break;
            }
            table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
        }
        if (prepared && table[(va >> 12) & 511]) prepared = false;
    }
    if (!prepared) {
        while (createdCount) {
            --createdCount;
            *createdEntries[createdCount] = 0;
            invalidatePage(address);
            database.freePage(createdPfns[createdCount]);
        }
        return false;
    }
    // Interrupts are disabled and only the BSP is running. No leaf can change
    // between validation and publication, and this phase cannot fail.
    for (size_t i = 0; i < count; ++i) {
        const uintptr_t va = address + i * mm::PageSize4KB;
        uint64_t* table = activePagingRoot();
        for (int shift = 39; shift > 12; shift -= 9)
            table = reinterpret_cast<uint64_t*>(table[(va >> shift) & 511] & kPhysicalMask);
        table[(va >> 12) & 511] = mm::PageTableEntry::create(
            pfns[i], protection == mm::PAGE_READWRITE, true, true).value;
        invalidatePage(va);
    }
    return true;
}

bool mapBootstrapManagedView(void* context, uintptr_t address, const uint64_t* pfns,
                             size_t count, uint32_t protection) noexcept {
    return mapBootstrapManagedViewImpl(context, address, pfns, count, protection, -1);
}

bool unmapBootstrapManagedView(void* context, uintptr_t address, size_t count) noexcept {
    if (!pagingContextMatches(context) || !count || (address & mm::PageMask4KB) ||
        address < BootstrapManagedBase || address - BootstrapManagedBase >= BootstrapManagedSize ||
        count > (BootstrapManagedSize - (address - BootstrapManagedBase)) / mm::PageSize4KB)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const uintptr_t va = address + i * mm::PageSize4KB;
        const uint64_t* table = activePagingRoot();
        for (int shift = 39; shift > 12; shift -= 9) {
            const uint64_t entry = table[(va >> shift) & 511];
            if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) || !(entry & kUser))
                return false;
            table = reinterpret_cast<const uint64_t*>(entry & kPhysicalMask);
        }
        if (!(table[(va >> 12) & 511] & kPresent)) return false;
    }
    // No interrupt or concurrent CPU can alter the validated page tables.
    for (size_t i = 0; i < count; ++i) {
        if (!unmapPage(address + i * mm::PageSize4KB, true)) __builtin_trap();
    }
    return true;
}

bool verifyBootstrapManagedViewRollback() noexcept {
    auto& database = mm::PfnDatabase::get();
    const size_t active = database.getActiveCount();
    const uint64_t pfn = database.allocatePage();
    if (!pfn) return false;
    const size_t held = database.getActiveCount();
    const uint64_t pfns[]{pfn, 0};
    bool ok = true;
    for (int failAfter = 0; failAfter <= 3; ++failAfter) {
        if (mapBootstrapManagedViewImpl(nullptr, BootstrapManagedBase, pfns, 2,
                                        mm::PAGE_READWRITE, failAfter) ||
            bootstrapUserRange(BootstrapManagedBase, mm::PageSize4KB, false) ||
            database.getActiveCount() != held) {
            ok = false;
            break;
        }
    }
    if (ok) {
        const uintptr_t occupied = BootstrapManagedBase + mm::PageSize4KB;
        const uint64_t pair[]{pfn, pfn};
        if (!mapBootstrapManagedPage(nullptr, occupied, pfn, mm::PAGE_READWRITE)) {
            ok = false;
        } else {
            const size_t mapped = database.getActiveCount();
            if (unmapBootstrapManagedView(nullptr, BootstrapManagedBase, 2) ||
                mapBootstrapManagedView(nullptr, BootstrapManagedBase, pair, 2, mm::PAGE_READWRITE) ||
                bootstrapUserRange(BootstrapManagedBase, mm::PageSize4KB, false) ||
                !bootstrapUserRange(occupied, mm::PageSize4KB, true) ||
                database.getActiveCount() != mapped) ok = false;
            if (!unmapBootstrapManagedPage(nullptr, occupied)) ok = false;
        }
    }
    database.freePage(pfn);
    return ok && database.getActiveCount() == active;
}

bool unmapBootstrapManagedPage(void* context, uintptr_t address) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (address & mm::PageMask4KB)) {
        return false;
    }
    return unmapPage(address, true);
}

bool mapBootstrapProtectManagedPage(void* context, uintptr_t address,
                                    uint32_t protection) noexcept {
    if (!pagingContextMatches(context) || address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (address & mm::PageMask4KB) ||
        (protection != mm::PAGE_READONLY && protection != mm::PAGE_READWRITE &&
         protection != mm::PAGE_EXECUTE_READ)) {
        return false;
    }
    uint64_t* table = activePagingRoot();
    for (int shift = 39; shift > 12; shift -= 9) {
        const uint64_t entry = table[(address >> shift) & 511];
        if (!(entry & kPresent) || !(entry & kOwnedTable) || (entry & 128) ||
            !(entry & kUser) || !(entry & kWritable) || (entry & kNoExecute)) {
            return false;
        }
        table = reinterpret_cast<uint64_t*>(entry & kPhysicalMask);
    }
    uint64_t& leaf = table[(address >> 12) & 511];
    if (!(leaf & kPresent) || !(leaf & kUser)) return false;
    leaf = protection == mm::PAGE_READWRITE ? leaf | kWritable : leaf & ~kWritable;
    leaf = protection == mm::PAGE_EXECUTE_READ ? leaf & ~kNoExecute : leaf | kNoExecute;
    invalidatePage(address);
    return true;
}

bool bootstrapQemuFaultTestsAvailable() noexcept {
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(ecx & (uint32_t{1} << 31))) return false;
    eax = 0x40000000;
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    // QEMU's TCG hypervisor signature; the runner explicitly selects TCG.
    return eax >= 0x40000000 && ebx == 0x54474354 &&
           ecx == 0x43544743 && edx == 0x47435447;
}

bool verifyBootstrapManagedPageFault(uintptr_t address, bool write,
                                     bool present) noexcept {
    if (!bootstrapQemuFaultTestsAvailable() || !bootstrapPagingContext() ||
        address < BootstrapManagedBase ||
        address - BootstrapManagedBase >= BootstrapManagedSize ||
        (write && !present) || (!write && present) || g_managedFaultArmed) return false;

    uint16_t cs{};
    uint64_t cr0{};
    Idtr previous{};
    __asm__ __volatile__("movw %%cs, %0; movq %%cr0, %1; sidt %2"
                         : "=r"(cs), "=r"(cr0), "=m"(previous) : : "memory");
    if (previous.base == reinterpret_cast<uint64_t>(g_managedFaultIdt)) return false;
    for (size_t vector = 0; vector < 256; ++vector) {
        const uint64_t handler = vector == 14
            ? reinterpret_cast<uint64_t>(MicantManagedPageFaultEntry)
            : reinterpret_cast<uint64_t>(MicantProbeFaultEntry);
        g_managedFaultIdt[vector] = {static_cast<uint16_t>(handler), cs, 0, 0x8e,
                                    static_cast<uint16_t>(handler >> 16),
                                    static_cast<uint32_t>(handler >> 32), 0};
    }
    const Idtr idtr{sizeof(g_managedFaultIdt) - 1,
                    reinterpret_cast<uint64_t>(g_managedFaultIdt)};
    g_managedFaultAddress = address;
    g_managedFaultError = write ? 3 : 0;
    g_managedFaultRip = reinterpret_cast<uintptr_t>(write ? MicantManagedWriteFault
                                                          : MicantManagedReadFault);
    g_managedFaultResume = reinterpret_cast<uintptr_t>(write ? MicantManagedWriteResume
                                                             : MicantManagedReadResume);
    g_managedFaultObserved = false;
    g_managedFaultArmed = true;
    __asm__ __volatile__("lidt %0; movq %1, %%cr0"
                         : : "m"(idtr), "r"(cr0 | (uint64_t{1} << 16)) : "memory");
    if (write) MicantManagedWriteProbe(address);
    else MicantManagedReadProbe(address);
    __asm__ __volatile__("movq %0, %%cr0; lidt %1"
                         : : "r"(cr0), "m"(previous) : "memory");
    g_managedFaultArmed = false;
    Idtr restored{};
    __asm__ __volatile__("sidt %0" : "=m"(restored) : : "memory");
    return g_managedFaultObserved && restored.base == previous.base &&
           restored.limit == previous.limit;
}

bool verifyBootstrapPageTables() noexcept {
    if (!bootstrapPagingContext() || g_pml4[130] != 0) return false;
    auto& database = mm::PfnDatabase::get();
    const size_t active = database.getActiveCount();
    const size_t available = database.getFreeCount() + database.getZeroedCount() +
                             database.getStandbyCount();
    const uint64_t originalRoot = g_pml4[0];
    const uint64_t rootAddress = reinterpret_cast<uint64_t>(g_pml4);
    const uint64_t rootTranslation = walk(rootAddress, rootAddress);
    if (!rootTranslation || unmapPage(rootAddress & ~uint64_t{4095}, false) ||
        mapPage(rootAddress & ~uint64_t{4095}, rootAddress, kWritable)) return false;
    const uint64_t first = database.allocatePage();
    const uint64_t second = database.allocatePage();
    if (!first || !second) {
        if (first) database.freePage(first);
        if (second) database.freePage(second);
        return false;
    }
    database.zeroPage(first);
    database.zeroPage(second);
    auto* firstBytes = reinterpret_cast<volatile uint64_t*>(first << 12);
    auto* secondBytes = reinterpret_cast<volatile uint64_t*>(second << 12);
    const bool zeroed = *firstBytes == 0 && firstBytes[511] == 0 &&
                        *secondBytes == 0 && secondBytes[511] == 0;
    *firstBytes = 0x1122334455667788ULL;
    *secondBytes = 0x8877665544332211ULL;
    bool ok = zeroed;
    for (int fail = 0; fail < 3; ++fail) {
        if (mapPage(kTestBase, first << 12, kWritable | kNoExecute, fail) ||
            g_pml4[130] != 0 || database.getActiveCount() != active + 2) ok = false;
    }
    const uint64_t sibling = kTestBase + 4096;
    const uint64_t branch = kTestBase + (uint64_t{1} << 30);
    if (!mapPage(kTestBase, first << 12, kWritable | kNoExecute)) ok = false;
    if (ok) {
        auto* mapped = reinterpret_cast<volatile uint64_t*>(kTestBase);
        if (*mapped != *firstBytes || !bootstrapKernelRange(kTestBase, 4096, true) ||
            bootstrapUserRange(kTestBase, 4096, true) ||
            mapPage(kTestBase, second << 12, kWritable | kNoExecute)) ok = false;
        *mapped = 0xA1B2C3D4E5F60718ULL;
        if (*firstBytes != 0xA1B2C3D4E5F60718ULL) ok = false;
        for (int fail = 0; fail < 2; ++fail) {
            if (mapPage(branch, second << 12, kWritable | kNoExecute, fail) ||
                database.getActiveCount() != active + 5 ||
                *mapped != *firstBytes) ok = false;
        }
        if (!mapPage(branch, second << 12, kWritable | kNoExecute) ||
            database.getActiveCount() != active + 7 ||
            !unmapPage(branch, false) || unmapPage(branch, false) ||
            database.getActiveCount() != active + 5 || *mapped != *firstBytes) ok = false;
        if (!mapPage(sibling, second << 12, kWritable | kNoExecute) ||
            database.getActiveCount() != active + 5 ||
            !unmapPage(kTestBase, false) ||
            database.getActiveCount() != active + 5 ||
            !mapPage(kTestBase, second << 12, kWritable | kNoExecute) ||
            *mapped != *secondBytes) ok = false;
        if (!unmapPage(kTestBase, false) || !unmapPage(sibling, false)) ok = false;
    }
    database.freePage(first);
    database.freePage(second);
    return ok && g_pml4[130] == 0 && g_pml4[0] == originalRoot &&
           walk(rootAddress, rootAddress) == rootTranslation &&
           database.getActiveCount() == active &&
           database.getFreeCount() + database.getZeroedCount() +
               database.getStandbyCount() == available;
}

bool mapBootstrapUserMemory(uint64_t address, const void* backing, size_t size,
                            bool writable, bool executable) noexcept {
    const uint64_t physical = reinterpret_cast<uint64_t>(backing);
    if (!g_addressSpaceReady || !size || (size & 4095) || (address & 4095) ||
        (physical & 4095) || !physical || address < kUserBase ||
        address >= kUserBase + (uint64_t{1} << 39) ||
        size > kUserBase + (uint64_t{1} << 39) - address ||
        physical > kPhysicalMask || size - 1 > kPhysicalMask + 4095 - physical) {
        return false;
    }
    const uint64_t root = reinterpret_cast<uint64_t>(g_pml4);
    for (size_t offset = 0; offset < size; offset += 4096) {
        if (walk(root, address + offset) ||
            walk(root, physical + offset) != physical + offset) {
            return false;
        }
    }
    const uint64_t flags = kUser | (writable ? kWritable : 0) |
                           (executable ? 0 : kNoExecute);
    for (size_t offset = 0; offset < size; offset += 4096) {
        if (!mapPage(address + offset, physical + offset, flags)) {
            while (offset) {
                offset -= 4096;
                (void)unmapPage(address + offset, true);
            }
            return false;
        }
    }
    return true;
}

bool initializeBootstrapSyscalls(UserServiceRoutine dispatch) noexcept {
    if (!g_addressSpaceReady || !g_probeSucceeded || !dispatch || g_dispatch) {
        return false;
    }
    g_syscallCpu.kernelRsp = reinterpret_cast<uint64_t>(
        g_syscallStack + sizeof(g_syscallStack));
    g_dispatch = dispatch;
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, 0);
    cpu::cpuWrmsr(cpu::MSR_KERNEL_GS_BASE, reinterpret_cast<uint64_t>(&g_syscallCpu));
    cpu::cpuWrmsr(cpu::MSR_STAR, uint64_t{cpu::KGDT64_R3_CMCODE} << 48 |
                                   uint64_t{cpu::SELECTOR_KCODE64} << 32);
    cpu::cpuWrmsr(cpu::MSR_LSTAR, reinterpret_cast<uint64_t>(MicantBootstrapSyscallEntry));
    cpu::cpuWrmsr(cpu::MSR_SFMASK, cpu::SFMASK_DEFAULT | (uint64_t{1} << 18));
    cpu::cpuWrmsr(cpu::MSR_EFER, cpu::cpuRdmsr(cpu::MSR_EFER) | cpu::EFER_SCE);
    return true;
}

bool verifyBootstrapSyscalls() noexcept {
    if (!g_dispatch ||
        !bootstrapKernelRange(reinterpret_cast<uint64_t>(g_nativeProbeImage),
                              sizeof(g_nativeProbeImage), true) ||
        bootstrapKernelRange(kUserBase, 1, false) ||
        bootstrapKernelRange(kUserBase + 4096, 1, true) ||
        bootstrapKernelRange(0x0000800000000000ULL, 1, false) ||
        bootstrapKernelRange(UINT64_MAX - 8, 16, false) ||
        bootstrapKernelRange(reinterpret_cast<uint64_t>(g_nativeProbeImage), 0, true)) {
        return false;
    }
    const auto* begin = reinterpret_cast<const uint8_t*>(MicantSyscallProbeProgram);
    const size_t size = reinterpret_cast<uint64_t>(MicantSyscallProbeEnd) -
                        reinterpret_cast<uint64_t>(begin);
    if (size > sizeof(g_userCode) || size < 3) {
        return false;
    }
    for (size_t byte = 0; byte < size; ++byte) {
        g_userCode[byte] = begin[byte];
    }
    g_probeMode = 5;
    g_probeSucceeded = false;
    g_probeReturnRip = kUserBase + size - 2;
    const size_t activeBefore = mm::PfnDatabase::get().getActiveCount();
    MicantProbeEnter();
    if (!g_probeSucceeded || mm::PfnDatabase::get().getActiveCount() != activeBefore) {
        return false;
    }
    __asm__ __volatile__(
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\n"
        : : : "rax", "memory"
    );
    return true;
}

bool verifyManagedExecutablePage() noexcept {
    if (!g_probeSucceeded || !bootstrapPagingContext() || activePagingRoot() != g_pml4) return false;
    auto& database = mm::PfnDatabase::get();
    const size_t active = database.getActiveCount();
    bool passed = false;
    {
        mm::ProcessAddressSpace space;
        const mm::ProcessAddressSpace::PagingBackend backend{
            nullptr, mapBootstrapManagedPage, unmapBootstrapManagedPage, mapBootstrapProtectManagedPage,
            mapBootstrapManagedView, unmapBootstrapManagedView};
        if (!space.configurePagingBackend(backend, BootstrapManagedBase, BootstrapManagedSize)) return false;
        uintptr_t address = BootstrapManagedBase;
        if (space.allocate(address, mm::PageSize4KB, mm::MEM_RESERVE | mm::MEM_COMMIT,
                           mm::PAGE_READWRITE) != NtStatus::Success) return false;
        *reinterpret_cast<volatile uint8_t*>(address) = 0xcc; // CPL3 breakpoint instruction.
        uint32_t oldProtection{};
        if (space.protect(address, mm::PageSize4KB, mm::PAGE_EXECUTE_READ, &oldProtection) != NtStatus::Success ||
            oldProtection != mm::PAGE_READWRITE || !bootstrapUserRange(address, 1, false, true) ||
            bootstrapUserRange(address, 1, true)) return false;
        const uint32_t previousMode = g_probeMode;
        const uint64_t previousRip = MicantProbeUserRip;
        const uint64_t previousReturn = g_probeReturnRip;
        g_probeMode = 0;
        g_probeSucceeded = false;
        MicantProbeUserRip = address;
        g_probeReturnRip = address + 1;
        MicantProbeEnter();
        passed = g_probeSucceeded;
        g_probeMode = previousMode;
        MicantProbeUserRip = previousRip;
        g_probeReturnRip = previousReturn;
        __asm__ __volatile__("movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es"
                             : : : "rax", "memory");
        if (space.free(address, 0, mm::MEM_RELEASE) != NtStatus::Success) return false;
    }
    return passed && database.getActiveCount() == active;
}

bool verifyNativeImageLoading() noexcept {
    using namespace micant::pe;
    if (!g_dispatch || !g_probeSucceeded) return false;
    constexpr uint64_t imageBase = kUserBase + 0x10000;
    constexpr uint64_t ntdllBase = kUserBase + 0x20000;
    const auto* client = reinterpret_cast<const uint8_t*>(MicantSyscallClientProgram);
    const size_t clientSize = reinterpret_cast<uint64_t>(MicantSyscallClientEnd) -
                              reinterpret_cast<uint64_t>(client);
    const size_t breakpointOffset =
        reinterpret_cast<uint64_t>(MicantSyscallClientBreakpoint) -
        reinterpret_cast<uint64_t>(client);
    if (clientSize > 4096 - 16 || breakpointOffset >= clientSize) {
        return false;
    }
    ImageDosHeader dos{};
    dos.e_magic = DOS_MAGIC;
    dos.e_lfanew = 64;
    memcpy(g_nativeProbeFile, &dos, sizeof(dos));
    ImageNtHeaders64 nt{};
    nt.signature = NT_SIGNATURE;
    nt.fileHeader.machine = MACHINE_AMD64;
    nt.fileHeader.numberOfSections = 1;
    nt.fileHeader.sizeOfOptionalHeader = sizeof(ImageOptionalHeader64);
    nt.fileHeader.characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
    nt.optionalHeader.magic = PE32PLUS_MAGIC;
    nt.optionalHeader.imageBase = imageBase;
    nt.optionalHeader.addressOfEntryPoint = 4096;
    nt.optionalHeader.sectionAlignment = 4096;
    nt.optionalHeader.fileAlignment = 512;
    nt.optionalHeader.sizeOfImage = sizeof(g_nativeProbeImage);
    nt.optionalHeader.sizeOfHeaders = 512;
    nt.optionalHeader.subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
    memcpy(g_nativeProbeFile + 64, &nt, sizeof(nt));
    ImageSectionHeader text{};
    text.virtualAddress = 4096;
    text.misc.virtualSize = 4096;
    text.pointerToRawData = 512;
    text.sizeOfRawData = 4096;
    text.characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    memcpy(g_nativeProbeFile + 64 + sizeof(nt), &text, sizeof(text));
    // CALL gives a compiled Windows x64 function the required entry alignment.
    constexpr uint8_t call[]{0x48, 0x83, 0xEC, 0x20, 0xE8, 7, 0, 0, 0, 0x0F, 0x0B};
    memcpy(g_nativeProbeFile + 512, call, sizeof(call));
    for (size_t byte = 0; byte < clientSize; ++byte) {
        g_nativeProbeFile[512 + 16 + byte] = client[byte];
    }
    if (g_nativeProbeModules.addModule("syscall-probe.exe", g_nativeProbeFile,
            g_nativeProbeImage, imageBase) != micant::NtStatus::Success ||
        g_nativeProbeModules.addModule("ntdll.dll", g_ntdllProbeFile,
            g_ntdllProbeImage, ntdllBase) != micant::NtStatus::Success ||
        g_nativeProbeModules.bindAll() != micant::NtStatus::Success ||
        !mapBootstrapUserMemory(imageBase, g_nativeProbeImage, 4096, false, false) ||
        !mapBootstrapUserMemory(imageBase + 4096, g_nativeProbeImage + 4096,
                                4096, false, true)) {
        return false;
    }
    const auto* dll = g_nativeProbeModules.module(1);
    if (!dll || !mapBootstrapUserMemory(ntdllBase, g_ntdllProbeImage, 4096, false, false))
        return false;
    for (size_t index = 0; index < dll->view.headers().fileHeader.numberOfSections; ++index) {
        const auto section = dll->view.section(index);
        const size_t size = section.misc.virtualSize > section.sizeOfRawData
            ? section.misc.virtualSize : section.sizeOfRawData;
        if (!size) continue;
        if ((section.virtualAddress & 4095) || section.virtualAddress > dll->image.size() ||
            size > dll->image.size() - section.virtualAddress ||
            !mapBootstrapUserMemory(ntdllBase + section.virtualAddress,
                g_ntdllProbeImage + section.virtualAddress, (size + 4095) & ~size_t{4095},
                (section.characteristics & IMAGE_SCN_MEM_WRITE) != 0,
                (section.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)) return false;
    }
    constexpr const char* names[]{"NtCreateSection", "NtMapViewOfSection", "NtUnmapViewOfSection", "NtClose",
                                  "NtAllocateVirtualMemory", "NtFreeVirtualMemory", "NtCreateEvent", "NtSetEvent",
                                  "NtWaitForSingleObject"};
    auto* exports = reinterpret_cast<uint64_t*>(kUserBase + 4096 + 768);
    for (size_t index = 0; index < 9; ++index) {
        if (g_nativeProbeModules.resolve("ntdll.dll", names[index], 0, exports[index]) != micant::NtStatus::Success ||
            !bootstrapUserRange(exports[index], 1, false, true)) return false;
    }
    MicantProbeUserRip = imageBase + 4096;
    g_probeMode = 6;
    g_probeSucceeded = false;
    g_probeReturnRip = imageBase + 4096 + 16 + breakpointOffset + 1;
    const size_t activeBefore = mm::PfnDatabase::get().getActiveCount();
    MicantProbeEnter();
    if (g_log) g_log("[MicaNT Hal] CPL3 syscall probe returned to kernel\r\n");
    MicantProbeUserRip = kUserBase;
    __asm__ __volatile__(
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\n"
        : : : "rax", "memory"
    );
    return g_probeSucceeded && mm::PfnDatabase::get().getActiveCount() == activeBefore;
}

void* mapBootstrapDeviceMemory(uint64_t physical, size_t size) noexcept {
    if (!g_addressSpaceReady || size == 0 || size > 1024 * 1024 ||
        physical > kPhysicalMask || size > kPhysicalMask - physical) {
        return nullptr;
    }
    const uint64_t offset = physical & 4095;
    const uint64_t length = (size + offset + 4095) & ~uint64_t{4095};
    const uint64_t start = g_deviceNext;
    if (length > 16 * 1024 * 1024 - (start - kDeviceBase)) {
        return nullptr;
    }
    for (uint64_t page = 0; page < length; page += 4096) {
        // PWT/PCD avoid caching device registers; no user-accessible MMIO.
        if (!mapPage(start + page, (physical & ~uint64_t{4095}) + page,
                     kWritable | kNoExecute | 0x18)) {
            while (page) {
                page -= 4096;
                (void)unmapPage(start + page, false);
            }
            return nullptr;
        }
    }
    g_deviceNext += length;
    return reinterpret_cast<void*>(start + offset);
}

struct TimerWorkerState {
    volatile uint64_t iterations{0};
    uint64_t canary{0};
    volatile uint64_t corrupted{0};
};
alignas(16) static uint8_t g_firstTimerWorkerStack[16384]{};
alignas(16) static uint8_t g_secondTimerWorkerStack[16384]{};

[[noreturn]] static void timerWorker(void* argument) noexcept {
    auto* state = static_cast<TimerWorkerState*>(argument);
    ke::KeLowerIrql(ke::PASSIVE_LEVEL);
    __asm__ __volatile__(
        "movq 8(%%rcx), %%r12\nmovq %%r12, %%xmm6\nsti\n"
        "1:\nmovq %%xmm6, %%rax\ncmpq %%r12, %%rax\njne 2f\n"
        "cmpq 8(%%rcx), %%r12\njne 2f\nincq (%%rcx)\njmp 1b\n"
        "2:\nmovq $1, 16(%%rcx)\njmp 1b\n"
        : : "c"(state) : "rax", "r12", "xmm6", "memory", "cc");
    __builtin_unreachable();
}

static bool verifyBootstrapPreemption() noexcept {
    using namespace ke;
    auto* boot = KeGetCurrentThread();
    auto& scheduler = PriorityScheduler::get();
    if (!boot || boot->state != ThreadState::Running || scheduler.getReadyThreadCount()) return false;
    TimerWorkerState firstState{0, 0x12345678abcdef00ULL, 0};
    TimerWorkerState secondState{0, 0xfedcba9876543210ULL, 0};
    KThread first, second;
    first.directoryTableBase = second.directoryTableBase = g_CurrentCpu.currentCr3;
    first.priority = second.priority = PRIORITY_NORMAL;
    first.quantumRemaining = second.quantumRemaining = 1;
    if (!detail::prepareBootstrapThread(first, g_firstTimerWorkerStack, sizeof(g_firstTimerWorkerStack), timerWorker, &firstState) ||
        !detail::prepareBootstrapThread(second, g_secondTimerWorkerStack, sizeof(g_secondTimerWorkerStack), timerWorker, &secondState) ||
        !scheduler.readyKThread(&second)) return false;
    const auto originalPriority = boot->priority;
    const auto previous = KfRaiseIrql(DISPATCH_LEVEL);
    const uint64_t switches = g_CurrentCpu.contextSwitches;
    boot->priority = PRIORITY_REALTIME_MAX;
    MicantTimerTicks = 0;
    MicantTimerBootThread = boot;
    MicantTimerBootQueued = false;
    MicantTimerDispatchFailed = false;
    MicantTimerPreemptionEnabled = true;
    const bool switched = KiSwapContext(boot, &first);
    MicantTimerPreemptionEnabled = false;
    MicantTimerBootThread = nullptr;
    boot->priority = originalPriority;
    KeLowerIrql(previous);
    const bool success = switched && MicantTimerBootQueued && !MicantTimerDispatchFailed &&
        firstState.iterations && secondState.iterations && !firstState.corrupted && !secondState.corrupted &&
        g_syscallCpu.kernelRsp == (boot->kernelStackTop ? boot->kernelStackTop :
            reinterpret_cast<uintptr_t>(g_syscallStack + sizeof(g_syscallStack))) &&
        cpu::CpuHardwareEngine::get().getTss().rsp0 == (boot->kernelStackTop ? boot->kernelStackTop :
            reinterpret_cast<uintptr_t>(g_kernelStack + sizeof(g_kernelStack))) &&
        g_CurrentCpu.currentThread == boot && g_CurrentCpu.contextSwitches >= switches + 4;
    const bool removedFirst = scheduler.removeReadyThread(&first);
    const bool removedSecond = scheduler.removeReadyThread(&second);
    first.state = second.state = ThreadState::Terminated;
    return success && removedFirst && removedSecond && scheduler.getReadyThreadCount() == 0;
}

extern "C" bool MicantTestPsUserPreemption() noexcept;

// Verify the one routed hardware priority with IF still enabled. A pending
// clock interrupt must become deliverable after the original IRQL is restored.
static bool verifyClockIrqlMask(volatile uint32_t* apic) noexcept {
    const auto previous = ke::KfRaiseIrql(ke::CLOCK_LEVEL);
    const auto nested = ke::KfRaiseIrql(ke::HIGH_LEVEL);
    uint64_t highPriority{};
    __asm__ __volatile__("movq %%cr8, %0" : "=r"(highPriority));
    ke::KeLowerIrql(nested);
    uint64_t clockPriority{}, flags{};
    __asm__ __volatile__("movq %%cr8, %0; pushfq; popq %1"
                         : "=r"(clockPriority), "=r"(flags));
    const uint64_t before = MicantTimerTicks;
    constexpr uint32_t timerBit = 1u << (0xd1 % 32);
    constexpr size_t timerIrr = (0x200 + (0xd1 / 32) * 0x10) / 4;
    for (size_t spin = 0; spin < 50000000 && !(apic[timerIrr] & timerBit); ++spin)
        __asm__ __volatile__("pause");
    const bool blocked = (apic[timerIrr] & timerBit) && MicantTimerTicks == before;
    ke::KeLowerIrql(previous);
    for (size_t spin = 0; spin < 50000000 && MicantTimerTicks == before; ++spin)
        __asm__ __volatile__("pause");
    return previous == ke::PASSIVE_LEVEL && nested == ke::CLOCK_LEVEL &&
        highPriority == 15 && clockPriority == 13 && (flags & (uint64_t{1} << 9)) &&
        blocked && MicantTimerTicks > before;
}

bool verifyBootstrapTimerInterrupt() noexcept {
    if (!bootstrapQemuFaultTestsAvailable() || !bootstrapPagingContext()) return false;
    Idtr current{};
    __asm__ __volatile__("sidt %0" : "=m"(current));
    if (current.base != reinterpret_cast<uint64_t>(g_idt) || current.limit != sizeof(g_idt) - 1)
        return false;
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & (1u << 9))) return false;
    const uint64_t apicBase = cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE);
    if (!(apicBase & (1u << 11)) || (apicBase & (1u << 10))) {
        logTrapValue("[MicaNT Hal] APIC base=0x", apicBase);
        return false;
    }
    auto* const apic = static_cast<volatile uint32_t*>(
        mapBootstrapDeviceMemory(apicBase & kPhysicalMask, 4096));
    if (!apic) { if (g_log) g_log("[MicaNT Hal] APIC MMIO mapping failed\r\n"); return false; }
    if (((apic[0x30 / 4] >> 16) & 0xff) < 5 || !(apic[0x320 / 4] & (1u << 16))) {
        logTrapValue("[MicaNT Hal] APIC version=0x", apic[0x30 / 4]);
        logTrapValue("[MicaNT Hal] APIC timer initial=0x", apic[0x380 / 4]);
        logTrapValue("[MicaNT Hal] APIC timer LVT=0x", apic[0x320 / 4]);
        return false;
    }

    const auto input = [](uint16_t port) noexcept {
        uint8_t value{};
        __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    };
    const auto output = [](uint16_t port, uint8_t value) noexcept {
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
    };
    const uint8_t masterMask = input(0x21), slaveMask = input(0xa1);
    output(0x21, 0xff);
    output(0xa1, 0xff);
    constexpr uint32_t offsets[]{0x320, 0x330, 0x340, 0x350, 0x360, 0x370};
    uint32_t savedLvt[6]{};
    for (size_t i = 0; i < 6; ++i) {
        savedLvt[i] = apic[offsets[i] / 4];
        apic[offsets[i] / 4] = savedLvt[i] | (1u << 16);
    }
    const uint32_t savedTpr = apic[0x80 / 4];
    const uint32_t savedSvr = apic[0xf0 / 4];
    const uint32_t savedDivide = apic[0x3e0 / 4];
    const uint32_t savedInitial = apic[0x380 / 4];
    const InterruptGate savedTimer = g_idt[0xd1], savedSpurious = g_idt[0xff];
    const auto gate = [](uint64_t handler) noexcept {
        return InterruptGate{static_cast<uint16_t>(handler), cpu::SELECTOR_KCODE64, 0, 0x8e,
                             static_cast<uint16_t>(handler >> 16), static_cast<uint32_t>(handler >> 32), 0};
    };
    g_idt[0xd1] = gate(reinterpret_cast<uint64_t>(MicantBootstrapTimerInterrupt));
    g_idt[0xff] = gate(reinterpret_cast<uint64_t>(MicantBootstrapSpuriousInterrupt));
    MicantTimerTicks = 0;
    MicantTimerEoi = apic + 0xb0 / 4;
    apic[0x80 / 4] = 0;
    g_clockIrqlMaskReady = true;
    setBootstrapClockIrqlMask(ke::KeGetCurrentIrql());
    apic[0xf0 / 4] = (savedSvr & ~0xffu) | 0x1ff;
    apic[0x3e0 / 4] = 0xb; // Divide by one; the test does not infer a frequency.
    apic[0x320 / 4] = 0xd1 | (1u << 17); // Periodic, unmasked.
    apic[0x380 / 4] = 100000;
    __asm__ __volatile__("sti" : : : "memory");
    ke::SpinLock lock;
    const ke::KIRQL oldIrql = lock.acquire();
    ke::SpinLock nested;
    const ke::KIRQL nestedIrql = nested.acquire();
    nested.release(nestedIrql);
    uint64_t lockedFlags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(lockedFlags));
    const uint64_t beforeLockedWait = MicantTimerTicks;
    for (size_t spin = 0; spin < 200000; ++spin) __asm__ __volatile__("pause");
    const bool lockMaskedInterrupts = !(lockedFlags & (uint64_t{1} << 9)) &&
        MicantTimerTicks == beforeLockedWait;
    lock.release(oldIrql);
    uint64_t releasedFlags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(releasedFlags));
    const bool irqlMaskedClock = verifyClockIrqlMask(apic);
    for (size_t spin = 0; spin < 50000000 && MicantTimerTicks < 3; ++spin)
        __asm__ __volatile__("pause");
    __asm__ __volatile__("cli" : : : "memory");
    const bool timerDelivered = MicantTimerTicks >= 3;
    const bool preemptionVerified = timerDelivered && verifyBootstrapPreemption();
    MicantTimerTicks = 0;
    MicantTimerBootThread = nullptr;
    MicantTimerDispatchFailed = false;
    MicantTimerPreemptionEnabled = preemptionVerified;
    const bool userPreemption = preemptionVerified && MicantTestPsUserPreemption() &&
        MicantTimerTicks > 0 && !MicantTimerDispatchFailed;
    MicantTimerPreemptionEnabled = false;
    apic[0x320 / 4] = savedLvt[0] | (1u << 16);
    apic[0x380 / 4] = 0;
    // Drain any timer already accepted into IRR before restoring the gate.
    constexpr uint32_t timerBit = 1u << (0xd1 % 32);
    constexpr size_t timerIrr = (0x200 + (0xd1 / 32) * 0x10) / 4;
    if (apic[timerIrr] & timerBit) {
        __asm__ __volatile__("sti" : : : "memory");
        for (size_t spin = 0; spin < 1000000 && (apic[timerIrr] & timerBit); ++spin)
            __asm__ __volatile__("pause");
        __asm__ __volatile__("cli" : : : "memory");
    }
    const bool success = lockMaskedInterrupts && irqlMaskedClock &&
        (releasedFlags & (uint64_t{1} << 9)) && timerDelivered && preemptionVerified &&
        userPreemption && !(apic[timerIrr] & timerBit);
    apic[0x3e0 / 4] = savedDivide;
    apic[0x380 / 4] = savedInitial;
    g_clockIrqlMaskReady = false;
    apic[0x80 / 4] = savedTpr;
    apic[0xf0 / 4] = savedSvr;
    for (size_t i = 0; i < 6; ++i) apic[offsets[i] / 4] = savedLvt[i];
    g_idt[0xd1] = savedTimer;
    g_idt[0xff] = savedSpurious;
    output(0x21, masterMask);
    output(0xa1, slaveMask);
    MicantTimerEoi = nullptr;
    uint64_t flags{};
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    const bool restored = !(flags & (uint64_t{1} << 9)) &&
        input(0x21) == masterMask && input(0xa1) == slaveMask &&
        apic[0x80 / 4] == savedTpr && apic[0xf0 / 4] == savedSvr &&
        apic[0x3e0 / 4] == savedDivide && apic[0x380 / 4] == savedInitial;
    if (!success) logTrapValue("[MicaNT Hal] Timer ticks=0x", MicantTimerTicks);
    if (!restored && g_log) g_log("[MicaNT Hal] Timer state restoration failed\r\n");
    return success && restored;
}

// Measure one approximately 10 ms interval against PIT channel 2. The
// speaker must be inactive; the port gate is restored after calibration.
static bool calibrateBootstrapApic(volatile uint32_t* apic, uint32_t& period) noexcept {
    const auto input = [](uint16_t port) noexcept {
        uint8_t value{};
        __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    };
    const auto output = [](uint16_t port, uint8_t value) noexcept {
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
    };
    const uint8_t speaker = input(0x61);
    if (speaker & 3) return false;
    const uint32_t savedTimer = apic[0x320 / 4];
    const uint32_t savedDivide = apic[0x3e0 / 4];
    const uint32_t savedInitial = apic[0x380 / 4];
    constexpr uint16_t pitInterval = 11932; // 8254 input clock: approximately 1.193182 MHz.
    output(0x61, static_cast<uint8_t>(speaker & ~uint8_t{3}));
    output(0x43, 0xb0); // Channel 2, low/high bytes, mode 0, binary.
    output(0x42, static_cast<uint8_t>(pitInterval));
    output(0x42, static_cast<uint8_t>(pitInterval >> 8));
    apic[0x320 / 4] = (savedTimer | (1u << 16)) & ~(1u << 17);
    apic[0x3e0 / 4] = 0xb; // Divide by one.
    apic[0x380 / 4] = 0xffffffff;
    output(0x61, static_cast<uint8_t>((speaker & ~uint8_t{2}) | 1));
    size_t spin = 0;
    while (!(input(0x61) & 0x20) && spin++ < 50000000) __asm__ __volatile__("pause");
    const uint32_t remaining = apic[0x390 / 4];
    output(0x61, speaker);
    apic[0x380 / 4] = 0;
    apic[0x3e0 / 4] = savedDivide;
    apic[0x380 / 4] = savedInitial;
    apic[0x320 / 4] = savedTimer;
    const uint32_t elapsed = 0xffffffffu - remaining;
    if (spin >= 50000000 || elapsed < 100 || remaining == 0) return false;
    period = elapsed;
    return true;
}

bool startBootstrapSchedulerClock() noexcept {
    if (g_schedulerClockActive || !g_dispatch || !bootstrapPagingContext() ||
        !ke::KeGetCurrentThread()) return false;
    uint64_t flags{}, cr4{};
    Idtr current{};
    __asm__ __volatile__("pushfq; popq %0; sidt %1; movq %%cr4, %2"
                         : "=r"(flags), "=m"(current), "=r"(cr4));
    if ((flags & (uint64_t{1} << 9)) || current.base != reinterpret_cast<uint64_t>(g_idt)) return false;
    // The IRQ switch saves x87/SSE with FXSAVE, not AVX or other XSTATE.
    // Do not enable persistent preemption while an unsaved component is active.
    if (cr4 & (uint64_t{1} << 18)) {
        uint32_t low{}, high{};
        __asm__ __volatile__("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        if ((uint64_t{high} << 32 | low) & ~uint64_t{3}) return false;
    }
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(edx & (1u << 9))) return false;
    const uint64_t apicBase = cpu::cpuRdmsr(cpu::MSR_IA32_APIC_BASE);
    if (!(apicBase & (1u << 11)) || (apicBase & (1u << 10))) return false;
    auto* apic = static_cast<volatile uint32_t*>(mapBootstrapDeviceMemory(apicBase & kPhysicalMask, 4096));
    if (!apic || ((apic[0x30 / 4] >> 16) & 0xff) < 5 ||
        !(apic[0x320 / 4] & (1u << 16))) return false;
    uint32_t interval{};
    if (!calibrateBootstrapApic(apic, interval)) return false;

    // Only the clock vector is routed in this bootstrap configuration.
    __asm__ __volatile__("outb %0, $0x21; outb %0, $0xa1" : : "a"(uint8_t{0xff}));
    constexpr uint32_t localVectors[]{0x320, 0x330, 0x340, 0x350, 0x360, 0x370};
    for (const auto offset : localVectors) apic[offset / 4] = apic[offset / 4] | (1u << 16);
    const auto gate = [](uint64_t handler) noexcept {
        return InterruptGate{static_cast<uint16_t>(handler), cpu::SELECTOR_KCODE64, 0, 0x8e,
                             static_cast<uint16_t>(handler >> 16), static_cast<uint32_t>(handler >> 32), 0};
    };
    g_idt[0xd1] = gate(reinterpret_cast<uint64_t>(MicantBootstrapTimerInterrupt));
    g_idt[0xff] = gate(reinterpret_cast<uint64_t>(MicantBootstrapSpuriousInterrupt));
    MicantTimerTicks = 0;
    MicantTimerEoi = apic + 0xb0 / 4;
    MicantTimerBootThread = nullptr;
    MicantTimerDispatchFailed = false;
    MicantTimerPreemptionEnabled = true;
    apic[0x80 / 4] = 0;
    g_clockIrqlMaskReady = true;
    setBootstrapClockIrqlMask(ke::KeGetCurrentIrql());
    apic[0xf0 / 4] = (apic[0xf0 / 4] & ~0xffu) | 0x1ff;
    apic[0x3e0 / 4] = 0xb;
    apic[0x320 / 4] = 0xd1 | (1u << 17);
    apic[0x380 / 4] = interval;
    g_schedulerClockActive = true;
    return true;
}

bool verifyBootstrapSchedulerClock() noexcept {
    if (!g_schedulerClockActive) return false;
    const uint64_t before = MicantTimerTicks;
    return MicantTestPsUserPreemption() && MicantTimerTicks > before && !MicantTimerDispatchFailed;
}

bool bootstrapSchedulerClockFailed() noexcept { return MicantTimerDispatchFailed; }
uint64_t bootstrapSchedulerClockTicks() noexcept { return MicantTimerTicks; }

bool verifyRing3Transition() noexcept {
    if (!g_addressSpaceReady ||
        !mapPage(kUserBase, reinterpret_cast<uint64_t>(g_userCode), kUser)) {
        return false;
    }
    for (size_t offset = 0; offset < sizeof(g_userStack); offset += 4096) {
        if (!mapPage(kUserStackBase + offset,
                     reinterpret_cast<uint64_t>(g_userStack) + offset,
                     kUser | kWritable | kNoExecute)) {
            while (offset) {
                offset -= 4096;
                (void)unmapPage(kUserStackBase + offset, true);
            }
            (void)unmapPage(kUserBase, true);
            return false;
        }
    }
    const bool firstTeb = mapPage(kUserTebBase, reinterpret_cast<uint64_t>(&g_userTebStorage),
                                  kUser | kWritable | kNoExecute);
    if (!firstTeb ||
        !mapPage(kUserTebBase + 4096, reinterpret_cast<uint64_t>(&g_userTebStorage) + 4096,
                 kUser | kWritable | kNoExecute)) {
        if (firstTeb) (void)unmapPage(kUserTebBase, true);
        for (size_t offset = 0; offset < sizeof(g_userStack); offset += 4096) {
            (void)unmapPage(kUserStackBase + offset, true);
        }
        (void)unmapPage(kUserBase, true);
        return false;
    }
    g_userTebStorage.teb.self = kUserTebBase;
    g_userTebStorage.teb.uniqueProcessId = 0x11223344;
    g_userTebStorage.teb.uniqueThreadId = 0x55667788;
    // Read CS at CPL=3, verify RPL=3, then enter the kernel via #BP.
    constexpr uint8_t code[]{
        0x8c, 0xc8,             // mov ax, cs
        0x83, 0xe0, 0x03,       // and eax, 3
        0x83, 0xf8, 0x03,       // cmp eax, 3
        0x75, 0x02,             // jne fault
        0xcc,                   // int3
        0x90,                   // nop (unreachable)
        0x0f, 0x0b,             // ud2
    };
    for (size_t index = 0; index < sizeof(code); ++index) {
        g_userCode[index] = code[index];
    }
    g_userStack[0] = 0xCC; // A missing NX bit must trap, not execute arbitrary bytes.
    auto& hardware = cpu::CpuHardwareEngine::get();
    hardware.initialize(reinterpret_cast<uint64_t>(g_kernelStack + sizeof(g_kernelStack)), 0);
    const auto& gdtr = hardware.getGdtr();
    __asm__ __volatile__(
        "cli\nlgdt %0\n"
        "pushq $0x10\nleaq 1f(%%rip), %%rax\npushq %%rax\nlretq\n1:\n"
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\nmovw %%ax, %%ss\n"
        "movw $0x40, %%ax\nltr %%ax\n"
        : : "m"(gdtr) : "rax", "memory"
    );
    for (size_t vector = 0; vector < 256; ++vector) {
        const uint64_t handler = vector == 3
            ? reinterpret_cast<uint64_t>(MicantProbeTrapEntry)
            : vector == 6
                ? reinterpret_cast<uint64_t>(MicantProbeInvalidOpcodeEntry)
            : vector == 13
                ? reinterpret_cast<uint64_t>(MicantProbeGeneralProtectionEntry)
            : vector == 14 ? reinterpret_cast<uint64_t>(MicantProbePageFaultEntry)
                          : reinterpret_cast<uint64_t>(MicantProbeFaultEntry);
        g_idt[vector] = {
            static_cast<uint16_t>(handler),
            cpu::SELECTOR_KCODE64, 1,
            static_cast<uint8_t>(vector == 3 ? 0xEE : 0x8E),
            static_cast<uint16_t>(handler >> 16),
            static_cast<uint32_t>(handler >> 32), 0,
        };
    }
    const Idtr idtr{sizeof(g_idt) - 1, reinterpret_cast<uint64_t>(g_idt)};
    __asm__ __volatile__("lidt %0" : : "m"(idtr) : "memory");
    // Exercise IDT restoration while the CPL3 probe table is actually active.
    if (bootstrapQemuFaultTestsAvailable() &&
        !verifyBootstrapManagedPageFault(BootstrapManagedBase, false, false)) {
        return false;
    }
    g_probeReturnRip = kUserBase + 11;
    MicantProbeEnter();
    if (!g_probeSucceeded) {
        return false;
    }
    // User accesses must fault: kernel/MMIO reads, code writes, stack execution.
    for (uint32_t mode = 1; mode <= 4; ++mode) {
        g_probeMode = mode;
        g_probeSucceeded = false;
        g_userCode[0] = 0x48;
        g_userCode[1] = 0xB8; // movabs rax, address
        const uint64_t address = mode == 1
            ? reinterpret_cast<uint64_t>(g_pml4)
            : mode == 2 ? kUserBase
            : mode == 3 ? kUserBase + 4096 : kDeviceBase;
        for (size_t byte = 0; byte < sizeof(address); ++byte) {
            g_userCode[2 + byte] = static_cast<uint8_t>(address >> (byte * 8));
        }
        g_userCode[10] = mode == 3 ? 0xFF : mode == 2 ? 0x89 : 0x8B;
        g_userCode[11] = mode == 3 ? 0xE0 : 0x00; // jmp rax / write / read
        g_userCode[12] = 0x0F;
        g_userCode[13] = 0x0B; // ud2 if protection was bypassed
        MicantProbeEnter();
        if (!g_probeSucceeded) {
            return false;
        }
    }
    constexpr uint8_t tebCode[]{
        0x65, 0xC7, 0x04, 0x25, 0x68, 0x00, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x65, 0x8B, 0x04, 0x25, 0x68, 0x00, 0x00, 0x00,
        0x3D, 0x78, 0x56, 0x34, 0x12,
        0x75, 0x02,
        0x65, 0x48, 0xC7, 0x04, 0x25, 0x80, 0x14, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x65, 0x8B, 0x04, 0x25, 0x80, 0x14, 0x00, 0x00,
        0x3D, 0x78, 0x56, 0x34, 0x12,
        0x75, 0x02,
        0x65, 0x8B, 0x04, 0x25, 0x40, 0x00, 0x00, 0x00,
        0x3D, 0x44, 0x33, 0x22, 0x11,
        0x75, 0x02,
        0x65, 0x8B, 0x04, 0x25, 0x48, 0x00, 0x00, 0x00,
        0x3D, 0x88, 0x77, 0x66, 0x55,
        0x75, 0x02, 0xCC, 0x0F, 0x0B,
    };
    for (size_t index = 0; index < sizeof(tebCode); ++index) {
        g_userCode[index] = tebCode[index];
    }
    g_probeMode = 7;
    g_probeSucceeded = false;
    g_probeReturnRip = kUserBase + sizeof(tebCode) - 2;
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, kUserTebBase);
    MicantProbeEnter();
    cpu::cpuWrmsr(cpu::MSR_GS_BASE, 0);
    if (!g_probeSucceeded ||
        g_userTebStorage.teb.lastErrorValue != 0x12345678 ||
        g_userTebStorage.teb.tlsSlots[0] != 0x12345678 ||
        g_userTebStorage.teb.uniqueProcessId != 0x11223344 ||
        g_userTebStorage.teb.uniqueThreadId != 0x55667788) {
        return false;
    }
    __asm__ __volatile__(
        "movw $0x18, %%ax\nmovw %%ax, %%ds\nmovw %%ax, %%es\n"
        : : : "rax", "memory"
    );
    return g_probeSucceeded;
}

} // namespace micant::hal::amd64
