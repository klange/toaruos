/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Wallpaper renderer.
 *
 */
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#define PNG_DEBUG 3
#include <png.h>

#include "lib/window.h"
#include "lib/graphics.h"

sprite_t * sprites[128];
sprite_t alpha_tmp;

uint16_t win_width;
uint16_t win_height;
gfx_context_t * ctx;

int center_x(int x) {
	return (win_width - x) / 2;
}

int center_y(int y) {
	return (win_height - y) / 2;
}

int x, y;

png_uint_32 width, height;
int color_type;
int bit_depth;

png_structp png_ptr;
png_infop info_ptr;
int number_of_passes;
png_bytep * row_pointers;

int read_png(char * file) {
	char header[8];
	FILE *fp = fopen(file, "rb");
	if (!fp) {
		printf("Oh dear. Failed to open wallpaper file.\n");
		return 1;
	}
	fread(header, 1, 8, fp);
	if (png_sig_cmp(header, 0, 8)) {
		printf("Oh dear. Bad signature.\n");
		return 1;
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		printf("Oh dear. Couldn't make a read struct.\n");
		return 1;
	}
	info_ptr = png_create_info_struct(png_ptr);

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

	row_pointers = (png_bytep*) malloc(sizeof(png_bytep) * height);
	for (y = 0; y < height; ++y) {
		row_pointers[y] = (png_byte *) malloc(png_get_rowbytes(png_ptr, info_ptr));
	}
	png_read_image(png_ptr, row_pointers);
	fclose(fp);

	sprites[0] = malloc(sizeof(sprite_t));

	sprite_t * sprite = sprites[0];
	sprite->width = width;
	sprite->height = height;
	sprite->bitmap = malloc(sizeof(uint32_t) * width * height);
	sprite->alpha = 0;
	sprite->blank = 0;

	for (y = 0; y < height; ++y) {
		png_byte* row = row_pointers[y];
		for (x = 0; x < width; ++x) {
			png_byte * ptr = &(row[x*3]);
			sprite->bitmap[(y) * width + x] = rgb(ptr[0], ptr[1], ptr[2]);
		}
	}

	return 0;
}

int main (int argc, char ** argv) {
	setup_windowing();

	int width  = wins_globals->server_width;
	int height = wins_globals->server_height;

	win_width = width;
	win_height = height;

	/* Do something with a window */
	window_t * wina = window_create(0,0, width, height);
	assert(wina);
	window_reorder (wina, 0);
	ctx = init_graphics_window_double_buffer(wina);
	draw_fill(ctx, rgb(127,127,127));
	flip(ctx);

	if (read_png("/usr/share/wallpaper.png")) {
		return 0;
	}
	draw_sprite_scaled(ctx, sprites[0], 0, 0, width, height);

	flip(ctx);

	while (1) {
		syscall_yield();
		//syscall_wait(wins_globals->server_pid);
	}


#if 0
	window_destroy(window); // (will close on exit)
	teardown_windowing();
#endif

	return 0;
}
