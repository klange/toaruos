/**
 * @file apps/font-tool.c
 * @brief Print information about TrueType fonts.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <getopt.h>

#include <toaru/graphics.h>
#include <toaru/text.h>

static void usage(char * argv[]) {
	printf(
			"usage: %s [-n] [FONT]\n"
			"Print information about TrueType fonts. If FONT is not specified,\n"
			"the system monospace font will be used.\n"
			"\n"
			" -n --name       \033[3mPrint the stored name of the font.\033[0m\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			"\n",
			argv[0]);
}

#define SHOW_NAME (1 << 0)

int main(int argc, char * argv[]) {
	static struct option long_opts[] = {
		{"name", no_argument, 0, 'n'},
		{"help", no_argument, 0, 'h'},
		{0,0,0,0}
	};

	int flags = 0;

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "nh", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'h':
				usage(argv);
				return 0;
				break;
			case 'n':
				flags |= SHOW_NAME;
				break;
			default:
				break;
		}
	}

	struct TT_Font * my_font;

	if (optind < argc) {
		my_font = tt_font_from_file(argv[optind]);
		if (!my_font) {
			fprintf(stderr, "%s: %s: Could not load font.\n", argv[0], argv[optind]);
			return 1;
		}
	} else {
		my_font = tt_font_from_shm("monospace");
		if (!my_font) {
			fprintf(stderr, "Unknown.\n");
			return 1;
		}
	}

	if (flags & SHOW_NAME) {
		extern char * tt_get_name_string(struct TT_Font * font, int identifier);
		fprintf(stdout, "%s\n", tt_get_name_string(my_font, 4));
	}

	return 0;
}

