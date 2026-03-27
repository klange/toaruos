/**
 * @brief A rudimentary implementation of some parts of ROFF.
 *
 * This is really just intended to read our own man pages, so it
 * only supports functionality that was used in the manpage
 * written for the 'nyancat' utility. It was also tested and
 * expanded based on the manpage for 'less'.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>

#define DEFAULT_INDENTATION 7
static struct winsize w;

static char * interpret_section(char * section) {
	if (!section) return "Unknown";
	if (strlen(section) == 1 && isdigit(*section)) {
		switch (atoi(section)) {
			case 1: return "General Commands Manual";
			case 2: return "System Calls Manual";
			case 3: return "Library Functions Manual";
			case 4: return "Special Files";
			case 5: return "File Formats";
			case 6: return "Games";
			case 7: return "Miscellaneous";
			case 8: return "System Administration Manual";
			case 9: return "Kernel Programming Manual";
			case 0: return "Section Zero";
		}
	}
	return section; /* Must be custom. */
}

static void formatted_title_and_section(char * title, char * section) {
	if (!title || !section) {
		printf("??");
		return;
	}
	printf("\033[4m");
	printf("%s", title);
	printf("\033[0m(%s)", section);
}

static void format_title(char * title, char * section) {

	/* XXX Should use display width, but too lazy. */
	int title_len = title ? strlen(title) : 0;
	int section_len = section ? strlen(section) : 0;
	char * section_name = interpret_section(section);
	int section_name_len = strlen(section_name);

	int avail = w.ws_col - 1; /* A gutter is fine. */

	int space_used = (title_len + section_len + 2) * 2 + section_name_len;
	if (avail < space_used) {
		/* Print a short form and hope it works */
		formatted_title_and_section(title, section);
		printf("\n");
		return;
	}

	int space_left = (avail - space_used) / 2;
	int space_right = avail - space_used - space_left;

	formatted_title_and_section(title, section);
	for (int i = 0; i < space_left; ++i) printf(" ");
	printf("%s", section_name);
	for (int i = 0; i < space_right; ++i) printf(" ");
	formatted_title_and_section(title, section);
	printf("\n\n");
}

static void format_footer(char * title, char * section, char * date) {
	int title_len = title ? strlen(title) : 0;
	int section_len = section ? strlen(section) : 0;
	int date_len = date ? strlen(date) : 0;

	int avail = w.ws_col - 1; /* A gutter is fine. */
	int space_left = (avail - date_len) / 2;
	int space_right = (avail - space_left - date_len - title_len - section_len - 2);
	for (int i = 0; i < space_left; ++i) printf(" ");
	printf("%s", date ? date : "");
	for (int i = 0; i < space_right; ++i) printf(" ");
	formatted_title_and_section(title,section);
	printf("\n");
}

static int skip_escape(char *x, size_t *len) {
	switch (x[1]) {
		case 'f': /* Font selection takes a few more characters */
			if (x[2] == '(') {
				if (x[3] == 0) return 3;
				if (x[4] == 0) return 4;
				return 5;
			} else if (x[2] == 0) {
				return 2;
			}
			return 3;
		case ',':
		case '/':
			return 2;
		case '(':
			(*len)++;
			if (!x[2]) return 2;
			if (!x[3]) return 3;
			return 4;
		default:
			(*len)++;
			return 2;
	}
}

struct RoffContext {
	char * topic_title;
	char * topic_section;
	char * topic_date;
	int current_x;
	int indent;
	int next_indent;
	int extra_indent;
	int printing_table;
	int squish_line;
	unsigned int previous_font;
	unsigned int current_font;
};

static int flush_line(struct RoffContext * ctx) {
	if (ctx->current_x != 0) {
		printf("\033[0m\n");
		ctx->current_x = 0;
		return 1;
	}
	return 0;
}

#define PAIR(a,b) (((unsigned int)a << 8) | (unsigned int)b)

static void switch_font(struct RoffContext * ctx, unsigned int font) {
	if (font == 'P') {
		ctx->current_font = ctx->previous_font;
	} else {
		ctx->previous_font = ctx->current_font;
		ctx->current_font = font;
	}

	/* Aliases first */
	switch (ctx->current_font) {
		case '1': ctx->current_font = 'R'; break;
		case '2': ctx->current_font = 'I'; break;
		case '3': ctx->current_font = 'B'; break;
		case '4': ctx->current_font = PAIR('B','I'); break;
		case PAIR('C','B'): ctx->current_font = 'B'; break;
		case PAIR('C','I'): ctx->current_font = 'I'; break;
		case PAIR('C','R'): ctx->current_font = 'R'; break;
		case PAIR('C','W'): ctx->current_font = 'R'; break;
	}
}

