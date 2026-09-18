/**
 * @brief LS_COLORS utility library
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "toaru/lscolors.h"

struct MatchColor {
	char * matcher;
	char * color;
	int match_len;
	struct MatchColor * next;
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

static struct MatchColor * ls_match_colors = NULL;

static int color_empty(const char * color) {
	if (!color) return 1;
	if (!*color) return 1;
	if (color[0] == '0' && (!color[1] || (color[1] == '0' && !color[2]))) return 1;
	return 0;
}

struct NamedColor * ls_base_colors_ptr(void) {
	return ls_base_colors;
}

/**
 * @brief Determine the appropriate color for a file with a given name and stat buf.
 *
 * @param name Filename, for glob matchers.
 * @param sb   stat buffer with file paramaters.
 * @returns Pointer to a fixed string.
 */
const char * ls_color_str(const char * name, struct stat * sb) {
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

int ls_colors_init(void) {
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

	return 0;
}
