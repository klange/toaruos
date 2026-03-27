/**
 * @brief Rudimentary manpage viewer.
 *
 * Searches for a manual page at /usr/share/man/manN/PAGE.N,
 * and if one is found it is pasesed to a 'roff|more' pipeline
 * for display.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
		"usage: %s [" X_S "section" X_E "] " X_S "page" X_E "\n"
		"\n"
		"Display a manual page.\n"
		"\n"
		"Options:\n"
		"\n"
		" --help   " X_S "Show this help text." X_E "\n"
		"\n", argv[0]);
	return 1;
}

static int sanity_check(char * c) {
	while (*c) {
		if (*c == '\'' || *c == '\n') return 1;
		c++;
	}
	return 1;
}

static int try_section(int i, char * page) {
	char * filename;
	asprintf(&filename, "/usr/share/man/man%d/%s.%d", i, page, i);
	struct stat st;
	if (!stat(filename, &st)) {
		char * systemcmd;
		asprintf(&systemcmd, "roff '%s' | more -rP'%s(%i)'", filename, page, i);
		system(systemcmd);
		return 1;
	}
	free(filename);
	asprintf(&filename, "/usr/share/man/man%d/%s.%d.gz", i, page, i);
	if (!stat(filename, &st)) {
		char * systemcmd;
		asprintf(&systemcmd, "gunzip -c '%s' | roff - | more -rP'%s(%i)'", filename, page, i);
		system(systemcmd);
		return 1;
	}
	return 0;
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "?-:")) != -1) {
		switch (opt) {
			case '-':
				if (!strcmp(optarg,"help")) {
					usage(argv);
					return 0;
				}
				fprintf(stderr, "%s: '--%s' is not a recognized long option.\n", argv[0], optarg);
				/* fallthrough */
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		return 0;
	}

	if (optind + 1 == argc) {
		if (!sanity_check(argv[optind])) {
			fprintf(stderr, "%s: rejecting suspicious page name\n", argv[0]);
			return 1;
		}
		/* Search for the right section */
		for (int i = 1; i <= 9; ++i) {
			if (try_section(i, argv[optind])) return 0;
		}
	} else if (optind + 2 == argc) {
		int section = atoi(argv[optind]);
		if (section < 1 || section > 9) {
			fprintf(stderr, "%s: unknown section '%s'\n", argv[0], argv[optind]);
			return 1;
		}

		if (try_section(section, argv[optind+1])) return 0;

		/* for shared error message */
		optind++;
	} else {
		fprintf(stderr, "%s: too many arguments\n", argv[0]);
		return 1;
	}

	fprintf(stderr, "No manual entry for %s\n", argv[optind]);
	return 1;
}
