/**
 * @file ntdll.cpp
 * @brief Sovereign NT Layer DLL (ntdll.dll) for MicaNT System32.
 *
 * Freestanding ISO C++23. Clean-Room design. Zero proprietary headers.
 * Native x64 system call trampolines and runtime library exports.
 */

#include <cstdint>
#include <cstddef>
#include "micant/shared/context.hpp"
#include "micant/shared/syscall_numbers.hpp"

#define MICANT_STRINGIFY_INNER(value) #value
#define MICANT_STRINGIFY(value) MICANT_STRINGIFY_INNER(value)

extern "C" {


// System time services use the same AMD64 syscall ABI as the rest of NTDLL.
// SSNs match the canonical Ke service table consumed by the bare-metal path.
__attribute__((naked)) __declspec(dllexport) int32_t NtQuerySystemTime(
    int64_t* /*systemTime*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x47, %eax\n\tsyscall\n\tretq\n");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtQueryPerformanceCounter(
    int64_t* /*performanceCounter*/, int64_t* /*performanceFrequency*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x31, %eax\n\tsyscall\n\tretq\n");
}

// Local freestanding runtime primitives. They satisfy this DLL's compiler
// calls and are not additional public NTDLL exports.
void* memset(void* dest, int c, size_t n) {
    auto d = static_cast<char*>(dest);
    while (n--) *d++ = static_cast<char>(c);
    return dest;
}

void* memcpy(void* dest, const void* src, size_t n) {
    auto d = static_cast<char*>(dest);
    auto s = static_cast<const char*>(src);
    while (n--) *d++ = *s++;
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    auto d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        auto s_end = s + n;
        while (n--) *--d = *--s_end;
    }
    return dest;
}

bool DllMain(void*, unsigned long, void*) noexcept {
    return true;
}

// ============================================================================
// 1. Native NT Fast Syscall Trampolines (Ring 3 -> Ring 0)
// ============================================================================

__attribute__((naked)) __declspec(dllexport) int32_t NtOpenProcess(void**, uint32_t, void*, void*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $" MICANT_STRINGIFY(MICANT_SSN_NT_OPEN_PROCESS) ", %rax\n\tsyscall\n\tretq\n\t");
}
__attribute__((naked)) __declspec(dllexport) int32_t NtOpenThread(void**, uint32_t, void*, void*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $" MICANT_STRINGIFY(MICANT_SSN_NT_OPEN_THREAD) ", %rax\n\tsyscall\n\tretq\n\t");
}
__attribute__((naked)) __declspec(dllexport) int32_t NtDuplicateObject(
    void*, void*, void*, void**, uint32_t, uint32_t, uint32_t) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $" MICANT_STRINGIFY(MICANT_SSN_NT_DUPLICATE_OBJECT)
                         ", %rax\n\tsyscall\n\tretq\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtTerminateProcess(void* /*processHandle*/, int32_t /*exitStatus*/) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x2c, %rax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) int32_t NtTerminateThread(void* /*threadHandle*/, int32_t /*exitStatus*/) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0x53, %rax\n\tsyscall\n\tret\n\t");
#else
    return 0;
#endif
}

