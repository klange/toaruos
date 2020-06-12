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
#include <ctype.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/fswait.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/icon_cache.h>
#include <toaru/list.h>
#include <toaru/sdf.h>
#include <toaru/button.h>

#define APPLICATION_TITLE "File Browser"
#define SCROLL_AMOUNT 120
#define WALLPAPER_PATH "/usr/share/wallpaper.jpg"

struct File {
	char name[256];      /* Displayed name (icon label) */
	char icon[256];      /* Icon identifier */
	char link[256];      /* Link target for symlinks */
	char launcher[256];  /* Launcher spec */
	char filename[256];  /* Actual filename for launchers */
	char filetype[256];  /* Textual description of file type */
	uint64_t size;       /* File size */
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
static int menu_bar_height = MENU_BAR_HEIGHT + 36; /* Height of the menu bar, if present - it's not in desktop mode */
static sprite_t * wallpaper_buffer = NULL; /* Prebaked wallpaper texture */
static sprite_t * wallpaper_old = NULL; /* Previous wallpaper when transitioning */
static uint64_t timer = 0; /* Timer for wallpaper transition fade */
static int restart = 0; /* Signal for desktop wallpaper to kill itself to save memory (this is dumb) */
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

/**
 * Navigation input box
 */
static char nav_bar[512] = {0};
static int  nav_bar_cursor = 0;
static int  nav_bar_cursor_x = 0;
static int  nav_bar_focused = 0;

/* Status bar displayed at the bottom of the window */
static char window_status[512] = {0};

/* Button row visibility statuses */
static int _button_hilights[4] = {3,3,3,3};
static int _button_disabled[4] = {1,1,0,0};
static int _button_hover = -1;

/* Menu bar entries */
static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"}, /* 0 */
	{"Edit", "edit"}, /* 1 */
	{"View", "view"}, /* 2 */
	{"Go", "go"},     /* 3 */
	{"Help", "help"}, /* 4 */
	{NULL, NULL},
};

/* Right click context menu */
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

	return (uint64_t)((uint64_t)sec_diff * 1000LL + usec_diff / 1000);
}

