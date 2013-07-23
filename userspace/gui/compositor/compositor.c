/*
 * Compositor
 * 
 * This is the window compositor application.
 * It serves shared memory regions to clients
 * and renders them to the screen.
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <sys/stat.h>
#include <cairo.h>
#include <signal.h>

#include "lib/list.h"
#include "lib/graphics.h"
#include "lib/window.h"
#include "lib/pthread.h"
#include "lib/kbd.h"

#include "../kernel/include/mouse.h"

#define SPRITE_COUNT 2
#define WIN_D 32
#define WIN_B (WIN_D / 8)
#define MOUSE_SCALE 3
#define MOUSE_OFFSET_X 26
#define MOUSE_OFFSET_Y 26
#define SPRITE_MOUSE 1
#define WINDOW_LAYERS 0x10000
#define FONT_PATH "/usr/share/fonts/"
#define FONT(a,b) {WINS_SERVER_IDENTIFIER ".fonts." a, FONT_PATH b}
#define BUFFER_SAFE_ZONE 7680

#define MODE_NORMAL        0x001
#define MODE_SCALE         0x002
#define MODE_WINDOW_PICKER 0x003

#define SCREENSHOT_WHOLE_SCREEN 1
#define SCREENSHOT_THIS_WINDOW  2

unsigned int tick_count = 0;

int animation_lengths[] = {
	0,
	256,
	256,
	256,
	10000,
};

struct font_def {
	char * identifier;
	char * path;
};

/* Non-public bits from window.h */
extern FILE *fdopen(int fd, const char *mode);

void actually_destroy_window(server_window_t * win);

server_window_t * focused = NULL;
server_window_t * windows[WINDOW_LAYERS];
sprite_t * sprites[SPRITE_COUNT];
gfx_context_t * ctx;
gfx_context_t * select_ctx;
list_t * process_list;
list_t * windows_to_clean;
int32_t mouse_x, mouse_y;
int32_t click_x, click_y;
uint32_t mouse_discard = 0;
volatile int am_drawing  = 0;
server_window_t * moving_window = NULL;
int32_t    moving_window_l = 0;
int32_t    moving_window_t = 0;
server_window_t * resizing_window = NULL;
int32_t    resizing_window_w = 0;
int32_t    resizing_window_h = 0;
cairo_t * cr;
cairo_t * cs;
cairo_surface_t * surface;
cairo_surface_t * selface;
wid_t volatile _next_wid = 1;
wins_server_global_t volatile * _request_page;
int error;
int focus_next_scale = 0;
int window_picker_index = 0;
int take_screenshot_now = 0;

int management_mode = MODE_NORMAL;

struct font_def fonts[] = {
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

static void spin_lock(int volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		syscall_yield();
	}
}

static void spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
}

void redraw_cursor() {
	draw_sprite(ctx, sprites[SPRITE_MOUSE], mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y);
}

static server_window_t * get_window_with_process (process_windows_t * pw, wid_t wid) {
	foreach (m, pw->windows) {
		server_window_t * w = (server_window_t *)m->value;
		if (w->wid == wid) {
			return w;
		}
	}

	return NULL;
}

void init_process_list () {
	process_list = list_create();
	memset(windows, 0x00000000, sizeof(server_window_t *) * WINDOW_LAYERS);
}

void send_window_event (process_windows_t * pw, uint8_t event, w_window_t * packet) {
	/* Construct the header */
	wins_packet_t header;
	header.magic = WINS_MAGIC;
	header.command_type = event;
	header.packet_size = sizeof(w_window_t);

	/* Send them */
	struct stat buf;
	fstat(pw->event_pipe, &buf);

	if (buf.st_size < BUFFER_SAFE_ZONE) {
		write(pw->event_pipe, &header, sizeof(wins_packet_t));
		write(pw->event_pipe, packet, sizeof(w_window_t));
		kill(pw->pid, SIGWINEVENT); // SIGWINEVENT
		syscall_yield();
	} else {
		fprintf(stderr, "[compositor] This client (pid=%d) is lagging, we are dropping WINDOW EVENTS!\n", pw->pid);
		kill(pw->pid, SIGWINEVENT); // SIGWINEVENT
		syscall_yield();
	}
}

void send_keyboard_event (process_windows_t * pw, uint8_t event, w_keyboard_t packet) {
	/* Construct the header */
	wins_packet_t header;
	header.magic = WINS_MAGIC;
	header.command_type = event;
	header.packet_size = sizeof(w_keyboard_t);

	/* Send them */
	struct stat buf;
	fstat(pw->event_pipe, &buf);

	if (buf.st_size < BUFFER_SAFE_ZONE) {
		write(pw->event_pipe, &header, sizeof(wins_packet_t));
		write(pw->event_pipe, &packet, sizeof(w_keyboard_t));
		kill(pw->pid, SIGWINEVENT); // SIGWINEVENT
		syscall_yield();
	} else {
		fprintf(stderr, "[compositor] This client (pid=%d) is lagging, we are dropping keyboard packets.\n", pw->pid);
		kill(pw->pid, SIGWINEVENT); // SIGWINEVENT
		syscall_yield();
	}
}

void send_mouse_event (process_windows_t * pw, uint8_t event, w_mouse_t * packet) {
	/* Construct the header */
	wins_packet_t header;
	header.magic = WINS_MAGIC;
	header.command_type = event;
	header.packet_size = sizeof(w_mouse_t);

	/* Send them */
	struct stat buf;
	fstat(pw->event_pipe, &buf);

	if (buf.st_size < BUFFER_SAFE_ZONE) {
		fwrite(&header, 1, sizeof(wins_packet_t), pw->event_pipe_file);
		fwrite(packet,  1, sizeof(w_mouse_t),     pw->event_pipe_file);
		fflush(pw->event_pipe_file);
	} else {
		fprintf(stderr, "[compositor] This client (pid=%d) is lagging, we are dropping mouse packets.\n", pw->pid);
	}
	//kill(pw->pid, SIGWINEVENT); // SIGWINEVENT
	//syscall_yield();
}


