/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * ls
 *
 * Lists files in a directory, with nice color
 * output like any modern ls should have.
 */


#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <termios.h>
#include <pwd.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "lib/list.h"

#define MIN_COL_SPACING 2

#define EXE_COLOR		"1;32"
#define DIR_COLOR		"1;34"
#define SYMLINK_COLOR	"1;36"
#define REG_COLOR		"0"
#define MEDIA_COLOR		""
#define SYM_COLOR		""
#define BROKEN_COLOR	"1;"
#define DEVICE_COLOR	"1;33;40"
#define SETUID_COLOR	"37;41"

#define DEFAULT_TERM_WIDTH 0
#define DEFAULT_TERM_HEIGHT 0

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define LINE_LEN 4096

static int human_readable = 0;
static int stdout_is_tty = 1;
static int this_year = 0;
static int explicit_path_set = 0;
static int show_hidden = 0;
static int long_mode   = 0;
static int print_dir   = 0;
static int term_width = DEFAULT_TERM_WIDTH;
static int term_height = DEFAULT_TERM_HEIGHT;

struct tfile {
	char * name;
	struct stat statbuf;
	char * link;
	struct stat statbufl;
};

static const char * color_str(struct stat * sb) {
	if (S_ISDIR(sb->st_mode)) {
		/* Directory */
		return DIR_COLOR;
	} else if (S_ISLNK(sb->st_mode)) {
		/* Symbolic Link */
		return SYMLINK_COLOR;
	} else if (sb->st_mode & S_ISUID) {
		/* setuid - sudo, etc. */
		return SETUID_COLOR;
	} else if (sb->st_mode & 0111) {
		/* Executable */
		return EXE_COLOR;
	} else if (S_ISBLK(sb->st_mode) || S_ISCHR(sb->st_mode)) {
		/* Device file */
		return DEVICE_COLOR;
	} else {
		/* Regular file */
		return REG_COLOR;
	}
}

static int filecmp(const void * c1, const void * c2) {
	const struct tfile * d1 = *(const struct tfile **)c1;
	const struct tfile * d2 = *(const struct tfile **)c2;

	int a = S_ISDIR(d1->statbuf.st_mode);
	int b = S_ISDIR(d2->statbuf.st_mode);

	if (a == b) return strcmp(d1->name, d2->name);
	else if (a < b) return -1;
	else if (a > b) return 1;
}

static int filecmp_notypesort(const void * c1, const void * c2) {
	const struct tfile * d1 = *(const struct tfile **)c1;
	const struct tfile * d2 = *(const struct tfile **)c2;

	return strcmp(d1->name, d2->name);
}

static void print_entry(struct tfile * file, int colwidth) {
	const char * ansi_color_str = color_str(&file->statbuf);

	/* Print the file name */
	if (stdout_is_tty) {
		printf("\033[%sm%s\033[0m", ansi_color_str, file->name);
	} else {
		printf("%s", file->name);
	}

	/* Pad the rest of the column */
	for (int rem = colwidth - strlen(file->name); rem > 0; rem--) {
		printf(" ");
	}
}

static int print_username(char * _out, int uid) {

	struct passwd * p = getpwuid(uid);
	int out = 0;

	if (p) {
		out = sprintf(_out, "%s", p->pw_name);
	} else {
		out = sprintf(_out, "%d", uid);
	}

	endpwent();

	return out;
}

static int print_human_readable_size(char * _out, size_t s) {
	if (s >= 1<<20) {
		size_t t = s / (1 << 20);
		return sprintf(_out, "%d.%1dM", t, (s - t * (1 << 20)) / ((1 << 20) / 10));
	} else if (s >= 1<<10) {
		size_t t = s / (1 << 10);
		return sprintf(_out, "%d.%1dK", t, (s - t * (1 << 10)) / ((1 << 10) / 10));
	} else {
		return sprintf(_out, "%d", s);
	}
}

static void update_column_widths(int * widths, struct tfile * file) {
	char tmp[256];
	int n;

	/* Links */
	n = sprintf(tmp, "%d", file->statbuf.st_nlink);
	if (n > widths[0]) widths[0] = n;

	/* User */
	n = print_username(tmp, file->statbuf.st_uid);
	if (n > widths[1]) widths[1] = n;

	/* Group */
	n = print_username(tmp, file->statbuf.st_gid);
	if (n > widths[2]) widths[2] = n;

	/* File size */
	if (human_readable) {
		n = print_human_readable_size(tmp, file->statbuf.st_size);
	} else {
		n = sprintf(tmp, "%d", file->statbuf.st_size);
	}
	if (n > widths[3]) widths[3] = n;
}

