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
	}
}

void init_sprite_png(int i, char * filename) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite_png(sprites[i], filename);
}

int main (int argc, char ** argv) {
	yutani_t * yctx = yutani_init();

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

	/* Do something with a window */
	yutani_window_t * wina = yutani_window_create(yctx, width, height);
	assert(wina);
	// window_reorder (wina, 0);
	yutani_set_stack(yctx, wina, YUTANI_ZORDER_BOTTOM);
	ctx = init_graphics_yutani_double_buffer(wina);
	draw_sprite(ctx, sprites[1], 0, 0);
	flip(ctx);

	syscall_signal(2, sig_int);
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
