#pragma once

#include <_cheader.h>

_Begin_C_Header

extern void *malloc(size_t size);
extern void free(void *ptr);
extern void *calloc(size_t nmemb, size_t size);
extern void *realloc(void *ptr, size_t size);
extern void *valloc(size_t size);

extern size_t malloc_usable_size(void *ptr);

_End_C_Header
