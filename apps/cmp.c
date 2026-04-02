/**
 * @brief Compare files
 *
 * Standard POSIX utility.
 *
 * XXX Only use this with normal files; errors in special files
 *     will not be handled well with our current libc.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2025 K. Lange
 */
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-l | -s] file1 file2 [skip1 [skip2]]\n", argv[0]);
	return 2;
}

/**
 * I'm implementing this to the specification of the help page
 * from GNU diffutils' cmp even though most of it is pointless,
 * just because I thought it was fun.
 */
static int maybe_suffix(char * argv[], char * c, size_t *ret) {
	char * end = NULL;
	size_t out = strtoul(c, &end, 10);
	if (end == c) goto _invalid;

	size_t amount = 1024;
	int power = 0;

	switch (*end) {
		case '\0':
			break;

		/* These are in the diffutils help output but they
		 * would not be representable. */
		case 'Y':
			power += 1; /* fallthrough */
		case 'Z':
			power += 1; /* fallthrough */

		/* Up to 7E is actually technically viable, but anything
		 * over LONG_MAX we're going to force you to sit
		 * through fgetc until the end of the universe. */
		case 'E':
			power += 1; /* fallthrough */
		case 'P':
			power += 1; /* fallthrough */
		case 'T':
			power += 1; /* fallthrough */
		case 'G':
			power += 1; /* fallthrough */
		case 'M':
			power += 1; /* fallthrough */
		case 'K':
		case 'k':
			if (end[1] == 'B' && end[2] == '\0') amount = 1000;
			else if (end[1] != '\0') goto _invalid;
			power += 1;
			break;

		default:
			goto _invalid;
	}

	for (int i = 0; i < power; ++i) {
		if (__builtin_mul_overflow(out, amount, &out)) {
			fprintf(stderr, "%s: '%s' is too big\n", argv[0], c);
			return 1;
		}
	}

	*ret = out;
	return 0;

_invalid:
	fprintf(stderr, "%s: '%s' is not a valid skip amount\n", argv[0], c);
	return 1;
}

int main(int argc, char * argv[]) {
	int c;
	int format = 0;
	int retval = 0;
	while ((c = getopt(argc, argv, "ls")) != -1) {
		switch (c) {
			case 'l':
				format = 'l';
				break;
			case 's':
				format = 's';
				break;
			default:
				return usage(argv);
		}
	}

	if (optind + 1 >= argc || optind + 4 < argc) return usage(argv);

	const char * f_names[2];
	FILE * f_files[2];

	for (int i = 0; i < 2; ++i) {
		f_names[i] = argv[optind+i];
		if (!strcmp(f_names[i], "-")) {
			f_names[i] = "stdin";
			f_files[i] = stdin;
		} else {
			f_files[i] = fopen(f_names[i], "r");
			if (!f_files[i]) return fprintf(stderr, "%s: %s: %s\n", argv[0], f_names[i], strerror(errno)), 2;
		}
	}

	if (f_files[0] == stdin && f_files[1] == stdin) {
		fprintf(stderr, "stdin may only be specified for one argument\n");
		return 2;
	}

	for (int i = 0; optind + i + 2 < argc; ++i) {
		size_t skip;
		if (maybe_suffix(argv, argv[optind+2+i], &skip)) return 2;
		if (skip) {
			if (skip > LONG_MAX || fseek(f_files[i], skip, SEEK_SET)) {
				clearerr(f_files[i]);
				while (skip) {
					int c = fgetc(f_files[i]);
					if (c < 0 && ferror(f_files[i])) return fprintf(stderr, "%s: %s: %s\n", argv[0], f_names[i], strerror(errno)), 2;
					skip--;
				}
			}
		}
	}

	size_t count = 1;
	size_t line = 1;

	while (!feof(f_files[0]) && !feof(f_files[1])) {
		int c[2];
		for (int i = 0; i < 2; ++i) {
			c[i] = fgetc(f_files[i]);
			if (c[i] < 0 && ferror(f_files[i])) return fprintf(stderr, "%s: %s: %s\n", argv[0], f_names[i], strerror(errno)), 2;
		}

		if (c[0] != c[1]) {
			if (c[0] == EOF || c[1] == EOF) {
				if (format != 's') fprintf(stderr, "%s: EOF on %s\n", argv[0], f_names[c[1] == EOF]);
				return 1;
			}
			if (format == 0) fprintf(stdout, "%s %s differ: char %zu, line %zu\n", f_names[0], f_names[1], count, line);
			if (format != 'l') return 1;
			fprintf(stdout, "%zu %o %o\n", count, c[0], c[1]);
			retval = 1;
		}

		count += 1;
		if (c[0] == '\n') line += 1;
	}

	return retval;
}
