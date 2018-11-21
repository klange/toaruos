/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * file-browser - Graphical file manager.
 *
 * Based on the original Python implementation and inspired by
 * Nautilus and Thunar. Also provides a "wallpaper" mode for
 * managing the desktop backgrond.
 */
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <math.h>
#include <libgen.h>
#include <signal.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/icon_cache.h>
#include <toaru/list.h>
#include <toaru/sdf.h>

#define APPLICATION_TITLE "File Browser"
#define SCROLL_AMOUNT 120
#define WALLPAPER_PATH "/usr/share/wallpaper.bmp"

struct File {
	char name[256];      /* Displayed name (icon label) */
	char icon[256];      /* Icon identifier */
	char link[256];      /* Link target for symlinks */
	char launcher[256];  /* Launcher spec */
	char filename[256];  /* Actual filename for launchers */
	int type;            /* File type: 0 = normal, 1 = directory, 2 = launcher */
	int selected;        /* Selection status */
};

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

static int application_running = 1; /* Big loop exit condition */
static int show_hidden = 0; /* Whether or not show hidden files */
static int scroll_offset = 0; /* How far the icon view should be scrolled */
static int available_height = 0; /* How much space is available in the main window for the icon view */
static int is_desktop_background = 0; /* If we're in desktop background mode */
static int menu_bar_height = MENU_BAR_HEIGHT; /* Height of the menu bar, if present - it's not in desktop mode */
static sprite_t * wallpaper_buffer = NULL; /* Prebaked wallpaper texture */
static char title[512]; /* Application title bar */
static int FILE_HEIGHT = 80; /* Height of one row of icons */
static int FILE_WIDTH = 100; /* Width of one column of icons */
static int FILE_PTR_WIDTH = 1; /* How many icons wide the display should be */
static sprite_t * contents_sprite = NULL; /* Icon view rendering context */
static gfx_context_t * contents = NULL; /* Icon view rendering context */
static char * current_directory = NULL; /* Current directory path */
static int hilighted_offset = -1; /* Which file is hovered by the mouse */
static struct File ** file_pointers = NULL; /* List of file pointers */
static ssize_t file_pointers_len = 0; /* How many files are in the current list */
static uint64_t last_click = 0; /* For double click */
static int last_click_offset = -1; /* So that clicking two different things quickly doesn't count as a double click */
static int modifiers = 0; /* For ctrl-click */

/* Menu bar entries */
static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"View", "view"},
	{"Go", "go"},
	{"Help", "help"},
	{NULL, NULL},
};

static struct MenuList * context_menu = NULL;

/**
 * Accurate time comparison.
 *
 * These methods were taken from the compositor and
 * allow us to time double-clicks accurately.
 */
static uint64_t precise_current_time(void) {
	struct timeval t;
	gettimeofday(&t, NULL);

	time_t sec_diff = t.tv_sec;
	suseconds_t usec_diff = t.tv_usec;

	return (uint64_t)(sec_diff * 1000 + usec_diff / 1000);
}

static uint64_t precise_time_since(uint64_t start_time) {

	uint32_t now = precise_current_time();
	uint32_t diff = now - start_time; /* Milliseconds */

	return diff;
}

/**
 * When in desktop mode, we fake decoration boundaries to
 * position the icon view correctly. When in normal mode,
 * we just passt through the actual bounds.
 */
static int _decor_get_bounds(yutani_window_t * win, struct decor_bounds * bounds) {
	if (is_desktop_background) {
		memset(bounds, 0, sizeof(struct decor_bounds));
		bounds->top_height = 30;
		bounds->left_width = 20;
		return 0;
	}
	return decor_get_bounds(win, bounds);
}

/**
 * This should probably be in a yutani core library...
 *
 * If a down and up event were close enough together to be considered a click.
 */
static int _close_enough(struct yutani_msg_window_mouse_event * me) {
	if (me->command == YUTANI_MOUSE_EVENT_RAISE && sqrt(pow(me->new_x - me->old_x, 2) + pow(me->new_y - me->old_y, 2)) < 10) {
		return 1;
	}
	return 0;
}

/**
 * Clear out the space for an icon.
 * We clear to transparent so that the desktop background can be shown in desktop mode.
 */
static void clear_offset(int offset) {
	/* From the flat array offset, figure out the x/y offset. */
	int offset_y = offset / FILE_PTR_WIDTH;
	int offset_x = offset % FILE_PTR_WIDTH;
	draw_rectangle_solid(contents, offset_x * FILE_WIDTH, offset_y * FILE_HEIGHT, FILE_WIDTH, FILE_HEIGHT, rgba(0,0,0,0));
}

