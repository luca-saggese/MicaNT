// Small freestanding CRT bridge for the upstream Doom sources. File reads
// use LuceOS Kernel32/NTDLL; unsupported mutations fail instead of succeeding.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "micant/shared/native_nt_api.hpp"


extern "C" {
__declspec(dllimport) void* __stdcall CreateFileW(const wchar_t*, uint32_t, uint32_t,
    void*, uint32_t, uint32_t, void*);
__declspec(dllimport) int32_t __stdcall GetFileSizeEx(void*, int64_t*);
__declspec(dllimport) int32_t __stdcall CloseHandle(void*);
__declspec(dllimport) int32_t __cdecl ucrt_stdio_common_vsprintf(
    uint64_t, char*, size_t, const char*, void*, va_list);
__declspec(dllimport) void __stdcall ExitProcess(uint32_t);
}

extern "C" void DG_ShowEngineError(const char* message);

namespace {
char g_lastReadError[128]{};
}

extern "C" const char* DG_GetLastReadError() { return g_lastReadError; }
extern "C" void DG_SetLastReadError(const char* message) {
    if (!message) { g_lastReadError[0] = '\0'; return; }
    size_t i{};
    for (; i + 1 < sizeof(g_lastReadError) && message[i]; ++i)
        g_lastReadError[i] = message[i];
    g_lastReadError[i] = '\0';
}

