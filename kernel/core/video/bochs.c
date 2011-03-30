/*
 * vim:tabstop=4
 * vim:noexpandtab
 *
 * Bochs VBE / QEMU vga=std Graphics Driver
 */

#include <system.h>
#include <fs.h>

#define PREFERRED_X 1024
#define PREFERRED_Y 768
#define PREFERRED_B 32

uint16_t bochs_resolution_x = 0;
uint16_t bochs_resolution_y = 0;
uint16_t bochs_resolution_b = 0;

uint32_t * BOCHS_VID_MEMORY = (uint32_t *)0xE0000000;

#define TERM_WIDTH 128
#define TERM_HEIGHT 64

static short csr_x = 0;
static short csr_y = 0;
static uint8_t * term_buffer;
static uint8_t current_fg = 7;
static uint8_t current_bg = 0;

void
graphics_install_bochs() {
	outports(0x1CE, 0x00);
	uint16_t i = inports(0x1CF);
	if (i < 0xB0C0 || i > 0xB0C6) {
		kprintf("[bochs] You are not a Bochs VBE pseudo-card!\n");
		kprintf("[bochs] 0x%x is totally wrong!\n", (unsigned int)i);
		return;
	}
	kprintf("[bochs] Successfully detected a Bochs VBE setup!\n");
	kprintf("[bochs] You are using QEMU or Bochs and I love you.\n");
	outports(0x1CF, 0xB0C4);
	i = inports(0x1CF);
	kprintf("[bochs] Enabling 1024x768x32 graphics mode!\n");
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
			BOCHS_VID_MEMORY = (uint32_t *)x;
			goto mem_found;
		}
	}
	for (uintptr_t x = 0xF0000000; x < 0xF0FF0000; x += 0x1000) {
		if (((uintptr_t *)x)[0] == 0xA5ADFACE) {
			BOCHS_VID_MEMORY = (uint32_t *)x;
			goto mem_found;
		}
	}

mem_found:
	kprintf("[bochs] Video memory paged and located.\n");

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
	BOCHS_VID_MEMORY[(y * bochs_resolution_x + x)] = color;
}

void
bochs_scroll() {
	__asm__ __volatile__ ("cli");
	uint32_t size = sizeof(uint32_t) * bochs_resolution_x * (bochs_resolution_y - 12);
	memmove((void *)BOCHS_VID_MEMORY, (void *)((uintptr_t)BOCHS_VID_MEMORY + bochs_resolution_x * 12 * sizeof(uint32_t)), size);
	__asm__ __volatile__ ("sti");
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
	uint8_t * c = number_font[val - 0x20];
	__asm__ __volatile__ ("cli");
	for (uint8_t i = 0; i < 12; ++i) {
		if (c[i] & 0x80) { bochs_set_point(x,y+i,fg);   } else { bochs_set_point(x,y+i,bg); }
		if (c[i] & 0x40) { bochs_set_point(x+1,y+i,fg); } else { bochs_set_point(x+1,y+i,bg); }
		if (c[i] & 0x20) { bochs_set_point(x+2,y+i,fg); } else { bochs_set_point(x+2,y+i,bg); }
		if (c[i] & 0x10) { bochs_set_point(x+3,y+i,fg); } else { bochs_set_point(x+3,y+i,bg); }
		if (c[i] & 0x08) { bochs_set_point(x+4,y+i,fg); } else { bochs_set_point(x+4,y+i,bg); }
		if (c[i] & 0x04) { bochs_set_point(x+5,y+i,fg); } else { bochs_set_point(x+5,y+i,bg); }
		if (c[i] & 0x02) { bochs_set_point(x+6,y+i,fg); } else { bochs_set_point(x+6,y+i,bg); }
		if (c[i] & 0x01) { bochs_set_point(x+7,y+i,fg); } else { bochs_set_point(x+7,y+i,bg); }
	}
	__asm__ __volatile__ ("sti");
}

/* This is mapped to ANSI */
uint32_t bochs_colors[16] = {
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
	/* white  */ 0xFFFFFF
};

static void cell_set(uint16_t x, uint16_t y, uint8_t c, uint8_t fg, uint8_t bg) {
	uint8_t * cell = (uint8_t *)((uintptr_t)term_buffer + (y * TERM_WIDTH + x) * 4);
	cell[0] = c;
	cell[1] = fg;
	cell[2] = bg;
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

void bochs_term_scroll() {
	/* Oh dear */
	bochs_scroll();
	for (uint16_t y = 0; y < TERM_HEIGHT - 1; ++y) {
		for (uint16_t x = 0; x < TERM_WIDTH; ++x) {
			cell_set(x,y,cell_ch(x,y+1),cell_fg(x,y+1),cell_bg(x,y+1));
		}
	}
	for (uint16_t x = 0; x < TERM_WIDTH; ++x) {
		cell_set(x, TERM_HEIGHT-1,' ',current_fg, current_bg);
		cell_redraw(x, TERM_HEIGHT-1);
	}
	//bochs_redraw();
}


void bochs_term_clear() {
	/* Oh dear */
	csr_x = 0;
	csr_y = 0;
	for (uint16_t y = 0; y < TERM_HEIGHT; ++y) {
		for (uint16_t x = 0; x < TERM_WIDTH; ++x) {
			cell_set(x,y,' ',current_fg, current_bg);
		}
	}
	bochs_redraw();
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
	for (uint32_t x = 0; x < 8; ++x) {
		bochs_set_point(csr_x * 8 + x, csr_y * 12 + 11, bochs_colors[current_fg]);
	}
}

void bochs_write(char c) {
	__asm__ __volatile__ ("cli");
	cell_redraw(csr_x, csr_y);
	if (c == '\n') {
		for (uint16_t i = csr_x; i < TERM_WIDTH; ++i) {
			/* I like this behaviour */
			cell_set(i, csr_y, ' ',current_fg, current_bg);
			cell_redraw(i, csr_y);
		}
		csr_x = 0;
		++csr_y;
	} else if (c == '\b') {
		--csr_x;
		cell_set(csr_x, csr_y, ' ',current_fg, current_bg);
		cell_redraw(csr_x, csr_y);
	} else {
		cell_set(csr_x,csr_y, c, current_fg, current_bg);
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
	__asm__ __volatile__ ("sti");
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
