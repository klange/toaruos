/*
 * vim:tabstop=4
 * vim:noexpandtab
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>
#include <fs.h>

/* Friggin' frick, this should be a config option
 * because it's 4096 on some instances of Qemu,
 * ie the one on my laptop, but it's 2048 on
 * the EWS machines. */
#define BOCHS_BUFFER_SIZE 2048
#define PREFERRED_X 1024
#define PREFERRED_Y 768
#define PREFERRED_VY 4096
#define PREFERRED_B 32

uint16_t bochs_resolution_x = 0;
uint16_t bochs_resolution_y = 0;
uint16_t bochs_resolution_b = 0;

/*
 * Address of the linear frame buffer.
 * This can move, so it's a pointer instead of
 * #define.
 */
uint32_t * bochs_vid_memory = (uint32_t *)0xE0000000;

#define TERM_WIDTH 128
#define TERM_HEIGHT 64

static short csr_x = 0;
static short csr_y = 0;
static uint8_t * term_buffer;
static uint8_t current_fg = 7;
static uint8_t current_bg = 0;
static uint16_t current_scroll = 0;
static uint8_t cursor_on = 1;

void
bochs_set_y_offset(uint16_t y) {
	outports(0x1CE, 0x9);
	outports(0x1CF, y);
	current_scroll = y;
}

uint16_t
bochs_current_scroll() {
	return current_scroll;
}

uintptr_t
bochs_get_address() {
	return (uintptr_t)bochs_vid_memory;
}

typedef struct sprite {
	uint16_t width;
	uint16_t height;
	uint32_t * bitmap;
	uint32_t * masks;
	uint32_t blank;
	uint8_t  alpha;
} sprite_t;

sprite_t * wallpaper = NULL;

#define _RED(color) ((color & 0x00FF0000) / 0x10000)
#define _GRE(color) ((color & 0x0000FF00) / 0x100)
#define _BLU(color) ((color & 0x000000FF) / 0x1)

void
bochs_screenshot(char * filename) {
	if (filename) {
		kprintf("Error: Writing screenshots to a file is not currently supported.\n");
		return;
	}
	uint8_t * buf = malloc(1024 * 768 * 4);
	uint32_t * bufi = (uint32_t *)buf;
	uint32_t x, y, i;
	for (x = 0; x < 1024; ++x) {
		for (y = 0; y < 768; ++y) {
			uint32_t color = bochs_vid_memory[((y + current_scroll) * bochs_resolution_x + x)];
			bufi[y * 1024 + x] = _BLU(color) * 0x10000 +
								 _GRE(color) * 0x100 +
								 _RED(color) * 0x1 +
								 0xFF000000;
		}
	}
	i = 0;
	while (i < 6144) {
		ide_write_sector(0x170, 0, i, (uint8_t *)((uint32_t)buf + i * 512));
		++i;
		PAUSE;
	}
	free(buf);
}

void
bochs_install_wallpaper(char * filename) {
	kprintf("Starting up...\n");
	fs_node_t * image = kopen(filename, 0);
	if (!image) {
		kprintf("[NOTICE] Failed to load wallpaper `%s`.\n", filename);
		return;
	}
	size_t image_size= 0;

	image_size = image->length;

	/* Alright, we have the length */
	char * bufferb = malloc(image_size);
	read_fs(image, 0, image_size, (uint8_t *)bufferb);
	close_fs(image);

	uint16_t x = 0; /* -> 212 */
	uint16_t y = 0; /* -> 68 */
	/* Get the width / height of the image */
	signed int *bufferi = (signed int *)((uintptr_t)bufferb + 2);
	uint32_t width  = bufferi[4];
	uint32_t height = bufferi[5];
	uint16_t bpp    = bufferi[6] / 0x10000;
	uint32_t row_width = (bpp * width + 31) / 32 * 4;
	/* Skip right to the important part */
	size_t i = bufferi[2];

	wallpaper = malloc(sizeof(sprite_t));
	wallpaper->width = width;
	wallpaper->height = height;
	wallpaper->bitmap = malloc(sizeof(uint32_t) * width * height);

	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			/* Extract the color */
			uint32_t color;
			if (bpp == 24) {
				color =	bufferb[i   + 3 * x] +
						bufferb[i+1 + 3 * x] * 0x100 +
						bufferb[i+2 + 3 * x] * 0x10000;
			} else if (bpp == 32) {
				color =	bufferb[i   + 4 * x] * 0x1000000 +
						bufferb[i+1 + 4 * x] * 0x100 +
						bufferb[i+2 + 4 * x] * 0x10000 +
						bufferb[i+3 + 4 * x] * 0x1;
			}
			/* Set our point */
			wallpaper->bitmap[(height - y - 1) * width + x] = color;
		}
		i += row_width;
	}

	free(bufferb);
}

