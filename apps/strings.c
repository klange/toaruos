/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * strings - print printable character sequences found in a file
 */
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char * argv[]) {
	int opt;
	char format = 0;
	int min_chars = 4;
	int ret_val = 0;

	while ((opt = getopt(argc, argv, "an:t:")) != -1) {
		switch (opt) {
			case 'a':
				/* TODO: With ELF support, read only the string table
				 * by default, unless this option is specified. */
				break;
			case 'n':
				min_chars = atoi(optarg);
				break;
			case 't':
				format = optarg[0];
				if (format != 'd' && format != 'x') {
					fprintf(stderr, "%s: format '%c' is not supported\n", argv[0], format);
					format = 0;
				}
				break;
		}
	}

	for (int i = optind; i < argc; ++i) {
		FILE * f = fopen(argv[i], "r");
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
			ret_val = 1;
			continue;
		}

		char buf[1024] = {0};
		int  ind = 0;
		size_t offset = 0;

		while (!feof(f)) {
			int c = fgetc(f);
			if (c < 0) break;
			if (c == '\n' || c == '\0') {
				if (ind >= min_chars) {
					switch (format) {
						case 'x':
							fprintf(stdout, "%lx ", offset - ind);
							break;
						case 'd':
							fprintf(stdout, "%lu ", offset - ind);
							break;
						default:
							break;
					}
					buf[ind] = '\0';
					fprintf(stdout, "%s\n", buf);
				}
				ind = 0;
				offset++;
				continue;
			}
			if (!isprint(c)) {
				ind = 0;
			} else {
				if (ind < 1024) {
					buf[ind] = c;
					ind++;
				}
			}
			offset++;
		}
		fclose(f);
	}

	return ret_val;
}
