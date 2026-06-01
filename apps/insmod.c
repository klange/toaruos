/**
 * @brief insmod - Load kernel module
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2026 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <sys/insmod.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-p] module [args...]\n", argv[0]);
	return 1;
}

static int help(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
		"%s - load a kernel module\n"
		"\n"
		"usage: %s [-p] module [args...]\n"
		"\n"
		" -p      " X_S "Search for module in module paths" X_E "\n"
		" --help  " X_S "Show this help text" X_E "\n"
		"\n",
		argv[0], argv[0]);
#undef X_E
#undef X_S
	return 0;
}

int main(int argc, char * argv[]) {
	int opt;
	int search_path = 0;

	while ((opt = getopt(argc, argv, "+p-:")) != -1) {
		switch (opt) {
			case 'p':
				search_path = 1;
				break;
			case '-':
				if (!strcmp(optarg, "help")) return help(argv);
				fprintf(stderr, "%s: unrecognized option '--%s'\n", argv[0], optarg);
				/* fallthrough */
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) return usage(argv);

	char * module = argv[optind];
	if (search_path) asprintf(&module, "/mod/%s.ko", argv[optind]);

	int fd = open(module, O_RDONLY);
	if (fd < 0) err(2, "open: %s", module);

	int status = insmod(fd, argc - optind, &argv[optind]);
	if (status == 1) err(2, "%s", argv[optind]);

	return 0;
}
