/**
 * @brief List files
 *
 * Lists files in a directory, with nice color
 * output like any modern ls should have.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2026 K. Lange
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

struct NamedColor {
	char * name;
	char * color;
};

enum ColorNames {
	LS_COLOR_LEFT,
	LS_COLOR_RIGHT,
	LS_COLOR_END,
	LS_COLOR_RESET,
	LS_COLOR_DIR,
	LS_COLOR_SYM,
	LS_COLOR_BDEV,
	LS_COLOR_CDEV,
	LS_COLOR_ORPHAN,
	LS_COLOR_EXE,
	LS_COLOR_SETUID,
	LS_COLOR_SETGID,

	LS_COLOR_PIPE,
	LS_COLOR_SOCK,
	LS_COLOR_NORM,
	LS_COLOR_FILE,
	LS_COLOR_MISS,
	LS_COLOR_ST,
	LS_COLOR_OW,
	LS_COLOR_TW,

	LS_COLOR_MAX,
};

struct NamedColor ls_base_colors[] = {
	[LS_COLOR_LEFT]   = {"lc", "\033["},   /* left of color */
	[LS_COLOR_RIGHT]  = {"rc", "m"},       /* right of color */
	[LS_COLOR_END]    = {"ec", NULL},      /* end color */
	[LS_COLOR_RESET]  = {"rs", "0"},       /* reset color */
	[LS_COLOR_DIR]    = {"di", "1;34"},    /* directory */
	[LS_COLOR_SYM]    = {"ln", "1;36"},    /* symlink */
	[LS_COLOR_BDEV]   = {"bd", "1;33;40"}, /* block device */
	[LS_COLOR_CDEV]   = {"cd", "1;33;40"}, /* char device */
	[LS_COLOR_ORPHAN] = {"or", "1;31"},    /* dangling symlink */
	[LS_COLOR_EXE]    = {"ex", "1;32"},    /* executable */
	[LS_COLOR_SETUID] = {"su", "37;41"},   /* setuid */
	[LS_COLOR_SETGID] = {"sg", "30;43"},   /* setgid */
	[LS_COLOR_PIPE]   = {"pi", "1;33"},    /* pipe */
	[LS_COLOR_SOCK]   = {"so", "1;35"},    /* socket */
	[LS_COLOR_NORM]   = {"no", NULL},      /* normal */
	[LS_COLOR_FILE]   = {"fi", NULL},      /* file */
	[LS_COLOR_MISS]   = {"mi", NULL},      /* missing target of symlink */
	[LS_COLOR_ST]     = {"st", NULL},      /* sticky bit */
	[LS_COLOR_OW]     = {"ow", NULL},      /* other-writable */
	[LS_COLOR_TW]     = {"tw", NULL},      /* other-writable + sticky */
};

struct MatchColor {
	char * matcher;
	char * color;
	int match_len;
	struct MatchColor * next;
};

struct MatchColor * ls_match_colors = NULL;

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static int human_readable = 0;
static int stdout_is_tty = 1;
static int this_year = 0;
static int show_hidden = 0;
static int long_mode   = 0;
static int print_dir   = 0;
static int term_width = 0;
static int term_height = 0;
static int columns = 1;
static int in_order = 0;
static int show_inode = 0;
static int show_size = 0;
static int one_column = 0;
static int show_slash = 0;
static int use_color = 0;
static int min_col_spacing = 1;
static int use_sym_target = 0;

struct tfile {
	char * name;
	struct stat statbuf;
	char * link;
	struct stat statbufl;
	int lstatres;
};

#define LS_C(type) (ls_base_colors[LS_COLOR_ ## type].color)

static int color_empty(const char * color) {
	if (!color) return 1;
	if (!*color) return 1;
	if (color[0] == '0' && (!color[1] || (color[1] == '0' && !color[2]))) return 1;
	return 0;
}

