#include <stdio.h>
#include <sys/utsname.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/drawstring.h"

#define PANEL_HEIGHT 24

static yutani_t * yctx;
static yutani_window_t * wallpaper_window;
static gfx_context_t * wallpaper_ctx;
static yutani_window_t * panel_window;
static gfx_context_t * panel_ctx;

static void draw_background(int width, int height) {
	draw_fill(wallpaper_ctx, rgb(110,110,110));
}

static void draw_panel(int width) {
	char label[100];
	struct utsname u;
	uname(&u);
	sprintf(label, "ToaruOS-NIH %s", u.release);
	draw_fill(panel_ctx, rgb(20,20,20));
	draw_string(panel_ctx, 1, 2, rgb(255,255,255), label);
}

static void resize_finish_wallpaper(int width, int height) {
	yutani_window_resize_accept(yctx, wallpaper_window, width, height);
	reinit_graphics_yutani(wallpaper_ctx, wallpaper_window);
	draw_background(width, height);
	yutani_window_resize_done(yctx, wallpaper_window);
	yutani_flip(yctx, wallpaper_window);
}

static void resize_finish_panel(int width, int height) {
	yutani_window_resize_accept(yctx, panel_window, width, height);
	reinit_graphics_yutani(panel_ctx, panel_window);
	draw_panel(width);
	yutani_window_resize_done(yctx, panel_window);
	yutani_flip(yctx, panel_window);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();

	/* wallpaper */
	wallpaper_window = yutani_window_create(yctx, yctx->display_width, yctx->display_height);
	yutani_window_move(yctx, wallpaper_window, 0, 0);
	yutani_set_stack(yctx, wallpaper_window, YUTANI_ZORDER_BOTTOM);

	wallpaper_ctx = init_graphics_yutani(wallpaper_window);
	draw_background(yctx->display_width, yctx->display_height);
	yutani_flip(yctx, wallpaper_window);

	/* panel */
	panel_window = yutani_window_create(yctx, yctx->display_width, PANEL_HEIGHT);
	yutani_window_move(yctx, panel_window, 0, 0);
	yutani_set_stack(yctx, panel_window, YUTANI_ZORDER_TOP);

	panel_ctx = init_graphics_yutani(panel_window);
	draw_panel(yctx->display_width);
	yutani_flip(yctx, panel_window);

	int should_exit = 0;

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_WELCOME:
					fprintf(stderr, "Request to resize desktop received, resizing to %d x %d\n", yctx->display_width, yctx->display_height);
					yutani_window_resize_offer(yctx, wallpaper_window, yctx->display_width, yctx->display_height);
					yutani_window_resize_offer(yctx, panel_window, yctx->display_width, PANEL_HEIGHT);
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == wallpaper_window->wid) {
							resize_finish_wallpaper(wr->width, wr->height);
						} else if (wr->wid == panel_window->wid) {
							resize_finish_panel(wr->width, wr->height);
						}
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

	yutani_close(yctx, wallpaper_window);

	return 0;
}