static void print_entry_long(int * widths, struct tfile * file) {
	const char * ansi_color_str = color_str(&file->statbuf);

	/* file permissions */
	if (S_ISLNK(file->statbuf.st_mode))       { printf("l"); }
	else if (S_ISCHR(file->statbuf.st_mode))  { printf("c"); }
	else if (S_ISBLK(file->statbuf.st_mode))  { printf("b"); }
	else if (S_ISDIR(file->statbuf.st_mode))  { printf("d"); }
	else { printf("-"); }
	printf( (file->statbuf.st_mode & S_IRUSR) ? "r" : "-");
	printf( (file->statbuf.st_mode & S_IWUSR) ? "w" : "-");
	if (file->statbuf.st_mode & S_ISUID) {
		printf("s");
	} else {
		printf( (file->statbuf.st_mode & S_IXUSR) ? "x" : "-");
	}
	printf( (file->statbuf.st_mode & S_IRGRP) ? "r" : "-");
	printf( (file->statbuf.st_mode & S_IWGRP) ? "w" : "-");
	printf( (file->statbuf.st_mode & S_IXGRP) ? "x" : "-");
	printf( (file->statbuf.st_mode & S_IROTH) ? "r" : "-");
	printf( (file->statbuf.st_mode & S_IWOTH) ? "w" : "-");
	printf( (file->statbuf.st_mode & S_IXOTH) ? "x" : "-");

	printf( " %*d ", widths[0], file->statbuf.st_nlink); /* number of links, not supported */

	char tmp[100];
	print_username(tmp, file->statbuf.st_uid);
	printf("%-*s ", widths[1], tmp);
	print_username(tmp, file->statbuf.st_gid);
	printf("%-*s ", widths[2], tmp);

	if (human_readable) {
		print_human_readable_size(tmp, file->statbuf.st_size);
		printf("%*s ", widths[3], tmp);
	} else {
		printf("%*d ", widths[3], file->statbuf.st_size);
	}

	char time_buf[80];
	struct tm * timeinfo = localtime(&file->statbuf.st_mtime);
	if (timeinfo->tm_year == this_year) {
		strftime(time_buf, 80, "%b %d %H:%M", timeinfo);
	} else {
		strftime(time_buf, 80, "%b %d  %Y", timeinfo);
	}
	printf("%s ", time_buf);

	/* Print the file name */
	if (stdout_is_tty) {
		printf("\033[%sm%s\033[0m", ansi_color_str, file->name);
		if (S_ISLNK(file->statbuf.st_mode)) {
			const char * s = color_str(&file->statbufl);
			printf(" -> \033[%sm%s\033[0m", s, file->link);
		}
	} else {
		printf("%s", file->name);
		if (S_ISLNK(file->statbuf.st_mode)) {
			printf(" -> %s", file->link);
		}
	}

	printf("\n");
}

