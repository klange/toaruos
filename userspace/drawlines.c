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

	printf("[drawlines] Windowing ready.\n");

	/* Do something with a window */
#define WINA_WIDTH 300
#define WINA_HEIGHT 300
	window_t * wina = window_create(0, 0, WINA_WIDTH, WINA_HEIGHT);
	printf("Window created?\n");
	assert(wina);
	window_fill(wina, rgb(0,255,0));
	window_redraw_full(wina);

	printf("Redraw sent\n");

	while (1) {
		window_draw_line(wina, rand() % WINA_WIDTH, rand() % WINA_WIDTH, rand() % WINA_HEIGHT, rand() % WINA_HEIGHT, rgb(rand() % 255,rand() % 255,rand() % 255));
		window_redraw_full(wina);
	}

	//window_destroy(window); // (will close on exit)
	teardown_windowing();

	return 0;
}
