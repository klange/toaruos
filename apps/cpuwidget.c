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

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"Help", "help"},
	{NULL, NULL},
};

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

static void plot_graph(gfx_context_t * ctx, size_t scale, long samples[100], uint32_t color, float shift) {
	float unit_width = (float)ctx->width / 99.0;
	float factor[EASE_WIDTH];
	for (int k = 0; k < EASE_WIDTH; ++k) {
		factor[k] = (cos(M_PI * ((float)k / (float)(EASE_WIDTH-1))) + 1.0) / 2.0;
	}

	struct TT_Contour * contour = NULL;
	size_t first = 1;
	for (int j = 1; j < 100; ++j) {
		if (samples[j-1] == -1) {
			first++;
			continue;
		}
		float start = (float)ctx->width * (float)(j - 1) / 99.0 + shift;

		size_t old = samples[j-1];
		size_t new = samples[j];

		if (old > scale) old = scale;
		if (new > scale) new = scale;

		float nsamples[EASE_WIDTH];
		for (int k = 0; k < EASE_WIDTH; ++k) {
			float value = old * factor[k] + new * (1.0 - factor[k]);
			nsamples[k] =  ((scale - value) * ((float)ctx->height - 1) / (float)scale);
		}

		if (!contour) {
			contour = tt_contour_start(start, nsamples[0]);
		}

		for (int k = 1; k < EASE_WIDTH; ++k) {
			contour = tt_contour_line_to(contour, start + unit_width * ((float)k / (float)(EASE_WIDTH-1)), nsamples[k]);
		}
	}

	if (!contour) return;

	struct TT_Shape * stroke = tt_contour_stroke_shape(contour, 0.5);
	tt_path_paint(ctx, stroke, color);
	free(stroke);

	contour = tt_contour_line_to(contour, ctx->width + shift, ctx->height);
	contour = tt_contour_line_to(contour, (float)ctx->width * (float)(first - 1) / 99.0 + shift, ctx->height);

	struct TT_Shape * shape = tt_contour_finish(contour);

	uint32_t c = premultiply(rgba(_RED(color),_GRE(color),_BLU(color),_ALP(color) * 0.25));
	tt_path_paint(ctx, shape, c);
	free(shape);
	free(contour);
}

static long cpu_samples[32][100];

static void draw_lines(gfx_context_t * ctx) {
	float unit_width = (float)ctx->width / 99.0;
	for (int i = 1; i < 10; i++) {
		struct TT_Contour * line = tt_contour_start((int)(unit_width * 10.0 * i) + 0.5, 0);
		line = tt_contour_line_to(line, (int)(unit_width * 10.0 * i) + 0.5, ctx->height);
		struct TT_Shape * shape = tt_contour_stroke_shape(line, 0.5);
		free(line);
		tt_path_paint(ctx, shape, rgb(150,150,150));
		free(shape);
	}
}

static void draw_cpu_graphs(gfx_context_t * ctx, float shift) {
	draw_fill(ctx, rgb(0xF8,0xF8,0xF8));
	draw_lines(ctx);
	for (int i = 0; i < cpu_count; ++i) {
		plot_graph(ctx, 1000, cpu_samples[i], colors[i], shift);
	}
}

