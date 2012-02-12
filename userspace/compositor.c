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
#include "lib/compositing.h"
#include "../kernel/include/signal.h"

//DEFN_SYSCALL0(getpid, 9)
//DEFN_SYSCALL0(mkpipe, 21)
DEFN_SYSCALL2(shm_obtain, 35, char *, int)
DEFN_SYSCALL1(shm_release, 36, char *)
DEFN_SYSCALL2(sys_signal, 38, int, int)
DEFN_SYSCALL2(share_fd, 39, int, int)

DECL_SYSCALL0(mkpipe);

/* For terminal, not for us */
#define FREETYPE 1

sprite_t * sprites[128];

#define WIN_D 32
#define WIN_B (WIN_D / 8)

typedef struct process_windows process_windows_t;

typedef struct {
	uint32_t wid; /* Window identifier */
	process_windows_t * owner; /* Owning process (back ptr) */

	uint16_t width;  /* Buffer width in pixels */
	uint16_t height; /* Buffer height in pixels */

	int32_t  x; /* X coordinate of upper-left corner */
	int32_t  y; /* Y coordinate of upper-left corner */
	uint16_t z; /* Stack order */

	void * buffer; /* Window buffer */
	uint16_t bufid; /* We occasionally replace the buffer; each is uniquely-indexed */
} window_t;

struct process_windows {
	uint32_t pid;

	int event_pipe;  /* Pipe to send events through */
	int command_pipe; /* Pipe on which we receive commands */

	list_t * windows;
};

list_t * process_list;

process_windows_t * get_process_windows (uint32_t pid) {
	foreach(n, process_list) {
		process_windows_t * pw = (process_windows_t *)n->value;
		if (pw->pid == pid) {
			return pw;
		}
	}

	return NULL;
}

window_t * get_window (uint32_t wid) {
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

#define TO_WINDOW_OFFSET(x,y) (((x) - window->x) + ((y) - window->y) * window->width)
#define DIRECT_OFFSET(x,y) ((x) + (y) * window->width)


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


window_t * init_window (process_windows_t * pw, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index) {
	static int _last_wid = 0;

	window_t * window = malloc(sizeof(window_t));
	if (!window) {
		printf("[compositor] SEVERE: Could not malloc a window_t!");
		return NULL;
	}

	window->owner = pw;
	window->wid = _last_wid++;
	window->bufid = 0;

	window->width  = width;
	window->height = height;
	window->x = x;
	window->y = y;
	window->z = index;

	char key[256];
	SHMKEY(key, 256, window);
	window->buffer = (uint8_t *)syscall_shm_obtain(key, (width * height * WIN_B));
	if (!window->buffer) {
		printf("[compositor] SEVERE: Could not create a buffer for a new window for pid %d!", pw->pid);
		free(window);
		return NULL;
	}

	list_insert(pw->windows, window);

	return window;
}

void resize_window (window_t * window, uint16_t width, uint16_t height) {
	window->width = width;
	window->height = height;

	/* Release the old buffer */
	char key[256];
	SHMKEY(key, 256, window);
	syscall_shm_release(key);

	/* Create the new one */
	window->bufid++;
	SHMKEY(key, 256, window);
	window->buffer = (uint8_t *)syscall_shm_obtain(key, (width * height * WIN_B));
}

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
				assert(0 <= x);
				assert(x < GFX_W);
				assert(0 <= y);
				assert(y < GFX_H);

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

void destroy_window (window_t * window) {
	/* Free the window buffer */
	char key[256];
	SHMKEY(key, 256, window);
	syscall_shm_release(key);

	/* Now, kill the object itself */
	process_windows_t * pw = window->owner;

	node_t * n = list_find(pw->windows, window);
	list_delete(pw->windows, n);
	free(n);

#if 0
	/* Does the owner have any windows themselves? */
	if (pw->windows->length == 0) {
		delete_process(pw);
	}
#endif
}

void sig_window_command (int sig) {
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
					/* No packet */
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					init_window(pw, wwt.left, wwt.top, wwt.width, wwt.height, 0); //XXX: an actual index
					break;

				case WC_RESIZE:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					resize_window(get_window(wwt.wid), /*wwt.left, wwt.top,*/ wwt.width, wwt.height);
					break;

				case WC_DESTROY:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					destroy_window(get_window(wwt.wid));
					break;

				case WC_DAMAGE:
					read(pw->command_pipe, &wwt, sizeof(w_window_t));
					redraw_window(get_window(wwt.wid), wwt.left, wwt.top, wwt.width, wwt.height);
					break;

				default:
					printf("[compositor] WARN: Unknown command type %d...\n");
					void * nullbuf = malloc(header.packet_size);
					read(pw->command_pipe, nullbuf, header.packet_size);
					free(nullbuf);
					break;
			}

			fstat(pw->command_pipe, &buf);
		}
	}
}

