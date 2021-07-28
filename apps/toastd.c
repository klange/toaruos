/**
 * @brief Toast notification daemon.
 * @file  apps/toastd.c
 *
 * Provides an endpoint for applications to post notifications
 * which are displayed in pop-up "toasts" in the upper-right
 * corner of the screen without stealing focus.
 *
 * @copyright 2021 K. Lange
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <sys/fswait.h>
#include <toaru/pex.h>
#include <toaru/yutani.h>
#include <toaru/markup_text.h>
#include <toaru/graphics.h>

static yutani_t * yctx;
static FILE * pex_endpoint = NULL;
static sprite_t background_sprite;

#define PAD_RIGHT 10
#define PAD_TOP   48

int main(int argc, char * argv[]) {
	/* Make sure we were actually expecting to be run... */
	if (argc < 2 || strcmp(argv[1],"--really")) {
		fprintf(stderr,
				"%s: Toast notification daemon\n"
				"\n"
				" Displays popup notifications from other\n"
				" applications in the corner of the screen.\n"
				" You probably don't want to run this directly - it is\n"
				" started automatically by the session manager.\n", argv[0]);
		return 1;
	}
	/* Daemonize... */
	if (!fork()) {
		/* Connect to display server... */
		yctx = yutani_init();
		if (!yctx) {
			fprintf(stderr, "%s: Failed to connect to compositor.\n", argv[0]);
		}
		/* Open pex endpoint to receive notifications... */
		pex_endpoint = pex_bind("toast");
		if (!pex_endpoint) {
			fprintf(stderr, "%s: Failed to establish socket.\n", argv[0]);
		}
		/* Set up our text rendering and sprite contexts... */
		markup_text_init();
		load_sprite(&background_sprite, "/usr/share/ttk/toast/default.png");
		/* Make a test window? */
		yutani_window_t * wina = yutani_window_create_flags(yctx, background_sprite.width, background_sprite.height, YUTANI_WINDOW_FLAG_ALT_ANIMATION);
		yutani_window_move(yctx, wina, yctx->display_width - background_sprite.width - PAD_RIGHT, PAD_TOP); /* We need to be able to query the panel location... */
		gfx_context_t * ctx = init_graphics_yutani_double_buffer(wina);
		draw_fill(ctx, rgba(0,0,0,0));
		draw_sprite(ctx, &background_sprite, 0, 0);
		/* Wait for messages from pex, or from compositor... */
		markup_draw_string(ctx,10,26,"<b><h1>Welcome!</h1></b><br>This is a sample <i>toast</i> notification.",rgb(255,255,255));
		flip(ctx);
		yutani_flip(yctx, wina);

		int should_exit = 0;
		while (!should_exit) {
			int fds[1] = {fileno(yctx->sock)};
			int index = fswait2(1,fds,20);
			if (index == 0) {
				yutani_msg_t * m = yutani_poll(yctx);
				while (m) {
					switch (m->type) {
						case YUTANI_MSG_KEY_EVENT:
							{
								struct yutani_msg_key_event * ke = (void*)m->data;
								if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
									should_exit = 1;
									sched_yield();
								}
							}
							break;
						case YUTANI_MSG_WINDOW_MOUSE_EVENT:
							{
								struct yutani_msg_window_mouse_event * me = (void*)m->data;
								if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
									yutani_window_drag_start(yctx, wina);
								}
							}
							break;
						case YUTANI_MSG_WINDOW_CLOSE:
						case YUTANI_MSG_SESSION_END:
							should_exit = 1;
							break;
						default:
							break;
					}
					free(m);
					m = yutani_poll_async(yctx);
				}
			}
		}

		yutani_close(yctx, wina);
	}
	return 0;
}
