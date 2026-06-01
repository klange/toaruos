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

#include <libc/dlfcn/internal.h>

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-e exe] [args...]\n",
		argv[0]);
	return 1;
}

static int help(char * argv[]) {
	usage(argv);
	fprintf(stderr,
		"\nSupported options:\n"
		"\n"
		" -e exe            Use 'exe' as the binary to load\n"
		"                   instead of the first argument.\n"
		" --list            List dependencies (act as ldd).\n"
		" --preload libs    Preload libraries.\n"
		"\n"
		" --help            Show this help text.\n"
	);
	return 0;
}

__attribute__((visibility("hidden")))
int __ld_so_main(int argc, char * argv[]) {
	char * file = NULL;
	int opt;

	if (argc && !strcmp(basename(argv[0]),"ldd")) __is_ldd = true;

	struct option long_opts[] = {
		{"exe",     required_argument, 0, 'e'},
		{"list",    no_argument,       0, 1000},
		{"help",    no_argument,       0, 1001},
		{"preload", required_argument, 0, 1002},
		{0,0,0,0}
	};

	while ((opt = getopt_long(argc, argv, "+e:", long_opts, NULL)) != -1) {
		switch (opt) {
			case 'e':
				file = optarg;
				break;
			case 1000: /* --list */
				__is_ldd = true;
				break;
			case 1001: /* --help */
				return help(argv);
			case 1002: /* --preload */
				__ld_preload = optarg;
				break;
			default:
				return usage(argv);
		}
	}

	int _optind = optind;
	optind = 0;

	if (!file) {
		if (argc == _optind) return usage(argv);
		file = argv[_optind];
	}

	int fd = open(file, O_RDONLY | O_CLOEXEC);

	if (fd < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], file, strerror(errno));
		return 1;
	}

	return __libc_load_from_file(fd, file, argc - _optind, &argv[_optind]);
}