static void show_usage(int argc, char * argv[]) {
	printf(
			"ls - list files\n"
			"\n"
			"usage: %s [-lha] [path]\n"
			"\n"
			" -a     \033[3mlist all files (including . files)\033[0m\n"
			" -l     \033[3muse a long listing format\033[0m\n"
			" -h     \033[3mhuman-readable file sizes\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

static void display_tfiles(struct tfile ** ents_array, int numents) {
	if (long_mode) {
		int widths[4] = {0,0,0,0};
		for (int i = 0; i < numents; i++) {
			update_column_widths(widths, ents_array[i]);
		}
		for (int i = 0; i < numents; i++) {
			print_entry_long(widths, ents_array[i]);
		}
	} else {
		/* Determine the gridding dimensions */
		int ent_max_len = 0;
		for (int i = 0; i < numents; i++) {
			ent_max_len = MAX(ent_max_len, strlen(ents_array[i]->name));
		}

		int col_ext = ent_max_len + MIN_COL_SPACING;
		int cols = ((term_width - ent_max_len) / col_ext) + 1;

		/* Print the entries */

		for (int i = 0; i < numents;) {

			/* Columns */
			print_entry(ents_array[i++], ent_max_len);

			for (int j = 0; (i < numents) && (j < (cols-1)); j++) {
				printf("  ");
				print_entry(ents_array[i++], ent_max_len);
			}

			printf("\n");
		}
	}
}

static int display_dir(char * p) {
	/* Open the directory */
	DIR * dirp = opendir(p);
	if (dirp == NULL) {
		return 2;
	}

	if (print_dir) {
		printf("%s:\n", p);
	}

	/* Read the entries in the directory */
	list_t * ents_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (show_hidden || (ent->d_name[0] != '.')) {
			struct tfile * f = malloc(sizeof(struct tfile));

			f->name = strdup(ent->d_name);

			char tmp[strlen(p)+strlen(ent->d_name)+1];
			sprintf(tmp, "%s/%s", p, ent->d_name);
			int t = lstat(tmp, &f->statbuf);
			if (S_ISLNK(f->statbuf.st_mode)) {
				stat(tmp, &f->statbufl);
				f->link = malloc(4096);
				readlink(tmp, f->link, 4096);
			}

			list_insert(ents_list, (void *)f);
		}
		ent = readdir(dirp);
	}
	closedir(dirp);

	/* Now, copy those entries into an array (for sorting) */

	struct tfile ** file_arr = malloc(sizeof(struct tfile *) * ents_list->length);
	int index = 0;
	foreach(node, ents_list) {
		file_arr[index++] = (struct tfile *)node->value;
	}

	list_free(ents_list);

	qsort(file_arr, index, sizeof(struct tfile *), filecmp_notypesort);

	display_tfiles(file_arr, index);

	free(file_arr);

	return 0;
}

int main (int argc, char * argv[]) {

	/* Parse arguments */
	char * p = ".";

	if (argc > 1) {
		int index, c;
		while ((c = getopt(argc, argv, "ahl?")) != -1) {
			switch (c) {
				case 'a':
					show_hidden = 1;
					break;
				case 'h':
					human_readable = 1;
					break;
				case 'l':
					long_mode = 1;
					break;
				case '?':
					show_usage(argc, argv);
					return 0;
			}
		}

		if (optind < argc) {
			p = argv[optind];
		}
		if (optind + 1 < argc) {
			print_dir = 1;
		}
	}

	stdout_is_tty = isatty(STDOUT_FILENO);

	if (long_mode) {
		struct tm * timeinfo;
		struct timeval now;
		gettimeofday(&now, NULL); //time(NULL);
		timeinfo = localtime((time_t *)&now.tv_sec);
		this_year = timeinfo->tm_year;
	}

	if (stdout_is_tty) {
		struct winsize w;
		ioctl(1, TIOCGWINSZ, &w);
		term_width = w.ws_col;
		term_height = w.ws_row;
		term_width -= 1; /* And this just helps clean up our math */
	}

	int out = 0;

	if (argc == 1 || optind == argc) {
		display_dir(p);
	} else {
		list_t * files = list_create();
		while (p) {
			struct tfile * f = malloc(sizeof(struct tfile));

			f->name = p;
			int t = stat(p, &f->statbuf);

			if (t < 0) {
				printf("ls: cannot access %s: No such file or directory\n", p);
				free(f);
				out = 2;
			} else {
				list_insert(files, f);
			}

			optind++;
			if (optind >= argc) p = NULL;
			else p = argv[optind];
		}

		struct tfile ** file_arr = malloc(sizeof(struct tfile *) * files->length);
		int index = 0;
		foreach(node, files) {
			file_arr[index++] = (struct tfile *)node->value;
		}

		list_free(files);

		qsort(file_arr, index, sizeof(struct tfile *), filecmp);

		int first_directory = index;

		for (int i = 0; i < index; ++i) {
			if (S_ISDIR(file_arr[i]->statbuf.st_mode)) {
				first_directory = i;
				break;
			}
		}

		if (first_directory) {
			display_tfiles(file_arr, first_directory);
		}

		for (int i = first_directory; i < index; ++i) {
			if (i != 0) {
				printf("\n");
			}
			display_dir(file_arr[i]->name);
		}
	}

	return out;
}

/*
 * vim: tabstop=4
 * vim: shiftwidth=4
 * vim: noexpandtab
 */
