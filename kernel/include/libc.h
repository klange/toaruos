/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef __LIBC_H
#define __LIBC_H
#include <stddef.h>

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

extern void * memcpy(void * restrict dest, const void * restrict src, size_t n);
extern void * memset(void * dest, int c, size_t n);
extern void * memchr(const void * src, int c, size_t n);
extern void * memrchr(const void * m, int c, size_t n);
extern void * memmove(void *dest, const void *src, size_t n);

extern int memcmp(const void *vl, const void *vr, size_t n);

extern char * strdup(const char * s);
extern char * stpcpy(char * restrict d, const char * restrict s);
extern char * strcpy(char * restrict dest, const char * restrict src);
extern char * strchrnul(const char * s, int c);
extern char * strchr(const char * s, int c);
extern char * strrchr(const char * s, int c);
extern char * strpbrk(const char * s, const char * b);
extern char * strstr(const char * h, const char * n);

extern int strcmp(const char * l, const char * r);

extern size_t strcspn(const char * s, const char * c);
extern size_t strspn(const char * s, const char * c);
extern size_t strlen(const char * s);

extern int atoi(const char * s);

/* Non-standard broken strtok_r */
extern char * strtok_r(char * str, const char * delim, char ** saveptr);

#endif
