/**
 * @brief System monitor tool
 *
 * Displays CPU usage, memory usage, and network usage, with nice
 * curvy anti-aliased graphs that scroll smoothly.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <sched.h>
#include <math.h>

#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/times.h>
#include <sys/fswait.h>
#include <sys/sysfunc.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/text.h>
#include <toaru/menu.h>

static int left, top, width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx_base;

static gfx_context_t * ctx_cpu;
static gfx_context_t * ctx_mem;
static gfx_context_t * ctx_net;

static int left_pad = 0;
static int h_pad = 0;
static int top_pad = 19;
static int bottom_pad = 34;
static int graph_height;

static struct TT_Font * tt_thin = NULL;
static struct TT_Font * tt_bold = NULL;

uint32_t hsv_to_rgb(float h, float s, float v) {
	float c  = v * s;
	float hp = fmod(h, 2 * M_PI);
	float x = c * (1.0 - fabs(fmod(hp / 1.0472, 2) - 1.0));
	float m = v - c;
	float rp, gp, bp;
	if (hp <= 1.0472)      { rp = c; gp = x; bp = 0; }
	else if (hp <= 2.0944) { rp = x; gp = c; bp = 0; }
	else if (hp <= 3.1416) { rp = 0; gp = c; bp = x; }
	else if (hp <= 4.1888) { rp = 0; gp = x; bp = c; }
	else if (hp <= 5.2360) { rp = x; gp = 0; bp = c; }
	else                   { rp = c; gp = 0; bp = x; }
	return rgb((rp + m) * 255, (gp + m) * 255, (bp + m) * 255);
}


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
static uint32_t if_colors[32];

#define EASE_WIDTH 8

static void shift_graph(gfx_context_t * ctx) {
	for (int y = 0; y < ctx->height; ++y) {
		for (int x = 0; x < ctx->width - 1; ++x) {
			GFX(ctx,x,y) = GFX(ctx,x+1,y);
		}
		for (int w = 1; w < 2; ++w) {
			GFX(ctx,ctx->width-w,y) = rgb(0xF8,0xF8,0xF8);
		}
	}
}

extern struct TT_Contour * tt_contour_start(float x, float y);
extern struct TT_Shape * tt_contour_finish(struct TT_Contour * in);
extern struct TT_Contour * tt_contour_line_to(struct TT_Contour * shape, float x, float y);
extern void tt_path_paint(gfx_context_t * ctx, const struct TT_Shape * shape, uint32_t color);
extern void tt_contour_stroke_bounded(gfx_context_t * ctx, struct TT_Contour * in, uint32_t color, float width,
		int x_0, int y_0, int w, int h);

static void graph_between(gfx_context_t * ctx, size_t old, size_t new, size_t scale, uint32_t color, int direction) {
	static float factor[EASE_WIDTH] = {0.0};
	if (factor[0] == 0.0) {
		for (int i = 0; i < EASE_WIDTH; ++i) {
			factor[i] = (cos(M_PI * ((float)i / (float)(EASE_WIDTH-1))) + 1.0) / 2.0;
		}
	}

	if (old > scale) old = scale;
	if (new > scale) new = scale;

	static float samples[EASE_WIDTH];
	for (int i = 0; i < EASE_WIDTH; ++i) {
		size_t value = old * factor[i] + new * (1.0 - factor[i]);
		samples[i] = (direction == 0) ? (value * (ctx->height - 1) / (float)scale) : ((scale - value) * (ctx->height - 1) / (float)scale);
	}

	/* Main line */
	struct TT_Contour * contour = tt_contour_start(ctx->width - EASE_WIDTH, samples[0]);
	for (int i = 1; i < EASE_WIDTH; ++i) {
		contour = tt_contour_line_to(contour, ctx->width - EASE_WIDTH + i, samples[i]);
	}
	/* Now slow stroke, but only within these bounds */
	tt_contour_stroke_bounded(ctx, contour, color, 0.5, ctx->width - EASE_WIDTH, 0, EASE_WIDTH - 1, ctx->height);

	/* Now finish the lower part of the graph */
	contour = tt_contour_line_to(contour, ctx->width - 1, ctx->height);
	contour = tt_contour_line_to(contour, ctx->width - EASE_WIDTH, ctx->height);

	struct TT_Shape * shape = tt_contour_finish(contour);

	uint32_t c = premultiply(rgba(_RED(color),_GRE(color),_BLU(color),_ALP(color) * 0.25));
	tt_path_paint(ctx, shape, c);
	free(shape);
	free(contour);
}

