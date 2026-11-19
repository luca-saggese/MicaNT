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
__declspec(dllimport) int32_t __stdcall SetCurrentDirectoryW(const wchar_t*) noexcept;
__declspec(dllimport) uint32_t __stdcall GetCurrentDirectoryW(uint32_t, wchar_t*) noexcept;
__declspec(dllimport) uint32_t __stdcall GetLastError() noexcept;

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
uint32_t g_randState{1};
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

__declspec(dllexport) void* __cdecl bsearch(const void* key, const void* base,
    size_t count, size_t width, int (__cdecl* compare)(const void*, const void*)) noexcept {
    if (!key || (!base && count) || !width || !compare) { *_errno() = ErrInvalid; return nullptr; }
    const auto* bytes = static_cast<const unsigned char*>(base);
    size_t first = 0, last = count;
    while (first < last) {
        const size_t middle = first + (last - first) / 2;
        const int order = compare(key, bytes + middle * width);
        if (!order) return const_cast<unsigned char*>(bytes + middle * width);
        if (order < 0) last = middle; else first = middle + 1;
    }
    return nullptr;
}

__declspec(dllexport) void* __cdecl memchr(const void* memory, int value, size_t count) noexcept {
    if (!memory && count) { *_errno() = ErrInvalid; return nullptr; }
    const auto* bytes = static_cast<const unsigned char*>(memory);
    for (size_t i = 0; i < count; ++i) if (bytes[i] == static_cast<unsigned char>(value))
        return const_cast<unsigned char*>(bytes + i);
    return nullptr;
}

