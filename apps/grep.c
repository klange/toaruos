/**
 * @brief grep - mediocre grep
 *
 * Based on the regex search matcher in bim.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>

#define LINE_SIZE 4096

static int invert = 0;
static int ignorecase = 0;
static int quiet = 0;
static int only_matching = 0;
static int counts = 0;

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

/**
 * Helper for handling smart case sensitivity.
 */
int match_char(struct MatchQualifier * self, char b, int mode) {
	if (mode == 0) {
		return self->matchChar == b;
	} else if (mode == 1) {
		return tolower(self->matchChar) == tolower(b);
	}
	return 0;
}

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
			if (mode ? (tolower(c) >= tolower(test) && tolower(c) <= tolower(right)) : (c >= test && c <= right)) return good;
		} else {
			if (mode ? (tolower(c) == tolower(test)) : (c == test)) return good;
		}
	}
	return !good;
}

int match_dot(struct MatchQualifier * self, char c, int mode) {
	return 1;
}

struct BackRef {
	int start;
	int len;
	uint32_t * copy;
};

struct Line {
	int actual;
	char * text;
};

#define MAX_REFS 10
int regex_matches(struct Line * line, int j, char * needle, int ignorecase, int *len, char **needleout, int refindex, struct BackRef * refs) {
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
		if (k == line->actual) break;

		struct MatchQualifier matcher = {match_char, .matchChar=*match};
		if (*match == '.') {
			matcher.matchFunc = match_dot;
			match++;
		} else if (*match == '\\' && strchr("$^/\\.[?]*+()",match[1]) != NULL) {
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
			if (!regex_matches(line, k, match, ignorecase, &_len, &newmatch, 0, NULL)) break;
			match = newmatch;
			if (refindex && refindex < MAX_REFS) {
				refs[refindex].start = k;
				refs[refindex].len = _len;
				refindex++;
			}
			k += _len;
			continue;
		} else {
			match++;
		}
		if (*match == '?') {
			/* Optional */
			match++;
			if (matcher.matchFunc(&matcher, line->text[k], ignorecase)) {
				int _len;
				if (regex_matches(line,k+1,match,ignorecase,&_len, needleout, refindex, refs)) {
					if (len) *len = _len + k + 1 - j;
					return 1;
				}
			}
			continue;
		} else if (*match == '+' || *match == '*') {
			/* Must match at least one */
			if (*match == '+') {
				if (!matcher.matchFunc(&matcher, line->text[k], ignorecase)) break;
				k++;
			}
			/* Match any */
			match++;
			int greedy = 1;
			if (*match == '?') {
				/* non-greedy */
				match++;
				greedy = 0;
			}

			int _j = k;
			while (_j < line->actual + 1) {
				int _len;
				if (!greedy && regex_matches(line, _j, match, ignorecase, &_len, needleout, refindex, refs)) {
					if (len) *len = _len + _j - j;
					return 1;
				}
				if (_j < line->actual && !matcher.matchFunc(&matcher, line->text[_j], ignorecase)) break;
				_j++;
			}
			if (!greedy) return 0;
			while (_j >= k) {
				int _len;
				if (regex_matches(line, _j, match, ignorecase, &_len, needleout, refindex, refs)) {
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

int subsearch_matches(struct Line * line, int j, char * needle, int *len) {
	return regex_matches(line, j, needle, ignorecase, len, NULL, 0, NULL);
}

int usage(char ** argv) {
#define _I "\033[3m"
#define _E "\033[0m\n"
	fprintf(stderr, "usage: %s [-ivqoc] PATTERN [FILE...]\n"
		"\n"
		" Supported options:\n"
		"  -c     " _I "Instead of printing matches, print counts of matched lines." _E
		"  -i     " _I "Ignore case in input and pattern." _E
		"  -o     " _I "Print only the matching parts of each line, separating\n"
		"         each match with a line feed." _E
		"  -q     " _I "Exit immediately with 0 when a match (or, with -v,\n"
		"         non-match) is found, do not print matches." _E
		"  -v     " _I "Invert match - print lines that do not match pattern." _E
		"\n"
		" Supported regex syntax:\n"
		"  [abc]  " _I "Match one of a set of characters." _E
		"  [a-z]  " _I "Match one from a range of characters." _E
		"  (abc)  " _I "Match a group; does nothing here, supported for compatibility\n"
		"         with bim and a possible future sed implementation." _E
		"  .      " _I "Match any single character." _E
		"  ^      " _I "Match the start of the line." _E
		"  $      " _I "Match the end of the line." _E
		"\n"
		" Modifiers (can be combined with [], ., and single characters):\n"
		"  ?      " _I "Match optionally" _E
		"  *      " _I "Match any number of occurances" _E
		"  +      " _I "Match at least one occurance" _E
		"  *? +?  " _I "Non-greedy match variants of * and +" _E
		"\n"
		" Some characters can be escaped in the pattern with \\.\n"
		" The regex engine is not Unicode-aware.\n",
		argv[0]);
#undef _I
#undef _E
	return 1;
}

#define LINE_SIZE 4096

int main(int argc, char ** argv) {
	int opt;
	while ((opt = getopt(argc, argv, "?hivqoc")) != -1) {
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
		}
	}

	if (optind == argc) return usage(argv);

	char * needle = argv[optind];
	char buf[LINE_SIZE];
	int ret = 1;
	int is_tty = isatty(STDOUT_FILENO);

	optind++;


	int showFilenames = (optind + 1 < argc);

	do {
		FILE * input = stdin;
		int count = 0;
		if (optind < argc && strcmp(argv[optind],"-")) {
			input = fopen(argv[optind], "r");
			if (!input) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
				return 1;
			}
		}

		const char * filename = input == stdin ? "(standard input)" : argv[optind];

		while (fgets(buf, LINE_SIZE, input)) {
			int lineLength = strlen(buf);
			if (lineLength && buf[lineLength-1] == '\n') {
				lineLength--;
			}
			struct Line line = {
				lineLength,
				buf
			};

			if (!invert) {
				int lastMatch = 0;
				for (int j = 0; j < lineLength;) {
					int len;
					if (subsearch_matches(&line, j, needle, &len)) {
						ret = 0;
						if (counts) {
							count++;
							break;
						}
						if (quiet) goto _done;
						if (only_matching) {
							if (showFilenames) fprintf(stdout, "%s:", filename);
							fprintf(stdout, "%.*s\n", len, buf + j);
						} else {
							if (lastMatch == 0 && showFilenames) fprintf(stdout, "%s:", filename);
							fprintf(stdout, "%.*s%s%.*s%s",
								j - lastMatch,
								buf + lastMatch,
								is_tty ? "\033[1;31m" : "",
								len,
								buf + j,
								is_tty ? "\033[0m" : "");
						}
						lastMatch = j + len;
						j = lastMatch;
					} else {
						j++;
					}
				}
				if (counts) continue;
				if (!only_matching && lastMatch) {
					fprintf(stdout, "%s", buf + lastMatch);
				}
			} else {
				int matched = 0;
				for (int j = 0; j < lineLength; ++j) {
					if (subsearch_matches(&line, j, needle, NULL)) {
						matched = 1;
						break;
					}
				}
				if (matched) continue;
				ret = 0;
				if (counts) {
					count++;
					continue;
				}
				if (quiet) goto _done;
				if (showFilenames) fprintf(stdout, "%s:", filename);
				fprintf(stdout, "%s", buf);
			}
		}

_done: (void)0;
		if (input != stdin) fclose(input);

		if (counts) {
			if (showFilenames) fprintf(stdout, "%s:", filename);
			fprintf(stdout, "%d\n", count);
		}

		optind++;
	} while (optind < argc);

	return ret;
}