namespace {
struct DoomFile {
    void* handle{};
    int64_t position{};
    int64_t length{};
    bool readable{};
    bool writable{};
    bool active{};
};
DoomFile g_files[24]{};

bool append(char* output, size_t capacity, size_t& length, char value) noexcept {
    if (length + 1 >= capacity) return false;
    output[length++] = value;
    output[length] = '\0';
    return true;
}

int formatBuffer(char* output, size_t capacity, const char* format,
                 va_list arguments) noexcept {
    if (!output || !capacity || !format) return -1;
    output[0] = '\0';
    size_t length{};
    for (size_t i = 0; format[i]; ++i) {
        if (format[i] != '%') {
            if (!append(output, capacity, length, format[i])) return -1;
            continue;
        }
        ++i;
        if (format[i] == '%') {
            if (!append(output, capacity, length, '%')) return -1;
            continue;
        }
        bool left{}, zero{}, plus{}, alternate{};
        while (format[i] == '-' || format[i] == '0' || format[i] == '+' || format[i] == ' ' || format[i] == '#') {
            left |= format[i] == '-'; zero |= format[i] == '0';
            plus |= format[i] == '+'; alternate |= format[i] == '#'; ++i;
        }
        unsigned width{};
        while (format[i] >= '0' && format[i] <= '9') {
            width = width * 10u + static_cast<unsigned>(format[i++] - '0');
            if (width > 1024) return -1;
        }
        int precision = -1;
        if (format[i] == '.') {
            precision = 0; ++i;
            while (format[i] >= '0' && format[i] <= '9') {
                precision = precision * 10 + (format[i++] - '0');
                if (precision > 1024) return -1;
            }
        }
        enum class Length { Default, Long, LongLong, Size } kind{};
        if (format[i] == 'h') { ++i; if (format[i] == 'h') ++i; }
        else if (format[i] == 'l') { ++i; kind = Length::Long; if (format[i] == 'l') { ++i; kind = Length::LongLong; } }
        else if (format[i] == 'z') { ++i; kind = Length::Size; }
        else if (format[i] == 'I' && format[i + 1] == '6' && format[i + 2] == '4') { i += 3; kind = Length::LongLong; }
        const char conversion = format[i];
        char temporary[1100]{};
        size_t used{};
        bool numeric{};
        if (conversion == 's') {
            const char* text = va_arg(arguments, const char*);
            if (!text) text = "(null)";
            const size_t limit = precision < 0 ? sizeof(temporary) - 1 : static_cast<size_t>(precision);
            while (used < limit && text[used]) temporary[used] = text[used], ++used;
        } else if (conversion == 'c') {
            temporary[used++] = static_cast<char>(va_arg(arguments, int));
        } else if (conversion == 'd' || conversion == 'i' || conversion == 'u' ||
                   conversion == 'x' || conversion == 'X' || conversion == 'o' || conversion == 'p') {
            numeric = true;
            const bool signedValue = conversion == 'd' || conversion == 'i';
            uint64_t value{};
            bool negative{};
            if (conversion == 'p') value = reinterpret_cast<uintptr_t>(va_arg(arguments, void*));
            else if (signedValue) {
                int64_t signedValueRaw = kind == Length::LongLong || kind == Length::Size
                    ? va_arg(arguments, int64_t) : kind == Length::Long
                    ? va_arg(arguments, long) : va_arg(arguments, int);
                negative = signedValueRaw < 0;
                value = negative ? static_cast<uint64_t>(-(signedValueRaw + 1)) + 1
                                 : static_cast<uint64_t>(signedValueRaw);
            } else value = kind == Length::LongLong || kind == Length::Size
                ? va_arg(arguments, uint64_t) : kind == Length::Long
                ? va_arg(arguments, unsigned long) : va_arg(arguments, unsigned int);
            const unsigned radix = conversion == 'o' ? 8u :
                conversion == 'x' || conversion == 'X' || conversion == 'p' ? 16u : 10u;
            const char* digits = conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char reversed[32]{};
            size_t digitCount{};
            do { reversed[digitCount++] = digits[value % radix]; value /= radix; } while (value);
            if (precision == 0 && digitCount == 1 && reversed[0] == '0') digitCount = 0;
            if (negative) temporary[used++] = '-';
            else if (signedValue && plus) temporary[used++] = '+';
            else if (signedValue && alternate) temporary[used++] = ' ';
            if (conversion == 'p' || (alternate && radix == 16)) {
                temporary[used++] = '0'; temporary[used++] = conversion == 'X' ? 'X' : 'x';
            } else if (alternate && radix == 8 && (!digitCount || reversed[digitCount - 1] != '0')) {
                temporary[used++] = '0';
            }
            while (precision > static_cast<int>(digitCount)) { temporary[used++] = '0'; --precision; }
            while (digitCount) temporary[used++] = reversed[--digitCount];
        } else return -1;

        const size_t pad = width > used ? width - used : 0;
        const bool zeroPad = numeric && zero && !left && precision < 0;
        if (!left && !zeroPad)
            for (size_t p = 0; p < pad; ++p)
                if (!append(output, capacity, length, ' ')) return -1;
        if (zeroPad && pad) {
            size_t prefix = used && (temporary[0] == '-' || temporary[0] == '+' || temporary[0] == ' ')
                ? 1 : (used >= 2 && temporary[0] == '0' ? 2 : 0);
            for (size_t p = 0; p < prefix; ++p) if (!append(output, capacity, length, temporary[p])) return -1;
            for (size_t p = prefix; p < pad + prefix; ++p) if (!append(output, capacity, length, '0')) return -1;
            for (size_t p = prefix; p < used; ++p) if (!append(output, capacity, length, temporary[p])) return -1;
        } else {
            for (size_t p = 0; p < used; ++p) if (!append(output, capacity, length, temporary[p])) return -1;
        }
        if (left) while (length < width && !append(output, capacity, length, ' ')) return -1;
    }
    return static_cast<int>(length);
}

DoomFile* findFile(FILE* stream) noexcept {
    for (auto& file : g_files)
        if (file.active && reinterpret_cast<void*>(stream) == &file) return &file;
    return nullptr;
}

int lowerAscii(unsigned char value) noexcept {
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

bool convertPath(const char* input, wchar_t* output, size_t capacity) noexcept {
    if (!input || !output || capacity < 2) return false;
    size_t index{};
    for (; input[index] && index + 1 < capacity; ++index) {
        const unsigned char byte = static_cast<unsigned char>(input[index]);
        if (byte >= 0x80) return false;
        output[index] = static_cast<wchar_t>(byte);
    }
    if (input[index]) return false;
    output[index] = L'\0';
    return true;
}

int parseInteger(const char* input, int base, int* result) noexcept {
    if (!input || !result) return 0;
    while (*input == ' ' || *input == '\t') ++input;
    bool negative{};
    if (*input == '-' || *input == '+') negative = *input++ == '-';
    if (base == 0) {
        if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) {
            input += 2; base = 16;
        } else if (input[0] == '0') base = 8;
        else base = 10;
    }
    if (base == 16 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) input += 2;
    unsigned value{};
    int digits{};
    for (;;) {
        const unsigned char c = static_cast<unsigned char>(*input);
        const int digit = c >= '0' && c <= '9' ? c - '0' :
            lowerAscii(c) >= 'a' && lowerAscii(c) <= 'f' ? lowerAscii(c) - 'a' + 10 : -1;
        if (digit < 0 || digit >= base) break;
        value = value * static_cast<unsigned>(base) + static_cast<unsigned>(digit);
        ++digits; ++input;
    }
    if (!digits) return 0;
    *result = negative ? -static_cast<int>(value) : static_cast<int>(value);
    return 1;
}
}

