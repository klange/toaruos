/**
 * @file  kernel/misc/assert.h
 * @brief Kernel assertion handler.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/misc.h>

void __assert_failed(const char * file, int line, const char * func, const char * cond) {
	arch_fatal_prepare();
	dprintf("%s:%d (%s) Assertion failed: %s\n", file, line, func, cond);
	arch_dump_traceback();
	arch_fatal();
}
