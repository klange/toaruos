/**
 * @brief See if the graphics library can load an image.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <toaru/graphics.h>

int main (int argc, char * argv[]) {
	int quiet = 0;
	int opt;
	while ((opt = getopt(argc, argv, "q")) != -1) {
		switch (opt) {
			case 'q':
				quiet = 1;
				break;
			case '?':
				return 1;
		}
	}

	int ret = 0;
	while (optind < argc) {
		sprite_t * image = calloc(sizeof(sprite_t),1);
		if (load_sprite(image, argv[optind])) {
			if (!quiet) {
				fprintf(stdout, "%s: invalid\n", argv[optind]);
			}
			free(image);
			ret |= 1;
			optind++;
			continue;
		}

		if (!quiet) {
			fprintf(stdout, "%s: %d x %d\n", argv[optind], image->width, image->height);
		}

		sprite_free(image);
		optind++;
	}

	return ret;
}
 

