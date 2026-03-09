/**
 * @brief grep - almost acceptable grep
 *
 * Based on the regex search matcher in bim.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022-2026 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>

static int invert = 0;
static int ignorecase = 0;
static int quiet = 0;
static int only_matching = 0;
static int counts = 0;
static int is_fgrep = 0;
static int whole_lines = 0;
static int list_files = 0;
static int line_numbers = 0;
static int use_color = 0;
static int suppress_errors = 0;

/**
 * This regex engine is adapted from bim.
 *
 * It has been modified to be more like POSIX BREs, but it's still pretty bad.
 *
 * While we can support * matches for square brackets, we don't support them
 * for groups. We don't support character classes (colons), repetition ranges,
 * and definitely not backrefs.
 */
struct MatchQualifier {
	int (*matchFunc)(struct MatchQualifier*,char,int);
	union {
		char matchChar;
		struct {
			char * start;
			char * end;
		} matchSquares;
	};
};

typedef enum {
	MATCH_OP_EQ,
	MATCH_OP_GE,
	MATCH_OP_LE,
} match_op;

/**
 * Helper for handling smart case sensitivity.
 */
static inline int match_char_internal(char a, char b, int ignorecase, match_op op) {
	if (ignorecase) {
		a = tolower(a);
		b = tolower(b);
	}
	switch (op) {
		/* Equality */
		case MATCH_OP_EQ: return a == b;
		case MATCH_OP_GE: return a >= b;
		case MATCH_OP_LE: return a <= b;
	}
	return 0;
}

/*
 * Basic single-character matcher.
 */
int match_char(struct MatchQualifier * self, char b, int mode) {
	return match_char_internal(self->matchChar, b, mode, MATCH_OP_EQ);
}

/*
 * Match collections of characters.
 */
int match_squares(struct MatchQualifier * self, char c, int mode) {
	char * start = self->matchSquares.start;
	char * end = self->matchSquares.end;
	char * t = start;
	int good = 1;
	if (*t == '^') { t++; good = 0; }
	while (t != end) {
		char test = *t++;
		if (test == '\\' && *t && strchr("\\]",*t)) {
			test = *t++;
		} else if (test == '\\' && *t == 't') {
			test = '\t'; t++;
		}

		if (*t == '-') {
			t++;
			if (t == end) return 0;
			char right = *t++;
			if (right == '\\' && *t && strchr("\\]",*t)) {
				right = *t++;
			} else if (right == '\\' && *t == 't') {
				right = '\t'; t++;
			}
			if (match_char_internal(c,test,mode,MATCH_OP_GE) && match_char_internal(c,right,mode,MATCH_OP_LE)) return good;
		} else {
			if (match_char_internal(c,test,mode,MATCH_OP_EQ)) return good;
		}
	}
	return !good;
}

/*
 * Match any single character.
 */
int match_dot(struct MatchQualifier * self, char c, int mode) {
	return 1;
}

struct Line {
	int actual;
	char * text;
};

int regex_matches(struct Line * line, int j, char * needle, int ignorecase, int *len, char **needleout) {
	int k = j;
	char * match = needle;
	if (*match == '^') {
		if (j != 0) return 0;
		match++;
	}
	while (k < line->actual + 1) {
		if (needleout && *match == ')') {
			*needleout = match + 1;
			if (len) *len = k - j;
			return 1;
		}
		if (*match == '\0') {
			if (needleout) return 0;
			if (len) *len = k - j;
			return 1;
		}
		if (*match == '$') {
			if (k != line->actual) return 0;
			match++;
			continue;
		}

		struct MatchQualifier matcher = {match_char, .matchChar=*match};
		if (*match == '.') {
			matcher.matchFunc = match_dot;
			match++;
		} else if (*match == '\\' && strchr("$^/\\.[]*()",match[1]) != NULL) {
			matcher.matchChar = match[1];
			match += 2;
		} else if (*match == '\\' && match[1] == 't') {
			matcher.matchChar = '\t';
			match += 2;
		} else if (*match == '[') {
			char * s = match+1;
			char * e = s;
			while (*e && *e != ']') {
				if (*e == '\\' && e[1] == ']') e++;
				e++;
			}
			if (!*e) break; /* fail match on unterminated [] sequence */
			match = e + 1;
			matcher.matchFunc = match_squares;
			matcher.matchSquares.start = s;
			matcher.matchSquares.end = e;
		} else if (*match == '(') {
			match++;
			int _len;
			char * newmatch;
			if (!regex_matches(line, k, match, ignorecase, &_len, &newmatch)) break;
			match = newmatch;
			k += _len;
			continue;
		} else {
			match++;
		}
		if (*match == '\\' && match[1] == '?') {
			/* Optional */
			match += 2;
			if (matcher.matchFunc(&matcher, line->text[k], ignorecase)) {
				int _len;
				if (regex_matches(line,k+1,match,ignorecase,&_len, needleout)) {
					if (len) *len = _len + k + 1 - j;
					return 1;
				}
			}
			continue;
		} else if ((*match == '\\' && match[1] == '+') || *match == '*') {
			if (*match == '\\' /* && match[1] == '+' */) {
				/* Must match at least one */
				match += 2;
				if (!matcher.matchFunc(&matcher, line->text[k], ignorecase)) break;
				k++;
			} else {
				match++;
			}

			int _j = k;
			while (_j < line->actual + 1) {
				if (_j < line->actual && !matcher.matchFunc(&matcher, line->text[_j], ignorecase)) break;
				_j++;
			}
			while (_j >= k) {
				int _len;
				if (regex_matches(line, _j, match, ignorecase, &_len, needleout)) {
					if (len) *len = _len + _j - j;
					return 1;
				}
				_j--;
			}
			return 0;
		} else {
			if (!matcher.matchFunc(&matcher, line->text[k], ignorecase)) break;
			k++;
		}
	}
	return 0;
}