static void activate_font(struct RoffContext * ctx) {
	switch (ctx->current_font) {
		case 'B': printf("\033[0;1m"); break;
		case 'R': printf("\033[0m"); break;
		/* These are actually supposed to be italic, but everyone treats them as underlined (4, rather than 3). */
		case 'I': printf("\033[0;4m"); break;
		case PAIR('B','I'): printf("\033[0;1;4m"); break;
	}
}

static int do_escape(struct RoffContext * ctx, char *x) {
	switch (x[1]) {
		case 'f': /* Font selection takes a few more characters */
			if (x[2] == 0) return 2;
			if (x[2] == '(') {
				if (x[3] == 0) return 3;
				if (x[4] == 0) return 4;
				switch_font(ctx, PAIR(x[3],x[4]));
			} else {
				switch_font(ctx, (unsigned int)x[2]);
			}
			activate_font(ctx);
			return x[2] == '(' ? 5 : 3;
		case 'e':
			fputc('\\', stdout);
			return 2;
		case ',':
		case '/':
			/* "left and right italic correction"; ignored. */
			return 2;
		case '(':
			if (!x[2]) return 2;
			if (!x[3]) return 3;
			switch (PAIR(x[2],x[3])) {
				case PAIR('h','a'): fputc('^', stdout); break;
				case PAIR('b','u'): printf("•"); break;
				case PAIR('t','i'): fputc('~', stdout); break;
				case PAIR('a','q'): fputc('\'', stdout); break;
				case PAIR('d','q'): fputc('"', stdout); break;
				default: fputc('?', stdout); break;
			}
			return 4;
		default:
			fputc(x[1], stdout);
			return 2;
	}
}

static int is_whitespace(char * c) {
	while (*c && *c == ' ') c++;
	if (*c && *c != ' ') return 0;
	return 1;
}

static void print_spaces(int indent) {
	for (int i = 0; i < indent; ++i) fputc(' ', stdout);
}

static size_t process_word(struct RoffContext * ctx, char * c, int delimited) {
	char * c_in = c;
	char * last_word = c;
	size_t last_len = 0;

	/* Collect word */
	while (*c && *c != ' ') {
		if (*c == '\\' && c[1]) {
			c += skip_escape(c, &last_len);
		} else {
			last_len++;
			c++;
		}
	}

	/* Word was empty, done with line. */
	if (!last_len) {
		while (*c && *c == ' ') c++;
		return c - c_in;
	}

	/* Word would wrap, continue to next line and print indentant. */
	if (last_len + ctx->current_x > (size_t)w.ws_col - 8) {
		flush_line(ctx);
		print_spaces(ctx->indent);
		ctx->current_x += ctx->indent;
	} else if (ctx->current_x == 0) {
		print_spaces(ctx->indent);
		ctx->current_x += ctx->indent;
	}

	activate_font(ctx);

	/* Print the word */
	char *x = last_word;
	while (*x && x != c) {
		if (*x == '\\' && x[1]) {
			x += do_escape(ctx, x);
		} else {
			fputc(*x, stdout);
			x++;
		}
	}
	ctx->current_x += last_len;

	if (!*c) printf("\033[0m");

	if (ctx->printing_table) {
		while (*c && *c == ' ' && (size_t)ctx->current_x < (size_t)w.ws_col - 8) {
			printf(" ");
			ctx->current_x++;
			c++;
		}
	} else {
		if (delimited) {
			fputc(' ', stdout);
			ctx->current_x += 1;
		}
	}

	/* Advance through whitespace */
	while (*c && *c == ' ') c++;

	return c - c_in;
}

#define MAYBE_QUOTES() do { \
	if (*c == '"' && line[len-1] == '"') { \
		line[len-1] = '\0'; \
		len--; \
		c++; \
	} \
} while (0)

