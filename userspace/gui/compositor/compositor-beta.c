#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <getopt.h>
#include <sys/stat.h>

#include <cairo.h>

#include "lib/graphics.h"
#include "lib/pthread.h"
#include "lib/mouse.h"
#include "lib/kbd.h"
#include "lib/pex.h"
#include "lib/yutani.h"
#include "lib/hashmap.h"
#include "lib/list.h"

#include "yutani_int.h"

struct {
	int nested;
	int nest_width;
	int nest_height;
} yutani_options = {
	.nested = 0,
	.nest_width = 0,
	.nest_height = 0
};

/* XXX this should be in lib/mouse.h? */
#define MOUSE_BUTTON_LEFT		0x01
#define MOUSE_BUTTON_RIGHT		0x02
#define MOUSE_BUTTON_MIDDLE		0x04


static int usage(char * argv[]) {
	fprintf(stderr,
			"Yutani - Window Compositor\n"
			"\n"
			"usage: %s [-n [-g WxH]] [-h]\n"
			"\n"
			" -n --nested     \033[3mRun in a window.\033[0m\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -g --geometry   \033[3mSet the size of the server framebuffer.\033[0m\n"
			"\n"
			"  Yutani is the standard system compositor.\n"
			"\n",
			argv[0]);
	return 1;
}

/**
 * Parse arguments
 */
static int parse_args(int argc, char * argv[], int * out) {
	static struct option long_opts[] = {
		{"nest",       no_argument,       0, 'n'},
		{"geometry",   required_argument, 0, 'g'},
		{"help",       no_argument,       0, 'h'},
		{0,0,0,0}
	};

	int index, c;
	while ((c = getopt_long(argc, argv, "hg:n", long_opts, &index)) != -1) {
		if (!c) {
			if (long_opts[index].flag == 0) {
				c = long_opts[index].val;
			}
		}
		switch (c) {
			case 'h':
				return usage(argv);
			case 'n':
				yutani_options.nested = 1;
				break;
			case 'g':
				{
					char * c = strstr(optarg, "x");
					if (c) {
						*c = '\0';
						c++;
						yutani_options.nest_width  = atoi(optarg);
						yutani_options.nest_height = atoi(c);
					}
				}
				break;
			default:
				fprintf(stderr, "Unrecognized option: %c\n", c);
				break;
		}
	}
	*out = optind;
	return 0;
}

static int next_buf_id(void) {
	static int _next = 1;
	return _next++;
}

static int next_wid(void) {
	static int _next = 1;
	return _next++;
}

static void rebalance_windows(yutani_globals_t * yg) {
	uint32_t i = 1;
	for (; i < YUTANI_ZORDER_TOP; ++i) {
		if (!yg->zlist[i]) break;
	}
	uint32_t j = i + 1;
	for (; j < YUTANI_ZORDER_TOP; ++j) {
		if (!yg->zlist[j]) break;
	}
	if (j == i + 1) {
		return;
	} else {
		for (j = i; j < YUTANI_ZORDER_TOP; ++j) {
			yg->zlist[j] = yg->zlist[j+1];
			if (yg->zlist[j+1] == NULL) return;
			yg->zlist[j]->z = j;
		}
	}
}

static void reorder_window(yutani_globals_t * yg, yutani_server_window_t * window, uint16_t new_zed) {
	if (!window) {
		return;
	}

	int z = window->z;
	window->z = new_zed;

	if (yg->zlist[z] == window) {
		yg->zlist[z] = NULL;
	}

	if (new_zed == 0 || new_zed == YUTANI_ZORDER_TOP) {
		yg->zlist[new_zed] = window;
		if (z != new_zed) {
			rebalance_windows(yg);
		}
		return;
	}

	if (yg->zlist[new_zed] != window) {
		reorder_window(yg, yg->zlist[new_zed], new_zed + 1);
		yg->zlist[new_zed ] = window;
	}
	if (z != new_zed) {
		rebalance_windows(yg);
	}
}

static void unorder_window(yutani_globals_t * yg, yutani_server_window_t * w) {
	if (yg->zlist[w->z] == w) {
		yg->zlist[w->z] = NULL;
	}
	rebalance_windows(yg);
}

