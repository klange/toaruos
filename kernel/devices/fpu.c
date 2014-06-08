/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2013 Kevin Lange
 *
 * FPU and SSE context handling.
 *
 * FPU context is kept through context switches,
 * but the FPU is disabled. When an FPU instruction
 * is executed, it will trap here and the context
 * will be saved to its original owner and the context
 * for the current process will be loaded or the FPU
 * will be reset for the new process.
 *
 * FPU states are per kernel thread.
 *
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
 * Enable the FPU and SSE
 */
void enable_fpu(void) {
	asm volatile ("clts");
	size_t t;
	asm volatile ("mov %%cr4, %0" : "=r"(t));
	t |= 3 << 9;
	asm volatile ("mov %0, %%cr4" :: "r"(t));
}

/**
 * Disable FPU and SSE so it traps to the kernel
 */
void disable_fpu(void) {
	size_t t;
	asm volatile ("mov %%cr0, %0" : "=r"(t));
	t |= 1 << 3;
	asm volatile ("mov %0, %%cr0" :: "r"(t));
}

/* Temporary aligned buffer for copying around FPU contexts */
uint8_t saves[512] __attribute__((aligned(16)));

/**
 * Restore the FPU for a process
 */
void restore_fpu(process_t * proc) {
	memcpy(&saves,(uint8_t *)&proc->thread.fp_regs,512);
	asm volatile ("fxrstor %0" : "=m"(saves));
}

/**
 * Save the FPU for a process
 */
void save_fpu(process_t * proc) {
	asm volatile ("fxsave %0" : "=m"(saves));
	memcpy((uint8_t *)&proc->thread.fp_regs,&saves,512);
}

/**
 * Initialize the FPU
 */
void init_fpu(void) {
	asm volatile ("fninit");
	set_fpu_cw(0x37F);
}

/**
 * Kernel trap for FPU usage when FPU is disabled
 */
void invalid_op(struct regs * r) {
	/* First, turn the FPU on */
	enable_fpu();
	if (fpu_thread == current_process) {
		/* If this is the tread that last used the FPU, do nothing */
		return;
	}
	if (fpu_thread) {
		/* If there is a thread that was using the FPU, save its state */
		save_fpu(fpu_thread);
	}
	fpu_thread = (process_t *)current_process;
	if (!fpu_thread->thread.fpu_enabled) {
		/*
		 * If the FPU has not been used in this thread previously,
		 * we need to initialize it.
		 */
		init_fpu();
		fpu_thread->thread.fpu_enabled = 1;
		return;
	}
	/* Otherwise we restore the context for this thread. */
	restore_fpu(fpu_thread);
}

/* Called during a context switch; disable the FPU */
void switch_fpu(void) {
	disable_fpu();
}

/* Enable the FPU context handling */
void fpu_install(void) {
	isrs_install_handler(6, &invalid_op);
	isrs_install_handler(7, &invalid_op);
}
