/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2015 K. Lange
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

#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/time.h>

#include <toaru/graphics.h>
#include <toaru/kbd.h>
#include <toaru/yutani.h>
#include <toaru/auth.h>
#include <toaru/confreader.h>
#include <toaru/sdf.h>

#include <toaru/trace.h>
#define TRACE_APP_NAME "glogin-provider"

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
static int BOX_ROUNDNESS = 8;
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
static char * WALLPAPER = "/usr/share/wallpaper.jpg";
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

void draw_text_box(gfx_context_t * ctx, struct text_box * tb) {
	int x = tb->parent->x + tb->x;
	int y = tb->parent->y + tb->y;

	int text_offset = 15;
	if (tb->is_focused) {
		draw_rounded_rectangle(ctx, 1 + x, 1 + y, tb->width - 2, tb->height - 2, 4, rgb(8,193,236));
		draw_rounded_rectangle(ctx, 2 + x, 2 + y, tb->width - 4, tb->height - 4, 4, rgb(244,244,244));
	} else {
		draw_rounded_rectangle(ctx, 1 + x, 1 + y, tb->width - 2, tb->height - 2, 4, rgb(158,169,177));
	}
	/* Line width 2? */
	char * text = tb->buffer;
	char password_circles[512];
	uint32_t color = tb->text_color;

	if (strlen(tb->buffer) == 0 && !tb->is_focused) {
		text = tb->placeholder;
		color = rgba(0,0,0,127);
	} else if (tb->is_password) {
		strcpy(password_circles, "");
		for (unsigned int i = 0; i < strlen(tb->buffer); ++i) {
			strcat(password_circles, "\007");
		}
		text = password_circles;
	}

	draw_sdf_string(ctx, x + TEXTBOX_INTERIOR_LEFT, y + text_offset - 12, text, 15, color, SDF_FONT_THIN);

	if (tb->is_focused) {
		int width = draw_sdf_string_width(text, 15, SDF_FONT_THIN) + 2;
		draw_line(ctx, x + TEXTBOX_INTERIOR_LEFT + width, x + TEXTBOX_INTERIOR_LEFT + width, y + 2, y + text_offset + 1, tb->text_color);
	}

}

void draw_login_container(gfx_context_t * ctx, struct login_container * lc) {


	/* Draw rounded rectangle */
	draw_rounded_rectangle(ctx, lc->x, lc->y, lc->width, lc->height, BOX_ROUNDNESS, rgba(BOX_COLOR_R,BOX_COLOR_G,BOX_COLOR_B,BOX_COLOR_A));

	/* Draw labels */
	if (lc->show_error) {
		char * error_message = "Incorrect username or password.";

		//set_font_size(11);
		//draw_string(ctx, lc->x + (lc->width - draw_string_width(error_message)) / 2, lc->y + 6 + EXTRA_TEXT_OFFSET, rgb(240, 20, 20), error_message);
		draw_sdf_string(ctx, lc->x + (lc->width - draw_sdf_string_width(error_message, 14, SDF_FONT_THIN)) / 2, lc->y + 6 + EXTRA_TEXT_OFFSET - 14, error_message, 14, rgb(240,20,20), SDF_FONT_THIN);
	}

	draw_text_box(ctx, lc->username_box);
	draw_text_box(ctx, lc->password_box);

}

/**
 * Get hostname information updated with the current time.
 *
 * @param hostname
 */
static void get_updated_hostname_with_time_info(char hostname[]) {
	// get hostname
	char _hostname[256];
	gethostname(_hostname, 255);

	// get current time
	struct tm * timeinfo;
	struct timeval now;
	gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	// format the hostname info
	char _date[256];
	strftime(_date, 256, "%a %B %d %Y", timeinfo);
	sprintf(hostname, "%s // %s", _hostname, _date);
}

static void draw_sdf_string_shadow(gfx_context_t * ctx, char * string, int font_size, int left, int top, uint32_t text_color, uint32_t shadow_color, int blur, int font) {
	int w = draw_sdf_string_width(string, font_size, font);
	sprite_t * _tmp_s = create_sprite(w + blur * 2, font_size + blur * 2, ALPHA_EMBEDDED);
	gfx_context_t * _tmp = init_graphics_sprite(_tmp_s);
	draw_fill(_tmp, rgba(0,0,0,0));
	draw_sdf_string(_tmp, blur, blur, string, font_size, shadow_color, font);
	blur_context_box(_tmp, blur);
	free(_tmp);
	draw_sprite(ctx, _tmp_s, left - blur, top - blur);
	sprite_free(_tmp_s);
	draw_sdf_string(ctx, left, top, string, 14, text_color, font);
}

