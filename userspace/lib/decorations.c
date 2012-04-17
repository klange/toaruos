/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Client-side Window Decoration library
 */

#include <stdint.h>
#include "graphics.h"
#include "window.h"
#include "decorations.h"
#include "shmemfonts.h"

uint32_t decor_top_height     = 24;
uint32_t decor_bottom_height  = 1;
uint32_t decor_left_width     = 1;
uint32_t decor_right_width    = 1;

#define TEXT_OFFSET_X 10
#define TEXT_OFFSET_Y 16

#define BORDERCOLOR rgb(60,60,60)
#define BACKCOLOR rgb(20,20,20)
#define TEXTCOLOR rgb(255,255,255)
#define SGFX(CTX,x,y,WIDTH) *((uint32_t *)&CTX[((WIDTH) * (y) + (x)) * 4])

void init_decorations() {
	init_shmemfonts();
}

void render_decorations(window_t * window, uint8_t * ctx, char * title) {
	for (uint32_t i = 0; i < window->height; ++i) {
		SGFX(ctx,0,i,window->width) = BORDERCOLOR;
		SGFX(ctx,window->width-1,i,window->width) = BORDERCOLOR;
	}
	for (uint32_t i = 1; i < decor_top_height; ++i) {
		for (uint32_t j = 1; j < window->width - 1; ++j) {
			SGFX(ctx,j,i,window->width) = BACKCOLOR;
		}
	}

	/* Fake context for decorations */
	gfx_context_t fake_context;
	fake_context.width  = window->width;
	fake_context.height = window->height;
	fake_context.depth  = 32;
	fake_context.backbuffer = ctx;

	draw_string(&fake_context, TEXT_OFFSET_X,TEXT_OFFSET_Y,TEXTCOLOR,title);
	for (uint32_t i = 0; i < window->width; ++i) {
		SGFX(ctx,i,0,window->width) = BORDERCOLOR;
		SGFX(ctx,i,decor_top_height-1,window->width) = BORDERCOLOR;
		SGFX(ctx,i,window->height-1,window->width) = BORDERCOLOR;
	}
}

uint32_t decor_width() {
	return decor_left_width + decor_right_width;
}

uint32_t decor_height() {
	return decor_top_height + decor_bottom_height;
}


