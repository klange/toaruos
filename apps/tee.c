/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * tee - copy stdin to stdout and to specified files
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

int main(int argc, char * argv[]) {
	int append = 0;
	int opt;

	while ((opt = getopt(argc, argv, "ai")) != -1) {
		switch (opt) {
			case 'a':
				append = 1;
				break;
			case 'i':
				signal(SIGINT, SIG_IGN);
				break;
		}
	}

	int file_count = argc - optind;
	FILE ** files = malloc(sizeof(FILE *) * file_count);

	for (int i = 0, j = optind; j < argc && i < file_count; j++) {
		files[i] = fopen(argv[j], append ? "a" : "w");
		if (!files[i]) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[j], strerror(errno));
			file_count--;
			continue;
		} else {
			i++;
		}
	}

	while (!feof(stdin)) {
		int c = fgetc(stdin);
		if (c >= 0) {
			fputc(c, stdout);

			for (int i = 0; i < file_count; ++i) {
				fputc(c, files[i]);
			}
		}
	}

	for (int i = 0; i < file_count; ++i) {
		fclose(files[i]);
	}

	return 0;
}
