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

static int current_x = 0;
static int indent = 0;
static int next_indent = 0;
static int extra_indent = 0;
static int printing_table = 0;

static int flush_line(void) {
	if (current_x != 0) {
		printf("\033[0m\n");
		current_x = 0;
		return 1;
	}
	return 0;
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
		default:
			(*len)++;
			return 2;
	}
}

static unsigned int previous_font = 'R';
static unsigned int current_font = 'R';

#define PAIR(a,b) (((unsigned int)a << 8) | (unsigned int)b)

static void switch_font(unsigned int font) {
	if (font == 'P') {
		current_font = previous_font;
	} else {
		previous_font = current_font;
		current_font = font;
	}

	/* Aliases first */
	switch (current_font) {
		case '1': current_font = 'R'; break;
		case '2': current_font = 'I'; break;
		case '3': current_font = 'B'; break;
		case '4': current_font = PAIR('B','I'); break;
		case PAIR('C','B'): current_font = 'B'; break;
		case PAIR('C','I'): current_font = 'I'; break;
		case PAIR('C','R'): current_font = 'R'; break;
		case PAIR('C','W'): current_font = 'R'; break;
	}
}

static void activate_font(void) {
	switch (current_font) {
		case 'B': printf("\033[0;1m"); break;
		case 'R': printf("\033[0m"); break;
		/* These are actually supposed to be italic, but everyone treats them as underlined (4, rather than 3). */
		case 'I': printf("\033[0;4m"); break;
		case PAIR('B','I'): printf("\033[0;1;4m"); break;
	}
}

