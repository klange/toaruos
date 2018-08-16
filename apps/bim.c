/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 * Copyright (C) 2014 Lioncash
 *
 * bim - Text editor
 *
 * bim is a "Bad IMitation" of vim. This is the standard text
 * editor fo ToaruOS. It provides basic editing capabilities.
 *
 * Expansion of bim is planned. I'd love to see it one day have
 * syntax hilighting, for example. It's also painfully slow,
 * so speed improvement is a must - most probably by not
 * redrawing the entire screen all the time.
 */
#define _XOPEN_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>

#ifdef __linux__
#include <stdio_ext.h>
#define BACKSPACE_KEY 0x7F
#else
#include <sys/fswait.h>
#define BACKSPACE_KEY 0x08
#endif

#include <locale.h>

#include <wchar.h>

#include <toaru/utf8decode.h>

#define BLOCK_SIZE 256
#define ENTER_KEY     '\n'

typedef struct {
	uint8_t  display_width;
	uint16_t codepoint;
} __attribute__((packed)) char_t;

typedef struct {
	int available;
	int actual;
	char_t   text[0];
} line_t;

char * term = NULL;

int term_width, term_height;
int csr_x_actual, csr_y_actual;

int _bim_unget = -1;

void bim_unget(int c) {
	_bim_unget = c;
}

int bim_getch(void) {
	if (_bim_unget != -1) {
		int out = _bim_unget;
		_bim_unget = -1;
		return out;
	}
	int fds[] = {STDIN_FILENO};
#ifdef __linux__
#error Need to replace fswait2 with select/poll
#else
	int index = fswait2(1,fds,200);
	if (index == 0) {
		return fgetc(stdin);
	} else {
		return -1;
	}
#endif
}

typedef struct _env {
	int    bottom_size;
	short  lineno_width;
	char * file_name;
	int    offset;
	int    coffset;
	int    line_no;
	int    line_count;
	int    line_avail;
	int    col_no;
	short  modified;
	line_t ** lines;
} buffer_t;

buffer_t * env;

int    buffers_len;
int    buffers_avail;
buffer_t ** buffers;

buffer_t * buffer_new() {
	if (buffers_len == buffers_avail) {
		buffers_avail *= 2;
		buffers = realloc(buffers, sizeof(buffer_t *) * buffers_avail);
	}
	buffers[buffers_len] = malloc(sizeof(buffer_t));
	memset(buffers[buffers_len], 0x00, sizeof(buffer_t));
	buffers_len++;

	return buffers[buffers_len-1];
}

buffer_t * buffer_close(buffer_t * buf) {
	int i;
	for (i = 0; i < buffers_len; i++) {
		if (buf == buffers[i])
			break;
	}
	if (i == buffers_len) {
		return env; /* wtf */
	}

	if (i != buffers_len - 1) {
		memmove(&buffers[i], &buffers[i+1], buffers_len - i);
	}

	buffers_len--;
	if (!buffers_len) { 
		return NULL;
	}
	if (i == buffers_len) {
		return buffers[buffers_len-1];
	}
	return buffers[buffers_len];
}

