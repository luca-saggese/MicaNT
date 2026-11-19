/**
 * @file winnt_ucrtbase_runtime_abi.cpp
 * @brief Horizon 116 (Kernel Unit #88): Sovereign WinNT Universal C Runtime (ucrtbase.dll)
 *        Official PE32+ Exports, OnExit Table Dispatch, C Stdio Streams, Heap Allocator,
 *        and C-Runtime Startup Sequences for Standard Windows PE32+ Executables.
 */

#if defined(MICANT_FREESTANDING_UCRT)

#include "micant/ucrt/bounded_arena.hpp"
#include "micant/ucrt/ceilf.hpp"
#include "micant/ucrt/onexit_table.hpp"
#include <cstdarg>
#include <climits>
#include <cstdint>
#include "micant/shared/native_process_abi.hpp"

extern "C" {
int32_t __stdcall MicantUcrtAllocateVirtualMemory(
    void** baseAddress, size_t* regionSize, uint32_t allocationType,
    uint32_t protection) noexcept;
int32_t __stdcall MicantUcrtFreeVirtualMemory(
    void** baseAddress, size_t* regionSize, uint32_t freeType) noexcept;
__declspec(dllimport) int32_t __stdcall NtWriteFile(
    void* file, void* event, void* apcRoutine, void* apcContext,
    void* ioStatus, const void* buffer, uint32_t length,
    void* byteOffset, uint32_t* key) noexcept;
__declspec(dllimport) int32_t __stdcall NtReadFile(
    void* file, void* event, void* apcRoutine, void* apcContext,
    void* ioStatus, void* buffer, uint32_t length,
    void* byteOffset, uint32_t* key) noexcept;
__declspec(dllimport) int32_t __stdcall NtQuerySystemTime(int64_t* systemTime) noexcept;
}

namespace {

class NativeVirtualHeap final {
    struct Header {
        uintptr_t cookie;
        void* baseAddress;
        size_t regionSize;
        size_t requestedSize;
        size_t alignment;
    };
    static constexpr uintptr_t Cookie = static_cast<uintptr_t>(0x4D6963614E54414Cull);
    static constexpr uint32_t MemCommit = 0x00001000u;
    static constexpr uint32_t MemReserve = 0x00002000u;
    static constexpr uint32_t MemRelease = 0x00008000u;
    static constexpr uint32_t PageReadWrite = 0x04u;
    static constexpr int32_t StatusSuccess = 0;

    static void zeroBytes(void* destination, size_t length) noexcept {
        auto* bytes = static_cast<uint8_t*>(destination);
        for (size_t i = 0; i < length; ++i) bytes[i] = 0;
    }
    static void copyBytes(void* destination, const void* source, size_t length) noexcept {
        auto* output = static_cast<uint8_t*>(destination);
        const auto* input = static_cast<const uint8_t*>(source);
        for (size_t i = 0; i < length; ++i) output[i] = input[i];
    }

    [[nodiscard]] static uintptr_t makeCookie(const void* user, const Header& header) noexcept {
        return Cookie ^ reinterpret_cast<uintptr_t>(user) ^
            reinterpret_cast<uintptr_t>(header.baseAddress) ^ header.regionSize ^
            header.requestedSize ^ header.alignment;
    }
    [[nodiscard]] static Header* findHeader(void* pointer) noexcept {
        if (!pointer) return nullptr;
        auto* header = reinterpret_cast<Header*>(static_cast<uint8_t*>(pointer) - sizeof(Header));
        if (header->cookie != makeCookie(pointer, *header) || !header->baseAddress ||
            !header->regionSize || header->requestedSize > header->regionSize ||
            !header->alignment || (header->alignment & (header->alignment - 1)) != 0 ||
            (reinterpret_cast<uintptr_t>(pointer) & (header->alignment - 1)) != 0 ||
            reinterpret_cast<uintptr_t>(pointer) < reinterpret_cast<uintptr_t>(header->baseAddress) + sizeof(Header) ||
            header->requestedSize > reinterpret_cast<uintptr_t>(header->baseAddress) + header->regionSize -
                reinterpret_cast<uintptr_t>(pointer)) return nullptr;
        return header;
    }

public:
    [[nodiscard]] void* allocate(size_t size,
                                 size_t alignment = alignof(std::max_align_t)) noexcept {
        if (!size) size = 1;
        if (!alignment || (alignment & (alignment - 1)) != 0) return nullptr;
        if (alignment < alignof(std::max_align_t)) alignment = alignof(std::max_align_t);
        if (size > SIZE_MAX - sizeof(Header) - (alignment - 1)) return nullptr;
        size_t regionSize = size + sizeof(Header) + alignment - 1;
        void* baseAddress{};
        const int32_t status = MicantUcrtAllocateVirtualMemory(
            &baseAddress, &regionSize, MemReserve | MemCommit, PageReadWrite);
        if (status != StatusSuccess || !baseAddress) return nullptr;
        const uintptr_t minimum = reinterpret_cast<uintptr_t>(baseAddress) + sizeof(Header);
        const uintptr_t userAddress = (minimum + alignment - 1) & ~static_cast<uintptr_t>(alignment - 1);
        auto* header = reinterpret_cast<Header*>(userAddress - sizeof(Header));
        header->baseAddress = baseAddress;
        header->regionSize = regionSize;
        header->requestedSize = size;
        header->alignment = alignment;
        header->cookie = makeCookie(reinterpret_cast<void*>(userAddress), *header);
        return reinterpret_cast<void*>(userAddress);
    }

    [[nodiscard]] void* allocateZeroed(size_t count, size_t size) noexcept {
        if (size && count > SIZE_MAX / size) return nullptr;
        const size_t bytes = count * size;
        auto* memory = static_cast<uint8_t*>(allocate(bytes ? bytes : 1));
        if (memory) zeroBytes(memory, bytes ? bytes : 1);
        return memory;
    }

    void release(void* pointer) noexcept {
        auto* header = findHeader(pointer);
        if (!header) return;
        void* baseAddress = header->baseAddress;
        size_t regionSize{};
        header->cookie = 0;
        (void)MicantUcrtFreeVirtualMemory(&baseAddress, &regionSize, MemRelease);
    }

    [[nodiscard]] void* reallocate(void* pointer, size_t size) noexcept {
        if (!pointer) return size ? allocate(size) : nullptr;
        if (!size) {
            release(pointer);
            return nullptr;
        }
        auto* header = findHeader(pointer);
        if (!header) return nullptr;
        void* replacement = allocate(size, header->alignment);
        if (!replacement) return nullptr;
        copyBytes(replacement, pointer,
            header->requestedSize < size ? header->requestedSize : size);
        release(pointer);
        return replacement;
    }
};

NativeVirtualHeap g_nativeHeap{};

}

extern "C" {

__declspec(dllimport) int32_t __stdcall NtYieldExecution() noexcept;
__declspec(dllimport) int32_t __stdcall NtTerminateProcess(void* process, int32_t status) noexcept;

struct NativeTlsDirectory64 {
    const void* startAddressOfRawData;
    const void* endAddressOfRawData;
    unsigned long* addressOfIndex;
    void** addressOfCallbacks;
    unsigned long sizeOfZeroFill;
    unsigned long characteristics;
};

static_assert(sizeof(NativeTlsDirectory64) == 40);

unsigned long _tls_index = 0;
extern unsigned char __tls_start;
extern unsigned char __tls_end;
extern thread_local int g_nativeErrno
    __attribute__((section(".tls$M")));
unsigned char __tls_start
    __attribute__((section(".tls$A"), used)) = 0;
thread_local int g_nativeErrno
    __attribute__((section(".tls$M"))) = 0;
unsigned char __tls_end
    __attribute__((section(".tls$Z"), used)) = 0;
extern const NativeTlsDirectory64 _tls_used
    __attribute__((section(".rdata$T"), used)) = {
        &__tls_start,
        &__tls_end,
        &_tls_index,
        nullptr,
        0,
        0
    };

}

namespace micant::ucrt::winnt_ucrtbase_abi {

void* Ucrt_malloc(std::size_t size) noexcept {
    return detail::allocateWithErrno(g_nativeHeap, size, g_nativeErrno);
}

void* Ucrt_calloc(std::size_t count, std::size_t size) noexcept {
    return detail::allocateZeroedWithErrno(g_nativeHeap, count, size,
                                           g_nativeErrno);
}

void* Ucrt_realloc(void* block, std::size_t size) noexcept {
    return detail::reallocateWithErrno(g_nativeHeap, block, size,
                                       g_nativeErrno);
}

void Ucrt_free(void* block) noexcept {
    g_nativeHeap.release(block);
}

void* Ucrt_aligned_malloc(std::size_t size, std::size_t alignment) noexcept {
    return micant::ucrt::detail::allocateAlignedWithErrno(
        g_nativeHeap, size, alignment, g_nativeErrno);
}

void Ucrt_aligned_free(void* block) noexcept {
    g_nativeHeap.release(block);
}

}

static_assert(sizeof(wchar_t) == 2);
static_assert(alignof(std::max_align_t) >= 16);

extern "C" int __stdcall DllMainCRTStartup(
    void*, unsigned long, void*) noexcept {
    return 1;
}

