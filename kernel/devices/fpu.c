/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Tiny FPU enable module.
 * 
 * Part of the ToAruOS Kernel
 * (C) 2011 Kevin Lange ...
 * To whatever possible level this short of a code chunk
 * can be considered copyrightable in your jurisdiction.
 */
#include <system.h>
#include <logging.h>

/**
 * Set the FPU control word
 *
 * @param cw What to set the control word to.
 */
void
set_fpu_cw(const uint16_t cw) {
	asm volatile("fldcw %0" :: "m"(cw));
}

/**
 * Enable the FPU
 *
 * We are assuming that we have one to begin with, but since we
 * only really operate on 686 machines, we do, so we're not
 * going to bother checking.
 */
void
enable_fpu() {
	blog("Enabling floating-point arithmetic unit...");
	LOG(INFO, "Enabling floating-point arithmetic unit");
	size_t cr4;
	asm volatile ("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= 0x200;
	asm volatile ("mov %0, %%cr4" :: "r"(cr4));
	set_fpu_cw(0x37F);
	bfinish(0);
}

