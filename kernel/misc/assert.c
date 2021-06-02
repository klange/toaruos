/**
 * @file  kernel/misc/assert.h
 * @brief Kernel assertion handler.
 */
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/misc.h>

extern void arch_fatal_prepare(void);

void __assert_failed(const char * file, int line, const char * func, const char * cond) {
	arch_fatal_prepare();
	printf("%s:%d (%s) Assertion failed: %s\n", file, line, func, cond);
	arch_fatal();
}
