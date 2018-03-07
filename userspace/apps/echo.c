/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * echo
 *
 * Prints its arguments (with some processing, ask --help)
 * to standard out.
 */
#include <stdio.h>
#include <string.h>

void usage() {
	printf("echo [-n] [-e] [STRING]...\n"
	       "  -n    do not output a new line at the end\n"
	       "  -e    process escape sequences\n");
}

int main(int argc, char ** argv) {
	int start           = 1;
	int use_newline     = 1;
	int process_escapes = 0;

	for (int i = start; i < argc; ++i) {
		if (argv[i][0] != '-') {
			start = i;
			break;
		} else {
			if (argv[i][1] == 'h') {
				usage();
				return 1;
			} else if (argv[i][1] == 'n') {
				use_newline = 0;
			} else if (argv[i][1] == 'e') {
				process_escapes = 1;
			}
		}
	}

	for (int i = start; i < argc; ++i) {
		if (process_escapes) {
			for (int j = 0; j < strlen(argv[i]) - 1; ++j) {
				if (argv[i][j] == '\\') {
					if (argv[i][j+1] == 'e') {
						argv[i][j] = '\033';
						for (int k = j + 1; k < strlen(argv[i]); ++k) {
							argv[i][k] = argv[i][k+1];
						}
					}
					if (argv[i][j+1] == 'n') {
						argv[i][j] = '\n';
						for (int k = j + 1; k < strlen(argv[i]); ++k) {
							argv[i][k] = argv[i][k+1];
						}
					}
				}
			}
		}
		printf("%s",argv[i]);
		if (i != argc - 1) {
			printf(" ");
		}
	}

	if (use_newline) {
		printf("\n");
	}

	fflush(stdout);
	return 0;
}
