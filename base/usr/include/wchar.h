#pragma once

#include <stddef.h>

extern int wcwidth(wchar_t c);
extern wchar_t * wcsncpy(wchar_t * dest, const wchar_t * src, size_t n);
extern size_t wcslen(const wchar_t * s);
extern int wcscmp(const wchar_t *s1, const wchar_t *s2);
extern wchar_t * wcscat(wchar_t *dest, const wchar_t *src);
extern wchar_t * wcstok(wchar_t * str, const wchar_t * delim, wchar_t ** saveptr);
extern size_t wcsspn(const wchar_t * wcs, const wchar_t * accept);
extern wchar_t *wcspbrk(const wchar_t *wcs, const wchar_t *accept);
extern wchar_t * wcschr(const wchar_t *wcs, wchar_t wc);
extern wchar_t * wcsrchr(const wchar_t *wcs, wchar_t wc);
extern wchar_t * wcsncat(wchar_t *dest, const wchar_t * src, size_t n);

typedef unsigned int wint_t;
