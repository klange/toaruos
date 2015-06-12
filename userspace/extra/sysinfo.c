/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * sysinfo - visually based on screenfetch
 *
 * Displays system information in a visually-pleasing format.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>

#include "lib/graphics.h"

#include "gui/terminal/lib/termemu.h"
#include "toaru_logo.h"

#define NUM_DATA_LINES 30

char data_lines[NUM_DATA_LINES][100];
const char * prog_lines[NUM_DATA_LINES] = {NULL};

#define C_A "\033[34;1m"
#define C_O "\033[0m"

void print_thing(int j) {
	printf("\033[0m  %s", data_lines[j]);
	fflush(stdout);
	if (prog_lines[j]) {
		system(prog_lines[j]);
	} else {
		printf("\n");
	}
}

int main(int argc, char * argv[]) {

	/* Prepare data */
	char * user = getenv("USER");
	char * wm_theme = getenv("WM_THEME");

	int i = 0;
	prog_lines[i] = "hostname";
	sprintf(data_lines[i++], C_A "%s" C_O "@" C_A, user);
	sprintf(data_lines[i++], C_A "OS: " C_O "ToaruOS");
	prog_lines[i] = "uname -sr";
	sprintf(data_lines[i++], C_A "Kernel: " C_O);
	prog_lines[i] = "uptime -p";
	sprintf(data_lines[i++], C_A "Uptime: " C_O);
	//sprintf(data_lines[i++], C_A "Packages: " C_O "(hell if I know!)");
	prog_lines[i] = "sh -v";
	sprintf(data_lines[i++], C_A "Shell: " C_O);
	prog_lines[i] = "yutani-query -r";
	sprintf(data_lines[i++], C_A "Resolution: " C_O);
	sprintf(data_lines[i++], C_A "WM: " C_O "Yutani");
	sprintf(data_lines[i++], C_A "WM Theme: " C_O "%s", wm_theme);
	prog_lines[i] = "yutani-query -m";
	sprintf(data_lines[i++], C_A "Font: " C_O);
	//sprintf(data_lines[i++], C_A "CPU: " C_O "(query cpudet)");
	//sprintf(data_lines[i++], C_A "GPU: " C_O "(hell if I know!)");
	prog_lines[i] = "free -ut";
	sprintf(data_lines[i++], C_A "RAM: " C_O);

	int j = 0;
	for (unsigned int y = 0; y < gimp_image.height; y += 2) {
		for (unsigned int x = 0; x < gimp_image.width; x += 1) {
			unsigned char r_t = gimp_image.pixel_data[(x + y * gimp_image.width) * 4];
			unsigned char g_t = gimp_image.pixel_data[(x + y * gimp_image.width) * 4 + 1];
			unsigned char b_t = gimp_image.pixel_data[(x + y * gimp_image.width) * 4 + 2];
			unsigned char a_t = gimp_image.pixel_data[(x + y * gimp_image.width) * 4 + 3];
			unsigned char r_b = 0;
			unsigned char g_b = 0;
			unsigned char b_b = 0;
			unsigned char a_b = 0;
			if (y != gimp_image.height - 1) {
				r_b = gimp_image.pixel_data[(x + (y + 1) * gimp_image.width) * 4];
				g_b = gimp_image.pixel_data[(x + (y + 1) * gimp_image.width) * 4 + 1];
				b_b = gimp_image.pixel_data[(x + (y + 1) * gimp_image.width) * 4 + 2];
				a_b = gimp_image.pixel_data[(x + (y + 1) * gimp_image.width) * 4 + 3];
			}

			uint32_t out = alpha_blend_rgba(
				rgba(0,0,0,TERM_DEFAULT_OPAC),
				premultiply(rgba(r_t, g_t, b_t, a_t)));

			uint32_t back = alpha_blend_rgba(
				rgba(0,0,0,TERM_DEFAULT_OPAC),
				premultiply(rgba(r_b, g_b, b_b, a_b)));

			/* Print half block */
			printf("\033[38;6;%d;%d;%d;%dm\033[48;6;%d;%d;%d;%dm▄",
					_RED(back), _GRE(back), _BLU(back), _ALP(back),
					_RED(out), _GRE(out), _BLU(out), _ALP(out));

		}
		if (j < i) {
			print_thing(j);
			j++;
		} else {
			printf("\033[0m\n");
		}
	}

	while (j < i) {
		for (int x = 0; x < gimp_image.width; x++) {
			printf(" ");
		}
		print_thing(j);
		j++;
	}
}