int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

uint8_t is_between(int32_t lo, int32_t hi, int32_t val) {
	if (val >= lo && val < hi) return 1;
	return 0;
}

server_window_t * top_at(uint16_t x, uint16_t y) {
	char buf[512];
	sprintf(buf, "Looking for top window at %d %d.\n", x, y);
	syscall_print(buf);

	uint32_t c = GFXR(select_ctx, x, y);

	sprintf(buf, "Select buf contents: 0x%x\n", c);
	syscall_print(buf);

	unsigned int w = (_GRE(c) << 8) | (_BLU(c));
	sprintf(buf, "0x%x %x = 0x%x", _GRE(c), _BLU(c), w);
	syscall_print(buf);

	sprintf(buf, "Window offset %d = %p\n", w, windows[w]);
	syscall_print(buf);

	return windows[w];
}

void rebalance_windows() {
	uint32_t i = 1;
	for (; i < 0xFFF8; ++i) {
		if (!windows[i]) break;
	}
	uint32_t j = i + 1;
	for (; j < 0xFFF8; ++j) {
		if (!windows[j]) break;
	}
	if (j == i + 1) {
		return;
	} else {
		for (j = i; j < 0xFFF8; ++j) {
			windows[j] = windows[j+1];
			if (windows[j+1] == NULL) return;
			windows[j]->z = j;
		}
	}
}

void reorder_window (server_window_t * window, uint16_t new_zed) {
	if (!window) {
		return;
	}

	int z = window->z;
	window->z = new_zed;

	if (windows[z] == window) {
		windows[z] = NULL;
	}

	if (new_zed == 0 || new_zed == 0xFFFF) {
		windows[new_zed] = window;
		if (z != new_zed) {
			rebalance_windows();
		}
		return;
	}

	if (windows[new_zed] != window) {
		reorder_window(windows[new_zed], new_zed + 1);
		windows[new_zed ] = window;
	}
	if (z != new_zed) {
		rebalance_windows();
	}
}


void make_top(server_window_t * window) {
	uint16_t index = window->z;
	if (index == 0)  return;
	if (index == 0xFFFF) return;
	uint16_t highest = 0;

	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;
		foreach(node, pw->windows) {
			server_window_t * win = (server_window_t *)node->value;
			if (win == window) continue;
			if (win->z == 0)   continue;
			if (win->z == 0xFFFF)  continue;
			if (highest < win->z) highest = win->z;
			if (win == window) continue;
			if (win->z > window->z) continue;
		}
	}

	reorder_window(window, highest+1);
}

server_window_t * focused_window() {
	if (!focused) {
		return windows[0];
	} else {
		return focused;
	}
}

void set_focused_window(server_window_t * n_focused) {
	if (n_focused == focused) {
		return;
	} else {
		if (focused) {
			w_window_t wwt;
			wwt.wid  = focused->wid;
			wwt.left = 0;
			send_window_event(focused->owner, WE_FOCUSCHG, &wwt);
		}
		focused = n_focused;
		if (focused) {
			w_window_t wwt;
			wwt.wid  = focused->wid;
			wwt.left = 1;
			send_window_event(focused->owner, WE_FOCUSCHG, &wwt);
			make_top(focused);
		} else {
			focused = windows[0];
		}
	}

}

void set_focused_at(int x, int y) {
	server_window_t * n_focused = top_at(x, y);
	set_focused_window(n_focused);
}

server_window_t * init_window (process_windows_t * pw, wid_t wid, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index) {

	server_window_t * window = malloc(sizeof(server_window_t));
	if (!window) {
		fprintf(stderr, "[%d] [window] Could not malloc a server_window_t!", getpid());
		return NULL;
	}

	window->owner = pw;
	window->wid = wid;
	window->bufid = 0;

	window->width  = width;
	window->height = height;
	window->x = x;
	window->y = y;
	window->z = index;

	window->rotation = 0;

	char key[1024];
	SHMKEY(key, 1024, window);

	size_t size = (width * height * WIN_B);
	window->buffer = (uint8_t *)syscall_shm_obtain(key, &size);

	if (!window->buffer) {
		fprintf(stderr, "[%d] [window] Could not create a buffer for a new window for pid %d!", getpid(), pw->pid);
		free(window);
		return NULL;
	}

	list_insert(pw->windows, window);

	return window;
}

void free_window (server_window_t * window) {
	/* Free the window buffer */
	if (!window) return;
	char key[256];
	SHMKEY(key, 256, window);
	syscall_shm_release(key);

	/* Now, kill the object itself */
	process_windows_t * pw = window->owner;

	node_t * n = list_find(pw->windows, window);
	if (n) {
		list_delete(pw->windows, n);
		free(n);
	}
}

void resize_window_buffer (server_window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height) {

	if (!window) {
		return;
	}
	/* If the window has enlarged, we need to create a new buffer */
	if ((width * height) > (window->width * window->height)) {
		/* Release the old buffer */
		char key[256], keyn[256];
		SHMKEY(key, 256, window);

		/* Create the new one */
		window->bufid++;
		SHMKEY(keyn, 256, window);

		size_t size = (width * height * WIN_B);
		char * new_buffer = (uint8_t *)syscall_shm_obtain(keyn, &size);
		memset(new_buffer, 0x44, size);
		window->buffer = new_buffer;
		syscall_shm_release(key);
	}

	if (left != 0 && top != 0) {
		window->x = left;
		window->y = top;
	}
	window->width = width;
	window->height = height;
}


