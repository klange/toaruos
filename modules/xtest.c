/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <module.h>
#include <printf.h>
#include <logging.h>

#include <mod/shell.h>

static void xtest_a(void * data, char * name) {
	fs_node_t * tty = data;

	fprintf(tty, "[%s] Hello world.\n", name);

	while (1) {
		fprintf(tty, "[%s] Ping.\n", name);
		unsigned long s, ss;
		relative_time(1, 0, &s, &ss);
		sleep_until((process_t *)current_process, s, ss);
		switch_task(0);
	}
}

static int hello(void) {
	fs_node_t * tty = kopen("/dev/ttyS0", 0);

	fprintf(tty, "[xtest] Starting background thread...\n");
	create_kernel_tasklet(xtest_a,  "xtest-a", (void *)tty);

	fprintf(tty, "[xtest] Enabling logging directly to serial...\n");
	debug_file = tty;
	debug_level = 1;

	return 0;
}

static int goodbye(void) {
	return 0;
}

MODULE_DEF(xtest, hello, goodbye);
MODULE_DEPENDS(serial);