extern "C" __attribute__((naked)) __declspec(dllexport) int32_t NtContinue(
    micant::CONTEXT* /*context*/, uint8_t /*testAlert*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0x43, %rax\n\tsyscall\n\tret\n\t");
}

struct ExceptionRecord64 {
    uint32_t code, flags;
    ExceptionRecord64* nestedRecord;
    void* exceptionAddress;
    uint32_t parameterCount, alignment;
    uintptr_t information[15];
};
struct ExceptionPointers64 { ExceptionRecord64* record; micant::CONTEXT* context; };
using VectoredExceptionHandler = int32_t (*)(ExceptionPointers64*);
static_assert(sizeof(ExceptionRecord64) == 0x98);

__attribute__((naked)) __declspec(dllexport) int32_t NtRaiseException(
    ExceptionRecord64*, micant::CONTEXT*, uint8_t) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $" MICANT_STRINGIFY(MICANT_SSN_NT_RAISE_EXCEPTION)
                         ", %rax\n\tsyscall\n\tret\n\t");
}

extern "C" __declspec(dllexport) [[noreturn]] void RtlRestoreContext(
    micant::CONTEXT* context, ExceptionRecord64* exceptionRecord) noexcept;

namespace {
struct alignas(16) VectoredEntry { uint64_t handler{}, state{}; };
struct VectoredEntryValue { uint64_t handler{}, state{}; };
static_assert(sizeof(VectoredEntry) == 16 && alignof(VectoredEntry) == 16);
VectoredEntry g_vectoredHandlers[32]{};
VectoredEntry g_vectoredContinueHandlers[32]{};
uint64_t g_nextVectoredCookie{1};
uint64_t g_nextVectoredOrder{1};
uint64_t g_vectoredPublication{};

struct RuntimeFunction64 { uint32_t beginAddress, endAddress, unwindInfo; };
struct UnwindState64 {
    uint64_t registers[16]{};
    uint64_t rip{};
    micant::M128A xmm[16]{};
    [[nodiscard]] uint64_t& rsp() noexcept { return registers[4]; }
};
struct alignas(64) HandlerContextStorage {
    micant::CONTEXT context{};
    micant::CONTEXT_EX contextEx{};
    // XSAVE requires a contiguous 512-byte legacy prefix followed by the
    // extended state. The public CONTEXT_EX points at the latter only.
    alignas(64) uint8_t xsaveArea[4096]{};
};
static_assert(offsetof(HandlerContextStorage, contextEx) == sizeof(micant::CONTEXT));
static_assert(offsetof(HandlerContextStorage, xsaveArea) % 64 == 0);
struct ImageView64 {
    uintptr_t base{};
    uint32_t size{};
    uint32_t exceptionRva{};
    uint32_t exceptionSize{};
};
struct DispatcherContext64 {
    uint64_t controlPc{};
    uint64_t imageBase{};
    RuntimeFunction64* functionEntry{};
    uint64_t establisherFrame{};
    uint64_t targetIp{};
    micant::CONTEXT* contextRecord{};
    void* languageHandler{};
    void* handlerData{};
    void* historyTable{};
    uint32_t scopeIndex{};
    uint32_t controlPcIsUnwound{};
    void* nonVolatileRegisters{};
};

constexpr uint32_t UnwindFlagExceptionHandler = 0x1;
constexpr uint32_t UnwindFlagTerminationHandler = 0x2;
constexpr uint32_t UnwindFlagChainInfo = 0x4;
constexpr int32_t ExceptionNestedException = 2;
constexpr int32_t ExceptionCollidedUnwind = 3;
constexpr int32_t VectoredContinueExecution = -1;
constexpr int32_t ExceptionContinueExecution = 0; // EXCEPTION_DISPOSITION
constexpr int32_t ExceptionContinueSearch = 1;    // EXCEPTION_DISPOSITION
constexpr uint32_t StatusNoncontinuableException = 0xC0000025u;
constexpr uint32_t StatusInvalidDisposition = 0xC0000026u;
constexpr uint32_t StatusUnwindConsolidate = 0x80000029u;
[[noreturn]] void raiseDispatchFailure(uint32_t status, ExceptionRecord64* nested) noexcept;

bool locateXstateChunk(micant::CONTEXT_EX* contextEx,
                       micant::XSTATE_HEADER*& header,
                       uint32_t& length) noexcept;

bool buildHandlerContext(const micant::CONTEXT& source, const UnwindState64& walk,
                         HandlerContextStorage& storage) noexcept {
    storage.context = source;
    auto* sourceRegisters = &storage.context.Rax;
    for (size_t i = 0; i < 16; ++i) sourceRegisters[i] = walk.registers[i];
    storage.context.Rip = walk.rip;
    for (size_t i = 0; i < 16; ++i)
        storage.context.FltSave.XmmRegisters[i] = walk.xmm[i];

    // RtlUnwindEx may use its captured context as the handler context. Its
    // extension already describes the same resident XSTATE storage.
    if (&source == &storage.context) return true;

    storage.contextEx = {};
    storage.contextEx.All.Offset = -static_cast<int32_t>(sizeof(micant::CONTEXT));
    storage.contextEx.Legacy.Offset = storage.contextEx.All.Offset;
    storage.contextEx.Legacy.Length = sizeof(micant::CONTEXT);
    if (!(storage.context.ContextFlags & CONTEXT_XSTATE)) {
        storage.contextEx.All.Length = sizeof(micant::CONTEXT) + sizeof(micant::CONTEXT_EX);
        storage.contextEx.XState.Offset = sizeof(micant::CONTEXT_EX) + 1;
        return true;
    }

    auto* sourceEx = reinterpret_cast<micant::CONTEXT_EX*>(
        reinterpret_cast<uintptr_t>(const_cast<micant::CONTEXT*>(&source)) + sizeof(micant::CONTEXT));
    micant::XSTATE_HEADER* sourceHeader{};
    uint32_t xstateLength{};
    if (!locateXstateChunk(sourceEx, sourceHeader, xstateLength) ||
        xstateLength > sizeof(storage.xsaveArea) - 512) return false;
    std::memcpy(storage.xsaveArea + 512, sourceHeader, xstateLength);
    const uintptr_t offset = reinterpret_cast<uintptr_t>(storage.xsaveArea + 512) -
                             reinterpret_cast<uintptr_t>(&storage.contextEx);
    if (offset > INT32_MAX || offset > UINT32_MAX - sizeof(micant::CONTEXT) - xstateLength)
        return false;
    storage.contextEx.XState.Offset = static_cast<int32_t>(offset);
    storage.contextEx.XState.Length = xstateLength;
    storage.contextEx.All.Length = static_cast<uint32_t>(sizeof(micant::CONTEXT) + offset + xstateLength);
    return true;
}

bool commitHandlerContext(const HandlerContextStorage& storage,
                          micant::CONTEXT& destination) noexcept {
    if (&storage.context == &destination) return true;
    destination = storage.context;
    if (!(destination.ContextFlags & CONTEXT_XSTATE)) return true;
    auto* destinationEx = reinterpret_cast<micant::CONTEXT_EX*>(
        reinterpret_cast<uintptr_t>(&destination) + sizeof(micant::CONTEXT));
    auto* sourceEx = const_cast<micant::CONTEXT_EX*>(&storage.contextEx);
    micant::XSTATE_HEADER* destinationHeader{};
    micant::XSTATE_HEADER* sourceHeader{};
    uint32_t destinationLength{}, sourceLength{};
    if (!locateXstateChunk(destinationEx, destinationHeader, destinationLength) ||
        !locateXstateChunk(sourceEx, sourceHeader, sourceLength) ||
        destinationLength != sourceLength) return false;
    std::memcpy(destinationHeader, sourceHeader, sourceLength);
    return true;
}

bool imageRange(const ImageView64& image, uint32_t rva, size_t length) noexcept {
    return image.base && rva <= image.size && length <= image.size - rva;
}

bool nonvolatileRegister(uint8_t reg) noexcept {
    return reg == 3 || (reg >= 5 && reg <= 7) || reg >= 12;
}

bool inspectImage(uintptr_t base, uint32_t imageSize, ImageView64& image) noexcept {
    if (!base || imageSize < 0x100 || base > UINTPTR_MAX - imageSize) return false;
    const auto* dos = reinterpret_cast<const uint8_t*>(base);
    if (*reinterpret_cast<const uint16_t*>(dos) != 0x5A4D) return false;
    const int32_t ntOffset = *reinterpret_cast<const int32_t*>(dos + 0x3C);
    if (ntOffset < 0x40 || ntOffset > 0x100000 ||
        static_cast<uint32_t>(ntOffset) > imageSize - 24) return false;
    const auto* nt = dos + ntOffset;
    if (*reinterpret_cast<const uint32_t*>(nt) != 0x00004550u ||
        *reinterpret_cast<const uint16_t*>(nt + 4) != 0x8664) return false;
    const uint16_t optionalSize = *reinterpret_cast<const uint16_t*>(nt + 20);
    const auto* optional = nt + 24;
    if (optionalSize < 112 || optionalSize > imageSize - 24 ||
        static_cast<uint32_t>(ntOffset) > imageSize - 24 - optionalSize ||
        *reinterpret_cast<const uint16_t*>(optional) != 0x020B) return false;
    const uint32_t declaredSize = *reinterpret_cast<const uint32_t*>(optional + 56);
    const uint32_t directoryCount = *reinterpret_cast<const uint32_t*>(optional + 108);
    if (!declaredSize || declaredSize > imageSize || directoryCount > 16 ||
        directoryCount > (optionalSize - 112) / 8) return false;
    // Images containing only leaf functions can legitimately omit the
    // exception directory entirely; they still need to be discoverable so
    // the dispatcher can unwind those frames using their return addresses.
    if (directoryCount <= 3) {
        image = {base, declaredSize, 0, 0};
        return true;
    }
    if (optionalSize < 144) return false;
    const uint32_t exceptionRva = *reinterpret_cast<const uint32_t*>(optional + 112 + 3 * 8);
    const uint32_t exceptionSize = *reinterpret_cast<const uint32_t*>(optional + 112 + 3 * 8 + 4);
    image = {base, declaredSize, exceptionRva, exceptionSize};
    if (!exceptionRva) return exceptionSize == 0;
    if (exceptionSize < sizeof(RuntimeFunction64) ||
        exceptionSize % sizeof(RuntimeFunction64) != 0 ||
        !imageRange(image, exceptionRva, exceptionSize)) return false;
    return true;
}

bool findImage(uintptr_t controlPc, ImageView64& image) noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb) return false;
    const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    if (!peb) return false;

    // PEB_LDR_DATA.InMemoryOrderModuleList and LDR_DATA_TABLE_ENTRY's
    // InMemoryOrderLinks are at their public AMD64 ABI offsets.
    const uintptr_t loader = *reinterpret_cast<const uintptr_t*>(peb + 0x18);
    if (loader) {
        const uintptr_t head = loader + 0x20;
        uintptr_t link = *reinterpret_cast<const uintptr_t*>(head);
        for (size_t count = 0; link && link != head && count < 256; ++count) {
            if (link < 0x10000 || link >= 0x0000800000000000ULL) return false;
            const uintptr_t next = *reinterpret_cast<const uintptr_t*>(link);
            const uintptr_t entry = link - 0x10;
            const uintptr_t base = *reinterpret_cast<const uintptr_t*>(entry + 0x30);
            const uint32_t imageSize = *reinterpret_cast<const uint32_t*>(entry + 0x40);
            ImageView64 candidate{};
            if (base && imageSize >= 0x100 && controlPc >= base &&
                inspectImage(base, imageSize, candidate) &&
                controlPc - base < candidate.size) {
                image = candidate;
                return true;
            }
            link = next;
        }
    }

    // Without a loader entry there is no trusted mapped-image bound. Never
    // probe arbitrary user memory based on PE-declared sizes alone.
    return false;
}

RuntimeFunction64* lookupFunction(const ImageView64& image, uintptr_t controlPc) noexcept {
    if (!image.exceptionRva || controlPc < image.base ||
        controlPc - image.base > UINT32_MAX) return nullptr;
    const uint32_t rva = static_cast<uint32_t>(controlPc - image.base);
    const size_t count = image.exceptionSize / sizeof(RuntimeFunction64);
    auto* table = reinterpret_cast<RuntimeFunction64*>(image.base + image.exceptionRva);
    size_t low = 0, high = count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        const auto& entry = table[mid];
        if (rva < entry.beginAddress) high = mid;
        else if (rva >= entry.endAddress) low = mid + 1;
        else return entry.beginAddress < entry.endAddress &&
                    entry.endAddress <= image.size ? &table[mid] : nullptr;
    }
    return nullptr;
}

uint64_t& unwindRegister(UnwindState64& context, uint8_t reg) noexcept {
    return context.registers[reg];
}

bool tryUnwindEpilog(const ImageView64& image, const RuntimeFunction64& function,
                     uintptr_t controlPc, UnwindState64& context,
                     uint64_t stackLimit, uint64_t stackBase) noexcept {
    if (controlPc < image.base + function.beginAddress ||
        controlPc >= image.base + function.endAddress) return false;
    const uint32_t endRva = function.endAddress;
    uint32_t cursor = static_cast<uint32_t>(controlPc - image.base);
    if (cursor > UINT32_MAX - 8) return false;
    UnwindState64 candidate = context;
    auto byteAt = [&](uint32_t rva, uint8_t& value) noexcept {
        if (rva >= endRva || !imageRange(image, rva, 1)) return false;
        value = *reinterpret_cast<const uint8_t*>(image.base + rva);
        return true;
    };
    auto readImmediate = [&](uint32_t rva, unsigned bytes, uint32_t& value) noexcept {
        if (rva > endRva || bytes > endRva - rva || !imageRange(image, rva, bytes)) return false;
        value = 0;
        std::memcpy(&value, reinterpret_cast<const void*>(image.base + rva), bytes);
        return true;
    };

    // The optional first instruction adjusts RSP with ADD or LEA.
    uint8_t b0{}, b1{}, b2{};
    if (byteAt(cursor, b0) && b0 == 0x48 && byteAt(cursor + 1, b1) &&
        (b1 == 0x83 || b1 == 0x81) && byteAt(cursor + 2, b2) && b2 == 0xC4) {
        uint32_t amount{};
        const unsigned immediateBytes = b1 == 0x83 ? 1 : 4;
        if (!readImmediate(cursor + 3, immediateBytes, amount)) return false;
        if (immediateBytes == 1) amount = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(amount)));
        const int64_t delta = immediateBytes == 1 ? static_cast<int8_t>(amount) : static_cast<int32_t>(amount);
        const uint64_t rsp = candidate.rsp();
        if ((delta < 0 && static_cast<uint64_t>(-delta) > rsp) ||
            (delta > 0 && static_cast<uint64_t>(delta) > UINT64_MAX - rsp)) return false;
        candidate.rsp() = delta < 0 ? rsp - static_cast<uint64_t>(-delta) : rsp + static_cast<uint64_t>(delta);
        cursor += 3 + immediateBytes;
    } else if (byteAt(cursor, b0) && (b0 == 0x48 || b0 == 0x49) &&
               byteAt(cursor + 1, b1) && b1 == 0x8D && byteAt(cursor + 2, b2) &&
               ((b2 >> 3) & 7) == 4 && ((b2 >> 6) & 3) != 3) {
        const uint8_t mod = b2 >> 6;
        const uint8_t rm = b2 & 7;
        if (mod == 0 && rm == 5) return false; // RIP-relative is not a frame register.
        uint8_t baseRegister = static_cast<uint8_t>(rm | ((b0 & 1) ? 8 : 0));
        uint32_t displacementRva = cursor + 3;
        if (rm == 4) {
            uint8_t sib{};
            if (!byteAt(displacementRva++, sib) || ((sib >> 3) & 7) != 4 ||
                (mod == 0 && (sib & 7) == 5)) return false;
            baseRegister = static_cast<uint8_t>((sib & 7) | ((b0 & 1) ? 8 : 0));
        }
        if (!nonvolatileRegister(baseRegister)) return false;
        int64_t displacement{};
        uint32_t raw{};
        unsigned immediateBytes{};
        if (mod == 1) immediateBytes = 1;
        else if (mod == 2) immediateBytes = 4;
        if (immediateBytes) {
            if (!readImmediate(displacementRva, immediateBytes, raw)) return false;
            displacement = immediateBytes == 1 ? static_cast<int8_t>(raw) : static_cast<int32_t>(raw);
        }
        const uint64_t base = candidate.registers[baseRegister];
        if ((displacement < 0 && static_cast<uint64_t>(-displacement) > base) ||
            (displacement > 0 && static_cast<uint64_t>(displacement) > UINT64_MAX - base)) return false;
        candidate.rsp() = displacement < 0 ? base - static_cast<uint64_t>(-displacement)
                                             : base + static_cast<uint64_t>(displacement);
        cursor = displacementRva + immediateBytes;
    }

    // Remaining legal epilogue operations are pops of nonvolatile GPRs,
    // followed by RET/RET imm16. Execute only instructions inside this image.
    for (size_t instruction = 0; instruction < 32; ++instruction) {
        if (cursor > UINT32_MAX - 8) return false;
        uint8_t opcode{};
        if (!byteAt(cursor, opcode)) return false;
        uint8_t reg{};
        uint32_t length{};
        if (opcode >= 0x58 && opcode <= 0x5F) {
            reg = opcode & 7;
            length = 1;
        } else if (opcode == 0x41) {
            uint8_t extended{};
            if (!byteAt(cursor + 1, extended) || extended < 0x58 || extended > 0x5F) return false;
            reg = static_cast<uint8_t>((extended & 7) + 8);
            length = 2;
        } else if (opcode == 0xFF || (opcode >= 0x40 && opcode <= 0x4F)) {
            uint32_t opcodeRva = cursor;
            if (opcode != 0xFF) {
                if (!byteAt(++opcodeRva, opcode) || opcode != 0xFF) return false;
            }
            uint8_t modrm{};
            if (!byteAt(opcodeRva + 1, modrm) || (modrm >> 6) != 0 ||
                ((modrm >> 3) & 7) != 4) return false;
            uint32_t addressRva = opcodeRva + 2;
            const uint8_t rm = modrm & 7;
            if (rm == 4) {
                uint8_t sib{};
                if (!byteAt(addressRva++, sib)) return false;
                if ((sib & 7) == 5) {
                    uint32_t displacement{};
                    if (!readImmediate(addressRva, 4, displacement)) return false;
                }
            } else if (rm == 5) {
                uint32_t displacement{};
                if (!readImmediate(addressRva, 4, displacement)) return false;
            }
            // A tail jump leaves the caller's return address on the stack.
            // Virtual unwind returns to that caller; it must not execute or
            // dereference the jump target itself.
            if (candidate.rsp() < stackLimit || candidate.rsp() > stackBase - sizeof(uint64_t)) return false;
            std::memcpy(&candidate.rip, reinterpret_cast<const void*>(candidate.rsp()), sizeof(candidate.rip));
            candidate.rsp() += sizeof(uint64_t);
            if (!candidate.rip) return false;
            context = candidate;
            return true;
        } else if (opcode == 0xC3 || opcode == 0xC2) {
            if (candidate.rsp() < stackLimit || candidate.rsp() > stackBase - sizeof(uint64_t)) return false;
            candidate.rip = *reinterpret_cast<const uint64_t*>(candidate.rsp());
            candidate.rsp() += sizeof(uint64_t);
            if (opcode == 0xC2) {
                uint32_t amount{};
                if (!readImmediate(cursor + 1, 2, amount) || amount > stackBase - candidate.rsp()) return false;
                candidate.rsp() += amount;
            }
            if (!candidate.rip || (candidate.rsp() & 7)) return false;
            context = candidate;
            return true;
        } else {
            return false;
        }
        if (!nonvolatileRegister(reg) || candidate.rsp() < stackLimit ||
            candidate.rsp() > stackBase - sizeof(uint64_t)) return false;
        candidate.registers[reg] = *reinterpret_cast<const uint64_t*>(candidate.rsp());
        candidate.rsp() += sizeof(uint64_t);
        cursor += length;
    }
    return false;
}

bool unwindFrame(const ImageView64& image, uintptr_t controlPc,
                 UnwindState64& context, RuntimeFunction64* function,
                 uint32_t handlerFlag, uint64_t stackLimit, uint64_t stackBase,
                 uint64_t& establisher,
                 void*& handler, void*& handlerData) noexcept {
    handler = nullptr;
    handlerData = nullptr;
    if (!function) {
        if (context.rsp() < stackLimit || context.rsp() > stackBase - sizeof(uint64_t)) return false;
        context.rip = *reinterpret_cast<const uint64_t*>(context.rsp());
        context.rsp() += sizeof(uint64_t);
        establisher = context.rsp();
        return context.rip != 0;
    }
    if (!imageRange(image, function->unwindInfo, 4) ||
        function->beginAddress >= function->endAddress || function->endAddress > image.size ||
        controlPc < image.base + function->beginAddress || controlPc >= image.base + function->endAddress)
        return false;

    const auto* info = reinterpret_cast<const uint8_t*>(image.base + function->unwindInfo);
    const uint32_t initialCodeOffset = static_cast<uint32_t>(controlPc - (image.base + function->beginAddress));
    const uint64_t epilogEstablisher = context.rsp();
    if (initialCodeOffset > info[1]) {
        if (tryUnwindEpilog(image, *function, controlPc, context, stackLimit, stackBase)) {
            establisher = epilogEstablisher;
            return true;
        }
        // A prefix such as REX or POP can occur in ordinary function code.
        // Only a complete legal epilogue is an epilogue; otherwise use the
        // prologue unwind metadata with the original, unmodified context.
    }
    establisher = context.rsp();
    bool machineFrame = false;
    uint32_t codeOffset = static_cast<uint32_t>(controlPc - (image.base + function->beginAddress));
    uint8_t finalFlags{};
    RuntimeFunction64 chainedFunction{};

    // Chained records describe one logical frame split across function
    // fragments. Apply the active fragment's prologue offset first, then all
    // parent records in full, matching RtlVirtualUnwind's chain traversal.
    for (size_t chainDepth = 0; ; ++chainDepth) {
        if (chainDepth >= 16 || !imageRange(image, function->unwindInfo, 4)) return false;
        info = reinterpret_cast<const uint8_t*>(image.base + function->unwindInfo);
        const uint8_t version = info[0] & 7;
        const uint8_t flags = info[0] >> 3;
        const uint8_t codeCount = info[2];
        const uint8_t frameRegister = info[3] & 0xF;
        const uint8_t frameOffset = info[3] >> 4;
        const size_t codeBytes = static_cast<size_t>(codeCount) * 2;
        if ((version != 1 && version != 2) ||
            !imageRange(image, function->unwindInfo, 4 + codeBytes) ||
            (flags & ~(UnwindFlagExceptionHandler | UnwindFlagTerminationHandler | UnwindFlagChainInfo)) ||
            ((flags & UnwindFlagChainInfo) && (flags & (UnwindFlagExceptionHandler | UnwindFlagTerminationHandler))) ||
            (frameRegister && !nonvolatileRegister(frameRegister))) return false;
        const auto* codes = info + 4;
        size_t i = 0;
        if (chainDepth == 0) {
            while (i < codeCount && codes[i * 2] > codeOffset) {
                const uint8_t op = codes[i * 2 + 1] & 0xF;
                const uint8_t opInfo = codes[i * 2 + 1] >> 4;
                const size_t slots = op == 1 && opInfo ? 3 :
                    (op == 1 || op == 4 || op == 6 || op == 8) ? 2 :
                    (op == 5 || op == 7 || op == 9 || op == 11) ? 3 : 1;
                if (slots > codeCount - i) return false;
                i += slots;
            }
            // Within a prologue the frame register is not valid until its
            // UWOP_SET_FPREG instruction has executed. Before that point
            // the current RSP is the establisher frame.
            bool frameEstablished = codeOffset >= info[1] || (flags & UnwindFlagChainInfo);
            for (size_t j = i; frameRegister && !frameEstablished && j < codeCount;) {
                const uint8_t op = codes[j * 2 + 1] & 0xF;
                const uint8_t opInfo = codes[j * 2 + 1] >> 4;
                const size_t slots = op == 1 && opInfo ? 3 :
                    (op == 1 || op == 4 || op == 6 || op == 8) ? 2 :
                    (op == 5 || op == 7 || op == 9 || op == 11) ? 3 : 1;
                if (slots > codeCount - j) return false;
                if (op == 3) frameEstablished = true;
                j += slots;
            }
            if (frameRegister && frameEstablished) {
                const uint64_t frame = unwindRegister(context, frameRegister);
                const uint64_t displacement = static_cast<uint64_t>(frameOffset) * 16;
                if (frame < displacement) return false;
                establisher = frame - displacement;
            }
        }
        while (i < codeCount) {
        if (context.rsp() < stackLimit || context.rsp() >= stackBase) return false;
        const uint8_t op = codes[i * 2 + 1] & 0xF;
        const uint8_t opInfo = codes[i * 2 + 1] >> 4;
        uint32_t offset{};
        switch (op) {
            case 0: // UWOP_PUSH_NONVOL
                if (!nonvolatileRegister(opInfo) || context.rsp() < stackLimit ||
                    context.rsp() > stackBase - sizeof(uint64_t)) return false;
                unwindRegister(context, opInfo) = *reinterpret_cast<const uint64_t*>(context.rsp());
                context.rsp() += 8;
                ++i;
                break;
            case 1: // UWOP_ALLOC_LARGE
                if (!opInfo) {
                    if (i + 1 >= codeCount) return false;
                    offset = *reinterpret_cast<const uint16_t*>(codes + (i + 1) * 2) * 8u;
                    i += 2;
                } else if (opInfo == 1) {
                    if (i + 2 >= codeCount) return false;
                    offset = *reinterpret_cast<const uint32_t*>(codes + (i + 1) * 2);
                    i += 3;
                } else return false;
                if (context.rsp() > stackBase || offset > stackBase - context.rsp() ||
                    sizeof(uint64_t) > stackBase - context.rsp() - offset) return false;
                context.rsp() += offset;
                break;
            case 2: // UWOP_ALLOC_SMALL
                offset = (opInfo + 1u) * 8u;
                if (context.rsp() > stackBase || offset > stackBase - context.rsp()) return false;
                context.rsp() += offset;
                ++i;
                break;
            case 3: // UWOP_SET_FPREG
                if (!frameRegister) return false;
                context.rsp() = unwindRegister(context, frameRegister) - frameOffset * 16;
                ++i;
                break;
            case 4: // UWOP_SAVE_NONVOL
            case 5: // UWOP_SAVE_NONVOL_FAR
                if (!nonvolatileRegister(opInfo) || (op == 4 && i + 1 >= codeCount) ||
                    (op == 5 && i + 2 >= codeCount)) return false;
                offset = op == 4 ?
                    static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(codes + (i + 1) * 2)) * 8u :
                    *reinterpret_cast<const uint32_t*>(codes + (i + 1) * 2);
                if (context.rsp() > stackBase || offset > stackBase - context.rsp() ||
                    sizeof(uint64_t) > stackBase - context.rsp() - offset) return false;
                unwindRegister(context, opInfo) = *reinterpret_cast<const uint64_t*>(context.rsp() + offset);
                i += op == 4 ? 2 : 3;
                break;
            case 6: // UWOP_EPILOG (version 2)
                if (version != 2 || i + 1 >= codeCount) return false;
                i += 2;
                break;
            case 8: // UWOP_SAVE_XMM128
            case 9: // UWOP_SAVE_XMM128_FAR
                if (opInfo < 6 || (op == 8 && i + 1 >= codeCount) ||
                    (op == 9 && i + 2 >= codeCount)) return false;
                offset = op == 8 ?
                    static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(codes + (i + 1) * 2)) * 16u :
                    *reinterpret_cast<const uint32_t*>(codes + (i + 1) * 2);
                if (context.rsp() > stackBase || offset > stackBase - context.rsp() ||
                    sizeof(micant::M128A) > stackBase - context.rsp() - offset) return false;
                std::memcpy(&context.xmm[opInfo],
                    reinterpret_cast<const void*>(context.rsp() + offset), sizeof(micant::M128A));
                i += op == 8 ? 2 : 3;
                break;
            case 10: // UWOP_PUSH_MACHFRAME
                if (opInfo > 1 || i + 1 != codeCount || (flags & UnwindFlagChainInfo)) return false;
                if (context.rsp() < stackLimit || context.rsp() > stackBase - (opInfo ? 0x28 : 0x20))
                    return false;
                context.rsp() += opInfo ? 8 : 0;
                context.rip = *reinterpret_cast<const uint64_t*>(context.rsp());
                context.rsp() = *reinterpret_cast<const uint64_t*>(context.rsp() + 0x18);
                machineFrame = true;
                i = codeCount;
                break;
                default: return false;
            }
        }

        finalFlags = flags;
        if (!(flags & UnwindFlagChainInfo)) break;
        const size_t chainedOffset = 4 + ((static_cast<size_t>(codeCount) + 1) & ~size_t{1}) * 2;
        if (!imageRange(image, function->unwindInfo, chainedOffset + sizeof(RuntimeFunction64))) return false;
        RuntimeFunction64 chained{};
        std::memcpy(&chainedFunction, info + chainedOffset, sizeof(chainedFunction));
        if (chainedFunction.beginAddress >= chainedFunction.endAddress ||
            chainedFunction.endAddress > image.size ||
            !imageRange(image, chainedFunction.unwindInfo, 4)) return false;
        function = &chainedFunction;
        codeOffset = 0;
    }

    if (!machineFrame) {
        if (context.rsp() < stackLimit || context.rsp() > stackBase - sizeof(uint64_t)) return false;
        context.rip = *reinterpret_cast<const uint64_t*>(context.rsp());
        context.rsp() += 8;
    }
    if (!context.rip || (context.rsp() & 7)) return false;

    if (finalFlags & handlerFlag) {
        const uint8_t codeCount = info[2];
        const size_t handlerOffset = (4 + static_cast<size_t>(codeCount) * 2 + 3) & ~size_t{3};
        if (!imageRange(image, function->unwindInfo, handlerOffset + 4)) return false;
        const uint32_t handlerRva = *reinterpret_cast<const uint32_t*>(info + handlerOffset);
        if (!imageRange(image, handlerRva, 1)) return false;
        handler = reinterpret_cast<void*>(image.base + handlerRva);
        handlerData = const_cast<uint8_t*>(info + handlerOffset + 4);
    }
    return true;
}

bool dispatchFrameHandlers(ExceptionRecord64* record, micant::CONTEXT* context) noexcept {
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb) return false;
    const uint64_t stackBase = *reinterpret_cast<const uint64_t*>(teb + 8);
    const uint64_t stackLimit = *reinterpret_cast<const uint64_t*>(teb + 16);
    if (stackBase < sizeof(uint64_t) || stackLimit >= stackBase ||
        stackBase - stackLimit < sizeof(uint64_t) || context->Rsp < stackLimit ||
        context->Rsp >= stackBase || (context->Rsp & 7)) return false;

    constexpr uint32_t ExceptionNoncontinuable = 0x01;
    constexpr uint32_t ExceptionNestedCall = 0x10;
    const uint32_t originalNestedCall = record->flags & ExceptionNestedCall;
    bool nestedCallActive = originalNestedCall != 0;
    struct NestedCallFlagRestore {
        ExceptionRecord64* record;
        uint32_t original;
        ~NestedCallFlagRestore() noexcept {
            record->flags = (record->flags & ~uint32_t{0x10}) | original;
        }
    } nestedCallFlagRestore{record, originalNestedCall};

    UnwindState64 walk{};
    auto* sourceRegisters = &context->Rax;
    for (size_t i = 0; i < 16; ++i) walk.registers[i] = sourceRegisters[i];
    walk.rip = context->Rip;
    for (size_t i = 0; i < 16; ++i) walk.xmm[i] = context->FltSave.XmmRegisters[i];
    uint64_t lastRsp{};
    for (size_t frame = 0; frame < 128 && walk.rip && walk.rsp() >= stackLimit && walk.rsp() < stackBase; ++frame) {
        if (frame && walk.rsp() <= lastRsp) return false;
        lastRsp = walk.rsp();
        const uintptr_t controlPc = static_cast<uintptr_t>(walk.rip - (frame && walk.rip ? 1 : 0));
        ImageView64 image{};
        if (!findImage(controlPc, image)) return false;
        RuntimeFunction64* function = lookupFunction(image, controlPc);
        const uint64_t currentRip = walk.rip;
        uint64_t establisher{};
        void* languageHandler{};
        void* handlerData{};
        if (!unwindFrame(image, controlPc, walk, function, UnwindFlagExceptionHandler,
                         stackLimit, stackBase, establisher,
                         languageHandler, handlerData)) return false;
        if (establisher < stackLimit || establisher >= stackBase || (establisher & 7)) {
            // Never invoke a language handler with a frame derived from
            // malformed unwind metadata or outside the active user stack.
            record->flags |= 0x8; // EXCEPTION_STACK_INVALID
            return false;
        }
        if (languageHandler) {
            HandlerContextStorage handlerStorage{};
            if (!buildHandlerContext(*context, walk, handlerStorage)) return false;
            DispatcherContext64 dispatcher{};
            dispatcher.controlPc = controlPc;
            dispatcher.imageBase = image.base;
            dispatcher.functionEntry = function;
            dispatcher.establisherFrame = establisher;
            dispatcher.contextRecord = &handlerStorage.context;
            dispatcher.languageHandler = languageHandler;
            dispatcher.handlerData = handlerData;
            using LanguageHandler = int32_t (*)(ExceptionRecord64*, void*, micant::CONTEXT*, DispatcherContext64*);
            if (nestedCallActive) record->flags |= ExceptionNestedCall;
            else record->flags &= ~ExceptionNestedCall;
            const auto disposition = reinterpret_cast<LanguageHandler>(languageHandler)(
                record, reinterpret_cast<void*>(establisher), context, &dispatcher);
            if (disposition == ExceptionNestedException) {
                nestedCallActive = true;
                record->flags |= ExceptionNestedCall;
            } else {
                nestedCallActive = false;
                record->flags &= ~ExceptionNestedCall;
            }
            if (disposition == ExceptionContinueExecution) {
                // The language handler receives the original CPU CONTEXT and
                // may edit it for continuation. The separately unwound copy
                // in DispatcherContext is only for RtlUnwindEx; committing it
                // here would resume in the caller frame instead of the fault.
                if (!(record->flags & ExceptionNoncontinuable)) return true;
                raiseDispatchFailure(StatusNoncontinuableException, record);
            }
            if (disposition == ExceptionNestedException) {
                // Keep EXCEPTION_NESTED_CALL visible to the next frame's
                // language handler; it is cleared after that invocation
                // unless that handler reports another nested exception.
            } else if (disposition != ExceptionContinueSearch) {
                raiseDispatchFailure(StatusInvalidDisposition, record);
            }
        }
        if (walk.rsp() <= stackLimit || walk.rsp() >= stackBase) return false;
    }
    return false;
}

bool locateXstateChunk(micant::CONTEXT_EX* contextEx,
                       micant::XSTATE_HEADER*& header,
                       uint32_t& length) noexcept {
    if (!contextEx) return false;
    auto* context = reinterpret_cast<micant::CONTEXT*>(contextEx) - 1;
    if (!(context->ContextFlags & CONTEXT_XSTATE) ||
        contextEx->All.Offset != -static_cast<int32_t>(sizeof(micant::CONTEXT)) ||
        contextEx->Legacy.Offset != contextEx->All.Offset ||
        contextEx->Legacy.Length != sizeof(micant::CONTEXT) ||
        contextEx->XState.Offset < static_cast<int32_t>(sizeof(micant::CONTEXT_EX)) ||
        contextEx->XState.Length < sizeof(micant::XSTATE_HEADER)) return false;
    const uint64_t chunkEnd = static_cast<uint64_t>(contextEx->XState.Offset) +
                              contextEx->XState.Length;
    if (contextEx->All.Length < sizeof(micant::CONTEXT) ||
        chunkEnd > contextEx->All.Length - sizeof(micant::CONTEXT)) return false;
    auto* address = reinterpret_cast<uint8_t*>(contextEx) + contextEx->XState.Offset;
    if (reinterpret_cast<uintptr_t>(address) & 63) return false;
    header = reinterpret_cast<micant::XSTATE_HEADER*>(address);
    length = contextEx->XState.Length;
    return true;
}

bool copyContextRecord(const micant::CONTEXT* source,
                       micant::CONTEXT* destination) noexcept {
    if (!source || !destination) return false;
    if (source == destination) return true;
    const bool sourceHasXstate = (source->ContextFlags & CONTEXT_XSTATE) != 0;
    if (!sourceHasXstate) {
        std::memmove(destination, source, sizeof(*destination));
        return true;
    }

    auto* sourceEx = reinterpret_cast<micant::CONTEXT_EX*>(
        reinterpret_cast<uintptr_t>(const_cast<micant::CONTEXT*>(source)) + sizeof(*source));
    auto* destinationEx = reinterpret_cast<micant::CONTEXT_EX*>(
        reinterpret_cast<uintptr_t>(destination) + sizeof(*destination));
    micant::XSTATE_HEADER* sourceHeader{};
    micant::XSTATE_HEADER* destinationHeader{};
    uint32_t sourceLength{}, destinationLength{};
    if (!locateXstateChunk(sourceEx, sourceHeader, sourceLength) ||
        !locateXstateChunk(destinationEx, destinationHeader, destinationLength) ||
        sourceLength != destinationLength || sourceLength > 4096) return false;

    // Preserve the small legacy context and destination descriptor before
    // moving XSTATE: either one can overlap the source or destination tail.
    // memmove handles arbitrary tail overlap without allocating a VAD or a
    // second 4 KiB stack buffer across a nonlocal unwind.
    micant::CONTEXT savedContext = *source;
    const micant::CONTEXT_EX savedDestinationEx = *destinationEx;
    std::memmove(destinationHeader, sourceHeader, sourceLength);
    *destinationEx = savedDestinationEx;
    std::memcpy(destination, &savedContext, sizeof(savedContext));
    return true;
}

uint64_t readXcr0() noexcept {
    uint32_t low{}, high{};
    __asm__ __volatile__("xorl %%ecx, %%ecx; xgetbv" : "=a"(low), "=d"(high) : : "ecx");
    return static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
}

void queryXstateFeature(uint32_t feature, uint32_t& size, uint32_t& offset) noexcept {
    uint32_t eax = 0x0D, ebx{}, ecx = feature, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    size = eax;
    offset = ebx;
}

// The public RtlUnwindEx buffer is only a CONTEXT. Keep any extension in our
// own aligned stack storage. Its XSAVE area provides a contiguous legacy
// prefix and extended tail without a temporary VAD or callback lifetime.
int32_t captureUnwindXstate(HandlerContextStorage& storage) noexcept {
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if ((ecx & ((1u << 26) | (1u << 27))) != ((1u << 26) | (1u << 27))) return 0;
    const uint64_t mask = readXcr0();
    if (!(mask & ~uint64_t{3})) return 0;
    if (mask >> 32) return static_cast<int32_t>(0xC00000BBu);
    eax = 0x0d; ecx = 0;
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (ebx < 576 || ebx > sizeof(storage.xsaveArea))
        return static_cast<int32_t>(0xC00000BBu);
    const uint32_t low = static_cast<uint32_t>(mask);
    const uint32_t high = static_cast<uint32_t>(mask >> 32);
    __asm__ __volatile__("xsave64 (%0)" : : "r"(storage.xsaveArea), "a"(low), "d"(high) : "memory");
    const size_t tailLength = ebx - 512;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(storage.xsaveArea + 512) -
                             reinterpret_cast<uintptr_t>(&storage.contextEx);
    storage.contextEx.All.Offset = -static_cast<int32_t>(sizeof(micant::CONTEXT));
    storage.contextEx.All.Length = static_cast<uint32_t>(sizeof(micant::CONTEXT) + offset + tailLength);
    storage.contextEx.Legacy.Offset = storage.contextEx.All.Offset;
    storage.contextEx.Legacy.Length = sizeof(micant::CONTEXT);
    storage.contextEx.XState.Offset = static_cast<int32_t>(offset);
    storage.contextEx.XState.Length = static_cast<uint32_t>(tailLength);
    storage.context.ContextFlags |= CONTEXT_XSTATE;
    return 0;
}

constexpr uint64_t VectoredReserved = uint64_t{1} << 63;
constexpr uint64_t VectoredFirst = uint64_t{1} << 62;
constexpr uint64_t VectoredIdMask = 0x7fffffffULL;

bool vectoredAtomicSupported() noexcept {
    uint32_t eax = 1, ebx{}, ecx{}, edx{};
    __asm__ __volatile__("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (ecx & (1u << 13)) != 0;
}

bool compareVectoredEntry(VectoredEntry& entry, VectoredEntryValue& expected,
                          VectoredEntryValue desired) noexcept {
    uint64_t low = expected.handler, high = expected.state;
    uint8_t exchanged{};
    __asm__ __volatile__("lock cmpxchg16b %1; sete %0"
                         : "=q"(exchanged), "+m"(entry), "+a"(low), "+d"(high)
                         : "b"(desired.handler), "c"(desired.state)
                         : "cc", "memory");
    expected = {low, high};
    return exchanged != 0;
}

VectoredEntryValue loadVectoredEntry(VectoredEntry& entry) noexcept {
    VectoredEntryValue value{};
    (void)compareVectoredEntry(entry, value, {});
    return value;
}

uint64_t allocateVectoredNumber(uint64_t& counter, uint64_t maximum) noexcept {
    uint64_t next = __atomic_load_n(&counter, __ATOMIC_RELAXED);
    while (next && next <= maximum) {
        if (__atomic_compare_exchange_n(&counter, &next, next + 1, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return next;
    }
    return 0;
}

// A publisher can be interrupted by an APC or exception on the same thread.
// Any reader or writer can finish its fully prepared slot and release the
// publication token; no caller waits for that interrupted thread to resume.
void helpVectoredPublication(uint64_t ticket) noexcept {
    if (!ticket) return;
    const size_t slot = static_cast<size_t>((ticket & 0x7f) - 1);
    const uint64_t cookie = ticket >> 7;
    if (slot >= 64 || !cookie || cookie > VectoredIdMask) __builtin_trap();
    auto& entry = slot < 32 ? g_vectoredHandlers[slot] : g_vectoredContinueHandlers[slot - 32];
    auto expected = loadVectoredEntry(entry);
    if ((expected.state & VectoredReserved) && (expected.state & VectoredIdMask) == cookie) {
        const uint64_t order = allocateVectoredNumber(g_nextVectoredOrder, 0x7fffffffULL);
        const VectoredEntryValue published = order
            ? VectoredEntryValue{expected.handler, (expected.state & VectoredFirst) | (order << 31) | cookie}
            : VectoredEntryValue{};
        (void)compareVectoredEntry(entry, expected, published);
    }
    uint64_t pending = ticket;
    (void)__atomic_compare_exchange_n(&g_vectoredPublication, &pending, uint64_t{0}, false,
                                      __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

void publishVectoredReservation(size_t index, uint64_t cookie) noexcept {
    const uint64_t ticket = (cookie << 7) | (index + 1);
    for (;;) {
        uint64_t empty = 0;
        if (__atomic_compare_exchange_n(&g_vectoredPublication, &empty, ticket, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            helpVectoredPublication(ticket);
            return;
        }
        helpVectoredPublication(empty);
    }
}

void* addVectoredHandler(VectoredEntry (&handlers)[32], unsigned long first,
                         VectoredExceptionHandler handler) noexcept {
    if (!handler || !vectoredAtomicSupported()) return nullptr;
    for (size_t slot = 0; slot < 32; ++slot) {
        auto& entry = handlers[slot];
        const auto existing = loadVectoredEntry(entry);
        if (existing.state & VectoredReserved) {
            const size_t index = slot + (&handlers == &g_vectoredContinueHandlers ? 32 : 0);
            publishVectoredReservation(index, existing.state & VectoredIdMask);
        }
        if (loadVectoredEntry(entry).state) continue;
        const uint64_t cookie = allocateVectoredNumber(g_nextVectoredCookie, VectoredIdMask);
        if (!cookie) return nullptr;
        VectoredEntryValue free{};
        const VectoredEntryValue reserved{reinterpret_cast<uintptr_t>(handler),
            VectoredReserved | (first ? VectoredFirst : 0) | cookie};
        if (!compareVectoredEntry(entry, free, reserved)) continue;
        const size_t index = slot + (&handlers == &g_vectoredContinueHandlers ? 32 : 0);
        publishVectoredReservation(index, cookie);
        const auto state = loadVectoredEntry(entry).state;
        if (!(state & VectoredReserved) && (state & VectoredIdMask) == cookie)
            return reinterpret_cast<void*>(static_cast<uintptr_t>(cookie));
        return nullptr;
    }
    return nullptr;
}

unsigned long removeVectoredHandler(VectoredEntry (&handlers)[32], void* cookie) noexcept {
    const uintptr_t id = reinterpret_cast<uintptr_t>(cookie);
    if (!id || id > VectoredIdMask || !vectoredAtomicSupported()) return 0;
    helpVectoredPublication(__atomic_load_n(&g_vectoredPublication, __ATOMIC_ACQUIRE));
    for (auto& entry : handlers) {
        auto value = loadVectoredEntry(entry);
        if ((value.state & VectoredReserved) || (value.state & VectoredIdMask) != id) continue;
        if (compareVectoredEntry(entry, value, {})) return 1;
    }
    return 0;
}

struct VectoredSnapshot { uint64_t state{}; uint8_t slot{}; bool first{}; };

size_t snapshotVectoredHandlers(VectoredEntry (&handlers)[32], VectoredSnapshot (&snapshot)[32]) noexcept {
    size_t count{};
    if (!vectoredAtomicSupported()) return 0;
    helpVectoredPublication(__atomic_load_n(&g_vectoredPublication, __ATOMIC_ACQUIRE));
    // Registrations that begin publishing after this snapshot boundary are
    // not mixed into it even if they occupy a later physical slot.
    const uint64_t cutoff = __atomic_load_n(&g_nextVectoredOrder, __ATOMIC_ACQUIRE) - 1;
    for (size_t i = 0; i < 32; ++i) {
        auto value = loadVectoredEntry(handlers[i]);
        if (value.state & VectoredReserved) {
            const size_t index = i + (&handlers == &g_vectoredContinueHandlers ? 32 : 0);
            publishVectoredReservation(index, value.state & VectoredIdMask);
            value = loadVectoredEntry(handlers[i]);
        }
        const uint64_t state = value.state;
        const uint64_t order = (state & ~(VectoredFirst | VectoredReserved)) >> 31;
        if (!state || (state & VectoredReserved) || !order || order > cutoff) continue;
        const bool first = (state & VectoredFirst) != 0;
        // First handlers are newest-first; the remaining handlers are
        // oldest-first. Insert into the local snapshot without a shared lock.
        size_t pos = count;
        while (pos && ((first && !snapshot[pos - 1].first) ||
               (first == snapshot[pos - 1].first &&
                (first ? order > ((snapshot[pos - 1].state & ~(VectoredFirst | VectoredReserved)) >> 31)
                       : order < ((snapshot[pos - 1].state & ~(VectoredFirst | VectoredReserved)) >> 31))))) {
            snapshot[pos] = snapshot[pos - 1];
            --pos;
        }
        snapshot[pos] = {state, static_cast<uint8_t>(i), first};
        ++count;
    }
    return count;
}

VectoredExceptionHandler resolveVectoredHandler(VectoredEntry (&handlers)[32],
                                                  const VectoredSnapshot& item) noexcept {
    auto& entry = handlers[item.slot];
    const auto value = loadVectoredEntry(entry);
    return value.state == item.state ? reinterpret_cast<VectoredExceptionHandler>(value.handler) : nullptr;
}

void callVectoredContinueHandlers(ExceptionPointers64& pointers) noexcept {
    VectoredSnapshot snapshot[32]{};
    const size_t count = snapshotVectoredHandlers(g_vectoredContinueHandlers, snapshot);
    for (size_t i = 0; i < count; ++i)
        if (const auto handler = resolveVectoredHandler(g_vectoredContinueHandlers, snapshot[i]))
            (void)handler(&pointers);
}
}

extern "C" __declspec(dllexport) int32_t RtlDispatchException(
    ExceptionRecord64* record, micant::CONTEXT* context) noexcept {
    if (!record || !context) return 0;
    ExceptionPointers64 pointers{record, context};
    VectoredSnapshot snapshot[32]{};
    const size_t count = snapshotVectoredHandlers(g_vectoredHandlers, snapshot);
    for (size_t i = 0; i < count; ++i) {
        const auto handler = resolveVectoredHandler(g_vectoredHandlers, snapshot[i]);
        if (handler && handler(&pointers) == VectoredContinueExecution) {
            if (record->flags & 1) {
                raiseDispatchFailure(StatusNoncontinuableException, record);
            }
            callVectoredContinueHandlers(pointers);
            return 1;
        }
    }
    const bool handled = dispatchFrameHandlers(record, context);
    if (handled) callVectoredContinueHandlers(pointers);
    return handled ? 1 : 0;
}

// Capture the caller's continuation, including the ABI nonvolatile registers
// and legacy floating-point state. Extended XSTATE is not advertised here:
// callers supplying only a CONTEXT do not own a CONTEXT_EX buffer.
extern "C" __attribute__((naked)) __declspec(dllexport) void RtlCaptureContext(
    micant::CONTEXT*) noexcept {
    __asm__ __volatile__(
        "movq %rax, 120(%rcx)\n"
        "movq %rcx, 128(%rcx)\n"
        "movq %rdx, 136(%rcx)\n"
        "movq %rbx, 144(%rcx)\n"
        "movq %rbp, 160(%rcx)\n"
        "movq %rsi, 168(%rcx)\n"
        "movq %rdi, 176(%rcx)\n"
        "movq %r8, 184(%rcx)\n"
        "movq %r9, 192(%rcx)\n"
        "movq %r10, 200(%rcx)\n"
        "movq %r11, 208(%rcx)\n"
        "movq %r12, 216(%rcx)\n"
        "movq %r13, 224(%rcx)\n"
        "movq %r14, 232(%rcx)\n"
        "movq %r15, 240(%rcx)\n"
        "leaq 8(%rsp), %rax\n"
        "movq %rax, 152(%rcx)\n"
        "movq (%rsp), %rax\n"
        "movq %rax, 248(%rcx)\n"
        "pushfq\n"
        "popq %rax\n"
        "movl %eax, 68(%rcx)\n"
        "movw %cs, 56(%rcx)\n"
        "movw %ds, 58(%rcx)\n"
        "movw %es, 60(%rcx)\n"
        "movw %fs, 62(%rcx)\n"
        "movw %gs, 64(%rcx)\n"
        "movw %ss, 66(%rcx)\n"
        "fxsave64 256(%rcx)\n"
        "stmxcsr 52(%rcx)\n"
        "movl $0x0010000f, 48(%rcx)\n"
        "retq\n");
}

namespace {
[[noreturn]] void raiseDispatchFailure(uint32_t status, ExceptionRecord64* nested) noexcept {
    alignas(16) micant::CONTEXT context{};
    RtlCaptureContext(&context);
    ExceptionRecord64 record{};
    record.code = status;
    record.flags = 1; // EXCEPTION_NONCONTINUABLE
    record.nestedRecord = nested;
    record.exceptionAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(context.Rip));
    // First chance builds a fresh user exception frame. If it is not handled,
    // KiUserExceptionDispatcher invokes the kernel second-chance path.
    const auto first = NtRaiseException(&record, &context, 1);
    const auto second = NtRaiseException(&record, &context, 0);
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}),
                             second < 0 ? second : first < 0 ? first : static_cast<int32_t>(status));
    __builtin_trap();
}
}

extern "C" __declspec(dllexport) [[noreturn]] void RtlRaiseStatus(int32_t status) noexcept {
    raiseDispatchFailure(static_cast<uint32_t>(status), nullptr);
}

extern "C" __declspec(dllexport) void RtlRaiseException(ExceptionRecord64* record) noexcept {
    if (!record) RtlRaiseStatus(static_cast<int32_t>(0xC000000Du));
    alignas(16) micant::CONTEXT context{};
    // NtContinue resumes immediately after capture. Keep the phase in stack
    // memory so a successfully handled continuable exception returns once
    // instead of raising the same exception again.
    volatile bool dispatched = false;
    RtlCaptureContext(&context);
    if (dispatched) return;
    dispatched = true;
    record->exceptionAddress = __builtin_return_address(0);
    const auto status = NtRaiseException(record, &context, 1);
    RtlRaiseStatus(status < 0 ? status : static_cast<int32_t>(0xC0000144u));
}

extern "C" __declspec(dllexport) [[noreturn]] void RtlRestoreContext(
    micant::CONTEXT* context, ExceptionRecord64* exceptionRecord) noexcept {
    if (!context) {
        raiseDispatchFailure(0xC000000Du, exceptionRecord);
    }

    constexpr uint32_t StatusUnwindConsolidate = 0x80000029u;
    constexpr uint32_t StatusLongJump = 0x80000026u;
    if (exceptionRecord && exceptionRecord->code == StatusUnwindConsolidate) {
        using ConsolidateRoutine = void* (*)(ExceptionRecord64*) noexcept;
        const uintptr_t callbackAddress = exceptionRecord->information[0];
        ImageView64 callbackImage{};
        if (!exceptionRecord->parameterCount || !callbackAddress ||
            !findImage(callbackAddress, callbackImage)) {
            raiseDispatchFailure(0xC0000028u, exceptionRecord);
        }
        const auto continuation = reinterpret_cast<ConsolidateRoutine>(callbackAddress)(exceptionRecord);
        ImageView64 continuationImage{};
        const uintptr_t continuationAddress = reinterpret_cast<uintptr_t>(continuation);
        if (!continuation || !findImage(continuationAddress, continuationImage)) {
            raiseDispatchFailure(0xC0000028u, exceptionRecord);
        }
        context->Rip = continuationAddress;
    } else if (exceptionRecord && exceptionRecord->code == StatusLongJump) {
        struct JumpBuffer64 {
            uint64_t frame, rbx, rsp, rbp, rsi, rdi, r12, r13, r14, r15, rip;
            uint32_t mxcsr;
            uint16_t fpCsr, spare;
            micant::M128A xmm[10];
        };
        static_assert(sizeof(JumpBuffer64) == 256);
        const uintptr_t jumpAddress = exceptionRecord->information[0];
        uintptr_t teb{};
        __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
        if (exceptionRecord->parameterCount < 1 || !jumpAddress || !teb) {
            raiseDispatchFailure(0xC0000028u, exceptionRecord);
        }
        // A jmp_buf can live in writable user storage outside this thread's
        // stack. NtContinue independently validates the restored RSP and RIP.
        constexpr uintptr_t highestUser = 0x00007FFFFFFFFFFFULL;
        if (jumpAddress < 0x10000 || jumpAddress > highestUser - sizeof(JumpBuffer64) + 1) {
            raiseDispatchFailure(0xC0000028u, exceptionRecord);
        }
        JumpBuffer64 jump{};
        memcpy(&jump, reinterpret_cast<const void*>(jumpAddress), sizeof(jump));
        context->Rbx = jump.rbx; context->Rsp = jump.rsp; context->Rbp = jump.rbp;
        context->Rsi = jump.rsi; context->Rdi = jump.rdi;
        context->R12 = jump.r12; context->R13 = jump.r13;
        context->R14 = jump.r14; context->R15 = jump.r15;
        context->Rip = jump.rip;
        context->MxCsr = jump.mxcsr;
        context->FltSave.MxCsr = jump.mxcsr;
        context->FltSave.ControlWord = jump.fpCsr;
        for (size_t i = 0; i < 10; ++i)
            context->FltSave.XmmRegisters[6 + i] = jump.xmm[i];
        context->ContextFlags |= CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT;
    }

    const auto status = NtContinue(context, 0);
    raiseDispatchFailure(status < 0 ? static_cast<uint32_t>(status) : 0xC0000144u,
                         exceptionRecord);
}

__declspec(dllexport) uint64_t RtlGetExtendedFeaturesMask(
    micant::CONTEXT_EX* contextEx) noexcept {
    micant::XSTATE_HEADER* header{};
    uint32_t length{};
    return locateXstateChunk(contextEx, header, length) ? header->Mask & ~uint64_t{3} : 0;
}

__declspec(dllexport) void RtlSetExtendedFeaturesMask(
    micant::CONTEXT_EX* contextEx, uint64_t featureMask) noexcept {
    micant::XSTATE_HEADER* header{};
    uint32_t length{};
    if (!locateXstateChunk(contextEx, header, length)) return;
    const uint64_t enabled = readXcr0() & ~uint64_t{3};
    header->Mask = (header->Mask & 3) | (featureMask & enabled);
}

__declspec(dllexport) void* RtlLocateExtendedFeature(
    micant::CONTEXT_EX* contextEx, uint32_t featureId, uint32_t* featureLength) noexcept {
    if (!contextEx) return nullptr;
    auto* context = reinterpret_cast<micant::CONTEXT*>(contextEx) - 1;
    if (!(context->ContextFlags & CONTEXT_XSTATE)) return nullptr;
    if (featureId == 0) {
        if (featureLength)
            *featureLength = offsetof(micant::XMM_SAVE_AREA32, XmmRegisters);
        return &context->FltSave;
    }
    if (featureId == 1) {
        if (featureLength)
            *featureLength = sizeof(context->FltSave.XmmRegisters);
        return context->FltSave.XmmRegisters;
    }

    micant::XSTATE_HEADER* header{};
    uint32_t xstateLength{};
    if (featureId >= 64 || !locateXstateChunk(contextEx, header, xstateLength) ||
        !(readXcr0() & (uint64_t{1} << featureId))) return nullptr;
    uint32_t size{}, standardOffset{};
    queryXstateFeature(featureId, size, standardOffset);
    if (!size || standardOffset < 512) return nullptr;
    const uint32_t offset = standardOffset - 512;
    if (offset > xstateLength || size > xstateLength - offset) return nullptr;
    if (featureLength) *featureLength = size;
    return reinterpret_cast<uint8_t*>(header) + offset;
}

// Synthetic user APC entry: RSP points at the saved CONTEXT whose first four
// home slots carry NormalContext, SystemArgument1, SystemArgument2 and the
// NormalRoutine. Run only the user callback, then restore the interrupted
// thread's complete CONTEXT/XSTATE through NtContinue.
__attribute__((naked, noreturn)) __declspec(dllexport) void KiUserApcDispatcher() noexcept {
    __asm__ __volatile__(
        "movq %rsp, %r12\n"
        "movq 32(%r12), %r13\n"
        "movq 40(%r12), %r14\n"
        "xorl %r15d, %r15d\n"
        "subq $32, %rsp\n"
        "1:\n"
        "cmpq %r14, %r15\n"
        "jae 2f\n"
        "movq 8(%r13), %rcx\n"
        "movq 16(%r13), %rdx\n"
        "movq 24(%r13), %r8\n"
        "movq %r12, %r9\n"
        "callq *0(%r13)\n"
        "addq $32, %r13\n"
        "incq %r15\n"
        "jmp 1b\n"
        "2:\n"
        "movq %r12, %rcx\n"
        "movl $1, %edx\n"
        "callq NtContinue\n"
        "ud2\n");
}

__declspec(dllexport) [[noreturn]] void RtlUnwindEx(
    void* targetFrame, void* targetIp, ExceptionRecord64* record,
    void* returnValue, micant::CONTEXT* context, void* /*historyTable*/) noexcept {
    const bool exitUnwind = targetFrame == nullptr;
    if (!context || (reinterpret_cast<uintptr_t>(context) & 15) ||
        (!exitUnwind && !targetIp && (!record || record->code != StatusUnwindConsolidate))) {
        raiseDispatchFailure(0xC0000028u, record); // STATUS_BAD_STACK
    }

    HandlerContextStorage capturedContext{};
    RtlCaptureContext(&capturedContext.context);
    // Preserve the public output contract without assuming that a caller's
    // standalone CONTEXT has writable extension bytes after it.
    *context = capturedContext.context;
    const auto xstateStatus = captureUnwindXstate(capturedContext);
    if (xstateStatus < 0) {
        (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), xstateStatus);
        __builtin_trap();
    }
    context = &capturedContext.context;

    ExceptionRecord64 unwindRecord{};
    if (!record) {
        // RtlUnwindEx supplies STATUS_UNWIND when no exception record was
        // provided; the originating control address belongs to the context
        // from which this unwind starts.
        unwindRecord.code = 0xC0000027u;
        unwindRecord.exceptionAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(context->Rip));
        record = &unwindRecord;
    }
    record->flags = 0x2 | (exitUnwind ? 0x4u : 0u); // UNWINDING | EXIT_UNWIND

    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb) raiseDispatchFailure(0xC0000028u, record);
    const uint64_t stackBase = *reinterpret_cast<const uint64_t*>(teb + 8);
    const uint64_t stackLimit = *reinterpret_cast<const uint64_t*>(teb + 16);
    const uintptr_t targetAddress = exitUnwind ? stackBase - 1 : reinterpret_cast<uintptr_t>(targetFrame);
    if (stackLimit >= stackBase || (!exitUnwind &&
        (targetAddress < stackLimit || targetAddress >= stackBase || (targetAddress & 7)))) {
        raiseDispatchFailure(0xC0000028u, record);
    }
    UnwindState64 walk{};
    auto* sourceRegisters = &context->Rax;
    for (size_t i = 0; i < 16; ++i) walk.registers[i] = sourceRegisters[i];
    walk.rip = context->Rip;
    for (size_t i = 0; i < 16; ++i) walk.xmm[i] = context->FltSave.XmmRegisters[i];

    uint32_t unwindFailureStatus = 0xC0000028u; // STATUS_BAD_STACK
    bool completedExitUnwind = false;
    uint64_t lastRsp{};
    uint64_t lastRip{};
    for (size_t frame = 0; frame < 128 && walk.rip &&
         walk.rsp() >= stackLimit && walk.rsp() < stackBase; ++frame) {
        if (frame && (walk.rsp() < lastRsp ||
            (walk.rsp() == lastRsp && walk.rip == lastRip))) break;
        lastRsp = walk.rsp();
        lastRip = walk.rip;
        const uintptr_t controlPc = static_cast<uintptr_t>(walk.rip - (frame && walk.rip ? 1 : 0));
        ImageView64 image{};
        if (!findImage(controlPc, image)) break;
        RuntimeFunction64* function = lookupFunction(image, controlPc);
        const uint64_t currentRip = walk.rip;
        UnwindState64 currentFrame = walk;
        uint64_t establisher{};
        void* languageHandler{};
        void* handlerData{};
        if (!unwindFrame(image, controlPc, walk, function, UnwindFlagTerminationHandler,
                         stackLimit, stackBase,
                         establisher, languageHandler, handlerData)) {
            if (exitUnwind && !walk.rip && walk.rsp() == stackBase)
                completedExitUnwind = true;
            break;
        }
        // No cleanup handler beyond the requested frame may run. This also
        // rejects a malformed unwind entry before invoking its handler.
        if (establisher < stackLimit || establisher > targetAddress || (establisher & 7))
            break;
        bool targetReached = !exitUnwind && establisher == targetAddress;
        if (languageHandler) {
            // Reuse the captured context. No heap allocation remains live
            // across the handler, which may unwind or continue without
            // returning to this frame.
            auto& handlerStorage = capturedContext;
            if (!buildHandlerContext(*context, walk, handlerStorage) ||
                !commitHandlerContext(handlerStorage, *context)) break;
            DispatcherContext64 dispatcher{};
            dispatcher.controlPc = controlPc;
            dispatcher.imageBase = image.base;
            dispatcher.functionEntry = function;
            dispatcher.establisherFrame = establisher;
            dispatcher.targetIp = reinterpret_cast<uintptr_t>(targetIp);
            dispatcher.contextRecord = &handlerStorage.context;
            dispatcher.languageHandler = languageHandler;
            dispatcher.handlerData = handlerData;
            using LanguageHandler = int32_t (*)(ExceptionRecord64*, void*, micant::CONTEXT*, DispatcherContext64*);
            int32_t disposition{};
            size_t collidedRetries{};
            bool collidedPending{};
            for (;;) {
                record->flags &= ~0x60u; // TARGET/COLLIDED flags are per invocation.
                if (targetReached) record->flags |= 0x20u;
                if (collidedPending) record->flags |= 0x40u;
                collidedPending = false;
                disposition = reinterpret_cast<LanguageHandler>(languageHandler)(
                    record, reinterpret_cast<void*>(establisher),
                    &handlerStorage.context, &dispatcher);
                record->flags &= ~0x60u;
                if (disposition != ExceptionCollidedUnwind) break;

                if (++collidedRetries > 32) {
                    unwindFailureStatus = 0xC0000028u; // STATUS_BAD_STACK
                    break;
                }
                const uintptr_t collidedAddress = reinterpret_cast<uintptr_t>(dispatcher.contextRecord);
                const uintptr_t sourceLimit = stackBase;
                if (!dispatcher.contextRecord || (collidedAddress & 15) ||
                    sourceLimit < sizeof(micant::CONTEXT) ||
                    collidedAddress < stackLimit ||
                    collidedAddress > sourceLimit - sizeof(micant::CONTEXT)) {
                    unwindFailureStatus = 0xC0000028u; // STATUS_BAD_STACK
                    break;
                }

                // ReactOS accepts the context selected by the language
                // handler, provided it belongs to this thread's active stack.
                // Copy it into the caller's context, then perform the virtual
                // unwind again before retrying the collided handler.
                const uint64_t previousRsp = walk.rsp();
                const uint64_t previousRip = walk.rip;
                const uintptr_t sourceContextAddress =
                    reinterpret_cast<uintptr_t>(dispatcher.contextRecord);
                const uintptr_t destinationContextAddress = reinterpret_cast<uintptr_t>(context);
                bool xstateRangesValid = true;
                if (dispatcher.contextRecord->ContextFlags & CONTEXT_XSTATE) {
                    constexpr size_t contextWithExtension = sizeof(micant::CONTEXT) +
                                                            sizeof(micant::CONTEXT_EX);
                    if (!(context->ContextFlags & CONTEXT_XSTATE) ||
                        stackBase < contextWithExtension ||
                        sourceLimit < contextWithExtension ||
                        sourceContextAddress > sourceLimit - contextWithExtension ||
                        destinationContextAddress < stackLimit ||
                        destinationContextAddress > stackBase - contextWithExtension) {
                        xstateRangesValid = false;
                    } else {
                        auto* sourceEx = reinterpret_cast<micant::CONTEXT_EX*>(
                            sourceContextAddress + sizeof(micant::CONTEXT));
                        auto* destinationEx = reinterpret_cast<micant::CONTEXT_EX*>(
                            destinationContextAddress + sizeof(micant::CONTEXT));
                        const size_t sourceAllLength = sourceEx->All.Length;
                        const size_t destinationAllLength = destinationEx->All.Length;
                        xstateRangesValid =
                            sourceAllLength >= sizeof(micant::CONTEXT) + sizeof(micant::CONTEXT_EX) &&
                            destinationAllLength >= sizeof(micant::CONTEXT) + sizeof(micant::CONTEXT_EX) &&
                            sourceAllLength <= sourceLimit - sourceContextAddress &&
                            destinationAllLength <= stackBase - destinationContextAddress;
                    }
                }
                if ((reinterpret_cast<uintptr_t>(context) & 15) ||
                    reinterpret_cast<uintptr_t>(context) < stackLimit ||
                    reinterpret_cast<uintptr_t>(context) > stackBase - sizeof(micant::CONTEXT) ||
                    !xstateRangesValid ||
                    !copyContextRecord(dispatcher.contextRecord, context) ||
                    !(context->ContextFlags & 0x00100000u) ||
                    !(context->ContextFlags & CONTEXT_CONTROL) ||
                    context->Rsp < stackLimit || context->Rsp >= stackBase ||
                    !context->Rip) {
                    unwindFailureStatus = 0xC0000028u; // STATUS_BAD_STACK
                    break;
                }
                auto* collidedRegisters = &context->Rax;
                for (size_t i = 0; i < 16; ++i) walk.registers[i] = collidedRegisters[i];
                walk.rip = context->Rip;
                for (size_t i = 0; i < 16; ++i)
                    walk.xmm[i] = context->FltSave.XmmRegisters[i];
                const uintptr_t collidedControlPc = static_cast<uintptr_t>(walk.rip);
                currentFrame = walk;
                ImageView64 collidedImage{};
                if (!findImage(collidedControlPc, collidedImage)) {
                    unwindFailureStatus = 0xC0000028u; // STATUS_BAD_STACK
                    break;
                }
                RuntimeFunction64* collidedFunction = lookupFunction(collidedImage, collidedControlPc);
                uint64_t collidedEstablisher{};
                void* collidedHandler{};
                void* collidedHandlerData{};
                if (!unwindFrame(collidedImage, collidedControlPc, walk, collidedFunction,
                                 UnwindFlagTerminationHandler, stackLimit, stackBase,
                                 collidedEstablisher, collidedHandler, collidedHandlerData) ||
                    collidedEstablisher < stackLimit || collidedEstablisher > targetAddress ||
                    (collidedEstablisher & 7) ||
                    walk.rsp() < stackLimit || walk.rsp() >= stackBase ||
                    (walk.rsp() <= previousRsp && walk.rip == previousRip)) {
                    unwindFailureStatus = 0xC0000028u; // STATUS_BAD_STACK
                    break;
                }
                if (!buildHandlerContext(*context, walk, handlerStorage) ||
                    !commitHandlerContext(handlerStorage, *context)) {
                    unwindFailureStatus = 0xC0000028u; // STATUS_BAD_STACK
                    break;
                }
                establisher = collidedEstablisher;
                targetReached = !exitUnwind && establisher == targetAddress;
                image = collidedImage;
                if (collidedHandler) languageHandler = collidedHandler;
                handlerData = collidedHandlerData;
                dispatcher.controlPc = collidedControlPc;
                dispatcher.imageBase = image.base;
                dispatcher.functionEntry = collidedFunction;
                dispatcher.establisherFrame = establisher;
                dispatcher.handlerData = handlerData;
                dispatcher.languageHandler = languageHandler;
                dispatcher.contextRecord = &handlerStorage.context;
                record->flags |= 0x40u; // Retry this handler with the collided context.
                collidedPending = true;
                if (!collidedHandler) {
                    disposition = ExceptionContinueSearch;
                    break;
                }
            }
            if (disposition == ExceptionCollidedUnwind) break;
            if (disposition != ExceptionContinueSearch) {
                unwindFailureStatus = StatusInvalidDisposition;
                break;
            }
        }
        if (targetReached) record->flags |= 0x20u; // Retain target unwind for the transfer.
        if (targetReached) {
            // Resume the target frame rather than the virtually unwound
            // caller. A frame-register-derived establisher need not be RSP.
            for (size_t i = 0; i < 16; ++i) sourceRegisters[i] = currentFrame.registers[i];
            for (size_t i = 0; i < 16; ++i) context->FltSave.XmmRegisters[i] = currentFrame.xmm[i];
            if (record->code != StatusUnwindConsolidate)
                context->Rip = reinterpret_cast<uintptr_t>(targetIp);
            context->Rax = reinterpret_cast<uintptr_t>(returnValue);
            RtlRestoreContext(context, record);
        }
        if (walk.rsp() <= stackLimit || walk.rsp() >= stackBase) {
            if (exitUnwind && walk.rsp() == stackBase) completedExitUnwind = true;
            break;
        }
    }

    if (exitUnwind && completedExitUnwind) {
        const auto status = NtRaiseException(record, context, 0);
        (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), status);
        __builtin_trap();
    }
    raiseDispatchFailure(unwindFailureStatus, record);
}