void window_add (server_window_t * window) {
	int z = window->z;
	while (windows[z]) {
		z++;
	}
	window->z = z;

	window->anim_start = tick_count;
	window->anim_mode  = 1;

	memset(window->buffer, 0x00, WIN_B * window->width * window->height);

	windows[z] = window;
}

void unorder_window (server_window_t * window) {
	int z = window->z;
	if (z < WINDOW_LAYERS && windows[z]) {
		windows[z] = 0;
	}
	window->z = 0;
	return;
}

void blit_window_cairo(server_window_t * window, int32_t left, int32_t top) {
	int stride = window->width * 4; //cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, window->width);
	cairo_surface_t * win = cairo_image_surface_create_for_data(window->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);

	if (cairo_surface_status(win) != CAIRO_STATUS_SUCCESS) {
		return;
	}

	assert(win);

	cairo_save(cr);
	cairo_save(cs);

	cairo_translate(cr, left, top);
	cairo_translate(cs, left, top);

	if (window->z != 0xFFFF && window->z != 0) {
		double r = window->rotation * M_PI / 180.0;
		cairo_translate(cr, (int)( window->width / 2), (int)( window->height / 2));
		cairo_rotate(cr, r);
		cairo_translate(cr, (int)(-window->width / 2), (int)(-window->height / 2));

		cairo_translate(cs, (int)( window->width / 2), (int)( window->height / 2));
		cairo_rotate(cs, r);
		cairo_translate(cs, (int)(-window->width / 2), (int)(-window->height / 2));
		cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_FAST);
	}

	if (window->anim_mode) {

		int frame = tick_count - window->anim_start;
		if (frame >= animation_lengths[window->anim_mode]) {
			if (window->anim_mode == 1) {
				window->anim_mode = 0;
				window->anim_start = 0;
				goto paint_window;
			} else if (window->anim_mode == 2) {
				window->anim_mode = 1;
				window->anim_start = tick_count;
			} else if (window->anim_mode == 3) {
				window->anim_mode = 4;
				{
					char buf[512];
					sprintf(buf, "windows_to_clean = %p\n", windows_to_clean);
					syscall_print(buf);
				}
				list_insert(windows_to_clean, window);
			}
		} else {
			if (window->anim_mode == 4) {
				goto done_drawing;
			}
			if (window->anim_mode == 2) {
				frame = 255 - frame;
			}
			if (window->anim_mode == 3) {
				frame = 255 - frame;
			}
			double x = 0.75 + ((double)frame / 256.0) * 0.25;

			int t_x = (window->width * (1.0 - x)) / 2;
			int t_y = (window->height * (1.0 - x)) / 2;

			cairo_translate(cr, t_x, t_y);

			cairo_scale(cr, x, x);
			cairo_set_source_surface(cr, win, 0, 0);
			cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_FAST);
			cairo_paint_with_alpha(cr, (double)frame / 256.0);
		}
	} else {
paint_window:
		cairo_set_source_surface(cr, win, 0, 0);
		cairo_paint(cr);
	}

done_drawing:

	cairo_surface_destroy(win);

	cairo_set_source_rgb(cs, 0, ((window->z & 0xFF00) >> 8) / 255.0, (window->z & 0xFF) / 255.0);
	cairo_rectangle(cs, 0, 0, window->width, window->height);
	cairo_set_antialias(cs, CAIRO_ANTIALIAS_NONE);
	cairo_fill(cs);


	cairo_restore(cr);
	cairo_restore(cs);
}

