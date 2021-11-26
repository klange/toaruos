/**
 * @brief Draw filled polygons from line segments.
 *
 * This is an older version of the polygon rasterizer that turned
 * into the TrueType gylph rasterizer. Still makes for a neat
 * little graphical demo. Should probably be updated to use
 * the glyph rasterization code instead of its own oudated
 * copy though...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */

#include <stdio.h>

#include <sys/fswait.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>

#define min(a,b) ((a) < (b) ? (a) : (b))

static int left, top, width, height;

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;
static int should_exit = 0;

struct coord {
	float x;
	float y;
};

struct edge {
	struct coord start;
	struct coord end;
	int direction;
};

struct contour {
	size_t edgeCount;
	size_t nextAlloc;
	size_t flags;
	size_t last_start;
	struct edge edges[];
};

struct intersection {
	float x;
	int affect;
};

struct shape {
	size_t edgeCount;
	int lastY;
	struct edge edges[];
};

static int edge_sorter_high_scanline(const void * a, const void * b) {
	const struct edge * left  = a;
	const struct edge * right = b;

	if (left->start.y < right->start.y) return -1;
	if (left->start.y > right->start.y) return 1;
	return 0;
}

static void sort_edges(size_t edgeCount, struct edge edges[edgeCount]) {
	qsort(edges, edgeCount, sizeof(struct edge), edge_sorter_high_scanline);
}

static int intersection_sorter(const void * a, const void * b) {
	const struct intersection * left  = a;
	const struct intersection * right = b;

	if (left->x < right->x) return -1;
	if (left->x > right->x) return 1;
	return 0;
}

static void sort_intersections(size_t cnt, struct intersection intersections[cnt]) {
	qsort(intersections, cnt, sizeof(struct intersection), intersection_sorter);
}

static size_t prune_edges(size_t edgeCount, float y, struct edge edges[edgeCount], struct edge into[edgeCount]) {
	size_t outWriter = 0;
	for (size_t i = 0; i < edgeCount; ++i) {
		if (y > edges[i].start.y && y > edges[i].end.y) continue;
		if (y <= edges[i].start.y && y <= edges[i].end.y) break;
		into[outWriter++] = edges[i];
	}
	return outWriter;
}

static float edge_at(float y, struct edge * edge) {
	float u = (y - edge->start.y) / (edge->end.y - edge->start.y);
	return edge->start.x + u * (edge->end.x - edge->start.x);
}

struct shape * path_finish(struct contour * in) {
	size_t size = in->edgeCount + 1;
	struct shape * tmp = malloc(sizeof(struct shape) + sizeof(struct edge) * size);
	memcpy(tmp->edges, in->edges, sizeof(struct edge) * in->edgeCount);

	if (in->flags & 1) {
		size--;
	} else {
		tmp->edges[in->edgeCount].start.x = in->edges[in->edgeCount-1].end.x;
		tmp->edges[in->edgeCount].start.y = in->edges[in->edgeCount-1].end.y;
		tmp->edges[in->edgeCount].end.x   = in->edges[in->last_start].start.x;
		tmp->edges[in->edgeCount].end.y   = in->edges[in->last_start].start.y;
	}

	for (size_t i = 0; i < size; ++i) {
		if (tmp->edges[i].start.y < tmp->edges[i].end.y) {
			tmp->edges[i].direction = 1;
		} else {
			tmp->edges[i].direction = -1;
			struct coord j = tmp->edges[i].start;
			tmp->edges[i].start = tmp->edges[i].end;
			tmp->edges[i].end = j;
		}
	}

	sort_edges(size, tmp->edges);
	tmp->edgeCount = size;
	tmp->lastY = 0;
	for (size_t i = 0; i < size; ++i) {
		if (tmp->edges[i].end.y + 1 > tmp->lastY) tmp->lastY = tmp->edges[i].end.y + 1;
	}

	return tmp;
}

void path_paint(gfx_context_t * ctx, struct shape * shape, uint32_t color) {
	size_t size = shape->edgeCount;
	struct edge * intersects = malloc(sizeof(struct edge) * size);
	struct intersection * crosses = malloc(sizeof(struct intersection) * size);
	float * subsamples = malloc(sizeof(float) * width);
	memset(subsamples, 0, sizeof(float) * width);

	/* We have sorted by the scanline at which the line becomes active, so we should be able to do this... */
	int yres = 4;
	for (int y = shape->edges[0].start.y; y < shape->lastY; ++y) {
		/* Figure out which ones fit here */
		float _y = y;
		int start_x = ctx->width;
		int max_x = 0;
		for (int l = 0; l < yres; ++l) {
			size_t cnt = prune_edges(size, _y, shape->edges, intersects);
			if (cnt) {
				/* Get intersections */
				for (size_t j = 0; j < cnt; ++j) {
					crosses[j].x = edge_at(_y,&intersects[j]);
					crosses[j].affect = intersects[j].direction;
				}

				/* Now sort the intersections */
				sort_intersections(cnt, crosses);

				if (crosses[0].x < start_x) start_x = crosses[0].x;
				if (crosses[cnt-1].x+1 > max_x) max_x = crosses[cnt-1].x+1;

				int wind = 0;
				size_t j = 0;
				for (int x = 0; x < width && j < cnt; ++x) {
					while (j < cnt && x > crosses[j].x) {
						wind += crosses[j].affect;
						j++;
					}
					float last = x;
					while (j < cnt && (x+1) > crosses[j].x) {
						if (wind != 0) {
							subsamples[x] += crosses[j].x - last;
						}
						last = crosses[j].x;
						wind += crosses[j].affect;
						j++;
					}
					if (wind != 0) {
						subsamples[x] += (x+1) - last;
					}
				}
			}
			_y += 1.0/(float)yres;
		}
		for (int x = start_x; x < max_x && x < ctx->width; ++x) {
			unsigned int c = subsamples[x] / (float)yres * (float)_ALP(color);
			uint32_t nc = premultiply((color & 0xFFFFFF) | ((c & 0xFF) << 24));
			GFX(ctx, x, y) = alpha_blend_rgba(GFX(ctx, x, y), nc);
			subsamples[x] = 0;
		}
	}

	free(subsamples);
	free(crosses);
	free(intersects);
}

