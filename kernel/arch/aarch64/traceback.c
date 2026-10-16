/**
 * @file  kernel/arch/aarch64/traceback.c
 * @brief Kernel fault traceback generator.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <stdint.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/traceback.h>

#include <kernel/arch/aarch64/regs.h>

static void dump_traceback(uintptr_t ip, uintptr_t bp, uintptr_t x30) {
	int depth = 0;
	int max_depth = 20;

	while (bp && ip && depth < max_depth) {
		if (traceback_print_line(ip, bp)) break;
		if (depth == 0) {
			ip = x30;
		} else {
			ip = *(uintptr_t*)(bp + sizeof(uintptr_t));
			bp = *(uintptr_t*)(bp);
		}
		depth++;
	}
}

void aarch64_safe_dump_traceback(uintptr_t elr, struct regs * r) {
	dump_traceback(elr, r->x29, r->x30);
}


/* We need to pull the frame address from the caller or this isn't going work, but
 * gcc is going to warn that that is unsafe... */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-address"

/**
 * @brief Display a traceback from the current call context.
 */
void arch_dump_traceback(void) {
	dump_traceback((uintptr_t)arch_dump_traceback+1, (uintptr_t)__builtin_frame_address(1), (uintptr_t)__builtin_return_address(0));
}

#pragma GCC diagnostic pop
