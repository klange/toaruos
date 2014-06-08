/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <module.h>
#include <fs.h>
#include <printf.h>
#include <mod/shell.h>

DEFINE_SHELL_FUNCTION(testwrite, "Test write") {

	fs_node_t * f = NULL;
	char * file = "/dev/hdb";

	if (argc > 1) {
		file = argv[1];
	}

	f = kopen(file, 0);

	if (!f) {
		fprintf(tty, "No device: %s\n", file);
		return 1;
	}

	char * s = malloc(1024);

	sprintf(s, "Hello World!");

	write_fs(f, 0, strlen(s), (uint8_t *)s);
	write_fs(f, 2, strlen(s), (uint8_t *)s);
	write_fs(f, 523, strlen(s), (uint8_t *)s);

	write_fs(f, 1024*12, 1024, (uint8_t *)s);

	free(s);

	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(testwrite);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(testwrite, init, fini);
MODULE_DEPENDS(debugshell);
