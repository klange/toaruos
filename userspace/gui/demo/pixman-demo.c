/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
#include <stdio.h>
#include <pixman.h>

#include "lib/yutani.h"
#include "lib/graphics.h"

static yutani_t * yctx;
static yutani_window_t * window;
static gfx_context_t * ctx;

int main(int argc, char * argv[]) {

#define WIDTH 400
#define HEIGHT 400
#define TILE_SIZE 25

	yctx = yutani_init();
	window = yutani_window_create(yctx,WIDTH,HEIGHT);
	yutani_window_move(yctx, window, 100, 100);
	ctx = init_graphics_yutani(window);
	draw_fill(ctx, rgba(0,0,0,255));

	pixman_image_t *checkerboard;
	pixman_image_t *destination;
#define D2F(d) (pixman_double_to_fixed(d))
	pixman_transform_t trans = { {
		{ D2F (-1.96830), D2F (-1.82250), D2F (512.12250)},
			{ D2F (0.00000), D2F (-7.29000), D2F (1458.00000)},
			{ D2F (0.00000), D2F (-0.00911), D2F (0.59231)},
	}};
	int i, j;

	checkerboard = pixman_image_create_bits (PIXMAN_a8r8g8b8,
			WIDTH, HEIGHT,
			NULL, 0);

	destination = pixman_image_create_bits (PIXMAN_a8r8g8b8,
			WIDTH, HEIGHT,
			NULL, 0);

	for (i = 0; i < HEIGHT / TILE_SIZE; ++i)
	{
		for (j = 0; j < WIDTH / TILE_SIZE; ++j)
		{
			double u = (double)(j + 1) / (WIDTH / TILE_SIZE);
			double v = (double)(i + 1) / (HEIGHT / TILE_SIZE);
			pixman_color_t black = { 0, 0, 0, 0xffff };
			pixman_color_t white = {
				v * 0xffff,
				u * 0xffff,
				(1 - (double)u) * 0xffff,
				0xffff };
			pixman_color_t *c;
			pixman_image_t *fill;

			if ((j & 1) != (i & 1))
				c = &black;
			else
				c = &white;

			fill = pixman_image_create_solid_fill (c);

			pixman_image_composite (PIXMAN_OP_SRC, fill, NULL, checkerboard,
					0, 0, 0, 0, j * TILE_SIZE, i * TILE_SIZE,
					TILE_SIZE, TILE_SIZE);
		}
	}

	pixman_image_set_transform (checkerboard, &trans);
	pixman_image_set_filter (checkerboard, PIXMAN_FILTER_BEST, NULL, 0);
	pixman_image_set_repeat (checkerboard, PIXMAN_REPEAT_NONE);

	pixman_image_composite (PIXMAN_OP_SRC,
			checkerboard, NULL, destination,
			0, 0, 0, 0, 0, 0,
			WIDTH, HEIGHT);

	printf("Going for native draw.\n");

	uint32_t * buf = pixman_image_get_data(destination);
	memcpy(ctx->buffer,buf,WIDTH*HEIGHT*4);

	yutani_flip(yctx, window);

	while (1) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							free(m);
							goto done;
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					goto done;
				default:
					break;
			}
		}
		free(m);
	}

done:
	yutani_close(yctx, window);
	return 0;
}
