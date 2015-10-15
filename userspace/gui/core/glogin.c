/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2015 Kevin Lange
 */
/*
 * glogin
 *
 * Graphical Login screen
 */
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include <cairo.h>

#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "lib/graphics.h"
#include "lib/shmemfonts.h"
#include "lib/kbd.h"
#include "lib/yutani.h"
#include "lib/toaru_auth.h"
#include "lib/confreader.h"

#include "gui/ttk/ttk.h"

#include "lib/trace.h"
#define TRACE_APP_NAME "glogin"

static sprite_t logo;

static gfx_context_t * ctx;

static uint16_t win_width;
static uint16_t win_height;

static int uid = 0;

#define USERNAME_BOX 1
#define PASSWORD_BOX 2

static int LOGO_FINAL_OFFSET = 100;
static int BOX_WIDTH = 272;
static int BOX_HEIGHT = 104;
static int BOX_ROUNDNESS = 4;
static int CENTER_BOX_X=1;
static int CENTER_BOX_Y=1;
static int BOX_LEFT=-1;
static int BOX_RIGHT=-1;
static int BOX_TOP=-1;
static int BOX_BOTTOM=-1;
static int BOX_COLOR_R=0;
static int BOX_COLOR_G=0;
static int BOX_COLOR_B=0;
static int BOX_COLOR_A=127;
static char * WALLPAPER = "/usr/share/wallpapers/yosemite.png";
static char * LOGO = "/usr/share/logo_login.png";

#define TEXTBOX_INTERIOR_LEFT 4
#define EXTRA_TEXT_OFFSET 15

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

#define INPUT_SIZE 1024
int buffer_put(char * input_buffer, char c) {
	int input_collected = strlen(input_buffer);
	if (c == 8) {
		/* Backspace */
		if (input_collected > 0) {
			input_collected--;
			input_buffer[input_collected] = '\0';
		}
		return 0;
	}
	if (c < 10 || (c > 10 && c < 32) || c > 126) {
		return 0;
	}
	input_buffer[input_collected] = c;
	input_collected++;
	input_buffer[input_collected] = '\0';
	if (input_collected == INPUT_SIZE - 1) {
		return 1;
	}
	return 0;
}

struct text_box {
	int x;
	int y;
	unsigned int width;
	unsigned int height;

	uint32_t text_color;

	struct login_container * parent;

	int is_focused:1;
	int is_password:1;

	unsigned int cursor;
	char * buffer;

	char * placeholder;
};

struct login_container {
	int x;
	int y;
	unsigned int width;
	unsigned int height;

	struct text_box * username_box;
	struct text_box * password_box;

	int show_error:1;
};

void draw_text_box(cairo_t * cr, struct text_box * tb) {
	int x = tb->parent->x + tb->x;
	int y = tb->parent->y + tb->y;

	set_font_size(13);
	int text_offset = 15;

	cairo_rounded_rectangle(cr, 1 + x, 1 + y, tb->width - 2, tb->height - 2, 2.0);
	if (tb->is_focused) {
		cairo_set_source_rgba(cr, 8.0/255.0, 193.0/255.0, 236.0/255.0, 1.0);
	} else {
		cairo_set_source_rgba(cr, 158.0/255.0, 169.0/255.0, 177.0/255.0, 1.0);
	}
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	{
		cairo_pattern_t * pat = cairo_pattern_create_linear(1 + x, 1 + y, 1 + x, 1 + y + tb->height - 2);
		if (tb->is_focused) {
			cairo_pattern_add_color_stop_rgba(pat, 0, 241.0/255.0, 241.0/255.0, 244.0/255.0, 1.0);
			cairo_pattern_add_color_stop_rgba(pat, 1, 1, 1, 1, 1.0);
		} else {
			cairo_pattern_add_color_stop_rgba(pat, 0, 241.0/255.0, 241.0/255.0, 244.0/255.0, 0.9);
			cairo_pattern_add_color_stop_rgba(pat, 1, 1, 1, 1, 0.9);
		}
		cairo_rounded_rectangle(cr, 1 + x, 1 + y, tb->width - 2, tb->height - 2, 2.0);
		cairo_set_source(cr, pat);
		cairo_fill(cr);
		cairo_pattern_destroy(pat);
	}

	char * text = tb->buffer;
	char password_circles[512];
	uint32_t color = tb->text_color;

	if (strlen(tb->buffer) == 0 && !tb->is_focused) {
		text = tb->placeholder;
		color = rgba(0,0,0,127);
	} else if (tb->is_password) {
		strcpy(password_circles, "");
		for (int i = 0; i < strlen(tb->buffer); ++i) {
			strcat(password_circles, "⚫");
		}
		text = password_circles;
	}

	draw_string(ctx, x + TEXTBOX_INTERIOR_LEFT, y + text_offset, color, text);

	if (tb->is_focused) {
		int width = draw_string_width(text);
		draw_line(ctx, x + TEXTBOX_INTERIOR_LEFT + width, x + TEXTBOX_INTERIOR_LEFT + width, y + 2, y + text_offset + 1, tb->text_color);
	}

}