static void make_top(yutani_globals_t * yg, yutani_server_window_t * w) {
	unsigned short index = w->z;

	if (index == YUTANI_ZORDER_BOTTOM) return;
	if (index == YUTANI_ZORDER_TOP) return;

	unsigned short highest = 0;

	for (unsigned int  i = 0; i <= YUTANI_ZORDER_MAX; ++i) {
		if (yg->zlist[i]) {
			yutani_server_window_t * win = yg->zlist[i];

			if (win == w) continue;
			if (win->z == YUTANI_ZORDER_BOTTOM) continue;
			if (win->z == YUTANI_ZORDER_TOP) continue;
			if (highest < win->z) highest = win->z;
			if (win->z > w->z) continue;
		}
	}

	reorder_window(yg, w, highest + 1);
}

static void set_focused_window(yutani_globals_t * yg, yutani_server_window_t * w) {
	if (w == yg->focused_window) {
		return; /* Already focused */
	}

	if (yg->focused_window) {
		/* XXX Send focus change to old focused window */
		yutani_msg_t * response = yutani_msg_build_window_focus_change(yg->focused_window->wid, 0);
		pex_send(yg->server, yg->focused_window->owner, response->size, (char *)response);
		free(response);
	}
	yg->focused_window = w;
	if (w) {
		/* XXX Send focus change to new focused window */
		yutani_msg_t * response = yutani_msg_build_window_focus_change(w->wid, 1);
		pex_send(yg->server, w->owner, response->size, (char *)response);
		free(response);
		make_top(yg, w);
	} else {
		/* XXX */
		yg->focused_window = yg->zlist[0];
	}
}

static yutani_server_window_t * get_focused(yutani_globals_t * yg) {
	if (yg->focused_window) return yg->focused_window;
	return yg->zlist[0];
}

int best_z_option(yutani_globals_t * yg) {
	for (int i = 1; i < YUTANI_ZORDER_TOP; ++i) {
		if (!yg->zlist[i]) return i;
	}
	return -1;
}


static yutani_server_window_t * server_window_create(yutani_globals_t * yg, int width, int height, uint32_t owner) {
	yutani_server_window_t * win = malloc(sizeof(yutani_server_window_t));

	win->wid = next_wid();
	win->owner = owner;
	list_insert(yg->windows, win);
	hashmap_set(yg->wids_to_windows, (void*)win->wid, win);

	win->x = 0;
	win->y = 0;
	win->z = best_z_option(yg);
	yg->zlist[win->z] = win;
	win->width = width;
	win->height = height;
	win->bufid = next_buf_id();

	char key[1024];
	YUTANI_SHMKEY(key, 1024, win);

	size_t size = (width * height * 4);
	win->buffer = (uint8_t *)syscall_shm_obtain(key, &size);
	return win;
}

/**
 * Mouse input thread
 *
 * Reads the kernel mouse device and converts
 * mouse clicks and movements into event objects
 * to send to the core compositor.
 */
void * mouse_input(void * garbage) {
	int mfd = open("/dev/mouse", O_RDONLY);

	yutani_t * y = yutani_init();
	mouse_device_packet_t packet;

	while (1) {
		int r = read(mfd, (char *)&packet, sizeof(mouse_device_packet_t));
		if (r > 0) {
			yutani_msg_t * m = yutani_msg_build_mouse_event(0, &packet);
			int result = yutani_msg_send(y, m);
			free(m);
		}
	}
}

/**
 * Keyboard input thread
 *
 * Reads the kernel keyboard device and converts
 * key presses into event objects to send to the
 * core compositor.
 */
void * keyboard_input(void * garbage) {
	int kfd = open("/dev/kbd", O_RDONLY);

	yutani_t * y = yutani_init();
	key_event_t event;

	while (1) {
		char buf[1];
		int r = read(kfd, buf, 1);
		if (r > 0) {
			kbd_scancode(buf[0], &event);
			yutani_msg_t * m = yutani_msg_build_key_event(0, &event);
			int result = yutani_msg_send(y, m);
			free(m);
		}
	}
}

#define FONT_PATH "/usr/share/fonts/"
#define FONT(a,b) {YUTANI_SERVER_IDENTIFIER ".fonts." a, FONT_PATH b}

struct font_def {
	char * identifier;
	char * path;
};