static void next_cpu(gfx_context_t * ctx) {
	int cpus_new[32];
	get_cpu_info(cpus_new);
	for (int i = 0; i < cpu_count; ++i) {
		graph_between(ctx, cpus[i], cpus_new[i], 1000, colors[i], 0);
	}
	memcpy(cpus, cpus_new, sizeof(cpus_new));
}

static void get_mem_info(int * total, int * used) {
	FILE * f = fopen("/proc/meminfo", "r");
	if (!f) return;
	int free;
	char buf[1024] = {0};
	fgets(buf, 1024, f);

	char * a, * b;
	a = strchr(buf, ' ');
	a++;
	b = strchr(a, '\n');
	*b = '\0';
	*total = atoi(a);
	fgets(buf, 1024, f);
	a = strchr(buf, ' ');
	a++;
	b = strchr(a, '\n');
	*b = '\0';
	free = atoi(a);
	*used = *total - free;

	fclose(f);
}

static void next_mem(gfx_context_t * ctx) {
	static int total = 0;
	static int old_use = 0;
	int mem_use = 0;
	get_mem_info(&total, &mem_use);

	if (!old_use) {
		old_use = mem_use;
		return;
	}

	graph_between(ctx, old_use, mem_use, total, rgb(250,110,240), 1);

	old_use = mem_use;
}

static char ifnames[32][256];

static int count_interfaces(void) {
	int count = 0;
	DIR * d = opendir("/dev/net");
	if (!d) {
		return 0;
	}

	struct dirent * ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.') continue;
		snprintf(ifnames[count>>1], 255, ent->d_name);
		count += 2;
	}

	closedir(d);
	return count;
}

static void refresh_interfaces(size_t ifs[32]) {
	int ind = 0;

	DIR * d = opendir("/dev/net");
	if (!d) return;

	struct dirent * ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.') continue;
		char if_path[1024];
		snprintf(if_path, 1023, "/dev/net/%s", ent->d_name);
		int netdev = open(if_path, O_RDWR);
		if (netdev < 0) {
			continue;
		}

		netif_counters_t counts;
		if (!ioctl(netdev, SIOCGIFCOUNTS, &counts)) {
			ifs[ind + 0] = counts.rx_bytes;
			ifs[ind + 1] = counts.tx_bytes;
			ind += 2;
		}

		close(netdev);
	}

	closedir(d);
}

static int if_count = -1;
static void next_net(gfx_context_t * ctx) {
	static size_t old_ifs[32];
	static size_t old_use[32];
	static clock_t ticks_last = 0;
	size_t new_ifs[32];
	size_t new_use[32];

	if (!ticks_last) {
		ticks_last = times(NULL);
		refresh_interfaces(old_ifs);
		return;
	}

	clock_t ticks_now = times(NULL);
	refresh_interfaces(new_ifs);

	for (int i = 0; i < if_count; ++i) {
		/* Kilobits... */
		new_use[i] = (new_ifs[i] - old_ifs[i]) * 8 / 1024;
		new_use[i] *= CLOCKS_PER_SEC;
		new_use[i] /= (ticks_now - ticks_last);
		/* Relative to 300mbps... */
		graph_between(ctx, old_use[i], new_use[i], 300 * 1024, if_colors[i], 1);
	}

	memcpy(old_ifs, new_ifs, sizeof(new_ifs));
	memcpy(old_use, new_use, sizeof(new_ifs));
	ticks_last = ticks_now;
}

static void demarcate(gfx_context_t * ctx) {
	for (int y = 0; y < ctx->height; ++y) {
		GFX(ctx,ctx->width - 1,y) = rgb(127,127,127);
	}
}