extern "C" __declspec(dllexport) void* RtlAddVectoredExceptionHandler(
    unsigned long first, VectoredExceptionHandler handler) noexcept {
    return addVectoredHandler(g_vectoredHandlers, first, handler);
}

extern "C" __declspec(dllexport) unsigned long RtlRemoveVectoredExceptionHandler(void* cookie) noexcept {
    return removeVectoredHandler(g_vectoredHandlers, cookie);
}

extern "C" __declspec(dllexport) void* RtlAddVectoredContinueHandler(
    unsigned long first, VectoredExceptionHandler handler) noexcept {
    return addVectoredHandler(g_vectoredContinueHandlers, first, handler);
}

extern "C" __declspec(dllexport) unsigned long RtlRemoveVectoredContinueHandler(void* cookie) noexcept {
    return removeVectoredHandler(g_vectoredContinueHandlers, cookie);
}

extern "C" __declspec(dllexport) [[noreturn]] void KiUserExceptionDispatcher(
    ExceptionRecord64* record, micant::CONTEXT* context) noexcept {
    if (record && context && RtlDispatchException(record, context))
        RtlRestoreContext(context, nullptr);
    const auto code = record ? record->code : static_cast<uint32_t>(0xC0000005u);
    if (record && context) (void)NtRaiseException(record, context, 0);
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), static_cast<int32_t>(code));
    __builtin_trap();
}

