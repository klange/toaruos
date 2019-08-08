/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * imgviewer - Display bitmaps in a graphical window.
 *
 * This is probably the 4th time I've (re)written a version of
 * this application... This uses the libtoaru_graphics sprite
 * functionality to load images, so it will support whatever
 * that ends up supporting - which at the time of writing is
 * just bitmaps of various types.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/jpeg.h>

/* Pointer to graphics memory */
static yutani_t * yctx;
static yutani_window_t * window = NULL;
static gfx_context_t * ctx = NULL;

static int decor_left_width = 0;
static int decor_top_height = 0;
static int decor_right_width = 0;
static int decor_bottom_height = 0;
static int decor_width = 0;
static int decor_height = 0;

int left   = 40;
int top    = 40;
int width  = 300;
int height = 300;

sprite_t img = {0};

#define APPLICATION_TITLE "Image Viewer"
#define IMGVIEWER_VERSION   "1.4.0" 
#define IMGVIEWER_COPYRIGHT "Copyright 2012-2019 K. Lange <\033[3mklange@toaruos.org\033[23m>"

#define DEBUG 1

/**
 * Show help text for -?
 */
void usage(char * argv[]) {
#define _s "\033[3m"
#define _e "\033[0m\n"
	printf(
			"Image Viewer - Shows images.\n"
			"\n"
			"usage: %s [file image]\n"			
			"\n"
			" -?     " _s "show this help text" _e
			"\n"
			" --version     " _s "show apps version" _e
			"\n", argv[0]);
#undef _e
#undef _s
}

static void decors() {
	render_decorations(window, ctx, APPLICATION_TITLE);
}

void redraw() {

	static double r = 0.0;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			GFX(ctx,x+decor_left_width,y+decor_top_height) = (((y / 10) % 2 == 0) ^ ((x / 10) % 2 == 0)) ? rgb(107,107,107) : rgb(147,147,147);
		}
	}

	draw_sprite(ctx, &img, decor_left_width + width/2 - img.width/2, decor_top_height + height/2 - img.height/2);
	decors();
	flip(ctx);
	r += 0.02;
}

void resize_finish(int w, int h) {
	yutani_window_resize_accept(yctx, window, w, h);
	reinit_graphics_yutani(ctx, window);

	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);

	decor_left_width = bounds.left_width;
	decor_top_height = bounds.top_height;
	decor_right_width = bounds.right_width;
	decor_bottom_height = bounds.bottom_height;
	decor_width = bounds.width;
	decor_height = bounds.height;

	width  = w - decor_left_width - decor_right_width;
	height = h - decor_top_height - decor_bottom_height;

	redraw();
	yutani_window_resize_done(yctx, window);

	yutani_flip(yctx, window);
}

int file_exists(char * filename){
	return access(filename, F_OK);
}

int is_readable(char * filename){	

	if ( file_exists( filename) != 0 ){
		fprintf(stderr, "%s: not exist! error_info: %s\n", filename, strerror( errno ) );
		return 1;
	}
	
	FILE * tmp = fopen( filename, "r" );

	if ( tmp == NULL ) {
        fprintf(stderr, "%s: failed to open! error_info %s\n", filename, strerror( errno ) );		
		return 1;
    } 
	
	fclose( tmp );	
	return  0;
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "?:-:")) != -1) {
		switch (opt) {
			case '-':
				if (!strcmp(optarg,"version")) {				
					printf("imgviewer %s %s\n", IMGVIEWER_VERSION, IMGVIEWER_COPYRIGHT);				
					return 0;
				}
				break;
			case '?':
				usage(argv);
				return 0;
		}
	}
	/* Open file */
	if (argc > optind) {
		char *in_file = argv[optind];
		if ( file_exists ( in_file) == 0 ){
			if ( is_readable( in_file) == 0 ) {

				if (strstr(argv[optind],".jpg")) {
					load_sprite_jpg(&img, in_file);			
				} else {
					load_sprite(&img, in_file);
				}
								
				if ( !img.width ) {
					fprintf(stderr, "%s: failed to open image %s!\n", argv[0], argv[optind]);
					return 1;
				}
				
				img.alpha = ALPHA_EMBEDDED;
				width = img.width;
				height = img.height;

				// main steps
				yctx = yutani_init();
				if (!yctx) {
					fprintf(stderr, "%s: failed to connect to compositor\n", argv[0]);
					return 1;
				}

				init_decorations();
				struct decor_bounds bounds;
				decor_get_bounds(NULL, &bounds);

				decor_left_width = bounds.left_width;
				decor_top_height = bounds.top_height;
				decor_right_width = bounds.right_width;
				decor_bottom_height = bounds.bottom_height;
				decor_width = bounds.width;
				decor_height = bounds.height;
				
	
				window = yutani_window_create(yctx, width + decor_width, height + decor_height);
				yutani_window_move(yctx, window, left, top);

				yutani_window_advertise_icon(yctx, window, APPLICATION_TITLE, "imgviewer");

				ctx = init_graphics_yutani_double_buffer(window);

				redraw();
				yutani_flip(yctx, window);

				int playing = 1;
				while (playing) {
					yutani_msg_t * m = yutani_poll(yctx);
					while (m) {
						if (menu_process_event(yctx, m)) {
							/* just decorations should be fine */
							decors();
							flip(ctx);
							yutani_flip(yctx, window);
						}
						switch (m->type) {
							case YUTANI_MSG_KEY_EVENT:
							{
								struct yutani_msg_key_event * ke = (void*)m->data;
								if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
									playing = 0;
								}
							}
							break;
							case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
							{
								struct yutani_msg_window_focus_change * wf = (void*)m->data;
								yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
								if (win && win == window) {
									win->focused = wf->focused;
									decors();
									flip(ctx);
									yutani_flip(yctx, window);
								}
							}
							break;
							case YUTANI_MSG_RESIZE_OFFER:
							{
								struct yutani_msg_window_resize * wr = (void*)m->data;
								resize_finish(wr->width, wr->height);
							}		
							break;
							case YUTANI_MSG_WINDOW_MOUSE_EVENT:
							{
								struct yutani_msg_window_mouse_event * me = (void*)m->data;
								int result = decor_handle_event(yctx, m);
								switch (result) {
									case DECOR_CLOSE:
										playing = 0;
									break;
								case DECOR_RIGHT:
									/* right click in decoration, show appropriate menu */
									decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
									break;
								default:
									/* Other actions */
								break;
								}
							}	
							break;
							case YUTANI_MSG_WINDOW_CLOSE:
							case YUTANI_MSG_SESSION_END:
								playing = 0;
							break;
							default:
							break;
						}
						free(m);
						m = yutani_poll_async(yctx);
					}
				}
				
				yutani_close(yctx, window);
			}else{
				printf("\nfile %s exist, but not readable!\n",in_file);
				usage(argv);
				return 1;
			}
		}else{
			printf("\nfile %s not exist!\n",in_file);
			usage(argv);
			return 1;
		}		
	} else {
		usage(argv);
		return 0;
	}
	return 0;
}
