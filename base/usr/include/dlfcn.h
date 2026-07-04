#pragma once

#include <_cheader.h>

_Begin_C_Header

/* Currently unused... */
#define RTLD_LAZY   (1 << 0)
#define RTLD_NOW    (1 << 1)
#define RTLD_GLOBAL (1 << 2)

#define RTLD_DEFAULT ((void*)0)
#define RTLD_NEXT    ((void*)-1)

typedef struct {
	const char *dli_fname;
	void       *dli_fbase;
	const char *dli_sname;
	void       *dli_saddr;
} Dl_info_t;

/* Provided by ld.so, but also defined by libc.so for linking */
extern void * dlopen(const char *, int);
extern int dlclose(void *);
extern void * dlsym(void *, const char *);
extern char * dlerror(void);
extern int dladdr(const void * __restrict, Dl_info_t * __restrict);

_End_C_Header