extern "C" {
int strcasecmp(const char* a, const char* b) {
    if (!a || !b) return a ? 1 : b ? -1 : 0;
    while (*a && *b) {
        const int difference = lowerAscii(static_cast<unsigned char>(*a++)) -
                               lowerAscii(static_cast<unsigned char>(*b++));
        if (difference) return difference;
    }
    return lowerAscii(static_cast<unsigned char>(*a)) -
           lowerAscii(static_cast<unsigned char>(*b));
}

int strncasecmp(const char* a, const char* b, size_t count) {
    if (!a || !b) return a ? 1 : b ? -1 : 0;
    for (size_t i = 0; i < count; ++i) {
        const int difference = lowerAscii(static_cast<unsigned char>(a[i])) -
                               lowerAscii(static_cast<unsigned char>(b[i]));
        if (difference || !a[i] || !b[i]) return difference;
    }
    return 0;
}

char* strrchr(const char* string, int character) {
    if (!string) return nullptr;
    const char value = static_cast<char>(character);
    const char* match = nullptr;
    do { if (*string == value) match = string; } while (*string++);
    return const_cast<char*>(match);
}

char* strdup(const char* source) {
    if (!source) return nullptr;
    const size_t length = std::strlen(source) + 1;
    auto* copy = static_cast<char*>(std::malloc(length));
    if (copy) std::memcpy(copy, source, length);
    return copy;
}

int toupper(int character) {
    return character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character;
}

int abs(int value) { return value < 0 ? -value : value; }
int atoi(const char* value) {
    int result{};
    (void)parseInteger(value, 10, &result);
    return result;
}
double atof(const char* value) {
    if (!value) return 0.0;
    while (*value == ' ' || *value == '\t') ++value;
    bool negative{};
    if (*value == '-' || *value == '+') negative = *value++ == '-';
    double result{};
    while (*value >= '0' && *value <= '9') result = result * 10.0 + (*value++ - '0');
    if (*value == '.') {
        ++value;
        double place = 0.1;
        while (*value >= '0' && *value <= '9') { result += (*value++ - '0') * place; place *= 0.1; }
    }
    return negative ? -result : result;
}

int sscanf(const char* input, const char* format, ...) {
    if (!input || !format || !format[0]) return 0;
    const char* conversion = format;
    while (*conversion && *conversion != '%') ++conversion;
    if (!*conversion || conversion[1] == '\0') return 0;
    const char* value = input;
    while (*value == ' ' || *value == '\t') ++value;
    int base{};
    switch (conversion[1]) {
    case 'i': base = 0; break;
    case 'x': case 'X': base = 16; break;
    case 'o': base = 8; break;
    case 'd': base = 10; break;
    default: return 0;
    }
    va_list arguments;
    va_start(arguments, format);
    int* output = va_arg(arguments, int*);
    va_end(arguments);
    return parseInteger(value, base, output);
}

int snprintf(char* output, size_t capacity, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = formatBuffer(output, capacity, format, arguments);
    va_end(arguments);
    return result;
}
int _vsnprintf(char* output, size_t capacity, const char* format, va_list arguments) {
    return formatBuffer(output, capacity, format, arguments);
}

int vfprintf(FILE* stream, const char* format, va_list arguments) {
    return __stdio_common_vfprintf(0, stream, format, nullptr, arguments);
}
int fprintf(FILE* stream, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}
int printf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(reinterpret_cast<FILE*>(__acrt_iob_func(1)), format, arguments);
    va_end(arguments);
    return result;
}
int puts(const char* string) { return printf("%s\n", string ? string : "(null)"); }
int putchar(int character) { return printf("%c", character); }

FILE* fopen(const char* path, const char* mode) {
    wchar_t widePath[512]{};
    if (!convertPath(path, widePath, std::size(widePath)) || !mode) return nullptr;
    const bool read = mode[0] == 'r';
    const bool write = mode[0] == 'w' || mode[0] == 'a';
    if ((!read && !write) || (mode[1] && mode[1] != 'b' && mode[1] != '+')) return nullptr;
    // Kernel32 currently supports OPEN_EXISTING and OPEN_ALWAYS. For now the
    // Doom CRT accepts reads and existing-file updates; it never fabricates a
    // new save/config file when the filesystem create contract is absent.
    void* handle = CreateFileW(widePath, (read ? 0x80000000u : 0u) |
        (write ? 0x40000000u : 0u), 7u, nullptr, 3u, 0x80u, nullptr);
    if (!handle || handle == reinterpret_cast<void*>(static_cast<intptr_t>(-1))) return nullptr;
    int64_t length{};
    if (!GetFileSizeEx(handle, &length) || length < 0) { (void)CloseHandle(handle); return nullptr; }
    for (auto& file : g_files) {
        if (file.active) continue;
        file = {handle, 0, length, read, write, true};
        return reinterpret_cast<FILE*>(&file);
    }
    (void)CloseHandle(handle);
    return nullptr;
}

int fclose(FILE* stream) {
    DoomFile* file = findFile(stream);
    if (!file) return EOF;
    const int32_t status = CloseHandle(file->handle);
    *file = {};
    return status ? 0 : EOF;
}