/**
 * Determine if 'line' matches a particular pattern 'needle' using the current
 * global mode settings (is_fgrep, ignorecase), starting from a given line, and
 * store the resulting length of a match in 'len'.
 */
static int subsearch_matches(struct Line * line, int j, char * needle, int *len) {
	if (is_fgrep) {
		/* Does 'line' starting at 'j' match 'needle' */
		const char *n = needle;
		for (; *n; ++n, ++j) if (j >= line->actual || !match_char_internal(*n, line->text[j], ignorecase, MATCH_OP_EQ)) return 0;
		*len = n - needle;
		return 1;
	}
	return regex_matches(line, j, needle, ignorecase, len, NULL);
}

int usage(char ** argv) {
#define _I "\033[3m"
#define _E "\033[0m\n"
	fprintf(stderr, "usage: %s [-cilnoqsvxF] PATTERN [FILE...]\n"
		"\n"
		"Search for PATTERN in each file.\n"
		"Take care that this grep's pattern engine is limited and not POSIX-compliant.\n"
		"\n"
		" Supported options:\n"
		"  -c      " _I "Instead of printing matches, print counts of matched lines." _E
		"  -i      " _I "Ignore case in input and pattern." _E
		"  -l      " _I "Instead of printing matches, print the names of files that.\n"
		"          match. Takes precedence over most other options." _E
		"  -n      " _I "Prefix each printed match with the line number it appeared on." _E
		"  -o      " _I "Print only the matching parts of each line, separating\n"
		"          each match with a line feed." _E
		"  -q      " _I "Exit immediately with 0 when a match (or, with -v,\n"
		"          non-match) is found; do not print matches." _E
		"  -s      " _I "Suppress the output of errors that would normally go to stderr." _E
		"  -v      " _I "Invert match - print lines that do not match pattern." _E
		"  -x      " _I "PATTERN must match a whole line." _E
		"  -F      " _I "Treat PATTERN as a fixed string (acts as 'fgrep')." _E
		"  --help  " _I "Show this help message." _E
		"  --color " _I "Wrap matches with escapes to highlight them in red.\n"
		"          The use of color may be controlled as follows:" _E
		"  --color=auto   " _I "When the output is a TTY. Same as above." _E
		"  --color=never  " _I "Never. Overrides a previous option." _E
		"  --color-always " _I "Regardless of whether the output is a TTY." _E
		"\n"
		" Supported regex syntax:\n"
		"  [abc]   " _I "Match one of a set of characters." _E
		"  [a-z]   " _I "Match one from a range of characters." _E
		"  (abc)   " _I "Match a group.\n"
		"          This implementation does not support repeating a group match,\n"
		"          so groups do not really do anything." _E
		"  .       " _I "Match any single character." _E
		"  ^       " _I "Match the start of the line." _E
		"  $       " _I "Match the end of the line." _E
		"\n"
		" Modifiers (can be combined with [], ., and single characters):\n"
		"  *       " _I "Match any number of occurances" _E
		"  \\?      " _I "Match zero or one time" _E
		"  \\+      " _I "Match at least one occurance" _E
		"\n"
		" Some characters can be escaped in the pattern with \\.\n"
		" The regex engine is not Unicode-aware.\n",
		argv[0]);
#undef _I
#undef _E
	return 1;
}

