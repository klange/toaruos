/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Panel
 *
 * Provides a graphical panel with a clock, and
 * hopefully more things in the future.
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#define PANEL_HEIGHT 28

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"

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

void init_sprite(int i, char * filename, char * alpha) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite(sprites[i], filename);
	if (alpha) {
		sprites[i]->alpha = 1;
		load_sprite(&alpha_tmp, alpha);
		sprites[i]->masks = alpha_tmp.bitmap;
	} else {
		sprites[i]->alpha = ALPHA_OPAQUE;
	}
}

void init_sprite_png(int i, char * filename) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[i], filename);
}

#define FONT_SIZE 14

volatile int _continue = 1;

void sig_int(int sig) {
	printf("Received shutdown signal in panel!\n");
	_continue = 0;
}

void panel_check_click(w_mouse_t * evt) {
	if (evt->command == WE_MOUSECLICK) {
		printf("Click!\n");
		if (evt->new_x >= win_width - 24 ) {
			printf("Clicked log-out button. Good bye!\n");
			_continue = 0;
		}
	}
}

int main (int argc, char ** argv) {
	setup_windowing();

	int width  = wins_globals->server_width;
	int height = wins_globals->server_height;

	win_width = width;
	win_height = height;

	init_shmemfonts();
	set_font_size(14);

	/* Create the panel */
	window_t * panel = window_create(0, 0, width, PANEL_HEIGHT);
	window_reorder (panel, 0xFFFF);
	ctx = init_graphics_window_double_buffer(panel);
	draw_fill(ctx, rgba(0,0,0,0));
	flip(ctx);

	init_sprite_png(0, "/usr/share/panel.png");
	init_sprite_png(1, "/usr/share/icons/panel-shutdown.png");

	for (uint32_t i = 0; i < width; i += sprites[0]->width) {
		draw_sprite(ctx, sprites[0], i, 0);
	}

	size_t buf_size = panel->width * panel->height * sizeof(uint32_t);
	char * buf = malloc(buf_size);
	memcpy(buf, ctx->backbuffer, buf_size);

	flip(ctx);

	struct timeval now;
	int last = 0;
	struct tm * timeinfo;
	char   buffer[80];

	char _uname[1024];
	syscall_kernel_string_XXX(_uname);

	char * os_version = strstr(_uname, " ");
	os_version++;
	char * tmp = strstr(os_version, " ");
	tmp[0] = 0;

	/* UTF-8 Strings FTW! */
	uint8_t * os_name_ = "とあるOS";
	uint8_t final[512];
	uint32_t l = snprintf(final, 512, "%s %s", os_name_, os_version);

	syscall_signal(2, sig_int);

	/* Enable mouse */
	win_use_threaded_handler();
	mouse_action_callback = panel_check_click;

	while (_continue) {
		/* Redraw the background by memcpy (super speedy) */
		memcpy(ctx->backbuffer, buf, buf_size);
		syscall_gettimeofday(&now, NULL); //time(NULL);
		if (now.tv_sec != last) {
			last = now.tv_sec;
			timeinfo = localtime((time_t *)&now.tv_sec);
			strftime(buffer, 80, "%I:%M:%S %p", timeinfo);

			draw_string(ctx, width - 120, 17, rgb(255,255,255), buffer);
			draw_string(ctx, 10, 17, rgb(255,255,255), final);

			draw_sprite(ctx, sprites[1], win_width - 23, 1); /* Logout button */

			flip(ctx);
		}
		syscall_yield();
	}

	teardown_windowing();

	return 0;
}
