/**
 * @brief Sample extra panel widget with a CPU usage meter.
 *
 * Based on @ref apps/cpuwidget.c
 *
 * This widget displays a CPU usage graph directly on the panel.
 * Clicking it opens the cpuwidget utility.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2026 K. Lange
 */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/times.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>

#define EASE_WIDTH 2
#define NSAMPLE    32

static int cpu_count = 1;
static long cpu_samples[NSAMPLE];
static clock_t last_tick;

static void plot_graph(gfx_context_t * ctx, size_t scale, long samples[NSAMPLE], uint32_t color, float shift) {
	float unit_width = (float)ctx->width / (float)(NSAMPLE-1);
	float factor[EASE_WIDTH];
	for (int k = 0; k < EASE_WIDTH; ++k) {
		factor[k] = (cos(M_PI * ((float)k / (float)(EASE_WIDTH-1))) + 1.0) / 2.0;
	}

	struct TT_Contour * contour = NULL;
	size_t first = 1;
	for (int j = 1; j < NSAMPLE; ++j) {
		if (samples[j-1] == -1) {
			first++;
			continue;
		}
		float start = (float)ctx->width * (float)(j - 1) / ((float)(NSAMPLE-1)) + shift;

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
	contour = tt_contour_line_to(contour, (float)ctx->width * (float)(first - 1) / ((float)(NSAMPLE-1)) + shift, ctx->height);

	struct TT_Shape * shape = tt_contour_finish(contour);

	uint32_t c = premultiply(rgba(_RED(color),_GRE(color),_BLU(color),_ALP(color) * 0.25));
	tt_path_paint(ctx, shape, c);
	free(shape);
	free(contour);
}

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

static int widget_update_cpu(struct PanelWidget * this, int * force_updates) {
	clock_t ticks = times(NULL);

	if (ticks >= last_tick + CLOCKS_PER_SEC) {
		last_tick = ticks;
		int cpus_new[32];
		get_cpu_info(cpus_new);

		float total_usage = 0.0;
		for (int i = 0; i < cpu_count; ++i) {
			total_usage += cpus_new[i];
		}

		memmove(&cpu_samples[0], &cpu_samples[1], (NSAMPLE-1) * sizeof(long));
		cpu_samples[NSAMPLE-1] = 1000 - (int)(total_usage / cpu_count);
	}


	return 0;
}

static int widget_click_cpu(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	system("cpuwidget &");
	return 1;
}

static void base_to_alpha(gfx_context_t * ctx) {
	for (unsigned int y = 0; y < ctx->height; ++y) {
		for (unsigned int x = 0; x < ctx->width; ++x) {
			uint32_t r = _RED(GFX(ctx, x, y));
			GFX(ctx, x, y) = rgba(r,r,r,r);
		}
	}
}

static uint32_t gfx_pattern_from_ctx(int32_t x, int32_t y, double alpha, void * extra) {
	if (alpha > 1.0) alpha = 1.0;
	if (alpha < 0.0) alpha = 0.0;
	gfx_context_t * t = extra;
	uint32_t c = GFX(t,x,y);
	uint32_t r = _RED(c) * alpha;
	uint32_t g = _GRE(c) * alpha;
	uint32_t b = _BLU(c) * alpha;
	uint32_t a = _ALP(c) * alpha;
	return rgba(r,g,b,a);
}

static int widget_draw_cpu(struct PanelWidget * this, gfx_context_t * ctx) {
	gfx_context_t * sctx = init_graphics_subregion(ctx, 2, 2, ctx->width - 4, ctx->height - 4);
	if (this->highlighted) draw_rounded_rectangle(sctx, 0, 0, sctx->width, sctx->height, 3, this->pctx->color_widget_bg_base);

	sprite_t * tmp_surface = create_sprite(sctx->width, sctx->height, ALPHA_EMBEDDED);
	gfx_context_t * tctx = init_graphics_sprite(tmp_surface);

	/* First use to to draw the graphs */
	draw_fill(tctx, rgba(0,0,0,0));
	plot_graph(tctx, 1000, cpu_samples, this->pctx->color_icon_normal, 0);
	draw_rounded_rectangle_pattern(sctx, 0, 0, sctx->width, sctx->height, 4, gfx_pattern_from_ctx, tctx);

	/* Then re-use to draw rounded border over top graph */
	draw_fill(tctx, rgb(0,0,0));
	draw_rounded_rectangle(tctx, 0, 0, tctx->width, tctx->height, 4, rgb(255,255,255));
	draw_rounded_rectangle(tctx, 1, 1, tctx->width-2, tctx->height-2, 3, rgb(0,0,0));
	base_to_alpha(tctx);
	free(tctx);

	draw_sprite(sctx, tmp_surface, 0, 0);

	sprite_free(tmp_surface);
	free(sctx);
	return 0;
}

struct PanelWidget * widget_init_cpu(void) {
	struct PanelWidget * widget = widget_new();

	cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

	for (int j = 0; j < NSAMPLE; ++j) {
		cpu_samples[j] = -1;
	}

	last_tick = times(NULL);

	widget->width = 48;
	widget->draw = widget_draw_cpu;
	widget->click = widget_click_cpu;
	widget->update = widget_update_cpu;
	list_insert(widgets_enabled, widget);

	return widget;
}