/*
 * POSIX says "The basename() function may modify the string pointed to by path",
 * and ours definitely does in order to handle trailing slashes. We don't want that,
 * and we're probably not going to be dealing with trailing slashes because 'path'
 * here is our argv[0] which should name a binary and trailing slashes would be
 * wrong there. This "simple_basename" doesn't muck things up.
 */
static char * simple_basename(char * path) {
	char * s = path;
	char * c = path;
	do {
		while (*s == '/') {
			s++;
			if (!*s) return c; /* Ends in trailing slashes, shouldn't happen... */
		}
		c = s;
		s = strchr(c,'/');
	} while (s);
	return c;
}

int main(int argc, char ** argv) {
	int opt;
	while ((opt = getopt(argc, argv, "?hivqocFxlns-:")) != -1) {
		switch (opt) {
			case 'h':
			case '?':
				return usage(argv);
			case 'i':
				ignorecase = 1;
				break;
			case 'v':
				invert = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case 'o':
				only_matching = 1;
				break;
			case 'c':
				counts = 1;
				break;
			case 'F':
				is_fgrep = 1;
				break;
			case 'x':
				whole_lines = 1;
				break;
			case 'l':
				list_files = 1;
				break;
			case 'n':
				line_numbers = 1;
				break;
			case 's':
				suppress_errors = 1;
				break;
			case '-':
				if (!strcmp(optarg,"help")) {
					return usage(argv);
				} else if (!strcmp(optarg,"color=never")) {
					use_color = 0; /* Overrides previous instances of conflicting option */
				} else if (!strcmp(optarg,"color") || !strcmp(optarg,"-color=auto")) {
					use_color = 1; /* treat 1 as maybe */
				} else if (!strcmp(optarg,"color=always")) {
					use_color = 2;
				}
				break;
		}
	}

	/* Detect if we were invoke as 'fgrep'. */
	if (!strcmp(simple_basename(argv[0]),"fgrep")) {
		is_fgrep = 1;
	}

	/* Require at least a PATTERN argument. */
	if (optind == argc) return usage(argv);
	char * needle = argv[optind++];

	int ret = 1; /* Normal exit status: 0 if something matched, 1 if not. */
	int err = 0; /* Whether an error was encountered that should override the exit status to 2. */

	/* We show additional messages for detected binaries only if we are on a TTY,
	 * and "auto" color mode should only activate if we are on a TTY. */
	int is_tty = isatty(STDOUT_FILENO);
	if (!is_tty && use_color == 1) use_color = 0;

	/* When multiple file arguments are provided, include the names of files as a prefix to matches. */
	int showFilenames = (optind + 1 < argc);

	/* This buffer is allocated by 'getline' and reused between files. */
	char * buf = NULL;
	size_t avail = 0;

	do {
		FILE * input = stdin;
		ssize_t lineLength = 0;
		int count = 0;    /* Count of matching lines. */
		int isbinary = 0; /* If we have detected any NUL characters in the input. */
		int ln = 0;       /* Current line number. */

		/* If there are file arguments, use them, but treat - as standard input. */
		if (optind < argc && strcmp(argv[optind],"-")) {
			input = fopen(argv[optind], "r");
			if (!input) {
				if (!suppress_errors) fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
				/* We continue to read other arguments but note this error to exit with 2 later. */
				err = 1;
				optind++;
				continue;
			}
		}

		/* POSIX says we should print '(standard input)' instead of -, so make it happy. */
		const char * filename = input == stdin ? "(standard input)" : argv[optind];

		while ((lineLength = getline(&buf, &avail, input)) >= 0) {
			ln++;

			/* Ignore any trailing line feed. */
			if (lineLength && buf[lineLength-1] == '\n') {
				lineLength--;
			}

			/* Prepare Line for regex engine. */
			struct Line line = {
				lineLength,
				buf
			};

			/* Scan for any NUL characters in this line to see if it is a binary. */
			if (!isbinary) {
				for (ssize_t i = 0; i < lineLength; ++i) {
					if (buf[i] == '\0') {
						isbinary = 1;
						break;
					}
				}
			}

			if (!invert) {
				int lastMatch = 0; /* End of the last match. */
				int matched = 0;   /* Whether this line has matched yet. Useful when a degenerate match means lastMatch is still 0. */
				for (int j = 0; j <= lineLength;) {
					int len;
					if (subsearch_matches(&line, j, needle, &len)) {
						if (whole_lines && len != lineLength) break;

						/* If quiet gets a match at all, ony any file, we immediately exit with 0,
						 * even ignoring any errors we may have printed about previously. */
						if (quiet) return 0;

						/* Increment count of matching lines only if this is the fist match */
						if (!matched) count++;

						/* If anything matched, return code is 0 except for an error. */
						ret = 0;
						matched = 1;

						/* If we are just listing matching files, we're done on the first match. */
						if (list_files) goto _done;

						/* If we're counting matching lines, we're done with this line. */
						if (counts) break;

						/* If this is a binary file, and we are not counting matched lines,
						 * we're done here, similar to listing. */
						if (isbinary) goto _done;

						if (!len && only_matching) {
							/* Don't try to print a degenerate match here. */
							lastMatch = j = j + 1;
							continue;
						}

						if (only_matching || !lastMatch) {
							/* Prefix this match result as needed. For -o, every match
							 * is prefixed. Otherwise, we only print the prefix before
							 * the first match. */
							if (showFilenames) fprintf(stdout, "%s:", filename);
							if (line_numbers) fprintf(stdout, "%d:", ln);
						}

						if (only_matching) {
							fprintf(stdout, "%.*s\n", len, buf + j);
						} else {
							/* Print from the end of the previous match up to the end of
							 * the current match, with color when enabled. */
							fprintf(stdout, "%.*s%s%.*s%s",
								j - lastMatch,
								buf + lastMatch,
								use_color ? "\033[1;31m" : "",
								len,
								buf + j,
								use_color ? "\033[0m" : "");
						}

						/* Update lastMatch to point to the end of this match. */
						lastMatch = j + len;

						/* Advance to that point, ensuring we skip to the next character
						 * if this match was degenerate. */
						j = lastMatch + !len;
					} else {
						j++;
					}

					/* When matching whole lines, we only check from index 0, so break now. */
					if (whole_lines) break;
				}

				/* If we are counting matches, don't print anything and go to next line. */
				if (counts) continue;

				/* If we weren't printing just the matching text and we had a match, there
				 * may be more of the line left to print, and we must also print a line feed
				 * ourselves (we ignore any line feed in the actual line, and we print one
				 * even if the line didn't actually end in one). */
				if (!only_matching && matched) fprintf(stdout, "%.*s\n", (int)(lineLength - lastMatch), buf + lastMatch);
			} else {

				/* The inverse matching case is a lot simpler as we don't have to deal with
				 * coloring submatches. Try the pattern at every point in the line, and if
				 * it ever matches, reject it. When un-matching whole lines, check only
				 * from index 0 and only reject if the match length is the same as the line. */
				int matched = 0;
				for (int j = 0; j <= lineLength; ++j) {
					int len;
					if (subsearch_matches(&line, j, needle, &len)) {
						if (whole_lines && len != lineLength) break;
						matched = 1;
						break;
					}
					if (whole_lines) break;
				}

				/* Do nothing on a matched line. */
				if (matched) continue;

				/* In quiet mode, if anything un-matched, immediately exit with 0,
				 * ignoring all the other arguments. */
				if (quiet) return 0;

				/* Otherwise any un-matched line sets our return status to 0 like any
				 * matched line does in the non-inverse case. */
				ret = 0;

				/* Count un-matched lines. */
				count++;

				/* If just listing file names, we're done with this file. */
				if (list_files) goto _done;

				/* If we're counting un-matching lines, we're done with this one. */
				if (counts) continue;

				/* And again, same as above, if this was a binary and we weren't
				 * counting un-matched 'lines', we're now done with this file. */
				if (isbinary) goto _done;

				/* -ov is a weird combo, but don't print anything. */
				if (only_matching) continue;

				/* Print relevant prefixes. */
				if (showFilenames) fprintf(stdout, "%s:", filename);
				if (line_numbers) fprintf(stdout, "%d:", ln);

				/* And finally, print the un-matched line with a line feed. */
				fprintf(stdout, "%.*s\n", (int)lineLength, buf);
			}
		}

		if (lineLength < 0 && ferror(input)) {
			if (!suppress_errors) fprintf(stderr, "%s: %s: %s\n", argv[0], filename, strerror(errno));
			err = 1;
			optind++;
			continue;
		}

_done: (void)0;
		if (input != stdin) fclose(input);

		if (!quiet) {
			if (list_files) {
				if (count) puts(filename);
			} else if (counts) {
				if (showFilenames) fprintf(stdout, "%s:", filename);
				fprintf(stdout, "%d\n", count);
			} else if (count && isbinary && is_tty) {
				fprintf(stdout, "%s: %s: binary file matches\n", argv[0], filename);
			}
		}

		optind++;
	} while (optind < argc);

	return err ? 2 : ret;
}