namespace micant::ucrt::winnt_ucrtbase_abi {

using _onexit_t = micant::ucrt::OnExitRoutine;
using _onexit_table_t = micant::ucrt::OnExitTable;

int32_t Ucrt_initialize_onexit_table(_onexit_table_t* table) noexcept {
    return micant::ucrt::initializeOnExitTable(table);
}

int32_t Ucrt_register_onexit_function(
    _onexit_table_t* table, _onexit_t function) noexcept {
    return micant::ucrt::registerOnExitFunction(table, function, g_nativeHeap);
}

int32_t Ucrt_execute_onexit_table(_onexit_table_t* table) noexcept {
    return micant::ucrt::executeOnExitTable(table, g_nativeHeap);
}

void Ucrt_initterm(void (**first)(void), void (**last)(void)) noexcept {
    micant::ucrt::initializeFunctionTable(first, last);
}

int32_t Ucrt_initterm_e(
    int32_t (**first)(void), int32_t (**last)(void)) noexcept {
    return micant::ucrt::initializeFunctionTableWithStatus(first, last);
}

bool encodeUtf16ToUtf8(const char16_t* source, size_t units, char* output,
                       size_t capacity, size_t& bytes) noexcept {
    bytes = 0;
    for (size_t i = 0; i < units; ++i) {
        uint32_t cp = source[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 >= units || source[i + 1] < 0xDC00 || source[i + 1] > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (source[++i] - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) return false;
        const size_t need = cp <= 0x7F ? 1 : cp <= 0x7FF ? 2 : cp <= 0xFFFF ? 3 : 4;
        if (bytes > capacity || need > capacity - bytes) return false;
        if (need == 1) output[bytes++] = static_cast<char>(cp);
        else if (need == 2) {
            output[bytes++] = static_cast<char>(0xC0 | (cp >> 6));
            output[bytes++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (need == 3) {
            output[bytes++] = static_cast<char>(0xE0 | (cp >> 12));
            output[bytes++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            output[bytes++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            output[bytes++] = static_cast<char>(0xF0 | (cp >> 18));
            output[bytes++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            output[bytes++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            output[bytes++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return true;
}

}

extern "C" {

// The UCRT startup objects access these process-global modes through the
// pointer accessors below. Match the documented defaults: no-commit streams
// and text mode for newly opened files.
static int g_nativeCommode = 0;
static int g_nativeFmode = 0x4000;
static int g_nativeNewMode = 0;
static thread_local int g_nativeThreadLocaleMode = 2;
using NativeInvalidParameterHandler = void (__cdecl *)(
    const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
static NativeInvalidParameterHandler g_nativeInvalidParameterHandler{};
static int (__cdecl *g_nativeMathErrorHandler)(void*){};
static int g_nativeArgc{};
static char* g_nativeArgv[257]{};
static char* g_nativeAcmdln{};
static char** g_nativeEnvironment{};
static wchar_t** g_nativeWEnvironment{};
static bool g_nativeWEnvironmentInitialized{};
static int g_nativeArgvMode{};
static bool g_nativeArgvInitialized{};
static int g_nativeWArgc{};
static wchar_t* g_nativeWArgv[257]{};
static wchar_t* g_nativeWcmdln{};
static int g_nativeWArgvMode{};
static bool g_nativeWArgvInitialized{};
static bool g_nativeEnvironmentInitialized{};
static micant::ucrt::OnExitTable g_nativeOnExitTable{};
static bool g_nativeOnExitInitialized{};
static int g_nativeApplicationType{};

static int32_t initializeNarrowArgv() noexcept {
    if (g_nativeArgvInitialized) return 0;
    if (g_nativeArgvMode == 2) {
        g_nativeArgc = 0;
        g_nativeArgv[0] = nullptr;
        g_nativeArgvInitialized = true;
        return 0;
    }
    micant::shared::native_process::UnicodeString64 commandLine{};
    if (!micant::shared::native_process::currentCommandLine(commandLine)) {
        if (!micant::shared::native_process::currentImagePath(commandLine)) return 22;
    }
    const size_t units = commandLine.Length / sizeof(char16_t);
    if (units > (SIZE_MAX - 1) / 3) return 7;
    const size_t capacity = units * 3 + 1;
    auto* raw = static_cast<char*>(micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(capacity));
    auto* parsed = static_cast<char*>(micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(capacity));
    if (!raw || !parsed) {
        if (raw) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(raw);
        if (parsed) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(parsed);
        return 12;
    }
    size_t bytes{};
    if (!micant::ucrt::winnt_ucrtbase_abi::encodeUtf16ToUtf8(
            commandLine.Buffer, units, raw, capacity - 1, bytes)) {
        micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(raw);
        micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(parsed);
        return 42;
    }
    raw[bytes] = '\0';
    for (size_t i = 0; i <= bytes; ++i) parsed[i] = raw[i];
    g_nativeAcmdln = raw;
    char* read = parsed;
    char* write = parsed;
    while (*read) {
        while (*read == ' ' || *read == '\t') ++read;
        if (!*read) break;
        if (g_nativeArgc == 256) {
            micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(parsed);
            g_nativeAcmdln = nullptr;
            return 7;
        }
        g_nativeArgv[g_nativeArgc++] = write;
        bool quoted = false;
        bool wildcard = false;
        while (*read) {
            if ((*read == ' ' || *read == '\t') && !quoted) break;
            size_t slashes{};
            while (*read == '\\') { ++slashes; ++read; }
            if (*read == '"') {
                for (size_t i = 0; i < slashes / 2; ++i) *write++ = '\\';
                if (slashes & 1) {
                    *write++ = '"';
                    ++read;
                } else {
                    ++read;
                    if (quoted && *read == '"') { *write++ = '"'; ++read; }
                    else quoted = !quoted;
                }
                continue;
            }
            while (slashes--) *write++ = '\\';
            if (!*read || ((*read == ' ' || *read == '\t') && !quoted)) break;
            if (*read == '*' || *read == '?') wildcard = true;
            *write++ = *read++;
        }
        // Wildcard expansion is only requested in mode 1. Preserve the
        // documented literal argument when no filesystem match is available;
        // matching paths are expanded by the command processor before launch.
        if (wildcard && g_nativeArgvMode == 1) {
            g_nativeErrno = 38;
            return 38;
        }
        *write++ = '\0';
        while (*read == ' ' || *read == '\t') ++read;
    }
    g_nativeArgv[g_nativeArgc] = nullptr;
    g_nativeArgvInitialized = true;
    return 0;
}

static int32_t initializeWideArgv() noexcept {
    if (g_nativeWArgvInitialized) return 0;
    if (g_nativeWArgvMode == 2) {
        g_nativeWArgc = 0;
        g_nativeWArgv[0] = nullptr;
        g_nativeWArgvInitialized = true;
        return 0;
    }
    micant::shared::native_process::UnicodeString64 commandLine{};
    if (!micant::shared::native_process::currentCommandLine(commandLine) &&
        !micant::shared::native_process::currentImagePath(commandLine)) return 22;
    if ((commandLine.Length & 1u) || commandLine.Length > 65534u) return 7;
    const size_t units = commandLine.Length / sizeof(char16_t);
    auto* raw = static_cast<wchar_t*>(micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(
        (units + 1) * sizeof(wchar_t)));
    auto* parsed = static_cast<wchar_t*>(micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(
        (units + 1) * sizeof(wchar_t)));
    if (!raw || !parsed) {
        if (raw) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(raw);
        if (parsed) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(parsed);
        return 12;
    }
    for (size_t i = 0; i < units; ++i) raw[i] = static_cast<wchar_t>(commandLine.Buffer[i]);
    raw[units] = L'\0';
    for (size_t i = 0; i <= units; ++i) parsed[i] = raw[i];
    g_nativeWcmdln = raw;
    wchar_t* read = parsed;
    wchar_t* write = parsed;
    while (*read) {
        while (*read == L' ' || *read == L'\t') ++read;
        if (!*read) break;
        if (g_nativeWArgc == 256) {
            micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(parsed);
            g_nativeWcmdln = nullptr;
            return 7;
        }
        g_nativeWArgv[g_nativeWArgc++] = write;
        bool quoted = false;
        while (*read) {
            if ((*read == L' ' || *read == L'\t') && !quoted) break;
            size_t slashes{};
            while (*read == L'\\') { ++slashes; ++read; }
            if (*read == L'"') {
                for (size_t i = 0; i < slashes / 2; ++i) *write++ = L'\\';
                if (slashes & 1) { *write++ = L'"'; ++read; }
                else {
                    ++read;
                    if (quoted && *read == L'"') { *write++ = L'"'; ++read; }
                    else quoted = !quoted;
                }
                continue;
            }
            while (slashes--) *write++ = L'\\';
            if (!*read || ((*read == L' ' || *read == L'\t') && !quoted)) break;
            *write++ = *read++;
        }
        *write++ = L'\0';
        while (*read == L' ' || *read == L'\t') ++read;
    }
    g_nativeWArgv[g_nativeWArgc] = nullptr;
    g_nativeWArgvInitialized = true;
    return 0;
}

__declspec(dllexport) int* __cdecl _errno() {
    return &g_nativeErrno;
}
__declspec(dllexport) int64_t __cdecl _time64(int64_t* output) noexcept {
    int64_t systemTime{};
    if (NtQuerySystemTime(&systemTime) < 0 || systemTime < 116444736000000000ll) {
        g_nativeErrno = 22;
        return -1;
    }
    const int64_t unixTime = (systemTime - 116444736000000000ll) / 10000000ll;
    if (output) *output = unixTime;
    return unixTime;
}
static uint32_t g_nativeRandSeed = 1;
__declspec(dllexport) int __cdecl rand(void) noexcept {
    g_nativeRandSeed = g_nativeRandSeed * 214013u + 2531011u;
    return static_cast<int>((g_nativeRandSeed >> 16) & 0x7fffu);
}
__declspec(dllexport) void __cdecl srand(unsigned int seed) noexcept {
    g_nativeRandSeed = seed;
}

__declspec(dllexport) int* __cdecl __p__commode() {
    return &g_nativeCommode;
}

__declspec(dllexport) int* __cdecl __p__fmode() {
    return &g_nativeFmode;
}

__declspec(dllexport) int* __cdecl __p___argc() {
    if (initializeNarrowArgv() != 0) return nullptr;
    return &g_nativeArgc;
}

__declspec(dllexport) char*** __cdecl __p___argv() {
    if (initializeNarrowArgv() != 0) return nullptr;
    static char** argv = g_nativeArgv;
    return &argv;
}

__declspec(dllexport) char** __cdecl __p__acmdln() {
    if (initializeNarrowArgv() != 0) return nullptr;
    return &g_nativeAcmdln;
}

__declspec(dllexport) int* __cdecl __p___wargc() {
    return initializeWideArgv() == 0 ? &g_nativeWArgc : nullptr;
}
__declspec(dllexport) wchar_t*** __cdecl __p___wargv() {
    if (initializeWideArgv() != 0) return nullptr;
    static wchar_t** argv = g_nativeWArgv;
    return &argv;
}
__declspec(dllexport) wchar_t** __cdecl __p__wcmdln() {
    return initializeWideArgv() == 0 ? &g_nativeWcmdln : nullptr;
}

static int32_t initializeWideEnvironment() noexcept {
    if (g_nativeWEnvironmentInitialized) return 0;
    char16_t* source{};
    if (!micant::shared::native_process::currentEnvironment(source)) {
        g_nativeErrno = 22; return 22;
    }
    size_t units{}, entries{}; bool ended{};
    while (units < 32768) {
        if (!source[units] && units + 1 < 32768 && !source[units + 1]) {
            units += 2; ended = true; break;
        }
        if (!source[units]) ++entries;
        ++units;
    }
    if (!ended || entries > SIZE_MAX / sizeof(wchar_t*) - 1 ||
        units > SIZE_MAX / sizeof(wchar_t)) {
        g_nativeErrno = ended ? 12 : 7; return g_nativeErrno;
    }
    auto** environment = static_cast<wchar_t**>(
        micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc((entries + 1) * sizeof(wchar_t*)));
    auto* text = static_cast<wchar_t*>(
        micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(units * sizeof(wchar_t)));
    if (!environment || !text) {
        if (environment) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(environment);
        if (text) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(text);
        g_nativeErrno = 12; return 12;
    }
    size_t entry{}, offset{};
    for (size_t i = 0; i < units; ++i) {
        text[i] = static_cast<wchar_t>(source[i]);
        if (i == offset && i < units - 1 && source[i]) environment[entry++] = text + i;
        if (!source[i]) offset = i + 1;
    }
    environment[entry] = nullptr;
    g_nativeWEnvironment = environment;
    g_nativeWEnvironmentInitialized = true;
    return 0;
}
__declspec(dllexport) wchar_t*** __cdecl __p__wenviron() {
    if (initializeWideEnvironment() != 0) return nullptr;
    static wchar_t** environment = g_nativeWEnvironment;
    return &environment;
}

static int32_t initializeNarrowEnvironment() noexcept {
    if (g_nativeEnvironmentInitialized) return 0;
    char16_t* source{};
    if (!micant::shared::native_process::currentEnvironment(source)) {
        g_nativeErrno = 22;
        return 22;
    }
    size_t units{};
    size_t entries{};
    bool ended{};
    while (units < 32768) {
        if (source[units] == 0 && units + 1 < 32768 && source[units + 1] == 0) {
            units += 2;
            ended = true;
            break;
        }
        if (source[units] == 0) ++entries;
        ++units;
    }
    if (!ended || units > (SIZE_MAX - 1) / 3 || entries > SIZE_MAX / sizeof(char*) - 1) {
        g_nativeErrno = ended ? 12 : 7;
        return g_nativeErrno;
    }
    auto** environment = static_cast<char**>(micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(
        (entries + 1) * sizeof(char*)));
    auto* text = static_cast<char*>(micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(units * 3 + 1));
    if (!environment || !text) {
        if (environment) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(environment);
        if (text) micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(text);
        g_nativeErrno = 12;
        return 12;
    }
    size_t sourceOffset{};
    size_t textOffset{};
    size_t index{};
    while (sourceOffset < units && source[sourceOffset]) {
        size_t end = sourceOffset;
        while (end < units && source[end]) ++end;
        environment[index++] = text + textOffset;
        size_t converted{};
        if (!micant::ucrt::winnt_ucrtbase_abi::encodeUtf16ToUtf8(
                source + sourceOffset, end - sourceOffset, text + textOffset,
                units * 3 + 1 - textOffset, converted)) {
            micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(environment);
            micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(text);
            g_nativeErrno = 42;
            return 42;
        }
        textOffset += converted;
        text[textOffset++] = '\0';
        sourceOffset = end + 1;
    }
    environment[index] = nullptr;
    g_nativeEnvironment = environment;
    g_nativeEnvironmentInitialized = true;
    return 0;
}

__declspec(dllexport) char*** __cdecl __p__environ() {
    (void)initializeNarrowEnvironment();
    return &g_nativeEnvironment;
}

__declspec(dllexport) wchar_t* __cdecl wcschr(const wchar_t* string, wchar_t ch) {
    if (!string) return nullptr;
    for (;;) {
        if (*string == ch) return const_cast<wchar_t*>(string);
        if (*string == L'\0') return nullptr;
        ++string;
    }
}

__declspec(dllexport) int __cdecl wcscmp(const wchar_t* left, const wchar_t* right) {
    if (!left || !right) return left ? 1 : right ? -1 : 0;
    while (*left && *left == *right) { ++left; ++right; }
    return static_cast<int>(*left) - static_cast<int>(*right);
}

__declspec(dllexport) int __cdecl _wcsicmp(const wchar_t* left, const wchar_t* right) {
    if (!left || !right) return left ? 1 : right ? -1 : 0;
    for (;;) {
        uint32_t a = static_cast<uint32_t>(*left++);
        uint32_t b = static_cast<uint32_t>(*right++);
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if ((a >= 0x00C0 && a <= 0x00D6) || (a >= 0x00D8 && a <= 0x00DE)) a += 0x20;
        if ((b >= 0x00C0 && b <= 0x00D6) || (b >= 0x00D8 && b <= 0x00DE)) b += 0x20;
        if (a != b || a == 0) return static_cast<int>(a) - static_cast<int>(b);
    }
}

__declspec(dllexport) char* __cdecl _strdup(const char* source) {
    if (!source) { g_nativeErrno = 22; return nullptr; }
    size_t length{};
    while (source[length]) ++length;
    if (length == SIZE_MAX) { g_nativeErrno = 12; return nullptr; }
    auto* copy = static_cast<char*>(micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(length + 1));
    if (!copy) return nullptr;
    for (size_t i = 0; i <= length; ++i) copy[i] = source[i];
    return copy;
}

__declspec(dllexport) char* __cdecl _strupr(char* string) {
    if (!string) { g_nativeErrno = 22; return nullptr; }
    for (char* cursor = string; *cursor; ++cursor)
        if (*cursor >= 'a' && *cursor <= 'z') *cursor = static_cast<char>(*cursor - ('a' - 'A'));
    return string;
}

__declspec(dllexport) unsigned int __cdecl ___lc_codepage_func() {
    return 65001;
}

__declspec(dllexport) int __cdecl ___mb_cur_max_func() {
    return 4;
}

__declspec(dllexport) void* __cdecl malloc(std::size_t size) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(size);
}

__declspec(dllexport) void* __cdecl calloc(std::size_t count,
                                           std::size_t size) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_calloc(count, size);
}

__declspec(dllexport) void* __cdecl realloc(void* block,
                                            std::size_t size) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_realloc(block, size);
}

__declspec(dllexport) void __cdecl free(void* block) {
    micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(block);
}

__declspec(dllexport) void* __cdecl _aligned_malloc(
    std::size_t size, std::size_t alignment) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_aligned_malloc(
        size, alignment);
}

__declspec(dllexport) void __cdecl _aligned_free(void* block) {
    micant::ucrt::winnt_ucrtbase_abi::Ucrt_aligned_free(block);
}

__declspec(dllexport) void* __cdecl memcpy(
    void* destination, const void* source, std::size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = in[i];
    }
    return destination;
}

__declspec(dllexport) void* __cdecl memmove(
    void* destination, const void* source, std::size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    const auto outAddress = reinterpret_cast<std::uintptr_t>(out);
    const auto inAddress = reinterpret_cast<std::uintptr_t>(in);
    if (outAddress < inAddress || outAddress - inAddress >= count) {
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = in[i];
        }
    } else {
        for (std::size_t i = count; i != 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }
    return destination;
}

__declspec(dllexport) void* __cdecl memset(
    void* destination, int value, std::size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    const auto byte = static_cast<unsigned char>(value);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = byte;
    }
    return destination;
}

__declspec(dllexport) int __cdecl memcmp(
    const void* left, const void* right, std::size_t count) {
    const auto* a = static_cast<const unsigned char*>(left);
    const auto* b = static_cast<const unsigned char*>(right);
    for (std::size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) {
            return static_cast<int>(a[i]) - static_cast<int>(b[i]);
        }
    }
    return 0;
}

__declspec(dllexport) void* __cdecl memchr(
    const void* buffer, int value, std::size_t count) {
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    const auto target = static_cast<unsigned char>(value);
    for (std::size_t i = 0; i < count; ++i) {
        if (bytes[i] == target) {
            return const_cast<unsigned char*>(bytes + i);
        }
    }
    return nullptr;
}

__declspec(dllexport) std::size_t __cdecl strlen(const char* string) {
    const char* end = string;
    while (*end != '\0') {
        ++end;
    }
    return static_cast<std::size_t>(end - string);
}

__declspec(dllexport) int __cdecl strcmp(
    const char* left, const char* right) {
    const auto* a = reinterpret_cast<const unsigned char*>(left);
    const auto* b = reinterpret_cast<const unsigned char*>(right);
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return static_cast<int>(*a) - static_cast<int>(*b);
}

__declspec(dllexport) int __cdecl strncmp(
    const char* left, const char* right, std::size_t count) {
    const auto* a = reinterpret_cast<const unsigned char*>(left);
    const auto* b = reinterpret_cast<const unsigned char*>(right);
    for (std::size_t i = 0; i < count; ++i) {
        if (a[i] != b[i] || a[i] == 0) {
            return static_cast<int>(a[i]) - static_cast<int>(b[i]);
        }
    }
    return 0;
}

__declspec(dllexport) std::size_t __cdecl strnlen(
    const char* string, std::size_t count) {
    std::size_t length = 0;
    while (length < count && string[length] != '\0') {
        ++length;
    }
    return length;
}

__declspec(dllexport) std::size_t __cdecl wcslen(
    const wchar_t* string) {
    const wchar_t* end = string;
    while (*end != L'\0') {
        ++end;
    }
    return static_cast<std::size_t>(end - string);
}

__declspec(dllexport) std::size_t __cdecl wcsnlen(
    const wchar_t* string, std::size_t count) {
    std::size_t length = 0;
    while (length < count && string[length] != L'\0') {
        ++length;
    }
    return length;
}

__declspec(dllexport) float __cdecl ceilf(float value) {
    return micant::ucrt::ceilf(value);
}

__declspec(dllexport) int32_t _initialize_onexit_table(
    micant::ucrt::winnt_ucrtbase_abi::_onexit_table_t* table) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_initialize_onexit_table(table);
}

__declspec(dllexport) int32_t _register_onexit_function(
    micant::ucrt::winnt_ucrtbase_abi::_onexit_table_t* table,
    micant::ucrt::winnt_ucrtbase_abi::_onexit_t function) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_register_onexit_function(
        table, function);
}

__declspec(dllexport) int32_t _execute_onexit_table(
    micant::ucrt::winnt_ucrtbase_abi::_onexit_table_t* table) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_execute_onexit_table(table);
}

__declspec(dllexport) void _initterm(void (**first)(void), void (**last)(void)) {
    micant::ucrt::winnt_ucrtbase_abi::Ucrt_initterm(first, last);
}

__declspec(dllexport) int32_t _initterm_e(
    int32_t (**first)(void), int32_t (**last)(void)) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_initterm_e(first, last);
}

struct NativeCrtStream {
    uint32_t descriptor;
    uint32_t mode;
    int32_t locked;
    uint32_t reserved;
    char* buffer{};
    size_t bufferSize{};
    size_t buffered{};
    bool ownsBuffer{};
    uint32_t bufferingMode{};
    int32_t pendingByte{-1};
    uint32_t pendingWide{UINT32_MAX};
};
static NativeCrtStream g_nativeStreams[3]{{0, 0x4000, 0, 0},
                                          {1, 0x4000, 0, 0},
                                          {2, 0x4000, 0, 0}};
struct NativeIoStatusBlock { uintptr_t status; uintptr_t information; };
struct NativeLocaleInfo {
    char* decimalPoint; char* thousandsSeparator; char* grouping;
    char* internationalCurrency; char* currency; char* monetaryDecimalPoint;
    char* monetaryThousandsSeparator; char* monetaryGrouping;
    char* positiveSign; char* negativeSign;
    char internationalFractionDigits; char fractionDigits;
    char positiveCurrencyPrecedes; char positiveSpaceSeparation;
    char negativeCurrencyPrecedes; char negativeSpaceSeparation;
    char positiveSignPosition; char negativeSignPosition;
};
struct NativeMultibyteState { uint32_t value; uint8_t remaining; uint8_t total; uint16_t reserved; };
static char g_decimalPoint[] = ".";
static char g_emptyString[] = "";
static NativeLocaleInfo g_nativeLocale{g_decimalPoint, g_emptyString, g_emptyString,
    g_emptyString, g_emptyString, g_emptyString, g_emptyString, g_emptyString,
    g_emptyString, g_emptyString, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX};

static NativeCrtStream* nativeStream(void* stream) noexcept {
    for (auto& candidate : g_nativeStreams)
        if (stream == &candidate) return &candidate;
    return nullptr;
}

static void nativeWriteBytes(NativeCrtStream& stream, const char* bytes,
                             size_t length, size_t& written) noexcept {
    written = 0;
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb) { g_nativeErrno = 9; return; }
    const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    if (!peb) { g_nativeErrno = 9; return; }
    const uintptr_t parameters = *reinterpret_cast<const uintptr_t*>(peb + 0x20);
    if (parameters < 0x10000 || parameters > 0x00007FFFFFFF0000ULL - 0x38) {
        g_nativeErrno = 9; return;
    }
    const size_t handleOffset = stream.descriptor == 0 ? 0x20 :
        stream.descriptor == 1 ? 0x28 : 0x30;
    void* handle = *reinterpret_cast<void* const*>(parameters + handleOffset);
    if (!handle || handle == reinterpret_cast<void*>(~uintptr_t{0}) || length > UINT32_MAX) {
        g_nativeErrno = 9; return;
    }
    NativeIoStatusBlock io{};
    const int32_t status = NtWriteFile(handle, nullptr, nullptr, nullptr, &io,
        bytes, static_cast<uint32_t>(length), nullptr, nullptr);
    if (status < 0 || io.status != 0) { g_nativeErrno = 5; return; }
    written = static_cast<size_t>(io.information);
}

