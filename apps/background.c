#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/drawstring.h>

static yutani_t * yctx;
static yutani_window_t * wallpaper_window;
static gfx_context_t * wallpaper_ctx;
static sprite_t * wallpaper;

static void draw_background(int width, int height) {

	float x = (float)wallpaper_window->width / (float)wallpaper->width;
	float y = (float)wallpaper_window->height / (float)wallpaper->height;

	int nh = (int)(x * (float)wallpaper->height);
	int nw = (int)(y * (float)wallpaper->width);

	if (nw == wallpaper->width && nh == wallpaper->height) {
		// special case
		draw_sprite(wallpaper_ctx, wallpaper, 0, 0);
	} else if (nw >= width) {
		draw_sprite_scaled(wallpaper_ctx, wallpaper, ((int)wallpaper_window->width - nw) / 2, 0, nw+2, wallpaper_window->height);
	} else {
		draw_sprite_scaled(wallpaper_ctx, wallpaper, 0, ((int)wallpaper_window->height - nh) / 2, wallpaper_window->width+2, nh);
	}
}

static void resize_finish_wallpaper(int width, int height) {
	yutani_window_resize_accept(yctx, wallpaper_window, width, height);
	reinit_graphics_yutani(wallpaper_ctx, wallpaper_window);
	draw_background(width, height);
	yutani_window_resize_done(yctx, wallpaper_window);
	yutani_flip(yctx, wallpaper_window);
}

int main (int argc, char ** argv) {

	if (argc < 2 || strcmp(argv[1],"--really")) {
		fprintf(stderr,
				"%s: Desktop environment wallpaper\n"
				"\n"
				" Renders the desktop wallpaper. You probably don't want\n"
				" to be running this directly - it is started by the\n"
				" session manager along with the panel.\n", argv[0]);
		return 1;
	}

	wallpaper = malloc(sizeof(sprite_t));
	load_sprite(wallpaper, "/usr/share/wallpaper.bmp");
	wallpaper->alpha = 0;

	yctx = yutani_init();

	/* wallpaper */
	wallpaper_window = yutani_window_create(yctx, yctx->display_width, yctx->display_height);
	yutani_window_move(yctx, wallpaper_window, 0, 0);
	yutani_set_stack(yctx, wallpaper_window, YUTANI_ZORDER_BOTTOM);

	wallpaper_ctx = init_graphics_yutani(wallpaper_window);
	draw_background(yctx->display_width, yctx->display_height);
	yutani_flip(yctx, wallpaper_window);

	int should_exit = 0;

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_WELCOME:
					yutani_window_resize_offer(yctx, wallpaper_window, yctx->display_width, yctx->display_height);
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == wallpaper_window->wid) {
							resize_finish_wallpaper(wr->width, wr->height);
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

