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

/*
 * Some of the system calls for the graphics
 * functionality.
 */
#if 0
DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL1(kbd_mode, 12, int);
DEFN_SYSCALL0(kbd_get, 13);
DEFN_SYSCALL1(setgraphicsoffset, 16, int);

DEFN_SYSCALL0(getgraphicswidth,  18);
DEFN_SYSCALL0(getgraphicsheight, 19);
DEFN_SYSCALL0(getgraphicsdepth,  20);
#endif

uint16_t graphics_width  = 0;
uint16_t graphics_height = 0;
uint16_t graphics_depth  = 0;

#define GFX_W  graphics_width /* Display width */
#define GFX_H  graphics_height  /* Display height */
#define GFX_B  (graphics_depth / 8)    /* Display byte depth */

/*
 * Macros make verything easier.
 */
#define GFX(x,y) *((uint32_t *)&gfx_mem[(GFX_W * (y) + (x)) * GFX_B])
#define SPRITE(sprite,x,y) sprite->bitmap[sprite->width * (y) + (x)]

/* Pointer to graphics memory */
uint8_t * gfx_mem;

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
		GFX(xpt, ypt) = 0;
	} else {
		GFX(xpt, ypt) = colors[color];
	}
	newcolor = color;
}

int main(int argc, char ** argv) {
	graphics_width  = syscall_getgraphicswidth();
	graphics_height = syscall_getgraphicsheight();
	graphics_depth  = syscall_getgraphicsdepth();
	gfx_mem = (void *)syscall_getgraphicsaddress();

	if (argc > 1) {
		/* Read some arguments */
		int index, c;
		while ((c = getopt(argc, argv, "ni:x:X:c:C:")) != -1) {
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
				default:
					break;
			}
		}
	}
	printf("initer: %f\n", initer);
	printf("X: %f %f\n", Minx, Maxx);
	float _x = Maxx - Minx;
	float _y = _x / GFX_W * GFX_H;
	Miny = 0 - _y / 2;
	Maxy = _y / 2;
	printf("Y: %f %f\n", Miny, Maxy);
	printf("conx: %f cony: %f\n", conx, cony);

	printf("\033[J\n");

	pixcorx = (Maxx - Minx) / GFX_W;
	pixcory = (Maxy - Miny) / GFX_H;
	int j = 0;
	do {
		int i = 1;
		do {
			julia(i,j);
			if (lastcolor != newcolor) julia(i-1,j);
			else if (i > 0) GFX(i-1,j) = colors[lastcolor];
			newcolor = lastcolor;
			i+= 2;
		} while ( i < GFX_W );
		++j;
	} while ( j < GFX_H );

	syscall_kbd_mode(1);

	int playing = 1;
	while (playing) {
		char ch = 0;
		ch = syscall_kbd_get();
		switch (ch) {
			case 16:
				playing = 0;
				break;
			default:
				break;
		}
	}

	syscall_kbd_mode(0);

	return 0;
}
