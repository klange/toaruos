#pragma once

#include <_cheader.h>
#include <stddef.h>

_Begin_C_Header
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
extern int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
extern wchar_t * wcscpy(wchar_t * dest, const wchar_t * src);
extern unsigned long int wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
extern unsigned long long int wcstoull(const char *nptr, wchar_t **endptr, int base);
extern long int wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
extern long long int wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);

typedef unsigned int wint_t;
_End_C_Header