static int32_t nativeReadByte(NativeCrtStream& stream) noexcept {
    if (stream.pendingByte >= 0) {
        const int32_t value = stream.pendingByte;
        stream.pendingByte = -1;
        return value;
    }
    uintptr_t teb{};
    __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb || teb > UINTPTR_MAX - 0x60) { g_nativeErrno = 9; return -1; }
    const auto* peb = *reinterpret_cast<const micant::shared::native_process::PebPrefix64* const*>(teb + 0x60);
    if (!peb || !peb->ProcessParameters) { g_nativeErrno = 9; return -1; }
    const uintptr_t rawHandle = peb->ProcessParameters->StandardInput;
    void* handle = reinterpret_cast<void*>(rawHandle);
    if (!handle || handle == reinterpret_cast<void*>(~uintptr_t{0})) {
        g_nativeErrno = 9;
        return -1;
    }
    unsigned char byte{};
    NativeIoStatusBlock io{};
    const int32_t status = NtReadFile(handle, nullptr, nullptr, nullptr, &io,
                                      &byte, 1, nullptr, nullptr);
    if (status < 0 || static_cast<intptr_t>(io.status) < 0) {
        g_nativeErrno = 5;
        return -1;
    }
    if (io.information == 0) return -1;
    return byte;
}

static int nativeFlushStream(NativeCrtStream& stream) noexcept {
    if (stream.buffered == 0) return 0;
    size_t written{};
    nativeWriteBytes(stream, stream.buffer, stream.buffered, written);
    if (written > stream.buffered) { g_nativeErrno = 5; return -1; }
    if (written != stream.buffered) {
        const size_t remaining = stream.buffered - written;
        for (size_t i = 0; i < remaining; ++i) stream.buffer[i] = stream.buffer[written + i];
        stream.buffered = remaining;
        g_nativeErrno = 5;
        return -1;
    }
    stream.buffered = 0;
    return 0;
}

static bool appendFormatted(char* output, size_t capacity, size_t& length,
                            char value) noexcept {
    if (length >= capacity) return false;
    output[length++] = value;
    return true;
}

static bool appendUnsigned(char* output, size_t capacity, size_t& length,
                           uint64_t value, unsigned radix, bool upper,
                           unsigned width, char pad, bool prefix) noexcept {
    char digits[32]{};
    size_t count{};
    const char* alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { digits[count++] = alphabet[value % radix]; value /= radix; } while (value);
    const unsigned prefixLength = prefix ? 2 : 0;
    while (count + prefixLength < width)
        if (!appendFormatted(output, capacity, length, pad)) return false;
    if (prefix) {
        if (!appendFormatted(output, capacity, length, '0') ||
            !appendFormatted(output, capacity, length, upper ? 'X' : 'x')) return false;
    }
    while (count) if (!appendFormatted(output, capacity, length, digits[--count])) return false;
    return true;
}

static bool formatFixedMagnitude(char* output, size_t capacity, size_t& length,
                                double value, unsigned precision,
                                bool alternate) noexcept {
    if (precision > 18) return false;
    uint64_t scale = 1;
    for (unsigned i = 0; i < precision; ++i) scale *= 10;
    if (value > static_cast<double>(UINT64_MAX / scale)) return false;
    const uint64_t rounded = static_cast<uint64_t>(value * static_cast<double>(scale) + 0.5);
    const uint64_t whole = rounded / scale;
    const uint64_t fraction = rounded % scale;
    if (!appendUnsigned(output, capacity, length, whole, 10, false, 0, ' ', false)) return false;
    if (precision || alternate) {
        if (!appendFormatted(output, capacity, length, '.')) return false;
    }
    if (precision) {
        uint64_t divisor = scale / 10;
        for (unsigned i = 0; i < precision; ++i) {
            const unsigned digit = static_cast<unsigned>((fraction / divisor) % 10);
            if (!appendFormatted(output, capacity, length, static_cast<char>('0' + digit))) return false;
            divisor /= 10;
        }
    }
    return true;
}

static bool formatScientificMagnitude(char* output, size_t capacity, size_t& length,
                                      double value, unsigned precision,
                                      bool alternate, bool upper) noexcept {
    if (precision > 18) return false;
    int exponent{};
    double mantissa = value;
    if (mantissa != 0.0) {
        while (mantissa >= 10.0) { mantissa *= 0.1; ++exponent; }
        while (mantissa < 1.0) { mantissa *= 10.0; --exponent; }
    }
    uint64_t scale = 1;
    for (unsigned i = 0; i < precision; ++i) scale *= 10;
    uint64_t rounded = static_cast<uint64_t>(mantissa * static_cast<double>(scale) + 0.5);
    if (rounded >= 10 * scale) { rounded /= 10; ++exponent; }
    if (!appendFormatted(output, capacity, length, static_cast<char>('0' + rounded / scale))) return false;
    const uint64_t fraction = rounded % scale;
    if (precision || alternate)
        if (!appendFormatted(output, capacity, length, '.')) return false;
    if (precision) {
        uint64_t divisor = scale / 10;
        for (unsigned i = 0; i < precision; ++i) {
            const unsigned digit = static_cast<unsigned>((fraction / divisor) % 10);
            if (!appendFormatted(output, capacity, length, static_cast<char>('0' + digit))) return false;
            divisor /= 10;
        }
    }
    if (!appendFormatted(output, capacity, length, upper ? 'E' : 'e') ||
        !appendFormatted(output, capacity, length, exponent < 0 ? '-' : '+')) return false;
    unsigned magnitude = static_cast<unsigned>(exponent < 0 ? -exponent : exponent);
    if (magnitude < 10 && !appendFormatted(output, capacity, length, '0')) return false;
    return appendUnsigned(output, capacity, length, magnitude, 10, false, 0, ' ', false);
}

