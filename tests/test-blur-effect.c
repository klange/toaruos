#include <stdio.h>
#include <time.h>
#include <math.h>
#include <sys/fswait.h>
#include <sys/time.h>
#include <toaru/yutani.h>
#include <toaru/text.h>
#include <toaru/graphics.h>

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * _ctx;

static uint64_t precise_current_time(void) {
	struct timeval t;
	gettimeofday(&t, NULL);

	time_t sec_diff = t.tv_sec;
	suseconds_t usec_diff = t.tv_usec;

	return (uint64_t)((uint64_t)sec_diff * 1000LL + usec_diff / 1000);
}

static void redraw(void) {
	draw_fill(_ctx, rgba(0,0,0,0));

	float tick = fmod(precise_current_time() / 2000.0, 2 * M_PI);

	float radius = (_ctx->width < _ctx->height ? _ctx->width : _ctx->height) / 2;
	
	float a_x = radius * cos(tick) + _ctx->width / 2;
	float a_y = radius * sin(tick) + _ctx->height / 2;
	float b_x = radius * cos(tick + 2.09439510239) + _ctx->width / 2;
	float b_y = radius * sin(tick + 2.09439510239) + _ctx->height / 2;
	float c_x = radius * cos(tick + 4.18879020479) + _ctx->width / 2;
	float c_y = radius * sin(tick + 4.18879020479) + _ctx->height / 2;

	struct TT_Contour * contour = tt_contour_start(a_x, a_y);
	contour = tt_contour_line_to(contour, b_x, b_y);
	contour = tt_contour_line_to(contour, c_x, c_y);
	struct TT_Shape * shape = tt_contour_finish(contour);

	tt_path_paint(_ctx, shape, premultiply(rgba(0,0,0,16)));
	free(shape);
	free(contour);

	flip(_ctx);

	int amount = sin(tick) * 50.0 + 50.0;
	yutani_window_set_blur(yctx, wina, YUTANI_BLUR_REQUEST_SET_SIZE, amount);
	yutani_flip(yctx, wina);
}

static void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, wina, w, h);
	reinit_graphics_yutani(_ctx, wina);
	redraw();
	yutani_window_resize_done(yctx, wina);
}

int main(int argc, char * argv[]) {
	yctx = yutani_init();
	wina = yutani_window_create_flags(yctx, 500, 500, YUTANI_WINDOW_FLAG_BLUR_BEHIND);
	_ctx = init_graphics_yutani_double_buffer(wina);
	yutani_window_set_blur(yctx, wina, YUTANI_BLUR_REQUEST_NO_FLIP | YUTANI_BLUR_REQUEST_SET_MODE, YUTANI_BLUR_MODE_SCALED);
	yutani_window_set_blur(yctx, wina, YUTANI_BLUR_REQUEST_NO_FLIP | YUTANI_BLUR_REQUEST_SET_PASSES, 2);
	yutani_window_set_blur(yctx, wina, YUTANI_BLUR_REQUEST_NO_FLIP | YUTANI_BLUR_REQUEST_SET_SIZE, 100);
	redraw();
	yutani_window_advertise_icon(yctx, wina, "blur demo", "drawlines");
	int should_exit = 0;
	while (!should_exit) {
		int fds[1] = {fileno(yctx->sock)};
		int index = fswait2(1,fds,10);
		if (index == 0) {
			yutani_msg_t * m = yutani_poll(yctx);
			while (m) {
				switch (m->type) {
					case YUTANI_MSG_KEY_EVENT:
						{
							struct yutani_msg_key_event * ke = (void*)m->data;
							if (ke->wid == wina->wid) {
								if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
									should_exit = 1;
									break;
								}
							}
						}
						break;
					case YUTANI_MSG_RESIZE_OFFER:
						{
							struct yutani_msg_window_resize * wr = (void*)m->data;
							if (wr->wid == wina->wid) {
								resize_finish(wr->width, wr->height);
							}
						}
						break;
					case YUTANI_MSG_WINDOW_CLOSE:
					case YUTANI_MSG_SESSION_END:
						should_exit = 1;
						break;
					default:
						break;
				}
				free(m);
				m = yutani_poll_async(yctx);
			}
		}
		redraw();
	}

	yutani_close(yctx, wina);

}

