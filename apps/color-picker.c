/**
 * @file apps/color-picker.c
 * @brief Color picker
 *
 * Color Picker widget demo, eventually maybe a paint app again...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <wait.h>
#include <sched.h>
#include <signal.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/spinlock.h>
#include <toaru/menu.h>
#include <toaru/text.h>

#define dist(a,b,c,d) sqrt((double)(((a) - (c)) * ((a) - (c)) + ((b) - (d)) * ((b) - (d))))

static yutani_t * yctx;
static yutani_window_t * wina;
static int should_exit = 0;

uint16_t win_width;
uint16_t win_height;

uint16_t off_x;
uint16_t off_y;

static int needs_redraw = 0;

gfx_context_t * ctx;
static struct TT_Font * tt_font_thin = NULL;

void redraw_borders() {
	render_decorations(wina, ctx, "Color Picker");
}

double fmin(double a, double b) {
	return a < b ? a : b;
}

double fmax(double a, double b) {
	return a > b ? a : b;
}

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

void rgb_to_hsv(uint32_t c, double *h, double *s, double *v) {
	float r = _RED(c) / 255.0;
	float g = _GRE(c) / 255.0;
	float b = _BLU(c) / 255.0;

	float c_max = fmax(r,fmax(g,b));
	float c_min = fmin(r,fmin(g,b));

	float delta = c_max - c_min;

	if (!delta) {
		*h = 0;
	} else if (c_max == r) {
		*h = 1.0471975512 * fmod((g - b) / delta, 6.0);
	} else if (c_max == g) {
		*h = 1.0471975512 * ((b - r) / delta + 2.0);
	} else {
		*h = 1.0471975512 * ((r - g) / delta + 4.0);
	}

	if (c_max == 0) {
		*s = 0;
	} else {
		*s = delta / c_max;
	}

	*v = c_max;
}

struct Picker {
	int x;
	int y;
	int radius;
	struct gfx_point red;
	struct gfx_point white;
	struct gfx_point black;
	double dp;
	double hue;
};

static double pt_sign(const struct gfx_point *p1, const struct gfx_point *p2, const struct gfx_point *p3) {
	return (p1->x - p3->x) * (p2->y - p3->y) - (p2->x - p3->x) * (p1->y - p3->y);
}

static int in_triangle(const struct gfx_point * pt, const struct gfx_point * v1, const struct gfx_point * v2, const struct gfx_point * v3, double *proximity) {
	double d1 = pt_sign(pt,v1,v2);
	double d2 = pt_sign(pt,v2,v3);
	double d3 = pt_sign(pt,v3,v1);
	int neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	int pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

	*proximity = 1.0;
	*proximity = fmin(*proximity, gfx_line_distance(pt, v1, v2));
	*proximity = fmin(*proximity, gfx_line_distance(pt, v2, v3));
	*proximity = fmin(*proximity, gfx_line_distance(pt, v3, v1));

	return !(neg && pos);
}

static uint32_t gfx_fill_magic(int32_t x, int32_t y, double alpha, void * extra) {
	if (alpha > 1.0) alpha = 1.0;
	if (alpha < 0.0) alpha = 0.0;

	struct Picker * picker = extra;

	/* Picker center */
	int _x = picker->x + picker->radius;
	int _y = picker->y + picker->radius;

	/* hole in the middle */
	double r = dist(x,y,_x,_y);
	int inner_radius = picker->radius * 0.8;

	uint32_t c;

	if (r < inner_radius) {
		struct gfx_point p = {(float)x,(float)y};

		/* Are we in the triangle? */
		double proximity;
		if (!in_triangle(&p,&picker->red,&picker->white,&picker->black,&proximity)) {
			return rgba(0,0,0,0);
		}

		alpha = proximity;

		double h = picker->hue;
		double v = 1.0 - (gfx_line_distance(&p, &picker->red, &picker->white) / picker->dp);
		double _l = gfx_line_distance(&p, &picker->black, &picker->white);
		double _h = gfx_line_distance(&p, &picker->black, &picker->red);
		double s = _l + _h > 0.0 ? (_l / (_l+_h)) : 1.0;
		c = hsv_to_rgb(h,s,v);
	} else {
		double angle = atan2(y-_y,_x-x) + M_PI;

		if (r < inner_radius + 1) {
			alpha *= r - inner_radius;
		}

		c = hsv_to_rgb(angle, 1.0, 1.0);
		
	}


	return premultiply(rgba(_RED(c),_GRE(c),_BLU(c),(int)(255 * alpha)));
}

