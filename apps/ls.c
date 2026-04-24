/**
 * @brief List files
 *
 * Lists files in a directory, with nice color
 * output like any modern ls should have.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <termios.h>
#include <time.h>
#include <pwd.h>
#include <wchar.h>
#include <getopt.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>

#define TRACE_APP_NAME "ls"
//#include "lib/trace.h"
#define TRACE(...)

#include <toaru/list.h>
#include <toaru/decodeutf8.h>

/*
 * Minimum padding between columns.
 *
 * This used to be 2. If you're building
 * from source, you can change it back.
 * 1 makes our output identical to macOS.
 */
#define MIN_COL_SPACING 1

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
static int show_hidden = 0;
static int long_mode   = 0;
static int print_dir   = 0;
static int term_width = DEFAULT_TERM_WIDTH;
static int term_height = DEFAULT_TERM_HEIGHT;
static int columns = 1;
static int in_order = 0;
static int show_inode = 0;
static int show_size = 0;
static int one_column = 0;
static int show_slash = 0;

struct tfile {
	char * name;
	struct stat statbuf;
	char * link;
	struct stat statbufl;
	int lstatres;
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
	} else if (S_ISBLK(sb->st_mode) || S_ISCHR(sb->st_mode) || S_ISFIFO(sb->st_mode)) {
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
	return 0; /* impossible ? */
}

static int filecmp_notypesort(const void * c1, const void * c2) {
	const struct tfile * d1 = *(const struct tfile **)c1;
	const struct tfile * d2 = *(const struct tfile **)c2;

	return strcmp(d1->name, d2->name);
}

static int display_width_of_string(const char * str) {
	int out = 0;
	uint32_t c, state = 0;
	for (uint8_t *s = (uint8_t*)str; *s; s++) {
		if (!decode(&state, &c, *s)) out += wcwidth(c);
		else if (state == UTF8_REJECT) state = 0;
	}
	return out;
}

static size_t as_1k_blocks(off_t size) {
	size_t blocks = size / 1024;
	if (size % 1024) blocks += 1;
	return blocks;
}

static int print_human_readable_size(char * _out, size_t avail, size_t s);

static void prefixes(int *colwidth, struct tfile * file) {
	/* Prefixed stuff */
	if (show_inode) printf("%*zu ", colwidth[1]-1, file->statbuf.st_ino);
	if (show_size) {
		size_t size = as_1k_blocks(file->statbuf.st_size);
		if (human_readable) {
			char tmp[100];
			print_human_readable_size(tmp, 100, size * 1024);
			printf("%*s ", colwidth[2]-1, tmp);
		} else {
			printf("%*zu ", colwidth[2]-1, size);
		}
	}
}

static void print_entry(struct tfile * file, int *colwidth) {
	prefixes(colwidth,file);

	const char * ansi_color_str = color_str(&file->statbuf);

	/* Print the file name */
	if (stdout_is_tty) {
		printf("\033[%sm%s\033[0m", ansi_color_str, file->name);
	} else {
		printf("%s", file->name);
	}

	/* These classifiers are accounted for in width calculations separately */
	if (show_slash) {
		if (S_ISDIR(file->statbuf.st_mode)) {
			printf("/");
		} else if (show_slash > 1 && S_ISLNK(file->statbuf.st_mode)) {
			printf("@");
		} else if (show_slash > 1 && S_ISFIFO(file->statbuf.st_mode)) {
			printf("|");
		} else if (show_slash > 1 && (file->statbuf.st_mode & 0111)) {
			printf("*");
		} else if (show_slash > 1 && S_ISSOCK(file->statbuf.st_mode)) {
			printf("=");
		} else {
			printf(" ");
		}
	}

	/* Pad the rest of the column */
	for (int rem = colwidth[0] - display_width_of_string(file->name); rem > 0; rem--) {
		printf(" ");
	}
}

static int print_username(char * _out, size_t avail, int uid) {

	TRACE("getpwuid");
	struct passwd * p = getpwuid(uid);
	int out = 0;

	if (p) {
		TRACE("p is set");
		out = snprintf(_out, avail, "%s", p->pw_name);
	} else {
		TRACE("p is not set");
		out = snprintf(_out, avail, "%d", uid);
	}

	endpwent();

	return out;
}

