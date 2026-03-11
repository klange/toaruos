/**
 * @brief hexify - Convert binary to hex.
 *
 * This is based on the output of xxd.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

void print_line(unsigned char * buf, size_t width, size_t sizer, size_t offset) {
	fprintf(stdout, "%08zx: ", sizer);
	for (size_t i = 0; i < width; ) {
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
	for (size_t i = 0; i < width; i++) {
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

static int stoih(size_t w, char c[w], size_t *out) {
	*out = 0;
	for (size_t  i = 0; i < w; ++i) {
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

static int usage(char * argv[]) {
	fprintf(stderr,
			"heixfy - convert to and from hexadecimal representation\n"
			"\n"
			"usage: %s [-w width] [-d] [file]\n"
			"\n"
			" -w width  \033[3mdisplay 'width' bytes per line\033[0m\n"
			"           \033[3m(default is 16, max is 256)\033[0m\n"
			" -d        \033[3mconvert output from hexify back to binary data\033[0m\n"
			" -?        \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	size_t width = 16; /* TODO make configurable */
	int opt;
	int direction = 0;

	while ((opt = getopt(argc, argv, "?w:d")) != -1) {
		switch (opt) {
			default:
			case '?':
				return usage(argv);
			case 'w':
				width = strtoul(optarg, NULL, 0);
				if (width == 0) width = 16;
				if (width > 256) {
					fprintf(stderr, "%s: invalid width\n", argv[0]);
					return 1;
				}
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
		if (!f) goto _fail;
	} else {
		name = "[stdin]";
		f = stdin;
	}

	if (direction == 0) {
		/* Convert to hexadecimal */

		size_t sizer = 0;
		size_t offset = 0;
		unsigned char buf[width];
		while (!feof(f)) {
			size_t r = fread(buf+offset, 1, width-offset, f);
			if (r == 0 && ferror(f)) goto _fail;
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
		size_t eoffset = 0;
		size_t lineno = 1;
		while (!feof(f)) {
			/* Read offset */
			char offset_bytes[8];
			size_t r = fread(&offset_bytes, 1, 8, f);
			if (r == 0 && ferror(f)) goto _fail;

			/* Convert offset for verification */
			size_t offset;
			if (stoih(8, offset_bytes, &offset)) {
				fprintf(stderr, "%s: %s: syntax error (bad offset) on line %zu\n", argv[0], name, lineno);
				fprintf(stderr, "offset bytes: %8s\n", offset_bytes);
				return 1;
			}

			if (offset != eoffset) {
				fprintf(stderr, "%s: %s: offset mismatch on line %zu\n", argv[0], name, lineno);
				fprintf(stderr, "expected 0x%zx, got 0x%zx\n", offset, eoffset);
				return 1;
			}

			/* Read ": " */
			char tmp[2];
			r = fread(&tmp, 1, 2, f);
			if (r == 0 && ferror(f)) goto _fail;

			if (tmp[0] != ':' || tmp[1] != ' ') {
				fprintf(stderr, "%s: %s: syntax error (unexpected characters after offset) on line %zu\n", argv[0], name, lineno);
				return 1;
			}

			/* Read [width] characters */
			for (size_t i = 0; i < width; ) {
				size_t byte = 0;
				for (size_t j = 0; i < width && j < 2; ++j, ++i) {
					r = fread(&tmp, 1, 2, f);
					if (r == 0 && ferror(f)) goto _fail;
					if (tmp[0] == ' ' && tmp[1] == ' ') {
						/* done; return */
						return 0;
					}
					if (stoih(2, tmp, &byte)) {
						fprintf(stderr, "%s: %s: syntax error (bad byte) on line %zu\n", argv[0], name, lineno);
						fprintf(stderr, "byte bytes: %2s\n", tmp);
						return 1;
					}
					fwrite(&byte, 1, 1, stdout);
				}
				/* Read space */
				r = fread(&tmp, 1, 1, f);
				if (r == 0 && ferror(f)) goto _fail;
				if (tmp[0] != ' ') {
					fprintf(stderr, "%s: %s: syntax error (unexpected characters after byte) on line %zu\n", argv[0], name, lineno);
					fprintf(stderr, "unexpected character: %c\n", tmp[0]);
					return 1;
				}
			}

			r = fread(&tmp, 1, 1, f);
			if (r == 0 && ferror(f)) goto _fail;
			if (tmp[0] != ' ') {
				fprintf(stderr, "%s: %s: syntax error (unexpected characters after bytes) on line %zu\n", argv[0], name, lineno);
			}

			/* Read correct number of characters, plus line feed */
			char tmp2[width+2];
			r = fread(&tmp2, 1, width+1, f);
			if (r == 0 && ferror(f)) goto _fail;
			tmp2[width+1] = '\0';
			if (tmp2[width] != '\n') {
				fprintf(stderr, "%s: %s: syntax error: expected end of line, got garbage on line %zu\n", argv[0], name, lineno);
				fprintf(stderr, "eol data: %s\n", tmp2);
			}

			lineno++;
			eoffset += width;
		}
	}

	return 0;

_fail:
	fprintf(stderr, "%s: %s: %s\n", argv[0], name, strerror(errno));
	return 1;
}
