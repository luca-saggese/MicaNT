// Generic MSVCRT import/ABI probe. It exercises the system DLL by ordinary PE
// imports and exits through its exported CRT termination path.
#include <cstddef>
#include <cstdint>
#include <cstdarg>

extern "C" {
__declspec(dllimport) void* __cdecl malloc(size_t) noexcept;
__declspec(dllimport) void* __cdecl realloc(void*, size_t) noexcept;
__declspec(dllimport) void __cdecl free(void*) noexcept;
__declspec(dllimport) int* __cdecl _errno() noexcept;
__declspec(dllimport) char* __cdecl setlocale(int, const char*) noexcept;
__declspec(dllimport) void* __cdecl localeconv() noexcept;
__declspec(dllimport) int __cdecl _vsnwprintf(wchar_t*, size_t, const wchar_t*, va_list) noexcept;
__declspec(dllimport) int __cdecl __wgetmainargs(int*, wchar_t***, wchar_t***, int, void*) noexcept;
__declspec(dllimport) void __cdecl _initterm(void (**)(void), void (**)(void)) noexcept;
__declspec(dllimport) int __cdecl _initterm_e(int (**)(void), int (**)(void)) noexcept;
__declspec(dllimport) int __cdecl atexit(void (*)(void)) noexcept;
__declspec(dllimport) void __cdecl _cexit() noexcept;
__declspec(dllimport) void __cdecl _fpreset() noexcept;
__declspec(dllimport) void (__cdecl* __cdecl signal(int, void (__cdecl*)(int)))(int);
__declspec(dllimport) [[noreturn]] void __cdecl exit(int) noexcept;
__declspec(dllimport) extern wchar_t* _wcmdln;
__declspec(dllimport) extern wchar_t** __winitenv;
}

namespace {
struct ProbeLconv {
    char* decimalPoint; char* thousandsSeparator; char* grouping;
    char* internationalCurrency; char* currency; char* monetaryDecimalPoint;
    char* monetaryThousandsSeparator; char* monetaryGrouping;
    char* positiveSign; char* negativeSign;
    char internationalFractionDigits; char fractionDigits;
    char positiveCurrencyPrecedes; char positiveSpaceSeparation;
    char negativeCurrencyPrecedes; char negativeSpaceSeparation;
    char positiveSignPosition; char negativeSignPosition;
};
int initState{};
int exitOrder{};

void initOne() { initState = initState * 10 + 1; }
void initTwo() { initState = initState * 10 + 2; }
int initStatus() { initState = initState * 10 + 3; return 0; }
void exitOne() { exitOrder = exitOrder * 10 + 1; }
void exitTwo() { exitOrder = exitOrder * 10 + 2; }
void signalHandler(int) {}

int format(wchar_t* output, size_t count, const wchar_t* pattern, ...) {
    va_list args;
    va_start(args, pattern);
    const int result = _vsnwprintf(output, count, pattern, args);
    va_end(args);
    return result;
}

bool equal(const wchar_t* left, const wchar_t* right) {
    size_t i{};
    while (left[i] && left[i] == right[i]) ++i;
    return left[i] == right[i];
}

int runProbe() {
    if (!_wcmdln || !*_wcmdln || !__winitenv || !*__winitenv) return 1;
    int argc{}; wchar_t** argv{}; wchar_t** environment{};
    if (__wgetmainargs(&argc, &argv, &environment, 0, nullptr) != 0 ||
        argc < 1 || !argv || !argv[0] || !environment) return 2;
    for (int i = 0; i < argc; ++i) free(argv[i]);
    free(argv);

    auto* memory = static_cast<uint8_t*>(malloc(16));
    if (!memory) return 3;
    for (size_t i = 0; i < 16; ++i) memory[i] = static_cast<uint8_t>(i ^ 0xA5u);
    auto* grown = static_cast<uint8_t*>(realloc(memory, 64));
    if (!grown) { free(memory); return 4; }
    for (size_t i = 0; i < 16; ++i)
        if (grown[i] != static_cast<uint8_t>(i ^ 0xA5u)) { free(grown); return 5; }
    free(grown);

    int* error = _errno();
    if (!error) return 6;
    *error = 73;
    if (*_errno() != 73) return 7;
    const char* locale = setlocale(0, nullptr);
    if (!locale || locale[0] != 'C' || locale[1]) return 8;
    if (!setlocale(0, "C")) return 9;
    auto* numeric = static_cast<ProbeLconv*>(localeconv());
    if (!numeric || !numeric->decimalPoint || numeric->decimalPoint[0] != '.' ||
        numeric->decimalPoint[1]) return 10;
    if (setlocale(0, "fr-FR") || *_errno() != 22) return 11;

    wchar_t formatted[32]{};
    if (format(formatted, 32, L"%s:%d:%X", L"crt", 7, 0x2A) != 8 ||
        !equal(formatted, L"crt:7:2A")) return 12;
    _fpreset();
    auto oldSignal = signal(2, signalHandler);
    if (oldSignal != nullptr) return 13;
    if (reinterpret_cast<uintptr_t>(signal(100, signalHandler)) != UINTPTR_MAX ||
        *_errno() != 22) return 14;

    void (*initializers[])(void) = {initOne, initTwo, nullptr};
    int (*statusInitializers[])(void) = {initStatus, nullptr};
    _initterm(initializers, initializers + 3);
    if (_initterm_e(statusInitializers, statusInitializers + 2) != 0 || initState != 123)
        return 15;
    if (atexit(exitOne) || atexit(exitTwo)) return 16;
    _cexit();
    if (exitOrder != 21) return 17;
    return 0;
}
}

extern "C" [[noreturn]] void MsvcrtProbeStartup() noexcept {
    exit(runProbe());
}
