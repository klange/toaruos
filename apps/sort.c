/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * sort - Sort standard in or files.
 *
 * Currently implemented with a naive insertion sort.
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

int main(int argc, char * argv[]) {
	int reverse = 0;
	int opt;

	list_t * lines = list_create();
	list_t * files = list_create();

	while ((opt = getopt(argc, argv, "r")) != -1) {
		switch (opt) {
			case 'r':
				reverse = 1;
				break;
		}
	}

	if (optind == argc) {
		/* No arguments */
		list_insert(files, stdin);
	} else {
		while (optind < argc) {
			FILE * f = fopen(argv[optind], "r");
			if (!f) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
			} else {
				list_insert(files, f);
			}
			optind++;
		}
	}

	char line_buf[4096] = {0};
	foreach (node, files) {
		FILE * f = node->value;
		while (!feof(f)) {
			if (!fgets(line_buf, 4096, f)) {
				break;
			}
			if (!strchr(line_buf,'\n')) {
				fprintf(stderr, "%s: oversized line\n", argv[0]);
			}
			char * line = strdup(line_buf);
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