/**
 * Draw an icon view entry
 */
static void draw_file(struct File * f, int offset) {

	/* From the flat array offset, figure out the x/y offset. */
	int offset_y = offset / FILE_PTR_WIDTH;
	int offset_x = offset % FILE_PTR_WIDTH;
	int x = offset_x * FILE_WIDTH;
	int y = offset_y * FILE_HEIGHT;

	/* Load the icon sprite from the cache */
	sprite_t * icon = icon_get_48(f->icon);

	/* If the display name is too long to fit, cut it with an ellipsis. */
	int len = strlen(f->name);
	char * name = malloc(len + 4);
	memcpy(name, f->name, len + 1);
	int name_width;
	while ((name_width = draw_sdf_string_width(name, 16, SDF_FONT_THIN)) > FILE_WIDTH - 8 /* Padding */) {
		len--;
		name[len+0] = '.';
		name[len+1] = '.';
		name[len+2] = '.';
		name[len+3] = '\0';
	}

	/* Draw the icon */
	int center_x_icon = (FILE_WIDTH - icon->width) / 2;
	int center_x_text = (FILE_WIDTH - name_width) / 2;
	draw_sprite(contents, icon, center_x_icon + x, y + 2);

	if (f->selected) {
		/* If this file is selected, paint the icon blue... */
		draw_sprite_alpha_paint(contents, icon, center_x_icon + x, y + 2, 0.5, rgb(72,167,255));
		/* And draw the name with a blue background and white text */
		draw_rounded_rectangle(contents, center_x_text + x - 2, y + 54, name_width + 6, 20, 3, rgb(72,167,255));
		draw_sdf_string(contents, center_x_text + x, y + 54, name, 16, rgb(255,255,255), SDF_FONT_THIN);
	} else {
		if (is_desktop_background) {
			/* If this is the desktop view, white text with a drop shadow */
			draw_sdf_string_stroke(contents, center_x_text + x + 1, y + 55, name, 16, rgba(0,0,0,120), SDF_FONT_THIN, 1.7, 0.5);
			draw_sdf_string(contents, center_x_text + x, y + 54, name, 16, rgb(255,255,255), SDF_FONT_THIN);
		} else {
			/* Otherwise, black text */
			draw_sdf_string(contents, center_x_text + x, y + 54, name, 16, rgb(0,0,0), SDF_FONT_THIN);
		}
	}

	if (offset == hilighted_offset) {
		/* The hovered icon should have some added brightness, so paint it white */
		draw_sprite_alpha_paint(contents, icon, center_x_icon + x, y + 2, 0.3, rgb(255,255,255));
	}

	if (f->link[0]) {
		/* For symlinks, draw an indicator */
		sprite_t * arrow = icon_get_16("forward");
		draw_sprite(contents, arrow, center_x_icon + 32 + x, y + 32);
	}

	free(name);
}

/**
 * Get file from array offset, with bounds check
 */
static struct File * get_file_at_offset(int offset) {
	if (offset >= 0 && offset < file_pointers_len) {
		return file_pointers[offset];
	}
	return NULL;
}

/**
 * Redraw all icon view entries
 */
static void redraw_files(void) {
	for (int i = 0; i < file_pointers_len; ++i) {
		draw_file(file_pointers[i], i);
	}
}

/**
 * Set the application title.
 */
static void set_title(char * directory) {
	/* Do nothing in desktop mode to avoid advertisement. */
	if (is_desktop_background) return;

	/* If the directory name is set... */
	if (directory) {
		/* Translate / to File System */
		if (!strcmp(directory, "/")) {
			directory = "File System";
		}
		sprintf(title, "%s - " APPLICATION_TITLE, directory);
	} else {
		/* Otherwise, just "File Browser" */
		sprintf(title, APPLICATION_TITLE);
	}

	/* Advertise to the panel */
	yutani_window_advertise_icon(yctx, main_window, title, "folder");
}

/**
 * Check if a file name ends with an extension.
 *
 * Can also be used for exact matches.
 */
static int has_extension(struct File * f, char * extension) {
	int i = strlen(f->name);
	int j = strlen(extension);

	do {
		if (f->name[i] != (extension)[j]) break;
		if (j == 0) return 1;
		if (i == 0) break;
		i--;
		j--;
	} while (1);
	return 0;
}

/**
 * Read the contents of a directory into the icon view.
 */
