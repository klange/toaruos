/**
 * @brief Line editor
 *
 * Interactive line input editor with syntax highlighting for
 * a handful of languages. Based on an old version of Bim.
 * Used by the shell and Kuroko.
 *
 * This library is generally usable on Linux and even Windows.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2022 K. Lange
 */
#define _XOPEN_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <locale.h>
#include <signal.h>
#ifndef _WIN32
#include <termios.h>
#include <sys/ioctl.h>
#else
#include <windows.h>
#include <io.h>
#include "wcwidth._h"
#endif
#ifdef __toaru__
#include <toaru/rline.h>
#else
#include "rline.h"
#endif

static __attribute__((used)) int _isdigit(int c) { if (c > 128) return 0; return isdigit(c); }
static __attribute__((used)) int _isxdigit(int c) { if (c > 128) return 0; return isxdigit(c); }

#undef isdigit
#undef isxdigit
#define isdigit(c) _isdigit(c)
#define isxdigit(c) _isxdigit(c)

char * rline_history[RLINE_HISTORY_ENTRIES];
int rline_history_count  = 0;
int rline_history_offset = 0;
int rline_scroll = 0;
char * rline_exit_string = "exit\n";
int rline_terminal_width = 0;
char * rline_preload = NULL;

void rline_history_insert(char * str) {
	if (str[strlen(str)-1] == '\n') {
		str[strlen(str)-1] = '\0';
	}
	if (rline_history_count) {
		if (!strcmp(str, rline_history_prev(1))) {
			free(str);
			return;
		}
	}
	if (rline_history_count == RLINE_HISTORY_ENTRIES) {
		free(rline_history[rline_history_offset]);
		rline_history[rline_history_offset] = str;
		rline_history_offset = (rline_history_offset + 1) % RLINE_HISTORY_ENTRIES;
	} else {
		rline_history[rline_history_count] = str;
		rline_history_count++;
	}
}

void rline_history_append_line(char * str) {
	if (rline_history_count) {
		char ** s = &rline_history[(rline_history_count - 1 + rline_history_offset) % RLINE_HISTORY_ENTRIES];
		size_t len = strlen(*s) + strlen(str) + 2;
		char * c = malloc(len);
		snprintf(c, len, "%s\n%s", *s, str);
		if (c[strlen(c)-1] == '\n') {
			c[strlen(c)-1] = '\0';
		}
		free(*s);
		*s = c;
	} else {
		/* wat */
	}
}

char * rline_history_get(int item) {
	return rline_history[(item + rline_history_offset) % RLINE_HISTORY_ENTRIES];
}

char * rline_history_prev(int item) {
	return rline_history_get(rline_history_count - item);
}

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

/**
 * Conceptually similar to its predecessor, this implementation is much
 * less cool, as it uses three separate state tables and more shifts.
 */
static inline uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
	static int state_table[32] = {
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xxxxxxx */
		1,1,1,1,1,1,1,1,                 /* 10xxxxxx */
		2,2,2,2,                         /* 110xxxxx */
		3,3,                             /* 1110xxxx */
		4,                               /* 11110xxx */
		1                                /* 11111xxx */
	};

	static int mask_bytes[32] = {
		0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,
		0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x1F,0x1F,0x1F,0x1F,
		0x0F,0x0F,
		0x07,
		0x00
	};

	static int next[5] = {
		0,
		1,
		0,
		2,
		3
	};

	if (*state == UTF8_ACCEPT) {
		*codep = byte & mask_bytes[byte >> 3];
		*state = state_table[byte >> 3];
	} else if (*state > 0) {
		*codep = (byte & 0x3F) | (*codep << 6);
		*state = next[*state];
	}
	return *state;
}

#define ENTER_KEY     '\n'
#define BACKSPACE_KEY 0x08
#define DELETE_KEY    0x7F
#define MINIMUM_SIZE  10

/**
 * Same structures as in bim.
 * A single character has:
 * - A codepoint (Unicode) of up to 21 bits.
 * - Flags for syntax highlighting.
 * - A display width for rendering.
 */
typedef struct {
	uint32_t display_width:4;
	uint32_t flags:7;
	uint32_t codepoint:21;
} __attribute__((packed)) char_t;

/**
 * We generally only have the one line,
 * but this matches bim for compatibility reasons.
 */
typedef struct {
	int available;
	int actual;
	int istate;
	char_t   text[];
} line_t;

/**
 * We operate on a single line of text.
 * Maybe we can expand this in the future
 * for continuations of edits such as when
 * a quote is unclosed?
 */
static line_t * the_line = NULL;

/**
 * Line editor state
 */
static int loading = 0;
static int column = 0;
static int offset = 0;
static int width =  0;
static int show_right_side = 0;
static int show_left_side = 0;
static int prompt_width_calc = 0;
static int buf_size_max = 0;

/**
 * Prompt strings.
 * Defaults to just a "> " prompt with no right side.
 * Support for right side prompts is important
 * for the ToaruOS shell.
 */
static int prompt_width = 2;
static char * prompt = "> ";
static int prompt_right_width = 0;
static char * prompt_right = "";

int rline_exp_set_prompts(char * left, char * right, int left_width, int right_width) {
	prompt = left;
	prompt_right = right;
	prompt_width = left_width;
	prompt_right_width = right_width;
	return 0;
}

/**
 * Extra shell commands to highlight as keywords.
 * These are basically just copied from the
 * shell's tab completion database on startup.
 */
static char ** shell_commands = {0};
static int shell_commands_len = 0;

int rline_exp_set_shell_commands(char ** cmds, int len) {
	shell_commands = cmds;
	shell_commands_len = len;
	return 0;
}

/**
 * Tab completion callback.
 * Compatible with the original rline version.
 */
static rline_callback_t tab_complete_func = NULL;

int rline_exp_set_tab_complete_func(rline_callback_t func) {
	tab_complete_func = func;
	return 0;
}

static int have_unget = -1;
static int getch(int timeout) {
	if (have_unget >= 0) {
		int out = have_unget;
		have_unget = -1;
		return out;
	}
#ifndef _WIN32
	return fgetc(stdin);
#else
	static int bytesRead = 0;
	static char  buf8[8];
	static uint8_t * b;

	if (bytesRead) {
		bytesRead--;
		return *(b++);
	}

	DWORD dwRead;
	uint16_t buf16[8] = {0};
	if (ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE),buf16,2,&dwRead,NULL)) {
		int r = WideCharToMultiByte(CP_UTF8, 0, buf16, -1, buf8, 8, 0, 0);
		if (r > 1 && buf8[r-1] == '\0') r--;
		b = (uint8_t*)buf8;
		bytesRead = r - 1;
		return *(b++);
	} else {
		fprintf(stderr, "error on console read\n");
		return -1;
	}
#endif
}

/**
 * Convert from Unicode string to utf-8.
 */
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

/**
 * Obtain codepoint display width.
 *
 * This is copied from bim. Supports a few useful
 * things like rendering escapes as codepoints.
 */
static int codepoint_width(int codepoint) {
	if (codepoint == '\t') {
		return 1; /* Recalculate later */
	}
	if (codepoint < 32) {
		/* We render these as ^@ */
		return 2;
	}
	if (codepoint == 0x7F) {
		/* Renders as ^? */
		return 2;
	}
	if (codepoint > 0x7f && codepoint < 0xa0) {
		/* Upper control bytes <xx> */
		return 4;
	}
	if (codepoint == 0xa0) {
		/* Non-breaking space _ */
		return 1;
	}
	/* Skip wcwidth for anything under 256 */
	if (codepoint > 256) {
		/* Higher codepoints may be wider (eg. Japanese) */
		int out = wcwidth(codepoint);
		if (out >= 1) return out;
		/* Invalid character, render as [U+ABCD] or [U+ABCDEF] */
		return (codepoint < 0x10000) ? 8 : 10;
	}
	return 1;
}

static void recalculate_tabs(line_t * line) {
	int j = 0;
	for (int i = 0; i < line->actual; ++i) {
		if (line->text[i].codepoint == '\t') {
			line->text[i].display_width = 4 - (j % 4);
		}
		j += line->text[i].display_width;
	}
}


/**
 * Color themes have also been copied from bim.
 *
 * Slimmed down to only the ones we use for syntax
 * highlighting; the UI colors have been removed.
 */
