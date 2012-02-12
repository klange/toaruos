/*
 * The ToAru Sample Game
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

#if 0
DEFN_SYSCALL2(shm_obtain, 35, char *, int)
DEFN_SYSCALL1(shm_release, 36, char *)
DEFN_SYSCALL2(send_signal, 37, int, int)
DEFN_SYSCALL2(sys_signal, 38, int, int)
DEFN_SYSCALL2(share_fd, 39, int, int)
DEFN_SYSCALL1(get_fd, 40, int)
#endif

DECL_SYSCALL0(mkpipe);


/* For terminal, not for us */
#define FREETYPE 1

sprite_t * sprites[128];

#define WIN_D 32
#define WIN_B (WIN_D / 8)


list_t * process_list;

extern window_t * init_window (process_windows_t * pw, wid_t wid, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index);
extern void free_window (window_t * window);
extern void resize_window_buffer (window_t * window, uint16_t left, uint16_t top, uint16_t width, uint16_t height);

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

void init_process_list () {
	process_list = list_create();
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




/* Internal drawing functions */

void redraw_window(window_t *window, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
	if (!window) {
		return;
	}

	uint16_t _lo_x = max(window->x + x, 0);
	uint16_t _hi_x = min(window->x + width - 1, graphics_width);
	uint16_t _lo_y = max(window->y + y, 0);
	uint16_t _hi_y = min(window->y + height - 1, graphics_height);

	for (uint16_t y = _lo_y; y < _hi_y; ++y) {
		for (uint16_t x = _lo_x; x < _hi_x; ++x) {
			if (is_top(window, x, y)) {
#if 0
				assert(0 <= x);
				assert(x < GFX_W);
				assert(0 <= y);
				assert(y < GFX_H);
#endif

				GFX(x,y) = ((uint32_t *)window->buffer)[TO_WINDOW_OFFSET(x,y)];
			}
		}
	}
}

void redraw_full_window (window_t * window) {
	if (!window) {
		return;
	}

	redraw_window(window, (uint16_t)0, (uint16_t)0, window->width, window->height);
}


wid_t volatile _next_wid = 0;

void send_window_event (process_windows_t * pw, uint8_t event, w_window_t packet) {
	/* Construct the header */
	wins_packet_t header;
	header.command_type = event;
	header.packet_size = sizeof(w_window_t);

	/* Send them */
	// XXX: we have a race condition here
	write(pw->event_pipe, &header, sizeof(wins_packet_t));
	write(pw->event_pipe, &packet, sizeof(w_window_t));
	syscall_send_signal(pw->pid, 35); // SIGWINEVENT
}

void process_window_command (int sig) {
	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;

		/* Are there any messages in this process's command pipe? */
		struct stat buf;
		fstat(pw->command_pipe, &buf);
		while (buf.st_size > 0) {
			w_window_t wwt;
			wins_packet_t header;
			read(pw->command_pipe, &header, sizeof(wins_packet_t));

			switch (header.command_type) {
				case WC_NEWWINDOW:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					init_window(pw, _next_wid++, wwt.left, wwt.top, wwt.width, wwt.height, 0); //XXX: an actual index
					send_window_event(pw, WE_NEWWINDOW, wwt);
					break;

				case WC_RESIZE:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					resize_window_buffer(get_window(wwt.wid), wwt.left, wwt.top, wwt.width, wwt.height);
					send_window_event(pw, WE_RESIZED, wwt);
					break;

				case WC_DESTROY:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					free_window(get_window(wwt.wid));
					break;

				case WC_DAMAGE:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					redraw_window(get_window(wwt.wid), wwt.left, wwt.top, wwt.width, wwt.height);
					break;

				default:
					printf("[compositor] WARN: Unknown command type %d...\n", header.command_type);
					void * nullbuf = malloc(header.packet_size);
					read(pw->command_pipe, nullbuf, header.packet_size);
					free(nullbuf);
					break;
			}

			fstat(pw->command_pipe, &buf);
		}
	}
}




void waitabit() {
	int x = time(NULL);
	while (time(NULL) < x + 1) {
		// Do nothing.
	}
}


/* Request page system */


wins_server_global_t volatile * _request_page;

void init_request_system () {
	_request_page = (wins_server_global_t *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER, sizeof(wins_server_global_t));
	if (!_request_page) {
		fprintf(stderr, "[wins] Could not get a shm block for its request page! Bailing...");
		exit(-1);
	}

	_request_page->lock          = 0;
	_request_page->server_done   = 0;
	_request_page->client_done   = 0;
	_request_page->client_pid    = 0;
	_request_page->event_pipe    = 0;
	_request_page->command_pipe  = 0;

	_request_page->server_pid    = getpid();
	_request_page->server_width  = graphics_width;
	_request_page->server_height = graphics_height;
	_request_page->server_depth  = graphics_depth;

	_request_page->magic         = WINS_MAGIC;
}

