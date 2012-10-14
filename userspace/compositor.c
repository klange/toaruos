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

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "lib/list.h"
#include "lib/graphics.h"
#include "lib/window.h"
#include "lib/pthread.h"

#include "../kernel/include/signal.h"
#include "../kernel/include/mouse.h"

#define SINGLE_USER_MODE 0

void spin_lock(int volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		syscall_yield();
	}
}

void spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
}

window_t * windows[0x10000];

volatile uint8_t screenshot_next_frame = 0;

sprite_t * sprites[128];

gfx_context_t * ctx;

#define WIN_D 32
#define WIN_B (WIN_D / 8)
#define MOUSE_DISCARD_LEVEL 6


list_t * process_list;

int32_t mouse_x, mouse_y;
int32_t click_x, click_y;
uint32_t mouse_discard = 0;
#define MOUSE_SCALE 3
#define MOUSE_OFFSET_X 26
#define MOUSE_OFFSET_Y 26

void redraw_region_slow(int32_t x, int32_t y, int32_t width, int32_t height);
void redraw_cursor() {
	//redraw_region_slow(mouse_x / MOUSE_SCALE - 32, mouse_y / MOUSE_SCALE - 32, 64, 64);
	draw_sprite(ctx, sprites[3], mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y);
}

extern window_t * init_window (process_windows_t * pw, wid_t wid, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index);
extern void free_window (window_t * window);
extern void resize_window_buffer (window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height);

uint16_t * depth_map = NULL;
uintptr_t * top_map  = NULL;

process_windows_t * get_process_windows (uint32_t pid) {
	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;
		if (pw->pid == pid) {
			return pw;
		}
	}

	return NULL;
}

static window_t * get_window (wid_t wid) {
	foreach (n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;
		foreach (m, pw->windows) {
			window_t * w = (window_t *)m->value;
			if (w->wid == wid) {
				return w;
			}
		}
	}

	return NULL;
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

uint8_t is_top(window_t *window, uint16_t x, uint16_t y) {
	uint16_t index = window->z;
	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;

		foreach(node, pw->windows) {
			window_t * win = (window_t *)node->value;
			if (win == window)  continue;
			if (win->z < index) continue;
			if (is_between(win->x, win->x + win->width, x) && is_between(win->y, win->y + win->height, y)) {
				return 0;
			}
		}
	}
	return 1;
}

uint8_t inline is_top_fast(window_t * window, uint16_t x, uint16_t y) {
	if (x >= ctx->width || y >= ctx->height) {
		return 0;
	}
	if (window->z == depth_map[x + y * ctx->width]) {
		return 1;
	}
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
		printf("Nothing to reorder.\n");
		return;
	} else {
		printf("Need to reshuffle. One moment.\n");
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
	printf("Window 0x%x is now at z=%d\n", window, new_zed);
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

	printf("Making top will make this window stack at %d.\n", highest+1);
	reorder_window(window, highest+1);
}

window_t * focused_window() {
	return top_at(mouse_x / MOUSE_SCALE, mouse_y / MOUSE_SCALE);
}

volatile int am_drawing  = 0;
window_t * moving_window = NULL;
int32_t    moving_window_l = 0;
int32_t    moving_window_t = 0;

window_t * resizing_window = NULL;
int32_t    resizing_window_w = 0;
int32_t    resizing_window_h = 0;

/* Internal drawing functions */

void redraw_window(window_t *window, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	if (!window) {
		return;
	}

	uint16_t _lo_x = max(window->x + x, 0);
	uint16_t _hi_x = min(window->x + width, ctx->width);
	uint16_t _lo_y = max(window->y + y, 0);
	uint16_t _hi_y = min(window->y + height, ctx->height);

	for (uint16_t y = _lo_y; y < _hi_y; ++y) {
		for (uint16_t x = _lo_x; x < _hi_x; ++x) {
			/* XXX MAKE THIS FASTER */
			if (is_top_fast(window, x, y)) {
				if (TO_WINDOW_OFFSET(x,y) >= window->width * window->height) continue;
				GFX(ctx,x,y) = ((uint32_t *)window->buffer)[TO_WINDOW_OFFSET(x,y)];
			}
		}
	}

	//redraw_region_slow(mouse_x / MOUSE_SCALE - 32, mouse_y / MOUSE_SCALE - 32, 64, 64);
	//redraw_cursor();
}

