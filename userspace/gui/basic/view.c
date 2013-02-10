/*
 * view
 *
 * Displays bitmap images in windows
 */
#include <stdlib.h>

#include "lib/window.h"
#include "lib/graphics.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;
gfx_context_t * ctx;

void init_sprite(int i, char * filename, char * alpha) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite(sprites[i], filename);
	if (alpha) {
		sprites[i]->alpha = 1;
		load_sprite(&alpha_tmp, alpha);
		sprites[i]->masks = alpha_tmp.bitmap;
	} else {
		sprites[i]->alpha = 0;
	}
	sprites[i]->blank = 0x0;
}

int main (int argc, char ** argv) {
	if (argc < 2) {
		printf("usage: %s file\n", argv[0]);
		return -1;
	}

	int left = 30;
	int top  = 30;

	if (strstr(argv[1], ".png")) {
		sprites[0] = malloc(sizeof(sprite_t));
		load_sprite_png(sprites[0], argv[1]);
	} else {
		init_sprite(0, argv[1], NULL);
	}

	int width  = sprites[0]->width;
	int height = sprites[0]->height;

	setup_windowing();

	/* Do something with a window */
	window_t * wina = window_create(left, top, width, height);
	ctx = init_graphics_window(wina);
	draw_fill(ctx, rgb(0,0,0));

	if (sprites[0]->alpha == ALPHA_EMBEDDED) {
		draw_fill(ctx, rgba(0,0,0,0));
		window_enable_alpha(wina);
	}

	draw_sprite(ctx, sprites[0], 0, 0);

	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			if (kbd->key == 'q') {
				break;
			}
			free(kbd);
		}
	}

	teardown_windowing();

	return 0;
}
