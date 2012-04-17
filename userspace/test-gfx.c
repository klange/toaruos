#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/decorations.h"

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

void darken(gfx_context_t * ctx) {
	for (uint16_t y = 0; y < ctx->height; ++y) {
		for (uint16_t x = 0; x < ctx->width; ++x) {
			GFX(ctx, x, y) = alpha_blend(GFX(ctx,x,y), rgb(0,0,0), rgb(1,0,0));
		}
	}
}

int main (int argc, char ** argv) {
	setup_windowing();

	int width  = 600;
	int height = 400;

	win_width = width;
	win_height = height;

	init_decorations();


	/* Do something with a window */
	window_t * wina = window_create(300, 300, width, height);
	assert(wina);
	ctx = init_graphics_window_double_buffer(wina);

	draw_fill(ctx, rgb(0,0,0));
	flip(ctx);

#if 0
	printf("Loading background...\n");
	init_sprite(0, "/usr/share/login-background.bmp", NULL);
	printf("Background loaded.\n");
	draw_sprite_scaled(ctx, sprites[0], 0, 0, width, height);
#endif

#if 1
	init_sprite(1, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
#endif

	flip(ctx);


	uint32_t i = 0;

	while (1) {
		++i;
		double herp = cos((double)i * 0.01) + 1.5;
		double derp = sin((double)i * 0.01) + 1.5;
		char ch = 0;
		w_keyboard_t * kbd;
		do {
			kbd = poll_keyboard();
			if (kbd != NULL) {
				ch = kbd->key;
				free(kbd);
			}
		} while (kbd != NULL);
		if (ch == 'q') {
			goto done;
			break;
		}
		//draw_fill(ctx, rgb(0,0,0));
		darken(ctx);
		draw_sprite_scaled(ctx, sprites[1], center_x(sprites[1]->width * herp), center_y(sprites[1]->height * derp), sprites[1]->width * herp, sprites[1]->height * derp);
		render_decorations(wina, ctx->backbuffer, "Graphics Test");
		flip(ctx);
	}
done:

#if 1
	teardown_windowing();
#endif

	return 0;
}
