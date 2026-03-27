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

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - query the kernel command line\n"
			"\n"
			"usage: %s -g " X_S "ARG" X_E "...\n"
			"       %s -q " X_S "ARG" X_E "...\n"
			"       %s -s\n"
			"\n"
			" -g " X_S "ARG   print the value for the requested argument" X_E "\n"
			" -q " X_S "ARG   query whether the requested argument is present (0 = yes)" X_E "\n"
			" -s       " X_S "query the size of the command line, returned as the" X_E "\n"
			"          " X_S "exit status (which may overflow, making this not" X_E "\n"
			"          " X_S "very useful in practice.)" X_E "\n"
			" -?       " X_S "show this help text" X_E "\n"
			"\n", argv[0], argv[0], argv[0], argv[0]);
	return 1;
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
				return show_usage(argc, argv);
		}
	}

	if (optind != argc) return show_usage(argc, argv);

	fprintf(stdout, "%s\n", cmdline);
}