static int do_escape(char *x) {
	switch (x[1]) {
		case 'f': /* Font selection takes a few more characters */
			if (x[2] == 0) return 2;
			if (x[2] == '(') {
				if (x[3] == 0) return 3;
				if (x[4] == 0) return 4;
				switch_font(PAIR(x[3],x[4]));
			} else {
				switch_font((unsigned int)x[2]);
			}
			activate_font();
			return x[2] == '(' ? 5 : 3;
		case 'e':
			fputc('\\', stdout);
			return 2;
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

static size_t process_word(char * c, int delimited) {
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
	if (last_len + current_x > (size_t)w.ws_col - 8) {
		flush_line();
		print_spaces(indent);
		current_x += indent;
	} else if (current_x == 0) {
		print_spaces(indent);
		current_x += indent;
	}

	activate_font();

	/* Print the word */
	char *x = last_word;
	while (*x && x != c) {
		if (*x == '\\' && x[1]) {
			x += do_escape(x);
		} else {
			fputc(*x, stdout);
			x++;
		}
	}
	current_x += last_len;

	if (!*c) printf("\033[0m");

	if (printing_table) {
		while (*c && *c == ' ' && (size_t)current_x < (size_t)w.ws_col - 8) {
			printf(" ");
			current_x++;
			c++;
		}
	} else {
		if (delimited) {
			fputc(' ', stdout);
			current_x += 1;
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

	char * topic_title = NULL;
	char * topic_section = NULL;
	char * topic_date = NULL;
	int ret = 0;

	current_x = 0;
	indent = 0;
	next_indent = 0;
	extra_indent = 0;
	printing_table = 0;

	while ((len = getline(&line, &avail, f)) >= 0) {

		/* Discard linefeed to make this simpler */
		if (len && line[len-1] == '\n') {
			line[len-1] = '\0';
			len--;
		}

		if (!*line && !printing_table) continue; /* Skip blank lines entirely? Is that correct? */

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
				if (topic_title) {
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
					topic_title = strdup(title);
				}
				if (section) {
					topic_section = strdup(section);
				}
				if (date) {
					/* Maybe handle quotes */
					int datel = strlen(date);
					if (datel > 1 && date[0] == '"' && date[datel-1] == '"') {
						date[datel-1] = '\0';
						date++;
					}
					topic_date = strdup(date);
				}

				format_title(topic_title, topic_section);
				continue;
			} else if (strstr(line, ".SH ") == line) {
				/* Section heading */
				if (flush_line()) printf("\n");
				c = line + 4;
				MAYBE_QUOTES();
				printf("\033[1m%s\033[0m\n", c);
				indent = extra_indent + DEFAULT_INDENTATION;
				next_indent = 0;
				continue;
			} else if (strstr(line, ".PP") == line) {
				flush_line(); printf("\n");
				indent = extra_indent + DEFAULT_INDENTATION;
				next_indent = 0;
				continue;
			} else if (strstr(line, ".TP") == line) {
				if (flush_line()) printf("\n");

				indent = extra_indent + DEFAULT_INDENTATION;
				next_indent = extra_indent + DEFAULT_INDENTATION * 2;
				continue;
			} else if (strstr(line, ".br") == line) {
				flush_line();
				continue;
			} else if (strstr(line, ".B ") == line) {
				switch_font('B');
				c = line + 3;
				MAYBE_QUOTES();
			} else if (strstr(line, ".I ") == line) {
				switch_font('I');
				c = line + 3;
				MAYBE_QUOTES();
			} else if (strstr(line, ".IR ") == line) {
				switch_font('I');
				c = line + 4;
				c += process_word(c, 0);
				switch_font('R');
				/* Continue */
			} else if (strstr(line, ".BR ") == line) {
				switch_font('B');
				c = line + 4;
				c += process_word(c, 0);
				switch_font('R');
				/* Continue */
			} else if (strstr(line, ".IP ") == line) {
				if (flush_line()) printf("\n");
				indent = extra_indent + DEFAULT_INDENTATION;
				next_indent = extra_indent + DEFAULT_INDENTATION * 2;
				c = line + 4;
				MAYBE_QUOTES();
			} else if (strstr(line, ".IP") == line) {
				if (flush_line()) printf("\n");
				indent = extra_indent + DEFAULT_INDENTATION;
				next_indent = extra_indent + DEFAULT_INDENTATION * 2;
				c = line + 3;
			} else if (strstr(line, ".sp") == line) {
				/* Just skip it ? */
				flush_line();
				printf("\n");
				continue;
			} else if (strstr(line, ".RS") == line) {
				extra_indent = DEFAULT_INDENTATION;
				indent += extra_indent;
				continue;
			} else if (strstr(line, ".RE") == line) {
				indent -= extra_indent;
				extra_indent = 0;
				continue;
			} else if (strstr(line, ".nh") == line) {
				/* TODO disable hyphenation */
				continue;
			} else if (strstr(line, ".hy") == line) {
				/* TODO reenable hyphenation */
				continue;
			} else if (strstr(line, ".TS") == line) {
				/* table start */
				flush_line();
				printing_table = 1;
				continue;
			} else if (strstr(line, ".TE") == line) {
				/* table end */
				flush_line();
				printf("\n");
				printing_table = 0;
				continue;
			} else if (strstr(line, ".nf") == line) {
				/* Treat this the same as a table; break lines where they are in the input */
				flush_line();
				printing_table = 1;
				continue;
			} else if (strstr(line, ".fi") == line) {
				flush_line();
				printing_table = 0;
				continue;
			} else {
				fprintf(stderr, "%s: %s: Found an unrecognized directive on this line: %s\n",
					argv[0], argv[i], line);
				ret = 1;
				goto _cleanup;
			}
		}

		if (*c == ' ' || *c == '\t') {
			flush_line();
			print_spaces(indent);
			current_x += indent;


			while (*c == ' ' || *c == '\t') {
				if (*c == '\t') {
					do {
						fputc(' ', stdout);
						current_x += 1;
					} while (current_x % 6);
				} else {
					fputc(' ', stdout);
					current_x += 1;
				}
				c++;
			}
		}

		while (*c) {
			c += process_word(c, delimited);
		}

		printf("\033[0m");

		if (printing_table) {
			printf("\n");
			current_x = 0;
			continue;
		}

		current_font = previous_font = 'R';

		if (next_indent) {
			indent = next_indent;
			next_indent = 0;
			flush_line();
		}
	}

	if (flush_line()) printf("\n");
	format_footer(topic_title, topic_section, topic_date);

_cleanup:
	if (topic_title) free(topic_title);
	if (topic_section) free(topic_section);
	if (topic_date) free(topic_date);

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
	while ((opt = getopt(argc, argv, "-:")) != -1) {
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
