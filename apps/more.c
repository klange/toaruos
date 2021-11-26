/**
 * @brief Print piped input or files one screenful at a time.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <toaru/decodeutf8.h>

static int term_width = 80;
static int term_height = 25;

static int term_x = 0;
static int term_yish = 1;

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
	if (c == '\t') {
		int count = 8 - (term_x % 8);
		for (int i = 0; i < count; ++i) {
			printf(" ");
		}
	} else if (c < 32 || c == 0x7F) {
		printf("\033[7m^%c\033[0m", (c < 32) ? ('@' + c) : '?');
	} else if (c > 0x7f && c < 0xa0) {
		printf("\033[7m<%02x>\033[0m", c);
	} else if (c == 0xa0) {
		printf("\033[7m \033[0m");
	} else if (c > 127) {
		if (wcwidth(c) >= 1) {
			char tmp[8] = {0};
			to_eight(c,tmp);
			printf("%s", tmp);
		} else {
			if (c < 0x10000) {
				printf("\033[7m[U+%04x]\033[0m", c);
			} else {
				printf("\033[7m[U+%06x]\033[0m", c);
			}
		}
	} else {
		printf("%c", c);
	}
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

static void next_line(void) {
	term_yish++;
	if (term_yish < term_height) {
		printf("\n");
		term_x = 0;
	} else {
		printf("\n\033[7m--More--\033[0m");
		fflush(stdout);
		do {
			char buf[1];
			read(STDERR_FILENO, buf, 1);
			char c = buf[0];
			switch (c) {
				case ' ':
					term_yish = 1;
					/* fallthrough */
				case '\n':
				case '\r':
					printf("\r\033[K");
					fflush(stdout);
					term_x = 0;
					return;
				case 'q':
					printf("\r\033[K");
					fflush(stdout);
					set_buffered();
					exit(0);
					return;
			}
		} while (1);
	}
}

static void do_file(char * name, FILE * f) {
	if (!f) {
		printf("\033[7m`%s`: %s\033[0m", name, strerror(errno));
		next_line();
		return;
	}
	uint32_t code, state = 0;
	while (!feof(f)) {
		int c = fgetc(f);
		if (c < 0) break;
		if (!decode(&state, &code, c)) {
			if (code == '\n') next_line();
			else {
				int width = char_width(code);
				if (term_x + width > term_width) {
					next_line();
				}
				char_draw(code);
				term_x += width;
			}
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}
}

int main(int argc, char * argv[]) {
	if (argc < 2 && isatty(STDIN_FILENO)) {
		fprintf(stderr, "usage: %s file...\n", argv[0]);
		return 1;
	}

	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	term_width  = w.ws_col;
	term_height = w.ws_row;
	get_initial_termios();
	set_unbuffered();

	if (argc < 2) {
		do_file("stdin",stdin);
	}

	for (int i = 1; i < argc; ++i) {
		FILE * f = fopen(argv[1], "r");
		do_file(argv[1], f);
		fclose(f);
	}

	set_buffered();
	return 0;
}
