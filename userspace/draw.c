#include <stdlib.h>

#include "lib/window.h"
#include "lib/graphics.h"

int main (int argc, char ** argv) {
	int left = 30;
	int top  = 30;

	int width  = 450;
	int height = 450;

	setup_windowing();

	/* Do something with a window */
	window_t * wina = window_create(left, top, width, height);
	window_fill(wina, rgb(255,255,255));
	init_graphics_window(wina);

	win_use_threaded_handler();

	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			if (kbd->key == 'q') {
				break;
			}
			free(kbd);
		}
		w_mouse_t * mouse = poll_mouse();
		if (mouse != NULL) {
			if (mouse->buttons & MOUSE_BUTTON_LEFT) {
				if (mouse->old_x >= 0 && mouse->new_x >= 0 && mouse->old_y >= 0 && mouse->new_y >= 0 &&
					mouse->old_x < width && mouse->new_x < width && mouse->old_y < width && mouse->new_y < width) {
					draw_line(mouse->old_x, mouse->new_x, mouse->old_y, mouse->new_y, rgb(255,0,0));
				}
			}
			free(mouse);
		}
		syscall_yield();
	}

	teardown_windowing();

	return 0;
}
