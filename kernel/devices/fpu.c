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

process_t * fpu_thread = NULL;

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
void enable_fpu(void) {
	asm volatile ("clts");
	size_t t;
	asm volatile ("mov %%cr4, %0" : "=r"(t));
	t |= 3 << 9;
	asm volatile ("mov %0, %%cr4" :: "r"(t));
}

void disable_fpu(void) {
	size_t t;
	asm volatile ("mov %%cr0, %0" : "=r"(t));
	t |= 1 << 3;
	asm volatile ("mov %0, %%cr0" :: "r"(t));
}

uint8_t saves[512] __attribute__((aligned(16)));

void restore_fpu(process_t * proc) {
	memcpy(&saves,(uint8_t *)&proc->thread.fp_regs,512);
	asm volatile ("fxrstor %0" : "=m"(saves));
}

void save_fpu(process_t * proc) {
	asm volatile ("fxsave %0" : "=m"(saves));
	memcpy((uint8_t *)&proc->thread.fp_regs,&saves,512);
}

void init_fpu(void) {
	asm volatile ("fninit");
	set_fpu_cw(0x37F);
}

void invalid_op(struct regs * r) {
	enable_fpu();
	if (fpu_thread == current_process) {
		return;
	}
	if (fpu_thread) {
		save_fpu(fpu_thread);
	}
	fpu_thread = (process_t *)current_process;
	if (!fpu_thread->thread.fpu_enabled) {
		init_fpu();
		fpu_thread->thread.fpu_enabled = 1;
		return;
	}
	restore_fpu(fpu_thread);
}

void switch_fpu(void) {
	disable_fpu();
}

void auto_fpu(void) {
	isrs_install_handler(6, &invalid_op);
	isrs_install_handler(7, &invalid_op);
}