void
graphics_install_bochs() {
	outports(0x1CE, 0x00);
	uint16_t i = inports(0x1CF);
	if (i < 0xB0C0 || i > 0xB0C6) {
		return;
	}
	outports(0x1CF, 0xB0C4);
	i = inports(0x1CF);
	/* Disable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x00);
	/* Set X resolution to 1024 */
	outports(0x1CE, 0x01);
	outports(0x1CF, PREFERRED_X);
	/* Set Y resolution to 768 */
	outports(0x1CE, 0x02);
	outports(0x1CF, PREFERRED_Y);
	/* Set bpp to 32 */
	outports(0x1CE, 0x03);
	outports(0x1CF, PREFERRED_B);
	/* Set Virtual Height to stuff */
	outports(0x1CE, 0x07);
	outports(0x1CF, PREFERRED_VY);
	/* Re-enable VBE */
	outports(0x1CE, 0x04);
	outports(0x1CF, 0x41);
	/* Herp derp */

	uint32_t * herp = (uint32_t *)0xA0000;
	herp[0] = 0xA5ADFACE;

	/* Enable the higher memory */
	for (uintptr_t i = 0xE0000000; i <= 0xE0FF0000; i += 0x1000) {
		dma_frame(get_page(i, 1, kernel_directory), 1, 0, i);
	}
	for (uintptr_t i = 0xF0000000; i <= 0xF0FF0000; i += 0x1000) {
		dma_frame(get_page(i, 1, kernel_directory), 1, 0, i);
	}

	/* Go find it */
	for (uintptr_t x = 0xE0000000; x < 0xE0FF0000; x += 0x1000) {
		if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
			bochs_vid_memory = (uint32_t *)x;
			goto mem_found;
		}
	}
	for (uintptr_t x = 0xF0000000; x < 0xF0FF0000; x += 0x1000) {
		if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
			bochs_vid_memory = (uint32_t *)x;
			goto mem_found;
		}
	}

mem_found:
	bochs_resolution_x = PREFERRED_X;
	bochs_resolution_y = PREFERRED_Y;
	bochs_resolution_b = PREFERRED_B;

	/* Buffer contains characters, fg (of 256), bg (same), flags (one byte) */
	term_buffer = (uint8_t *)malloc(sizeof(uint8_t) * 4 * TERM_WIDTH * TERM_HEIGHT);
}

static void
bochs_set_point(
		uint16_t x,
		uint16_t y,
		uint32_t color
		) {
	bochs_vid_memory[((y + current_scroll) * bochs_resolution_x + x)] = color;
}

static void
bochs_set_point_bg(
		uint16_t x,
		uint16_t y,
		uint32_t color
		) {
	if (!color && wallpaper) {
		bochs_vid_memory[((y + current_scroll) * bochs_resolution_x + x)] = wallpaper->bitmap[bochs_resolution_x * y + x];
	} else {
		bochs_vid_memory[((y + current_scroll) * bochs_resolution_x + x)] = color;
	}
}

void
bochs_scroll() {
	uint32_t size = sizeof(uint32_t) * bochs_resolution_x * (bochs_resolution_y - 12);
	memmove((void *)bochs_vid_memory, (void *)((uintptr_t)bochs_vid_memory + bochs_resolution_x * 12 * sizeof(uint32_t)), size);
}