static const char * COLOR_FG        = "@9";
static const char * COLOR_BG        = "@9";
static const char * COLOR_ALT_FG    = "@5";
static const char * COLOR_ALT_BG    = "@9";
static const char * COLOR_KEYWORD   = "@4";
static const char * COLOR_STRING    = "@2";
static const char * COLOR_COMMENT   = "@5";
static const char * COLOR_TYPE      = "@3";
static const char * COLOR_PRAGMA    = "@1";
static const char * COLOR_NUMERAL   = "@1";
static const char * COLOR_RED       = "@1";
static const char * COLOR_GREEN     = "@2";
static const char * COLOR_ESCAPE    = "@2";
static const char * COLOR_SEARCH_FG = "@0";
static const char * COLOR_SEARCH_BG = "@3";
static const char * COLOR_ERROR_FG  = "@9";
static const char * COLOR_ERROR_BG  = "@9";
static const char * COLOR_BOLD      = "@9";
static const char * COLOR_LINK      = "@9";

/**
 * Themes are selected from the $RLINE_THEME
 * environment variable.
 */
static void rline_exp_load_colorscheme_default(void) {
	COLOR_FG        = "@9";
	COLOR_BG        = "@9";
	COLOR_ALT_FG    = "@10";
	COLOR_ALT_BG    = "@9";
	COLOR_KEYWORD   = "@14";
	COLOR_STRING    = "@2";
	COLOR_COMMENT   = "@10";
	COLOR_TYPE      = "@3";
	COLOR_PRAGMA    = "@1";
	COLOR_NUMERAL   = "@1";
	COLOR_RED       = "@1";
	COLOR_GREEN     = "@2";
	COLOR_ESCAPE    = "@12";
	COLOR_SEARCH_FG = "@0";
	COLOR_SEARCH_BG = "@13";
	COLOR_ERROR_FG  = "@17";
	COLOR_ERROR_BG  = "@1";
	COLOR_BOLD      = "@9";
	COLOR_LINK      = "@14";
}

static void rline_exp_load_colorscheme_sunsmoke(void) {
	COLOR_FG        = "2;230;230;230";
	COLOR_BG        = "@9";
	COLOR_ALT_FG    = "2;122;122;122";
	COLOR_ALT_BG    = "2;46;43;46";
	COLOR_KEYWORD   = "2;51;162;230";
	COLOR_STRING    = "2;72;176;72";
	COLOR_COMMENT   = "2;158;153;129;3";
	COLOR_TYPE      = "2;230;206;110";
	COLOR_PRAGMA    = "2;194;70;54";
	COLOR_NUMERAL   = "2;230;43;127";
	COLOR_RED       = "2;222;53;53";
	COLOR_GREEN     = "2;55;167;0";
	COLOR_ESCAPE    = "2;113;203;173";
	COLOR_SEARCH_FG = "5;234";
	COLOR_SEARCH_BG = "5;226";
	COLOR_ERROR_FG  = "5;15";
	COLOR_ERROR_BG  = "5;196";
	COLOR_BOLD      = "2;230;230;230;1";
	COLOR_LINK      = "2;51;162;230;4";
}

/**
 * Syntax highlighting flags.
 */
#define FLAG_NONE      0
#define FLAG_KEYWORD   1
#define FLAG_STRING    2
#define FLAG_COMMENT   3
#define FLAG_TYPE      4
#define FLAG_PRAGMA    5
#define FLAG_NUMERAL   6
#define FLAG_ERROR     7
#define FLAG_DIFFPLUS  8
#define FLAG_DIFFMINUS 9
#define FLAG_NOTICE    10
#define FLAG_BOLD      11
#define FLAG_LINK      12
#define FLAG_ESCAPE    13

#define FLAG_SELECT    (1 << 5)

struct syntax_state {
	line_t * line;
	int line_no;
	int state;
	int i;
};

#define paint(length, flag) do { for (int i = 0; i < (length) && state->i < state->line->actual; i++, state->i++) { state->line->text[state->i].flags = (flag); } } while (0)
#define charat() (state->i < state->line->actual ? state->line->text[(state->i)].codepoint : -1)
#define nextchar() (state->i + 1 < state->line->actual ? state->line->text[(state->i+1)].codepoint : -1)
#define lastchar() (state->i - 1 >= 0 ? state->line->text[(state->i-1)].codepoint : -1)
#define skip() (state->i++)
#define charrel(x) (state->i + (x) < state->line->actual ? state->line->text[(state->i+(x))].codepoint : -1)

/**
 * Match and paint a single keyword. Returns 1 if the keyword was matched and 0 otherwise,
 * so it can be used for prefix checking for things that need further special handling.
 */
static int match_and_paint(struct syntax_state * state, const char * keyword, int flag, int (*keyword_qualifier)(int c)) {
	if (keyword_qualifier(lastchar())) return 0;
	if (!keyword_qualifier(charat())) return 0;
	int i = state->i;
	int slen = 0;
	while (i < state->line->actual || *keyword == '\0') {
		if (*keyword == '\0' && (i >= state->line->actual || !keyword_qualifier(state->line->text[i].codepoint))) {
			for (int j = 0; j < slen; ++j) {
				paint(1, flag);
			}
			return 1;
		}
		if (*keyword != state->line->text[i].codepoint) return 0;

		i++;
		keyword++;
		slen++;
	}
	return 0;
}

/**
 * Find keywords from a list and paint them, assuming they aren't in the middle of other words.
 * Returns 1 if a keyword from the last was found, otherwise 0.
 */
static int find_keywords(struct syntax_state * state, char ** keywords, int flag, int (*keyword_qualifier)(int c)) {
	if (keyword_qualifier(lastchar())) return 0;
	if (!keyword_qualifier(charat())) return 0;
	for (char ** keyword = keywords; *keyword; ++keyword) {
		int d = 0;
		while (state->i + d < state->line->actual && state->line->text[state->i+d].codepoint == (*keyword)[d]) d++;
		if ((*keyword)[d] == '\0' && (state->i + d >= state->line->actual || !keyword_qualifier(state->line->text[state->i+d].codepoint))) {
			paint((int)strlen(*keyword), flag);
			return 1;
		}
	}

	return 0;
}

/**
 * This is a basic character matcher for "keyword" characters.
 */
static int simple_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}


static int common_comment_buzzwords(struct syntax_state * state) {
	if (match_and_paint(state, "TODO", FLAG_NOTICE, simple_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "XXX", FLAG_NOTICE, simple_keyword_qualifier)) { return 1; }
	else if (match_and_paint(state, "FIXME", FLAG_ERROR, simple_keyword_qualifier)) { return 1; }
	return 0;
}

/**
 * Paint a comment until end of line, assumes this comment can not continue.
 * (Some languages have comments that can continue with a \ - don't use this!)
 * Assumes you've already painted your comment start characters.
 */
static int paint_comment(struct syntax_state * state) {
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else { paint(1, FLAG_COMMENT); }
	}
	return -1;
}

static int c_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}

static void paintNHex(struct syntax_state * state, int n) {
	paint(2, FLAG_ESCAPE);
	/* Why is my FLAG_ERROR not valid in rline? */
	for (int i = 0; i < n; ++i) {
		paint(1, isxdigit(charat()) ? FLAG_ESCAPE : FLAG_DIFFMINUS);
	}
}

static char * syn_krk_keywords[] = {
	"and","class","def","else","for","if","in","import","del",
	"let","not","or","return","while","try","except","raise",
	"continue","break","as","from","elif","lambda","with","is",
	"pass","assert","yield","finally","async","await",
	NULL
};

static char * syn_krk_types[] = {
	/* built-in functions */
	"self", "super", /* implicit in a class method */
	"len", "str", "int", "float", "dir", "repr", /* global functions from __builtins__ */
	"list","dict","range", /* builtin classes */
	"object","exception","isinstance","type","tuple","reversed",
	"print","set","any","all","bool","ord","chr","hex","oct","filter",
	"sorted","bytes","getattr","sum","min","max","id","hash","map","bin",
	"enumerate","zip","setattr","property","staticmethod","classmethod",
	"issubclass","hasattr","delattr","NotImplemented","abs","slice","long",
	NULL
};

static char * syn_krk_special[] = {
	"True","False","None",
	/* Exception names */
	NULL
};

static char * syn_krk_exception[] = {
	"Exception", "TypeError", "ArgumentError", "IndexError", "KeyError",
	"AttributeError", "NameError", "ImportError", "IOError", "ValueError",
	"KeyboardInterrupt", "ZeroDivisionError", "NotImplementedError", "SyntaxError",
	"AssertionError", "BaseException", "OSError", "SystemError",
	NULL
};