void window_add (window_t * window) {
	int z = window->z;
	while (windows[z]) {
		z++;
	}
	printf("Assigning depth of %d to window 0x%x\n", z, window);
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

void redraw_full_window (window_t * window) {
	if (!window) {
		return;
	}

	redraw_window(window, (uint16_t)0, (uint16_t)0, window->width, window->height);
}

void redraw_region_slow(int32_t x, int32_t y, int32_t width, int32_t height) {
	uint16_t _lo_x = max(x, 0);
	uint16_t _hi_x = min(x + width, ctx->width);
	uint16_t _lo_y = max(y, 0);
	uint16_t _hi_y = min(y + height, ctx->height);

	for (uint32_t y = _lo_y; y < _hi_y; ++y) {
		for (uint32_t x = _lo_x; x < _hi_x; ++x) {
			window_t * window = top_at(x,y);
			if (window) {
				//GFX(ctx,x,y) = ((uint32_t *)window->buffer)[TO_WINDOW_OFFSET(x,y)];
				depth_map[x + y * ctx->width] = window->z;
				top_map[x + y * ctx->width]   = (uintptr_t)window;
			} else {
				//GFX(ctx,x,y) = (y % 2 ^ x % 2) ? rgb(0,0,0) : rgb(255,255,255);
				depth_map[x + y * ctx->width] = 0;
				top_map[x + y * ctx->width]   = 0;
			}
		}
	}
}

void blit_window(window_t * window, int32_t left, int32_t top) {
#define TO_DERPED_OFFSET(x,y) (((x) - left) + ((y) - top) * window->width)
	uint16_t _lo_x = max(left, 0);
	uint16_t _hi_x = min(left + window->width, ctx->width);
	uint16_t _lo_y = max(top, 0);
	uint16_t _hi_y = min(top + window->height, ctx->height);
	if (window->use_alpha) {
		for (uint16_t y = _lo_y; y < _hi_y; ++y) {
			for (uint16_t x = _lo_x; x < _hi_x; ++x) {
				GFX(ctx,x,y) = alpha_blend_rgba(GFX(ctx,x,y), ((uint32_t *)window->buffer)[TO_DERPED_OFFSET(x,y)]);
			}
		}
	} else {
		uint16_t win_x = _lo_x - left;
		uint16_t width = (_hi_x - _lo_x) * 4;
		uint16_t win_y = _lo_y - top;

		for (uint16_t y = _lo_y; y < _hi_y; ++y) {
			win_y = y - top;
			memcpy(&ctx->backbuffer[4 * (y * ctx->width + _lo_x)], &window->buffer[(win_y * window->width + win_x) * 4], width);
		}
	}

}

void redraw_everything_fast() {
	for (uint32_t i = 0; i < 0x10000; ++i) {
		window_t * window = NULL;
		if (windows[i]) {
			window = windows[i];
			if (window == moving_window) {
				blit_window(moving_window, moving_window_l, moving_window_t);
			} else {
				blit_window(window, window->x, window->y);
			}
		}
	}
}

void redraw_bounding_box(window_t *window, int32_t left, int32_t top, uint32_t derped) {
	return;
	if (!window) {
		return;
	}

	int32_t _min_x = max(left, 0);
	int32_t _min_y = max(top,  0);
	int32_t _max_x = min(left + window->width  - 1, ctx->width  - 1);
	int32_t _max_y = min(top  + window->height - 1, ctx->height - 1);

	if (!derped) {
		redraw_region_slow(_min_x, _min_y, (_max_x - _min_x + 1), 1);
		redraw_region_slow(_min_x, _max_y, (_max_x - _min_x + 1), 1);
		redraw_region_slow(_min_x, _min_y, 1, (_max_y - _min_y + 1));
		redraw_region_slow(_max_x, _min_y, 1, (_max_y - _min_y + 1));
	} else {
		uint32_t color = rgb(255,0,0);
		draw_line(ctx, _min_x, _max_x, _min_y, _min_y, color);
		draw_line(ctx, _min_x, _max_x, _max_y, _max_y, color);
		draw_line(ctx, _min_x, _min_x, _min_y, _max_y, color);
		draw_line(ctx, _max_x, _max_x, _min_y, _max_y, color);
	}
}

void redraw_bounding_box_r(window_t *window, int32_t width, int32_t height, uint32_t derped) {
	return;
	if (!window) {
		return;
	}

	int32_t _min_x = max(window->x, 0);
	int32_t _min_y = max(window->y,  0);
	int32_t _max_x = min(window->x + width  - 1, ctx->width  - 1);
	int32_t _max_y = min(window->y + height - 1, ctx->height - 1);

	if (!derped) {
		redraw_region_slow(_min_x, _min_y, (_max_x - _min_x + 1), 1);
		redraw_region_slow(_min_x, _max_y, (_max_x - _min_x + 1), 1);
		redraw_region_slow(_min_x, _min_y, 1, (_max_y - _min_y + 1));
		redraw_region_slow(_max_x, _min_y, 1, (_max_y - _min_y + 1));
	} else {
		uint32_t color = rgb(0,255,0);
		draw_line(ctx, _min_x, _max_x, _min_y, _min_y, color);
		draw_line(ctx, _min_x, _max_x, _max_y, _max_y, color);
		draw_line(ctx, _min_x, _min_x, _min_y, _max_y, color);
		draw_line(ctx, _max_x, _max_x, _min_y, _max_y, color);
	}
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


wid_t volatile _next_wid = 1;

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

FILE *fdopen(int fd, const char *mode);

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

void process_window_command (int sig) {
	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;

		/* Are there any messages in this process's command pipe? */
		struct stat buf;
		fstat(pw->command_pipe, &buf);

		int max_requests_per_cycle = 1;

		while ((buf.st_size > 0) && (max_requests_per_cycle > 0)) {
#if 0
			if (!(buf.st_size > sizeof(wins_packet_t))) {
				fstat(pw->command_pipe, &buf);
				continue;
			}
#endif
			w_window_t wwt;
			wins_packet_t header;
			int bytes_read = read(pw->command_pipe, &header, sizeof(wins_packet_t));

			while (header.magic != WINS_MAGIC) {
				printf("Magic is wrong from pid %d, expected 0x%x but got 0x%x [read %d bytes of %d]\n", pw->pid, WINS_MAGIC, header.magic, bytes_read, sizeof(header));
				max_requests_per_cycle--;
				goto bad_magic;
				memcpy(&header, (void *)((uintptr_t)&header + 1), (sizeof(header) - 1));
				read(pw->event_pipe, (char *)((uintptr_t)&header + sizeof(header) - 1), 1);
			}

			max_requests_per_cycle--;

			switch (header.command_type) {
				case WC_NEWWINDOW:
					{
						printf("[compositor] New window request\n");
						read(pw->command_pipe, &wwt, sizeof(w_window_t));
						wwt.wid = _next_wid;
						window_t * new_window = init_window(pw, _next_wid, wwt.left, wwt.top, wwt.width, wwt.height, _next_wid); //XXX: an actual index
						window_add(new_window);
						_next_wid++;
						send_window_event(pw, WE_NEWWINDOW, &wwt);
						redraw_region_slow(0,0,ctx->width,ctx->height);
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
						window_t * window = get_window(wwt.wid);
						resize_window_buffer(window, window->x, window->y, wwt.width, wwt.height);

						printf("Sending event.\n");
						send_window_event(pw, WE_RESIZED, &wwt);
					}
					break;

				case WC_DESTROY:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					window_t * win = get_window_with_process(pw, wwt.wid);
					win->x = 0xFFFF;
					unorder_window(win);
					redraw_region_slow(0,0,ctx->width,ctx->height);
					/* Wait until we're done drawing */
					spin_lock(&am_drawing);
					spin_unlock(&am_drawing);
					free_window(win);
					send_window_event(pw, WE_DESTROYED, &wwt);
					break;

				case WC_DAMAGE:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					//redraw_window(get_window_with_process(pw, wwt.wid), wwt.left, wwt.top, wwt.width, wwt.height);
					break;

				case WC_REDRAW:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					//redraw_window(get_window_with_process(pw, wwt.wid), wwt.left, wwt.top, wwt.width, wwt.height);
					send_window_event(pw, WE_REDRAWN, &wwt);
					break;

				case WC_REORDER:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					reorder_window(get_window_with_process(pw, wwt.wid), wwt.left);
					redraw_region_slow(0,0,ctx->width,ctx->height);
					break;

				default:
					printf("[compositor] WARN: Unknown command type %d...\n", header.command_type);
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




void waitabit() {
	int x = time(NULL);
	while (time(NULL) < x + 1) {
		syscall_yield();
	}
}


/* Request page system */


wins_server_global_t volatile * _request_page;

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
		fprintf(stderr, "[wins] Could not get a shm block for its request page! Bailing...");
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
	}

	if (!_request_page->lock) {
		reset_request_system();
	}
}

void delete_process (process_windows_t * pw) {
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


sprite_t alpha_tmp;

void init_sprite(int i, char * filename, char * alpha) {
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

static int progress = 0;
static int progress_width = 0;

#define PROGRESS_WIDTH  120
#define PROGRESS_HEIGHT 6
#define PROGRESS_OFFSET 50

void draw_progress() {
	int x = center_x(PROGRESS_WIDTH);
	int y = center_y(0);
	uint32_t color = rgb(0,120,230);
	uint32_t fill  = rgb(0,70,160);
	draw_line(ctx, x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET, color);
	draw_line(ctx, x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(ctx, x, x, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(ctx, x + PROGRESS_WIDTH, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);

	if (progress_width > 0) {
		int width = ((PROGRESS_WIDTH - 2) * progress) / progress_width;
		for (int8_t i = 0; i < PROGRESS_HEIGHT - 1; ++i) {
			draw_line(ctx, x + 1, x + 1 + width, y + PROGRESS_OFFSET + i + 1, y + PROGRESS_OFFSET + i + 1, fill);
		}
	}

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
	draw_progress();
	flip(ctx);
}


typedef struct {
	void (*func)();
	char * name;
	int  time;
} startup_item;

list_t * startup_items;

void add_startup_item(char * name, void (*func)(), int time) {
	progress_width += time;
	startup_item * item = malloc(sizeof(startup_item));

	item->name = name;
	item->func = func;
	item->time = time;

	list_insert(startup_items, item);
}

static void test() {
	/* Do Nothing */
}

void run_startup_item(startup_item * item) {
	item->func();
	progress += item->time;
}

FT_Library   library;
FT_Face      face;
FT_Face      face_bold;
FT_Face      face_italic;
FT_Face      face_bold_italic;
FT_Face      face_extra;
FT_GlyphSlot slot;
FT_UInt      glyph_index;

char * loadMemFont(char * ident, char * name, size_t * size) {
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
	*size = s;
	return font;
}

void _init_freetype() {
	int error;
	error = FT_Init_FreeType(&library);
}

#define FONT_SIZE 13
#define ACTUALLY_LOAD_FONTS 0

int error;

void _load_dejavu() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.sans-serif", "/usr/share/fonts/DejaVuSans.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face);
	error = FT_Set_Pixel_Sizes(face, FONT_SIZE, FONT_SIZE);
#endif
}

void _load_dejavubold() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.sans-serif.bold", "/usr/share/fonts/DejaVuSans-Bold.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold);
	error = FT_Set_Pixel_Sizes(face_bold, FONT_SIZE, FONT_SIZE);
#endif
}

void _load_dejavuitalic() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.sans-serif.italic", "/usr/share/fonts/DejaVuSans-Oblique.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face_italic);
	error = FT_Set_Pixel_Sizes(face_italic, FONT_SIZE, FONT_SIZE);
#endif
}

void _load_dejavubolditalic() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.sans-serif.bolditalic", "/usr/share/fonts/DejaVuSans-BoldOblique.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold_italic);
	error = FT_Set_Pixel_Sizes(face_bold_italic, FONT_SIZE, FONT_SIZE);
