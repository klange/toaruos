/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * echo - Print arguments to stdout.
 *
 * Prints arguments to stdout, possibly interpreting escape
 * sequences in the arguments.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void show_usage(char * argv[]) {
	printf(
			"echo - print arguments\n"
			"\n"
			"usage: %s [-ne] ARG...\n"
			"\n"
			" -n     \033[3mdo not output a new line at the end\033[0m\n"
			" -e     \033[3mprocess escape sequences\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int main(int argc, char ** argv) {
	int use_newline     = 1;
	int process_escapes = 0;

	int opt;
	while ((opt = getopt(argc, argv, "enh?")) != -1) {
		switch (opt) {
			case '?':
			case 'h':
				show_usage(argv);
				return 1;
			case 'n':
				use_newline = 0;
				break;
			case 'e':
				process_escapes = 1;
				break;
		}
	}

	for (int i = optind; i < argc; ++i) {
		if (process_escapes) {
			char * c = argv[i];
			while (*c) {
				if (*c == '\\') {
					c++;
					switch (*c) {
						case '\\':
							putchar('\\');
							break;
						case 'a':
							putchar('\a');
							break;
						case 'b':
							putchar('\b');
							break;
						case 'c':
							return 0;
						case 'e':
							putchar('\033');
							break;
						case 'f':
							putchar('\f');
							break;
						case 'n':
							putchar('\n');
							break;
						case 't':
							putchar('\t');
							break;
						case 'v':
							putchar('\v');
							break;
						case '0':
							{
								int i = 0;
								if (!isdigit(*(c+1)) || *(c+1) > '7') {
									break;
								}
								c++;
								i = *c - '0';
								if (isdigit(*(c+1)) && *(c+1) <= '7') {
									c++;
									i = (i << 3) | (*c - '0');
									if (isdigit(*(c+1)) && *(c+1) <= '7') {
										c++;
										i = (i << 3) | (*c - '0');
									}
								}
								putchar(i);
							}
							break;
						default:
							putchar('\\');
							putchar(*c);
							break;
					}
				} else {
					putchar(*c);
				}
				c++;
			}
		} else {
			printf("%s",argv[i]);
		}
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
