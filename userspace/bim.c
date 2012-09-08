/*
 * edit
 *
 * A super-simple one-pass file... uh "editor".
 * Takes stdin until a blank line and writes
 * it back to standard out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#ifdef __linux__
#include <stdio_ext.h>
#include <sys/ioctl.h>
#include <termios.h>
#define BACKSPACE_KEY 0x7F
#else
#include <syscall.h>
#define BACKSPACE_KEY 0x08
#endif

#include "lib/utf8decode.h"

#define BLOCK_SIZE 256
#define ENTER_KEY     '\n'


static struct _env {
	int    width;
	int    height;
	int    bottom_size;
	short  lineno_width;
	char * file_name;
	int    offset;
	int    line_no;
	int    line_count;
	int    line_avail;
	int    col_no;
	short  modified;
} env;

typedef struct {
	uint8_t  display_width;
	uint16_t codepoint;
} __attribute__((packed)) char_t;

typedef struct {
	uint32_t available;
	uint32_t actual;
	char_t   text[0];
} line_t;

line_t ** lines = 0;

line_t * line_insert(line_t * line, char_t c, uint32_t offset) {
	if (line->actual == line->available) {
		line->available *= 2;
		line = realloc(line, sizeof(line_t) + sizeof(char_t) * line->available);
	}
	if (offset < line->actual) {
		memmove(&line->text[offset+1], &line->text[offset], sizeof(char_t) * (line->actual - offset));
	}
	line->text[offset] = c;
	line->actual += 1;
	return line;
}

void line_delete(line_t * line, uint32_t offset) {
	if (offset == 0) return;
	if (offset < line->actual) {
		memmove(&line->text[offset-1], &line->text[offset], sizeof(char_t) * (line->actual - offset - 1));
	}
	line->actual -= 1;
}

line_t ** add_line(line_t ** lines, uint32_t offset) {
	if (env.line_count == env.line_avail) {
		env.line_avail *= 2;
		lines = realloc(lines, sizeof(line_t *) * env.line_avail);
	}
	if (offset < env.line_count) {
		memmove(&lines[offset+1], &lines[offset], sizeof(line_t *) * (env.line_count - offset));
	}
	lines[offset] = malloc(sizeof(line_t) + sizeof(char_t) * 32);
	lines[offset]->available = 32;
	lines[offset]->actual    = 0;
	env.line_count += 1;
	return lines;
}

line_t ** split_line(line_t ** lines, uint32_t line, uint32_t split) {
	if (split == 0) {
		return add_line(lines, line - 1);
	}
	if (env.line_count == env.line_avail) {
		env.line_avail *= 2;
		lines = realloc(lines, sizeof(line_t *) * env.line_avail);
	}
	if (line < env.line_count) {
		memmove(&lines[line+1], &lines[line], sizeof(line_t *) * (env.line_count - line));
	}
	uint32_t remaining = lines[line-1]->actual - split;

	uint32_t v = remaining;
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;

	lines[line] = malloc(sizeof(line_t) + sizeof(char_t) * v);
	lines[line]->available = v;
	lines[line]->actual = remaining;

	memmove(lines[line]->text, &lines[line-1]->text[split], sizeof(char_t) * remaining);
	lines[line-1]->actual = split;

	env.line_count += 1;

	return lines;
}

void setup_buffer() {
	if (lines) {
		for (int i = 0; i < env.line_count; ++i) {
			free(lines[i]);
		}
		free(lines);
	}
	env.line_no     = 1;
	env.col_no      = 1;
	env.line_count  = 1; /* XXX */
	env.modified    = 0;
	env.bottom_size = 2;
	env.offset      = 0;
	env.line_avail  = 8;

	lines = malloc(sizeof(line_t *) * env.line_avail);
	lines[0] = malloc(sizeof(line_t) + sizeof(char_t) * 32);
	lines[0]->available = 32;
	lines[0]->actual    = 0;

}

#define COLOR_FG        230
#define COLOR_BG        235
#define COLOR_CURSOR    15
#define COLOR_ALT_FG    244
#define COLOR_ALT_BG    236
#define COLOR_NUMBER_BG 16
#define COLOR_NUMBER_FG 101
#define COLOR_STATUS_BG 238
#define COLOR_TABBAR_BG 230
#define COLOR_TAB_BG    248
#define COLOR_ERROR_FG  15
#define COLOR_ERROR_BG  196