void redraw_scale_mode() {
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ctx->width);
	surface = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);
	selface = cairo_image_surface_create_for_data(select_ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);
	cr = cairo_create(surface);
	cs = cairo_create(selface);

	/* Draw background window */
	if (windows[0]) {
		blit_window_cairo(windows[0], windows[0]->x, windows[0]->y);
	}

	/* Draw panel window */
	if (windows[WINDOW_LAYERS-1]) {
		blit_window_cairo(windows[WINDOW_LAYERS-1], windows[WINDOW_LAYERS-1]->x, windows[WINDOW_LAYERS-1]->y);
	}

	/* Fade all of that out */
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, ctx->width, ctx->height);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
	cairo_fill(cr);
	cairo_restore(cr);

	int window_count = 0;
	for (uint32_t i = 1; i < WINDOW_LAYERS - 1; ++i) {
		if (windows[i]) {
			window_count++;
		}
	}

	int columns;
	int rows;
	switch (window_count) {
		case 0:
			management_mode = MODE_NORMAL;
			goto _finish;
		case 1:
			columns = 1;
			rows = 1;
			break;
		case 2:
			columns = 2;
			rows = 1;
			break;
		case 3:
			columns = 3;
			rows = 1;
			break;
		case 4:
			columns = 2;
			rows = 2;
			break;
		case 5:
			columns = 3;
			rows = 2;
			break;
		default:
			{
				double sqr = sqrt((double)window_count);
				rows = (int)sqr;
				columns = (window_count) / rows;
				if (rows * columns < window_count) {
					columns += 1;
				}
			}
	}

	double cell_height = (double)ctx->height / (double)rows;
	double cell_width  = (double)ctx->width  / (double)columns;

	int x = 0;
	int y = 0;

	double last_row_width = cell_width;

	if (columns * rows > window_count) {
		int remaining = window_count - (rows - 1) * columns;
		last_row_width = (double)ctx->width / (double)remaining;
	}

	server_window_t * n_focus = NULL;

	for (uint32_t i = 1; i < WINDOW_LAYERS - 1; ++i) {
		if (windows[i]) {
			server_window_t * window = windows[i];
			int w = window->width;
			int h = window->height;

			int stride = window->width * 4;
			cairo_surface_t * win = cairo_image_surface_create_for_data(window->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);

			if (cairo_surface_status(win) != CAIRO_STATUS_SUCCESS) {
				continue;
			}

			if (y == rows - 1) {
				cell_width = last_row_width;
			}

			cairo_save(cr);

			double y_scale = cell_height / (double)h;
			double x_scale = cell_width  / (double)w;

			cairo_translate(cr, cell_width * x, cell_height * y);
			if (x_scale < y_scale) {
				cairo_translate(cr, 0, (cell_height - h * x_scale) / 2);
				cairo_scale(cr, x_scale, x_scale);
			} else {
				cairo_translate(cr, (cell_width - w * y_scale) / 2, 0);
				cairo_scale(cr, y_scale, y_scale);
			}
			cairo_set_source_surface(cr, win, 0, 0);
			if ((x_scale < y_scale && x_scale < 1.0) || (x_scale > y_scale && y_scale < 1.0)) {
				cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_FAST);
			} else {
				cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_GOOD);
			}

			if (mouse_x / MOUSE_SCALE >= x * cell_width &&
				mouse_x / MOUSE_SCALE < x * cell_width + cell_width &&
				mouse_y / MOUSE_SCALE >= y * cell_height &&
				mouse_y / MOUSE_SCALE < y * cell_height + cell_height) {
				cairo_paint(cr);

				if (focus_next_scale) {
					n_focus = window;
					management_mode = MODE_NORMAL;
				}
			} else {
				cairo_paint_with_alpha(cr, 0.7);
			}
			cairo_surface_destroy(win);

			cairo_restore(cr);

			x++;
			if (x == columns) {
				y++;
				x = 0;
			}
		}
	}

	if (focus_next_scale) {
		set_focused_window(n_focus);
		focus_next_scale = 0;
	}

_finish:
	cairo_surface_flush(surface);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);

	cairo_surface_flush(selface);
	cairo_destroy(cs);
	cairo_surface_destroy(selface);
}

void redraw_windows() {
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ctx->width);
	surface = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);

	selface = cairo_image_surface_create_for_data(select_ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);
	cr = cairo_create(surface);
	cs = cairo_create(selface);

	for (uint32_t i = 0; i < WINDOW_LAYERS; ++i) {
		server_window_t * window = NULL;
		if (windows[i]) {
			window = windows[i];
			if (window == moving_window) {
				blit_window_cairo(moving_window, moving_window_l, moving_window_t);
			} else {
				blit_window_cairo(window, window->x, window->y);
			}
		}
	}

	/* Resizing window outline */
	if (resizing_window) {
		cairo_save(cr);

		cairo_set_line_width(cr, 3);
		cairo_set_source_rgba(cr, 0.0, 0.4, 1.0, 0.9);
		cairo_rectangle(cr, resizing_window->x, resizing_window->y, resizing_window_w, resizing_window_h);
		cairo_stroke_preserve(cr);
		cairo_set_source_rgba(cr, 0.33, 0.55, 1.0, 0.5);
		cairo_fill(cr);

		cairo_restore(cr);
	}

	cairo_surface_flush(surface);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	cairo_surface_flush(selface);
	cairo_destroy(cs);
	cairo_surface_destroy(selface);
}

