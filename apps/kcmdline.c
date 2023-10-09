/**
 * @brief kcmdline - Parse /proc/cmdline usefully.
 *
 * Parses /proc/cmdline and provides an interface for querying
 * whether an argument was present, and its value if applicable.
 *
 * Also converts ASCII field separators to spaces so that cmdline
 * arguments can have spaces in them.
 *
 * Useful for shell scripts.
 *
 * TODO: This should probably be a library we can use in other
 *       applications...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <toaru/hashmap.h>

#include "../kernel/misc/args.c"

void show_usage(int argc, char * argv[]) {
	printf(
			"kcmdline - query the kernel command line\n"
			"\n"
			"usage: %s -g ARG...\n"
			"       %s -q ARG...\n"
			"\n"
			" -g     \033[3mprint the value for the requested argument\033[0m\n"
			" -q     \033[3mquery whether the requested argument is present (0 = yes)\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0], argv[0]);
}

int main(int argc, char * argv[]) {
	char * cmdline = args_from_procfs();

	/* Figure out what we're doing */
	int opt;
	while ((opt = getopt(argc, argv, "?g:q:s")) != -1) {
		switch (opt) {
			case 'g':
				if (hashmap_has(kernel_args_map, optarg)) {
					char * tmp = (char*)hashmap_get(kernel_args_map, optarg);
					if (!tmp) {
						printf("%s\n", optarg); /* special case = present but not set should yield name of variable */
					} else {
						printf("%s\n", tmp);
					}
					return 0;
				} else {
					return 1;
				}
			case 'q':
				return !hashmap_has(kernel_args_map,optarg);
			case 's':
				return strlen(cmdline);
			case '?':
				show_usage(argc, argv);
				return 1;
		}
	}

	fprintf(stdout, "%s\n", cmdline);
}