static struct font_def fonts[] = {
	FONT("sans-serif",            "DejaVuSans.ttf"),
	FONT("sans-serif.bold",       "DejaVuSans-Bold.ttf"),
	FONT("sans-serif.italic",     "DejaVuSans-Oblique.ttf"),
	FONT("sans-serif.bolditalic", "DejaVuSans-BoldOblique.ttf"),
	FONT("monospace",             "DejaVuSansMono.ttf"),
	FONT("monospace.bold",        "DejaVuSansMono-Bold.ttf"),
	FONT("monospace.italic",      "DejaVuSansMono-Oblique.ttf"),
	FONT("monospace.bolditalic",  "DejaVuSansMono-BoldOblique.ttf"),
	{NULL, NULL}
};

static char * precache_shmfont(char * ident, char * name) {
	FILE * f = fopen(name, "r");
	size_t s = 0;
	fseek(f, 0, SEEK_END);
	s = ftell(f);
	fseek(f, 0, SEEK_SET);

	size_t shm_size = s;
	char * font = (char *)syscall_shm_obtain(ident, &shm_size); //malloc(s);
	assert((shm_size >= s) && "shm_obtain returned too little memory to load a font into!");

	fread(font, s, 1, f);

	fclose(f);
	return font;
}

static void load_fonts(void) {
	int i = 0;
	while (fonts[i].identifier) {
		fprintf(stderr, "[compositor] Loading font %s -> %s\n", fonts[i].path, fonts[i].identifier);
		precache_shmfont(fonts[i].identifier, fonts[i].path);
		++i;
	}
}

static void draw_cursor(yutani_globals_t * yg, int x, int y) {
	draw_sprite(yg->backend_ctx, &yg->mouse_sprite, x / MOUSE_SCALE - MOUSE_OFFSET_X, y / MOUSE_SCALE - MOUSE_OFFSET_Y);
}

static void yutani_add_clip(yutani_globals_t * yg, double x, double y, double w, double h) {
	cairo_rectangle(yg->framebuffer_ctx, x, y, w, h);
	cairo_rectangle(yg->real_ctx, x, y, w, h);
}

static void save_cairo_states(yutani_globals_t * yg) {
	cairo_save(yg->framebuffer_ctx);
	cairo_save(yg->selectbuffer_ctx);
	cairo_save(yg->real_ctx);
}

static void restore_cairo_states(yutani_globals_t * yg) {
	cairo_restore(yg->framebuffer_ctx);
	cairo_restore(yg->selectbuffer_ctx);
	cairo_restore(yg->real_ctx);
}

static void yutani_set_clip(yutani_globals_t * yg) {
	cairo_clip(yg->framebuffer_ctx);
	cairo_clip(yg->real_ctx);
}

yutani_server_window_t * top_at(yutani_globals_t * yg, uint16_t x, uint16_t y) {
	uint32_t c = ((uint32_t *)yg->select_framebuffer)[(yg->width * y + x)];
	yutani_wid_t w = (_RED(c) << 16) | (_GRE(c) << 8) | (_BLU(c));
	return hashmap_get(yg->wids_to_windows, (void *)w);
}

static int window_is_top(yutani_globals_t * yg, yutani_server_window_t * window) {
	/* For now, just use simple z-order */
	return window->z == YUTANI_ZORDER_TOP;
}

static int window_is_bottom(yutani_globals_t * yg, yutani_server_window_t * window) {
	/* For now, just use simple z-order */
	return window->z == YUTANI_ZORDER_BOTTOM;
}

