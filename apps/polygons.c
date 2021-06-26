/**
 * @brief Draw filled polygons from line segments.
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
};

struct contour {
	size_t edgeCount;
	size_t nextAlloc;
	struct edge edges[];
};

struct intersection {
	float x;
	int affect;
};

struct contour * shape = NULL;

int edge_sorter_high_scanline(const void * a, const void * b) {
	const struct edge * left  = a;
	const struct edge * right = b;

	float left_lowest = min(left->start.y, left->end.y);
	float right_lowest = min(right->start.y, right->end.y);

	if (left_lowest < right_lowest) return -1;
	if (left_lowest > right_lowest) return 1;

	return 0;
}

void sort_edges(size_t edgeCount, struct edge edges[edgeCount]) {
	qsort(edges, edgeCount, sizeof(struct edge), edge_sorter_high_scanline);
}

int intersection_sorter(const void * a, const void * b) {
	const struct intersection * left  = a;
	const struct intersection * right = b;

	if (left->x < right->x) return -1;
	if (left->x > right->x) return 1;
	return 0;
}

void sort_intersections(size_t cnt, struct intersection intersections[cnt]) {
	qsort(intersections, cnt, sizeof(struct intersection), intersection_sorter);
}

size_t prune_edges(size_t edgeCount, float y, struct edge edges[edgeCount], struct edge into[edgeCount]) {
	size_t outWriter = 0;
	for (size_t i = 0; i < edgeCount; ++i) {
		if (y > edges[i].start.y && y > edges[i].end.y) continue;
		if (y <= edges[i].start.y && y <= edges[i].end.y) break;
		into[outWriter++] = edges[i];
	}
	return outWriter;
}

float edge_at(float y, struct edge * edge) {
	float u = (y - edge->start.y) / (edge->end.y - edge->start.y);
	return edge->start.x + u * (edge->end.x - edge->start.x);
}

int was_moving = 0;
size_t last_start = 0;
static void move_to(float x, float y);
static void add_point(float x, float y) {
	if (!shape) {
		move_to(x,y);
	} else if (was_moving) {
		shape->edges[shape->edgeCount].end.x = x;
		shape->edges[shape->edgeCount].end.y = y;
		shape->edgeCount++;
		was_moving = 0;
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
		was_moving = 0;
	}
}

static void move_to(float x, float y) {
	if (!shape) {
		shape = malloc(sizeof(struct contour) + sizeof(struct edge) * 2);
		shape->edgeCount = 0;
		shape->nextAlloc = 2;
	} else if (!was_moving && shape->edgeCount) {
		add_point(shape->edges[last_start].start.x, shape->edges[last_start].start.y);
	}
	if (shape->edgeCount + 1 == shape->nextAlloc) {
		shape->nextAlloc *= 2;
		shape = realloc(shape, sizeof(struct contour) + sizeof(struct edge) * (shape->nextAlloc));
	}
	shape->edges[shape->edgeCount].start.x = x;
	shape->edges[shape->edgeCount].start.y = y;
	last_start = shape->edgeCount;
	was_moving = 1;
}

static void draw(void) {
	draw_fill(ctx, rgb(0,0,0));
	if (shape) {

		if (last_start + 1 == shape->edgeCount) {
			draw_line(ctx, shape->edges[last_start].start.x, shape->edges[last_start].end.x, shape->edges[last_start].start.y, shape->edges[last_start].end.y, rgb(255,255,255));
		}

		if (shape->edgeCount > 1) {
			/* Oh boy */

			size_t size = shape->edgeCount + 1;
			struct edge * tmp = malloc(sizeof(struct edge) * size);
			memcpy(tmp, shape->edges, sizeof(struct edge) * shape->edgeCount);

			if (was_moving) {
				size--;
			} else {
				tmp[shape->edgeCount].start.x = shape->edges[shape->edgeCount-1].end.x;
				tmp[shape->edgeCount].start.y = shape->edges[shape->edgeCount-1].end.y;
				tmp[shape->edgeCount].end.x = shape->edges[last_start].start.x;
				tmp[shape->edgeCount].end.y = shape->edges[last_start].start.y;
			}

			sort_edges(size, tmp);

			struct edge * intersects = malloc(sizeof(struct edge) * size);
			struct intersection * crosses = malloc(sizeof(struct intersection) * size);
			float * subsamples = malloc(sizeof(float) * width);
			memset(subsamples, 0, sizeof(float) * width);

			/* We have sorted by the scanline at which the line becomes active, so we should be able to do this... */
			int start_y = (int)min(tmp[0].start.y, tmp[0].end.y);
			int yres = 4;
			for (int y = start_y; y < height; ++y) {
				/* Figure out which ones fit here */
				float _y = (float)y;
				for (int l = 0; l < yres; ++l) {
					size_t cnt = prune_edges(size, _y, tmp, intersects);
					if (cnt) {
						int wind = 0;

						/* Get intersections */
						for (size_t j = 0; j < cnt; ++j) {
							crosses[j].x = edge_at(_y,&intersects[j]);
							crosses[j].affect = (intersects[j].start.y < intersects[j].end.y) ? -1 : 1;
						}

						/* Now sort the intersections */
						sort_intersections(cnt, crosses);

						size_t j = 0;
						for (int x = 0; x < width; ++x) {
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
				for (int x = 0; x < width; ++x) {
					int c = subsamples[x] / (float)yres * 255;
					GFX(ctx,x,y) = rgb(c,c,c);
					subsamples[x] = 0;
				}
			}

			free(subsamples);
			free(crosses);
			free(intersects);
			free(tmp);
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
								draw();
								finish_draw();
							} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
								move_to(x, y);
								draw();
								finish_draw();
							} else if (shape && was_moving) {
								draw();
								draw_line(ctx,
									shape->edges[shape->edgeCount].start.x,
									x,
									shape->edges[shape->edgeCount].start.y,
									y,
									rgb(0,255,0));
								finish_draw();
							} else if (shape && !was_moving) {
								draw();
								draw_line(ctx,
									shape->edges[shape->edgeCount-1].end.x,
									x,
									shape->edges[shape->edgeCount-1].end.y,
									y,
									rgb(0,255,0));
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
