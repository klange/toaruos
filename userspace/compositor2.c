#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "lib/graphics.c"

#define BUFW   800
#define BUFH   800
#define BUFD   4
#define SIZE   BUFW * BUFH * BUFD
#define WIDTH  "800"
#define HEIGHT "800"
#define DEPTH  "32"

#if 0
uint16_t graphics_width  = 0;
uint16_t graphics_height = 0;
uint16_t graphics_depth  = 0;

#define GFX_W  graphics_width
#define GFX_H  graphics_height
#define GFX_D  graphics_depth
#define GFX_B  (graphics_depth / 8)    /* Display byte depth */
#define GFX(x,y) *((uint32_t *)&frame_mem[(GFX_W * (y) + (x)) * GFX_B])
#endif
#define BUF(buf,x,y) *((uint32_t *)&buf[(BUFW * (y) + (x)) * BUFD])

/*
DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL1(setgraphicsoffset, 16, int);
DEFN_SYSCALL0(getgraphicswidth,  18);
DEFN_SYSCALL0(getgraphicsheight, 19);
DEFN_SYSCALL0(getgraphicsdepth,  20);
*/
DEFN_SYSCALL2(shm_obtain, 35, char *, size_t *)
DEFN_SYSCALL1(shm_release, 36, char *)


void composite (char * buf, uint16_t x_off, uint16_t y_off) {
	for (int y = 0; y < BUFH; y++) {
		int ytrue = y + y_off;
		if (ytrue > GFX_H) {
			break;
		}

		for (int x = 0; x < BUFW; x++) {
			int xtrue = x + x_off;
			if (xtrue > GFX_W) {
				break;
			}

			GFX(xtrue,ytrue) = BUF(buf,x,y);
		}
	}
}


int main (int argc, char ** argv) {

/*	graphics_width  = syscall_getgraphicswidth();
	graphics_height = syscall_getgraphicsheight();
	graphics_depth  = syscall_getgraphicsdepth();
	int map_x = GFX_W / 2 - (64 * 9) / 2;
	int map_y = GFX_H / 2 - (64 * 9) / 2;
	int flip_offset = GFX_H;
	gfx_mem = (void *)syscall_getgraphicsaddress();
	frame_mem = (void *)((uintptr_t)gfx_mem + sizeof(uint32_t) * GFX_W * GFX_H);*/

	init_graphics_double_buffer();

	char * julia_window_key = "julia2.windowbuffer";
	char * game_window_key = "game2.windowbuffer";

	size_t size = SIZE;
	char * julia_window = (char *)syscall_shm_obtain(julia_window_key, &size);
	char * game_window = (char *)syscall_shm_obtain(game_window_key, &size);
	memset(julia_window, 0, size);
	memset(game_window, 0, size);

	/* Fork off two children */
	if (!fork()) {
		char * args[] = {"/bin/julia2", WIDTH, HEIGHT, DEPTH, julia_window_key, NULL};
		execve(args[0], args, NULL);
	}
	if (!fork()) {
		char * args[] = {"/bin/game2", WIDTH, HEIGHT, DEPTH, game_window_key, NULL};
		execve(args[0], args, NULL);
	}

	/* write loop */
	while (1) {
		composite(julia_window, 0, 0);
		composite(game_window, 100, 100);
		flip();
	}

	return 0;
}