void cairo_rounded_rectangle(cairo_t * cr, double x, double y, double width, double height, double radius) {
	double degrees = M_PI / 180.0;

	cairo_new_sub_path(cr);
	cairo_arc (cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
	cairo_arc (cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
	cairo_arc (cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
	cairo_arc (cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
	cairo_close_path(cr);
}

void draw_window_picker() {
	/* TODO draw window picker */
	int window_count = 0;
	for (uint32_t i = 1; i < WINDOW_LAYERS - 1; ++i) {
		if (windows[i]) {
			window_count++;
		}
	}
	if (window_count < 2) {
		fprintf(stderr, "[compositor] Exiting window picker (<2 regular windows)\n");
		management_mode = MODE_NORMAL;
		return;
	}
	window_picker_index = (window_picker_index + window_count) % window_count;

	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ctx->width);
	surface = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);
	cr = cairo_create(surface);
	/* Don't need a select surface, already have one */

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	/* Draw the background */

	int width = 600;
	int height = 230;

	cairo_rounded_rectangle(cr, (ctx->width - width) / 2, (ctx->height - height) / 2, width, height, 20.0);
	cairo_set_source_rgba(cr, 44.0/255.0, 71.0/255.0, 91.0/255.0, 29.0/255.0);
	cairo_set_line_width(cr, 4);
	cairo_stroke(cr);

	cairo_rounded_rectangle(cr, (ctx->width - width) / 2 + 3, (ctx->height - height) / 2 + 3, width - 6, height - 6, 18.0);
	cairo_set_source_rgba(cr, 158.0/255.0, 169.0/255.0, 177.0/255.0, 0.9);
	cairo_fill(cr);

	/* Now draw the previous, current, and next window. */

}

void internal_free_window(server_window_t * window) {
	if (window == focused_window()) {
		if (window->z == 0xFFFF) {
			focused = NULL;
			return;
		}
		for (int i = window->z; i > 0; --i) {
			if (windows[i - 1]) {
				set_focused_window(windows[i - 1]);
				return;
			}
		}
	}
}

void actually_destroy_window(server_window_t * win) {
	win->x = 0xFFFF;
	internal_free_window(win);
	unorder_window(win);
	/* Wait until we're done drawing */
	spin_lock(&am_drawing);
	spin_unlock(&am_drawing);
	free_window(win);
}

void destroy_window(server_window_t * win) {
	win->anim_mode = 3;
	win->anim_start = tick_count;
}

void process_window_command (int sig) {
	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;

		/* Are there any messages in this process's command pipe? */
		struct stat buf;
		fstat(pw->command_pipe, &buf);

		int max_requests_per_cycle = 1;

		while ((buf.st_size > 0) && (max_requests_per_cycle > 0)) {
			w_window_t wwt;
			wins_packet_t header;
			int bytes_read = read(pw->command_pipe, &header, sizeof(wins_packet_t));

			while (header.magic != WINS_MAGIC) {
				fprintf(stderr, "[compositor] Magic is wrong from pid %d, expected 0x%x but got 0x%x [read %d bytes of %d]\n", pw->pid, WINS_MAGIC, header.magic, bytes_read, sizeof(header));
				max_requests_per_cycle--;
				goto bad_magic;
				memcpy(&header, (void *)((uintptr_t)&header + 1), (sizeof(header) - 1));
				read(pw->event_pipe, (char *)((uintptr_t)&header + sizeof(header) - 1), 1);
			}

			max_requests_per_cycle--;

			switch (header.command_type) {
				case WC_NEWWINDOW:
					{
						read(pw->command_pipe, &wwt, sizeof(w_window_t));
						wwt.wid = _next_wid;
						server_window_t * new_window = init_window(pw, _next_wid, wwt.left, wwt.top, wwt.width, wwt.height, _next_wid);
						window_add(new_window);
						_next_wid++;
						send_window_event(pw, WE_NEWWINDOW, &wwt);
					}
					break;

				case WC_SET_ALPHA:
					{
						read(pw->command_pipe, &wwt, sizeof(w_window_t));
						/* XXX ignored */
					}
					break;

				case WC_RESIZE:
					{
						read(pw->command_pipe, &wwt, sizeof(w_window_t));
						server_window_t * window = get_window_with_process(pw, wwt.wid);
						resize_window_buffer(window, window->x, window->y, wwt.width, wwt.height);
						send_window_event(pw, WE_RESIZED, &wwt);
					}
					break;

				case WC_DESTROY:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					server_window_t * win = get_window_with_process(pw, wwt.wid);

					destroy_window(win);
					send_window_event(pw, WE_DESTROYED, &wwt);
					break;

				case WC_DAMAGE:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					break;

				case WC_REDRAW:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					send_window_event(pw, WE_REDRAWN, &wwt);
					break;

				case WC_REORDER:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					reorder_window(get_window_with_process(pw, wwt.wid), wwt.left);
					break;

				default:
					fprintf(stderr, "[compositor] WARN: Unknown command type %d...\n", header.command_type);
					void * nullbuf = malloc(header.packet_size);
					read(pw->command_pipe, nullbuf, header.packet_size);
					free(nullbuf);
					break;
			}

bad_magic:
			fstat(pw->command_pipe, &buf);
		}
	}
	syscall_yield();
}

/* Request page system */
void reset_request_system () {
	_request_page->lock          = 0;
	_request_page->server_done   = 0;
	_request_page->client_done   = 0;
	_request_page->client_pid    = 0;
	_request_page->event_pipe    = 0;
	_request_page->command_pipe  = 0;

	_request_page->server_pid    = getpid();
	_request_page->server_width  = ctx->width;
	_request_page->server_height = ctx->height;
	_request_page->server_depth  = ctx->depth;

	_request_page->magic         = WINS_MAGIC;
}

void init_request_system () {
	size_t size = sizeof(wins_server_global_t);
	_request_page = (wins_server_global_t *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER, &size);
	if (!_request_page) {
		fprintf(stderr, "[compositor] Could not get a shm block for its request page! Bailing...");
		exit(-1);
	}

	reset_request_system();

}

void process_request () {
	fflush(stdout);
	if (_request_page->client_done) {
		process_windows_t * pw = malloc(sizeof(process_windows_t));
		pw->pid = _request_page->client_pid;
		pw->event_pipe = syscall_mkpipe();
		pw->event_pipe_file = fdopen(pw->event_pipe, "a");
		pw->command_pipe = syscall_mkpipe();
		pw->windows = list_create();

		_request_page->event_pipe = syscall_share_fd(pw->event_pipe, pw->pid);
		_request_page->command_pipe = syscall_share_fd(pw->command_pipe, pw->pid);
		_request_page->client_done = 0;
		_request_page->server_done = 1;

		list_insert(process_list, pw);

		syscall_yield();
	}

	if (!_request_page->lock) {
		reset_request_system();
	}
}

void delete_process (process_windows_t * pw) {
	/* XXX: this is not used anywhere! We need a closing handshake signal. */
	list_destroy(pw->windows);
	list_free(pw->windows);
	free(pw->windows);

	close(pw->command_pipe);
	close(pw->event_pipe);

	node_t * n = list_find(process_list, pw);
	list_delete(process_list, n);
	free(n);
	free(pw);
}

/* Signals */
void * ignore(void * value) {
	return NULL;
}

void init_signal_handlers () {
#if 0
	syscall_signal(SIGWINEVENT, process_window_command); // SIGWINEVENT
#else
	syscall_signal(SIGWINEVENT, ignore); // SIGWINEVENT
#endif
}

/* Sprite stuff */
void init_sprite(int i, char * filename, char * alpha) {
	sprite_t alpha_tmp;
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite(sprites[i], filename);
	if (alpha) {
		sprites[i]->alpha = 1;
		load_sprite(&alpha_tmp, alpha);
		sprites[i]->masks = alpha_tmp.bitmap;
	} else {
		sprites[i]->alpha = 0;
	}
	sprites[i]->blank = 0x0;
}

void init_sprite_png(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[id], path);
}