size_t fread(void* buffer, size_t size, size_t count, FILE* stream) {
    g_lastReadError[0] = '\0';
    DoomFile* file = findFile(stream);
    if (!file) {
        (void)snprintf(g_lastReadError, sizeof(g_lastReadError), "fread invalid stream");
        return 0;
    }
    if (!file->readable) {
        (void)snprintf(g_lastReadError, sizeof(g_lastReadError), "fread stream not readable");
        return 0;
    }
    if ((!buffer && size && count) || (size && count > SIZE_MAX / size)) {
        (void)snprintf(g_lastReadError, sizeof(g_lastReadError), "fread invalid buffer or size");
        return 0;
    }
    const size_t bytes = size * count;
    if (bytes > UINT32_MAX) {
        (void)snprintf(g_lastReadError, sizeof(g_lastReadError), "fread request too large");
        return 0;
    }
    if (!bytes) return 0;
    if (file->position < 0 || file->position > INT64_MAX - static_cast<int64_t>(bytes)) return 0;

    size_t total{};
    auto* destination = static_cast<uint8_t*>(buffer);
    while (total < bytes) {
        micant::IoStatusBlock io{};
        micant::LargeInteger offset{};
        offset.QuadPart = file->position + static_cast<int64_t>(total);
        const uint32_t remaining = static_cast<uint32_t>(bytes - total);
        const auto status = micant::native_nt::NtReadFile(
            reinterpret_cast<micant::Handle>(file->handle), 0, nullptr, nullptr,
            &io, destination + total, remaining, &offset, nullptr);
        if (status != micant::NtStatus::Success || io.information > remaining) {
            (void)snprintf(g_lastReadError, sizeof(g_lastReadError),
                "NtReadFile 0x%08x at %llu requested %u got %llu",
                static_cast<unsigned>(status),
                static_cast<unsigned long long>(offset.QuadPart), remaining,
                static_cast<unsigned long long>(io.information));
            break;
        }

        const size_t transferred = static_cast<size_t>(io.information);
        if (!transferred) {
            (void)snprintf(g_lastReadError, sizeof(g_lastReadError),
                "NtReadFile empty at %llu requested %u",
                static_cast<unsigned long long>(offset.QuadPart), remaining);
            break;
        }
        total += transferred;
    }
    file->position += static_cast<int64_t>(total);
    return total / size;
}

size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream) {
    DoomFile* file = findFile(stream);
    if (!file || !file->writable || (!buffer && size && count) || (size && count > SIZE_MAX / size)) return 0;
    const size_t bytes = size * count;
    if (bytes > UINT32_MAX) return 0;
    micant::IoStatusBlock io{};
    micant::LargeInteger offset{};
    offset.QuadPart = file->position;
    const auto status = micant::native_nt::NtWriteFile(
        reinterpret_cast<micant::Handle>(file->handle), 0, nullptr, nullptr,
        &io, buffer, static_cast<uint32_t>(bytes), &offset, nullptr);
    if (status != micant::NtStatus::Success) return 0;
    const size_t written = static_cast<size_t>(io.information);
    file->position += static_cast<int64_t>(written);
    if (file->position > file->length) file->length = file->position;
    return size ? written / size : 0;
}

int fseek(FILE* stream, long offset, int origin) {
    DoomFile* file = findFile(stream);
    if (!file) return -1;
    int64_t base{};
    if (origin == SEEK_SET) base = 0;
    else if (origin == SEEK_CUR) base = file->position;
    else if (origin == SEEK_END) base = file->length;
    else return -1;
    const int64_t next = base + offset;
    if (next < 0 || next > file->length) return -1;
    file->position = next;
    return 0;
}

long ftell(FILE* stream) {
    DoomFile* file = findFile(stream);
    if (!file || file->position > LONG_MAX) return -1;
    return static_cast<long>(file->position);
}

int remove(const char*) { return -1; }
int rename(const char*, const char*) { return -1; }
int mkdir(const char*) { return -1; }
char* strncpy(char* destination, const char* source, size_t count) {
    if (!destination || !source) return destination;
    size_t i{};
    for (; i < count && source[i]; ++i) destination[i] = source[i];
    for (; i < count; ++i) destination[i] = '\0';
    return destination;
}
char* strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return nullptr;
    if (!*needle) return const_cast<char*>(haystack);
    for (const char* current = haystack; *current; ++current) {
        size_t i{};
        while (needle[i] && current[i] == needle[i]) ++i;
        if (!needle[i]) return const_cast<char*>(current);
    }
    return nullptr;
}
double fabs(double value) { return value < 0.0 ? -value : value; }
void exit(int status) { ExitProcess(static_cast<uint32_t>(status)); __builtin_unreachable(); }
}