static char * ellipsify(char * input, int font_size, struct TT_Font * font, int max_width, int * out_width) {
	int len = strlen(input);
	char * out = malloc(len + 4);
	memcpy(out, input, len + 1);
	int width;
	tt_set_size(font, font_size);
	while ((width = tt_string_width(font, out)) > max_width) {
		len--;
		out[len+0] = '.';
		out[len+1] = '.';
		out[len+2] = '.';
		out[len+3] = '\0';
	}

	if (out_width) *out_width = width;

	return out;
}

static void draw_legend_element(int which, int count, int index, uint32_t color, char * label) {
	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);

	/* Available display width */
	int legend_width = ctx_base->width - bounds.width - 40;
	if (legend_width <= 0) return;

	/* Calculate graph offset from the usual rule */
	int y = bounds.top_height + (which + 1) * (top_pad + graph_height) + which * bottom_pad + 4;

	/* Space to give to each unit. */
	int unit_width = legend_width / count;

	/* Left offset of this unit */
	int unit_x = unit_width * index + bounds.left_width + 10;

	/* First draw blob */
	draw_rounded_rectangle(ctx_base,
		unit_x, y, 20, 20, 5, color);

	if (unit_width > 22) {
		char * label_cropped = ellipsify(label, 12, tt_thin, unit_width - 22, NULL);
		tt_draw_string(ctx_base, tt_thin, 22 + unit_x, y + 14, label_cropped, rgb(0,0,0));
	}

}

static void draw_legend_cpu(void) {
	for (int i = 0; i < cpu_count; ++i) {
		char _cpu_name[] = "CPU    ";
		sprintf(_cpu_name, "CPU %d", i+1);
		draw_legend_element(0, cpu_count, i, colors[i], _cpu_name);
	}
}

static void draw_legend_mem(void) {
	draw_legend_element(1, 1, 0, rgb(250,110,240), "Memory Usage");
}

static void draw_legend_net(void) {
	for (int i = 0; i < if_count; ++i) {
		char _net_name[300];
		sprintf(_net_name, "%s (%s)", (i & 1) ? "TX" : "RX", ifnames[i>>1]);
		draw_legend_element(2, if_count, i, if_colors[i], _net_name);
	}
}

static void refresh(clock_t ticks) {
	static int poll_tick = 0;
	static clock_t last_line = 0;

	/* Shift graphs */
	shift_graph(ctx_cpu);
	shift_graph(ctx_mem);
	shift_graph(ctx_net);

	if (ticks > last_line + CLOCKS_PER_SEC * 10) {
		demarcate(ctx_cpu);
		demarcate(ctx_mem);
		demarcate(ctx_net);
		last_line = ticks;
	}

	if (poll_tick == (EASE_WIDTH-2)) {
		next_cpu(ctx_cpu);
		next_mem(ctx_mem);
		next_net(ctx_net);
		poll_tick = 0;
	} else {
		poll_tick++;
	}

	flip(ctx_base);
	yutani_flip(yctx, wina);

	last_redraw = ticks;
}

static void initial_stuff(void) {
	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);
	graph_height = (height - top_pad * 3 - bottom_pad * 3) / 3;

	draw_fill(ctx_base, rgb(204,204,204));

	ctx_cpu = init_graphics_subregion(ctx_base, bounds.left_width + left_pad, bounds.top_height + top_pad, width - h_pad, graph_height);
	ctx_mem = init_graphics_subregion(ctx_base, bounds.left_width + left_pad, bounds.top_height + 2 * top_pad + graph_height + bottom_pad, width - h_pad, graph_height);
	ctx_net = init_graphics_subregion(ctx_base, bounds.left_width + left_pad, bounds.top_height + 3 * top_pad + 2 * graph_height + 2 * bottom_pad, width - h_pad, graph_height);

	draw_fill(ctx_cpu, rgb(0xF8,0xF8,0xF8));
	draw_fill(ctx_mem, rgb(0xF8,0xF8,0xF8));
	draw_fill(ctx_net, rgb(0xF8,0xF8,0xF8));

	tt_set_size(tt_bold, 13);
	tt_draw_string(ctx_base, tt_bold, bounds.left_width + 3, bounds.top_height + 14, "CPU", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_bold, bounds.left_width + 3, bounds.top_height + (top_pad + bottom_pad + graph_height) + 14, "Memory", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_bold, bounds.left_width + 3, bounds.top_height + 2 * (top_pad + bottom_pad + graph_height) + 14, "Network", rgb(0,0,0));

	tt_set_size(tt_thin, 10);
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 30, bounds.top_height + 17, "100%", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 30, bounds.top_height + (top_pad + bottom_pad + graph_height) + 17, "100%", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 50, bounds.top_height + 2 * (top_pad + bottom_pad + graph_height) + 17, "300mbps", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 25, bounds.top_height + top_pad + graph_height + 13, "0%", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 25, bounds.top_height + 2 * (top_pad + graph_height) + bottom_pad + 13, "0%", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 40, bounds.top_height + 3 * (top_pad + graph_height) + 2 * bottom_pad + 13, "0mbps", rgb(0,0,0));

	render_decorations(wina, ctx_base, "System Monitor");

	draw_legend_cpu();
	draw_legend_mem();
	draw_legend_net();
}

