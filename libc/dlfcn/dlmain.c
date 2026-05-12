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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-e] [binary [args...]]\n",
		argv[0]);
	return 1;
}

extern int __libc_load_from_file(int fd, const char * name, int argc, char *argv[]);
extern int __is_ldd;

int ld_so_main(int argc, char * argv[]) {
	char * file = NULL;
	int opt;

	if (argc && !strcmp(basename(argv[0]),"ldd")) __is_ldd = true;

	while ((opt = getopt(argc, argv, "e:-:")) != -1) {
		switch (opt) {
			case 'e':
				file = optarg;
				break;
			case '-':
				if (!strcmp(optarg,"list")) {
					__is_ldd = true;
					break;
				}
				fprintf(stderr, "%s: unrecognized option '--%s'\n", argv[0], optarg);
				// fallthrough
			default:
				return usage(argv);
		}
	}

	if (!file) {
		if (argc == optind) return usage(argv);
		file = argv[optind];
	}

	int fd = open(file, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], file, strerror(errno));
		return 1;
	}

	return __libc_load_from_file(fd, file, argc - optind, &argv[optind]);
}

