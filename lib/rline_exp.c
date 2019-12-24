/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * Experimental rline replacement with syntax highlighting, based
 * on bim's highlighting and line editing.
 *
 */
#define _XOPEN_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <termios.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>

#include <toaru/decodeutf8.h>
#include <toaru/rline.h>

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
	char_t   text[0];
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
static int full_width = 0;
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

static int _unget = -1;
static void _ungetc(int c) {
	_unget = c;
}

static int getch(int immediate) {
	if (_unget != -1) {
		int out = _unget;
		_unget = -1;
		return out;
	}
	if (immediate) {
		return getc(stdin);
	}
	struct pollfd fds[1];
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	int ret = poll(fds,1,10);
	if (ret > 0 && fds[0].revents & POLLIN) {
		unsigned char buf[1];
		read(STDIN_FILENO, buf, 1);
		return buf[0];
	} else {
		return -1;
	}
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
static int codepoint_width(wchar_t codepoint) {
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

void recalculate_tabs(line_t * line) {
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
#define FLAG_SEARCH    (1 << 6)

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
int match_and_paint(struct syntax_state * state, const char * keyword, int flag, int (*keyword_qualifier)(int c)) {
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
int find_keywords(struct syntax_state * state, char ** keywords, int flag, int (*keyword_qualifier)(int c)) {
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
int simple_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}


int common_comment_buzzwords(struct syntax_state * state) {
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
int paint_comment(struct syntax_state * state) {
	while (charat() != -1) {
		if (common_comment_buzzwords(state)) continue;
		else { paint(1, FLAG_COMMENT); }
	}
	return -1;
}

int c_keyword_qualifier(int c) {
	return isalnum(c) || (c == '_');
}

int esh_variable_qualifier(int c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_');
}

int paint_esh_variable(struct syntax_state * state) {
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

int paint_esh_string(struct syntax_state * state) {
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

int paint_esh_single_string(struct syntax_state * state) {
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

int esh_keyword_qualifier(int c) {
	return (isalnum(c) || c == '?' || c == '_' || c == '-'); /* technically anything that isn't a space should qualify... */
}

char * esh_keywords[] = {
	"cd","exit","export","help","history","if","empty?",
	"equals?","return","export-cmd","source","exec","not","while",
	"then","else","echo",
	NULL
};

int syn_esh_calculate(struct syntax_state * state) {
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

void paint_simple_string(struct syntax_state * state) {
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

char * syn_py_keywords[] = {
	"class","def","return","del","if","else","elif","for","while","continue",
	"break","assert","as","and","or","except","finally","from","global",
	"import","in","is","lambda","with","nonlocal","not","pass","raise","try","yield",
	NULL
};

char * syn_py_types[] = {
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

char * syn_py_special[] = {
	"True","False","None",
	NULL
};

int paint_py_triple_double(struct syntax_state * state) {
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

int paint_py_triple_single(struct syntax_state * state) {
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

int paint_py_single_string(struct syntax_state * state) {
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

int paint_py_numeral(struct syntax_state * state) {
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

void paint_py_format_string(struct syntax_state * state, char type) {
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

int syn_py_calculate(struct syntax_state * state) {
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
		case FLAG_SELECT:
			return COLOR_FG;
//		case FLAG_BOLD:
//			return COLOR_BOLD;
//		case FLAG_LINK:
//			return COLOR_LINK;
		case FLAG_ESCAPE:
			return COLOR_ESCAPE;
		default:
			return COLOR_FG;
	}
}

struct syntax_definition {
	char * name;
	int (*calculate)(struct syntax_state *);
} syntaxes[] = {
	{"esh",syn_esh_calculate},
	{"python",syn_py_calculate},
	{NULL, NULL},
};

static struct syntax_definition * syntax;

int rline_exp_set_syntax(char * name) {
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
			if ((c.flags & FLAG_SEARCH) || (c.flags == FLAG_NOTICE)) {
				set_colors(COLOR_SEARCH_FG, COLOR_SEARCH_BG);
				was_searching = 1;
			} else if (was_searching) {
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

	set_colors(COLOR_FG, COLOR_BG);

	/* Fill to end right hand side */
	for (; j < width + offset - prompt_width_calc; ++j) {
		printf(" ");
	}

	/* Print right hand side */
	if (show_right_side) {
		printf("\033[0m%s", prompt_right);
	}
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
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
	full_width = w.ws_col;
	if (full_width - prompt_right_width - prompt_width > MINIMUM_SIZE) {
		show_right_side = 1;
		show_left_side = 1;
		prompt_width_calc = prompt_width;
		width = full_width - prompt_right_width;
	} else {
		show_right_side = 0;
		if (full_width - prompt_width > MINIMUM_SIZE) {
			show_left_side = 1;
			prompt_width_calc = prompt_width;
		} else {
			show_left_side = 0;
			prompt_width_calc = 1;
		}
		width = full_width;
	}
}

/**
 * Place the cursor within the line
 */
static void place_cursor_actual(void) {
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

/**
 * Delete whole word
 */
static void delete_word(void) {
	if (!the_line->actual) return;
	if (!column) return;

	do {
		if (column > 0) {
			line_delete(the_line, column);
			column--;
			if (offset > 0) offset--;
		}
	} while (column && the_line->text[column-1].codepoint != ' ');
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

/**
 * Move cursor left
 */
static void cursor_left(void) {
	if (column > 0) column--;
	place_cursor_actual();
}

/**
 * Move cursor right
 */
static void cursor_right(void) {
	if (column < the_line->actual) column++;
	place_cursor_actual();
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
	place_cursor_actual();
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
		if (the_line->text[column].codepoint == ' ') break;
	}
	place_cursor_actual();
}

/**
 * Move cursor to start of line
 */
static void cursor_home(void) {
	column = 0;
	place_cursor_actual();
}

/*
 * Move cursor to end of line
 */
static void cursor_end(void) {
	column = the_line->actual;
	place_cursor_actual();
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
	place_cursor_actual();
}

/**
 * Cycle to next history entry
 */
static void history_next(void) {
	if (rline_scroll > 1) {
		rline_scroll--;

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
	} else if (rline_scroll == 1) {
		/* Copy in from temp */
		rline_scroll = 0;

		the_line->actual = 0;
		column = 0;
		loading = 1;
		char * buf = temp_buffer;
		uint32_t istate = 0, c = 0;
		for (unsigned int i = 0; i < strlen(buf); ++i) {
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
	place_cursor_actual();
}

/**
 * Handle escape sequences (arrow keys, etc.)
 */
static int handle_escape(int * this_buf, int * timeout, int c) {
	if (*timeout >=  1 && this_buf[*timeout-1] == '\033' && c == '\033') {
		this_buf[*timeout] = c;
		(*timeout)++;
		return 1;
	}
	if (*timeout >= 1 && this_buf[*timeout-1] == '\033' && c != '[') {
		*timeout = 0;
		_ungetc(c);
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
	place_cursor_actual();
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
	uint32_t c;
	int timeout = 0;
	int this_buf[20];
	uint32_t istate = 0;
	int immediate = 1;

	set_colors(COLOR_ALT_FG, COLOR_ALT_BG);
	fprintf(stdout, "◄\033[0m"); /* TODO: This could be retrieved from an envvar */
	for (int i = 0; i < full_width - 1; ++i) {
		fprintf(stdout, " ");
	}
	render_line();
	place_cursor_actual();

	while ((cin = getch(immediate))) {
		if (cin == -1) {
			immediate = 1;
			render_line();
			place_cursor_actual();
			continue;
		}
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
					immediate = 0;
					raise(SIGINT);
					return 1;
				}
				if (_EOF && c == _EOF) {
					if (column == 0 && the_line->actual == 0) {
						for (char *_c = rline_exit_string; *_c; ++_c) {
							insert_char(*_c);
						}
						render_line();
						place_cursor_actual();
						return 1;
					} else { /* Otherwise act like delete */
						if (column < the_line->actual) {
							line_delete(the_line, column+1);
							if (offset > 0) offset--;
							immediate = 0;
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
						delete_at_cursor();
						immediate = 0;
						break;
					case ENTER_KEY:
						/* Finished */
						loading = 1;
						column = the_line->actual;
						render_line();
						insert_char('\n');
						immediate = 0;
						return 1;
					case 22: /* ^V */
						/* Don't bother with unicode, just take the next byte */
						place_cursor_actual();
						printf("^\b");
						insert_char(getc(stdin));
						immediate = 0;
						break;
					case 23: /* ^W */
						delete_word();
						immediate = 0;
						break;
					case 12: /* ^L - Repaint the whole screen */
						printf("\033[2J\033[H");
						render_line();
						place_cursor_actual();
						break;
					case 11: /* ^K - Clear to end */
						the_line->actual = column;
						immediate = 0;
						break;
					case 21: /* ^U - Kill to beginning */
						while (column) {
							delete_at_cursor();
						}
						immediate = 0;
						break;
					case '\t':
						if (tab_complete_func) {
							/* Tab complete */
							rline_context_t context = {0};
							call_rline_func(tab_complete_func, &context);
							immediate = 0;
						} else {
							/* Insert tab character */
							insert_char('\t');
							immediate = 0;
						}
						break;
					case 18:
						{
							rline_context_t context = {0};
							call_rline_func(rline_reverse_search, &context);
							if (!context.cancel) {
								return 1;
							}
							immediate = 0;
						}
						break;
					default:
						insert_char(c);
						immediate = 0;
						break;
				}
			} else {
				if (handle_escape(this_buf,&timeout,c)) {
					continue;
				}
				immediate = 0;
			}
		} else if (istate == UTF8_REJECT) {
			istate = 0;
		}
	}
	return 0;
}

/**
 * Read a line of text with interactive editing.
 */
int rline_experimental(char * buffer, int buf_size) {
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

void * rline_exp_for_python(void * _stdin, void * _stdout, char * prompt) {

	rline_exp_set_prompts(prompt, "", strlen(prompt), 0);

	char * buf = malloc(1024);
	memset(buf, 0, 1024);

	rline_exp_set_syntax("python");
	rline_exit_string = "";
	rline_experimental(buf, 1024);
	rline_history_insert(strdup(buf));
	rline_scroll = 0;

	return buf;
}
