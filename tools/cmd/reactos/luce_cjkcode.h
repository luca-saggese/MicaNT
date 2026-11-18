#pragma once

/* The terminal layout only needs the code-page classification used by CMD. */
inline constexpr unsigned int CP_SHIFTJIS = 932;
inline constexpr unsigned int CP_HANGUL = 949;
inline constexpr unsigned int CP_GB2312 = 936;
inline constexpr unsigned int CP_BIG5 = 950;

inline constexpr bool IsCJKCodePage(unsigned int codePage) noexcept {
    return codePage == CP_SHIFTJIS || codePage == CP_HANGUL ||
        codePage == CP_GB2312 || codePage == CP_BIG5;
}