int center_x(int x) {
	return (ctx->width - x) / 2;
}

int center_y(int y) {
	return (ctx->height - y) / 2;
}

void display() {
	draw_fill(ctx, rgb(0,0,0));
	draw_sprite(ctx, sprites[0], center_x(sprites[0]->width), center_y(sprites[0]->height));
	flip(ctx);
}

char * precacheMemFont(char * ident, char * name) {
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

void load_fonts() {
	int i = 0;
	while (fonts[i].identifier) {
		fprintf(stderr, "[compositor] Loading font %s -> %s\n", fonts[i].path, fonts[i].identifier);
		precacheMemFont(fonts[i].identifier, fonts[i].path);
		++i;
	}
}

/**
 * Keybindings
 */
int handle_key_press(w_keyboard_t * keyboard, server_window_t * window) {

	fprintf(stderr, "[compositor] Key event: %x %x %x\n", keyboard->event.action, keyboard->event.keycode, keyboard->event.modifiers);

	if ((management_mode == MODE_WINDOW_PICKER) &&
	    (keyboard->event.action == KEY_ACTION_UP) &&
	    (!(keyboard->event.modifiers & KEY_MOD_LEFT_ALT))) {
		management_mode = MODE_NORMAL;
		fprintf(stderr, "[compositor] Exiting window picker.\n");
		return 1;
	}

	if (keyboard->event.action != KEY_ACTION_DOWN) return 0;

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == KEY_F4)) {
		/* kill the currently focused window */
		destroy_window(focused_window());
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == 'a')) {
		/* reset the animation and start from scratch */
		server_window_t * win = focused_window();
		win->anim_mode = 1;
		win->anim_start = tick_count;
		return 1;
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == 's')) {
		/* reset the animation and go backwards */
		server_window_t * win = focused_window();
		win->anim_mode = 2;
		win->anim_start = tick_count;
		return 1;
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == 'z')) {
		server_window_t * win = focused_window();
		if (win) {
			win->rotation -= 5;
			if (win->rotation < 0) win->rotation += 360;
			return 1;
		}
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == 'x')) {
		server_window_t * win = focused_window();
		if (win) {
			win->rotation += 5;
			if (win->rotation >= 360) win->rotation -= 360;
			return 1;
		}
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == 'c')) {
		server_window_t * win = focused_window();
		if (win) {
			win->rotation = 0;
			return 1;
		}
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == 'e')) {

		switch (management_mode) {
			case MODE_NORMAL:
				management_mode = MODE_SCALE;
				return 1;
			case MODE_SCALE:
				management_mode = MODE_NORMAL;
				return 1;
			default:
				break;
		}
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_CTRL) &&
	    (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) &&
	    (keyboard->event.keycode == 'p')) {
		if (keyboard->event.modifiers & KEY_MOD_LEFT_ALT) {
			take_screenshot_now = SCREENSHOT_THIS_WINDOW;
		} else {
			take_screenshot_now = SCREENSHOT_WHOLE_SCREEN;
		}
		return 1;
	}

	if ((keyboard->event.modifiers & KEY_MOD_LEFT_ALT) &&
	    (keyboard->event.keycode == '\t')) {
		int direction = (keyboard->event.modifiers & KEY_MOD_LEFT_SHIFT) ? -1 : 1;

		switch (management_mode) {
			case MODE_NORMAL:
				fprintf(stderr, "[compositor] Entering window picker.\n");
				window_picker_index = 0;
			case MODE_WINDOW_PICKER:
				management_mode = MODE_WINDOW_PICKER;
				break;
			default:
				goto _not_valid;
		}
		fprintf(stderr, "[compositor] Setting pick window %d\n", direction);

		window_picker_index += direction;
		return 1;
	}
_not_valid:

	return 0;
}

void device_to_window(server_window_t * window, int32_t x, int32_t y, int32_t * out_x, int32_t * out_y) {
	*out_x = x - window->x;
	*out_y = y - window->y;

	double t_x = *out_x - (window->width / 2);
	double t_y = *out_y - (window->height / 2);

	double s = sin(-(window->rotation * M_PI / 180.0));
	double c = cos(-(window->rotation * M_PI / 180.0));

	double n_x = t_x * c - t_y * s;
	double n_y = t_x * s + t_y * c;

	*out_x = (int32_t)n_x + (window->width / 2);
	*out_y = (int32_t)n_y + (window->height / 2);
}


