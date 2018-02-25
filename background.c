#include <stdio.h>

#include "lib/yutani.h"
#include "lib/graphics.h"

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;

static void draw_background(int width, int height) {
	draw_fill(ctx, rgb(110,110,110));
}

static void resize_finish(int width, int height) {
	yutani_window_resize_accept(yctx, wina, width, height);
	reinit_graphics_yutani(ctx, wina);
	draw_background(width, height);
	yutani_window_resize_done(yctx, wina);
	yutani_flip(yctx, wina);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();
	wina = yutani_window_create(yctx, yctx->display_width, yctx->display_height);
	yutani_window_move(yctx, wina, 0, 0);
	yutani_set_stack(yctx, wina, YUTANI_ZORDER_BOTTOM);
	ctx = init_graphics_yutani(wina);
	draw_background(yctx->display_width, yctx->display_height);
	yutani_flip(yctx, wina);

	int should_exit = 0;

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_WELCOME:
					fprintf(stderr, "Request to resize desktop received, resizing to %d x %d\n", yctx->display_width, yctx->display_height);
					yutani_window_resize_offer(yctx, wina, yctx->display_width, yctx->display_height);
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						resize_finish(wr->width, wr->height);
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
		}
		free(m);
	}

	yutani_close(yctx, wina);

	return 0;
}