int main (int argc, char ** argv) {
	if (getuid() != 0) {
		return 1;
	}

	fprintf(stdout, "Hello\n");

	yutani_t * y = yutani_init();

	if (!y) {
		fprintf(stderr, "[glogin] Connection to server failed.\n");
		return 1;
	}

	/* Load config */
	{
		confreader_t * conf = confreader_load("/etc/glogin.conf");

		if (conf) {

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
		}

		TRACE("Loading complete");
	}

	TRACE("Loading logo...");
	load_sprite(&logo, LOGO);
	TRACE("... done.");

	/* Generate surface for background */
	sprite_t * bg_sprite;

	int width  = y->display_width;
	int height = y->display_height;
	int skip_animation = 0;

	/* Do something with a window */
	TRACE("Connecting to window server...");
	yutani_window_t * wina = yutani_window_create(y, width, height);
	assert(wina);
	yutani_set_stack(y, wina, 0);
	ctx = init_graphics_yutani_double_buffer(wina);
	draw_fill(ctx, rgba(0,0,0,255));
	yutani_flip(y, wina);
	TRACE("... done.");


redo_everything:
	win_width = width;
	win_height = height;

#if 0
	cairo_surface_t * cs = cairo_image_surface_create_for_data((void*)ctx->backbuffer, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ctx->width));
	cairo_t * cr = cairo_create(cs);
#endif


	TRACE("Loading wallpaper...");
	{
		sprite_t * wallpaper = malloc(sizeof(sprite_t));
		load_sprite(wallpaper, WALLPAPER);

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
		if (!skip_animation) {
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
		skip_animation = 0;

		char username[INPUT_SIZE] = {0};
		char password[INPUT_SIZE] = {0};
		char hostname[512];

		// we do it here to calculate the final string position
		get_updated_hostname_with_time_info(hostname);

		char kernel_v[512];

		{
			struct utsname u;
			uname(&u);
			char * os_name_ = "ToaruOS";
			snprintf(kernel_v, 512, "%s %s", os_name_, u.release);
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

		//set_font_size(11);
		int hostname_label_left = width - 10 - draw_sdf_string_width(hostname, 14, SDF_FONT_BOLD);
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

				// update time info
				get_updated_hostname_with_time_info(hostname);

				memcpy(ctx->backbuffer, foo, sizeof(uint32_t) * width * height);
				draw_sprite(ctx, &logo, center_x(logo.width), center_y(logo.height) - LOGO_FINAL_OFFSET);

				draw_sdf_string_shadow(ctx, hostname, 14, hostname_label_left, height - 22, rgb(255,255,255), rgb(0,0,0), 4, SDF_FONT_BOLD);
				draw_sdf_string_shadow(ctx, kernel_v, 14, kernel_v_label_left, height - 22, rgb(255,255,255), rgb(0,0,0), 4, SDF_FONT_BOLD);

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

				draw_login_container(ctx, &lc);

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
						case YUTANI_MSG_WELCOME:
							{
								struct yutani_msg_welcome * mw = (void*)msg->data;
								yutani_window_resize(y, wina, mw->display_width, mw->display_height);
							}
							break;
						case YUTANI_MSG_RESIZE_OFFER:
							{
								struct yutani_msg_window_resize * wr = (void*)msg->data;
								width = wr->width;
								height = wr->height;
								yutani_window_resize_accept(y, wina, width, height);
								reinit_graphics_yutani(ctx, wina);
								yutani_window_resize_done(y, wina);

								sprite_free(bg_sprite);
								//cairo_destroy(cr);
								//cairo_surface_destroy(cs);

								skip_animation = 1;
								goto redo_everything;
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

						if (mou.new_x >= (int)lc.x + (int)username_box.x &&
						    mou.new_x <= (int)lc.x + (int)username_box.x + (int)username_box.width &&
						    mou.new_y >= (int)lc.y + (int)username_box.y &&
						    mou.new_y <= (int)lc.y + (int)username_box.y + (int)username_box.height) {
							/* Ensure this box is focused. */
							focus = USERNAME_BOX;
							continue;
						} else if (
						    (int)mou.new_x >= (int)lc.x + (int)password_box.x &&
						    (int)mou.new_x <= (int)lc.x + (int)password_box.x + (int)password_box.width &&
						    (int)mou.new_y >= (int)lc.y + (int)password_box.y &&
						    (int)mou.new_y <= (int)lc.y + (int)password_box.y + (int)password_box.height) {
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

			fprintf(stdout, "USER %s\n", username);
			fprintf(stdout, "PASS %s\n", password);
			fprintf(stdout, "AUTH\n");

			char tmp[1024];
			fgets(tmp, 1024, stdin);
			if (!strcmp(tmp,"FAIL\n")) {
				lc.show_error = 1;
				continue;
			} else if (!strcmp(tmp,"SUCC\n")) {
				fprintf(stderr,"Success!\n");
				goto _success;
			}
		}
	}

_success:
	yutani_close(y, wina);


	return 0;
}
