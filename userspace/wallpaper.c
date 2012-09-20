/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Wallpaper renderer.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>

#include "lib/window.h"
#include "lib/graphics.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

#define ICON_X         24
#define ICON_TOP_Y     40
#define ICON_SPACING_Y 74
#define ICON_WIDTH     48

uint16_t win_width;
uint16_t win_height;
gfx_context_t * ctx;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

typedef struct {
	char * icon;
	char * appname;
	char * title;
} application_t;

application_t applications[] = {
	{"/usr/share/icons/utilities-terminal.png",      "terminal", "Terminal"},
	{"/usr/share/icons/applications-painting.png",   "draw", "Draw"},
	{"/usr/share/icons/applications-simulation.png", "game-win", "RPG Demo"},
	{NULL, NULL, NULL}
};

volatile int _continue = 1;

void sig_int(int sig) {
	printf("Received shutdown signal in wallpaper!\n");
	_continue = 0;
}

void launch_application(char * app) {
	if (!fork()) {
		char name[512];
		sprintf(name, "/bin/%s", app);
		printf("Starting %s\n", name);
		char * args[] = {name, NULL};
		execve(args[0], args, NULL);
		exit(1);
	}
}

char * next_run_activate = NULL;

void wallpaper_check_click(w_mouse_t * evt) {
	if (evt->command == WE_MOUSECLICK) {
		printf("Click!\n");
		if (evt->new_x > ICON_X && evt->new_x < ICON_X + ICON_WIDTH) {
			uint32_t i = 0;
			while (1) {
				if (!applications[i].icon) {
					break;
				}
				if ((evt->new_y > ICON_TOP_Y + ICON_SPACING_Y * i) &&
					(evt->new_y < ICON_TOP_Y + ICON_SPACING_Y + ICON_SPACING_Y * i)) {
					printf("Launching application \"%s\"...\n", applications[i].title);
					next_run_activate = applications[i].appname;
				}
				++i;
			}
			/* Within the icon range */
		}
	}
}

void init_sprite_png(int i, char * filename) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[i], filename);
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

	init_sprite_png(0, "/usr/share/wallpaper.png");
	draw_sprite_scaled(ctx, sprites[0], 0, 0, width, height);
	flip(ctx);

	/* Load Application Shortcuts */
	uint32_t i = 0;
	while (1) {
		if (!applications[i].icon) {
			break;
		}
		printf("Loading png %s\n", applications[i].icon);
		init_sprite_png(i+1, applications[i].icon);
		draw_sprite(ctx, sprites[i+1], ICON_X, ICON_TOP_Y + ICON_SPACING_Y * i);
		++i;
	}

	flip(ctx);

	/* Enable mouse */
	win_use_threaded_handler();
	mouse_action_callback = wallpaper_check_click;

	while (_continue) {
		if (next_run_activate) {
			launch_application(next_run_activate);
			next_run_activate = NULL;
		}
		syscall_yield();
	}

	teardown_windowing();

	return 0;
}