void draw_login_container(cairo_t * cr, struct login_container * lc) {

	/* Draw rounded rectangle */
	cairo_rounded_rectangle(cr, lc->x, lc->y, lc->width, lc->height, (float)BOX_ROUNDNESS);
	cairo_set_source_rgba(cr, (float)(BOX_COLOR_R)/255.0, (float)(BOX_COLOR_G)/255.0, (float)(BOX_COLOR_B)/255.0, (float)(BOX_COLOR_A)/255.0);
	cairo_fill(cr);

	/* Draw labels */
	if (lc->show_error) {
		char * error_message = "Incorrect username or password.";

		set_font_size(11);
		draw_string(ctx, lc->x + (lc->width - draw_string_width(error_message)) / 2, lc->y + 6 + EXTRA_TEXT_OFFSET, rgb(240, 20, 20), error_message);
	}

	draw_text_box(cr, lc->username_box);
	draw_text_box(cr, lc->password_box);

}

int main (int argc, char ** argv) {
	init_shmemfonts();

	yutani_t * y = yutani_init();

	if (!y) {
		fprintf(stderr, "[glogin] Connection to server failed.\n");
		return 1;
	}

	/* Load config */
	{
		confreader_t * conf = confreader_load("/etc/glogin.conf");

		LOGO_FINAL_OFFSET = confreader_intd(conf, "style", "logo_padding", LOGO_FINAL_OFFSET);
		BOX_WIDTH = confreader_intd(conf, "style", "box_width", BOX_WIDTH);
		BOX_HEIGHT = confreader_intd(conf, "style", "box_height", BOX_HEIGHT);
		BOX_ROUNDNESS = confreader_intd(conf, "style", "box_roundness", BOX_ROUNDNESS);
		CENTER_BOX_X = confreader_intd(conf, "style", "center_box_x", CENTER_BOX_X);
		CENTER_BOX_Y = confreader_intd(conf, "style", "center_box_y", CENTER_BOX_Y);
		BOX_LEFT = confreader_intd(conf, "style", "box_left", BOX_LEFT);
		BOX_RIGHT = confreader_intd(conf, "style", "box_right", BOX_RIGHT);
		BOX_TOP = confreader_intd(conf, "style", "box_top", BOX_TOP);
		BOX_BOTTOM = confreader_intd(conf, "style", "box_bottom", BOX_BOTTOM);
		BOX_COLOR_R = confreader_intd(conf, "style", "box_color_r", BOX_COLOR_R);
		BOX_COLOR_G = confreader_intd(conf, "style", "box_color_g", BOX_COLOR_G);
		BOX_COLOR_B = confreader_intd(conf, "style", "box_color_b", BOX_COLOR_B);
		BOX_COLOR_A = confreader_intd(conf, "style", "box_color_a", BOX_COLOR_A);

		WALLPAPER = confreader_getd(conf, "image", "wallpaper", WALLPAPER);
		LOGO = confreader_getd(conf, "image", "logo", LOGO);

		confreader_free(conf);

		TRACE("Loading complete");
	}

	TRACE("Loading logo...");
	load_sprite_png(&logo, LOGO);
	TRACE("... done.");

	/* Generate surface for background */
	sprite_t * bg_sprite;

	int width  = y->display_width;
	int height = y->display_height;

	win_width = width;
	win_height = height;

	/* Do something with a window */
	TRACE("Connecting to window server...");
	yutani_window_t * wina = yutani_window_create(y, width, height);
	assert(wina);
	yutani_set_stack(y, wina, 0);
	ctx = init_graphics_yutani_double_buffer(wina);
	draw_fill(ctx, rgba(0,0,0,255));
	yutani_flip(y, wina);
	TRACE("... done.");

	cairo_surface_t * cs = cairo_image_surface_create_for_data((void*)ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ctx->width));
	cairo_t * cr = cairo_create(cs);

	TRACE("Loading wallpaper...");
	{
		sprite_t * wallpaper = malloc(sizeof(sprite_t));
		load_sprite_png(wallpaper, WALLPAPER);

		float x = (float)width  / (float)wallpaper->width;
		float y = (float)height / (float)wallpaper->height;

		int nh = (int)(x * (float)wallpaper->height);
		int nw = (int)(y * (float)wallpaper->width);;

		bg_sprite = create_sprite(width, height, ALPHA_OPAQUE);
		gfx_context_t * bg = init_graphics_sprite(bg_sprite);

		if (nw > width) {
			draw_sprite_scaled(bg, wallpaper, (width - nw) / 2, 0, nw, height);
		} else {
			draw_sprite_scaled(bg, wallpaper, 0, (height - nh) / 2, width, nh);
		}

		/* Three box blurs = good enough approximation of a guassian, but faster*/
		blur_context_box(bg, 20);
		blur_context_box(bg, 20);
		blur_context_box(bg, 20);

		free(bg);
		free(wallpaper);
	}
	TRACE("... done.");

	while (1) {

		yutani_set_stack(y, wina, 0);
		yutani_focus_window(y, wina->wid);

		draw_fill(ctx, rgb(0,0,0));
		draw_sprite(ctx, bg_sprite, center_x(width), center_y(height));
		flip(ctx);
		yutani_flip(y, wina);

		char * foo = malloc(sizeof(uint32_t) * width * height);
		memcpy(foo, ctx->backbuffer, sizeof(uint32_t) * width * height);

		TRACE("Begin animation.");
		{
			struct timeval start;
			gettimeofday(&start, NULL);

			while (1) {
				uint32_t tick;
				struct timeval t;
				gettimeofday(&t, NULL);

				uint32_t sec_diff = t.tv_sec - start.tv_sec;
				uint32_t usec_diff = t.tv_usec - start.tv_usec;

				if (t.tv_usec < start.tv_usec) {
					sec_diff -= 1;
					usec_diff = (1000000 + t.tv_usec) - start.tv_usec;
				}

				tick = (uint32_t)(sec_diff * 1000 + usec_diff / 1000);
				int i = (float)LOGO_FINAL_OFFSET * (float)tick / 700.0f;
				if (i >= LOGO_FINAL_OFFSET) break;

				memcpy(ctx->backbuffer, foo, sizeof(uint32_t) * width * height);
				draw_sprite(ctx, &logo, center_x(logo.width), center_y(logo.height) - i);
				flip(ctx);
				yutani_flip_region(y, wina, center_x(logo.width), center_y(logo.height) - i, logo.width, logo.height + 5);
				usleep(10000);
			}
		}
		TRACE("End animation.");

		size_t buf_size = wina->width * wina->height * sizeof(uint32_t);
		char * buf = malloc(buf_size);

		uint32_t i = 0;

		uint32_t black = rgb(0,0,0);
		uint32_t white = rgb(255,255,255);

		int x_offset = 65;
		int y_offset = 64;

		int fuzz = 3;

		char username[INPUT_SIZE] = {0};
		char password[INPUT_SIZE] = {0};
		char hostname[512];

		{
			char _hostname[256];
			syscall_gethostname(_hostname);

			struct tm * timeinfo;
			struct timeval now;
			gettimeofday(&now, NULL); //time(NULL);
			timeinfo = localtime((time_t *)&now.tv_sec);

			char _date[256];
			strftime(_date, 256, "%a %B %d %Y", timeinfo);

			sprintf(hostname, "%s // %s", _hostname, _date);
		}

		char kernel_v[512];

		{
			struct utsname u;
			uname(&u);
			/* UTF-8 Strings FTW! */
			uint8_t * os_name_ = "とあるOS";
			uint32_t l = snprintf(kernel_v, 512, "%s %s", os_name_, u.release);
		}

		uid = 0;

		int box_x, box_y;

		if (CENTER_BOX_X) {
			box_x = center_x(BOX_WIDTH);
		} else if (BOX_LEFT == -1) {
			box_x = win_width - BOX_RIGHT - BOX_WIDTH;
		} else {
			box_x = BOX_LEFT;
		}
		if (CENTER_BOX_Y) {
			box_y = center_y(0) + 8;
		} else if (BOX_TOP == -1) {
			box_y = win_width - BOX_BOTTOM - BOX_HEIGHT;
		} else {
			box_y = BOX_TOP;
		}

		int focus = 0;

		set_font_size(11);
		int hostname_label_left = width - 10 - draw_string_width(hostname);
		int kernel_v_label_left = 10;

		struct text_box username_box = { (BOX_WIDTH - 170) / 2, 30, 170, 20, rgb(0,0,0), NULL, 0, 0, 0, username, "Username" };
		struct text_box password_box = { (BOX_WIDTH - 170) / 2, 58, 170, 20, rgb(0,0,0), NULL, 0, 1, 0, password, "Password" };

		struct login_container lc = { box_x, box_y, BOX_WIDTH, BOX_HEIGHT, &username_box, &password_box, 0 };

		username_box.parent = &lc;
		password_box.parent = &lc;

		while (1) {
			focus = 0;
			memset(username, 0x0, INPUT_SIZE);
			memset(password, 0x0, INPUT_SIZE);

			while (1) {

				memcpy(ctx->backbuffer, foo, sizeof(uint32_t) * width * height);
				draw_sprite(ctx, &logo, center_x(logo.width), center_y(logo.height) - LOGO_FINAL_OFFSET);

				set_font_size(11);
				draw_string_shadow(ctx, hostname_label_left, height - 12, white, hostname, rgb(0,0,0), 2, 1, 1, 3.0);
				draw_string_shadow(ctx, kernel_v_label_left, height - 12, white, kernel_v, rgb(0,0,0), 2, 1, 1, 3.0);

				if (focus == USERNAME_BOX) {
					username_box.is_focused = 1;
					password_box.is_focused = 0;
				} else if (focus == PASSWORD_BOX) {
					username_box.is_focused = 0;
					password_box.is_focused = 1;
				} else {
					username_box.is_focused = 0;
					password_box.is_focused = 0;
				}

				draw_login_container(cr, &lc);

				flip(ctx);
				yutani_flip(y, wina);

				struct yutani_msg_key_event kbd;
				struct yutani_msg_window_mouse_event mou;
				int msg_type = 0;
collect_events:
				do {
					yutani_msg_t * msg = yutani_poll(y);
					switch (msg->type) {
						case YUTANI_MSG_KEY_EVENT:
							{
								struct yutani_msg_key_event * ke = (void*)msg->data;
								if (ke->event.action == KEY_ACTION_DOWN) {
									memcpy(&kbd, ke, sizeof(struct yutani_msg_key_event));
									msg_type = 1;
								}
							}
							break;
						case YUTANI_MSG_WINDOW_MOUSE_EVENT:
							{
								struct yutani_msg_window_mouse_event * me = (void*)msg->data;
								memcpy(&mou, me, sizeof(struct yutani_msg_mouse_event));
								msg_type = 2;
							}
							break;
					}
					free(msg);
				} while (!msg_type);

				if (msg_type == 1) {

					if (kbd.event.keycode == '\n') {
						if (focus == USERNAME_BOX) {
							focus = PASSWORD_BOX;
							continue;
						} else if (focus == PASSWORD_BOX) {
							break;
						} else {
							focus = USERNAME_BOX;
							continue;
						}
					}

					if (kbd.event.keycode == '\t') {
						if (focus == USERNAME_BOX) {
							focus = PASSWORD_BOX;
						} else {
							focus = USERNAME_BOX;
						}
						continue;
					}

					if (kbd.event.key) {

						if (!focus) {
							focus = USERNAME_BOX;
						}

						if (focus == USERNAME_BOX) {
							buffer_put(username, kbd.event.key);
						} else if (focus == PASSWORD_BOX) {
							buffer_put(password, kbd.event.key);
						}

					}

				} else if (msg_type == 2) {

					if ((mou.command == YUTANI_MOUSE_EVENT_DOWN
					     && mou.buttons & YUTANI_MOUSE_BUTTON_LEFT)
					    || (mou.command == YUTANI_MOUSE_EVENT_CLICK)) {
						/* Determine if we were inside of a text box */

						if (mou.new_x >= lc.x + username_box.x &&
						    mou.new_x <= lc.x + username_box.x + username_box.width &&
						    mou.new_y >= lc.y + username_box.y &&
						    mou.new_y <= lc.y + username_box.y + username_box.height) {
							/* Ensure this box is focused. */
							focus = USERNAME_BOX;
							continue;
						} else if (mou.new_x >= lc.x + password_box.x &&
						    mou.new_x <= lc.x + password_box.x + password_box.width &&
						    mou.new_y >= lc.y + password_box.y &&
						    mou.new_y <= lc.y + password_box.y + password_box.height) {
							/* Ensure this box is focused. */
							focus = PASSWORD_BOX;
							continue;
						} else {
							focus = 0;
							continue;
						}

					} else {
						goto collect_events;
					}
				}

			}

			uid = toaru_auth_check_pass(username, password);

			if (uid >= 0) {
				break;
			}
			lc.show_error = 1;
		}

		memcpy(ctx->backbuffer, foo, sizeof(uint32_t) * width * height);
		flip(ctx);
		yutani_flip(y, wina);
		syscall_yield();

		pid_t _session_pid = fork();
		if (!_session_pid) {
			setuid(uid);
			toaru_auth_set_vars();
			char * args[] = {"/bin/gsession", NULL};
			execvp(args[0], args);
		}

		free(foo);
		free(buf);

		waitpid(_session_pid, NULL, 0);
	}

	yutani_close(y, wina);


	return 0;
}
