#pragma once

#include <stddef.h>

extern int wcwidth(wchar_t c);
extern wchar_t * wcsncpy(wchar_t * dest, const wchar_t * src, size_t n);
extern size_t wcslen(const wchar_t * s);
extern int wcscmp(const wchar_t *s1, const wchar_t *s2);
extern wchar_t * wcscat(wchar_t *dest, const wchar_t *src);

typedef unsigned int wint_t;