static void paint_krk_string_shared(struct syntax_state * state, int type, int isFormat, int isTriple) {
	if (charat() == '\\') {
		if (nextchar() == 'x') {
			paintNHex(state, 2);
		} else if (nextchar() == 'u') {
			paintNHex(state, 4);
		} else if (nextchar() == 'U') {
			paintNHex(state, 8);
		} else if (nextchar() >= '0' && nextchar() <= '7') {
			paint(2, FLAG_ESCAPE);
			if (charat() >= '0' && charat() <= '7') {
				paint(1, FLAG_ESCAPE);
				if (charat() >= '0' && charat() <= '7') {
					paint(1, FLAG_ESCAPE);
				}
			}
		} else {
			paint(2, FLAG_ESCAPE);
		}
	} else if (isFormat && charat() == '{') {
		if (nextchar() == '{') {
			paint(2, FLAG_STRING);
			return;
		}
		paint(1, FLAG_ESCAPE);
		if (charat() == '}') {
			state->i--;
			paint(2, FLAG_ERROR); /* Can't do that. */
		} else {
			int x = 0;
			while (charat() != -1) {
				if (charat() == '{') {
					x++;
				} else if (charat() == '}') {
					if (x == 0) {
						paint(1, FLAG_ESCAPE);
						break;
					}
					x--;
				} else if (charat() == type && !isTriple) {
					while (charat() != -1) {
						paint(1, FLAG_ERROR);
					}
					return;
				} else if (find_keywords(state, syn_krk_keywords, FLAG_ESCAPE, c_keyword_qualifier)) {
					continue;
				} else if (lastchar() != '.' && find_keywords(state, syn_krk_types, FLAG_TYPE, c_keyword_qualifier)) {
					continue;
				} else if (find_keywords(state, syn_krk_exception, FLAG_PRAGMA, c_keyword_qualifier)) {
					continue;
				}
				paint(1, FLAG_NUMERAL);
			}
		}
	} else {
		paint(1, FLAG_STRING);
	}
}

static void paint_krk_string(struct syntax_state * state, int type, int isFormat) {
	/* Assumes you came in from a check of charat() == '"' */
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == type) {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == type) {
			paint(1, FLAG_STRING);
			return;
		} else {
			paint_krk_string_shared(state,type,isFormat,0);
		}
	}
}

static int paint_krk_numeral(struct syntax_state * state) {
	if (charat() == '0' && (nextchar() == 'x' || nextchar() == 'X')) {
		paint(2, FLAG_NUMERAL);
		while (isxdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && (nextchar() == 'o' || nextchar() == 'O')) {
		paint(2, FLAG_NUMERAL);
		while ((charat() >= '0' && charat() <= '7') || charat() == '_') paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && (nextchar() == 'b' || nextchar() == 'B')) {
		paint(2, FLAG_NUMERAL);
		while (charat() == '0' || charat() == '1' || charat() == '_') paint(1, FLAG_NUMERAL);
	} else {
		while (isdigit(charat()) || charat() == '_') paint(1, FLAG_NUMERAL);
		if (charat() == '.' && isdigit(nextchar())) {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		}
	}
	return 0;
}

static int paint_krk_triple_string(struct syntax_state * state, int type, int isFormat) {
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == type) {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == type) {
			paint(1, FLAG_STRING);
			if (charat() == type && nextchar() == type) {
				paint(2, FLAG_STRING);
				return 0;
			}
		} else {
			paint_krk_string_shared(state,type,isFormat,1);
		}
	}
	return (type == '"') ? 1 : 2; /* continues */
}

static int syn_krk_calculate(struct syntax_state * state) {
	switch (state->state) {
		case -1:
		case 0:
			if (charat() == '#') {
				paint_comment(state);
			} else if (charat() == '@') {
				paint(1, FLAG_TYPE);
				while (c_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
				return 0;
			} else if (charat() == '"' || charat() == '\'') {
				int isFormat = (lastchar() == 'f');
				if (nextchar() == charat() && charrel(2) == charat()) {
					int type = charat();
					paint(3, FLAG_STRING);
					return paint_krk_triple_string(state, type, isFormat);
				} else {
					paint_krk_string(state, charat(), isFormat);
				}
				return 0;
			} else if (find_keywords(state, syn_krk_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
				return 0;
			} else if (lastchar() != '.' && find_keywords(state, syn_krk_types, FLAG_TYPE, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_krk_special, FLAG_NUMERAL, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_krk_exception, FLAG_PRAGMA, c_keyword_qualifier)) {
				return 0;
			} else if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
				paint_krk_numeral(state);
				return 0;
			} else if (charat() != -1) {
				skip();
				return 0;
			}
			break;
		/* rline doesn't support multiline editing anyway */
		case 1:
			return paint_krk_triple_string(state, '"', 0);
		case 2:
			return paint_krk_triple_string(state, '\'', 0);
	}
	return -1;
}

static char * syn_krk_dbg_commands[] = {
	"s", "skip",
	"c", "continue",
	"q", "quit",
	"e", "enable",
	"d", "disable",
	"r", "remove",
	"bt", "backtrace",
	"break",
	"abort",
	"help",
	NULL,
};

static char * syn_krk_dbg_info_types[] = {
	"breakpoints",
	NULL,
};

static int syn_krk_dbg_calculate(struct syntax_state * state) {
	if (state->state < 1) {
		if (state->i == 0) {
			if (match_and_paint(state, "p", FLAG_KEYWORD, c_keyword_qualifier) ||
			    match_and_paint(state, "print", FLAG_KEYWORD, c_keyword_qualifier)) {
				while (1) {
					int result = syn_krk_calculate(state);
					if (result == 0) continue;
					if (result == -1) return -1;
					return result + 1;
				}
			} else if (match_and_paint(state,"info", FLAG_KEYWORD, c_keyword_qualifier) ||
			           match_and_paint(state,"i", FLAG_KEYWORD, c_keyword_qualifier)) {
				skip();
				find_keywords(state,syn_krk_dbg_info_types, FLAG_TYPE, c_keyword_qualifier);
				return -1;
			} else if (find_keywords(state, syn_krk_dbg_commands, FLAG_KEYWORD, c_keyword_qualifier)) {
				return 0;
			}
		}
		return -1;
	} else {
		state->state -= 1;
		return syn_krk_calculate(state) + 1;
	}
}

#ifdef __toaru__
static int esh_variable_qualifier(int c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_');
}

static int paint_esh_variable(struct syntax_state * state) {
	if (charat() == '{') {
		paint(1, FLAG_TYPE);
		while (charat() != '}' && charat() != -1) paint(1, FLAG_TYPE);
		if (charat() == '}') paint(1, FLAG_TYPE);
	} else {
		if (charat() == '?' || charat() == '$' || charat() == '#') {
			paint(1, FLAG_TYPE);
		} else {
			while (esh_variable_qualifier(charat())) paint(1, FLAG_TYPE);
		}
	}
	return 0;
}

static int paint_esh_string(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (last != '\\' && charat() == '"') {
			paint(1, FLAG_STRING);
			return 0;
		} else if (charat() == '$') {
			paint(1, FLAG_TYPE);
			paint_esh_variable(state);
			last = -1;
		} else if (charat() != -1) {
			last = charat();
			paint(1, FLAG_STRING);
		}
	}
	return 2;
}

static int paint_esh_single_string(struct syntax_state * state) {
	int last = -1;
	while (charat() != -1) {
		if (last != '\\' && charat() == '\'') {
			paint(1, FLAG_STRING);
			return 0;
		} else if (charat() != -1) {
			last = charat();
			paint(1, FLAG_STRING);
		}
	}
	return 1;
}

static int esh_keyword_qualifier(int c) {
	return (isalnum(c) || c == '?' || c == '_' || c == '-'); /* technically anything that isn't a space should qualify... */
}

static char * esh_keywords[] = {
	"cd","exit","export","help","history","if","empty?",
	"equals?","return","export-cmd","source","exec","not","while",
	"then","else","echo",
	NULL
};

