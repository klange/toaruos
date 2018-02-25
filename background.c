#include "lib/yutani.h"
#include "lib/graphics.h"

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;


int main (int argc, char ** argv) {
	yctx = yutani_init();
	wina = yutani_window_create(yctx, yctx->display_width, yctx->display_height);
	yutani_window_move(yctx, wina, 0, 0);
	yutani_set_stack(yctx, wina, YUTANI_ZORDER_BOTTOM);
	ctx = init_graphics_yutani(wina);
	draw_fill(ctx, rgb(110,110,110));

	int should_exit = 0;

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
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