void resize_finish(int w, int h) {

	if (w < 300) w = 300;
	if (h < 300) h = 300;

	free(ctx_cpu);
	free(ctx_mem);
	free(ctx_net);

	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(ctx_base, wina);

	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);

	width  = w - bounds.left_width - bounds.right_width;
	height = h - bounds.top_height - bounds.bottom_height;

	initial_stuff();
	flip(ctx_base);

	yutani_window_resize_done(yctx, wina);
}

int main (int argc, char ** argv) {
	left   = 100;
	top    = 100;
	width  = 640;
	height = 480;
	cpu_count = sysfunc(TOARU_SYS_FUNC_NPROC, NULL);

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	srand(time(NULL));
	for (int i = 0; i < cpu_count; ++i) {
		colors[i] = hsv_to_rgb((float)i / (float)cpu_count * 6.24, 0.9, 0.9);
	}

	init_decorations();
	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	wina = yutani_window_create(yctx, width + bounds.width, height + bounds.height);
	yutani_window_move(yctx, wina, left, top);
	yutani_window_advertise_icon(yctx, wina, "System Monitor", "system-monitor");

	ctx_base = init_graphics_yutani_double_buffer(wina);

	tt_thin = tt_font_from_shm("sans-serif");
	tt_bold = tt_font_from_shm("sans-serif.bold");

	get_cpu_info(cpus);
	if_count = count_interfaces();
	for (int i = 0; i < if_count; ++i) {
		if_colors[i] = hsv_to_rgb((float)i / (float)(if_count)* 6.24 + 0.2, 0.9, 0.9);
	}

	initial_stuff();
	refresh(times(NULL));

	while (!should_exit) {
		int fds[1] = {fileno(yctx->sock)};
		int index = fswait2(1,fds,20);
		if (index == 0) {
			yutani_msg_t * m = yutani_poll(yctx);
			while (m) {
				if (menu_process_event(yctx, m)) {
					/* just decorations should be fine */
					render_decorations(wina, ctx_base, "System Monitor");
					flip(ctx_base);
					yutani_flip(yctx, wina);
				}
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
					case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
						{
							struct yutani_msg_window_focus_change * wf = (void*)m->data;
							yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
							if (win == wina) {
								win->focused = wf->focused;
								render_decorations(wina, ctx_base, "System Monitor");
								flip(ctx_base);
								yutani_flip(yctx, wina);
							}
						}
						break;
					case YUTANI_MSG_RESIZE_OFFER:
						{
							struct yutani_msg_window_resize * wr = (void*)m->data;
							resize_finish(wr->width, wr->height);
						}
						break;
					case YUTANI_MSG_WINDOW_MOUSE_EVENT:
						{
							struct yutani_msg_window_mouse_event * me = (void*)m->data;
							yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)me->wid);
							if (win == wina) {
								int result = decor_handle_event(yctx, m);
								switch (result) {
									case DECOR_CLOSE:
										should_exit = 1;
										break;
									case DECOR_RIGHT:
										/* right click in decoration, show appropriate menu */
										decor_show_default_menu(wina, wina->x + me->new_x, wina->y + me->new_y);
										break;
									default:
										/* Other actions */
										break;
								}
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
		clock_t ticks = times(NULL);
		if (ticks > last_redraw + CLOCKS_PER_SEC/12) {
			refresh(ticks);
		}
	}

	yutani_close(yctx, wina);

	return 0;
}