static void load_directory(const char * path) {

	/* Free the current icon view entries */
	if (file_pointers) {
		for (int i = 0; i < file_pointers_len; ++i) {
			free(file_pointers[i]);
		}
		free(file_pointers);
	}

	DIR * dirp = opendir(path);

	if (!dirp) {
		/**
		 * TODO: This should probably show a dialog and then reload the current directory,
		 *       or maybe we should be checking this before clearing the current file pointers.
		 */
		file_pointers = NULL;
		file_pointers_len = 0;
		return;
	}

	if (current_directory) {
		free(current_directory);
	}

	char * home = getenv("HOME");
	if (home && !strcmp(path, home)) {
		/* If the current directory is the user's homedir, present it that way in the title */
		set_title("Home");
	} else {
		/* Otherwise use just the directory base name */
		char * tmp = strdup(path);
		char * base = basename(tmp);
		set_title(base);
		free(tmp);
	}

	/* If we ended up in a path with //two/initial/slashes, fix that. */
	if (path[0] == '/' && path[1] == '/') {
		current_directory = strdup(path+1);
	} else {
		current_directory = strdup(path);
	}

	/* TODO: Show relative time informaton... */
#if 0
	/* Get the current time */
	struct tm * timeinfo;
	struct timeval now;
	gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	int this_year = timeinfo->tm_year;
#endif

	list_t * file_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] == '.' &&
			(ent->d_name[1] == '\0' || 
			 (ent->d_name[1] == '.' &&
			  ent->d_name[2] == '\0'))) {
			/* skip . and .. */
			ent = readdir(dirp);
			continue;
		}
		if (show_hidden || (ent->d_name[0] != '.')) {

			/* Set display name from file name */
			struct File * f = malloc(sizeof(struct File));
			sprintf(f->name, "%s", ent->d_name); /* snprintf? copy min()? */

			struct stat statbuf;
			struct stat statbufl;

			/* Calculate absolute path to file */
			char tmp[strlen(path)+strlen(ent->d_name)+2];
			sprintf(tmp, "%s/%s", path, ent->d_name);
			lstat(tmp, &statbuf);

			/* Read link target for symlinks */
			if (S_ISLNK(statbuf.st_mode)) {
				memcpy(&statbufl, &statbuf, sizeof(struct stat));
				stat(tmp, &statbuf);
				readlink(tmp, f->link, 256);
			} else {
				f->link[0] = '\0';
			}

			f->launcher[0] = '\0';
			f->selected = 0;

			if (S_ISDIR(statbuf.st_mode)) {
				/* Directory */
				sprintf(f->icon, "folder");
				f->type = 1;
			} else {
				/* Regular file */

				/* Default regular files to open in bim */
				sprintf(f->launcher, "exec terminal bim");

				if (is_desktop_background && has_extension(f, ".launcher")) {
					/* In desktop mode, read launchers specially */
					FILE * file = fopen(tmp,"r");
					char tbuf[1024];
					while (!feof(file)) {
						fgets(tbuf, 1024, file);
						char * nl = strchr(tbuf,'\n');
						if (nl) *nl = '\0';
						char * eq = strchr(tbuf,'=');
						if (!eq) continue;
						*eq = '\0'; eq++;

						if (!strcmp(tbuf, "icon")) {
							sprintf(f->icon, "%s", eq);
						} else if (!strcmp(tbuf, "run")) {
							sprintf(f->launcher, "%s #", eq);
						} else if (!strcmp(tbuf, "title")) {
							sprintf(f->name, eq);
						}
					}
					sprintf(f->filename, "%s", ent->d_name);
					f->type = 2;
				} else {
					/* Handle various file types */
					if (has_extension(f, ".c")) {
						sprintf(f->icon, "c");
					} else if (has_extension(f, ".h")) {
						sprintf(f->icon, "h");
					} else if (has_extension(f, ".bmp")) {
						sprintf(f->icon, "image");
						sprintf(f->launcher, "exec imgviewer");
					} else if (statbuf.st_mode & 0111) {
						/* Executable files - use their name for their icon, and launch themselves. */
						sprintf(f->icon, "%s", f->name);
						sprintf(f->launcher, "SELF");
					} else {
						sprintf(f->icon, "file");
					}
					f->type = 0;
				}
			}

			list_insert(file_list, f);
		}
		ent = readdir(dirp);
	}
	closedir(dirp);

	/* Store the entries in a flat array. */
	file_pointers = malloc(sizeof(struct File *) * file_list->length);
	file_pointers_len = file_list->length;
	int i = 0;
	foreach (node, file_list) {
		file_pointers[i] = node->value;
		i++;
	}

	/* Free our temporary linked list */
	list_free(file_list);
	free(file_list);

	/* Sort files */
	int comparator(const void * c1, const void * c2) {
		const struct File * f1 = *(const struct File **)(c1);
		const struct File * f2 = *(const struct File **)(c2);
		/* Launchers before directories before files */
		if (f1->type > f2->type) return -1;
		if (f2->type > f1->type) return 1;
		/* Launchers sorted by filename, not by display name */
		if (f1->type == 2 && f2->type == 2) {
			return strcmp(f1->filename, f2->filename);
		}
		/* Files sorted by name */
		return strcmp(f1->name, f2->name);
	}
	qsort(file_pointers, file_pointers_len, sizeof(struct File *), comparator);

	/* Reset scroll offset when navigating */
	scroll_offset = 0;
}