line_t * line_insert(line_t * line, char_t c, int offset) {
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

void line_delete(line_t * line, int offset) {
	if (offset == 0) return;
	if (offset < line->actual) {
		memmove(&line->text[offset-1], &line->text[offset], sizeof(char_t) * (line->actual - offset - 1));
	}
	line->actual -= 1;
}

void render_error(char * message);
line_t ** remove_line(line_t ** lines, int offset) {

	if (env->line_count == 1) {
		lines[offset]->actual = 0;
		return lines;
	}

	free(lines[offset]);
	if (offset < env->line_count) {
		memmove(&lines[offset], &lines[offset+1], sizeof(line_t *) * (env->line_count - (offset - 1)));
		lines[env->line_count-1] = NULL;
	}
	env->line_count -= 1;
	return lines;
}

line_t ** add_line(line_t ** lines, int offset) {
	if (offset > env->line_count) return lines;

	if (env->line_count == env->line_avail) {
		env->line_avail *= 2;
		lines = realloc(lines, sizeof(line_t *) * env->line_avail);
	}
	if (offset < env->line_count) {
		memmove(&lines[offset+1], &lines[offset], sizeof(line_t *) * (env->line_count - offset));
	}
	lines[offset] = malloc(sizeof(line_t) + sizeof(char_t) * 32);
	lines[offset]->available = 32;
	lines[offset]->actual    = 0;
	env->line_count += 1;
	return lines;
}

line_t ** merge_lines(line_t ** lines, int lineb) {
	int linea = lineb - 1;

	while (lines[linea]->available < lines[linea]->actual + lines[lineb]->actual) {
		lines[linea]->available *= 2;
		lines[linea] = realloc(lines[linea], sizeof(line_t) + sizeof(char_t) * lines[linea]->available);
	}

	memcpy(&lines[linea]->text[lines[linea]->actual], &lines[lineb]->text, sizeof(char_t) * lines[lineb]->actual);
	lines[linea]->actual = lines[linea]->actual + lines[lineb]->actual;

	return remove_line(lines, lineb);
}

line_t ** split_line(line_t ** lines, int line, int split) {
	if (split == 0) {
		return add_line(lines, line - 1);
	}
	if (env->line_count == env->line_avail) {
		env->line_avail *= 2;
		lines = realloc(lines, sizeof(line_t *) * env->line_avail);
	}
	if (line < env->line_count) {
		memmove(&lines[line+1], &lines[line], sizeof(line_t *) * (env->line_count - line));
	}
	int remaining = lines[line-1]->actual - split;

	int v = remaining;
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

	env->line_count += 1;

	return lines;
}

void setup_buffer(buffer_t * env) {
	if (env->lines) {
		for (int i = 0; i < env->line_count; ++i) {
			free(env->lines[i]);
		}
		free(env->lines);
	}
	env->line_no     = 1;
	env->col_no      = 1;
	env->line_count  = 1; /* XXX */
	env->modified    = 0;
	env->bottom_size = 2;
	env->offset      = 0;
	env->line_avail  = 8;

	env->lines = malloc(sizeof(line_t *) * env->line_avail);
	env->lines[0] = malloc(sizeof(line_t) + sizeof(char_t) * 32);
	env->lines[0]->available = 32;
	env->lines[0]->actual    = 0;

}

#define COLOR_FG        230
#define COLOR_BG        235
#define COLOR_CURSOR    15
#define COLOR_ALT_FG    244
#define COLOR_ALT_BG    236
#define COLOR_NUMBER_BG 232
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

struct termios old;

void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

int to_eight(uint32_t codepoint, char * out) {
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

int codepoint_width(uint16_t codepoint) {
	if (codepoint < 32) {
		return 4;
	}
	if (codepoint > 256) {
		return wcwidth(codepoint);
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

void set_underline() {
	printf("\033[4m");
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

void hide_cursor() {
	printf("\033[?25l");
	fflush(stdout);
}

void redraw_tabbar() {
	hide_cursor();
	place_cursor(1,1);
	for (int i = 0; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];
		if (_env == env) {
			reset();
			set_colors(COLOR_FG, COLOR_BG);
			set_bold();
		} else {
			reset();
			set_colors(COLOR_FG, COLOR_TAB_BG);
			set_underline();
		}
		if (_env->modified) {
			printf(" +");
		}
		if (_env->file_name) {
			printf(" %s ", _env->file_name);
		} else {
			printf(" [No Name] ");
		}
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

void render_line(line_t * line, int width, int offset) {
	int i = 0;
	int j = 0;
	set_colors(COLOR_FG, COLOR_BG);
	int remainder = 0;
	while (i < line->actual) {
		char_t c = line->text[i];
		if (remainder) {
			if (j >= offset) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("-");
				set_colors(COLOR_FG, COLOR_BG);
			}
			remainder--;
			j++;
			if (remainder == 0) {
				i++;
			}
			continue;
		}
		if (j >= offset) {
			if (j - offset + c.display_width >= width) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				while (j - offset < width - 1) {
					printf("-");
					j++;
				}
				printf(">");
				break;
			}
			if (c.codepoint == '\t') {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("»···");
				set_colors(COLOR_FG, COLOR_BG);
			} else if (c.codepoint < 32) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("<%02x>", c.codepoint);
				set_colors(COLOR_FG, COLOR_BG);
			} else {
				char tmp[8];
				to_eight(c.codepoint, tmp);
				printf("%s", tmp);
			}
			j += c.display_width;
			i++;
		} else if (j + c.display_width == offset + 1) {
			set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
			printf("<");
			set_colors(COLOR_FG, COLOR_BG);
			j += c.display_width;
			i++;
		} else if (c.display_width > 1) {
			remainder = c.display_width - 1;
			j++;
		} else {
			j++;
			i++;
		}
	}
}

void redraw_line(int j, int x) {
	hide_cursor();
	place_cursor(1,2 + j);
	/* draw line number */
	set_colors(COLOR_NUMBER_FG, COLOR_ALT_FG);
	printf(" ");
	set_colors(COLOR_NUMBER_FG, COLOR_NUMBER_BG);
	int num_size = log_base_10(env->line_count) + 2;
	for (int y = 0; y < num_size - log_base_10(x + 1); ++y) {
		printf(" ");
	}
	printf("%d%c", x + 1, (x+1 == env->line_no && env->coffset > 0) ? '<' : ' ');
	set_colors(COLOR_FG, COLOR_BG);
	render_line(env->lines[x], term_width - 3 - num_size, (x + 1 == env->line_no) ? env->coffset : 0);
	clear_to_end();
}

void redraw_text() {
	hide_cursor();
	int l = term_height - env->bottom_size - 1;
	int j = 0;

	for (int x = env->offset; j < l && x < env->line_count; x++) {
		redraw_line(j,x);
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
	hide_cursor();
	place_cursor(1, term_height - 1);
	set_colors(COLOR_FG, COLOR_STATUS_BG);
	if (env->file_name) {
		printf("%s", env->file_name);
	} else {
		printf("[No Name]");
	}
	if (env->modified) {
		printf(" [+]");
	}
	clear_to_end();
	char right_hand[1024];
	sprintf(right_hand, "Line %d/%d Col: %d ", env->line_no, env->line_count, env->col_no);
	place_cursor_h(term_width - strlen(right_hand));
	printf("%s",right_hand);
	fflush(stdout);
}

void redraw_commandline() {
	hide_cursor();
	place_cursor(1, term_height);
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
	getcwd(cwd, 1024);
	printf("\033]1;%s%s (%s) - BIM\007", env->file_name, env->modified ? " +" : "", cwd);
}

void set_modified() {
	if (env->modified) return;

	update_title();
	env->modified = 1;
	redraw_tabbar();
	redraw_statusbar();
}

void render_error(char * message) {
	redraw_commandline();
	set_colors(COLOR_ERROR_FG, COLOR_ERROR_BG);
	printf("%s", message);
	fflush(stdout);
}

void show_cursor() {
	fprintf(stdout, "\033[?25h");
	fflush(stdout);
}

void place_cursor_actual() {
	int num_size = log_base_10(env->line_count) + 5;
	int x = num_size + 1 - env->coffset;
	for (int i = 0; i < env->col_no - 1; ++i) {
		char_t * c = &env->lines[env->line_no-1]->text[i];
		x += c->display_width;
	}
	int y = env->line_no - env->offset + 1;

	if (x > term_width - 1) {
		int diff = x - (term_width - 1);
		env->coffset += diff;
		x -= diff;
		redraw_text();
	}

	if (x < num_size + 1) {
		int diff = (num_size + 1) - x;
		env->coffset -= diff;
		x += diff;
		redraw_text();
	}

	place_cursor(x,y);
	csr_x_actual = x;
	csr_y_actual = y;

	show_cursor();
}

void SIGWINCH_handler(int sig) {
	(void)sig;
	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);
	term_width = w.ws_col;
	term_height = w.ws_row;
	redraw_all();

	signal(SIGWINCH, SIGWINCH_handler);
}

void initialize() {
	setlocale(LC_ALL, "");

	term = getenv("TERM");

	buffers_avail = 4;
	buffers = malloc(sizeof(buffer_t *) * buffers_avail);

	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);
	term_width = w.ws_col;
	term_height = w.ws_row;
	set_unbuffered();

	signal(SIGWINCH, SIGWINCH_handler);
}

void goto_line(int line) {
	if (line < 1) line = 1;
	if (line > env->line_count) line = env->line_count;
	env->offset = line - 1;
	env->line_no = line;
	env->col_no  = 1;
	redraw_all();
}

void add_buffer(uint8_t * buf, int size) {
	for (int i = 0; i < size; ++i) {
		if (!decode(&state, &codepoint_r, buf[i])) {
			uint32_t c = codepoint_r;
			if (c == '\n') {
				env->lines = add_line(env->lines, env->line_no);
				env->col_no = 1;
				env->line_no += 1;
			} else {
				char_t _c;
				_c.codepoint = (uint16_t)c;
				_c.display_width = codepoint_width((uint16_t)c);
				line_t * line  = env->lines[env->line_no - 1];
				line_t * nline = line_insert(line, _c, env->col_no - 1);
				if (line != nline) {
					env->lines[env->line_no - 1] = nline;
				}
				env->col_no += 1;
			}
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}
	if (env->lines[env->line_no-1]->actual == 0) {
		/* Remove blank line from end */
		remove_line(env->lines, env->line_no-1);
	}
}


void open_file(char * file) {
	env = buffer_new();

	env->file_name = malloc(strlen(file) + 1);
	memcpy(env->file_name, file, strlen(file) + 1);

	setup_buffer(env);

	FILE * f = fopen(file, "r");

	if (!f) {
		char buf[1024];
		sprintf(buf, "Could not open %s", file);
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

void try_quit() {
	for (int i = 0; i < buffers_len; i++ ) {
		buffer_t * _env = buffers[i];
		if (_env->modified) {
			char msg[100];
			sprintf(msg, "Modifications made to file `%s` in tab %d. Aborting.", _env->file_name, i+1);
			render_error(msg);
			return;
		}
	}
	quit();
}

void previous_tab() {
	buffer_t * last = NULL;
	for (int i = 0; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];
		if (_env == env) {
			if (last) {
				env = last;
				redraw_all();
				return;
			} else {
				env = buffers[buffers_len-1];
				redraw_all();
				return;
			}
		}
		last = _env;
	}
}

void next_tab() {
	for (int i = 0; i < buffers_len; i++) {
		buffer_t * _env = buffers[i];
		if (_env == env) {
			if (i != buffers_len - 1) {
				env = buffers[i+1];
				redraw_all();
				return;
			} else {
				env = buffers[0];
				redraw_all();
				return;
			}
		}
	}
}

int isnumeric(char * str) {
	char *p = str;
	while (*p) {
		if (*p < '0' || *p > '9') return 0;
		++p;
	}
	return 1;
}

void write_file(char * file) {
	if (!file) {
		render_error("Need a file to write to.");
		return;
	}

	FILE * f = fopen(file, "w+");

	if (!f) {
		render_error("Failed to open file for writing.");
		return;
	}

	int i, j;
	for (i = 0; i < env->line_count; ++i) {
		line_t * line = env->lines[i];
		for (j = 0; j < line->actual; j++) {
			char_t c = line->text[j];
			if (c.codepoint == 0) {
				char buf[1] = {0};
				fwrite(buf, 1, 1, f);
			} else {
				char tmp[4];
				int i = to_eight(c.codepoint, tmp);
				fwrite(tmp, i, 1, f);
			}
		}
		fputc('\n', f);
	}
	fclose(f);

	env->modified = 0;
	if (!env->file_name) {
		env->file_name = malloc(strlen(file) + 1);
		memcpy(env->file_name, file, strlen(file) + 1);
	}

	redraw_all();
}

void close_buffer() {
	buffer_t * previous_env = env;
	buffer_t * new_env = buffer_close(env);
	if (new_env == previous_env) {
		render_error("lolwat");
	}
	if (!new_env) {
		quit();
	}
	free(previous_env);
	env = new_env;
	redraw_all();
}

void cursor_down(void) {
	if (env->line_no < env->line_count) {
		env->line_no += 1;
		if (env->col_no > env->lines[env->line_no-1]->actual) {
			env->col_no = env->lines[env->line_no-1]->actual;
		}
		int redraw = 0;
		if (env->coffset != 0) {
			env->coffset = 0;
			redraw = 1;
		}
		if (env->col_no == 0) env->col_no = 1;
		if (env->line_no > env->offset + term_height - env->bottom_size - 1) {
			env->offset += 1;
			redraw = 1;
		}
		if (redraw) {
			redraw_text();
		}
		redraw_statusbar();
		place_cursor_actual();
	}
}

void cursor_up(void) {
	if (env->line_no > 1) {
		env->line_no -= 1;
		if (env->col_no > env->lines[env->line_no-1]->actual) {
			env->col_no = env->lines[env->line_no-1]->actual;
		}
		int redraw = 0;
		if (env->coffset != 0) {
			env->coffset = 0;
			redraw = 1;
		}
		if (env->col_no == 0) env->col_no = 1;
		if (env->line_no <= env->offset) {
			env->offset -= 1;
			redraw = 1;
		}
		if (redraw) {
			redraw_text();
		}
		redraw_statusbar();
		place_cursor_actual();
	}
}

void cursor_left(void) {
	if (env->col_no > 1) {
		env->col_no -= 1;
		redraw_statusbar();
		place_cursor_actual();
	}
}

void cursor_home(void) {
	env->col_no = 1;
	redraw_statusbar();
	place_cursor_actual();
}

void cursor_end(int insert_mode) {
	env->col_no = env->lines[env->line_no-1]->actual+!!insert_mode;
	redraw_statusbar();
	place_cursor_actual();
}

void cursor_right(int insert_mode) {
	if (env->col_no < env->lines[env->line_no-1]->actual + !!insert_mode) {
		env->col_no += 1;
		redraw_statusbar();
		place_cursor_actual();
	}
}

void break_input(void) {
	if (env->col_no > env->lines[env->line_no-1]->actual) {
		env->col_no = env->lines[env->line_no-1]->actual;
	}
	if (env->col_no == 0) env->col_no = 1;
	redraw_commandline();
}

void process_command(char * cmd) {
	/* special case ! */
	if (*cmd == '!') {
		printf("\033[0m\n\n");
		set_buffered();
		system(&cmd[1]);
		printf("\n\nPress ENTER to continue.");
		fflush(stdout);
		set_unbuffered();
		while (bim_getch() != ENTER_KEY);
		redraw_all();
		return;
	}

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
	} else if (!strcmp(argv[0], "w")) {
		if (argc > 1) {
			write_file(argv[1]);
		} else {
			write_file(env->file_name);
		}
	} else if (!strcmp(argv[0], "wq")) {
		write_file(env->file_name);
		close_buffer();
	} else if (!strcmp(argv[0], "q")) {
		if (env->modified) {
			render_error("No write since last change. Use :q! to force exit.");
		} else {
			close_buffer();
		}
	} else if (!strcmp(argv[0], "qa")) {
		try_quit();
	} else if (!strcmp(argv[0], "qall")) {
		try_quit();
	} else if (!strcmp(argv[0], "qa!")) {
		quit();
	} else if (!strcmp(argv[0], "q!")) {
		quit();
	} else if (!strcmp(argv[0], "tabp")) {
		previous_tab();
	} else if (!strcmp(argv[0], "tabn")) {
		next_tab();
	} else if (isnumeric(argv[0])) {
		goto_line(atoi(argv[0]));
	} else {
		char buf[512];
		sprintf(buf, "Not an editor command: %s", argv[0]);
		render_error(buf);
	}
}

void command_mode() {
	char c;
	char buffer[1024] = {0};
	int  buffer_len = 0;

	redraw_commandline();
	printf(":");
	show_cursor();

	while ((c = bim_getch())) {
		if (c == -1) {
			continue;
		}
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
			} else {
				redraw_commandline();
				break;
			}
		} else {
			buffer[buffer_len] = c;
			buffer_len++;
			printf("%c", c);
		}
		show_cursor();
	}
}

void insert_mode() {
	int cin;
	uint32_t c;
	redraw_commandline();
	set_bold();
	printf("-- INSERT --");
	reset();
	place_cursor_actual();
	set_colors(COLOR_FG, COLOR_BG);
	int timeout = 0;
	int this_buf[20];
	while ((cin = bim_getch())) {
		if (cin == -1) {
			if (timeout && this_buf[timeout-1] == '\033') {
				break_input();
				return;
			}
			timeout = 0;
			continue;
		}
		if (!decode(&istate, &c, cin)) {
			switch (c) {
				case '\033':
					if (timeout == 0) {
						this_buf[timeout] = c;
						timeout++;
					} else if (timeout == 1 && this_buf[0] == '\033') {
						break_input();
						bim_unget(c);
						return;
					}
					break;
				case BACKSPACE_KEY:
					if (env->col_no > 1) {
						line_delete(env->lines[env->line_no - 1], env->col_no - 1);
						env->col_no -= 1;
						redraw_line(env->line_no - env->offset - 1, env->line_no-1);
						set_modified();
						redraw_statusbar();
						place_cursor_actual();
					} else if (env->line_no > 1) {
						int tmp = env->lines[env->line_no - 2]->actual;
						merge_lines(env->lines, env->line_no - 1);
						env->line_no -= 1;
						env->col_no = tmp+1;
						redraw_text();
						set_modified();
						redraw_statusbar();
						place_cursor_actual();
					}
					break;
				case ENTER_KEY:
					if (env->col_no == env->lines[env->line_no - 1]->actual + 1) {
						env->lines = add_line(env->lines, env->line_no);
					} else {
						/* oh oh god we're all gonna die */
						env->lines = split_line(env->lines, env->line_no, env->col_no - 1);
					}
					env->col_no = 1;
					env->line_no += 1;
					if (env->line_no > env->offset + term_height - env->bottom_size - 1) {
						env->offset += 1;
					}
					redraw_text();
					set_modified();
					redraw_statusbar();
					place_cursor_actual();
					break;
				default:
					{
						if (timeout == 1 && c == '[' && this_buf[0] == '\033') {
							this_buf[1] = c;
							timeout++;
							continue;
						} else if (timeout == 1 && c  != '[') {
							break_input();
							bim_unget(c);
							return;
						}
						if (timeout == 2 && this_buf[0] == '\033' && this_buf[1] == '[') {
							switch (c) {
								case 'A': // up
									cursor_up();
									break;
								case 'B': // down
									cursor_down();
									break;
								case 'C': // right
									cursor_right(1);
									break;
								case 'D': // left
									cursor_left();
									break;
								case 'H': // home
									cursor_home();
									break;
								case 'F': // end
									cursor_end(1);
									break;
							}
							timeout = 0;
							continue;
						}
						timeout = 0;
						char_t _c;
						_c.codepoint = c;
						_c.display_width = codepoint_width(c);
						line_t * line  = env->lines[env->line_no - 1];
						line_t * nline = line_insert(line, _c, env->col_no - 1);
						if (line != nline) {
							env->lines[env->line_no - 1] = nline;
						}
						redraw_line(env->line_no - env->offset - 1, env->line_no-1);
						env->col_no += 1;
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
	} else {
		env = buffer_new();
		update_title();
		setup_buffer(env);
	}

	redraw_all();

	while (1) {
		place_cursor_actual();
		char c;
		int timeout = 0;
		int this_buf[20];
		while ((c = bim_getch())) {
			switch (c) {
				case '\033':
					if (timeout == 0) {
						this_buf[timeout] = c;
						timeout++;
					}
					redraw_all();
					break;
				case ':':
					/* Switch to command mode */
					command_mode();
					break;
				case 'j':
					cursor_down();
					break;
				case 'k':
					cursor_up();
					break;
				case 'h':
					cursor_left();
					break;
				case 'l':
					cursor_right(0);
					break;
				case 'd':
					remove_line(env->lines, env->line_no-1);
					env->col_no = 1;
					if (env->line_no > env->line_count) {
						env->line_no--;
					}
					redraw_text();
					set_modified();
					place_cursor_actual();
					break;
				case ' ':
					goto_line(env->line_no + term_height - 6);
					break;
				case 'O':
					{
						env->lines = add_line(env->lines, env->line_no-1);
						env->col_no = 1;
						redraw_text();
						set_modified();
						place_cursor_actual();
						goto _insert;
					}
				case 'o':
					{
						env->lines = add_line(env->lines, env->line_no);
						env->col_no = 1;
						env->line_no += 1;
						if (env->line_no > env->offset + term_height - env->bottom_size - 1) {
							env->offset += 1;
						}
						redraw_text();
						set_modified();
						place_cursor_actual();
						goto _insert;
					}
				case 'a':
					if (env->col_no < env->lines[env->line_no-1]->actual + 1) {
						env->col_no += 1;
					}
					goto _insert;
				case '$':
					env->col_no = env->lines[env->line_no-1]->actual+1;
					break;
				case '0':
					env->col_no = 1;
					break;
				case 'i':
_insert:
					insert_mode();
					timeout = 0;
					break;
				default:
					if (timeout == 1 && c == '[' && this_buf[0] == '\033') {
						this_buf[1] = c;
						timeout++;
						continue;
					}
					if (timeout == 2 && this_buf[0] == '\033' && this_buf[1] == '[') {
						switch (c) {
							case 'A': // up
								cursor_up();
								break;
							case 'B': // down
								cursor_down();
								break;
							case 'C': // right
								cursor_right(0);
								break;
							case 'D': // left
								cursor_left();
								break;
							case 'H': // home
								cursor_home();
								break;
							case 'F': // end
								cursor_end(0);
								break;
						}
						timeout = 0;
						continue;
					}
					timeout = 0;
					break;
			}
			place_cursor_actual();
		}
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