static int syn_esh_calculate(struct syntax_state * state) {
	if (state->state == 1) {
		return paint_esh_single_string(state);
	} else if (state->state == 2) {
		return paint_esh_string(state);
	}
	if (charat() == '#') {
		while (charat() != -1) {
			if (common_comment_buzzwords(state)) continue;
			else paint(1, FLAG_COMMENT);
		}
		return -1;
	} else if (charat() == '$') {
		paint(1, FLAG_TYPE);
		paint_esh_variable(state);
		return 0;
	} else if (charat() == '\'') {
		paint(1, FLAG_STRING);
		return paint_esh_single_string(state);
	} else if (charat() == '"') {
		paint(1, FLAG_STRING);
		return paint_esh_string(state);
	} else if (match_and_paint(state, "export", FLAG_KEYWORD, esh_keyword_qualifier)) {
		while (charat() == ' ') skip();
		while (esh_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
		return 0;
	} else if (match_and_paint(state, "export-cmd", FLAG_KEYWORD, esh_keyword_qualifier)) {
		while (charat() == ' ') skip();
		while (esh_keyword_qualifier(charat())) paint(1, FLAG_TYPE);
		return 0;
	} else if (find_keywords(state, esh_keywords, FLAG_KEYWORD, esh_keyword_qualifier)) {
		return 0;
	} else if (find_keywords(state, shell_commands, FLAG_KEYWORD, esh_keyword_qualifier)) {
		return 0;
	} else if (isdigit(charat())) {
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		return 0;
	} else if (charat() != -1) {
		skip();
		return 0;
	}
	return -1;
}

static char * syn_py_keywords[] = {
	"class","def","return","del","if","else","elif","for","while","continue",
	"break","assert","as","and","or","except","finally","from","global",
	"import","in","is","lambda","with","nonlocal","not","pass","raise","try","yield",
	NULL
};

static char * syn_py_types[] = {
	/* built-in functions */
	"abs","all","any","ascii","bin","bool","breakpoint","bytes",
	"bytearray","callable","compile","complex","delattr","chr",
	"dict","dir","divmod","enumerate","eval","exec","filter","float",
	"format","frozenset","getattr","globals","hasattr","hash","help",
	"hex","id","input","int","isinstance","issubclass","iter","len",
	"list","locals","map","max","memoryview","min","next","object",
	"oct","open","ord","pow","print","property","range","repr","reverse",
	"round","set","setattr","slice","sorted","staticmethod","str","sum",
	"super","tuple","type","vars","zip",
	NULL
};

static char * syn_py_special[] = {
	"True","False","None",
	NULL
};

static int paint_py_triple_double(struct syntax_state * state) {
	while (charat() != -1) {
		if (charat() == '"') {
			paint(1, FLAG_STRING);
			if (charat() == '"' && nextchar() == '"') {
				paint(2, FLAG_STRING);
				return 0;
			}
		} else {
			paint(1, FLAG_STRING);
		}
	}
	return 1; /* continues */
}

static int paint_py_triple_single(struct syntax_state * state) {
	while (charat() != -1) {
		if (charat() == '\'') {
			paint(1, FLAG_STRING);
			if (charat() == '\'' && nextchar() == '\'') {
				paint(2, FLAG_STRING);
				return 0;
			}
		} else {
			paint(1, FLAG_STRING);
		}
	}
	return 2; /* continues */
}

static int paint_py_single_string(struct syntax_state * state) {
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == '\'') {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == '\'') {
			paint(1, FLAG_STRING);
			return 0;
		} else if (charat() == '\\') {
			paint(2, FLAG_ESCAPE);
		} else {
			paint(1, FLAG_STRING);
		}
	}
	return 0;
}

static int paint_py_numeral(struct syntax_state * state) {
	if (charat() == '0' && (nextchar() == 'x' || nextchar() == 'X')) {
		paint(2, FLAG_NUMERAL);
		while (isxdigit(charat())) paint(1, FLAG_NUMERAL);
	} else if (charat() == '0' && nextchar() == '.') {
		paint(2, FLAG_NUMERAL);
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		if ((charat() == '+' || charat() == '-') && (nextchar() == 'e' || nextchar() == 'E')) {
			paint(2, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		} else if (charat() == 'e' || charat() == 'E') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		}
		if (charat() == 'j') paint(1, FLAG_NUMERAL);
		return 0;
	} else {
		while (isdigit(charat())) paint(1, FLAG_NUMERAL);
		if (charat() == '.') {
			paint(1, FLAG_NUMERAL);
			while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			if ((charat() == '+' || charat() == '-') && (nextchar() == 'e' || nextchar() == 'E')) {
				paint(2, FLAG_NUMERAL);
				while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			} else if (charat() == 'e' || charat() == 'E') {
				paint(1, FLAG_NUMERAL);
				while (isdigit(charat())) paint(1, FLAG_NUMERAL);
			}
			if (charat() == 'j') paint(1, FLAG_NUMERAL);
			return 0;
		}
		if (charat() == 'j') paint(1, FLAG_NUMERAL);
	}
	while (charat() == 'l' || charat() == 'L') paint(1, FLAG_NUMERAL);
	return 0;
}

static void paint_py_format_string(struct syntax_state * state, char type) {
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == type) {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == type) {
			paint(1, FLAG_STRING);
			return;
		} else if (charat() == '\\') {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == '{') {
			paint(1, FLAG_NUMERAL);
			if (charat() == '}') {
				state->i--;
				paint(2, FLAG_ERROR); /* Can't do that. */
			} else {
				while (charat() != -1 && charat() != '}') {
					paint(1, FLAG_NUMERAL);
				}
				paint(1, FLAG_NUMERAL);
			}
		} else {
			paint(1, FLAG_STRING);
		}
	}
}

static void paint_simple_string(struct syntax_state * state) {
	/* Assumes you came in from a check of charat() == '"' */
	paint(1, FLAG_STRING);
	while (charat() != -1) {
		if (charat() == '\\' && nextchar() == '"') {
			paint(2, FLAG_ESCAPE);
		} else if (charat() == '"') {
			paint(1, FLAG_STRING);
			return;
		} else if (charat() == '\\') {
			paint(2, FLAG_ESCAPE);
		} else {
			paint(1, FLAG_STRING);
		}
	}
}

static int syn_py_calculate(struct syntax_state * state) {
	switch (state->state) {
		case -1:
		case 0:
			if (charat() == '#') {
				paint_comment(state);
			} else if (state->i == 0 && match_and_paint(state, "import", FLAG_PRAGMA, c_keyword_qualifier)) {
				return 0;
			} else if (charat() == '@') {
				paint(1, FLAG_PRAGMA);
				while (c_keyword_qualifier(charat())) paint(1, FLAG_PRAGMA);
				return 0;
			} else if (charat() == '"') {
				if (nextchar() == '"' && charrel(2) == '"') {
					paint(3, FLAG_STRING);
					return paint_py_triple_double(state);
				} else if (lastchar() == 'f') {
					/* I don't like backtracking like this, but it makes this parse easier */
					state->i--;
					paint(1,FLAG_TYPE);
					paint_py_format_string(state,'"');
					return 0;
				} else {
					paint_simple_string(state);
					return 0;
				}
			} else if (find_keywords(state, syn_py_keywords, FLAG_KEYWORD, c_keyword_qualifier)) {
				return 0;
			} else if (lastchar() != '.' && find_keywords(state, syn_py_types, FLAG_TYPE, c_keyword_qualifier)) {
				return 0;
			} else if (find_keywords(state, syn_py_special, FLAG_NUMERAL, c_keyword_qualifier)) {
				return 0;
			} else if (charat() == '\'') {
				if (nextchar() == '\'' && charrel(2) == '\'') {
					paint(3, FLAG_STRING);
					return paint_py_triple_single(state);
				} else if (lastchar() == 'f') {
					/* I don't like backtracking like this, but it makes this parse easier */
					state->i--;
					paint(1,FLAG_TYPE);
					paint_py_format_string(state,'\'');
					return 0;
				} else {
					return paint_py_single_string(state);
				}
			} else if (!c_keyword_qualifier(lastchar()) && isdigit(charat())) {
				paint_py_numeral(state);
				return 0;
			} else if (charat() != -1) {
				skip();
				return 0;
			}
			break;
		case 1: /* multiline """ string */
			return paint_py_triple_double(state);
		case 2: /* multiline ''' string */
			return paint_py_triple_single(state);
	}
	return -1;
}

void * rline_exp_for_python(void * _stdin, void * _stdout, char * prompt) {

	rline_exp_set_prompts(prompt, "", strlen(prompt), 0);

	char * buf = malloc(1024);
	memset(buf, 0, 1024);

	rline_exp_set_syntax("python");
	rline_exit_string = "";
	rline(buf, 1024);
	rline_history_insert(strdup(buf));
	rline_scroll = 0;

	return buf;
}
#endif