uint32_t codepoint;
uint32_t codepoint_r;
uint32_t state = 0;
uint32_t istate = 0;

#ifdef __linux__
struct termios old;
#endif

void set_unbuffered() {
#ifdef __linux__
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
#else
	printf("\033[1560z");
	fflush(stdout);
#endif
}

void set_buffered() {
#ifdef __linux__
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
#else
	printf("\033[1561z");
	fflush(stdout);
#endif
}

int to_eight(uint16_t codepoint, uint8_t * out) {
	memset(out, 0x00, 4);

	if (codepoint < 0x0080) {
		out[0] = (uint8_t)codepoint;
	} else if (codepoint < 0x0800) {
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x3F);
	} else {
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[2] = 0x80 | (codepoint & 0x3F);
	}

	return strlen(out);
}

int codepoint_width(uint16_t codepoint) {
	if (codepoint < 32) {
		return 4;
	}
	if (codepoint > 256) {
		char tmp[4];
		int x, y;
		to_eight(codepoint, tmp);
#ifdef __linux__
		__fpurge(stdin);
#else
		fpurge(stdin);
#endif
		printf("\033[s\033[%d;1H%s\033[6n", env.height, tmp);
		fflush(stdout);
		scanf("\033[%d;%dR", &y, &x);

		return x - 1;
	}
	return 1;
}


void place_cursor(int x, int y) {
	printf("\033[%d;%dH", y, x);
	fflush(stdout);
}

void place_cursor_h(int h) {
	printf("\033[%dG", h);
	fflush(stdout);
}

void set_colors(int fg, int bg) {
	printf("\033[48;5;%dm", bg);
	printf("\033[38;5;%dm", fg);
	fflush(stdout);
}

void clear_to_end() {
	printf("\033[K");
	fflush(stdout);
}

void set_bold() {
	printf("\033[1m");
	fflush(stdout);
}

void reset() {
	printf("\033[0m");
	fflush(stdout);
}

void clear_screen() {
	printf("\033[H\033[2J");
	fflush(stdout);
}

void redraw_tabbar() {
	place_cursor(1,1);
	set_colors(COLOR_FG, COLOR_BG);
	set_bold();
	if (env.modified) {
		printf(" +");
	}
	if (env.file_name) {
		printf(" %s ", env.file_name);
	} else {
		printf(" [No Name] ");
	}
	reset();
	set_colors(COLOR_FG, COLOR_TABBAR_BG);
	clear_to_end();
}

int log_base_10(unsigned int v) {
	int r = (v >= 1000000000) ? 9 : (v >= 100000000) ? 8 : (v >= 10000000) ? 7 :
		(v >= 1000000) ? 6 : (v >= 100000) ? 5 : (v >= 10000) ? 4 :
		(v >= 1000) ? 3 : (v >= 100) ? 2 : (v >= 10) ? 1 : 0;
	return r;
}

void render_line(line_t * line, int width) {
	uint32_t i = 0;
	uint32_t j = 0;
	set_colors(COLOR_FG, COLOR_BG);
	while (i < line->actual) {
		char_t c = line->text[i];
		if (j + c.display_width >= width) {
			set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
			while (j < width) {
				printf("…");
				j++;
			}
			break;
		}
		j += c.display_width;
		if (c.codepoint == '\t') {
			set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
			printf("»···");
			set_colors(COLOR_FG, COLOR_BG);
		} else if (c.codepoint < 32) {
			set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
			printf("<%02x>", c.codepoint);
			set_colors(COLOR_FG, COLOR_BG);
		} else {
			char tmp[4];
			to_eight(c.codepoint, tmp);
			printf("%s", tmp);
		}
		i += 1;
	}
}