void
bochs_draw_logo(char * filename) {
	/* This is slow and ineffecient, but it's also dead simple. */
	if (!bochs_resolution_x) { return; }
	fs_node_t * file = kopen(filename,0);
	if (!file) { return; }
	char *bufferb = malloc(file->length);
	/* Read the boot logo */
	size_t bytes_read = read_fs(file, 0, file->length, (uint8_t *)bufferb);
	uint16_t x = 0; /* -> 212 */
	uint16_t y = 0; /* -> 68 */
	/* Get the width / height of the image */
	signed int *bufferi = (signed int *)((uintptr_t)bufferb + 2);
	uint32_t width = bufferi[4];
	uint32_t height = bufferi[5];
	uint32_t row_width = (24 * width + 31) / 32 * 4;
	/* Skip right to the important part */
	size_t i = bufferi[2];
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			if (i > bytes_read) return;
			/* Extract the color */
			uint32_t color =	bufferb[i   + 3 * x] +
								bufferb[i+1 + 3 * x] * 0x100 +
								bufferb[i+2 + 3 * x] * 0x10000;
			/* Set our point */
			bochs_set_point((bochs_resolution_x - width) / 2 + x, (bochs_resolution_y - height) / 2 + (height - y), color);
		}
		i += row_width;
	}
	free(bufferb);
}

void
bochs_fill_rect(
		uint16_t x,
		uint16_t y,
		uint16_t w,
		uint16_t h,
		uint32_t color
		) {
	for (uint16_t i = y; i < h + y; ++i) {
		for (uint16_t j = x; j < w + x; ++j) {
			bochs_set_point(j,i,color);
		}
	}
}

void
bochs_write_char(
		uint8_t val,
		uint16_t x,
		uint16_t y,
		uint32_t fg,
		uint32_t bg
		) {
	if (val > 131) {
		val = ' ';
	}
	uint8_t * c = number_font[val];
	for (uint8_t i = 0; i < 12; ++i) {
		if (c[i] & 0x80) { bochs_set_point(x,y+i,fg);   } else { bochs_set_point_bg(x,y+i,bg); }
		if (c[i] & 0x40) { bochs_set_point(x+1,y+i,fg); } else { bochs_set_point_bg(x+1,y+i,bg); }
		if (c[i] & 0x20) { bochs_set_point(x+2,y+i,fg); } else { bochs_set_point_bg(x+2,y+i,bg); }
		if (c[i] & 0x10) { bochs_set_point(x+3,y+i,fg); } else { bochs_set_point_bg(x+3,y+i,bg); }
		if (c[i] & 0x08) { bochs_set_point(x+4,y+i,fg); } else { bochs_set_point_bg(x+4,y+i,bg); }
		if (c[i] & 0x04) { bochs_set_point(x+5,y+i,fg); } else { bochs_set_point_bg(x+5,y+i,bg); }
		if (c[i] & 0x02) { bochs_set_point(x+6,y+i,fg); } else { bochs_set_point_bg(x+6,y+i,bg); }
		if (c[i] & 0x01) { bochs_set_point(x+7,y+i,fg); } else { bochs_set_point_bg(x+7,y+i,bg); }
	}
}