static bool formatFloating(char* output, size_t capacity, size_t& length,
                           double value, char conversion, int precision,
                           bool alternate) noexcept {
    union { double value; uint64_t bits; } representation{value};
    const bool negative = (representation.bits >> 63) != 0;
    const uint64_t exponentBits = (representation.bits >> 52) & 0x7FF;
    const uint64_t fractionBits = representation.bits & 0x000FFFFFFFFFFFFFULL;
    const bool upper = conversion == 'E' || conversion == 'F' || conversion == 'G';
    if (negative && !appendFormatted(output, capacity, length, '-')) return false;
    if (exponentBits == 0x7FF) {
        const char* token = fractionBits ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        for (size_t i = 0; token[i]; ++i)
            if (!appendFormatted(output, capacity, length, token[i])) return false;
        return true;
    }
    if (!negative && (conversion == 'f' || conversion == 'F' || conversion == 'e' ||
                      conversion == 'E' || conversion == 'g' || conversion == 'G')) {
        /* Sign flags are inserted by the caller, before this formatter. */
    }
    const double magnitude = negative ? -value : value;
    unsigned digits = precision < 0 ? 6u : static_cast<unsigned>(precision);
    if (conversion == 'f' || conversion == 'F')
        return formatFixedMagnitude(output, capacity, length, magnitude, digits, alternate);
    if (conversion == 'e' || conversion == 'E')
        return formatScientificMagnitude(output, capacity, length, magnitude, digits, alternate, upper);
    if (digits == 0) digits = 1;
    int exponent{};
    double normalized = magnitude;
    if (normalized != 0.0) {
        while (normalized >= 10.0) { normalized *= 0.1; ++exponent; }
        while (normalized < 1.0) { normalized *= 10.0; --exponent; }
    }
    if (exponent < -4 || exponent >= static_cast<int>(digits)) {
        if (!formatScientificMagnitude(output, capacity, length, magnitude,
                                       digits - 1, alternate, upper)) return false;
    } else {
        const int fractional = static_cast<int>(digits) - exponent - 1;
        if (!formatFixedMagnitude(output, capacity, length, magnitude,
                                  fractional > 0 ? static_cast<unsigned>(fractional) : 0u,
                                  alternate)) return false;
    }
    if (!alternate) {
        size_t end = length;
        size_t exponentAt = length;
        for (size_t i = 0; i < length; ++i)
            if (output[i] == 'e' || output[i] == 'E') { exponentAt = i; break; }
        while (end > 0 && end - 1 > exponentAt && output[end - 1] == '0') --end;
        if (end > 0 && end - 1 > exponentAt && output[end - 1] == '.') --end;
        if (end < length) {
            size_t write = end;
            for (size_t read = exponentAt; exponentAt < length && read < length; ++read)
                output[write++] = output[read];
            length = write;
        }
    }
    return true;
}

static int32_t formatNative(char* output, size_t capacity, const char* format,
                            va_list arguments) noexcept {
    if (!output || !format) { g_nativeErrno = 22; return -1; }
    size_t length{};
    for (size_t i = 0; format[i]; ++i) {
        if (format[i] != '%') {
            if (!appendFormatted(output, capacity, length, format[i])) { g_nativeErrno = 34; return -1; }
            continue;
        }
        ++i;
        if (format[i] == '%') {
            if (!appendFormatted(output, capacity, length, '%')) { g_nativeErrno = 34; return -1; }
            continue;
        }
        bool left{};
        bool zero{};
        bool positiveSign{};
        bool spaceSign{};
        bool alternate{};
        while (format[i] == '-' || format[i] == '0' || format[i] == '+' || format[i] == ' ' || format[i] == '#') {
            if (format[i] == '-') left = true;
            if (format[i] == '0') zero = true;
            if (format[i] == '+') positiveSign = true;
            if (format[i] == ' ') spaceSign = true;
            if (format[i] == '#') alternate = true;
            ++i;
        }
        unsigned width{};
        if (format[i] == '*') {
            const int requested = va_arg(arguments, int);
            if (requested < 0) { left = true; width = static_cast<unsigned>(-requested); }
            else width = static_cast<unsigned>(requested);
            ++i;
        } else {
            while (format[i] >= '0' && format[i] <= '9') {
                if (width > 255) { g_nativeErrno = 22; return -1; }
                width = width * 10 + static_cast<unsigned>(format[i++] - '0');
            }
        }
        int precision = -1;
        if (format[i] == '.') {
            precision = 0; ++i;
            if (format[i] == '*') {
                precision = va_arg(arguments, int);
                if (precision < 0) precision = -1;
                ++i;
            } else {
                while (format[i] >= '0' && format[i] <= '9') {
                    if (precision > 255) { g_nativeErrno = 22; return -1; }
                    precision = precision * 10 + (format[i++] - '0');
                }
            }
        }
        enum class Length { Default, Long, LongLong, Size } lengthKind{};
        if (format[i] == 'h') { ++i; if (format[i] == 'h') ++i; }
        else if (format[i] == 'l') { ++i; lengthKind = Length::Long; if (format[i] == 'l') { ++i; lengthKind = Length::LongLong; } }
        else if (format[i] == 'z') { ++i; lengthKind = Length::Size; }
        else if (format[i] == 'I' && format[i + 1] == '6' && format[i + 2] == '4') { i += 3; lengthKind = Length::LongLong; }
        const char conversion = format[i];
        char temp[320]{};
        size_t tempLength{};
        if (conversion == 's') {
            const size_t maxLength = precision >= 0 ? static_cast<size_t>(precision) : 256;
            if (lengthKind == Length::Long) {
                const wchar_t* string = va_arg(arguments, const wchar_t*);
                if (!string) string = L"(null)";
                size_t units{};
                while (string[units] && units < 256) ++units;
                if (!micant::ucrt::winnt_ucrtbase_abi::encodeUtf16ToUtf8(
                        reinterpret_cast<const char16_t*>(string), units,
                        temp, sizeof(temp), tempLength)) return -1;
                if (tempLength > maxLength) tempLength = maxLength;
            } else {
                const char* string = va_arg(arguments, const char*);
                if (!string) string = "(null)";
                while (tempLength < maxLength && string[tempLength])
                    temp[tempLength] = string[tempLength], ++tempLength;
            }
        } else if (conversion == 'c') {
            temp[tempLength++] = static_cast<char>(va_arg(arguments, int));
        } else if (conversion == 'p') {
            const uintptr_t pointer = reinterpret_cast<uintptr_t>(va_arg(arguments, void*));
            if (!appendUnsigned(temp, sizeof(temp), tempLength, pointer, 16, false, 0, '0', true)) return -1;
        } else if (conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'x' || conversion == 'X' || conversion == 'o') {
            const bool signedValue = conversion == 'd' || conversion == 'i';
            uint64_t value{};
            bool negative{};
            if (signedValue) {
                int64_t signedValueRaw{};
                if (lengthKind == Length::LongLong || lengthKind == Length::Size) signedValueRaw = va_arg(arguments, int64_t);
                else if (lengthKind == Length::Long) signedValueRaw = va_arg(arguments, long);
                else signedValueRaw = va_arg(arguments, int);
                negative = signedValueRaw < 0;
                value = negative ? static_cast<uint64_t>(-(signedValueRaw + 1)) + 1 : static_cast<uint64_t>(signedValueRaw);
            } else {
                if (lengthKind == Length::LongLong || lengthKind == Length::Size) value = va_arg(arguments, uint64_t);
                else if (lengthKind == Length::Long) value = va_arg(arguments, unsigned long);
                else value = va_arg(arguments, unsigned int);
            }
            unsigned radix = conversion == 'o' ? 8 : (conversion == 'x' || conversion == 'X' ? 16 : 10);
            if (negative) temp[tempLength++] = '-';
            else if (signedValue && positiveSign) temp[tempLength++] = '+';
            else if (signedValue && spaceSign) temp[tempLength++] = ' ';
            if (!appendUnsigned(temp, sizeof(temp), tempLength, value, radix, conversion == 'X',
                                0, ' ', alternate || conversion == 'p')) return -1;
        } else if (conversion == 'f' || conversion == 'F' || conversion == 'e' ||
                   conversion == 'E' || conversion == 'g' || conversion == 'G') {
            const double value = va_arg(arguments, double);
            if (value >= 0.0 && positiveSign) temp[tempLength++] = '+';
            else if (value >= 0.0 && spaceSign) temp[tempLength++] = ' ';
            if (!formatFloating(temp, sizeof(temp), tempLength, value,
                                conversion, precision, alternate)) {
                g_nativeErrno = 34;
                return -1;
            }
        } else {
            g_nativeErrno = 22;
            return -1;
        }
        const size_t padding = tempLength < width ? width - tempLength : 0;
        if (!left && length + padding > capacity) { g_nativeErrno = 34; return -1; }
        if (!left && padding) {
            const bool hasSign = tempLength && (temp[0] == '-' || temp[0] == '+' || temp[0] == ' ');
            if (zero && hasSign) {
                output[length++] = temp[0];
                for (size_t p = 0; p < padding; ++p) output[length++] = '0';
                for (size_t p = 1; p < tempLength; ++p) output[length++] = temp[p];
                continue;
            }
            const char fill = zero && conversion != 's' && conversion != 'c' ? '0' : ' ';
            for (size_t p = 0; p < padding; ++p) output[length++] = fill;
        }
        if (length + tempLength > capacity) { g_nativeErrno = 34; return -1; }
        for (size_t p = 0; p < tempLength; ++p) output[length++] = temp[p];
        if (left && padding) {
            if (length + padding > capacity) { g_nativeErrno = 34; return -1; }
            for (size_t p = 0; p < padding; ++p) output[length++] = ' ';
        }
    }
    return static_cast<int32_t>(length);
}

static int32_t formatWide(wchar_t* output, size_t capacity, const wchar_t* format,
                          va_list arguments) noexcept {
    if (!output || !capacity || !format) { g_nativeErrno = 22; return -1; }
    char narrowFormat[1024]{};
    size_t formatLength{};
    bool inConversion = false;
    while (format[formatLength]) {
        const uint32_t value = static_cast<uint32_t>(format[formatLength]);
        if (value > 0x7F || formatLength + 1 >= sizeof(narrowFormat)) {
            output[0] = L'\0';
            g_nativeErrno = value > 0x7F ? 42 : 22;
            return -1;
        }
        char converted = static_cast<char>(value);
        if (value == '%') {
            if (inConversion) inConversion = false;
            else inConversion = true;
        } else if (inConversion) {
            const bool modifier = (value >= '0' && value <= '9') || value == '-' ||
                value == '+' || value == ' ' || value == '#' || value == '0' ||
                value == '.' || value == '*' || value == 'I' || value == 'h' ||
                value == 'l' || value == 'j' || value == 'z' || value == 't' ||
                value == 'L';
            if (!modifier) {
                // Microsoft wide printf's %C consumes a narrow char and
                // widens it. The shared formatter emits that byte, then this
                // routine widens each result byte into the UTF-16 output.
                if (value == 'C') converted = 'c';
                inConversion = false;
            }
        }
        narrowFormat[formatLength] = converted;
        ++formatLength;
    }
    constexpr size_t OutputCapacity = 8192;
    auto* narrowOutput = static_cast<char*>(g_nativeHeap.allocate(OutputCapacity, alignof(char)));
    if (!narrowOutput) { output[0] = L'\0'; g_nativeErrno = 12; return -1; }
    va_list copy;
    va_copy(copy, arguments);
    const int32_t result = formatNative(narrowOutput, OutputCapacity - 1,
                                        narrowFormat, copy);
    va_end(copy);
    if (result < 0 || static_cast<size_t>(result) >= capacity) {
        output[0] = L'\0';
        if (result >= 0) g_nativeErrno = 34;
        g_nativeHeap.release(narrowOutput);
        return -1;
    }
    for (int32_t i = 0; i < result; ++i)
        output[i] = static_cast<unsigned char>(narrowOutput[i]);
    output[result] = L'\0';
    g_nativeHeap.release(narrowOutput);
    return result;
}

static double ucrtExp(double value) noexcept;
static double ucrtDoubleFromBits(uint64_t bits) noexcept;