void rline_redraw(rline_context_t * context) {
	if (context->quiet) return;
	printf("\033[u%s\033[K", context->buffer);
	for (int i = context->offset; i < context->collected; ++i) {
		printf("\033[D");
	}
	fflush(stdout);
}


/**
 * Convert syntax hilighting flag to color code
 */
static const char * flag_to_color(int _flag) {
	int flag = _flag & 0xF;
	switch (flag) {
		case FLAG_KEYWORD:
			return COLOR_KEYWORD;
		case FLAG_STRING:
			return COLOR_STRING;
		case FLAG_COMMENT:
			return COLOR_COMMENT;
		case FLAG_TYPE:
			return COLOR_TYPE;
		case FLAG_NUMERAL:
			return COLOR_NUMERAL;
		case FLAG_PRAGMA:
			return COLOR_PRAGMA;
		case FLAG_DIFFPLUS:
			return COLOR_GREEN;
		case FLAG_DIFFMINUS:
			return COLOR_RED;
		case FLAG_BOLD:
			return COLOR_BOLD;
		case FLAG_LINK:
			return COLOR_LINK;
		case FLAG_ESCAPE:
			return COLOR_ESCAPE;
		default:
			return COLOR_FG;
	}
}

static struct syntax_definition {
	char * name;
	int (*calculate)(struct syntax_state *);
	int tabIndents;
} syntaxes[] = {
	{"krk",syn_krk_calculate, 1},
	{"krk-dbg",syn_krk_dbg_calculate, 1},
#ifdef __toaru__
	{"python",syn_py_calculate, 1},
	{"esh",syn_esh_calculate, 0},
#endif
	{NULL, NULL, 0},
};

static struct syntax_definition * syntax;

int rline_exp_set_syntax(char * name) {
	if (!name) {
		syntax = NULL;
		return 0;
	}
	for (struct syntax_definition * s = syntaxes; s->name; ++s) {
		if (!strcmp(name,s->name)) {
			syntax = s;
			return 0;
		}
	}
	return 1;
}

/**
 * Syntax highlighting
 * Slimmed down from the bim implementation a bit,
 * but generally compatible with the same definitions.
 *
 * Type highlighting has been removed as the sh highlighter
 * didn't use it. This should be made pluggable again, and
 * the bim syntax highlighters should probably be broken
 * out into dynamically-loaded libraries?
 */
static void recalculate_syntax(line_t * line) {
	/* Clear syntax for this line first */
	int line_no = 0;
	//int is_original = 1;
	while (1) {
		for (int i = 0; i < line->actual; ++i) {
			line->text[i].flags = 0;
		}

		if (!syntax) {
			return;
		}

		/* Start from the line's stored in initial state */
		struct syntax_state state;
		state.line = line;
		state.line_no = line_no;
		state.state = line->istate;
		state.i = 0;

		while (1) {
			state.state = syntax->calculate(&state);

			if (state.state != 0) {
				/* TODO: Figure out a way to make this work for multiline input */
#if 0
				if (line_no == -1) return;
				if (!is_original) {
					redraw_line(line_no);
				}
				if (line_no + 1 < env->line_count && env->lines[line_no+1]->istate != state.state) {
					line_no++;
					line = env->lines[line_no];
					line->istate = state.state;
					if (env->loading) return;
					is_original = 0;
					goto _next;
				}
#endif
				return;
			}
		}
//_next:
//		(void)0;
	}
}

/**
 * Set colors
 */
static void set_colors(const char * fg, const char * bg) {
	printf("\033[22;23;");
	if (*bg == '@') {
		int _bg = atoi(bg+1);
		if (_bg < 10) {
			printf("4%d;", _bg);
		} else {
			printf("10%d;", _bg-10);
		}
	} else {
		printf("48;%s;", bg);
	}
	if (*fg == '@') {
		int _fg = atoi(fg+1);
		if (_fg < 10) {
			printf("3%dm", _fg);
		} else {
			printf("9%dm", _fg-10);
		}
	} else {
		printf("38;%sm", fg);
	}
	fflush(stdout);
}

/**
 * Set just the foreground color
 *
 * (See set_colors above)
 */
static void set_fg_color(const char * fg) {
	printf("\033[22;23;");
	if (*fg == '@') {
		int _fg = atoi(fg+1);
		if (_fg < 10) {
			printf("3%dm", _fg);
		} else {
			printf("9%dm", _fg-10);
		}
	} else {
		printf("38;%sm", fg);
	}
	fflush(stdout);
}

void rline_set_colors(rline_style_t style) {
	switch (style) {
		case RLINE_STYLE_MAIN:
			set_colors(COLOR_FG, COLOR_BG);
			break;
		case RLINE_STYLE_ALT:
			set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
			break;
		case RLINE_STYLE_KEYWORD:
			set_fg_color(COLOR_KEYWORD);
			break;
		case RLINE_STYLE_STRING:
			set_fg_color(COLOR_STRING);
			break;
		case RLINE_STYLE_COMMENT:
			set_fg_color(COLOR_COMMENT);
			break;
		case RLINE_STYLE_TYPE:
			set_fg_color(COLOR_TYPE);
			break;
		case RLINE_STYLE_PRAGMA:
			set_fg_color(COLOR_PRAGMA);
			break;
		case RLINE_STYLE_NUMERAL:
			set_fg_color(COLOR_NUMERAL);
			break;
	}
}

/**
 * Mostly copied from bim, but with some minor
 * alterations and removal of selection support.
 */
static void render_line(void) {
	printf("\033[?25l");
	if (show_left_side) {
		printf("\033[0m\r%s", prompt);
	} else {
		printf("\033[0m\r$");
	}

	if (offset && prompt_width_calc) {
		set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
		printf("\b<");
	}

	int i = 0; /* Offset in char_t line data entries */
	int j = 0; /* Offset in terminal cells */

	const char * last_color = NULL;
	int was_searching = 0;

	/* Set default text colors */
	set_colors(COLOR_FG, COLOR_BG);

	/*
	 * When we are rendering in the middle of a wide character,
	 * we render -'s to fill the remaining amount of the 
	 * charater's width
	 */
	int remainder = 0;

	int is_spaces = 1;

	line_t * line = the_line;

	/* For each character in the line ... */
	while (i < line->actual) {

		/* If there is remaining text... */
		if (remainder) {

			/* If we should be drawing by now... */
			if (j >= offset) {
				/* Fill remainder with -'s */
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("-");
				set_colors(COLOR_FG, COLOR_BG);
			}

			/* One less remaining width cell to fill */
			remainder--;

			/* Terminal offset moves forward */
			j++;

			/*
			 * If this was the last remaining character, move to
			 * the next codepoint in the line
			 */
			if (remainder == 0) {
				i++;
			}

			continue;
		}

		/* Get the next character to draw */
		char_t c = line->text[i];
		if (c.codepoint != ' ') is_spaces = 0;

		/* If we should be drawing by now... */
		if (j >= offset) {

			/* If this character is going to fall off the edge of the screen... */
			if (j - offset + c.display_width >= width - prompt_width_calc) {
				/* We draw this with special colors so it isn't ambiguous */
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);

				/* If it's wide, draw ---> as needed */
				while (j - offset < width - prompt_width_calc - 1) {
					printf("-");
					j++;
				}

				/* End the line with a > to show it overflows */
				printf(">");
				set_colors(COLOR_FG, COLOR_BG);
				j++;
				break;
			}

			/* Syntax hilighting */
			const char * color = flag_to_color(c.flags);
			if (c.flags & FLAG_SELECT) {
				set_colors(color, COLOR_BG);
				fprintf(stdout,"\033[7m");
				was_searching = 1;
			} else if (c.flags == FLAG_NOTICE) {
				set_colors(COLOR_SEARCH_FG, COLOR_SEARCH_BG);
				was_searching = 1;
			} else if (c.flags == FLAG_ERROR) {
				set_colors(COLOR_ERROR_FG, COLOR_ERROR_BG);
				was_searching = 1; /* co-opting this should work... */
			} else if (was_searching) {
				fprintf(stdout,"\033[0m");
				set_colors(color, COLOR_BG);
				last_color = color;
			} else if (!last_color || strcmp(color, last_color)) {
				set_fg_color(color);
				last_color = color;
			}

			/* Render special characters */
			if (c.codepoint == '\t') {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("»");
				for (int i = 1; i < c.display_width; ++i) {
					printf("·");
				}
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint < 32) {
				/* Codepoints under 32 to get converted to ^@ escapes */
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("^%c", '@' + c.codepoint);
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint == 0x7f) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("^?");
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint > 0x7f && c.codepoint < 0xa0) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("<%2x>", c.codepoint);
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.codepoint == 0xa0) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("_");
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.display_width == 8) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("[U+%04x]", c.codepoint);
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else if (c.display_width == 10) {
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("[U+%06x]", c.codepoint);
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
#if 0
			} else if (c.codepoint == ' ' && i == line->actual - 1) {
				/* Special case: space at end of line */
				set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
				printf("·");
				set_colors(COLOR_FG, COLOR_BG);
#endif
			} else if (i > 0 && is_spaces && c.codepoint == ' ' && !(i % 4)) {
				set_colors(COLOR_ALT_FG, COLOR_BG); /* Normal background so this is more subtle */
				printf("▏");
				set_colors(last_color ? last_color : COLOR_FG, COLOR_BG);
			} else {
				/* Normal characters get output */
				char tmp[7]; /* Max six bytes, use 7 to ensure last is always nil */
				to_eight(c.codepoint, tmp);
				printf("%s", tmp);
			}

			/* Advance the terminal cell offset by the render width of this character */
			j += c.display_width;

			/* Advance to the next character */
			i++;
		} else if (c.display_width > 1) {
			/*
			 * If this is a wide character but we aren't ready to render yet,
			 * we may need to draw some filler text for the remainder of its
			 * width to ensure we don't jump around when horizontally scrolling
			 * past wide characters.
			 */
			remainder = c.display_width - 1;
			j++;
		} else {
			/* Regular character, not ready to draw, advance without doing anything */
			j++;
			i++;
		}
	}

	printf("\033[0m");
	set_colors(COLOR_FG, COLOR_BG);

	if (show_right_side && prompt_right_width) {
		/* Fill to end right hand side */
		for (; j < width + offset - prompt_width_calc; ++j) {
			printf(" ");
		}

		/* Print right hand side */
		printf("\033[0m%s", prompt_right);
	} else {
		printf("\033[0K");
	}
	fflush(stdout);
}

