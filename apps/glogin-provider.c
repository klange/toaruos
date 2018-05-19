#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/drawstring.h>
#include <toaru/sdf.h>

static yutani_t * yctx;
static yutani_window_t * wallpaper_window;
static gfx_context_t * wallpaper_ctx;
static sprite_t * wallpaper;

static void draw_background(int width, int height) {

	float x = (float)wallpaper_window->width / (float)wallpaper->width;
	float y = (float)wallpaper_window->height / (float)wallpaper->height;

	int nh = (int)(x * (float)wallpaper->height);
	int nw = (int)(y * (float)wallpaper->width);

	if (nw == wallpaper->width && nh == wallpaper->height) {
		// special case
		draw_sprite(wallpaper_ctx, wallpaper, 0, 0);
	} else if (nw >= width) {
		draw_sprite_scaled(wallpaper_ctx, wallpaper, ((int)wallpaper_window->width - nw) / 2, 0, nw+2, wallpaper_window->height);
	} else {
		draw_sprite_scaled(wallpaper_ctx, wallpaper, 0, ((int)wallpaper_window->height - nh) / 2, wallpaper_window->width+2, nh);
	}

	blur_context_box(wallpaper_ctx, 20);
	blur_context_box(wallpaper_ctx, 20);
	blur_context_box(wallpaper_ctx, 20);

	{
		struct utsname u;
		uname(&u);

		char * version_str = strdup(u.release);
		char * tmp = strstr(version_str, "-");
		if (tmp) {
			*tmp = '\0';
		}
		char version[100];
		sprintf(version, "ToaruOS-NIH %s", version_str);
		free(version_str);

		int w = draw_sdf_string_width(version, 14, SDF_FONT_BOLD);

		sprite_t * _tmp_s = create_sprite(w, 20, ALPHA_EMBEDDED);
		gfx_context_t * _tmp = init_graphics_sprite(_tmp_s);

		draw_fill(_tmp, rgba(0,0,0,0));
		draw_sdf_string(_tmp, 0, 0, version, 14, rgb(0,0,0), SDF_FONT_BOLD);
		blur_context_box(_tmp, 4);

		free(_tmp);
		draw_sprite(wallpaper_ctx, _tmp_s, 10, wallpaper_ctx->height - 22);
		sprite_free(_tmp_s);

		draw_sdf_string(wallpaper_ctx, 10, wallpaper_ctx->height - 22 , version, 14, rgb(255,255,255), SDF_FONT_BOLD);

	}

	{
		char * s = "It is now safe to shut down your virtual machine.";

		int w = draw_sdf_string_width(s, 24, SDF_FONT_THIN);

		sprite_t * _tmp_s = create_sprite(w, 50, ALPHA_EMBEDDED);
		gfx_context_t * _tmp = init_graphics_sprite(_tmp_s);

		draw_fill(_tmp, rgba(0,0,0,0));
		draw_sdf_string(_tmp, 0, 0, s, 24, rgb(0,0,0), SDF_FONT_THIN);
		blur_context_box(_tmp, 4);

		free(_tmp);
		draw_sprite(wallpaper_ctx, _tmp_s, (wallpaper_ctx->width - w) / 2, wallpaper_ctx->height / 2 - 20);
		draw_sprite(wallpaper_ctx, _tmp_s, (wallpaper_ctx->width - w) / 2, wallpaper_ctx->height / 2 - 20);
		sprite_free(_tmp_s);

		draw_sdf_string(wallpaper_ctx, (wallpaper_ctx->width - w) / 2, wallpaper_ctx->height / 2 - 20, s, 24, rgb(255,255,255), SDF_FONT_THIN);

	}
}

static void resize_finish_wallpaper(int width, int height) {
	yutani_window_resize_accept(yctx, wallpaper_window, width, height);
	reinit_graphics_yutani(wallpaper_ctx, wallpaper_window);
	draw_background(width, height);
	yutani_window_resize_done(yctx, wallpaper_window);
	yutani_flip(yctx, wallpaper_window);
}

int main (int argc, char ** argv) {

	if (getuid() != 0) {
		return 1;
	}

	wallpaper = malloc(sizeof(sprite_t));
	load_sprite(wallpaper, "/usr/share/wallpaper.bmp");
	wallpaper->alpha = 0;

	fprintf(stdout, "Hello\n");

	yctx = yutani_init();

	/* wallpaper */
	wallpaper_window = yutani_window_create(yctx, yctx->display_width, yctx->display_height);
	yutani_window_move(yctx, wallpaper_window, 0, 0);
	yutani_set_stack(yctx, wallpaper_window, YUTANI_ZORDER_BOTTOM);

	wallpaper_ctx = init_graphics_yutani(wallpaper_window);
	draw_background(yctx->display_width, yctx->display_height);
	yutani_flip(yctx, wallpaper_window);

	int should_exit = 0;

	while (!should_exit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_WELCOME:
					yutani_window_resize_offer(yctx, wallpaper_window, yctx->display_width, yctx->display_height);
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == wallpaper_window->wid) {
							resize_finish_wallpaper(wr->width, wr->height);
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					should_exit = 1;
					break;
				default:
					break;
			}
		}
		free(m);
	}

	yutani_close(yctx, wallpaper_window);

	return 0;
}


