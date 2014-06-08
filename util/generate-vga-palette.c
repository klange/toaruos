#include <stdio.h>
#include <stdint.h>

#include "../userspace/gui/terminal/terminal-palette.h"

static int abs(int a) {
	return a > 0 ? a : -a;
}

static int color_distance(uint32_t a, uint32_t b) {
	int a_r = (a & 0xFF0000) >> 16;
	int a_g = (a & 0xFF00) >> 8;
	int a_b = (a & 0xFF);

	int b_r = (b & 0xFF0000) >> 16;
	int b_g = (b & 0xFF00) >> 8;
	int b_b = (b & 0xFF);

	int distance = 0;
	distance += abs(a_r - b_r) * 3;
	distance += abs(a_g - b_g) * 6;
	distance += abs(a_b - b_b) * 10;

	return distance;
}

static uint32_t vga_base_colors[] = {
	0x000000,
	0xAA0000,
	0x00AA00,
	0xAA5500,
	0x0000AA,
	0xAA00AA,
	0x00AAAA,
	0xAAAAAA,
	0x555555,
	0xFF5555,
	0x55AA55,
	0xFFFF55,
	0x5555FF,
	0xFF55FF,
	0x55FFFF,
	0xFFFFFF,
};

static int is_gray(uint32_t a) {
	int a_r = (a & 0xFF0000) >> 16;
	int a_g = (a & 0xFF00) >> 8;
	int a_b = (a & 0xFF);

	return (a_r == a_g && a_g == a_b);
}

int main(int argc, char * argv[]) {
	printf("#define PALETTE_COLORS 256\n");
	printf("uint32_t vga_colors[PALETTE_COLORS] = {\n");
	for (int i = 0; i < 16; ++i) {
		printf("\t0x%x,\n", i);
	}
	for (int i = 16; i < 256; ++i) {
		int best_distance = INT32_MAX;
		int best_index = 0;
		for (int j = 0; j < 16; ++j) {
			if (is_gray(term_colors[i]) && !is_gray(vga_base_colors[j]));
			int distance = color_distance(term_colors[i], vga_base_colors[j]);
			if (distance < best_distance) {
				best_index = j;
				best_distance = distance;
			}
		}
		printf("\t0x%x, /* #%06x -> #%06x */\n", best_index, term_colors[i], vga_base_colors[best_index]);
	}
	printf("};\n");
	return 0;
}