/* This is mapped to ANSI */
uint32_t bochs_colors[256] = {
	/* black  */ 0x000000,
	/* red    */ 0xcc0000,
	/* green  */ 0x3e9a06,
	/* brown  */ 0xc4a000,
	/* navy   */ 0x3465a4,
	/* purple */ 0x75507b,
	/* d cyan */ 0x06989a,
	/* gray   */ 0xeeeeec,
	/* d gray */ 0x555753,
	/* red    */ 0xef2929,
	/* green  */ 0x8ae234,
	/* yellow */ 0xfce94f,
	/* blue   */ 0x729fcf,
	/* magenta*/ 0xad7fa8,
	/* cyan   */ 0x34e2e2,
	/* white  */ 0xFFFFFF,
				 0x000000,
				 0x00005f,
				 0x000087,
				 0x0000af,
				 0x0000d7,
				 0x0000ff,
				 0x005f00,
				 0x005f5f,
				 0x005f87,
				 0x005faf,
				 0x005fd7,
				 0x005fff,
				 0x008700,
				 0x00875f,
				 0x008787,
				 0x0087af,
				 0x0087d7,
				 0x0087ff,
				 0x00af00,
				 0x00af5f,
				 0x00af87,
				 0x00afaf,
				 0x00afd7,
				 0x00afff,
				 0x00d700,
				 0x00d75f,
				 0x00d787,
				 0x00d7af,
				 0x00d7d7,
				 0x00d7ff,
				 0x00ff00,
				 0x00ff5f,
				 0x00ff87,
				 0x00ffaf,
				 0x00ffd7,
				 0x00ffff,
				 0x5f0000,
				 0x5f005f,
				 0x5f0087,
				 0x5f00af,
				 0x5f00d7,
				 0x5f00ff,
				 0x5f5f00,
				 0x5f5f5f,
				 0x5f5f87,
				 0x5f5faf,
				 0x5f5fd7,
				 0x5f5fff,
				 0x5f8700,
				 0x5f875f,
				 0x5f8787,
				 0x5f87af,
				 0x5f87d7,
				 0x5f87ff,
				 0x5faf00,
				 0x5faf5f,
				 0x5faf87,
				 0x5fafaf,
				 0x5fafd7,
				 0x5fafff,
				 0x5fd700,
				 0x5fd75f,
				 0x5fd787,
				 0x5fd7af,
				 0x5fd7d7,
				 0x5fd7ff,
				 0x5fff00,
				 0x5fff5f,
				 0x5fff87,
				 0x5fffaf,
				 0x5fffd7,
				 0x5fffff,
				 0x870000,
				 0x87005f,
				 0x870087,
				 0x8700af,
				 0x8700d7,
				 0x8700ff,
				 0x875f00,
				 0x875f5f,
				 0x875f87,
				 0x875faf,
				 0x875fd7,
				 0x875fff,
				 0x878700,
				 0x87875f,
				 0x878787,
				 0x8787af,
				 0x8787d7,
				 0x8787ff,
				 0x87af00,
				 0x87af5f,
				 0x87af87,
				 0x87afaf,
				 0x87afd7,
				 0x87afff,
				 0x87d700,
				 0x87d75f,
				 0x87d787,
				 0x87d7af,
				 0x87d7d7,
				 0x87d7ff,
				 0x87ff00,
				 0x87ff5f,
				 0x87ff87,
				 0x87ffaf,
				 0x87ffd7,
				 0x87ffff,
				 0xaf0000,
				 0xaf005f,
				 0xaf0087,
				 0xaf00af,
				 0xaf00d7,
				 0xaf00ff,
				 0xaf5f00,
				 0xaf5f5f,
				 0xaf5f87,
				 0xaf5faf,
				 0xaf5fd7,
				 0xaf5fff,
				 0xaf8700,
				 0xaf875f,
				 0xaf8787,
				 0xaf87af,
				 0xaf87d7,
				 0xaf87ff,
				 0xafaf00,
				 0xafaf5f,
				 0xafaf87,
				 0xafafaf,
				 0xafafd7,
				 0xafafff,
				 0xafd700,
				 0xafd75f,
				 0xafd787,
				 0xafd7af,
				 0xafd7d7,
				 0xafd7ff,
				 0xafff00,
				 0xafff5f,
				 0xafff87,
				 0xafffaf,
				 0xafffd7,
				 0xafffff,
				 0xd70000,
				 0xd7005f,
				 0xd70087,
				 0xd700af,
				 0xd700d7,
				 0xd700ff,
				 0xd75f00,
				 0xd75f5f,
				 0xd75f87,
				 0xd75faf,
				 0xd75fd7,
				 0xd75fff,
				 0xd78700,
				 0xd7875f,
				 0xd78787,
				 0xd787af,
				 0xd787d7,
				 0xd787ff,
				 0xd7af00,
				 0xd7af5f,
				 0xd7af87,
				 0xd7afaf,
				 0xd7afd7,
				 0xd7afff,
				 0xd7d700,
				 0xd7d75f,
				 0xd7d787,
				 0xd7d7af,
				 0xd7d7d7,
				 0xd7d7ff,
				 0xd7ff00,
				 0xd7ff5f,
				 0xd7ff87,
				 0xd7ffaf,
				 0xd7ffd7,
				 0xd7ffff,
				 0xff0000,
				 0xff005f,
				 0xff0087,
				 0xff00af,
				 0xff00d7,
				 0xff00ff,
				 0xff5f00,
				 0xff5f5f,
				 0xff5f87,
				 0xff5faf,
				 0xff5fd7,
				 0xff5fff,
				 0xff8700,
				 0xff875f,
				 0xff8787,
				 0xff87af,
				 0xff87d7,
				 0xff87ff,
				 0xffaf00,
				 0xffaf5f,
				 0xffaf87,
				 0xffafaf,
				 0xffafd7,
				 0xffafff,
				 0xffd700,
				 0xffd75f,
				 0xffd787,
				 0xffd7af,
				 0xffd7d7,
				 0xffd7ff,
				 0xffff00,
				 0xffff5f,
				 0xffff87,
				 0xffffaf,
				 0xffffd7,
				 0xffffff,
				 0x080808,
				 0x121212,
				 0x1c1c1c,
				 0x262626,
				 0x303030,
				 0x3a3a3a,
				 0x444444,
				 0x4e4e4e,
				 0x585858,
				 0x626262,
				 0x6c6c6c,
				 0x767676,
				 0x808080,
				 0x8a8a8a,
				 0x949494,
				 0x9e9e9e,
				 0xa8a8a8,
				 0xb2b2b2,
				 0xbcbcbc,
				 0xc6c6c6,
				 0xd0d0d0,
				 0xdadada,
				 0xe4e4e4,
				 0xeeeeee,
};

