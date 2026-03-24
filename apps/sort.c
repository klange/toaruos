/**
 * @brief Sort input lines.
 *
 * XXX for reasons unknown this is using its own insertion-sort
 *     instead of our much nicer quicksort?
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <toaru/list.h>

int compare(const char * a, const char * b) {
	while (1) {
		while (*a == *b || tolower(*a) == tolower(*b)) {
			if (!*a) return 0;
			a++;
			b++;
		}

		while (*a && !isalnum(*a)) a++;
		while (*b && !isalnum(*b)) b++;

		if (tolower(*a) == tolower(*b)) continue;

		if (tolower(*a) < tolower(*b)) return -1;
		return 1;
	}
}

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-r] [file...]\n"
		"\n"
		"Reads lines from all of the specified files, sorts them all, and prints the result.\n",
		argv[0]);
	return 2;
}

int main(int argc, char * argv[]) {
	int reverse = 0;
	int opt;

	list_t * lines = list_create();
	list_t * files = list_create();

	while ((opt = getopt(argc, argv, "?r")) != -1) {
		switch (opt) {
			case 'r':
				reverse = 1;
				break;
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		/* No arguments */
		list_insert(files, stdin);
	} else {
		while (optind < argc) {
			if (!strcmp(argv[optind], "-")) {
				list_insert(files, stdin);
				optind++;
				continue;
			}
			FILE * f = fopen(argv[optind], "r");
			if (!f) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
			} else {
				list_insert(files, f);
			}
			optind++;
		}
	}

	foreach (node, files) {
		FILE * f = node->value;
		while (!feof(f)) {
			char * line = NULL;
			size_t avail = 0;
			ssize_t len;

			if ((len = getline(&line, &avail, f)) < 0) break;

			node_t * next = NULL;
			foreach (lnode, lines) {
				char * cmp = lnode->value;
				if (reverse ? (compare(cmp, line) < 0) : (compare(line, cmp) < 0)) {
					next = lnode;
					break;
				}
			}
			if (next) {
				list_insert_before(lines, next, line);
			} else {
				list_insert(lines, line);
			}
		}
	}

	foreach (lnode, lines) {
		char * line = lnode->value;
		fprintf(stdout, "%s", line);
	}

	return 0;
}
