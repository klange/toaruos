/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * head - Print the first `n` lines of a file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

int main(int argc, char * argv[]) {
	int n = 10;
	int opt;
	int print_names = 0;
	int retval = 0;

	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch (opt) {
			case 'n':
				n = atoi(optarg);
				break;
		}
	}

	if (argc > optind + 1) {
		/* Multiple files */
		print_names = 1;
	}

	if (argc == optind) {
		/* This is silly, but should work due to reasons. */
		argv[optind] = "-";
		argc++;
	}

	for (int i = optind; i < argc; ++i) {
		FILE * f = (!strcmp(argv[i],"-")) ? stdin : fopen(argv[i],"r");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			retval = 1;
			continue;
		}

		if (print_names) {
			fprintf(stdout, "==> %s <==\n", (f == stdin) ? "standard input" : argv[i]);
		}

		int line = 1;

		while (!feof(f)) {
			int c = fgetc(f);
			if (c >= 0) {
				fputc(c, stdout);

				if (c == '\n') {
					line++;
					if (line > n) break;
				}
			}
		}

		if (f != stdin) {
			fclose(f);
		}
	}

	return retval;
}
