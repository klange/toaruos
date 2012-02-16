#include <stdlib.h>
#include <assert.h>

#include "lib/window.h"


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
	window_fill(wina, rgb(0,255,0));
	window_redraw_full(wina);

	printf("[drawlines] Window drawn for client[%d,%d,%d,%d]\n", left, top, width, height);

	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			printf("[drawlines] got key '%c'\n", kbd->key);
			free(kbd);
		}

		window_draw_line(wina, rand() % width, rand() % width, rand() % height, rand() % height, rgb(rand() % 255,rand() % 255,rand() % 255));
		window_redraw_full(wina);
	}

	//window_destroy(window); // (will close on exit)
	teardown_windowing();

	return 0;
}