void * process_requests(void * garbage) {
	int mfd = *((int *)garbage);

	mouse_x = MOUSE_SCALE * ctx->width / 2;
	mouse_y = MOUSE_SCALE * ctx->height / 2;
	click_x = 0;
	click_y = 0;

	uint16_t _mouse_state = 0;
	server_window_t * _mouse_window = NULL;
	int32_t _mouse_init_x;
	int32_t _mouse_init_y;
	int32_t _mouse_win_x;
	int32_t _mouse_win_y;
	int8_t  _mouse_moved = 0;

	int32_t _mouse_win_x_p;
	int32_t _mouse_win_y_p;

	char buf[sizeof(mouse_device_packet_t)];
	while (1) {
		mouse_device_packet_t * packet = (mouse_device_packet_t *)&buf;
		int r = read(mfd, &buf, sizeof(mouse_device_packet_t));
		if (packet->magic != MOUSE_MAGIC) {
			int r = read(mfd, buf, 1);
			break;
		}
		/* Apply mouse movement */
		int l;
		l = 3;
		mouse_x += packet->x_difference * l;
		mouse_y -= packet->y_difference * l;
		if (mouse_x < 0) mouse_x = 0;
		if (mouse_y < 0) mouse_y = 0;
		if (mouse_x >= ctx->width  * MOUSE_SCALE) mouse_x = (ctx->width)   * MOUSE_SCALE;
		if (mouse_y >= ctx->height * MOUSE_SCALE) mouse_y = (ctx->height) * MOUSE_SCALE;

		if (management_mode == MODE_SCALE) {
			if (packet->buttons & MOUSE_BUTTON_LEFT) {
				focus_next_scale = 1;
			}
		} else {

			if (_mouse_state == 0 && (packet->buttons & MOUSE_BUTTON_LEFT) && k_alt) {
				set_focused_at(mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE);
				_mouse_window = focused_window();
				if (_mouse_window) {
					if (_mouse_window->z != 0 && _mouse_window->z != 0xFFFF) {
						_mouse_state = 1;
						_mouse_init_x = mouse_x;
						_mouse_init_y = mouse_y;
						_mouse_win_x  = _mouse_window->x;
						_mouse_win_y  = _mouse_window->y;
						_mouse_win_x_p = _mouse_win_x;
						_mouse_win_y_p = _mouse_win_y;
						moving_window = _mouse_window;
						moving_window_l = _mouse_win_x_p;
						moving_window_t = _mouse_win_y_p;
						make_top(_mouse_window);
					}
				}
			} else if (_mouse_state == 0 && (packet->buttons & MOUSE_BUTTON_MIDDLE) && k_alt) {
				set_focused_at(mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE);
				_mouse_window = focused_window();
				if (_mouse_window) {
					if (_mouse_window->z != 0 && _mouse_window->z != 0xFFFF) {
						_mouse_state = 3;
						_mouse_init_x = mouse_x;
						_mouse_init_y = mouse_y;
						_mouse_win_x  = _mouse_window->x;
						_mouse_win_y  = _mouse_window->y;
						resizing_window   = _mouse_window;
						resizing_window_w = _mouse_window->width;
						resizing_window_h = _mouse_window->height;
						make_top(_mouse_window);
					}
				}
			} else if (_mouse_state == 0 && (packet->buttons & MOUSE_BUTTON_LEFT) && !k_alt) {
				set_focused_at(mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE);
				_mouse_window = focused_window();
				if (_mouse_window) {
					_mouse_state = 2; /* Dragging */
					/* In window coordinates, that's... */
					device_to_window(_mouse_window, mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE, &click_x, &click_y);

					mouse_discard = 1;
					_mouse_moved = 0;
				}
#if 0
				_mouse_window = focused_window();
				if (_mouse_window) {
					if (_mouse_window->z == 0 || _mouse_window->z == 0xFFFF) {

					} else {
						_mouse_state = 2;
						_mouse_init_x = mouse_x;
						_mouse_init_y = mouse_y;
						_mouse_win_x  = _mouse_window->width;
						_mouse_win_y  = _mouse_window->height;
						_mouse_win_x_p= _mouse_win_x;
						_mouse_win_y_p= _mouse_win_y;
						make_top(_mouse_window);
					}
				}
#endif
			} else if (_mouse_state == 0) {

				w_mouse_t _packet;
				if (packet->buttons) {
					set_focused_at(mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE);
				}
				_mouse_window = focused_window();
				_packet.wid = _mouse_window->wid;

				_packet.old_x = click_x;
				_packet.old_y = click_y;

				device_to_window(_mouse_window, mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE, &click_x, &click_y);

				_packet.new_x = click_x;
				_packet.new_y = click_y;

				_packet.buttons = packet->buttons;
				_packet.command = WE_MOUSEMOVE;

				if (_packet.new_x != _packet.old_x || _packet.new_y != _packet.old_y) {
					send_mouse_event(_mouse_window->owner, WE_MOUSEMOVE, &_packet);
				}
			} else if (_mouse_state == 1) {
				if (!(packet->buttons & MOUSE_BUTTON_LEFT)) {
					_mouse_window->x = _mouse_win_x + (mouse_x - _mouse_init_x) / MOUSE_SCALE;
					_mouse_window->y = _mouse_win_y + (mouse_y - _mouse_init_y) / MOUSE_SCALE;
					moving_window = NULL;
					_mouse_state = 0;
				} else {
					_mouse_win_x_p = _mouse_win_x + (mouse_x - _mouse_init_x) / MOUSE_SCALE;
					_mouse_win_y_p = _mouse_win_y + (mouse_y - _mouse_init_y) / MOUSE_SCALE;
					moving_window_l = _mouse_win_x_p;
					moving_window_t = _mouse_win_y_p;
				}
			} else if (_mouse_state == 2) {
				if (!(packet->buttons & MOUSE_BUTTON_LEFT)) {
					/* Released */
					_mouse_state = 0;
					device_to_window(_mouse_window, mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE, &click_x, &click_y);
					if (!_mouse_moved) {
						w_mouse_t _packet;
						_packet.wid = _mouse_window->wid;
						device_to_window(_mouse_window, mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE, &click_x, &click_y);
						_packet.new_x = click_x;
						_packet.new_y = click_y;
						_packet.old_x = -1;
						_packet.old_y = -1;
						_packet.buttons = packet->buttons;
						_packet.command = WE_MOUSECLICK;
						send_mouse_event(_mouse_window->owner, WE_MOUSEMOVE, &_packet);
					}

				} else {
					/* Still down */

					_mouse_moved = 1;
					w_mouse_t _packet;
					_packet.wid = _mouse_window->wid;

					_packet.old_x = click_x;
					_packet.old_y = click_y;

					device_to_window(_mouse_window, mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE, &click_x, &click_y);

					_packet.new_x = click_x;
					_packet.new_y = click_y;

					_packet.buttons = packet->buttons;
					_packet.command = WE_MOUSEMOVE;

					if (_packet.new_x != _packet.old_x || _packet.new_y != _packet.old_y) {
						send_mouse_event(_mouse_window->owner, WE_MOUSEMOVE, &_packet);
					}
				}

			} else if (_mouse_state == 3) {
				int width_diff  = (mouse_x - _mouse_init_x) / MOUSE_SCALE;
				int height_diff = (mouse_y - _mouse_init_y) / MOUSE_SCALE;

				resizing_window_w = resizing_window->width  + width_diff;
				resizing_window_h = resizing_window->height + height_diff;
				if (!(packet->buttons & MOUSE_BUTTON_MIDDLE)) {
					/* Resize */
					w_window_t wwt;
					wwt.wid    = resizing_window->wid;
					wwt.width  = resizing_window_w;
					wwt.height = resizing_window_h;
					resize_window_buffer(resizing_window, resizing_window->x, resizing_window->y, wwt.width, wwt.height);
					send_window_event(resizing_window->owner, WE_RESIZED, &wwt);
					resizing_window = NULL;
					_mouse_state = 0;
				}
			}
		}
	}
	return NULL;
}

