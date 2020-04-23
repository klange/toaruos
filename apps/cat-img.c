/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2018 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <toaru/graphics.h>
#include <toaru/termemu.h>

void get_cell_sizes(int * w, int * h) {
	struct winsize wsz;
	ioctl(0, TIOCGWINSZ, &wsz);

	if (!wsz.ws_col || !wsz.ws_row) {
		*w = 0;
		*h = 0;
	}

	*w = wsz.ws_xpixel / wsz.ws_col;
	*h = wsz.ws_ypixel / wsz.ws_row;
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

int usage(char * argv[]) {
	printf(
			"usage: %s [-?ns] [path]\n"
			"\n"
			" -n     \033[3mdon't print a new line after image\033[0m\n"
			" -s     \033[3mscale to cell height (up or down)\033[0m\n"
			" -w     \033[3mscale to terminal width (up or down)\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
	return 1;
}

int main (int argc, char * argv[]) {
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		fprintf(stderr, "Can't cat-img to a non-terminal.\n");
		exit(1);
	}

	int opt;
	int no_newline = 0;
	int scale_to_cell_height = 0;
	int scale_to_term_width = 0;

	while ((opt = getopt(argc, argv, "?nsw")) != -1) {
		switch (opt) {
			case '?':
				return usage(argv);
			case 'n':
				no_newline = 1;
				break;
			case 'w':
				scale_to_term_width = 1;
				break;
			case 's':
				scale_to_cell_height = 1;
				break;
		}
	}

	if (optind >= argc ) {
		return usage(argv);
	}

	int w, h;
	get_cell_sizes(&w, &h);

	if (!w || !h) return 1;

	while (optind < argc) {
		sprite_t * image = calloc(sizeof(sprite_t),1);
		load_sprite(image, argv[optind]);

		sprite_t * source = image;

		if (scale_to_cell_height) {
			int new_width = (h * image->width) / image->height;
			source = create_sprite(new_width,h,1);
			gfx_context_t * g = init_graphics_sprite(source);
			draw_fill(g, 0x00000000);
			draw_sprite_scaled(g, image, 0, 0, new_width, h);
			sprite_free(image);
		}

		if (scale_to_term_width) {
			struct winsize w;
			ioctl(0, TIOCGWINSZ, &w);
			int new_height = (w.ws_xpixel * image->height) / image->width;
			source = create_sprite(w.ws_xpixel, new_height, 1);
			gfx_context_t * g = init_graphics_sprite(source);
			draw_fill(g, 0x00000000);
			draw_sprite_scaled(g, image, 0, 0, w.ws_xpixel, new_height);
			sprite_free(image);
		}

		int width_in_cells = source->width / w;
		if (source->width % w) width_in_cells++;

		int height_in_cells = source->height / h;
		if (source->height % h) height_in_cells++;

		raw_output();
		printf("\033[?25l");

		for (int y = 0; y < height_in_cells; y++) {
			for (int x = 0; x < width_in_cells; x++) {
				printf("\033Ts");
				uint32_t * tmp = malloc(sizeof(uint32_t) * w * h);
				for (int yy = 0; yy < h; yy++) {
					for (int xx = 0; xx < w; xx++) {
						if (x*w + xx >= source->width || y*h + yy >= source->height) {
							tmp[yy * w + xx] = rgba(0,0,0,TERM_DEFAULT_OPAC);
						} else {
							uint32_t data = alpha_blend_rgba(
								rgba(0,0,0,TERM_DEFAULT_OPAC),
								premultiply(source->bitmap[(x*w+xx)+(y*h+yy)*source->width]));
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

		sprite_free(source);

		printf("\033[?25h");
		unraw_output();
		fflush(stdout);
		optind++;
	}

	return 0;
}
 