static int yutani_blit_window(yutani_globals_t * yg, yutani_server_window_t * window, yutani_server_window_t * modifiers) {

	/* If there are no modifiers to be set, use the window's existing setup */
	if (!modifiers) {
		modifiers = window;
	}

	/* Obtain the previously initialized cairo contexts */
	cairo_t * cr = yg->framebuffer_ctx;
	cairo_t * cs = yg->selectbuffer_ctx;

	/* Window stride is always 4 bytes per pixel... */
	int stride = window->width * 4;

	/* Initialize a cairo surface object for this window */
	cairo_surface_t * surf = cairo_image_surface_create_for_data(
			window->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);

	/* Save cairo contexts for both rendering and selectbuffer */
	cairo_save(cr);
	cairo_save(cs);

	/*
	 * Offset the rendering context appropriately for the position of the window
	 * based on the modifier paramters
	 */
	cairo_translate(cr, modifiers->x, modifiers->y);
	cairo_translate(cs, modifiers->x, modifiers->y);

	/* Top and bottom windows can not be rotated. */
	if (!window_is_top(yg, window) && !window_is_bottom(yg, window)) {
		/* Calcuate radians from degrees */
#if 0
		double r = window->rotation * M_PI / 180.0;

		/* Rotate the render context about the center of the window */
		cairo_translate(cr, (int)( window->width / 2), (int)( window->height / 2));
		cairo_rotate(cr, r);
		cairo_translate(cr, (int)(-window->width / 2), (int)(-window->height / 2));

		/* Rotate the selectbuffer context about the center of the window */
		cairo_translate(cs, (int)( window->width / 2), (int)( window->height / 2));
		cairo_rotate(cs, r);
		cairo_translate(cs, (int)(-window->width / 2), (int)(-window->height / 2));

		/* Prefer faster filter when rendering rotated windows */
		cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_FAST);
#endif
	}

	/* Paint window */
	cairo_set_source_surface(cr, surf, 0, 0);
	cairo_paint(cr);

	/* Clean up */
	cairo_surface_destroy(surf);

	/* Paint select buffer */
	cairo_set_operator(cs, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgb(cs,
			((window->wid & 0xFF0000) >> 16) / 255.0,
			((window->wid & 0xFF00) >> 8) / 255.0,
			((window->wid & 0xFF) >> 0) / 255.0
	);
	cairo_rectangle(cs, 0, 0, window->width, window->height);
	cairo_set_antialias(cs, CAIRO_ANTIALIAS_NONE);
	cairo_fill(cs);

	/* Restore context stack */
	cairo_restore(cr);
	cairo_restore(cs);

	return 0;
}

static void redraw_windows(yutani_globals_t * yg) {
	/* Save the cairo contexts so we can apply clipping */
	save_cairo_states(yg);
	int has_updates = 0;

	/* We keep our own temporary mouse coordinates as they may change while we're drawing. */
	int tmp_mouse_x = yg->mouse_x;
	int tmp_mouse_y = yg->mouse_y;

	/* If the mouse has moved, that counts as two damage regions */
	if ((yg->last_mouse_x != tmp_mouse_x) || (yg->last_mouse_y != tmp_mouse_y)) {
		has_updates = 2;
		yutani_add_clip(yg, yg->last_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, yg->last_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, 64, 64);
		yutani_add_clip(yg, tmp_mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, tmp_mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y, 64, 64);
	}

	yg->last_mouse_x = tmp_mouse_x;
	yg->last_mouse_y = tmp_mouse_y;

	/* Calculate damage regions from currently queued updates */
	while (yg->update_list->length) {
		node_t * win = list_dequeue(yg->update_list);
		yutani_server_window_t * window = (yutani_server_window_t *)win->value;

		/* We add a clip region for each window in the update queue */
		has_updates = 1;
		yutani_add_clip(yg, window->x, window->y, window->width, window->height);
		free(win);
	}

	/* Render */
	if (has_updates) {

		yutani_set_clip(yg);

		/*
		 * In theory, we should restrict this to windows within the clip region,
		 * but calculating that may be more trouble than it's worth;
		 * we also need to render windows in stacking order...
		 */
		for (unsigned int  i = 0; i <= YUTANI_ZORDER_MAX; ++i) {
			if (yg->zlist[i]) {
				yutani_blit_window(yg, yg->zlist[i], NULL);
			}
		}

		/*
		 * Draw the cursor.
		 * We may also want to draw other compositor elements, like effects, but those
		 * can also go in the stack order of the windows.
		 */
		draw_cursor(yg, tmp_mouse_x, tmp_mouse_y);

		/*
		 * Flip the updated areas. This minimizes writes to video memory,
		 * which is very important on real hardware where these writes are slow.
		 */
		cairo_set_operator(yg->real_ctx, CAIRO_OPERATOR_SOURCE);
		cairo_translate(yg->real_ctx, 0, 0);
		cairo_set_source_surface(yg->real_ctx, yg->framebuffer_surface, 0, 0);
		cairo_paint(yg->real_ctx);

	}

	/* Restore the cairo contexts to reset clip regions */
	restore_cairo_states(yg);
}

