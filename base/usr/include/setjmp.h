#pragma once

#include <_cheader.h>

_Begin_C_Header

/* i386 */
#ifdef __aarch64__
#define _JBLEN 32
#else
#define _JBLEN 9
#endif

typedef long long jmp_buf[_JBLEN];

extern void longjmp(jmp_buf j, int r);
extern int setjmp(jmp_buf j);

_End_C_Header