/**
 * Resize and redraw the icon view */
static void reinitialize_contents(void) {

	/* If there already is a context, free it. */
	if (contents) {
		free(contents);
	}

	/* If there already is a context buffer, free it. */
	if (contents_sprite) {
		sprite_free(contents_sprite);
	}

	/* Get window bounds to determine how wide we can make our icon view */
	struct decor_bounds bounds;
	_decor_get_bounds(main_window, &bounds);

	if (is_desktop_background) {
		/**
		 * TODO: Actually calculate an optimal FILE_PTR_WIDTH or fix this to
		 *       work properly with vertical rows of files
		 */
		FILE_PTR_WIDTH = 1;
	} else {
		FILE_PTR_WIDTH = (ctx->width - bounds.width) / FILE_WIDTH;
	}

	/* Calculate required height to fit files */
	int calculated_height = (file_pointers_len / FILE_PTR_WIDTH + 1) * FILE_HEIGHT;

	/* Create buffer */
	contents_sprite = create_sprite(FILE_PTR_WIDTH * FILE_WIDTH, calculated_height, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);

	/* Fill to blank */
	draw_fill(contents, rgba(0,0,0,0));

	/* Draw file entries */
	redraw_files();
}

/**
 * Desktop mode responsds to sig_usr2 by returning to
 * the bottom of the Z-order stack.
 */
static void sig_usr2(int sig) {
	yutani_set_stack(yctx, main_window, YUTANI_ZORDER_BOTTOM);
	yutani_flip(yctx, main_window);
	signal(SIGUSR2, sig_usr2);
}

/**
 * Redraw the entire window.
 */
static void redraw_window(void) {
	if (!is_desktop_background) {
		/* Clear to white and draw decorations */
		draw_fill(ctx, rgb(255,255,255));
		render_decorations(main_window, ctx, title);
	} else {
		/* Draw wallpaper in desktop mode */
		draw_sprite(ctx, wallpaper_buffer, 0, 0);
	}

	struct decor_bounds bounds;
	_decor_get_bounds(main_window, &bounds);

	if (!is_desktop_background) {
		/* Position, size, and draw the menu bar */
		menu_bar.x = bounds.left_width;
		menu_bar.y = bounds.top_height;
		menu_bar.width = ctx->width - bounds.width;
		menu_bar.window = main_window;
		menu_bar_render(&menu_bar, ctx);
	}

	/* Draw the icon view, clipped to the viewport and scrolled appropriately. */
	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, bounds.left_width, bounds.top_height + menu_bar_height, ctx->width - bounds.width, available_height);
	draw_sprite(ctx, contents_sprite, bounds.left_width, bounds.top_height + menu_bar_height - scroll_offset);
	gfx_clear_clip(ctx);
	gfx_add_clip(ctx, 0, 0, ctx->width, ctx->height);

	/* Flip graphics context and inform compositor */
	flip(ctx);
	yutani_flip(yctx, main_window);
}

/**
 * Loads and bakes the wallpaper to the appropriate size.
 */
static void draw_background(int width, int height) {

	/* If the wallpaper is already loaded, free it. */
	if (wallpaper_buffer) {
		sprite_free(wallpaper_buffer);
	}

	/* Open the wallpaper */
	sprite_t * wallpaper = malloc(sizeof(sprite_t));
	load_sprite(wallpaper, WALLPAPER_PATH);
	wallpaper->alpha = 0;

	/* Create a new buffer to hold the baked wallpaper */
	wallpaper_buffer = create_sprite(width, height, 0);
	gfx_context_t * ctx = init_graphics_sprite(wallpaper_buffer);

	/* Calculate the appropriate scaled size to fit the screen. */
	float x = (float)width / (float)wallpaper->width;
	float y = (float)height / (float)wallpaper->height;

	int nh = (int)(x * (float)wallpaper->height);
	int nw = (int)(y * (float)wallpaper->width);

	/* Clear to black to avoid odd transparency issues along edges */
	draw_fill(ctx, rgb(0,0,0));

	/* Scale the wallpaper into the buffer. */
	if (nw == wallpaper->width && nh == wallpaper->height) {
		/* No scaling necessary */
		draw_sprite(ctx, wallpaper, 0, 0);
	} else if (nw >= width) {
		/* Scaled wallpaper is wider, height should match. */
		draw_sprite_scaled(ctx, wallpaper, ((int)width - nw) / 2, 0, nw+2, height);
	} else {
		/* Scaled wallpaper is taller, width should match. */
		draw_sprite_scaled(ctx, wallpaper, 0, ((int)height - nh) / 2, width+2, nh);
	}

	/* Free the original wallpaper. */
	sprite_free(wallpaper);
	free(ctx);
}