struct contour * shape = NULL;
struct shape * finalizedShape = NULL;
static void move_to(float x, float y);
static uint32_t myColor = 0;
static void add_point(float x, float y) {
	myColor = rgb(rand() % 255,rand() % 255,rand() % 255);
	if (!shape) {
		move_to(x,y);
	} else if (shape->flags & 1) {
		shape->edges[shape->edgeCount].end.x = x;
		shape->edges[shape->edgeCount].end.y = y;
		shape->edgeCount++;
		shape->flags &= ~1;
	} else {
		if (shape->edgeCount + 1 == shape->nextAlloc) {
			shape->nextAlloc *= 2;
			shape = realloc(shape, sizeof(struct contour) + sizeof(struct edge) * (shape->nextAlloc));
		}
		shape->edges[shape->edgeCount].start.x = shape->edges[shape->edgeCount-1].end.x;
		shape->edges[shape->edgeCount].start.y = shape->edges[shape->edgeCount-1].end.y;
		shape->edges[shape->edgeCount].end.x = x;
		shape->edges[shape->edgeCount].end.y = y;
		shape->edgeCount++;
		shape->flags &= ~1;
	}
}

static void move_to(float x, float y) {
	if (!shape) {
		shape = malloc(sizeof(struct contour) + sizeof(struct edge) * 2);
		shape->edgeCount = 0;
		shape->nextAlloc = 2;
		shape->flags = 0;
		shape->last_start = 0;
	} else if (!(shape->flags & 1) && shape->edgeCount) {
		add_point(shape->edges[shape->last_start].start.x, shape->edges[shape->last_start].start.y);
	}
	if (shape->edgeCount + 1 == shape->nextAlloc) {
		shape->nextAlloc *= 2;
		shape = realloc(shape, sizeof(struct contour) + sizeof(struct edge) * (shape->nextAlloc));
	}
	shape->edges[shape->edgeCount].start.x = x;
	shape->edges[shape->edgeCount].start.y = y;
	shape->last_start = shape->edgeCount;
	shape->flags |= 1;
}

static void draw(void) {
	draw_fill(ctx, rgba(0,0,0,10));
	if (shape) {

		if (shape->last_start + 1 == shape->edgeCount) {
			draw_line(ctx, shape->edges[shape->last_start].start.x, shape->edges[shape->last_start].end.x, shape->edges[shape->last_start].start.y, shape->edges[shape->last_start].end.y, rgb(255,255,255));
		}

		if (finalizedShape) {
			/* Oh boy */
			path_paint(ctx, finalizedShape, myColor);
		}
	}
}

static void finish_draw(void) {
	flip(ctx);
	yutani_flip(yctx, wina);
}

int main (int argc, char ** argv) {
	left   = 100;
	top    = 100;
	width  = 500;
	height = 500;

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
		return 1;
	}

	wina = yutani_window_create(yctx, width, height);
	yutani_window_move(yctx, wina, left, top);
	yutani_window_advertise_icon(yctx, wina, "polygons", "polygons");

	ctx = init_graphics_yutani_double_buffer(wina);
	draw();
	finish_draw();

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
							}
						}
						break;
					case YUTANI_MSG_WINDOW_MOUSE_EVENT:
						{
							struct yutani_msg_window_mouse_event * me = (void*)m->data;
							float x = (float)me->new_x;
							float y = (float)me->new_y;
							if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
								add_point(x, y);
								if (finalizedShape) free(finalizedShape);
								finalizedShape = path_finish(shape);
								draw();
								finish_draw();
							} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
								move_to(x, y);
								draw();
								finish_draw();
							} else if (shape && (shape->flags & 1)) {
								draw();
								draw_line(ctx,
									shape->edges[shape->edgeCount].start.x,
									x,
									shape->edges[shape->edgeCount].start.y,
									y,
									rgb(0,200,0));
								finish_draw();
							} else if (shape && !(shape->flags & 1)) {
								draw();
								draw_line(ctx,
									shape->edges[shape->edgeCount-1].end.x,
									x,
									shape->edges[shape->edgeCount-1].end.y,
									y,
									rgb(0,200,0));
								finish_draw();
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

	return 0;
}
