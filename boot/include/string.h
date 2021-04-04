#pragma once

#include <stddef.h>

#ifndef __SIZE_TYPE__
# if defined(__x86_64__)
typedef unsigned long long size_t;
# else
typedef unsigned long size_t;
# endif
#else
typedef __SIZE_TYPE__ size_t;
#endif

#ifndef ssize_t
# if defined(__x86_64__)
typedef long long ssize_t;
# else
typedef long ssize_t;
# endif
#endif

extern void * memcpy(void * restrict dest, const void * restrict src, size_t n);
extern void * memset(void * dest, int c, size_t n);
extern int strcmp(const char * l, const char * r);
extern size_t strlen(const char *s);
extern int memcmp(const void * vl, const void * vr, size_t n);
extern char * strdup(const char * src);
extern void * memmove(void * dest, const void * src, size_t n);
extern char * strcat(char *dest, const char *src);
extern char *strstr(const char *haystack, const char *needle);
extern char * strchr(const char * s, int c);
extern char * strrchr(const char * s, int c);
extern char * strcpy(char * restrict dest, const char * restrict src);
