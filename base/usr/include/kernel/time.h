#pragma once

#include <kernel/types.h>

extern void relative_time(unsigned long, unsigned long, unsigned long *, unsigned long *);
extern uint64_t now(void);
extern uint64_t arch_perf_timer(void);
