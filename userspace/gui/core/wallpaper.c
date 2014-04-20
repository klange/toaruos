/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Wallpaper renderer.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

#define ICON_X         24
#define ICON_TOP_Y     40
#define ICON_SPACING_Y 74
#define ICON_WIDTH     48
#define EXTRA_WIDTH    24

uint16_t win_width;
uint16_t win_height;
yutani_t * yctx;
yutani_window_t * wina;
gfx_context_t * ctx;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

void init_sprite_png(int i, char * filename) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[i], filename);
}

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
int focused_app = -1;

void redraw_apps(void) {
	draw_sprite(ctx, sprites[1], 0, 0);

	/* Load Application Shortcuts */
	uint32_t i = 0;
	while (1) {
		if (!applications[i].icon) {
			break;
		}
		draw_sprite(ctx, sprites[i+2], ICON_X, ICON_TOP_Y + ICON_SPACING_Y * i);

		uint32_t color = rgb(255,255,255);

		if (i == focused_app) {
			color = rgb(142,216,255);
		}

		int str_w = draw_string_width(applications[i].title) / 2;
		int str_x = ICON_X + ICON_WIDTH / 2 - str_w;
		int str_y = ICON_TOP_Y + ICON_SPACING_Y * i + ICON_WIDTH + 14;
		draw_string_shadow(ctx, str_x, str_y, color, applications[i].title, rgb(0,0,0), 2, 1, 1, 3.0);

		++i;
	}

	flip(ctx);
}

void set_focused(int i) {
	if (focused_app != i) {
		int old_focused = focused_app;
		focused_app = i;
		redraw_apps();
		if (old_focused >= 0) {
			yutani_flip_region(yctx, wina, 0, ICON_TOP_Y + ICON_SPACING_Y * old_focused, ICON_WIDTH + 2 * EXTRA_WIDTH, ICON_SPACING_Y);
		}
		if (focused_app >= 0) {
			yutani_flip_region(yctx, wina, 0, ICON_TOP_Y + ICON_SPACING_Y * focused_app, ICON_WIDTH + 2 * EXTRA_WIDTH, ICON_SPACING_Y);
		}
	}
}

void wallpaper_check_click(struct yutani_msg_window_mouse_event * evt) {
	if (evt->command == YUTANI_MOUSE_EVENT_CLICK) {
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
					launch_application(applications[i].appname);
				}
				++i;
			}
			/* Within the icon range */
		}
	} else if (evt->command == YUTANI_MOUSE_EVENT_MOVE) {
		if (evt->new_x > 0 && evt->new_x < ICON_X + ICON_WIDTH + EXTRA_WIDTH) {
			uint32_t i = 0;
			while (1) {
				if (!applications[i].icon) {
					set_focused(-1);
					break;
				}
				if ((evt->new_y > ICON_TOP_Y + ICON_SPACING_Y * i) &&
					(evt->new_y < ICON_TOP_Y + ICON_SPACING_Y + ICON_SPACING_Y * i)) {
					set_focused(i);
					break;
				}
				++i;
			}
			/* Within the icon range */
		} else {
			set_focused(-1);
		}
	}
}

int main (int argc, char ** argv) {
	yctx = yutani_init();

	int width  = yctx->display_width;
	int height = yctx->display_height;

	char f_name[512];
	sprintf(f_name, "%s/.wallpaper.png", getenv("HOME"));
	FILE * f = fopen(f_name, "r");
	if (f) {
		fclose(f);
		init_sprite_png(0, f_name);
	} else {
		init_sprite_png(0, "/usr/share/wallpaper.png");
	}

	uint32_t i = 0;
	while (1) {
		if (!applications[i].icon) {
			break;
		}
		printf("Loading png %s\n", applications[i].icon);
		init_sprite_png(i+2, applications[i].icon);
		++i;
	}

	float x = (float)width  / (float)sprites[0]->width;
	float y = (float)height / (float)sprites[0]->height;

	int nh = (int)(x * (float)sprites[0]->height);
	int nw = (int)(y * (float)sprites[0]->width);;

	sprites[1] = create_sprite(width, height, ALPHA_OPAQUE);
	gfx_context_t * tmp = init_graphics_sprite(sprites[1]);

	if (nw > width) {
		draw_sprite_scaled(tmp, sprites[0], (width - nw) / 2, 0, nw, height);
	} else {
		draw_sprite_scaled(tmp, sprites[0], 0, (height - nh) / 2, width, nh);
	}

	free(tmp);

	win_width = width;
	win_height = height;

	wina = yutani_window_create(yctx, width, height);
	assert(wina);
	yutani_set_stack(yctx, wina, YUTANI_ZORDER_BOTTOM);
	ctx = init_graphics_yutani_double_buffer(wina);
	init_shmemfonts();

	redraw_apps();
	yutani_flip(yctx, wina);

	while (_continue) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					wallpaper_check_click((struct yutani_msg_window_mouse_event *)m->data);
					break;
				case YUTANI_MSG_SESSION_END:
					_continue = 0;
					break;
			}
			free(m);
		}
	}

	yutani_close(yctx, wina);

	return 0;
}