static void next_cpu(gfx_context_t * ctx) {
	int cpus_new[32];
	get_cpu_info(cpus_new);

	for (int i = 0; i < cpu_count; ++i) {
		memmove(&cpu_samples[i][0], &cpu_samples[i][1], 99 * sizeof(long));
		cpu_samples[i][99] = 1000-cpus_new[i];
	}

	draw_cpu_graphs(ctx, 0.0);
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

static long mem_samples[100];
static long mem_total;
static void draw_mem_graphs(gfx_context_t * ctx, float shift) {
	draw_fill(ctx, rgb(0xF8,0xF8,0xF8));
	draw_lines(ctx);
	plot_graph(ctx, mem_total, mem_samples, rgb(250,110,240), shift);
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

	memmove(&mem_samples[0], &mem_samples[1], 99 * sizeof(long));
	mem_total = total;
	mem_samples[99] = mem_use;
	draw_mem_graphs(ctx, 0.0);

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
		int netdev = open(if_path, O_RDONLY);
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

static long net_samples[32][100];
static size_t net_scale = 300 * 1024;

static void redraw_net_scale(void);

static int if_count = -1;

static void draw_net_graphs(gfx_context_t * ctx, float shift) {
	draw_fill(ctx, rgb(0xF8,0xF8,0xF8));
	draw_lines(ctx);
	for (int i = 0; i < if_count; ++i) {
		plot_graph(ctx, net_scale, net_samples[i], if_colors[i], shift);
	}
}

static void next_net(gfx_context_t * ctx) {
	static size_t old_ifs[32];
	static clock_t ticks_last = 0;
	size_t new_ifs[32];

	if (!ticks_last) {
		ticks_last = times(NULL);
		refresh_interfaces(old_ifs);
		return;
	}

	clock_t ticks_now = times(NULL);
	refresh_interfaces(new_ifs);

	long max = 0;
	for (int i = 0; i < if_count; ++i) {
		for (int j = 0; j < 99; ++j) {
			net_samples[i][j] = net_samples[i][j+1];
			if (net_samples[i][j] != -1) {
				if (net_samples[i][j] > max) {
					max = net_samples[i][j];
				}
			}
		}

		/* Kilobits... */
		size_t use = (new_ifs[i] - old_ifs[i]) * 8 / 1024;
		use *= CLOCKS_PER_SEC;
		use /= (ticks_now - ticks_last);

		net_samples[i][99] = use;

		if ((long)use > max) {
			max = use;
		}
	}

	size_t scale = max ? max : (300 * 1024);
	if (scale != net_scale) {
		net_scale = scale;
		redraw_net_scale();
	}

	draw_net_graphs(ctx, 0.0);

	memcpy(old_ifs, new_ifs, sizeof(new_ifs));
	ticks_last = ticks_now;
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
	int y = MENU_BAR_HEIGHT + bounds.top_height + (which + 1) * (top_pad + graph_height) + which * bottom_pad + 4;

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

static int poll_tick = 0;
static void redraw_graphs(void) {
	float shift = -(float)(poll_tick + 1) / (float)(EASE_WIDTH-1) * ctx_cpu->width / 100.0;
	draw_cpu_graphs(ctx_cpu, shift);
	draw_mem_graphs(ctx_mem, shift);
	draw_net_graphs(ctx_net, shift);
}

static void refresh(clock_t ticks) {

	if (poll_tick == (EASE_WIDTH-2)) {
		next_cpu(ctx_cpu);
		next_mem(ctx_mem);
		next_net(ctx_net);
		poll_tick = 0;
	} else {
		redraw_graphs();
		poll_tick++;
	}

	flip(ctx_base);
	yutani_flip(yctx, wina);

	last_redraw = ticks;
}

static void redraw_net_scale(void) {
	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);
	tt_set_size(tt_thin, 10);

	char net_max[100];
	snprintf(net_max, 100, "%0.2fmbps", (double)net_scale / 1024.0);
	int swidth = tt_string_width(tt_thin, net_max) + 2;
	draw_rectangle(ctx_base, bounds.left_width + width - swidth, MENU_BAR_HEIGHT + bounds.top_height + 2 * (top_pad + bottom_pad + graph_height), swidth, 20, rgb(204,204,204));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - swidth, MENU_BAR_HEIGHT + bounds.top_height + 2 * (top_pad + bottom_pad + graph_height) + 17, net_max, rgb(0,0,0));
}

void render_base(void) {
	render_decorations(wina, ctx_base, "System Monitor");
	menu_bar_render(&menu_bar, ctx_base);
}

static void redraw_window_callback(struct menu_bar * self) {
	(void)self;
	render_base();
	flip(ctx_base);
	yutani_flip(yctx,wina);
}

static void initial_stuff(void) {
	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);
	graph_height = (height - top_pad * 3 - bottom_pad * 3) / 3;

	menu_bar.x = bounds.left_width;
	menu_bar.y = bounds.top_height;
	menu_bar.width = ctx_base->width - bounds.width;
	menu_bar.window = wina;

	draw_fill(ctx_base, rgb(204,204,204));

	ctx_cpu = init_graphics_subregion(ctx_base, bounds.left_width + left_pad, MENU_BAR_HEIGHT + bounds.top_height + top_pad, width - h_pad, graph_height);
	ctx_mem = init_graphics_subregion(ctx_base, bounds.left_width + left_pad, MENU_BAR_HEIGHT + bounds.top_height + 2 * top_pad + graph_height + bottom_pad, width - h_pad, graph_height);
	ctx_net = init_graphics_subregion(ctx_base, bounds.left_width + left_pad, MENU_BAR_HEIGHT + bounds.top_height + 3 * top_pad + 2 * graph_height + 2 * bottom_pad, width - h_pad, graph_height);

	draw_fill(ctx_cpu, rgb(0xF8,0xF8,0xF8));
	draw_fill(ctx_mem, rgb(0xF8,0xF8,0xF8));
	draw_fill(ctx_net, rgb(0xF8,0xF8,0xF8));

	tt_set_size(tt_bold, 13);
	tt_draw_string(ctx_base, tt_bold, bounds.left_width + 3, MENU_BAR_HEIGHT + bounds.top_height + 14, "CPU", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_bold, bounds.left_width + 3, MENU_BAR_HEIGHT + bounds.top_height + (top_pad + bottom_pad + graph_height) + 14, "Memory", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_bold, bounds.left_width + 3, MENU_BAR_HEIGHT + bounds.top_height + 2 * (top_pad + bottom_pad + graph_height) + 14, "Network", rgb(0,0,0));

	tt_set_size(tt_thin, 10);
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 30, MENU_BAR_HEIGHT + bounds.top_height + 17, "100%", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 30, MENU_BAR_HEIGHT + bounds.top_height + (top_pad + bottom_pad + graph_height) + 17, "100%", rgb(0,0,0));

	char net_max[100];
	snprintf(net_max, 100, "%0.2fmbps", (double)net_scale / 1024.0);
	int swidth = tt_string_width(tt_thin, net_max) + 2;
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - swidth, MENU_BAR_HEIGHT + bounds.top_height + 2 * (top_pad + bottom_pad + graph_height) + 17, net_max, rgb(0,0,0));

	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 25, MENU_BAR_HEIGHT + bounds.top_height + top_pad + graph_height + 13, "0%", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 25, MENU_BAR_HEIGHT + bounds.top_height + 2 * (top_pad + graph_height) + bottom_pad + 13, "0%", rgb(0,0,0));
	tt_draw_string(ctx_base, tt_thin, bounds.left_width + width - 40, MENU_BAR_HEIGHT + bounds.top_height + 3 * (top_pad + graph_height) + 2 * bottom_pad + 13, "0mbps", rgb(0,0,0));

	render_base();

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
	height = h - MENU_BAR_HEIGHT - bounds.top_height - bounds.bottom_height;

	initial_stuff();
	redraw_graphs();

	flip(ctx_base);

	yutani_window_resize_done(yctx, wina);
}

