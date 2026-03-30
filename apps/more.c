/**
 * @brief Print piped input or files one screenful at a time.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2026 K. Lange
 */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <termios.h>
#include <signal.h>
#include <sys/signal.h>
#include <sys/ioctl.h>

#include <toaru/decodeutf8.h>

static int term_width = 80;
static int term_height = 25;

static int term_x = 0;
static int term_yish = 1;

static int handle_escapes = 0;
static int stop_asking = 0;
static int exit_if_fits = 0;
static int prompted = 0;
static char * promptstring = NULL;
static int no_quit = 0;
static int use_alt_screen = 0;
static int no_scrollback = 0;

/* Line builder */
static char * lineBuf = NULL;
static size_t lineLen = 0;
static size_t lineCap = 0;

static void line_add_chr(char c) {
	if (no_scrollback) return;
	if (lineLen == lineCap) {
		lineCap = (lineCap < 5) ? 5 : lineCap * 2;
		lineBuf = realloc(lineBuf, lineCap);
	}
	lineBuf[lineLen++] = c;
}

static void line_add_str(char *s) {
	while (*s) {
		line_add_chr(*s);
		s++;
	}
}

static char ** lines = NULL;
static ssize_t linesLen = 0;
static ssize_t linesCap = 0;

static void lines_add(char * line) {
	if (linesLen == linesCap) {
		linesCap = (linesCap < 5) ? 5 : linesCap * 2;
		lines = realloc(lines, linesCap * sizeof(char*));
	}
	lines[linesLen++] = line;
}

static int to_eight(uint32_t codepoint, char * out) {
	memset(out, 0x00, 7);

	if (codepoint < 0x0080) {
		out[0] = (char)codepoint;
	} else if (codepoint < 0x0800) {
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x10000) {
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[2] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x200000) {
		out[0] = 0xF0 | (codepoint >> 18);
		out[1] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[3] = 0x80 | ((codepoint) & 0x3F);
	} else if (codepoint < 0x4000000) {
		out[0] = 0xF8 | (codepoint >> 24);
		out[1] = 0x80 | (codepoint >> 18);
		out[2] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[4] = 0x80 | ((codepoint) & 0x3F);
	} else {
		out[0] = 0xF8 | (codepoint >> 30);
		out[1] = 0x80 | ((codepoint >> 24) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 18) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[4] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[5] = 0x80 | ((codepoint) & 0x3F);
	}

	return strlen(out);
}

static void char_draw(int c) {
	char tmp[32] = {0};
	if (c == '\t') {
		int count = 8 - (term_x % 8);
		for (int i = 0; i < count; ++i) {
			printf(" ");
			line_add_chr(' ');
		}
	} else if (c < 32 || c == 0x7F) {
		sprintf(tmp, "\033[7m^%c\033[0m", (c < 32) ? ('@' + c) : '?');
	} else if (c > 0x7f && c < 0xa0) {
		sprintf(tmp, "\033[7m<%02x>\033[0m", c);
	} else if (c == 0xa0) {
		sprintf(tmp, "\033[7m \033[0m");
	} else if (c > 127) {
		if (wcwidth(c) >= 1) {
			to_eight(c,tmp);
		} else {
			if (c < 0x10000) {
				sprintf(tmp, "\033[7m[U+%04x]\033[0m", c);
			} else {
				sprintf(tmp, "\033[7m[U+%06x]\033[0m", c);
			}
		}
	} else {
		sprintf(tmp, "%c", c);
	}

	line_add_str(tmp);
	printf("%s", tmp);
}

static int char_width(int c) {
	if (c == '\t') {
		return 8 - (term_x % 8);
	} else if (c < 32 || c == 0x7F) {
		return 2; /* ^@ */
	} else if (c > 0x7f && c < 0xa0) {
		return 4; /* <XX> */
	} else if (c == 0xa0) {
		return 1; /* nbsp */
	} else if (c > 127) {
		int out = wcwidth(c);
		if (out >= 1) return out;
		return (c < 0x10000) ? 8 : 10;
	}
	return 1;
}

static struct termios old;
static void get_initial_termios(void) {
	tcgetattr(STDOUT_FILENO, &old);
}

static void set_unbuffered(void) {
	struct termios new = old;
	new.c_iflag &= (~ICRNL) & (~IXON);
	new.c_lflag &= (~ICANON) & (~ECHO);
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &new);
}

static void set_buffered(void) {
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &old);
}

static void clear_line(void) {
	printf("\r\033[K");
	fflush(stdout);
	term_x = 0;
}

static void cleanup(void) {
	if (use_alt_screen) printf("\033[?1049l");
	printf("\033[?25h");
	set_buffered();
	fflush(stdout);
}

static void quit_cleanly(int sig) {
	cleanup();
	signal(sig, SIG_DFL);
	raise(sig);
}

