/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 *
 * yutani-query - Query display server information
 *
 * At the moment, this only supports querying the display
 * resolution. An older version of this application had
 * support for getting the default font names, but the
 * font server is no longer part of the compositor, so
 * that functionality doesn't make sense here.
 */
#include <stdio.h>
#include <unistd.h>

#include <toaru/yutani.h>

yutani_t * yctx;

void show_usage(int argc, char * argv[]) {
	printf(
			"yutani-query - show misc. information about the display system\n"
			"\n"
			"usage: %s [-r?]\n"
			"\n"
			" -r     \033[3mprint display resoluton\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int show_resolution(void) {
	printf("%dx%d\n", (int)yctx->display_width, (int)yctx->display_height);
	return 0;
}

int main(int argc, char * argv[]) {
	yctx = yutani_init();
	if (!yctx) {
		printf("(not connected)\n");
		return 1;
	}
	int opt;
	while ((opt = getopt(argc, argv, "?r")) != -1) {
		switch (opt) {
			case 'r':
				return show_resolution();
			case '?':
				show_usage(argc,argv);
				return 0;
		}
	}

	return 0;
}
