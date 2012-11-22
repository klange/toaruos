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
#include <assert.h>
#include <sys/stat.h>
#include <cairo.h>

#include "lib/list.h"
#include "lib/graphics.h"
#include "lib/window.h"
#include "lib/pthread.h"
#include "lib/kbd.h"

#include "../kernel/include/signal.h"
#include "../kernel/include/mouse.h"

#define SINGLE_USER_MODE 1
#define FORCE_UID 1000
#define SPRITE_COUNT 2
#define WIN_D 32
#define WIN_B (WIN_D / 8)
#define MOUSE_DISCARD_LEVEL 10
#define MOUSE_SCALE 3
#define MOUSE_OFFSET_X 26
#define MOUSE_OFFSET_Y 26
#define SPRITE_MOUSE 1
#define WINDOW_LAYERS 0x100000
#define FONT_PATH "/usr/share/fonts/"
#define FONT(a,b) {WINS_SERVER_IDENTIFIER ".fonts." a, FONT_PATH b}

struct font_def {
	char * identifier;
	char * path;
};

/* Non-public bits from window.h */
extern window_t * init_window (process_windows_t * pw, wid_t wid, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index);
extern void free_window (window_t * window);
extern void resize_window_buffer (window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height);
extern FILE *fdopen(int fd, const char *mode);

window_t * focused = NULL;
window_t * windows[WINDOW_LAYERS];
sprite_t * sprites[SPRITE_COUNT];
gfx_context_t * ctx;
list_t * process_list;
int32_t mouse_x, mouse_y;
int32_t click_x, click_y;
uint32_t mouse_discard = 0;
volatile int am_drawing  = 0;
window_t * moving_window = NULL;
int32_t    moving_window_l = 0;
int32_t    moving_window_t = 0;
window_t * resizing_window = NULL;
int32_t    resizing_window_w = 0;
int32_t    resizing_window_h = 0;
cairo_t * cr;
cairo_surface_t * surface;
wid_t volatile _next_wid = 1;
wins_server_global_t volatile * _request_page;
int error;

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

static window_t * get_window_with_process (process_windows_t * pw, wid_t wid) {
	foreach (m, pw->windows) {
		window_t * w = (window_t *)m->value;
		if (w->wid == wid) {
			return w;
		}
	}

	return NULL;
}

void init_process_list () {
	process_list = list_create();
	memset(windows, 0x00000000, sizeof(window_t *) * 0x10000);
}

void send_window_event (process_windows_t * pw, uint8_t event, w_window_t * packet) {
	/* Construct the header */
	wins_packet_t header;
	header.magic = WINS_MAGIC;
	header.command_type = event;
	header.packet_size = sizeof(w_window_t);

	/* Send them */
	// XXX: we have a race condition here
	write(pw->event_pipe, &header, sizeof(wins_packet_t));
	write(pw->event_pipe, packet, sizeof(w_window_t));
	syscall_send_signal(pw->pid, SIGWINEVENT); // SIGWINEVENT
	syscall_yield();
}

void send_keyboard_event (process_windows_t * pw, uint8_t event, w_keyboard_t packet) {
	/* Construct the header */
	wins_packet_t header;
	header.magic = WINS_MAGIC;
	header.command_type = event;
	header.packet_size = sizeof(w_keyboard_t);

	/* Send them */
	// XXX: we have a race condition here
	write(pw->event_pipe, &header, sizeof(wins_packet_t));
	write(pw->event_pipe, &packet, sizeof(w_keyboard_t));
	syscall_send_signal(pw->pid, SIGWINEVENT); // SIGWINEVENT
	syscall_yield();
}