__declspec(dllexport) int __cdecl memcmp(const void* left, const void* right, size_t count) noexcept {
    const auto* a = static_cast<const unsigned char*>(left);
    const auto* b = static_cast<const unsigned char*>(right);
    if ((!a || !b) && count) { *_errno() = ErrInvalid; return 0; }
    for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

__declspec(dllexport) void* __cdecl memmove(void* destination, const void* source, size_t count) noexcept {
    if ((!destination || !source) && count) { *_errno() = ErrInvalid; return destination; }
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    if (out < in) for (size_t i = 0; i < count; ++i) out[i] = in[i];
    else if (out > in) for (size_t i = count; i; --i) out[i - 1] = in[i - 1];
    return destination;
}

__declspec(dllexport) size_t __cdecl strlen(const char* s) noexcept { if (!s) { *_errno() = ErrInvalid; return 0; } size_t n=0; while(s[n]) ++n; return n; }
__declspec(dllexport) int __cdecl strcmp(const char* a,const char* b) noexcept { if(!a||!b){*_errno()=ErrInvalid;return a?1:b?-1:0;} while(*a&&*a==*b){++a;++b;} return (unsigned char)*a-(unsigned char)*b; }
__declspec(dllexport) char* __cdecl strcpy(char* d,const char* s) noexcept { if(!d||!s){*_errno()=ErrInvalid;return d;} char* r=d; do{*d++=*s;}while(*s++); return r; }
__declspec(dllexport) const char* __cdecl strchr(const char* s,int c) noexcept { if(!s){*_errno()=ErrInvalid;return nullptr;} do{if((unsigned char)*s==(unsigned char)c)return s;}while(*s++);return nullptr; }
__declspec(dllexport) size_t __cdecl strcspn(const char* s,const char* reject) noexcept { if(!s||!reject){*_errno()=ErrInvalid;return 0;} size_t n=0;for(;s[n];++n)if(strchr(reject,(unsigned char)s[n]))break;return n; }
__declspec(dllexport) int __cdecl strncmp(const char* a,const char* b,size_t n) noexcept { if(!a||!b){*_errno()=ErrInvalid;return a?1:b?-1:0;}for(size_t i=0;i<n;++i){unsigned char x=a[i],y=b[i];if(x!=y)return x-y;if(!x)break;}return 0; }
__declspec(dllexport) char* __cdecl strncpy(char* d,const char* s,size_t n) noexcept { if(!d||!s){*_errno()=ErrInvalid;return d;}char* r=d;size_t i=0;for(;i<n&&s[i];++i)d[i]=s[i];for(;i<n;++i)d[i]=0;return r; }
__declspec(dllexport) char* __cdecl strcat(char* d,const char* s) noexcept { if(!d||!s){*_errno()=ErrInvalid;return d;}char* r=d;while(*d)++d;while((*d++=*s++));return r; }
__declspec(dllexport) wchar_t* __cdecl wcscpy(wchar_t* d,const wchar_t* s) noexcept { if(!d||!s){*_errno()=ErrInvalid;return d;}wchar_t* r=d;do{*d++=*s;}while(*s++);return r; }
__declspec(dllexport) wchar_t* __cdecl wcscat(wchar_t* d,const wchar_t* s) noexcept { if(!d||!s){*_errno()=ErrInvalid;return d;}wchar_t* r=d;while(*d)++d;while((*d++=*s++));return r; }
__declspec(dllexport) int __cdecl wcscmp(const wchar_t* a,const wchar_t* b) noexcept { if(!a||!b){*_errno()=ErrInvalid;return a?1:b?-1:0;}while(*a&&*a==*b){++a;++b;}return *a<*b?-1:*a>*b?1:0; }
__declspec(dllexport) size_t __cdecl wcslen(const wchar_t* s) noexcept { if(!s){*_errno()=ErrInvalid;return 0;}size_t n=0;while(s[n])++n;return n; }
__declspec(dllexport) const wchar_t* __cdecl wcschr(const wchar_t* s,wchar_t c) noexcept { if(!s){*_errno()=ErrInvalid;return nullptr;}do{if(*s==c)return s;}while(*s++);return nullptr; }
__declspec(dllexport) wchar_t* __cdecl wcsncat(wchar_t* d,const wchar_t* s,size_t n) noexcept { if(!d||!s){*_errno()=ErrInvalid;return d;}wchar_t* r=d;while(*d)++d;while(n--&&*s)*d++=*s++;*d=0;return r; }
__declspec(dllexport) int __cdecl wcsncmp(const wchar_t* a,const wchar_t* b,size_t n) noexcept { if(!a||!b){*_errno()=ErrInvalid;return a?1:b?-1:0;}for(size_t i=0;i<n;++i){if(a[i]!=b[i])return a[i]<b[i]?-1:1;if(!a[i])break;}return 0; }
__declspec(dllexport) wchar_t* __cdecl wcsncpy(wchar_t* d,const wchar_t* s,size_t n) noexcept { if(!d||!s){*_errno()=ErrInvalid;return d;}wchar_t* r=d;size_t i=0;for(;i<n&&s[i];++i)d[i]=s[i];for(;i<n;++i)d[i]=0;return r; }
__declspec(dllexport) size_t __cdecl wcscspn(const wchar_t* s,const wchar_t* reject) noexcept { if(!s||!reject){*_errno()=ErrInvalid;return 0;}size_t n=0;for(;s[n];++n)if(wcschr(reject,s[n]))break;return n; }
__declspec(dllexport) const wchar_t* __cdecl wcsrchr(const wchar_t* s,wchar_t c) noexcept { if(!s){*_errno()=ErrInvalid;return nullptr;}const wchar_t* found=nullptr;do{if(*s==c)found=s;}while(*s++);return found; }
__declspec(dllexport) size_t __cdecl wcsspn(const wchar_t* s,const wchar_t* accept) noexcept { if(!s||!accept){*_errno()=ErrInvalid;return 0;}size_t n=0;while(s[n]&&wcschr(accept,s[n]))++n;return n; }
__declspec(dllexport) const wchar_t* __cdecl wcspbrk(const wchar_t* s,const wchar_t* accept) noexcept { if(!s||!accept){*_errno()=ErrInvalid;return nullptr;}for(;*s;++s)if(wcschr(accept,*s))return s;return nullptr; }
__declspec(dllexport) const wchar_t* __cdecl wcsstr(const wchar_t* s,const wchar_t* sub) noexcept { if(!s||!sub){*_errno()=ErrInvalid;return nullptr;}if(!*sub)return s;for(;*s;++s){size_t i=0;while(sub[i]&&s[i]==sub[i])++i;if(!sub[i])return s;}return nullptr; }
__declspec(dllexport) wchar_t* __cdecl wcstok(wchar_t* s,const wchar_t* delim,wchar_t** context) noexcept { if(!delim||!context){*_errno()=ErrInvalid;return nullptr;}if(!s)s=*context;if(!s)return nullptr;while(*s&&wcschr(delim,*s))++s;if(!*s){*context=nullptr;return nullptr;}wchar_t* token=s;while(*s&&!wcschr(delim,*s))++s;if(*s)*s++=0;*context=s;return token; }

// With no SEH scope-table ABI in the current runtime, this handler correctly
// declines dispatch so the native exception dispatcher can continue searching.
// It never claims an exception was handled or resumed.
__declspec(dllexport) int __cdecl __C_specific_handler(void*, void*, void*, void*) noexcept { return 0; }

__declspec(dllexport) int __cdecl _vsnprintf(char* output, size_t capacity,
    const char* format, va_list arguments) noexcept {
    if ((!output && capacity) || !format) { *_errno() = ErrInvalid; return -1; }
    size_t written = 0; bool overflow = false;
    auto put = [&](char c) { if (written < capacity) output[written] = c; else overflow = true; ++written; };
    va_list args; va_copy(args, arguments);
    for (const char* p = format; *p; ++p) {
        if (*p != '%') { put(*p); continue; }
        ++p; if (*p == '%') { put('%'); continue; }
        if (*p == 's') { const char* s = va_arg(args, const char*); if (!s) s = "(null)"; while (*s) put(*s++); }
        else if (*p == 'c') put(static_cast<char>(va_arg(args, int)));
        else if (*p == 'd' || *p == 'i') { int v = va_arg(args, int); unsigned u = v < 0 ? 0u - static_cast<unsigned>(v) : static_cast<unsigned>(v); if (v < 0) put('-'); char b[16]; size_t n=0; do{b[n++]=static_cast<char>('0'+u%10);u/=10;}while(u);while(n)put(b[--n]); }
        else if (*p == 'u' || *p == 'x' || *p == 'X') { unsigned v=va_arg(args,unsigned); unsigned radix=*p=='u'?10:16; char b[16];size_t n=0;do{unsigned d=v%radix;b[n++]=static_cast<char>(d<10?'0'+d:(*p=='X'?'A':'a')+d-10);v/=radix;}while(v);while(n)put(b[--n]); }
        else { va_end(args); *_errno() = ErrInvalid; return -1; }
    }
    va_end(args);
    if (capacity && written < capacity) output[written] = 0;
    if (overflow || written > INT_MAX) return -1;
    return static_cast<int>(written);
}
__declspec(dllexport) int __cdecl _snprintf(char* output, size_t capacity,
    const char* format, ...) noexcept {
    va_list args; va_start(args, format); const int result = _vsnprintf(output, capacity, format, args); va_end(args); return result;
}
__declspec(dllexport) int __cdecl sprintf(char* output,const char* format,...) noexcept {
    if(!output){*_errno()=ErrInvalid;return -1;}
    va_list args;va_start(args,format);const int result=_vsnprintf(output,SIZE_MAX,format,args);va_end(args);return result;
}
__declspec(dllexport) int __cdecl _snwprintf(wchar_t* output, size_t capacity,
    const wchar_t* format, ...) noexcept {
    va_list args; va_start(args, format); const int result = formatWide(output, capacity, format, args); va_end(args); return result;
}
__declspec(dllexport) int __cdecl vswprintf(wchar_t* output, size_t capacity,
    const wchar_t* format, va_list args) noexcept { return _vsnwprintf(output, capacity, format, args); }
__declspec(dllexport) int __cdecl swprintf(wchar_t* output, const wchar_t* format, ...) noexcept {
    va_list args; va_start(args, format); const int result = formatWide(output, SIZE_MAX, format, args); va_end(args); return result;
}
__declspec(dllexport) int __cdecl _swprintf(wchar_t* output, const wchar_t* format, ...) noexcept {
    va_list args; va_start(args, format); const int result = formatWide(output, SIZE_MAX, format, args); va_end(args); return result;
}
__declspec(dllexport) int __cdecl _vswprintf(wchar_t* output, const wchar_t* format, va_list args) noexcept {
    return formatWide(output, SIZE_MAX, format, args);
}

__declspec(dllexport) int __cdecl isalpha(int c) noexcept { unsigned char x=static_cast<unsigned char>(c); return (x>='A'&&x<='Z')||(x>='a'&&x<='z'); }
__declspec(dllexport) int __cdecl isalnum(int c) noexcept { unsigned char x=static_cast<unsigned char>(c); return isalpha(x)||(x>='0'&&x<='9'); }
__declspec(dllexport) wchar_t __cdecl towlower(wchar_t c) noexcept { return c>=L'A'&&c<=L'Z'?c+(L'a'-L'A'):c; }
__declspec(dllexport) wchar_t __cdecl towupper(wchar_t c) noexcept { return c>=L'a'&&c<=L'z'?c-(L'a'-L'A'):c; }
__declspec(dllexport) int __cdecl iswctype(wchar_t c,unsigned short type) noexcept {
    constexpr unsigned short upper=1,lower=2,digit=4,space=8,punct=16,control=32,blank=64,xdigit=128,alpha=256;
    const bool u=c>=L'A'&&c<=L'Z',l=c>=L'a'&&c<=L'z',d=c>=L'0'&&c<=L'9';
    const bool ws=c==L' '||c==L'\t'||c==L'\n'||c==L'\r'||c==L'\v'||c==L'\f';
    const bool ctl=c<32||(c>=127&&c<=159),pun=!u&&!l&&!d&&!ws&&!ctl;
    return ((type&upper)&&u)||((type&lower)&&l)||((type&digit)&&d)||((type&space)&&ws)||((type&blank)&&(c==L' '||c==L'\t'))||((type&control)&&ctl)||((type&punct)&&pun)||((type&alpha)&&(u||l))||((type&xdigit)&&(d||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F')));
}
__declspec(dllexport) int __cdecl iswalpha(wchar_t c) noexcept {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}
__declspec(dllexport) int __cdecl iswdigit(wchar_t c) noexcept {
    return c >= L'0' && c <= L'9';
}
__declspec(dllexport) int __cdecl iswspace(wchar_t c) noexcept {
    return c == L' ' || (c >= L'\t' && c <= L'\r');
}
__declspec(dllexport) int __cdecl iswcntrl(wchar_t c) noexcept {
    return c < 0x20 || (c >= 0x7f && c <= 0x9f);
}
__declspec(dllexport) int __cdecl iswprint(wchar_t c) noexcept {
    return c >= 0x20 && !(c >= 0x7f && c <= 0x9f);
}
__declspec(dllexport) int __cdecl __iscsymf(int c) noexcept {
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
__declspec(dllexport) int __cdecl __iscsym(int c) noexcept {
    return __iscsymf(c) || (c >= '0' && c <= '9');
}
__declspec(dllexport) wchar_t* __cdecl _wcstok(wchar_t* text, const wchar_t* delimiters) noexcept {
    static wchar_t* next{};
    return wcstok(text, delimiters, &next);
}
__declspec(dllexport) int __cdecl _wcsicmp(const wchar_t* a,const wchar_t* b) noexcept { if(!a||!b){*_errno()=ErrInvalid;return a?1:b?-1:0;}for(;;++a,++b){wchar_t x=towlower(*a),y=towlower(*b);if(x!=y)return x<y?-1:1;if(!x)return 0;} }
__declspec(dllexport) int __cdecl _wcsnicmp(const wchar_t* a,const wchar_t* b,size_t n) noexcept { if(!a||!b){*_errno()=ErrInvalid;return a?1:b?-1:0;}for(size_t i=0;i<n;++i){wchar_t x=towlower(a[i]),y=towlower(b[i]);if(x!=y)return x<y?-1:1;if(!x)return 0;}return 0; }
__declspec(dllexport) wchar_t* __cdecl _wcsupr(wchar_t* s) noexcept { if(!s){*_errno()=ErrInvalid;return nullptr;}for(auto* p=s;*p;++p)*p=towupper(*p);return s; }
__declspec(dllexport) wchar_t* __cdecl _wcslwr(wchar_t* s) noexcept { if(!s){*_errno()=ErrInvalid;return nullptr;}for(auto* p=s;*p;++p)*p=towlower(*p);return s; }
__declspec(dllexport) char* __cdecl _strupr(char* s) noexcept {
    if (!s) { *_errno() = ErrInvalid; return nullptr; }
    for (auto* p = reinterpret_cast<unsigned char*>(s); *p; ++p)
        if (*p >= 'a' && *p <= 'z') *p = static_cast<unsigned char>(*p - ('a' - 'A'));
    return s;
}
__declspec(dllexport) char* __cdecl _strdup(const char* s) noexcept {
    if (!s) { *_errno() = ErrInvalid; return nullptr; }
    size_t length = 0;
    while (s[length]) {
        if (length == SIZE_MAX - 1) { *_errno() = ErrNoMemory; return nullptr; }
        ++length;
    }
    auto* copy = static_cast<char*>(malloc(length + 1));
    if (!copy) return nullptr;
    for (size_t i = 0; i <= length; ++i) copy[i] = s[i];
    return copy;
}
__declspec(dllexport) int __cdecl swscanf(const wchar_t* input,
    const wchar_t* format, ...) noexcept {
    if (!input || !format) { *_errno() = ErrInvalid; return -1; }
    auto white = [](wchar_t c) noexcept {
        return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' ||
               c == L'\v' || c == L'\f';
    };
    va_list args;
    va_start(args, format);
    int assigned = 0;
    const wchar_t* in = input;
    const wchar_t* f = format;
    bool inputFailure = false;
    while (*f) {
        if (white(*f)) {
            while (white(*f)) ++f;
            while (white(*in)) ++in;
            continue;
        }
        if (*f != L'%') {
            if (*in != *f) { inputFailure = *in == 0; break; }
            ++in; ++f; continue;
        }
        ++f;
        if (*f == L'%') {
            if (*in != L'%') { inputFailure = *in == 0; break; }
            ++in; ++f; continue;
        }
        bool suppress = false;
        if (*f == L'*') { suppress = true; ++f; }
        size_t width = SIZE_MAX;
        if (*f >= L'0' && *f <= L'9') {
            width = 0;
            while (*f >= L'0' && *f <= L'9') {
                const unsigned digit = static_cast<unsigned>(*f++ - L'0');
                if (width > (SIZE_MAX - digit) / 10) width = SIZE_MAX;
                else width = width * 10 + digit;
            }
            if (!width) { *_errno() = ErrInvalid; break; }
        }
        enum class Length { None, Short, Long, LongLong, DoubleLong } length{};
        if (f[0] == L'I' && f[1] == L'6' && f[2] == L'4') {
            length = Length::LongLong; f += 3;
        } else if (*f == L'h') { length = Length::Short; ++f; if (*f == L'h') ++f; }
        else if (*f == L'l') { length = Length::Long; ++f; if (*f == L'l') length = Length::LongLong, ++f; }
        else if (*f == L'L') { length = Length::DoubleLong; ++f; }
        const wchar_t spec = *f++;
        if (spec != L'c' && spec != L'[' && spec != L'n')
            while (white(*in)) ++in;
        size_t consumed = 0;
        if (spec == L'd' || spec == L'i' || spec == L'u' || spec == L'o' ||
            spec == L'x' || spec == L'X') {
            const wchar_t* p = in;
            bool negative = false;
            if (consumed < width && (*p == L'+' || *p == L'-')) {
                negative = *p == L'-'; ++p; ++consumed;
            }
            unsigned base = spec == L'o' ? 8u :
                (spec == L'x' || spec == L'X') ? 16u : spec == L'i' ? 0u : 10u;
            if (consumed + 2 < width && p[0] == L'0' &&
                (p[1] == L'x' || p[1] == L'X')) { if (!base) base = 16; p += 2; consumed += 2; }
            if (!base) base = *p == L'0' ? 8u : 10u;
            uint64_t value = 0; size_t digits = 0;
            while (consumed < width) {
                const wchar_t c = *p;
                const unsigned d = c >= L'0' && c <= L'9' ? static_cast<unsigned>(c - L'0') :
                    c >= L'a' && c <= L'z' ? static_cast<unsigned>(c - L'a' + 10) :
                    c >= L'A' && c <= L'Z' ? static_cast<unsigned>(c - L'A' + 10) : UINT_MAX;
                if (d >= base) break;
                value = value * base + d; ++p; ++consumed; ++digits;
            }
            if (!digits) { inputFailure = *in == 0; break; }
            if (!suppress) {
                void* destination = va_arg(args, void*);
                if (!destination) { *_errno() = ErrInvalid; break; }
                const bool signedValue = spec == L'd' || spec == L'i';
                if (length == Length::LongLong) {
                    if (signedValue) *static_cast<int64_t*>(destination) = negative
                        ? static_cast<int64_t>(0u - value) : static_cast<int64_t>(value);
                    else *static_cast<uint64_t*>(destination) = negative ? 0u - value : value;
                } else if (length == Length::Long) {
                    if (signedValue) *static_cast<int32_t*>(destination) = negative
                        ? static_cast<int32_t>(0u - value) : static_cast<int32_t>(value);
                    else *static_cast<uint32_t*>(destination) = negative ? 0u - value : value;
                } else if (length == Length::Short) {
                    if (signedValue) *static_cast<int16_t*>(destination) = negative
                        ? static_cast<int16_t>(0u - value) : static_cast<int16_t>(value);
                    else *static_cast<uint16_t*>(destination) = static_cast<uint16_t>(negative ? 0u - value : value);
                } else {
                    if (signedValue) *static_cast<int*>(destination) = negative
                        ? static_cast<int>(0u - value) : static_cast<int>(value);
                    else *static_cast<unsigned*>(destination) = static_cast<unsigned>(negative ? 0u - value : value);
                }
                ++assigned;
            }
            in = p;
        } else if (spec == L'f' || spec == L'e' || spec == L'E' || spec == L'g' || spec == L'G' || spec == L'a' || spec == L'A') {
            const wchar_t* p = in; bool negative = false;
            if (consumed < width && (*p == L'+' || *p == L'-')) { negative = *p == L'-'; ++p; ++consumed; }
            double value = 0.0; bool any = false;
            while (consumed < width && *p >= L'0' && *p <= L'9') { value = value * 10.0 + (*p++ - L'0'); ++consumed; any = true; }
            if (consumed < width && *p == L'.') {
                ++p; ++consumed; double place = 0.1;
                while (consumed < width && *p >= L'0' && *p <= L'9') { value += (*p++ - L'0') * place; place *= 0.1; ++consumed; any = true; }
            }
            int exponent = 0; bool exponentNegative = false;
            if (any && consumed < width && (*p == L'e' || *p == L'E')) {
                const wchar_t* expStart = p; const size_t expConsumed = consumed;
                ++p; ++consumed;
                if (consumed < width && (*p == L'+' || *p == L'-')) { exponentNegative = *p == L'-'; ++p; ++consumed; }
                bool expDigits = false;
                while (consumed < width && *p >= L'0' && *p <= L'9') { if (exponent < 400) exponent = exponent * 10 + (*p - L'0'); ++p; ++consumed; expDigits = true; }
                if (!expDigits) { p = expStart; consumed = expConsumed; }
            }
            if (!any) { inputFailure = *in == 0; break; }
            for (int i = 0; i < exponent && i < 400; ++i) value = exponentNegative ? value / 10.0 : value * 10.0;
            if (negative) value = -value;
            if (!suppress) {
                void* destination = va_arg(args, void*);
                if (!destination) { *_errno() = ErrInvalid; break; }
                if (length == Length::Long || length == Length::LongLong || length == Length::DoubleLong)
                    *static_cast<double*>(destination) = value;
                else *static_cast<float*>(destination) = static_cast<float>(value);
                ++assigned;
            }
            in = p;
        } else if (spec == L'c' || spec == L's') {
            if (width == SIZE_MAX) width = spec == L'c' ? 1 : SIZE_MAX;
            wchar_t* destination = suppress ? nullptr : va_arg(args, wchar_t*);
            if (!suppress && !destination) { *_errno() = ErrInvalid; break; }
            while (consumed < width && *in && (spec == L'c' || !white(*in))) {
                if (destination) destination[consumed] = *in;
                ++in; ++consumed;
            }
            if (!consumed) { inputFailure = *in == 0; break; }
            if (destination) { if (spec == L's') destination[consumed] = 0; ++assigned; }
        } else if (spec == L'n') {
            if (!suppress) *va_arg(args, int*) = static_cast<int>(in - input);
        } else {
            *_errno() = ErrInvalid;
            break;
        }
    }
    va_end(args);
    return assigned ? assigned : inputFailure ? -1 : 0;
}
__declspec(dllexport) wchar_t* __cdecl _wcsnset(wchar_t* s,wchar_t c,size_t n) noexcept { if(!s){*_errno()=ErrInvalid;return nullptr;}size_t i=0;for(;i<n&&s[i];++i)s[i]=c;return s; }
__declspec(dllexport) wchar_t* __cdecl _wcsdup(const wchar_t* s) noexcept { if(!s){*_errno()=ErrInvalid;return nullptr;}size_t n=wcslen(s);if(n>(SIZE_MAX/sizeof(wchar_t))-1){*_errno()=ErrNoMemory;return nullptr;}auto* p=static_cast<wchar_t*>(malloc((n+1)*sizeof(wchar_t)));if(!p)return nullptr;for(size_t i=0;i<=n;++i)p[i]=s[i];return p; }
__declspec(dllexport) int __cdecl _wtoi(const wchar_t* s) noexcept { if(!s){*_errno()=ErrInvalid;return 0;}while(*s==L' '||*s==L'\t')++s;bool neg=*s==L'-';if(*s==L'-'||*s==L'+')++s;int v=0;while(*s>=L'0'&&*s<=L'9'){if(v>(INT_MAX-9)/10){*_errno()=34;return neg?INT_MIN:INT_MAX;}v=v*10+(*s++-L'0');}return neg?-v:v; }
__declspec(dllexport) long __cdecl _wtol(const wchar_t* s) noexcept { return static_cast<long>(_wtoi(s)); }
__declspec(dllexport) unsigned long __cdecl wcstoul(const wchar_t* s,wchar_t** end,int radix) noexcept { if(!s||radix<0||radix==1||radix>36){*_errno()=ErrInvalid;return 0;}while(*s==L' '||*s==L'\t')++s;bool neg=*s==L'-';if(*s==L'-'||*s==L'+')++s;if(!radix)radix=10;if(radix==16&&s[0]==L'0'&&(s[1]==L'x'||s[1]==L'X'))s+=2;unsigned long v=0;const wchar_t* start=s;for(;;++s){int d=*s>=L'0'&&*s<=L'9'?*s-L'0':*s>=L'a'&&*s<=L'z'?*s-L'a'+10:*s>=L'A'&&*s<=L'Z'?*s-L'A'+10:-1;if(d<0||d>=radix)break;v=v*radix+static_cast<unsigned>(d);}if(end)*end=const_cast<wchar_t*>(s);if(s==start)return 0;return neg?0ul-v:v; }
__declspec(dllexport) long __cdecl wcstol(const wchar_t* s,wchar_t** end,int radix) noexcept { return static_cast<long>(wcstoul(s,end,radix)); }
__declspec(dllexport) wchar_t* __cdecl _itow(int value,wchar_t* out,int radix) noexcept { if(!out||radix<2||radix>36){*_errno()=ErrInvalid;return nullptr;}bool neg=value<0&&radix==10;unsigned v=neg?0u-static_cast<unsigned>(value):static_cast<unsigned>(value);wchar_t b[34];size_t n=0;do{unsigned d=v%radix;b[n++]=static_cast<wchar_t>(d<10?L'0'+d:L'a'+d-10);v/=radix;}while(v);size_t i=0;if(neg)out[i++]=L'-';while(n)out[i++]=b[--n];out[i]=0;return out; }
__declspec(dllexport) int __cdecl _getdrive() noexcept { return 3; }
__declspec(dllexport) int __cdecl _wchdir(const wchar_t* path) noexcept {
    if (!path || !*path) { *_errno() = ErrInvalid; return -1; }
    if (SetCurrentDirectoryW(path)) return 0;
    const auto error = GetLastError();
    *_errno() = error == 2 || error == 3 ? 2 : error == 5 ? 13 : error == 8 ? ErrNoMemory : ErrInvalid;
    return -1;
}
__declspec(dllexport) wchar_t* __cdecl _wgetdcwd(int drive, wchar_t* buffer, int maxLength) noexcept {
    if (drive != 0 && drive != 3) { *_errno() = 15; return nullptr; }
    if (maxLength <= 0) { *_errno() = ErrInvalid; return nullptr; }
    const bool allocated = buffer == nullptr;
    if (allocated) {
        buffer = static_cast<wchar_t*>(malloc(static_cast<size_t>(maxLength) * sizeof(wchar_t)));
        if (!buffer) return nullptr;
    }
    const auto length = GetCurrentDirectoryW(static_cast<uint32_t>(maxLength), buffer);
    if (!length || length >= static_cast<uint32_t>(maxLength)) {
        if (allocated) free(buffer);
        *_errno() = length ? 34 : ErrInvalid;
        return nullptr;
    }
    return buffer;
}
__declspec(dllexport) void __cdecl qsort(void* base,size_t count,size_t width,int (__cdecl* compare)(const void*,const void*)) noexcept { if((!base&&count)||!width||!compare){*_errno()=ErrInvalid;return;}auto* b=static_cast<unsigned char*>(base);for(size_t i=1;i<count;++i){size_t j=i;while(j&&compare(b+(j-1)*width,b+j*width)>0){for(size_t k=0;k<width;++k){unsigned char x=b[(j-1)*width+k];b[(j-1)*width+k]=b[j*width+k];b[j*width+k]=x;}--j;}} }
__declspec(dllexport) void __cdecl srand(unsigned seed) noexcept { g_randState=seed; }
__declspec(dllexport) int __cdecl rand() noexcept { g_randState=g_randState*214013u+2531011u;return static_cast<int>((g_randState>>16)&0x7fff); }
// MSVCRT FILE layout/locking and the CRT pipe-to-child lifecycle are not yet
// backed by the native I/O/process contracts. Fail these operations explicitly
// instead of manufacturing a FILE* or reporting successful close/pipe creation.
struct MsvcrtFile;
__declspec(dllexport) MsvcrtFile* __cdecl _wfopen(const wchar_t*,const wchar_t*) noexcept { *_errno()=38;return nullptr; }
__declspec(dllexport) wchar_t* __cdecl fgetws(wchar_t*,int,MsvcrtFile*) noexcept { *_errno()=38;return nullptr; }
__declspec(dllexport) int __cdecl fclose(MsvcrtFile*) noexcept { *_errno()=38;return -1; }
__declspec(dllexport) MsvcrtFile* __cdecl _wpopen(const wchar_t*,const wchar_t*) noexcept { *_errno()=38;return nullptr; }
__declspec(dllexport) int __cdecl _pclose(MsvcrtFile*) noexcept { *_errno()=38;return -1; }

namespace {
uint64_t doubleBits(double value) noexcept { uint64_t bits; __builtin_memcpy(&bits, &value, sizeof(bits)); return bits; }
double fromBits(uint64_t bits) noexcept { double value; __builtin_memcpy(&value, &bits, sizeof(value)); return value; }
bool isNanValue(double value) noexcept { const uint64_t b=doubleBits(value); return (b&0x7ff0000000000000ULL)==0x7ff0000000000000ULL && (b&0xfffffffffffffULL)!=0; }
bool isFiniteValue(double value) noexcept { return (doubleBits(value)&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL; }
double x87Atan(double x) noexcept { double r; __asm__ __volatile__("fldl %1; fld1; fpatan; fstpl %0":"=m"(r):"m"(x):"st"); return r; }
double x87Atan2(double y,double x) noexcept { double r; __asm__ __volatile__("fldl %1; fldl %2; fpatan; fstpl %0":"=m"(r):"m"(x),"m"(y):"st"); return r; }
double x87Sqrt(double x) noexcept { double r; __asm__ __volatile__("fldl %1; fsqrt; fstpl %0":"=m"(r):"m"(x):"st"); return r; }
double x87Sin(double x) noexcept { double r; __asm__ __volatile__("fldl %1; fsin; fstpl %0":"=m"(r):"m"(x):"st"); return r; }
double x87Cos(double x) noexcept { double r; __asm__ __volatile__("fldl %1; fcos; fstpl %0":"=m"(r):"m"(x):"st"); return r; }
double x87Exp(double x) noexcept {
    if (x > 709.782712893384) return fromBits(0x7ff0000000000000ULL);
    if (x < -745.133219101941) return 0.0;
    double r;
    __asm__ __volatile__("fldl %1; fldl2e; fmulp %%st, %%st(1); fld %%st(0); frndint; fxch %%st(1); fsub %%st(1), %%st(0); f2xm1; fld1; faddp; fscale; fstp %%st(1); fstpl %0":"=m"(r):"m"(x):"st");
    return r;
}
double x87Log(double x) noexcept { double r; __asm__ __volatile__("fldln2; fldl %1; fyl2x; fstpl %0":"=m"(r):"m"(x):"st"); return r; }
double x87Fmod(double x,double y) noexcept {
    if (!isFiniteValue(x) || !isFiniteValue(y) || y==0.0) return fromBits(0x7ff8000000000000ULL);
    double r;
    __asm__ __volatile__("fldl %1; fldl %2; 1: fprem; fnstsw %%ax; test $0x0400, %%ah; jnz 1b; fstp %%st(1); fstpl %0":"=m"(r):"m"(y),"m"(x):"ax","st");
    return r;
}
double x87Modf(double x,double* integral) noexcept {
    if (!isFiniteValue(x)) { if(integral)*integral=x; return fromBits(doubleBits(x)&0x8000000000000000ULL); }
    uint64_t bits=doubleBits(x); unsigned e=static_cast<unsigned>((bits>>52)&0x7ff);
    if(e<1023) { if(integral)*integral=fromBits(bits&0x8000000000000000ULL); return x; }
    if(e>=1075) { if(integral)*integral=x; return fromBits(bits&0x8000000000000000ULL); }
    const unsigned fractional=1075-e; const uint64_t mask=(uint64_t{1}<<fractional)-1;
    const double whole=fromBits(bits&~mask); if(integral)*integral=whole; return x-whole;
}
}
// MSVCRT's assertion helper is a process-terminating failure path. ReactOS
// Notepad imports it for its standard assert macro; do not let a failed
// assertion return into the application as if it succeeded.
[[noreturn]] __declspec(dllexport) void __cdecl _assert(
    const char* expression, const char* filename, unsigned line) noexcept {
    (void)expression;
    (void)filename;
    (void)line;
    (void)NtTerminateProcess(nullptr, static_cast<int32_t>(0xC0000420u));
    for (;;) __asm__ __volatile__("pause");
}
__declspec(dllexport) int __cdecl _isnan(double x) noexcept { return isNanValue(x)?1:0; }
__declspec(dllexport) int __cdecl _finite(double x) noexcept { return isFiniteValue(x)?1:0; }
__declspec(dllexport) double __cdecl atan(double x) noexcept { return x87Atan(x); }
__declspec(dllexport) double __cdecl asin(double x) noexcept { if(x>1.0||x< -1.0){*_errno()=33;return fromBits(0x7ff8000000000000ULL);} return x87Atan(x/x87Sqrt((1.0-x)*(1.0+x))); }
__declspec(dllexport) double __cdecl acos(double x) noexcept { if(x>1.0||x< -1.0){*_errno()=33;return fromBits(0x7ff8000000000000ULL);} return x87Atan2(x87Sqrt((1.0-x)*(1.0+x)),x); }
__declspec(dllexport) double __cdecl modf(double x,double* integral) noexcept { return x87Modf(x,integral); }
__declspec(dllexport) double __cdecl ceil(double x) noexcept { double i; const double f=x87Modf(x,&i); return f>0.0?i+1.0:i; }
__declspec(dllexport) double __cdecl fabs(double x) noexcept { return fromBits(doubleBits(x)&0x7fffffffffffffffULL); }
__declspec(dllexport) double __cdecl fmod(double x,double y) noexcept { if(y==0.0||!isFiniteValue(x)){*_errno()=33;return fromBits(0x7ff8000000000000ULL);} return x87Fmod(x,y); }
__declspec(dllexport) double __cdecl exp(double x) noexcept { return x87Exp(x); }
__declspec(dllexport) double __cdecl log(double x) noexcept { if(x<0.0){*_errno()=33;return fromBits(0x7ff8000000000000ULL);} if(x==0.0){*_errno()=34;return fromBits(0xfff0000000000000ULL);} return x87Log(x); }
__declspec(dllexport) double __cdecl log10(double x) noexcept { return log(x)*0.43429448190325182765; }
__declspec(dllexport) double __cdecl tan(double x) noexcept { return x87Sin(x)/x87Cos(x); }
__declspec(dllexport) double __cdecl cosh(double x) noexcept { const double a=x87Exp(x),b=x87Exp(-x);return (a+b)*0.5; }
__declspec(dllexport) double __cdecl sinh(double x) noexcept { const double a=x87Exp(x),b=x87Exp(-x);return (a-b)*0.5; }
__declspec(dllexport) double __cdecl tanh(double x) noexcept { if(x>20.0)return 1.0;if(x< -20.0)return -1.0;const double a=x87Exp(2.0*x);return (a-1.0)/(a+1.0); }
__declspec(dllexport) double __cdecl sin(double x) noexcept { return x87Sin(x); }
__declspec(dllexport) double __cdecl cos(double x) noexcept { return x87Cos(x); }
__declspec(dllexport) double __cdecl sqrt(double x) noexcept { if(x<0.0){*_errno()=33;return fromBits(0x7ff8000000000000ULL);}return x87Sqrt(x); }
__declspec(dllexport) double __cdecl pow(double x,double y) noexcept {
    if(y==0.0)return 1.0;
    if(x==0.0)return y>0.0?x:fromBits(0x7ff0000000000000ULL);
    if(x<0.0){const double yi=x87Modf(y,nullptr);if(yi!=y){*_errno()=33;return fromBits(0x7ff8000000000000ULL);}const double magnitude=x87Exp(y*x87Log(-x));return (static_cast<int64_t>(y)&1)?-magnitude:magnitude;}
    return x87Exp(y*x87Log(x));
}
}