static void fill_picker(struct Picker *picker) {
	/* Triangle stuff */
	picker->red.x   = 0.8 * picker->radius * cos(-picker->hue) + picker->x + picker->radius;
	picker->red.y   = 0.8 * picker->radius * sin(-picker->hue) + picker->y + picker->radius;
	picker->white.x = 0.8 * picker->radius * cos(-picker->hue + 2.09439510239) + picker->x + picker->radius;
	picker->white.y = 0.8 * picker->radius * sin(-picker->hue + 2.09439510239) + picker->y + picker->radius;
	picker->black.x = 0.8 * picker->radius * cos(-picker->hue + 4.18879020479) + picker->x + picker->radius;
	picker->black.y = 0.8 * picker->radius * sin(-picker->hue + 4.18879020479) + picker->y + picker->radius;

	/* Midpoint */
	struct gfx_point midpoint = {(picker->white.x + picker->black.x) / 2.0, (picker->white.y + picker->black.y) / 2.0};
	picker->dp = gfx_point_distance(&picker->red, &midpoint);
}

static double _hue = 0;
static double _sat = 1.0;
static double _val = 1.0;
static uint32_t my_color = 0xFFFF0000;

static void draw_ring(gfx_context_t * ctx, double x, double y, double radius, double thickness, uint32_t c) {
	struct gfx_point p = {x,y};
	for (int _y = y - radius - thickness; _y <= y + radius + thickness; ++_y) {
		if (_y < 0) continue;
		if (_y >= ctx->height) break;
		for (int _x = x - radius - thickness; _x <= x + radius + thickness; ++_x) {
			if (_x < 0) continue;
			if (_x >= ctx->width) break;

			struct gfx_point pt = {_x,_y};

			double dist = gfx_point_distance(&p,&pt);
			if (dist > radius - thickness && dist < radius + thickness) {
				double alpha = fmin(1.0,thickness - fabs(radius - dist));
				GFX(ctx,_x,_y) = alpha_blend_rgba(GFX(ctx,_x,_y), premultiply(rgba(_RED(c),_GRE(c),_BLU(c),_ALP(c)*alpha)));
			}
		}
	}
}

static void redraw_everything(void) {
	draw_fill(ctx, rgb(200,200,200));

	struct Picker picker = {off_x, off_y, win_width / 2, {0,0}, {0,0}, {0,0}, 0, _hue};
	fill_picker(&picker);
	draw_rounded_rectangle_pattern(ctx,picker.x,picker.y,picker.radius*2,picker.radius*2,picker.radius,gfx_fill_magic, &picker);

	/* Now figure out where the s + v goes */

	double x = picker.white.x * (1.0 - _sat) + picker.red.x * (_sat);
	double y = picker.white.y * (1.0 - _sat) + picker.red.y * (_sat);

	x = x * (_val) + picker.black.x * (1.0 - _val);
	y = y * (_val) + picker.black.y * (1.0 - _val);

	draw_ring(ctx, x, y, 5, 1.5, _val < 0.5 ? rgb(255,255,255) : rgb(0,0,0));

	draw_rounded_rectangle(ctx,off_x + 5, off_y + picker.radius * 2 + 5, 15, 15, 5, my_color);

	char colorName[11];
	sprintf(colorName,"#%02x%02x%02x", _RED(my_color), _GRE(my_color), _BLU(my_color));

	tt_set_size(tt_font_thin, 13);
	tt_draw_string(ctx, tt_font_thin, off_x + 25, off_y + picker.radius * 2 + 18, colorName, rgb(0,0,0));

	redraw_borders();
	flip(ctx);
	yutani_flip(yctx, wina);
}

static double clamp_to_line(struct gfx_point *p, const struct gfx_point *v, const struct gfx_point *w, struct gfx_point *v_t) {
	float lengthlength = gfx_point_distance_squared(v,w);
	struct gfx_point p_v = gfx_point_sub(p,v);
	struct gfx_point w_v = gfx_point_sub(w,v);
	float tmp = gfx_point_dot(&p_v,&w_v) / lengthlength;
	tmp = fmin(1.0,tmp);
	float t = fmax(0.0, tmp);
	w_v.x *= t;
	w_v.y *= t;
	*v_t= gfx_point_add(v, &w_v);
	return gfx_point_distance(p, v_t);
}

