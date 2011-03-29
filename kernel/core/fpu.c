/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
#include <system.h>

void
set_fpu_cw(const uint16_t cw) {
	__asm__ __volatile__("fldcw %0" :: "m"(cw));
}

void
enable_fpu() {
	size_t cr4;
	/* Trust me, we have an FPU */
	__asm__ __volatile__ ("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= 0x200;
	__asm__ __volatile__ ("mov %0, %%cr4" :: "r"(cr4));
	set_fpu_cw(0x37F);
}
