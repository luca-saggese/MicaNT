/**
 * @file cli_common.hpp
 * Luce OS Ring 3 Command-Line Argument Parser & Console I/O Primitives.
 *
 * Clean-Room ISO C++23. Freestanding. Zero CRT dependencies.
 * Executed in User-Mode Ring 3 (CPL=3).
 *
 * Provides standard argc/argv tokenization, case-insensitive switch extraction,
 * positional parameter queries, tabular alignment, and fast syscall dispatch (0x25 / 0x01).
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace micant_cli {

// ============================================================================
// 1. Syscall Numbers & Low-Level Invocations
// ============================================================================

inline constexpr uint64_t SSN_TERMINATE_PROCESS = 0x01;
inline constexpr uint64_t SSN_SHUTDOWN_SYSTEM    = 0x08;
inline constexpr uint64_t SSN_QUERY_SYS_INFO     = 0x11;
inline constexpr uint64_t SSN_ALLOCATE_VM        = 0x18;
inline constexpr uint64_t SSN_FREE_VM            = 0x1B;
inline constexpr uint64_t SSN_READ_FILE          = 0x24;
inline constexpr uint64_t SSN_WRITE_FILE         = 0x25;
inline constexpr uint64_t SSN_ICMP_ECHO          = 0x26;
inline constexpr uint64_t SSN_YIELD_EXEC         = 0x46;
inline constexpr uint64_t SSN_GDI_GET_VIDEO_INFO = 0x80;
inline constexpr uint64_t SSN_GDI_PRESENT_SURF   = 0x81;

inline uint64_t DoSyscall(uint64_t ssn, uint64_t arg1 = 0, uint64_t arg2 = 0) noexcept {
    uint64_t res = 0;
#if defined(__x86_64__)
    __asm__ __volatile__(
        "movq %1, %%rax\n\t"
        "movq %2, %%rdx\n\t"
        "movq %3, %%r8\n\t"
        "syscall\n\t"
        "movq %%rax, %0\n\t"
        : "=r"(res)
        : "r"(ssn), "r"(arg1), "r"(arg2)
        : "rax", "rdx", "r8", "rcx", "r11", "memory"
    );
#endif
    return res;
}

[[noreturn]] inline void ExitProcess(uint32_t exitCode = 0) noexcept {
    DoSyscall(SSN_TERMINATE_PROCESS, exitCode, 0);
    while (true) {
#if defined(__x86_64__)
        __asm__ __volatile__("pause");
#endif
    }
}

inline void VerifyRing3Cpl() noexcept {
    uint16_t csVal = 0;
#if defined(__x86_64__)
    __asm__ __volatile__("movw %%cs, %0" : "=r"(csVal));
#endif
    DoSyscall(SSN_QUERY_SYS_INFO, csVal & 3, 0);
}

// ============================================================================
// 2. Freestanding String & Number Manipulation
// ============================================================================

constexpr size_t StrLen(const char* s) noexcept {
    size_t len = 0;
    while (s && s[len] != '\0') ++len;
    return len;
}

constexpr char ToLowerChar(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

constexpr char ToUpperChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
}

inline int StrCmp(const char* a, const char* b) noexcept {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && (*a == *b)) {
        a++; b++;
    }
    return static_cast<int>(static_cast<unsigned char>(*a)) - static_cast<int>(static_cast<unsigned char>(*b));
}

inline int StrCaseCmp(const char* a, const char* b) noexcept {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && (ToLowerChar(*a) == ToLowerChar(*b))) {
        a++; b++;
    }
    return static_cast<int>(static_cast<unsigned char>(ToLowerChar(*a))) -
           static_cast<int>(static_cast<unsigned char>(ToLowerChar(*b)));
}

inline bool StrEqual(const char* a, const char* b) noexcept {
    return StrCmp(a, b) == 0;
}

inline bool StrCaseEqual(const char* a, const char* b) noexcept {
    return StrCaseCmp(a, b) == 0;
}

inline bool StrStartsWith(const char* str, const char* prefix) noexcept {
    if (!str || !prefix) return false;
    while (*prefix) {
        if (*str != *prefix) return false;
        str++; prefix++;
    }
    return true;
}

inline bool StrCaseStartsWith(const char* str, const char* prefix) noexcept {
    if (!str || !prefix) return false;
    while (*prefix) {
        if (ToLowerChar(*str) != ToLowerChar(*prefix)) return false;
        str++; prefix++;
    }
    return true;
}

inline bool StrContains(const char* str, const char* sub) noexcept {
    if (!str || !sub) return false;
    if (*sub == '\0') return true;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        size_t j = 0;
        while (sub[j] != '\0' && str[i + j] == sub[j]) ++j;
        if (sub[j] == '\0') return true;
    }
    return false;
}

inline bool StrCaseContains(const char* str, const char* sub) noexcept {
    if (!str || !sub) return false;
    if (*sub == '\0') return true;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        size_t j = 0;
        while (sub[j] != '\0' && ToLowerChar(str[i + j]) == ToLowerChar(sub[j])) ++j;
        if (sub[j] == '\0') return true;
    }
    return false;
}

inline void StrCopy(char* dst, const char* src, size_t maxLen) noexcept {
    if (!dst || maxLen == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] != '\0' && i + 1 < maxLen) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

inline void StrConcat(char* dst, const char* src, size_t maxLen) noexcept {
    if (!dst || maxLen == 0) return;
    size_t dLen = StrLen(dst);
    if (dLen >= maxLen - 1) return;
    size_t sIdx = 0;
    if (src) {
        while (src[sIdx] != '\0' && (dLen + sIdx + 1) < maxLen) {
            dst[dLen + sIdx] = src[sIdx];
            ++sIdx;
        }
    }
    dst[dLen + sIdx] = '\0';
}

inline uint64_t ParseUInt64(const char* s) noexcept {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        uint64_t val = 0;
        while (*s) {
            char c = *s++;
            if (c >= '0' && c <= '9') val = (val << 4) | static_cast<uint64_t>(c - '0');
            else if (c >= 'a' && c <= 'f') val = (val << 4) | static_cast<uint64_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = (val << 4) | static_cast<uint64_t>(c - 'A' + 10);
            else break;
        }
        return val;
    }
    uint64_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = (val * 10) + static_cast<uint64_t>(*s - '0');
        s++;
    }
    return val;
}

inline int64_t ParseInt64(const char* s) noexcept {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') ++s;
    bool neg = false;
    if (*s == '-') {
        neg = true;
        s++;
    } else if (*s == '+') {
        s++;
    }
    uint64_t u = ParseUInt64(s);
    return neg ? -static_cast<int64_t>(u) : static_cast<int64_t>(u);
}

inline void FormatUInt64(uint64_t val, char* buf, size_t bufSize) noexcept {
    if (!buf || bufSize == 0) return;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[32];
    size_t len = 0;
    while (val > 0) {
        tmp[len++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    size_t outLen = 0;
    while (len > 0 && outLen + 1 < bufSize) {
        buf[outLen++] = tmp[--len];
    }
    buf[outLen] = '\0';
}

inline void FormatInt64(int64_t val, char* buf, size_t bufSize) noexcept {
    if (!buf || bufSize == 0) return;
    if (val < 0) {
        buf[0] = '-';
        FormatUInt64(static_cast<uint64_t>(-val), buf + 1, bufSize - 1);
    } else {
        FormatUInt64(static_cast<uint64_t>(val), buf, bufSize);
    }
}

inline void FormatHex64(uint64_t val, char* buf, size_t bufSize, bool prefix = true, bool uppercase = false) noexcept {
    if (!buf || bufSize == 0) return;
    size_t pos = 0;
    if (prefix && pos + 2 < bufSize) {
        buf[pos++] = '0';
        buf[pos++] = 'x';
    }
    if (val == 0) {
        if (pos + 1 < bufSize) buf[pos++] = '0';
        buf[pos] = '\0';
        return;
    }
    char hexDigitsUpper[] = "0123456789ABCDEF";
    char hexDigitsLower[] = "0123456789abcdef";
    const char* digits = uppercase ? hexDigitsUpper : hexDigitsLower;

    char tmp[16];
    size_t tLen = 0;
    while (val > 0) {
        tmp[tLen++] = digits[val & 0xF];
        val >>= 4;
    }
    while (tLen > 0 && pos + 1 < bufSize) {
        buf[pos++] = tmp[--tLen];
    }
    buf[pos] = '\0';
}

// ============================================================================
// 3. Freestanding Console I/O Primitives
// ============================================================================

inline void Print(const char* s) noexcept {
    if (s && *s) {
        DoSyscall(SSN_WRITE_FILE, reinterpret_cast<uint64_t>(s), StrLen(s));
    }
}

inline void PrintLine(const char* s = "") noexcept {
    if (s && *s) {
        Print(s);
    }
    Print("\r\n");
}

inline void PrintChar(char c) noexcept {
    char buf[2] = { c, '\0' };
    Print(buf);
}

inline void PrintDec(uint64_t val) noexcept {
    char buf[32];
    FormatUInt64(val, buf, sizeof(buf));
    Print(buf);
}

inline void PrintHex(uint64_t val, bool prefix = true) noexcept {
    char buf[32];
    FormatHex64(val, buf, sizeof(buf), prefix, true);
    Print(buf);
}

inline void PrintPadded(const char* s, size_t width, bool rightAlign = false) noexcept {
    size_t len = StrLen(s);
    if (rightAlign) {
        for (size_t i = len; i < width; ++i) Print(" ");
        Print(s ? s : "");
    } else {
        Print(s ? s : "");
        for (size_t i = len; i < width; ++i) Print(" ");
    }
}

// ============================================================================
// 4. Clean-Room Command-Line Tokenizer & Switch Extractor
// ============================================================================

class CommandLineParser {
public:
    static constexpr size_t MAX_ARGS = 32;
    static constexpr size_t BUFFER_SIZE = 1024;

private:
    char        m_buffer[BUFFER_SIZE]{};
    const char* m_rawCmdLine{""};
    const char* m_defaultAppName{""};
    char*       m_argv[MAX_ARGS]{};
    int         m_argc{0};

public:
    CommandLineParser(const char* rawCmdLine, const char* defaultAppName = "app.exe") noexcept
        : m_rawCmdLine(rawCmdLine ? rawCmdLine : ""),
          m_defaultAppName(defaultAppName ? defaultAppName : "app.exe")
    {
        tokenize();
    }

    [[nodiscard]] int argc() const noexcept { return m_argc; }
    [[nodiscard]] char** argv() noexcept { return m_argv; }
    [[nodiscard]] const char* const* argv() const noexcept { return m_argv; }
    [[nodiscard]] const char* GetArg(int idx) const noexcept {
        return (idx >= 0 && idx < m_argc) ? m_argv[idx] : "";
    }
    [[nodiscard]] const char* GetRawCommandLine() const noexcept { return m_rawCmdLine; }

    /**
     * @brief Checks if a switch is present, matching /name, -name, --name.
     */
    [[nodiscard]] bool HasSwitch(const char* swName) const noexcept {
        if (!swName || m_argc <= 1) return false;
        for (int i = 1; i < m_argc; ++i) {
            const char* arg = m_argv[i];
            if (!arg) continue;
            if (*arg == '/' || *arg == '-') {
                const char* cur = arg + 1;
                if (*cur == '-') cur++; // handle --switch
                // Check exact match or match before ':' or '='
                size_t swLen = StrLen(swName);
                if (StrCaseStartsWith(cur, swName)) {
                    char nextC = cur[swLen];
                    if (nextC == '\0' || nextC == ':' || nextC == '=') {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    /**
     * @brief Retrieves value associated with /switch:value, /switch=value, or /switch value.
     */
    [[nodiscard]] const char* GetSwitchValue(const char* swName) const noexcept {
        if (!swName || m_argc <= 1) return nullptr;
        size_t swLen = StrLen(swName);
        for (int i = 1; i < m_argc; ++i) {
            const char* arg = m_argv[i];
            if (!arg) continue;
            if (*arg == '/' || *arg == '-') {
                const char* cur = arg + 1;
                if (*cur == '-') cur++;
                if (StrCaseStartsWith(cur, swName)) {
                    char sep = cur[swLen];
                    if (sep == ':' || sep == '=') {
                        return cur + swLen + 1;
                    }
                    if (sep == '\0') {
                        // Check if next arg is value
                        if (i + 1 < m_argc && m_argv[i + 1] && m_argv[i + 1][0] != '/' && m_argv[i + 1][0] != '-') {
                            return m_argv[i + 1];
                        }
                        return "";
                    }
                }
            }
        }
        return nullptr;
    }

    /**
     * @brief Returns the count of positional (non-switch) parameters.
     */
    [[nodiscard]] int PositionalCount() const noexcept {
        int count = 0;
        for (int i = 1; i < m_argc; ++i) {
            const char* arg = m_argv[i];
            if (!arg) continue;
            if (*arg != '/' && *arg != '-') {
                // Check if previous was a switch requiring a separate value
                if (i > 1 && (m_argv[i - 1][0] == '/' || m_argv[i - 1][0] == '-')) {
                    // Check if previous switch consumed this token
                    const char* prev = m_argv[i - 1] + 1;
                    if (*prev == '-') prev++;
                    if (!StrContains(prev, ":") && !StrContains(prev, "=")) {
                        // Check common value switches: pid, im, u, p, s, fo, fi, n, l, w, h, t, r, c, m, d, target, size, timeout, id
                        if (StrCaseEqual(prev, "pid") || StrCaseEqual(prev, "im") ||
                            StrCaseEqual(prev, "u") || StrCaseEqual(prev, "p") ||
                            StrCaseEqual(prev, "s") || StrCaseEqual(prev, "fo") ||
                            StrCaseEqual(prev, "fi") || StrCaseEqual(prev, "n") ||
                            StrCaseEqual(prev, "l") || StrCaseEqual(prev, "w") ||
                            StrCaseEqual(prev, "h") || StrCaseEqual(prev, "t") ||
                            StrCaseEqual(prev, "r") || StrCaseEqual(prev, "c") ||
                            StrCaseEqual(prev, "m") || StrCaseEqual(prev, "d") ||
                            StrCaseEqual(prev, "target") || StrCaseEqual(prev, "size") ||
                            StrCaseEqual(prev, "recoverypassword") || StrCaseEqual(prev, "rp") ||
                            StrCaseEqual(prev, "recoverykey") || StrCaseEqual(prev, "rk") ||
                            StrCaseEqual(prev, "include") || StrCaseEqual(prev, "backuptarget") ||
                            StrCaseEqual(prev, "timeout") || StrCaseEqual(prev, "id")) {
                            continue;
                        }
                    }
                }
                count++;
            }
        }
        return count;
    }

    /**
     * @brief Returns the N-th positional (non-switch) parameter.
     */
    [[nodiscard]] const char* GetPositional(int targetIndex) const noexcept {
        int count = 0;
        for (int i = 1; i < m_argc; ++i) {
            const char* arg = m_argv[i];
            if (!arg) continue;
            if (*arg != '/' && *arg != '-') {
                if (i > 1 && (m_argv[i - 1][0] == '/' || m_argv[i - 1][0] == '-')) {
                    const char* prev = m_argv[i - 1] + 1;
                    if (*prev == '-') prev++;
                    if (!StrContains(prev, ":") && !StrContains(prev, "=")) {
                        if (StrCaseEqual(prev, "pid") || StrCaseEqual(prev, "im") ||
                            StrCaseEqual(prev, "u") || StrCaseEqual(prev, "p") ||
                            StrCaseEqual(prev, "s") || StrCaseEqual(prev, "fo") ||
                            StrCaseEqual(prev, "fi") || StrCaseEqual(prev, "n") ||
                            StrCaseEqual(prev, "l") || StrCaseEqual(prev, "w") ||
                            StrCaseEqual(prev, "h") || StrCaseEqual(prev, "t") ||
                            StrCaseEqual(prev, "r") || StrCaseEqual(prev, "c") ||
                            StrCaseEqual(prev, "m") || StrCaseEqual(prev, "d") ||
                            StrCaseEqual(prev, "target") || StrCaseEqual(prev, "size") ||
                            StrCaseEqual(prev, "recoverypassword") || StrCaseEqual(prev, "rp") ||
                            StrCaseEqual(prev, "recoverykey") || StrCaseEqual(prev, "rk") ||
                            StrCaseEqual(prev, "include") || StrCaseEqual(prev, "backuptarget") ||
                            StrCaseEqual(prev, "timeout") || StrCaseEqual(prev, "id")) {
                            continue;
                        }
                    }
                }
                if (count == targetIndex) {
                    return arg;
                }
                count++;
            }
        }
        return "";
    }

    /**
     * @brief Extracts remainder of command line after a given switch (e.g. /c <cmd>).
     */
    [[nodiscard]] const char* GetRemainingAfterSwitch(const char* swName) const noexcept {
        if (!swName || !m_rawCmdLine) return "";
        size_t swLen = StrLen(swName);
        const char* p = m_rawCmdLine;
        while (*p) {
            if (*p == '/' || *p == '-') {
                const char* cur = p + 1;
                if (*cur == '-') cur++;
                if (StrCaseStartsWith(cur, swName)) {
                    const char* after = cur + swLen;
                    if (*after == ' ' || *after == '\t' || *after == ':' || *after == '=' || *after == '\0') {
                        if (*after == ':' || *after == '=') after++;
                        while (*after == ' ' || *after == '\t') after++;
                        return after;
                    }
                }
            }
            p++;
        }
        return "";
    }

private:
    void tokenize() noexcept {
        m_argc = 0;
        for (size_t i = 0; i < MAX_ARGS; ++i) m_argv[i] = nullptr;

        // Copy raw command line to internal buffer
        StrCopy(m_buffer, m_rawCmdLine, BUFFER_SIZE);

        char* p = m_buffer;
        while (*p == ' ' || *p == '\t') ++p;

        if (*p == '\0') {
            // Empty command line: argv[0] = defaultAppName
            m_argv[0] = const_cast<char*>(m_defaultAppName);
            m_argc = 1;
            return;
        }

        // Temporary array of tokens parsed from the buffer
        char* tokens[MAX_ARGS]{};
        int tokenCount = 0;

        while (*p && tokenCount < static_cast<int>(MAX_ARGS)) {
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '\0') break;

            if (*p == '"') {
                // Quoted token
                p++;
                tokens[tokenCount++] = p;
                while (*p && *p != '"') ++p;
                if (*p == '"') {
                    *p = '\0';
                    p++;
                }
            } else {
                tokens[tokenCount++] = p;
                while (*p && *p != ' ' && *p != '\t') ++p;
                if (*p != '\0') {
                    *p = '\0';
                    p++;
                }
            }
        }

        if (tokenCount == 0) {
            m_argv[0] = const_cast<char*>(m_defaultAppName);
            m_argc = 1;
            return;
        }

        // Check if first token is the application name or an argument
        const char* firstTok = tokens[0];
        bool firstIsAppName = false;
        size_t tokLen = StrLen(firstTok);
        size_t defLen = StrLen(m_defaultAppName);

        if (StrCaseEqual(firstTok, m_defaultAppName)) {
            firstIsAppName = true;
        } else if (defLen > 4 && StrCaseEqual(m_defaultAppName + defLen - 4, ".exe") &&
                   tokLen == defLen - 4 &&
                   StrCaseStartsWith(m_defaultAppName, firstTok)) {
            firstIsAppName = true;
        } else if (tokLen > 4 && StrCaseEqual(firstTok + tokLen - 4, ".exe") &&
                   defLen == tokLen - 4 &&
                   StrCaseStartsWith(firstTok, m_defaultAppName)) {
            firstIsAppName = true;
        }

        if (firstIsAppName) {
            for (int i = 0; i < tokenCount && i < static_cast<int>(MAX_ARGS); ++i) {
                m_argv[i] = tokens[i];
            }
            m_argc = tokenCount;
        } else {
            // Synthesize argv[0] = defaultAppName, and shift tokens
            m_argv[0] = const_cast<char*>(m_defaultAppName);
            m_argc = 1;
            for (int i = 0; i < tokenCount && (m_argc < static_cast<int>(MAX_ARGS)); ++i) {
                m_argv[m_argc++] = tokens[i];
            }
        }
    }
};

} // namespace micant_cli
