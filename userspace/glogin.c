#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/pthread.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

FT_Library   library;
FT_Face      face;
FT_Face      face_bold;
FT_Face      face_italic;
FT_Face      face_bold_italic;
FT_Face      face_extra;
FT_GlyphSlot slot;
FT_UInt      glyph_index;

gfx_context_t * ctx;


uint16_t win_width;
uint16_t win_height;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}


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

void draw_char(FT_Bitmap * bitmap, int x, int y, uint32_t fg) {
	int i, j, p, q;
	int x_max = x + bitmap->width;
	int y_max = y + bitmap->rows;
	for (j = y, q = 0; j < y_max; j++, q++) {
		for ( i = x, p = 0; i < x_max; i++, p++) {
			GFX(ctx, i,j) = alpha_blend(GFX(ctx, i,j),fg,rgb(bitmap->buffer[q * bitmap->width + p],0,0));
			//term_set_point(i,j, alpha_blend(bg, fg, rgb(bitmap->buffer[q * bitmap->width + p],0,0)));
		}
	}
}

void draw_string(int x, int y, uint32_t fg, char * string) {
	slot = face->glyph;
	int pen_x = x, pen_y = y, i = 0;
	int len = strlen(string);
	int error;

	for (i = 0; i < len; ++i) {
		FT_UInt glyph_index;

		glyph_index = FT_Get_Char_Index( face, string[i]);
		error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
		if (error) {
			printf("Error loading glyph for '%c'\n", string[i]);
			continue;
		}
		slot = (face)->glyph;
		if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
			error = FT_Render_Glyph((face)->glyph, FT_RENDER_MODE_NORMAL);
			if (error) {
				printf("Error rendering glyph for '%c'\n", string[i]);
				continue;
			}
		}

		draw_char(&slot->bitmap, pen_x + slot->bitmap_left, pen_y - slot->bitmap_top, fg);
		pen_x += slot->advance.x >> 6;
		pen_y += slot->advance.y >> 6;
	}
}

#define FONT_SIZE 14

void _loadDejavu() {
	char * font;
	size_t s = 0;
	int error;
	font = (char *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER ".fonts.sans-serif", &s);
	error = FT_New_Memory_Face(library, font, s, 0, &face);
	if (error) {
		printf("Oh dear, this is bad.\n");
	}
	error = FT_Set_Pixel_Sizes(face, FONT_SIZE, FONT_SIZE);
	if (error) {
		printf("Oh my.\n");
	}
}


#define INPUT_SIZE 1024
char input_buffer[1024];
uint32_t input_collected = 0;

int buffer_put(char c) {
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
	if (input_buffer[input_collected] == '\n') {
		input_collected++;
		return 1;
	}
	input_collected++;
	if (input_collected == INPUT_SIZE) {
		return 1;
	}
	return 0;
}

void * process_input(void * arg) {
	while (1) {
		w_keyboard_t * kbd = poll_keyboard();
		if (kbd != NULL) {
			buffer_put(kbd->key);
			free(kbd);
		}
	}
}

int main (int argc, char ** argv) {
	if (argc < 3) {
		printf("usage: %s width height\n", argv[0]);
		return -1;
	}

	int width = atoi(argv[1]);
	int height = atoi(argv[2]);

	win_width = width;
	win_height = height;

	setup_windowing();

	FT_Init_FreeType(&library);
	_loadDejavu();

	/* Do something with a window */
	window_t * wina = window_create(0,0, width, height);
	assert(wina);
	ctx = init_graphics_window_double_buffer(wina);
	draw_fill(ctx, rgb(0,0,0));
#if 0
	window_redraw_full(wina);
#endif

#if 1
	printf("Loading background...\n");
	init_sprite(0, "/usr/share/login-background.bmp", NULL);
	printf("Background loaded.\n");
	draw_sprite_scaled(ctx,sprites[0], 0, 0, width, height);
#endif

	init_sprite(1, "/usr/share/bs.bmp", "/usr/share/bs-alpha.bmp");
	draw_sprite_scaled(ctx, sprites[1], center_x(sprites[1]->width), center_y(sprites[1]->height), sprites[1]->width, sprites[1]->height);

	flip(ctx);

	size_t buf_size = wina->width * wina->height * sizeof(uint32_t);
	char * buf = malloc(buf_size);
	memcpy(buf, wina->buffer, buf_size);

	uint32_t i = 0;

	pthread_t input_thread;
	pthread_create(&input_thread, NULL, process_input, NULL);

	while (1) {

		double scale = 2.0 + 1.5 * sin((double)i * 0.02);

#if 1
		/* Redraw the background by memcpy (super speedy) */
		memcpy(ctx->backbuffer, buf, buf_size);

		draw_string(50 + scale * 30,50 + scale * 30, rgb(255,0,0), input_buffer);


		flip(ctx);
		window_redraw_full(wina);
#endif

		++i;
	}

	//window_destroy(window); // (will close on exit)
	teardown_windowing();

	return 0;
}
