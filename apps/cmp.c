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

static int usage(char * argv[]) {
	fprintf(stderr, "usage: %s [-l | -s] file1 file2\n", argv[0]);
	return 2;
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

	if (optind + 1 >= argc) return usage(argv);

	const char * file_a = argv[optind];
	const char * file_b = argv[optind+1];


	FILE * a = fopen(file_a, "r");
	if (!a) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], file_a, strerror(errno));
		return 2;
	}

	FILE * b = fopen(file_b, "r");
	if (!b) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], file_b, strerror(errno));
		fclose(a);
		return 2;
	}

	size_t count = 1;
	size_t line = 1;

	while (!feof(a) && !feof(b)) {
		int _a = fgetc(a);
		int _b = fgetc(b);

		if (_a != _b) {
			if (_a == EOF || _b == EOF) {
				retval = 1;
				if (format != 's') fprintf(stderr, "%s: EOF on %s\n", argv[0], _a == EOF ? file_a : file_b);
				goto finish;
			}
			switch (format) {
				case 0:
					fprintf(stdout, "%s %s differ: char %zu, line %zu\n", file_a, file_b, count, line);
					/* fallthrough */
				case 's':
					retval = 1;
					goto finish;
				case 'l':
					fprintf(stdout, "%zu %o %o\n", count, _a, _b);
					retval = 1;
					break;
			}

		}

		count += 1;
		if (_a == '\n') line += 1;
	}

finish:
	fclose(a);
	fclose(b);
	return retval;
}
