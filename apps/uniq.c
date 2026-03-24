/**
 * @brief Filter repeated lines from an input file
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>

static int skip_fields = 0;
static int skip_chars  = 0;
static int suppress_unique = 0;
static int suppress_nonunique = 0;
static int show_count = 0;

static int usage(char * argv[]) {
	fprintf(stderr,
		"usage: %s [-c] [-d] [-f fields] [-s chars] [-u]  [input_file [output_file]]\n"
		"\n"
		"Reads lines from all of the specified files, sorts them all, and prints the result.\n",
		argv[0]);
	return 2;
}

static char * skip_as_needed(char * str) {
	char * out = str;

	for (int i = 0; i < skip_fields && *out; ++i) {
		while (*out && isspace(*out)) out++;
		while (*out && !isspace(*out)) out++;
	}

	for (int i = 0; i < skip_chars && *out; ++i) out++;

	return out;
}

static void finish_line(FILE * output, char * prev, size_t count) {
	if (count == 1 && suppress_unique) return;
	if (count > 1 && suppress_nonunique) return;

	if (show_count) fprintf(output, "%zu ", count);
	fprintf(output, "%s\n", prev);

	free(prev);
}

static char * getline_helper(FILE * input_file) {
	char * line = NULL;
	size_t avail = 0;
	ssize_t len = getline(&line, &avail, input_file);
	if (len < 0) {
		if (line) free(line);
		return NULL;
	}
	if (len && line[len-1] == '\n') line[len-1] = '\0';
	return line;
}

int main(int argc, char * argv[]) {
	FILE * input_file = stdin;
	FILE * output_file = stdout;

	int opt;

	/* XXX: POSIX says we should also accept + as an alternative for - but we don't do that. */
	while ((opt = getopt(argc, argv, "?cdf:s:u")) != -1) {
		switch (opt) {
			case 'c':
				show_count = 1;
				break;
			case 'd':
				suppress_unique = 1;
				break;
			case 'u':
				suppress_nonunique = 1;
				break;
			case 'f':
				skip_fields = atoi(optarg);
				break;
			case 's':
				skip_chars = atoi(optarg);
				break;
			case '?':
				return usage(argv);
		}
	}

	if (optind + 2 < argc) return usage(argv);

	if (optind < argc) {
		if (strcmp(argv[optind],"-")) {
			input_file = fopen(argv[optind], "r");
			if (!input_file) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
				return 1;
			}
		}
		if (optind + 1 < argc) {
			output_file = fopen(argv[optind+1],"w");
			if (!output_file) {
				fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind+1], strerror(errno));
				return 1;
			}
		}
	}

	size_t count = 1;
	char *prev = getline_helper(input_file);

	if (!prev) return 0; /* Empty file? */

	char *prev_comp = skip_as_needed(prev);

	while (1) {
		char * nline = getline_helper(input_file);
		if (!nline) break;

		char * ncomp = skip_as_needed(nline);

		if (!strcmp(prev_comp, ncomp)) {
			free(nline);
			count++;
		} else {
			finish_line(output_file, prev, count);
			count = 1;
			prev = nline;
			prev_comp = ncomp;
		}
	}

	finish_line(output_file, prev, count);

	if (input_file != stdin) fclose(input_file);
	if (output_file != stdout) fclose(output_file);

	return 0;
}

