#pragma once

#include <_cheader.h>

_Begin_C_Header

/* i386 */
#define _JBLEN 9

typedef int jmp_buf[_JBLEN];

extern void longjmp(jmp_buf j, int r);
extern int setjmp(jmp_buf j);

_End_C_Header