void yutani_cairo_init(yutani_globals_t * yg) {

	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, yg->width);
	yg->framebuffer_surface = cairo_image_surface_create_for_data(
			yg->backend_framebuffer, CAIRO_FORMAT_ARGB32, yg->width, yg->height, stride);
	yg->real_surface = cairo_image_surface_create_for_data(
			yg->backend_ctx->buffer, CAIRO_FORMAT_ARGB32, yg->width, yg->height, stride);

	yg->select_framebuffer = malloc(YUTANI_BYTE_DEPTH * yg->width * yg->height);

	yg->selectbuffer_surface = cairo_image_surface_create_for_data(
			yg->select_framebuffer, CAIRO_FORMAT_ARGB32, yg->width, yg->height, stride);

	yg->framebuffer_ctx = cairo_create(yg->framebuffer_surface);
	yg->selectbuffer_ctx = cairo_create(yg->selectbuffer_surface);
	yg->real_ctx = cairo_create(yg->real_surface);

	yg->update_list = list_create();
}

void * redraw(void * in) {
	yutani_globals_t * yg = in;
	while (1) {
		/*
		 * Perform whatever redraw work is required.
		 */
		redraw_windows(yg);

		/*
		 * Attempt to run at about 60fps...
		 * we should actually see how long it took to render so
		 * we can sleep *less* if it took a long time to render
		 * this particular frame. We are definitely not
		 * going to run at 60fps unless there's nothing to do
		 * (and even then we've wasted cycles checking).
		 */
		usleep(16666);
	}
}

static void handle_mouse_event(yutani_globals_t * yg, struct yutani_msg_mouse_event * me)  {
	/* XXX handle focus change, drag, etc. */

	if (me->event.buttons & MOUSE_BUTTON_LEFT) {
		yutani_server_window_t * n_focused = top_at(yg, yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE);
		fprintf(stderr, "[yutani-server] Window at %dx%d is %d (%dx%d)\n", yg->mouse_x / MOUSE_SCALE, yg->mouse_y / MOUSE_SCALE, n_focused->wid, n_focused->width, n_focused->height);
		set_focused_window(yg, n_focused);
	}
}

/**
 * main
 */