void redraw_text() {
	int l = env.height - env.bottom_size - 1;
	int j = 0;

	int num_size = log_base_10(env.line_count) + 2;
	for (int x = env.offset; j < l && x < env.line_count; x++) {
		place_cursor(1,2 + j);
		/* draw line number */
		set_colors(COLOR_NUMBER_FG, COLOR_ALT_FG);
		printf(" ");
		set_colors(COLOR_NUMBER_FG, COLOR_NUMBER_BG);
		for (int y = 0; y < num_size - log_base_10(x + 1); ++y) {
			printf(" ");
		}
		printf("%d ", x + 1);
		set_colors(COLOR_FG, COLOR_BG);
		clear_to_end();
		render_line(lines[x], env.width - 3 - num_size);
		j++;
	}
	for (; j < l; ++j) {
		place_cursor(1,2 + j);
		set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
		printf("~");
		clear_to_end();
	}
}

void redraw_statusbar() {
	place_cursor(1, env.height - 1);
	set_colors(COLOR_FG, COLOR_STATUS_BG);
	if (env.file_name) {
		printf("%s", env.file_name);
	} else {
		printf("[No Name]");
	}
	if (env.modified) {
		printf(" [+]");
	}
	clear_to_end();
	char right_hand[1024];
	sprintf(right_hand, "Line %d/%d Col: %d ", env.line_no, env.line_count, env.col_no);
	place_cursor_h(env.width - strlen(right_hand));
	printf("%s",right_hand);
	fflush(stdout);
}

void redraw_commandline() {
	place_cursor(1, env.height);
	set_colors(COLOR_FG, COLOR_BG);
	clear_to_end();
}

void redraw_all() {
	redraw_tabbar();
	redraw_text();
	redraw_statusbar();
	redraw_commandline();
}

void update_title() {
	char cwd[1024] = {'/',0};
#ifdef __linux__
	getcwd(cwd, 1024);
#else
	syscall_getcwd(cwd, 1024);
#endif
	printf("\033]1;%s%s (%s) - BIM\007", env.file_name, env.modified ? " +" : "", cwd);
}

void set_modified() {
	if (env.modified) return;

	update_title();
	env.modified = 1;
	redraw_tabbar();
	redraw_statusbar();
}

void render_error(char * message) {
	redraw_commandline();
	set_colors(COLOR_ERROR_FG, COLOR_ERROR_BG);
	printf("%s", message);
	fflush(stdout);
}

void render_cursor() {
	printf("\033[1z");
	fflush(stdout);
}

void place_cursor_actual() {
	int num_size = log_base_10(env.line_count) + 5;
	int x = num_size + 1;
	for (int i = 0; i < env.col_no - 1; ++i) {
		char_t * c = &lines[env.line_no-1]->text[i];
		x += c->display_width;
	}
	int y = env.line_no - env.offset + 1;

	place_cursor(x,y);
#ifndef __linux__
	render_cursor();
#endif
}

void initialize() {
#ifdef __linux__
	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);
	env.width = w.ws_col;
	env.height = w.ws_row;
#else
	printf("\033[1003z");
	fflush(stdout);
	scanf("%d,%d", &env.width, &env.height);
	fpurge(stdin);
#endif
	set_unbuffered();

	update_title();
	setup_buffer();
}

void goto_line(int line) {
	if (line < 1) line = 1;
	if (line > env.line_count) line = env.line_count;
	env.offset = line - 1;
	env.line_no = line;
	env.col_no  = 1;
	redraw_all();
}

