#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "lib/window.h"
#include "lib/graphics.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

uint16_t win_width;
uint16_t win_height;
gfx_context_t * ctx;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

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
	setup_windowing();

	int width  = wins_globals->server_width;
	int height = wins_globals->server_height;

	win_width = width;
	win_height = height;


	/* Do something with a window */
	window_t * wina = window_create(0,0, width, height);
	assert(wina);
	window_reorder (wina, 0);
	ctx = init_graphics_window_double_buffer(wina);
	draw_fill(ctx, rgb(127,127,127));
	flip(ctx);

#if 1
	printf("Loading background...\n");
	init_sprite(0, "/usr/share/login-background.bmp", NULL);
	printf("Background loaded.\n");
	draw_sprite_scaled(ctx, sprites[0], 0, 0, width, height);
#endif

#if 0
	init_sprite(1, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
	draw_sprite_scaled(sprites[1], center_x(sprites[1]->width), center_y(sprites[1]->height), sprites[1]->width, sprites[1]->height);
#endif

	flip(ctx);
	window_redraw_full(wina);

	while (1) {
		syscall_yield();
		//syscall_wait(wins_globals->server_pid);
	}


#if 0
	window_destroy(window); // (will close on exit)
	teardown_windowing();
#endif

	return 0;
}