static int inside_circle = -1;
static void handle_mouse(struct yutani_msg_window_mouse_event * me) {

	if (me->command != YUTANI_MOUSE_EVENT_DOWN &&
		me->command != YUTANI_MOUSE_EVENT_DRAG) return;

	/* Can we figure out a hue? */
	int32_t x = me->new_x;
	int32_t y = me->new_y;
	struct Picker _picker = {off_x, off_y, win_width / 2, {0,0}, {0,0}, {0,0}, 0, _hue};
	struct Picker * picker = &_picker;
	fill_picker(&_picker);

	/* Picker center */
	int _x = picker->x + picker->radius;
	int _y = picker->y + picker->radius;

	/* hole in the middle */
	double r = dist(x,y,_x,_y);
	int inner_radius = picker->radius * 0.8;

	struct gfx_point p = {(float)x,(float)y};

	if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
		if (r > picker->radius) {
			inside_circle = -1;
			return;
		}

		inside_circle = r < inner_radius;

		if (inside_circle) {
			double proximity;
			if (!in_triangle(&p,&picker->red,&picker->white,&picker->black,&proximity)) {
				inside_circle = -1;
				return;
			}
		}
	}

	if (inside_circle == 1) {
		double proximity;
		if (!in_triangle(&p,&picker->red,&picker->white,&picker->black,&proximity)) {
			struct gfx_point a;
			struct gfx_point b;
			struct gfx_point c;
			double a_d = clamp_to_line(&p,&picker->red,&picker->white,&a);
			double b_d = clamp_to_line(&p,&picker->black,&picker->white,&b);
			double c_d = clamp_to_line(&p,&picker->black,&picker->red,&c);

			if (a_d <= b_d && a_d <= c_d) {
				p = a;
			} else if (b_d <= a_d && b_d <= c_d) {
				p = b;
			} else if (c_d <= a_d && c_d <= b_d) {
				p = c;
			}
		}

		double v = 1.0 - (gfx_line_distance(&p, &picker->red, &picker->white) / picker->dp);
		double _l = gfx_line_distance(&p, &picker->black, &picker->white);
		double _h = gfx_line_distance(&p, &picker->black, &picker->red);
		double s = _l + _h > 0.0 ? (_l / (_l+_h)) : 1.0;

		_sat = s;
		_val = v;
	} else if (inside_circle == 0) {
		_hue = atan2(y-_y,_x-x) + M_PI;
	} else {
		return;
	}

	my_color = hsv_to_rgb(_hue,_sat,_val);
	needs_redraw = 1;
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(ctx, wina);

	struct decor_bounds bounds;
	decor_get_bounds(wina, &bounds);

	win_width  = w - bounds.width;
	win_height = h - bounds.height;
	off_x = bounds.left_width;
	off_y = bounds.top_height;

	redraw_everything();

	yutani_window_resize_done(yctx, wina);
}

static uint32_t parseColor(const char * c) {
	if (*c != '#' || strlen(c) != 7) return rgba(0,0,0,255);

	char r[3] = {c[1],c[2],'\0'};
	char g[3] = {c[3],c[4],'\0'};
	char b[3] = {c[5],c[6],'\0'};

	return rgba(strtoul(r,NULL,16),strtoul(g,NULL,16),strtoul(b,NULL,16),255);
}

int main (int argc, char ** argv) {
	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	if (argc > 1) {
		my_color = parseColor(argv[1]);
		rgb_to_hsv(my_color, &_hue, &_sat, &_val);
	}

	win_width  = 160;
	win_height = 200;

	tt_font_thin = tt_font_from_shm("sans-serif");

	init_decorations();

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	/* Do something with a window */
	wina = yutani_window_create(yctx, win_width + bounds.width, win_height + bounds.height);
	yutani_window_move(yctx, wina, 300, 300);

	decor_get_bounds(wina, &bounds);
	off_x = bounds.left_width;
	off_y = bounds.top_height;
	win_width  = wina->width - bounds.width;
	win_height = wina->height - bounds.height;

	ctx = init_graphics_yutani_double_buffer(wina);

	redraw_everything();

	yutani_window_advertise_icon(yctx, wina, "Color Picker", "art");

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			menu_process_event(yctx, m);
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							should_exit = 1;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
						if (win && win == wina) {
							win->focused = wf->focused;
							needs_redraw = 1;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == wina->wid) {
							resize_finish(wr->width, wr->height);
						}
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						switch (decor_handle_event(yctx, m)) {
							case DECOR_CLOSE:
								should_exit = 1;
								break;
							case DECOR_RIGHT:
								decor_show_default_menu(wina, wina->x + me->new_x, wina->y + me->new_y);
								break;
						}

						if (me->wid == wina->wid) {
							handle_mouse(me);
						}
					}
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
		if (needs_redraw) {
			redraw_everything();
			needs_redraw = 0;
		}
	}

	wait(NULL);

	yutani_close(yctx, wina);
	return 0;
}