static int print_human_readable_size(char * _out, size_t avail, size_t s) {
	if (s >= 1<<20) {
		size_t t = s / (1 << 20);
		return snprintf(_out, avail, "%d.%1dM", (int)t, (int)(s - t * (1 << 20)) / ((1 << 20) / 10));
	} else if (s >= 1<<10) {
		size_t t = s / (1 << 10);
		return snprintf(_out, avail, "%d.%1dK", (int)t, (int)(s - t * (1 << 10)) / ((1 << 10) / 10));
	} else {
		return snprintf(_out, avail, "%d", (int)s);
	}
}

static void update_column_widths(int * widths, struct tfile * file) {
	int n;

	/* Links */
	TRACE("links");
	n = snprintf(NULL, 0, "%d", file->statbuf.st_nlink);
	if (n > widths[0]) widths[0] = n;

	/* User */
	TRACE("user");
	n = print_username(NULL, 0, file->statbuf.st_uid);
	if (n > widths[1]) widths[1] = n;

	/* Group */
	TRACE("group");
	n = print_username(NULL, 0, file->statbuf.st_gid);
	if (n > widths[2]) widths[2] = n;

	/* File size */
	TRACE("file size");
	if (human_readable) {
		n = print_human_readable_size(NULL, 0, file->statbuf.st_size);
	} else {
		n = snprintf(NULL, 0, "%d", (int)file->statbuf.st_size);
	}
	if (n > widths[3]) widths[3] = n;
}

static void print_entry_long(int * widths, int * colwidth, struct tfile * file) {
	prefixes(colwidth, file);

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
	print_username(tmp, 100, file->statbuf.st_uid);
	printf("%-*s ", widths[1], tmp);
	print_username(tmp, 100, file->statbuf.st_gid);
	printf("%-*s ", widths[2], tmp);

	if (human_readable) {
		print_human_readable_size(tmp, 100, file->statbuf.st_size);
		printf("%*s ", widths[3], tmp);
	} else {
		printf("%*d ", widths[3], (int)file->statbuf.st_size);
	}

	char time_buf[80];
	struct tm * timeinfo = localtime((time_t*)&file->statbuf.st_mtime);
	if (timeinfo->tm_year == this_year) {
		strftime(time_buf, 80, "%b %d %H:%M", timeinfo);
	} else {
		strftime(time_buf, 80, "%b %d  %Y", timeinfo);
	}
	printf("%s ", time_buf);

	/* Print the file name */
	if (stdout_is_tty) {
		printf("\033[%sm%s\033[0m", ansi_color_str, file->name);
		if (show_slash && S_ISDIR(file->statbuf.st_mode)) {
			printf("/");
		}
		if (S_ISLNK(file->statbuf.st_mode)) {
			const char * s = file->lstatres == 0 ? color_str(&file->statbufl) : "1;31";
			printf(" -> \033[%sm%s\033[0m", s, file->link);
		}
	} else {
		printf("%s", file->name);
		if (show_slash && S_ISDIR(file->statbuf.st_mode)) {
			printf("/");
		}
		if (S_ISLNK(file->statbuf.st_mode)) {
			printf(" -> %s", file->link);
		}
	}

	printf("\n");
}

#define X_S "\033[3m"
#define X_E "\033[0m"
static int show_help(int argc, char * argv[]) {
	fprintf(stderr,
			"%s - list files\n"
			"\n"
			"usage: %s [-aAfFhiklpsxC1] [" X_S "path" X_E "...]\n"
			"\n"
			" -a  --all             " X_S "list all files (including . files)" X_E "\n"
			" -A  --almost-all      " X_S "like -a, but still hide . and .." X_E "\n"
			" -f                    " X_S "display entries in original order" X_E "\n"
			" -F  --classify        " X_S "show characters after files depending on type" X_E "\n"
			" -h  --human-readable  " X_S "human-readable file sizes" X_E "\n"
			" -i  --inode           " X_S "display inode number before name" X_E "\n"
			" -k  --kibibytes       " X_S "(no-op; block size is always 1024)" X_E "\n"
			" -l                    " X_S "use a long listing format" X_E "\n"
			" -p                    " X_S "show a / after directory names" X_E "\n"
			" -s  --size            " X_S "show size (in block) before file names" X_E "\n"
			" -x                    " X_S "sort entries across columns instead of down" X_E "\n"
			" -C                    " X_S "format output by columns (default)" X_E "\n"
			" --help                " X_S "show this help text" X_E "\n"
			"\n", argv[0], argv[0]);
	return 0;
}