void send_mouse_event (process_windows_t * pw, uint8_t event, w_mouse_t * packet) {
	/* Construct the header */
	wins_packet_t header;
	header.magic = WINS_MAGIC;
	header.command_type = event;
	header.packet_size = sizeof(w_mouse_t);

	/* Send them */
	fwrite(&header, 1, sizeof(wins_packet_t), pw->event_pipe_file);
	fwrite(packet,  1, sizeof(w_mouse_t),     pw->event_pipe_file);
	fflush(pw->event_pipe_file);
	//syscall_send_signal(pw->pid, SIGWINEVENT); // SIGWINEVENT
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

window_t * top_at(uint16_t x, uint16_t y) {
	uint32_t index_top = 0;
	window_t * window_top = NULL;
	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;
		foreach(node, pw->windows) {
			window_t * win = (window_t *)node->value;
			if (is_between(win->x, win->x + win->width, x) && is_between(win->y, win->y + win->height, y)) {
				if (window_top == NULL) {
					window_top = win;
					index_top = win->z;
				} else {
					if (win->z < index_top) continue;
					window_top = win;
					index_top  = win->z;
				}
			}
		}
	}
	return window_top;
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

void reorder_window (window_t * window, uint16_t new_zed) {
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


void make_top(window_t * window) {
	uint16_t index = window->z;
	if (index == 0)  return;
	if (index == 0xFFFF) return;
	uint16_t highest = 0;

	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;
		foreach(node, pw->windows) {
			window_t * win = (window_t *)node->value;
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

window_t * focused_window() {
	if (!focused) {
		return windows[0];
	} else {
		return focused;
	}
}

void set_focused_at(int x, int y) {
	window_t * n_focused = top_at(x, y);
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
		w_window_t wwt;
		wwt.wid  = focused->wid;
		wwt.left = 1;
		send_window_event(focused->owner, WE_FOCUSCHG, &wwt);
		make_top(focused);
	}
}

/* Internal drawing functions */

void window_add (window_t * window) {
	int z = window->z;
	while (windows[z]) {
		z++;
	}
	window->z = z;
	windows[z] = window;
}

void unorder_window (window_t * window) {
	int z = window->z;
	if (z < 0x10000 && windows[z]) {
		windows[z] = 0;
	}
	window->z = 0;
	return;
}

void blit_window_cairo(window_t * window, int32_t left, int32_t top) {
	int stride = window->width * 4; //cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, window->width);
	cairo_surface_t * win = cairo_image_surface_create_for_data(window->buffer, CAIRO_FORMAT_ARGB32, window->width, window->height, stride);

	if (cairo_surface_status(win) != CAIRO_STATUS_SUCCESS) {
		return;
	}

	assert(win);

	cairo_save(cr);

	cairo_set_source_surface(cr, win, (double)left, (double)top);
	cairo_paint(cr);
	cairo_surface_destroy(win);

	cairo_restore(cr);
}

void redraw_windows() {
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ctx->width);
	surface = cairo_image_surface_create_for_data(ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride);
	cr = cairo_create(surface);

	for (uint32_t i = 0; i < 0x10000; ++i) {
		window_t * window = NULL;
		if (windows[i]) {
			window = windows[i];
			if (window == moving_window) {
				blit_window_cairo(moving_window, moving_window_l, moving_window_t);
			} else {
				blit_window_cairo(window, window->x, window->y);
			}
		}
	}

	cairo_surface_flush(surface);
	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);
}

void draw_box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
	int32_t _min_x = max(x, 0);
	int32_t _min_y = max(y,  0);
	int32_t _max_x = min(x + w - 1, ctx->width  - 1);
	int32_t _max_y = min(y + h - 1, ctx->height - 1);

	draw_line(ctx, _min_x, _max_x, _min_y, _min_y, color);
	draw_line(ctx, _min_x, _max_x, _max_y, _max_y, color);
	draw_line(ctx, _min_x, _min_x, _min_y, _max_y, color);
	draw_line(ctx, _max_x, _max_x, _min_y, _max_y, color);
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
						window_t * new_window = init_window(pw, _next_wid, wwt.left, wwt.top, wwt.width, wwt.height, _next_wid);
						window_add(new_window);
						_next_wid++;
						send_window_event(pw, WE_NEWWINDOW, &wwt);
					}
					break;

				case WC_SET_ALPHA:
					{
						read(pw->command_pipe, &wwt, sizeof(w_window_t));
						window_t * window = get_window_with_process(pw, wwt.wid);
						window->use_alpha = wwt.left;
					}
					break;

				case WC_RESIZE:
					{
						read(pw->command_pipe, &wwt, sizeof(w_window_t));
						window_t * window = get_window_with_process(pw, wwt.wid);
						resize_window_buffer(window, window->x, window->y, wwt.width, wwt.height);
						send_window_event(pw, WE_RESIZED, &wwt);
					}
					break;

				case WC_DESTROY:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					window_t * win = get_window_with_process(pw, wwt.wid);
					win->x = 0xFFFF;
					unorder_window(win);
					/* Wait until we're done drawing */
					spin_lock(&am_drawing);
					spin_unlock(&am_drawing);
					free_window(win);
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

int center_x(int x) {
	return (ctx->width - x) / 2;
}

int center_y(int y) {
	return (ctx->height - y) / 2;
}

uint32_t gradient_at(uint16_t j) {
	float x = j * 80;
	x = x / ctx->height;
	return rgb(0, 1 * x, 2 * x);
}

