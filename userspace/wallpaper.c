/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Wallpaper renderer.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "lib/window.h"
#include "lib/graphics.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

uint16_t win_width;
uint16_t win_height;
gfx_context_t * ctx;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

volatile int _continue = 1;

void sig_int(int sig) {
	printf("Received shutdown signal in wallpaper!\n");
	_continue = 0;
}

int main (int argc, char ** argv) {
	setup_windowing();

	int width  = wins_globals->server_width;
	int height = wins_globals->server_height;

	win_width = width;
	win_height = height;

	/* Do something with a window */
	window_t * wina = window_create(0,0, width, height);
	assert(wina);
	window_reorder (wina, 0);
	ctx = init_graphics_window_double_buffer(wina);
	draw_fill(ctx, rgb(127,127,127));
	flip(ctx);

	syscall_signal(2, sig_int);

	sprites[0] = malloc(sizeof(sprite_t));
	if (load_sprite_png(sprites[0], "/usr/share/wallpaper.png")) {
		return 0;
	}
	draw_sprite_scaled(ctx, sprites[0], 0, 0, width, height);

	flip(ctx);

	while (_continue) {
		syscall_yield();
	}

	teardown_windowing();

	return 0;
}