__attribute__((naked)) __declspec(dllexport) int32_t NtResumeThread(void*, uint32_t*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0x52, %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtCreateThreadEx(
    void**, uint32_t, void*, void*, void*, void*, uint32_t, uintptr_t, size_t, size_t, void*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0xbd, %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtQueryInformationProcess(
    void*, uint32_t, void*, uint32_t, uint32_t*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0x19, %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtQueryInformationThread(
    void*, uint32_t, void*, uint32_t, uint32_t*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $" MICANT_STRINGIFY(MICANT_SSN_NT_QUERY_INFORMATION_THREAD)
                         ", %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtDelayExecution(
    uint8_t /*alertable*/, const int64_t* /*delayInterval*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0x34, %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtAlertThread(void* /*threadHandle*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $" MICANT_STRINGIFY(MICANT_SSN_NT_ALERT_THREAD)
                         ", %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtTestAlert() noexcept {
    __asm__ __volatile__("movq $" MICANT_STRINGIFY(MICANT_SSN_NT_TEST_ALERT)
                         ", %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtReadVirtualMemory(
    void*, const void*, void*, size_t, size_t*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0x16c, %rax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtCreateProcessEx(
    void**, uint32_t, void*, void*, uint32_t, void*, void*, void*, uint8_t) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__("movq %rcx, %r10\n\tmovq $0xb4, %rax\n\tsyscall\n\tret\n\t");
#else
    return 0;
#endif
}

// Initial AMD64 thread entry receives the caller's routine and parameter in
// RCX/RDX. Returning from the routine terminates this thread with its result.
__attribute__((naked, noreturn)) __declspec(dllexport) void RtlUserThreadStart(void*, void*) noexcept {
    __asm__ __volatile__(
        "andq $-16, %rsp\nsubq $32, %rsp\nmovq %rcx, %rax\nmovq %rdx, %rcx\n"
        "callq *%rax\nmovl %eax, %edx\nmovq $-2, %rcx\nmovq %rcx, %r10\n"
        "movq $0x53, %rax\nsyscall\nud2\n");
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtShutdownSystem(uint32_t /*action*/) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x0118, %rax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtQuerySystemInformation(
    uint32_t /*infoClass*/,
    void* /*infoBuffer*/,
    uint32_t /*length*/,
    uint32_t* /*returnLength*/
) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x11, %rax\n\t"
        "movq %rdx, %r8\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) int32_t NtAllocateVirtualMemory(
    void* /*processHandle*/,
    void** /*baseAddress*/,
    uint64_t /*zeroBits*/,
    size_t* /*regionSize*/,
    uint32_t /*allocationType*/,
    uint32_t /*protect*/
) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x18, %rax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) int32_t NtFreeVirtualMemory(
    void* /*processHandle*/,
    void** /*baseAddress*/,
    size_t* /*regionSize*/,
    uint32_t /*freeType*/
) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x1E, %rax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) int32_t NtClose(void* /*handle*/) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movl $0x0F, %eax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) int32_t NtCreateEvent(
    void** /*handle*/, uint32_t /*access*/, void* /*attributes*/, uint32_t /*type*/, uint8_t /*initial*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x48, %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtSetEvent(
    void* /*handle*/, int32_t* /*previous*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x4e, %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtWaitForSingleObject(
    void* /*handle*/, uint8_t /*alertable*/, int64_t* /*timeout*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x04, %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtQueueApcThread(
    void* /*threadHandle*/, void* /*apcRoutine*/, void* /*apcContext*/,
    void* /*systemArgument1*/, void* /*systemArgument2*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0xbf, %eax\n\tsyscall\n\tret\n\t");
}

// These service numbers are the MicaNT NTDLL/kernel profile, not a stable
// Windows ABI. The x64 home area and arguments after the fourth stay on the
// caller's stack for the kernel to capture.
__attribute__((naked)) __declspec(dllexport) int32_t NtCreateSection(
    void** /*sectionHandle*/, uint32_t /*desiredAccess*/, void* /*objectAttributes*/,
    int64_t* /*maximumSize*/, uint32_t /*pageProtection*/,
    uint32_t /*allocationAttributes*/, void* /*fileHandle*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x4a, %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtCreateFile(
    void** /*fileHandle*/, uint32_t /*desiredAccess*/, void* /*objectAttributes*/,
    void* /*ioStatusBlock*/, int64_t* /*allocationSize*/, uint32_t /*fileAttributes*/,
    uint32_t /*shareAccess*/, uint32_t /*createDisposition*/, uint32_t /*createOptions*/,
    void* /*eaBuffer*/, uint32_t /*eaLength*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\nmovl $0x55, %eax\nsyscall\nretq\n");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtDeviceIoControlFile(
    void* /*fileHandle*/, void* /*event*/, void* /*apcRoutine*/, void* /*apcContext*/,
    void* /*ioStatusBlock*/, uint32_t /*ioControlCode*/, const void* /*inputBuffer*/,
    uint32_t /*inputBufferLength*/, void* /*outputBuffer*/, uint32_t /*outputBufferLength*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\nmovl $0x07, %eax\nsyscall\nretq\n");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtMapViewOfSection(
    void* /*sectionHandle*/, void* /*processHandle*/, void** /*baseAddress*/,
    uintptr_t /*zeroBits*/, size_t /*commitSize*/, int64_t* /*sectionOffset*/,
    size_t* /*viewSize*/, uint32_t /*inheritDisposition*/, uint32_t /*allocationType*/,
    uint32_t /*win32Protect*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x28, %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtUnmapViewOfSection(
    void* /*processHandle*/, void* /*baseAddress*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x2a, %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtProtectVirtualMemory(
    void*, void**, size_t*, uint32_t, uint32_t*) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x50, %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtFlushInstructionCache(
    void*, const void*, size_t) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $" MICANT_STRINGIFY(MICANT_SSN_NT_FLUSH_INSTRUCTION_CACHE)
                         ", %eax\n\tsyscall\n\tret\n\t");
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtReadFile(
    void* /*fileHandle*/,
    void* /*event*/,
    void* /*apcRoutine*/,
    void* /*apcContext*/,
    void* /*ioStatusBlock*/,
    void* /*buffer*/,
    uint32_t /*length*/,
    void* /*byteOffset*/,
    uint32_t* /*key*/
) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x06, %rax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) int32_t NtSetInformationFile(
    void* /*handle*/, void* /*status*/, void* /*information*/, uint32_t /*length*/, uint32_t /*class*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x27, %eax\n\tsyscall\n\tretq\n");
}
__attribute__((naked)) __declspec(dllexport) int32_t NtCreateIoCompletion(
    void** /*handle*/, uint32_t /*access*/, void* /*attributes*/, uint32_t /*concurrency*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x164, %eax\n\tsyscall\n\tretq\n");
}
__attribute__((naked)) __declspec(dllexport) int32_t NtSetIoCompletion(
    void* /*handle*/, uintptr_t /*key*/, uintptr_t /*context*/, int32_t /*status*/, uintptr_t /*information*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x165, %eax\n\tsyscall\n\tretq\n");
}
__attribute__((naked)) __declspec(dllexport) int32_t NtRemoveIoCompletion(
    void* /*handle*/, uintptr_t* /*key*/, uintptr_t* /*context*/, void* /*status*/, int64_t* /*timeout*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x09, %eax\n\tsyscall\n\tretq\n");
}

__attribute__((naked)) __declspec(dllexport) int32_t NtCancelIoFileEx(
    void* /*fileHandle*/, void* /*requestStatus*/, void* /*ioStatus*/) noexcept {
    __asm__ __volatile__("movq %rcx, %r10\n\tmovl $0x90, %eax\n\tsyscall\n\tretq\n");
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtWriteFile(
    void* /*fileHandle*/,
    void* /*event*/,
    void* /*apcRoutine*/,
    void* /*apcContext*/,
    void* /*ioStatusBlock*/,
    const void* /*buffer*/,
    uint32_t /*length*/,
    void* /*byteOffset*/,
    uint32_t* /*key*/
) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x08, %rax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtIcmpSendEcho(
    uint32_t /*destIp*/,
    const void* /*buffer*/,
    uint32_t /*size*/,
    uint32_t /*timeoutMs*/
) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x26, %rax\n\t"
        "movq %rdx, %r8\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtYieldExecution() noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq $0x46, %rax\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtGdiGetVideoInfo(void* /*videoInfo*/) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x80, %rax\n\t"
        "movq %rdx, %r8\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__attribute__((naked)) __declspec(dllexport) uint64_t NtGdiPresentSurface(const void* /*params*/) noexcept {
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %rcx, %r10\n\t"
        "movq $0x81, %rax\n\t"
        "movq %rdx, %r8\n\t"
        "syscall\n\t"
        "ret\n\t"
    );