#endif
}

void _load_dejamonovu() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.monospace", "/usr/share/fonts/DejaVuSansMono.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face);
	error = FT_Set_Pixel_Sizes(face, FONT_SIZE, FONT_SIZE);
#endif
}

void _load_dejamonovubold() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.monospace.bold", "/usr/share/fonts/DejaVuSansMono-Bold.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold);
	error = FT_Set_Pixel_Sizes(face_bold, FONT_SIZE, FONT_SIZE);
#endif
}

void _load_dejamonovuitalic() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.monospace.italic", "/usr/share/fonts/DejaVuSansMono-Oblique.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face_italic);
	error = FT_Set_Pixel_Sizes(face_italic, FONT_SIZE, FONT_SIZE);
#endif
}

void _load_dejamonovubolditalic() {
	char * font;
	size_t s;
	font = loadMemFont(WINS_SERVER_IDENTIFIER ".fonts.monospace.bolditalic", "/usr/share/fonts/DejaVuSansMono-BoldOblique.ttf", &s);
#if ACTUALLY_LOAD_FONTS
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold_italic);
	error = FT_Set_Pixel_Sizes(face_bold_italic, FONT_SIZE, FONT_SIZE);
#endif
}

void init_base_windows () {
	process_windows_t * pw = malloc(sizeof(process_windows_t));
	pw->pid = getpid();
	pw->command_pipe = syscall_mkpipe(); /* nothing in here */
	pw->event_pipe = syscall_mkpipe(); /* nothing in here */
	pw->windows = list_create();
	list_insert(process_list, pw);

	init_sprite(3, "/usr/share/arrow.bmp","/usr/share/arrow_alpha.bmp");
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
			//redraw_region_slow(mouse_x / MOUSE_SCALE - 32, mouse_y / MOUSE_SCALE - 32, 64, 64);
			/* Apply mouse movement */
			int l;
			l = 3;
			mouse_x += packet->x_difference * l;
			mouse_y -= packet->y_difference * l;
			if (mouse_x < 0) mouse_x = 0;
			if (mouse_y < 0) mouse_y = 0;
			if (mouse_x >= ctx->width  * MOUSE_SCALE) mouse_x = (ctx->width)   * MOUSE_SCALE;
			if (mouse_y >= ctx->height * MOUSE_SCALE) mouse_y = (ctx->height) * MOUSE_SCALE;
			//draw_sprite(sprites[3], mouse_x / MOUSE_SCALE - MOUSE_OFFSET_X, mouse_y / MOUSE_SCALE - MOUSE_OFFSET_Y);
			if (_mouse_state == 0 && (packet->buttons & MOUSE_BUTTON_RIGHT)) {
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
						redraw_region_slow(0,0,ctx->width,ctx->height);
					}
				}
			} else if (_mouse_state == 0 && (packet->buttons & MOUSE_BUTTON_MIDDLE)) {
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
						redraw_region_slow(0,0,ctx->width, ctx->height);
					}
				}
			} else if (_mouse_state == 0 && (packet->buttons & MOUSE_BUTTON_LEFT)) {
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

					printf("Mouse down at @ %d,%d = %d,%d\n", mouse_x, mouse_y, click_x, click_y);
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
						redraw_region_slow(0,0,ctx->width,ctx->height);
					}
				}
