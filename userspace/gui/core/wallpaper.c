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
#include "lib/shmemfonts.h"

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

static int event_pipe;

typedef struct {
	char * icon;
	char * appname;
	char * title;
} application_t;

application_t applications[] = {
	{"/usr/share/icons/utilities-terminal.png",      "terminal", "Terminal"},
	{"/usr/share/icons/applications-painting.png",   "draw",     "Draw!"},
	{"/usr/share/icons/applications-simulation.png", "game",     "RPG Demo"},
	{"/usr/share/icons/julia.png",                   "julia",    "Julia Fractals"},
	{NULL, NULL, NULL}
};

volatile int _continue = 1;

void sig_int(int sig) {
	printf("Received shutdown signal in wallpaper!\n");
	_continue = 0;
	char buf = '1';
	write(event_pipe, &buf, 1);
}

void launch_application(char * app) {
	if (!fork()) {
		char name[512];
		sprintf(name, "/bin/%s", app);
		printf("Starting %s\n", name);
		char * args[] = {name, NULL};
		execvp(args[0], args);
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
					char buf = '1';
					write(event_pipe, &buf, 1);
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

	event_pipe = syscall_mkpipe();

	/* Do something with a window */
	window_t * wina = window_create(0,0, width, height);
	assert(wina);
	window_reorder (wina, 0);
	ctx = init_graphics_window_double_buffer(wina);
	draw_fill(ctx, rgb(127,127,127));
	flip(ctx);

	syscall_signal(2, sig_int);

	char f_name[512];
	sprintf(f_name, "%s/.wallpaper.png", getenv("HOME"));
	FILE * f = fopen(f_name, "r");
	if (f) {
		fclose(f);
		init_sprite_png(0, f_name);
	} else {
		init_sprite_png(0, "/usr/share/wallpaper.png");
	}

	float x = (float)width  / (float)sprites[0]->width;
	float y = (float)height / (float)sprites[0]->height;

	int nh = (int)(x * (float)sprites[0]->height);
	int nw = (int)(y * (float)sprites[0]->width);;

	if (nw > width) {
		draw_sprite_scaled(ctx, sprites[0], (width - nw) / 2, 0, nw, height);
	} else {
		draw_sprite_scaled(ctx, sprites[0], 0, (height - nh) / 2, width, nh);
	}
	flip(ctx);

	init_shmemfonts();

	/* Load Application Shortcuts */
	uint32_t i = 0;
	while (1) {
		if (!applications[i].icon) {
			break;
		}
		printf("Loading png %s\n", applications[i].icon);
		init_sprite_png(i+1, applications[i].icon);
		draw_sprite(ctx, sprites[i+1], ICON_X, ICON_TOP_Y + ICON_SPACING_Y * i);

		int str_w = draw_string_width(applications[i].title) / 2;
		int str_x = ICON_X + ICON_WIDTH / 2 - str_w;
		int str_y = ICON_TOP_Y + ICON_SPACING_Y * i + ICON_WIDTH + 14;
		draw_string_shadow(ctx, str_x, str_y, rgb(255,255,255), applications[i].title, rgb(0,0,0), 2, 1, 1, 3.0);

		++i;
	}

	flip(ctx);

	/* Enable mouse */
	win_use_threaded_handler();
	mouse_action_callback = wallpaper_check_click;

	while (_continue) {
		char buf;
		read(event_pipe, &buf, 1);
		if (next_run_activate) {
			launch_application(next_run_activate);
			next_run_activate = NULL;
		}
	}

	teardown_windowing();

	return 0;
}
