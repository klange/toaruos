/*
 * The ToAru Sample Game
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "lib/list.h"
#include "lib/graphics.h"

sprite_t * sprites[128];


void waitabit() {
	int x = time(NULL);
	while (time(NULL) < x + 1) {
		// Do nothing.
	}
}

sprite_t alpha_tmp;

void init_sprite(int i, char * filename, char * alpha) {
	sprites[i] = malloc(sizeof(sprite_t));
	load_sprite(sprites[i], filename);
	if (alpha) {
		sprites[i]->alpha = 1;
		load_sprite(&alpha_tmp, alpha);
		sprites[i]->masks = alpha_tmp.bitmap;
	} else {
		sprites[i]->alpha = 0;
	}
	sprites[i]->blank = 0x0;
}

int center_x(int x) {
	return (graphics_width - x) / 2;
}

int center_y(int y) {
	return (graphics_height - y) / 2;
}

static int progress = 0;
static int progress_width = 0;

#define PROGRESS_WIDTH  120
#define PROGRESS_HEIGHT 6
#define PROGRESS_OFFSET 50

void draw_progress() {
	int x = center_x(PROGRESS_WIDTH);
	int y = center_y(0);
	uint32_t color = rgb(0,120,230);
	uint32_t fill  = rgb(0,70,160);
	draw_line(x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET, color);
	draw_line(x, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(x, x, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);
	draw_line(x + PROGRESS_WIDTH, x + PROGRESS_WIDTH, y + PROGRESS_OFFSET, y + PROGRESS_OFFSET + PROGRESS_HEIGHT, color);

	if (progress_width > 0) {
		int width = ((PROGRESS_WIDTH - 2) * progress) / progress_width;
		for (int8_t i = 0; i < PROGRESS_HEIGHT - 1; ++i) {
			draw_line(x + 1, x + 1 + width, y + PROGRESS_OFFSET + i + 1, y + PROGRESS_OFFSET + i + 1, fill);
		}
	}

}

void display() {
	draw_sprite(sprites[0], center_x(sprites[0]->width), center_y(sprites[0]->height));
	draw_progress();
	flip();
}


typedef struct {
	void (*func)();
	char * name;
	int  time;
} startup_item;

list_t * startup_items;

void add_startup_item(char * name, void (*func)(), int time) {
	progress_width += time;
	startup_item * item = malloc(sizeof(startup_item));

	item->name = name;
	item->func = func;
	item->time = time;

	list_insert(startup_items, item);
}

static void test() {
	/* Do Nothing */
}

void run_startup_item(startup_item * item) {
	item->func();
	progress += item->time;
}

int main(int argc, char ** argv) {

	/* Initialize graphics setup */
	init_graphics_double_buffer();

	/* Load sprites */
	init_sprite(0, "/usr/share/bs.bmp", NULL);
	display();

	/* Count startup items */
	startup_items = list_create();
	for (uint32_t i = 0; i < 1000; ++i) {
		add_startup_item("test", test, 1);
	}

	foreach(node, startup_items) {
		run_startup_item((startup_item *)node->value);
		display();
	}

	char * tokens[] = {
		"/bin/terminal",
		"-f",
		NULL
	};
	int i = execve(tokens[0], tokens, NULL);

	return 0;
}