static int do_history_mode(int mode, char * title) {
	/* Enter history mode; scroll up one line */
	ssize_t offset = 1;

	if (mode == 2) {
		offset = linesLen;
	} if (mode == 3) {
		offset = term_height;
	}

	while (1) {
		if (linesLen - term_height + 1 < offset) {
			offset = linesLen - term_height + 1;
		}

		int break_here = 0;
		if (offset < 0) {
			term_yish = term_height + offset + 1;
			offset = 0;
			break_here = 1;
		}

		/* We're going to do this the slow way because actually scrolling is a bit scary. */
		printf("\033[H");
		for (ssize_t line = 1; line < term_height; ++line) {
			ssize_t this = linesLen - term_height - offset + line;
			if (this >= 0 && this < linesLen) printf("%s\033[K\n",lines[linesLen - term_height - offset + line]);
		}

		if (offset == 0 && break_here) {
			printf("\033[J");
			return 0;
		}

		int attop = (offset == linesLen - term_height + 1);
		printf("\r\033[K\033[7m%s%s\033[0m", title, attop ? " (TOP)" : "");
		printf("\033[?25h");
		fflush(stdout);

		char c;
		read(STDERR_FILENO, &c, 1);
		printf("\033[?25l");
		fflush(stdout);

		switch (c) {
			case '~':
				/* ignore */
				break;
			case 'k':
			case 'A':
				clear_line();
				offset += 1;
				break;
			case ' ':
			case '6':
				clear_line();
				offset -= term_height - 1;
				break;
			case '5':
				clear_line();
				offset += term_height - 1;
				break;
			case 'H':
				clear_line();
				offset = linesLen;
				break;
			case 'F':
			case 'G':
				offset = 0;
				clear_line();
				stop_asking = 1;
				break;
			case 'B':
			case 'j':
			case '\n':
			case '\r':
				clear_line();
				offset -= 1;
				break;
			case 'q':
				clear_line();
				return 1;
		}
	}

}

static int next_line(char * title, int forced, int atend) {
	term_yish++;

	/* Whenever we hit "next_line", store the line we were just building */
	if (!forced && !no_scrollback) {
		if (!lineLen) {
			lines_add(strdup(""));
		} else {
			char * c = malloc(lineLen + 1);
			memcpy(c, lineBuf, lineLen);
			c[lineLen] = 0;
			lines_add(c);
		}
		lineLen = 0;
	}

	if (!forced && (term_yish < term_height || stop_asking)) {
		printf("\n");
		term_x = 0;
	} else {
		prompted = 1;
_reprint_prompt:
		printf("%s\033[7m%s%s\033[0m", forced ? "" : "\n", title, atend ? " (END)" : "");
		do {
			printf("\033[?25h");
			fflush(stdout);
			char c;
			read(STDERR_FILENO, &c, 1);
			printf("\033[?25l");
			fflush(stdout);
			switch (c) {
				case '~':
					/* ignore */
					break;
				case '6':
				case ' ':
					term_yish = 1;
					/* fallthrough */
				case 'B': /* Means down generally ends up here */
				case 'j': /* For the vi users. */
				case '\n':
				case '\r':
					clear_line();
					return 0;
				case 'q':
					clear_line();
					return 1;
				case 'F':
				case 'G':
					clear_line();
					stop_asking = 1;
					return 0;
				case 'A':
				case 'k':
					if (no_scrollback) goto _no_scrollback;
					if (do_history_mode(1, title)) return 1;
					clear_line();
					if (forced) goto _reprint_prompt;
					return 0;
				case 'H':
					if (no_scrollback) goto _no_scrollback;
					if (do_history_mode(2, title)) return 1;
					clear_line();
					if (forced) goto _reprint_prompt;
					return 0;
				case '5':
					if (no_scrollback) goto _no_scrollback;
					if (do_history_mode(3, title)) return 1;
					clear_line();
					if (forced) goto _reprint_prompt;
					return 0;
				default:
					printf("\r\033[K\033[7munreocgnized command:\033[0m %c", c);
					fflush(stdout);
					break;
			}

			continue;
_no_scrollback:
			clear_line();
			printf("\r\033[K\033[7mScrollback is disabled.\033[0m");
			fflush(stdout);
		} while (1);
	}

	return 0;
}

static int handle_one(int code, char * name) {
	int width = char_width(code);
	if (term_x + width > term_width) {
		if (next_line(promptstring ? promptstring : name, 0, 0)) return 1;
	}
	char_draw(code);
	term_x += width;
	return 0;
}