void * keyboard_input(void * garbage) {
	char buf[1];
	while (1) {
		/* Read keyboard */
		int r = read(0, buf, 1);
		if (r > 0) {
			w_keyboard_t packet;
			packet.ret = kbd_scancode(buf[0], &packet.event);
			server_window_t * focused = focused_window();
			if (!handle_key_press(&packet, focused)) {
				if (focused) {
					packet.wid = focused->wid;
					packet.command = 0;
					packet.key = packet.ret ? packet.event.key : 0;
					send_keyboard_event(focused->owner, WE_KEYDOWN, packet);
				}
			}
		}
	}
	return NULL;
}

void take_screenshot(int of_what) {
	cairo_surface_t * srf;
	if (of_what == SCREENSHOT_WHOLE_SCREEN) {
		int stride = ctx->width * 4;
		srf = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);
	} else if (of_what == SCREENSHOT_THIS_WINDOW) {
		server_window_t * window = focused_window();
		if (!window) { return; }

		int stride = window->width * 4;
		srf = cairo_image_surface_create_for_data(window->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);
	}
	cairo_surface_write_to_png(srf, "/tmp/screenshot.png");
	cairo_surface_destroy(srf);
}

void * redraw_thread(void * derp) {
	while (1) {
		spin_lock(&am_drawing);
		switch (management_mode) {
			case MODE_SCALE:
				redraw_scale_mode();
				break;
			case MODE_WINDOW_PICKER:
				redraw_windows();
				draw_window_picker();
				break;
			default:
				redraw_windows();
		}
		/* Other stuff */
		redraw_cursor();

		if (take_screenshot_now) {
			take_screenshot(take_screenshot_now);
			take_screenshot_now = 0;
		}

		spin_unlock(&am_drawing);

		tick_count += 10;
		node_t * win;
		while (windows_to_clean->head) {
			win = list_pop(windows_to_clean);
			server_window_t * w = (server_window_t *)win->value;
			actually_destroy_window(w);
		}

		flip(ctx);
		flip(select_ctx);
		syscall_yield();
	}
}

int main(int argc, char ** argv) {

	/* Initialize graphics setup */
	ctx = init_graphics_fullscreen_double_buffer();

	/* Initialize the select buffer */
	select_ctx = malloc(sizeof(gfx_context_t));
	select_ctx->buffer     = malloc(sizeof(uint32_t) * ctx->width * ctx->height);
	select_ctx->backbuffer = malloc(sizeof(uint32_t) * ctx->width * ctx->height);
	select_ctx->width      = ctx->width;
	select_ctx->height     = ctx->height;
	select_ctx->size       = ctx->size;
	select_ctx->depth      = ctx->depth;

	/* Initialize the client request system */
	init_request_system();

	/* Initialize process list */
	init_process_list();

	/* Initialize signal handlers */
	init_signal_handlers();

	/* Load sprites */
	init_sprite_png(0, "/usr/share/logo_login.png");
	display();

	/* Precache shared memory fonts */
	load_fonts();

	/* load the mouse cursor */
	init_sprite(SPRITE_MOUSE, "/usr/share/arrow.bmp","/usr/share/arrow_alpha.bmp");

	windows_to_clean = list_create();

	/* Grab the mouse */
	int mfd = syscall_mousedevice();
	pthread_t mouse_thread;
	pthread_create(&mouse_thread, NULL, process_requests, (void *)&mfd);

	pthread_t keyboard_thread;
	pthread_create(&keyboard_thread, NULL, keyboard_input, NULL);

	pthread_t redraw_everything_thread;
	pthread_create(&redraw_everything_thread, NULL, redraw_thread, NULL);

	setenv("DISPLAY", WINS_SERVER_IDENTIFIER, 1);

	if (!fork()) {
		syscall_system_function(5,0);
		if (argc < 2) {
			char * args[] = {"/bin/glogin", NULL};
			execvp(args[0], args);
		} else {
			execvp(argv[1], &argv[1]);
		}
	}

	/* Sit in a run loop */
	while (1) {
		process_request();
		process_window_command(0);
		syscall_yield();
	}

	// XXX: Better have SIGINT/SIGSTOP handlers
	return 0;
}
