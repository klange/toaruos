#include <stdio.h>
#include <unistd.h>
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
		draw_sprite_scaled(wallpaper_ctx, wallpaper, ((int)wallpaper_window->width - nw) / 2, 0, nw, wallpaper_window->height);
	} else {
		draw_sprite_scaled(wallpaper_ctx, wallpaper, 0, ((int)wallpaper_window->height - nh) / 2, wallpaper_window->width, nh);
	}
}

static void draw_panel(int width) {
	char label[100];
	struct utsname u;
	uname(&u);
	sprintf(label, "ToaruOS-NIH %s", u.release);
	draw_fill(panel_ctx, rgba(0,0,0,170));
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

static void launch_application(char * app) {
	if (!fork()) {
		printf("Starting %s\n", app);
		char * args[] = {"/bin/sh", "-c", app, NULL};
		execvp(args[0], args);
		exit(1);
	}
}

static void handle_key_event(struct yutani_msg_key_event * ke) {
	if ((ke->event.modifiers & KEY_MOD_LEFT_CTRL) &&
		(ke->event.modifiers & KEY_MOD_LEFT_ALT) &&
		(ke->event.keycode == 't') &&
		(ke->event.action == KEY_ACTION_DOWN)) {
		launch_application("terminal");
	}
}

int main (int argc, char ** argv) {

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

	/* panel */
	panel_window = yutani_window_create(yctx, yctx->display_width, PANEL_HEIGHT);
	yutani_window_move(yctx, panel_window, 0, 0);
	yutani_set_stack(yctx, panel_window, YUTANI_ZORDER_TOP);

	panel_ctx = init_graphics_yutani(panel_window);
	draw_panel(yctx->display_width);
	yutani_flip(yctx, panel_window);

	int should_exit = 0;

	yutani_key_bind(yctx, 't', KEY_MOD_LEFT_CTRL | KEY_MOD_LEFT_ALT, YUTANI_BIND_STEAL);

	int _terminal_pid = fork();
	if (!_terminal_pid) {
		char * args[] = {"/bin/terminal", NULL};
		execvp(args[0], args);
	}

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_WELCOME:
					fprintf(stderr, "Request to resize desktop received, resizing to %d x %d\n", yctx->display_width, yctx->display_height);
					yutani_window_resize_offer(yctx, wallpaper_window, yctx->display_width, yctx->display_height);
					yutani_window_resize_offer(yctx, panel_window, yctx->display_width, PANEL_HEIGHT);
					break;
				case YUTANI_MSG_KEY_EVENT:
					handle_key_event((struct yutani_msg_key_event *)m->data);
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

