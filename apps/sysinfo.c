/**
 * @brief Display system information.
 *
 * Similar to tools like 'screenfetch', this displays information
 * about ToaruOS, the current machine state, and the user's
 * configuration options, alongside a terminal-safe rendition
 * of the OS's logo.
 *
 * This is a bit overcomplicated as we used to show an elaborate
 * logo from a BMP/PNG, but our current logo can be nicely
 * represented with block characters so that's kind moot; maybe
 * this should be simplified...
 *
 * Uses a few other utilities:
 *   hostname
 *   uname -sr
 *   uptime -p
 *   msk count
 *   sh -v
 *   yutani-query resolution
 *   font-tool -n
 *   cpu-name.krk
 *   free -ut
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2021 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <toaru/graphics.h>
#include <toaru/termemu.h>

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

static void reset(void) {
	printf("\033[0m");
}

static int term_is_toaru = 0;
static void foreground_color(uint32_t color) {
	printf(term_is_toaru ? "\033[38;6;%d;%d;%d;%dm" : "\033[38;2;%d;%d;%dm",
		(int)_RED(color), (int)_GRE(color), (int)_BLU(color), (int)_ALP(color));
}

static void background_color(uint32_t color) {
	printf(term_is_toaru ? "\033[48;6;%d;%d;%d;%dm" : "\033[48;2;%d;%d;%dm",
		(int)_RED(color), (int)_GRE(color), (int)_BLU(color), (int)_ALP(color));
}

int main(int argc, char * argv[]) {

	/* Prepare data */
	char * user = getenv("USER");
	char * wm_theme = getenv("WM_THEME");
	char * term = getenv("TERM");
	term_is_toaru = term && strstr(term,"toaru");

	int i = 0;

	prog_lines[i] = "hostname";
	sprintf(data_lines[i++], C_A "%s" C_O "@" C_A, user);

	prog_lines[i] = ". /etc/os-release; echo ${PRETTY_NAME}";
	sprintf(data_lines[i++], C_A "OS: " C_O);

	prog_lines[i] = "uname -sr";
	sprintf(data_lines[i++], C_A "Kernel: " C_O);

	prog_lines[i] = "uptime -p";
	sprintf(data_lines[i++], C_A "Uptime: " C_O);

	prog_lines[i] = "msk count";
	sprintf(data_lines[i++], C_A "Packages: " C_O);

	prog_lines[i] = "esh -v";
	sprintf(data_lines[i++], C_A "Shell: " C_O);

	prog_lines[i] = "yutani-query resolution";
	sprintf(data_lines[i++], C_A "Resolution: " C_O);

	/* no command */
	sprintf(data_lines[i++], C_A "WM: " C_O "Yutani");

	/* from environment */
	sprintf(data_lines[i++], C_A "WM Theme: " C_O "%s", wm_theme);

	prog_lines[i] = "font-tool -n";
	sprintf(data_lines[i++], C_A "Font: " C_O);

	prog_lines[i] = "cpu-name.krk";
	sprintf(data_lines[i++], C_A "CPU: " C_O);

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

			/* Are both cells transparent? */
			if (a_t == 0 && a_b == 0) {
				reset();
				printf(" ");
				continue;
			}

			if (a_b == 0) {
				reset();
				foreground_color(out);
				printf("▀");
			} else if (a_t == 0) {
				reset();
				foreground_color(back);
				printf("▄");
			} else {
				foreground_color(back);
				background_color(out);
				printf("▄");
			}
		}
		if (j < i) {
			print_thing(j);
			j++;
		} else {
			printf("\033[0m\n");
		}
	}

	while (j < i) {
		for (int x = 0; x < (int)gimp_image.width; x++) {
			printf(" ");
		}
		print_thing(j);
		j++;
	}
}