void process_request () {
	if (_request_page->client_done) {
		process_windows_t * pw = malloc(sizeof(process_windows_t));
		pw->pid = _request_page->client_pid;
		pw->event_pipe = syscall_mkpipe();
		pw->command_pipe = syscall_mkpipe();
		pw->windows = list_create();

		_request_page->event_pipe = syscall_share_fd(pw->event_pipe, pw->pid);
		_request_page->command_pipe = syscall_share_fd(pw->command_pipe, pw->pid);
		_request_page->server_done = 1;

		list_insert(process_list, pw);
		printf("A client (%d) replied to\n", pw->pid);
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


void init_signal_handlers () {
	syscall_sys_signal(35, (uintptr_t)process_window_command); // SIGWINEVENT
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
	return (graphics_width - x) / 2;
}

int center_y(int y) {
	return (graphics_height - y) / 2;
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
	draw_line(x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET, color);
	draw_line(x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(x, x, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(x + PROGRESS_WIDTH, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);

	if (progress_width > 0) {
		int width = ((PROGRESS_WIDTH - 2) * progress) / progress_width;
		for (int8_t i = 0; i < PROGRESS_HEIGHT - 1; ++i) {
			draw_line(x + 1, x + 1 + width, y + PROGRESS_OFFSET + i + 1, y + PROGRESS_OFFSET + i + 1, fill);
		}
	}

}

uint32_t gradient_at(uint16_t j) {
	float x = j * 80;
	x = x / graphics_height;
	return rgb(0, 1 * x, 2 * x);
}

void display() {
	for (uint16_t j = 0; j < graphics_height; ++j) {
		draw_line(0, graphics_width, j, j, gradient_at(j));
	}
	draw_sprite(sprites[0], center_x(sprites[0]->width), center_y(sprites[0]->height));
	draw_progress();
	flip();
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
#if 0 // No printing!
	printf("[compositor] Running startup item: %s\n", item->name);
#endif
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

char * loadMemFont(char * name, size_t * size) {
	FILE * f = fopen(name, "r");
	size_t s = 0;
	fseek(f, 0, SEEK_END);
	s = ftell(f);
	fseek(f, 0, SEEK_SET);
	char * font = malloc(s);
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

int error;

void _load_dejavu() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face);
	error = FT_Set_Pixel_Sizes(face, FONT_SIZE, FONT_SIZE);
}

void _load_dejavubold() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans-Bold.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold);
	error = FT_Set_Pixel_Sizes(face_bold, FONT_SIZE, FONT_SIZE);
}

void _load_dejavuitalic() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans-Oblique.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face_italic);
	error = FT_Set_Pixel_Sizes(face_italic, FONT_SIZE, FONT_SIZE);
}

void _load_dejavubolditalic() {
	char * font;
	size_t s;
	font = loadMemFont("/usr/share/fonts/DejaVuSans-BoldOblique.ttf", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face_bold_italic);
	error = FT_Set_Pixel_Sizes(face_bold_italic, FONT_SIZE, FONT_SIZE);
}

void _load_wallpaper() {
	init_sprite(1, "/usr/share/wallpaper.bmp", NULL);
}

void init_base_windows () {
	process_windows_t * pw = malloc(sizeof(process_windows_t));
	pw->pid = getpid();
	pw->command_pipe = syscall_mkpipe(); /* nothing in here */
	pw->event_pipe = syscall_mkpipe(); /* nothing in here */
	pw->windows = list_create();
	list_insert(process_list, pw);

	list_insert(process_list, pw);

#if 1
	/* Create the background window */
	window_t * root = init_window(pw, _next_wid++, 0, 0, graphics_width, graphics_height, 0);
	window_draw_sprite(root, sprites[1], 0, 0);
	redraw_full_window(root);

	/* Create the panel */
	window_t * panel = init_window(pw, _next_wid++, 0, 0, graphics_width, 24, -1);
	window_fill(panel, rgb(0,120,230));
	init_sprite(2, "/usr/share/panel.bmp", NULL);
	for (uint32_t i = 0; i < graphics_width; i += sprites[2]->width) {
		window_draw_sprite(panel, sprites[2], i, 0);
	}
	redraw_full_window(panel);
#endif
}

int main(int argc, char ** argv) {

	/* Initialize graphics setup */
	init_graphics_double_buffer();

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
#if 0
	add_startup_item("Loading font: Deja Vu Sans", _load_dejavu, 2);
	add_startup_item("Loading font: Deja Vu Sans Bold", _load_dejavubold, 2);
	add_startup_item("Loading font: Deja Vu Sans Oblique", _load_dejavuitalic, 2);
	add_startup_item("Loading font: Deja Vu Sans Bold+Oblique", _load_dejavubolditalic, 2);
#endif
	add_startup_item("Loading wallpaper (/usr/share/wallpaper.bmp)", _load_wallpaper, 4);

	foreach(node, startup_items) {
		run_startup_item((startup_item *)node->value);
		display();
	}

#if 1
	/* Reinitialize for single buffering */
	init_graphics();
#endif

	/* Create the root and panel */
	init_base_windows();

	process_windows_t * rootpw = get_process_windows(getpid());
	if (!rootpw) {
		printf("[compositor] SEVERE: No root process windows!\n");
		return 1;
	}

	if (!fork()) {
		char * args[] = {"/bin/drawlines", NULL};
		execve(args[0], args, NULL);
	}
	printf("waiting for clients...\n");
	/* Sit in a run loop */
	while (1) {
		process_request();
		waitabit(); // XXX: Can't we deschedule?
	}

	// XXX: Better have SIGINT/SIGSTOP handlers
	return 0;
}
