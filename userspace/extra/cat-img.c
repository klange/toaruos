/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016 Kevin Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "lib/graphics.h"
#include "gui/terminal/lib/termemu.h"

void get_cell_sizes(int * w, int * h) {
	struct termios old;
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
	printf("\033Tq");
	fflush(stdout);
	scanf("\033T%d;%dq", w, h);
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

void raw_output(void) {
	struct termios new;
	tcgetattr(fileno(stdin), &new);
	new.c_oflag &= (~ONLCR);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

void unraw_output(void) {
	struct termios new;
	tcgetattr(fileno(stdin), &new);
	new.c_oflag |= ONLCR;
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

int main (int argc, char * argv[]) {
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		fprintf(stderr, "Can't cat-img to a non-terminal.\n");
		exit(1);
	}
	int i = 1;
	int no_newline = 0;
	if (!strcmp(argv[1],"-n")) {
		i++;
		no_newline = 1;
	}
	int w, h;
	get_cell_sizes(&w, &h);
	sprite_t image;
	load_sprite_png(&image, argv[i]);

	int width_in_cells = image.width / w;
	if (image.width % w) width_in_cells++;

	int height_in_cells = image.height / h;
	if (image.height % h) height_in_cells++;

	raw_output();

	for (int y = 0; y < height_in_cells; y++) {
		for (int x = 0; x < width_in_cells; x++) {
			printf("\033Ts");
			uint32_t * tmp = malloc(sizeof(uint32_t) * w * h);
			for (int yy = 0; yy < h; yy++) {
				for (int xx = 0; xx < w; xx++) {
					if (x*w + xx >= image.width || y*h + yy >= image.height) {
						tmp[yy * w + xx] = rgba(0,0,0,TERM_DEFAULT_OPAC);
					} else {
						uint32_t data = alpha_blend_rgba(
							rgba(0,0,0,TERM_DEFAULT_OPAC),
							premultiply(image.bitmap[(x*w+xx)+(y*h+yy)*image.width]));
						tmp[yy * w + xx] = data;
					}
				}
			}
			fwrite(tmp, sizeof(uint32_t) * w * h, 1, stdout);
			free(tmp);
			fflush(stdout);
		}
		if (y != height_in_cells - 1 || !no_newline) {
			printf("\r\n");
		}
	}

	unraw_output();

	return 0;
}
 