/**
 * Resize window when asked by the compositor.
 */
static void resize_finish(int w, int h) {
	int width_changed = (main_window->width != (unsigned int)w);

	yutani_window_resize_accept(yctx, main_window, w, h);
	reinit_graphics_yutani(ctx, main_window);

	struct decor_bounds bounds;
	_decor_get_bounds(main_window, &bounds);

	/* Recalculate available size */
	available_height = ctx->height - menu_bar_height - bounds.height;

	/* If the width changed, we need to rebuild the icon view */
	if (width_changed) {
		reinitialize_contents();
	}

	/* Make sure we're not scrolled weirdly after resizing */
	if (available_height > contents->height) {
		scroll_offset = 0;
	} else {
		if (scroll_offset > contents->height - available_height) {
			scroll_offset = contents->height - available_height;
		}
	}

	/* If the desktop background changes size, we have to reload and rescale the wallpaper */
	if (is_desktop_background) {
		draw_background(w, h);
	}

	/* Redraw */
	redraw_window();
	yutani_window_resize_done(yctx, main_window);

	yutani_flip(yctx, main_window);
}

/* TODO: We don't have an input box yet. */
#if 0
static void _menu_action_input_path(struct MenuEntry * entry) {

}
#endif

/* File > Exit */
static void _menu_action_exit(struct MenuEntry * entry) {
	application_running = 0;
}

/* Go > ... generic handler */
static void _menu_action_navigate(struct MenuEntry * entry) {
	/* go to entry->action */
	struct MenuEntry_Normal * _entry = (void*)entry;
	load_directory(_entry->action);
	reinitialize_contents();
	redraw_window();
}

/* Go > Up */
static void _menu_action_up(struct MenuEntry * entry) {
	/* go up */
	char * tmp = strdup(current_directory);
	char * dir = dirname(tmp);
	load_directory(dir);
	reinitialize_contents();
	redraw_window();
}

/* [Context] > Refresh */
static void _menu_action_refresh(struct MenuEntry * entry) {
	char * tmp = strdup(current_directory);
	load_directory(tmp);
	reinitialize_contents();
	redraw_window();
}

/* Help > Contents */
static void _menu_action_help(struct MenuEntry * entry) {
	/* show help documentation */
	system("help-browser file-browser.trt &");
	redraw_window();
}

/* [Context] > Copy */
static void _menu_action_copy(struct MenuEntry * entry) {
	size_t output_size = 0;

	/* Calculate required space for the clipboard */
	int base_is_root = !strcmp(current_directory, "/"); /* avoid redundant slash */
	for (int i = 0; i < file_pointers_len; ++i) {
		if (file_pointers[i]->selected) {
			output_size += strlen(current_directory) + !base_is_root + strlen(file_pointers[i]->type == 2 ? file_pointers[i]->filename : file_pointers[i]->name) + 1; /* base / file \n */
		}
	}

	/* Nothing to copy? */
	if (!output_size) return;

	/* Create the clipboard contents as a LF-separated list of absolute paths */
	char * clipboard = malloc(output_size);
	clipboard[0] = '\0';
	for (int i = 0; i < file_pointers_len; ++i) {
		if (file_pointers[i]->selected) {
			strcat(clipboard, current_directory);
			if (!base_is_root) { strcat(clipboard, "/"); }
			strcat(clipboard, file_pointers[i]->type == 2 ? file_pointers[i]->filename : file_pointers[i]->name);
			strcat(clipboard, "\n");
		}
	}

	if (clipboard[output_size-1] == '\n') {
		/* Remove trailing line feed */
		clipboard[output_size-1] = '\0';
	}

	yutani_set_clipboard(yctx, clipboard);
	free(clipboard);
}

static void _menu_action_paste(struct MenuEntry * entry) {
	yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_CLIPBOARD);
}