/**
 * Create a line_t
 */
static line_t * line_create(void) {
	line_t * line = malloc(sizeof(line_t) + sizeof(char_t) * 32);
	line->available = 32;
	line->actual    = 0;
	line->istate    = 0;
	return line;
}

/**
 * Insert a character into a line
 */
static line_t * line_insert(line_t * line, char_t c, int offset) {

	/* If there is not enough space... */
	if (line->actual == line->available) {
		/* Expand the line buffer */
		line->available *= 2;
		line = realloc(line, sizeof(line_t) + sizeof(char_t) * line->available);
	}

	/* If this was not the last character, then shift remaining characters forward. */
	if (offset < line->actual) {
		memmove(&line->text[offset+1], &line->text[offset], sizeof(char_t) * (line->actual - offset));
	}

	/* Insert the new character */
	line->text[offset] = c;

	/* There is one new character in the line */
	line->actual += 1;

	if (!loading) {
		recalculate_tabs(line);
		recalculate_syntax(line);
	}

	return line;
}

/**
 * Update terminal size
 *
 * We don't listen for sigwinch for various reasons...
 */
static void get_size(void) {
#ifndef _WIN32
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	rline_terminal_width = w.ws_col;
#else
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	rline_terminal_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
#endif
	if (rline_terminal_width - prompt_right_width - prompt_width > MINIMUM_SIZE) {
		show_right_side = 1;
		show_left_side = 1;
		prompt_width_calc = prompt_width;
		width = rline_terminal_width - prompt_right_width;
	} else {
		show_right_side = 0;
		if (rline_terminal_width - prompt_width > MINIMUM_SIZE) {
			show_left_side = 1;
			prompt_width_calc = prompt_width;
		} else {
			show_left_side = 0;
			prompt_width_calc = 1;
		}
		width = rline_terminal_width;
	}
}

/**
 * Place the cursor within the line
 */
void rline_place_cursor(void) {
	int x = prompt_width_calc + 1 - offset;
	for (int i = 0; i < column; ++i) {
		char_t * c = &the_line->text[i];
		x += c->display_width;
	}

	if (x > width - 1) {
		/* Adjust the offset appropriately to scroll horizontally */
		int diff = x - (width - 1);
		offset += diff;
		x -= diff;
		render_line();
	}

	/* Same for scrolling horizontally to the left */
	if (x < prompt_width_calc + 1) {
		int diff = (prompt_width_calc + 1) - x;
		offset -= diff;
		x += diff;
		render_line();
	}

	printf("\033[?25h\033[%dG", x);
	fflush(stdout);
}

/**
 * Delete a character
 */
static void line_delete(line_t * line, int offset) {

	/* Can't delete character before start of line. */
	if (offset == 0) return;

	/* If this isn't the last character, we need to move all subsequent characters backwards */
	if (offset < line->actual) {
		memmove(&line->text[offset-1], &line->text[offset], sizeof(char_t) * (line->actual - offset));
	}

	/* The line is one character shorter */
	line->actual -= 1;

	if (!loading) {
		recalculate_tabs(line);
		recalculate_syntax(line);
	}
}

/**
 * Backspace from the cursor position
 */
static void delete_at_cursor(void) {
	if (column > 0) {
		line_delete(the_line, column);
		column--;
		if (offset > 0) offset--;
	}
}

static void smart_backspace(void) {
	if (column > 0) {
		int i;
		for (i = 0; i < column; ++i) {
			if (the_line->text[i].codepoint != ' ') break;
		}
		if (i == column) {
			delete_at_cursor();
			while (column > 0 && (column % 4)) delete_at_cursor();
			return;
		}
	}
	delete_at_cursor();
}

/**
 * Delete whole word
 */
static void delete_word(void) {
	if (!the_line->actual) return;
	if (!column) return;

	while (column > 0 && the_line->text[column-1].codepoint == ' ') {
		delete_at_cursor();
	}

	do {
		if (column > 0) {
			delete_at_cursor();
		}
	} while (column > 0 && the_line->text[column-1].codepoint != ' ');
}

/**
 * Insert at cursor position
 */
static void insert_char(uint32_t c) {
	char_t _c;
	_c.codepoint = c;
	_c.flags = 0;
	_c.display_width = codepoint_width(c);

	the_line = line_insert(the_line, _c, column);

	column++;
}

static char * paren_pairs = "()[]{}<>";

static int is_paren(int c) {
	char * p = paren_pairs;
	while (*p) {
		if (c == *p) return 1;
		p++;
	}
	return 0;
}

static void find_matching_paren(int * out_col, int in_col) {
	if (column - in_col > the_line->actual) {
		return; /* Invalid cursor position */
	}

	int paren_match = 0;
	int direction = 0;
	int start = the_line->text[column-in_col].codepoint;
	int flags = the_line->text[column-in_col].flags & 0x1F;
	int count = 0;

	/* TODO what about unicode parens? */
	for (int i = 0; paren_pairs[i]; ++i) {
		if (start == paren_pairs[i]) {
			direction = (i % 2 == 0) ? 1 : -1;
			paren_match = paren_pairs[(i % 2 == 0) ? (i+1) : (i-1)];
			break;
		}
	}

	if (!paren_match) return;

	/* Scan for match */
	int col = column - in_col;

	while (col > -1 && col < the_line->actual) {
		/* Only match on same syntax */
		if ((the_line->text[col].flags & 0x1F) == flags) {
			/* Count up on same direction */
			if (the_line->text[col].codepoint == start) count++;
			/* Count down on opposite direction */
			if (the_line->text[col].codepoint == paren_match) {
				count--;
				/* When count == 0 we have a match */
				if (count == 0) goto _match_found;
			}
		}
		col += direction;
	}

_match_found:
	*out_col = col;
}

static void redraw_matching_paren(int col) {
	for (int j = 0; j < the_line->actual; ++j) {
		if (j == col) {
			the_line->text[j].flags |= FLAG_SELECT;
		} else {
			the_line->text[j].flags &= ~(FLAG_SELECT);
		}
	}
}

static void highlight_matching_paren(void) {
	int col = -1;
	if (column < the_line->actual && is_paren(the_line->text[column].codepoint)) {
		find_matching_paren(&col, 0);
	} else if (column > 0 && is_paren(the_line->text[column-1].codepoint)) {
		find_matching_paren(&col, 1);
	}
	redraw_matching_paren(col);
}