#else
    return 0;
#endif
}

__declspec(dllexport) uint64_t NtQueryVirtualMemory() noexcept { return 0; }

// ============================================================================
// 2. RTL Runtime Library Functions
// ============================================================================

struct UnicodeString {
    uint16_t length{0};
    uint16_t maximumLength{0};
    wchar_t* buffer{nullptr};
};

__declspec(dllexport) void RtlInitUnicodeString(UnicodeString* dest, const wchar_t* source) noexcept {
    if (!dest) return;
    dest->buffer = const_cast<wchar_t*>(source);
    if (!source) {
        dest->length = 0;
        dest->maximumLength = 0;
        return;
    }
    uint16_t len = 0;
    while (source[len] != L'\0') len++;
    dest->length = static_cast<uint16_t>(len * sizeof(wchar_t));
    dest->maximumLength = static_cast<uint16_t>((len + 1) * sizeof(wchar_t));
}

__declspec(dllexport) void RtlZeroMemory(void* dest, size_t length) noexcept {
    memset(dest, 0, length);
}

__declspec(dllexport) void RtlCopyMemory(void* dest, const void* src, size_t length) noexcept {
    memcpy(dest, src, length);
}

__declspec(dllexport) void RtlMoveMemory(void* dest, const void* src, size_t length) noexcept {
    memmove(dest, src, length);
}

