#pragma once

#include <stddef.h>

extern void exit(int status);
extern char * getenv(const char *name);

extern void *malloc(size_t size);
extern void free(void *ptr);
extern void *calloc(size_t nmemb, size_t size);
extern void *realloc(void *ptr, size_t size);

extern void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void*,const void*));

extern int system(const char * command);

extern int abs(int j);

extern int putenv(char * name);
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char * str);

extern double strtod(const char *nptr, char **endptr);
extern double atof(const char * nptr);
extern int atoi(const char * nptr);
extern long atol(const char * nptr);
extern long int labs(long int j);
extern long int strtol(const char * s, char **endptr, int base);
extern long long int strtoll(const char *nptr, char **endptr, int base);
extern unsigned long int strtoul(const char *nptr, char **endptr, int base);
extern unsigned long long int strtoull(const char *nptr, char **endptr, int base);

extern void srand(unsigned int);
extern int rand(void);

#define ATEXIT_MAX 32
extern int atexit(void (*h)(void));
extern void _handle_atexit(void);

#define RAND_MAX 0x7FFFFFFF

extern void abort(void);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define NULL 0

extern void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
	int (*compar)(const void *, const void *));

extern char * mktemp(char * template);

extern size_t mbstowcs(wchar_t *dest, const char *src, size_t n);
extern size_t wcstombs(char * dest, const wchar_t *src, size_t n);
