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
#include <sys/stat.h>

#include "lib/graphics.h"
#include "lib/pthread.h"
#include "lib/kbd.h"
#include "lib/pex.h"
#include "lib/yutani.h"
#include "lib/list.h"

#include "../kernel/include/mouse.h"

/**
 * Parse arguments
 */
int parse_args(int argc, char * argv[]) {

}

static list_t * windows = NULL;

static int next_buf_id(void) {
	static int _next = 0;
	return _next++;
}

static yutani_window_t * server_window_create(int width, int height) {
	yutani_window_t * win = malloc(sizeof(yutani_window_t));

	if (!windows) {
		windows = list_create();
	}

	list_insert(windows, win);

	win->width = width;
	win->height = height;
	win->bufid = next_buf_id();

	char key[1024];
	YUTANI_SHMKEY(key, 1024, win);

	size_t size = (width * height * 4);
	win->buffer = (uint8_t *)syscall_shm_obtain(key, &size);
	return win;
}

void * demo_client(void * garbage) {
	yutani_t * y = yutani_init();

	if (!y) {
		fprintf(stderr, "[demo-client] Connection to server failed.\n");
		return NULL;
	}

	yutani_window_t * w = yutani_window_create(y, 500, 500);

	gfx_context_t * gfx = init_graphics_yutani(w);
	draw_fill(gfx, rgb(240,100,100));

	fprintf(stderr, "[demo-client] Flipping\n");
	yutani_msg_t * m = yutani_msg_build_flip();
	int result = yutani_msg_send(y, m);

	while (1) {
		char data[MAX_PACKET_SIZE];
		pex_recv(y->sock, data);
	}
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
	char buf[sizeof(mouse_device_packet_t)];

	while (1) {
		mouse_device_packet_t * packet = (mouse_device_packet_t *)&buf;
		int r = read(mfd, &buf, sizeof(mouse_device_packet_t));

		fprintf(stderr, "[mouse] mouse packet get! %d\n", r);
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

	while (1) {
		char buf[1];
		int r = read(kfd, buf, 1);
		if (r > 0) {
			key_event_t event;
			kbd_scancode(buf[0], &event);
			fprintf(stderr, "[keyboard] key get!  %d\n", event.keycode);
		}
	}
}

#define FONT_PATH "/usr/share/fonts/"
#define FONT(a,b) {YUTANI_SERVER_IDENTIFIER ".fonts." a, FONT_PATH b}

struct font_def {
	char * identifier;
	char * path;
};

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

static void redraw_windows(gfx_context_t * framebuffer) {
	foreach(node, windows) {
		yutani_window_t * win = (void*)node->value;

		sprite_t tmp;
		tmp.width = win->width;
		tmp.height = win->height;
		tmp.bitmap = (uint32_t*)win->buffer;
		tmp.alpha = ALPHA_EMBEDDED;
		tmp.masks = NULL;
		tmp.blank = 0x0;

		draw_sprite(framebuffer, &tmp, 0, 0);
	}

	flip(framebuffer);
}

/**
 * main
 */
int main(int argc, char * argv[]) {

	gfx_context_t * framebuffer = init_graphics_fullscreen_double_buffer();

	draw_fill(framebuffer, rgb(150,150,240));
	flip(framebuffer);

	FILE * server = pex_bind("compositor");

	fprintf(stderr, "[yutani] Loading fonts...\n");
	load_fonts();
	fprintf(stderr, "[yutani] Done.\n");

	pthread_t mouse_thread;
	pthread_create(&mouse_thread, NULL, mouse_input, NULL);

	pthread_t keyboard_thread;
	pthread_create(&keyboard_thread, NULL, keyboard_input, NULL);

#if 0
	pthread_t demo_client_thread;
	pthread_create(&demo_client_thread, NULL, demo_client, NULL);
#endif

#if 1
	if (!fork()) {
		fprintf(stderr, "Starting Login...\n");
		if (argc < 2) {
			char * args[] = {"/bin/glogin-beta", NULL};
			execvp(args[0], args);
		} else {
			execvp(argv[1], &argv[1]);
		}
	}
#endif

	while (1) {
		pex_packet_t * p = calloc(PACKET_SIZE, 1);
		pex_listen(server, p);

		fprintf(stderr, "[yutani-server] Received packet from client [%08x] of size %d\n", p->source, p->size);

		yutani_msg_t * m = (yutani_msg_t *)p->data;

		if (m->magic != YUTANI_MSG__MAGIC) {
			fprintf(stderr, "[yutani-server] Message has bad magic. (Should eject client, but will instead skip this message.) 0x%x\n", m->magic);
			continue;
		}

		fprintf(stderr, "[yutani-server] Message type == 0x%08x\n", m->type);

		switch(m->type) {
			case YUTANI_MSG_HELLO: {
				fprintf(stderr, "[yutani-server] And hello to you, %08x!\n", p->source);
				yutani_msg_t * response = yutani_msg_build_welcome(framebuffer->width, framebuffer->height);
				pex_send(server, p->source, response->size, (char *)response);
				free(response);
			} break;
			case YUTANI_MSG_WINDOW_NEW: {
				struct yutani_msg_window_new * wn = (void *)m->data;
				fprintf(stderr, "[yutani-server] Client %08x requested a new window (%xx%x).\n", p->source, wn->width, wn->height);
				yutani_window_t * w = server_window_create(wn->width, wn->height);
				yutani_msg_t * response = yutani_msg_build_window_init(w->width, w->height, w->bufid);
				pex_send(server, p->source, response->size, (char *)response);
				free(response);
			} break;
			case YUTANI_MSG_FLIP: {
				/* XXX redraw windows */
				fprintf(stderr, "[yutani-server] Redraw requested.\n");
				redraw_windows(framebuffer);
			} break;
			default: {
				fprintf(stderr, "[yutani-server] Unknown type!\n");
			} break;
		}

	}

	return 0;
}