__declspec(dllexport) bool RtlEqualUnicodeString(const UnicodeString* s1, const UnicodeString* s2, bool caseInSensitive) noexcept {
    if (!s1 || !s2) return s1 == s2;
    if (s1->length != s2->length) return false;
    size_t chars = s1->length / sizeof(wchar_t);
    for (size_t i = 0; i < chars; ++i) {
        wchar_t c1 = s1->buffer[i];
        wchar_t c2 = s2->buffer[i];
        if (caseInSensitive) {
            if (c1 >= L'A' && c1 <= L'Z') c1 += (L'a' - L'A');
            if (c2 >= L'A' && c2 <= L'Z') c2 += (L'a' - L'A');
        }
        if (c1 != c2) return false;
    }
    return true;
}

// Sovereign Static Heap Pool (64 KB)
static uint8_t s_heapStorage[65536];
static size_t s_heapOffset = 0;

__declspec(dllexport) void* RtlAllocateHeap(void* /*heapHandle*/, uint32_t /*flags*/, size_t size) noexcept {
    size_t aligned = (size + 15) & ~15ULL;
    if (s_heapOffset + aligned <= sizeof(s_heapStorage)) {
        void* ptr = &s_heapStorage[s_heapOffset];
        s_heapOffset += aligned;
        return ptr;
    }
    return nullptr;
}

__declspec(dllexport) bool RtlFreeHeap(void* /*heapHandle*/, uint32_t /*flags*/, void* /*baseAddress*/) noexcept {
    return true;
}

// ============================================================================
// 3. Loader Thunks
// ============================================================================

__declspec(dllexport) void LdrInitializeThunk() noexcept {}
__declspec(dllexport) void LdrLoadDll() noexcept {}
__declspec(dllexport) void LdrGetProcedureAddress() noexcept {}

} // extern "C"
