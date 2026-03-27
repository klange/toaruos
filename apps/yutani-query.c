/**
 * @brief yutani-query - Query display server information
 *
 * At the moment, this only supports querying the display
 * resolution. An older version of this application had
 * support for getting the default font names, but the
 * font server is no longer part of the compositor, so
 * that functionality doesn't make sense here.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
 */
#include <stdio.h>
#include <unistd.h>

#include <toaru/yutani.h>

yutani_t * yctx;
int quiet = 0;

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - show misc. information about the display system\n"
			"\n"
			"usage: %s [-qre?]\n"
			"       %s [-q] " X_S "COMMAND" X_E "\n"
			"\n"
			" -q               " X_S "operate quietly" X_E "\n"
			" -?               " X_S "show this help text" X_E "\n"
			"\n"
			"Commands:\n"
			" resolution  (-r) " X_S "print display resolution" X_E "\n"
			" reload      (-e) " X_S "ask compositor to reload extensions" X_E "\n"
			"\n", argv[0], argv[0], argv[0]);
	return 1;
}

int check(char * argv[]) {
	if (!yctx) {
		if (!quiet) fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}
	return 0;
}

int show_resolution(void) {
	if (!quiet) printf("%dx%d\n", (int)yctx->display_width, (int)yctx->display_height);
	return 0;
}

int reload(void) {
	yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_RELOAD);
	return 0;
}

int main(int argc, char * argv[]) {
	yctx = yutani_init();
	int opt;
	while ((opt = getopt(argc, argv, "?qre")) != -1) {
		switch (opt) {
			case 'q':
				quiet = 1;
				break;
			/* Legacy options */
			case 'r':
				if (check(argv)) return 1;
				return show_resolution();
			case 'e':
				if (check(argv)) return 1;
				return reload();
			case '?':
				return show_usage(argc,argv);
		}
	}

	if (check(argv)) return 1;

	if (optind < argc) {
		if (!strcmp(argv[optind], "resolution")) {
			return show_resolution();
		} else if (!strcmp(argv[optind], "reload")) {
			return reload();
		} else {
			fprintf(stderr, "%s: unsupported command: %s\n", argv[0], argv[optind]);
			return 1;
		}
	}

	return 0;
}
