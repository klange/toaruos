#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>

#include <sys/utsname.h>

static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;
static sprite_t logo;

static int32_t width = 350;
static int32_t height = 250;
static char * version_str;

#define TITLE "About ToaruOS-NIH"

static int center_x(int x) {
	return (width - x) / 2;
}

static void draw_string(int y, const char * string, int font, uint32_t color) {
	draw_sdf_string(ctx, decor_left_width + center_x(draw_sdf_string_width(string, 16, font)), decor_top_height + 10 + logo.height + 10 + y, string, 16, color, font);
}

static void redraw(void) {

	draw_fill(ctx, rgb(204,204,204));
	draw_sprite(ctx, &logo, decor_left_width + center_x(logo.width), decor_top_height + 10);

	char version[100];
	sprintf(version, "ToaruOS-NIH %s", version_str);
	draw_string(0, version, SDF_FONT_BOLD, rgb(0,0,0));
	draw_string(20, "(C) 2011-2018 K. Lange, et al.", SDF_FONT_THIN, rgb(0,0,0));
	draw_string(50, "ToaruOS is free software released under the", SDF_FONT_THIN, rgb(0,0,0));
	draw_string(70, "NCSA/University of Illinois license.", SDF_FONT_THIN, rgb(0,0,0));
	draw_string(100, "http://toaruos.org", SDF_FONT_THIN, rgb(0,0,255));
	draw_string(120, "https://github.com/klange/toaru-nih", SDF_FONT_THIN, rgb(0,0,255));

	render_decorations(window, ctx, TITLE);

	flip(ctx);
	yutani_flip(yctx, window);
}

int main(int argc, char * argv[]) {
	struct utsname u;
	uname(&u);

	version_str = strdup(u.release);
	char * tmp = strstr(version_str, "-");
	if (tmp) {
		*tmp = '\0';
	}

	yctx = yutani_init();
	init_decorations();

	window = yutani_window_create(yctx, width + decor_width(), height + decor_height());
	yutani_window_move(yctx, window, yctx->display_width / 2 - window->width / 2, yctx->display_height / 2 - window->height / 2);

	yutani_window_advertise_icon(yctx, window, TITLE, "star");

	ctx = init_graphics_yutani_double_buffer(window);
	load_sprite(&logo, "/usr/share/logo_login.bmp");
	logo.alpha = ALPHA_EMBEDDED;

	redraw();

	int playing = 1;
	while (playing) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
							playing = 0;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win) {
							win->focused = wf->focused;
							redraw();
						}
					}
					break;
#if 0
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						resize_finish(wr->width, wr->height);
					}
					break;
#endif
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						int result = decor_handle_event(yctx, m);
						switch (result) {
							case DECOR_CLOSE:
								playing = 0;
								break;
							default:
								/* Other actions */
								break;
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					playing = 0;
					break;
				default:
					break;
			}
		}
		free(m);
	}

	yutani_close(yctx, window);

	return 0;
}