static void cell_set(uint16_t x, uint16_t y, uint8_t c, uint8_t fg, uint8_t bg, uint8_t flags) {
	uint8_t * cell = (uint8_t *)((uintptr_t)term_buffer + (y * TERM_WIDTH + x) * 4);
	cell[0] = c;
	cell[1] = fg;
	cell[2] = bg;
	cell[3] = flags;
}

static uint16_t cell_ch(uint16_t x, uint16_t y) {
	uint8_t * cell = (uint8_t *)((uintptr_t)term_buffer + (y * TERM_WIDTH + x) * 4);
	return cell[0];
}

static uint16_t cell_fg(uint16_t x, uint16_t y) {
	uint8_t * cell = (uint8_t *)((uintptr_t)term_buffer + (y * TERM_WIDTH + x) * 4);
	return cell[1];
}

static uint16_t cell_bg(uint16_t x, uint16_t y) {
	uint8_t * cell = (uint8_t *)((uintptr_t)term_buffer + (y * TERM_WIDTH + x) * 4);
	return cell[2];
}

static void cell_redraw(uint16_t x, uint16_t y) {
	uint8_t * cell = (uint8_t *)((uintptr_t)term_buffer + (y * TERM_WIDTH + x) * 4);
	bochs_write_char(cell[0], x * 8, y * 12, bochs_colors[cell[1]], bochs_colors[cell[2]]);
}

void bochs_redraw() {
	for (uint16_t y = 0; y < TERM_HEIGHT; ++y) {
		for (uint16_t x = 0; x < TERM_WIDTH; ++x) {
			cell_redraw(x,y);
		}
	}

}

void bochs_redraw_all() { 
	for (uint16_t y = 0; y < TERM_HEIGHT; ++y) {
		for (uint16_t x = 0; x < TERM_WIDTH; ++x) {
			cell_redraw(x,y);
		}
	}
}

