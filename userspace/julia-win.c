/*
 * Julia Fractal Generator
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include "lib/window.h"
#include "lib/graphics.h"
#include "lib/decorations.h"

/*
 * Macros make verything easier.
 */
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]

#define GFX_(xpt, ypt) ((uint32_t *)window->buffer)[DIRECT_OFFSET(xpt+decor_left_width,ypt+decor_top_height)]

/* Pointer to graphics memory */
window_t * window = NULL;

/* Julia fractals elements */
float conx = -0.74;  /* real part of c */
float cony = 0.1;    /* imag part of c */
float Maxx = 2;      /* X bounds */
float Minx = -2;
float Maxy = 1;      /* Y bounds */
float Miny = -1;
float initer = 1000; /* Iteration levels */
float pixcorx;       /* Internal values */
float pixcory;

int newcolor;        /* Color we're placing */
int lastcolor;       /* Last color we placed */
int no_repeat = 0;   /* Repeat colors? */

/*
 * Color table
 * These are orange/red shades from the Ubuntu platte.
 */
int colors[] = {
	0xeec73e,
	0xf0a513,
	0xfb8b00,
	0xf44800,
	0xffff99,
	0xffff00,
	0xfdca01,
	0x986601,
	0xf44800,
	0xfd3301,
	0xd40000,
	0x980101,
};

void julia(int xpt, int ypt) {
	long double x = xpt * pixcorx + Minx;
	long double y = Maxy - ypt * pixcory;
	long double xnew = 0;
	long double ynew = 0;

	int k = 0;
	for (k = 0; k <= initer; k++) {
		xnew = x * x - y * y + conx;
		ynew = 2 * x * y     + cony;
		x    = xnew;
		y    = ynew;
		if ((x * x + y * y) > 4)
			break;
	}

	int color;
	if (no_repeat) {
		color = 12 * k / initer;
	} else {
		color = k;
		if (color > 11) {
			color = color % 12;
		}
	}
	if (k >= initer) {
		GFX_(xpt,ypt) = 0;
	} else {
		GFX_(xpt,ypt) = colors[color];
	}
	newcolor = color;
}

void usage(char * argv[]) {
	printf(
			"Julia fractal generator.\n"
			"\n"
			"usage: %s [-n] [-i \033[3miniter\033[0m] [-x \033[3mminx\033[0m] \n"
			"          [-X \033[3mmaxx\033[0m] [-c \033[3mconx\033[0m] [-C \033[3mcony\033[0m]\n"
			"          [-W \033[3mwidth\033[0m] [-H \033[3mheight\033[0m] [-h]\n"
			"\n"
			" -n --no-repeat \033[3mDo not repeat colors\033[0m\n"
			" -i --initer    \033[3mInitializer value\033[0m\n"
			" -x --minx      \033[3mMinimum X value\033[0m\n"
			" -X --maxx      \033[3mMaximum X value\033[0m\n"
			" -c --conx      \033[3mcon x\033[0m\n"
			" -C --cony      \033[3mcon y\033[0m\n"
			" -W --width     \033[3mWindow width\033[0m\n"
			" -H --height    \033[3mWindow height\033[0m\n"
			" -h --help      \033[3mShow this help message.\033[0m\n",
			argv[0]);
}

int main(int argc, char * argv[]) {
	int left   = 40;
	int top    = 40;
	int width  = 300;
	int height = 300;

	static struct option long_opts[] = {
		{"no-repeat", no_argument,    0, 'n'},
		{"initer", required_argument, 0, 'i'},
		{"minx",   required_argument, 0, 'x'},
		{"maxx",   required_argument, 0, 'X'},
		{"conx",   required_argument, 0, 'c'},
		{"cony",   required_argument, 0, 'C'},
		{"width",  required_argument, 0, 'W'},
		{"height", required_argument, 0, 'H'},
		{"help",   no_argument,       0, 'h'},
		{0,0,0,0}
	};

	if (argc > 1) {
		/* Read some arguments */
		int index, c;
		while ((c = getopt_long(argc, argv, "ni:x:X:c:C:W:H:h", long_opts, &index)) != -1) {
			if (!c) {
				if (long_opts[index].flag == 0) {
					c = long_opts[index].val;
				}
			}
			switch (c) {
				case 'n':
					no_repeat = 1;
					break;
				case 'i':
					initer = atof(optarg);
					break;
				case 'x':
					Minx = atof(optarg);
					break;
				case 'X':
					Maxx = atof(optarg);
					break;
				case 'c':
					conx = atof(optarg);
					break;
				case 'C':
					cony = atof(optarg);
					break;
				case 'W':
					width = atoi(optarg);
					break;
				case 'H':
					height = atoi(optarg);
					break;
				case 'h':
					usage(argv);
					exit(0);
					break;
				default:
					break;
			}
		}
	}

	setup_windowing();

	window = window_create(left, top, width + decor_width(), height + decor_height());
	//window_fill(window, rgb(127,127,127));
	init_decorations();
	render_decorations(window, window->buffer, "Julia Fractals");

	printf("initer: %f\n", initer);
	printf("X: %f %f\n", Minx, Maxx);
	float _x = Maxx - Minx;
	float _y = _x / width * height;
	Miny = 0 - _y / 2;
	Maxy = _y / 2;
	printf("Y: %f %f\n", Miny, Maxy);
	printf("conx: %f cony: %f\n", conx, cony);

	pixcorx = (Maxx - Minx) / width;
	pixcory = (Maxy - Miny) / height;
	int j = 0;
	do {
		int i = 1;
		do {
			julia(i,j);
			if (lastcolor != newcolor) julia(i-1,j);
			else if (i > 0) GFX_(i-1,j) = colors[lastcolor];
			newcolor = lastcolor;
			i+= 2;
		} while ( i < width );
		++j;
	} while ( j < height );

	window_redraw_wait(window);

	int playing = 1;
	while (playing) {
		char ch = 0;
		w_keyboard_t * kbd;
		do {
			kbd = poll_keyboard();
			if (kbd != NULL) {
				ch = kbd->key;
				free(kbd);
			}
		} while (kbd != NULL);

		switch (ch) {
			case 'q':
				playing = 0;
				break;
			default:
				break;
		}
		syscall_yield();
	}

	printf("Closing down Julia Fractal Generate\n");

	teardown_windowing();
	printf("Exiting...\n");

	return 0;
}
