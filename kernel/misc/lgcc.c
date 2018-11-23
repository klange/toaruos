/* Garbage */

#include <kernel/system.h>

#define USES(func) void * _USES_ ## func (void) { return (void*)(uintptr_t)&func; }

extern unsigned long __umoddi3 (unsigned long a, unsigned long b);
USES(__umoddi3)

extern unsigned long __udivdi3 (unsigned long a, unsigned long b);
USES(__udivdi3)