static uint64_t precise_time_since(uint64_t start_time) {

	uint64_t now = precise_current_time();
	uint64_t diff = now - start_time; /* Milliseconds */

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
		bounds->top_height = 54;
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
 * Select view mode.
 */
#define VIEW_MODE_ICONS 0
#define VIEW_MODE_TILES 1
#define VIEW_MODE_LIST  2
static int view_mode = VIEW_MODE_ICONS;

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

static int print_human_readable_size(char * _out, uint64_t s) {
	if (s >= 1LL << 30) {
		size_t t = s / (1LL << 30);
		return sprintf(_out, "%d.%1d GiB", (int)t, (int)((int)(s - t * (1LL << 30)) / ((1LL << 30) / 10)));
	} else if (s >= 1<<20) {
		size_t t = s / (1 << 20);
		return sprintf(_out, "%d.%1d MiB", (int)t, (int)(s - t * (1 << 20)) / ((1 << 20) / 10));
	} else if (s >= 1<<10) {
		size_t t = s / (1 << 10);
		return sprintf(_out, "%d.%1d KiB", (int)t, (int)(s - t * (1 << 10)) / ((1 << 10) / 10));
	} else {
		return sprintf(_out, "%d B", (int)s);
	}
}

#define HILIGHT_BORDER_TOP rgb(54,128,205)
#define HILIGHT_GRADIENT_TOP rgb(93,163,236)
#define HILIGHT_GRADIENT_BOTTOM rgb(56,137,220)
#define HILIGHT_BORDER_BOTTOM rgb(47,106,167)

/**
 * Clip text and add ellipsis to fit a specified display width.
 */
static char * ellipsify(char * input, int font_size, int font, int max_width, int * out_width) {
	int len = strlen(input);
	char * out = malloc(len + 4);
	memcpy(out, input, len + 1);
	int width;
	while ((width = draw_sdf_string_width(out, font_size, font)) > max_width) {
		len--;
		out[len+0] = '.';
		out[len+1] = '.';
		out[len+2] = '.';
		out[len+3] = '\0';
	}

	if (out_width) *out_width = width;

	return out;
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
	if (view_mode == VIEW_MODE_ICONS) {
		sprite_t * icon = icon_get_48(f->icon);

		/* If the display name is too long to fit, cut it with an ellipsis. */
		int name_width;
		char * name = ellipsify(f->name, 16, SDF_FONT_THIN, FILE_WIDTH - 8, &name_width);

		/* Draw the icon */
		int center_x_icon = (FILE_WIDTH - icon->width) / 2;
		int center_x_text = (FILE_WIDTH - name_width) / 2;
		draw_sprite(contents, icon, center_x_icon + x, y + 2);

		if (f->selected) {
			/* If this file is selected, paint the icon blue... */
			if (main_window->focused) {
				draw_sprite_alpha_paint(contents, icon, center_x_icon + x, y + 2, 0.5, rgb(72,167,255));
			}
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
	} else if (view_mode == VIEW_MODE_TILES) {
		sprite_t * icon = icon_get_48(f->icon);

		uint32_t text_color = rgb(0,0,0);

		/* If selected, draw background box */
		if (f->selected) {
			struct gradient_definition edge = {FILE_HEIGHT - 4, y+2, HILIGHT_BORDER_TOP, HILIGHT_BORDER_BOTTOM};
			struct gradient_definition body = {FILE_HEIGHT - 6, y+3, HILIGHT_GRADIENT_TOP, HILIGHT_GRADIENT_BOTTOM};
			draw_rounded_rectangle_pattern(contents, x + 2,y + 2, FILE_WIDTH-4, FILE_HEIGHT-4, 3, gfx_vertical_gradient_pattern, &edge);
			draw_rounded_rectangle_pattern(contents, x + 3,y + 3, FILE_WIDTH-6, FILE_HEIGHT-6, 4, gfx_vertical_gradient_pattern, &body);

			text_color = rgb(255,255,255);
		}

		draw_sprite(contents, icon, x + 11, y + 11);
		if (offset == hilighted_offset) {
			/* The hovered icon should have some added brightness, so paint it white */
			draw_sprite_alpha_paint(contents, icon, x + 11, y + 11, 0.3, rgb(255,255,255));
		}

		char * name = ellipsify(f->name, 16, SDF_FONT_BOLD, FILE_WIDTH - 81, NULL);
		char * type = ellipsify(f->filetype, 16, SDF_FONT_THIN, FILE_WIDTH - 81, NULL);

		if (f->type == 0) {
			draw_sdf_string(contents, x + 70, y + 8, name, 16, text_color, SDF_FONT_BOLD);
			draw_sdf_string(contents, x + 70, y + 25, type, 16, text_color, SDF_FONT_THIN);

			char line_three[48] = {0};
			if (*f->link) {
				sprintf(line_three, "Symbolic link");
			} else {
				print_human_readable_size(line_three, f->size);
			}
			draw_sdf_string(contents, x + 70, y + 42, line_three, 16, text_color, SDF_FONT_THIN);
		} else {
			draw_sdf_string(contents, x + 70, y + 15, name, 16, text_color, SDF_FONT_BOLD);
			draw_sdf_string(contents, x + 70, y + 32, type, 16, text_color, SDF_FONT_THIN);
		}

		free(name);
		free(type);
	} else if (view_mode == VIEW_MODE_LIST) {
		sprite_t * icon = icon_get_16(f->icon);
		uint32_t text_color = rgb(0,0,0);

		if (f->selected) {
			struct gradient_definition edge = {FILE_HEIGHT - 4, y+2, HILIGHT_BORDER_TOP, HILIGHT_BORDER_BOTTOM};
			struct gradient_definition body = {FILE_HEIGHT - 6, y+3, HILIGHT_GRADIENT_TOP, HILIGHT_GRADIENT_BOTTOM};
			draw_rounded_rectangle_pattern(contents, x + 2,y + 2, FILE_WIDTH-4, FILE_HEIGHT-4, 3, gfx_vertical_gradient_pattern, &edge);
			draw_rounded_rectangle_pattern(contents, x + 3,y + 3, FILE_WIDTH-6, FILE_HEIGHT-6, 4, gfx_vertical_gradient_pattern, &body);

			text_color = rgb(255,255,255);
		} else if (offset == hilighted_offset) {
			draw_rounded_rectangle(contents, x + 2, y + 2, FILE_WIDTH - 4, FILE_HEIGHT - 4, 3, rgb(180,180,180));
			draw_rounded_rectangle(contents, x + 3, y + 3, FILE_WIDTH - 6, FILE_HEIGHT - 6, 4, rgb(255,255,255));
		}

		if (icon->width != 16 || icon->height != 16) {
			draw_sprite_scaled(contents, icon, x + 4, y + 4, 16, 16);
		} else {
			draw_sprite(contents, icon, x + 4, y + 4);
		}

		char * name = ellipsify(f->name, 16, SDF_FONT_THIN, FILE_WIDTH - 26, NULL);

		draw_sdf_string(contents, x + 24, y + 2, name, 16, text_color, SDF_FONT_THIN);

		free(name);

	}
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
	/* Fill to blank */
	draw_fill(contents, rgba(0,0,0,0));

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
 * Forward/backward history; we're always in the middle of these.
 * When we navigate somewhere new, clear the forward history, but
 * keep the back history and append the previous location.
 */
static list_t * history_back;
static list_t * history_forward;

/**
 * Update the status bar text at the bottom of the window
 * based on the selected items in the file view.
 */
static void update_status(void) {
	uint64_t total_size = 0;
	uint64_t selected_size = 0;
	int selected_count = 0;
	struct File * selected = NULL;

	for (int i = 0; i < file_pointers_len; ++i) {
		total_size += file_pointers[i]->size;
		if (file_pointers[i]->selected) {
			selected_count += 1;
			selected_size += file_pointers[i]->size;
			selected = file_pointers[i];
		}
	}

	char tmp_size[50];
	if (selected_count == 0) {
		print_human_readable_size(tmp_size, total_size);
		sprintf(window_status, "%d item%s (%s)", file_pointers_len, file_pointers_len == 1 ? "" : "s", tmp_size);
	} else if (selected_count == 1) {
		print_human_readable_size(tmp_size, selected->size);
		sprintf(window_status, "\"%s\" (%s) %s", selected->name, tmp_size, selected->filetype);
	} else {
		print_human_readable_size(tmp_size, selected_size);
		sprintf(window_status, "%d items selected (%s)", selected_count, tmp_size);
	}
}

/**
 * Read the contents of a directory into the icon view.
 */
static void load_directory(const char * path, int modifies_history) {

	/* Free the current icon view entries */
	DIR * dirp = opendir(path);

	if (!dirp) {
		/*
		 * Display a warning dialog with the appropriate error message for
		 * why this directory failed to load. XXX This uses `showdialog`
		 * but it should use a dialog library like with the buttons.
		 */
		if (!fork()) {
			char tmp[512];
			sprintf(tmp, "Could not open directory \"%s\": %s", path, strerror(errno));
			char * args[] = {"showdialog","File Browser","/usr/share/icons/48/folder.png",tmp,NULL};
			execvp(args[0],args);
			exit(0);
		}
		return;
	}

	/* Free the previously loaded directory */
	if (file_pointers) {
		for (int i = 0; i < file_pointers_len; ++i) {
			free(file_pointers[i]);
		}
		free(file_pointers);
	}

	if (modifies_history) {
		/* Clear forward history */
		list_destroy(history_forward);
		list_free(history_forward);
		free(history_forward);
		history_forward = list_create();
		/* Append current pointer */
		if (current_directory) {
			list_insert(history_back, strdup(current_directory));
		}
	}

	if (current_directory) {
		free(current_directory);
	}

	/* Set button displays appropriately */
	_button_disabled[0] = !(history_back->length);    /* Back */
	_button_disabled[1] = !(history_forward->length); /* Forward */
	_button_disabled[2] = 0; /* Up */
	_button_disabled[3] = 0; /* Home */

	char * home = getenv("HOME");
	if (home && !strcmp(path, home)) {
		/* If the current directory is the user's homedir, present it that way in the title */
		set_title("Home");
		/* Disable the 'go home' button */
		_button_disabled[3] = 1;
	} else if (!strcmp(path, "/")) {
		set_title("File System");
		/* If this is the root of the file system, disable the up button */
		_button_disabled[2] = 1;
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

	strcpy(nav_bar, current_directory);

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

			f->size = statbuf.st_size;

			/* Read link target for symlinks */
			if (S_ISLNK(statbuf.st_mode)) {
				memcpy(&statbufl, &statbuf, sizeof(struct stat));
				stat(tmp, &statbuf);
				readlink(tmp, f->link, 256);
			} else {
				f->link[0] = '\0';
			}

			f->launcher[0] = '\0';
			f->filetype[0] = '\0';
			f->selected = 0;

			if (S_ISDIR(statbuf.st_mode)) {
				/* Directory */
				sprintf(f->icon, "folder");
				sprintf(f->filetype, "Directory");
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
					sprintf(f->filetype, "Launcher");
					sprintf(f->filename, "%s", ent->d_name);
					f->type = 2;
				} else {
					/* Handle various file types */
					if (has_extension(f, ".c")) {
						sprintf(f->icon, "c");
						sprintf(f->filetype, "C Source");
					} else if (has_extension(f, ".h")) {
						sprintf(f->icon, "h");
						sprintf(f->filetype, "C Header");
					} else if (has_extension(f, ".bmp")) {
						sprintf(f->icon, "image");
						sprintf(f->launcher, "exec imgviewer");
						sprintf(f->filetype, "Bitmap Image");
					} else if (has_extension(f, ".tga")) {
						sprintf(f->icon, "image");
						sprintf(f->launcher, "exec imgviewer");
						sprintf(f->filetype, "Targa Image");
					} else if (has_extension(f, ".jpg") || has_extension(f,".jpeg")) {
						sprintf(f->icon, "image");
						sprintf(f->launcher, "exec imgviewer");
						sprintf(f->filetype, "JPEG Image");
					} else if (has_extension(f, ".png")) {
						sprintf(f->icon, "image");
						sprintf(f->launcher, "exec imgviewer");
						sprintf(f->filetype, "Portable Network Graphics Image");
					} else if (has_extension(f, ".sdf")) {
						sprintf(f->icon, "font");
						sprintf(f->filetype, "SDF Font");
						/* TODO: Font viewer for SDF and TrueType */
					} else if (has_extension(f, ".ttf")) {
						sprintf(f->icon, "font");
						sprintf(f->filetype, "TrueType Font");
					} else if (has_extension(f, ".tgz") || has_extension(f, ".tar.gz")) {
						sprintf(f->icon, "package");
						sprintf(f->filetype, "Compressed Archive File");
					} else if (has_extension(f, ".tar")) {
						sprintf(f->icon, "package");
						sprintf(f->filetype, "Archive File");
					} else if (has_extension(f, ".sh")) {
						sprintf(f->icon, "sh");
						if (statbuf.st_mode & 0111) {
							/* Make executable */
							sprintf(f->launcher, "SELF");
							sprintf(f->filetype, "Executable Shell Script");
						} else {
							sprintf(f->filetype, "Shell Script");
						}
					} else if (has_extension(f, ".py")) {
						sprintf(f->icon, "py");
						if (statbuf.st_mode & 0111) {
							/* Make executable */
							sprintf(f->launcher, "SELF");
							sprintf(f->filetype, "Executable Python Script");
						} else {
							sprintf(f->filetype, "Python Script");
						}
					} else if (has_extension(f, ".ko")) {
						sprintf(f->icon, "file");
						sprintf(f->filetype, "Kernel Module");
					} else if (has_extension(f, ".o")) {
						sprintf(f->icon, "file");
						sprintf(f->filetype, "Object File");
					} else if (has_extension(f, ".so")) {
						sprintf(f->icon, "file");
						sprintf(f->filetype, "Shared Object File");
					} else if (has_extension(f, ".S")) {
						sprintf(f->icon, "file");
						sprintf(f->filetype, "Assembly Source");
					} else if (has_extension(f, ".ld")) {
						sprintf(f->icon, "file");
						sprintf(f->filetype, "Linker Script");
					} else if (statbuf.st_mode & 0111) {
						/* Executable files - use their name for their icon, and launch themselves. */
						sprintf(f->icon, "%s", f->name);
						sprintf(f->launcher, "SELF");
						sprintf(f->filetype, "Executable");
					} else {
						sprintf(f->icon, "file");
						sprintf(f->filetype, "File");
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

	update_status();

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
	} else if (view_mode == VIEW_MODE_LIST) {
		FILE_PTR_WIDTH = 1;
		FILE_WIDTH = (ctx->width - bounds.width);
	} else {
		FILE_PTR_WIDTH = (ctx->width - bounds.width) / FILE_WIDTH;
	}

	/* Calculate required height to fit files */
	int calculated_height = (file_pointers_len / FILE_PTR_WIDTH + 1) * FILE_HEIGHT;

	/* Create buffer */
	contents_sprite = create_sprite(FILE_PTR_WIDTH * FILE_WIDTH, calculated_height, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);

	/* Draw file entries */
	redraw_files();
}

#define BUTTON_SPACE 34
#define BUTTON_COUNT 4
/**
 * Render toolbar buttons
 */
static void _draw_buttons(struct decor_bounds bounds) {

	/* Draws the toolbar background as a gradient; XXX hardcoded theme details */
	uint32_t gradient_top = rgb(59,59,59);
	uint32_t gradient_bot = rgb(40,40,40);
	for (int i = 0; i < 36; ++i) {
		uint32_t c = interp_colors(gradient_top, gradient_bot, i * 255 / 36);
		draw_rectangle(ctx, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT + i,
				BUTTON_SPACE * BUTTON_COUNT, 1, c);
	}

	int x = 0;
	int i = 0;
#define draw_button(label) do { \
	struct TTKButton _up = {bounds.left_width + 2 + x,bounds.top_height + MENU_BAR_HEIGHT + 2,32,32,"\033" label,_button_hilights[i] | (_button_disabled[i] << 8)}; \
	ttk_button_draw(ctx, &_up); \
	x += BUTTON_SPACE; i++; } while (0)

	/* Draw actual buttons */
	draw_button("back");
	draw_button("forward");
	draw_button("up");
	draw_button("home");
}

/**
 * Determine what character offset the cursor should be at for
 * a given X coordinate.
 */
static void _figure_out_navbar_cursor(int x, struct decor_bounds bounds) {
	x = x - bounds.left_width - 2 - BUTTON_SPACE * BUTTON_COUNT - 5;
	if (x <= 0) {
		nav_bar_cursor_x = 0;
		return;
	}

	char * tmp = strdup(nav_bar);
	int candidate = 0;
	while (*tmp && x + 2 < (candidate = draw_sdf_string_width(tmp, 16, SDF_FONT_THIN))) {
		tmp[strlen(tmp)-1] = '\0';
	}
	nav_bar_cursor_x = candidate + 2;
	nav_bar_cursor = strlen(tmp);
	free(tmp);
}

/**
 * Recalculate the location of the cursor indicator bar
 * based on the current cursor character offset;
 * also handles cursor bounds within the text
 * (eg. to avoid cursor moving beyond the beginning)
 */
static void _recalculate_nav_bar_cursor(void) {
	if (nav_bar_cursor < 0) {
		nav_bar_cursor = 0;
	}
	if (nav_bar_cursor > (int)strlen(nav_bar)) {
		nav_bar_cursor = strlen(nav_bar);
	}
	char * tmp = strdup(nav_bar);
	tmp[nav_bar_cursor] = '\0';
	nav_bar_cursor_x = draw_sdf_string_width(tmp, 16, SDF_FONT_THIN) + 2;
	free(tmp);
}

/**
 * Draw the navigation input box.
 */
static void _draw_nav_bar(struct decor_bounds bounds) {

	/* Draw toolbar background */
	uint32_t gradient_top = rgb(59,59,59);
	uint32_t gradient_bot = rgb(40,40,40);
	int x = BUTTON_SPACE * BUTTON_COUNT;

	for (int i = 0; i < 36; ++i) {
		uint32_t c = interp_colors(gradient_top, gradient_bot, i * 255 / 36);
		draw_rectangle(ctx, bounds.left_width + BUTTON_SPACE * BUTTON_COUNT, bounds.top_height + MENU_BAR_HEIGHT + i,
				ctx->width - bounds.width - BUTTON_SPACE * BUTTON_COUNT, 1, c);
	}

	/* Draw input box */
	if (nav_bar_focused) {
		struct gradient_definition edge = {28, bounds.top_height + MENU_BAR_HEIGHT + 3, rgb(0,120,220), rgb(0,120,220)};
		draw_rounded_rectangle_pattern(ctx, bounds.left_width + 2 + x + 1, bounds.top_height + MENU_BAR_HEIGHT + 4, main_window->width - bounds.width - x - 6, 26, 4, gfx_vertical_gradient_pattern, &edge);
		draw_rounded_rectangle(ctx, bounds.left_width + 2 + x + 3, bounds.top_height + MENU_BAR_HEIGHT + 6, main_window->width - bounds.width - x - 10, 22, 3, rgb(250,250,250));
	} else {
		struct gradient_definition edge = {28, bounds.top_height + MENU_BAR_HEIGHT + 3, rgb(90,90,90), rgb(110,110,110)};
		draw_rounded_rectangle_pattern(ctx, bounds.left_width + 2 + x + 1, bounds.top_height + MENU_BAR_HEIGHT + 4, main_window->width - bounds.width - x - 6, 26, 4, gfx_vertical_gradient_pattern, &edge);
		draw_rounded_rectangle(ctx, bounds.left_width + 2 + x + 2, bounds.top_height + MENU_BAR_HEIGHT + 5, main_window->width - bounds.width - x - 8, 24, 3, rgb(250,250,250));
	}

	/* Draw the nav bar text, ellipsified if needed */
	int max_width = main_window->width - bounds.width - x - 12;
	char * name = ellipsify(nav_bar, 16, SDF_FONT_THIN, max_width, NULL);
	draw_sdf_string(ctx, bounds.left_width + 2 + x + 5, bounds.top_height + MENU_BAR_HEIGHT + 8, name, 16, rgb(0,0,0), SDF_FONT_THIN);
	free(name);

	if (nav_bar_focused) {
		/* Draw cursor indicator at cursor_x */
		draw_line(ctx,
				bounds.left_width + 2 + x + 5 + nav_bar_cursor_x,
				bounds.left_width + 2 + x + 5 + nav_bar_cursor_x,
				bounds.top_height + MENU_BAR_HEIGHT + 8,
				bounds.top_height + MENU_BAR_HEIGHT + 8 + 15,
				rgb(0,0,0));
	}
}

#define STATUS_HEIGHT 24
/**
 * Draw the status bar at the bottom of the window
 */
static void _draw_status(struct decor_bounds bounds) {

	/* Background gradient */
	uint32_t gradient_top = rgb(80,80,80);
	uint32_t gradient_bot = rgb(59,59,59);
	draw_rectangle(ctx, bounds.left_width, ctx->height - bounds.bottom_height - STATUS_HEIGHT,
			ctx->width - bounds.width, 1, rgb(110,110,110) );
	for (int i = 1; i < STATUS_HEIGHT; ++i) {
		uint32_t c = interp_colors(gradient_top, gradient_bot, i * 255 / STATUS_HEIGHT);
		draw_rectangle(ctx, bounds.left_width, ctx->height - bounds.bottom_height - STATUS_HEIGHT + i,
				ctx->width - bounds.width, 1, c );
	}


	/* Text with draw shadow */
	{
		sprite_t * _tmp_s = create_sprite(ctx->width - bounds.width - 4, STATUS_HEIGHT-3, ALPHA_EMBEDDED);
		gfx_context_t * _tmp = init_graphics_sprite(_tmp_s);

		draw_fill(_tmp, rgba(0,0,0,0));
		draw_sdf_string(_tmp, 1, 1, window_status, 16, rgb(0,0,0), SDF_FONT_THIN);
		blur_context_box(_tmp, 4);

		draw_sdf_string(_tmp, 0, 0, window_status, 16, rgb(255,255,255), SDF_FONT_THIN);

		free(_tmp);
		draw_sprite(ctx, _tmp_s, bounds.left_width + 4, ctx->height - bounds.bottom_height - STATUS_HEIGHT + 3);
		sprite_free(_tmp_s);
	}
}

/**
 * Redraw the navigation input box (while typing)
 */
static void _redraw_nav_bar(void) {
	struct decor_bounds bounds;
	_decor_get_bounds(main_window, &bounds);
	_draw_nav_bar(bounds);
	flip(ctx);
	yutani_flip(yctx, main_window);
}

/**
 * navbar: Text editing helpers for ^W, deletes one directory element
 */
static void nav_bar_backspace_word(void) {
	if (!*nav_bar) return;
	if (nav_bar_cursor == 0) return;

	char * after = strdup(&nav_bar[nav_bar_cursor]);

	if (nav_bar[nav_bar_cursor-1] == '/') {
		nav_bar[nav_bar_cursor-1] = '\0';
		nav_bar_cursor--;
	}
	while (nav_bar_cursor && nav_bar[nav_bar_cursor-1] != '/') {
		nav_bar[nav_bar_cursor-1] = '\0';
		nav_bar_cursor--;
	}

	strcat(nav_bar, after);
	free(after);

	_recalculate_nav_bar_cursor();
	_redraw_nav_bar();
}

/**
 * navbar: Text editing helper for backspace, deletes one character
 */
static void nav_bar_backspace(void) {
	if (nav_bar_cursor == 0) return;

	char * after = strdup(&nav_bar[nav_bar_cursor]);

	nav_bar[nav_bar_cursor-1] = '\0';
	nav_bar_cursor--;

	strcat(nav_bar, after);
	free(after);

	_recalculate_nav_bar_cursor();
	_redraw_nav_bar();
}

/**
 * navbar: Text editing helper for inserting characters
 */
static void nav_bar_insert_char(char c) {
	char * tmp = strdup(nav_bar);
	tmp[nav_bar_cursor] = '\0';
	char * after = strdup(&nav_bar[nav_bar_cursor]);
	sprintf(nav_bar, "%s%c%s", tmp, c, after);
	free(tmp);
	free(after);

	nav_bar_cursor += 1;
	_recalculate_nav_bar_cursor();
	_redraw_nav_bar();
}

/**
 * navbar: Move editing cursor one character left
 */
static void nav_bar_cursor_left(void) {
	nav_bar_cursor--;
	_recalculate_nav_bar_cursor();
	_redraw_nav_bar();
}

/**
 * navbar: Move editing cursor one character right
 */
static void nav_bar_cursor_right(void) {
	nav_bar_cursor++;
	_recalculate_nav_bar_cursor();
	_redraw_nav_bar();
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
		if (wallpaper_old) {
			draw_sprite(ctx, wallpaper_old, 0, 0);
			uint64_t ellapsed = precise_time_since(timer);
			if (ellapsed > 1000) {
				free(wallpaper_old);
				wallpaper_old = NULL;
				draw_sprite(ctx, wallpaper_buffer, 0, 0);
				restart = 1; /* quietly restart */
			} else {
				draw_sprite_alpha(ctx, wallpaper_buffer, 0, 0, (float)ellapsed / 1000.0);
			}
		} else {
			draw_sprite(ctx, wallpaper_buffer, 0, 0);
		}
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

		/* Draw toolbar */
		_draw_buttons(bounds);
		_draw_nav_bar(bounds);

		_draw_status(bounds);
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
		if (wallpaper_old) {
			free(wallpaper_old);
		}
		wallpaper_old = wallpaper_buffer;
		timer = precise_current_time();
	}

	/* Open the wallpaper */
	sprite_t * wallpaper = malloc(sizeof(sprite_t));

	char * wallpaper_path = WALLPAPER_PATH;
	int free_it = 0;
	char * home = getenv("HOME");
	if (home) {
		char tmp[512];
		sprintf(tmp, "%s/.wallpaper.conf", home);
		FILE * c = fopen(tmp, "r");
		if (c) {
			char line[1024];
			while (!feof(c)) {
				fgets(line, 1024, c);
				char * nl = strchr(line, '\n');
				if (nl) *nl = '\0';
				if (line[0] == ';') {
					continue;
				}
				if (strstr(line, "wallpaper=") == line) {
					free_it = 1;
					wallpaper_path = strdup(line+strlen("wallpaper="));
					break;
				}
			}
			fclose(c);
		}
	}

	load_sprite(wallpaper, wallpaper_path);

	if (free_it) {
		free(wallpaper_path);
	}

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

	if (w < 300 || h < 300) {
		yutani_window_resize_offer(yctx, main_window, w < 300 ? 300 : w, h < 300 ? 300 : h);
		return;
	}

	int width_changed = (main_window->width != (unsigned int)w);

	yutani_window_resize_accept(yctx, main_window, w, h);
	reinit_graphics_yutani(ctx, main_window);

	struct decor_bounds bounds;
	_decor_get_bounds(main_window, &bounds);

	/* Recalculate available size */
	available_height = ctx->height - menu_bar_height - bounds.height - (is_desktop_background ? 0 : STATUS_HEIGHT);
	fprintf(stderr, "available_height = %d; bounds.bottom_height = %d, (isd...) = %d\n", available_height, bounds.bottom_height, (is_desktop_background ? 0 : STATUS_HEIGHT));

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
	load_directory(_entry->action, 1);
	reinitialize_contents();
	redraw_window();
}

/* Go > Up */
static void _menu_action_up(struct MenuEntry * entry) {
	/* go up */
	char * tmp = strdup(current_directory);
	char * dir = dirname(tmp);
	load_directory(dir, 1);
	reinitialize_contents();
	redraw_window();
}

/* [Context] > Refresh */
static void _menu_action_refresh(struct MenuEntry * entry) {
	char * tmp = strdup(current_directory);
	load_directory(tmp, 0);
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
	char * clipboard = malloc(output_size+1); /* last nil */
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
	strcat(about_cmd, "about \"About File Browser\" /usr/share/icons/48/folder.png \"ToaruOS File Browser\" \"(C) 2018 K. Lange\n-\nPart of ToaruOS, which is free software\nreleased under the NCSA/University of Illinois\nlicense.\n-\n%https://toaruos.org\n%https://github.com/klange/toaruos\" ");
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
			load_directory(tmp, 1);
			reinitialize_contents();
			redraw_window();
		}
	} else if (f->launcher[0]) {
		char tmp[4096];
		if (!strcmp(f->launcher, "SELF")) {
			/* "SELF" launchers are for binaries. */
			sprintf(tmp, "exec ./%s", f->name);
		} else {
			/* Other launchers should take file names as arguments.
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

static void _menu_action_select_all(struct MenuEntry * self) {
	for (int i = 0; i < file_pointers_len; ++i) {
		file_pointers[i]->selected = 1;
	}
	reinitialize_contents();
	update_status();
	redraw_window();
}

/**
 * Set the view mode for the file view
 * We support three modes:
 * - Icons: Standard view, label centered below icon.
 * - Tiles: Like Windows Explorer, labels left-aligned to the right of
 *          the icon, with extra details also displayed. Bold file name.
 * - List:  One-line-per-file, icon on the left, left aligne file name.
 */
static void set_view_mode(int mode) {

	switch (mode) {
		default:
		case VIEW_MODE_ICONS:
			FILE_HEIGHT = 80;
			FILE_WIDTH = 100;
			view_mode = VIEW_MODE_ICONS;
			break;
		case VIEW_MODE_TILES:
			FILE_HEIGHT = 70;
			FILE_WIDTH = 260;
			view_mode = VIEW_MODE_TILES;
			break;
		case VIEW_MODE_LIST:
			FILE_HEIGHT = 24;
			FILE_WIDTH  = 100; /* Readjusts elsewhere */
			view_mode = VIEW_MODE_LIST;
			break;
	}

	reinitialize_contents();
	redraw_window();
}

/**
 * Dropdown action handler for view mode entries;
 * use menuitem action to determine which view mode to set.
 */
static void _menu_action_view_mode(struct MenuEntry * entry) {
	struct MenuEntry_Normal * _entry = (void*)entry;
	int mode = VIEW_MODE_ICONS;
	if (!strcmp(_entry->action, "icons")) {
		mode = VIEW_MODE_ICONS;
	} else if (!strcmp(_entry->action, "tiles")) {
		mode = VIEW_MODE_TILES;
	} else if (!strcmp(_entry->action, "list")) {
		mode = VIEW_MODE_LIST;
	}
	set_view_mode(mode);
}

/**
 * Receive pastes, which are presumed to be file names of files
 * which have been copied and should now be pasted into a new
 * directory; will not overwrite if pasted into the same directory
 * or a directory with a file with the same name.
 *
 * XXX: Calls `cp` to perform actual copy.
 *
 * TODO: Actually check if clipboard contains a file name.
 * TODO: Handle pastes into the navbar of arbitrary text.
 */
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
			sprintf(message, "showdialog \"File Browser\" /usr/share/icons/48/folder.png \"Not overwriting file '%s'.\"", cheap_basename);
			launch_application(message);
		} else {
			char cp[1024];
			sprintf(cp, "cp -r \"%s\" \"%s\"", file, current_directory);
			if (system(cp)) {
				char message[4096];
				sprintf(message, "showdialog \"File Browser\" /usr/share/icons/48/folder.png \"Error copying file '%s'.\"", cheap_basename);
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
	if (!(modifiers & YUTANI_KEY_MODIFIER_CTRL)) {
		for (int i = 0; i < file_pointers_len; ++i) {
			if (file_pointers[i] != f && file_pointers[i]->selected) {
				file_pointers[i]->selected = 0;
				clear_offset(i);
				draw_file(file_pointers[i], i);
			}
		}
	}

	update_status();

	/* Redraw the file */
	clear_offset(hilighted_offset);
	draw_file(f, hilighted_offset);

	/* And repaint the window */
	redraw_window();
}

/**
 * Handle button hover highlights
 */
static int _down_button = -1;
static void _set_hilight(int index, int hilight) {
	int _update = 0;
	if (_button_hover != index || (_button_hover == index && index != -1 && _button_hilights[index] != hilight)) {
		if (_button_hover != -1 && _button_hilights[_button_hover] != 3) {
			_button_hilights[_button_hover] = 3;
			_update = 1;
		}
		_button_hover = index;
		if (index != -1 && !_button_disabled[index]) {
			_button_hilights[_button_hover] = hilight;
			_update = 1;
		}
		if (_update) {
			redraw_window();
		}
	}
}

/**
 * Handle toolbar button clicking
 */
static void _handle_button_press(int index) {
	if (index != -1 && _button_disabled[index]) return; /* can't click disabled buttons */
	switch (index) {
		case 0:
			/* Back */
			if (history_back->length) {
				list_insert(history_forward, strdup(current_directory));
				node_t * next = list_pop(history_back);
				load_directory(next->value, 0);
				free(next->value);
				free(next);
				reinitialize_contents();
				redraw_window();
			}
			break;
		case 1:
			/* Forward */
			if (history_forward->length) {
				list_insert(history_back, strdup(current_directory));
				node_t * next = list_pop(history_forward);
				load_directory(next->value, 0);
				free(next->value);
				free(next);
				reinitialize_contents();
				redraw_window();
			}
			break;
		case 2:
			/* Up */
			_menu_action_up(NULL);
			break;
		case 3:
			/* Home */
			{
				struct MenuEntry_Normal _fake = {.action = getenv("HOME") };
				_menu_action_navigate(&_fake);
			}
			break;
		default:
			/* ??? */
			break;
	}
}

/**
 * Scroll file view up
 */
static void _scroll_up(void) {
	scroll_offset -= SCROLL_AMOUNT;
	if (scroll_offset < 0) {
		scroll_offset = 0;
	}
}

/**
 * Scroll file view down
 */
static void _scroll_down(void) {
	if (available_height > contents->height) {
		scroll_offset = 0;
	} else {
		scroll_offset += SCROLL_AMOUNT;
		if (scroll_offset > contents->height - available_height) {
			scroll_offset = contents->height - available_height;
		}
	}
}

/**
 * Desktop mode responsds to sig_usr2 by returning to
 * the bottom of the Z-order stack.
 */
static void sig_usr2(int sig) {
	yutani_set_stack(yctx, main_window, YUTANI_ZORDER_BOTTOM);
	_menu_action_refresh(NULL);
	signal(SIGUSR2, sig_usr2);
}

/**
 * Desktop mode responds to sig_usr1 by resizing the window
 * to the current display size.
 */
static void sig_usr1(int sig) {
	yutani_window_resize_offer(yctx, main_window, yctx->display_width, yctx->display_height);
	signal(SIGUSR1, sig_usr1);
}

/**
 * Accept keyboard arrows left/right/up/down and select the appropriate
 * file in the file view based on the currently selected file; if multiple
 * files are currently selected, the first one (up/left) is used as the basis
 * for the new selection.
 */
static void arrow_select(int x, int y) {
	if (!file_pointers_len) return;

	/* Find first selected */
	int selected = -1;
	for (int i = 0; i < file_pointers_len; ++i) {
		if (file_pointers[i]->selected) {
			selected = i;
		}
		file_pointers[i]->selected = 0;
	}

	if (selected == -1) {
		selected = 0;
	} else {
		int offset_y = selected / FILE_PTR_WIDTH;
		int offset_x = selected % FILE_PTR_WIDTH;

		offset_y += y;
		offset_x += x;

		if (offset_x >= FILE_PTR_WIDTH) {
			offset_x = FILE_PTR_WIDTH - 1;
		}
		if (offset_x < 0) {
			offset_x = 0;
		}
		if (offset_y < 0) {
			offset_y = 0;
		}

		selected = offset_y * FILE_PTR_WIDTH + offset_x;
		if (selected >= file_pointers_len) selected = file_pointers_len - 1;
		if (selected < 0) selected = 0;
	}

	int offset_y = selected / FILE_PTR_WIDTH;
	if (offset_y * FILE_HEIGHT < scroll_offset) {
		scroll_offset = offset_y * FILE_HEIGHT;
	}
	if (offset_y * FILE_HEIGHT + FILE_HEIGHT > scroll_offset + available_height) {
		scroll_offset = offset_y * FILE_HEIGHT + FILE_HEIGHT - available_height;
	}

	file_pointers[selected]->selected = 1;
	update_status();
	reinitialize_contents();
	redraw_window();
}

int main(int argc, char * argv[]) {

	yctx = yutani_init();
	init_decorations();

	int arg_ind = 1;

	if (argc > 1 && !strcmp(argv[1], "--wallpaper")) {
		is_desktop_background = 1;
		menu_bar_height = 0;
		signal(SIGUSR1, sig_usr1);
		signal(SIGUSR2, sig_usr2);
		draw_background(yctx->display_width, yctx->display_height);
		main_window = yutani_window_create_flags(yctx, yctx->display_width, yctx->display_height, YUTANI_WINDOW_FLAG_NO_STEAL_FOCUS);
		yutani_window_move(yctx, main_window, 0, 0);
		yutani_set_stack(yctx, main_window, YUTANI_ZORDER_BOTTOM);
		arg_ind++;
		FILE * f = fopen("/var/run/.wallpaper.pid", "w");
		fprintf(f, "%d\n", getpid());
		fclose(f);
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
	menu_insert(m, menu_create_normal(NULL,NULL,"Copy",_menu_action_copy));
	menu_insert(m, menu_create_normal(NULL,NULL,"Paste",_menu_action_paste));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal(NULL,NULL,"Select all",_menu_action_select_all));
	menu_set_insert(menu_bar.set, "edit", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("refresh",NULL,"Refresh", _menu_action_refresh));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal(NULL,"icons","Show Icons", _menu_action_view_mode));
	menu_insert(m, menu_create_normal(NULL,"tiles","Show Tiles", _menu_action_view_mode));
	menu_insert(m, menu_create_normal(NULL,"list","Show List", _menu_action_view_mode));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal(NULL,NULL,"Show Hidden Files", _menu_action_toggle_hidden));
	menu_set_insert(menu_bar.set, "view", m);

	m = menu_create(); /* Go */
	menu_insert(m, menu_create_normal("home",getenv("HOME"),"Home",_menu_action_navigate));
	menu_insert(m, menu_create_normal(NULL,"/","File System",_menu_action_navigate));
	menu_insert(m, menu_create_normal("up",NULL,"Up",_menu_action_up));
	menu_set_insert(menu_bar.set, "go", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	available_height = ctx->height - menu_bar_height - bounds.height - (is_desktop_background ? 0 : STATUS_HEIGHT);
	fprintf(stderr, "available_height = %d\n", available_height);

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

	history_back = list_create();
	history_forward = list_create();


	/* Load the current working directory */
	char tmp[1024];
	getcwd(tmp, 1024);
	load_directory(tmp, 1);

	/* Draw files */
	reinitialize_contents();
	redraw_window();

	while (application_running) {
		waitpid(-1, NULL, WNOHANG);
		int fds[1] = {fileno(yctx->sock)};
		int index = fswait2(1,fds,wallpaper_old ? 10 : 200);

		if (restart) {
			execvp(argv[0],argv);
			return 1;
		}

		if (index == 1) {
			if (wallpaper_old) {
				redraw_window();
			}
			continue;
		}

		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			int redraw = 0;
			if (menu_process_event(yctx, m)) {
				redraw = 1;
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
						if (ke->event.action == KEY_ACTION_DOWN && ke->wid == main_window->wid) {
							if (nav_bar_focused) {
								switch (ke->event.key) {
									case KEY_ESCAPE:
										nav_bar_focused = 0;
										redraw_window();
										break;
									case KEY_BACKSPACE:
										nav_bar_backspace();
										break;
									case KEY_CTRL_W:
										nav_bar_backspace_word();
										break;
									case '\n':
										nav_bar_focused = 0;
										char * tmp = strdup(nav_bar);
										load_directory(tmp, 1);
										reinitialize_contents();
										redraw_window();
										break;
									default:
										if (isgraph(ke->event.key)) {
											nav_bar_insert_char(ke->event.key);
										} else {
											switch (ke->event.keycode) {
												case KEY_ARROW_LEFT:
													nav_bar_cursor_left();
													break;
												case KEY_ARROW_RIGHT:
													nav_bar_cursor_right();
													break;
											}
										}
										break;
								}
								break;
							}
							switch (ke->event.keycode) {
								case KEY_PAGE_UP:
									_scroll_up();
									redraw = 1;
									break;
								case KEY_PAGE_DOWN:
									_scroll_down();
									redraw = 1;
									break;
								/* if not focused on anything focusable */
								case KEY_ARROW_DOWN:
									arrow_select(0,1);
									break;
								case KEY_ARROW_UP:
									arrow_select(0,-1);
									break;
								case KEY_ARROW_LEFT:
									arrow_select(-1,0);
									break;
								case KEY_ARROW_RIGHT:
									arrow_select(1,0);
									break;
								case KEY_BACKSPACE:
									_menu_action_up(NULL);
									break;
								case '\n':
									_menu_action_open(NULL);
									break;
								case 'l':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_CTRL && !is_desktop_background) {
										nav_bar_focused = 1;
										redraw_window();
									}
									break;
								case 'f':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT && !is_desktop_background) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[0]);
									}
									break;
								case 'e':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT && !is_desktop_background) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[1]);
									}
									break;
								case 'v':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT && !is_desktop_background) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[2]);
									}
									break;
								case 'g':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT && !is_desktop_background) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[3]);
									}
									break;
								case 'h':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT && !is_desktop_background) {
										menu_bar_show_menu(yctx,main_window,&menu_bar,-1,&menu_entries[4]);
									}
									break;
								case 'q':
									if (!is_desktop_background) {
										_menu_action_exit(NULL);
									}
									break;
								default:
									break;
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
							redraw_files();
							redraw = 1;
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

							if (menu_bar_height &&
								me->new_y > (int)(bounds.top_height + menu_bar_height - 36) &&
								me->new_y < (int)(bounds.top_height + menu_bar_height) &&
								me->new_x > (int)(bounds.left_width) &&
								me->new_x < (int)(main_window->width - bounds.right_width)) {

								int x = me->new_x - bounds.left_width - 2;
								if (x >= 0) {
									int i = x / 34;
									if (i < 4) {
										if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
											_set_hilight(i, 2);
											nav_bar_focused = 0;
											_down_button = i;
										} else if (me->command == YUTANI_MOUSE_EVENT_RAISE || me->command == YUTANI_MOUSE_EVENT_CLICK) {
											if (_down_button != -1 && _down_button == i) {
												_handle_button_press(i);
												_set_hilight(i, 1);
											}
											_down_button = -1;
										} else {
											if (!(me->buttons & YUTANI_MOUSE_BUTTON_LEFT)) {
												_set_hilight(i, 1);
											} else {
												if (_down_button == i) {
													_set_hilight(i, 2);
												} else if (_down_button != -1) {
													_set_hilight(_down_button, 3);
												}
											}
										}
									} else {
										_set_hilight(-1,0);
										if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
											nav_bar_focused = 1;
											_figure_out_navbar_cursor(me->new_x, bounds);
											redraw = 1;
										}
									}
								}
							} else {
								if (me->command == YUTANI_MOUSE_EVENT_DOWN) {
									if (nav_bar_focused) {
										nav_bar_focused = 0;
										redraw = 1;
									}
								}
								if (_button_hover != -1) {
									_button_hilights[_button_hover] = 3;
									_button_hover = -1;
									redraw = 1; /* Double redraw ??? */
								}
							}

							if (!is_desktop_background && me->new_y > (int)(main_window->height - bounds.bottom_height - STATUS_HEIGHT)) {

							} else if (me->new_y > (int)(bounds.top_height + menu_bar_height) &&
								me->new_y < (int)(main_window->height - bounds.bottom_height) &&
								me->new_x > (int)(bounds.left_width) &&
								me->new_x < (int)(main_window->width - bounds.right_width) &&
								me->command != YUTANI_MOUSE_EVENT_LEAVE) {
								if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
									/* Scroll up */
									_scroll_up();
									redraw = 1;
								} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
									_scroll_down();
									redraw = 1;
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
									redraw = 1;
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
											toggle_selected(hilighted_offset, me->modifiers);
										}
									} else {
										if (!(me->modifiers & YUTANI_KEY_MODIFIER_CTRL)) {
											for (int i = 0; i < file_pointers_len; ++i) {
												if (file_pointers[i]->selected) {
													file_pointers[i]->selected = 0;
													update_status();
													clear_offset(i);
													draw_file(file_pointers[i], i);
												}
											}
											redraw = 1;
										}
									}
								} else if (me->buttons & YUTANI_MOUSE_BUTTON_RIGHT) {
									if (!context_menu->window) {
										struct File * f = get_file_at_offset(hilighted_offset);
										if (f && !f->selected) {
											toggle_selected(hilighted_offset, me->modifiers);
										}
										menu_show_at(context_menu, main_window, me->new_x, me->new_y);
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
									redraw = 1;
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
			if (redraw || wallpaper_old) {
				redraw_window();
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
	}
}
