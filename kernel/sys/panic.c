/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2018 K. Lange
 *
 * Panic functions
 */
#include <kernel/system.h>
#include <kernel/logging.h>
#include <kernel/printf.h>
#include <kernel/module.h>

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
	send_signal(current_process->id, SIGILL, 1);
}

char * probable_function_name(uintptr_t ip, uintptr_t * out_addr) {
	char * closest  = NULL;
	size_t distance = 0xFFFFFFFF;
	uintptr_t  addr = 0;

	if (modules_get_symbols()) {
		list_t * hash_keys = hashmap_keys(modules_get_symbols());
		foreach(_key, hash_keys) {
			char * key = (char *)_key->value;
			uintptr_t a = (uintptr_t)hashmap_get(modules_get_symbols(), key);

			if (!a) continue;

			size_t d = 0xFFFFFFFF;
			if (a <= ip) {
				d = ip - a;
			}
			if (d < distance) {
				closest = key;
				distance = d;
				addr = a;
			}
		}
		free(hash_keys);

	}
	*out_addr = addr;
	return closest;
}

void assert_failed(const char *file, uint32_t line, const char *desc) {
	IRQ_OFF;
	debug_print(INSANE, "Kernel Assertion Failed: %s", desc);
	debug_print(INSANE, "File: %s", file);
	debug_print(INSANE, "Line: %d", line);
	debug_print(INSANE, "System Halted!");

#if 1
	unsigned int * ebp = (unsigned int *)(&file - 2);

	debug_print(INSANE, "Stack trace:");

	for (unsigned int frame = 0; frame < 20; ++frame) {
		unsigned int eip = ebp[1];
		if (eip == 0) break;
		ebp = (unsigned int *)(ebp[0]);
		unsigned int * args = &ebp[2];
		(void)args;
		uintptr_t addr;
		char * func = probable_function_name(eip, &addr);
		debug_print(INSANE, "    0x%x (%s+%d)\n", eip, func, eip-addr);
	}


#endif

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