static int32_t scanWide(const wchar_t* input, const wchar_t* format,
                        va_list arguments) noexcept {
    if (!input || !format) { g_nativeErrno = 22; return -1; }
    int32_t assigned{};
    while (*format) {
        if (*format <= L' ' && *format != L'%') {
            while (*input && *input <= L' ') ++input;
            while (*format && *format <= L' ') ++format;
            continue;
        }
        if (*format != L'%') {
            if (*input++ != *format++) return assigned;
            continue;
        }
        ++format;
        if (*format == L'%') {
            if (*input++ != L'%') return assigned;
            ++format;
            continue;
        }
        while (*input && *input <= L' ') ++input;
        size_t width{};
        while (*format >= L'0' && *format <= L'9')
            width = width * 10 + static_cast<size_t>(*format++ - L'0');
        bool longValue{};
        if (*format == L'l' || *format == L'I') {
            longValue = true;
            if (*format == L'I' && format[1] == L'6' && format[2] == L'4') format += 3;
            else ++format;
        } else if (*format == L'h') ++format;
        const wchar_t conversion = *format++;
        if (conversion == L's') {
            wchar_t* destination = va_arg(arguments, wchar_t*);
            if (!destination) { g_nativeErrno = 22; return assigned; }
            size_t copied{};
            while (*input && *input > L' ' && (!width || copied < width))
                destination[copied++] = *input++;
            if (!copied) return assigned;
            destination[copied] = L'\0';
            ++assigned;
        } else if (conversion == L'c') {
            wchar_t* destination = va_arg(arguments, wchar_t*);
            const size_t count = width ? width : 1;
            for (size_t i = 0; i < count; ++i) {
                if (!input[i]) return assigned;
                destination[i] = input[i];
            }
            input += count;
            ++assigned;
        } else if (conversion == L'd' || conversion == L'i' || conversion == L'u' ||
                   conversion == L'x' || conversion == L'X' || conversion == L'o') {
            bool negative{};
            if (*input == L'+' || *input == L'-') negative = *input++ == L'-';
            unsigned radix = conversion == L'o' ? 8u :
                conversion == L'x' || conversion == L'X' ? 16u : 10u;
            uint64_t value{};
            size_t digits{};
            while (*input) {
                unsigned digit;
                if (*input >= L'0' && *input <= L'9') digit = static_cast<unsigned>(*input - L'0');
                else if (*input >= L'a' && *input <= L'f') digit = static_cast<unsigned>(*input - L'a' + 10);
                else if (*input >= L'A' && *input <= L'F') digit = static_cast<unsigned>(*input - L'A' + 10);
                else break;
                if (digit >= radix || (width && digits >= width)) break;
                if (value > (UINT64_MAX - digit) / radix) { g_nativeErrno = 34; return assigned; }
                value = value * radix + digit;
                ++digits;
                ++input;
            }
            if (!digits) return assigned;
            if (conversion == L'd' || conversion == L'i') {
                if (longValue) *va_arg(arguments, int64_t*) = negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
                else *va_arg(arguments, int*) = negative ? -static_cast<int>(value) : static_cast<int>(value);
            } else if (longValue) *va_arg(arguments, uint64_t*) = value;
            else *va_arg(arguments, unsigned int*) = static_cast<unsigned int>(value);
            ++assigned;
        } else if (conversion == L'a' || conversion == L'A' || conversion == L'e' ||
                   conversion == L'E' || conversion == L'f' || conversion == L'F' ||
                   conversion == L'g' || conversion == L'G') {
            const wchar_t* cursor = input;
            size_t consumed{};
            bool negative{};
            if (*cursor == L'+' || *cursor == L'-') {
                negative = *cursor == L'-'; ++cursor; ++consumed;
            }
            double value{};
            size_t digits{};
            while (*cursor >= L'0' && *cursor <= L'9' && (!width || consumed < width)) {
                value = value * 10.0 + static_cast<double>(*cursor - L'0');
                ++cursor; ++consumed; ++digits;
            }
            int32_t decimalExponent{};
            if (*cursor == L'.' && (!width || consumed < width)) {
                ++cursor; ++consumed;
                while (*cursor >= L'0' && *cursor <= L'9' && (!width || consumed < width)) {
                    value = value * 10.0 + static_cast<double>(*cursor - L'0');
                    --decimalExponent;
                    ++cursor; ++consumed; ++digits;
                }
            }
            int32_t explicitExponent{};
            if ((*cursor == L'e' || *cursor == L'E') && (!width || consumed < width)) {
                const wchar_t* exponentStart = cursor;
                ++cursor; ++consumed;
                bool exponentNegative{};
                if (*cursor == L'+' || *cursor == L'-') {
                    exponentNegative = *cursor == L'-'; ++cursor; ++consumed;
                }
                size_t exponentDigits{};
                while (*cursor >= L'0' && *cursor <= L'9' &&
                       (!width || consumed < width)) {
                    if (explicitExponent < 10000)
                        explicitExponent = explicitExponent * 10 + (*cursor - L'0');
                    ++cursor; ++consumed; ++exponentDigits;
                }
                if (!exponentDigits) {
                    cursor = exponentStart;
                    while (consumed > static_cast<size_t>(cursor - input)) --consumed;
                } else if (exponentNegative) explicitExponent = -explicitExponent;
            }
            if (!digits) return assigned;
            const int32_t totalExponent = decimalExponent + explicitExponent;
            if (totalExponent > 308) value = ucrtDoubleFromBits(0x7FF0000000000000ULL);
            else if (totalExponent < -324) value = 0.0;
            else if (totalExponent != 0)
                value *= ucrtExp(static_cast<double>(totalExponent) * 2.302585092994045684);
            if (negative) value = -value;
            if (longValue) *va_arg(arguments, double*) = value;
            else *va_arg(arguments, float*) = static_cast<float>(value);
            input = cursor;
            ++assigned;
        } else {
            g_nativeErrno = 22;
            return assigned;
        }
    }
    return assigned;
}

