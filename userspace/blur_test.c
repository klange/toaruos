#include <stdio.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"

gfx_context_t * ctx;
window_t * window;

#define INPUT_SIZE 512
char input_buffer[INPUT_SIZE] = {0};
int  input_collected = 0;

void display() {
	gfx_context_t * tmp_c, * out_c;
	sprite_t * tmp_s, * out_s;

	tmp_s = create_sprite(window->width, window->height, ALPHA_EMBEDDED);
	tmp_c = init_graphics_sprite(tmp_s);

	out_s = create_sprite(window->width, window->height, ALPHA_EMBEDDED);
	out_c = init_graphics_sprite(out_s);

	draw_fill(tmp_c, rgba(0,0,0,0));
	draw_string(tmp_c, 20, 20, rgb(0,0,0), input_buffer);

	blur_context(out_c, tmp_c, 3);

	draw_string(out_c, 19, 19, rgb(255,255,255), input_buffer);

	draw_fill(ctx, rgba(0,0,0,0));
	draw_sprite(ctx, out_s, 0, 0);
	draw_sprite(ctx, out_s, 0, 0);
	draw_sprite(ctx, out_s, 0, 0);
	draw_sprite(ctx, out_s, 0, 0);

	sprite_free(tmp_s);
	free(tmp_c);

	sprite_free(out_s);
	free(out_c);

	flip(ctx);
}

void resize_callback(window_t * win) {
	reinit_graphics_window(ctx, window);

	display();
}

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
	input_collected++;
	input_buffer[input_collected] = '\0';
	if (input_collected == INPUT_SIZE - 1) {
		return 1;
	}
	return 0;
}

int main(int argc, char * argv[]) {
	setup_windowing();

	resize_window_callback = resize_callback;
	window = window_create(40, 40, 200, 30);
	ctx = init_graphics_window_double_buffer(window);

	window_enable_alpha(window);

	init_shmemfonts();

	buffer_put('$');
	display();

	int playing = 1;
	while (playing) {

		char ch = 0;
		w_keyboard_t * kbd;
		do {
			kbd = poll_keyboard();
			if (kbd != NULL) {
				if ((kbd->event.modifiers & KEY_MOD_LEFT_ALT) && (kbd->event.keycode == KEY_F4)) {
					playing = 0;
					break;
				}
				if (kbd->key) {
					buffer_put(kbd->key);
					display();
				}
				free(kbd);
			}
		} while (kbd != NULL);
	}

	teardown_windowing();

	return 0;
}