static const char * color_str(const char * name, struct stat * sb) {
	if (S_ISREG(sb->st_mode)) {
		if ((sb->st_mode & S_ISUID) && !color_empty(LS_C(SETUID))) return LS_C(SETUID);
		if ((sb->st_mode & S_ISGID) && !color_empty(LS_C(SETGID))) return LS_C(SETGID);
		if ((sb->st_mode & 0111) && !color_empty(LS_C(EXE))) return LS_C(EXE);

		int slen = strlen(name);
		struct MatchColor * matches = ls_match_colors;
		while (matches) {
			if (slen >= matches->match_len && !strcasecmp(name + slen - matches->match_len, matches->matcher)) {
				return matches->color;
			}
			matches = matches->next;
		}

		return LS_C(FILE);
	} else if (S_ISDIR(sb->st_mode)) {
		if ((sb->st_mode & S_ISVTX) && (sb->st_mode & S_IWOTH) && !color_empty(LS_C(TW))) return LS_C(TW);
		if ((sb->st_mode & S_ISVTX) && !color_empty(LS_C(ST))) return LS_C(ST);
		if ((sb->st_mode & S_IWOTH) && !color_empty(LS_C(OW))) return LS_C(OW);
		return LS_C(DIR);
	} else if (S_ISLNK(sb->st_mode)) {
		return LS_C(SYM);
	} else if (S_ISBLK(sb->st_mode)) {
		return LS_C(BDEV);
	} else if (S_ISCHR(sb->st_mode)) {
		return LS_C(CDEV);
	} else if (S_ISFIFO(sb->st_mode)) {
		return LS_C(PIPE);
	} else if (S_ISSOCK(sb->st_mode)) {
		return LS_C(SOCK);
	}

	return LS_C(FILE);
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

	const char * ansi_color_str = color_str(file->name, &file->statbuf);

	if (S_ISLNK(file->statbuf.st_mode)) {
		if (file->lstatres && LS_C(ORPHAN)) {
			ansi_color_str = LS_C(ORPHAN);
		} else if (!file->lstatres && use_sym_target) {
			ansi_color_str = color_str(file->link, &file->statbufl);
		}
	}

	/* Print the file name */
	if (use_color && ansi_color_str) {
		printf("%s%s%s%s%s", LS_C(LEFT), ansi_color_str, LS_C(RIGHT), file->name, LS_C(END));
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

	const char * ansi_color_str = color_str(file->name, &file->statbuf);
	mode_t mode = file->statbuf.st_mode;

	/* file permissions */
	if (S_ISLNK(mode))       { printf("l"); }
	else if (S_ISCHR(mode))  { printf("c"); }
	else if (S_ISBLK(mode))  { printf("b"); }
	else if (S_ISDIR(mode))  { printf("d"); }
	else { printf("-"); }
	printf("%c", (mode & S_IRUSR) ? 'r' : '-');
	printf("%c", (mode & S_IWUSR) ? 'w' : '-');
	printf("%c", (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') : ((mode & S_ISUID) ? 'S' : '-'));
	printf("%c", (mode & S_IRGRP) ? 'r' : '-');
	printf("%c", (mode & S_IWGRP) ? 'w' : '-');
	printf("%c", (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') : ((mode & S_ISGID) ? 'S' : '-'));
	printf("%c", (mode & S_IROTH) ? 'r' : '-');
	printf("%c", (mode & S_IWOTH) ? 'w' : '-');
	printf("%c", (mode & S_IXOTH) ? 'x' : '-');

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

	if (S_ISLNK(file->statbuf.st_mode)) {
		if (file->lstatres && LS_C(ORPHAN)) {
			ansi_color_str = LS_C(ORPHAN);
		} else if (!file->lstatres && use_sym_target) {
			ansi_color_str = color_str(file->link, &file->statbufl);
		}
	}

	/* Print the file name */
	if (use_color && ansi_color_str) {
		printf("%s%s%s%s%s", LS_C(LEFT), ansi_color_str, LS_C(RIGHT), file->name, LS_C(END));
	} else {
		printf("%s", file->name);
	}
	if (show_slash && S_ISDIR(file->statbuf.st_mode)) {
		printf("/");
	}
	if (S_ISLNK(file->statbuf.st_mode)) {
		printf(" -> ");
		const char * s = file->lstatres == 0 ? color_str(file->link, &file->statbufl) : LS_C(MISS);
		if (use_color && s) {
			printf("%s%s%s%s%s", LS_C(LEFT), s, LS_C(RIGHT), file->link, LS_C(END));
		} else {
			printf("%s", file->link);
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
			" --color[=when]        " X_S "specify when to enable color output" X_E "\n"
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
		int col_ext = total_width + min_col_spacing;
		int cols = ((term_width + min_col_spacing) / col_ext);
		if (cols == 0) cols = 1;

		/* Print the entries */
		if (columns) {
			int rows = numents / cols;
			if (rows * cols < numents) rows++;
			for (int i = 0; i < rows; ++i) {
				print_entry(ents_array[i], ent_max_len);
				for (int j = 1; j < cols; ++j) {
					if (i + j * rows >= numents) break;
					for (int t = 0; t < min_col_spacing; ++t) printf(" ");
					print_entry(ents_array[i + j * rows], ent_max_len);
				}
				printf("\n");
			}
		} else {
			for (int i = 0; i < numents;) {
				print_entry(ents_array[i++], ent_max_len);
				for (int j = 0; (i < numents) && (j < (cols-1)); j++) {
					for (int t = 0; t < min_col_spacing; ++t) printf(" ");
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
			f->link = malloc(f->statbuf.st_size + 1);
			ssize_t len = readlink(tmp, f->link, f->statbuf.st_size);
			if (len >= 0) f->link[len] = '\0';
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

static void parse_escaped(char **input, char **output, int eq_ends) {
	char * i = *input;
	char * o = *output;

	while (1) {
		if (*i == 0) break;
		if (*i == ':') break;
		if (eq_ends && *i == '=') break;

		if (*i == '\\') {
			i++;
			if (*i == 0) break;
			switch (*i) {
				case '0' ... '7': {
					/* octal string */
					int n = 0;
					while (*i && (*i >= '0' && *i <= '7')) n = (n << 3) + (*i++ - '0');
					*o++ = n;
					break;
				}
				case 'x': {
					/* hex string */
					i++;
					int n = 0;
					while (*i && ((*i >= '0' && *i <= '9') || (*i >= 'a' && *i <= 'f') || (*i >= 'A' && *i <= 'F'))) {
						if (*i >= '0' && *i <= '9') n = (n << 4) + (*i - '0');
						if (*i >= 'a' && *i <= 'f') n = (n << 4) + (*i - 'a' + 0xa);
						if (*i >= 'A' && *i <= 'F') n = (n << 4) + (*i - 'A' + 0xa);
						i++;
					}
					*o++ = n;
					break;
				}
				case 'a': *o++ = '\a'; i++; break;
				case 'b': *o++ = '\b'; i++; break;
				case 'e': *o++ = 27;   i++; break;
				case 'f': *o++ = '\f'; i++; break;
				case 'n': *o++ = '\n'; i++; break;
				case 'r': *o++ = '\r'; i++; break;
				case 't': *o++ = '\t'; i++; break;
				case 'v': *o++ = '\v'; i++; break;
				case '?': *o++ = 127;  i++; break;
				case '_': *o++ = ' ';  i++; break;
				default:  *o++ = *i;   i++; break;
			}
		} else if (*i == '^') {
			i++;
			if (*i == '?') {
				*o++ = 127;
				i++;
			} else if (*i >= '@' && *i <= '~') {
				*o++ = (*i) & 037;
				i++;
			} else {
				*o++ = '^';
			}
		} else {
			*o++ = *i++;
		}
	}

	*output = o;
	*input = i;
}

static char * parse_color_type(char **input, char **output) {
	char * i = *input;
	char * o = *output;
	char * s = o;

	if (*i == ':') {
		*input = i + 1;
		return NULL;
	}

	if (*i == '*') {
		parse_escaped(&i, &o, 1);
	} else {
		while (*i && *i != ':' && *i != '=') {
			*o++ = *i++;
		}
	}

	if (!*i) {
		*input = i;
		return NULL;
	}

	*input = i + 1;

	if (*i != '=') return NULL;

	*o++ = '\0';
	*output = o;
	return s;
}

static char * parse_color_val(char **input, char **output) {
	char * i = *input;
	char * o = *output;
	char * s = o;

	if (*i == ':') {
		/* NULL, continue */
		*input = i + 1;
		return NULL;
	}

	parse_escaped(&i, &o, 0);

	*o++ = '\0';
	*output = o;
	*input = i;
	return s;
}

static void setup_colors(void) {
	char * ls_colors = getenv("LS_COLORS");
	if (ls_colors) {
		char * colors_buf = calloc(strlen(ls_colors)+1, 1);
		char * buf = colors_buf;
		char * c = ls_colors;

		while (*c) {
			char * type = parse_color_type(&c, &buf);
			if (!type) continue; /* null entry */
			char * color_val = parse_color_val(&c, &buf);

			if (*type != '*') {
				for (int i = 0; i < LS_COLOR_MAX; ++i) {
					if (!strcmp(type, ls_base_colors[i].name)) {
						ls_base_colors[i].color = color_val;
						break;
					}
				}
			} else {
				struct MatchColor * new_color = malloc(sizeof(struct MatchColor));
				new_color->matcher = type + 1;
				new_color->color = color_val;
				new_color->match_len = strlen(type + 1);
				new_color->next = ls_match_colors;
				ls_match_colors = new_color;
			}
		}
	}

	if (!LS_C(END)) {
		char * color_end;
		asprintf(&color_end, "%s%s%s", LS_C(LEFT), LS_C(RESET), LS_C(RIGHT));
		LS_C(END) = color_end;
	}

	if (color_empty(LS_C(MISS)) && !color_empty(LS_C(ORPHAN))) {
		LS_C(MISS) = LS_C(ORPHAN);
	}

	if (LS_C(SYM) && !strcmp(LS_C(SYM), "target")) {
		use_sym_target = 1;
		LS_C(SYM) = "1;36";
	}

	char * ls_colsep = getenv("LS_COLSEP");
	if (ls_colsep) {
		min_col_spacing = atoi(ls_colsep);
	}
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
		{"color", optional_argument, 0, 1000},
		{0,0,0,0},
	};

	stdout_is_tty = isatty(STDOUT_FILENO);
	use_color = stdout_is_tty; /* we default to 'auto' */

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

			case 1000:
				if (optarg) {
					if (!strcmp(optarg, "never") || !strcmp(optarg,"n")) {
						use_color = 0;
					} else if (!strcmp(optarg, "auto")) {
						use_color = stdout_is_tty;
					} else if (!strcmp(optarg, "always")) {
						use_color = 1;
					} else {
						fprintf(stderr, "%s: --color= must be one of 'never', 'auto', or 'always'\n", argv[0]);
						return 1;
					}
				} else {
					use_color = 1; /* --color is equivalent to --color=always */
				}
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

	setup_colors();

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
					f->link = malloc(f->statbuf.st_size + 1);
					ssize_t len = readlink(p, f->link, f->statbuf.st_size);
					if (len >= 0) f->link[len] = '\0';
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

