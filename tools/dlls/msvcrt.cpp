// LuceOS MSVCRT compatibility DLL. This is a CRT-facing PE module, not an
// application shim. Shared allocation and errno state are delegated to the
// canonical UCRT only where their exported ABI and semantics match.
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <climits>
#include "micant/shared/native_process_abi.hpp"

static_assert(sizeof(wchar_t) == sizeof(char16_t));

extern "C" {
__declspec(dllimport) void* __cdecl malloc(size_t) noexcept;
__declspec(dllimport) void __cdecl free(void*) noexcept;
__declspec(dllimport) void* __cdecl realloc(void*, size_t) noexcept;
__declspec(dllimport) void* __cdecl memcpy(void*, const void*, size_t) noexcept;
__declspec(dllimport) void* __cdecl memset(void*, int, size_t) noexcept;
__declspec(dllimport) int* __cdecl _errno() noexcept;
__declspec(dllimport) void* __cdecl localeconv() noexcept;
__declspec(dllimport) int __cdecl fflush(void*) noexcept;
__declspec(dllimport) void __cdecl _initterm(void (**)(void), void (**)(void)) noexcept;
__declspec(dllimport) int __cdecl _initterm_e(int (**)(void), int (**)(void)) noexcept;
__declspec(dllimport) int32_t __stdcall NtTerminateProcess(void*, int32_t) noexcept;

namespace {
constexpr size_t MaxCommandLine = 32768;
constexpr size_t MaxArguments = 256;
constexpr size_t MaxEnvironmentEntries = 4096;
constexpr size_t MaxEnvironmentChars = 32768;
constexpr size_t MaxExitHandlers = 128;
constexpr int ErrInvalid = 22;
constexpr int ErrNoMemory = 12;

wchar_t g_commandLine[MaxCommandLine]{};
wchar_t* g_environmentBlock{};
wchar_t* g_environmentVector[MaxEnvironmentEntries]{};
void (*g_exitVoid[MaxExitHandlers])(){};
int (*g_exitInt[MaxExitHandlers])(){};
uint8_t g_exitKind[MaxExitHandlers]{};
size_t g_exitCount{};
uint32_t g_exitLock{};
bool g_exitDispatching{};
int g_applicationType{};
void (*g_userMathError)(void*){};
void (*g_signalHandlers[32])(int){};
char g_cLocale[] = "C";

void lockExit() noexcept {
    while (__atomic_exchange_n(&g_exitLock, 1, __ATOMIC_ACQUIRE))
        __asm__ __volatile__("pause");
}
void unlockExit() noexcept { __atomic_store_n(&g_exitLock, 0, __ATOMIC_RELEASE); }

bool initializeProcessData() noexcept {
    micant::shared::native_process::UnicodeString64 command{};
    if (!micant::shared::native_process::currentCommandLine(command) ||
        command.Length > (MaxCommandLine - 1) * sizeof(wchar_t)) return false;
    for (size_t i = 0; i < command.Length / sizeof(wchar_t); ++i)
        g_commandLine[i] = static_cast<wchar_t>(command.Buffer[i]);
    g_commandLine[command.Length / sizeof(wchar_t)] = L'\0';

    char16_t* environment{};
    if (!micant::shared::native_process::currentEnvironment(environment)) return false;
    g_environmentBlock = reinterpret_cast<wchar_t*>(environment);
    size_t offset{};
    size_t count{};
    while (offset < MaxEnvironmentChars && g_environmentBlock[offset]) {
        if (count + 1 >= MaxEnvironmentEntries) return false;
        g_environmentVector[count++] = g_environmentBlock + offset;
        size_t end = offset;
        while (end < MaxEnvironmentChars && g_environmentBlock[end]) ++end;
        if (end == MaxEnvironmentChars) return false;
        offset = end + 1;
    }
    if (offset == MaxEnvironmentChars) return false;
    g_environmentVector[count] = nullptr;
    return true;
}

bool isSpace(wchar_t c) noexcept { return c == L' ' || c == L'\t'; }

int parseArguments(int* argc, wchar_t*** argv, wchar_t*** environment) noexcept {
    if (!argc || !argv || !environment || !initializeProcessData()) return -1;
    auto** result = static_cast<wchar_t**>(malloc((MaxArguments + 1) * sizeof(wchar_t*)));
    if (!result) return -1;
    size_t count{};
    const wchar_t* cursor = g_commandLine;
    while (*cursor) {
        while (isSpace(*cursor)) ++cursor;
        if (!*cursor) break;
        if (count == MaxArguments) return -1;
        const size_t remaining = static_cast<size_t>(&g_commandLine[MaxCommandLine] - cursor);
        auto* argument = static_cast<wchar_t*>(malloc((remaining + 1) * sizeof(wchar_t)));
        if (!argument) return -1;
        size_t out{};
        bool quoted{};
        while (*cursor && (quoted || !isSpace(*cursor))) {
            size_t slashes{};
            while (*cursor == L'\\') { ++slashes; ++cursor; }
            if (*cursor == L'"') {
                for (size_t n = 0; n < slashes / 2; ++n) argument[out++] = L'\\';
                if (slashes & 1) { argument[out++] = L'"'; ++cursor; }
                else { quoted = !quoted; ++cursor; }
            } else {
                while (slashes--) argument[out++] = L'\\';
                if (*cursor && (quoted || !isSpace(*cursor))) argument[out++] = *cursor++;
            }
        }
        if (quoted) return -1;
        argument[out] = L'\0';
        result[count++] = argument;
    }
    if (!count) return -1;
    result[count] = nullptr;
    *argc = static_cast<int>(count);
    *argv = result;
    *environment = g_environmentVector;
    return 0;
}

bool appendWide(wchar_t* output, size_t limit, size_t& length, wchar_t value) noexcept {
    if (length >= limit) return false;
    output[length++] = value;
    return true;
}

bool appendUnsigned(wchar_t* output, size_t limit, size_t& length, uint64_t value,
                    unsigned radix, bool upper, size_t width, wchar_t pad) noexcept {
    wchar_t digits[65]{};
    size_t count{};
    do {
        const unsigned digit = static_cast<unsigned>(value % radix);
        digits[count++] = static_cast<wchar_t>(digit < 10 ? L'0' + digit :
            (upper ? L'A' : L'a') + digit - 10);
        value /= radix;
    } while (value && count < sizeof(digits) / sizeof(digits[0]));
    while (count < width) { if (!appendWide(output, limit, length, pad)) return false; --width; }
    while (count) if (!appendWide(output, limit, length, digits[--count])) return false;
    return true;
}

int formatWide(wchar_t* output, size_t count, const wchar_t* format, va_list args) noexcept {
    if ((!output && count) || !format) { *_errno() = ErrInvalid; return -1; }
    size_t length{};
    bool overflow{};
    const size_t writeLimit = count;
    auto put = [&](wchar_t c) {
        if (length < writeLimit) output[length] = c;
        else overflow = true;
        if (length != SIZE_MAX) ++length;
    };
    for (const wchar_t* p = format; *p; ++p) {
        if (*p != L'%') { put(*p); continue; }
        ++p;
        if (*p == L'%') { put(L'%'); continue; }
        bool left{}, plus{}, space{}, alternate{}, zero{};
        for (;; ++p) {
            if (*p == L'-') left = true;
            else if (*p == L'+') plus = true;
            else if (*p == L' ') space = true;
            else if (*p == L'#') alternate = true;
            else if (*p == L'0') zero = true;
            else break;
        }
        int width{};
        if (*p == L'*') { width = va_arg(args, int); ++p; if (width < 0) { left = true; width = -width; } }
        else while (*p >= L'0' && *p <= L'9') {
            if (width > 100000) { *_errno() = ErrInvalid; return -1; }
            width = width * 10 + static_cast<int>(*p++ - L'0');
        }
        int precision = -1;
        if (*p == L'.') {
            ++p; precision = 0;
            if (*p == L'*') { precision = va_arg(args, int); ++p; if (precision < 0) precision = -1; }
            else while (*p >= L'0' && *p <= L'9') {
                if (precision > 100000) { *_errno() = ErrInvalid; return -1; }
                precision = precision * 10 + static_cast<int>(*p++ - L'0');
            }
        }
        enum class Length { Default, Long, LongLong, Size } modifier{};
        if (*p == L'h') { ++p; if (*p == L'h') ++p; }
        else if (*p == L'l' || *p == L'w') { ++p; if (*p == L'l') { modifier = Length::LongLong; ++p; } else modifier = Length::Long; }
        else if (*p == L'I') { ++p; if (p[0] == L'6' && p[1] == L'4') { modifier = Length::LongLong; p += 2; } }
        else if (*p == L'z') { modifier = Length::Size; ++p; }
        if (!*p) { *_errno() = ErrInvalid; return -1; }
        wchar_t field[1024]{};
        size_t fieldLength{};
        bool negative{};
        bool numeric{};
        unsigned radix = 10;
        bool upper{};
        const wchar_t conversion = *p;
        if (conversion == L's' || conversion == L'S') {
            if (conversion == L's') {
                const wchar_t* value = va_arg(args, const wchar_t*);
                if (!value) value = L"(null)";
                while (value[fieldLength] && (precision < 0 || fieldLength < static_cast<size_t>(precision)) && fieldLength < 1023)
                    field[fieldLength] = value[fieldLength], ++fieldLength;
            } else {
                const char* value = va_arg(args, const char*);
                if (!value) value = "(null)";
                while (value[fieldLength] && (precision < 0 || fieldLength < static_cast<size_t>(precision)) && fieldLength < 1023)
                    field[fieldLength] = static_cast<unsigned char>(value[fieldLength]), ++fieldLength;
            }
        } else if (conversion == L'c' || conversion == L'C') {
            field[0] = static_cast<wchar_t>(va_arg(args, int)); fieldLength = 1;
        } else if (conversion == L'd' || conversion == L'i' || conversion == L'u' ||
                   conversion == L'o' || conversion == L'x' || conversion == L'X' || conversion == L'p') {
            numeric = true; radix = conversion == L'o' ? 8u : (conversion == L'x' || conversion == L'X' || conversion == L'p' ? 16u : 10u);
            upper = conversion == L'X';
            uint64_t value{};
            if (conversion == L'd' || conversion == L'i') {
                int64_t signedValue = modifier == Length::LongLong ? va_arg(args, int64_t) :
                    modifier == Length::Size ? static_cast<int64_t>(va_arg(args, ptrdiff_t)) : va_arg(args, int);
                negative = signedValue < 0;
                value = negative ? uint64_t{0} - static_cast<uint64_t>(signedValue) : static_cast<uint64_t>(signedValue);
            } else if (conversion == L'p') value = reinterpret_cast<uintptr_t>(va_arg(args, void*));
            else value = modifier == Length::LongLong ? va_arg(args, uint64_t) :
                modifier == Length::Size ? va_arg(args, size_t) : va_arg(args, unsigned int);
            wchar_t reverse[65]{}; size_t digits{};
            do { const unsigned d = static_cast<unsigned>(value % radix); reverse[digits++] = static_cast<wchar_t>(d < 10 ? L'0' + d : (upper ? L'A' : L'a') + d - 10); value /= radix; } while (value && digits < 65);
            size_t zeroes = precision > 0 && static_cast<size_t>(precision) > digits ? static_cast<size_t>(precision) - digits : 0;
            while (zeroes && fieldLength < 1023) field[fieldLength++] = L'0', --zeroes;
            while (digits && fieldLength < 1023) field[fieldLength++] = reverse[--digits];
            if (negative || plus || space || alternate || conversion == L'p') {
                size_t shift = negative || plus || space ? 1 : 0;
                if ((alternate && radix == 16) || conversion == L'p') shift += 2;
                if (fieldLength + shift > 1023) { *_errno() = ErrInvalid; return -1; }
                for (size_t n = fieldLength; n; --n) field[n - 1 + shift] = field[n - 1];
                size_t pos{};
                if (negative) field[pos++] = L'-'; else if (plus) field[pos++] = L'+'; else if (space) field[pos++] = L' ';
                if (shift - pos == 2) field[pos++] = L'0', field[pos++] = upper ? L'X' : L'x';
                fieldLength += shift;
            }
        } else {
            // Floating-point and unknown conversions are explicit errors until
            // the corresponding MSVCRT formatter contract is implemented.
            *_errno() = ErrInvalid;
            return -1;
        }
        size_t padding = width > 0 && static_cast<size_t>(width) > fieldLength ? static_cast<size_t>(width) - fieldLength : 0;
        const wchar_t pad = numeric && zero && !left && precision < 0 ? L'0' : L' ';
        if (!left) while (padding--) put(pad);
        for (size_t n = 0; n < fieldLength; ++n) put(field[n]);
        if (left) { padding = width > 0 && static_cast<size_t>(width) > fieldLength ? static_cast<size_t>(width) - fieldLength : 0; while (padding--) put(L' '); }
    }
    if (count && length < count) output[length] = L'\0';
    if (overflow || length > static_cast<size_t>(INT_MAX)) return -1;
    return static_cast<int>(length);
}
}

// CRT startup data exports are initialized during DLL_PROCESS_ATTACH, before
// application initializers can import and read them.
__declspec(dllexport) wchar_t* _wcmdln = g_commandLine;
__declspec(dllexport) wchar_t** __winitenv = g_environmentVector;

int __stdcall DllMainCRTStartup(void*, unsigned long, void*) noexcept {
    return initializeProcessData() ? 1 : 0;
}

__declspec(dllexport) void __cdecl _fpreset() noexcept {
    uint32_t mxcsr = 0x1f80;
    __asm__ __volatile__("fninit\n\tldmxcsr %0" : : "m"(mxcsr));
}
__declspec(dllexport) void __cdecl __set_app_type(int type) noexcept { g_applicationType = type; }
__declspec(dllexport) void __cdecl __setusermatherr(void (*handler)(void*)) noexcept { g_userMathError = handler; }

__declspec(dllexport) int __cdecl atexit(void (*function)(void)) noexcept {
    if (!function) return -1;
    lockExit();
    if (g_exitCount == MaxExitHandlers) { unlockExit(); return -1; }
    g_exitVoid[g_exitCount] = function; g_exitInt[g_exitCount] = nullptr; g_exitKind[g_exitCount++] = 0;
    unlockExit();
    return 0;
}
__declspec(dllexport) int (__cdecl* __cdecl _onexit(int (__cdecl* function)(void)))(void) {
    if (!function) return nullptr;
    lockExit();
    if (g_exitCount == MaxExitHandlers) { unlockExit(); return nullptr; }
    g_exitVoid[g_exitCount] = nullptr; g_exitInt[g_exitCount] = function; g_exitKind[g_exitCount++] = 1;
    unlockExit();
    return function;
}
static void runExitHandlers() noexcept {
    for (;;) {
        lockExit();
        if (!g_exitCount) { g_exitDispatching = false; unlockExit(); return; }
        const size_t index = --g_exitCount;
        auto voidFunction = g_exitVoid[index];
        auto intFunction = g_exitInt[index];
        const auto kind = g_exitKind[index];
        g_exitVoid[index] = nullptr; g_exitInt[index] = nullptr;
        unlockExit();
        if (kind == 0 && voidFunction) voidFunction();
        else if (kind == 1 && intFunction) (void)intFunction();
    }
}
__declspec(dllexport) void __cdecl _cexit() noexcept {
    lockExit();
    if (g_exitDispatching) { unlockExit(); return; }
    g_exitDispatching = true;
    unlockExit();
    runExitHandlers();
    (void)fflush(nullptr); // Flush only canonical UCRT streams; MSVCRT FILE* is not yet exposed.
}

__declspec(dllexport) [[noreturn]] void __cdecl exit(int status) noexcept {
    _cexit();
    (void)g_applicationType; (void)g_userMathError;
    (void)NtTerminateProcess(reinterpret_cast<void*>(~uintptr_t{}), status);
    __builtin_trap();
}
__declspec(dllexport) [[noreturn]] void __cdecl _amsg_exit(int status) noexcept { exit(status ? status : 255); }

__declspec(dllexport) void (*__cdecl signal(int number, void (*handler)(int)))(int) {
    const bool validSignal = number == 2 || number == 4 || number == 5 || number == 6 ||
        number == 8 || number == 9 || number == 11 || number == 15 || number == 21 ||
        number == 22;
    if (!validSignal || reinterpret_cast<uintptr_t>(handler) == UINTPTR_MAX) {
        *_errno() = ErrInvalid;
        return reinterpret_cast<void (*)(int)>(UINTPTR_MAX); // SIG_ERR
    }
    auto previous = __atomic_exchange_n(&g_signalHandlers[number], handler, __ATOMIC_ACQ_REL);
    return previous;
}

__declspec(dllexport) int __cdecl __wgetmainargs(int* argc, wchar_t*** argv,
    wchar_t*** environment, int doWildcard, void* startupInfo) noexcept {
    (void)startupInfo;
    if (doWildcard) return -1; // wildcard expansion remains explicitly unsupported
    return parseArguments(argc, argv, environment);
}

__declspec(dllexport) void __cdecl __lconv_init() noexcept { (void)localeconv(); }

__declspec(dllexport) char* __cdecl setlocale(int category, const char* locale) noexcept {
    if (category < 0 || category > 5) {
        *_errno() = ErrInvalid;
        return nullptr;
    }
    if (!locale) return g_cLocale;
    if (locale[0] == 'C' && locale[1] == '\0') return g_cLocale;
    *_errno() = ErrInvalid; // The canonical UCRT currently provides the C locale only.
    return nullptr;
}

__declspec(dllexport) int __cdecl _vsnwprintf(wchar_t* output, size_t count,
    const wchar_t* format, va_list arguments) noexcept {
    va_list copy;
    va_copy(copy, arguments);
    const int result = formatWide(output, count, format, copy);
    va_end(copy);
    return result;
}
}
