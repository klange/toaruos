/*
 * glogin
 *
 * Graphical Login screen
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/pthread.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

gfx_context_t * ctx;


uint16_t win_width;
uint16_t win_height;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

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

#define INPUT_SIZE 1024
char input_buffer[1024];
uint32_t input_collected = 0;

int buffer_put(char c) {
	if (c == 8) {
		/* Backspace */
		if (input_collected > 0) {
			input_collected--;
			input_buffer[input_collected] = '\0';
		}
		return 0;
	}
	if (c < 10 || (c > 10 && c < 32) || c > 126) {
		return 0;
	}
	input_buffer[input_collected] = c;
	if (input_buffer[input_collected] == '\n') {
		input_collected++;
		return 1;
	}
	input_collected++;
	if (input_collected == INPUT_SIZE) {
		return 1;
	}
	return 0;
}

void * process_input(void * arg) {
	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			buffer_put(kbd->key);
			free(kbd);
		}
	}
}

int main (int argc, char ** argv) {
	if (argc < 3) {
		printf("usage: %s width height\n", argv[0]);
		return -1;
	}

	int width = atoi(argv[1]);
	int height = atoi(argv[2]);

	win_width = width;
	win_height = height;

	setup_windowing();

	init_shmemfonts();

	/* Do something with a window */
	window_t * wina = window_create(0,0, width, height);
	assert(wina);
	ctx = init_graphics_window_double_buffer(wina);
	draw_fill(ctx, rgb(0,0,0));

#if 1
	printf("Loading background...\n");
	init_sprite(0, "/usr/share/login-background.bmp", NULL);
	printf("Background loaded.\n");
	draw_sprite_scaled(ctx,sprites[0], 0, 0, width, height);
#endif

	init_sprite(1, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
	draw_sprite_scaled(ctx, sprites[1], center_x(sprites[1]->width), center_y(sprites[1]->height), sprites[1]->width, sprites[1]->height);

	flip(ctx);

	size_t buf_size = wina->width * wina->height * sizeof(uint32_t);
	char * buf = malloc(buf_size);
	memcpy(buf, wina->buffer, buf_size);

	uint32_t i = 0;

	pthread_t input_thread;
	pthread_create(&input_thread, NULL, process_input, NULL);

	while (1) {

		double scale = 2.0 + 1.5 * sin((double)i * 0.02);

#if 1
		/* Redraw the background by memcpy (super speedy) */
		memcpy(ctx->backbuffer, buf, buf_size);

		draw_string(ctx, 50 + scale * 30,50 + scale * 30, rgb(255,0,0), input_buffer);


		flip(ctx);
#endif

		++i;
	}

	//window_destroy(window); // (will close on exit)
	teardown_windowing();

	return 0;
}