/**
 * Move cursor left
 */
static void cursor_left(void) {
	if (column > 0) column--;
	rline_place_cursor();
}

/**
 * Move cursor right
 */
static void cursor_right(void) {
	if (column < the_line->actual) column++;
	rline_place_cursor();
}

/**
 * Move cursor one whole word left
 */
static void word_left(void) {
	if (column == 0) return;
	column--;
	while (column && the_line->text[column].codepoint == ' ') {
		column--;
	}
	while (column > 0) {
		if (the_line->text[column-1].codepoint == ' ') break;
		column--;
	}
	rline_place_cursor();
}

/**
 * Move cursor one whole word right
 */
static void word_right(void) {
	while (column < the_line->actual && the_line->text[column].codepoint == ' ') {
		column++;
	}
	while (column < the_line->actual) {
		column++;
		if (column < the_line->actual && the_line->text[column].codepoint == ' ') break;
	}
	rline_place_cursor();
}

/**
 * Move cursor to start of line
 */
static void cursor_home(void) {
	column = 0;
	rline_place_cursor();
}

/*
 * Move cursor to end of line
 */
static void cursor_end(void) {
	column = the_line->actual;
	rline_place_cursor();
}

/**
 * Temporary buffer for holding utf-8 data
 */
static char temp_buffer[1024];

/**
 * Cycle to previous history entry
 */
static void history_previous(void) {
	if (rline_scroll == 0) {
		/* Convert to temporaary buffer */
		unsigned int off = 0;
		memset(temp_buffer, 0, sizeof(temp_buffer));
		for (int j = 0; j < the_line->actual; j++) {
			char_t c = the_line->text[j];
			off += to_eight(c.codepoint, &temp_buffer[off]);
		}
	}

	if (rline_scroll < rline_history_count) {
		rline_scroll++;

		/* Copy in from history */
		the_line->actual = 0;
		column = 0;
		loading = 1;
		unsigned char * buf = (unsigned char *)rline_history_prev(rline_scroll);
		uint32_t istate = 0, c = 0;
		for (unsigned int i = 0; i < strlen((char *)buf); ++i) {
			if (!decode(&istate, &c, buf[i])) {
				insert_char(c);
			}
		}
		loading = 0;
	}
	/* Set cursor at end */
	column = the_line->actual;
	offset = 0;
	recalculate_tabs(the_line);
	recalculate_syntax(the_line);
	render_line();
	rline_place_cursor();
}

/**
 * Cycle to next history entry
 */
static void history_next(void) {
	if (rline_scroll >= 1) {
		unsigned char * buf;
		if (rline_scroll > 1) buf = (unsigned char *)rline_history_prev(rline_scroll-1);
		else buf = (unsigned char *)temp_buffer;
		rline_scroll--;

		/* Copy in from history */
		the_line->actual = 0;
		column = 0;
		loading = 1;
		uint32_t istate = 0, c = 0;
		for (unsigned int i = 0; i < strlen((char *)buf); ++i) {
			if (!decode(&istate, &c, buf[i])) {
				insert_char(c);
			}
		}
		loading = 0;
	}
	/* Set cursor at end */
	column = the_line->actual;
	offset = 0;
	recalculate_tabs(the_line);
	recalculate_syntax(the_line);
	render_line();
	rline_place_cursor();
}

/**
 * Handle escape sequences (arrow keys, etc.)
 */
static int handle_escape(int * this_buf, int * timeout, int c) {
	if (*timeout >=  1 && this_buf[*timeout-1] == '\033' && c == '\033') {
		this_buf[0]= c;
		*timeout = 1;
		return 1;
	}
	if (*timeout >= 1 && this_buf[*timeout-1] == '\033' && c == '[') {
		*timeout = 1;
		this_buf[*timeout] = c;
		(*timeout)++;
		return 0;
	}
	if (*timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[' &&
			(isdigit(c) || c == ';')) {
		this_buf[*timeout] = c;
		(*timeout)++;
		return 0;
	}
	if (*timeout >= 2 && this_buf[0] == '\033' && this_buf[1] == '[') {
		switch (c) {
			case 'A': // up
				history_previous();
				break;
			case 'B': // down
				history_next();
				break;
			case 'C': // right
				if (this_buf[*timeout-1] == '5') {
					word_right();
				} else {
					cursor_right();
				}
				break;
			case 'D': // left
				if (this_buf[*timeout-1] == '5') {
					word_left();
				} else {
					cursor_left();
				}
				break;
			case 'H': // home
				cursor_home();
				break;
			case 'F': // end
				cursor_end();
				break;
			case '~':
				switch (this_buf[*timeout-1]) {
					case '1':
						cursor_home();
						break;
					case '3':
						/* Delete forward */
						if (column < the_line->actual) {
							line_delete(the_line, column+1);
							if (offset > 0) offset--;
						}
						break;
					case '4':
						cursor_end();
						break;
				}
				break;
			default:
				break;
		}
		*timeout = 0;
		return 0;
	}

	*timeout = 0;
	return 0;
}

#ifndef _WIN32
static unsigned int _INTR, _EOF;
static struct termios old;
static void get_initial_termios(void) {
	tcgetattr(STDOUT_FILENO, &old);
	_INTR = old.c_cc[VINTR];
	_EOF  = old.c_cc[VEOF];
}
static void set_unbuffered(void) {
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO & ~ISIG);
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &new);
}

static void set_buffered(void) {
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &old);
}
#else
static unsigned int _INTR = 3;
static unsigned int _EOF  = 4;
static void get_initial_termios(void) {
}
static void set_unbuffered(void) {
	/* Disables line input, echo, ^C processing, and a few others. */
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_VIRTUAL_TERMINAL_INPUT);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_WRAP_AT_EOL_OUTPUT);
}
static void set_buffered(void) {
	/* These are the defaults */
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),
		ENABLE_ECHO_INPUT |
		ENABLE_INSERT_MODE |
		ENABLE_LINE_INPUT |
		ENABLE_MOUSE_INPUT |
		ENABLE_PROCESSED_INPUT |
		ENABLE_QUICK_EDIT_MODE |
		ENABLE_VIRTUAL_TERMINAL_INPUT
	);
}
#endif

static int tabbed;

static void dummy_redraw(rline_context_t * context) {
	/* Do nothing */
}

/**
 * Juggle our buffer with an rline context so we can
 * call original rline functions such as a tab-completion callback
 * or reverse search.
 */
static void call_rline_func(rline_callback_t func, rline_context_t * context) {
	/* Unicode parser state */
	uint32_t istate = 0;
	uint32_t c;

	/* Don't let rline draw things */
	context->quiet = 1;

	/* Allocate a temporary buffer */
	context->buffer = malloc(buf_size_max);
	memset(context->buffer,0,buf_size_max);

	/* Convert current data to utf-8 */
	unsigned int off = 0;
	for (int j = 0; j < the_line->actual; j++) {
		if (j == column) {
			/* Track cursor position */
			context->offset = off;
		}
		char_t c = the_line->text[j];
		off += to_eight(c.codepoint, &context->buffer[off]);
	}

	/* If the cursor was at the end, the loop above didn't catch it */
	if (column == the_line->actual) context->offset = off;

	/*
	 * Did we just press tab before this? This is actually managed
	 * by the tab-completion function.
	 */
	context->tabbed = tabbed;

	/* Empty callbacks */
	rline_callbacks_t tmp = {0};
	/*
	 * Because some clients expect this to be set...
	 * (we don't need it, we'll redraw ourselves later)
	 */
	tmp.redraw_prompt = dummy_redraw;

	/* Setup context */
	context->callbacks = &tmp;
	context->collected = off;
	context->buffer[off] = '\0';
	context->requested = 1024;

	/* Reset colors (for tab completion candidates, etc. */
	printf("\033[0m");

	/* Call the function */
	func(context);

	/* Now convert back */
	loading = 1;
	int final_column = 0;
	the_line->actual = 0;
	column = 0;
	istate = 0;
	for (int i = 0; i < context->collected; ++i) {
		if (i == context->offset) {
			final_column = column;
		}
		if (!decode(&istate, &c, ((unsigned char *)context->buffer)[i])) {
			insert_char(c);
		}
	}

	free(context->buffer);

	/* Position cursor */
	if (context->offset == context->collected) {
		column = the_line->actual;
	} else {
		column = final_column;
	}
	tabbed = context->tabbed;
	loading = 0;

	/* Recalculate + redraw */
	recalculate_tabs(the_line);
	recalculate_syntax(the_line);
	render_line();
	rline_place_cursor();
}

