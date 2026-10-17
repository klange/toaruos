#pragma once

#include <_cheader.h>

_Begin_C_Header
extern int sched_yield(void);

#if defined(_TOARU_SOURCE)
#include <sys/types.h>
#include <stdint.h>
extern int clone(uintptr_t,uintptr_t,void*);
extern int sched_setaffinity_t(pid_t, uint64_t);
extern int sched_getaffinity_t(pid_t, uint64_t*);
#endif

_End_C_Header
