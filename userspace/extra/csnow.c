/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define MAX_SPEED        2
#define INITIAL_SNOW     40
#define INCREMENTAL_SNOW 10
#define BUFFER_SIZE      10
#define BLANK_SPACE      " "

#define NUM_FLAKE_TEXTURES 4
char * flake_textures[] = {
	"❄",
	"❅",
	"❆",
	"*",
};

typedef struct {
	int width;
	int height;
	char ** backingstore;
} screen_t;

typedef struct {
	char * display;
	signed short x;
	signed short y;
	signed char  speed;
	char   gravity;
} flake_t;

screen_t * init_screen() {
	char * term = getenv("TERM");

	screen_t * screen = malloc(sizeof(screen_t));

	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);
	screen->width  = w.ws_col;
	screen->height = w.ws_row;

	screen->backingstore = malloc(sizeof(char *) * screen->width * screen->height);

	return screen;
}

flake_t * make_some_flakes(screen_t * screen, int how_many) {
	flake_t * out = malloc(sizeof(flake_t) * (how_many + 1));

	for (int i = 0; i < how_many; ++i) {
		out[i].display = flake_textures[rand() % NUM_FLAKE_TEXTURES];
		out[i].x       = rand() % screen->width;
		out[i].y       = -(rand() % BUFFER_SIZE);
		out[i].speed   = rand() % MAX_SPEED - (MAX_SPEED / 2);
		out[i].gravity = 1;
	}

	out[how_many].display = NULL;
	return out;
}

flake_t * add_flakes(screen_t * screen, flake_t * flakes, int how_many) {
	flake_t * more = make_some_flakes(screen, how_many);

	int len = 0; for (; flakes[len].display; ++len);
	flakes = realloc(flakes, sizeof(flake_t) * (how_many + len + 1));

	memcpy(&flakes[len], more, sizeof(flake_t) * (how_many + 1));

	free(more);

	return flakes;
}

int detect_collisions(screen_t * screen, flake_t * flakes, int i) {
	int x = flakes[i].x;
	int y = flakes[i].y;

	if (flakes[i].y >= screen->height - 1) {
		flakes[i].gravity = 0;
		return 1;
	}

	for (int j = 0; flakes[j].display; ++j) {
		if (flakes[j].gravity) continue;
		if (flakes[j].x == x && flakes[j].y == (y + 1)) {
			flakes[i].gravity = 0;
			return 1;
		}
	}

	return 0;
}

void update_flakes(screen_t * screen, flake_t * flakes) {
	for (int i = 0; flakes[i].display; ++i) {
		if (flakes[i].gravity) {
			flakes[i].x += flakes[i].speed;
			if (flakes[i].x < 0) {
				flakes[i].x = screen->width - 1;
			} else if (flakes[i].x >= screen->width) {
				flakes[i].x = 0;
			}
			if (!detect_collisions(screen, flakes, i)) {
				flakes[i].y += flakes[i].gravity;
			}
		}
	}
}

void write_screen(screen_t * screen, flake_t * flakes) {
	for (int y = 0; y < screen->height; ++y) {
		for (int x = 0; x < screen->width; ++x) {
			screen->backingstore[x + y * screen->width] = BLANK_SPACE;
		}
	}
	for (int i = 0; flakes[i].display; ++i) {
		if (flakes[i].y >= 0 && flakes[i].y < screen->height && flakes[i].x >= 0 && flakes[i].x < screen->width) {
			screen->backingstore[flakes[i].x + flakes[i].y * screen->width] = flakes[i].display;
		}
	}
}

void flip_screen(screen_t * screen) {
	printf("\033[H");
	for (int y = 0; y < screen->height; ++y) {
		for (int x = 0; x < screen->width; ++x) {
			if ((y == screen->height - 1) && (x == screen->width - 1)) {
				fflush(stdout);
				return;
			}
			printf("%s", screen->backingstore[x + y * screen->width]);
		}
		printf("\n");
	}
}

int main(int argc, char * argv) {

	screen_t * main_screen = init_screen();
	flake_t *  flakes      = make_some_flakes(main_screen, INITIAL_SNOW);

	for (;;) {
		write_screen(main_screen, flakes);
		flip_screen(main_screen);
		update_flakes(main_screen, flakes);
		flakes = add_flakes(main_screen, flakes, INCREMENTAL_SNOW);
		usleep(90000);
	}


	return 0;
}
