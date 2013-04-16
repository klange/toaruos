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

int _fpu_enabled = 0;

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
#if 1
	size_t cr4;
	asm volatile ("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= 0x200;
	asm volatile ("mov %0, %%cr4" :: "r"(cr4));
#else
	size_t t, r;
	asm volatile ("mov %%cr0, %0" : "=r"(t));
	t &=  0xFFFFFFFB;
	t |=  0x2;
	asm volatile ("mov %0, %%cr0" :: "r"(t));
	asm volatile ("mov %%cr4, %0" : "=r"(r));
	r |=  (1 << 9);
	r |=  (1 << 10);
	asm volatile ("mov %0, %%cr4" :: "r"(r));
#endif
	set_fpu_cw(0x37F);
	_fpu_enabled = 1;
}

void
disable_fpu() {
#if 1
	size_t cr4;
	asm volatile ("mov %%cr4, %0" : "=r"(cr4));
	cr4 &= ~(0x200);
	asm volatile ("mov %0, %%cr4" :: "r"(cr4));
#else
	size_t t;
	asm volatile ("mov %%cr0, %0" : "=r"(t));
	t |= 0x4;
	asm volatile ("mov %0, %%cr0" :: "r"(t));
#endif
	_fpu_enabled = 0;
}

uint8_t saves[512] __attribute__((aligned(16)));

void check_restore_fpu() {
	if (_fpu_enabled) {
		memcpy(&saves,(uint8_t *)&current_process->thread.fp_regs,512);
		asm volatile ("fxrstor %0" : "=m"(saves));
	}
}

void invalid_op(struct regs * r) {
	assert(!_fpu_enabled);

	debug_print(CRITICAL, "Hello world");

	enable_fpu();
	check_restore_fpu();
}

void auto_fpu() {

	enable_fpu();

#if 0
	disable_fpu();

	isrs_install_handler(6, &invalid_op);
	isrs_install_handler(7, &invalid_op);
	isrs_install_handler(7, &invalid_op);
#endif
}

void check_save_fpu() {
	if (_fpu_enabled) {
		asm volatile ("fxsave %0" : "=m"(saves));
		memcpy((uint8_t *)&current_process->thread.fp_regs,&saves,512);
#if 0
		disable_fpu();
#endif
	}
}