static uint64_t ucrtDoubleBits(double value) noexcept {
    union { double value; uint64_t bits; } representation{value};
    return representation.bits;
}
static double ucrtDoubleFromBits(uint64_t bits) noexcept {
    union { double value; uint64_t bits; } representation{};
    representation.bits = bits;
    return representation.value;
}
static bool ucrtFinite(double value) noexcept {
    return (ucrtDoubleBits(value) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}
static bool ucrtNan(double value) noexcept {
    const uint64_t bits = ucrtDoubleBits(value);
    return (bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL &&
           (bits & 0x000FFFFFFFFFFFFFULL) != 0;
}
static double ucrtAtan(double value) noexcept {
    double result;
    __asm__ __volatile__("fldl %1; fld1; fpatan; fstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
static double ucrtAtan2(double y, double x) noexcept {
    double result;
    __asm__ __volatile__("fldl %1; fldl %2; fpatan; fstpl %0" : "=m"(result) : "m"(x), "m"(y) : "st");
    return result;
}
static double ucrtSqrt(double value) noexcept {
    double result;
    __asm__ __volatile__("fldl %1; fsqrt; fstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
static double ucrtSin(double value) noexcept {
    double result;
    __asm__ __volatile__("fldl %1; fsin; fstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
static double ucrtCos(double value) noexcept {
    double result;
    __asm__ __volatile__("fldl %1; fcos; fstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
static double ucrtExp(double value) noexcept {
    if (value > 709.782712893384) return ucrtDoubleFromBits(0x7FF0000000000000ULL);
    if (value < -745.133219101941) return 0.0;
    double result;
    __asm__ __volatile__(
        "fldl %1; fldl2e; fmulp %%st, %%st(1); fld %%st(0); frndint; "
        "fxch %%st(1); fsub %%st(1), %%st(0); f2xm1; fld1; faddp; "
        "fscale; fstp %%st(1); fstpl %0"
        : "=m"(result) : "m"(value) : "st");
    return result;
}
static double ucrtLog(double value) noexcept {
    double result;
    __asm__ __volatile__("fldln2; fldl %1; fyl2x; fstpl %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
static double ucrtFmod(double x, double y) noexcept {
    if (!ucrtFinite(x) || !ucrtFinite(y) || y == 0.0)
        return ucrtDoubleFromBits(0x7FF8000000000000ULL);
    double result;
    __asm__ __volatile__(
        "fldl %1; fldl %2; 1: fprem; fnstsw %%ax; test $0x0400, %%ah; "
        "jnz 1b; fstp %%st(1); fstpl %0"
        : "=m"(result) : "m"(y), "m"(x) : "ax", "st");
    return result;
}
static double ucrtModf(double value, double* integral) noexcept {
    const uint64_t bits = ucrtDoubleBits(value);
    const unsigned exponent = static_cast<unsigned>((bits >> 52) & 0x7FF);
    if (exponent == 0x7FF) {
        if (integral) *integral = value;
        return ucrtDoubleFromBits(bits & 0x8000000000000000ULL);
    }
    if (exponent < 1023) {
        if (integral) *integral = ucrtDoubleFromBits(bits & 0x8000000000000000ULL);
        return value;
    }
    if (exponent >= 1075) {
        if (integral) *integral = value;
        return ucrtDoubleFromBits(bits & 0x8000000000000000ULL);
    }
    const unsigned fractionalBits = 1075 - exponent;
    const uint64_t mask = (uint64_t{1} << fractionalBits) - 1;
    const double whole = ucrtDoubleFromBits(bits & ~mask);
    if (integral) *integral = whole;
    return value - whole;
}
static double ucrtPow(double x, double y) noexcept {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return y > 0.0 ? x : ucrtDoubleFromBits(0x7FF0000000000000ULL);
    if (x < 0.0) {
        double integer{};
        (void)ucrtModf(y, &integer);
        if (integer != y) { g_nativeErrno = 33; return ucrtDoubleFromBits(0x7FF8000000000000ULL); }
        const double magnitude = ucrtExp(y * ucrtLog(-x));
        return ucrtFmod(y, 2.0) == 0.0 ? magnitude : -magnitude;
    }
    return ucrtExp(y * ucrtLog(x));
}

__declspec(dllexport) void* __cdecl __acrt_iob_func(unsigned index) {
    return index < 3 ? &g_nativeStreams[index] : nullptr;
}

__declspec(dllexport) int __cdecl _fileno(void* stream) {
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return -1; }
    return static_cast<int>(value->descriptor);
}

__declspec(dllexport) int __cdecl _setmode(int descriptor, int mode) {
    if (descriptor < 0 || descriptor >= 3 || (mode != 0x4000 && mode != 0x8000)) {
        g_nativeErrno = descriptor < 0 || descriptor >= 3 ? 9 : 22;
        return -1;
    }
    const int old = static_cast<int>(g_nativeStreams[descriptor].mode);
    g_nativeStreams[descriptor].mode = static_cast<uint32_t>(mode);
    return old;
}

__declspec(dllexport) void __cdecl _lock_file(void* stream) {
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return; }
    for (;;) {
        int32_t expected{};
        if (__atomic_compare_exchange_n(&value->locked, &expected, 1, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
        (void)NtYieldExecution();
    }
}

__declspec(dllexport) void __cdecl _unlock_file(void* stream) {
    if (auto* value = nativeStream(stream)) __atomic_store_n(&value->locked, 0, __ATOMIC_RELEASE);
    else g_nativeErrno = 9;
}

__declspec(dllexport) int __cdecl fflush(void* stream) {
    if (!stream) {
        for (auto& value : g_nativeStreams)
            if (nativeFlushStream(value) != 0) return -1;
        return 0;
    }
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return -1; }
    return nativeFlushStream(*value);
}

__declspec(dllexport) int __cdecl fputc(int character, void* stream) {
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return -1; }
    _lock_file(stream);
    char bytes[2]{};
    size_t count = 1;
    bytes[0] = static_cast<char>(character);
    if (bytes[0] == '\n' && value->mode == 0x4000) {
        bytes[0] = '\r';
        bytes[1] = '\n';
        count = 2;
    }
    bool failed = false;
    if (value->buffer && value->bufferSize) {
        for (size_t i = 0; i < count; ++i) {
            if (value->buffered == value->bufferSize && nativeFlushStream(*value) != 0) {
                failed = true;
                break;
            }
            value->buffer[value->buffered++] = bytes[i];
        }
        if (!failed && value->bufferingMode == 1 && bytes[count - 1] == '\n')
            failed = nativeFlushStream(*value) != 0;
    } else {
        size_t written{};
        nativeWriteBytes(*value, bytes, count, written);
        failed = written != count;
    }
    _unlock_file(stream);
    return failed ? -1 : (unsigned char)character;
}

__declspec(dllexport) int __cdecl __stdio_common_vfprintf(
    uint64_t /*options*/, void* stream, const char* format, void* /*locale*/, va_list arguments) {
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return -1; }
    char output[2048]{};
    va_list copy;
    va_copy(copy, arguments);
    const int32_t length = formatNative(output, sizeof(output), format, copy);
    va_end(copy);
    if (length < 0) return -1;
    _lock_file(stream);
    size_t written{};
    nativeWriteBytes(*value, output, static_cast<size_t>(length), written);
    _unlock_file(stream);
    return written == static_cast<size_t>(length) ? length : -1;
}

__declspec(dllexport) int __cdecl __stdio_common_vsprintf(
    uint64_t, char* buffer, size_t capacity, const char* format, void*, va_list arguments) {
    if (!buffer || !capacity || !format) { g_nativeErrno = 22; return -1; }
    const int32_t result = formatNative(buffer, capacity - 1, format, arguments);
    if (result < 0) { buffer[0] = '\0'; return -1; }
    buffer[result] = '\0';
    return result;
}

__declspec(dllexport) int __cdecl __stdio_common_vswprintf(
    uint64_t, wchar_t* buffer, size_t capacity, const wchar_t* format, void*, va_list arguments) {
    return formatWide(buffer, capacity, format, arguments);
}

__declspec(dllexport) int __cdecl __stdio_common_vswscanf(
    uint64_t, const wchar_t* input, size_t, const wchar_t* format, void*, va_list arguments) {
    return scanWide(input, format, arguments);
}

__declspec(dllexport) int __cdecl getc(void* stream) {
    auto* value = nativeStream(stream);
    if (!value || value->descriptor != 0) { g_nativeErrno = 9; return -1; }
    const int32_t byte = nativeReadByte(*value);
    if (byte < 0) return -1;
    if (value->mode == 0x4000 && byte == '\r') {
        const int32_t following = nativeReadByte(*value);
        if (following == '\n') return '\n';
        if (following >= 0) value->pendingByte = following;
    }
    return byte;
}

__declspec(dllexport) unsigned int __cdecl fgetwc(void* stream) {
    auto* value = nativeStream(stream);
    if (!value || value->descriptor != 0) { g_nativeErrno = 9; return UINT32_MAX; }
    if (value->pendingWide != UINT32_MAX) {
        const unsigned int pending = value->pendingWide;
        value->pendingWide = UINT32_MAX;
        return pending;
    }
    const int first = getc(stream);
    if (first < 0) return UINT32_MAX;
    uint32_t cp{}; unsigned remaining{};
    if (first < 0x80) cp = static_cast<uint32_t>(first);
    else if (first >= 0xC2 && first <= 0xDF) { cp = first & 0x1F; remaining = 1; }
    else if (first >= 0xE0 && first <= 0xEF) { cp = first & 0x0F; remaining = 2; }
    else if (first >= 0xF0 && first <= 0xF4) { cp = first & 7; remaining = 3; }
    else { g_nativeErrno = 42; return UINT32_MAX; }
    for (unsigned i = 0; i < remaining; ++i) {
        const int next = getc(stream);
        if (next < 0 || (next & 0xC0) != 0x80) { g_nativeErrno = 42; return UINT32_MAX; }
        cp = (cp << 6) | static_cast<uint32_t>(next & 0x3F);
    }
    if ((remaining == 1 && cp < 0x80) || (remaining == 2 && cp < 0x800) ||
        (remaining == 3 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF)) { g_nativeErrno = 42; return UINT32_MAX; }
    if (cp <= 0xFFFF) return cp;
    cp -= 0x10000;
    value->pendingWide = 0xDC00 + (cp & 0x3FF);
    return 0xD800 + (cp >> 10);
}

__declspec(dllexport) int __cdecl setvbuf(void* stream, char* buffer, int mode, size_t size) {
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return -1; }
    if (mode < 0 || mode > 2 || (mode != 2 && size == 0) || size > UINT32_MAX) {
        g_nativeErrno = 22; return -1;
    }
    if (fflush(stream) != 0) return -1;
    if (value->ownsBuffer && value->buffer) g_nativeHeap.release(value->buffer);
    value->buffer = buffer; value->bufferSize = mode == 2 ? 0 : size;
    value->buffered = 0; value->ownsBuffer = false;
    value->bufferingMode = static_cast<uint32_t>(mode);
    if (mode != 2 && !buffer) {
        value->buffer = static_cast<char*>(g_nativeHeap.allocate(size, alignof(char)));
        if (!value->buffer) { value->bufferSize = 0; g_nativeErrno = 12; return -1; }
        value->ownsBuffer = true;
    }
    return 0;
}

__declspec(dllexport) int32_t __cdecl _configure_narrow_argv(int32_t mode) {
    if (mode < 0 || mode > 2) { g_nativeErrno = 22; return 22; }
    if (g_nativeArgvInitialized) { g_nativeErrno = 22; return 22; }
    g_nativeArgvMode = mode; return 0;
}
__declspec(dllexport) int32_t __cdecl _configure_wide_argv(int32_t mode) {
    if (mode < 0 || mode > 2 || g_nativeWArgvInitialized) {
        g_nativeErrno = 22; return 22;
    }
    g_nativeWArgvMode = mode;
    return 0;
}
__declspec(dllexport) int32_t __cdecl _initialize_narrow_environment() {
    return initializeNarrowEnvironment();
}
__declspec(dllexport) int32_t __cdecl _initialize_wide_environment() {
    return initializeWideEnvironment();
}
__declspec(dllexport) char** __cdecl _get_initial_narrow_environment() {
    return initializeNarrowEnvironment() == 0 ? g_nativeEnvironment : nullptr;
}
__declspec(dllexport) wchar_t** __cdecl _get_initial_wide_environment() {
    return initializeWideEnvironment() == 0 ? g_nativeWEnvironment : nullptr;
}
__declspec(dllexport) int32_t __cdecl _crt_atexit(micant::ucrt::OnExitRoutine function) {
    if (!function) { g_nativeErrno = 22; return 22; }
    if (!g_nativeOnExitInitialized) {
        if (micant::ucrt::initializeOnExitTable(&g_nativeOnExitTable) != 0) {
            g_nativeErrno = 12; return 12;
        }
        g_nativeOnExitInitialized = true;
    }
    return micant::ucrt::registerOnExitFunction(
        &g_nativeOnExitTable, function, g_nativeHeap);
}
__declspec(dllexport) int32_t __cdecl _seh_filter_exe(uint32_t, void*) {
    return 1; // EXCEPTION_EXECUTE_HANDLER
}
__declspec(dllexport) int32_t __cdecl _set_app_type(int32_t type) {
    const int previous = g_nativeApplicationType; g_nativeApplicationType = type; return previous;
}
__declspec(dllexport) int __cdecl _configthreadlocale(int mode) {
    const int previous = g_nativeThreadLocaleMode;
    if (mode == -1) return previous;
    if (mode != 0 && mode != 1 && mode != 2) { g_nativeErrno = 22; return -1; }
    g_nativeThreadLocaleMode = mode; return previous;
}
__declspec(dllexport) int __cdecl _set_new_mode(int mode) {
    const int previous = g_nativeNewMode;
    if (mode != 0 && mode != 1) { g_nativeErrno = 22; return -1; }
    g_nativeNewMode = mode; return previous;
}
__declspec(dllexport) NativeInvalidParameterHandler __cdecl
_set_invalid_parameter_handler(NativeInvalidParameterHandler handler) {
    const auto previous = g_nativeInvalidParameterHandler;
    g_nativeInvalidParameterHandler = handler; return previous;
}
__declspec(dllexport) void __cdecl __setusermatherr(int (__cdecl *handler)(void*)) {
    g_nativeMathErrorHandler = handler;
}
__declspec(dllexport) int __cdecl _finite(double value) { return ucrtFinite(value) ? 1 : 0; }
__declspec(dllexport) int __cdecl _isnan(double value) { return ucrtNan(value) ? 1 : 0; }
__declspec(dllexport) double __cdecl atan(double value) { return ucrtAtan(value); }
__declspec(dllexport) double __cdecl acos(double value) {
    if (value < -1.0 || value > 1.0) { g_nativeErrno = 33; return ucrtDoubleFromBits(0x7FF8000000000000ULL); }
    return ucrtAtan2(ucrtSqrt((1.0 - value) * (1.0 + value)), value);
}
__declspec(dllexport) double __cdecl asin(double value) {
    if (value < -1.0 || value > 1.0) { g_nativeErrno = 33; return ucrtDoubleFromBits(0x7FF8000000000000ULL); }
    return ucrtAtan2(value, ucrtSqrt((1.0 - value) * (1.0 + value)));
}
__declspec(dllexport) double __cdecl cbrt(double value) {
    if (value == 0.0 || !ucrtFinite(value)) return value;
    const double magnitude = ucrtExp(ucrtLog(value < 0.0 ? -value : value) / 3.0);
    return value < 0.0 ? -magnitude : magnitude;
}
__declspec(dllexport) double __cdecl ceil(double value) {
    if (!ucrtFinite(value) || value == 0.0) return value;
    double integral{}; const double fraction = ucrtModf(value, &integral);
    return fraction > 0.0 ? integral + 1.0 : integral;
}
__declspec(dllexport) double __cdecl cos(double value) { return ucrtCos(value); }
__declspec(dllexport) double __cdecl cosh(double value) { return (ucrtExp(value) + ucrtExp(-value)) * 0.5; }
__declspec(dllexport) double __cdecl exp(double value) { return ucrtExp(value); }
__declspec(dllexport) double __cdecl fmod(double x, double y) { return ucrtFmod(x, y); }
__declspec(dllexport) double __cdecl log(double value) {
    if (value < 0.0) { g_nativeErrno = 33; return ucrtDoubleFromBits(0x7FF8000000000000ULL); }
    if (value == 0.0) { g_nativeErrno = 34; return -ucrtDoubleFromBits(0x7FF0000000000000ULL); }
    return ucrtLog(value);
}
__declspec(dllexport) double __cdecl log10(double value) { return ucrtLog(value) / 2.302585092994045684; }
__declspec(dllexport) double __cdecl modf(double value, double* integral) { return ucrtModf(value, integral); }
__declspec(dllexport) double __cdecl pow(double x, double y) { return ucrtPow(x, y); }
__declspec(dllexport) double __cdecl sin(double value) { return ucrtSin(value); }
__declspec(dllexport) double __cdecl sinh(double value) { return (ucrtExp(value) - ucrtExp(-value)) * 0.5; }
__declspec(dllexport) double __cdecl sqrt(double value) {
    if (value < 0.0) { g_nativeErrno = 33; return ucrtDoubleFromBits(0x7FF8000000000000ULL); }
    return ucrtSqrt(value);
}
__declspec(dllexport) double __cdecl tan(double value) { return ucrtSin(value) / ucrtCos(value); }
__declspec(dllexport) double __cdecl tanh(double value) {
    const double positive = ucrtExp(value), negative = ucrtExp(-value);
    return (positive - negative) / (positive + negative);
}
__declspec(dllexport) void __cdecl _cexit() {
    if (g_nativeOnExitInitialized)
        (void)micant::ucrt::executeOnExitTable(&g_nativeOnExitTable, g_nativeHeap);
    (void)fflush(nullptr);
}
__declspec(dllexport) void __cdecl exit(int status) {
    _cexit();
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), status);
    __builtin_trap();
}

__declspec(dllexport) void __cdecl abort(void) {
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), 3);
    __builtin_trap();
}

__declspec(dllexport) void __cdecl _exit(int status) {
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), status);
    __builtin_trap();
}

__declspec(dllexport) void __cdecl _assert(const char* /*expression*/, const char* /*file*/, unsigned /*line*/) {
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), 3);
    __builtin_trap();
}

__declspec(dllexport) char* __cdecl strerror(int error) {
    switch (error) {
        case 0: return const_cast<char*>("No error");
        case 1: return const_cast<char*>("Operation not permitted");
        case 2: return const_cast<char*>("No such file or directory");
        case 5: return const_cast<char*>("Input/output error");
        case 9: return const_cast<char*>("Bad file descriptor");
        case 12: return const_cast<char*>("Not enough memory");
        case 13: return const_cast<char*>("Permission denied");
        case 22: return const_cast<char*>("Invalid argument");
        case 34: return const_cast<char*>("Result out of range");
        case 42: return const_cast<char*>("Illegal byte sequence");
        default: return const_cast<char*>("Unknown error");
    }
}

__declspec(dllexport) NativeLocaleInfo* __cdecl localeconv(void) { return &g_nativeLocale; }