static int show_usage(int argc, char * argv[]) {
	fprintf(stderr,
			"usage: %s [-aAfFhiklpsxC1] [" X_S "path" X_E "...]\n"
			" or try '%s --help' for more information\n",
			argv[0], argv[0]);
	return 1;
}

static void display_tfiles(struct tfile ** ents_array, int numents) {
	int ent_max_len[4] = {0,0,0,0};
	size_t total_blocks = 0;
	for (int i = 0; i < numents; i++) {
		ent_max_len[0] = MAX(ent_max_len[0], display_width_of_string(ents_array[i]->name));
		if (show_inode) ent_max_len[1] = MAX(ent_max_len[1], snprintf(NULL,0,"%zu ",ents_array[i]->statbuf.st_ino));
		if (show_size) {
			size_t size = as_1k_blocks(ents_array[i]->statbuf.st_size);
			if (human_readable) {
				ent_max_len[2] = MAX(ent_max_len[2], print_human_readable_size(NULL, 0, size * 1024) + 1);
			} else {
				ent_max_len[2] = MAX(ent_max_len[2], snprintf(NULL,0,"%zu ",size));
			}
		}
		total_blocks += as_1k_blocks(ents_array[i]->statbuf.st_size);
	}
	int total_width = 0;
	for (size_t i = 0; i < sizeof(ent_max_len) / sizeof(*ent_max_len); ++i) {
		total_width += ent_max_len[i];
	}
	if (show_slash) total_width += 1;

	if (long_mode || show_size) {
		/* || a few other things as well we don't have yet */
		printf("total %zu\n", total_blocks);
	}

	if (long_mode) {
		TRACE("long mode display, column lengths");
		int widths[4] = {0,0,0,0};
		for (int i = 0; i < numents; i++) {
			update_column_widths(widths, ents_array[i]);
		}
		TRACE("actual printing");
		for (int i = 0; i < numents; i++) {
			print_entry_long(widths, ent_max_len, ents_array[i]);
		}
	} else {
		/* Determine the gridding dimensions */
		int col_ext = total_width + MIN_COL_SPACING;
		int cols = ((term_width + MIN_COL_SPACING) / col_ext);
		if (cols == 0) cols = 1;

		/* Print the entries */
		if (columns) {
			int rows = numents / cols;
			if (rows * cols < numents) rows++;
			for (int i = 0; i < rows; ++i) {
				print_entry(ents_array[i], ent_max_len);
				for (int j = 1; j < cols; ++j) {
					if (i + j * rows >= numents) break;
					for (int t = 0; t < MIN_COL_SPACING; ++t) printf(" ");
					print_entry(ents_array[i + j * rows], ent_max_len);
				}
				printf("\n");
			}
		} else {
			for (int i = 0; i < numents;) {
				print_entry(ents_array[i++], ent_max_len);
				for (int j = 0; (i < numents) && (j < (cols-1)); j++) {
					for (int t = 0; t < MIN_COL_SPACING; ++t) printf(" ");
					print_entry(ents_array[i++], ent_max_len);
				}
				printf("\n");
			}
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

	TRACE("reading entries");
	struct dirent * ent = readdir(dirp);
	for (; ent; ent = readdir(dirp)) {
		if (ent->d_name[0] == '.' && !show_hidden) continue;
		if (show_hidden != 1 && !strcmp(ent->d_name, ".")) continue;
		if (show_hidden != 1 && !strcmp(ent->d_name, "..")) continue;

		struct tfile * f = malloc(sizeof(struct tfile));

		f->name = strdup(ent->d_name);

		char tmp[strlen(p)+strlen(ent->d_name)+2];
		sprintf(tmp, "%s/%s", p, ent->d_name);
		lstat(tmp, &f->statbuf);
		if (S_ISLNK(f->statbuf.st_mode)) {
			f->lstatres = stat(tmp, &f->statbufl);
			f->link = malloc(4096);
			readlink(tmp, f->link, 4096);
		}

		list_insert(ents_list, (void *)f);
	}
	closedir(dirp);

	TRACE("copying");

	/* Now, copy those entries into an array (for sorting) */

	if (!ents_list->length) return 0;

	struct tfile ** file_arr = malloc(sizeof(struct tfile *) * ents_list->length);
	int index = 0;
	foreach(node, ents_list) {
		file_arr[index++] = (struct tfile *)node->value;
	}

	list_free(ents_list);

	TRACE("sorting");
	if (!in_order) qsort(file_arr, index, sizeof(struct tfile *), filecmp_notypesort);

	TRACE("displaying");
	display_tfiles(file_arr, index);

	free(file_arr);

	return 0;
}

int main (int argc, char * argv[]) {
	char * p = ".";

	static struct option long_opts[] = {
		{"help", no_argument, 0, '-'},
		{"all", no_argument, 0, 'a'},
		{"almost-all", no_argument, 0, 'A'},
		{"classify", no_argument, 0, 'F'},
		{"human-readable", no_argument, 0, 'h'},
		{"inode", no_argument, 0, 'i'},
		{"kibibytes", no_argument, 0, 'k'},
		{"size", no_argument, 0, 's'},
		{0,0,0,0},
	};

	int opt, index;
	while ((opt = getopt_long(argc, argv, "aAfFhiklpsxC1?", long_opts, &index)) != -1) {
		switch (opt) {
			case 'a':
				show_hidden = 1;
				break;
			case 'A':
				show_hidden = 2;
				break;
			case 'f':
				in_order = 1;
				break;
			case 'F':
				show_slash = 2;
				break;
			case 'h':
				human_readable = 1;
				break;
			case 'i':
				show_inode = 1;
				break;
			case 'k':
				/* no op, we always use 1024 like GNU */
				break;
			case 'l':
				long_mode = 1;
				break;
			case 'p':
				show_slash = 1;
				break;
			case 's':
				show_size = 1;
				break;

			/* TODO These two should also force the multi-column
			 * display even when stdout is not a TTY... */
			case 'x':
				long_mode = 0;
				columns = 0;
				break;
			case 'C':
				long_mode = 0;
				columns = 1;
				break;

			case '1':
				one_column = 1;
				break;

			case '-':
				if (index == 0) return show_help(argc, argv);
				/* fallthrough */
			case '?':
				return show_usage(argc, argv);
		}
	}

	if (optind < argc) p = argv[optind];
	if (optind + 1 < argc) print_dir = 1;

	stdout_is_tty = isatty(STDOUT_FILENO);

	if (long_mode) {
		struct tm * timeinfo;
		struct timeval now;
		gettimeofday(&now, NULL); //time(NULL);
		timeinfo = localtime((time_t *)&now.tv_sec);
		this_year = timeinfo->tm_year;
	}

	if (stdout_is_tty && !one_column) {
		TRACE("getting display size");
		struct winsize w;
		ioctl(1, TIOCGWINSZ, &w);
		term_width = w.ws_col;
		term_height = w.ws_row;
		term_width -= 1; /* And this just helps clean up our math */
	}

	int out = 0;

	if (argc == 1 || optind == argc) {
		TRACE("no file to look up");
		if (display_dir(p) == 2) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], p, strerror(errno));
		}
	} else {
		list_t * files = list_create();
		while (p) {
			struct tfile * f = malloc(sizeof(struct tfile));

			f->name = p;
			int t = lstat(p, &f->statbuf);

			if (t < 0) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], p, strerror(errno));
				free(f);
				out = 2;
			} else {
				if (S_ISLNK(f->statbuf.st_mode)) {
					f->lstatres = stat(p, &f->statbufl);
					f->link = malloc(4096);
					readlink(p, f->link, 4096);
				}
				list_insert(files, f);
			}

			optind++;
			if (optind >= argc) p = NULL;
			else p = argv[optind];
		}

		if (!files->length) {
			/* No valid entries */
			return out;
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
			if (display_dir(file_arr[i]->name) == 2) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], file_arr[i]->name, strerror(errno));
			}
		}
	}

	return out;
}

