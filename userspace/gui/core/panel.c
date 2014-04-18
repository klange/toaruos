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
#include <unistd.h>
#include <sys/time.h>
#include <sys/utsname.h>

#define PANEL_HEIGHT 28

#include "lib/pthread.h"
#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/shmemfonts.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

gfx_context_t * ctx;
yutani_t * yctx;
yutani_window_t * panel;
list_t * window_list;
volatile int lock;

int width;
int height;

int center_x(int x) {
	return (width - x) / 2;
}

int center_y(int y) {
	return (height - y) / 2;
}

static void spin_lock(int volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		syscall_yield();
	}
}

static void spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
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
#define TIME_LEFT 108
#define DATE_WIDTH 70

volatile int _continue = 1;

void sig_int(int sig) {
	printf("Received shutdown signal in panel!\n");
	_continue = 0;
}

#if 0
void panel_check_click(w_mouse_t * evt) {
	if (evt->command == WE_MOUSECLICK) {
		printf("Click!\n");
		if (evt->new_x >= width - 24 ) {
			printf("Clicked log-out button. Good bye!\n");
			_continue = 0;
		}
	}
}
#endif

void update_window_list(void) {
	yutani_query_windows(yctx);

	list_t * new_window_list = list_create();

	while (1) {
		yutani_msg_t * m = yutani_wait_for(yctx, YUTANI_MSG_WINDOW_ADVERTISE);
		struct yutani_msg_window_advertise * wa = (void*)m->data;

		if (wa->size == 0) {
			free(m);
			break;
		}

		fprintf(stderr, "Window available: %s\n", wa->name);

		char * s = malloc(wa->size + 1);
		memcpy(s, wa->name, wa->size + 1);

		list_insert(new_window_list, s);
		free(m);
	}

	spin_lock(&lock);
	if (window_list) {
		list_destroy(window_list);
		list_free(window_list);
		free(window_list);
	}
	window_list = new_window_list;
	spin_unlock(&lock);
}


void * clock_thread(void * garbage) {
	for (uint32_t i = 0; i < width; i += sprites[0]->width) {
		draw_sprite(ctx, sprites[0], i, 0);
	}

	size_t buf_size = panel->width * panel->height * sizeof(uint32_t);
	char * buf = malloc(buf_size);
	memcpy(buf, ctx->backbuffer, buf_size);

	struct timeval now;
	int last = 0;
	struct tm * timeinfo;
	char   buffer[80];

#if 0
	struct utsname u;
	uname(&u);
#endif

	uint32_t txt_color = rgb(230,230,230);
	int t = 0;

	while (_continue) {
		/* Redraw the background by memcpy (super speedy) */
		memcpy(ctx->backbuffer, buf, buf_size);
		gettimeofday(&now, NULL);
		if (now.tv_sec != last) {
			last = now.tv_sec;
			timeinfo = localtime((time_t *)&now.tv_sec);

			strftime(buffer, 80, "%H:%M:%S", timeinfo);
			set_font_face(FONT_SANS_SERIF_BOLD);
			set_font_size(16);
			draw_string(ctx, width - TIME_LEFT, 19, txt_color, buffer);

			strftime(buffer, 80, "%A", timeinfo);
			set_font_face(FONT_SANS_SERIF);
			set_font_size(9);
			t = draw_string_width(buffer);
			t = (DATE_WIDTH - t) / 2;
			draw_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 11, txt_color, buffer);

			strftime(buffer, 80, "%h %e", timeinfo);
			set_font_face(FONT_SANS_SERIF_BOLD);
			set_font_size(9);
			t = draw_string_width(buffer);
			t = (DATE_WIDTH - t) / 2;
			draw_string(ctx, width - TIME_LEFT - DATE_WIDTH + t, 21, txt_color, buffer);

			set_font_face(FONT_SANS_SERIF_BOLD);
			set_font_size(14);
			draw_string(ctx, 10, 18, txt_color, "Applications");

			int i = 0;
			spin_lock(&lock);
			if (window_list) {
				foreach(node, window_list) {
					char * s = node->value;

					set_font_face(FONT_SANS_SERIF);
					set_font_size(14);
					draw_string(ctx, 140 + i, 18, txt_color, s);
					i += draw_string_width(s) + 20;
				}
			}
			spin_unlock(&lock);

			draw_sprite(ctx, sprites[1], width - 23, 1); /* Logout button */

			flip(ctx);
			yutani_flip(yctx, panel);
		}
		usleep(500000);
	}
}

int main (int argc, char ** argv) {
	yctx = yutani_init();

	width  = yctx->display_width;
	height = yctx->display_height;

	init_shmemfonts();
	set_font_size(14);

	/* Create the panel */
	panel = yutani_window_create(yctx, width, PANEL_HEIGHT);
	yutani_set_stack(yctx, panel, YUTANI_ZORDER_TOP);
	ctx = init_graphics_yutani_double_buffer(panel);
	draw_fill(ctx, rgba(0,0,0,0));
	flip(ctx);
	yutani_flip(yctx, panel);

	window_list = NULL;

	yutani_subscribe_windows(yctx);

	init_sprite_png(0, "/usr/share/panel.png");
	init_sprite_png(1, "/usr/share/icons/panel-shutdown.png");

	syscall_signal(2, sig_int);

	pthread_t _clock_thread;
	pthread_create(&_clock_thread, NULL, clock_thread, NULL);

	update_window_list();

	while (_continue) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			if (m->type == YUTANI_MSG_MOUSE_EVENT) {
				/* Do something */

			}
			if (m->type == YUTANI_MSG_NOTIFY) {
				update_window_list();
			}
			free(m);
		}
	}

	yutani_close(yctx, panel);
	yutani_unsubscribe_windows(yctx);

	return 0;
}
