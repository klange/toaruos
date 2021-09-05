/*
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <math.h>

#include <sys/times.h>
#include <sys/fswait.h>
#include <sys/sysfunc.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>

static int left, top, width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static int should_exit = 0;
static clock_t last_redraw = 0;
static int cpu_count = 1;
int cpus[32] = {0};

static void get_cpu_info(int cpus[]) {
	FILE * f = fopen("/proc/idle","r");
	char buf[4096];
	fread(buf, 4096, 1, f);

	char * buffer = buf;
	for (int i = 0; i < cpu_count; ++i) {
		/* pid */
		char * a = strchr(buffer, ':');
		a++;
		cpus[i] = strtoul(a, &a, 10);
		cpus[i] += strtoul(a, &a, 10);
		cpus[i] += strtoul(a, &a, 10);
		cpus[i] += strtoul(a, &a, 10);
		cpus[i] /= 4;

		if (cpus[i] < 0) cpus[i] = 0;
		if (cpus[i] > 1000) cpus[i] = 1000;
		buffer = strchr(a, '\n');
	}

	fclose(f);
}

static uint32_t colors[32];

#define UNIT_WIDTH 3

static void refresh(void) {
	last_redraw = times(NULL);

	for (int y = 0; y < ctx->height; ++y) {
		for (int x = 0; x < ctx->width - UNIT_WIDTH; ++x) {
			GFX(ctx,x,y) = GFX(ctx,x+UNIT_WIDTH,y);
		}
		for (int w = 1; w < UNIT_WIDTH+1; ++w) {
			GFX(ctx,ctx->width-w,y) = rgb(0,0,0);
		}
	}

	int cpus_new[32];
	get_cpu_info(cpus_new);

	for (int i = 0; i < cpu_count; ++i) {
		draw_line_aa(ctx, ctx->width - UNIT_WIDTH, ctx->width - 1, cpus[i] * (ctx->height - 1) / 1000, cpus_new[i] * (ctx->height - 1) / 1000, colors[i], 0.5);
	}

	memcpy(cpus, cpus_new, sizeof(cpus_new));

	yutani_flip(yctx, wina);
}

int main (int argc, char ** argv) {
	left   = 100;
	top    = 100;
	width  = 300;
	height = 300;
	cpu_count = sysfunc(TOARU_SYS_FUNC_NPROC, NULL);

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	srand(time(NULL));
	for (int i = 0; i < cpu_count; ++i) {
		colors[i] = rgb(rand() % 255,rand() % 255,rand() % 255);
	}

	wina = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, wina, left, top);
	yutani_window_advertise_icon(yctx, wina, "CPU Usage", "cpuwidget");

	ctx = init_graphics_yutani(wina);
	draw_fill(ctx, rgb(0,0,0));

	get_cpu_info(cpus);
	refresh();

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
		if (times(NULL) > last_redraw + CLOCKS_PER_SEC/4) {
			refresh();
		}
	}

	yutani_close(yctx, wina);

	return 0;
}

