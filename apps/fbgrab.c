/**
 * @brief Attempt to grab framebuffer contents
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <kernel/video.h>
#include <toaru/graphics.h>

static int usage(int argc, char * argv[]) {
	fprintf(stderr, "usage: %s [output]\n",
		argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "")) != -1) {
		switch (opt) {
			default:
				return usage(argc, argv);
		}
	}

	char * out = "/tmp/fbgrab.tga";
	if (optind < argc) out = argv[optind];

	gfx_context_t * fb = init_graphics_fullscreen();
	if (!fb) err(1, "/dev/fb0");

	FILE * out_file = fopen(out, "w");
	if (!out_file) err(1, out);

	if (gfx_buffer_write(out_file, fb, GFX_WRITE_FORMAT_TARGA)) errx(1, "write error");

	return 0;
}
