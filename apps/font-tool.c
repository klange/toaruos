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
			" -s --strings    \033[3mPrint all supported entries in the names table.\033[0m\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			"\n",
			argv[0]);
}

#define SHOW_NAME    (1 << 0)
#define SHOW_STRINGS (1 << 1)
extern char * tt_get_name_string(struct TT_Font * font, int identifier);

int main(int argc, char * argv[]) {
	static struct option long_opts[] = {
		{"name", no_argument, 0, 'n'},
		{"help", no_argument, 0, 'h'},
		{"strings", no_argument, 0, 's'},
		{0,0,0,0}
	};

	int flags = 0;

	/* Read some arguments */
	int index, c;
	while ((c = getopt_long(argc, argv, "nhs", long_opts, &index)) != -1) {
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
			case 's':
				flags |= SHOW_STRINGS;
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
		fprintf(stdout, "%s\n", tt_get_name_string(my_font, 4));
	}

	if (flags & SHOW_STRINGS) {
		struct IDName {
			int identifier;
			const char * description;
		} names[] = {
			{0, "Copyright"},
			{1, "Font family"},
			{2, "Font style"},
			{3, "Subfamily identification"},
			{4, "Full name"},
			{5, "Version"},
			{6, "PostScript name"},
			{7, "Trademark notice"},
			{8, "Manufacturer"},
			{9, "Designer"},
			{10, "Description"},
			{11, "Vendor URL"},
			{12, "Designer URL"},
			{13, "License description"},
			{14, "License URL"},
			/* 15 is reserved */
			{16, "Preferred family"},
			{17, "Preferred subfamily"},
			{18, "macOS name"},
			{19, "Sample text"},
			/* Other stuff */
		};

		for (size_t i = 0; i < sizeof(names)/sizeof(*names); ++i) {
			char * value = tt_get_name_string(my_font, names[i].identifier);
			if (value) {
				fprintf(stdout, "%s: %s\n", names[i].description, value);
			}
		}
	}

	return 0;
}

