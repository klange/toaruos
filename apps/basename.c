/**
 * @brief basename - print file name
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-z] string [suffix]\n"
		"       %s [-z] string string string...\n"
		"       %s [-z] [-a] [-s suffix] string...\n",
		argv[0], argv[0], argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	char * suffix = NULL;
	int all_strings = 0;
	int opt;
	int endchr = '\n';

	while ((opt = getopt(argc, argv, "?as:z")) != -1) {
		switch (opt) {
			case 'a':
				all_strings = 1;
				break;
			case 's':
				suffix = optarg;
				break;
			case 'z':
				endchr = '\0';
				break;
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		return usage(argv);
	}

	if (optind + 2 < argc) {
		/* If there are more than 2 arguments, treat them all as 'string' */
		all_strings = 1;
	} else if (!all_strings && !suffix && optind + 2 == argc) {
		/* If we aren't already treating everything as strings from -a, or from having -s,
		 * and we have 2 arguments, then the second one is the suffix. */
		suffix = argv[optind+1];
		argc--;
	}

	for (int i = optind; i < argc; ++i) {
		char * c = basename(argv[i]);
		if (suffix) {
			char * found = strstr(c + strlen(c) - strlen(suffix), suffix);
			/* Only strip the suffix if it wasn't the whole remaining string. */
			if (found && found != c) *found = '\0';
		}

		printf("%s%c", c, endchr);
	}

	return 0;
}