/* Help > About File Browser */
static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About File Browser\" /usr/share/icons/48/folder.bmp \"ToaruOS File Browser\" \"(C) 2018 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://gitlab.com/toaruos\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)main_window->x + (int)main_window->width / 2, (int)main_window->y + (int)main_window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw_window();
}

/**
 * Generic application launcher - like system(), but without the wait.
 * Also sets the working directory to the currently-opened directory.
 */
static void launch_application(char * app) {
	if (!fork()) {
		if (current_directory) chdir(current_directory);
		char * tmp = malloc(strlen(app) + 10);
		sprintf(tmp, "%s", app);
		char * args[] = {"/bin/sh", "-c", tmp, NULL};
		execvp(args[0], args);
		exit(1);
	}
}

/* Generic handler for various launcher menus */
static void launch_application_menu(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (void *)self;
	launch_application((char *)_self->action);
}

/**
 * Perform the appropriate action to open a File
 */
static void open_file(struct File * f) {
	if (f->type == 1) {
		char tmp[1024];
		if (is_desktop_background) {
			/* Always open directories in new file browser windows when launched from desktop */
			sprintf(tmp,"file-browser \"%s/%s\"", current_directory, f->name);
			launch_application(tmp);
		} else {
			/* In normal mode, navigate to this directory. */
			sprintf(tmp,"%s/%s", current_directory, f->name);
			load_directory(tmp);
			reinitialize_contents();
			redraw_window();
		}
	} else if (f->launcher[0]) {
		char tmp[4096];
		if (!strcmp(f->launcher, "SELF")) {
			/* "SELF" launchers are for binaries. */
			sprintf(tmp, "exec ./%s", f->name);
		} else {
			/* Other launchers shouuld take file names as arguments.
			 * NOTE: If you don't want the file name, you can append # to your launcher.
			 *       Since it's parsed by the shell, this will yield a comment.
			 */
			sprintf(tmp, "%s \"%s\"", f->launcher, f->name);
		}
		launch_application(tmp);
	}
}

/* [Context] > Open */
static void _menu_action_open(struct MenuEntry * self) {
	for (int i = 0; i < file_pointers_len; ++i) {
		if (file_pointers[i]->selected) {
			open_file(file_pointers[i]);
		}
	}
}

/* [Context] > Edit in Bim */
static void _menu_action_edit(struct MenuEntry * self) {
	for (int i = 0; i < file_pointers_len; ++i) {
		if (file_pointers[i]->selected) {
			char tmp[1024];
			sprintf(tmp, "exec terminal bim \"%s\"", file_pointers[i]->type == 2 ? file_pointers[i]->filename : file_pointers[i]->name);
			launch_application(tmp);
		}
	}
}

/* View > (Show/Hide) Hidden Files */
static void _menu_action_toggle_hidden(struct MenuEntry * self) {
	show_hidden = !show_hidden;
	menu_update_title(self, show_hidden ? "Hide Hidden Files" : "Show Hidden Files");
	_menu_action_refresh(NULL);
}

static void handle_clipboard(char * contents) {
	fprintf(stderr, "Received clipboard:\n%s\n",contents);

	char * file = contents;
	while (file && *file) {
		char * next_file = strchr(file, '\n');
		if (next_file) {
			*next_file = '\0';
			next_file++;
		}

		/* determine if the destination already exists */
		char * cheap_basename = strrchr(file, '/');
		if (!cheap_basename) cheap_basename = file;
		else cheap_basename++;

		char destination[4096];
		sprintf(destination, "%s/%s", current_directory, cheap_basename);

		struct stat statbuf;
		if (!stat(destination, &statbuf)) {
			char message[4096];
			sprintf(message, "showdialog \"File Browser\" /usr/share/icons/48/folder.bmp \"Not overwriting file '%s'.\"", cheap_basename);
			launch_application(message);
		} else {
			char cp[1024];
			sprintf(cp, "cp -r \"%s\" \"%s\"", file, current_directory);
			if (system(cp)) {
				char message[4096];
				sprintf(message, "showdialog \"File Browser\" /usr/share/icons/48/folder.bmp \"Error copying file '%s'.\"", cheap_basename);
				launch_application(message);
			}
		}
		file = next_file;
	}

	_menu_action_refresh(NULL);
}

/**
 * Toggle the selected status of the highlighted icon.
 *
 * When Ctrl is held, the current selection is maintained.
 */