void display() {
	for (uint16_t j = 0; j < ctx->height; ++j) {
		draw_line(ctx, 0, ctx->width, j, j, gradient_at(j));
	}
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

void * process_requests(void * garbage) {
	int mfd = *((int *)garbage);

	mouse_x = MOUSE_SCALE * ctx->width / 2;
	mouse_y = MOUSE_SCALE * ctx->height / 2;
	click_x = 0;
	click_y = 0;

	uint16_t _mouse_state = 0;
	window_t * _mouse_window = NULL;
	int32_t _mouse_init_x;
	int32_t _mouse_init_y;
	int32_t _mouse_win_x;
	int32_t _mouse_win_y;
	int8_t  _mouse_moved = 0;

	int32_t _mouse_win_x_p;
	int32_t _mouse_win_y_p;

	struct stat _stat;
	char buf[1024];
	while (1) {
		fstat(mfd, &_stat);
		while (_stat.st_size >= sizeof(mouse_device_packet_t)) {
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
					_mouse_win_x  = _mouse_window->x;
					_mouse_win_y  = _mouse_window->y;

					click_x = mouse_x / MOUSE_SCALE - _mouse_win_x;
					click_y = mouse_y / MOUSE_SCALE - _mouse_win_y;

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
				mouse_discard--;
				if (mouse_discard < 1) {
					mouse_discard = MOUSE_DISCARD_LEVEL;

					w_mouse_t _packet;
					if (packet->buttons) {
						set_focused_at(mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE);
					}
					_mouse_window = focused_window();
					_packet.wid = _mouse_window->wid;

					_mouse_win_x  = _mouse_window->x;
					_mouse_win_y  = _mouse_window->y;

					_packet.old_x = click_x;
					_packet.old_y = click_y;

					click_x = mouse_x / MOUSE_SCALE - _mouse_win_x;
					click_y = mouse_y / MOUSE_SCALE - _mouse_win_y;

					_packet.new_x = click_x;
					_packet.new_y = click_y;

					_packet.buttons = packet->buttons;
					_packet.command = WE_MOUSEMOVE;

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
					_mouse_win_x  = _mouse_window->x;
					_mouse_win_y  = _mouse_window->y;

					click_x = mouse_x / MOUSE_SCALE - _mouse_win_x;
					click_y = mouse_y / MOUSE_SCALE - _mouse_win_y;
					
					if (!_mouse_moved) {
						w_mouse_t _packet;
						_packet.wid = _mouse_window->wid;
						_mouse_win_x  = _mouse_window->x;
						_mouse_win_y  = _mouse_window->y;
						click_x = mouse_x / MOUSE_SCALE - _mouse_win_x;
						click_y = mouse_y / MOUSE_SCALE - _mouse_win_y;
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
					mouse_discard--;
					if (mouse_discard < 1) {
						mouse_discard = MOUSE_DISCARD_LEVEL;

						w_mouse_t _packet;
						_packet.wid = _mouse_window->wid;

						_mouse_win_x  = _mouse_window->x;
						_mouse_win_y  = _mouse_window->y;

						_packet.old_x = click_x;
						_packet.old_y = click_y;

						click_x = mouse_x / MOUSE_SCALE - _mouse_win_x;
						click_y = mouse_y / MOUSE_SCALE - _mouse_win_y;

						_packet.new_x = click_x;
						_packet.new_y = click_y;

						_packet.buttons = packet->buttons;
						_packet.command = WE_MOUSEMOVE;

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
			fstat(mfd, &_stat);
		}
		fstat(0, &_stat);
		if (_stat.st_size) {
			int r = read(0, buf, 1);
			if (r > 0) {
				w_keyboard_t packet;
				packet.ret = kbd_scancode(buf[0], &packet.event);
				window_t * focused = focused_window();
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

void * redraw_thread(void * derp) {
	while (1) {
		spin_lock(&am_drawing);
		redraw_windows();
		/* Other stuff */
		redraw_cursor();
		/* Resizing window outline */
		if (resizing_window) {
			draw_box(resizing_window->x, resizing_window->y, resizing_window_w, resizing_window_h, rgb(0,128,128));
		}

		spin_unlock(&am_drawing);
		flip(ctx);
		syscall_yield();
	}
}

int main(int argc, char ** argv) {

	/* Initialize graphics setup */
	ctx = init_graphics_fullscreen_double_buffer();

	/* Initialize the client request system */
	init_request_system();

	/* Initialize process list */
	init_process_list();

	/* Initialize signal handlers */
	init_signal_handlers();

	/* Load sprites */
	init_sprite(0, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
	display();

	/* Precache shared memory fonts */
	load_fonts();

	/* load the mouse cursor */
	init_sprite(SPRITE_MOUSE, "/usr/share/arrow.bmp","/usr/share/arrow_alpha.bmp");

	/* Grab the mouse */
	int mfd = syscall_mousedevice();
	pthread_t input_thread;
	pthread_create(&input_thread, NULL, process_requests, (void *)&mfd);

	pthread_t redraw_everything_thread;
	pthread_create(&redraw_everything_thread, NULL, redraw_thread, NULL);

	setenv("DISPLAY", WINS_SERVER_IDENTIFIER, 1);

	if (!fork()) {
#if SINGLE_USER_MODE
#ifdef FORCE_UID
		syscall_setuid(FORCE_UID);
#endif
		char * args[] = {"/bin/gsession", NULL};
#else
		char * args[] = {"/bin/glogin", NULL};
#endif
		execvp(args[0], args);
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
