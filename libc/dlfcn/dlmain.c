/**
 * @file libc/dlfcn/dlmain.c
 * @brief ld.so entry point
 *
 * When /lib/ld.so is called directly, handles argument parsing
 * and loads a binary from the command line.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-e] [binary [args...]]\n",
		argv[0]);
	return 1;
}

int ld_so_main(int argc, char * argv[]) {
	char * file = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "e:")) != -1) {
		switch (opt) {
			case 'e':
				file = optarg;
				break;
			default:
				return usage(argv);
		}
	}

	if (!file) {
		if (argc == optind) return usage(argv);
		file = argv[optind];
	}

	fprintf(stderr, "Nothing to do.\n");

	return 0;
}

