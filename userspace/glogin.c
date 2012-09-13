/*
 * glogin
 *
 * Graphical Login screen
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/pthread.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

gfx_context_t * ctx;

int _alright_get_on_with_it = 0;


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
			if (kbd->key == '\n') {
				_alright_get_on_with_it = 1;
				pthread_exit(NULL);
			}
			free(kbd);
		}
	}
}

uint32_t gradient_at(uint16_t j) {
	float x = j * 80;
	x = x / ctx->height;
	return rgb(0, 1 * x, 2 * x);
}

void draw_gradient() {
	for (uint16_t j = 0; j < ctx->height; ++j) {
		draw_line(ctx, 0, ctx->width, j, j, gradient_at(j));
	}
}

int main (int argc, char ** argv) {
	while (1) {
		setup_windowing();

		int width  = wins_globals->server_width;
		int height = wins_globals->server_height;

		win_width = width;
		win_height = height;

		init_shmemfonts();

		/* Do something with a window */
		window_t * wina = window_create(0,0, width, height);
		assert(wina);
		ctx = init_graphics_window_double_buffer(wina);
		draw_gradient();
		flip(ctx);

		/* Fade in */
		size_t buf_size = wina->width * wina->height * sizeof(uint32_t);
		char * buf = malloc(buf_size);
		uint16_t fade = 0;
		gfx_context_t fade_ctx;
		fade_ctx.backbuffer = buf;
		fade_ctx.width      = wina->width;
		fade_ctx.height     = wina->height;
		fade_ctx.depth      = 32;
		gfx_context_t * fc = &fade_ctx;

		sprites[0] = malloc(sizeof(sprite_t));
		load_sprite_png(sprites[0], "/usr/share/wallpaper.png");
		draw_sprite_scaled(fc,sprites[0], 0, 0, width, height);

		while (fade < 256) {
			for (uint32_t y = 0; y < wina->height; y++) {
				for (uint32_t x = 0; x < wina->width; x++) {
					GFX(ctx, x, y) = alpha_blend(GFX(ctx, x, y), GFX(fc, x, y), rgb(fade,0,0));
				}
			}
			flip(ctx);
			fade += 10;
		}

		init_sprite(1, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
		draw_sprite_scaled(fc, sprites[1], center_x(sprites[1]->width), center_y(sprites[1]->height), sprites[1]->width, sprites[1]->height);

		uint32_t i = 0;

		pthread_t input_thread;
		pthread_create(&input_thread, NULL, process_input, NULL);


		uint32_t black = rgb(0,0,0);
		uint32_t white = rgb(255,255,255);

		int x_offset = 65;
		int y_offset = 64;

		int fuzz = 3;

		set_font_size(22);

		char * msg = "Press enter.";

		while (1) {

			if (_alright_get_on_with_it) {
				_alright_get_on_with_it = 0;
				break;
			}

#if 1
			/* Redraw the background by memcpy (super speedy) */
			memcpy(ctx->backbuffer, buf, buf_size);

			set_text_opacity(0.2);
			for (int y = -fuzz; y <= fuzz; ++y) {
				for (int x = -fuzz; x <= fuzz; ++x) {
					draw_string(ctx, wina->width / 2 - x_offset + x, wina->height / 2 + y_offset + y, black, msg);
				}
			}
			set_text_opacity(1.0);
			draw_string(ctx, wina->width / 2 - x_offset, wina->height / 2 + y_offset, white, msg);

			flip(ctx);
#endif

			++i;
		}

		memcpy(ctx->backbuffer, buf, buf_size);
		flip(ctx);

		teardown_windowing();

		int _session_pid = fork();
		if (!_session_pid) {
			syscall_setuid(1000);
			char * args[] = {"/bin/gsession", NULL};
			execve(args[0], args, NULL);
		}

		syscall_wait(_session_pid);

		free(buf);
		free(sprites[0]);
		free(sprites[1]);

	}

	return 0;
}