static int do_file(char * name, FILE * f, int opti, int argc) {
	if (linesLen) linesLen = 0;
	if (lineLen) lineLen = 0;

	if (!f) {
		printf("%s: %s\n", name, strerror(errno));
		return next_line("Press RETURN to continue, q to exit.", 1, 0);
	}
	int is_escaped = 0;
	int maybe_escaped = 0;

	term_x = 0;


	uint32_t code, state = 0;
	while (!feof(f)) {
		int c = fgetc(f);
		if (c < 0) break;
		if (!decode(&state, &code, c)) {
			if (code == '\n') {
				if (next_line(promptstring ? promptstring : name, 0, 0)) return 1;
			} else if (is_escaped) {
				if (code >= 'A' && code <= 'z') {
					is_escaped = 0;
				}
				char tmp[8] = {0};
				to_eight(code,tmp);
				printf("%s", tmp);
				line_add_str(tmp);
			} else if (maybe_escaped) {
				if (code == '[') {
					maybe_escaped = 0;
					is_escaped = 1;
					printf("\033[");
					line_add_str("\033[");
				} else {
					handle_one('\033',name);
					handle_one(code,name);
				}
			} else if (handle_escapes) {
				if (code == '\033') {
					maybe_escaped = 1;
				} else {
					handle_one(code,name);
				}
			} else {
				handle_one(code,name);
			}
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}

	/* If we never prompted, the file fit on the screen. If this is the last (only) file,
	 * and we had the -F option, we can skip printing the forced END prompt. */
	if (!prompted && exit_if_fits && opti + 1 >= argc) return 0;

	do {
		stop_asking = 0;
		if (next_line(promptstring ? promptstring : name, 1, 1)) return 1;
	} while (no_quit);
	return 0;
}

static int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
		"usage: %s [-r] [-F] [-P " X_S "str" X_E "] [" X_S "file" X_E "...]\n"
		"\n"
		"Print files one screenful at a time. If no files are provided,\n"
		"attempts to read from stdin only if it is not a terminal.\n"
		"\n"
		"Options:\n"
		"\n"
		" -r       " X_S "Try to parse some escape sequences to support, eg., color." X_E "\n"
		"          " X_S "Only 'm' sequences are likely to work; this option is meant" X_E "\n"
		"          " X_S "for things like manpages or 'bim -c'." X_E "\n"
		" -F       " X_S "If the file fits on one screen, exit after printing." X_E "\n"
		"          " X_S "This option only works if there is only one file." X_E "\n"
		" -P " X_S "str   Instead of showing file names as the command prompt, use the" X_E "\n"
		"          " X_S "provided string, eg. '-P man' will display the prompt 'man'." X_E "\n"
		" --stay   " X_S "Normally, more will quit when asked to advance when at the end" X_E "\n"
		"          " X_S "of a file, but with this option it will stay running until a" X_E "\n"
		"          " X_S "more explicit quit command (eg. 'q') is issued." X_E "\n"
		" --alt    " X_S "Switch to the alternate buffer and clear the screen on startup." X_E "\n"
		" --scroll " X_S "Disable scrollback." X_E "\n"
		" --help   " X_S "Show this help text." X_E "\n"
		"\n"
		"Command input is received on stderr. The following commands are accepted when\n"
		"prompted during viewing of a file:\n"
		"\n"
		" RETURN   " X_S "Proceed to the next line, or the next file if at the end." X_E "\n"
		"          " X_S "'B' and 'j' are also accepted as aliases." X_E "\n"
		" SPACE    " X_S "Proceed to the next screenful of text." X_E "\n"
		" G        " X_S "Continue outputting this file until reaching the end." X_E "\n"
		" q        " X_S "Quit immediately, ignoring all other files." X_E "\n"
		"\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
#ifdef __APPLE__
	/* TODO figure out a better way to do this; maybe just LC_CTYPE? */
	setlocale(LC_ALL, "en_US.UTF-8");
#else
	setlocale(LC_ALL, "");
#endif

	int opt;

	while ((opt = getopt(argc, argv, "rFP:-:")) != -1) {
		switch (opt) {
			case 'r':
				handle_escapes = 1;
				break;
			case 'F':
				exit_if_fits = 1;
				break;
			case 'P':
				promptstring = optarg;
				break;
			case '-':
				if (!strcmp(optarg,"help")) {
					usage(argv);
					return 0;
				}
				if (!strcmp(optarg,"stay")) {
					no_quit = 1;
					break;
				}
				if (!strcmp(optarg,"alt")) {
					use_alt_screen = 1;
					break;
				}
				if (!strcmp(optarg,"scroll")) {
					no_scrollback = 1;
					break;
				}
				fprintf(stderr, "%s: '--%s' is not a recognized long option\n", argv[0], optarg);
				/* fallthrough */
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc && isatty(STDIN_FILENO)) {
		fprintf(stderr, "%s: stdin is a TTY and no file names were provided.\n", argv[0]);
		return 1;
	}

	if (!isatty(STDOUT_FILENO)) {
		fprintf(stderr, "%s: This implementation refuses to write to a non-terminal.\n", argv[0]);
		return 1;
	}

	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	term_width  = w.ws_col;
	term_height = w.ws_row;
	get_initial_termios();
	set_unbuffered();
	printf("\033[?25l");

	signal(SIGINT, quit_cleanly);
	signal(SIGQUIT, quit_cleanly);

	if (use_alt_screen) printf("\033[?1049h\033[H\033[2J");

	if (optind == argc) {
		do_file("stdin",stdin, optind, argc);
	}

	for (; optind < argc; optind++) {
		FILE * f = fopen(argv[optind], "r");
		if (do_file(argv[optind], f, optind, argc)) break;
		if (f) fclose(f);
	}

	cleanup();
	return 0;
}
