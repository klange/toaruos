#pragma once

#include <_cheader.h>

_Begin_C_Header
extern int sched_yield(void);

#if defined(_TOARU_SOURCE)
#include <stdint.h>
extern int clone(uintptr_t,uintptr_t,void*);
#endif

_End_C_Header