void add_buffer(uint8_t * buf, int size) {
	for (int i = 0; i < size; ++i) {
		if (!decode(&state, &codepoint_r, buf[i])) {
			uint32_t c = codepoint_r;
			if (c == '\n') {
				line_t ** nlines = add_line(lines, env.line_no);
				if (nlines != lines) {
					lines = nlines;
				}
				env.col_no = 1;
				env.line_no += 1;
			} else {
				char_t _c;
				_c.codepoint = (uint16_t)c;
				_c.display_width = codepoint_width((uint16_t)c);
				line_t * line  = lines[env.line_no - 1];
				line_t * nline = line_insert(line, _c, env.col_no - 1);
				if (line != nline) {
					lines[env.line_no - 1] = nline;
				}
				env.col_no += 1;
			}
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}
}


void open_file(char * file) {
	if (env.modified) {
		render_error("No writes since last edit, you'll need to save this to open a new file.\n");
		return;
	}

	if (env.file_name) {
		free(env.file_name);
	}
	env.file_name = malloc(strlen(file) + 1);
	memcpy(env.file_name, file, strlen(file) + 1);

	setup_buffer();

	FILE * f = fopen(file, "r");

	if (!f) {
		char buf[1024];
		snprintf(buf, 1204, "Could not open %s", file);
		render_error(buf);
		return;
	}

	size_t length;

	fseek(f, 0, SEEK_END);
	length = ftell(f);
	fseek(f, 0, SEEK_SET);

	uint8_t buf[BLOCK_SIZE];

	while (length > BLOCK_SIZE) {
		fread(buf, BLOCK_SIZE, 1, f);
		add_buffer(buf, BLOCK_SIZE);
		length -= BLOCK_SIZE;
	}
	if (length > 0) {
		fread(buf, 1, length, f);
		add_buffer((uint8_t *)buf, length);
	}

	update_title();
	goto_line(0);

	fclose(f);
}

void quit() {
	set_buffered();
	reset();
	clear_screen();
	printf("Thanks for flying bim!\n");
	exit(0);
}

int isnumeric(char * str) {
	char *p = str;
	while (*p) {
		if (*p < '0' || *p > '9') return 0;
		++p;
	}
	return 1;
}

void process_command(char * cmd) {
	char *p, *argv[512], *last;
	int argc = 0;
	for ((p = strtok_r(cmd, " ", &last)); p;
			(p = strtok_r(NULL, " ", &last)), argc++) {
		if (argc < 511) argv[argc] = p;
	}
	argv[argc] = NULL;

	if (argc < 1) {
		/* no op */
		return;
	}
	if (!strcmp(argv[0], "e")) {
		if (argc > 1) {
			open_file(argv[1]);
		} else {
			render_error("Expected a file to open...");
		}
	} else if (!strcmp(argv[0], "q")) {
		if (env.modified) {
			render_error("No write since last change. Use :q! to force exit.");
		} else {
			quit();
		}
	} else if (!strcmp(argv[0], "q!")) {
		quit();
	} else if (isnumeric(argv[0])) {
		goto_line(atoi(argv[0]));
	} else {
		char buf[512];
		snprintf(buf, 512, "Not an editor command: %s", argv[0]);
		render_error(buf);
	}
}

void command_mode() {
	char c;
	char buffer[1024] = {0};
	int  buffer_len = 0;

	redraw_commandline();
	printf(":");
	fflush(stdout);

	while (c = fgetc(stdin)) {
		if (c == '\033') {
			break;
		} else if (c == ENTER_KEY) {
			process_command(buffer);
			break;
		} else if (c == BACKSPACE_KEY) {
			if (buffer_len > 0) {
				buffer_len -= 1;
				buffer[buffer_len] = '\0';
				redraw_commandline();
				printf(":%s", buffer);
				fflush(stdout);
			} else {
				redraw_commandline();
				break;
			}
		} else {
			buffer[buffer_len] = c;
			buffer_len++;
			printf("%c", c);
			fflush(stdout);
		}
	}
}

void insert_mode() {
	char cin;
	redraw_commandline();
	set_bold();
	printf("-- INSERT --");
	reset();
	place_cursor_actual();
	set_colors(COLOR_FG, COLOR_BG);
	while (cin = fgetc(stdin)) {
		uint32_t c;
		if (!decode(&istate, &c, cin)) {
			switch (c) {
				case '\033':
					if (env.col_no > lines[env.line_no-1]->actual) {
						env.col_no = lines[env.line_no-1]->actual;
					}
					if (env.col_no == 0) env.col_no = 1;
					redraw_commandline();
					return;
				case BACKSPACE_KEY:
					if (env.col_no > 1) {
						line_delete(lines[env.line_no - 1], env.col_no - 1);
						env.col_no -= 1;
						redraw_text();
						set_modified();
						redraw_statusbar();
						place_cursor_actual();
					}
					break;
				case ENTER_KEY:
					if (env.col_no == lines[env.line_no - 1]->actual + 1) {
						line_t ** nlines = add_line(lines, env.line_no);
						if (nlines != lines) {
							lines = nlines;
						}
					} else {
						/* oh oh god we're all gonna die */
						line_t ** nlines = split_line(lines, env.line_no, env.col_no - 1);
						if (nlines != lines) {
							lines = nlines;
						}
					}
					env.col_no = 1;
					env.line_no += 1;
					if (env.line_no > env.offset + env.height - env.bottom_size - 1) {
						env.offset += 1;
					}
					redraw_text();
					set_modified();
					redraw_statusbar();
					place_cursor_actual();
					break;
				default:
					{
						char_t _c;
						_c.codepoint = c;
						_c.display_width = codepoint_width(c);
						line_t * line  = lines[env.line_no - 1];
						line_t * nline = line_insert(line, _c, env.col_no - 1);
						if (line != nline) {
							lines[env.line_no - 1] = nline;
						}
						redraw_text(); /* XXX */
						env.col_no += 1;
						set_modified();
						redraw_statusbar();
						place_cursor_actual();
					}
					break;
			}
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}
}

int main(int argc, char * argv[]) {
	initialize();

	if (argc > 1) {
		open_file(argv[1]);
	}

	while (1) {
		redraw_all();
		place_cursor_actual();
		char buf[1];
		char c;
		while (c = fgetc(stdin)) {
			switch (c) {
				case '\033':
					redraw_all();
					break;
				case ':':
					/* Switch to command mode */
					command_mode();
					break;
				case 'j':
					if (env.line_no < env.line_count) {
						env.line_no += 1;
						if (env.col_no > lines[env.line_no-1]->actual) {
							env.col_no = lines[env.line_no-1]->actual;
						}
						if (env.col_no == 0) env.col_no = 1;
						if (env.line_no > env.offset + env.height - env.bottom_size - 1) {
							env.offset += 1;
							redraw_text();
						}
						redraw_statusbar();
						place_cursor_actual();
					}
					break;
				case 'k':
					if (env.line_no > 1) {
						env.line_no -= 1;
						if (env.col_no > lines[env.line_no-1]->actual) {
							env.col_no = lines[env.line_no-1]->actual;
						}
						if (env.col_no == 0) env.col_no = 1;
						if (env.line_no <= env.offset) {
							env.offset -= 1;
							redraw_text();
						}
						redraw_statusbar();
						place_cursor_actual();
					}
					break;
				case 'h':
					if (env.col_no > 1) {
						env.col_no -= 1;
						redraw_statusbar();
						place_cursor_actual();
					}
					break;
				case 'l':
					if (env.col_no < lines[env.line_no-1]->actual) {
						env.col_no += 1;
						redraw_statusbar();
						place_cursor_actual();
					}
					break;
				case ' ':
					goto_line(env.line_no + env.height - 6);
					break;
				case 'O':
					{
						line_t ** nlines = add_line(lines, env.line_no-1);
						if (nlines != lines) {
							lines = nlines;
						}
						env.col_no = 1;
						redraw_text();
						set_modified();
						place_cursor_actual();
						goto _insert;
					}
				case 'o':
					{
						line_t ** nlines = add_line(lines, env.line_no);
						if (nlines != lines) {
							lines = nlines;
						}
						env.col_no = 1;
						env.line_no += 1;
						if (env.line_no > env.offset + env.height - env.bottom_size - 1) {
							env.offset += 1;
						}
						redraw_text();
						set_modified();
						place_cursor_actual();
						goto _insert;
					}
				case 'a':
					if (env.col_no < lines[env.line_no-1]->actual + 1) {
						env.col_no += 1;
					}
					goto _insert;
				case '$':
					env.col_no = lines[env.line_no-1]->actual+1;
					break;
				case '0':
					env.col_no = 1;
					break;
				case 'i':
_insert:
					insert_mode();
					break;
				default:
					break;
			}
			place_cursor_actual();
		}
_continue:
		printf("%c", c);
	}

#if 0
	if (argc < 2) {
		fprintf(stderr, "%s: argument expected\n", argv[0]);
		return 1;
	}

	fprintf(stderr, "Writing out file again:\n\n");

	for (int i = 0; i < file_len_unicode; ++i) {
		uint8_t buf[4];
		int len = to_eight(file_buffer[i], buf);
		fwrite(buf, len, 1, stdout);
	}
	fflush(stdout);
#endif


	return 0;
}
