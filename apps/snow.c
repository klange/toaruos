/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <math.h>

#include <sys/fswait.h>
#include <sys/time.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static int should_exit = 0;
static sprite_t snowflake;

#define FLAKES 40
#define FALL_SPEED 3

struct {
	int16_t x;
	int16_t y;
	uint8_t rotation;
	uint8_t alpha;
	int8_t wind;
	uint8_t exists;
} flakes[FLAKES];

int flakes_made = 0;

static void add_flake() {
	for (int i = 0; i < FLAKES; ++i) {
		if (!flakes[i].exists) {
			flakes[i].exists = 1;
			flakes[i].y = -snowflake.height / 2;
			flakes[i].x = rand() % (ctx->width);
			flakes[i].alpha = rand() % 50 + 50;
			flakes[i].rotation = rand() % 255;
			flakes[i].wind = (rand() % 6) - 3;
			return;
		}
	}
}

static void draw(void) {
	draw_fill(ctx, 0);
	for (int i = 0; i < FLAKES; ++i) {
		if (flakes[i].exists) {
			draw_sprite_rotate(ctx, &snowflake,
					flakes[i].x, flakes[i].y,
					(float)(flakes[i].rotation)  / 100.0,
					(float)(flakes[i].alpha) / 100.0);
			flakes[i].y += FALL_SPEED;
			flakes[i].x += flakes[i].wind;
			if (flakes[i].y >= ctx->height + snowflake.height / 2 ||
				flakes[i].x <= -snowflake.width / 2 ||
				flakes[i].x >= ctx->width + snowflake.width / 2) {
				flakes[i].exists = 0;
				add_flake();
			}
		}
	}
	flip(ctx);
	yutani_flip(yctx, wina);
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(ctx, wina);
	draw();
	yutani_window_resize_done(yctx, wina);
}

uint64_t last_flake = 0;

static uint64_t precise_current_time(void) {
	struct timeval t;
	gettimeofday(&t, NULL);

	time_t sec_diff = t.tv_sec;
	suseconds_t usec_diff = t.tv_usec;

	return (uint64_t)(sec_diff * 1000 + usec_diff / 1000);
}

static uint64_t precise_time_since(uint64_t start_time) {

	uint32_t now = precise_current_time();
	uint32_t diff = now - start_time; /* Milliseconds */

	return diff;
}

int main (int argc, char ** argv) {
	srand(time(NULL));
	memset(&flakes, 0, sizeof(flakes));

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	load_sprite(&snowflake, "/usr/share/snowflake.bmp");

	wina = yutani_window_create(yctx, 100, 100);
	if (argc < 2 || strcmp(argv[1],"--no-ad")) {
		yutani_window_advertise(yctx, wina, "snow");
	}
	yutani_special_request(yctx, wina, YUTANI_SPECIAL_REQUEST_MAXIMIZE);
	yutani_window_update_shape(yctx, wina, 256);

	ctx = init_graphics_yutani_double_buffer(wina);
	draw_fill(ctx, rgba(0,0,0,0));
	flip(ctx);

	while (!should_exit) {
		int fds[1] = {fileno(yctx->sock)};
		int index = fswait2(1,fds,10);
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
					case YUTANI_MSG_RESIZE_OFFER:
						{
							struct yutani_msg_window_resize * wr = (void*)m->data;
							resize_finish(wr->width, wr->height);
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
		} else {
			if (flakes_made < 20 && precise_time_since(last_flake) > 1000) {
				add_flake();
				flakes_made += 1;
				last_flake = precise_current_time();
			}
		}
		draw();
	}

	yutani_close(yctx, wina);

	return 0;
}

