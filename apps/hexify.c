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
#include <errno.h>
#include <unistd.h>
#include <string.h>

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

static int stoih(int w, char c[w], unsigned int *out) {
	*out = 0;
	for (int i = 0; i < w; ++i) {
		(*out) <<= 4;
		if (c[i] >= '0' && c[i] <= '9') {
			*out |= (c[i] - '0');
		} else if (c[i] >= 'A' && c[i] <= 'F') {
			*out |= (c[i] - 'A' + 0xA);
		} else if (c[i] >= 'a' && c[i] <= 'f') {
			*out |= (c[i] - 'a' + 0xA);
		} else {
			*out = 0;
			return 1;
		}
	}

	return 0;
}

int main(int argc, char * argv[]) {
	unsigned int width = 16; /* TODO make configurable */
	int opt;
	int direction = 0;

	while ((opt = getopt(argc, argv, "?w:d")) != -1) {
		switch (opt) {
			default:
			case '?':
				fprintf(stderr, "%s: convert to/from hexadecimal dump\n", argv[0]);
				return 1;
			case 'w':
				width = atoi(optarg);
				break;
			case 'd':
				direction = 1;
				break;
		}
	}

	FILE * f;
	char * name;

	if (optind < argc) {
		f = fopen(argv[optind], "r");
		name = argv[optind];
		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
		}
	} else {
		name = "[stdin]";
		f = stdin;
	}

	if (direction == 0) {
		/* Convert to hexadecimal */

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

	} else {
		/* Convert from hexify's output format */
		unsigned int eoffset = 0;
		unsigned int lineno = 1;
		while (!feof(f)) {
			/* Read offset */
			char offset_bytes[8];
			fread(&offset_bytes, 1, 8, f);

			/* Convert offset for verification */
			unsigned int offset;
			if (stoih(8, offset_bytes, &offset)) {
				fprintf(stderr, "%s: %s: syntax error (bad offset) on line %d\n", argv[0], name, lineno);
				fprintf(stderr, "offset bytes: %8s\n", offset_bytes);
				return 1;
			}

			if (offset != eoffset) {
				fprintf(stderr, "%s: %s: offset mismatch on line %d\n", argv[0], name, lineno);
				fprintf(stderr, "expected 0x%x, got 0x%x\n", offset, eoffset);
				return 1;
			}

			/* Read ": " */
			char tmp[2];
			fread(&tmp, 1, 2, f);

			if (tmp[0] != ':' || tmp[1] != ' ') {
				fprintf(stderr, "%s: %s: syntax error (unexpected characters after offset) on line %d\n", argv[0], name, lineno);
				return 1;
			}

			/* Read [width] characters */
			for (unsigned int i = 0; i < width; ) {
				unsigned int byte = 0;
				for (unsigned int j = 0; i < width && j < 2; ++j, ++i) {
					fread(&tmp, 1, 2, f);
					if (tmp[0] == ' ' && tmp[1] == ' ') {
						/* done; return */
						return 0;
					}
					if (stoih(2, tmp, &byte)) {
						fprintf(stderr, "%s: %s: syntax error (bad byte) on line %d\n", argv[0], name, lineno);
						fprintf(stderr, "byte bytes: %2s\n", tmp);
						return 1;
					}
					fwrite(&byte, 1, 1, stdout);
				}
				/* Read space */
				fread(&tmp, 1, 1, f);
				if (tmp[0] != ' ') {
					fprintf(stderr, "%s: %s: syntax error (unexpected characters after byte) on line %d\n", argv[0], name, lineno);
					fprintf(stderr, "unexpected character: %c\n", tmp[0]);
					return 1;
				}
			}

			fread(&tmp, 1, 1, f);
			if (tmp[0] != ' ') {
				fprintf(stderr, "%s: %s: syntax error (unexpected characters after bytes) on line %d\n", argv[0], name, lineno);
			}

			/* Read correct number of characters, plus line feed */
			char tmp2[width+2];
			fread(&tmp2, 1, width+1, f);
			tmp2[width+1] = '\0';
			if (tmp2[width] != '\n') {
				fprintf(stderr, "%s: %s: syntax error: expected end of line, got garbage on line %d\n", argv[0], name, lineno);
				fprintf(stderr, "eol data: %s\n", tmp2);
			}

			lineno++;
			eoffset += width;
		}
	}
	return 0;
}
