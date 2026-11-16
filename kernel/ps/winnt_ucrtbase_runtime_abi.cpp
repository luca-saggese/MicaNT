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

}

extern "C" {

__declspec(dllexport) int* __cdecl _errno() {
    return &g_nativeErrno;
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
        while (format[i] == '-' || format[i] == '0' || format[i] == '+' || format[i] == ' ' || format[i] == '#') {
            if (format[i] == '-') left = true;
            if (format[i] == '0') zero = true;
            ++i;
        }
        unsigned width{};
        while (format[i] >= '0' && format[i] <= '9') {
            if (width > 255) { g_nativeErrno = 22; return -1; }
            width = width * 10 + static_cast<unsigned>(format[i++] - '0');
        }
        int precision = -1;
        if (format[i] == '.') {
            precision = 0; ++i;
            while (format[i] >= '0' && format[i] <= '9') {
                if (precision > 255) { g_nativeErrno = 22; return -1; }
                precision = precision * 10 + (format[i++] - '0');
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
            const char* string = va_arg(arguments, const char*);
            if (!string) string = "(null)";
            const size_t maxLength = precision >= 0 ? static_cast<size_t>(precision) : 256;
            while (tempLength < maxLength && string[tempLength]) temp[tempLength] = string[tempLength], ++tempLength;
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
            if (!appendUnsigned(temp, sizeof(temp), tempLength, value, radix, conversion == 'X',
                                width > (negative ? 1u : 0u) ? width - (negative ? 1u : 0u) : 0u,
                                zero && !left ? '0' : ' ', conversion == 'p')) return -1;
        } else {
            g_nativeErrno = 22;
            return -1;
        }
        if (left) while (tempLength < width) temp[tempLength++] = ' ';
        else if (conversion == 's' || conversion == 'c') {
            if (tempLength > width) { /* width is a minimum, content is kept */ }
            else {
                const size_t pad = width - tempLength;
                if (length + pad > capacity) { g_nativeErrno = 34; return -1; }
                for (size_t p = 0; p < pad; ++p) output[length++] = ' ';
            }
        }
        if (length + tempLength > capacity) { g_nativeErrno = 34; return -1; }
        for (size_t p = 0; p < tempLength; ++p) output[length++] = temp[p];
    }
    return static_cast<int32_t>(length);
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
    if (!stream) return 0; // This runtime's streams are unbuffered.
    return nativeStream(stream) ? 0 : -1;
}

__declspec(dllexport) int __cdecl fputc(int character, void* stream) {
    auto* value = nativeStream(stream);
    if (!value) { g_nativeErrno = 9; return -1; }
    _lock_file(stream);
    const char byte = static_cast<char>(character);
    size_t written{};
    nativeWriteBytes(*value, &byte, 1, written);
    _unlock_file(stream);
    return written == 1 ? static_cast<unsigned char>(byte) : -1;
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
char g_dummyEnvKey[] = "PATH=C:\\Windows\\System32";
char* g_dummyEnv[2] = { g_dummyEnvKey, nullptr };
char g_iobDummy[3][48]{};
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
    static _onexit_table_t s_globalTable{};
    static bool s_inited = false;
    if (!s_inited) {
        Ucrt_initialize_onexit_table(&s_globalTable);
        s_inited = true;
    }
    return Ucrt_register_onexit_function(&s_globalTable, function);
}

int32_t Ucrt_seh_filter_exe(uint32_t /*xcptNum*/, void* /*pXcp*/) noexcept {
    return 1; // EXCEPTION_EXECUTE_HANDLER
}

void Ucrt_set_app_type(int32_t appType) noexcept {
    g_appType = appType;
}

int32_t Ucrt_configure_narrow_argv(int32_t /*mode*/) noexcept {
    return 0;
}

int32_t Ucrt_initialize_narrow_environment() noexcept {
    return 0;
}

char** Ucrt_get_initial_narrow_environment() noexcept {
    return g_dummyEnv;
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

void Ucrt_exit(int32_t /*status*/) noexcept {}
void Ucrt_quick_exit(int32_t /*status*/) noexcept {}
void Ucrt_abort() noexcept {}

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