#endif
			} else if (_mouse_state == 1) {
				if (!(packet->buttons & MOUSE_BUTTON_RIGHT)) {
					_mouse_window->x = _mouse_win_x + (mouse_x - _mouse_init_x) / MOUSE_SCALE;
					_mouse_window->y = _mouse_win_y + (mouse_y - _mouse_init_y) / MOUSE_SCALE;
					moving_window = NULL;
					redraw_region_slow(0,0,ctx->width,ctx->height);
					_mouse_state = 0;
				} else {
					redraw_bounding_box(_mouse_window, _mouse_win_x_p, _mouse_win_y_p, 0);
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
						printf("Finished a click!\n");
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

					printf("Mouse up at @ %d,%d = %d,%d\n", mouse_x, mouse_y, click_x, click_y);
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
				window_t * focused = focused_window();
				if (focused) {
					packet.wid = focused->wid;
					packet.command = 0;
					packet.key = (uint16_t)buf[0];
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
		redraw_everything_fast();
		/* Other stuff */
		redraw_cursor();
		/* Resizing window outline */
		if (resizing_window) {
			draw_box(resizing_window->x, resizing_window->y, resizing_window_w, resizing_window_h, rgb(0,128,128));
		}

		spin_unlock(&am_drawing);
		flip(ctx);
		if (screenshot_next_frame) {
			screenshot_next_frame = 0;

			printf("Going for screenshot...\n");

			FILE * screenshot = fopen("/usr/share/screenshot.png", "w");
			context_to_png(screenshot, ctx);
			fclose(screenshot);
		}
		syscall_yield();
	}
}

int main(int argc, char ** argv) {

	/* Initialize graphics setup */
	ctx = init_graphics_fullscreen_double_buffer();

	depth_map = malloc(sizeof(uint16_t)  * ctx->width * ctx->height);
	top_map   = malloc(sizeof(uintptr_t) * ctx->width * ctx->height);

	/* Initialize the client request system */
	init_request_system();

	/* Initialize process list */
	init_process_list();

	/* Initialize signal handlers */
	init_signal_handlers();

	/* Load sprites */
	init_sprite(0, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
	display();

	/* Count startup items */
	startup_items = list_create();
	add_startup_item("Initializing FreeType", _init_freetype, 1);
	add_startup_item("Loading font: Deja Vu Sans", _load_dejavu, 2);
	add_startup_item("Loading font: Deja Vu Sans Bold", _load_dejavubold, 2);
	add_startup_item("Loading font: Deja Vu Sans Oblique", _load_dejavuitalic, 2);
	add_startup_item("Loading font: Deja Vu Sans Bold+Oblique", _load_dejavubolditalic, 2);
	add_startup_item("Loading font: Deja Vu Sans Mono", _load_dejamonovu, 2);
	add_startup_item("Loading font: Deja Vu Sans Mono Bold", _load_dejamonovubold, 2);
	add_startup_item("Loading font: Deja Vu Sans Mono Oblique", _load_dejamonovuitalic, 2);
	add_startup_item("Loading font: Deja Vu Sans Mono Bold+Oblique", _load_dejamonovubolditalic, 2);

	foreach(node, startup_items) {
		run_startup_item((startup_item *)node->value);
		display();
	}

	/* load the mouse cursor */
	init_sprite(3, "/usr/share/arrow.bmp","/usr/share/arrow_alpha.bmp");

	/* Grab the mouse */
	int mfd = syscall_mousedevice();
	pthread_t input_thread;
	pthread_create(&input_thread, NULL, process_requests, (void *)&mfd);

	pthread_t redraw_everything_thread;
	pthread_create(&redraw_everything_thread, NULL, redraw_thread, NULL);

	setenv("DISPLAY", WINS_SERVER_IDENTIFIER, 1);

	if (!fork()) {
#if SINGLE_USER_MODE
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
