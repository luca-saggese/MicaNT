#pragma once

#include <cstddef>

int mk_wcwidth(wchar_t codePoint);
int mk_wcwidth_cjk(wchar_t codePoint);
int mk_wcswidth(const wchar_t* text, size_t length);
int mk_wcswidth_cjk(const wchar_t* text, size_t length);