__declspec(dllexport) char* __cdecl getenv(const char* name) {
    if (!name || !*name) return nullptr;
    uintptr_t teb{}; __asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(teb));
    if (!teb) return nullptr;
    const uintptr_t peb = *reinterpret_cast<const uintptr_t*>(teb + 0x60);
    const uintptr_t parameters = peb ? *reinterpret_cast<const uintptr_t*>(peb + 0x20) : 0;
    if (parameters < 0x10000 || parameters > 0x00007FFFFFFF0000ULL - 0x88) return nullptr;
    const auto* environment = *reinterpret_cast<const char16_t* const*>(parameters + 0x80);
    if (!environment) return nullptr;
    for (size_t entry = 0; entry < 32768 && environment[entry];) {
        size_t end = entry;
        while (end < 32768 && environment[end]) ++end;
        size_t equal = entry;
        while (equal < end && environment[equal] != u'=') ++equal;
        size_t nameLength{}; while (name[nameLength]) ++nameLength;
        bool matches = equal - entry == nameLength;
        for (size_t i = 0; matches && i < nameLength; ++i) {
            char c = name[i]; char16_t w = environment[entry + i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            if (w >= u'A' && w <= u'Z') w = static_cast<char16_t>(w + (u'a' - u'A'));
            matches = static_cast<unsigned char>(c) == w;
        }
        if (matches && equal < end) {
            static char result[4096];
            size_t output{};
            for (size_t i = equal + 1; i < end && output + 1 < sizeof(result); ++i) {
                uint32_t cp = environment[i];
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < end &&
                    environment[i + 1] >= 0xDC00 && environment[i + 1] <= 0xDFFF)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (environment[++i] - 0xDC00);
                if (cp < 0x80) result[output++] = static_cast<char>(cp);
                else if (cp < 0x800 && output + 2 < sizeof(result)) {
                    result[output++] = static_cast<char>(0xC0 | (cp >> 6));
                    result[output++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000 && output + 3 < sizeof(result)) {
                    result[output++] = static_cast<char>(0xE0 | (cp >> 12));
                    result[output++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    result[output++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp <= 0x10FFFF && output + 4 < sizeof(result)) {
                    result[output++] = static_cast<char>(0xF0 | (cp >> 18));
                    result[output++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    result[output++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    result[output++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else { g_nativeErrno = 42; return nullptr; }
            }
            result[output] = 0;
            return result;
        }
        entry = end + 1;
    }
    return nullptr;
}

__declspec(dllexport) size_t __cdecl mbrtowc(
    wchar_t* output, const char* input, size_t count, void* statePointer) {
    static NativeMultibyteState internalState{};
    auto& state = statePointer ? *static_cast<NativeMultibyteState*>(statePointer) : internalState;
    if (!input) { state = {}; return 0; }
    if (!count) return static_cast<size_t>(-2);
    size_t consumed{};
    while (consumed < count) {
        const uint8_t byte = static_cast<uint8_t>(input[consumed++]);
        if (!state.remaining) {
            if (byte < 0x80) {
                if (!byte) { if (output) *output = 0; return 0; }
                if (output) *output = static_cast<wchar_t>(byte);
                return consumed;
            }
            if (byte >= 0xC2 && byte <= 0xDF) { state.value = byte & 0x1F; state.remaining = 1; state.total = 1; }
            else if (byte >= 0xE0 && byte <= 0xEF) { state.value = byte & 0x0F; state.remaining = 2; state.total = 2; }
            else if (byte >= 0xF0 && byte <= 0xF4) { state.value = byte & 7; state.remaining = 3; state.total = 3; }
            else { state = {}; g_nativeErrno = 42; return static_cast<size_t>(-1); }
            continue;
        }
        if ((byte & 0xC0) != 0x80) { state = {}; g_nativeErrno = 42; return static_cast<size_t>(-1); }
        state.value = (state.value << 6) | (byte & 0x3F);
        if (--state.remaining == 0) {
            const uint32_t cp = state.value;
            const uint8_t total = state.total;
            state = {};
            if (cp < 0x80 || (cp < 0x800 && total >= 2) ||
                (cp < 0x10000 && total >= 3) || cp > 0xFFFF ||
                (cp >= 0xD800 && cp <= 0xDFFF)) {
                g_nativeErrno = 42; return static_cast<size_t>(-1);
            }
            if (output) *output = static_cast<wchar_t>(cp);
            return consumed;
        }
    }
    return static_cast<size_t>(-2);
}

__declspec(dllexport) size_t __cdecl wcrtomb(
    char* output, wchar_t character, void* statePointer) {
    static NativeMultibyteState internalState{};
    if (statePointer) *static_cast<NativeMultibyteState*>(statePointer) = {};
    else internalState = {};
    if (!output) return 1;
    const uint32_t cp = static_cast<uint16_t>(character);
    if (cp <= 0x7F) { output[0] = static_cast<char>(cp); return 1; }
    if (cp <= 0x7FF) {
        output[0] = static_cast<char>(0xC0 | (cp >> 6));
        output[1] = static_cast<char>(0x80 | (cp & 0x3F)); return 2;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) { g_nativeErrno = 42; return static_cast<size_t>(-1); }
    output[0] = static_cast<char>(0xE0 | (cp >> 12));
    output[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    output[2] = static_cast<char>(0x80 | (cp & 0x3F)); return 3;
}

}

#else

#include "micant/ucrt/winnt_ucrtbase_runtime_abi.hpp"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <vector>

namespace micant::ucrt::winnt_ucrtbase_abi {

namespace {
thread_local int32_t g_ucrtErrno = 0;
int32_t g_appType = 0;
char* g_dummyEnv[1]{};
char g_iobDummy[3][48]{};
_onexit_table_t g_globalOnExitTable{};
bool g_globalOnExitInitialized{};
} // namespace

int32_t Ucrt_initialize_onexit_table(_onexit_table_t* table) noexcept {
    if (!table) return -1;
    table->_first = nullptr;
    table->_last  = nullptr;
    table->_end   = nullptr;
    return 0;
}

int32_t Ucrt_register_onexit_function(_onexit_table_t* table, _onexit_t function) noexcept {
    if (!table || !function) return -1;
    size_t count = (table->_last && table->_first) ? (table->_last - table->_first) : 0;
    size_t cap   = (table->_end && table->_first)  ? (table->_end - table->_first) : 0;
    if (count >= cap) {
        size_t newCap = cap == 0 ? 8 : cap * 2;
        auto* newBuf = static_cast<_onexit_t*>(std::realloc(table->_first, newCap * sizeof(_onexit_t)));
        if (!newBuf) return -1;
        table->_first = newBuf;
        table->_last  = newBuf + count;
        table->_end   = newBuf + newCap;
    }
    *(table->_last) = function;
    table->_last++;
    return 0;
}

int32_t Ucrt_execute_onexit_table(_onexit_table_t* table) noexcept {
    if (!table || !table->_first || !table->_last) return 0;
    while (table->_last > table->_first) {
        table->_last--;
        _onexit_t fn = *(table->_last);
        if (fn) fn();
    }
    std::free(table->_first);
    table->_first = nullptr;
    table->_last  = nullptr;
    table->_end   = nullptr;
    return 0;
}

int32_t Ucrt_crt_atexit(_onexit_t function) noexcept {
    if (!g_globalOnExitInitialized) {
        Ucrt_initialize_onexit_table(&g_globalOnExitTable);
        g_globalOnExitInitialized = true;
    }
    return Ucrt_register_onexit_function(&g_globalOnExitTable, function);
}

bool encodeUtf16ToUtf8(const char16_t* source, size_t units, char* output,
                       size_t capacity, size_t& bytes) noexcept {
    bytes = 0;
    for (size_t i = 0; i < units; ++i) {
        uint32_t cp = source[i];
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 >= units || source[i + 1] < 0xDC00 || source[i + 1] > 0xDFFF)
                return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (source[++i] - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false;
        }
        const size_t need = cp <= 0x7F ? 1 : cp <= 0x7FF ? 2 : cp <= 0xFFFF ? 3 : 4;
        if (need > capacity - bytes) return false;
        if (need == 1) output[bytes++] = static_cast<char>(cp);
        else if (need == 2) {
            output[bytes++] = static_cast<char>(0xC0 | (cp >> 6));
            output[bytes++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (need == 3) {
            output[bytes++] = static_cast<char>(0xE0 | (cp >> 12));
            output[bytes++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            output[bytes++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            output[bytes++] = static_cast<char>(0xF0 | (cp >> 18));
            output[bytes++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            output[bytes++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            output[bytes++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return true;
}

static int32_t initializeNarrowEnvironment() noexcept {
    if (g_nativeEnvironmentInitialized) return 0;
    char16_t* source{};
    if (!micant::shared::native_process::currentEnvironment(source)) {
        g_ucrtErrno = 22;
        return 22;
    }
    size_t units{};
    size_t entries{};
    bool ended{};
    while (units < 32768) {
        if (source[units] == 0 && units + 1 < 32768 && source[units + 1] == 0) {
            units += 2;
            ended = true;
            break;
        }
        if (source[units] == 0) ++entries;
        ++units;
    }
    if (!ended) { g_ucrtErrno = 7; return 7; }
    if (units > SIZE_MAX / 3 || entries > (SIZE_MAX / sizeof(char*)) - 1) {
        g_ucrtErrno = 12;
        return 12;
    }
    auto** environment = static_cast<char**>(g_nativeHeap.allocate(
        (entries + 1) * sizeof(char*), alignof(char*)));
    auto* text = static_cast<char*>(g_nativeHeap.allocate(units * 3 + 1, alignof(char)));
    if (!environment || !text) {
        if (environment) g_nativeHeap.release(environment);
        if (text) g_nativeHeap.release(text);
        g_ucrtErrno = 12;
        return 12;
    }
    size_t sourceOffset{};
    size_t textOffset{};
    size_t index{};
    while (sourceOffset < units && source[sourceOffset]) {
        size_t end = sourceOffset;
        while (end < units && source[end]) ++end;
        environment[index++] = text + textOffset;
        size_t converted{};
        if (!encodeUtf16ToUtf8(source + sourceOffset, end - sourceOffset,
                         text + textOffset, units * 3 + 1 - textOffset,
                         converted)) {
            g_nativeHeap.release(environment);
            g_nativeHeap.release(text);
            g_ucrtErrno = 42;
            return 42;
        }
        textOffset += converted;
        text[textOffset++] = '\0';
        sourceOffset = end + 1;
    }
    environment[index] = nullptr;
    g_nativeEnvironment = environment;
    g_nativeEnvironmentInitialized = true;
    return 0;
}

int32_t Ucrt_seh_filter_exe(uint32_t /*xcptNum*/, void* /*pXcp*/) noexcept {
    return 1; // EXCEPTION_EXECUTE_HANDLER
}

void Ucrt_set_app_type(int32_t appType) noexcept {
    g_appType = appType;
}

int32_t Ucrt_configure_narrow_argv(int32_t mode) noexcept {
    if (mode < 0 || mode > 2) { g_ucrtErrno = 22; return 22; }
    g_nativeArgvMode = mode;
    return 0;
}

int32_t Ucrt_initialize_narrow_environment() noexcept {
    return initializeNarrowEnvironment();
}

char** Ucrt_get_initial_narrow_environment() noexcept {
    return g_nativeEnvironmentInitialized ? g_nativeEnvironment : nullptr;
}

void Ucrt_initterm(void (**first)(void), void (**last)(void)) noexcept {
    if (!first || !last) return;
    for (auto it = first; it < last; ++it) {
        if (*it) (*it)();
    }
}

int32_t Ucrt_initterm_e(int32_t (**first)(void), int32_t (**last)(void)) noexcept {
    if (!first || !last) return 0;
    for (auto it = first; it < last; ++it) {
        if (*it) {
            int32_t res = (*it)();
            if (res != 0) return res;
        }
    }
    return 0;
}

void* Ucrt_malloc(size_t size) noexcept {
    return std::malloc(size > 0 ? size : 1);
}

void* Ucrt_calloc(size_t count, size_t size) noexcept {
    return std::calloc(count > 0 ? count : 1, size > 0 ? size : 1);
}

void* Ucrt_realloc(void* block, size_t size) noexcept {
    if (!block) {
        return std::malloc(size > 0 ? size : 1);
    }
    if (size == 0) {
        std::free(block);
        return nullptr;
    }
    return std::realloc(block, size);
}

void Ucrt_free(void* block) noexcept {
    if (block) std::free(block);
}

void* Ucrt_acrt_iob_func(uint32_t index) noexcept {
    return (index < 3) ? reinterpret_cast<void*>(&g_iobDummy[index]) : nullptr;
}

int32_t Ucrt_puts(const char* str) noexcept {
    return str ? static_cast<int32_t>(std::strlen(str)) : 0;
}

int32_t Ucrt_stdio_common_vsprintf(uint64_t /*options*/, char* buffer, size_t bufferCount, const char* format, void* /*locale*/, va_list valist) noexcept {
    if (!buffer || bufferCount == 0 || !format) return -1;
    return std::vsnprintf(buffer, bufferCount, format, valist);
}

int32_t* Ucrt_errno() noexcept {
    return &g_ucrtErrno;
}

void Ucrt_exit(int32_t status) noexcept {
    if (g_globalOnExitInitialized) (void)Ucrt_execute_onexit_table(&g_globalOnExitTable);
    (void)::fflush(nullptr);
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), status);
    __builtin_trap();
}
void Ucrt_quick_exit(int32_t status) noexcept {
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), status);
    __builtin_trap();
}
void Ucrt_abort() noexcept {
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{0}), 3);
    __builtin_trap();
}

double Ucrt_sin(double x) noexcept { return std::sin(x); }
double Ucrt_cos(double x) noexcept { return std::cos(x); }
double Ucrt_sqrt(double x) noexcept { return std::sqrt(x); }
double Ucrt_pow(double x, double y) noexcept { return std::pow(x, y); }

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
void WinNtUcrtbaseCoordinator::initialize() noexcept {
    if (m_initialized) return;
    m_cachedReport = runUcrtbaseAudit();
    m_initialized = true;
}

Horizon116UcrtbaseAuditReport WinNtUcrtbaseCoordinator::runUcrtbaseAudit() noexcept {
    Horizon116UcrtbaseAuditReport rep{};
    rep.ucrtbaseExportCount = 26;

    // 1. Verify OnExit Table Lifecycle
    _onexit_table_t tbl{};
    static int s_exitRan = 0;
    s_exitRan = 0;
    if (Ucrt_initialize_onexit_table(&tbl) == 0) {
        Ucrt_register_onexit_function(&tbl, []() -> int {
            s_exitRan = 42;
            return 0;
        });
        Ucrt_execute_onexit_table(&tbl);
        rep.onExitTableDispatchVerified = (s_exitRan == 42);
    }

    // 2. Verify _initterm and _initterm_e Execution
    static int s_termCalls = 0;
    s_termCalls = 0;
    void (*terms[2])(void) = {
        []() { s_termCalls += 1; },
        []() { s_termCalls += 2; }
    };
    int32_t (*term_e[2])(void) = {
        []() -> int32_t { s_termCalls += 10; return 0; },
        []() -> int32_t { s_termCalls += 20; return 0; }
    };
    Ucrt_initterm(&terms[0], &terms[2]);
    Ucrt_initterm_e(&term_e[0], &term_e[2]);
    rep.inittermExecutionVerified = (s_termCalls == 33);

    // 3. Verify Memory Allocation, Realloc, and Free
    void* p = Ucrt_malloc(64);
    if (p) {
        memset(p, 0xAB, 64);
        void* p2 = Ucrt_realloc(p, 128);
        if (p2) {
            rep.heapAllocReallocFreeVerified = true;
            Ucrt_free(p2);
        }
    }

    // 4. Verify Stdio Streams and Format
    char buf[128]{};
    auto testFormat = [](char* b, size_t bc, const char* fmt, ...) -> int32_t {
        va_list args;
        va_start(args, fmt);
        int32_t r = Ucrt_stdio_common_vsprintf(0, b, bc, fmt, nullptr, args);
        va_end(args);
        return r;
    };
    int32_t n = testFormat(buf, sizeof(buf), "MicaNT UCRT %d", 26100);
    rep.stdioFormatAndStreamsVerified = (n > 0 && std::strcmp(buf, "MicaNT UCRT 26100") == 0 &&
                                         Ucrt_acrt_iob_func(0) != nullptr);

    // 5. Verify Math & Errno
    *Ucrt_errno() = 0;
    double s = Ucrt_sin(0.0);
    double c = Ucrt_cos(0.0);
    double sq = Ucrt_sqrt(16.0);
    double pw = Ucrt_pow(2.0, 8.0);
    rep.mathAndErrnoVerified = (s == 0.0 && c == 1.0 && sq == 4.0 && pw == 256.0 && *Ucrt_errno() == 0);

    return rep;
}

void InitializeWinNtUcrtbaseSubsystem() noexcept {
    WinNtUcrtbaseCoordinator::instance().initialize();
}
#endif

} // namespace micant::ucrt::winnt_ucrtbase_abi

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
namespace micant::sys::internal {
void WinNtUcrtbaseInitializeExecutiveSubsystem() noexcept {
    micant::ucrt::winnt_ucrtbase_abi::InitializeWinNtUcrtbaseSubsystem();
}
} // namespace micant::sys::internal
#endif

#if !defined(BUILD_SYSTEM32_UCRTBASE_DLL)
extern "C" {

__declspec(dllexport) int32_t __cdecl __stdio_common_vswprintf(
    uint64_t, wchar_t* buffer, size_t capacity, const wchar_t* format,
    void*, va_list arguments) {
    if (!buffer || !capacity || !format) { g_nativeErrno = 22; return -1; }
    const int result = std::vswprintf(buffer, capacity, format, arguments);
    if (result < 0) g_nativeErrno = 34;
    return result;
}

__declspec(dllexport) int32_t __cdecl __stdio_common_vswscanf(
    uint64_t, const wchar_t* input, size_t, const wchar_t* format,
    void*, va_list arguments) {
    if (!input || !format) { g_nativeErrno = 22; return -1; }
    return std::vswscanf(input, format, arguments);
}

__declspec(dllexport) int __cdecl getc(void* stream) {
    auto* value = nativeStream(stream);
    if (!value || value->descriptor != 0) { g_nativeErrno = 9; return -1; }
    int32_t byte = nativeReadByte(*value);
    if (byte < 0) return -1;
    if (value->mode == 0x4000 && byte == '\r') {
        const int32_t following = nativeReadByte(*value);
        if (following == '\n') return '\n';
        if (following >= 0) value->pendingByte = following;
    }
    return byte;
}

__declspec(dllexport) wint_t __cdecl fgetwc(void* stream) {
    auto* value = nativeStream(stream);
    if (!value || value->descriptor != 0) { g_nativeErrno = 9; return WEOF; }
    if (value->pendingWide != UINT32_MAX) {
        const wint_t pending = static_cast<wint_t>(value->pendingWide);
        value->pendingWide = UINT32_MAX;
        return pending;
    }
    const int first = getc(stream);
    if (first < 0) return WEOF;
    uint32_t codepoint{};
    unsigned remaining{};
    if (first <= 0x7F) codepoint = static_cast<uint32_t>(first);
    else if (first >= 0xC2 && first <= 0xDF) { codepoint = first & 0x1F; remaining = 1; }
    else if (first >= 0xE0 && first <= 0xEF) { codepoint = first & 0x0F; remaining = 2; }
    else if (first >= 0xF0 && first <= 0xF4) { codepoint = first & 0x07; remaining = 3; }
    else { g_nativeErrno = 42; return WEOF; }
    for (unsigned i = 0; i < remaining; ++i) {
        const int next = getc(stream);
        if (next < 0 || (next & 0xC0) != 0x80) { g_nativeErrno = 42; return WEOF; }
        codepoint = (codepoint << 6) | static_cast<uint32_t>(next & 0x3F);
    }
    if ((remaining == 1 && codepoint < 0x80) ||
        (remaining == 2 && codepoint < 0x800) ||
        (remaining == 3 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        g_nativeErrno = 42;
        return WEOF;
    }
    if (codepoint <= 0xFFFF) return static_cast<wint_t>(codepoint);
    codepoint -= 0x10000;
    value->pendingWide = 0xDC00 + (codepoint & 0x3FF);
    return static_cast<wint_t>(0xD800 + (codepoint >> 10));
}

__declspec(dllexport) int __cdecl setvbuf(
    void* stream, char* buffer, int mode, size_t size) {
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return -1; }
    if (mode != 0 && mode != 1 && mode != 2) { g_nativeErrno = 22; return -1; }
    if (mode != 2 && size == 0) { g_nativeErrno = 22; return -1; }
    if (mode == 2) size = 0;
    else if (size > UINT32_MAX) { g_nativeErrno = 22; return -1; }
    if (fflush(stream) != 0) return -1;
    if (value->ownsBuffer && value->buffer)
        micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(value->buffer);
    value->buffer = buffer;
    value->bufferSize = size;
    value->buffered = 0;
    value->ownsBuffer = false;
    value->bufferingMode = static_cast<uint32_t>(mode);
    if (mode != 2 && !buffer) {
        value->buffer = static_cast<char*>(
            micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(size));
        if (!value->buffer) { value->bufferSize = 0; return -1; }
        value->ownsBuffer = true;
    }
    return 0;
}

__declspec(dllexport) void __cdecl _cexit() {
    if (micant::ucrt::winnt_ucrtbase_abi::g_globalOnExitInitialized)
        (void)micant::ucrt::winnt_ucrtbase_abi::Ucrt_execute_onexit_table(
            &micant::ucrt::winnt_ucrtbase_abi::g_globalOnExitTable);
    (void)::fflush(nullptr);
}

__declspec(dllexport) int32_t __cdecl _configure_narrow_argv(int32_t mode) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_configure_narrow_argv(mode);
}

__declspec(dllexport) int32_t __cdecl _initialize_narrow_environment() {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_initialize_narrow_environment();
}

__declspec(dllexport) char** __cdecl _get_initial_narrow_environment() {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_get_initial_narrow_environment();
}

__declspec(dllexport) int32_t __cdecl _crt_atexit(
    micant::ucrt::winnt_ucrtbase_abi::_onexit_t function) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_crt_atexit(function);
}

__declspec(dllexport) int32_t __cdecl _seh_filter_exe(uint32_t code, void* context) {
    return micant::ucrt::winnt_ucrtbase_abi::Ucrt_seh_filter_exe(code, context);
}

__declspec(dllexport) void __cdecl _set_app_type(int32_t type) {
    micant::ucrt::winnt_ucrtbase_abi::Ucrt_set_app_type(type);
}

__declspec(dllexport) NativeInvalidParameterHandler __cdecl
_set_invalid_parameter_handler(NativeInvalidParameterHandler handler) {
    const auto previous = g_nativeInvalidParameterHandler;
    g_nativeInvalidParameterHandler = handler;
    return previous;
}

__declspec(dllexport) void __cdecl exit(int status) {
    micant::ucrt::winnt_ucrtbase_abi::Ucrt_exit(status);
}

__declspec(dllexport) int __cdecl _configthreadlocale(int mode) {
    const int previous = g_nativeThreadLocaleMode;
    if (mode == -1) return previous;
    if (mode == 1 || mode == 2) {
        g_nativeThreadLocaleMode = mode;
        return previous;
    }
    g_nativeErrno = 22;
    return -1;
}

__declspec(dllexport) int __cdecl _set_new_mode(int mode) {
    const int previous = g_nativeNewMode;
    if (mode != 0 && mode != 1) { g_nativeErrno = 22; return -1; }
    g_nativeNewMode = mode;
    return previous;
}

__declspec(dllexport) void __cdecl __setusermatherr(
    int (__cdecl *handler)(void*)) {
    g_nativeMathErrorHandler = handler;
}

__declspec(dllexport) int __cdecl _finite(double value) {
    return std::isfinite(value) ? 1 : 0;
}
__declspec(dllexport) int __cdecl _isnan(double value) {
    return std::isnan(value) ? 1 : 0;
}
__declspec(dllexport) double __cdecl acos(double value) { return std::acos(value); }
__declspec(dllexport) double __cdecl asin(double value) { return std::asin(value); }
__declspec(dllexport) double __cdecl atan(double value) { return std::atan(value); }
__declspec(dllexport) double __cdecl cbrt(double value) { return std::cbrt(value); }
__declspec(dllexport) double __cdecl ceil(double value) { return std::ceil(value); }
__declspec(dllexport) double __cdecl cos(double value) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_cos(value); }
__declspec(dllexport) double __cdecl cosh(double value) { return std::cosh(value); }
__declspec(dllexport) double __cdecl exp(double value) { return std::exp(value); }
__declspec(dllexport) double __cdecl fmod(double left, double right) { return std::fmod(left, right); }
__declspec(dllexport) double __cdecl log(double value) { return std::log(value); }
__declspec(dllexport) double __cdecl log10(double value) { return std::log10(value); }
__declspec(dllexport) double __cdecl modf(double value, double* integral) { return std::modf(value, integral); }
__declspec(dllexport) double __cdecl pow(double left, double right) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_pow(left, right); }
__declspec(dllexport) double __cdecl sin(double value) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_sin(value); }
__declspec(dllexport) double __cdecl sinh(double value) { return std::sinh(value); }
__declspec(dllexport) double __cdecl sqrt(double value) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_sqrt(value); }
__declspec(dllexport) double __cdecl tan(double value) { return std::tan(value); }
__declspec(dllexport) double __cdecl tanh(double value) { return std::tanh(value); }

} // extern "C"
#endif

#if defined(BUILD_SYSTEM32_UCRTBASE_DLL)
extern "C" {
__declspec(dllexport) int32_t _initialize_onexit_table(micant::ucrt::winnt_ucrtbase_abi::_onexit_table_t* t) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_initialize_onexit_table(t); }
__declspec(dllexport) int32_t _register_onexit_function(micant::ucrt::winnt_ucrtbase_abi::_onexit_table_t* t, micant::ucrt::winnt_ucrtbase_abi::_onexit_t fn) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_register_onexit_function(t, fn); }
__declspec(dllexport) int32_t _execute_onexit_table(micant::ucrt::winnt_ucrtbase_abi::_onexit_table_t* t) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_execute_onexit_table(t); }
__declspec(dllexport) int32_t _crt_atexit(micant::ucrt::winnt_ucrtbase_abi::_onexit_t fn) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_crt_atexit(fn); }
__declspec(dllexport) int32_t _seh_filter_exe(uint32_t n, void* x) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_seh_filter_exe(n, x); }
__declspec(dllexport) void _set_app_type(int32_t t) { micant::ucrt::winnt_ucrtbase_abi::Ucrt_set_app_type(t); }
__declspec(dllexport) int32_t _configure_narrow_argv(int32_t m) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_configure_narrow_argv(m); }
__declspec(dllexport) int32_t _initialize_narrow_environment() { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_initialize_narrow_environment(); }
__declspec(dllexport) char** _get_initial_narrow_environment() { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_get_initial_narrow_environment(); }
__declspec(dllexport) void _initterm(void (**f)(void), void (**l)(void)) { micant::ucrt::winnt_ucrtbase_abi::Ucrt_initterm(f, l); }
__declspec(dllexport) int32_t _initterm_e(int32_t (**f)(void), int32_t (**l)(void)) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_initterm_e(f, l); }
__declspec(dllexport) void* ucrt_malloc(size_t s) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_malloc(s); }
__declspec(dllexport) void* ucrt_calloc(size_t c, size_t s) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_calloc(c, s); }
__declspec(dllexport) void* ucrt_realloc(void* b, size_t s) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_realloc(b, s); }
__declspec(dllexport) void ucrt_free(void* b) { micant::ucrt::winnt_ucrtbase_abi::Ucrt_free(b); }
__declspec(dllexport) void* ucrt_acrt_iob_func(uint32_t i) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_acrt_iob_func(i); }
__declspec(dllexport) int32_t ucrt_puts(const char* s) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_puts(s); }
__declspec(dllexport) int32_t ucrt_stdio_common_vsprintf(uint64_t opt, char* b, size_t bc, const char* f, void* l, va_list va) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_stdio_common_vsprintf(opt, b, bc, f, l, va); }
__declspec(dllexport) int32_t __cdecl __stdio_common_vsprintf(uint64_t opt, char* b, size_t bc, const char* f, void* l, va_list va) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_stdio_common_vsprintf(opt, b, bc, f, l, va); }
__declspec(dllexport) int32_t* ucrt_errno() { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_errno(); }
__declspec(dllexport) void ucrt_exit(int32_t s) { micant::ucrt::winnt_ucrtbase_abi::Ucrt_exit(s); }
__declspec(dllexport) void ucrt_quick_exit(int32_t s) { micant::ucrt::winnt_ucrtbase_abi::Ucrt_quick_exit(s); }
__declspec(dllexport) void ucrt_abort() { micant::ucrt::winnt_ucrtbase_abi::Ucrt_abort(); }
__declspec(dllexport) double ucrt_sin(double x) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_sin(x); }
__declspec(dllexport) double ucrt_cos(double x) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_cos(x); }
__declspec(dllexport) double ucrt_sqrt(double x) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_sqrt(x); }
__declspec(dllexport) double ucrt_pow(double x, double y) { return micant::ucrt::winnt_ucrtbase_abi::Ucrt_pow(x, y); }
} // extern "C"
#endif

#endif