static int reverse_search(void) {
	/* Store state */
	char * old_prompt = prompt;
	int old_prompt_width = prompt_width;
	int old_prompt_width_calc = prompt_width_calc;
	line_t * old_line = the_line;

	char buffer[1024] = {0};
	unsigned int off = 0;

	the_line = NULL;

	prompt = "(r-search) ";
	prompt_width = strlen(prompt);
	prompt_width_calc = prompt_width;

	int cin, timeout = 0;
	uint32_t c = 0, istate = 0;

	int start_at = 0;
	int retval = 0;

	while (1) {
		_next: (void)0;

		off = 0;
		buffer[0] = '\0';
		for (int j = 0; j < old_line->actual; j++) {
			buffer[off] = '\0';
			char_t c = old_line->text[j];
			off += to_eight(c.codepoint, &buffer[off]);
		}

		if (the_line) free(the_line);
		the_line = line_create();

		int match_offset = 0;

		if (off) {
			for (int i = start_at; i < rline_history_count; ++i) {
				char * buf= rline_history_prev(i+1);
				char * match = strstr(buf, buffer);
				if (match) {
					match_offset = i;
					column = 0;
					loading = 1;
					uint32_t istate = 0, c = 0;
					int invert_start = 0;
					for (unsigned int i = 0; i < strlen((char *)buf); ++i) {
						if (match == &buf[i]) invert_start = the_line->actual;
						if (!decode(&istate, &c, buf[i])) {
							insert_char(c);
						}
					}
					loading = 0;
					offset = 0;
					recalculate_tabs(the_line);
					recalculate_syntax(the_line);
					for (int i = 0; i < old_line->actual; ++i) {
						the_line->text[invert_start+i].flags |= FLAG_SELECT;
					}
					column = invert_start;
					break;
				}
			}
		}

		render_line();

		if (the_line->actual == 0) {
			offset = 0;
			column = 0;
			rline_place_cursor();
			set_fg_color(COLOR_ALT_FG);
			printf("%s", buffer);
			fflush(stdout);
		}

		while ((cin = getch(timeout))) {
			if (cin == -1) continue;
			if (!decode(&istate, &c, cin)) {
				switch (c) {
					case '\033':
						have_unget = '\033';
						goto _done;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						line_delete(old_line, old_line->actual);
						goto _next;
					case 13:
					case ENTER_KEY:
						retval = 1;
						goto _done;
					case 18:
						start_at = match_offset + 1;
						goto _next;
					default: {
						char_t _c;
						_c.codepoint = c;
						_c.flags = 0;
						_c.display_width = codepoint_width(c);
						old_line = line_insert(old_line, _c, old_line->actual);
						goto _next;
					}
				}
			}
		}
	}

_done:
	free(old_line);
	prompt = old_prompt;
	prompt_width = old_prompt_width;
	prompt_width_calc = old_prompt_width_calc;
	offset = 0;
	render_line();
	rline_place_cursor();
	return retval;
}

/**
 * Perform actual interactive line editing.
 *
 * This is mostly a reimplementation of bim's
 * INSERT mode, but with some cleanups and fixes
 * to work on a single line and to add some new
 * key bindings we don't have in bim.
 */
static int read_line(void) {
	int cin;
	uint32_t c = 0;
	int timeout = 0;
	int this_buf[20];
	uint32_t istate = 0;

	/* Let's disable this under Windows... */
	set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
	fprintf(stdout, "◄\033[0m"); /* TODO: This could be retrieved from an envvar */
	for (int i = 0; i < rline_terminal_width - 1; ++i) {
		fprintf(stdout, " ");
	}

	if (rline_preload) {
		char * c = rline_preload;
		while (*c) {
			insert_char(*c);
			c++;
		}
		free(rline_preload);
		rline_preload = NULL;
	}

	render_line();
	rline_place_cursor();

	while ((cin = getch(timeout))) {
		if (cin == -1) continue;
		get_size();
		if (!decode(&istate, &c, cin)) {
			if (timeout == 0) {
				if (c != '\t') tabbed = 0;
				if (_INTR && c == _INTR) {
					set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
					printf("^%c", (int)('@' + c));
					printf("\033[0m");
					loading = 1;
					the_line->actual = 0;
					column = 0;
					insert_char('\n');
					raise(SIGINT);
					return 1;
				}
				if (_EOF && c == _EOF) {
					if (column == 0 && the_line->actual == 0) {
						for (char *_c = rline_exit_string; *_c; ++_c) {
							insert_char(*_c);
						}
						redraw_matching_paren(-1);
						render_line();
						rline_place_cursor();
						if (!*rline_exit_string) {
							set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
							printf("^D\033[0m");
						}
						return 1;
					} else { /* Otherwise act like delete */
						if (column < the_line->actual) {
							line_delete(the_line, column+1);
							if (offset > 0) offset--;
						}
						continue;
					}
				}
				switch (c) {
					case '\033':
						if (timeout == 0) {
							this_buf[timeout] = c;
							timeout++;
						}
						break;
					case DELETE_KEY:
					case BACKSPACE_KEY:
						smart_backspace();
						break;
					case 13:
					case ENTER_KEY:
						/* Finished */
						loading = 1;
						column = the_line->actual;
						redraw_matching_paren(-1);
						render_line();
						insert_char('\n');
						return 1;
					case 22: /* ^V */
						/* Don't bother with unicode, just take the next byte */
						rline_place_cursor();
						printf("^\b");
						insert_char(getc(stdin));
						break;
					case 23: /* ^W */
						delete_word();
						break;
					case 18: /* ^R - Begin reverse search */
						if (reverse_search()) {
							loading = 1;
							column = the_line->actual;
							recalculate_syntax(the_line);
							render_line();
							insert_char('\n');
							return 1;
						}
						break;
					case 12: /* ^L - Repaint the whole screen */
						printf("\033[2J\033[H");
						render_line();
						rline_place_cursor();
						break;
					case 11: /* ^K - Clear to end */
						the_line->actual = column;
						break;
					case 21: /* ^U - Kill to beginning */
						while (column) {
							delete_at_cursor();
						}
						break;
					case '\t':
						if ((syntax && syntax->tabIndents) && (column == 0 || the_line->text[column-1].codepoint == ' ')) {
							/* Insert tab character */
							insert_char(' ');
							insert_char(' ');
							insert_char(' ');
							insert_char(' ');
						} else if (tab_complete_func) {
							/* Tab complete */
							rline_context_t context = {0};
							call_rline_func(tab_complete_func, &context);
							continue;
						}
						break;
					default:
						insert_char(c);
						break;
				}
			} else {
				if (handle_escape(this_buf,&timeout,c)) {
					render_line();
					rline_place_cursor();
					continue;
				}
			}
			highlight_matching_paren();
			render_line();
			rline_place_cursor();
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}
	return 0;
}

/**
 * Read a line of text with interactive editing.
 */
int rline(char * buffer, int buf_size) {
#ifndef _WIN32
	setlocale(LC_ALL, "");
#else
	setlocale(LC_ALL, "C.UTF-8");
#endif
	get_initial_termios();
	set_unbuffered();
	get_size();

	column = 0;
	offset = 0;
	buf_size_max = buf_size;

	char * theme = getenv("RLINE_THEME");
	if (theme && !strcmp(theme,"sunsmoke")) { /* TODO bring back theme tables */
		rline_exp_load_colorscheme_sunsmoke();
	} else {
		rline_exp_load_colorscheme_default();
	}

	the_line = line_create();
	loading = 0;
	read_line();
	printf("\r\033[?25h\033[0m\n");

	unsigned int off = 0;
	for (int j = 0; j < the_line->actual; j++) {
		char_t c = the_line->text[j];
		off += to_eight(c.codepoint, &buffer[off]);
	}

	free(the_line);

	set_buffered();

	return strlen(buffer);
}

void rline_insert(rline_context_t * context, const char * what) {
	size_t insertion_length = strlen(what);

	if (context->collected + (int)insertion_length > context->requested) {
		insertion_length = context->requested - context->collected;
	}

	/* Move */
	memmove(&context->buffer[context->offset + insertion_length], &context->buffer[context->offset], context->collected - context->offset);
	memcpy(&context->buffer[context->offset], what, insertion_length);
	context->collected += insertion_length;
	context->offset += insertion_length;
}
