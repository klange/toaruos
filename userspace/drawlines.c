#include <stdlib.h>
#include <assert.h>

#include "lib/window.h"
#include "lib/graphics.h"


int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

int32_t max(int32_t a, int32_t b) {
	return (a > b) ? a : b;
}

int main (int argc, char ** argv) {
	if (argc < 5) {
		printf("usage: %s left top width height\n", argv[0]);
		return -1;
	}

	int left = atoi(argv[1]);
	int top = atoi(argv[2]);
	int width = atoi(argv[3]);
	int height = atoi(argv[4]);

	setup_windowing();

	printf("[drawlines] Windowing ready for client[%d,%d,%d,%d]\n", left, top, width, height);

	/* Do something with a window */
	window_t * wina = window_create(left, top, width, height);
	assert(wina);

	gfx_context_t * ctx = init_graphics_window(wina);
	draw_fill(ctx, rgb(0,0,0));


	printf("[drawlines] Window drawn for client[%d,%d,%d,%d]\n", left, top, width, height);

	int exit = 0;
	while (!exit) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			printf("[drawlines] kbd=0x%x\n", kbd);
			printf("[drawlines] got key '%c'\n", (char)kbd->key);
			if (kbd->key == 'q')
				exit = 1;
			free(kbd);
		}

		draw_line(ctx, rand() % width, rand() % width, rand() % height, rand() % height, rgb(rand() % 255,rand() % 255,rand() % 255));
	}

	//window_destroy(wina); // (will close on exit)
	teardown_windowing();

	return 0;
}