static int do_file(char ** argv, int i) {
	FILE * f = fopen(argv[i], "r");
	if (!f) {
		fprintf(stdout, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
		return 1;
	}

	char * line = NULL;
	size_t avail = 0;
	ssize_t len;

	struct RoffContext ctx = {0};

	ctx.previous_font = 'R';
	ctx.current_font = 'R';

	int ret = 0;

	while ((len = getline(&line, &avail, f)) >= 0) {

		/* Discard linefeed to make this simpler */
		if (len && line[len-1] == '\n') {
			line[len-1] = '\0';
			len--;
		}

		if (!*line && !ctx.printing_table) continue; /* Skip blank lines entirely? Is that correct? */

		if (*line == '\'') line[0] = '.'; /* single tick is an alias for . at the start of a line. */

		/* Skip comment lines */
		if (strstr(line, ".\\\"") == line) continue;

		/* Also skip lines that are just the "control character" */
		if (*line == '.' && is_whitespace(&line[1])) continue;

		char * c = line;
		int delimited = 1;

		if (line[0] == '.') {
			/* Macro directives */

			if (strstr(line,".TH ") == line) {
				if (ctx.topic_title) {
					fprintf(stderr, "%s: %s: More than one .TH found.\n", argv[0], argv[i]);
					ret = 1;
					goto _cleanup;
				}
				/* Topic heading */
				char * c = line + 4;
				char * title = NULL;
				char * section = NULL;
				char * date = NULL;

				while (*c && *c == ' ') c++;
				if (*c) {
					title = c;

					while (*c && *c != ' ') c++;
					if (*c) *c++ = '\0';
					while (*c && *c == ' ') c++;
					if (*c) {
						section = c;
						while (*c && *c != ' ') c++;
						if (*c) *c++ = '\0';
						while (*c && *c == ' ') c++;
						if (*c) {
							date = c;
						}
					}
				}

				if (title) {
					ctx.topic_title = strdup(title);
				}
				if (section) {
					ctx.topic_section = strdup(section);
				}
				if (date) {
					/* Maybe handle quotes */
					int datel = strlen(date);
					if (datel > 1 && date[0] == '"' && date[datel-1] == '"') {
						date[datel-1] = '\0';
						date++;
					}
					ctx.topic_date = strdup(date);
				}

				format_title(ctx.topic_title, ctx.topic_section);
				continue;
			} else if (strstr(line, ".SH ") == line) {
				/* Section heading */
				if (flush_line(&ctx)) printf("\n");
				c = line + 4;
				MAYBE_QUOTES();
				printf("\033[1m%s\033[0m\n", c);
				ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
				ctx.next_indent = 0;
				continue;
			} else if (strstr(line, ".PP") == line) {
				flush_line(&ctx); printf("\n");
				ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
				ctx.next_indent = 0;
				continue;
			} else if (strstr(line, ".TP") == line) {
				if (flush_line(&ctx)) printf("\n");

				ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
				ctx.next_indent = ctx.extra_indent + DEFAULT_INDENTATION * 2;
				ctx.squish_line = 1;
				continue;
			} else if (strstr(line, ".br") == line) {
				flush_line(&ctx);
				continue;
			} else if (strstr(line, ".B ") == line) {
				switch_font(&ctx, 'B');
				c = line + 3;
				MAYBE_QUOTES();
			} else if (strstr(line, ".I ") == line) {
				switch_font(&ctx, 'I');
				c = line + 3;
				MAYBE_QUOTES();
			} else if (strstr(line, ".IR ") == line) {
				switch_font(&ctx, 'I');
				c = line + 4;
				c += process_word(&ctx, c, 0);
				switch_font(&ctx, 'R');
				/* Continue */
			} else if (strstr(line, ".BR ") == line) {
				switch_font(&ctx, 'B');
				c = line + 4;
				c += process_word(&ctx, c, 0);
				switch_font(&ctx, 'R');
				/* Continue */
			} else if (strstr(line, ".IP ") == line) {
				if (flush_line(&ctx)) printf("\n");
				ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
				ctx.next_indent = ctx.extra_indent + DEFAULT_INDENTATION * 2;
				c = line + 4;
				MAYBE_QUOTES();
				ctx.squish_line = 1;
			} else if (strstr(line, ".IP") == line) {
				if (flush_line(&ctx)) printf("\n");
				ctx.indent = ctx.extra_indent + DEFAULT_INDENTATION;
				ctx.next_indent = ctx.extra_indent + DEFAULT_INDENTATION;
				c = line + 3;
				ctx.squish_line = 1;
			} else if (strstr(line, ".sp") == line) {
				/* Just skip it ? */
				flush_line(&ctx);
				printf("\n");
				continue;
			} else if (strstr(line, ".RS") == line) {
				ctx.extra_indent += DEFAULT_INDENTATION;
				ctx.indent += DEFAULT_INDENTATION;
				continue;
			} else if (strstr(line, ".RE") == line) {
				ctx.indent -= DEFAULT_INDENTATION;
				ctx.extra_indent -= DEFAULT_INDENTATION;
				continue;
			} else if (strstr(line, ".nh") == line) {
				/* TODO disable hyphenation */
				continue;
			} else if (strstr(line, ".hy") == line) {
				/* TODO reenable hyphenation */
				continue;
			} else if (strstr(line, ".TS") == line) {
				/* table start */
				flush_line(&ctx);
				ctx.printing_table = 1;
				continue;
			} else if (strstr(line, ".TE") == line) {
				/* table end */
				flush_line(&ctx);
				printf("\n");
				ctx.printing_table = 0;
				continue;
			} else if (strstr(line, ".nf") == line) {
				/* Treat this the same as a table; break lines where they are in the input */
				flush_line(&ctx);
				ctx.printing_table = 1;
				continue;
			} else if (strstr(line, ".fi") == line) {
				flush_line(&ctx);
				ctx.printing_table = 0;
				continue;
			} else {
				fprintf(stderr, "%s: %s: Found an unrecognized directive on this line: %s\n",
					argv[0], argv[i], line);
				ret = 1;
				goto _cleanup;
			}
		}

		if (*c == ' ' || *c == '\t') {
			flush_line(&ctx);
			print_spaces(ctx.indent);
			ctx.current_x += ctx.indent;


			while (*c == ' ' || *c == '\t') {
				if (*c == '\t') {
					do {
						fputc(' ', stdout);
						ctx.current_x += 1;
					} while (ctx.current_x % 6);
				} else {
					fputc(' ', stdout);
					ctx.current_x += 1;
				}
				c++;
			}
		}

		while (*c) {
			c += process_word(&ctx, c, delimited);
		}

		printf("\033[0m");

		if (ctx.printing_table) {
			printf("\n");
			ctx.current_x = 0;
			continue;
		}

		ctx.current_font = ctx.previous_font = 'R';

		if (ctx.next_indent) {
			ctx.indent = ctx.next_indent;
			ctx.next_indent = 0;
			if (ctx.squish_line && ctx.current_x < ctx.indent) {
				ctx.squish_line = 0;
				while (ctx.current_x < ctx.indent) {
					fputc(' ', stdout);
					ctx.current_x += 1;
				}
			} else {
				flush_line(&ctx);
			}
		}
	}

	if (flush_line(&ctx)) printf("\n");
	format_footer(ctx.topic_title, ctx.topic_section, ctx.topic_date);

_cleanup:
	if (ctx.topic_title) free(ctx.topic_title);
	if (ctx.topic_section) free(ctx.topic_section);
	if (ctx.topic_date) free(ctx.topic_date);

	fclose(f);
	return ret;
}

static int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
		"usage: %s " X_S "FILE" X_E "...\n"
		"\n"
		"Parse a very limited subset of ROFF and format it for display on STDOUT, using\n"
		"STDERR as the basis for sizing, suitable for passing to '|more -r'.\n"
		"\n"
		"If an unrecognized directive is found, processing will halt and an error will\n"
		"be printed indicating the unhandled line.\n"
		"\n"
		"Options:\n"
		"\n"
		" --help   " X_S "Show this help text." X_E "\n"
		"\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "?-:")) != -1) {
		switch (opt) {
			case '-':
				if (!strcmp(optarg,"help")) {
					usage(argv);
					return 0;
				}
				fprintf(stderr, "%s: '--%s' is not a recognized long option.\n", argv[0], optarg);
				/* fallthrough */
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		fprintf(stderr, "%s: no files (see `%s --help` for usage)\n", argv[0], argv[0]);
		return 1;
	}

	ioctl(STDERR_FILENO, TIOCGWINSZ, &w);

	int ret = 0;

	for (; optind < argc; optind++) {
		ret |= do_file(argv, optind);
	}

	return ret;
}