void bochs_term_scroll() {
	/* Oh dear */
	/* I'd really prefer the much-bigger 4096 - 768 */
#if 0
	if (current_scroll + 12 >= BOCHS_BUFFER_SIZE - 768) {
		/* And here's where it gets hacky */
		uint32_t size = sizeof(uint32_t) * bochs_resolution_x * (bochs_resolution_y - 12);
		memcpy((void *)bochs_vid_memory, (void *)((uintptr_t)bochs_vid_memory + bochs_resolution_x * (current_scroll + 12) * sizeof(uint32_t)), size);
		bochs_set_y_offset(0);
	} else {
		bochs_set_y_offset(current_scroll + 12);
	}
#else
	for (uint16_t y = 0; y < TERM_HEIGHT - 1; ++y) {
		for (uint16_t x = 0; x < TERM_WIDTH; ++x) {
			cell_set(x,y,cell_ch(x,y+1),cell_fg(x,y+1),cell_bg(x,y+1), 0);
		}
	}
	for (uint16_t x = 0; x < TERM_WIDTH; ++x) {
		cell_set(x, TERM_HEIGHT-1,' ',current_fg, current_bg,0);
		//cell_redraw(x, TERM_HEIGHT-1);
	}
	bochs_redraw_all();
#endif
}

void bochs_term_clear() {
	/* Oh dear */
	csr_x = 0;
	csr_y = 0;
	memset((void *)term_buffer, 0x00,TERM_WIDTH * TERM_HEIGHT * sizeof(uint8_t) * 4);
	memset((void *)bochs_vid_memory, 0x00, sizeof(uint32_t) * bochs_resolution_x * bochs_resolution_y);
	bochs_set_y_offset(0);
	bochs_redraw_all();
}

void bochs_set_colors(uint8_t fg, uint8_t bg) {
	current_fg = fg;
	current_bg = bg;
}

void bochs_reset_colors() {
	current_fg = 7;
	current_bg = 0;
}

void draw_cursor() {
	if (!cursor_on) return;
	for (uint32_t x = 0; x < 8; ++x) {
		bochs_set_point(csr_x * 8 + x, csr_y * 12 + 11, bochs_colors[current_fg]);
	}
}

void bochs_redraw_cursor() {
	if (term_buffer) {
		draw_cursor();
	}
}

void bochs_write(char c) {
	cell_redraw(csr_x, csr_y);
	if (c == '\n') {
		for (uint16_t i = csr_x; i < TERM_WIDTH; ++i) {
			/* I like this behaviour */
			cell_set(i, csr_y, ' ',current_fg, current_bg, 0);
			cell_redraw(i, csr_y);
		}
		csr_x = 0;
		++csr_y;
	} else if (c == '\r') {
		cell_redraw(csr_x,csr_y);
		csr_x = 0;
	} else if (c == '\b') {
		--csr_x;
		cell_set(csr_x, csr_y, ' ',current_fg, current_bg, 0);
		cell_redraw(csr_x, csr_y);
	} else if (c == '\t') {
		csr_x = (csr_x + 8) & ~(8 - 1);
	} else {
		cell_set(csr_x,csr_y, c, current_fg, current_bg, 0);
		cell_redraw(csr_x,csr_y);
		csr_x++;
	}
	if (csr_x == TERM_WIDTH) {
		csr_x = 0;
		++csr_y;
	}
	if (csr_y == TERM_HEIGHT) {
		bochs_term_scroll();
		csr_y = TERM_HEIGHT - 1;
	}
	draw_cursor();
}


void bochs_draw_line(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		bochs_set_point(x0, y0, color);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * error;
		if (e2 > -deltay) {
			error -= deltay;
			x0 += sx;
		}
		if (e2 < deltax) {
			error += deltax;
			y0 += sy;
		}
	}
}

void
bochs_set_csr(int x, int y) {
	cell_redraw(csr_x,csr_y);
	csr_x = x;
	csr_y = y;
}

int
bochs_get_csr_x() {
	return csr_x;
}

int
bochs_get_csr_y() {
	return csr_y;
}

void
bochs_set_csr_show(uint8_t on) {
	cursor_on = on;
}

int
bochs_get_width() {
	return bochs_resolution_x / 8;
}

int
bochs_get_height() {
	return bochs_resolution_y / 12;
}

void
bochs_set_cell(int x, int y, char c) {
	cell_set(x, y, c, current_fg, current_bg, 0);
	cell_redraw(x, y);
}

void bochs_redraw_cell(int x, int y) {
	if (x < 0 || y < 0 || x >= TERM_WIDTH || y >= TERM_HEIGHT) return;
	cell_redraw(x,y);
}