int main(int argc, char * argv[]) {

	int argx = 0;
	int results = parse_args(argc, argv, &argx);
	if (results) return results;

	yutani_globals_t * yg = malloc(sizeof(yutani_globals_t));
	memset(yg, 0x00, sizeof(yutani_globals_t));
	yg->backend_ctx = init_graphics_fullscreen_double_buffer();

	if (!yg->backend_ctx) {
		free(yg);
		fprintf(stderr, "%s: Failed to open framebuffer, bailing.\n", argv[0]);
		return 1;
	}

	yg->width = yg->backend_ctx->width;
	yg->height = yg->backend_ctx->height;

	draw_fill(yg->backend_ctx, rgb(0,0,0));
	flip(yg->backend_ctx);

	yg->backend_framebuffer = yg->backend_ctx->backbuffer;
	yg->pex_endpoint = "compositor";
	setenv("DISPLAY", yg->pex_endpoint, 1);

	FILE * server = pex_bind(yg->pex_endpoint);
	yg->server = server;

	fprintf(stderr, "[yutani] Loading fonts...\n");
	load_fonts();
	fprintf(stderr, "[yutani] Done.\n");

	load_sprite_png(&yg->mouse_sprite, "/usr/share/arrow.png");
	yg->last_mouse_x = 0;
	yg->last_mouse_y = 0;
	yg->mouse_x = yg->width * MOUSE_SCALE / 2;
	yg->mouse_y = yg->height * MOUSE_SCALE / 2;

	yg->windows = list_create();
	yg->wids_to_windows = hashmap_create_int(10);

	yutani_cairo_init(yg);

	pthread_t mouse_thread;
	pthread_create(&mouse_thread, NULL, mouse_input, NULL);

	pthread_t keyboard_thread;
	pthread_create(&keyboard_thread, NULL, keyboard_input, NULL);

	pthread_t render_thread;
	pthread_create(&render_thread, NULL, redraw, yg);

	if (!fork()) {
		fprintf(stderr, "Have %d args, argx=%d\n", argc, argx);
		if (argx < argc) {
			fprintf(stderr, "Starting %s\n", argv[argx]);
			execvp(argv[argx], &argv[argx]);
		} else {
			char * args[] = {"/bin/glogin-beta", NULL};
			execvp(args[0], args);
		}
	}

	while (1) {
		pex_packet_t * p = calloc(PACKET_SIZE, 1);
		pex_listen(server, p);

		yutani_msg_t * m = (yutani_msg_t *)p->data;

		if (m->magic != YUTANI_MSG__MAGIC) {
			fprintf(stderr, "[yutani-server] Message has bad magic. (Should eject client, but will instead skip this message.) 0x%x\n", m->magic);
			continue;
		}

		switch(m->type) {
			case YUTANI_MSG_HELLO: {
				fprintf(stderr, "[yutani-server] And hello to you, %08x!\n", p->source);
				yutani_msg_t * response = yutani_msg_build_welcome(yg->width, yg->height);
				pex_send(server, p->source, response->size, (char *)response);
				free(response);
			} break;
			case YUTANI_MSG_WINDOW_NEW: {
				struct yutani_msg_window_new * wn = (void *)m->data;
				fprintf(stderr, "[yutani-server] Client %08x requested a new window (%xx%x).\n", p->source, wn->width, wn->height);
				yutani_server_window_t * w = server_window_create(yg, wn->width, wn->height, p->source);
				yutani_msg_t * response = yutani_msg_build_window_init(w->wid, w->width, w->height, w->bufid);
				pex_send(server, p->source, response->size, (char *)response);
				free(response);
			} break;
			case YUTANI_MSG_FLIP: {
				struct yutani_msg_flip * wf = (void *)m->data;
				yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wf->wid);
				if (w) {
					list_insert(yg->update_list, w);
				}
			} break;
			case YUTANI_MSG_KEY_EVENT: {
				/* XXX Verify this is from a valid device client */
				struct yutani_msg_key_event * ke = (void *)m->data;
				yutani_server_window_t * focused = get_focused(yg);
				if (focused) {
					/* XXX focused window */
					ke->wid = focused->wid;
					pex_send(server, focused->owner, m->size, (char *)m);
					/* XXX key loggers ;) */
				}
			} break;
			case YUTANI_MSG_MOUSE_EVENT: {
				/* XXX Verify this is from a valid device client */
				struct yutani_msg_mouse_event * me = (void *)m->data;

				yg->mouse_x += me->event.x_difference * 3;
				yg->mouse_y -= me->event.y_difference * 3;

				if (yg->mouse_x < 0) yg->mouse_x = 0;
				if (yg->mouse_y < 0) yg->mouse_y = 0;
				if (yg->mouse_x > (yg->width) * MOUSE_SCALE) yg->mouse_x = (yg->width) * MOUSE_SCALE;
				if (yg->mouse_y > (yg->height) * MOUSE_SCALE) yg->mouse_y = (yg->height) * MOUSE_SCALE;

				/* XXX Handle mouse events */
				handle_mouse_event(yg, me);
			} break;
			case YUTANI_MSG_WINDOW_MOVE: {
				struct yutani_msg_window_move * wm = (void *)m->data;
				fprintf(stderr, "[yutani-server] %08x wanted to move window %d\n", p->source, wm->wid);
				yutani_server_window_t * win = hashmap_get(yg->wids_to_windows, (void*)wm->wid);
				if (win) {
					win->x = wm->x;
					win->y = wm->y;
				} else {
					fprintf(stderr, "[yutani-server] %08x wanted to move window %d, but I can't find it?\n", p->source, wm->wid);
				}
			} break;
			case YUTANI_MSG_WINDOW_CLOSE: {
				struct yutani_msg_window_close * wc = (void *)m->data;
				yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)wc->wid);
				if (w) {
					/* XXX free window */
					hashmap_remove(yg->wids_to_windows, (void *)wc->wid);
					list_remove(yg->windows, list_index_of(yg->windows, w));
					list_insert(yg->update_list, w);
					unorder_window(yg, w);
					if (w == yg->focused_window) {
						yg->focused_window = NULL;
					}
				}
			} break;
			case YUTANI_MSG_WINDOW_STACK: {
				struct yutani_msg_window_stack * ws = (void *)m->data;
				yutani_server_window_t * w = hashmap_get(yg->wids_to_windows, (void *)ws->wid);
				if (w) {
					reorder_window(yg, w, ws->z);
				}
			} break;
			default: {
				fprintf(stderr, "[yutani-server] Unknown type!\n");
			} break;
		}
		free(p);
	}

	return 0;
}
