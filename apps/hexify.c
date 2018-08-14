/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * hexify - Convert binary to hex.
 *
 * This is based on the output of xxd.
 * Does NOT a hex-to-bin option - something to consider.
 */
#include <stdio.h>
#include <ctype.h>

void print_line(unsigned char * buf, unsigned int width, unsigned int sizer, unsigned int offset) {
	fprintf(stdout, "%08x: ", sizer);
	for (unsigned int i = 0; i < width; ) {
		if (i >= offset) {
			fprintf(stdout, "  ");
		} else {
			fprintf(stdout, "%02x", buf[i]);
		}
		i++;
		if (i == width) break; /* in case of odd width */
		if (i >= offset) {
			fprintf(stdout, "   ");
		} else {
			fprintf(stdout, "%02x ", buf[i]);
		}
		i++;
	}
	fprintf(stdout, " ");
	for (unsigned int i = 0; i < width; i++) {
		if (i >= offset) {
			fprintf(stdout, " ");
		} else {
			if (isprint(buf[i])) {
				fprintf(stdout, "%c", buf[i]);
			} else {
				fprintf(stdout, ".");
			}
		}
	}
	fprintf(stdout, "\n");
}

int main(int argc, char * argv[]) {
	FILE * f;
	unsigned int width = 16; /* TODO make configurable */
	if (argc > 1) {
		f = fopen(argv[1], "r");
		if (!f) return 1;
	} else {
		f = stdin;
	}

	unsigned int sizer = 0;
	unsigned int offset = 0;
	unsigned char buf[width];
	while (!feof(f)) {
		unsigned int r = fread(buf+offset, 1, width-offset, f);
		offset += r;

		if (offset == width) {
			print_line(buf, width, sizer, offset);
			offset = 0;
			sizer += width;
		}
	}

	if (offset != 0) {
		print_line(buf, width, sizer, offset);
	}
	return 0;
}
