/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 *
 * Module to provide a debug shell command to set display mode.
 */
#include <kernel/system.h>
#include <kernel/module.h>
#include <kernel/logging.h>
#include <kernel/printf.h>
#include <kernel/video.h>
#include <kernel/mod/shell.h>

DEFINE_SHELL_FUNCTION(set_mode, "Set display mode") {
	if (argc < 3) {
		fprintf(tty, "set_mode <x> <y>\n");
		return 1;
	}
	int x = atoi(argv[1]);
	int y = atoi(argv[2]);
	fprintf(tty, "Setting mode to %dx%d.\n", x, y);
	lfb_set_resolution(x,y);
	return 0;
}

static int hello(void) {
	BIND_SHELL_FUNCTION(set_mode);

	return 0;
}

static int goodbye(void) {
	return 0;
}

MODULE_DEF(vidset, hello, goodbye);
MODULE_DEPENDS(debugshell);
MODULE_DEPENDS(lfbvideo);