static void toggle_selected(int hilighted_offset, int modifiers) {
	struct File * f = get_file_at_offset(hilighted_offset);

	/* No file at this offset, do nothing. */
	if (!f) return;

	/* Toggle selection of the current file */
	f->selected = !f->selected;

	/* If Ctrl wasn't held, unselect everything else. */
	if (!(modifiers & KEY_MOD_LEFT_CTRL)) {
		for (int i = 0; i < file_pointers_len; ++i) {
			if (file_pointers[i] != f && file_pointers[i]->selected) {
				file_pointers[i]->selected = 0;
				clear_offset(i);
				draw_file(file_pointers[i], i);
			}
		}
	}

	/* Redraw the file */
	clear_offset(hilighted_offset);
	draw_file(f, hilighted_offset);

	/* And repaint the window */
	redraw_window();
}

int main(int argc, char * argv[]) {

	yctx = yutani_init();
	init_decorations();

	int arg_ind = 1;

	if (argc > 1 && !strcmp(argv[1], "--wallpaper")) {
		is_desktop_background = 1;
		signal(SIGUSR2, sig_usr2);
		draw_background(yctx->display_width, yctx->display_height);
		main_window = yutani_window_create(yctx, yctx->display_width, yctx->display_height);
		yutani_window_move(yctx, main_window, 0, 0);
		yutani_set_stack(yctx, main_window, YUTANI_ZORDER_BOTTOM);
		arg_ind++;
	} else {
		main_window = yutani_window_create(yctx, 800, 600);
		yutani_window_move(yctx, main_window, yctx->display_width / 2 - main_window->width / 2, yctx->display_height / 2 - main_window->height / 2);
	}

	if (arg_ind < argc) {
		chdir(argv[arg_ind]);
	}

	ctx = init_graphics_yutani_double_buffer(main_window);

	struct decor_bounds bounds;
	_decor_get_bounds(main_window, &bounds);

	set_title(NULL);

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window;

	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create();
	menu_insert(m, menu_create_normal(NULL,NULL,"Show Hidden Files", _menu_action_toggle_hidden));
	menu_set_insert(menu_bar.set, "view", m);

	m = menu_create(); /* Go */
	/* TODO implement input dialog for Path... */
#if 0
	menu_insert(m, menu_create_normal("open",NULL,"Path...", _menu_action_input_path));
	menu_insert(m, menu_create_separator());
#endif
	menu_insert(m, menu_create_normal("home",getenv("HOME"),"Home",_menu_action_navigate));
	menu_insert(m, menu_create_normal(NULL,"/","File System",_menu_action_navigate));
	menu_insert(m, menu_create_normal("up",NULL,"Up",_menu_action_up));
	menu_set_insert(menu_bar.set, "go", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	available_height = ctx->height - menu_bar_height - bounds.height;

	context_menu = menu_create(); /* Right-click menu */
	menu_insert(context_menu, menu_create_normal(NULL,NULL,"Open",_menu_action_open));
	menu_insert(context_menu, menu_create_normal(NULL,NULL,"Edit in Bim",_menu_action_edit));
	menu_insert(context_menu, menu_create_separator());
	menu_insert(context_menu, menu_create_normal(NULL,NULL,"Copy",_menu_action_copy));
	menu_insert(context_menu, menu_create_normal(NULL,NULL,"Paste",_menu_action_paste));
	menu_insert(context_menu, menu_create_separator());
	if (!is_desktop_background) {
		menu_insert(context_menu, menu_create_normal("up",NULL,"Up",_menu_action_up));
	}
	menu_insert(context_menu, menu_create_normal("refresh",NULL,"Refresh",_menu_action_refresh));
	menu_insert(context_menu, menu_create_normal("utilities-terminal","terminal","Open Terminal",launch_application_menu));


	/* Load the current working directory */
	char tmp[1024];
	getcwd(tmp, 1024);
	load_directory(tmp);

	/* Draw files */
	reinitialize_contents();
	redraw_window();

	while (application_running) {
		waitpid(-1, NULL, WNOHANG);
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			if (menu_process_event(yctx, m)) {
				redraw_window();
			}
			switch (m->type) {
				case YUTANI_MSG_WELCOME:
					if (is_desktop_background) {
						yutani_window_resize_offer(yctx, main_window, yctx->display_width, yctx->display_height);
					}
					break;
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						modifiers = ke->event.modifiers;
						if (!is_desktop_background) {
							if (ke->event.action == KEY_ACTION_DOWN && ke->event.keycode == 'q') {
								_menu_action_exit(NULL);
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
					{
						struct yutani_msg_window_focus_change * wf = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)wf->wid);
						if (win == main_window) {
							win->focused = wf->focused;
							redraw_window();
						}
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						if (wr->wid == main_window->wid) {
							resize_finish(wr->width, wr->height);
						}
					}
					break;
				case YUTANI_MSG_CLIPBOARD:
					{
						struct yutani_msg_clipboard * cb = (void *)m->data;
						char * selection_text;
						if (*cb->content == '\002') {
							int size = atoi(&cb->content[2]);
							FILE * clipboard = yutani_open_clipboard(yctx);
							selection_text = malloc(size + 1);
							fread(selection_text, 1, size, clipboard);
							selection_text[size] = '\0';
							fclose(clipboard);
						} else {
							selection_text = malloc(cb->size+1);
							memcpy(selection_text, cb->content, cb->size);
							selection_text[cb->size] = '\0';
						}
						handle_clipboard(selection_text);
						free(selection_text);
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						yutani_window_t * win = hashmap_get(yctx->windows, (void*)me->wid);
						struct decor_bounds bounds;
						_decor_get_bounds(win, &bounds);

						if (win == main_window) {
							int result = decor_handle_event(yctx, m);
							switch (result) {
								case DECOR_CLOSE:
									_menu_action_exit(NULL);
									break;
								case DECOR_RIGHT:
									/* right click in decoration, show appropriate menu */
									decor_show_default_menu(main_window, main_window->x + me->new_x, main_window->y + me->new_y);
									break;
								default:
									/* Other actions */
									break;
							}

							/* Menu bar */
							menu_bar_mouse_event(yctx, main_window, &menu_bar, me, me->new_x, me->new_y);

							if (me->new_y > (int)(bounds.top_height + menu_bar_height) &&
								me->new_y < (int)(main_window->height - bounds.bottom_height) &&
								me->new_x > (int)(bounds.left_width) &&
								me->new_x < (int)(main_window->width - bounds.right_width)) {
								if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
									/* Scroll up */
									scroll_offset -= SCROLL_AMOUNT;
									if (scroll_offset < 0) {
										scroll_offset = 0;
									}
									redraw_window();
								} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
									if (available_height > contents->height) {
										scroll_offset = 0;
									} else {
										scroll_offset += SCROLL_AMOUNT;
										if (scroll_offset > contents->height - available_height) {
											scroll_offset = contents->height - available_height;
										}
									}
									redraw_window();
								}

								/* Get offset into contents */
								int y_into = me->new_y - bounds.top_height - menu_bar_height + scroll_offset;
								int x_into = me->new_x - bounds.left_width;
								int offset = (y_into / FILE_HEIGHT) * FILE_PTR_WIDTH + x_into / FILE_WIDTH;
								if (x_into > FILE_PTR_WIDTH * FILE_WIDTH) {
									offset = -1;
								}
								if (offset != hilighted_offset) {
									int old_offset = hilighted_offset;
									hilighted_offset = offset;
									if (old_offset != -1) {
										clear_offset(old_offset);
										struct File * f = get_file_at_offset(old_offset);
										if (f) {
											clear_offset(old_offset);
											draw_file(f, old_offset);
										}
									}
									struct File * f = get_file_at_offset(hilighted_offset);
									if (f) {
										clear_offset(hilighted_offset);
										draw_file(f, hilighted_offset);
									}
									redraw_window();
								}

								if (me->command == YUTANI_MOUSE_EVENT_CLICK || _close_enough(me)) {
									struct File * f = get_file_at_offset(hilighted_offset);
									if (f) {
										if (last_click_offset == hilighted_offset && precise_time_since(last_click) < 400) {
											open_file(f);
											last_click = 0;
										} else {
											last_click = precise_current_time();
											last_click_offset = hilighted_offset;
											toggle_selected(hilighted_offset, modifiers);
										}
									} else {
										if (!(modifiers & KEY_MOD_LEFT_CTRL)) {
											for (int i = 0; i < file_pointers_len; ++i) {
												if (file_pointers[i]->selected) {
													file_pointers[i]->selected = 0;
													clear_offset(i);
													draw_file(file_pointers[i], i);
												}
											}
											redraw_window();
										}
									}
								} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
									if (!context_menu->window) {
										struct File * f = get_file_at_offset(hilighted_offset);
										if (f && !f->selected) {
											toggle_selected(hilighted_offset, modifiers);
										}
										menu_show(context_menu, main_window->ctx);
										yutani_window_move(main_window->ctx, context_menu->window, me->new_x + main_window->x, me->new_y + main_window->y);
									}
								}

							} else {
								int old_offset = hilighted_offset;
								hilighted_offset = -1;
								if (old_offset != -1) {
									clear_offset(old_offset);
									struct File * f = get_file_at_offset(old_offset);
									if (f) {
										clear_offset(old_offset);
										draw_file(f, old_offset);
									}
									redraw_window();
								}
							}

						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					_menu_action_exit(NULL);
					break;
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}
}
