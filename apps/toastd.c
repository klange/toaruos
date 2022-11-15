/**
 * @brief Toast notification daemon.
 * @file  apps/toastd.c
 *
 * Provides an endpoint for applications to post notifications
 * which are displayed in pop-up "toasts" in the upper-right
 * corner of the screen without stealing focus.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <sys/fswait.h>
#include <toaru/pex.h>
#include <toaru/yutani.h>
#include <toaru/markup_text.h>
#include <toaru/graphics.h>
#include <toaru/json.h>
#include <toaru/list.h>

typedef struct JSON_Value JSON_Value;

static yutani_t * yctx;
static FILE * pex_endpoint = NULL;
static sprite_t background_sprite;
static list_t * windows = NULL;
static list_t * garbage = NULL;

struct ToastNotification {
	yutani_window_t * window;
	struct timespec created;
	int duration;
};

#define PAD_RIGHT 10
#define PAD_TOP   48

static void handle_msg(JSON_Value * msg) {
	if (msg->type != JSON_TYPE_OBJECT) {
		fprintf(stderr, "expected an object, but json value was of type %d\n", msg->type);
		return;
	}

	JSON_Value * msg_body = JSON_KEY(msg, "body");

	if (!msg_body) {
		fprintf(stderr, "missing 'body'\n");
		return;
	}
	if (msg_body->type != JSON_TYPE_STRING) {
		fprintf(stderr, "'body' should have been a string, but got type %d instead\n", msg_body->type);
		return;
	}

	/* At this point, we're going to show something, at least... */
	yutani_window_t * win = yutani_window_create_flags(yctx,
		background_sprite.width,
		background_sprite.height,
		YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS | YUTANI_WINDOW_FLAG_ALT_ANIMATION);

	yutani_set_stack(yctx, win, YUTANI_ZORDER_OVERLAY);

	/* TODO: We need to figure out how to place this... */
	yutani_window_move(yctx, win, yctx->display_width - background_sprite.width - PAD_RIGHT, PAD_TOP + background_sprite.height * windows->length);

	struct ToastNotification * notification = malloc(sizeof(struct ToastNotification));
	notification->window = win;
	clock_gettime(CLOCK_MONOTONIC, &notification->created);
	list_insert(windows, notification);

	JSON_Value * msg_duration = JSON_KEY(msg,"duration");
	if (msg_duration && msg_duration->type == JSON_TYPE_NUMBER) {
		notification->duration = msg_duration->number;
	} else {
		notification->duration = 5;
	}

	/* Establish the rendering context for this window, we'll only need it for a bit.
	 * We won't even both double buffering... */
	gfx_context_t * ctx = init_graphics_yutani(win);
	draw_fill(ctx, rgba(0,0,0,0));
	draw_sprite(ctx, &background_sprite, 0, 0);
	int textOffset = 0;

	/* Does it have an icon? */
	JSON_Value * msg_icon = JSON_KEY(msg, "icon");
	if (msg_icon && msg_icon->type == JSON_TYPE_STRING) {
		/* Just ignore the icon if it's not a string... */
		sprite_t myIcon;
		if (!load_sprite(&myIcon, msg_icon->string)) {
			/* Is this a reasonable icon to display? */
			if (myIcon.width < 100) {
				textOffset = myIcon.width + 8; /* Sounds like a fine padding... */
				draw_sprite(ctx, &myIcon, 10, (background_sprite.height - myIcon.height) / 2);
			} else {
				int h = myIcon.height * 100 / myIcon.width;
				textOffset = 100 + 8;
				draw_sprite_scaled(ctx, &myIcon, 10, (background_sprite.height - h) / 2, 100, h);
			}
			free(myIcon.bitmap);
		}
	}

	int height = markup_string_height(msg_body->string);

	markup_draw_string(ctx, 10 + textOffset, (ctx->height - height) / 2, msg_body->string, rgb(255,255,255));
	yutani_flip(yctx, win);
}

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
			return 1;
		}
		/* Open pex endpoint to receive notifications... */
		pex_endpoint = pex_bind("toast");
		if (!pex_endpoint) {
			fprintf(stderr, "%s: Failed to establish socket.\n", argv[0]);
			return 1;
		}
		/* Set up our text rendering and sprite contexts... */
		markup_text_init();
		load_sprite(&background_sprite, "/usr/share/ttk/toast/default.png");
		windows = list_create();
		garbage = list_create();

		int should_exit = 0;
		while (!should_exit) {
			int fds[2] = {fileno(yctx->sock),fileno(pex_endpoint)};
			int index = fswait2(2,fds,windows->length ? 20 : -1);
			if (index == 0) {
				yutani_msg_t * m = yutani_poll(yctx);
				while (m) {
					switch (m->type) {
						case YUTANI_MSG_SESSION_END:
							should_exit = 1;
							break;
						default:
							break;
					}
					free(m);
					m = yutani_poll_async(yctx);
				}
			} else if (index == 1) {
				pex_packet_t * p = calloc(PACKET_SIZE, 1);
				pex_listen(pex_endpoint, p);

				JSON_Value * msg = json_parse((char*)p->data);

				if (msg) {
					handle_msg(msg);
					json_free(msg);
				}

				free(p);
			}

			if (windows->length) {
				/* Check all the existing toasts for expired ones... */
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);

				foreach(node, windows) {
					struct ToastNotification * notification = node->value;

					/* How long has it been since this notification was created? */
					struct timespec diff;
					diff.tv_sec  = now.tv_sec  - notification->created.tv_sec;
					diff.tv_nsec = now.tv_nsec - notification->created.tv_nsec;
					if (diff.tv_nsec < 0) { diff.tv_sec--; diff.tv_nsec += 1000000000L; }

					if (diff.tv_sec >= notification->duration) {
						if (notification->window) {
							yutani_close(yctx, notification->window);
							notification->window = NULL;
						}
						if (diff.tv_sec >= notification->duration + 1) {
							list_insert(garbage, node);
						}
					}
				}

				/* Expunge garbage */
				if (garbage->length) {
					while (garbage->length) {
						node_t * n = list_pop(garbage);
						node_t * node = n->value;
						free(n);
						list_delete(windows, node);
						free(node->value);
						free(node);
					}
				}

				/* Figure out if we need to move anything */
				if (index == 2) {
					int index = 0;
					foreach(node, windows) {
						struct ToastNotification * notification = node->value;
						if (notification->window && notification->window->y > PAD_TOP + background_sprite.height * index) {
							yutani_window_move(yctx, notification->window, notification->window->x, notification->window->y - 4);
						}

						index++;
					}

					sched_yield();
				}
			}
		}
	}
	return 0;
}