void window_set_point(window_t * window, uint16_t x, uint16_t y, uint32_t color) {
#if 0
	printf("window_set_point(0x%x, %d, %d) = 0x%x\n", window->buffer, x, y, color);
	if (!window) {
		return;
	}
#endif
	if (x < 0 || y < 0 || x >= window->width || y >= window->height)
		return;

	((uint32_t *)window->buffer)[DIRECT_OFFSET(x,y)] = color;
}

void window_draw_line(window_t * window, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		window_set_point(window, x0, y0, color);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * error;
		if (e2 > -deltay) {
			error -= deltay;
			x0 += sx;
		}
		if (e2 < deltax) {
			error += deltax;
			y0 += sy;
		}
	}
}

void window_draw_sprite(window_t * window, sprite_t * sprite, uint16_t x, uint16_t y) {
	int x_hi = min(sprite->width, (window->width - x));
	int y_hi = min(sprite->height, (window->height - y));

	for (uint16_t _y = 0; _y < y_hi; ++_y) {
		for (uint16_t _x = 0; _x < x_hi; ++_x) {
			if (sprite->alpha) {
				/* Technically, unsupported! */
				window_set_point(window, x + _x, y + _y, SPRITE(sprite, _x, _y));
			} else {
				if (SPRITE(sprite,_x,_y) != sprite->blank) {
					window_set_point(window, x + _x, y + _y, SPRITE(sprite, _x, _y));
				}
			}
		}
	}
}

void window_fill(window_t *window, uint32_t color) {
	for (uint16_t i = 0; i < window->height; ++i) {
		for (uint16_t j = 0; j < window->width; ++j) {
			((uint32_t *)window->buffer)[DIRECT_OFFSET(j,i)] = color;
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


wins_server_global_t * _request_page;

void init_request_system () {
	_request_page = (wins_server_global_t *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER, sizeof(wins_server_global_t));
	if (!_request_page) {
		printf("Compositor could not get a shm block for its request page! Bailing...");
		exit(-1);
	}

	_request_page->lock         = 0;
	_request_page->server_done  = 0;
	_request_page->client_done  = 0;
	_request_page->client_pid   = 0;
	_request_page->event_pipe   = 0;
	_request_page->command_pipe = 0;
	_request_page->magic = WINS_MAGIC;
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
		_request_page->client_pid = 0;
		_request_page->server_done = 1;
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
	syscall_sys_signal(SIGWINEVENT, (uintptr_t)sig_window_command);
	syscall_sys_signal(SIGWINEVENT, (uintptr_t)sig_window_command);
	syscall_sys_signal(SIGWINEVENT, (uintptr_t)sig_window_command);
	syscall_sys_signal(SIGWINEVENT, (uintptr_t)sig_window_command);
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
	window_t * root = init_window(pw, 0, 0, graphics_width, graphics_height, 0);
	window_draw_sprite(root, sprites[1], 0, 0);
	redraw_full_window(root);

	/* Create the panel */
	window_t * panel = init_window(pw, 0, 0, graphics_width, 24, -1);
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
	assert(rootpw);

	if (!rootpw) {
		printf("? No root process window?\n");
		while (1) {

		}
	}

#define WINA_WIDTH  300
#define WINA_HEIGHT 300
	window_t * wina = init_window(rootpw, 10, 10, WINA_WIDTH, WINA_HEIGHT, 1);
	window_fill(wina, rgb(0,255,0));

#define WINB_WIDTH  700
#define WINB_HEIGHT 700
	window_t * winb = init_window(rootpw, 120, 120, WINB_WIDTH, WINB_HEIGHT, 2);
	window_fill(winb, rgb(0,0,255));

	redraw_full_window(wina);
	redraw_full_window(winb);

#if 0
	flip();
#endif

	while (1) {
		window_draw_line(wina, rand() % WINA_WIDTH, rand() % WINA_WIDTH, rand() % WINA_HEIGHT, rand() % WINA_HEIGHT, rgb(rand() % 255,rand() % 255,rand() % 255));
		window_draw_line(winb, rand() % WINB_WIDTH, rand() % WINB_WIDTH, rand() % WINB_HEIGHT, rand() % WINB_HEIGHT, rgb(rand() % 255,rand() % 255,rand() % 255));
		redraw_full_window(wina);
		redraw_full_window(winb);
#if 0
		redraw_window(&root);
		redraw_window(&panel);
		flip();
#endif
	}

	return 0;
}