static void _menu_action_exit(struct MenuEntry * entry) {
	exit(0);
}

static void _menu_action_help(struct MenuEntry * entry) {
	system("help-browser systemmonitor.trt &");
	render_base();
}

static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About System Monitor\" /usr/share/icons/48/system-monitor.png \"System Monitor\" \"© 2021-2023 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)wina->x + (int)wina->width / 2, (int)wina->y + (int)wina->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	render_base();
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
		for (int j = 0; j < 100; ++j) {
			cpu_samples[i][j] = -1;
		}
	}

	init_decorations();
	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	wina = yutani_window_create(yctx, width + bounds.width, height + bounds.height + MENU_BAR_HEIGHT);
	yutani_window_move(yctx, wina, left, top);
	yutani_window_advertise_icon(yctx, wina, "System Monitor", "system-monitor");

	ctx_base = init_graphics_yutani_double_buffer(wina);

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window_callback;
	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About System Monitor",_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	tt_thin = tt_font_from_shm("sans-serif");
	tt_bold = tt_font_from_shm("sans-serif.bold");

	if_count = count_interfaces();
	for (int i = 0; i < if_count; ++i) {
		if_colors[i] = hsv_to_rgb((float)i / (float)(if_count)* 6.24 + 0.2, 0.9, 0.9);
		for (int j = 0; j < 100; ++j) {
			net_samples[i][j] = -1;
		}
	}

	for (int i = 0; i < 100; ++i) {
		mem_samples[i] = -1;
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
					render_base();
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
								render_base();
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
								menu_bar_mouse_event(yctx, wina, &menu_bar, me, me->new_x, me->new_y);
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

