#pragma once
#include <stdint.h>
#include <string.h>

extern void * realloc(void * ptr, size_t size);
extern void free(void *ptr);
extern void * malloc(size_t size);
extern void * calloc(size_t nmemb, size_t size);
extern double strtod(const char *nptr, char **endptr);
extern long int strtol(const char *nptr, char **endptr, int base);

extern void qsort(void *base, size_t nmemb, size_t size,
       int (*compar)(const void *, const void *));

extern void abort(void);
extern void exit(int status);
extern int atoi(const char * c);
