/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 *
 * Panic functions
 */
#include <system.h>
#include <logging.h>
#include <printf.h>

void halt_and_catch_fire(char * error_message, const char * file, int line, struct regs * regs) {
	IRQ_OFF;
	debug_print(ERROR, "HACF: %s", error_message);
	debug_print(ERROR, "Proc: %d", getpid());
	debug_print(ERROR, "File: %s", file);
	debug_print(ERROR, "Line: %d", line);
	if (regs) {
		debug_print(ERROR, "Registers at interrupt:");
		debug_print(ERROR, "eax=0x%x ebx=0x%x", regs->eax, regs->ebx);
		debug_print(ERROR, "ecx=0x%x edx=0x%x", regs->ecx, regs->edx);
		debug_print(ERROR, "esp=0x%x ebp=0x%x", regs->esp, regs->ebp);
		debug_print(ERROR, "Error code: 0x%x",  regs->err_code);
		debug_print(ERROR, "EFLAGS:     0x%x",  regs->eflags);
		debug_print(ERROR, "User ESP:   0x%x",  regs->useresp);
		debug_print(ERROR, "eip=0x%x",          regs->eip);
	}
	debug_print(ERROR, "This process has been descheduled.");
	kexit(1);
}

void assert_failed(const char *file, uint32_t line, const char *desc) {
	IRQ_OFF;
	debug_print(INSANE, "Kernel Assertion Failed: %s", desc);
	debug_print(INSANE, "File: %s", file);
	debug_print(INSANE, "Line: %d", line);
	debug_print(INSANE, "System Halted!");

	if (debug_video_crash) {
		char msg[4][256];
		sprintf(msg[0], "Kernel Assertion Failed: %s", desc);
		sprintf(msg[1], "File: %s", file);
		sprintf(msg[2], "Line: %d", line);
		sprintf(msg[3], "System Halted!");
		char * msgs[] = {msg[0], msg[1], msg[2], msg[3], NULL};
		debug_video_crash(msgs);
	}

	while (1) {
		IRQ_OFF;
		PAUSE;
	}
}
